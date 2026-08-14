# DE10-Pro project preparation

This directory prepares a DE10-Pro Quartus project for the software-command-
processor compatibility backend. It does not migrate the local crypto RTL and
does not run Quartus.

The flow deliberately separates three inputs:

- The existing project at
  `/home/jiangbowang/aphdcode/PCIE_test/PCIE_DDR4_Vortex` supplies the tested
  DE10-Pro pin assignments, PCIe/DDR4 Qsys system, and generated board IP.
- The current `vortexCrypto` checkout supplies all Vortex RTL, the DE10-Pro
  wrapper, and generated configuration headers.
- The prepared project is always written to
  `/home/jiangbowang/aphdcode/PCIE_test/PCIE_DDR4_VortexCrypto`.

Run from any directory:

```bash
/home/jiangbowang/aphdcode/vortexCrypto/hw/syn/altera/de10pro/prepare_project.sh
```

The script copies the board project without `.git`, Quartus databases,
`output_files`, results, saved bitstreams, or other compile products. It refuses
to update an existing destination unless that directory was created by this
script. A later refresh preserves any compile output already in the isolated
destination.

The default generated profile is the conservative bring-up configuration:

- RV32
- one cluster and one core
- one warp and one thread (one execution lane)
- F and D disabled
- one platform-memory bank
- platform-memory interleaving disabled
- no migrated crypto-extension macros

Only the warp and thread counts may be overridden for a reproducible
multi-warp/multi-lane test. For example:

```bash
VX_DE10PRO_NUM_WARPS=4 VX_DE10PRO_NUM_THREADS=4 \
  /home/jiangbowang/aphdcode/vortexCrypto/hw/syn/altera/de10pro/prepare_project.sh
```

`generated/vortexcrypto/vortex_sources.qsf` is generated from current master by
`hw/scripts/gen_sources.sh`; its contents are appended to `DE10_Pro.qsf` after
the legacy hand-written `myvortex` source list is removed. The custom-component
Tcl path and cached Qsys synthesis copies of `vortex_shell.sv` are also updated.
Updating the cached copy is required because the board project intentionally
uses `PROJECT_IP_REGENERATION_POLICY NEVER_REGENERATE_IP`.

Before a future compile, these checks should produce no stale-source output and
show the generated configuration macros:

```bash
rg -ni 'myvortex/hw|aes test|EXT_(AES|SHA|KECCAK|GHASH|CHACHA|POLY1305)' \
  /home/jiangbowang/aphdcode/PCIE_test/PCIE_DDR4_VortexCrypto/DE10_Pro.qsf \
  /home/jiangbowang/aphdcode/PCIE_test/PCIE_DDR4_VortexCrypto/vortex_shell_hw.tcl

rg -n 'VERILOG_MACRO "VX_CFG_(XLEN|NUM_CLUSTERS|NUM_CORES|NUM_WARPS|NUM_THREADS|EXT_F_DISABLE|EXT_D_DISABLE|PLATFORM_MEMORY_NUM_BANKS|PLATFORM_MEMORY_INTERLEAVE)=' \
  /home/jiangbowang/aphdcode/PCIE_test/PCIE_DDR4_VortexCrypto/generated/vortexcrypto/vortex_sources.qsf
```

Only after software/RTL smoke checks pass should synthesis be started from the
isolated project directory:

```bash
cd /home/jiangbowang/aphdcode/PCIE_test/PCIE_DDR4_VortexCrypto
/data/Quartus/tools/19.2/quartus/bin/quartus_sh --flow compile DE10_Pro
```

## Validated board run

Both the 1-core/1-warp/1-thread image and the
1-core/4-warp/4-thread image were validated on DE10-Pro with Quartus Pro 19.2.
Program the named 4-warp/4-thread SRAM image with:

```bash
cd /home/jiangbowang/aphdcode/PCIE_test/PCIE_DDR4_VortexCrypto
/data/Quartus/tools/19.2/quartus/bin/quartus_pgm -m jtag -c 1 \
  -o 'p;output_files/DE10_Pro_1c4w4t.sof'
```

After JTAG programming, perform a warm reboot so PCIe retrains against the new
image. Do not power-cycle the board: a complete power loss discards the SRAM
image. If the reboot selects a different kernel, rebuild the driver against the
running kernel before loading it:

```bash
sudo reboot

cd /home/jiangbowang/aphd2026/myvortex/PCIe_SW_KIT/PCIe_Driver
make clean
make
sudo insmod ./altera_pcie.ko vendor_id=0x1172 device_id=0xE003
```

The Vortex control slave is at BAR4 offset `0x00401000`. Run the validated
integer demo using the isolated build tree:

```bash
VX_CONFIGS='-DVX_CFG_NUM_CLUSTERS=1 -DVX_CFG_NUM_CORES=1 -DVX_CFG_NUM_WARPS=4 -DVX_CFG_NUM_THREADS=4 -DVX_CFG_EXT_F_DISABLE=1 -DVX_CFG_EXT_D_DISABLE=1 -DVX_CFG_PLATFORM_MEMORY_NUM_BANKS=1 -DVX_CFG_PLATFORM_MEMORY_INTERLEAVE=0'

TERASIC_PCIE_SO_PATH=/home/jiangbowang/aphd2026/myvortex/PCIe_SW_KIT/PCIe_DDR4/32G/terasic_pcie_qsys.so \
make -C /home/jiangbowang/aphdcode/vortexCrypto/build32/tests/regression/demo \
  run-de10pro \
  VORTEX_RT_SRC=/home/jiangbowang/aphdcode/vortexCrypto/build32/sw/runtime \
  VORTEX_RT_LIB=/home/jiangbowang/aphdcode/vortexCrypto/build32/sw/runtime \
  CONFIGS="$VX_CONFIGS" OPTS=-n64
```

The expected launch is `grid_dim=4x1, block_dim=4x1`, followed by `PASSED!`.
The validated image also passed block widths 1, 2, 4, 8, and 16. The width-16
case places all 16 hardware threads in one block, spanning all four warps, and
passed ten consecutive board runs without a DMA, timeout, reset, or result
failure. The demo leaves `std::hex` enabled after printing device addresses, so
it displays decimal width 16 as `block_dim=10x1`.

The 4-warp/4-thread full Quartus flow completed with zero errors and uses 73,848
ALMs (8%), 146,029 registers, 7,468,400 block-memory bits, and 10 DSP blocks.
It is suitable for functional validation but is not timing-clean: the 250 MHz
Vortex clock domain has a worst setup slack of -0.539 ns.
