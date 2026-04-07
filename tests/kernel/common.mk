ROOT_DIR := $(realpath ../../..)

ifeq ($(XLEN),64)
ifeq ($(and $(filter 1,$(EXT_F_DISABLE)),$(filter 1,$(EXT_D_DISABLE))),1)
CFLAGS += -DVX_NO_LIBC_RUNTIME -DPRINTF_DISABLE_SUPPORT_FLOAT
endif
ifeq ($(EXT_F_DISABLE),1)
CFLAGS += -march=rv64ima_zicsr -mabi=lp64
else ifeq ($(EXT_D_DISABLE),1)
CFLAGS += -march=rv64imaf_zicsr -mabi=lp64f
else
CFLAGS += -march=rv64imafd_zicsr -mabi=lp64d
endif
else
ifeq ($(EXT_F_DISABLE),1)
CFLAGS += -march=rv32ima_zicsr -mabi=ilp32
else
CFLAGS += -march=rv32imaf_zicsr -mabi=ilp32f
endif
endif
STARTUP_ADDR ?= 0x80000000

VORTEX_KN_PATH ?= $(ROOT_DIR)/kernel

LLVM_CFLAGS += --sysroot=$(RISCV_SYSROOT)
LLVM_CFLAGS += --gcc-toolchain=$(RISCV_TOOLCHAIN_PATH)
LLVM_CFLAGS += -Xclang -target-feature -Xclang +vortex

CC  = $(LLVM_VORTEX)/bin/clang $(LLVM_CFLAGS)
CXX = $(LLVM_VORTEX)/bin/clang++ $(LLVM_CFLAGS)
AR  = $(LLVM_VORTEX)/bin/llvm-ar
DP  = $(LLVM_VORTEX)/bin/llvm-objdump
CP  = $(LLVM_VORTEX)/bin/llvm-objcopy

CFLAGS += -O3 -mcmodel=medany -fno-exceptions -nostartfiles -nostdlib -fdata-sections -ffunction-sections
CFLAGS += -I$(VORTEX_HOME)/kernel/include -I$(ROOT_DIR)/hw -I$(SW_COMMON_DIR)
CFLAGS += -DXLEN_$(XLEN) -DNDEBUG $(CONFIGS)

ifeq ($(and $(filter 1,$(EXT_F_DISABLE)),$(filter 1,$(EXT_D_DISABLE))),1)
ifeq ($(XLEN),32)
LIBC_LIB += -lgcc
endif
else
LIBC_LIB += -L$(LIBC_VORTEX)/lib -lm -lc
ifeq ($(XLEN),32)
LIBC_LIB += -lgcc
else
LIBC_LIB += $(LIBCRT_VORTEX)/lib/baremetal/libclang_rt.builtins-riscv$(XLEN).a
endif
endif

LDFLAGS += -Wl,-Bstatic,--gc-sections,-T,$(VORTEX_HOME)/kernel/scripts/link$(XLEN).ld,--defsym=STARTUP_ADDR=$(STARTUP_ADDR) $(VORTEX_KN_PATH)/libvortex.a $(LIBC_LIB)

all: $(PROJECT).elf $(PROJECT).bin $(PROJECT).dump

$(PROJECT).dump: $(PROJECT).elf
	$(DP) -D $< > $@

$(PROJECT).bin: $(PROJECT).elf
	$(CP) -O binary $< $@

$(PROJECT).elf: $(SRCS)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

run-rtlsim: $(PROJECT).bin
	$(ROOT_DIR)/sim/rtlsim/rtlsim $(PROJECT).bin

run-simx: $(PROJECT).bin
	$(ROOT_DIR)/sim/simx/simx $(PROJECT).bin

.depend: $(SRCS)
	$(CC) $(CFLAGS) -MM $^ > .depend;

clean:
	rm -rf *.elf *.bin *.dump *.log .depend
