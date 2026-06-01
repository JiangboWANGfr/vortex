// SPDX-License-Identifier: Apache-2.0
#ifndef CHACHA20POLY1305_SMOKE_COMMON_H
#define CHACHA20POLY1305_SMOKE_COMMON_H

#include <stdint.h>

typedef struct {
  uint32_t errors;
  uint32_t failed_mask;  // bit0 = poly1305, bit1 = aead ct, bit2 = aead tag
} ccp_smoke_status_t;

typedef struct {
  uint64_t status_addr;
} kernel_arg_t;

enum {
  CCP_FAIL_POLY1305 = 1u << 0,
  CCP_FAIL_AEAD_CT  = 1u << 1,
  CCP_FAIL_AEAD_TAG = 1u << 2,
};

#endif // CHACHA20POLY1305_SMOKE_COMMON_H
