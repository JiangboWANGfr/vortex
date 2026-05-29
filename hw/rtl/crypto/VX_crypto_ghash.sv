`include "VX_define.vh"

// GHASH processing element (GF(2^128) multiply-accumulate for AES-GCM / GMAC).
//
// Structurally a twin of VX_crypto_keccak.sv: a stateful, per-warp, multi-cycle
// crypto PE. The 1600-bit Keccak state is replaced by a 256-bit {H, Y} pair and
// the 24-round permutation is replaced by a bit-serial GF(2^128) multiply.
//
// State is held as 128-bit BIG-ENDIAN integers: byte 0 of the NIST
// representation is the most-significant byte, so NIST polynomial bit i maps to
// integer bit [127-i]. With that convention the carry-less multiply below is a
// bit-for-bit transcription of gf128_mul() in tests/.../ghash_smoke/ghash_ref.h,
// which makes the software smoke a gold model for this PE.
//
// Ops (op_type):
//   GHASH_SETH : H[word] = rs1          (word index in rs2 low bits)
//   GHASH_XOR  : Y[word] ^= rs1         (word index in rs2 low bits)
//   GHASH_RD   : rd = Y[word]           (word index in rs1 low bits)
//   GHASH_MUL  : Y = Y * H mod (x^128 + x^7 + x^2 + x + 1)   [128-cycle FSM]
//
// A 128-bit value spans NWORDS = 128/XLEN registers (2 on RV64, 4 on RV32);
// the word index selects which XLEN-bit slice to write/read.

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
        logic [127:0] H;   // hash subkey
        logic [127:0] Y;   // running tag accumulator
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
    // ST_MUL  : bit-serial GF(2^128) multiply, one bit per cycle (128 cycles)
    // ST_RESP : hold result until result_if.ready
    localparam ST_IDLE = 2'd0;
    localparam ST_MUL  = 2'd1;
    localparam ST_RESP = 2'd2;

    ghash_state_t state_mem [STATE_WARPS];

    reg [1:0]            state_r;
    reg [7:0]            bit_ctr_r;     // 0..127 multiply bit counter
    reg [127:0]          mul_x_r;       // scanned operand (Y after XOR)
    reg [127:0]          mul_v_r;       // shifted operand (H)
    reg [127:0]          mul_z_r;       // product accumulator
    reg [NW_WIDTH-1:0]   wid_r;
    reg [META_DATAW-1:0] meta_r;
    reg [`XLEN-1:0]      pending_data_r;

    wire [WSEL_BITS-1:0] wr_word = execute_if.data.rs2_data[0][WSEL_BITS-1:0];
    wire [WSEL_BITS-1:0] rd_word = execute_if.data.rs1_data[0][WSEL_BITS-1:0];

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

    // One bit of the carry-less multiply for the current counter value.
    // Scans X bit (NIST bit i = X[127-i]); accumulates V into Z; shifts V right
    // with conditional reduction. Mirrors gf128_mul() exactly.
    wire        mul_x_bit  = mul_x_r[127 - bit_ctr_r];
    wire [127:0] mul_z_next = mul_x_bit ? (mul_z_r ^ mul_v_r) : mul_z_r;
    wire        mul_v_lsb  = mul_v_r[0];
    wire [127:0] mul_v_sh   = mul_v_r >> 1;
    wire [127:0] mul_v_next = mul_v_lsb ? (mul_v_sh ^ GHASH_R) : mul_v_sh;

    integer w;
    always_ff @(posedge clk) begin
        if (reset) begin
            state_r        <= ST_IDLE;
            bit_ctr_r      <= '0;
            mul_x_r        <= '0;
            mul_v_r        <= '0;
            mul_z_r        <= '0;
            wid_r          <= '0;
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
                        pending_data_r <= '0;
                        if (do_seth) begin
                            state_mem[sidx].H[wr_word*`XLEN +: `XLEN] <= execute_if.data.rs1_data[0];
                            state_r <= ST_RESP;
                        end else if (do_xor) begin
                            state_mem[sidx].Y[wr_word*`XLEN +: `XLEN]
                                <= state_mem[sidx].Y[wr_word*`XLEN +: `XLEN] ^ execute_if.data.rs1_data[0];
                            state_r <= ST_RESP;
                        end else if (do_read) begin
                            pending_data_r <= state_mem[sidx].Y[rd_word*`XLEN +: `XLEN];
                            state_r <= ST_RESP;
                        end else if (do_mul) begin
                            // Z = X * V with X = current Y, V = H.
                            mul_x_r   <= state_mem[sidx].Y;
                            mul_v_r   <= state_mem[sidx].H;
                            mul_z_r   <= '0;
                            bit_ctr_r <= '0;
                            state_r   <= ST_MUL;
                        end else begin
                            state_r <= ST_RESP;
                        end
                    end
                end
                ST_MUL: begin
                    mul_z_r <= mul_z_next;
                    mul_v_r <= mul_v_next;
                    if (bit_ctr_r == 8'd127) begin
                        state_mem[ghash_state_idx(wid_r)].Y <= mul_z_next;
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
        assign result_if.data.data[i] = `XLEN'(pending_data_r);
    end

endmodule
