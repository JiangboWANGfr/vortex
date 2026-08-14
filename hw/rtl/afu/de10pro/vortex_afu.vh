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

`ifndef VORTEX_DE10PRO_AFU_VH
`define VORTEX_DE10PRO_AFU_VH

`define VX_DE10PRO_DEFAULT_BAR         0
`define VX_DE10PRO_DEFAULT_MMIO_BASE   64'h0000_0000_0000_1000
`define VX_DE10PRO_DEFAULT_STAGING     64'h0000_0008_0000_0000
`define VX_DE10PRO_DEFAULT_CHUNK_SIZE  64'h0000_0000_0010_0000

`define AFU_IMAGE_CMD_RUN              4'd3
`define AFU_IMAGE_CMD_DCR_WRITE        4'd4
`define AFU_IMAGE_CMD_RESET            4'd5
`define AFU_IMAGE_CMD_DCR_READ         4'd6

`define AFU_IMAGE_MMIO_CMD_TYPE        8'h00
`define AFU_IMAGE_MMIO_CMD_ARG0        8'h08
`define AFU_IMAGE_MMIO_CMD_ARG1        8'h10
`define AFU_IMAGE_MMIO_CMD_ARG2        8'h18
`define AFU_IMAGE_MMIO_STATUS          8'h20
`define AFU_IMAGE_MMIO_DEV_CAPS        8'h28
`define AFU_IMAGE_MMIO_ISA_CAPS        8'h30

`define AFU_IMAGE_STATUS_LAUNCH_SEQ_SHIFT 32

`endif
