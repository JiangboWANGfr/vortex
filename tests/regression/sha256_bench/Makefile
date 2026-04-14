ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := sha256_bench

SRC_DIR := $(VORTEX_HOME)/tests/regression/$(PROJECT)

SHA_BENCH_MESSAGES ?= 1
SHA_BENCH_MSG_BYTES ?= 128
OPTS ?= -n $(SHA_BENCH_MESSAGES) -m $(SHA_BENCH_MSG_BYTES)

SHA_ACCEL_MODE ?= NATIVE

SRCS := $(SRC_DIR)/main.cpp
VX_SRCS := $(SRC_DIR)/kernel.cpp $(VORTEX_HOME)/tests/kernel/sha256/sha256-impl.c

ifeq ($(SHA_ACCEL_MODE),NATIVE)
  CONFIGS += -DSHA_NATIVE
else ifeq ($(SHA_ACCEL_MODE),SOFTWARE)
  CONFIGS += -DSHA_SOFTWARE
else
  $(error unsupported SHA_ACCEL_MODE=$(SHA_ACCEL_MODE); supported: NATIVE SOFTWARE)
endif

VX_USE_GCC := 1

include ../common.mk
