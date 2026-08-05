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
// FPGA Fmax: the multiply-accumulate is pipelined (operand-select register ->
// registered multiplier -> accumulate), so the only single-cycle feedback is
// the 64-bit accumulate; the donna per-block carry/fold runs one carry step
// per cycle instead of chaining six wide adds combinationally (the unpipelined
// version closed at 163 MHz on Stratix 10, cf. the ChaCha sub-QR stepping).
// Same operations in the same order, so results stay bit-identical.
//
// Ops (clamp / final-reduce / +s done in the software wrapper):
//   POLY_SETR  : r = {rs2,rs1} (clamped); acc = 0
//   POLY_BLOCK : acc = (acc + {rs2,rs1} + 2^128) * r mod P   [multi-cycle]
//   POLY_RD    : rd = acc_limb[rs1[2:0]]

module VX_crypto_poly1305 import VX_gpu_pkg::*; #(
    parameter `STRING INSTANCE_ID = "",
    parameter NUM_LANES = 1,
    parameter STATE_LANES = NUM_LANES,  // independent Poly1305 chains held (<= NUM_LANES)
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

    localparam PID_WIDTH = `LOG2UP(`NUM_THREADS / NUM_LANES);
    localparam META_DATAW = UUID_WIDTH + NW_WIDTH + NUM_LANES + PC_BITS + 1 + NUM_REGS_BITS + PID_WIDTH + 1 + 1;
    localparam STATE_WARPS = `NUM_WARPS / BLOCK_SIZE;
    localparam STATE_WID_BITS = `CLOG2(STATE_WARPS);
    localparam STATE_WID_WIDTH = `UP(STATE_WID_BITS);

    // Per-warp, per-lane Poly1305 state (radix-2^26, 5 limbs).
    // acc limbs are 27-bit: the donna per-block fold leaves limb1 (h1) up to one
    // bit over 2^26 (poly1305.h line 73 `h1 += c` is not re-masked).
    typedef struct packed {
        logic [STATE_LANES-1:0][4:0][25:0] r;     // clamped key limbs (26-bit)
        logic [STATE_LANES-1:0][4:0][28:0] s;     // 5*r limbs (index 1..4 used)
        logic [STATE_LANES-1:0][4:0][26:0] acc;   // accumulator limbs (27-bit)
`ifndef XLEN_64
        // RV32 only: two source registers hold 64 bits, so the 128-bit SETR/BLOCK
        // operand is staged here a word at a time before being consumed. Gated on
        // XLEN so the RV64 netlist is unchanged.
        logic [STATE_LANES-1:0][127:0]     opbuf;
`endif
    } poly_state_t;

    localparam ST_IDLE  = 2'd0;
    localparam ST_MUL   = 2'd1;
    localparam ST_CARRY = 2'd2;
    localparam ST_RESP  = 2'd3;

    // Parallel-multiply design-space knob (analogue of GHASH_MUL_RADIX): do
    // POLY_MUL_RADIX schoolbook products per cycle. Unset (default) = the
    // area-minimal pipelined 1-product/cycle multiply (25 cycles). 5 = one
    // column/cycle (5 multipliers, 5 cycles); 25 = every product in one cycle
    // (25 multipliers). The 5x5 product result is bit-identical regardless.
`ifdef POLY_MUL_RADIX
    localparam POLY_RADIX   = `POLY_MUL_RADIX;
    `STATIC_ASSERT ((POLY_RADIX == 5) || (POLY_RADIX == 25), ("POLY_MUL_RADIX must be 5 or 25 (omit for the default 1)"))
    localparam COLS_PER_CYC = POLY_RADIX / 5;    // 5->1 col/cyc, 25->5 cols/cyc
    localparam MUL_STEPS    = 5 / COLS_PER_CYC;  // 5->5 cyc, 25->1 cyc
`endif

    poly_state_t state_mem [STATE_WARPS];

    reg [1:0]                       state_r;
`ifndef POLY_MUL_RADIX
    reg [2:0]                       i_ctr;     // partial-product i (0..4)
    reg [2:0]                       j_ctr;     // partial-product j (0..4)
`endif
    reg [STATE_LANES-1:0][4:0][27:0]  ha;        // acc+block, 28-bit limbs
    reg [STATE_LANES-1:0][4:0][63:0]  dacc;      // schoolbook accumulators
`ifndef POLY_MUL_RADIX
    // multiply pipeline: P0 operand-select regs, P1 product reg (DSP I/O regs)
    reg [STATE_LANES-1:0][27:0]       mul_a_r;   // P0: selected ha limb
    reg [STATE_LANES-1:0][28:0]       mul_b_r;   // P0: selected r/5r coefficient
    reg [STATE_LANES-1:0][56:0]       prod_r;    // P1: registered product
    reg [2:0]                         j_p1, j_p2; // accumulate target, pipelined
    reg                               v_p1, v_p2; // pipeline stage valids
    reg                               mul_done;   // all 25 products issued
`else
    reg [2:0]                         col_step;   // parallel-multiply column step
`endif
    reg [2:0]                         carry_step; // ST_CARRY sub-step (0..5)
    reg [NW_WIDTH-1:0]                wid_r;
    reg [STATE_LANES-1:0]             tmask_r;
    reg [META_DATAW-1:0]              meta_r;
    reg [STATE_LANES-1:0][`XLEN-1:0]  pending_data_r;

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

