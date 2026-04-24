ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := aes_smoke

SRC_DIR := $(VORTEX_HOME)/tests/kernel/$(PROJECT)

SRCS := $(SRC_DIR)/main.cpp

CONFIGS += -DEXT_AES_ENABLE

ifeq ($(and $(filter 1,$(EXT_F_DISABLE)),$(filter 1,$(EXT_D_DISABLE))),1)
USE_GCC := 1
endif

include ../common.mk
