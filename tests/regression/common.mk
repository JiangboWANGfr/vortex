ROOT_DIR := $(realpath ../../..)

TARGET ?= opaesim
VX_USE_GCC ?= 0

XRT_SYN_DIR ?= $(VORTEX_HOME)/hw/syn/xilinx/xrt
XRT_DEVICE_INDEX ?= 0

VORTEX_RT_PATH ?= $(ROOT_DIR)/runtime
VORTEX_KN_PATH ?= $(ROOT_DIR)/kernel

ifeq ($(XLEN),64)
	ifeq ($(EXT_D_DISABLE),1)
		VX_CFLAGS += -DVX_NO_LIBC_RUNTIME -DPRINTF_DISABLE_SUPPORT_FLOAT
	endif
	ifeq ($(EXT_V_ENABLE),1)
		ifeq ($(EXT_F_DISABLE),1)
			VX_CFLAGS += -march=rv64imav_zve64x_zicsr -mabi=lp64 # vector extension
		else ifeq ($(EXT_D_DISABLE),1)
			VX_CFLAGS += -march=rv64imafv_zve64f_zicsr -mabi=lp64f # vector extension
		else
			VX_CFLAGS += -march=rv64imafdv_zve64d_zicsr -mabi=lp64d # vector extension
		endif
	else
		ifeq ($(EXT_F_DISABLE),1)
			VX_CFLAGS += -march=rv64ima_zicsr -mabi=lp64
		else ifeq ($(EXT_D_DISABLE),1)
			VX_CFLAGS += -march=rv64imaf_zicsr -mabi=lp64f
		else
			VX_CFLAGS += -march=rv64imafd_zicsr -mabi=lp64d
		endif
	endif
	STARTUP_ADDR ?= 0x180000000
else
	ifeq ($(EXT_V_ENABLE),1)
		ifeq ($(EXT_F_DISABLE),1)
			VX_CFLAGS += -march=rv32imav_zve32x_zicsr -mabi=ilp32 # vector extension
		else
			VX_CFLAGS += -march=rv32imafv_zve32f_zicsr -mabi=ilp32f # vector extension
		endif
	else
		ifeq ($(EXT_F_DISABLE),1)
			VX_CFLAGS += -march=rv32ima_zicsr -mabi=ilp32
		else
			VX_CFLAGS += -march=rv32imaf_zicsr -mabi=ilp32f
		endif
	endif
	STARTUP_ADDR ?= 0x80000000
endif

LLVM_CFLAGS += --sysroot=$(RISCV_SYSROOT)
LLVM_CFLAGS += --gcc-toolchain=$(RISCV_TOOLCHAIN_PATH)
LLVM_CFLAGS += -Xclang -target-feature -Xclang +vortex
LLVM_CFLAGS += -Xclang -target-feature -Xclang +zicond
LLVM_CFLAGS += -mllvm -disable-loop-idiom-all # disable memset/memcpy loop idiom
#LLVM_CFLAGS += -mllvm -vortex-branch-divergence=0
#LLVM_CFLAGS += -mllvm -debug -mllvm -print-after-all
#LLVM_CFLAGS += -I$(RISCV_SYSROOT)/include/c++/9.2.0/$(RISCV_PREFIX)
#LLVM_CFLAGS += -I$(RISCV_SYSROOT)/include/c++/9.2.0
#LLVM_CFLAGS += -Wl,-L$(RISCV_TOOLCHAIN_PATH)/lib/gcc/$(RISCV_PREFIX)/9.2.0
#LLVM_CFLAGS += --rtlib=libgcc

ifeq ($(VX_USE_GCC),1)
VX_CC  = $(RISCV_TOOLCHAIN_PATH)/bin/$(RISCV_PREFIX)-gcc
VX_CXX = $(RISCV_TOOLCHAIN_PATH)/bin/$(RISCV_PREFIX)-g++
VX_DP  = $(RISCV_TOOLCHAIN_PATH)/bin/$(RISCV_PREFIX)-objdump
VX_CP  = $(RISCV_TOOLCHAIN_PATH)/bin/$(RISCV_PREFIX)-objcopy
else
VX_CC  = $(LLVM_VORTEX)/bin/clang $(LLVM_CFLAGS)
VX_CXX = $(LLVM_VORTEX)/bin/clang++ $(LLVM_CFLAGS)
VX_DP  = $(LLVM_VORTEX)/bin/llvm-objdump
VX_CP  = $(LLVM_VORTEX)/bin/llvm-objcopy
endif

