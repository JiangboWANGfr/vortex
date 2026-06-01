`include "VX_define.vh"

// ChaCha20 stream-cipher processing element (RFC 8439).
//
// The ARX cipher-core PE, the structural counterpart to the AES round unit just
// as Poly1305 is the counterpart to GHASH: a per-lane 16x32-bit ChaCha state on
// which BLOCK runs the 20-round (10 double-round) quarter-round permutation and
// the feedforward add, producing one 64-byte keystream block. Each SIMT lane
// enciphers its own block (per-lane state).
//
// The permutation multiplier is a SINGLE quarter-round datapath time-multiplexed
// over the 80 quarter-rounds (10 double-rounds x 8 QR); CHACHA_QR_RADIX chains
// that many quarter-rounds combinationally per cycle, so BLOCK takes 80/RADIX
// cycles. RADIX=1 is the area-minimal serial baseline (80 cycles); RADIX=80 is a
// fully combinational block (1 cycle, long path). Because it is the same QR
// schedule unrolled in order, every radix is bit-identical to chacha20.h.
//
// Words are 32-bit (decoupled from XLEN); byte<->word order and the block
// counter stay in the software wrapper, so the PE only ever sees 32-bit words.
//
// Ops (op_type):
//   CHACHA_WR    : state[lane][rs2[3:0]] = rs1[31:0]
//   CHACHA_BLOCK : state = permute(state) + state   (feedforward) [multi-cycle]
//   CHACHA_RD    : rd[lane] = state[lane][rs1[3:0]]

module VX_crypto_chacha import VX_gpu_pkg::*; #(
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

    // Quarter-rounds chained per cycle (design-space knob); BLOCK = 80/RADIX cyc.
`ifdef CHACHA_QR_RADIX
    localparam QR_RADIX = `CHACHA_QR_RADIX;
`else
    localparam QR_RADIX = 1;
