ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := keccak

SRC_DIR := $(VORTEX_HOME)/tests/kernel/$(PROJECT)

SRCS := $(SRC_DIR)/main.c $(SRC_DIR)/keccak-impl.c

ifeq ($(and $(filter 1,$(EXT_F_DISABLE)),$(filter 1,$(EXT_D_DISABLE))),1)
USE_GCC := 1
endif

include ../common.mk
