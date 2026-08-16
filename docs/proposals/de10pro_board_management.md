# DE10-Pro Board Management, Telemetry, and Runtime Clock Control

**Scope:** `sw/common/de10pro_board_manager_abi.h`,
`sw/runtime/de10pro/`, the DE10-Pro Platform Designer system, and board-level
clock/reset constraints
**Status:** Proposal
**Target:** Quartus Prime Pro 19.2, Stratix 10 DE10-Pro, one SOF with runtime
clock profiles

---

## 1. Motivation

The DE10-Pro runtime currently assumes `VX_CFG_PLATFORM_CLOCK_RATE`. That is
correct for a fixed clock, but becomes stale after an IOPLL runtime
reconfiguration. The software also has no stable interface for board
temperature, fan speed, or power-monitor samples.

This proposal adds a small versioned board manager in BAR0. It is optional:
existing SOFs without the block remain valid and continue to report the
compile-time clock rate. A compatible manager provides telemetry and safely
selects one of the IOPLL profiles compiled into the same SOF.

The board manager is a control plane, not part of the kernel ABI. Crypto and
other device kernels do not change.

---

## 2. Invariants

1. BAR0 offsets `0x2000–0x20ff` belong to the board manager. The Vortex AFU
   control window remains at `0x1000`.
2. Every register is a naturally aligned, little-endian 32-bit word.
3. Software reads magic, version, and capabilities before any other use.
4. The management FSM and IOPLL reconfiguration port run from free-running
   50 MHz `CLK_50_B2C`. The IOPLL reference input runs from independent 50 MHz
   `CLK_50_B3I`; neither path depends on `vortex_clk` or the 250 MHz PCIe HIP
   clock.
5. PCIe, DMA, DDR4 PHY/user clocks, and the existing HIP-to-EMIF crossings do
   not change frequency. Only Vortex and its two dedicated control/memory CDC
   boundaries use `vortex_clk`.
6. A clock request may advance to reset and reconfiguration only after the
   shell source-side acknowledgement and the target-side post-CDC memory-drain
   acknowledgement remain asserted throughout the guard interval.
7. One SOF contains the 100, 125, 200, and 250 MHz runtime profiles. The
   initial profile is 250 MHz, and static timing still covers every profile (§9).

---

## 3. Version and capability discovery

`MAGIC` is `0x5658424d` (`VXBM`). `VERSION[31:16]` is the major version and
`VERSION[15:0]` the minor version. Version 1.0 is `0x00010000`.

- A failed magic read or magic mismatch means no board manager. Runtime device
  open continues normally.
- An unknown major version is incompatible and is not accessed further.
- A newer minor version is compatible when software uses only advertised
  capabilities.
- A failed read after a valid identity is an I/O error, not proof that the
  feature is absent.

Capabilities:

| Bit | Name | Meaning |
|---:|---|---|
| 0 | `TELEMETRY` | Coherent sampled telemetry is implemented |
| 1 | `TEMPERATURE` | Signed millidegrees-Celsius sample is valid |
| 2 | `FAN` | Fan RPM sample is valid |
| 3 | `POWER0` | Power channel 0 raw code and scale are valid |
| 4 | `POWER1` | Power channel 1 raw code and scale are valid |
| 5 | `TIMESTAMP` | Sample timestamp and timebase are valid |
| 6 | `FAN_CONTROL` | `FAN_STATUS` mode, validity, and DAC readback are implemented |
| 8 | `CLOCK_READBACK` | Nominal and measured clock readback is implemented |
| 9 | `DYNAMIC_CLOCK` | Runtime IOPLL profile selection is implemented |
| 10 | `QUIESCE` | Source-side and post-CDC drain acknowledgements are implemented |

`STATUS` bit 0 is manager ready, bit 1 is telemetry valid, and bit 2 is a
board-manager fault. Reserved bits read as zero.

---

## 4. BAR0 CSR ABI v1.0

