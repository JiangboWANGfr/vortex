#ifndef _SHA256_BENCH_COMMON_H_
#define _SHA256_BENCH_COMMON_H_

#include <stdint.h>
#include "../../kernel/sha256/sha256.h"

typedef struct {
  uint64_t msg_addr;
  uint64_t digest_addr;
  uint64_t status_addr;
  uint32_t num_tasks;
  uint32_t messages_per_task;
  uint32_t msg_bytes;
  uint32_t padded_msg_bytes;
} kernel_arg_t;

typedef struct {
  uint32_t errors;
  uint32_t completed_tasks;
  uint32_t messages_per_task;
  uint32_t msg_bytes;
  uint64_t total_messages;
  uint64_t total_bytes;
} sha256_bench_status_t;

#endif
