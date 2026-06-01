ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := chacha20poly1305_smoke

SRC_DIR := $(VORTEX_HOME)/tests/regression/$(PROJECT)

SRCS := $(SRC_DIR)/main.cpp

VX_SRCS := $(SRC_DIR)/kernel.cpp

# Poly1305 MAC backend: SOFTWARE (pure donna-32) or NATIVE (hardware PE).
POLY1305_ACCEL_MODE ?= SOFTWARE

ifeq ($(POLY1305_ACCEL_MODE),NATIVE)
  CONFIGS += -DPOLY1305_NATIVE -DEXT_POLY1305_ENABLE
else ifeq ($(POLY1305_ACCEL_MODE),SOFTWARE)
  # pure software Poly1305; no hardware Poly1305 PE
else
  $(error unsupported POLY1305_ACCEL_MODE=$(POLY1305_ACCEL_MODE); supported: NATIVE SOFTWARE)
endif

# ChaCha20 cipher backend: SOFTWARE (pure ARX) or NATIVE (hardware PE).
CHACHA20_ACCEL_MODE ?= SOFTWARE

ifeq ($(CHACHA20_ACCEL_MODE),NATIVE)
  CONFIGS += -DCHACHA20_NATIVE -DEXT_CHACHA_ENABLE
else ifeq ($(CHACHA20_ACCEL_MODE),SOFTWARE)
  # pure software ChaCha20; no hardware ChaCha PE
else
  $(error unsupported CHACHA20_ACCEL_MODE=$(CHACHA20_ACCEL_MODE); supported: NATIVE SOFTWARE)
endif

# Pure software ChaCha20-Poly1305 reference (no crypto PE). Match the prebuilt
# libvortex.a float ABI (rv64ima, lp64 soft-float).
EXT_F_DISABLE := 1

VX_USE_GCC := 1

include ../common.mk
