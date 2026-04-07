# DE10-Pro XLEN Bring-Up Guide

This guide records the bring-up and debugging flow for the DE10-Pro PCIe platform under the current `myvortex` tree.

It is intended to answer one practical question:

- Is the current hardware bitstream running `XLEN=32` or `XLEN=64`?

The short answer from our validation is:

- The current board image behaves as `XLEN=32`.
- Running 64-bit software on the current image causes incorrect behavior.
- Rebuilding software as 32-bit makes `basic_diag` and `demo` pass on `de10pro`.

## 1. Final Diagnosis

The following evidence was collected:

- 64-bit software showed hangs or incorrect behavior on `de10pro`.
- 32-bit `basic_diag` passed on `de10pro`.
- 32-bit `basic_diag` variants using `src0`, `src1`, and `src0 + src1` all passed.
- 32-bit `demo` also passed on `de10pro`.
- In the hardware tree, [`VX_config.vh`](hw/rtl/VX_config.vh) defaults to `XLEN_32` unless `XLEN_64` is explicitly defined.
- The Quartus project [`DE10_Pro.qsf`](/home/jiangbowang/aphd2026/PCIE_test/PCIE_DDR4_Vortex/DE10_Pro.qsf) did not define `XLEN_64`.

Therefore:

- The currently programmed hardware should be treated as 32-bit.

## 2. Important Paths

- Vortex source tree: `/home/jiangbowang/aphd2026/myvortex`
- Quartus project: `/home/jiangbowang/aphd2026/PCIE_test/PCIE_DDR4_Vortex`
- PCIe runtime library path used at runtime: `/home/jiangbowang/aphd2026/myvortex/PCIe_SW_KIT/Linux/PCIe_Library`

## 3. What `kernel` and `runtime` Mean

The `kernel` and `runtime` folders serve different layers of the system.

### 3.1 `kernel/`: device-side runtime and startup code

The `kernel` folder contains code that runs on the Vortex device itself.

Typical responsibilities:

- Device-side startup and exit code
- Thread spawn helpers
- Device-side printing and syscalls
- Kernel-side support library linked into `kernel.elf`

Examples:

- [`kernel/src/vx_start.S`](kernel/src/vx_start.S)
- [`kernel/src/vx_spawn.c`](kernel/src/vx_spawn.c)
- [`kernel/src/vx_print.c`](kernel/src/vx_print.c)
- [`kernel/src/vx_syscalls.c`](kernel/src/vx_syscalls.c)

When a regression test builds:

- [`tests/regression/basic/kernel.cpp`](tests/regression/basic/kernel.cpp)
- [`tests/regression/demo/kernel.cpp`](tests/regression/demo/kernel.cpp)

these files are compiled into:

- `kernel.elf`
- `kernel.vxbin`

That image is uploaded to the device and executed there.

### 3.2 `runtime/`: host-side driver abstraction

The `runtime` folder contains code that runs on the host machine.

Typical responsibilities:

- Open the selected backend device
- Allocate device memory
- Upload kernel images and arguments
- Start execution
- Wait for completion
- Download results

Examples:

- [`runtime/include/vortex.h`](runtime/include/vortex.h)
- [`runtime/de10pro/vortex.cpp`](runtime/de10pro/vortex.cpp)
- [`runtime/simx/vortex.cpp`](runtime/simx/vortex.cpp)

The host regression programs:

- [`tests/regression/basic/main.cpp`](tests/regression/basic/main.cpp)
- [`tests/regression/demo/main.cpp`](tests/regression/demo/main.cpp)

call the runtime API. The runtime then dispatches to a backend such as:

- `de10pro`
- `simx`
- `rtlsim`

### 3.3 How they work together

Running a test such as `./demo -n1` involves both layers:

1. The host program starts and calls the runtime API.
2. The runtime opens the selected backend, such as `de10pro`.
3. The runtime uploads `kernel.vxbin` and kernel arguments.
4. The Vortex device executes the uploaded kernel image.
5. The runtime waits for completion and copies results back.

Short version:

- `kernel/` is device-side code.
- `runtime/` is host-side control code.

## 4. Toolchain Environment

