#include <stdint.h>
#include <vx_spawn.h>
#include "common.h"

namespace {

struct TaskArgs {
  uint8_t* msg;
  uint64_t* bit_lens;
  uint8_t* digest;
  uint32_t cases_per_task;
  uint32_t msg_stride;
};

void keccak_worker(const TaskArgs* __UNIFORM__ args) {
  uint32_t task_id = blockIdx.x;
  uint32_t start_case = task_id * args->cases_per_task;

  for (uint32_t i = 0; i < args->cases_per_task; ++i) {
    uint32_t case_idx = start_case + i;
    uint8_t* msg = args->msg + (case_idx * args->msg_stride);
    uint8_t* digest = args->digest + (case_idx * KECCAK_SHA3_256_DIGEST_BYTES);
    sha3_256_bits(msg, args->bit_lens[case_idx], digest);
  }
}

} // namespace

int main() {
  kernel_arg_t* __UNIFORM__ arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);
  keccak_smoke_status_t* status = (keccak_smoke_status_t*)(uintptr_t)arg->status_addr;

  status->errors = 0;
  status->completed_tasks = 0;
  status->completed_cases = 0;

  if (arg->num_tasks == 0 || arg->cases_per_task == 0 || arg->msg_stride == 0) {
    status->errors = 1;
    return 1;
  }

  TaskArgs task_args = {
    (uint8_t*)(uintptr_t)arg->msg_addr,
    (uint64_t*)(uintptr_t)arg->bit_len_addr,
    (uint8_t*)(uintptr_t)arg->digest_addr,
    arg->cases_per_task,
    arg->msg_stride,
  };

  vx_spawn_threads(1, &arg->num_tasks, nullptr, (vx_kernel_func_cb)keccak_worker, &task_args);
  status->completed_tasks = arg->num_tasks;
  status->completed_cases = arg->num_tasks * arg->cases_per_task;
  return 0;
}
