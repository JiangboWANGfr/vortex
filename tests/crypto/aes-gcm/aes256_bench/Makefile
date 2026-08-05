ROOT_DIR := $(realpath ../../../..)
include $(ROOT_DIR)/config.mk

PROJECT := aes256_bench

SRC_DIR := $(VORTEX_HOME)/tests/crypto/aes-gcm/$(PROJECT)

# 0 = ECB encrypt, 1 = ECB decrypt,
# 2 = CBC encrypt, 3 = CBC decrypt,
# 4 = CTR encrypt, 5 = CTR decrypt,
# 6 = key expansion for encrypt, 7 = key expansion for decrypt.
CRYPTO_OP_TYPE ?= 4

# Blocks processed per Vortex hardware task. Total AES payload bytes are:
# blocks_per_task * cores * warps * threads * 16.
AES_BENCH_BLOCKS ?= 8192
OPTS ?= -n $(AES_BENCH_BLOCKS) -t $(CRYPTO_OP_TYPE)

# AES-only restored modes. HYBRID still needs the old rotate intrinsic pieces.
CRYPTO_ACCEL_MODE ?= NATIVE

SRCS := $(SRC_DIR)/main.cpp
VX_SRCS := $(SRC_DIR)/kernel.cpp $(VORTEX_HOME)/tests/kernel/aes256/aes256-impl.c $(VORTEX_HOME)/tests/kernel/aes256/tables.c

ifeq ($(CRYPTO_ACCEL_MODE),NATIVE)
  CONFIGS += -DAES_NATIVE -DEXT_AES_ENABLE
else ifeq ($(CRYPTO_ACCEL_MODE),SOFTWARE)
  CONFIGS += -DAES_SOFTWARE
else ifeq ($(CRYPTO_ACCEL_MODE),TABLE)
  CONFIGS += -DAES_TABLE
else ifeq ($(CRYPTO_ACCEL_MODE),TABLE_MONOTABLE)
  CONFIGS += -DAES_TABLE -DAES_MONOTABLE
else
  $(error unsupported CRYPTO_ACCEL_MODE=$(CRYPTO_ACCEL_MODE); supported: NATIVE SOFTWARE TABLE TABLE_MONOTABLE)
endif

VX_USE_GCC := 1

include ../../common.mk
