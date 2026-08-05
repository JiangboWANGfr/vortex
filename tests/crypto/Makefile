ROOT_DIR := $(realpath ../..)
include $(ROOT_DIR)/config.mk

all:
	$(MAKE) -C aes-gcm
	$(MAKE) -C chacha-poly
	$(MAKE) -C keccak

run-simx:
	$(MAKE) -C aes-gcm run-simx
	$(MAKE) -C chacha-poly run-simx
	$(MAKE) -C keccak run-simx

run-rtlsim:
	$(MAKE) -C aes-gcm run-rtlsim
	$(MAKE) -C chacha-poly run-rtlsim
	$(MAKE) -C keccak run-rtlsim

clean:
	$(MAKE) -C aes-gcm clean
	$(MAKE) -C chacha-poly clean
	$(MAKE) -C keccak clean

.PHONY: all run-simx run-rtlsim clean aes-gcm chacha-poly keccak
