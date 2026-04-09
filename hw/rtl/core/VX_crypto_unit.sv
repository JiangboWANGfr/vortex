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

module VX_crypto_unit import VX_gpu_pkg::*; #(
    parameter `STRING INSTANCE_ID = ""
) (
    input wire              clk,
    input wire              reset,

    // Inputs
    VX_dispatch_if.slave    dispatch_if [`ISSUE_WIDTH],

    // Outputs
    VX_commit_if.master     commit_if [`ISSUE_WIDTH]
);

    `UNUSED_SPARAM (INSTANCE_ID)
    localparam BLOCK_SIZE   = `NUM_ALU_BLOCKS;
    localparam NUM_LANES    = `NUM_ALU_LANES;
    localparam PARTIAL_BW   = (BLOCK_SIZE != `ISSUE_WIDTH) || (NUM_LANES != `SIMD_WIDTH);

    VX_execute_if #(
        .data_t (alu_exe_t)
    ) per_block_execute_if[BLOCK_SIZE]();

    VX_result_if #(
        .data_t (alu_res_t)
    ) per_block_result_if[BLOCK_SIZE]();

    VX_dispatch_unit #(
        .BLOCK_SIZE (BLOCK_SIZE),
        .NUM_LANES  (NUM_LANES),
        .OUT_BUF    (PARTIAL_BW ? 3 : 0)
    ) dispatch_unit (
        .clk        (clk),
        .reset      (reset),
        .dispatch_if(dispatch_if),
        .execute_if (per_block_execute_if)
    );

    for (genvar block_idx = 0; block_idx < BLOCK_SIZE; ++block_idx) begin : g_blocks
        VX_alu_crypto #(
            .INSTANCE_ID (`SFORMATF(("%s-aes%0d", INSTANCE_ID, block_idx))),
            .NUM_LANES (NUM_LANES)
        ) aes_unit (
            .clk        (clk),
            .reset      (reset),
            .execute_if (per_block_execute_if[block_idx]),
            .result_if  (per_block_result_if[block_idx])
        );
    end

    VX_gather_unit #(
        .BLOCK_SIZE (BLOCK_SIZE),
        .NUM_LANES  (NUM_LANES),
        .OUT_BUF    (PARTIAL_BW ? 3 : 0)
    ) gather_unit (
        .clk       (clk),
        .reset     (reset),
        .result_if (per_block_result_if),
        .commit_if (commit_if)
    );

endmodule