VX_CFLAGS += -O3 -mcmodel=medany -fno-rtti -fno-exceptions -nostartfiles -nostdlib -fdata-sections -ffunction-sections
VX_CFLAGS += -I$(VORTEX_HOME)/kernel/include -I$(ROOT_DIR)/hw -I$(SW_COMMON_DIR)
VX_CFLAGS += -DXLEN_$(XLEN)
VX_CFLAGS += -DNDEBUG
VX_CFLAGS += $(CONFIGS)

ifeq ($(XLEN),64)
	ifeq ($(EXT_D_DISABLE),1)
		VX_LIBS += -lgcc
	else
		VX_LIBS += -L$(LIBC_VORTEX)/lib -lm -lc
		VX_LIBS += $(LIBCRT_VORTEX)/lib/baremetal/libclang_rt.builtins-riscv$(XLEN).a
	endif
else
	ifeq ($(and $(filter 1,$(EXT_F_DISABLE)),$(filter 1,$(EXT_D_DISABLE))),1)
		VX_LIBS += -lgcc
	else
		VX_LIBS += -L$(LIBC_VORTEX)/lib -lm -lc
		VX_LIBS += -lgcc
	endif
endif
	#VX_LIBS += -lgcc

VX_LDFLAGS += -Wl,-Bstatic,--gc-sections,-T,$(VORTEX_HOME)/kernel/scripts/link$(XLEN).ld,--defsym=STARTUP_ADDR=$(STARTUP_ADDR) $(VORTEX_KN_PATH)/libvortex.a $(VX_LIBS)

CXXFLAGS += -std=c++17 -Wall -Wextra -pedantic -Wfatal-errors
CXXFLAGS += -I$(VORTEX_HOME)/runtime/include -I$(ROOT_DIR)/hw -I$(SW_COMMON_DIR)
CXXFLAGS += $(CONFIGS)

LDFLAGS += -L$(VORTEX_RT_PATH) -lvortex

# Debugging
ifdef DEBUG
	CXXFLAGS += -g -O0
else
	CXXFLAGS += -O2 -DNDEBUG
endif

ifeq ($(TARGET), fpga)
	OPAE_DRV_PATHS ?= libopae-c.so
else
ifeq ($(TARGET), asesim)
	OPAE_DRV_PATHS ?= libopae-c-ase.so
else
ifeq ($(TARGET), opaesim)
	OPAE_DRV_PATHS ?= libopae-c-sim.so
endif
endif
endif

all: $(PROJECT) kernel.vxbin kernel.dump

kernel.dump: kernel.elf
	$(VX_DP) -D $< > $@

kernel.vxbin: kernel.elf
	OBJCOPY=$(VX_CP) $(VORTEX_HOME)/kernel/scripts/vxbin.py $< $@

kernel.elf: $(VX_SRCS)
	$(VX_CXX) $(VX_CFLAGS) $^ $(VX_LDFLAGS) -o kernel.elf

$(PROJECT): $(SRCS)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

run-simx: $(PROJECT) kernel.vxbin
	LD_LIBRARY_PATH=$(VORTEX_RT_PATH):$(LD_LIBRARY_PATH) VORTEX_DRIVER=simx ./$(PROJECT) $(OPTS)

