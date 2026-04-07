#ifndef _COMMON_H_
#define _COMMON_H_

typedef struct {
  uint32_t errors;
  uint32_t failed_mask;
} aes256_status_t;

typedef struct {
  uint64_t status_addr;
} kernel_arg_t;

enum {
  AES256_FAIL_ECB_ENC = 1u << 0,
  AES256_FAIL_ECB_DEC = 1u << 1,
  AES256_FAIL_CBC_ENC = 1u << 2,
  AES256_FAIL_CBC_DEC = 1u << 3,
  AES256_FAIL_CTR_ENC = 1u << 4,
  AES256_FAIL_CTR_DEC = 1u << 5,
};

#endif
