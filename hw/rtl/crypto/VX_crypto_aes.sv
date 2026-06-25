// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

`include "VX_define.vh"

module VX_crypto_aes import VX_gpu_pkg::*; #(
    parameter `STRING INSTANCE_ID = "",
    parameter NUM_LANES = 1,
    parameter STATE_LANES = NUM_LANES  // active AES round datapaths built (<= NUM_LANES)
) (
    input wire              clk,
    input wire              reset,

    VX_execute_if.slave     execute_if,
    VX_result_if.master     result_if
);
    `UNUSED_SPARAM(INSTANCE_ID)
    `STATIC_ASSERT ((STATE_LANES >= 1) && (STATE_LANES <= NUM_LANES), ("invalid STATE_LANES"))
    // AES is stateless (combinational round), so STATE_LANES just sets how many
    // per-lane round datapaths are physically built. STATE_LANES=1 collapses to a
    // single lane for WARP dispatch (only lane 0 active); the high interface lanes
    // are tied off and their result lanes are forced to 0 (masked at commit).

    localparam PID_WIDTH = `LOG2UP(`NUM_THREADS / NUM_LANES);
    localparam META_DATAW = UUID_WIDTH + NW_WIDTH + NUM_LANES + PC_BITS + 1 + NUM_REGS_BITS + PID_WIDTH + 1 + 1;

`ifdef XLEN_64
    // RV64 decodes only the round-granular Zkn aes64* ops. The byte-granular
    // aes32* encodings are RV32-only (see VX_decode), so the VX_aes (aes32) unit
    // is not instantiated here -- on RV64 it would be permanently idle dead logic.
    wire do_aes64es   = (execute_if.data.op_type == INST_CRYPTO_AES64ES);
    wire do_aes64esm  = (execute_if.data.op_type == INST_CRYPTO_AES64ESM);
    wire do_aes64ds   = (execute_if.data.op_type == INST_CRYPTO_AES64DS);
    wire do_aes64dsm  = (execute_if.data.op_type == INST_CRYPTO_AES64DSM);
    wire do_aes64im   = (execute_if.data.op_type == INST_CRYPTO_AES64IM);
    wire do_aes64ks1i = (execute_if.data.op_type == INST_CRYPTO_AES64KS1I);
    wire do_aes64ks2  = (execute_if.data.op_type == INST_CRYPTO_AES64KS2);
    wire is_aes64 = do_aes64es || do_aes64esm || do_aes64ds || do_aes64dsm || do_aes64im || do_aes64ks1i || do_aes64ks2;

    wire [3:0] round_imm = execute_if.data.op_args.crypto.round_imm;

    wire [STATE_LANES-1:0][63:0] rs1_data_64 = execute_if.data.rs1_data[STATE_LANES-1:0];
    wire [STATE_LANES-1:0][63:0] rs2_data_64 = execute_if.data.rs2_data[STATE_LANES-1:0];
    wire [STATE_LANES-1:0][63:0] crypto64_result;
    wire crypto64_ready_in;
    wire crypto64_valid_out;

    VX_aes64 #(
        .LANES (STATE_LANES)
    ) aes64_unit (
        .clk         (clk),
        .reset       (reset),
        .rs1_data    (rs1_data_64),
        .rs2_data    (rs2_data_64),
        .round_imm   (round_imm),
        .op_aes64es  (do_aes64es),
        .op_aes64esm (do_aes64esm),
        .op_aes64ds  (do_aes64ds),
        .op_aes64dsm (do_aes64dsm),
        .op_aes64im  (do_aes64im),
        .op_aes64ks1i(do_aes64ks1i),
        .op_aes64ks2 (do_aes64ks2),
        .result      (crypto64_result),
        .valid_in    (execute_if.valid && is_aes64),
        .ready_in    (crypto64_ready_in),
        .valid_out   (crypto64_valid_out),
        .ready_out   (result_if.ready)
    );

    wire selected_valid_out = crypto64_valid_out;
    assign execute_if.ready = crypto64_ready_in && (~crypto64_valid_out || result_if.ready);
`else
    // RV32: byte-granular Zkn aes32* is the only AES path.
    wire do_esi  = (execute_if.data.op_type == INST_CRYPTO_AES32ESI);
    wire do_esmi = (execute_if.data.op_type == INST_CRYPTO_AES32ESMI);
    wire do_dsi  = (execute_if.data.op_type == INST_CRYPTO_AES32DSI);
    wire do_dsmi = (execute_if.data.op_type == INST_CRYPTO_AES32DSMI);
    wire is_aes32 = do_esi || do_esmi || do_dsi || do_dsmi;

    wire [1:0] byte_select = execute_if.data.op_args.crypto.byte_select;

    wire [STATE_LANES-1:0][31:0] rs1_data;
    wire [STATE_LANES-1:0][31:0] rs2_data;
    for (genvar i = 0; i < STATE_LANES; ++i) begin : g_rs_data
        assign rs1_data[i] = execute_if.data.rs1_data[i][31:0];
        assign rs2_data[i] = execute_if.data.rs2_data[i][31:0];
    end

    wire [STATE_LANES-1:0][31:0] crypto_result;
    wire crypto32_ready_in;
    wire crypto32_valid_out;

    VX_aes #(
        .LANES (STATE_LANES)
    ) aes_unit (
        .clk            (clk),
        .reset          (reset),
        .rs1_data       (rs1_data),
        .rs2_data       (rs2_data),
        .bs             (byte_select),
        .op_saes32_encs (do_esi),
        .op_saes32_encsm(do_esmi),
        .op_saes32_decs (do_dsi),
        .op_saes32_decsm(do_dsmi),
        .result         (crypto_result),
        .valid_in       (execute_if.valid && is_aes32),
        .ready_in       (crypto32_ready_in),
        .valid_out      (crypto32_valid_out),
        .ready_out      (result_if.ready)
    );

    wire selected_valid_out = crypto32_valid_out;
    assign execute_if.ready = crypto32_ready_in;