# CONFIGS selects which hardware extensions the RTL instantiates, so the rtlsim
# driver has to be built with the same set the kernel was compiled against.
# Without this the test loads whatever librtlsim.so happens to be lying around:
# an instruction whose unit is absent falls through VX_decode's default arm and
# executes as an unrelated ALU op, so the test reports wrong results instead of
# failing to build. sim/rtlsim/Makefile keeps a stamp of its verilator flags, so
# this is a no-op unless CONFIGS actually changed.
# Runners build the driver themselves, often with CONFIGS the test does not know
# about (design-space knobs such as AES_SBOX_RADIX live only in the driver). They
# set VX_SKIP_DRIVER_BUILD=1 so this rule does not rebuild it back to the test's
# own CONFIGS and silently undo the sweep.
.PHONY: driver-rtlsim
driver-rtlsim:
	@if [ "$(VX_SKIP_DRIVER_BUILD)" = "1" ]; then \
	  echo "driver-rtlsim: skipped, caller manages the driver"; \
	else \
	  $(MAKE) -C $(VORTEX_RT_PATH)/rtlsim CONFIGS='$(CONFIGS)'; \
	fi

run-rtlsim: $(PROJECT) kernel.vxbin driver-rtlsim
	LD_LIBRARY_PATH=$(VORTEX_RT_PATH):$(LD_LIBRARY_PATH) VORTEX_DRIVER=rtlsim ./$(PROJECT) $(OPTS)

run-opae: $(PROJECT) kernel.vxbin
	SCOPE_JSON_PATH=$(VORTEX_RT_PATH)/scope.json OPAE_DRV_PATHS=$(OPAE_DRV_PATHS) LD_LIBRARY_PATH=$(VORTEX_RT_PATH):$(LD_LIBRARY_PATH) VORTEX_DRIVER=opae ./$(PROJECT) $(OPTS)

run-de10pro: $(PROJECT) kernel.vxbin
	LD_LIBRARY_PATH=$(ROOT_DIR)/../PCIe_SW_KIT/Linux/PCIe_Library:$(VORTEX_RT_PATH):$(LD_LIBRARY_PATH) VORTEX_DRIVER=de10pro ./$(PROJECT) $(OPTS)

run-xrt: $(PROJECT) kernel.vxbin
ifeq ($(TARGET), hw)
	SCOPE_JSON_PATH=$(FPGA_BIN_DIR)/scope.json XRT_INI_PATH=$(VORTEX_RT_PATH)/xrt/xrt.ini EMCONFIG_PATH=$(FPGA_BIN_DIR) XRT_DEVICE_INDEX=$(XRT_DEVICE_INDEX) XRT_XCLBIN_PATH=$(FPGA_BIN_DIR)/vortex_afu.xclbin LD_LIBRARY_PATH=$(XILINX_XRT)/lib:$(VORTEX_RT_PATH):$(LD_LIBRARY_PATH) VORTEX_DRIVER=xrt ./$(PROJECT) $(OPTS)
else ifeq ($(TARGET), hw_emu)
	SCOPE_JSON_PATH=$(FPGA_BIN_DIR)/scope.json XCL_EMULATION_MODE=$(TARGET) XRT_INI_PATH=$(VORTEX_RT_PATH)/xrt/xrt.ini EMCONFIG_PATH=$(FPGA_BIN_DIR) XRT_DEVICE_INDEX=$(XRT_DEVICE_INDEX) XRT_XCLBIN_PATH=$(FPGA_BIN_DIR)/vortex_afu.xclbin LD_LIBRARY_PATH=$(XILINX_XRT)/lib:$(VORTEX_RT_PATH):$(LD_LIBRARY_PATH) VORTEX_DRIVER=xrt ./$(PROJECT) $(OPTS)
else
	SCOPE_JSON_PATH=$(VORTEX_RT_PATH)/scope.json LD_LIBRARY_PATH=$(XILINX_XRT)/lib:$(VORTEX_RT_PATH):$(LD_LIBRARY_PATH) VORTEX_DRIVER=xrt ./$(PROJECT) $(OPTS)
endif

.depend: $(SRCS)
	$(CXX) $(CXXFLAGS) -MM $^ > .depend;

clean-kernel:
	rm -rf *.elf *.vxbin *.dump

clean-host:
	rm -rf $(PROJECT) *.o *.log .depend

clean: clean-kernel clean-host

ifneq ($(MAKECMDGOALS),clean)
    -include .depend
endif
