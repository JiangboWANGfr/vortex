# Cryptographic ISA extensions: software baseline

This document fixes the measurement contract for comparing software
cryptography against the instruction-set extensions that will replace it. The
numbers a later hardware variant is compared against are only meaningful if the
baseline, the workload and the counter definitions do not move, so they are
recorded here rather than left to whoever runs the benchmark.

## 1. Scope

The first landing is `tests/crypto/aes_gcm`, a software AES-128-GCM running on
unmodified hardware: no RTL, no decode change, no simulator change, no new
instruction. It establishes the correctness oracle and the cycle numbers that
the hardware work is measured against.

AES-GCM is the target, so the cipher is measured in the mode it is actually
used in: counter-mode encryption over whole blocks, GHASH authentication over
the resulting ciphertext, and tag generation. ECB was rejected because it is
not on the GCM critical path.

## 2. Design

### Workload shape

One independent GCM message per thread, one shared key, a distinct 96-bit IV
per message, empty AAD. Both halves of GCM are then parallel across threads:
counter-mode encryption is parallel by construction, and the GHASH chain is
serial only within a message. This matches the shape a hardware GHASH would
need, which keeps per-warp or per-lane state, and it models a server handling
many independent records rather than one long stream.

### Tables live in local memory

The four 1 KB T-tables, the 176-byte key schedule and the 256-byte GHASH table
are copied into local memory once per CTA and then read by every thread.

This is forced by the machine, not preferred. A table lookup is a
data-dependent gather across the SIMD width, and in this configuration
`VX_CFG_LMEM_NUM_BANKS` scales with `VX_CFG_SIMD_WIDTH` (32 banks at
`SIMD_WIDTH=32`) while `VX_CFG_DCACHE_NUM_BANKS` is 1. A table left in global
memory would serialise every gather through a single cache bank, and with
`VX_CFG_L2_ENABLED=0` and `VX_CFG_L3_ENABLED=0` the streaming plaintext would
evict it continuously. The baseline would then measure the memory system rather
than the round function, and would inflate any speedup claimed against it.

### One CTA per core

`grid_dim = num_cores`, `block_dim = num_warps * num_threads`, with a
grid-stride loop over messages. Sizing the grid to the problem instead would
launch one CTA per few messages, each refilling 4 KB of local memory, and the
fill would dominate. One persistent CTA per core amortises the fill. The CTA
spans every warp on the core, so the fill is strided over `threadIdx.x` and
fenced with `__syncthreads()`.

### Key expansion is host-side

The host expands the key and uploads the schedule; the device only copies it
into local memory. This is a policy, not an implementation detail: a hardware
variant that expands keys on-device would otherwise be measured against a
baseline that does not, so either both include expansion or neither does. This
baseline excludes it.

## 3. Correctness

Two levels, because a device-versus-host comparison alone cannot catch a broken
oracle.

1. The host reference — byte-oriented AES from FIPS-197 and a bit-at-a-time
   GF(2^128) multiply from SP 800-38D Algorithm 1 — must reproduce FIPS-197
   Appendix C.1 and two published GCM vectors before any device call. The
   vectors are compiled into `main.cpp`; there is no external file to fetch.
2. The device output over pseudorandom messages is compared against that
   reference.

Both levels earned their place during bring-up: level 1 caught a test vector
that mixed one GCM case's tag with another's key, and level 2 isolated a
reversed Horner iteration in the device GHASH — the ciphertext matched, so only
the authentication half was suspect.

## 4. Measurement protocol

Frozen. Changing any of it invalidates comparison with the numbers in section 5.

**Counters.** `cycles` is the maximum of `VX_CSR_MCYCLE` across cores;
`instrs` is the sum of `VX_CSR_MINSTRET` across cores. MCYCLE is per-core
elapsed time, so summing it is wrong by a factor of the core count — passing
the broadcast core id to `vx_device_mpm_query` sums, and must not be used for
cycles. The application recomputes both with an explicit per-core loop and
prints them; they must equal the runtime's own `PERF:` line or the run is
discarded.

**No `--perf` flag.** `VX_CSR_MCYCLE` and `VX_CSR_MINSTRET` are decoded outside
the `PERF_ENABLE` guard, so the baseline is taken from a build with empty
`CONFIGS` and therefore from the same RTL that CI compiles. A pipeline
breakdown, if ever wanted, is a separate run whose cycle count is not mixed
with this one.

**One launch per process.** rtlsim asserts reset once at device construction, so
MCYCLE accumulates across launches, while simx resets per launch. A warmup
launch or a repeat loop would mean different things in the two drivers. Repeat
by re-running the process.

**rtlsim is authoritative.** Its counter is busy-gated, so host idle time is not
counted. simx is recorded as a secondary line and the gap is reported, never
substituted.

**Steady state.** The problem size must be large enough that the one-time local
memory fill does not dominate. The gate is that doubling the block count
doubles the cycles: `cycles(2N)/cycles(N)` in `[1.96, 2.04]`.

**Determinism.** Three rtlsim runs at the recorded point returned identical
cycle and instruction counts. A run that does not reproduce means something in
the harness is varying — a stray flag, a leftover `CONFIGS`, a second launch —
and must be found before a number is recorded.

## 5. Recorded baseline

Configuration `c1w4t32`: one core, four warps, 32 threads, therefore
`SIMD_WIDTH=32` and `ISSUE_WIDTH=1`. Message count is set to the thread count
so every lane carries exactly one message with no tail.

```
./aes_gcm -n128 -b64 -i0
```

128 messages of 64 blocks: 8192 blocks, 131072 bytes.

| | simx | rtlsim |
| --- | ---: | ---: |
| cycles | 12,255,505 | 14,247,819 |
| instrs | 1,229,052 | 1,229,052 |
| cycles/block | 1496.03 | 1739.24 |
| bytes/cycle | 0.0107 | 0.0092 |

Retired instruction counts agree exactly. The cycle gap is 16.3% at this size
and 6.3% at `-n64 -b4`, so it grows with the problem; that is a modelling
discrepancy to resolve before a `model_parity` gate can cover these cases, not
a tolerance to widen.

Steady-state evidence, simx, `-n128`:

| blocks/msg | cycles | ratio |
| ---: | ---: | ---: |
| 16 | 3,241,766 | |
| 32 | 6,202,459 | 1.913 |
| 64 | 12,255,505 | 1.976 |

`-b64` is the first size inside the gate. Below it the fixed cost — the local
memory fill plus launch, about 137k cycles by a two-point fit — is still
visible.

Provenance:

- commit `6533ce29fc2f44572b2bc3e6144f594bbd96e635`
- `VX_config.toml` sha256 `3f020a15364555da8da610b5f78501f670299440685846fb40bb0f757593fb93`
- `VX_types.toml` sha256 `2e22f4fbac08fe54036ee80b01116615f7675bb7cc512f6de77571ca5d91f877`
- clang 20.1.8 (vortexgpgpu/llvm 4c836512)

## 6. What comes next

The hardware variants join the same application as additional entry points in
the same binary, selected by `-i`. The host program, buffers, vectors, counter
reduction and output format are shared, so a comparison between two rows
measures the device code and nothing else. Splitting them into separate
applications would reintroduce exactly the variables this protocol removes.
