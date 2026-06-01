ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := ghash_bench

SRC_DIR := $(VORTEX_HOME)/tests/regression/$(PROJECT)

SRCS := $(SRC_DIR)/main.cpp

VX_SRCS := $(SRC_DIR)/kernel.cpp

GHASH_ACCEL_MODE ?= SOFTWARE

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

VX_USE_GCC := 1

include ../common.mk
