#ifndef _KECCAK_SMOKE_COMMON_H_
#define _KECCAK_SMOKE_COMMON_H_

#include <stdint.h>
#include "../../../kernel/keccak/keccak.h"

typedef struct {
  uint64_t msg_addr;
  uint64_t bit_len_addr;
  uint64_t digest_addr;
  uint64_t status_addr;
  uint32_t num_tasks;
  uint32_t cases_per_task;
  uint32_t msg_stride;
} kernel_arg_t;

typedef struct {
  uint32_t errors;
  uint32_t completed_tasks;
  uint32_t completed_cases;
} keccak_smoke_status_t;

#endif
