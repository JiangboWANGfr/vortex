// SPDX-License-Identifier: Apache-2.0
#ifndef GHASH_BENCH_COMMON_H
#define GHASH_BENCH_COMMON_H

#include <stdint.h>

typedef struct {
  uint64_t data_addr;    // num_tasks * blocks_per_task * 16 bytes
  uint64_t h_addr;       // 16 bytes (shared hash subkey H)
  uint64_t tag_addr;     // num_tasks * 16 bytes (output GHASH tags)
  uint64_t status_addr;
  uint32_t num_tasks;        // independent GHASH streams (one per warp)
  uint32_t blocks_per_task;  // 16-byte blocks per stream
} kernel_arg_t;

typedef struct {
  uint32_t errors;
  uint32_t completed_tasks;
  uint64_t total_blocks;
} ghash_bench_status_t;

#endif // GHASH_BENCH_COMMON_H
