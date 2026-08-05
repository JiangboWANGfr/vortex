#ifndef _AES256_BENCH_COMMON_H_
#define _AES256_BENCH_COMMON_H_

#include <stdint.h>
#include "../../../kernel/aes256/aes256.h"

typedef enum {
  AES256_BENCH_ECB_ENC = 0,
  AES256_BENCH_ECB_DEC = 1,
  AES256_BENCH_CBC_ENC = 2,
  AES256_BENCH_CBC_DEC = 3,
  AES256_BENCH_CTR_ENC = 4,
  AES256_BENCH_CTR_DEC = 5,
  AES256_BENCH_KEY_ENC = 6,
  AES256_BENCH_KEY_DEC = 7,
  AES256_BENCH_OP_COUNT,
} aes256_bench_op_t;

typedef struct {
  uint64_t in_addr;
  uint64_t out_addr;
  uint64_t status_addr;
  uint32_t num_tasks;
  uint32_t nblocks_per_task;
  uint32_t op_type;
  uint32_t reserved;
  uint8_t key[KEY_SIZE];
  uint8_t iv[BLOCK_SIZE];
} kernel_arg_t;

typedef struct {
  uint32_t errors;
  uint32_t completed_tasks;
  uint32_t op_type;
  uint32_t nblocks_per_task;
  uint64_t total_blocks;
} aes256_bench_status_t;

#endif
