ROOT_DIR := $(realpath ../../../..)
include $(ROOT_DIR)/config.mk

PROJECT := chacha20poly1305_bench

SRC_DIR := $(VORTEX_HOME)/tests/crypto/chacha-poly/$(PROJECT)

SRCS := $(SRC_DIR)/main.cpp

VX_SRCS := $(SRC_DIR)/kernel.cpp

# AEAD backend: UNPROTECTED (move-only baseline), SOFTWARE (pure ARX+donna), or
# NATIVE (hardware ChaCha PE + hardware Poly1305 PE).
CHACHA20POLY1305_ACCEL_MODE ?= NATIVE

# Stream parallelism: one AEAD stream per warp (WARP) or per thread (LANE).
CHACHA_BENCH_DISPATCH ?= WARP

# Compute-isolating mode: loop the ChaCha20 block function over a fixed 64-byte
# keystream (no DRAM per iter, no Poly1305) so the CHACHA_QR_RADIX sweep exercises
# the quarter-round datapath, not memory bandwidth. OFF = streaming AEAD overhead.
CHACHA_BENCH_COMPUTE_ONLY ?= 0
CHACHA_BENCH_ITERS ?= 256

ifeq ($(CHACHA_BENCH_DISPATCH),LANE)
  CONFIGS += -DCHACHA20POLY1305_BENCH_DISPATCH_LANE
else ifeq ($(CHACHA_BENCH_DISPATCH),WARP)
else
  $(error unsupported CHACHA_BENCH_DISPATCH=$(CHACHA_BENCH_DISPATCH); supported: WARP LANE)
endif

ifeq ($(CHACHA_BENCH_COMPUTE_ONLY),1)
  CONFIGS += -DCHACHA_BENCH_COMPUTE_ONLY -DCHACHA_BENCH_ITERS=$(CHACHA_BENCH_ITERS)
endif

ifeq ($(CHACHA20POLY1305_ACCEL_MODE),UNPROTECTED)
  CONFIGS += -DCHACHA20POLY1305_BENCH_UNPROTECTED
else ifeq ($(CHACHA20POLY1305_ACCEL_MODE),SOFTWARE)
  # pure software ChaCha20-Poly1305; no crypto PE
else ifeq ($(CHACHA20POLY1305_ACCEL_MODE),NATIVE)
  CONFIGS += -DCHACHA20_NATIVE -DPOLY1305_NATIVE -DEXT_CHACHA_ENABLE -DEXT_POLY1305_ENABLE
else
  $(error unsupported CHACHA20POLY1305_ACCEL_MODE=$(CHACHA20POLY1305_ACCEL_MODE); supported: UNPROTECTED SOFTWARE NATIVE)
endif

# Match the float ABI of the prebuilt libvortex.a (rv64ima, lp64 soft-float).
EXT_F_DISABLE := 1
ifeq ($(XLEN),32)
# On RV32 the prebuilt libc32 is ilp32f and will not link against the ilp32
# objects this produces; common.mk only drops libc (-lgcc alone) when BOTH
# disable flags are set.
EXT_D_DISABLE := 1
endif

VX_USE_GCC := 1

include ../../common.mk
