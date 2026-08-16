# DE10-Pro Gen3x16 + DDR4A integration

This directory integrates `vortexCrypto` into the existing Quartus 19.2
project at:

```text
/home/jiangbowang/aphdcode/vortex_proj/fpga_proj/PCIE_DDR4_Vortex_G3X16
```

The FPGA project keeps all four DDR4 interfaces and their calibration logic.
Vortex uses DDR4A only in this first integration step. DDR4B/C/D remain in the
system but are not connected to the Vortex memory master.

## Generated profile

The default profile is deliberately small for bring-up:

- RV32, one cluster, one core
- one warp and one thread/lane
- F and D disabled
- three-cycle I-cache and D-cache pipelines for the maximum 250 MHz core clock
- one platform-memory bank with interleaving disabled
- one runtime-reconfigurable IOPLL with 100, 125, 200, and 250 MHz profiles;
  the initial profile is 250 MHz

The Qsys connections are:

- `CLK_50_B3I` to the runtime-reconfigurable Vortex IOPLL reference input
- `CLK_50_B2C` to the free-running board-management and clock-control logic
- the IOPLL output to Vortex through dedicated control and DDR4A CDC bridges
- the synchronized dynamic-clock reset to the Vortex reset interface
- `BAR_INTERPRETER.bri_master` to Vortex control at BAR0 offset `0x1000`
- `BAR_INTERPRETER.bri_master` to the board manager at BAR0 offset `0x2000`
- the Vortex memory master through the CDC and post-CDC drain monitor to
  `ddr4_ingress_pipe_ddr4a.s0` at base 0
- `ddr4_ingress_pipe_ddr4a.m0` to
  `ddr4_clock_crossing_bridge_ddr4a.s0` at base 0

## Prepare the project

Run from any directory:

```bash
/home/jiangbowang/aphdcode/vortex_proj/vortexCrypto/hw/syn/altera/de10pro/prepare_project.sh
```

The script generates the Vortex configuration and source assignments under
`generated/vortexcrypto`, creates the four-profile IOPLL MIF under
`generated/dynclk`, updates `pcie_ddr4_system.qsys`, and regenerates its
synthesis output. It also generates the PCIe DUT child IP when that output is
missing or stale. It does not run a Quartus compilation or program the board.

The target and Quartus installation can be overridden when working in an
isolated checkout:

```bash
VX_DE10PRO_PROJECT_DIR=/path/to/PCIE_DDR4_Vortex_G3X16 \
QUARTUS_ROOT=/data/Quartus/tools/19.2/quartus \
  ./hw/syn/altera/de10pro/prepare_project.sh
```

Keep the default one-warp/one-thread profile for initial bring-up. When those
counts are increased later, the prepare-script overrides and the software
`CONFIGS` macros must be changed together.

## Build the host runtime

The runtime talks directly to `/dev/intel_fpga_pcie_drv`; it does not use the
old Terasic shared library. A 32-bit Vortex target can be configured and built
out of tree with:

```bash
mkdir -p build32
cd build32
../configure --xlen=32 \
  --tooldir=/home/jiangbowang/aphdcode/vortex_proj/toolchains
DE10PRO_CONFIGS='-DVX_CFG_NUM_CLUSTERS=1 -DVX_CFG_NUM_CORES=1 -DVX_CFG_NUM_WARPS=1 -DVX_CFG_NUM_THREADS=1 -DVX_CFG_EXT_F_DISABLE=1 -DVX_CFG_EXT_D_DISABLE=1 -DVX_CFG_ICACHE_LATENCY=3 -DVX_CFG_DCACHE_LATENCY=3 -DVX_CFG_PLATFORM_MEMORY_NUM_BANKS=1 -DVX_CFG_PLATFORM_MEMORY_INTERLEAVE=0 -DVX_CFG_PLATFORM_CLOCK_RATE=250'
make -C sw/runtime de10pro CONFIGS="$DE10PRO_CONFIGS"
```

Use the same `CONFIGS` value when building test kernels; it must match the
profile used to generate the SOF.

Runtime defaults for this project are BAR0, control offset `0x1000`, DDR4A
physical base `0x800000000`, and a 1 MiB DMA staging buffer. If more than one
matching FPGA is installed, select its 16-bit bus/device/function value with
`DE10PRO_PCIE_BDF`; zero selects the driver default device. The kernel DMA
buffer and queues are shared per FPGA, so run only one Vortex runtime process per
device.

The runtime reports the board manager's active Vortex clock instead of the
compile-time 250 MHz value when the new SOF is loaded. Use the board tool to
read temperature, fan, power, and clock telemetry or to change the Vortex
clock after a workload has stopped:

```bash
./sw/runtime/vortex-de10pro-boardctl
./sw/runtime/vortex-de10pro-boardctl --clock-hz 200000000
./sw/runtime/vortex-de10pro-boardctl --fan-percent 75
./sw/runtime/vortex-de10pro-boardctl --fan-percent 0
./sw/runtime/vortex-de10pro-boardctl --fan-full
./sw/runtime/vortex-de10pro-boardctl --fan-auto
```

`--fan-percent` accepts 0 through 100 and uses the same open-loop mapping as
the Terasic Nios demo: 100 is full-on, 0 is full-off, and 1 through 99 map to
DAC codes 8 through 120. It disables automatic temperature control until
`--fan-auto` is issued, so monitor board temperature during manual operation.
`--fan-dac` remains available for raw MAX6651 diagnostics.

The tool and runtime take the same exclusive device lock, so do not run them
concurrently. A clock request quiesces Vortex and drains the DDR4A CDC before
asserting Vortex reset and reconfiguring the IOPLL; PCIe and all DDR4 clocks
remain unchanged.

## Compile and validate later

Adding Vortex changes the netlist, so the timing result of the PCIe + four-DDR4
baseline does not prove timing closure for the integrated image. Run a complete
Quartus compile before board validation:

```bash
cd /home/jiangbowang/aphdcode/vortex_proj/fpga_proj/PCIE_DDR4_Vortex_G3X16
/data/Quartus/tools/19.2/quartus/bin/quartus_sh \
  --flow compile vortex_g3x16_ddr4x4
```

After programming the resulting SOF, a warm reboot is normally required for
PCIe link retraining. Then load the kernel module built for the running kernel
and run the 1-core/1-warp/1-thread smoke test before increasing warp/thread
counts.

The Gen3x16 Vortex integration has not yet been validated on hardware.
