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
`include "vortex_afu.vh"

module VX_de10pro_afu_wrap import VX_gpu_pkg::*; #(
    parameter C_AVS_CTRL_ADDR_WIDTH = 64,
    parameter C_AVS_CTRL_DATA_WIDTH = 32,
`ifdef PLATFORM_MERGED_MEMORY_INTERFACE
    parameter C_AVS_MEM_NUM_BANKS   = 1,
`else
    parameter C_AVS_MEM_NUM_BANKS   = `PLATFORM_MEMORY_NUM_BANKS,
`endif
    parameter C_AVS_MEM_DATA_WIDTH  = `PLATFORM_MEMORY_DATA_SIZE * 8,
    parameter C_AVS_MEM_ADDR_WIDTH  = `PLATFORM_MEMORY_ADDR_WIDTH - `CLOG2(C_AVS_MEM_NUM_BANKS),
    parameter C_AVS_MEM_BURST_WIDTH = 1
) (
    `SCOPE_IO_DECL

    input  wire                                 clk,
    input  wire                                 reset,

    input  wire                                 avs_ctrl_read,
    input  wire                                 avs_ctrl_write,
    input  wire [C_AVS_CTRL_ADDR_WIDTH-1:0]     avs_ctrl_address,
    input  wire [C_AVS_CTRL_DATA_WIDTH-1:0]     avs_ctrl_writedata,
    input  wire [C_AVS_CTRL_DATA_WIDTH/8-1:0]   avs_ctrl_byteenable,
    output wire [C_AVS_CTRL_DATA_WIDTH-1:0]     avs_ctrl_readdata,
    output wire                                 avs_ctrl_readdatavalid,
    output wire                                 avs_ctrl_waitrequest,

    output wire [C_AVS_MEM_DATA_WIDTH-1:0]      avs_mem_writedata [C_AVS_MEM_NUM_BANKS],
    input  wire [C_AVS_MEM_DATA_WIDTH-1:0]      avs_mem_readdata [C_AVS_MEM_NUM_BANKS],
    output wire [C_AVS_MEM_ADDR_WIDTH-1:0]      avs_mem_address [C_AVS_MEM_NUM_BANKS],
    input  wire                                 avs_mem_waitrequest [C_AVS_MEM_NUM_BANKS],
    output wire                                 avs_mem_write [C_AVS_MEM_NUM_BANKS],
    output wire                                 avs_mem_read [C_AVS_MEM_NUM_BANKS],
    output wire [C_AVS_MEM_DATA_WIDTH/8-1:0]    avs_mem_byteenable [C_AVS_MEM_NUM_BANKS],
    output wire [C_AVS_MEM_BURST_WIDTH-1:0]     avs_mem_burstcount [C_AVS_MEM_NUM_BANKS],
    input  wire                                 avs_mem_readdatavalid [C_AVS_MEM_NUM_BANKS]
);
    localparam COUT_TID_WIDTH = `CLOG2(VX_MEM_BYTEEN_WIDTH);
    localparam COUT_QUEUE_DATAW = COUT_TID_WIDTH + 8;
    localparam COUT_QUEUE_SIZE = 1024;
    localparam MEM_PORTS_BITS = `CLOG2(VX_MEM_PORTS);
    localparam MEM_PORTS_WIDTH = (MEM_PORTS_BITS > 0) ? MEM_PORTS_BITS : 1;
    localparam STATUS_QUEUE_PADW = 64 - 8 - 1 - COUT_QUEUE_DATAW;
    localparam RESET_CTR_WIDTH = `CLOG2(`RESET_DELAY + 1);
    localparam MEMORY_BANK_ADDR_WIDTH = `PLATFORM_MEMORY_ADDR_WIDTH - `CLOG2(C_AVS_MEM_NUM_BANKS);
    localparam STATE_IDLE = 8'd0;
    localparam STATE_INIT = 8'd1;
    localparam STATE_RUN  = 8'd2;

    wire [63:0] dev_caps = {
        8'b0,
        5'(MEMORY_BANK_ADDR_WIDTH - 20),
        3'(`CLOG2(C_AVS_MEM_NUM_BANKS)),
        8'(`LMEM_ENABLED ? `LMEM_LOG_SIZE : 0),
        16'(`NUM_CORES * `NUM_CLUSTERS),
        8'(`NUM_WARPS),
        8'(`NUM_THREADS),
        8'(`IMPLEMENTATION_ID)
    };

    wire [63:0] isa_caps = {
        32'(`MISA_EXT),
        2'(`CLOG2(`XLEN) - 4),
        30'(`MISA_STD)
    };

    wire                            vx_mem_req_valid [VX_MEM_PORTS];
    wire                            vx_mem_req_rw [VX_MEM_PORTS];
    wire [VX_MEM_BYTEEN_WIDTH-1:0]  vx_mem_req_byteen [VX_MEM_PORTS];
    wire [VX_MEM_ADDR_WIDTH-1:0]    vx_mem_req_addr [VX_MEM_PORTS];
    wire [VX_MEM_DATA_WIDTH-1:0]    vx_mem_req_data [VX_MEM_PORTS];
    wire [VX_MEM_TAG_WIDTH-1:0]     vx_mem_req_tag [VX_MEM_PORTS];
    wire                            vx_mem_req_ready [VX_MEM_PORTS];

    wire                            vx_mem_rsp_valid [VX_MEM_PORTS];
    wire [VX_MEM_DATA_WIDTH-1:0]    vx_mem_rsp_data [VX_MEM_PORTS];
    wire [VX_MEM_TAG_WIDTH-1:0]     vx_mem_rsp_tag [VX_MEM_PORTS];
    wire                            vx_mem_rsp_ready [VX_MEM_PORTS];

    wire                            vx_mem_req_valid_qual [VX_MEM_PORTS];
    wire                            vx_mem_req_ready_qual [VX_MEM_PORTS];

    wire [VX_MEM_PORTS-1:0][COUT_QUEUE_DATAW-1:0] cout_q_dout;
    wire [VX_MEM_PORTS-1:0] cout_q_full, cout_q_empty, cout_q_pop;

    reg [MEM_PORTS_WIDTH-1:0] cout_q_id;
    reg [7:0] state;
    reg [RESET_CTR_WIDTH-1:0] vx_reset_ctr;
    reg vx_busy_wait;
    reg vx_reset;

    wire vx_busy;
    wire status_read;
    wire run_valid;
    wire soft_reset_valid;
    wire dcr_wr_valid;
    wire [VX_DCR_ADDR_WIDTH-1:0] dcr_wr_addr;
    wire [VX_DCR_DATA_WIDTH-1:0] dcr_wr_data;
    // Pipeline the soft reset one stage: the raw (reset || strobe) OR fans
    // out to the whole AVS adapter and the cout queues, putting the reset
    // synchronizer on the critical path. soft_reset_valid_r keeps the reset
    // counter preload below aligned with the registered reset.
    reg soft_reset_r, soft_reset_valid_r;
    always @(posedge clk) begin
        soft_reset_r       <= reset || soft_reset_valid;
        soft_reset_valid_r <= soft_reset_valid;
    end

    wire [COUT_QUEUE_DATAW-1:0] cout_q_dout_s = cout_q_dout[cout_q_id] & {COUT_QUEUE_DATAW{~cout_q_empty[cout_q_id]}};
    wire cout_q_empty_all = &cout_q_empty;

    wire [63:0] status_data = {
        {STATUS_QUEUE_PADW{1'b0}},
        cout_q_dout_s,
        ~cout_q_empty_all,
        state
    };

    always @(posedge clk) begin
        if (soft_reset_r) begin
            state <= STATE_IDLE;
            vx_reset <= 1;
            vx_reset_ctr <= soft_reset_valid_r ? RESET_CTR_WIDTH'(`RESET_DELAY - 1) : '0;
            vx_busy_wait <= 0;
        end else begin
            case (state)
            STATE_IDLE: begin
                if (run_valid) begin
                    state <= STATE_INIT;
                    vx_reset <= 1;
                    vx_reset_ctr <= RESET_CTR_WIDTH'(`RESET_DELAY - 1);
                    vx_busy_wait <= 0;
                end
            end
            STATE_INIT: begin
                if (vx_reset) begin
                    if (vx_reset_ctr == RESET_CTR_WIDTH'(0)) begin
                        vx_reset <= 0;
                        vx_busy_wait <= 1;
                    end
                end else if (vx_busy_wait) begin
                    if (vx_busy) begin
                        vx_busy_wait <= 0;
                        state <= STATE_RUN;
                    end
                end
            end
            STATE_RUN: begin
                if (~vx_busy) begin
                    state <= STATE_IDLE;
                end
            end
            default: begin
                state <= STATE_IDLE;
            end
            endcase

            if (vx_reset_ctr != RESET_CTR_WIDTH'(0)) begin
                vx_reset_ctr <= vx_reset_ctr - RESET_CTR_WIDTH'(1);
            end
        end
    end

    always @(posedge clk) begin
        if (reset) begin
            cout_q_id <= '0;
        end else if (status_read) begin
            cout_q_id <= cout_q_id + MEM_PORTS_WIDTH'(1);
        end
    end

    for (genvar i = 0; i < VX_MEM_PORTS; ++i) begin : g_cout_q_pop
        assign cout_q_pop[i] = status_read && (cout_q_id == i) && ~cout_q_empty[i];
    end

    for (genvar i = 0; i < VX_MEM_PORTS; ++i) begin : g_cout
        wire [COUT_TID_WIDTH-1:0] cout_tid;
        wire [VX_MEM_BYTEEN_WIDTH-1:0][7:0] vx_mem_req_data_m = vx_mem_req_data[i];
        wire [7:0] cout_char = vx_mem_req_data_m[cout_tid];
        wire [VX_MEM_ADDR_WIDTH-1:0] io_cout_addr_b = VX_MEM_ADDR_WIDTH'(`IO_COUT_ADDR >> `CLOG2(`MEM_BLOCK_SIZE));
        wire vx_mem_is_cout = (vx_mem_req_addr[i] == io_cout_addr_b);
        wire cout_q_push = vx_mem_req_valid[i] && vx_mem_is_cout && ~cout_q_full[i];

        VX_onehot_encoder #(
            .N (VX_MEM_BYTEEN_WIDTH)
        ) cout_tid_enc (
            .data_in  (vx_mem_req_byteen[i]),
            .data_out (cout_tid),
            `UNUSED_PIN (valid_out)
        );

        assign vx_mem_req_valid_qual[i] = vx_mem_req_valid[i] && ~vx_mem_is_cout;
        assign vx_mem_req_ready[i] = vx_mem_is_cout ? ~cout_q_full[i] : vx_mem_req_ready_qual[i];

        VX_fifo_queue #(
            .DATAW (COUT_QUEUE_DATAW),
            .DEPTH (COUT_QUEUE_SIZE)
        ) cout_queue (
            .clk      (clk),
            .reset    (soft_reset_r),
            .push     (cout_q_push),
            .pop      (cout_q_pop[i]),
            .data_in  ({cout_tid, cout_char}),
            .data_out (cout_q_dout[i]),
            .empty    (cout_q_empty[i]),
            .full     (cout_q_full[i]),
            `UNUSED_PIN (alm_empty),
            `UNUSED_PIN (alm_full),
            `UNUSED_PIN (size)
        );
    end

    VX_de10pro_afu_ctrl #(
        .AVS_ADDR_WIDTH (C_AVS_CTRL_ADDR_WIDTH)
    ) afu_ctrl (
        .clk              (clk),
        .reset            (reset),
        .avs_read         (avs_ctrl_read),
        .avs_write        (avs_ctrl_write),
        .avs_address      (avs_ctrl_address),
        .avs_writedata    (avs_ctrl_writedata),
        .avs_byteenable   (avs_ctrl_byteenable),
        .avs_readdata     (avs_ctrl_readdata),
        .avs_readdatavalid(avs_ctrl_readdatavalid),
        .avs_waitrequest  (avs_ctrl_waitrequest),
        .status_data      (status_data),
        .dev_caps         (dev_caps),
        .isa_caps         (isa_caps),
        .status_read      (status_read),
        .run_valid        (run_valid),
        .soft_reset_valid (soft_reset_valid),
        .dcr_wr_valid     (dcr_wr_valid),
        .dcr_wr_addr      (dcr_wr_addr),
        .dcr_wr_data      (dcr_wr_data)
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

        .dcr_wr_valid   (dcr_wr_valid),
        .dcr_wr_addr    (dcr_wr_addr),
        .dcr_wr_data    (dcr_wr_data),

        .busy           (vx_busy)
    );

    VX_avs_adapter #(
        .DATA_WIDTH    (VX_MEM_DATA_WIDTH),
        .ADDR_WIDTH_IN (VX_MEM_ADDR_WIDTH),
        .ADDR_WIDTH_OUT(C_AVS_MEM_ADDR_WIDTH),
        .BURST_WIDTH   (C_AVS_MEM_BURST_WIDTH),
        .NUM_PORTS_IN  (VX_MEM_PORTS),
        .NUM_BANKS_OUT (C_AVS_MEM_NUM_BANKS),
        .TAG_WIDTH     (VX_MEM_TAG_WIDTH),
        .RD_QUEUE_SIZE (16),
        .INTERLEAVE    (`PLATFORM_MEMORY_INTERLEAVE),
        .REQ_OUT_BUF   (2),
        .RSP_OUT_BUF   ((VX_MEM_PORTS > 1 || C_AVS_MEM_NUM_BANKS > 1) ? 2 : 0)
    ) avs_adapter (
        .clk              (clk),
        .reset            (soft_reset_r),
        .mem_req_valid    (vx_mem_req_valid_qual),
        .mem_req_rw       (vx_mem_req_rw),
        .mem_req_byteen   (vx_mem_req_byteen),
        .mem_req_addr     (vx_mem_req_addr),
        .mem_req_data     (vx_mem_req_data),
        .mem_req_tag      (vx_mem_req_tag),
        .mem_req_ready    (vx_mem_req_ready_qual),
        .mem_rsp_valid    (vx_mem_rsp_valid),
        .mem_rsp_data     (vx_mem_rsp_data),
        .mem_rsp_tag      (vx_mem_rsp_tag),
        .mem_rsp_ready    (vx_mem_rsp_ready),
        .avs_writedata    (avs_mem_writedata),
        .avs_readdata     (avs_mem_readdata),
        .avs_address      (avs_mem_address),
        .avs_waitrequest  (avs_mem_waitrequest),
        .avs_write        (avs_mem_write),
        .avs_read         (avs_mem_read),
        .avs_byteenable   (avs_mem_byteenable),
        .avs_burstcount   (avs_mem_burstcount),
        .avs_readdatavalid(avs_mem_readdatavalid)
    );

endmodule
