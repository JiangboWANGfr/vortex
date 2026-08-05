ROOT_DIR := $(realpath ../../../..)
include $(ROOT_DIR)/config.mk

PROJECT := ghash_bench

SRC_DIR := $(VORTEX_HOME)/tests/crypto/aes-gcm/$(PROJECT)

SRCS := $(SRC_DIR)/main.cpp

VX_SRCS := $(SRC_DIR)/kernel.cpp

GHASH_ACCEL_MODE ?= SOFTWARE
GHASH_BENCH_DISPATCH ?= WARP

ifeq ($(GHASH_BENCH_DISPATCH),LANE)
  CONFIGS += -DGHASH_BENCH_DISPATCH_LANE
else ifeq ($(GHASH_BENCH_DISPATCH),WARP)
  # one chain per warp (one active thread)
else
  $(error unsupported GHASH_BENCH_DISPATCH=$(GHASH_BENCH_DISPATCH); supported: WARP LANE)
endif

# Data layout: TASK_MAJOR (default, each stream contiguous) vs INTERLEAVED
# (block-major: same block index of all streams adjacent, so a warp's lanes
# coalesce into one cache line). Must reach both host and kernel.
GHASH_BENCH_LAYOUT ?= TASK_MAJOR
ifeq ($(GHASH_BENCH_LAYOUT),INTERLEAVED)
  CONFIGS += -DGHASH_BENCH_INTERLEAVE
else ifeq ($(GHASH_BENCH_LAYOUT),TASK_MAJOR)
  # streams stored contiguously
else
  $(error unsupported GHASH_BENCH_LAYOUT=$(GHASH_BENCH_LAYOUT); supported: TASK_MAJOR INTERLEAVED)
endif

ifeq ($(GHASH_ACCEL_MODE),NATIVE)
  # GHASH_NATIVE must reach only the kernel (VX_CFLAGS); the x86 host computes
  # expected tags with the software reference and cannot emit RISC-V intrinsics.
  VX_CFLAGS += -DGHASH_NATIVE
  # EXT_GHASH_ENABLE goes to both (harmless on host) so the simulator builds the PE.
  CONFIGS += -DEXT_GHASH_ENABLE
else ifeq ($(GHASH_ACCEL_MODE),SOFTWARE)
  # pure software GF(2^128) reference
else
  $(error unsupported GHASH_ACCEL_MODE=$(GHASH_ACCEL_MODE); supported: NATIVE SOFTWARE)
endif

# Match the float ABI of the prebuilt libvortex.a (rv64ima, lp64 soft-float).
EXT_F_DISABLE := 1
ifeq ($(XLEN),32)
# On RV32 the prebuilt libc32 is ilp32f and will not link against the ilp32
# objects this produces; common.mk only drops libc (-lgcc alone) when BOTH
# disable flags are set.
EXT_D_DISABLE := 1
endif

VX_USE_GCC := 1

include ../../common.mk
