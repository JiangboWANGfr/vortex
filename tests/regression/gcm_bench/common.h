// SPDX-License-Identifier: Apache-2.0
#ifndef GCM_BENCH_COMMON_H
#define GCM_BENCH_COMMON_H

#include <stdint.h>

typedef struct {
  uint64_t data_addr;    // num_tasks * bytes_per_task plaintext
  uint64_t out_addr;     // num_tasks * bytes_per_task ciphertext / moved data
  uint64_t tag_addr;     // num_tasks * 16 tags (crypto) or data-fold (unprotected)
  uint64_t key_addr;     // 32-byte shared AES-256 key
  uint64_t status_addr;
  uint32_t num_tasks;        // independent GCM streams
  uint32_t bytes_per_task;   // bytes per stream (multiple of 16)
} kernel_arg_t;

typedef struct {
  uint32_t errors;
  uint32_t completed_tasks;
  uint64_t total_bytes;
} gcm_bench_status_t;

#endif // GCM_BENCH_COMMON_H
