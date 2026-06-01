// SPDX-License-Identifier: Apache-2.0
//
// Warp-affine GHASH throughput benchmark. Each task owns one warp and runs
// one independent GHASH stream (blocks_per_task 16-byte blocks under a shared
// H). vx_tmc_one() keeps exactly one active thread per warp so the per-warp
// {H,Y} hardware state is not shared between independent streams. Parallelism
// therefore comes from warps x cores (num_tasks streams in flight), which is
// the correct granularity for the per-warp GHASH PE.
//
// SOFTWARE vs NATIVE is selected by ghash_ref.h (GHASH_NATIVE), so both modes
// run the identical dispatch and differ only in the GF(2^128) implementation.

#include <stdint.h>
#include <vx_spawn.h>
#include <vx_intrinsics.h>
#include "../ghash_smoke/ghash_ref.h"
#include "common.h"

namespace {

struct TaskArgs {
  const uint8_t* data;
  const uint8_t* H;
  uint8_t* tags;
  uint32_t blocks_per_task;
};

void ghash_worker(const TaskArgs* __UNIFORM__ args) {
  // Warp-affine: collapse to one active thread per warp.
  vx_tmc_one();

  uint32_t task_id = blockIdx.x;
  const uint8_t* data = args->data
      + (uint64_t)task_id * args->blocks_per_task * GHASH_BLOCK_BYTES;
  uint8_t* tag = args->tags + (uint64_t)task_id * GHASH_BLOCK_BYTES;

  ghash_oneshot(args->H, data,
                (uint64_t)args->blocks_per_task * GHASH_BLOCK_BYTES, tag);
}

} // namespace

int main() {
  kernel_arg_t* __UNIFORM__ arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);
  ghash_bench_status_t* status = (ghash_bench_status_t*)(uintptr_t)arg->status_addr;

  status->errors = 0;
  status->completed_tasks = 0;
  status->total_blocks = (uint64_t)arg->num_tasks * arg->blocks_per_task;

  if (arg->num_tasks == 0 || arg->blocks_per_task == 0) {
    status->errors = 1;
    return 1;
  }

  TaskArgs task_args = {
    (const uint8_t*)(uintptr_t)arg->data_addr,
    (const uint8_t*)(uintptr_t)arg->h_addr,
    (uint8_t*)(uintptr_t)arg->tag_addr,
    arg->blocks_per_task,
  };

  // block_dim = threads-per-warp makes each task own a whole warp; the worker
  // then masks down to one active thread (warp affinity).
  uint32_t block_dim = vx_num_threads();
  vx_spawn_threads(1, &arg->num_tasks, &block_dim,
                   (vx_kernel_func_cb)ghash_worker, &task_args);
  status->completed_tasks = arg->num_tasks;
  return 0;
}
