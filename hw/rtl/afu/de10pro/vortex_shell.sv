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

module vortex_shell #(
    parameter C_CTRL_ADDR_WIDTH = 64,
    parameter C_CTRL_DATA_WIDTH = 32,
    parameter C_MEM_ADDR_WIDTH  = 64,
    parameter C_MEM_DATA_WIDTH  = 512,
    parameter C_MEM_BURST_WIDTH = 1
) (
    input  wire                             clk,
    input  wire                             reset,

    input  wire                             ctrl_chipselect,
    input  wire                             ctrl_read,
    input  wire                             ctrl_write,
    input  wire [C_CTRL_ADDR_WIDTH-1:0]     ctrl_address,
    input  wire [C_CTRL_DATA_WIDTH-1:0]     ctrl_writedata,
    input  wire [C_CTRL_DATA_WIDTH/8-1:0]   ctrl_byteenable,
    output wire [C_CTRL_DATA_WIDTH-1:0]     ctrl_readdata,
    output wire                             ctrl_readdatavalid,
    output wire                             ctrl_waitrequest,

    output wire [C_MEM_ADDR_WIDTH-1:0]      vx_mem_address,
    output wire                             vx_mem_read,
    output wire                             vx_mem_write,
    output wire [C_MEM_DATA_WIDTH-1:0]      vx_mem_writedata,
    output wire [C_MEM_DATA_WIDTH/8-1:0]    vx_mem_byteenable,
    output wire [C_MEM_BURST_WIDTH-1:0]     vx_mem_burstcount,
    input  wire [C_MEM_DATA_WIDTH-1:0]      vx_mem_readdata,
    input  wire                             vx_mem_waitrequest,
    input  wire                             vx_mem_readdatavalid
);
    wire afu_ctrl_read  = ctrl_chipselect && ctrl_read;
    wire afu_ctrl_write = ctrl_chipselect && ctrl_write;

    wire [C_MEM_DATA_WIDTH-1:0]   afu_avs_writedata [0:0];
    wire [C_MEM_DATA_WIDTH-1:0]   afu_avs_readdata [0:0];
    wire [C_MEM_ADDR_WIDTH-1:0]   afu_avs_address [0:0];
    wire                          afu_avs_waitrequest [0:0];
    wire                          afu_avs_write [0:0];
    wire                          afu_avs_read [0:0];
    wire [C_MEM_DATA_WIDTH/8-1:0] afu_avs_byteenable [0:0];
    wire [C_MEM_BURST_WIDTH-1:0]  afu_avs_burstcount [0:0];
    wire                          afu_avs_readdatavalid [0:0];

    localparam C_MEM_ADDR_SHIFT = $clog2(C_MEM_DATA_WIDTH / 8);
    assign vx_mem_address           = C_MEM_ADDR_WIDTH'(afu_avs_address[0]) << C_MEM_ADDR_SHIFT;
    assign vx_mem_read              = afu_avs_read[0];
    assign vx_mem_write             = afu_avs_write[0];
    assign vx_mem_writedata         = afu_avs_writedata[0];
    assign vx_mem_byteenable        = afu_avs_byteenable[0];
    assign vx_mem_burstcount        = afu_avs_burstcount[0];
    assign afu_avs_readdata[0]      = vx_mem_readdata;
    assign afu_avs_waitrequest[0]   = vx_mem_waitrequest;
    assign afu_avs_readdatavalid[0] = vx_mem_readdatavalid;

    de10pro_vortex_afu #(
        .C_BAR_ADDR_WIDTH  (C_CTRL_ADDR_WIDTH),
        .C_BAR_DATA_WIDTH  (C_CTRL_DATA_WIDTH),
        .C_MEM_NUM_BANKS   (1),
        .C_MEM_DATA_WIDTH  (C_MEM_DATA_WIDTH),
        .C_MEM_ADDR_WIDTH  (C_MEM_ADDR_WIDTH),
        .C_MEM_BURST_WIDTH (C_MEM_BURST_WIDTH)
    ) afu (
        .clk                    (clk),
        .reset                  (reset),
        .rxm_bar4_read          (afu_ctrl_read),
        .rxm_bar4_write         (afu_ctrl_write),
        .rxm_bar4_address       (ctrl_address),
        .rxm_bar4_writedata     (ctrl_writedata),
        .rxm_bar4_byteenable    (ctrl_byteenable),
        .rxm_bar4_readdata      (ctrl_readdata),
        .rxm_bar4_readdatavalid (ctrl_readdatavalid),
        .rxm_bar4_waitrequest   (ctrl_waitrequest),
        .avs_writedata          (afu_avs_writedata),
        .avs_readdata           (afu_avs_readdata),
        .avs_address            (afu_avs_address),
        .avs_waitrequest        (afu_avs_waitrequest),
        .avs_write              (afu_avs_write),
        .avs_read               (afu_avs_read),
        .avs_byteenable         (afu_avs_byteenable),
        .avs_burstcount         (afu_avs_burstcount),
        .avs_readdatavalid      (afu_avs_readdatavalid)
    );

endmodule
