# Validation & methodology notes

## simx vs rtlsim: what each is good for

simx (functional + coarse timing) is fast and bit-accurate for correctness, but
it does **not** model the crypto functional-unit latency: a GHASH `MUL` with a
130-cycle hardware latency and a 3-cycle one produce **bit-identical** simx
cycle counts, even at 1 warp/core. So simx is **invalid for functional-unit
microarchitecture / latency studies** — use cycle-accurate rtlsim for those.

simx is still trustworthy for throughput/overhead at the workload level, because
the quantities of interest here are memory-bound (see below): the crypto FU
latency it omits is hidden behind memory access anyway.

## Radix (MUL latency) design-space — rtlsim, 1 warp/core

`ghash_bench` NATIVE LANE, n=128, **1 warp/core** (no warp-level latency hiding):

| radix | MUL cycles | total cycles |
|------:|-----------:|-------------:|
| 1     | 130        | 60157        |
| 2     | 66         | 60137        |
| 8     | 18         | 60137        |
| 32    | 6          | 60137        |
| 128   | 3          | 60137        |

A 43x faster multiplier changes throughput by 0.03%, with **no other warp to
hide behind** -> the MUL is genuinely off the critical path; it overlaps the
per-block memory access. Not MUL-bound. (Earlier simx version of this sweep was
an artifact; superseded.)

## AES-256-GCM overhead — simx vs rtlsim cross-check

`gcm_bench` WARP, t=32, n=256:

| mode        | simx cyc/byte | rtlsim cyc/byte |
|-------------|--------------:|----------------:|
| UNPROTECTED | 18.54         | 20.26           |
| NATIVE      | 37.46         | 40.06           |
| **overhead**| **2.02x**     | **1.98x**       |

The confidentiality-overhead ratio agrees within 2% between simx and
cycle-accurate rtlsim, so the headline overhead numbers (≈1.7–2.0x vs
unprotected) are trustworthy despite simx's missing FU-latency model — both
modes are memory-bound.

## Net

Three independent observations all say **GCM on this SIMT GPGPU is memory /
data-movement bound, not crypto-compute bound**:
1. MUL radix doesn't matter (even at 1 warp/core) — crypto compute is hidden.
2. Per-lane multi-chain doesn't help full GCM — memory-access divergence dominates.
3. simx (no FU latency) and rtlsim agree on overhead — FU latency is irrelevant.
