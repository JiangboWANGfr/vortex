# Regression Runner Guide

This guide documents a small helper script for repeatedly building and running the most common regression apps without retyping long command sequences.

The script is intended for these apps:

- `basic`
- `demo`
- `aes_smoke`
- `aes256`

and these drivers:

- `simx`
- `rtlsim`
- `de10pro`

## 1. Where the Script Lives

The source template is:

- [`ci/regression_runner.sh.in`](ci/regression_runner.sh.in)

After running `configure` in a build directory, it is generated as:

- `build32/ci/regression_runner.sh`
- `build64/ci/regression_runner.sh`

Use the generated script from the build directory that matches your target hardware/software configuration.

## 2. Current Recommended Board Setup

The current DE10-Pro board image validated in this tree is:

- `XLEN=32`
- `EXT_F_DISABLE=1`
- `EXT_D_DISABLE=1`

For that setup, use `build32`.

## 3. One-Time Build Directory Setup

### 3.1 32-bit build

```bash
cd /home/jiangbowang/aphd2026/myvortex
mkdir -p build32
cd build32
../configure --xlen=32 --tooldir=$HOME/tools
source ./ci/toolchain_env.sh
```

### 3.2 64-bit build

```bash
cd /home/jiangbowang/aphd2026/myvortex
mkdir -p build64
cd build64
../configure --xlen=64 --tooldir=$HOME/tools
source ./ci/toolchain_env.sh
```

If you add or update the runner script later, run `../configure` again inside the existing build directory so the generated `ci/regression_runner.sh` is refreshed.

## 4. What the Script Does

For each requested run, the script performs:

1. `make -C hw config`
2. rebuild `kernel`
3. rebuild `runtime/stub`
4. rebuild the selected backend runtime:
   - `runtime/simx`
   - `runtime/rtlsim`
   - `runtime/de10pro`
5. rebuild the selected regression app
6. execute `run-simx`, `run-rtlsim`, or `run-de10pro`

The script also provides built-in default runtime arguments:

- `basic` -> `-t1 -n128`
- `demo` -> `-n1`
- `aes_smoke` -> no extra args
- `aes256` -> no extra args

## 5. Script Options

```text
--driver=<simx|rtlsim|de10pro>
--apps=<list|all>
--opts=<args>
--startup-addr=<hex>
--ext-f-disable
--ext-d-disable
--fd-disable
--debug=<level>
```

Notes:

- `--apps=all` expands to `basic,demo,aes_smoke,aes256`
- `--fd-disable` is shorthand for both `--ext-f-disable` and `--ext-d-disable`
- `--opts` overrides the built-in default runtime arguments for every selected app

## 6. Common Examples

### 6.1 Current board flow: 32-bit + F/D disabled + SimX

```bash
cd /home/jiangbowang/aphd2026/myvortex/build32
source ./ci/toolchain_env.sh
./ci/regression_runner.sh --driver=simx --apps=all --fd-disable
```

### 6.2 Current board flow: 32-bit + F/D disabled + RTLSim

```bash
cd /home/jiangbowang/aphd2026/myvortex/build32
source ./ci/toolchain_env.sh
./ci/regression_runner.sh --driver=rtlsim --apps=basic,demo --fd-disable
```

### 6.3 Current board flow: 32-bit + F/D disabled + DE10-Pro

```bash
cd /home/jiangbowang/aphd2026/myvortex/build32
source ./ci/toolchain_env.sh
./ci/regression_runner.sh --driver=de10pro --apps=all --fd-disable
```

### 6.4 Single AES smoke test on DE10-Pro

```bash
cd /home/jiangbowang/aphd2026/myvortex/build32
source ./ci/toolchain_env.sh
./ci/regression_runner.sh --driver=de10pro --apps=aes_smoke --fd-disable
```

### 6.5 Single demo test on SimX with custom args

```bash
cd /home/jiangbowang/aphd2026/myvortex/build32
source ./ci/toolchain_env.sh
./ci/regression_runner.sh --driver=simx --apps=demo --fd-disable --opts="-n4"
```

## 7. DE10-Pro Environment Defaults

When `--driver=de10pro` is selected, the script exports these defaults unless you already set them in your shell:

```bash
TERASIC_PCIE_SO_PATH=$BUILD_DIR/../PCIe_SW_KIT/PCIe_DDR4/32G/terasic_pcie_qsys.so
DE10PRO_VX_TIMEOUT_MS=5000
DE10PRO_VX_VERBOSE_STATUS=1
DE10PRO_VX_BAR=4
DE10PRO_VX_MMIO_BASE=0x4001000
DE10PRO_VX_STAGING_ADDR=0x800000000
DE10PRO_VX_STAGING_SIZE=0x200000
```

If your board uses different values, export them before running the script.

## 8. Running DE10-Pro Without `sudo`

The regression runner does not call `sudo`. It assumes the PCIe driver device node is already accessible.

Recommended setup:

1. Make sure the driver is loaded.
2. Make sure `/dev/altera_pcie0` exists.
3. Give your user permission through a `udev` rule or a dedicated group.

Example check:

```bash
ls -l /dev/altera_pcie*
id
```

## 9. Manual Commands the Script Replaces

If you want to compare with the manual flow, the runner is mainly replacing command sequences like:

```bash
make -C build32/kernel clean all EXT_F_DISABLE=1 EXT_D_DISABLE=1
make -C build32/runtime/de10pro clean all
make -C build32/tests/regression/aes256 clean all XLEN=32 EXT_F_DISABLE=1 EXT_D_DISABLE=1 STARTUP_ADDR=0x80000000
make -C build32/tests/regression/aes256 run-de10pro
```

The script keeps this logic in one place and applies the same build flags consistently across runtime and app builds.