| Offset | Access | Register | Encoding |
|---:|:---:|---|---|
| `0x00` | RO | `MAGIC` | `0x5658424d` |
| `0x04` | RO | `VERSION` | major/minor |
| `0x08` | RO | `CAPABILITIES` | capability bitmap |
| `0x0c` | RO | `STATUS` | ready/telemetry-valid/fault |
| `0x10` | RO | `TEMP_MC` | signed two's-complement millidegrees Celsius |
| `0x14` | RO | `FAN_RPM` | revolutions per minute; zero means stopped |
| `0x18` | RO | `POWER0_RAW` | unsigned sensor-native sample code |
| `0x1c` | RO | `POWER1_RAW` | unsigned sensor-native sample code |
| `0x20` | RO | `POWER0_LSB_NW` | nanowatts represented by one channel-0 raw LSB |
| `0x24` | RO | `POWER1_LSB_NW` | nanowatts represented by one channel-1 raw LSB |
| `0x28` | RO | `SAMPLE_COUNT` | increments after a complete snapshot is latched |
| `0x2c` | RO | `TIMESTAMP_LO` | low word of sample timestamp |
| `0x30` | RO | `TIMESTAMP_HI` | high word of sample timestamp |
| `0x34` | RO | `TIMESTAMP_HZ` | timestamp ticks per second |
| `0x38` | RW | `CLOCK_REQ_HZ` | exact requested Vortex frequency in Hz |
| `0x3c` | RO | `CLOCK_CUR_HZ` | active nominal Vortex frequency in Hz |
| `0x40` | WO | `CLOCK_COMMAND` | one-cycle command bits |
| `0x44` | RO | `CLOCK_STATUS` | clock FSM state |
| `0x48` | RW | `CLOCK_REQ_SEQ` | nonzero software request cookie |
| `0x4c` | RO | `CLOCK_DONE_SEQ` | cookie of the last completed request |
| `0x50` | RO | `QUIESCE_STATUS` | request, source/target acknowledgements, completion |
| `0x54` | RO | `CLOCK_ERROR` | stable error code for the completed request |
| `0x58` | RO | `CLOCK_MEASURED_HZ` | measured Vortex Hz; zero means unavailable |
| `0x5c` | RO | `FAN_STATUS` | bit 0 full-on; bit 1 valid; bits 15:8 fan DAC code |

`CLOCK_REQ_HZ` accepts exactly 100,000,000, 125,000,000, 200,000,000,
and 250,000,000 Hz in ABI v1. `CLOCK_CUR_HZ` is the nominal selected rate;
`CLOCK_MEASURED_HZ` is estimated against the stable 50 MHz `CLK_50_B2C`
management clock.
Hardware rejects every other request without choosing a nearby rate. Profile
IDs, MIF indices, and PLL parameters are private hardware implementation details.

`FAN_STATUS.FULL_ON` is bit 0, `FAN_STATUS.VALID` is bit 1, and
`FAN_STATUS.DAC` is bits 15:8. `VALID` remains zero after reset until a
MAX6651 configuration write is acknowledged. While it is zero, software
reports the fan-control mode as unknown. When `VALID=1` and `FULL_ON=1`, the
DAC field is reported but ignored by the fan drive; otherwise the 8-bit DAC
code controls the configured fan level. All other bits read zero.

Power conversion is:

```text
power_uW = round(POWER_RAW * POWER_LSB_NW / 1000)
```

The raw word remains authoritative. A board revision may use a different
monitor or shunt while keeping software conversion stable by changing the LSB
scale. A scale of zero means that only the raw code is meaningful.

Timestamp conversion is:

```text
sample_ns = TIMESTAMP * 1,000,000,000 / TIMESTAMP_HZ
```

Software obtains an untorn snapshot by reading `SAMPLE_COUNT`, then all desired
sample registers, then `SAMPLE_COUNT` again. It retries if the counts differ.

---

## 5. Clock request protocol

`CLOCK_COMMAND` uses write-one pulses:

| Bit | Command |
|---:|---|
| 0 | Apply `CLOCK_REQ_HZ` and `CLOCK_REQ_SEQ` |
| 1 | Clear sticky clock error/done state |

`CLOCK_STATUS` bits are `BUSY`, `DONE`, `ERROR`, `PLL_LOCKED`,
`CURRENT_VALID`, `QUIESCE_REQUEST`, `QUIESCE_ACK`, and `RESET_ASSERTED` in
bits 0 through 7 respectively. `DONE` and `ERROR` are sticky until clear or a
new accepted request.

Software serializes requests and performs:

1. Check `CLOCK_READBACK | DYNAMIC_CLOCK | QUIESCE` capabilities.
2. Reject a frequency outside 100, 125, 200, or 250 MHz, a zero cookie, or a
   request while `BUSY` is set.
3. Write clear-error, the exact frequency in Hz, a new nonzero request cookie,
   then `APPLY`.
