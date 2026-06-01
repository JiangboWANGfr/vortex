PROJECT = Vortex
TOP_LEVEL_ENTITY = $(PROJECT)
SRC_FILE = $(PROJECT).sv
FPU_IP = 1

include ../../common.mk

#CONFIGS += -DNUM_CORES=2
#CONFIGS += -DNUM_WARPS=32
#CONFIGS += -DNUM_THREADS=32
#CONFIGS += -DL2_ENABLE

# Crypto extensions: VX_crypto_unit is always instantiated, each PE guarded by
# its EXT_*_ENABLE macro. Uncomment to synthesize a crypto PE into the bitstream.
#CONFIGS += -DEXT_AES_ENABLE
#CONFIGS += -DEXT_SHA256_ENABLE
#CONFIGS += -DEXT_KECCAK_ENABLE
#CONFIGS += -DEXT_GHASH_ENABLE

FPU_INCLUDE = -I$(RTL_DIR)/fpu
ifneq (,$(findstring -DFPU_FPNEW, $(CONFIGS)))
	FPU_INCLUDE += -J$(THIRD_PARTY_DIR)/cvfpu/src/common_cells/include -J$(THIRD_PARTY_DIR)/cvfpu/src/common_cells/src -J$(THIRD_PARTY_DIR)/cvfpu/src/fpu_div_sqrt_mvp/hdl -J$(THIRD_PARTY_DIR)/cvfpu/src
endif
# VX_crypto_unit (in hw/rtl/crypto) is instantiated unconditionally by
# VX_execute, so the crypto include path is always required.
RTL_INCLUDE = -I$(RTL_DIR) -I$(RTL_DIR)/libs -I$(RTL_DIR)/crypto -I$(RTL_DIR)/interfaces -I$(RTL_DIR)/core -I$(RTL_DIR)/mem -I$(RTL_DIR)/cache $(FPU_INCLUDE)

# Add V extension sources
ifneq (,$(findstring -DEXT_V_ENABLE, $(CONFIGS)))
	RTL_INCLUDE += -I$(RTL_DIR)/vpu
endif

# Add TCU extension sources
ifneq (,$(findstring -DEXT_TCU_ENABLE, $(CONFIGS)))
	RTL_INCLUDE += -I$(RTL_DIR)/tcu
ifneq (,$(findstring -DTCU_DRL, $(CONFIGS)))
RTL_INCLUDE += -I$(RTL_DIR)/tcu/drl
endif
ifneq (,$(findstring -DTCU_BHF, $(CONFIGS)))
RTL_INCLUDE += -I$(RTL_DIR)/tcu/bhf
endif
	RTL_INCLUDE += -J$(THIRD_PARTY_DIR)/hardfloat/source/RISCV
	RTL_INCLUDE += -I$(THIRD_PARTY_DIR)/hardfloat/source
endif