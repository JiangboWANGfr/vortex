// Copyright © 2019-2023
// Licensed under the Apache License, Version 2.0.

#pragma once

#include <stdint.h>

// BAR0 board-management aperture. All registers are aligned 32-bit
// little-endian words; the aperture is separate from the Vortex window at
// 0x1000.
#define VX_DE10PRO_BM_BAR0_BASE          0x2000ull
#define VX_DE10PRO_BM_APERTURE_SIZE      0x0100ull

#define VX_DE10PRO_BM_MAGIC_VALUE        0x5658424du // "VXBM"
#define VX_DE10PRO_BM_VERSION_MAJOR      1u
#define VX_DE10PRO_BM_VERSION_MINOR      5u
#define VX_DE10PRO_BM_VERSION_VALUE      0x00010005u
#define VX_DE10PRO_BM_VERSION_MAJOR_OF(v) ((uint32_t)(v) >> 16)
#define VX_DE10PRO_BM_VERSION_MINOR_OF(v) ((uint32_t)(v) & 0xffffu)

#define VX_DE10PRO_BM_REG_MAGIC          0x00u
#define VX_DE10PRO_BM_REG_VERSION        0x04u
#define VX_DE10PRO_BM_REG_CAPABILITIES   0x08u
#define VX_DE10PRO_BM_REG_STATUS         0x0cu
#define VX_DE10PRO_BM_REG_TEMP_MC        0x10u
#define VX_DE10PRO_BM_REG_FAN_RPM        0x14u
#define VX_DE10PRO_BM_REG_POWER0_RAW     0x18u
#define VX_DE10PRO_BM_REG_POWER1_RAW     0x1cu
#define VX_DE10PRO_BM_REG_POWER0_LSB_NW  0x20u
#define VX_DE10PRO_BM_REG_POWER1_LSB_NW  0x24u
#define VX_DE10PRO_BM_REG_SAMPLE_COUNT   0x28u
#define VX_DE10PRO_BM_REG_TIMESTAMP_LO   0x2cu
#define VX_DE10PRO_BM_REG_TIMESTAMP_HI   0x30u
#define VX_DE10PRO_BM_REG_TIMESTAMP_HZ   0x34u
#define VX_DE10PRO_BM_REG_CLOCK_REQ_HZ   0x38u
#define VX_DE10PRO_BM_REG_CLOCK_CUR_HZ   0x3cu
#define VX_DE10PRO_BM_REG_CLOCK_COMMAND  0x40u
#define VX_DE10PRO_BM_REG_CLOCK_STATUS   0x44u
#define VX_DE10PRO_BM_REG_CLOCK_REQ_SEQ  0x48u
#define VX_DE10PRO_BM_REG_CLOCK_DONE_SEQ 0x4cu
#define VX_DE10PRO_BM_REG_QUIESCE_STATUS 0x50u
#define VX_DE10PRO_BM_REG_CLOCK_ERROR    0x54u
#define VX_DE10PRO_BM_REG_CLOCK_MEASURED_HZ 0x58u
#define VX_DE10PRO_BM_REG_FAN_STATUS     0x5cu
#define VX_DE10PRO_BM_REG_FAN_CONTROL    0x60u
#define VX_DE10PRO_BM_REG_SENSOR_VALID   0x64u
#define VX_DE10PRO_BM_REG_I2C_ERROR      0x68u

#define VX_DE10PRO_BM_CAP_TELEMETRY      (1u << 0)
#define VX_DE10PRO_BM_CAP_TEMPERATURE    (1u << 1)
#define VX_DE10PRO_BM_CAP_FAN            (1u << 2)
#define VX_DE10PRO_BM_CAP_POWER0         (1u << 3)
#define VX_DE10PRO_BM_CAP_POWER1         (1u << 4)
#define VX_DE10PRO_BM_CAP_TIMESTAMP      (1u << 5)
#define VX_DE10PRO_BM_CAP_FAN_CONTROL    (1u << 6)
#define VX_DE10PRO_BM_CAP_FAN_OVERRIDE   (1u << 7)
#define VX_DE10PRO_BM_CAP_CLOCK_READBACK (1u << 8)
#define VX_DE10PRO_BM_CAP_DYNAMIC_CLOCK  (1u << 9)
#define VX_DE10PRO_BM_CAP_QUIESCE        (1u << 10)
#define VX_DE10PRO_BM_CAP_DIAGNOSTICS    (1u << 11)

