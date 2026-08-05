// SPDX-License-Identifier: Apache-2.0
#ifndef GHASH_SMOKE_COMMON_H
#define GHASH_SMOKE_COMMON_H

#include <stdint.h>

typedef struct {
  uint32_t errors;      // total failing checks
  uint32_t failed_mask; // bitmask of GHASH_SMOKE_FAIL_* flags
} ghash_smoke_status_t;

typedef struct {
  uint64_t status_addr;
} kernel_arg_t;

enum {
  GHASH_SMOKE_FAIL_EMPTY = 1u << 0,           // GHASH(*, empty) != 0
  GHASH_SMOKE_FAIL_ZERO_BLOCK = 1u << 1,      // GHASH(*, 0) != 0
  GHASH_SMOKE_FAIL_ZERO_H = 1u << 2,          // GHASH(0, *) != 0
  GHASH_SMOKE_FAIL_IDENTITY_1BLOCK = 1u << 3, // GHASH(H, "1") != H
  GHASH_SMOKE_FAIL_IDENTITY_2BLOCK = 1u << 4, // GHASH(H, 0||"1") != H
  GHASH_SMOKE_FAIL_DETERMINISM = 1u << 5,     // two runs differ
  GHASH_SMOKE_FAIL_STREAMING = 1u << 6,       // chunked update != oneshot
  GHASH_SMOKE_FAIL_LINEARITY = 1u << 7,       // f(A^B) ^ f(A) ^ f(B) != 0
};

#endif // GHASH_SMOKE_COMMON_H
