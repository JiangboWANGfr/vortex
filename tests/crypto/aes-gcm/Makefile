ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

all:
	$(MAKE) -C aes256
	$(MAKE) -C aes256_bench
	$(MAKE) -C aes_gcm_smoke
	$(MAKE) -C aes_smoke
	$(MAKE) -C gcm_bench
	$(MAKE) -C ghash_bench
	$(MAKE) -C ghash_simt_smoke
	$(MAKE) -C ghash_smoke

run-simx:
	$(MAKE) -C aes256 run-simx
	$(MAKE) -C aes256_bench run-simx
	$(MAKE) -C aes_gcm_smoke run-simx
	$(MAKE) -C aes_smoke run-simx
	$(MAKE) -C gcm_bench run-simx
	$(MAKE) -C ghash_bench run-simx
	$(MAKE) -C ghash_simt_smoke run-simx
	$(MAKE) -C ghash_smoke run-simx

run-rtlsim:
	$(MAKE) -C aes256 run-rtlsim
	$(MAKE) -C aes256_bench run-rtlsim
	$(MAKE) -C aes_gcm_smoke run-rtlsim
	$(MAKE) -C aes_smoke run-rtlsim
	$(MAKE) -C gcm_bench run-rtlsim
	$(MAKE) -C ghash_bench run-rtlsim
	$(MAKE) -C ghash_simt_smoke run-rtlsim
	$(MAKE) -C ghash_smoke run-rtlsim

clean:
	$(MAKE) -C aes256 clean
	$(MAKE) -C aes256_bench clean
	$(MAKE) -C aes_gcm_smoke clean
	$(MAKE) -C aes_smoke clean
	$(MAKE) -C gcm_bench clean
	$(MAKE) -C ghash_bench clean
	$(MAKE) -C ghash_simt_smoke clean
	$(MAKE) -C ghash_smoke clean

.PHONY: all run-simx run-rtlsim clean
