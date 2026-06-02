# Headline

- **Hardware AES-256-GCM speedup over software:** 161.18x (WARP).
- **Confidentiality overhead vs unprotected:** software 298.43x -> hardware 1.85x (near-free).
- **GHASH multiplier design-space (cycle-accurate rtlsim, 1 warp/core):** a 43x faster MUL (radix 1->128) changes cycles by ~0% even with no warp-level latency hiding -> the MUL is off the critical path (dwarfed by per-block memory), so the cheap bit-serial multiplier suffices. (simx is insensitive to the radix -- its coarse timing model does not expose the per-op FU result latency -- so rtlsim is required.)
- **ChaCha20-Poly1305 AEAD (cycle-accurate rtlsim, WARP):** hardware ChaCha+Poly1305 overhead vs the unprotected data-movement baseline shrinks with message size (1.78x @64B -> 1.25x @1KB) while the speedup over software grows (2.52x -> 4.80x); SW and NATIVE tags are bit-exact.
- **ChaCha20 quarter-round design-space (rtlsim, 1 warp/core):** an 80x faster QR (radix 1->80) changes per-block cycles by ~0.6% -- the QR is off the critical path (its latency is <1% of the ~9.3k-cycle/block cost, dwarfed by per-block memory + the WR/RD PE interface), so the area-minimal serial QR is the right design point. Same not-compute-bound signature as GHASH, now for the ARX cipher.

Matplotlib available: PNG figures written.
