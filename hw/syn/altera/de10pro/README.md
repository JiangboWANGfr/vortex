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
- one platform-memory bank with interleaving disabled
- 125 MHz platform clock

The Qsys connections are:

- `DUT.coreclkout_hip` to the Vortex clock
- `DUT.app_nreset_status` to the Vortex reset interface
- `BAR_INTERPRETER.bri_master` to Vortex control at BAR0 offset `0x1000`
- the Vortex memory master to `ddr4_clock_crossing_bridge_ddr4a.s0` at base 0

## Prepare the project

Run from any directory:

```bash
/home/jiangbowang/aphdcode/vortex_proj/vortexCrypto/hw/syn/altera/de10pro/prepare_project.sh
```

The script generates the Vortex configuration and source assignments under
`generated/vortexcrypto`, updates `pcie_ddr4_system.qsys`, and regenerates its
synthesis output. It does not run a Quartus compilation or program the board.

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
DE10PRO_CONFIGS='-DVX_CFG_NUM_CLUSTERS=1 -DVX_CFG_NUM_CORES=1 -DVX_CFG_NUM_WARPS=1 -DVX_CFG_NUM_THREADS=1 -DVX_CFG_EXT_F_DISABLE=1 -DVX_CFG_EXT_D_DISABLE=1 -DVX_CFG_PLATFORM_MEMORY_NUM_BANKS=1 -DVX_CFG_PLATFORM_MEMORY_INTERLEAVE=0 -DVX_CFG_PLATFORM_CLOCK_RATE=125'
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
