// Copyright © 2019-2023
// Licensed under the Apache License, Version 2.0.

#include "board_manager.h"
#include "driver.h"
#include "vortex_afu.h"

#include <de10pro_board_manager_abi.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <thread>

using vortex::de10pro::BoardManager;
using vortex::de10pro::BoardManagerIo;
using vortex::de10pro::BoardDiagnostics;
using vortex::de10pro::BoardProbeResult;
using vortex::de10pro::BoardTelemetry;
using vortex::de10pro::ClockPollResult;
using vortex::de10pro::ClockRequestResult;
using vortex::de10pro::ClockState;

namespace {

// The RTL reaches a terminal state in less than 5001 ms after accepting a
// request. Leave additional margin for PCIe reads and host scheduling.
constexpr uint32_t kHardwareRequestBoundMs = 5001;
constexpr uint32_t kDefaultTimeoutMs = 7000;
static_assert(kDefaultTimeoutMs > kHardwareRequestBoundMs,
              "host timeout must exceed the RTL request bound");

bool parse_u32(const char* text, uint32_t* value) {
  if (text == nullptr || value == nullptr || text[0] == '\0') {
    return false;
  }
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(text, &end, 0);
  if (end == text || *end != '\0'
   || parsed > std::numeric_limits<uint32_t>::max()) {
    return false;
  }
  *value = static_cast<uint32_t>(parsed);
  return true;
}

uint32_t env_u32(const char* name, uint32_t default_value) {
  const char* text = std::getenv(name);
  uint32_t value = 0;
  return parse_u32(text, &value) ? value : default_value;
}

void print_usage(const char* program) {
  std::fprintf(stderr,
               "usage: %s [--clock-hz HZ] [--fan-auto | --fan-full | --fan-percent PERCENT | --fan-dac DAC]\n"
               "          [--timeout-ms MS]\n"
               "       HZ: 100000000, 125000000, 200000000, or 250000000\n"
               "       PERCENT: Terasic-compatible fan power from 0 through 100\n"
               "       DAC: raw MAX6651 value from 0x00 through 0xff\n",
               program);
}

void print_telemetry(const BoardManager& manager) {
  uint32_t nominal_hz = 0;
  if (manager.read_current_clock_hz(&nominal_hz)) {
    std::printf("clock.nominal_hz=%u\n", nominal_hz);
  } else {
    std::printf("clock.nominal_hz=unavailable\n");
  }

  uint32_t measured_hz = 0;
  if (manager.read_measured_clock_hz(&measured_hz)) {
    std::printf("clock.measured_hz=%u\n", measured_hz);
  } else {
    std::printf("clock.measured_hz=unavailable\n");
  }

  uint32_t fan_status = 0;
  if (manager.read_fan_status(&fan_status)) {
    if (!(fan_status & VX_DE10PRO_BM_FAN_STATUS_VALID)) {
      std::printf("fan.mode=unknown\n");
    } else {
      const char* mode = fan_status & VX_DE10PRO_BM_FAN_STATUS_FULL_ON
          ? "full-on"
          : fan_status & VX_DE10PRO_BM_FAN_STATUS_FULL_OFF
              ? "full-off" : "dac";
      std::printf("fan.mode=%s\n", mode);
      std::printf("fan.dac=%u\n",
                  VX_DE10PRO_BM_FAN_STATUS_DAC_OF(fan_status));
    }
  } else {
    std::printf("fan.mode=unavailable\n");
  }

  BoardDiagnostics diagnostics{};
  if (manager.read_diagnostics(&diagnostics)) {
    static const char* const buses[] = {"temp", "fan", "power", "unknown"};
    static const char* const steps[] = {
        "fan-config", "fan-count", "input-config", "core-config",
        "temp-local", "temp-remote", "policy-dac", "policy-config",
        "tach0", "tach1", "input-sense", "input-vin", "input-power",
        "core-sense", "core-vin", "core-power", "fault-full-on",
        "commit"};
    const uint32_t step = VX_DE10PRO_BM_I2C_ERROR_STEP_OF(
        diagnostics.i2c_error);
    const uint32_t bus = VX_DE10PRO_BM_I2C_ERROR_BUS_OF(
        diagnostics.i2c_error);
    std::printf("telemetry.status=0x%08x\n", diagnostics.status);
    std::printf("sensor.valid=0x%03x\n",
                diagnostics.sensor_valid & VX_DE10PRO_BM_SENSOR_VALID_MASK);
    if (VX_DE10PRO_BM_VERSION_MINOR_OF(manager.version()) >= 5) {
      std::printf("i2c.drive_fault_scl=0x%x\n",
                  VX_DE10PRO_BM_DRIVE_FAULT_SCL_OF(diagnostics.sensor_valid));
      std::printf("i2c.drive_fault_sda=0x%x\n",
                  VX_DE10PRO_BM_DRIVE_FAULT_SDA_OF(diagnostics.sensor_valid));
    }
    std::printf("i2c.error_count=%u\n",
                VX_DE10PRO_BM_I2C_ERROR_COUNT_OF(diagnostics.i2c_error));
    std::printf("i2c.last_step=%u:%s\n", step,
                step < sizeof(steps) / sizeof(steps[0])
                    ? steps[step] : "unknown");
    std::printf("i2c.last_bus=%u:%s\n", bus, buses[bus]);
    static const char* const bytes[] = {
        "device-address-write", "register-address", "write-data",
        "device-address-read"};
    std::printf("i2c.error_nack=%u\n",
                !!(diagnostics.i2c_error & VX_DE10PRO_BM_I2C_ERROR_NACK));
    if (VX_DE10PRO_BM_VERSION_MINOR_OF(manager.version()) >= 4) {
      std::printf("i2c.error_byte=%u:%s\n",
                  VX_DE10PRO_BM_I2C_ERROR_BYTE_OF(diagnostics.i2c_error),
                  bytes[VX_DE10PRO_BM_I2C_ERROR_BYTE_OF(diagnostics.i2c_error)]);
    }
    std::printf("i2c.error_timeout=%u\n",
                !!(diagnostics.i2c_error & VX_DE10PRO_BM_I2C_ERROR_TIMEOUT));
    std::printf("i2c.error_bus_stuck=%u\n",
                !!(diagnostics.i2c_error
                    & VX_DE10PRO_BM_I2C_ERROR_BUS_STUCK));
    std::printf("i2c.error_short_read=%u\n",
                !!(diagnostics.i2c_error
                    & VX_DE10PRO_BM_I2C_ERROR_SHORT_READ));
  }

  BoardTelemetry telemetry{};
  if (!manager.read_telemetry(&telemetry)) {
    return;
  }
  std::printf("sample.count=%u\n", telemetry.sample_count);
  if (telemetry.capabilities & VX_DE10PRO_BM_CAP_TIMESTAMP) {
    std::printf("sample.timestamp_ticks=%llu\n",
                static_cast<unsigned long long>(telemetry.timestamp));
    std::printf("sample.timestamp_hz=%u\n", telemetry.timestamp_hz);
  }
  if (telemetry.capabilities & VX_DE10PRO_BM_CAP_TEMPERATURE) {
    if (telemetry.sensor_valid & VX_DE10PRO_BM_SENSOR_TEMP) {
      std::printf("temperature.mc=%d\n", telemetry.temperature_mc);
    } else {
      std::printf("temperature.mc=unavailable\n");
    }
  }
  if (telemetry.capabilities & VX_DE10PRO_BM_CAP_FAN) {
    if (telemetry.sensor_valid & VX_DE10PRO_BM_SENSOR_TACH0) {
      std::printf("fan.rpm=%u\n", telemetry.fan_rpm);
    } else {
      std::printf("fan.rpm=unavailable\n");
    }
  }
  for (uint32_t channel = 0; channel < 2; ++channel) {
    const uint32_t capability = channel == 0
        ? VX_DE10PRO_BM_CAP_POWER0 : VX_DE10PRO_BM_CAP_POWER1;
    const uint32_t sensor = channel == 0
        ? VX_DE10PRO_BM_SENSOR_INPUT_POWER : VX_DE10PRO_BM_SENSOR_CORE_POWER;
    if (!(telemetry.capabilities & capability)) {
      continue;
    }
    if (!(telemetry.sensor_valid & sensor)) {
      std::printf("power%u.uw=unavailable\n", channel);
      continue;
    }
    std::printf("power%u.raw=%u\n", channel,
                telemetry.power_raw[channel]);
    std::printf("power%u.lsb_nw=%u\n", channel,
                telemetry.power_lsb_nw[channel]);
    if (telemetry.power_lsb_nw[channel] != 0) {
      const uint64_t microwatts = BoardManager::power_raw_to_microwatts(
          telemetry.power_raw[channel], telemetry.power_lsb_nw[channel]);
      std::printf("power%u.uw=%llu\n", channel,
                  static_cast<unsigned long long>(microwatts));
    }
  }
}

int request_clock(const BoardManager& manager, uint32_t frequency_hz,
                  uint32_t timeout_ms) {
  if (!manager.supports_clock_control()) {
    std::fprintf(stderr, "dynamic clock control is not supported\n");
    return 1;
  }

  ClockState initial{};
  if (manager.poll_clock_request(0, &initial) == ClockPollResult::IoError) {
    std::fprintf(stderr, "failed to read initial clock state\n");
    return 1;
  }
  const uint32_t sequence = BoardManager::next_clock_sequence(
      initial.completed_sequence);
  const auto begin = manager.begin_clock_request(frequency_hz, sequence);
  if (begin != ClockRequestResult::Started) {
    std::fprintf(stderr, "clock request was not started: result=%u\n",
                 static_cast<unsigned int>(begin));
    return 1;
  }

  const auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds(timeout_ms);
  bool host_timed_out = false;
  for (;;) {
    ClockState state{};
    const auto poll = manager.poll_clock_request(sequence, &state);
    if (poll == ClockPollResult::Complete) {
      if (state.current_hz != frequency_hz) {
        std::fprintf(stderr,
                     "clock request completed at unexpected rate: %u Hz\n",
                     state.current_hz);
        return 1;
      }
      std::printf("clock.nominal_hz=%u\n", state.current_hz);
      return host_timed_out ? 1 : 0;
    }
    if (poll == ClockPollResult::IoError) {
      std::fprintf(stderr,
                   "clock-status I/O failed; hardware state is unknown and cannot be polled safely\n");
      return 1;
    }
    if (poll == ClockPollResult::Rejected) {
      std::fprintf(stderr,
                   "clock request failed: poll=%u error=%u status=0x%08x quiesce=0x%08x\n",
                   static_cast<unsigned int>(poll), state.error,
                   state.status, state.quiesce_status);
      return 1;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!host_timed_out && (timeout_ms == 0 || now >= deadline)) {
      std::fprintf(stderr,
                   "clock request timed out; retaining the device lock until hardware reaches a terminal state\n");
      host_timed_out = true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

int request_fan(const BoardManager& manager, uint32_t mode, uint32_t dac,
                uint32_t timeout_ms) {
  if (!manager.supports_fan_override()) {
    std::fprintf(stderr, "manual fan control is not supported\n");
    return 1;
  }
  if (!manager.set_fan_control(mode, dac)) {
    std::fprintf(stderr, "fan-control request was rejected\n");
    return 1;
  }
  if (mode == VX_DE10PRO_BM_FAN_CONTROL_AUTO) {
    std::printf("fan.control=auto\n");
    return 0;
  }

  const auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds(timeout_ms);
  for (;;) {
    uint32_t control = 0;
    uint32_t status = 0;
    if (!manager.read_fan_control(&control)
     || !manager.read_fan_status(&status)) {
      std::fprintf(stderr, "failed to read fan-control state\n");
      return 1;
    }
    const bool control_matches =
        VX_DE10PRO_BM_FAN_CONTROL_MODE_OF(control) == mode
     && VX_DE10PRO_BM_FAN_CONTROL_DAC_OF(control) == dac;
    const bool status_valid = status & VX_DE10PRO_BM_FAN_STATUS_VALID;
    const bool status_matches = mode == VX_DE10PRO_BM_FAN_CONTROL_FULL_ON
        ? (status & VX_DE10PRO_BM_FAN_STATUS_FULL_ON)
        : mode == VX_DE10PRO_BM_FAN_CONTROL_FULL_OFF
            ? (status & VX_DE10PRO_BM_FAN_STATUS_FULL_OFF)
            : (!(status & (VX_DE10PRO_BM_FAN_STATUS_FULL_ON
                         | VX_DE10PRO_BM_FAN_STATUS_FULL_OFF))
            && VX_DE10PRO_BM_FAN_STATUS_DAC_OF(status) == dac);
    if (control_matches && status_valid && status_matches) {
      std::printf("fan.control=%s\n",
                  mode == VX_DE10PRO_BM_FAN_CONTROL_FULL_ON
                      ? "full-on"
                      : mode == VX_DE10PRO_BM_FAN_CONTROL_FULL_OFF
                          ? "full-off" : "manual-dac");
      std::printf("fan.dac=%u\n", dac);
      return 0;
    }
    if (timeout_ms == 0 || std::chrono::steady_clock::now() >= deadline) {
      std::fprintf(stderr, "fan-control request timed out\n");
      return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

} // namespace

int main(int argc, char** argv) {
  uint32_t frequency_hz = 0;
  bool request_frequency = false;
  bool request_fan_control = false;
  bool request_fan_percent = false;
  uint32_t fan_mode = VX_DE10PRO_BM_FAN_CONTROL_AUTO;
  uint32_t fan_dac = 0x20;
  uint32_t fan_percent = 0;
  uint32_t timeout_ms = kDefaultTimeoutMs;
  for (int index = 1; index < argc; ++index) {
    if (std::strcmp(argv[index], "--clock-hz") == 0
     && index + 1 < argc
     && parse_u32(argv[++index], &frequency_hz)) {
      request_frequency = true;
      continue;
    }
    if (std::strcmp(argv[index], "--fan-auto") == 0
     && !request_fan_control) {
      request_fan_control = true;
      fan_mode = VX_DE10PRO_BM_FAN_CONTROL_AUTO;
      continue;
    }
    if (std::strcmp(argv[index], "--fan-full") == 0
     && !request_fan_control) {
      request_fan_control = true;
      fan_mode = VX_DE10PRO_BM_FAN_CONTROL_FULL_ON;
      continue;
    }
    if (std::strcmp(argv[index], "--fan-percent") == 0
     && !request_fan_control
     && index + 1 < argc
     && parse_u32(argv[++index], &fan_percent)
     && BoardManager::fan_percent_to_control(fan_percent, &fan_mode,
                                             &fan_dac)) {
      request_fan_control = true;
      request_fan_percent = true;
      continue;
    }
    if (std::strcmp(argv[index], "--fan-dac") == 0
     && !request_fan_control
     && index + 1 < argc
     && parse_u32(argv[++index], &fan_dac)
     && fan_dac <= 0xffu) {
      request_fan_control = true;
      fan_mode = VX_DE10PRO_BM_FAN_CONTROL_MANUAL_DAC;
      continue;
    }
    if (std::strcmp(argv[index], "--timeout-ms") == 0
     && index + 1 < argc
     && parse_u32(argv[++index], &timeout_ms)) {
      continue;
    }
    if (std::strcmp(argv[index], "--help") == 0) {
      print_usage(argv[0]);
      return 0;
    }
    print_usage(argv[0]);
    return 2;
  }

  const uint32_t bdf = env_u32("DE10PRO_PCIE_BDF",
                               VX_DE10PRO_DEFAULT_BDF);
  const auto bar = static_cast<pcie_bar_t>(
      env_u32("DE10PRO_VX_BAR", VX_DE10PRO_DEFAULT_BAR));
  auto handle = drv_open(bdf, bar, 0);
  if (handle == nullptr) {
    std::fprintf(stderr, "PCIe open failed: %s\n", drv_get_last_error());
    return 1;
  }

  BoardManagerIo io{
      handle,
      [](void* context, uint64_t address, uint32_t* value) {
        return drv_read32(context, address, value);
      },
      [](void* context, uint64_t address, uint32_t value) {
        return drv_write32(context, address, value);
      }};
  BoardManager manager(io);
  const auto probe = manager.probe();
  if (probe != BoardProbeResult::Available) {
    std::fprintf(stderr, "board manager unavailable: probe=%u\n",
                 static_cast<unsigned int>(probe));
    drv_close(handle);
    return 2;
  }

  std::printf("board.version=%u.%u\n",
              VX_DE10PRO_BM_VERSION_MAJOR_OF(manager.version()),
              VX_DE10PRO_BM_VERSION_MINOR_OF(manager.version()));
  std::printf("board.capabilities=0x%08x\n", manager.capabilities());
  print_telemetry(manager);
  int result = 0;
  if (request_fan_control) {
    result = request_fan(manager, fan_mode, fan_dac, timeout_ms);
  }
  if (result == 0 && request_fan_percent) {
    std::printf("fan.percent=%u\n", fan_percent);
  }
  if (result == 0 && request_frequency) {
    result = request_clock(manager, frequency_hz, timeout_ms);
  }
  drv_close(handle);
  return result;
}
