ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := sha256

SRC_DIR := $(VORTEX_HOME)/tests/kernel/$(PROJECT)

SRCS := $(SRC_DIR)/main.c $(SRC_DIR)/sha256-impl.c

SHA_ACCEL_MODE ?= NATIVE

ifeq ($(SHA_ACCEL_MODE),NATIVE)
CFLAGS += -DSHA_NATIVE
else ifeq ($(SHA_ACCEL_MODE),SOFTWARE)
CFLAGS += -DSHA_SOFTWARE
else
$(error unsupported SHA_ACCEL_MODE=$(SHA_ACCEL_MODE); supported: NATIVE SOFTWARE)
endif

ifeq ($(and $(filter 1,$(EXT_F_DISABLE)),$(filter 1,$(EXT_D_DISABLE))),1)
USE_GCC := 1
endif

include ../common.mk
