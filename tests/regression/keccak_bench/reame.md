上板跑先 smoke：

```bash
cd /home/jiangbowang/aphd2026/myvortex
./build64/ci/keccak_smoke_runner.sh --drivers=de10pro --accel-modes=NATIVE --fd-disable
```

bench：

```bash
cd /home/jiangbowang/aphd2026/myvortex
./build64/ci/keccak_bench_runner.sh --driver=de10pro --accel-modes=NATIVE --datasets=byte-long,bit-long --messages=2 --cases=256 --fd-disable
```

手动单跑而不是走 runner，可以：

```bash
make -C build64/runtime/de10pro clean all CONFIGS=-DEXT_KECCAK_ENABLE
make -C build64/tests/regression/keccak_smoke clean all KECCAK_ACCEL_MODE=NATIVE EXT_F_DISABLE=1 EXT_D_DISABLE=1
sudo env \
LD_LIBRARY_PATH=/home/jiangbowang/aphd2026/myvortex/build64/../PCIe_SW_KIT/Linux/PCIe_Library:/home/jiangbowang/aphd2026/myvortex/build64/runtime \
VORTEX_DRIVER=de10pro \
TERASIC_PCIE_SO_PATH=/home/jiangbowang/aphd2026/myvortex/PCIe_SW_KIT/PCIe_DDR4/32G/terasic_pcie_qsys.so \
DE10PRO_VX_TIMEOUT_MS=5000 \
DE10PRO_VX_VERBOSE_STATUS=1 \
DE10PRO_VX_BAR=4 \
DE10PRO_VX_MMIO_BASE=0x4001000 \
DE10PRO_VX_STAGING_ADDR=0x800000000 \
DE10PRO_VX_STAGING_SIZE=0x200000 \
make -C build64/tests/regression/keccak_smoke run-de10pro KECCAK_ACCEL_MODE=NATIVE OPTS='-n 16 -l 4'
```
