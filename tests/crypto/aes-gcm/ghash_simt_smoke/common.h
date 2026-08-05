// SPDX-License-Identifier: Apache-2.0
#ifndef GHASH_SIMT_SMOKE_COMMON_H
#define GHASH_SIMT_SMOKE_COMMON_H

#include <stdint.h>

// Per-task error byte: low 4 bits = bitmask of failing checks,
// high 4 bits reserved. Host sums these as the global error count.
typedef uint8_t task_status_t;

typedef struct {
  uint64_t status_addr;      // points to ghash_simt_status_t
  uint64_t task_status_addr; // points to task_status_t[num_tasks]
  uint32_t num_tasks;
  uint32_t pad0;
} kernel_arg_t;

typedef struct {
  uint32_t errors;      // total failing checks across all tasks
  uint32_t failed_mask; // OR of per-check failure bits
  uint32_t completed_tasks;
  uint32_t pad0;
} ghash_simt_status_t;

enum {
  GHASH_SIMT_FAIL_EMPTY = 1u << 0,
  GHASH_SIMT_FAIL_ZERO_BLOCK = 1u << 1,
  GHASH_SIMT_FAIL_ZERO_H = 1u << 2,
  GHASH_SIMT_FAIL_IDENTITY_1BLOCK = 1u << 3,
  GHASH_SIMT_FAIL_IDENTITY_2BLOCK = 1u << 4,
  GHASH_SIMT_FAIL_DETERMINISM = 1u << 5,
  GHASH_SIMT_FAIL_STREAMING = 1u << 6,
  GHASH_SIMT_FAIL_LINEARITY = 1u << 7,
};

#endif // GHASH_SIMT_SMOKE_COMMON_H
