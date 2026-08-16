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
               "usage: %s [--clock-hz HZ] [--timeout-ms MS]\n"
               "       HZ: 100000000, 125000000, 200000000, or 250000000\n",
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
          ? "full-on" : "dac";
      std::printf("fan.mode=%s\n", mode);
      std::printf("fan.dac=%u\n",
                  VX_DE10PRO_BM_FAN_STATUS_DAC_OF(fan_status));
    }
  } else {
    std::printf("fan.mode=unavailable\n");
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
    std::printf("temperature.mc=%d\n", telemetry.temperature_mc);
  }
  if (telemetry.capabilities & VX_DE10PRO_BM_CAP_FAN) {
    std::printf("fan.rpm=%u\n", telemetry.fan_rpm);
  }
  for (uint32_t channel = 0; channel < 2; ++channel) {
    const uint32_t capability = channel == 0
        ? VX_DE10PRO_BM_CAP_POWER0 : VX_DE10PRO_BM_CAP_POWER1;
    if (!(telemetry.capabilities & capability)) {
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

} // namespace

int main(int argc, char** argv) {
  uint32_t frequency_hz = 0;
  bool request_frequency = false;
  uint32_t timeout_ms = kDefaultTimeoutMs;
  for (int index = 1; index < argc; ++index) {
    if (std::strcmp(argv[index], "--clock-hz") == 0
     && index + 1 < argc
     && parse_u32(argv[++index], &frequency_hz)) {
      request_frequency = true;
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
  const int result = request_frequency
      ? request_clock(manager, frequency_hz, timeout_ms) : 0;
  drv_close(handle);
  return result;
}