#define VX_DE10PRO_BM_STATUS_READY           (1u << 0)
#define VX_DE10PRO_BM_STATUS_TELEMETRY_VALID (1u << 1)
#define VX_DE10PRO_BM_STATUS_FAULT           (1u << 2)

#define VX_DE10PRO_BM_SENSOR_TEMP            (1u << 0)
#define VX_DE10PRO_BM_SENSOR_TACH0           (1u << 1)
#define VX_DE10PRO_BM_SENSOR_TACH1           (1u << 2)
#define VX_DE10PRO_BM_SENSOR_INPUT_SENSE     (1u << 3)
#define VX_DE10PRO_BM_SENSOR_INPUT_VIN       (1u << 4)
#define VX_DE10PRO_BM_SENSOR_INPUT_POWER     (1u << 5)
#define VX_DE10PRO_BM_SENSOR_CORE_SENSE      (1u << 6)
#define VX_DE10PRO_BM_SENSOR_CORE_VIN        (1u << 7)
#define VX_DE10PRO_BM_SENSOR_CORE_POWER      (1u << 8)

// Sticky per-bus drive faults in SENSOR_VALID, indexed by bus (0 temperature,
// 1 fan, 2 power). A set bit means the master drove that line low for a full
// phase and still read it high, so the drive is not reaching the wire. Reads as
// zero before ABI 1.5.
#define VX_DE10PRO_BM_SENSOR_VALID_MASK      0x1ffu
#define VX_DE10PRO_BM_DRIVE_FAULT_SCL_OF(v)  (((uint32_t)(v) >> 9) & 0x7u)
#define VX_DE10PRO_BM_DRIVE_FAULT_SDA_OF(v)  (((uint32_t)(v) >> 12) & 0x7u)

#define VX_DE10PRO_BM_I2C_ERROR_COUNT_OF(v) ((uint32_t)(v) & 0xffffu)
#define VX_DE10PRO_BM_I2C_ERROR_STEP_OF(v) \
  ((((uint32_t)(v) >> 16) & 0x0fu) | (((uint32_t)(v) >> 25) & 0x10u))
#define VX_DE10PRO_BM_I2C_ERROR_BUS_OF(v) (((uint32_t)(v) >> 20) & 0x3u)
#define VX_DE10PRO_BM_I2C_ERROR_NACK       (1u << 22)
#define VX_DE10PRO_BM_I2C_ERROR_TIMEOUT    (1u << 23)
#define VX_DE10PRO_BM_I2C_ERROR_BUS_STUCK  (1u << 24)
#define VX_DE10PRO_BM_I2C_ERROR_SHORT_READ (1u << 25)
// Which byte of the failed transaction was not acknowledged. Reads as zero
// before ABI 1.4, so check the minor version before reporting it. Only
// meaningful when VX_DE10PRO_BM_I2C_ERROR_NACK is set.
#define VX_DE10PRO_BM_I2C_ERROR_BYTE_OF(v) (((uint32_t)(v) >> 30) & 0x3u)

#define VX_DE10PRO_BM_CLOCK_HZ_100M          100000000u
#define VX_DE10PRO_BM_CLOCK_HZ_125M          125000000u
#define VX_DE10PRO_BM_CLOCK_HZ_200M          200000000u
#define VX_DE10PRO_BM_CLOCK_HZ_250M          250000000u

#define VX_DE10PRO_BM_FAN_STATUS_FULL_ON    (1u << 0)
#define VX_DE10PRO_BM_FAN_STATUS_VALID      (1u << 1)
#define VX_DE10PRO_BM_FAN_STATUS_FULL_OFF   (1u << 2)
#define VX_DE10PRO_BM_FAN_STATUS_DAC_SHIFT  8u
#define VX_DE10PRO_BM_FAN_STATUS_DAC_MASK   (0xffu << 8)
#define VX_DE10PRO_BM_FAN_STATUS_DAC_OF(v) \
  (((uint32_t)(v) & VX_DE10PRO_BM_FAN_STATUS_DAC_MASK) >> \
   VX_DE10PRO_BM_FAN_STATUS_DAC_SHIFT)

