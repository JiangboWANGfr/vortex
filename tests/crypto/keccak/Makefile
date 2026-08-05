ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

all:
	$(MAKE) -C keccak_bench
	$(MAKE) -C keccak_smoke

run-simx:
	$(MAKE) -C keccak_bench run-simx
	$(MAKE) -C keccak_smoke run-simx

run-rtlsim:
	$(MAKE) -C keccak_bench run-rtlsim
	$(MAKE) -C keccak_smoke run-rtlsim

clean:
	$(MAKE) -C keccak_bench clean
	$(MAKE) -C keccak_smoke clean

.PHONY: all run-simx run-rtlsim clean
