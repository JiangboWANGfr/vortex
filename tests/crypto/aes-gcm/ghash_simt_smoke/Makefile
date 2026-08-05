ROOT_DIR := $(realpath ../../../..)
include $(ROOT_DIR)/config.mk

PROJECT := ghash_simt_smoke

SRC_DIR := $(VORTEX_HOME)/tests/crypto/aes-gcm/$(PROJECT)

SRCS := $(SRC_DIR)/main.cpp

VX_SRCS := $(SRC_DIR)/kernel.cpp

# Same knob as ghash_smoke: the shared ghash_ref.h dispatches on GHASH_NATIVE,
# so NATIVE here runs one independent per-lane GHASH chain per SIMT thread.
GHASH_ACCEL_MODE ?= SOFTWARE

ifeq ($(GHASH_ACCEL_MODE),NATIVE)
  CONFIGS += -DGHASH_NATIVE -DEXT_GHASH_ENABLE
else ifeq ($(GHASH_ACCEL_MODE),SOFTWARE)
  # pure software GF(2^128) reference; no hardware GHASH PE
else
  $(error unsupported GHASH_ACCEL_MODE=$(GHASH_ACCEL_MODE); supported: NATIVE SOFTWARE)
endif

# Match the prebuilt libvortex.a ABI (rv64i_m_a, lp64 soft-float).
EXT_F_DISABLE := 1
ifeq ($(XLEN),32)
# On RV32 the prebuilt libc32 is ilp32f and will not link against the ilp32
# objects this produces; common.mk only drops libc (-lgcc alone) when BOTH
# disable flags are set.
EXT_D_DISABLE := 1
endif

VX_USE_GCC := 1

include ../../common.mk
