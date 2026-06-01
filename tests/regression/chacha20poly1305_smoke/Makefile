ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := chacha20poly1305_smoke

SRC_DIR := $(VORTEX_HOME)/tests/regression/$(PROJECT)

SRCS := $(SRC_DIR)/main.cpp

VX_SRCS := $(SRC_DIR)/kernel.cpp

# Pure software ChaCha20-Poly1305 reference (no crypto PE). Match the prebuilt
# libvortex.a float ABI (rv64ima, lp64 soft-float).
EXT_F_DISABLE := 1

VX_USE_GCC := 1

include ../common.mk
