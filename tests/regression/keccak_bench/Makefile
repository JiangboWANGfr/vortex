ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := keccak_bench

SRC_DIR := $(VORTEX_HOME)/tests/regression/$(PROJECT)

KECCAK_BENCH_MESSAGES ?= 1
KECCAK_BENCH_CASES ?= 256
KECCAK_BENCH_DATASET ?= byte-long
OPTS ?= -n $(KECCAK_BENCH_MESSAGES) -c $(KECCAK_BENCH_CASES) -d $(KECCAK_BENCH_DATASET)

KECCAK_ACCEL_MODE ?= NATIVE

SRCS := $(SRC_DIR)/main.cpp
VX_SRCS := $(SRC_DIR)/kernel.cpp $(VORTEX_HOME)/tests/kernel/keccak/keccak-impl.c

ifeq ($(KECCAK_ACCEL_MODE),NATIVE)
  CONFIGS += -DKECCAK_NATIVE -DEXT_KECCAK_ENABLE
else ifeq ($(KECCAK_ACCEL_MODE),SOFTWARE)
  CONFIGS += -DKECCAK_SOFTWARE
else
  $(error unsupported KECCAK_ACCEL_MODE=$(KECCAK_ACCEL_MODE); supported: NATIVE SOFTWARE)
endif

VX_USE_GCC := 1

include ../common.mk
