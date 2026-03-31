ROOT_DIR := $(realpath ../..)
include ../common.mk

DESTDIR ?= $(CURDIR)/..

CONFIG_FILE := $(DESTDIR)/de10pro_config.stamp
SRC_DIR := $(VORTEX_HOME)/runtime/de10pro
TERASIC_PCIE_LIB_DIR ?= $(abspath $(ROOT_DIR)/../PCIe_SW_KIT/Linux/PCIe_Library)

CXXFLAGS += -std=c++17 -Wall -Wextra -pedantic -Wfatal-errors
CXXFLAGS += -I$(INC_DIR) -I$(ROOT_DIR)/hw -I$(DESTDIR) -I$(SW_COMMON_DIR) -I$(RT_COMMON_DIR)
CXXFLAGS += -I$(TERASIC_PCIE_LIB_DIR)
CXXFLAGS += -I$(SRC_DIR)
CXXFLAGS += -DXLEN_$(XLEN)
CXXFLAGS += -fPIC
CXXFLAGS += $(CONFIGS)

LDFLAGS += -shared -ldl -pthread

SRCS = $(SRC_DIR)/vortex.cpp $(SRC_DIR)/driver.cpp

ifdef DEBUG
	CXXFLAGS += -g -O0
else
	CXXFLAGS += -O2 -DNDEBUG
endif

ifdef SCOPE
	CXXFLAGS += -DSCOPE
	SRCS += $(RT_COMMON_DIR)/scope.cpp
endif

PROJECT := libvortex-de10pro.so

.PHONY: all force clean

all: $(DESTDIR)/$(PROJECT)

$(DESTDIR)/$(PROJECT): $(SRCS) $(CONFIG_FILE)
	$(CXX) $(CXXFLAGS) $(SRCS) $(LDFLAGS) -o $@

$(CONFIG_FILE): force
	@mkdir -p $(@D)
	@printf '%s\n' "$(CXXFLAGS)" > $@.tmp
	@if ! cmp -s $@.tmp $@; then \
	  mv $@.tmp $@; \
	else \
	  rm $@.tmp; \
	fi

clean:
	rm -f $(DESTDIR)/$(PROJECT) $(CONFIG_FILE)
