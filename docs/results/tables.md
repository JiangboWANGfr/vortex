# Vortex crypto results

## AES-256-GCM confidentiality overhead

| accel_mode | dispatch | total_bytes | cycles | cyc_per_byte | overhead_x | native_speedup | status |
|---|---|---|---|---|---|---|---|
| UNPROTECTED | WARP | 32768 | 534359 | 16.307 | 1.00x |  | PASS |
| UNPROTECTED | LANE | 32768 | 655440 | 20.002 | 1.00x |  | PASS |
| SOFTWARE | WARP | 32768 | 159471158 | 4866.674 | 298.43x |  | PASS |
| SOFTWARE | LANE | 32768 | 193756669 | 5912.984 | 295.61x |  | PASS |
| NATIVE | WARP | 32768 | 989392 | 30.194 | 1.85x | 161.18x | PASS |
| NATIVE | LANE | 32768 | 1666012 | 50.843 | 2.54x | 116.30x | PASS |

## GHASH multiplier design-space (radix sweep)

| mul_radix | mul_cycles | total_bytes | cycles | cyc_per_byte | ipc | status |
|---|---|---|---|---|---|---|
| 1 | 130 | 32,768 | 110612 | 3.38 | 1.008480 | PASS |
| 2 | 66 | 32,768 | 108226 | 3.3 | 1.030714 | PASS |
| 8 | 18 | 32,768 | 108113 | 3.3 | 1.031791 | PASS |
| 32 | 6 | 32,768 | 110334 | 3.37 | 1.011021 | PASS |
| 128 | 3 | 32,768 | 107388 | 3.28 | 1.038757 | PASS |

## Correctness

| suite | what | cases | result |
|---|---|---|---|
| ghash_smoke | GF(2^128) algebraic identities | 8 | PASS (sw+native, simx+rtlsim) |
| aes_gcm_smoke | NIST AES-256-GCM TC13-16 | 4 | PASS (sw+native, simx+rtlsim) |
| gcm_bench | sw vs native tag checksum | all | MATCH (bit-exact) |

## Figures

![fig_gcm_overhead.png](fig_gcm_overhead.png)

![fig_ghash_design_space.png](fig_ghash_design_space.png)

![fig_gcm_speedup.png](fig_gcm_speedup.png)

