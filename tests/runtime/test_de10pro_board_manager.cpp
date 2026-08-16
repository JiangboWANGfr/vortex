// Copyright © 2019-2023
// Licensed under the Apache License, Version 2.0.

#include "board_manager.h"

#include <de10pro_board_manager_abi.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <utility>
#include <vector>

using vortex::de10pro::BoardManager;
using vortex::de10pro::BoardManagerIo;
using vortex::de10pro::BoardDiagnostics;
using vortex::de10pro::BoardProbeResult;
using vortex::de10pro::BoardTelemetry;
using vortex::de10pro::ClockPollResult;
using vortex::de10pro::ClockRequestResult;
using vortex::de10pro::ClockState;

namespace {

struct FakeMmio {
  std::map<uint64_t, uint32_t> registers;
  std::map<uint64_t, std::vector<uint32_t>> scripted_reads;
  std::map<uint64_t, size_t> scripted_read_indices;
  std::vector<uint64_t> reads;
  std::vector<std::pair<uint64_t, uint32_t>> writes;
  bool reads_fail = false;
  bool writes_fail = false;
  uint64_t failing_read_address = UINT64_MAX;

  static bool read32(void* context, uint64_t address, uint32_t* value) {
    auto* self = static_cast<FakeMmio*>(context);
    self->reads.push_back(address);
    if (self->reads_fail || address == self->failing_read_address
     || value == nullptr) {
      return false;
    }
    const auto scripted = self->scripted_reads.find(address);
    if (scripted != self->scripted_reads.end()
     && !scripted->second.empty()) {
      auto& index = self->scripted_read_indices[address];
      const size_t selected = index < scripted->second.size()
          ? index : scripted->second.size() - 1;
      *value = scripted->second[selected];
      ++index;
      return true;
    }
    const auto found = self->registers.find(address);
    if (found == self->registers.end()) {
      return false;
    }
    *value = found->second;
    return true;
  }

  static bool write32(void* context, uint64_t address, uint32_t value) {
    auto* self = static_cast<FakeMmio*>(context);
    if (self->writes_fail) {
      return false;
    }
    self->registers[address] = value;
    self->writes.emplace_back(address, value);
    return true;
  }

  void set(uint32_t offset, uint32_t value) {
    registers[VX_DE10PRO_BM_BAR0_BASE + offset] = value;
  }

  void script(uint32_t offset, std::vector<uint32_t> values) {
    scripted_reads[VX_DE10PRO_BM_BAR0_BASE + offset] = std::move(values);
  }
};

int failures = 0;

void expect(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "FAILED: %s\n", message);
    ++failures;
  }
}

BoardManager make_manager(FakeMmio* mmio) {
  return BoardManager(BoardManagerIo{mmio, FakeMmio::read32,
                                     FakeMmio::write32});
}

void populate_identity(FakeMmio* mmio, uint32_t capabilities) {
  mmio->set(VX_DE10PRO_BM_REG_MAGIC, VX_DE10PRO_BM_MAGIC_VALUE);
  mmio->set(VX_DE10PRO_BM_REG_VERSION, VX_DE10PRO_BM_VERSION_VALUE);
  mmio->set(VX_DE10PRO_BM_REG_CAPABILITIES, capabilities);
  mmio->set(VX_DE10PRO_BM_REG_STATUS, VX_DE10PRO_BM_STATUS_READY);
}

