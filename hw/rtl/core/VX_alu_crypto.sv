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

module VX_alu_crypto import VX_gpu_pkg::*; #(
    parameter `STRING INSTANCE_ID = "",
    parameter NUM_LANES = 1
) (
    input wire              clk,
    input wire              reset,

    VX_execute_if.slave     execute_if,
    VX_result_if.master     result_if
);
    `UNUSED_SPARAM(INSTANCE_ID)

    localparam PID_WIDTH = `LOG2UP(`NUM_THREADS / NUM_LANES);
    localparam META_DATAW = 1 + UUID_WIDTH + NW_WIDTH + NUM_LANES + PC_BITS + 1 + NUM_REGS_BITS + PID_WIDTH + 1 + 1;

    wire do_esi  = (execute_if.data.op_type == INST_CRYPTO_AES32ESI);
    wire do_esmi = (execute_if.data.op_type == INST_CRYPTO_AES32ESMI);
    wire do_dsi  = (execute_if.data.op_type == INST_CRYPTO_AES32DSI);
    wire do_dsmi = (execute_if.data.op_type == INST_CRYPTO_AES32DSMI);

    wire [1:0] byte_select = execute_if.data.op_args.alu.imm[1:0];

    wire [NUM_LANES-1:0][31:0] rs1_data;
    wire [NUM_LANES-1:0][31:0] rs2_data;
    for (genvar i = 0; i < NUM_LANES; ++i) begin : g_rs_data
        assign rs1_data[i] = execute_if.data.rs1_data[i][31:0];
        assign rs2_data[i] = execute_if.data.rs2_data[i][31:0];
    end

    wire [NUM_LANES-1:0][31:0] crypto_result;
    wire crypto_ready_in;

    VX_aes #(
        .LANES (NUM_LANES)
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
        .valid_in       (execute_if.valid),
        .ready_in       (crypto_ready_in),
        .valid_out      (),
        .ready_out      (result_if.ready)
    );

    wire meta_valid;
    assign execute_if.ready = crypto_ready_in;

    VX_pipe_register #(
        .DATAW  (META_DATAW),
        .RESETW (1)
    ) meta_pipe (
        .clk      (clk),
        .reset    (reset),
        .enable   (crypto_ready_in),
        .data_in  ({execute_if.valid, execute_if.data.uuid, execute_if.data.wid, execute_if.data.tmask,
                    execute_if.data.PC, execute_if.data.wb, execute_if.data.rd, execute_if.data.pid,
                    execute_if.data.sop, execute_if.data.eop}),
        .data_out ({meta_valid, result_if.data.uuid, result_if.data.wid, result_if.data.tmask,
                    result_if.data.PC, result_if.data.wb, result_if.data.rd, result_if.data.pid,
                    result_if.data.sop, result_if.data.eop})
    );

    assign result_if.valid = meta_valid;

    for (genvar i = 0; i < NUM_LANES; ++i) begin : g_wb_data
        assign result_if.data.data[i] = `XLEN'(crypto_result[i]);
    end

endmodule
