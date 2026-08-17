ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := aes_gcm

SRC_DIR := $(VORTEX_HOME)/tests/crypto/$(PROJECT)

SRCS := $(SRC_DIR)/main.cpp

VX_SRCS := $(SRC_DIR)/kernel.cpp

OPTS ?= -n64 -b4

KERNEL_LIB := vortex2

include ../common.mk