void test_probe_fallback() {
  FakeMmio missing;
  auto missing_manager = make_manager(&missing);
  expect(missing_manager.probe() == BoardProbeResult::NotPresent,
         "failed magic read must mean optional manager is absent");
  uint32_t frequency_hz = 0;
  expect(!missing_manager.read_current_clock_hz(&frequency_hz),
         "absent manager must not provide a clock rate");

  FakeMmio mismatch;
  mismatch.set(VX_DE10PRO_BM_REG_MAGIC, 0x12345678u);
  auto mismatch_manager = make_manager(&mismatch);
  expect(mismatch_manager.probe() == BoardProbeResult::NotPresent,
         "magic mismatch must preserve the legacy-device fallback");

  FakeMmio incompatible;
  populate_identity(&incompatible, VX_DE10PRO_BM_CAP_CLOCK_READBACK);
  incompatible.set(VX_DE10PRO_BM_REG_VERSION, 0x00020000u);
  auto incompatible_manager = make_manager(&incompatible);
  expect(incompatible_manager.probe() == BoardProbeResult::Incompatible,
         "unknown major version must not be consumed");
  const uint64_t capabilities_address = VX_DE10PRO_BM_BAR0_BASE
                                      + VX_DE10PRO_BM_REG_CAPABILITIES;
  expect(std::find(incompatible.reads.begin(), incompatible.reads.end(),
                   capabilities_address) == incompatible.reads.end(),
         "unknown major version must not read capabilities");

  FakeMmio version_io_error;
  version_io_error.set(VX_DE10PRO_BM_REG_MAGIC,
                       VX_DE10PRO_BM_MAGIC_VALUE);
  auto version_io_error_manager = make_manager(&version_io_error);
  expect(version_io_error_manager.probe() == BoardProbeResult::IoError,
         "failed version read after matching magic is an I/O error");

  FakeMmio io_error;
  io_error.set(VX_DE10PRO_BM_REG_MAGIC, VX_DE10PRO_BM_MAGIC_VALUE);
  io_error.set(VX_DE10PRO_BM_REG_VERSION, VX_DE10PRO_BM_VERSION_VALUE);
  auto io_error_manager = make_manager(&io_error);
  expect(io_error_manager.probe() == BoardProbeResult::IoError,
         "failed capability read after compatible identity is an I/O error");
}

void test_telemetry_and_units() {
  constexpr uint32_t capabilities = VX_DE10PRO_BM_CAP_TELEMETRY
                                  | VX_DE10PRO_BM_CAP_TEMPERATURE
                                  | VX_DE10PRO_BM_CAP_FAN
                                  | VX_DE10PRO_BM_CAP_POWER0
                                  | VX_DE10PRO_BM_CAP_POWER1
                                  | VX_DE10PRO_BM_CAP_TIMESTAMP;
  FakeMmio mmio;
  populate_identity(&mmio, capabilities);
  mmio.set(VX_DE10PRO_BM_REG_STATUS,
           VX_DE10PRO_BM_STATUS_READY
         | VX_DE10PRO_BM_STATUS_TELEMETRY_VALID);
  mmio.set(VX_DE10PRO_BM_REG_TEMP_MC,
           static_cast<uint32_t>(int32_t(-12500)));
  mmio.set(VX_DE10PRO_BM_REG_FAN_RPM, 4200);
  mmio.set(VX_DE10PRO_BM_REG_POWER0_RAW, 2000);
  mmio.set(VX_DE10PRO_BM_REG_POWER1_RAW, 333);
  mmio.set(VX_DE10PRO_BM_REG_POWER0_LSB_NW, 500000);
  mmio.set(VX_DE10PRO_BM_REG_POWER1_LSB_NW, 1500);
  mmio.set(VX_DE10PRO_BM_REG_SAMPLE_COUNT, 7);
  mmio.set(VX_DE10PRO_BM_REG_TIMESTAMP_LO, 0x89abcdefu);
  mmio.set(VX_DE10PRO_BM_REG_TIMESTAMP_HI, 0x01234567u);
  mmio.set(VX_DE10PRO_BM_REG_TIMESTAMP_HZ, 50000000);
  mmio.script(VX_DE10PRO_BM_REG_SAMPLE_COUNT, {7, 8, 8, 8});

  auto manager = make_manager(&mmio);
  expect(manager.probe() == BoardProbeResult::Available,
         "version 1 manager must probe");
  BoardTelemetry telemetry{};
  expect(manager.read_telemetry(&telemetry),
         "telemetry read must retry a torn snapshot");
  expect(telemetry.sample_count == 8,
         "telemetry retry must return the stable sample sequence");
  expect(telemetry.temperature_mc == -12500,
         "temperature is signed millidegrees Celsius");
  expect(telemetry.fan_rpm == 4200, "fan unit is RPM");
  expect(telemetry.timestamp == 0x0123456789abcdefull,
         "timestamp words must combine little-endian");
  expect(BoardManager::power_raw_to_microwatts(2000, 500000)
         == 1000000, "power raw scale conversion must produce microwatts");
  expect(BoardManager::power_raw_to_microwatts(333, 1500)
         == 500, "power conversion must round to nearest microwatt");
  expect(BoardManager::timestamp_to_nanoseconds(25, 50000000) == 500,
         "50 MHz timestamp ticks must convert to 20 ns each");
  expect(BoardManager::clock_hz_to_mhz(199600000) == 200,
         "clock capability conversion must round to nearest MHz");
  uint32_t fan_mode = 0;
  uint32_t fan_dac = 0;
  expect(BoardManager::fan_percent_to_control(100, &fan_mode, &fan_dac)
         && fan_mode == VX_DE10PRO_BM_FAN_CONTROL_FULL_ON
         && fan_dac == 8,
         "100 percent must select full-on");
  expect(BoardManager::fan_percent_to_control(75, &fan_mode, &fan_dac)
         && fan_mode == VX_DE10PRO_BM_FAN_CONTROL_MANUAL_DAC
         && fan_dac == 36,
         "75 percent must match the Terasic DAC conversion");
  expect(BoardManager::fan_percent_to_control(50, &fan_mode, &fan_dac)
         && fan_mode == VX_DE10PRO_BM_FAN_CONTROL_MANUAL_DAC
         && fan_dac == 64,
         "50 percent must match the Terasic DAC conversion");
  expect(BoardManager::fan_percent_to_control(0, &fan_mode, &fan_dac)
         && fan_mode == VX_DE10PRO_BM_FAN_CONTROL_FULL_OFF
         && fan_dac == 120,
         "zero percent must select full-off");
  expect(!BoardManager::fan_percent_to_control(101, &fan_mode, &fan_dac),
         "fan percentages above 100 must be rejected");
}

