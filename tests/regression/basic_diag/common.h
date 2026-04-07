#ifndef _COMMON_H_
#define _COMMON_H_

typedef struct {
  uint32_t count;
  uint64_t src0_addr;
  uint64_t src1_addr;
  uint64_t dst_addr;
} kernel_arg_t;

#endif
