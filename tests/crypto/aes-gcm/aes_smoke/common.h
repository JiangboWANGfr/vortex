#ifndef _COMMON_H_
#define _COMMON_H_

typedef struct {
  uint32_t errors;
  uint32_t failed_mask;
} aes_smoke_status_t;

typedef struct {
  uint64_t status_addr;
} kernel_arg_t;

enum {
  AES_SMOKE_FAIL_SCALAR = 1u << 0,
  AES_SMOKE_FAIL_ROUND  = 1u << 1,
};

#endif