void test_partial_round_telemetry() {
  // A round that lost one I2C transaction still commits the sensors that did
  // read back. Telemetry must survive it and report which sensors are valid,
  // rather than being discarded wholesale on the STATUS fault bit.
  constexpr uint32_t capabilities = VX_DE10PRO_BM_CAP_TELEMETRY
                                  | VX_DE10PRO_BM_CAP_TEMPERATURE
                                  | VX_DE10PRO_BM_CAP_POWER0
                                  | VX_DE10PRO_BM_CAP_POWER1
                                  | VX_DE10PRO_BM_CAP_DIAGNOSTICS;
  FakeMmio mmio;
  populate_identity(&mmio, capabilities);
  mmio.set(VX_DE10PRO_BM_REG_STATUS,
           VX_DE10PRO_BM_STATUS_READY | VX_DE10PRO_BM_STATUS_FAULT);
  mmio.set(VX_DE10PRO_BM_REG_SENSOR_VALID, 0x1feu);
  // The temperature shadow still holds the previous round's value; the caller
  // must discard it on the SENSOR_VALID bit, not on it being unreadable.
  mmio.set(VX_DE10PRO_BM_REG_TEMP_MC, 41000);
  mmio.set(VX_DE10PRO_BM_REG_POWER0_RAW, 262143);
  mmio.set(VX_DE10PRO_BM_REG_POWER1_RAW, 5004);
  mmio.set(VX_DE10PRO_BM_REG_POWER0_LSB_NW, 208435);
  mmio.set(VX_DE10PRO_BM_REG_POWER1_LSB_NW, 5002440);
  mmio.set(VX_DE10PRO_BM_REG_SAMPLE_COUNT, 11);

  auto manager = make_manager(&mmio);
  expect(manager.probe() == BoardProbeResult::Available,
         "diagnostics-capable manager must probe");
  BoardTelemetry telemetry{};
  expect(manager.read_telemetry(&telemetry),
         "a faulted round must still return the sensors that committed");
  expect(telemetry.sensor_valid == 0x1feu,
         "per-sensor validity must come from SENSOR_VALID");
  expect(!(telemetry.sensor_valid & VX_DE10PRO_BM_SENSOR_TEMP),
         "the failed temperature sensor must report invalid");
  expect((telemetry.sensor_valid & VX_DE10PRO_BM_SENSOR_INPUT_POWER)
      && (telemetry.sensor_valid & VX_DE10PRO_BM_SENSOR_CORE_POWER),
         "both power sensors must report valid");
  expect(telemetry.power_raw[0] == 262143 && telemetry.power_raw[1] == 5004,
         "power readings must survive a faulted round");
}

