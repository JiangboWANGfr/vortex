// SPDX-License-Identifier: Apache-2.0
#ifndef CHACHA20POLY1305_BENCH_COMMON_H
#define CHACHA20POLY1305_BENCH_COMMON_H

#include <stdint.h>

typedef struct {
  uint64_t data_addr;    // num_tasks * bytes_per_task plaintext
  uint64_t out_addr;     // num_tasks * bytes_per_task ciphertext / moved data
  uint64_t tag_addr;     // num_tasks * 16 tags (or data-fold for unprotected)
  uint64_t key_addr;     // 32-byte shared ChaCha20 key
  uint64_t status_addr;
  uint32_t num_tasks;        // independent AEAD streams
  uint32_t bytes_per_task;   // bytes per stream (multiple of 16)
} kernel_arg_t;

typedef struct {
  uint32_t errors;
  uint32_t completed_tasks;
  uint64_t total_bytes;
} ccp_bench_status_t;

#endif // CHACHA20POLY1305_BENCH_COMMON_H