`ifndef XLEN_64
    wire do_setrb = (execute_if.data.op_type == INST_CRYPTO_POLY_SETRB);
    localparam WSEL_BITS = `CLOG2(128 / `XLEN);   // RV32: 4 words, 2 index bits
`endif

    // 128-bit operand from two XLEN registers: RV64 packs it as {hi,lo}; RV32 has
    // only 64 bits of source register, so it reads the staged opbuf instead.
`ifdef XLEN_64
    function automatic [127:0] op128(input [`XLEN-1:0] lo, input [`XLEN-1:0] hi);
        op128 = {hi, lo};
    endfunction
`endif

`ifndef POLY_MUL_RADIX
    // Per-lane one-product-per-cycle schoolbook:
    //   dacc[j] += ha[i] * coeff,  coeff = (i<=j) ? r[j-i] : 5*r[j-i+5]
    // P0 selects the operands, P1 multiplies between registers, P2 accumulates;
    // only the accumulate is a feedback loop.
    wire       use_r    = (i_ctr <= j_ctr);
    wire [2:0] coff_idx = use_r ? (j_ctr - i_ctr) : (j_ctr - i_ctr + 3'd5);

    wire [STATE_LANES-1:0][28:0] coeff_w;
    for (genvar l = 0; l < STATE_LANES; ++l) begin : g_pp
        assign coeff_w[l] = use_r ? {3'b0, state_mem[widx].r[l][coff_idx]}
                                  : state_mem[widx].s[l][coff_idx];
    end
`endif

    integer w, l;
    always_ff @(posedge clk) begin
        if (reset) begin
            state_r        <= ST_IDLE;
`ifndef POLY_MUL_RADIX
            i_ctr          <= '0;
            j_ctr          <= '0;
`endif
            ha             <= '0;
            dacc           <= '0;
`ifndef POLY_MUL_RADIX
            mul_a_r        <= '0;
            mul_b_r        <= '0;
            prod_r         <= '0;
            j_p1           <= '0;
            j_p2           <= '0;
            v_p1           <= 0;
            v_p2           <= 0;
            mul_done       <= 0;
`else
            col_step       <= '0;
`endif
            carry_step     <= '0;
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
                        tmask_r <= tmask[STATE_LANES-1:0];
                        pending_data_r <= '0;
`ifdef XLEN_64
                        if (do_setr) begin
`else
                        if (do_setrb) begin
`endif
                            for (l = 0; l < STATE_LANES; ++l) begin
                                if (tmask[l]) begin
                                    logic [127:0] v;
                                    logic [25:0] r0, r1, r2, r3, r4;
`ifdef XLEN_64
                                    v  = op128(execute_if.data.rs1_data[l], execute_if.data.rs2_data[l]);
`else
                                    v  = state_mem[sidx].opbuf[l];
`endif
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
`ifndef XLEN_64
                        end else if (do_setr) begin
                            // RV32: stage one XLEN-wide word of the 128-bit operand
                            // (data in rs1, word index in rs2), mirroring GHASH SETH.
                            for (l = 0; l < STATE_LANES; ++l) begin
                                if (tmask[l]) begin
                                    state_mem[sidx].opbuf[l][execute_if.data.rs2_data[l][WSEL_BITS-1:0]*`XLEN +: `XLEN]
                                        <= execute_if.data.rs1_data[l];
                                end
                            end
                            state_r <= ST_RESP;
`endif
                        end else if (do_block) begin
                            // ha = acc + block_limbs (+ 2^128 in limb 4)
                            for (l = 0; l < STATE_LANES; ++l) begin
                                logic [127:0] b;
`ifdef XLEN_64
                                b = op128(execute_if.data.rs1_data[l], execute_if.data.rs2_data[l]);
`else
                                b = state_mem[sidx].opbuf[l];
`endif
                                ha[l][0] <= {1'b0, state_mem[sidx].acc[l][0]} + {1'b0, b[25:0]};
                                ha[l][1] <= {1'b0, state_mem[sidx].acc[l][1]} + {1'b0, b[51:26]};
                                ha[l][2] <= {1'b0, state_mem[sidx].acc[l][2]} + {1'b0, b[77:52]};
                                ha[l][3] <= {1'b0, state_mem[sidx].acc[l][3]} + {1'b0, b[103:78]};
                                ha[l][4] <= {1'b0, state_mem[sidx].acc[l][4]} + {4'b0, b[127:104]} + 28'h1000000; // +2^128 (bit 24 of limb4)
                                for (int q = 0; q < 5; ++q) dacc[l][q] <= '0;
                            end
`ifndef POLY_MUL_RADIX
                            i_ctr <= '0;
                            j_ctr <= '0;
                            v_p1 <= 0;
                            v_p2 <= 0;
                            mul_done <= 0;
`else
                            col_step <= '0;
`endif
                            carry_step <= '0;
                            state_r <= ST_MUL;
                        end else if (do_read) begin
                            for (l = 0; l < STATE_LANES; ++l)
                                pending_data_r[l] <= `XLEN'(state_mem[sidx].acc[l][execute_if.data.rs1_data[l][2:0]]);
                            state_r <= ST_RESP;
                        end else begin
                            state_r <= ST_RESP;
                        end
                    end
                end
                ST_MUL: begin
`ifndef POLY_MUL_RADIX
                    // P0: select operands into registers while issues remain
                    if (!mul_done) begin
                        for (l = 0; l < STATE_LANES; ++l) begin
                            mul_a_r[l] <= ha[l][i_ctr];
                            mul_b_r[l] <= coeff_w[l];
                        end
                        if (i_ctr == 3'd4) begin
                            i_ctr <= '0;
                            if (j_ctr == 3'd4)
                                mul_done <= 1;
                            else
                                j_ctr <= j_ctr + 3'd1;
                        end else begin
                            i_ctr <= i_ctr + 3'd1;
                        end
                    end
                    v_p1 <= ~mul_done;
                    j_p1 <= j_ctr;
                    // P1: multiply between registers (DSP input/output regs)
                    for (l = 0; l < STATE_LANES; ++l)
                        prod_r[l] <= {29'b0, mul_a_r[l]} * {28'b0, mul_b_r[l]};
                    v_p2 <= v_p1;
                    j_p2 <= j_p1;
                    // P2: accumulate -- the only single-cycle feedback path
                    if (v_p2) begin
                        for (l = 0; l < STATE_LANES; ++l)
                            dacc[l][j_p2] <= dacc[l][j_p2] + {7'b0, prod_r[l]};
                    end
                    if (mul_done && ~v_p1 && ~v_p2)
                        state_r <= ST_CARRY;
`else
                    // Parallel: COLS_PER_CYC columns this cycle, each column
                    // dacc[jj] = sum_i ha[i] * coeff(i,jj),
                    // coeff(i,jj) = (i<=jj) ? r[jj-i] : 5r[5+jj-i]. Bit-identical
                    // to the serial path (same products, summed combinationally).
                    for (l = 0; l < STATE_LANES; ++l) begin
                        for (int cc = 0; cc < COLS_PER_CYC; ++cc) begin
                            int          jj;
                            logic [63:0] colsum;
                            jj = col_step * COLS_PER_CYC + cc;
                            colsum = '0;
                            for (int ii = 0; ii < 5; ++ii) begin
                                logic [28:0] cf;
                                if (ii <= jj)
                                    cf = {3'b0, state_mem[widx].r[l][jj - ii]};
                                else
                                    cf = state_mem[widx].s[l][jj + 5 - ii];
                                colsum = colsum + {7'b0, ({29'b0, ha[l][ii]} * {28'b0, cf})};
                            end
                            dacc[l][jj] <= colsum;
                        end
                    end
                    if (col_step == 3'(MUL_STEPS - 1))
                        state_r <= ST_CARRY;
                    else
                        col_step <= col_step + 3'd1;
`endif
                end
                ST_CARRY: begin
                    // donna-32 per-block carry + 2^130-5 fold, ONE carry step per
                    // cycle (same e/c sequence as the former combinational chain,
                    // so results are bit-identical; dacc holds e0..e4 in place)
                    for (l = 0; l < STATE_LANES; ++l) begin
                        case (carry_step)
                            3'd0: dacc[l][1] <= dacc[l][1] + (dacc[l][0] >> 26);
                            3'd1: dacc[l][2] <= dacc[l][2] + (dacc[l][1] >> 26);
                            3'd2: dacc[l][3] <= dacc[l][3] + (dacc[l][2] >> 26);
                            3'd3: dacc[l][4] <= dacc[l][4] + (dacc[l][3] >> 26);
                            3'd4: // a0 = (e0 & M) + c*5 (dacc[0] still holds e0)
                                dacc[l][0] <= (dacc[l][0] & 64'h3ffffff) + (dacc[l][4] >> 26) * 5;
                            default: begin // masked write-back; h1 keeps its carry bit
                                if (tmask_r[l]) begin
                                    state_mem[widx].acc[l][0] <= {1'b0, dacc[l][0][25:0]};   // h0 masked
                                    state_mem[widx].acc[l][1] <= 27'((dacc[l][1] & 64'h3ffffff) + (dacc[l][0] >> 26));
                                    state_mem[widx].acc[l][2] <= {1'b0, dacc[l][2][25:0]};
                                    state_mem[widx].acc[l][3] <= {1'b0, dacc[l][3][25:0]};
                                    state_mem[widx].acc[l][4] <= {1'b0, dacc[l][4][25:0]};
                                end
                            end
                        endcase
                    end
                    if (carry_step == 3'd5)
                        state_r <= ST_RESP;
                    else
                        carry_step <= carry_step + 3'd1;
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