void test_capability_gates() {
  FakeMmio mmio;
  populate_identity(&mmio, VX_DE10PRO_BM_CAP_CLOCK_READBACK);
  auto manager = make_manager(&mmio);
  expect(manager.probe() == BoardProbeResult::Available,
         "readback-only manager must probe");
  mmio.reads.clear();
  expect(!manager.supports_clock_control(),
         "partial clock capabilities must not enable control");
  expect(!manager.supports_fan_override(),
         "missing fan-override capability must disable manual control");
  expect(!manager.set_fan_control(VX_DE10PRO_BM_FAN_CONTROL_FULL_ON, 0),
         "capability gate must reject manual fan control");
  expect(manager.begin_clock_request(VX_DE10PRO_BM_CLOCK_HZ_200M, 1)
         == ClockRequestResult::NotSupported,
         "begin must reject missing dynamic-clock capabilities");
  ClockState state{};
  expect(manager.poll_clock_request(1, &state) == ClockPollResult::IoError,
         "poll must reject missing dynamic-clock capabilities");
  expect(mmio.reads.empty() && mmio.writes.empty(),
         "capability-gated clock operations must not touch clock CSRs");
}

void test_diagnostics() {
  FakeMmio mmio;
  populate_identity(&mmio, VX_DE10PRO_BM_CAP_DIAGNOSTICS);
  mmio.set(VX_DE10PRO_BM_REG_STATUS,
           VX_DE10PRO_BM_STATUS_READY | VX_DE10PRO_BM_STATUS_FAULT);
  mmio.set(VX_DE10PRO_BM_REG_SENSOR_VALID, 0x123u);
  mmio.set(VX_DE10PRO_BM_REG_I2C_ERROR, 0x25551234u);
  auto manager = make_manager(&mmio);
  expect(manager.probe() == BoardProbeResult::Available,
         "diagnostic manager must probe");
  BoardDiagnostics diagnostics{};
  expect(manager.read_diagnostics(&diagnostics),
         "diagnostic registers must be readable");
  expect(diagnostics.status
         == (VX_DE10PRO_BM_STATUS_READY | VX_DE10PRO_BM_STATUS_FAULT),
         "diagnostics must preserve board status");
  expect(diagnostics.sensor_valid == 0x123u,
         "diagnostics must preserve the sensor-valid bitmap");
  expect(VX_DE10PRO_BM_I2C_ERROR_COUNT_OF(diagnostics.i2c_error) == 0x1234u
      && VX_DE10PRO_BM_I2C_ERROR_STEP_OF(diagnostics.i2c_error) == 0x15u
      && VX_DE10PRO_BM_I2C_ERROR_BUS_OF(diagnostics.i2c_error) == 1u
      && (diagnostics.i2c_error & VX_DE10PRO_BM_I2C_ERROR_NACK),
         "diagnostics must decode the packed I2C error register");
}

