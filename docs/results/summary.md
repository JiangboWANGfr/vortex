# Headline

- **Hardware AES-256-GCM speedup over software:** 161.18x (WARP).
- **Confidentiality overhead vs unprotected:** software 298.43x -> hardware 1.85x (near-free).
- **GHASH multiplier design-space (cycle-accurate rtlsim, 1 warp/core):** a 43x faster MUL (radix 1->128) changes cycles by ~0% even with no warp-level latency hiding -> the MUL is off the critical path (overlaps per-block memory), so the cheap bit-serial multiplier suffices. (simx cannot show this: it does not model crypto FU latency.)

Matplotlib available: PNG figures written.
