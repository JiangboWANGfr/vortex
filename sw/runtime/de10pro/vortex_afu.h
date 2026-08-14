// Copyright © 2019-2023
// Licensed under the Apache License, Version 2.0.

#pragma once

#define VX_DE10PRO_DEFAULT_BDF         0u
#define VX_DE10PRO_DEFAULT_BAR         0u
#define VX_DE10PRO_DEFAULT_MMIO_BASE   0x1000ull
#define VX_DE10PRO_DEFAULT_STAGING     0x800000000ull
#define VX_DE10PRO_DEFAULT_CHUNK_SIZE  0x100000ull
#define VX_DE10PRO_MAX_DMA_SIZE        0x100000ull

#define AFU_IMAGE_CMD_MEM_READ         1
#define AFU_IMAGE_CMD_MEM_WRITE        2
#define AFU_IMAGE_CMD_RUN              3
#define AFU_IMAGE_CMD_DCR_WRITE        4
#define AFU_IMAGE_CMD_RESET            5
#define AFU_IMAGE_CMD_DCR_READ         6

#define AFU_IMAGE_MMIO_CMD_TYPE        0x00
#define AFU_IMAGE_MMIO_CMD_ARG0        0x08
#define AFU_IMAGE_MMIO_CMD_ARG1        0x10
#define AFU_IMAGE_MMIO_CMD_ARG2        0x18
#define AFU_IMAGE_MMIO_STATUS          0x20
#define AFU_IMAGE_MMIO_DEV_CAPS        0x28
#define AFU_IMAGE_MMIO_ISA_CAPS        0x30

#define AFU_IMAGE_DCR_READ_VALID_BIT   32
#define AFU_IMAGE_STATUS_LAUNCH_SEQ_SHIFT 32
