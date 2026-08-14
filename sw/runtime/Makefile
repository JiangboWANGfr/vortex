ROOT_DIR := $(realpath ../..)
include $(ROOT_DIR)/config.mk

all: stub rtlsim simx opae xrt

vm: stub simx

stub:
	$(MAKE) -C stub

simx:
	$(MAKE) -C simx

rtlsim:
	$(MAKE) -C rtlsim

opae:
	$(MAKE) -C opae

xrt:
	$(MAKE) -C xrt

de10pro:
	$(MAKE) -C de10pro

clean:
	$(MAKE) -C stub clean
	$(MAKE) -C simx clean
	$(MAKE) -C rtlsim clean
	$(MAKE) -C opae clean
	$(MAKE) -C xrt clean
	$(MAKE) -C de10pro clean

.PHONY: all stub simx rtlsim opae xrt de10pro clean
