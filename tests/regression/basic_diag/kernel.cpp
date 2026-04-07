#include <vx_intrinsics.h>
#include "common.h"

int main() {
  kernel_arg_t* __UNIFORM__ arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);
  uint32_t count    = arg->count;
  int32_t* src0_ptr = (int32_t*)arg->src0_addr;
  int32_t* src1_ptr = (int32_t*)arg->src1_addr;
  int32_t* dst_ptr  = (int32_t*)arg->dst_addr;

  uint32_t offset = vx_core_id() * count;

  for (uint32_t i = 0; i < count; ++i) {
#ifdef BASIC_DIAG_ADD
    dst_ptr[offset + i] = src0_ptr[offset + i] + src1_ptr[offset + i];
#elif defined(BASIC_DIAG_COPY_SRC1)
    dst_ptr[offset + i] = src1_ptr[offset + i];
#else
    dst_ptr[offset + i] = src0_ptr[offset + i];
#endif
  }

  return 0;
}
