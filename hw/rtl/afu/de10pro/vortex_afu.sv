// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

`include "VX_define.vh"

module de10pro_vortex_afu import VX_gpu_pkg::*; #(
    parameter C_BAR_ADDR_WIDTH      = 64,
    parameter C_BAR_DATA_WIDTH      = 32,
    parameter C_MEM_NUM_BANKS       = `VX_CFG_PLATFORM_MEMORY_NUM_BANKS,
    parameter C_MEM_DATA_WIDTH      = `VX_CFG_PLATFORM_MEMORY_DATA_SIZE * 8,
    parameter C_MEM_ADDR_WIDTH      = `VX_CFG_PLATFORM_MEMORY_ADDR_WIDTH - `CLOG2(C_MEM_NUM_BANKS),
    parameter C_MEM_BURST_WIDTH     = 1
) (
    `SCOPE_IO_DECL

    input  wire                             clk,
    input  wire                             reset,

    input  wire                             rxm_bar4_read,
    input  wire                             rxm_bar4_write,
    input  wire [C_BAR_ADDR_WIDTH-1:0]      rxm_bar4_address,
    input  wire [C_BAR_DATA_WIDTH-1:0]      rxm_bar4_writedata,
    input  wire [C_BAR_DATA_WIDTH/8-1:0]    rxm_bar4_byteenable,
    output wire [C_BAR_DATA_WIDTH-1:0]      rxm_bar4_readdata,
    output wire                             rxm_bar4_readdatavalid,
    output wire                             rxm_bar4_waitrequest,

    output wire [C_MEM_DATA_WIDTH-1:0]      avs_writedata [C_MEM_NUM_BANKS],
    input  wire [C_MEM_DATA_WIDTH-1:0]      avs_readdata [C_MEM_NUM_BANKS],
    output wire [C_MEM_ADDR_WIDTH-1:0]      avs_address [C_MEM_NUM_BANKS],
    input  wire                             avs_waitrequest [C_MEM_NUM_BANKS],
    output wire                             avs_write [C_MEM_NUM_BANKS],
    output wire                             avs_read [C_MEM_NUM_BANKS],
    output wire [C_MEM_DATA_WIDTH/8-1:0]    avs_byteenable [C_MEM_NUM_BANKS],
    output wire [C_MEM_BURST_WIDTH-1:0]     avs_burstcount [C_MEM_NUM_BANKS],
    input  wire                             avs_readdatavalid [C_MEM_NUM_BANKS]
);

    VX_de10pro_afu_wrap #(
        .C_AVS_CTRL_ADDR_WIDTH (C_BAR_ADDR_WIDTH),
        .C_AVS_CTRL_DATA_WIDTH (C_BAR_DATA_WIDTH),
        .C_AVS_MEM_NUM_BANKS   (C_MEM_NUM_BANKS),
        .C_AVS_MEM_DATA_WIDTH  (C_MEM_DATA_WIDTH),
        .C_AVS_MEM_ADDR_WIDTH  (C_MEM_ADDR_WIDTH),
        .C_AVS_MEM_BURST_WIDTH (C_MEM_BURST_WIDTH)
    ) afu_wrap (
        `SCOPE_IO_BIND (0)

        .clk                   (clk),
        .reset                 (reset),
        .avs_ctrl_read         (rxm_bar4_read),
        .avs_ctrl_write        (rxm_bar4_write),
        .avs_ctrl_address      (rxm_bar4_address),
        .avs_ctrl_writedata    (rxm_bar4_writedata),
        .avs_ctrl_byteenable   (rxm_bar4_byteenable),
        .avs_ctrl_readdata     (rxm_bar4_readdata),
        .avs_ctrl_readdatavalid(rxm_bar4_readdatavalid),
        .avs_ctrl_waitrequest  (rxm_bar4_waitrequest),
        .avs_mem_writedata     (avs_writedata),
        .avs_mem_readdata      (avs_readdata),
        .avs_mem_address       (avs_address),
        .avs_mem_waitrequest   (avs_waitrequest),
        .avs_mem_write         (avs_write),
        .avs_mem_read          (avs_read),
        .avs_mem_byteenable    (avs_byteenable),
        .avs_mem_burstcount    (avs_burstcount),
        .avs_mem_readdatavalid (avs_readdatavalid)
    );

endmodule
