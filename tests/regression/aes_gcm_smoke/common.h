// SPDX-License-Identifier: Apache-2.0
#ifndef AES_GCM_SMOKE_COMMON_H
#define AES_GCM_SMOKE_COMMON_H

#include <stdint.h>

typedef struct {
  uint32_t errors;       // failing KAT cases
  uint32_t failed_mask;  // bit i set if kGcmKats[i] failed
  uint32_t num_cases;
} gcm_smoke_status_t;

typedef struct {
  uint64_t status_addr;
} kernel_arg_t;

#endif // AES_GCM_SMOKE_COMMON_H
