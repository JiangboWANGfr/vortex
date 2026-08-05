ROOT_DIR := $(realpath ../../../..)
include $(ROOT_DIR)/config.mk

PROJECT := aes_smoke

SRC_DIR := $(VORTEX_HOME)/tests/crypto/aes-gcm/$(PROJECT)

SRCS := $(SRC_DIR)/main.cpp

VX_SRCS := $(SRC_DIR)/kernel.cpp

CONFIGS += -DEXT_AES_ENABLE
VX_USE_GCC := 1

include ../../common.mk
