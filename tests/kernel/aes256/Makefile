ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := aes256

SRC_DIR := $(VORTEX_HOME)/tests/kernel/$(PROJECT)

SRCS := $(SRC_DIR)/main.cpp $(SRC_DIR)/aes256-impl.c

CFLAGS += -DAES_NATIVE

include ../common.mk

