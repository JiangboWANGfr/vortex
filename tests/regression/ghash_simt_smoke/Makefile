ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := ghash_simt_smoke

SRC_DIR := $(VORTEX_HOME)/tests/regression/$(PROJECT)

SRCS := $(SRC_DIR)/main.cpp

VX_SRCS := $(SRC_DIR)/kernel.cpp

# Match the prebuilt libvortex.a ABI (rv64i_m_a, lp64 soft-float).
EXT_F_DISABLE := 1
ifeq ($(XLEN),32)
# On RV32 the prebuilt libc32 is ilp32f and will not link against the ilp32
# objects this produces; common.mk only drops libc (-lgcc alone) when BOTH
# disable flags are set.
EXT_D_DISABLE := 1
endif

VX_USE_GCC := 1

include ../common.mk
