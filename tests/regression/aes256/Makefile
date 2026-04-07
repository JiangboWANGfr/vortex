ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := aes256

SRC_DIR := $(VORTEX_HOME)/tests/regression/$(PROJECT)

SRCS := $(SRC_DIR)/main.cpp

VX_SRCS := $(SRC_DIR)/kernel.cpp $(VORTEX_HOME)/tests/kernel/aes256/aes256-impl.c

CONFIGS += -DAES_NATIVE
VX_USE_GCC := 1

include ../common.mk
