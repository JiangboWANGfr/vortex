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
    parameter C_BAR_ADDR_WIDTH      = 20,
    parameter C_BAR_DATA_WIDTH      = 32,
    parameter C_MEM_NUM_BANKS       = `VX_CFG_PLATFORM_MEMORY_NUM_BANKS,
    parameter C_MEM_DATA_WIDTH      = `VX_CFG_PLATFORM_MEMORY_DATA_SIZE * 8,
    parameter C_MEM_ADDR_WIDTH      = 33,
    parameter C_MEM_BURST_WIDTH     = 5
) (
    `SCOPE_IO_DECL

    input  wire                             clk,
    input  wire                             reset,
    input  wire                             clock_change_req,
    input  wire                             clock_reset_req,
    output wire                             quiescent,
    output wire                             reset_active,

    input  wire                             ctrl_read,
    input  wire                             ctrl_write,
    input  wire [C_BAR_ADDR_WIDTH-1:0]      ctrl_address,
    input  wire [C_BAR_DATA_WIDTH-1:0]      ctrl_writedata,
    input  wire [C_BAR_DATA_WIDTH/8-1:0]    ctrl_byteenable,
    output wire [C_BAR_DATA_WIDTH-1:0]      ctrl_readdata,
    output wire                             ctrl_readdatavalid,
    output wire                             ctrl_waitrequest,

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
        .clock_change_req      (clock_change_req),
        .clock_reset_req       (clock_reset_req),
        .quiescent             (quiescent),
        .reset_active          (reset_active),
        .avs_ctrl_read         (ctrl_read),
        .avs_ctrl_write        (ctrl_write),
        .avs_ctrl_address      (ctrl_address),
        .avs_ctrl_writedata    (ctrl_writedata),
        .avs_ctrl_byteenable   (ctrl_byteenable),
        .avs_ctrl_readdata     (ctrl_readdata),
        .avs_ctrl_readdatavalid(ctrl_readdatavalid),
        .avs_ctrl_waitrequest  (ctrl_waitrequest),
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
