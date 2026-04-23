`include "VX_define.vh"

module VX_crypto_keccak import VX_gpu_pkg::*; #(
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

    typedef logic [4:0][4:0][63:0] keccak_state_t;

    localparam PID_WIDTH = `LOG2UP(`NUM_THREADS / NUM_LANES);
    localparam META_DATAW = UUID_WIDTH + NW_WIDTH + NUM_LANES + PC_BITS + 1 + NUM_REGS_BITS + PID_WIDTH + 1 + 1;
    localparam STATE_WARPS = `NUM_WARPS / BLOCK_SIZE;
    localparam STATE_WID_BITS = `CLOG2(STATE_WARPS);
    localparam STATE_WID_WIDTH = `UP(STATE_WID_BITS);

    localparam ST_IDLE    = 2'd0;
    localparam ST_PERMUTE = 2'd1;
    localparam ST_RESP    = 2'd2;

    keccak_state_t state_mem [STATE_WARPS];
    keccak_state_t perm_state_r;
    keccak_state_t perm_state_n;

    reg [1:0] state_r;
    reg [4:0] round_ctr_r;
    reg [NW_WIDTH-1:0] wid_r;
    reg [META_DATAW-1:0] meta_r;
    reg [63:0] pending_data_r;

    wire [4:0] lane_idx = execute_if.data.rs2_data[0][4:0];
    wire [4:0] read_lane_idx = execute_if.data.rs1_data[0][4:0];
    wire [63:0] lane_data_in = execute_if.data.rs1_data[0];
    wire [63:0] lane_data_out;

    function automatic [STATE_WID_WIDTH-1:0] keccak_state_idx(input [NW_WIDTH-1:0] wid);
        begin
            if (BLOCK_SIZE == 1) begin
                keccak_state_idx = STATE_WID_WIDTH'(wid);
            end else if (BLOCK_SIZE == `NUM_WARPS) begin
                keccak_state_idx = '0;
            end else begin
                keccak_state_idx = STATE_WID_WIDTH'(wid_to_wis(wid));
            end
        end
    endfunction

    function automatic [63:0] rotl64(input [63:0] value, input integer shamt);
        begin
            if (shamt == 0)
                rotl64 = value;
            else
                rotl64 = (value << shamt) | (value >> (64 - shamt));
        end
    endfunction

    function automatic [63:0] keccak_rc(input [4:0] round_number);
        begin
            case (round_number)
                5'd0:  keccak_rc = 64'h0000_0000_0000_0001;
                5'd1:  keccak_rc = 64'h0000_0000_0000_8082;
                5'd2:  keccak_rc = 64'h8000_0000_0000_808A;
                5'd3:  keccak_rc = 64'h8000_0000_8000_8000;
                5'd4:  keccak_rc = 64'h0000_0000_0000_808B;
                5'd5:  keccak_rc = 64'h0000_0000_8000_0001;
                5'd6:  keccak_rc = 64'h8000_0000_8000_8081;
                5'd7:  keccak_rc = 64'h8000_0000_0000_8009;
                5'd8:  keccak_rc = 64'h0000_0000_0000_008A;
                5'd9:  keccak_rc = 64'h0000_0000_0000_0088;
                5'd10: keccak_rc = 64'h0000_0000_8000_8009;
                5'd11: keccak_rc = 64'h0000_0000_8000_000A;
                5'd12: keccak_rc = 64'h0000_0000_8000_808B;
                5'd13: keccak_rc = 64'h8000_0000_0000_008B;
                5'd14: keccak_rc = 64'h8000_0000_0000_8089;
                5'd15: keccak_rc = 64'h8000_0000_0000_8003;
                5'd16: keccak_rc = 64'h8000_0000_0000_8002;
                5'd17: keccak_rc = 64'h8000_0000_0000_0080;
                5'd18: keccak_rc = 64'h0000_0000_0000_800A;
                5'd19: keccak_rc = 64'h8000_0000_8000_000A;
                5'd20: keccak_rc = 64'h8000_0000_8000_8081;
                5'd21: keccak_rc = 64'h8000_0000_0000_8080;
                5'd22: keccak_rc = 64'h0000_0000_8000_0001;
                5'd23: keccak_rc = 64'h8000_0000_8000_8008;
                default: keccak_rc = 64'h0;
            endcase
        end
    endfunction

    function automatic [63:0] keccak_lane_get(input keccak_state_t state, input [4:0] lane);
        begin
            keccak_lane_get = state[lane / 5][lane % 5];
        end
    endfunction

    function automatic keccak_state_t keccak_lane_set(
        input keccak_state_t state,
        input [4:0] lane,
        input [63:0] value,
        input logic do_xor
    );
        keccak_state_t tmp;
        begin
            tmp = state;
            if (do_xor)
                tmp[lane / 5][lane % 5] = tmp[lane / 5][lane % 5] ^ value;
            else
                tmp[lane / 5][lane % 5] = value;
            keccak_lane_set = tmp;
        end
    endfunction

    function automatic keccak_state_t keccak_round(
        input keccak_state_t state_in,
        input [4:0] round_number
    );
        keccak_state_t after_theta;
        keccak_state_t after_rho_pi;
        keccak_state_t after_chi;
        logic [4:0][63:0] c;
        logic [4:0][63:0] d;
        integer x, y;
        begin
            for (x = 0; x < 5; ++x) begin
                c[x] = state_in[0][x] ^ state_in[1][x] ^ state_in[2][x] ^ state_in[3][x] ^ state_in[4][x];
            end

            for (x = 0; x < 5; ++x) begin
                d[x] = c[(x + 4) % 5] ^ rotl64(c[(x + 1) % 5], 1);
            end

            for (y = 0; y < 5; ++y) begin
                for (x = 0; x < 5; ++x) begin
                    after_theta[y][x] = state_in[y][x] ^ d[x];
                    after_rho_pi[y][x] = '0;
                    after_chi[y][x] = '0;
                end
            end

            after_rho_pi[(2 * 0 + 3 * 0) % 5][0] = rotl64(after_theta[0][0], 0);
            after_rho_pi[(2 * 1 + 3 * 0) % 5][0] = rotl64(after_theta[0][1], 1);
            after_rho_pi[(2 * 2 + 3 * 0) % 5][0] = rotl64(after_theta[0][2], 62);
            after_rho_pi[(2 * 3 + 3 * 0) % 5][0] = rotl64(after_theta[0][3], 28);
            after_rho_pi[(2 * 4 + 3 * 0) % 5][0] = rotl64(after_theta[0][4], 27);

            after_rho_pi[(2 * 0 + 3 * 1) % 5][1] = rotl64(after_theta[1][0], 36);
            after_rho_pi[(2 * 1 + 3 * 1) % 5][1] = rotl64(after_theta[1][1], 44);
            after_rho_pi[(2 * 2 + 3 * 1) % 5][1] = rotl64(after_theta[1][2], 6);
            after_rho_pi[(2 * 3 + 3 * 1) % 5][1] = rotl64(after_theta[1][3], 55);
            after_rho_pi[(2 * 4 + 3 * 1) % 5][1] = rotl64(after_theta[1][4], 20);

            after_rho_pi[(2 * 0 + 3 * 2) % 5][2] = rotl64(after_theta[2][0], 3);
            after_rho_pi[(2 * 1 + 3 * 2) % 5][2] = rotl64(after_theta[2][1], 10);
            after_rho_pi[(2 * 2 + 3 * 2) % 5][2] = rotl64(after_theta[2][2], 43);
            after_rho_pi[(2 * 3 + 3 * 2) % 5][2] = rotl64(after_theta[2][3], 25);
            after_rho_pi[(2 * 4 + 3 * 2) % 5][2] = rotl64(after_theta[2][4], 39);

            after_rho_pi[(2 * 0 + 3 * 3) % 5][3] = rotl64(after_theta[3][0], 41);
            after_rho_pi[(2 * 1 + 3 * 3) % 5][3] = rotl64(after_theta[3][1], 45);
            after_rho_pi[(2 * 2 + 3 * 3) % 5][3] = rotl64(after_theta[3][2], 15);
            after_rho_pi[(2 * 3 + 3 * 3) % 5][3] = rotl64(after_theta[3][3], 21);
            after_rho_pi[(2 * 4 + 3 * 3) % 5][3] = rotl64(after_theta[3][4], 8);

            after_rho_pi[(2 * 0 + 3 * 4) % 5][4] = rotl64(after_theta[4][0], 18);
            after_rho_pi[(2 * 1 + 3 * 4) % 5][4] = rotl64(after_theta[4][1], 2);
            after_rho_pi[(2 * 2 + 3 * 4) % 5][4] = rotl64(after_theta[4][2], 61);
            after_rho_pi[(2 * 3 + 3 * 4) % 5][4] = rotl64(after_theta[4][3], 56);
            after_rho_pi[(2 * 4 + 3 * 4) % 5][4] = rotl64(after_theta[4][4], 14);

            for (y = 0; y < 5; ++y) begin
                for (x = 0; x < 5; ++x) begin
                    after_chi[y][x] = after_rho_pi[y][x]
                                    ^ ((~after_rho_pi[y][(x + 1) % 5]) & after_rho_pi[y][(x + 2) % 5]);
                end
            end

            after_chi[0][0] = after_chi[0][0] ^ keccak_rc(round_number);
            keccak_round = after_chi;
        end
    endfunction

    assign perm_state_n = keccak_round(perm_state_r, round_ctr_r);
    assign lane_data_out = keccak_lane_get(state_mem[keccak_state_idx(execute_if.data.wid)], read_lane_idx);

    wire execute_fire = (state_r == ST_IDLE) && execute_if.valid;
    wire do_write = (execute_if.data.op_type == INST_CRYPTO_KECCAK_WR);
    wire do_xor = (execute_if.data.op_type == INST_CRYPTO_KECCAK_XOR);
    wire do_read = (execute_if.data.op_type == INST_CRYPTO_KECCAK_RD);
    wire do_perm = (execute_if.data.op_type == INST_CRYPTO_KECCAK_F1600);

    integer w;
    always_ff @(posedge clk) begin
        if (reset) begin
            state_r <= ST_IDLE;
            round_ctr_r <= '0;
            wid_r <= '0;
            meta_r <= '0;
            pending_data_r <= '0;
            perm_state_r <= '0;
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
                        if (do_write) begin
                            state_mem[keccak_state_idx(execute_if.data.wid)] <= keccak_lane_set(state_mem[keccak_state_idx(execute_if.data.wid)], lane_idx, lane_data_in, 1'b0);
                            state_r <= ST_RESP;
                        end else if (do_xor) begin
                            state_mem[keccak_state_idx(execute_if.data.wid)] <= keccak_lane_set(state_mem[keccak_state_idx(execute_if.data.wid)], lane_idx, lane_data_in, 1'b1);
                            state_r <= ST_RESP;
                        end else if (do_read) begin
                            pending_data_r <= lane_data_out;
                            state_r <= ST_RESP;
                        end else if (do_perm) begin
                            perm_state_r <= state_mem[keccak_state_idx(execute_if.data.wid)];
                            round_ctr_r <= '0;
                            state_r <= ST_PERMUTE;
                        end else begin
                            state_r <= ST_RESP;
                        end
                    end
                end
                ST_PERMUTE: begin
                    perm_state_r <= perm_state_n;
                    if (round_ctr_r == 5'd23) begin
                        state_mem[keccak_state_idx(wid_r)] <= perm_state_n;
                        pending_data_r <= '0;
                        state_r <= ST_RESP;
                    end else begin
                        round_ctr_r <= round_ctr_r + 5'd1;
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
    assign result_if.valid = (state_r == ST_RESP);
    assign {result_if.data.uuid, result_if.data.wid, result_if.data.tmask,
            result_if.data.PC, result_if.data.wb, result_if.data.rd,
            result_if.data.pid, result_if.data.sop, result_if.data.eop} = meta_r;

    for (genvar i = 0; i < NUM_LANES; ++i) begin : g_wb_data
        assign result_if.data.data[i] = `XLEN'(pending_data_r);
    end

endmodule
