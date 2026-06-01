ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := gcm_bench

SRC_DIR := $(VORTEX_HOME)/tests/regression/$(PROJECT)

SRCS := $(SRC_DIR)/main.cpp

GCM_ACCEL_MODE ?= NATIVE
GCM_BENCH_DISPATCH ?= WARP

ifeq ($(GCM_BENCH_DISPATCH),LANE)
  CONFIGS += -DGCM_BENCH_DISPATCH_LANE
else ifeq ($(GCM_BENCH_DISPATCH),WARP)
else
  $(error unsupported GCM_BENCH_DISPATCH=$(GCM_BENCH_DISPATCH); supported: WARP LANE)
endif

ifeq ($(GCM_ACCEL_MODE),UNPROTECTED)
  # baseline: move the data only, no crypto -> no AES/GHASH sources
  CONFIGS += -DGCM_BENCH_UNPROTECTED
  VX_SRCS := $(SRC_DIR)/kernel.cpp
else ifeq ($(GCM_ACCEL_MODE),SOFTWARE)
  VX_SRCS := $(SRC_DIR)/kernel.cpp $(VORTEX_HOME)/tests/kernel/aes256/aes256-impl.c
else ifeq ($(GCM_ACCEL_MODE),NATIVE)
  CONFIGS += -DGCM_NATIVE -DAES_NATIVE -DGHASH_NATIVE -DEXT_AES_ENABLE -DEXT_GHASH_ENABLE
  VX_SRCS := $(SRC_DIR)/kernel.cpp $(VORTEX_HOME)/tests/kernel/aes256/aes256-impl.c
else
  $(error unsupported GCM_ACCEL_MODE=$(GCM_ACCEL_MODE); supported: UNPROTECTED SOFTWARE NATIVE)
endif

# Match the float ABI of the prebuilt libvortex.a (rv64ima, lp64 soft-float).
EXT_F_DISABLE := 1

VX_USE_GCC := 1

include ../common.mk
