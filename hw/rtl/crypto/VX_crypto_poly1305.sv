`include "VX_define.vh"

// Poly1305 one-time-MAC processing element (RFC 8439).
//
// Same per-lane Horner-MAC framework as VX_crypto_ghash.sv: per-lane {r, acc}
// state; per-block acc = (acc + block) * r mod P. GHASH uses GF(2^128); this
// uses the prime field 2^130-5. Internals follow the radix-2^26 5-limb
// "donna-32" formulation, so results are bit-identical to poly1305.h: acc and r
// are 5 x 26-bit limbs and the reduction folds the overflow limb back times 5
// (2^130 == 5 mod P).
//
// Area-minimal multiplier: ONE multiplier per lane is time-multiplexed over the
// 5x5 = 25 schoolbook partial products (one product/cycle); latency is hidden by
// SIMT warp interleaving (cf. the GHASH radix study).
//
// Ops (clamp / final-reduce / +s done in the software wrapper):
//   POLY_SETR  : r = {rs2,rs1} (clamped); acc = 0
//   POLY_BLOCK : acc = (acc + {rs2,rs1} + 2^128) * r mod P   [multi-cycle]
//   POLY_RD    : rd = acc_limb[rs1[2:0]]

module VX_crypto_poly1305 import VX_gpu_pkg::*; #(
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

    localparam PID_WIDTH = `LOG2UP(`NUM_THREADS / NUM_LANES);
    localparam META_DATAW = UUID_WIDTH + NW_WIDTH + NUM_LANES + PC_BITS + 1 + NUM_REGS_BITS + PID_WIDTH + 1 + 1;
    localparam STATE_WARPS = `NUM_WARPS / BLOCK_SIZE;
    localparam STATE_WID_BITS = `CLOG2(STATE_WARPS);
    localparam STATE_WID_WIDTH = `UP(STATE_WID_BITS);

    // Per-warp, per-lane Poly1305 state (radix-2^26, 5 limbs).
    // acc limbs are 27-bit: the donna per-block fold leaves limb1 (h1) up to one
    // bit over 2^26 (poly1305.h line 73 `h1 += c` is not re-masked).
    typedef struct packed {
        logic [NUM_LANES-1:0][4:0][25:0] r;     // clamped key limbs (26-bit)
        logic [NUM_LANES-1:0][4:0][28:0] s;     // 5*r limbs (index 1..4 used)
        logic [NUM_LANES-1:0][4:0][26:0] acc;   // accumulator limbs (27-bit)
    } poly_state_t;

    localparam ST_IDLE  = 2'd0;
    localparam ST_MUL   = 2'd1;
    localparam ST_CARRY = 2'd2;
    localparam ST_RESP  = 2'd3;

    poly_state_t state_mem [STATE_WARPS];

    reg [1:0]                       state_r;
    reg [2:0]                       i_ctr;     // partial-product i (0..4)
    reg [2:0]                       j_ctr;     // partial-product j (0..4)
    reg [NUM_LANES-1:0][4:0][27:0]  ha;        // acc+block, 28-bit limbs
    reg [NUM_LANES-1:0][4:0][63:0]  dacc;      // schoolbook accumulators
    reg [NW_WIDTH-1:0]              wid_r;
    reg [NUM_LANES-1:0]             tmask_r;
    reg [META_DATAW-1:0]            meta_r;
    reg [NUM_LANES-1:0][`XLEN-1:0]  pending_data_r;

    wire [NUM_LANES-1:0] tmask = execute_if.data.tmask;

    function automatic [STATE_WID_WIDTH-1:0] poly_state_idx(input [NW_WIDTH-1:0] wid);
        begin
            if (BLOCK_SIZE == 1)
                poly_state_idx = STATE_WID_WIDTH'(wid);
            else if (BLOCK_SIZE == `NUM_WARPS)
                poly_state_idx = '0;
            else
                poly_state_idx = STATE_WID_WIDTH'(wid_to_wis(wid));
        end
    endfunction

    wire [STATE_WID_WIDTH-1:0] sidx  = poly_state_idx(execute_if.data.wid);
    wire [STATE_WID_WIDTH-1:0] widx  = poly_state_idx(wid_r);

    wire execute_fire = (state_r == ST_IDLE) && execute_if.valid;
    wire do_setr  = (execute_if.data.op_type == INST_CRYPTO_POLY_SETR);
    wire do_block = (execute_if.data.op_type == INST_CRYPTO_POLY_BLOCK);
    wire do_read  = (execute_if.data.op_type == INST_CRYPTO_POLY_RD);

    // 128-bit operand from two XLEN registers (RV64: {hi,lo}; RV32 unsupported).
    function automatic [127:0] op128(input [`XLEN-1:0] lo, input [`XLEN-1:0] hi);
`ifdef XLEN_64
        op128 = {hi, lo};
`else
        op128 = {{(128-`XLEN){1'b0}}, lo};
`endif
    endfunction

    // Per-lane one-product-per-cycle schoolbook:
    //   dacc[j] += ha[i] * coeff,  coeff = (i<=j) ? r[j-i] : 5*r[j-i+5]
    wire       use_r    = (i_ctr <= j_ctr);
    wire [2:0] coff_idx = use_r ? (j_ctr - i_ctr) : (j_ctr - i_ctr + 3'd5);

    wire [NUM_LANES-1:0][63:0] dacc_next;
    for (genvar l = 0; l < NUM_LANES; ++l) begin : g_pp
        wire [28:0] coeff = use_r ? {3'b0, state_mem[widx].r[l][coff_idx]}
                                  : state_mem[widx].s[l][coff_idx];
        wire [56:0] prod  = {29'b0, ha[l][i_ctr]} * coeff;
        assign dacc_next[l] = dacc[l][j_ctr] + {7'b0, prod};
    end

    integer w, l;
    always_ff @(posedge clk) begin
        if (reset) begin
            state_r        <= ST_IDLE;
            i_ctr          <= '0;
            j_ctr          <= '0;
            ha             <= '0;
            dacc           <= '0;
            wid_r          <= '0;
            tmask_r        <= '0;
            meta_r         <= '0;
            pending_data_r <= '0;
            for (w = 0; w < STATE_WARPS; ++w)
                state_mem[w] <= '0;
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
                        if (do_setr) begin
                            for (l = 0; l < NUM_LANES; ++l) begin
                                if (tmask[l]) begin
                                    logic [127:0] v;
                                    logic [25:0] r0, r1, r2, r3, r4;
                                    v  = op128(execute_if.data.rs1_data[l], execute_if.data.rs2_data[l]);
                                    r0 = v[25:0];   r1 = v[51:26];  r2 = v[77:52];
                                    r3 = v[103:78]; r4 = {2'b0, v[127:104]};
                                    state_mem[sidx].r[l][0] <= r0;
                                    state_mem[sidx].r[l][1] <= r1;
                                    state_mem[sidx].r[l][2] <= r2;
                                    state_mem[sidx].r[l][3] <= r3;
                                    state_mem[sidx].r[l][4] <= r4;
                                    state_mem[sidx].s[l][0] <= '0;
                                    state_mem[sidx].s[l][1] <= {3'b0, r1} + {1'b0, r1, 2'b0}; // 5*r1
                                    state_mem[sidx].s[l][2] <= {3'b0, r2} + {1'b0, r2, 2'b0};
                                    state_mem[sidx].s[l][3] <= {3'b0, r3} + {1'b0, r3, 2'b0};
                                    state_mem[sidx].s[l][4] <= {3'b0, r4} + {1'b0, r4, 2'b0};
                                    for (int q = 0; q < 5; ++q) state_mem[sidx].acc[l][q] <= '0;
                                end
                            end
                            state_r <= ST_RESP;
                        end else if (do_block) begin
                            // ha = acc + block_limbs (+ 2^128 in limb 4)
                            for (l = 0; l < NUM_LANES; ++l) begin
                                logic [127:0] b;
                                b = op128(execute_if.data.rs1_data[l], execute_if.data.rs2_data[l]);
                                ha[l][0] <= {1'b0, state_mem[sidx].acc[l][0]} + {1'b0, b[25:0]};
                                ha[l][1] <= {1'b0, state_mem[sidx].acc[l][1]} + {1'b0, b[51:26]};
                                ha[l][2] <= {1'b0, state_mem[sidx].acc[l][2]} + {1'b0, b[77:52]};
                                ha[l][3] <= {1'b0, state_mem[sidx].acc[l][3]} + {1'b0, b[103:78]};
                                ha[l][4] <= {1'b0, state_mem[sidx].acc[l][4]} + {4'b0, b[127:104]} + 28'h1000000; // +2^128 (bit 24 of limb4)
                                for (int q = 0; q < 5; ++q) dacc[l][q] <= '0;
                            end
                            i_ctr <= '0;
                            j_ctr <= '0;
                            state_r <= ST_MUL;
                        end else if (do_read) begin
                            for (l = 0; l < NUM_LANES; ++l)
                                pending_data_r[l] <= `XLEN'(state_mem[sidx].acc[l][execute_if.data.rs1_data[l][2:0]]);
                            state_r <= ST_RESP;
                        end else begin
                            state_r <= ST_RESP;
                        end
                    end
                end
                ST_MUL: begin
                    for (l = 0; l < NUM_LANES; ++l)
                        dacc[l][j_ctr] <= dacc_next[l];
                    if (i_ctr == 3'd4) begin
                        i_ctr <= '0;
                        if (j_ctr == 3'd4)
                            state_r <= ST_CARRY;
                        else
                            j_ctr <= j_ctr + 3'd1;
                    end else begin
                        i_ctr <= i_ctr + 3'd1;
                    end
                end
                ST_CARRY: begin
                    // donna-32 per-block carry + 2^130-5 fold (combinational chain)
                    for (l = 0; l < NUM_LANES; ++l) begin
                        if (tmask_r[l]) begin
                            logic [63:0] e0, e1, e2, e3, e4;
                            logic [63:0] c;
                            logic [63:0] a0;
                            logic [26:0] a1;
                            e0 = dacc[l][0]; e1 = dacc[l][1]; e2 = dacc[l][2];
                            e3 = dacc[l][3]; e4 = dacc[l][4];
                            c = e0 >> 26; e1 = e1 + c;
                            c = e1 >> 26; e2 = e2 + c;
                            c = e2 >> 26; e3 = e3 + c;
                            c = e3 >> 26; e4 = e4 + c;
                            c = e4 >> 26;
                            a0 = (e0 & 64'h3ffffff) + c * 5;
                            c = a0 >> 26;
                            a1 = 27'((e1 & 64'h3ffffff) + c);
                            state_mem[widx].acc[l][0] <= {1'b0, a0[25:0]};   // h0 masked
                            state_mem[widx].acc[l][1] <= a1;                 // h1 may be 27-bit
                            state_mem[widx].acc[l][2] <= {1'b0, e2[25:0]};
                            state_mem[widx].acc[l][3] <= {1'b0, e3[25:0]};
                            state_mem[widx].acc[l][4] <= {1'b0, e4[25:0]};
                        end
                    end
                    state_r <= ST_RESP;
                end
                ST_RESP: begin
                    if (result_if.ready)
                        state_r <= ST_IDLE;
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
