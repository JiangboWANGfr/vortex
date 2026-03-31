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

#ifndef VORTEX_DE10PRO_AFU_H
#define VORTEX_DE10PRO_AFU_H

#define VX_DE10PRO_DEFAULT_BAR        4
#define VX_DE10PRO_DEFAULT_MMIO_BASE  0x4001000ull
#define VX_DE10PRO_DEFAULT_STAGING    0x800000000ull
#define VX_DE10PRO_DEFAULT_CHUNK_SIZE 0x200000ull

#define AFU_IMAGE_CMD_MEM_READ        1
#define AFU_IMAGE_CMD_MEM_WRITE       2
#define AFU_IMAGE_CMD_RUN             3
#define AFU_IMAGE_CMD_DCR_WRITE       4

#define AFU_IMAGE_MMIO_CMD_TYPE       0x00
#define AFU_IMAGE_MMIO_CMD_ARG0       0x08
#define AFU_IMAGE_MMIO_CMD_ARG1       0x10
#define AFU_IMAGE_MMIO_CMD_ARG2       0x18
#define AFU_IMAGE_MMIO_STATUS         0x20
#define AFU_IMAGE_MMIO_DEV_CAPS       0x28
#define AFU_IMAGE_MMIO_ISA_CAPS       0x30
#define AFU_IMAGE_MMIO_SCOPE_READ     0x38
#define AFU_IMAGE_MMIO_SCOPE_WRITE    0x40

#endif
