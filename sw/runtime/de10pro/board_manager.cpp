// Copyright © 2019-2023
// Licensed under the Apache License, Version 2.0.

#include "board_manager.h"

#include <de10pro_board_manager_abi.h>

#include <limits>

namespace vortex {
namespace de10pro {

namespace {

constexpr uint32_t kTelemetryReadAttempts = 8;
constexpr uint64_t kNanosecondsPerSecond = 1000000000ull;
constexpr uint64_t kHertzPerMegahertz = 1000000ull;
constexpr uint32_t kClockControlCapabilities =
    VX_DE10PRO_BM_CAP_CLOCK_READBACK
  | VX_DE10PRO_BM_CAP_DYNAMIC_CLOCK
  | VX_DE10PRO_BM_CAP_QUIESCE;

bool is_supported_clock_hz(uint32_t frequency_hz) {
  switch (frequency_hz) {
  case VX_DE10PRO_BM_CLOCK_HZ_100M:
  case VX_DE10PRO_BM_CLOCK_HZ_125M:
  case VX_DE10PRO_BM_CLOCK_HZ_200M:
  case VX_DE10PRO_BM_CLOCK_HZ_250M:
    return true;
  default:
    return false;
  }
}

} // namespace

BoardManager::BoardManager(BoardManagerIo io)
    : io_(io), available_(false), version_(0), capabilities_(0) {
}

BoardProbeResult BoardManager::probe() {
  available_ = false;
  version_ = 0;
  capabilities_ = 0;

  uint32_t magic = 0;
  if (!read(VX_DE10PRO_BM_REG_MAGIC, &magic)) {
    return BoardProbeResult::NotPresent;
  }
  if (magic != VX_DE10PRO_BM_MAGIC_VALUE) {
    return BoardProbeResult::NotPresent;
  }
  if (!read(VX_DE10PRO_BM_REG_VERSION, &version_)) {
    return BoardProbeResult::IoError;
  }
  if (VX_DE10PRO_BM_VERSION_MAJOR_OF(version_)
      != VX_DE10PRO_BM_VERSION_MAJOR) {
    return BoardProbeResult::Incompatible;
  }
  if (!read(VX_DE10PRO_BM_REG_CAPABILITIES, &capabilities_)) {
    return BoardProbeResult::IoError;
  }
  available_ = true;
  return BoardProbeResult::Available;
}

bool BoardManager::available() const {
  return available_;
}

uint32_t BoardManager::version() const {
  return version_;
}

uint32_t BoardManager::capabilities() const {
  return capabilities_;
}

bool BoardManager::read_telemetry(BoardTelemetry* telemetry) const {
  if (telemetry == nullptr || !available_
   || !(capabilities_ & VX_DE10PRO_BM_CAP_TELEMETRY)) {
    return false;
  }

  for (uint32_t attempt = 0; attempt < kTelemetryReadAttempts; ++attempt) {
    BoardTelemetry next{};
    next.capabilities = capabilities_;
    uint32_t count_before = 0;
    uint32_t timestamp_low = 0;
    uint32_t timestamp_high = 0;
    uint32_t temperature = 0;
    if (!read(VX_DE10PRO_BM_REG_SAMPLE_COUNT, &count_before)
     || !read(VX_DE10PRO_BM_REG_STATUS, &next.status)) {
      return false;
    }
    if (capabilities_ & VX_DE10PRO_BM_CAP_DIAGNOSTICS) {
      if (!read(VX_DE10PRO_BM_REG_SENSOR_VALID, &next.sensor_valid)) {
        return false;
      }
    } else {
      next.sensor_valid =
          (next.status & VX_DE10PRO_BM_STATUS_TELEMETRY_VALID) ? 0x1ffu : 0u;
    }
    if ((capabilities_ & VX_DE10PRO_BM_CAP_TEMPERATURE)
     && !read(VX_DE10PRO_BM_REG_TEMP_MC, &temperature)) {
      return false;
    }
    next.temperature_mc = static_cast<int32_t>(temperature);
    if ((capabilities_ & VX_DE10PRO_BM_CAP_FAN)
     && !read(VX_DE10PRO_BM_REG_FAN_RPM, &next.fan_rpm)) {
      return false;
    }
    if ((capabilities_ & VX_DE10PRO_BM_CAP_POWER0)
     && (!read(VX_DE10PRO_BM_REG_POWER0_RAW, &next.power_raw[0])
      || !read(VX_DE10PRO_BM_REG_POWER0_LSB_NW,
               &next.power_lsb_nw[0]))) {
      return false;
    }
    if ((capabilities_ & VX_DE10PRO_BM_CAP_POWER1)
     && (!read(VX_DE10PRO_BM_REG_POWER1_RAW, &next.power_raw[1])
      || !read(VX_DE10PRO_BM_REG_POWER1_LSB_NW,
               &next.power_lsb_nw[1]))) {
      return false;
    }
    if ((capabilities_ & VX_DE10PRO_BM_CAP_TIMESTAMP)
     && (!read(VX_DE10PRO_BM_REG_TIMESTAMP_LO, &timestamp_low)
      || !read(VX_DE10PRO_BM_REG_TIMESTAMP_HI, &timestamp_high)
      || !read(VX_DE10PRO_BM_REG_TIMESTAMP_HZ, &next.timestamp_hz))) {
      return false;
    }
    next.timestamp = uint64_t(timestamp_low)
                   | (uint64_t(timestamp_high) << 32);

    uint32_t count_after = 0;
    if (!read(VX_DE10PRO_BM_REG_SAMPLE_COUNT, &count_after)) {
      return false;
    }
    if (count_before == count_after) {
      next.sample_count = count_after;
      *telemetry = next;
      return true;
    }
  }
  return false;
}

bool BoardManager::read_diagnostics(BoardDiagnostics* diagnostics) const {
  if (diagnostics == nullptr || !available_
   || !(capabilities_ & VX_DE10PRO_BM_CAP_DIAGNOSTICS)) {
    return false;
  }
  BoardDiagnostics next{};
  if (!read(VX_DE10PRO_BM_REG_STATUS, &next.status)
   || !read(VX_DE10PRO_BM_REG_SENSOR_VALID, &next.sensor_valid)
   || !read(VX_DE10PRO_BM_REG_I2C_ERROR, &next.i2c_error)) {
    return false;
  }
  *diagnostics = next;
  return true;
}

bool BoardManager::read_current_clock_hz(uint32_t* frequency_hz) const {
  if (frequency_hz == nullptr || !available_
   || !(capabilities_ & VX_DE10PRO_BM_CAP_CLOCK_READBACK)) {
    return false;
  }
  uint32_t clock_status = 0;
  uint32_t current_hz = 0;
  if (!read(VX_DE10PRO_BM_REG_CLOCK_STATUS, &clock_status)
   || !read(VX_DE10PRO_BM_REG_CLOCK_CUR_HZ, &current_hz)
   || !(clock_status & VX_DE10PRO_BM_CLOCK_CURRENT_VALID)
   || current_hz == 0) {
    return false;
  }
  *frequency_hz = current_hz;
  return true;
}

bool BoardManager::read_measured_clock_hz(uint32_t* frequency_hz) const {
  if (frequency_hz == nullptr || !available_
   || !(capabilities_ & VX_DE10PRO_BM_CAP_CLOCK_READBACK)) {
    return false;
  }
  uint32_t measured_hz = 0;
  if (!read(VX_DE10PRO_BM_REG_CLOCK_MEASURED_HZ, &measured_hz)
   || measured_hz == 0) {
    return false;
  }
  *frequency_hz = measured_hz;
  return true;
}

bool BoardManager::read_fan_status(uint32_t* status) const {
  return status != nullptr && available_
      && (capabilities_ & VX_DE10PRO_BM_CAP_FAN_CONTROL)
      && read(VX_DE10PRO_BM_REG_FAN_STATUS, status);
}

bool BoardManager::read_fan_control(uint32_t* control) const {
  return control != nullptr && supports_fan_override()
      && read(VX_DE10PRO_BM_REG_FAN_CONTROL, control);
}

bool BoardManager::supports_fan_override() const {
  return available_
      && (capabilities_ & VX_DE10PRO_BM_CAP_FAN_OVERRIDE);
}

bool BoardManager::set_fan_control(uint32_t mode, uint32_t dac) const {
  if (!supports_fan_override() || dac > 0xffu
   || (mode != VX_DE10PRO_BM_FAN_CONTROL_AUTO
    && mode != VX_DE10PRO_BM_FAN_CONTROL_FULL_ON
    && mode != VX_DE10PRO_BM_FAN_CONTROL_MANUAL_DAC
    && mode != VX_DE10PRO_BM_FAN_CONTROL_FULL_OFF)) {
    return false;
  }
  return write(VX_DE10PRO_BM_REG_FAN_CONTROL,
               VX_DE10PRO_BM_FAN_CONTROL_VALUE(mode, dac));
}

bool BoardManager::fan_percent_to_control(uint32_t percent, uint32_t* mode,
                                                  uint32_t* dac) {
  if (percent > 100 || mode == nullptr || dac == nullptr) {
    return false;
  }
  if (percent == 100) {
    *mode = VX_DE10PRO_BM_FAN_CONTROL_FULL_ON;
    *dac = 8;
  } else if (percent == 0) {
    *mode = VX_DE10PRO_BM_FAN_CONTROL_FULL_OFF;
    *dac = 120;
  } else {
    *mode = VX_DE10PRO_BM_FAN_CONTROL_MANUAL_DAC;
    *dac = 8 + ((100 - percent) * (120 - 8)) / 100;
  }
  return true;
}

bool BoardManager::supports_clock_control() const {
  return available_
      && (capabilities_ & kClockControlCapabilities)
      == kClockControlCapabilities;
}

ClockRequestResult BoardManager::begin_clock_request(
    uint32_t frequency_hz, uint32_t sequence) const {
  if (!supports_clock_control()) {
    return ClockRequestResult::NotSupported;
  }
  if (!is_supported_clock_hz(frequency_hz) || sequence == 0) {
    return ClockRequestResult::InvalidArgument;
  }
  uint32_t clock_status = 0;
  if (!read(VX_DE10PRO_BM_REG_CLOCK_STATUS, &clock_status)) {
    return ClockRequestResult::IoError;
  }
  if (clock_status & VX_DE10PRO_BM_CLOCK_BUSY) {
    return ClockRequestResult::Busy;
  }
  if (!write(VX_DE10PRO_BM_REG_CLOCK_COMMAND,
             VX_DE10PRO_BM_CLOCK_CMD_CLEAR_ERROR)
   || !write(VX_DE10PRO_BM_REG_CLOCK_REQ_HZ, frequency_hz)
   || !write(VX_DE10PRO_BM_REG_CLOCK_REQ_SEQ, sequence)
   || !write(VX_DE10PRO_BM_REG_CLOCK_COMMAND,
             VX_DE10PRO_BM_CLOCK_CMD_APPLY)) {
    return ClockRequestResult::IoError;
  }
  return ClockRequestResult::Started;
}

ClockPollResult BoardManager::poll_clock_request(uint32_t sequence,
                                                 ClockState* state) const {
  if (state == nullptr || !supports_clock_control()) {
    return ClockPollResult::IoError;
  }
  ClockState next{};
  if (!read(VX_DE10PRO_BM_REG_CLOCK_STATUS, &next.status)
   || !read(VX_DE10PRO_BM_REG_CLOCK_CUR_HZ, &next.current_hz)
   || !read(VX_DE10PRO_BM_REG_CLOCK_DONE_SEQ,
            &next.completed_sequence)
   || !read(VX_DE10PRO_BM_REG_QUIESCE_STATUS,
            &next.quiesce_status)
   || !read(VX_DE10PRO_BM_REG_CLOCK_ERROR, &next.error)) {
    return ClockPollResult::IoError;
  }
  *state = next;
  if ((next.status & VX_DE10PRO_BM_CLOCK_BUSY)
   || next.completed_sequence != sequence) {
    return ClockPollResult::Pending;
  }
  if ((next.status & VX_DE10PRO_BM_CLOCK_ERROR)
   || next.error != VX_DE10PRO_BM_CLOCK_ERR_NONE) {
    return ClockPollResult::Rejected;
  }
  if ((next.status & VX_DE10PRO_BM_CLOCK_DONE)
   && (next.status & VX_DE10PRO_BM_CLOCK_CURRENT_VALID)) {
    return ClockPollResult::Complete;
  }
  return ClockPollResult::Pending;
}

uint64_t BoardManager::power_raw_to_microwatts(
    uint32_t raw, uint32_t lsb_nanowatts) {
  const uint64_t whole = uint64_t(raw) * (lsb_nanowatts / 1000u);
  const uint64_t fraction = uint64_t(raw) * (lsb_nanowatts % 1000u);
  return whole + (fraction + 500u) / 1000u;
}

uint64_t BoardManager::timestamp_to_nanoseconds(uint64_t ticks,
                                                uint32_t timestamp_hz) {
  if (timestamp_hz == 0) {
    return 0;
  }
  const uint64_t seconds = ticks / timestamp_hz;
  if (seconds > std::numeric_limits<uint64_t>::max()
              / kNanosecondsPerSecond) {
    return std::numeric_limits<uint64_t>::max();
  }
  const uint64_t remainder = ticks % timestamp_hz;
  const uint64_t fractional = remainder * kNanosecondsPerSecond
                            / timestamp_hz;
  const uint64_t whole = seconds * kNanosecondsPerSecond;
  if (whole > std::numeric_limits<uint64_t>::max() - fractional) {
    return std::numeric_limits<uint64_t>::max();
  }
  return whole + fractional;
}

uint64_t BoardManager::clock_hz_to_mhz(uint32_t frequency_hz) {
  return frequency_hz / kHertzPerMegahertz
       + ((frequency_hz % kHertzPerMegahertz)
          >= kHertzPerMegahertz / 2);
}

uint32_t BoardManager::next_clock_sequence(uint32_t completed_sequence) {
  const uint32_t next = completed_sequence + 1u;
  return next == 0 ? 1u : next;
}

bool BoardManager::read(uint32_t offset, uint32_t* value) const {
  return io_.read32 != nullptr
      && io_.read32(io_.context, VX_DE10PRO_BM_BAR0_BASE + offset, value);
}

bool BoardManager::write(uint32_t offset, uint32_t value) const {
  return io_.write32 != nullptr
      && io_.write32(io_.context, VX_DE10PRO_BM_BAR0_BASE + offset, value);
}

} // namespace de10pro
} // namespace vortex