`endif

    VX_pipe_register #(
        .DATAW  (META_DATAW),
        .RESETW (1)
    ) meta_pipe (
        .clk      (clk),
        .reset    (reset),
        .enable   (execute_if.ready),
        .data_in  ({execute_if.data.uuid, execute_if.data.wid, execute_if.data.tmask,
                    execute_if.data.PC, execute_if.data.wb, execute_if.data.rd, execute_if.data.pid,
                    execute_if.data.sop, execute_if.data.eop}),
        .data_out ({result_if.data.uuid, result_if.data.wid, result_if.data.tmask,
                    result_if.data.PC, result_if.data.wb, result_if.data.rd, result_if.data.pid,
                    result_if.data.sop, result_if.data.eop})
    );

    assign result_if.valid = selected_valid_out;

    for (genvar i = 0; i < NUM_LANES; ++i) begin : g_wb_data
        if (i < STATE_LANES) begin : g_active
        `ifdef XLEN_64
            assign result_if.data.data[i] = `XLEN'(crypto64_result[i]);
        `else
            assign result_if.data.data[i] = `XLEN'(crypto_result[i]);
        `endif
        end else begin : g_idle
            assign result_if.data.data[i] = '0;  // lane >= STATE_LANES: masked at commit
        end
    end

    // When fewer compute lanes than interface lanes, the high operand lanes are unused.
    if (STATE_LANES < NUM_LANES) begin : g_unused_hi
        wire _unused_hi = &{1'b0,
            execute_if.data.rs1_data[NUM_LANES-1:STATE_LANES],
            execute_if.data.rs2_data[NUM_LANES-1:STATE_LANES], 1'b0};
        `UNUSED_VAR(_unused_hi)
    end

endmodule