#define VX_DE10PRO_BM_FAN_CONTROL_AUTO       0u
#define VX_DE10PRO_BM_FAN_CONTROL_FULL_ON    1u
#define VX_DE10PRO_BM_FAN_CONTROL_MANUAL_DAC 2u
#define VX_DE10PRO_BM_FAN_CONTROL_FULL_OFF   3u
#define VX_DE10PRO_BM_FAN_CONTROL_MODE_MASK  0x3u
#define VX_DE10PRO_BM_FAN_CONTROL_DAC_SHIFT  8u
#define VX_DE10PRO_BM_FAN_CONTROL_DAC_MASK   (0xffu << 8)
#define VX_DE10PRO_BM_FAN_CONTROL_VALUE(mode, dac) \
  (((uint32_t)(mode) & VX_DE10PRO_BM_FAN_CONTROL_MODE_MASK) | \
   (((uint32_t)(dac) << VX_DE10PRO_BM_FAN_CONTROL_DAC_SHIFT) & \
    VX_DE10PRO_BM_FAN_CONTROL_DAC_MASK))
#define VX_DE10PRO_BM_FAN_CONTROL_MODE_OF(v) \
  ((uint32_t)(v) & VX_DE10PRO_BM_FAN_CONTROL_MODE_MASK)
#define VX_DE10PRO_BM_FAN_CONTROL_DAC_OF(v) \
  (((uint32_t)(v) & VX_DE10PRO_BM_FAN_CONTROL_DAC_MASK) >> \
   VX_DE10PRO_BM_FAN_CONTROL_DAC_SHIFT)

#define VX_DE10PRO_BM_CLOCK_CMD_APPLY        (1u << 0)
#define VX_DE10PRO_BM_CLOCK_CMD_CLEAR_ERROR  (1u << 1)

#define VX_DE10PRO_BM_CLOCK_BUSY             (1u << 0)
#define VX_DE10PRO_BM_CLOCK_DONE             (1u << 1)
#define VX_DE10PRO_BM_CLOCK_ERROR            (1u << 2)
#define VX_DE10PRO_BM_CLOCK_PLL_LOCKED       (1u << 3)
#define VX_DE10PRO_BM_CLOCK_CURRENT_VALID    (1u << 4)
#define VX_DE10PRO_BM_CLOCK_QUIESCE_REQUEST  (1u << 5)
#define VX_DE10PRO_BM_CLOCK_QUIESCE_ACK      (1u << 6)
#define VX_DE10PRO_BM_CLOCK_RESET_ASSERTED   (1u << 7)

#define VX_DE10PRO_BM_QUIESCE_REQUEST          (1u << 0)
#define VX_DE10PRO_BM_QUIESCE_SHELL_ACK        (1u << 1)
#define VX_DE10PRO_BM_QUIESCE_MEMORY_DRAIN_ACK (1u << 2)
#define VX_DE10PRO_BM_QUIESCE_COMPLETE         (1u << 31)
#define VX_DE10PRO_BM_QUIESCE_REQUIRED_MASK    0x00000007u

#define VX_DE10PRO_BM_CLOCK_ERR_NONE             0u
#define VX_DE10PRO_BM_CLOCK_ERR_BAD_FREQUENCY    1u
#define VX_DE10PRO_BM_CLOCK_ERR_BUSY              2u
#define VX_DE10PRO_BM_CLOCK_ERR_QUIESCE_TIMEOUT   3u
#define VX_DE10PRO_BM_CLOCK_ERR_RECONFIG_TIMEOUT  4u
#define VX_DE10PRO_BM_CLOCK_ERR_PLL_UNLOCKED      5u
#define VX_DE10PRO_BM_CLOCK_ERR_INTERNAL          6u