The generated environment script is in the build directory, not in the source tree root.

Example:

```bash
cd /home/jiangbowang/aphd2026/myvortex/build32
source ./ci/toolchain_env.sh
```

This script adds tool paths such as:

- `verilator`
- `sv2v`
- `yosys`

For example, the generated file is:

- [`build32/ci/toolchain_env.sh`](build32/ci/toolchain_env.sh)

and it prepends:

- `$TOOLDIR/verilator/bin`

to `PATH`.

Without sourcing this file, targets that rely on `verilator` may fail with "command not found".

## 5. Current Board Workflow: Use 32-bit Software

This is the recommended workflow if you want to keep using the current bitstream.

### 5.1 Create a 32-bit build directory

```bash
cd /home/jiangbowang/aphd2026/myvortex
mkdir -p build32
cd build32
../configure --xlen=32 --tooldir=$HOME/tools
source ./ci/toolchain_env.sh
```

### 5.2 Generate hardware config headers

```bash
cd /home/jiangbowang/aphd2026/myvortex/build32/hw
make config
```

This generates:

- [`build32/hw/VX_config.h`](build32/hw/VX_config.h)
- [`build32/hw/VX_types.h`](build32/hw/VX_types.h)

### 5.3 Build the 32-bit kernel runtime

```bash
cd /home/jiangbowang/aphd2026/myvortex/build32/kernel
make clean all EXT_F_DISABLE=1 EXT_D_DISABLE=1
```

### 5.4 Build the 32-bit DE10-Pro runtime
Build the host-side runtime base library:

```bash
cd /home/jiangbowang/aphd2026/myvortex/build32/runtime/stub
make clean all
```

Build the DE10-Pro runtime backend:

```bash
cd /home/jiangbowang/aphd2026/myvortex/build32/runtime/de10pro
make clean all
```

This produces:

- [`build32/runtime/libvortex-de10pro.so`](build32/runtime/libvortex-de10pro.so)

### 5.5 Optional: build the 32-bit SimX runtime

```bash
cd /home/jiangbowang/aphd2026/myvortex/build32/runtime/simx
CCACHE_DISABLE=1 make clean all DEBUG=0
```

Notes:

- `CCACHE_DISABLE=1` was used to avoid a local `ccache` permission issue during our environment-driven builds.
- `DEBUG=0` avoids a bad `DEBUG=release` environment expansion in the SimX Makefiles.

This produces:

- [`build32/runtime/libvortex-simx.so`](build32/runtime/libvortex-simx.so)

### 5.6 Build and run `basic_diag`

Build:

```bash
cd /home/jiangbowang/aphd2026/myvortex/build32/tests/regression/basic_diag
make clean all XLEN=32 EXT_F_DISABLE=1 EXT_D_DISABLE=1 STARTUP_ADDR=0x80000000
```

Run on board:

```bash
cd /home/jiangbowang/aphd2026/myvortex/build32/tests/regression/basic_diag
sudo env \
LD_LIBRARY_PATH=/home/jiangbowang/aphd2026/myvortex/build32/../PCIe_SW_KIT/Linux/PCIe_Library:/home/jiangbowang/aphd2026/myvortex/build32/runtime \
VORTEX_DRIVER=de10pro \
DE10PRO_VX_TIMEOUT_MS=5000 \
DE10PRO_VX_VERBOSE_STATUS=1 \
TERASIC_PCIE_SO_PATH=/home/jiangbowang/aphd2026/myvortex/PCIe_SW_KIT/PCIe_DDR4/32G/terasic_pcie_qsys.so \
DE10PRO_VX_BAR=4 \
DE10PRO_VX_MMIO_BASE=0x4001000 \
DE10PRO_VX_STAGING_ADDR=0x800000000 \
DE10PRO_VX_STAGING_SIZE=0x200000 \
./basic_diag -t1 -n128
```

### 5.7 Build and run `demo`

Build:

```bash
cd /home/jiangbowang/aphd2026/myvortex/build32/tests/regression/demo
make clean all XLEN=32 EXT_F_DISABLE=1 EXT_D_DISABLE=1 STARTUP_ADDR=0x80000000
```

Run on board:

