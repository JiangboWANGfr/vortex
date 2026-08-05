ROOT_DIR := $(realpath ..)
include $(ROOT_DIR)/config.mk

all: unittest kernel regression crypto opencl riscv

unittest:
	$(MAKE) -C unittest

kernel:
	$(MAKE) -C kernel

regression:
	$(MAKE) -C regression

crypto:
	$(MAKE) -C crypto

opencl:
	$(MAKE) -C opencl

riscv:
	$(MAKE) -C riscv

clean:
	$(MAKE) -C unittest clean
	$(MAKE) -C kernel clean
	$(MAKE) -C regression clean
	$(MAKE) -C crypto clean
	$(MAKE) -C opencl clean
	$(MAKE) -C riscv clean

.PHONY: all unittest kernel regression crypto opencl riscv