4. Poll without holding a PCIe Avalon transaction open.
5. Finish only when `BUSY=0`, `CLOCK_DONE_SEQ` equals the submitted cookie,
   `DONE=1`, `ERROR=0`, and `CURRENT_VALID=1`.
6. Require `CLOCK_CUR_HZ` to equal the request exactly. Independently measured
   Hz remains available through a separate telemetry read. Software never
   handles a profile or MIF index.

The request is non-cancellable. A host timeout marks the command unsuccessful,
but the board tool retains its device handle and advisory lock and keeps
polling until hardware reports the matching terminal cookie. It never returns
normally while the request is `BUSY`. An unrecoverable BAR I/O error is the
only case where software can no longer verify the hardware state.

The quiesce wait and drain guard share one non-restarting one-second budget;
falling acknowledgements return to the quiesce wait without resetting that
budget. Reset assertion, reconfiguration acceptance, PLL-lock wait, and reset
release then each have a one-second watchdog. An accepted request therefore
reaches a terminal state in less than 5.001 seconds.

Clock error codes are: none, invalid frequency, busy, quiesce timeout,
reconfiguration timeout, PLL unlocked, and internal error.

---

## 6. Quiesce contract

`QUIESCE_STATUS` is a live 32-bit status word:

| Bit | Name | Meaning |
|---:|---|---|
| 0 | `REQUEST` | the manager is holding the clock-change request active |
| 1 | `SHELL_ACK` | the Vortex-clock source side has quiesced |
| 2 | `MEMORY_DRAIN_ACK` | the target-side monitor after the memory CDC has drained |
| 31 | `COMPLETE` | both acknowledgements are asserted |

Bits 30:3 are reserved and read zero. `REQUIRED_MASK` is `0x00000007`; a
complete observation contains all three required low bits plus bit 31.

`SHELL_ACK` asserts after the request is synchronized into `vortex_clk`, new
RUN launches are blocked, the AFU is idle, source-side memory and control
traffic is quiet, and accepted source-side reads have returned. The source
conditions must remain quiet while the request is held.

`MEMORY_DRAIN_ACK` comes from a transparent monitor immediately after
`vortex_mem_cdc.m0`. It counts read burst beats accepted by the target and
waits for every response beat. A write is drained only when it is accepted at
the target-side downstream handshake; a stalled command remains activity. The
acknowledgement asserts only after the outstanding-read count is zero and the
configured target-clock quiet window has elapsed without a command or response.
It drops immediately if activity resumes.

The B2C-domain manager synchronizes both acknowledgements and requires them to
remain asserted for a separate guard interval. If either drops during the
guard, the FSM returns to the quiesce wait instead of asserting reset. Only
after the guard completes may it assert the internal AFU/core reset. The drained
CDC bridges remain running so their FIFO pointers are never reset from only one
clock domain.

BAR accesses to the board manager remain responsive during quiesce and clock
reconfiguration. The board manager is therefore a separate BAR slave, not a
register block behind `vortex_shell.ctrl`.

---

## 7. Hardware clock and reset sequence

Quartus 19.2's Stratix 10 IOPLL Reconfig IP uses MIF streaming. Each supported
frequency is generated by the IOPLL parameter editor and appended to one MIF.
The board manager maps the four exact public Hz values to private MIF indices.
Runtime writes neither M, N, C, loop-filter, nor calibration registers directly.

The 50 MHz `CLK_50_B2C` manager FSM performs:

1. Assert the clock-change request; the shell synchronizes it into `vortex_clk`,
   blocks new RUN launches, and produces the source-side acknowledgement.
2. Wait for the independent memory-drain acknowledgement from the target-side
   monitor after `vortex_mem_cdc`.
3. Require both acknowledgements to remain asserted through the guard interval;
   return to the wait state if either drops.
4. Assert the internal AFU/core reset while the old clock is still valid, then
   wait for the AFU reset-active acknowledgement. The drained CDC bridges remain
   running.
5. Start the selected MIF-streaming operation and wait for the reconfiguration
   handshake and PLL lock, all with bounded timeouts.
6. Require `locked` stable for 16 consecutive B2C samples, update
   `CLOCK_CUR_HZ`, and deassert the internal AFU/core reset.
7. Wait for reset-active to clear after the synchronous reset-release delay.
8. Copy the request cookie to
   `CLOCK_DONE_SEQ`, release the launch block, and set `DONE`.