`endif
    `STATIC_ASSERT ((80 % QR_RADIX) == 0, ("CHACHA_QR_RADIX must divide 80"))
    localparam PERM_STEPS = 80 / QR_RADIX;

    localparam WSEL_BITS = 4;   // 16 words, addressed in 32-bit units (not XLEN)

    localparam PID_WIDTH = `LOG2UP(`NUM_THREADS / NUM_LANES);
    localparam META_DATAW = UUID_WIDTH + NW_WIDTH + NUM_LANES + PC_BITS + 1 + NUM_REGS_BITS + PID_WIDTH + 1 + 1;
    localparam STATE_WARPS = `NUM_WARPS / BLOCK_SIZE;
    localparam STATE_WID_BITS = `CLOG2(STATE_WARPS);
    localparam STATE_WID_WIDTH = `UP(STATE_WID_BITS);

    localparam ST_IDLE    = 2'd0;
    localparam ST_PERMUTE = 2'd1;
    localparam ST_FEEDFWD = 2'd2;
    localparam ST_RESP    = 2'd3;

    // Per-warp, per-lane ChaCha state: 16 x 32-bit words.
    logic [NUM_LANES-1:0][15:0][31:0] state_mem [STATE_WARPS];

    reg [1:0]                       state_r;
    reg [NUM_LANES-1:0][15:0][31:0] x_r;       // working buffer (C x[])
    reg [NUM_LANES-1:0][15:0][31:0] st_r;      // feedforward snapshot (C st[])
    reg [6:0]                       perm_ctr_r; // 0..PERM_STEPS-1
    reg [NW_WIDTH-1:0]              wid_r;
    reg [NUM_LANES-1:0]             tmask_r;
    reg [META_DATAW-1:0]            meta_r;
    reg [NUM_LANES-1:0][`XLEN-1:0]  pending_data_r;

    wire [NUM_LANES-1:0] tmask = execute_if.data.tmask;

    function automatic [STATE_WID_WIDTH-1:0] chacha_state_idx(input [NW_WIDTH-1:0] wid);
        begin
            if (BLOCK_SIZE == 1)
                chacha_state_idx = STATE_WID_WIDTH'(wid);
            else if (BLOCK_SIZE == `NUM_WARPS)
                chacha_state_idx = '0;
            else
                chacha_state_idx = STATE_WID_WIDTH'(wid_to_wis(wid));
        end
    endfunction

    wire [STATE_WID_WIDTH-1:0] sidx = chacha_state_idx(execute_if.data.wid);
    wire [STATE_WID_WIDTH-1:0] widx = chacha_state_idx(wid_r);

    // left rotate by a constant amount (16/12/8/7)
    function automatic [31:0] rotl32(input [31:0] v, input integer n);
        rotl32 = (v << n) | (v >> (32 - n));
    endfunction

    // one ChaCha quarter-round (exact CC20_QR), returns {a,b,c,d}
    function automatic [127:0] chacha_qr(input [31:0] a_in, input [31:0] b_in,
                                         input [31:0] c_in, input [31:0] d_in);
        reg [31:0] a, b, c, d;
        begin
            a = a_in; b = b_in; c = c_in; d = d_in;
            a = a + b; d = d ^ a; d = rotl32(d, 16);
            c = c + d; b = b ^ c; b = rotl32(b, 12);
            a = a + b; d = d ^ a; d = rotl32(d, 8);
            c = c + d; b = b ^ c; b = rotl32(b, 7);
            chacha_qr = {a, b, c, d};
        end
    endfunction

    // QR step within a double-round (g mod 8) -> packed 4 word indices {ia,ib,ic,id}.
    function automatic [15:0] qr_indices(input [2:0] s);
        begin
            case (s)
                3'd0: qr_indices = {4'd0, 4'd4, 4'd8,  4'd12};
                3'd1: qr_indices = {4'd1, 4'd5, 4'd9,  4'd13};
                3'd2: qr_indices = {4'd2, 4'd6, 4'd10, 4'd14};
                3'd3: qr_indices = {4'd3, 4'd7, 4'd11, 4'd15};
                3'd4: qr_indices = {4'd0, 4'd5, 4'd10, 4'd15};
                3'd5: qr_indices = {4'd1, 4'd6, 4'd11, 4'd12};
                3'd6: qr_indices = {4'd2, 4'd7, 4'd8,  4'd13};
                3'd7: qr_indices = {4'd3, 4'd4, 4'd9,  4'd14};
            endcase
        end
    endfunction

    // chain QR_RADIX quarter-rounds (in schedule order) for one lane's state
    function automatic [511:0] perm_chain(input [511:0] xin, input [6:0] base_g);
        reg [15:0][31:0] xw;
        reg [15:0]       idx;
        reg [127:0]      q;
        reg [3:0]        ia, ib, ic, id;
        integer          k;
        begin
            xw = xin;
            for (k = 0; k < QR_RADIX; k = k + 1) begin
                idx = qr_indices(3'(base_g + 7'(k)));  // schedule index = (g) mod 8
                ia = idx[15:12]; ib = idx[11:8]; ic = idx[7:4]; id = idx[3:0];
                q = chacha_qr(xw[ia], xw[ib], xw[ic], xw[id]);
                xw[ia] = q[127:96]; xw[ib] = q[95:64]; xw[ic] = q[63:32]; xw[id] = q[31:0];
            end
            perm_chain = xw;
        end
    endfunction

    wire execute_fire = (state_r == ST_IDLE) && execute_if.valid;
    wire do_write = (execute_if.data.op_type == INST_CRYPTO_CHACHA_WR);
    wire do_block = (execute_if.data.op_type == INST_CRYPTO_CHACHA_BLOCK);
    wire do_read  = (execute_if.data.op_type == INST_CRYPTO_CHACHA_RD);

    integer w, l;
    always_ff @(posedge clk) begin
        if (reset) begin
            state_r        <= ST_IDLE;
            perm_ctr_r     <= '0;
            x_r            <= '0;
            st_r           <= '0;
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
                        if (do_write) begin
                            for (l = 0; l < NUM_LANES; ++l)
                                if (tmask[l])
                                    state_mem[sidx][l][execute_if.data.rs2_data[l][WSEL_BITS-1:0]]
                                        <= execute_if.data.rs1_data[l][31:0];
                            state_r <= ST_RESP;
                        end else if (do_read) begin
                            for (l = 0; l < NUM_LANES; ++l)
                                pending_data_r[l]
                                    <= `XLEN'(state_mem[sidx][l][execute_if.data.rs1_data[l][WSEL_BITS-1:0]]);
                            state_r <= ST_RESP;
                        end else if (do_block) begin
                            x_r        <= state_mem[sidx];
                            st_r       <= state_mem[sidx];  // snapshot for feedforward
                            perm_ctr_r <= '0;
                            state_r    <= ST_PERMUTE;
                        end else begin
                            state_r <= ST_RESP;
                        end
                    end
                end
                ST_PERMUTE: begin
                    for (l = 0; l < NUM_LANES; ++l)
                        x_r[l] <= perm_chain(x_r[l], 7'(perm_ctr_r * 7'(QR_RADIX)));
                    if (perm_ctr_r == 7'(PERM_STEPS - 1))
                        state_r <= ST_FEEDFWD;
                    else
                        perm_ctr_r <= perm_ctr_r + 7'd1;
                end
                ST_FEEDFWD: begin
                    for (l = 0; l < NUM_LANES; ++l)
                        if (tmask_r[l])
                            for (w = 0; w < 16; ++w)
                                state_mem[widx][l][w] <= x_r[l][w] + st_r[l][w];
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
