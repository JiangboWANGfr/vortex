ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

all:
	$(MAKE) -C chacha20poly1305_bench
	$(MAKE) -C chacha20poly1305_smoke

run-simx:
	$(MAKE) -C chacha20poly1305_bench run-simx
	$(MAKE) -C chacha20poly1305_smoke run-simx

run-rtlsim:
	$(MAKE) -C chacha20poly1305_bench run-rtlsim
	$(MAKE) -C chacha20poly1305_smoke run-rtlsim

clean:
	$(MAKE) -C chacha20poly1305_bench clean
	$(MAKE) -C chacha20poly1305_smoke clean

.PHONY: all run-simx run-rtlsim clean
