ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := ghash_smoke

SRC_DIR := $(VORTEX_HOME)/tests/regression/$(PROJECT)

SRCS := $(SRC_DIR)/main.cpp

VX_SRCS := $(SRC_DIR)/kernel.cpp

GHASH_ACCEL_MODE ?= SOFTWARE

ifeq ($(GHASH_ACCEL_MODE),NATIVE)
  CONFIGS += -DGHASH_NATIVE -DEXT_GHASH_ENABLE
else ifeq ($(GHASH_ACCEL_MODE),SOFTWARE)
  # pure software GF(2^128) reference; no hardware GHASH PE
else
  $(error unsupported GHASH_ACCEL_MODE=$(GHASH_ACCEL_MODE); supported: NATIVE SOFTWARE)
endif

# Match the float ABI of the prebuilt libvortex.a (rv64i_m_a, lp64 soft-float).
EXT_F_DISABLE := 1
ifeq ($(XLEN),32)
# On RV32 the prebuilt libc32 is ilp32f and will not link against the ilp32
# objects this produces; common.mk only drops libc (-lgcc alone) when BOTH
# disable flags are set.
EXT_D_DISABLE := 1
endif

VX_USE_GCC := 1

include ../common.mk
