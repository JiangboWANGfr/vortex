// Copyright © 2019-2023
// Licensed under the Apache License, Version 2.0.

#pragma once

#include <cstdint>

namespace vortex {
namespace de10pro {

struct BoardManagerIo {
  void* context;
  bool (*read32)(void* context, uint64_t address, uint32_t* value);
  bool (*write32)(void* context, uint64_t address, uint32_t value);
};

enum class BoardProbeResult {
  Available,
  NotPresent,
  Incompatible,
  IoError,
};

enum class ClockRequestResult {
  Started,
  NotSupported,
  InvalidArgument,
  Busy,
  IoError,
};

enum class ClockPollResult {
  Pending,
  Complete,
  Rejected,
  IoError,
};

struct BoardTelemetry {
  uint32_t capabilities;
  uint32_t status;
  int32_t temperature_mc;
  uint32_t fan_rpm;
  uint32_t power_raw[2];
  uint32_t power_lsb_nw[2];
  uint32_t sample_count;
  uint64_t timestamp;
  uint32_t timestamp_hz;
};

struct ClockState {
  uint32_t status;
  uint32_t current_hz;
  uint32_t completed_sequence;
  uint32_t quiesce_status;
  uint32_t error;
};

class BoardManager {
public:
  explicit BoardManager(BoardManagerIo io);

  BoardProbeResult probe();
  bool available() const;
  uint32_t version() const;
  uint32_t capabilities() const;

  bool read_telemetry(BoardTelemetry* telemetry) const;
  bool read_current_clock_hz(uint32_t* frequency_hz) const;
  bool read_measured_clock_hz(uint32_t* frequency_hz) const;
  bool read_fan_status(uint32_t* status) const;
  bool supports_clock_control() const;
  ClockRequestResult begin_clock_request(uint32_t frequency_hz,
                                         uint32_t sequence) const;
  ClockPollResult poll_clock_request(uint32_t sequence,
                                     ClockState* state) const;

  static uint64_t power_raw_to_microwatts(uint32_t raw,
                                          uint32_t lsb_nanowatts);
  static uint64_t timestamp_to_nanoseconds(uint64_t ticks,
                                           uint32_t timestamp_hz);
  static uint64_t clock_hz_to_mhz(uint32_t frequency_hz);
  static uint32_t next_clock_sequence(uint32_t completed_sequence);

private:
  bool read(uint32_t offset, uint32_t* value) const;
  bool write(uint32_t offset, uint32_t value) const;

  BoardManagerIo io_;
  bool available_;
  uint32_t version_;
  uint32_t capabilities_;
};

} // namespace de10pro
} // namespace vortex
