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
    // wid encodes the issue slot in its low bits, so using one crypto block per
    // issue slot preserves a stable warp-to-block affinity for Keccak without
    // forcing the whole crypto unit through a single serialized block.
    localparam BLOCK_SIZE   = `ISSUE_WIDTH;
    localparam NUM_LANES    = `NUM_ALU_LANES;
    localparam PARTIAL_BW   = (BLOCK_SIZE != `ISSUE_WIDTH) || (NUM_LANES != `SIMD_WIDTH);
    localparam ACTIVE_PE_COUNT = `EXT_AES_ENABLED + `EXT_SHA256_ENABLED + `EXT_KECCAK_ENABLED + `EXT_GHASH_ENABLED + `EXT_POLY1305_ENABLED;
    localparam PE_COUNT     = `UP(ACTIVE_PE_COUNT);
    localparam PE_SEL_BITS  = `CLOG2(PE_COUNT);
`ifdef EXT_AES_ENABLE
    localparam PE_IDX_AES   = 0;
`endif
`ifdef EXT_SHA256_ENABLE
    localparam PE_IDX_SHA   = `EXT_AES_ENABLED;
`endif
`ifdef EXT_KECCAK_ENABLE
    localparam PE_IDX_KECCAK = `EXT_AES_ENABLED + `EXT_SHA256_ENABLED;
`endif
`ifdef EXT_GHASH_ENABLE
    localparam PE_IDX_GHASH = `EXT_AES_ENABLED + `EXT_SHA256_ENABLED + `EXT_KECCAK_ENABLED;
`endif
`ifdef EXT_POLY1305_ENABLE
    localparam PE_IDX_POLY1305 = `EXT_AES_ENABLED + `EXT_SHA256_ENABLED + `EXT_KECCAK_ENABLED + `EXT_GHASH_ENABLED;
`endif

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
        VX_execute_if #(
            .data_t (alu_exe_t)
        ) pe_execute_if[PE_COUNT]();

        VX_result_if #(
            .data_t (alu_res_t)
        ) pe_result_if[PE_COUNT]();

        reg [`UP(PE_SEL_BITS)-1:0] pe_select;
        always @(*) begin
            pe_select = '0;
        `ifdef EXT_AES_ENABLE
            if (per_block_execute_if[block_idx].data.op_args.crypto.unit == CRYPTO_CLASS_AES)
                pe_select = PE_IDX_AES;
        `endif
        `ifdef EXT_SHA256_ENABLE
            if (per_block_execute_if[block_idx].data.op_args.crypto.unit == CRYPTO_CLASS_SHA)
                pe_select = PE_IDX_SHA;
        `endif
        `ifdef EXT_KECCAK_ENABLE
            if (per_block_execute_if[block_idx].data.op_args.crypto.unit == CRYPTO_CLASS_MISC)
                pe_select = PE_IDX_KECCAK;
        `endif
        `ifdef EXT_GHASH_ENABLE
            if (per_block_execute_if[block_idx].data.op_args.crypto.unit == CRYPTO_CLASS_GHASH)
                pe_select = PE_IDX_GHASH;
        `endif
        `ifdef EXT_POLY1305_ENABLE
            if (per_block_execute_if[block_idx].data.op_args.crypto.unit == CRYPTO_CLASS_POLY1305)
                pe_select = PE_IDX_POLY1305;
        `endif
        end

        VX_pe_switch #(
            .PE_COUNT    (PE_COUNT),
            .NUM_LANES   (NUM_LANES),
            .ARBITER     ("R"),
            .REQ_OUT_BUF (0),
            .RSP_OUT_BUF (PARTIAL_BW ? 1 : 3)
        ) pe_switch (
            .clk            (clk),
            .reset          (reset),
            .pe_sel         (pe_select),
            .execute_in_if  (per_block_execute_if[block_idx]),
            .result_out_if  (per_block_result_if[block_idx]),
            .execute_out_if (pe_execute_if),
            .result_in_if   (pe_result_if)
        );

    `ifdef EXT_AES_ENABLE
        VX_crypto_aes #(
            .INSTANCE_ID (`SFORMATF(("%s-aes%0d", INSTANCE_ID, block_idx))),
            .NUM_LANES   (NUM_LANES)
        ) aes_unit (
            .clk        (clk),
            .reset      (reset),
            .execute_if (pe_execute_if[PE_IDX_AES]),
            .result_if  (pe_result_if[PE_IDX_AES])
        );
    `endif

    `ifdef EXT_SHA256_ENABLE
        VX_crypto_sha #(
            .INSTANCE_ID (`SFORMATF(("%s-sha%0d", INSTANCE_ID, block_idx))),
            .NUM_LANES   (NUM_LANES)
        ) sha_unit (
            .clk        (clk),
            .reset      (reset),
            .execute_if (pe_execute_if[PE_IDX_SHA]),
            .result_if  (pe_result_if[PE_IDX_SHA])
        );
    `endif

    `ifdef EXT_KECCAK_ENABLE
        VX_crypto_keccak #(
            .INSTANCE_ID (`SFORMATF(("%s-keccak%0d", INSTANCE_ID, block_idx))),
            .NUM_LANES   (NUM_LANES),
            .BLOCK_SIZE  (BLOCK_SIZE),
            .BLOCK_IDX   (block_idx)
        ) keccak_unit (
            .clk        (clk),
            .reset      (reset),
            .execute_if (pe_execute_if[PE_IDX_KECCAK]),
            .result_if  (pe_result_if[PE_IDX_KECCAK])
        );
    `endif

    `ifdef EXT_GHASH_ENABLE
        VX_crypto_ghash #(
            .INSTANCE_ID (`SFORMATF(("%s-ghash%0d", INSTANCE_ID, block_idx))),
            .NUM_LANES   (NUM_LANES),
            .BLOCK_SIZE  (BLOCK_SIZE),
            .BLOCK_IDX   (block_idx)
        ) ghash_unit (
            .clk        (clk),
            .reset      (reset),
            .execute_if (pe_execute_if[PE_IDX_GHASH]),
            .result_if  (pe_result_if[PE_IDX_GHASH])
        );
    `endif

    `ifdef EXT_POLY1305_ENABLE
        VX_crypto_poly1305 #(
            .INSTANCE_ID (`SFORMATF(("%s-poly1305%0d", INSTANCE_ID, block_idx))),
            .NUM_LANES   (NUM_LANES),
            .BLOCK_SIZE  (BLOCK_SIZE),
            .BLOCK_IDX   (block_idx)
        ) poly1305_unit (
            .clk        (clk),
            .reset      (reset),
            .execute_if (pe_execute_if[PE_IDX_POLY1305]),
            .result_if  (pe_result_if[PE_IDX_POLY1305])
        );
    `endif

        if (ACTIVE_PE_COUNT == 0) begin : g_no_crypto_pe
            assign pe_execute_if[0].ready = 1'b1;
            assign pe_result_if[0].valid = 1'b0;
            assign pe_result_if[0].data = 'x;
        end
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
