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
`include "vortex_afu.vh"

module VX_de10pro_afu_wrap import VX_gpu_pkg::*; #(
    parameter C_AVS_CTRL_ADDR_WIDTH = 20,
    parameter C_AVS_CTRL_DATA_WIDTH = 32,
    parameter C_AVS_MEM_NUM_BANKS   = `VX_CFG_PLATFORM_MEMORY_NUM_BANKS,
    parameter C_AVS_MEM_DATA_WIDTH  = `VX_CFG_PLATFORM_MEMORY_DATA_SIZE * 8,
    parameter C_AVS_MEM_ADDR_WIDTH  = 33,
    parameter C_AVS_MEM_BURST_WIDTH = 5
) (
    `SCOPE_IO_DECL

    input  wire                                  clk,
    input  wire                                  reset,

    input  wire                                  avs_ctrl_read,
    input  wire                                  avs_ctrl_write,
    input  wire [C_AVS_CTRL_ADDR_WIDTH-1:0]      avs_ctrl_address,
    input  wire [C_AVS_CTRL_DATA_WIDTH-1:0]      avs_ctrl_writedata,
    input  wire [C_AVS_CTRL_DATA_WIDTH/8-1:0]    avs_ctrl_byteenable,
    output wire [C_AVS_CTRL_DATA_WIDTH-1:0]      avs_ctrl_readdata,
    output wire                                  avs_ctrl_readdatavalid,
    output wire                                  avs_ctrl_waitrequest,

    output wire [C_AVS_MEM_DATA_WIDTH-1:0]       avs_mem_writedata [C_AVS_MEM_NUM_BANKS],
    input  wire [C_AVS_MEM_DATA_WIDTH-1:0]       avs_mem_readdata [C_AVS_MEM_NUM_BANKS],
    output wire [C_AVS_MEM_ADDR_WIDTH-1:0]       avs_mem_address [C_AVS_MEM_NUM_BANKS],
    input  wire                                  avs_mem_waitrequest [C_AVS_MEM_NUM_BANKS],
    output wire                                  avs_mem_write [C_AVS_MEM_NUM_BANKS],
    output wire                                  avs_mem_read [C_AVS_MEM_NUM_BANKS],
    output wire [C_AVS_MEM_DATA_WIDTH/8-1:0]     avs_mem_byteenable [C_AVS_MEM_NUM_BANKS],
    output wire [C_AVS_MEM_BURST_WIDTH-1:0]      avs_mem_burstcount [C_AVS_MEM_NUM_BANKS],
    input  wire                                  avs_mem_readdatavalid [C_AVS_MEM_NUM_BANKS]
);
    localparam RESET_CTR_WIDTH = `CLOG2(`VX_CFG_RESET_DELAY + 1);
    localparam STATE_IDLE = 8'd0;
    localparam STATE_INIT = 8'd1;
    localparam STATE_RUN  = 8'd2;

    localparam GPU_CLUSTER_SIZE = `VX_CFG_NUM_CORES / `VX_CFG_SOCKET_SIZE;
    localparam GPU_BANK_ADDR_W  = `VX_CFG_PLATFORM_MEMORY_ADDR_WIDTH
                                  - `CLOG2(`VX_CFG_PLATFORM_MEMORY_NUM_BANKS);

    `STATIC_ASSERT((GPU_CLUSTER_SIZE * `VX_CFG_SOCKET_SIZE) == `VX_CFG_NUM_CORES,
                   ("NUM_CORES must be a multiple of SOCKET_SIZE"));

    wire [63:0] dev_caps = {
        22'b0,
        5'(GPU_BANK_ADDR_W - 20),
        3'($clog2(`VX_CFG_PLATFORM_MEMORY_NUM_BANKS)),
        8'(`VX_CFG_LMEM_ENABLED ? `VX_CFG_LMEM_LOG_SIZE : 0),
        3'($clog2(`VX_CFG_ISSUE_WIDTH)),
        3'($clog2(`VX_CFG_NUM_CLUSTERS)),
        3'($clog2(GPU_CLUSTER_SIZE)),
        3'($clog2(`VX_CFG_SOCKET_SIZE)),
        3'($clog2(`VX_CFG_NUM_WARPS)),
        3'($clog2(`VX_CFG_NUM_THREADS)),
        8'(`VX_ISA_IMPL_ID)
    };

    wire [63:0] isa_caps = {
        32'(`VX_CFG_MISA_EXT),
        2'(`CLOG2(`VX_CFG_XLEN) - 4),
        30'(`VX_CFG_MISA_STD)
    };

    wire                           vx_mem_req_valid [VX_MEM_PORTS];
    wire                           vx_mem_req_rw [VX_MEM_PORTS];
    wire [VX_MEM_BYTEEN_WIDTH-1:0] vx_mem_req_byteen [VX_MEM_PORTS];
    wire [VX_MEM_ADDR_WIDTH-1:0]   vx_mem_req_addr [VX_MEM_PORTS];
    wire [VX_MEM_DATA_WIDTH-1:0]   vx_mem_req_data [VX_MEM_PORTS];
    wire [VX_MEM_TAG_WIDTH-1:0]    vx_mem_req_tag [VX_MEM_PORTS];
    wire                           vx_mem_req_ready [VX_MEM_PORTS];

    wire                           vx_mem_rsp_valid [VX_MEM_PORTS];
    wire [VX_MEM_DATA_WIDTH-1:0]   vx_mem_rsp_data [VX_MEM_PORTS];
    wire [VX_MEM_TAG_WIDTH-1:0]    vx_mem_rsp_tag [VX_MEM_PORTS];
    wire                           vx_mem_rsp_ready [VX_MEM_PORTS];

    wire                           run_valid;
    wire                           soft_reset_valid;
    wire                           dcr_req_valid;
    wire                           dcr_req_rw;
    wire [VX_DCR_ADDR_WIDTH-1:0]   dcr_req_addr;
    wire [VX_DCR_DATA_WIDTH-1:0]   dcr_req_data;
    wire                           dcr_rsp_valid;
    wire [VX_DCR_DATA_WIDTH-1:0]   dcr_rsp_data;
    wire                           vx_busy;

    reg [7:0] state;
    reg [RESET_CTR_WIDTH-1:0] vx_reset_ctr;
    reg vx_reset;
    reg launch_seen_busy;
    reg [31:0] launch_seq;

    wire start = run_valid && (state == STATE_IDLE) && ~vx_reset;
    wire [7:0] status_state = vx_reset ? STATE_INIT : state;
    wire [63:0] status_data = {launch_seq, 24'b0, status_state};

    always @(posedge clk) begin
        if (reset) begin
            state            <= STATE_IDLE;
            vx_reset         <= 1'b1;
            vx_reset_ctr     <= RESET_CTR_WIDTH'(`VX_CFG_RESET_DELAY - 1);
            launch_seen_busy <= 1'b0;
            launch_seq       <= '0;
        end else if (soft_reset_valid) begin
            state            <= STATE_IDLE;
            vx_reset         <= 1'b1;
            vx_reset_ctr     <= RESET_CTR_WIDTH'(`VX_CFG_RESET_DELAY - 1);
            launch_seen_busy <= 1'b0;
            launch_seq       <= '0;
        end else begin
            if (vx_reset) begin
                if (vx_reset_ctr == RESET_CTR_WIDTH'(0)) begin
                    vx_reset <= 1'b0;
                end else begin
                    vx_reset_ctr <= vx_reset_ctr - RESET_CTR_WIDTH'(1);
                end
            end

            case (state)
            STATE_IDLE: begin
                if (start) begin
                    state            <= STATE_RUN;
                    launch_seen_busy <= 1'b0;
                end
            end
            STATE_RUN: begin
                if (vx_busy) begin
                    launch_seen_busy <= 1'b1;
                end else if (launch_seen_busy) begin
                    state      <= STATE_IDLE;
                    launch_seq <= launch_seq + 32'd1;
                end
            end
            default: begin
                state <= STATE_IDLE;
            end
            endcase
        end
    end

    VX_de10pro_afu_ctrl #(
        .AVS_ADDR_WIDTH (C_AVS_CTRL_ADDR_WIDTH)
    ) afu_ctrl (
        .clk               (clk),
        .reset             (reset),
        .avs_read          (avs_ctrl_read),
        .avs_write         (avs_ctrl_write),
        .avs_address       (avs_ctrl_address),
        .avs_writedata     (avs_ctrl_writedata),
        .avs_byteenable    (avs_ctrl_byteenable),
        .avs_readdata      (avs_ctrl_readdata),
        .avs_readdatavalid (avs_ctrl_readdatavalid),
        .avs_waitrequest   (avs_ctrl_waitrequest),
        .status_data       (status_data),
        .dev_caps          (dev_caps),
        .isa_caps          (isa_caps),
        .run_valid         (run_valid),
        .soft_reset_valid  (soft_reset_valid),
        .dcr_req_valid     (dcr_req_valid),
        .dcr_req_rw        (dcr_req_rw),
        .dcr_req_addr      (dcr_req_addr),
        .dcr_req_data      (dcr_req_data),
        .dcr_rsp_valid     (dcr_rsp_valid),
        .dcr_rsp_data      (dcr_rsp_data)
    );

    Vortex vortex (
        `SCOPE_IO_BIND (0)

        .clk            (clk),
        .reset          (vx_reset),

        .mem_req_valid  (vx_mem_req_valid),
        .mem_req_rw     (vx_mem_req_rw),
        .mem_req_byteen (vx_mem_req_byteen),
        .mem_req_addr   (vx_mem_req_addr),
        .mem_req_data   (vx_mem_req_data),
        .mem_req_tag    (vx_mem_req_tag),
        .mem_req_ready  (vx_mem_req_ready),

        .mem_rsp_valid  (vx_mem_rsp_valid),
        .mem_rsp_data   (vx_mem_rsp_data),
        .mem_rsp_tag    (vx_mem_rsp_tag),
        .mem_rsp_ready  (vx_mem_rsp_ready),

        .dcr_req_valid  (dcr_req_valid),
        .dcr_req_rw     (dcr_req_rw),
        .dcr_req_addr   (dcr_req_addr),
        .dcr_req_data   (dcr_req_data),
        .dcr_rsp_valid  (dcr_rsp_valid),
        .dcr_rsp_data   (dcr_rsp_data),

        .start          (start),
        .busy           (vx_busy)
    );

    VX_mem_to_avs #(
        .DATA_WIDTH     (VX_MEM_DATA_WIDTH),
        .ADDR_WIDTH_IN  (VX_MEM_ADDR_WIDTH),
        .ADDR_WIDTH_OUT (C_AVS_MEM_ADDR_WIDTH),
        .BURST_WIDTH    (C_AVS_MEM_BURST_WIDTH),
        .NUM_PORTS_IN   (VX_MEM_PORTS),
        .NUM_BANKS_OUT  (C_AVS_MEM_NUM_BANKS),
        .TAG_WIDTH      (VX_MEM_TAG_WIDTH),
        .RD_QUEUE_SIZE  (16),
        .INTERLEAVE     (`VX_CFG_PLATFORM_MEMORY_INTERLEAVE),
        .REQ_OUT_BUF    (2),
        .RSP_OUT_BUF    ((VX_MEM_PORTS > 1 || C_AVS_MEM_NUM_BANKS > 1) ? 2 : 0)
    ) avs_adapter (
        .clk               (clk),
        .reset             (vx_reset),
        .mem_req_valid     (vx_mem_req_valid),
        .mem_req_rw        (vx_mem_req_rw),
        .mem_req_byteen    (vx_mem_req_byteen),
        .mem_req_addr      (vx_mem_req_addr),
        .mem_req_data      (vx_mem_req_data),
        .mem_req_tag       (vx_mem_req_tag),
        .mem_req_ready     (vx_mem_req_ready),
        .mem_rsp_valid     (vx_mem_rsp_valid),
        .mem_rsp_data      (vx_mem_rsp_data),
        .mem_rsp_tag       (vx_mem_rsp_tag),
        .mem_rsp_ready     (vx_mem_rsp_ready),
        .avs_writedata     (avs_mem_writedata),
        .avs_readdata      (avs_mem_readdata),
        .avs_address       (avs_mem_address),
        .avs_waitrequest   (avs_mem_waitrequest),
        .avs_write         (avs_mem_write),
        .avs_read          (avs_mem_read),
        .avs_byteenable    (avs_mem_byteenable),
        .avs_burstcount    (avs_mem_burstcount),
        .avs_readdatavalid (avs_mem_readdatavalid)
    );

endmodule
