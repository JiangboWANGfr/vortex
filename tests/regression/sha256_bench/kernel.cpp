#include <stdint.h>
#include <vx_spawn.h>
#include "common.h"

namespace {

struct TaskArgs {
  uint8_t* msg;
  uint8_t* digest;
  uint32_t messages_per_task;
  uint32_t msg_bytes;
  uint32_t padded_msg_bytes;
};

void sha_worker(const TaskArgs* __UNIFORM__ args) {
  uint32_t task_id = blockIdx.x;
  uint32_t start_msg = task_id * args->messages_per_task;

  for (uint32_t i = 0; i < args->messages_per_task; ++i) {
    uint32_t msg_idx = start_msg + i;
    uint8_t* msg = args->msg + (msg_idx * args->padded_msg_bytes);
    uint8_t* digest = args->digest + (msg_idx * SHA256_DIGEST_BYTES);
    sha256(msg, args->msg_bytes, digest);
  }
}

} // namespace

int main() {
  kernel_arg_t* __UNIFORM__ arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);
  sha256_bench_status_t* status = (sha256_bench_status_t*)(uintptr_t)arg->status_addr;

  status->errors = 0;
  status->completed_tasks = 0;
  status->messages_per_task = arg->messages_per_task;
  status->msg_bytes = arg->msg_bytes;
  status->total_messages = (uint64_t)arg->num_tasks * arg->messages_per_task;
  status->total_bytes = status->total_messages * arg->msg_bytes;

  if (arg->num_tasks == 0 || arg->messages_per_task == 0 || arg->msg_bytes == 0 || arg->padded_msg_bytes < arg->msg_bytes + 9) {
    status->errors = 1;
    return 1;
  }

  TaskArgs task_args = {
    (uint8_t*)(uintptr_t)arg->msg_addr,
    (uint8_t*)(uintptr_t)arg->digest_addr,
    arg->messages_per_task,
    arg->msg_bytes,
    arg->padded_msg_bytes,
  };

  vx_spawn_threads(1, &arg->num_tasks, nullptr, (vx_kernel_func_cb)sha_worker, &task_args);
  status->completed_tasks = arg->num_tasks;
  return 0;
}
