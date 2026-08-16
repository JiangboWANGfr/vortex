// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0.

module VX_de10pro_clock_control #(
    parameter BURST_WIDTH = 5,
    parameter OUTSTANDING_WIDTH = 9,
    parameter QUIET_CYCLES = 16
) (
    input  wire                         clk,
    input  wire                         reset,

    input  wire                         clock_change_req_async,
    input  wire                         clock_reset_req_async,
    output wire                         clock_change_req,
    output wire                         clock_reset_req,
    output wire                         clock_change_ack,
    output wire                         clock_reset_ack,

    input  wire                         afu_idle,
    input  wire                         avs_read,
    input  wire                         avs_write,
    input  wire                         avs_waitrequest,
    input  wire [BURST_WIDTH-1:0]       avs_burstcount,
    input  wire                         avs_readdatavalid,
    input  wire                         ctrl_read,
    input  wire                         ctrl_write,
    input  wire                         ctrl_waitrequest,
    input  wire                         ctrl_readdatavalid,
    input  wire                         afu_reset_active
);
    localparam QUIET_WIDTH = $clog2(QUIET_CYCLES + 1);

    (* altera_attribute = "-name SYNCHRONIZER_IDENTIFICATION FORCED_IF_ASYNCHRONOUS" *)
    reg [1:0] clock_change_req_sync;
    (* altera_attribute = "-name SYNCHRONIZER_IDENTIFICATION FORCED_IF_ASYNCHRONOUS" *)
    reg [1:0] clock_reset_req_sync;

    reg [OUTSTANDING_WIDTH-1:0] outstanding_reads;
    reg                         ctrl_read_outstanding;
    reg [QUIET_WIDTH-1:0] quiet_count;

    wire read_accepted = avs_read && ~avs_waitrequest;
    wire [BURST_WIDTH-1:0] accepted_beats =
        (avs_burstcount == BURST_WIDTH'(0))
            ? BURST_WIDTH'(1) : avs_burstcount;
    wire memory_quiet = ~avs_read && ~avs_write
                     && (outstanding_reads == OUTSTANDING_WIDTH'(0));
    wire ctrl_read_accepted = ctrl_read && ~ctrl_waitrequest;
    wire control_quiet = ~ctrl_read && ~ctrl_write
                      && ~ctrl_read_outstanding;

    always @(posedge clk) begin
        if (reset) begin
            clock_change_req_sync <= '0;
            clock_reset_req_sync  <= '0;
        end else begin
            clock_change_req_sync <= {clock_change_req_sync[0],
                                      clock_change_req_async};
            clock_reset_req_sync  <= {clock_reset_req_sync[0],
                                      clock_reset_req_async};
        end
    end

    always @(posedge clk) begin
        if (reset) begin
            outstanding_reads <= '0;
        end else begin
            case ({read_accepted, avs_readdatavalid})
            2'b10: begin
                outstanding_reads <= outstanding_reads
                                   + OUTSTANDING_WIDTH'(accepted_beats);
            end
            2'b01: begin
                if (outstanding_reads != OUTSTANDING_WIDTH'(0)) begin
                    outstanding_reads <= outstanding_reads
                                       - OUTSTANDING_WIDTH'(1);
                end
            end
            2'b11: begin
                outstanding_reads <= outstanding_reads
                                   + OUTSTANDING_WIDTH'(accepted_beats)
                                   - OUTSTANDING_WIDTH'(1);
            end
            default: begin
            end
            endcase
        end
    end

    always @(posedge clk) begin
        if (reset) begin
            ctrl_read_outstanding <= 1'b0;
        end else begin
            case ({ctrl_read_accepted, ctrl_readdatavalid})
            2'b10: ctrl_read_outstanding <= 1'b1;
            2'b01: ctrl_read_outstanding <= 1'b0;
            default: begin
            end
            endcase
        end
    end

    always @(posedge clk) begin
        if (reset || ~clock_change_req_sync[1]
         || ~afu_idle || ~memory_quiet || ~control_quiet) begin
            quiet_count <= '0;
        end else if (quiet_count != QUIET_WIDTH'(QUIET_CYCLES)) begin
            quiet_count <= quiet_count + QUIET_WIDTH'(1);
        end
    end

    assign clock_change_req = clock_change_req_sync[1];
    assign clock_reset_req  = clock_reset_req_sync[1];
    assign clock_change_ack = clock_change_req_sync[1]
                           && (quiet_count == QUIET_WIDTH'(QUIET_CYCLES));
    assign clock_reset_ack = afu_reset_active;

endmodule