```bash
cd /home/jiangbowang/aphd2026/myvortex/build32/tests/regression/demo
sudo env \
LD_LIBRARY_PATH=/home/jiangbowang/aphd2026/myvortex/build32/../PCIe_SW_KIT/Linux/PCIe_Library:/home/jiangbowang/aphd2026/myvortex/build32/runtime \
VORTEX_DRIVER=de10pro \
DE10PRO_VX_TIMEOUT_MS=5000 \
DE10PRO_VX_VERBOSE_STATUS=1 \
TERASIC_PCIE_SO_PATH=/home/jiangbowang/aphd2026/myvortex/PCIe_SW_KIT/PCIe_DDR4/32G/terasic_pcie_qsys.so \
DE10PRO_VX_BAR=4 \
DE10PRO_VX_MMIO_BASE=0x4001000 \
DE10PRO_VX_STAGING_ADDR=0x800000000 \
DE10PRO_VX_STAGING_SIZE=0x200000 \
./demo -n1
```

This configuration has been validated to pass.

## 6. Rebuilding Hardware as True 64-bit

If the goal is to run RV64 software, the hardware project must explicitly define `XLEN_64`.

### 6.1 Update Quartus macros

Edit:

- [`DE10_Pro.qsf`](/home/jiangbowang/aphd2026/PCIE_test/PCIE_DDR4_Vortex/DE10_Pro.qsf)

Use:

```tcl
set_global_assignment -name VERILOG_MACRO "QUARTUS=1"
set_global_assignment -name VERILOG_MACRO "XLEN_64"
set_global_assignment -name VERILOG_MACRO "EXT_F_DISABLE=1"
set_global_assignment -name VERILOG_MACRO "EXT_D_DISABLE=1"
```

Use `XLEN_64`, not `XLEN_64=1`.

Reason:

- [`VX_config.vh`](hw/rtl/VX_config.vh) checks this with `` `ifdef XLEN_64 ``.

### 6.2 Keep F/D disabled at first

Do not enable floating point yet unless you also regenerate and integrate the required Altera FPU IPs.

For first 64-bit bring-up, keep:

- `EXT_F_DISABLE=1`
- `EXT_D_DISABLE=1`

### 6.3 Recompile and reflash hardware

After editing `DE10_Pro.qsf`:

1. Run a full Quartus compile
2. Program the new `sof`
3. Reboot or reinitialize the board as needed

### 6.4 Build a 64-bit software tree

```bash
cd /home/jiangbowang/aphd2026/myvortex
mkdir -p build64
cd build64
../configure --xlen=64 --tooldir=$HOME/tools
source ./ci/toolchain_env.sh
```

Build the shared config headers:

```bash
cd /home/jiangbowang/aphd2026/myvortex/build64/hw
make config
```

Build the 64-bit kernel runtime:

```bash
cd /home/jiangbowang/aphd2026/myvortex/build64/kernel
make clean all EXT_F_DISABLE=1 EXT_D_DISABLE=1
```

Build the host-side runtime base library:

```bash
cd /home/jiangbowang/aphd2026/myvortex/build64/runtime/stub
make clean all
```

Build the DE10-Pro runtime backend:

```bash
cd /home/jiangbowang/aphd2026/myvortex/build64/runtime/de10pro
make clean all
```

Build `basic`:

```bash
cd /home/jiangbowang/aphd2026/myvortex/build64/tests/regression/basic
make clean all XLEN=64 EXT_F_DISABLE=1 EXT_D_DISABLE=1 STARTUP_ADDR=0x80000000
```

Run `basic` on board:

```bash
cd /home/jiangbowang/aphd2026/myvortex/build64/tests/regression/basic
sudo env \
LD_LIBRARY_PATH=/home/jiangbowang/aphd2026/myvortex/build64/../PCIe_SW_KIT/Linux/PCIe_Library:/home/jiangbowang/aphd2026/myvortex/build64/runtime \
VORTEX_DRIVER=de10pro \
DE10PRO_VX_TIMEOUT_MS=5000 \
DE10PRO_VX_VERBOSE_STATUS=1 \
TERASIC_PCIE_SO_PATH=/home/jiangbowang/aphd2026/myvortex/PCIe_SW_KIT/PCIe_DDR4/32G/terasic_pcie_qsys.so \
DE10PRO_VX_BAR=4 \
DE10PRO_VX_MMIO_BASE=0x4001000 \
DE10PRO_VX_STAGING_ADDR=0x800000000 \
DE10PRO_VX_STAGING_SIZE=0x200000 \
./basic -t1 -n128
```

Build `demo`:

```bash
cd /home/jiangbowang/aphd2026/myvortex/build64/tests/regression/demo
make clean all XLEN=64 EXT_F_DISABLE=1 EXT_D_DISABLE=1 STARTUP_ADDR=0x80000000
```

Run `demo` on board:

```bash
cd /home/jiangbowang/aphd2026/myvortex/build64/tests/regression/demo
sudo env \
LD_LIBRARY_PATH=/home/jiangbowang/aphd2026/myvortex/build64/../PCIe_SW_KIT/Linux/PCIe_Library:/home/jiangbowang/aphd2026/myvortex/build64/runtime \
VORTEX_DRIVER=de10pro \
DE10PRO_VX_TIMEOUT_MS=5000 \
DE10PRO_VX_VERBOSE_STATUS=1 \
TERASIC_PCIE_SO_PATH=/home/jiangbowang/aphd2026/myvortex/PCIe_SW_KIT/PCIe_DDR4/32G/terasic_pcie_qsys.so \
DE10PRO_VX_BAR=4 \
DE10PRO_VX_MMIO_BASE=0x4001000 \
DE10PRO_VX_STAGING_ADDR=0x800000000 \
DE10PRO_VX_STAGING_SIZE=0x200000 \
./demo -n1
```

Expected result after the hardware has really been rebuilt as 64-bit:

- `basic` should pass under `build64`
- `demo` should pass under `build64`

## 7. Can `runtime/make all` be used?

Yes, but with caveats.

`build32/runtime/Makefile` defines:

- `stub`
- `rtlsim`
- `simx`
- `opae`
- `xrt`
- `de10pro`

So:

```bash
cd /home/jiangbowang/aphd2026/myvortex/build32/runtime
make all
```

tries to build all runtime backends, not just `de10pro`.

That means:

- `verilator` must be on `PATH`
- simulator-related dependencies must be healthy
- OPAE/XRT-related paths may also be pulled in

For board-only work, this is not recommended.

Prefer:

```bash
make -C runtime/de10pro
```

and optionally:

```bash
make -C runtime/simx DEBUG=0
```

## 8. Can top-level `make -s` from `README.md` be used?

Yes, but only when the full environment is ready.

The generated top-level build Makefile:

- [`build32/Makefile`](build32/Makefile)

shows that `make -s` builds:

- `third_party`
- `hw`
- `sim`
- `kernel`
- `runtime`
- `tests`

This is broader than needed for board bring-up.

For DE10-Pro debug, `make -s` is usually not the best first command because it also builds:

- SimX
- RTLSim
- OPAE sim
- XRT sim

and those may fail for reasons unrelated to the board.

Recommended rule:

- For board debugging: use targeted `make -C ...` commands
- For a full developer environment: use top-level `make -s` after sourcing the build-dir `toolchain_env.sh`

## 9. Current Local Caveat: `DEBUG=release`

In the current shell environment, `DEBUG` is set to:

```bash
DEBUG=release
```

Some simulator Makefiles treat any non-empty `DEBUG` as:

```make
-DDEBUG_LEVEL=$(DEBUG)
```

which becomes:

```bash
-DDEBUG_LEVEL=release
```

and breaks compilation.

If that happens, use one of:

```bash
unset DEBUG
```

or:

```bash
make ... DEBUG=0
```

## 10. Recommended Practical Workflow

For the current board image:

1. Use `build32`
2. Source `build32/ci/toolchain_env.sh`
3. Build `hw/config`, `kernel`, and `runtime/de10pro`
4. Build only the test you need
5. Run on `de10pro`

For future true RV64 hardware:

1. Add `XLEN_64` to Quartus macros
2. Full compile and reflash
3. Build a separate `build64`
4. Re-run the same tests under 64-bit software