void test_clock_request() {
  constexpr uint32_t capabilities = VX_DE10PRO_BM_CAP_CLOCK_READBACK
                                  | VX_DE10PRO_BM_CAP_DYNAMIC_CLOCK
                                  | VX_DE10PRO_BM_CAP_QUIESCE
                                  | VX_DE10PRO_BM_CAP_FAN_CONTROL
                                  | VX_DE10PRO_BM_CAP_FAN_OVERRIDE;
  FakeMmio mmio;
  populate_identity(&mmio, capabilities);
  mmio.set(VX_DE10PRO_BM_REG_CLOCK_STATUS,
           VX_DE10PRO_BM_CLOCK_CURRENT_VALID
         | VX_DE10PRO_BM_CLOCK_PLL_LOCKED);
  mmio.set(VX_DE10PRO_BM_REG_CLOCK_CUR_HZ,
           VX_DE10PRO_BM_CLOCK_HZ_250M);
  mmio.set(VX_DE10PRO_BM_REG_CLOCK_MEASURED_HZ, 249875000);
  mmio.set(VX_DE10PRO_BM_REG_FAN_CONTROL,
           VX_DE10PRO_BM_FAN_CONTROL_VALUE(
               VX_DE10PRO_BM_FAN_CONTROL_AUTO, 0x20));
  mmio.set(VX_DE10PRO_BM_REG_FAN_STATUS,
           VX_DE10PRO_BM_FAN_STATUS_VALID
         | VX_DE10PRO_BM_FAN_STATUS_FULL_ON
         | (0xa5u << VX_DE10PRO_BM_FAN_STATUS_DAC_SHIFT));
  mmio.set(VX_DE10PRO_BM_REG_CLOCK_DONE_SEQ, 8);
  mmio.set(VX_DE10PRO_BM_REG_QUIESCE_STATUS, 0);
  mmio.set(VX_DE10PRO_BM_REG_CLOCK_ERROR,
           VX_DE10PRO_BM_CLOCK_ERR_NONE);

  auto manager = make_manager(&mmio);
  expect(manager.probe() == BoardProbeResult::Available,
         "clock manager must probe");
  uint32_t measured_hz = 0;
  expect(manager.read_measured_clock_hz(&measured_hz)
         && measured_hz == 249875000,
         "measured clock must be read in Hz");
  uint32_t fan_status = 0;
  expect(manager.read_fan_status(&fan_status)
         && (fan_status & VX_DE10PRO_BM_FAN_STATUS_VALID)
         && (fan_status & VX_DE10PRO_BM_FAN_STATUS_FULL_ON)
         && VX_DE10PRO_BM_FAN_STATUS_DAC_OF(fan_status) == 0xa5u,
         "fan-control mode and DAC code must decode from the fixed fields");
  uint32_t fan_control = 0;
  expect(manager.read_fan_control(&fan_control)
         && VX_DE10PRO_BM_FAN_CONTROL_MODE_OF(fan_control)
             == VX_DE10PRO_BM_FAN_CONTROL_AUTO
         && VX_DE10PRO_BM_FAN_CONTROL_DAC_OF(fan_control) == 0x20u,
         "fan-control request must decode from the fixed fields");
  expect(!manager.set_fan_control(4, 0x20),
         "out-of-range fan mode must be rejected");
  expect(manager.set_fan_control(VX_DE10PRO_BM_FAN_CONTROL_FULL_OFF, 120),
         "full-off fan request must be written");
  expect(mmio.writes.back()
         == std::make_pair(uint64_t(VX_DE10PRO_BM_BAR0_BASE
                                  + VX_DE10PRO_BM_REG_FAN_CONTROL),
                          VX_DE10PRO_BM_FAN_CONTROL_VALUE(
                              VX_DE10PRO_BM_FAN_CONTROL_FULL_OFF, 120)),
         "full-off request must use the FAN_CONTROL register");
  expect(!manager.set_fan_control(VX_DE10PRO_BM_FAN_CONTROL_MANUAL_DAC,
                                  0x100),
         "out-of-range fan DAC must be rejected");
  expect(manager.set_fan_control(VX_DE10PRO_BM_FAN_CONTROL_MANUAL_DAC,
                                 0x44),
         "manual fan DAC request must be written");
  expect(mmio.writes.back()
         == std::make_pair(uint64_t(VX_DE10PRO_BM_BAR0_BASE
                                  + VX_DE10PRO_BM_REG_FAN_CONTROL),
                          VX_DE10PRO_BM_FAN_CONTROL_VALUE(
                              VX_DE10PRO_BM_FAN_CONTROL_MANUAL_DAC, 0x44)),
         "manual fan request must use the FAN_CONTROL register");
  mmio.writes.clear();
  uint32_t nominal_hz = 0;
  expect(manager.read_current_clock_hz(&nominal_hz)
         && nominal_hz == VX_DE10PRO_BM_CLOCK_HZ_250M,
         "nominal clock readback must use exact Hz");
  expect(manager.begin_clock_request(150000000, 9)
         == ClockRequestResult::InvalidArgument,
         "unsupported exact frequency must be rejected");
  expect(mmio.writes.empty(),
         "invalid frequency must not write any clock registers");
  mmio.set(VX_DE10PRO_BM_REG_CLOCK_STATUS, VX_DE10PRO_BM_CLOCK_BUSY);
  expect(manager.begin_clock_request(VX_DE10PRO_BM_CLOCK_HZ_200M, 9)
         == ClockRequestResult::Busy,
         "busy manager must reject a new request");
  expect(mmio.writes.empty(),
         "busy request must not write any clock registers");
  mmio.set(VX_DE10PRO_BM_REG_CLOCK_STATUS,
           VX_DE10PRO_BM_CLOCK_CURRENT_VALID
         | VX_DE10PRO_BM_CLOCK_PLL_LOCKED);
  const uint32_t legal_frequencies[] = {
      VX_DE10PRO_BM_CLOCK_HZ_100M,
      VX_DE10PRO_BM_CLOCK_HZ_125M,
      VX_DE10PRO_BM_CLOCK_HZ_200M,
      VX_DE10PRO_BM_CLOCK_HZ_250M,
  };
  uint32_t sequence = 1;
  for (uint32_t frequency_hz : legal_frequencies) {
    expect(manager.begin_clock_request(frequency_hz, sequence++)
           == ClockRequestResult::Started,
           "each public exact-Hz clock value must be accepted");
  }
  mmio.writes.clear();
  expect(manager.begin_clock_request(VX_DE10PRO_BM_CLOCK_HZ_200M, 9)
         == ClockRequestResult::Started,
         "valid exact-Hz clock request must start");
  const std::vector<std::pair<uint64_t, uint32_t>> expected_writes{
      {VX_DE10PRO_BM_BAR0_BASE + VX_DE10PRO_BM_REG_CLOCK_COMMAND,
       VX_DE10PRO_BM_CLOCK_CMD_CLEAR_ERROR},
      {VX_DE10PRO_BM_BAR0_BASE + VX_DE10PRO_BM_REG_CLOCK_REQ_HZ,
       VX_DE10PRO_BM_CLOCK_HZ_200M},
      {VX_DE10PRO_BM_BAR0_BASE + VX_DE10PRO_BM_REG_CLOCK_REQ_SEQ, 9},
      {VX_DE10PRO_BM_BAR0_BASE + VX_DE10PRO_BM_REG_CLOCK_COMMAND,
       VX_DE10PRO_BM_CLOCK_CMD_APPLY},
  };
  expect(mmio.writes == expected_writes,
         "clock request writes must follow clear-frequency-cookie-apply order");

  mmio.set(VX_DE10PRO_BM_REG_CLOCK_STATUS,
           VX_DE10PRO_BM_CLOCK_BUSY
         | VX_DE10PRO_BM_CLOCK_QUIESCE_REQUEST);
  ClockState state{};
  expect(manager.poll_clock_request(9, &state)
         == ClockPollResult::Pending,
         "busy request must remain pending");

  mmio.set(VX_DE10PRO_BM_REG_CLOCK_STATUS,
           VX_DE10PRO_BM_CLOCK_DONE
         | VX_DE10PRO_BM_CLOCK_CURRENT_VALID);
  mmio.set(VX_DE10PRO_BM_REG_CLOCK_DONE_SEQ, 8);
  expect(manager.poll_clock_request(9, &state)
         == ClockPollResult::Pending,
         "mismatched completion cookie must remain pending");

  mmio.set(VX_DE10PRO_BM_REG_CLOCK_STATUS,
           VX_DE10PRO_BM_CLOCK_DONE
         | VX_DE10PRO_BM_CLOCK_ERROR);
  mmio.set(VX_DE10PRO_BM_REG_CLOCK_DONE_SEQ, 9);
  mmio.set(VX_DE10PRO_BM_REG_CLOCK_ERROR,
           VX_DE10PRO_BM_CLOCK_ERR_PLL_UNLOCKED);
  expect(manager.poll_clock_request(9, &state)
         == ClockPollResult::Rejected,
         "matching error completion must be rejected");

  mmio.set(VX_DE10PRO_BM_REG_CLOCK_CUR_HZ,
           VX_DE10PRO_BM_CLOCK_HZ_200M);
  mmio.set(VX_DE10PRO_BM_REG_CLOCK_DONE_SEQ, 9);
  mmio.set(VX_DE10PRO_BM_REG_QUIESCE_STATUS,
           VX_DE10PRO_BM_QUIESCE_REQUIRED_MASK
         | VX_DE10PRO_BM_QUIESCE_COMPLETE);
  mmio.set(VX_DE10PRO_BM_REG_CLOCK_ERROR,
           VX_DE10PRO_BM_CLOCK_ERR_NONE);
  mmio.set(VX_DE10PRO_BM_REG_CLOCK_STATUS,
           VX_DE10PRO_BM_CLOCK_DONE
         | VX_DE10PRO_BM_CLOCK_PLL_LOCKED
         | VX_DE10PRO_BM_CLOCK_CURRENT_VALID
         | VX_DE10PRO_BM_CLOCK_QUIESCE_ACK);
  expect(manager.poll_clock_request(9, &state)
         == ClockPollResult::Complete,
         "matching completion cookie and valid current clock must complete");
  expect(state.current_hz == VX_DE10PRO_BM_CLOCK_HZ_200M,
         "completion must report the applied frequency");
  expect(state.quiesce_status
         == (VX_DE10PRO_BM_QUIESCE_REQUIRED_MASK
           | VX_DE10PRO_BM_QUIESCE_COMPLETE),
         "completion must preserve the two-stage quiesce status");

  mmio.failing_read_address = VX_DE10PRO_BM_BAR0_BASE
                            + VX_DE10PRO_BM_REG_CLOCK_ERROR;
  expect(manager.poll_clock_request(9, &state)
         == ClockPollResult::IoError,
         "clock-state read failure must report I/O error");
  expect(BoardManager::next_clock_sequence(8) == 9,
         "next cookie must increment the completed sequence");
  expect(BoardManager::next_clock_sequence(UINT32_MAX) == 1,
         "cookie wrap must skip the reserved zero value");
}

} // namespace

