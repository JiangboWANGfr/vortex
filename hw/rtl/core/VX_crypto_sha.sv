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

module VX_crypto_sha import VX_gpu_pkg::*; #(
    parameter `STRING INSTANCE_ID = "",
    parameter NUM_LANES = 1
) (
    input wire              clk,
    input wire              reset,

    VX_execute_if.slave     execute_if,
    VX_result_if.master     result_if
);
    `UNUSED_SPARAM(INSTANCE_ID)
    `UNUSED_VAR (execute_if.data.rs2_data)
    `UNUSED_VAR (execute_if.data.rs3_data)

    localparam PID_WIDTH = `LOG2UP(`NUM_THREADS / NUM_LANES);
    localparam META_DATAW = UUID_WIDTH + NW_WIDTH + NUM_LANES + PC_BITS + 1 + NUM_REGS_BITS + PID_WIDTH + 1 + 1;

    function automatic [31:0] ror32(
        input [31:0] value,
        input integer shamt
    );
        ror32 = (value >> shamt) | (value << (32 - shamt));
    endfunction

    wire [NUM_LANES-1:0][`XLEN-1:0] sha_result;
    wire [NUM_LANES-1:0][`XLEN-1:0] sha_result_r;

    for (genvar i = 0; i < NUM_LANES; ++i) begin : g_sha_result
        wire [31:0] rs1_word = execute_if.data.rs1_data[i][31:0];
        reg  [31:0] result32;

        always @(*) begin
            case (execute_if.data.op_type)
                INST_CRYPTO_SHA256SUM0: result32 = ror32(rs1_word, 2)  ^ ror32(rs1_word, 13) ^ ror32(rs1_word, 22);
                INST_CRYPTO_SHA256SUM1: result32 = ror32(rs1_word, 6)  ^ ror32(rs1_word, 11) ^ ror32(rs1_word, 25);
                INST_CRYPTO_SHA256SIG0: result32 = ror32(rs1_word, 7)  ^ ror32(rs1_word, 18) ^ (rs1_word >> 3);
                INST_CRYPTO_SHA256SIG1: result32 = ror32(rs1_word, 17) ^ ror32(rs1_word, 19) ^ (rs1_word >> 10);
                default:                result32 = '0;
            endcase
        end

        assign sha_result[i] = `SEXT(`XLEN, result32);
    end

    VX_elastic_buffer #(
        .DATAW (META_DATAW + (NUM_LANES * `XLEN))
    ) rsp_buf (
        .clk       (clk),
        .reset     (reset),
        .valid_in  (execute_if.valid),
        .ready_in  (execute_if.ready),
        .data_in   ({execute_if.data.uuid, execute_if.data.wid, execute_if.data.tmask,
                     execute_if.data.PC, execute_if.data.wb, execute_if.data.rd,
                     execute_if.data.pid, execute_if.data.sop, execute_if.data.eop,
                     sha_result}),
        .data_out  ({result_if.data.uuid, result_if.data.wid, result_if.data.tmask,
                     result_if.data.PC, result_if.data.wb, result_if.data.rd,
                     result_if.data.pid, result_if.data.sop, result_if.data.eop,
                     sha_result_r}),
        .valid_out (result_if.valid),
        .ready_out (result_if.ready)
    );

    for (genvar i = 0; i < NUM_LANES; ++i) begin : g_wb_data
        assign result_if.data.data[i] = sha_result_r[i];
    end

endmodule
