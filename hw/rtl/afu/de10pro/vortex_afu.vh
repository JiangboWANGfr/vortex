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

`ifndef VORTEX_DE10PRO_AFU_VH
`define VORTEX_DE10PRO_AFU_VH

`define VX_DE10PRO_DEFAULT_BAR         4
`define VX_DE10PRO_DEFAULT_MMIO_BASE   64'h0000_0000_0400_1000
`define VX_DE10PRO_DEFAULT_STAGING     64'h0000_0008_0000_0000
`define VX_DE10PRO_DEFAULT_CHUNK_SIZE  64'h0000_0000_0020_0000

`define AFU_IMAGE_CMD_MEM_READ         1
`define AFU_IMAGE_CMD_MEM_WRITE        2
`define AFU_IMAGE_CMD_RUN              3
`define AFU_IMAGE_CMD_DCR_WRITE        4
`define AFU_IMAGE_CMD_RESET            5
`define AFU_IMAGE_CMD_MAX_VALUE        5

`define AFU_IMAGE_MMIO_CMD_TYPE        32'h0000
`define AFU_IMAGE_MMIO_CMD_ARG0        32'h0008
`define AFU_IMAGE_MMIO_CMD_ARG1        32'h0010
`define AFU_IMAGE_MMIO_CMD_ARG2        32'h0018
`define AFU_IMAGE_MMIO_STATUS          32'h0020
`define AFU_IMAGE_MMIO_DEV_CAPS        32'h0028
`define AFU_IMAGE_MMIO_ISA_CAPS        32'h0030
`define AFU_IMAGE_MMIO_SCOPE_READ      32'h0038
`define AFU_IMAGE_MMIO_SCOPE_WRITE     32'h0040

`endif