static_assert(VX_DE10PRO_BM_BAR0_BASE == 0x2000ull,
              "board-manager BAR0 base is part of ABI v1");
static_assert(VX_DE10PRO_BM_REG_MAGIC == 0x00u
           && VX_DE10PRO_BM_REG_CLOCK_REQ_HZ == 0x38u
           && VX_DE10PRO_BM_REG_CLOCK_CUR_HZ == 0x3cu
           && VX_DE10PRO_BM_REG_QUIESCE_STATUS == 0x50u
           && VX_DE10PRO_BM_REG_CLOCK_MEASURED_HZ == 0x58u
           && VX_DE10PRO_BM_REG_FAN_STATUS == 0x5cu
           && VX_DE10PRO_BM_REG_FAN_CONTROL == 0x60u
           && VX_DE10PRO_BM_REG_SENSOR_VALID == 0x64u
           && VX_DE10PRO_BM_REG_I2C_ERROR == 0x68u,
              "board-manager CSR offsets are part of ABI v1");
static_assert(VX_DE10PRO_BM_QUIESCE_REQUEST == 0x00000001u
           && VX_DE10PRO_BM_QUIESCE_SHELL_ACK == 0x00000002u
           && VX_DE10PRO_BM_QUIESCE_MEMORY_DRAIN_ACK == 0x00000004u
           && VX_DE10PRO_BM_QUIESCE_REQUIRED_MASK == 0x00000007u
           && VX_DE10PRO_BM_QUIESCE_COMPLETE == 0x80000000u,
              "quiesce status bits are part of ABI v1");
static_assert((VX_DE10PRO_BM_REG_FAN_STATUS & 3u) == 0,
              "board-manager registers must be word aligned");
static_assert(VX_DE10PRO_BM_REG_FAN_CONTROL
              < VX_DE10PRO_BM_APERTURE_SIZE,
              "board-manager registers must fit their BAR0 aperture");

int main() {
  test_probe_fallback();
  test_telemetry_and_units();
  test_partial_round_telemetry();
  test_capability_gates();
  test_diagnostics();
  test_clock_request();
  if (failures != 0) {
    std::fprintf(stderr, "%d board-manager tests failed\n", failures);
    return 1;
  }
  std::printf("DE10-Pro board-manager tests passed\n");
  return 0;
}
