ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := keccak

SRC_DIR := $(VORTEX_HOME)/tests/kernel/$(PROJECT)

SRCS := $(SRC_DIR)/main.c $(SRC_DIR)/keccak-impl.c

KECCAK_ACCEL_MODE ?= SOFTWARE

ifeq ($(KECCAK_ACCEL_MODE),NATIVE)
CFLAGS += -DKECCAK_NATIVE
CONFIGS += -DEXT_KECCAK_ENABLE
else ifeq ($(KECCAK_ACCEL_MODE),SOFTWARE)
CFLAGS += -DKECCAK_SOFTWARE
else
$(error unsupported KECCAK_ACCEL_MODE=$(KECCAK_ACCEL_MODE))
endif

ifeq ($(and $(filter 1,$(EXT_F_DISABLE)),$(filter 1,$(EXT_D_DISABLE))),1)
USE_GCC := 1
endif

include ../common.mk
