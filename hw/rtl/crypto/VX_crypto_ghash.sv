`include "VX_define.vh"

// GHASH processing element (GF(2^128) multiply-accumulate for AES-GCM / GMAC).
//
// Per-lane multi-chain (design C-a): each SIMT lane owns an independent GHASH
// chain. State holds {H, Y} per lane per warp, and every op is pure SIMT —
// SETH/XOR/RD act on each active lane's own operand, and MUL multiplies all
// active lanes' chains in parallel.
//
// The MUL multiplier is digit-serial with a compile-time radix (design C-b):
// GHASH_MUL_RADIX bits are processed per cycle by chaining that many bit-steps
// combinationally, so MUL takes 128/RADIX cycles. RADIX=1 is the bit-serial
// baseline (128 cycles); RADIX=128 is a fully combinational multiply (1 cycle,
// long critical path — Karatsuba would optimize its gate count/fmax). Because
// it is the same shift-XOR loop unrolled, every radix is bit-identical to the
// software gf128_mul gold model.
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
    parameter STATE_LANES = NUM_LANES,  // independent GHASH chains held (<= NUM_LANES)
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
    `STATIC_ASSERT ((STATE_LANES >= 1) && (STATE_LANES <= NUM_LANES), ("invalid STATE_LANES"))

    typedef struct packed {
        logic [STATE_LANES-1:0][127:0] H;   // per-chain hash subkey
        logic [STATE_LANES-1:0][127:0] Y;   // per-chain tag accumulator
    } ghash_state_t;

    // GF(2^128) reduction polynomial top byte: R = 0xE1 in byte 0 (MSB).
    localparam logic [127:0] GHASH_R = {8'hE1, 120'h0};

    // Digit-serial multiply radix: bits processed per MUL cycle (128/RADIX cyc).
`ifdef GHASH_MUL_RADIX
    localparam MUL_RADIX = `GHASH_MUL_RADIX;
`else
    localparam MUL_RADIX = 1;
`endif
    `STATIC_ASSERT ((128 % MUL_RADIX) == 0, ("GHASH_MUL_RADIX must divide 128"))

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
    reg [STATE_LANES-1:0][127:0] mul_x_r;       // per-chain scanned operand (Y)
    reg [STATE_LANES-1:0][127:0] mul_v_r;       // per-chain shifted operand (H)
    reg [STATE_LANES-1:0][127:0] mul_z_r;       // per-chain product accumulator
    reg [NW_WIDTH-1:0]           wid_r;
    reg [STATE_LANES-1:0]        tmask_r;       // active-chain mask for MUL writeback
    reg [META_DATAW-1:0]         meta_r;
    reg [STATE_LANES-1:0][`XLEN-1:0] pending_data_r;

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

    // RADIX bits of the carry-less multiply per lane per cycle (shared counter).
    // Chains MUL_RADIX bit-steps combinationally: NIST bit i of X = X[127-i];
    // accumulate V into Z; shift V right with reduction. Returns {Z, V}.
    function automatic [255:0] gf_radix(input [127:0] x,
                                        input [127:0] z_in,
                                        input [127:0] v_in,
                                        input [7:0]   base);
        reg [127:0] z, v, v_sh;
        reg         x_bit, v_lsb;
        integer     j;
        begin
            z = z_in;
            v = v_in;
            for (j = 0; j < MUL_RADIX; j = j + 1) begin
                x_bit = x[127 - (base + j[7:0])];
                z     = x_bit ? (z ^ v) : z;
                v_lsb = v[0];
                v_sh  = v >> 1;
                v     = v_lsb ? (v_sh ^ GHASH_R) : v_sh;
            end
            gf_radix = {z, v};
        end
    endfunction

    wire [STATE_LANES-1:0][127:0] mul_z_next;
    wire [STATE_LANES-1:0][127:0] mul_v_next;
    for (genvar l = 0; l < STATE_LANES; ++l) begin : g_mul_step
        wire [255:0] step = gf_radix(mul_x_r[l], mul_z_r[l], mul_v_r[l], bit_ctr_r);
        assign mul_z_next[l] = step[255:128];
        assign mul_v_next[l] = step[127:0];
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
                        tmask_r <= tmask[STATE_LANES-1:0];
                        pending_data_r <= '0;
                        if (do_seth) begin
                            for (l = 0; l < STATE_LANES; ++l) begin
                                if (tmask[l]) begin
                                    state_mem[sidx].H[l][execute_if.data.rs2_data[l][WSEL_BITS-1:0]*`XLEN +: `XLEN]
                                        <= execute_if.data.rs1_data[l];
                                end
                            end
                            state_r <= ST_RESP;
                        end else if (do_xor) begin
                            for (l = 0; l < STATE_LANES; ++l) begin
                                if (tmask[l]) begin
                                    state_mem[sidx].Y[l][execute_if.data.rs2_data[l][WSEL_BITS-1:0]*`XLEN +: `XLEN]
                                        <= state_mem[sidx].Y[l][execute_if.data.rs2_data[l][WSEL_BITS-1:0]*`XLEN +: `XLEN]
                                         ^ execute_if.data.rs1_data[l];
                                end
                            end
                            state_r <= ST_RESP;
                        end else if (do_read) begin
                            for (l = 0; l < STATE_LANES; ++l) begin
                                pending_data_r[l]
                                    <= state_mem[sidx].Y[l][execute_if.data.rs1_data[l][WSEL_BITS-1:0]*`XLEN +: `XLEN];
                            end
                            state_r <= ST_RESP;
                        end else if (do_mul) begin
                            for (l = 0; l < STATE_LANES; ++l) begin
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
                    for (l = 0; l < STATE_LANES; ++l) begin
                        mul_z_r[l] <= mul_z_next[l];
                        mul_v_r[l] <= mul_v_next[l];
                    end
                    if (bit_ctr_r == 8'(128 - MUL_RADIX)) begin
                        for (l = 0; l < STATE_LANES; ++l) begin
                            if (tmask_r[l]) begin
                                state_mem[ghash_state_idx(wid_r)].Y[l] <= mul_z_next[l];
                            end
                        end
                        state_r <= ST_RESP;
                    end else begin
                        bit_ctr_r <= bit_ctr_r + 8'(MUL_RADIX);
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
        if (i < STATE_LANES) begin : g_active
            assign result_if.data.data[i] = `XLEN'(pending_data_r[i]);
        end else begin : g_idle
            assign result_if.data.data[i] = '0;  // chain > STATE_LANES: masked at commit
        end
    end

    // When fewer chains than interface lanes, the high operand lanes are unused.
    if (STATE_LANES < NUM_LANES) begin : g_unused_hi
        wire _unused_hi = &{1'b0,
            execute_if.data.rs1_data[NUM_LANES-1:STATE_LANES],
            execute_if.data.rs2_data[NUM_LANES-1:STATE_LANES],
            tmask[NUM_LANES-1:STATE_LANES], 1'b0};
        `UNUSED_VAR(_unused_hi)
    end

endmodule
