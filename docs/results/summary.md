# Headline

- **Hardware AES-256-GCM speedup over software:** 161.18x (WARP).
- **Confidentiality overhead vs unprotected:** software 298.43x -> hardware 1.85x (near-free).
- **GHASH multiplier design-space:** total cycles ~flat across MUL radix 1..128 -> SIMT GHASH is not MUL-bound; the cheap bit-serial multiplier suffices.

Matplotlib available: PNG figures written.
