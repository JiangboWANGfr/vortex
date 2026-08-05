#include <stdint.h>
#include <vx_spawn.h>
#include "common.h"

namespace {

struct TaskArgs {
  uint8_t* msg;
  uint64_t* bit_lens;
  uint8_t* digest;
  uint32_t messages_per_task;
  uint32_t msg_stride;
};

void keccak_worker(const TaskArgs* __UNIFORM__ args) {
#ifdef KECCAK_BENCH_DISPATCH_WARP
  vx_tmc_one();
#endif
  uint32_t task_id = blockIdx.x;
  uint32_t start_msg = task_id * args->messages_per_task;

  for (uint32_t i = 0; i < args->messages_per_task; ++i) {
    uint32_t msg_idx = start_msg + i;
    uint8_t* msg = args->msg + (msg_idx * args->msg_stride);
    uint8_t* digest = args->digest + (msg_idx * KECCAK_SHA3_256_DIGEST_BYTES);
    sha3_256_bits(msg, args->bit_lens[msg_idx], digest);
  }
}

} // namespace

int main() {
  kernel_arg_t* __UNIFORM__ arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);
  keccak_bench_status_t* status = (keccak_bench_status_t*)(uintptr_t)arg->status_addr;

  status->errors = 0;
  status->completed_tasks = 0;
  status->messages_per_task = arg->messages_per_task;
  status->msg_stride = arg->msg_stride;
  status->total_messages = (uint64_t)arg->num_tasks * arg->messages_per_task;
  status->total_bits = 0;

  if (arg->num_tasks == 0 || arg->messages_per_task == 0 || arg->msg_stride == 0) {
    status->errors = 1;
    return 1;
  }

  TaskArgs task_args = {
    (uint8_t*)(uintptr_t)arg->msg_addr,
    (uint64_t*)(uintptr_t)arg->bit_len_addr,
    (uint8_t*)(uintptr_t)arg->digest_addr,
    arg->messages_per_task,
    arg->msg_stride,
  };

  for (uint64_t i = 0; i < status->total_messages; ++i) {
    status->total_bits += task_args.bit_lens[i];
  }

#ifdef KECCAK_BENCH_DISPATCH_WARP
  uint32_t block_dim = vx_num_threads();
  vx_spawn_threads(1, &arg->num_tasks, &block_dim, (vx_kernel_func_cb)keccak_worker, &task_args);
#else
  vx_spawn_threads(1, &arg->num_tasks, nullptr, (vx_kernel_func_cb)keccak_worker, &task_args);
#endif
  status->completed_tasks = arg->num_tasks;
  return 0;
}
