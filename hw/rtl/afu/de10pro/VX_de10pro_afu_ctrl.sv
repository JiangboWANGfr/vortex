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

`include "vortex_afu.vh"

module VX_de10pro_afu_ctrl import VX_gpu_pkg::*; #(
    parameter AVS_ADDR_WIDTH = 8
) (
    input  wire                         clk,
    input  wire                         reset,

    input  wire                         avs_read,
    input  wire                         avs_write,
    input  wire [AVS_ADDR_WIDTH-1:0]    avs_address,
    input  wire [31:0]                  avs_writedata,
    input  wire [3:0]                   avs_byteenable,
    output logic [31:0]                 avs_readdata,
    output logic                        avs_readdatavalid,
    output wire                         avs_waitrequest,

    input  wire [63:0]                  status_data,
    input  wire [63:0]                  dev_caps,
    input  wire [63:0]                  isa_caps,
    output logic                        status_read,

    output logic                        run_valid,
    output logic                        soft_reset_valid,
    output logic                        dcr_wr_valid,
    output logic [VX_DCR_ADDR_WIDTH-1:0] dcr_wr_addr,
    output logic [VX_DCR_DATA_WIDTH-1:0] dcr_wr_data
);
    localparam MMIO_CMD_TYPE = `AFU_IMAGE_MMIO_CMD_TYPE;
    localparam MMIO_CMD_ARG0 = `AFU_IMAGE_MMIO_CMD_ARG0;
    localparam MMIO_CMD_ARG1 = `AFU_IMAGE_MMIO_CMD_ARG1;
    localparam MMIO_CMD_ARG2 = `AFU_IMAGE_MMIO_CMD_ARG2;
    localparam MMIO_STATUS   = `AFU_IMAGE_MMIO_STATUS;
    localparam MMIO_DEV_CAPS = `AFU_IMAGE_MMIO_DEV_CAPS;
    localparam MMIO_ISA_CAPS = `AFU_IMAGE_MMIO_ISA_CAPS;

    localparam CMD_DCR_WRITE = `AFU_IMAGE_CMD_DCR_WRITE;
    localparam CMD_RUN       = `AFU_IMAGE_CMD_RUN;
    localparam CMD_RESET     = `AFU_IMAGE_CMD_RESET;

    // Register the AVS command inputs once on entry: MMIO commands arrive
    // through the Platform Designer clock crosser from across the die, and
    // decoupling that route from the decode logic removes the build's worst
    // setup path. waitrequest is hard-wired 0 and reads complete through
    // avs_readdatavalid, so the extra cycle of command latency is
    // protocol-legal (Avalon-MM variable-latency read).
    reg        avs_read_r, avs_write_r;
    reg [7:0]  avs_address_r;
    reg [31:0] avs_writedata_r;
    reg [3:0]  avs_byteenable_r;

    always @(posedge clk) begin
        if (reset) begin
            avs_read_r  <= 0;
            avs_write_r <= 0;
        end else begin
            avs_read_r  <= avs_read;
            avs_write_r <= avs_write;
        end
        avs_address_r    <= avs_address[7:0];
        avs_writedata_r  <= avs_writedata;
        avs_byteenable_r <= avs_byteenable;
    end

    wire [7:0] mmio_addr = avs_address_r;
    wire [7:0] mmio_addr_aligned = {mmio_addr[7:3], 3'b000};
    wire mmio_hi_word = mmio_addr[2];
    wire write_fire = avs_write_r;
    wire read_fire  = avs_read_r;

    logic [63:0] cmd_args [0:2];
    logic [63:0] status_latch;
    logic [31:0] read_data_n;

    function automatic [31:0] apply_byteenable(
        input [31:0] orig,
        input [31:0] data,
        input [3:0]  byteenable
    );
        begin
            apply_byteenable = orig;
            for (int i = 0; i < 4; ++i) begin
                if (byteenable[i]) begin
                    apply_byteenable[i * 8 +: 8] = data[i * 8 +: 8];
                end
            end
        end
    endfunction

    always @(*) begin
        read_data_n = 32'h0;
        case (mmio_addr_aligned)
        MMIO_CMD_ARG0: read_data_n = mmio_hi_word ? cmd_args[0][63:32] : cmd_args[0][31:0];
        MMIO_CMD_ARG1: read_data_n = mmio_hi_word ? cmd_args[1][63:32] : cmd_args[1][31:0];
        MMIO_CMD_ARG2: read_data_n = mmio_hi_word ? cmd_args[2][63:32] : cmd_args[2][31:0];
        MMIO_STATUS:   read_data_n = mmio_hi_word ? status_latch[63:32] : status_data[31:0];
        MMIO_DEV_CAPS: read_data_n = mmio_hi_word ? dev_caps[63:32] : dev_caps[31:0];
        MMIO_ISA_CAPS: read_data_n = mmio_hi_word ? isa_caps[63:32] : isa_caps[31:0];
        default:       read_data_n = 32'h0;
        endcase
    end

    always @(posedge clk) begin
        if (reset) begin
            // Clear control strobes only. Data registers (cmd_args,
            // status_latch, avs_readdata, dcr_wr_addr/data) are always
            // written by the host before they are consumed, and keeping them
            // out of the synchronous clear keeps the high-fanout reset net
            // off their datapath (it was the worst setup path of the shell).
            avs_readdatavalid <= 0;
            status_read <= 0;
            run_valid <= 0;
            soft_reset_valid <= 0;
            dcr_wr_valid <= 0;
        end else begin
            avs_readdatavalid <= read_fire;
            avs_readdata <= read_data_n;
            status_read <= 0;
            run_valid <= 0;
            soft_reset_valid <= 0;
            dcr_wr_valid <= 0;

            if (read_fire && (mmio_addr_aligned == MMIO_STATUS) && ~mmio_hi_word) begin
                status_latch <= status_data;
                avs_readdata <= status_data[31:0];
                status_read <= 1;
            end

            if (write_fire) begin
                case (mmio_addr_aligned)
                MMIO_CMD_ARG0: begin
                    if (mmio_hi_word) begin
                        cmd_args[0][63:32] <= apply_byteenable(cmd_args[0][63:32], avs_writedata_r, avs_byteenable_r);
                    end else begin
                        cmd_args[0][31:0] <= apply_byteenable(cmd_args[0][31:0], avs_writedata_r, avs_byteenable_r);
                    end
                end
                MMIO_CMD_ARG1: begin
                    if (mmio_hi_word) begin
                        cmd_args[1][63:32] <= apply_byteenable(cmd_args[1][63:32], avs_writedata_r, avs_byteenable_r);
                    end else begin
                        cmd_args[1][31:0] <= apply_byteenable(cmd_args[1][31:0], avs_writedata_r, avs_byteenable_r);
                    end
                end
                MMIO_CMD_ARG2: begin
                    if (mmio_hi_word) begin
                        cmd_args[2][63:32] <= apply_byteenable(cmd_args[2][63:32], avs_writedata_r, avs_byteenable_r);
                    end else begin
                        cmd_args[2][31:0] <= apply_byteenable(cmd_args[2][31:0], avs_writedata_r, avs_byteenable_r);
                    end
                end
                MMIO_CMD_TYPE: begin
                    if (~mmio_hi_word) begin
                        unique case (avs_writedata_r[2:0])
                        CMD_DCR_WRITE: begin
                            dcr_wr_valid <= 1;
                            dcr_wr_addr  <= VX_DCR_ADDR_WIDTH'(cmd_args[0][31:0]);
                            dcr_wr_data  <= VX_DCR_DATA_WIDTH'(cmd_args[1][31:0]);
                        end
                        CMD_RUN: begin
                            run_valid <= 1;
                        end
                        CMD_RESET: begin
                            soft_reset_valid <= 1;
                        end
                        default: begin
                        end
                        endcase
                    end
                end
                default: begin
                end
                endcase
            end
        end
    end

    assign avs_waitrequest = 1'b0;

endmodule
