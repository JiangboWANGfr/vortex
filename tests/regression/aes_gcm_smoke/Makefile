ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := aes_gcm_smoke

SRC_DIR := $(VORTEX_HOME)/tests/regression/$(PROJECT)

SRCS := $(SRC_DIR)/main.cpp

VX_SRCS := $(SRC_DIR)/kernel.cpp $(VORTEX_HOME)/tests/kernel/aes256/aes256-impl.c

GCM_ACCEL_MODE ?= SOFTWARE

ifeq ($(GCM_ACCEL_MODE),NATIVE)
  # Hardware AES rounds + hardware GHASH. AES_NATIVE/GHASH_NATIVE must reach
  # both aes256-impl.c and the kernel; EXT_* build the PEs into the simulator.
  CONFIGS += -DGCM_NATIVE -DAES_NATIVE -DGHASH_NATIVE -DEXT_AES_ENABLE -DEXT_GHASH_ENABLE
else ifeq ($(GCM_ACCEL_MODE),SOFTWARE)
  # pure software AES-256 + GF(2^128)
else
  $(error unsupported GCM_ACCEL_MODE=$(GCM_ACCEL_MODE); supported: NATIVE SOFTWARE)
endif

# Match the float ABI of the prebuilt libvortex.a (rv64ima, lp64 soft-float).
EXT_F_DISABLE := 1

VX_USE_GCC := 1

include ../common.mk