An invalid request or a quiesce timeout releases the request without first
asserting AFU reset. A failure after reset assertion keeps the AFU/core reset,
while the CDC bridges, PCIe, and DDR4 remain running. Hardware records
`CLOCK_ERROR` and completes the cookie with `ERROR=1`. PCIe app reset may reset
Vortex state, but must not be the IOPLL management clock or reset source.

---

## 8. Runtime behavior and tool

The DE10-Pro backend probes the identity once at open. It reads magic, then
version, rejects an unknown major before reading capabilities, and finally
reads capabilities. Board-manager absence is not an error; a read failure
after recognized identity fails device open. `VX_CAPS_CLOCK_RATE` behaves as
follows:

- compatible manager plus valid `CLOCK_READBACK`: read `CLOCK_CUR_HZ` and
  round to the nearest MHz because the existing public capability unit is MHz;
- manager absent, incompatible, or without readback: retain
  `VX_CFG_PLATFORM_CLOCK_RATE`;
- valid manager followed by a BAR/readback failure: return device-lost rather
  than silently reporting stale compile-time data.

The clock-rate query crosses the runtime/backend shared-library boundary via
the optional `vx_dev_platform_query` symbol. The required `callbacks_t` remains
the original six-pointer ABI, so an older runtime can load a newer backend and
a newer runtime can load an older backend. If the optional symbol is absent,
the common runtime uses the compile-time clock rate.

`vortex-de10pro-boardctl` is the board-specific user entry point. With no
arguments it prints identity, nominal and measured clocks, fan-control mode,
and available telemetry. With `--clock-hz HZ` it submits one cookie and polls
the complete protocol; the
optional `--timeout-ms MS` controls when the request is reported as timed out;
the default is 7000 ms, leaving margin beyond the RTL's 5.001-second upper
bound for PCIe reads and host scheduling. A timed-out, non-cancellable request
is still polled to its terminal cookie while the lock is held, including beyond
that bound if hardware behaves anomalously. It uses the same BDF and BAR
environment settings as the DE10-Pro runtime. The runtime and board tool
hold an exclusive advisory lock on the PCIe character device for the lifetime
of their handle, so a clock request cannot overlap a cooperating runtime or
another board-tool transaction. The board tool opens without allocating the
driver's per-device DMA staging buffer. Hardware quiesce remains mandatory for
in-flight work left by a crashed or legacy process. Runtime device open also
refuses a compatible manager that reports a clock transition still `BUSY`.

---

## 9. Timing constraints and one-SOF sign-off

Runtime reconfiguration does not cause TimeQuest to discover every MIF profile.
The generated PLL clock is analyzed for the initial 250 MHz compiled profile
only unless additional scenarios are supplied. One deployed SOF therefore does
not mean one STA scenario.

Required sign-off:

- constrain manager/reconfiguration clock `CLK_50_B2C` and IOPLL reference
  `CLK_50_B3I` at 20.000 ns, and retain generated PLL clocks;
- analyze every allowed Vortex frequency and its PLL uncertainty, either with
  per-profile analysis revisions or explicit generated-clock scenarios;
- mark alternative clocks on the same output logically exclusive;
- close setup, hold, recovery/removal, and minimum pulse width at all corners;
- run the implementation with the fastest profile as the primary constraint;
- verify all clock transfers between Vortex, HIP, and DDR traverse only the
  intended explicit CDC bridges.

Analysis-only per-profile builds are permitted and recommended. Hardware
testing still programs one SOF and changes profiles through this ABI.

---

## 10. Verification

Software unit tests use a fake 32-bit MMIO map and cover:

- the original six-pointer callback-table layout;
- missing magic and incompatible-major fallback;
- I/O failure after recognized identity and capability-gated accesses without
  clock-CSR traffic;
- signed temperature, timestamp composition and conversion;
- torn telemetry snapshot retry;
- both power raw/scale conversions;
- nominal and measured clocks plus valid fan-status field decoding;
- all four legal exact-Hz requests, rejection of other values, write ordering,
  cookie matching and mismatch, pending, completion, rejection, and I/O-error
  states.

Hardware verification, when the board is available, proceeds with one SOF:

1. Read a stable telemetry snapshot and current frequency.
2. Run the 1-core/1-warp/1-thread baseline.
3. After completion, request the next legal exact-Hz value and confirm nominal
   and measured readback.
4. Repeat the workload, then expand to multi-warp/multi-thread tests.
5. Exercise repeated frequency cycles, rejected busy/unsupported requests,
   quiesce timeout, PLL-lock timeout, PCIe reset, and DDR4 A/B/C/D integrity.
