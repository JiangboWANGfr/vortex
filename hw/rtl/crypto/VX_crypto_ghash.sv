`include "VX_define.vh"

// GHASH processing element (GF(2^128) multiply-accumulate for AES-GCM / GMAC).
//
// Per-lane multi-chain (design C-a): each SIMT lane owns an independent GHASH
// chain. State holds {H, Y} per lane per warp, and every op is pure SIMT —
// SETH/XOR/RD act on each active lane's own operand, and MUL multiplies all
// active lanes' chains in parallel (the bit-serial datapath is replicated per
// lane and shares one bit counter, so NUM_LANES chains finish in 128 cycles).
//
// State is held as 128-bit BIG-ENDIAN integers (byte 0 = MSB), so NIST
// polynomial bit i maps to integer bit [127-i]; the carry-less multiply is a
// bit-for-bit transcription of gf128_mul() in ghash_smoke/ghash_ref.h.
//
// Ops (op_type):
//   GHASH_SETH : H[lane][word] = rs1[lane]      (word in rs2[lane] low bits)
//   GHASH_XOR  : Y[lane][word] ^= rs1[lane]     (word in rs2[lane] low bits)
//   GHASH_RD   : rd[lane] = Y[lane][word]       (word in rs1[lane] low bits)
//   GHASH_MUL  : Y[lane] = Y[lane] * H[lane] mod P   [128-cycle FSM]

module VX_crypto_ghash import VX_gpu_pkg::*; #(
    parameter `STRING INSTANCE_ID = "",
    parameter NUM_LANES = 1,
    parameter BLOCK_SIZE = 1,
    parameter BLOCK_IDX = 0
) (
    input wire              clk,
    input wire              reset,

    VX_execute_if.slave     execute_if,
    VX_result_if.master     result_if
);
    `UNUSED_SPARAM(INSTANCE_ID)
    `UNUSED_VAR(execute_if.data.rs3_data)
    `UNUSED_PARAM(BLOCK_IDX)
    `STATIC_ASSERT (`IS_DIVISBLE(`NUM_WARPS, BLOCK_SIZE), ("invalid parameter"))

    typedef struct packed {
        logic [NUM_LANES-1:0][127:0] H;   // per-lane hash subkey
        logic [NUM_LANES-1:0][127:0] Y;   // per-lane tag accumulator
    } ghash_state_t;

    // GF(2^128) reduction polynomial top byte: R = 0xE1 in byte 0 (MSB).
    localparam logic [127:0] GHASH_R = {8'hE1, 120'h0};

    // 128-bit value occupies NWORDS XLEN-wide registers.
    localparam NWORDS    = 128 / `XLEN;
    localparam WSEL_BITS = `CLOG2(NWORDS);

    localparam PID_WIDTH = `LOG2UP(`NUM_THREADS / NUM_LANES);
    localparam META_DATAW = UUID_WIDTH + NW_WIDTH + NUM_LANES + PC_BITS + 1 + NUM_REGS_BITS + PID_WIDTH + 1 + 1;
    localparam STATE_WARPS = `NUM_WARPS / BLOCK_SIZE;
    localparam STATE_WID_BITS = `CLOG2(STATE_WARPS);
    localparam STATE_WID_WIDTH = `UP(STATE_WID_BITS);

    // ST_IDLE : accept a new GHASH op
    // ST_MUL  : bit-serial GF(2^128) multiply for all lanes (128 cycles)
    // ST_RESP : hold result until result_if.ready
    localparam ST_IDLE = 2'd0;
    localparam ST_MUL  = 2'd1;
    localparam ST_RESP = 2'd2;

    ghash_state_t state_mem [STATE_WARPS];

    reg [1:0]                    state_r;
    reg [7:0]                    bit_ctr_r;     // 0..127 multiply bit counter
    reg [NUM_LANES-1:0][127:0]   mul_x_r;       // per-lane scanned operand (Y)
    reg [NUM_LANES-1:0][127:0]   mul_v_r;       // per-lane shifted operand (H)
    reg [NUM_LANES-1:0][127:0]   mul_z_r;       // per-lane product accumulator
    reg [NW_WIDTH-1:0]           wid_r;
    reg [NUM_LANES-1:0]          tmask_r;       // active-lane mask for MUL writeback
    reg [META_DATAW-1:0]         meta_r;
    reg [NUM_LANES-1:0][`XLEN-1:0] pending_data_r;

    wire [NUM_LANES-1:0] tmask = execute_if.data.tmask;

    // map global warp id -> per-block state index (identical to Keccak PE)
    function automatic [STATE_WID_WIDTH-1:0] ghash_state_idx(input [NW_WIDTH-1:0] wid);
        begin
            if (BLOCK_SIZE == 1) begin
                ghash_state_idx = STATE_WID_WIDTH'(wid);
            end else if (BLOCK_SIZE == `NUM_WARPS) begin
                ghash_state_idx = '0;
            end else begin
                ghash_state_idx = STATE_WID_WIDTH'(wid_to_wis(wid));
            end
        end
    endfunction

    wire [STATE_WID_WIDTH-1:0] sidx = ghash_state_idx(execute_if.data.wid);

    wire execute_fire = (state_r == ST_IDLE) && execute_if.valid;
    wire do_seth = (execute_if.data.op_type == INST_CRYPTO_GHASH_SETH);
    wire do_xor  = (execute_if.data.op_type == INST_CRYPTO_GHASH_XOR);
    wire do_read = (execute_if.data.op_type == INST_CRYPTO_GHASH_RD);
    wire do_mul  = (execute_if.data.op_type == INST_CRYPTO_GHASH_MUL);

    // One bit of the carry-less multiply per lane (shared counter). NIST bit i
    // of X = X[127-i]; accumulate V into Z; shift V right with reduction.
    wire [NUM_LANES-1:0][127:0] mul_z_next;
    wire [NUM_LANES-1:0][127:0] mul_v_next;
    for (genvar l = 0; l < NUM_LANES; ++l) begin : g_mul_step
        wire        x_bit = mul_x_r[l][127 - bit_ctr_r];
        wire        v_lsb = mul_v_r[l][0];
        wire [127:0] v_sh = mul_v_r[l] >> 1;
        assign mul_z_next[l] = x_bit ? (mul_z_r[l] ^ mul_v_r[l]) : mul_z_r[l];
        assign mul_v_next[l] = v_lsb ? (v_sh ^ GHASH_R) : v_sh;
    end

    integer w;
    integer l;
    always_ff @(posedge clk) begin
        if (reset) begin
            state_r        <= ST_IDLE;
            bit_ctr_r      <= '0;
            mul_x_r        <= '0;
            mul_v_r        <= '0;
            mul_z_r        <= '0;
            wid_r          <= '0;
            tmask_r        <= '0;
            meta_r         <= '0;
            pending_data_r <= '0;
            for (w = 0; w < STATE_WARPS; ++w) begin
                state_mem[w] <= '0;
            end
        end else begin
            case (state_r)
                ST_IDLE: begin
                    if (execute_fire) begin
                        meta_r <= {execute_if.data.uuid, execute_if.data.wid, execute_if.data.tmask,
                                   execute_if.data.PC, execute_if.data.wb, execute_if.data.rd,
                                   execute_if.data.pid, execute_if.data.sop, execute_if.data.eop};
                        wid_r <= execute_if.data.wid;
                        tmask_r <= tmask;
                        pending_data_r <= '0;
                        if (do_seth) begin
                            for (l = 0; l < NUM_LANES; ++l) begin
                                if (tmask[l]) begin
                                    state_mem[sidx].H[l][execute_if.data.rs2_data[l][WSEL_BITS-1:0]*`XLEN +: `XLEN]
                                        <= execute_if.data.rs1_data[l];
                                end
                            end
                            state_r <= ST_RESP;
                        end else if (do_xor) begin
                            for (l = 0; l < NUM_LANES; ++l) begin
                                if (tmask[l]) begin
                                    state_mem[sidx].Y[l][execute_if.data.rs2_data[l][WSEL_BITS-1:0]*`XLEN +: `XLEN]
                                        <= state_mem[sidx].Y[l][execute_if.data.rs2_data[l][WSEL_BITS-1:0]*`XLEN +: `XLEN]
                                         ^ execute_if.data.rs1_data[l];
                                end
                            end
                            state_r <= ST_RESP;
                        end else if (do_read) begin
                            for (l = 0; l < NUM_LANES; ++l) begin
                                pending_data_r[l]
                                    <= state_mem[sidx].Y[l][execute_if.data.rs1_data[l][WSEL_BITS-1:0]*`XLEN +: `XLEN];
                            end
                            state_r <= ST_RESP;
                        end else if (do_mul) begin
                            for (l = 0; l < NUM_LANES; ++l) begin
                                mul_x_r[l] <= state_mem[sidx].Y[l];
                                mul_v_r[l] <= state_mem[sidx].H[l];
                                mul_z_r[l] <= '0;
                            end
                            bit_ctr_r <= '0;
                            state_r   <= ST_MUL;
                        end else begin
                            state_r <= ST_RESP;
                        end
                    end
                end
                ST_MUL: begin
                    for (l = 0; l < NUM_LANES; ++l) begin
                        mul_z_r[l] <= mul_z_next[l];
                        mul_v_r[l] <= mul_v_next[l];
                    end
                    if (bit_ctr_r == 8'd127) begin
                        for (l = 0; l < NUM_LANES; ++l) begin
                            if (tmask_r[l]) begin
                                state_mem[ghash_state_idx(wid_r)].Y[l] <= mul_z_next[l];
                            end
                        end
                        state_r <= ST_RESP;
                    end else begin
                        bit_ctr_r <= bit_ctr_r + 8'd1;
                    end
                end
                ST_RESP: begin
                    if (result_if.ready) begin
                        state_r <= ST_IDLE;
                    end
                end
                default: state_r <= ST_IDLE;
            endcase
        end
    end

    assign execute_if.ready = (state_r == ST_IDLE);
    assign result_if.valid  = (state_r == ST_RESP);
    assign {result_if.data.uuid, result_if.data.wid, result_if.data.tmask,
            result_if.data.PC, result_if.data.wb, result_if.data.rd,
            result_if.data.pid, result_if.data.sop, result_if.data.eop} = meta_r;

    for (genvar i = 0; i < NUM_LANES; ++i) begin : g_wb_data
        assign result_if.data.data[i] = `XLEN'(pending_data_r[i]);
    end

endmodule
