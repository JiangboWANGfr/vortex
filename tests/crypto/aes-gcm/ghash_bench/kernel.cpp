// SPDX-License-Identifier: Apache-2.0
//
// GHASH throughput benchmark. Each task runs one independent GHASH stream
// (blocks_per_task 16-byte blocks under a shared H).
//
// Two dispatch modes select the parallelism granularity:
//   WARP (default): vx_tmc_one() keeps one active thread per warp, so each
//     task = one warp = one stream. Parallelism comes from warps x cores.
//   LANE (GHASH_BENCH_DISPATCH_LANE): all threads active, each thread = one
//     stream, so a warp runs NUM_THREADS independent chains at once. Requires
//     per-lane GHASH state (design C-a); this is where multi-chain throughput
//     shows up.
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
  uint32_t num_tasks;
};

void ghash_worker(const TaskArgs* __UNIFORM__ args) {
#ifndef GHASH_BENCH_DISPATCH_LANE
  // WARP mode: collapse to one active thread per warp (one chain per warp).
  vx_tmc_one();
#endif

  uint32_t task_id = blockIdx.x;
  uint8_t* tag = args->tags + (uint64_t)task_id * GHASH_BLOCK_BYTES;

#ifdef GHASH_BENCH_INTERLEAVE
  // Block-major layout: block i of task t lives at (i*num_tasks + t)*16, so the
  // NUM_THREADS lanes of a warp (adjacent task_ids) read 16B-apart addresses
  // that fall in one 64B cache line -> coalesced load. Each lane's own chain is
  // strided by num_tasks*16 across blocks.
  ghash_ctx_t ctx;
  ghash_init(&ctx, args->H);
  uint32_t nt = args->num_tasks;
  uint32_t nb = args->blocks_per_task;
  for (uint32_t i = 0; i < nb; ++i) {
    const uint8_t* blk = args->data
        + (uint64_t)((uint64_t)i * nt + task_id) * GHASH_BLOCK_BYTES;
    ghash_update_block(&ctx, blk);
  }
  ghash_final(&ctx, tag);
#else
  const uint8_t* data = args->data
      + (uint64_t)task_id * args->blocks_per_task * GHASH_BLOCK_BYTES;
  ghash_oneshot(args->H, data,
                (uint64_t)args->blocks_per_task * GHASH_BLOCK_BYTES, tag);
#endif
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
    arg->num_tasks,
  };

#ifdef GHASH_BENCH_DISPATCH_LANE
  // LANE mode: one task per thread; all lanes active, each its own chain.
  vx_spawn_threads(1, &arg->num_tasks, nullptr,
                   (vx_kernel_func_cb)ghash_worker, &task_args);
#else
  // WARP mode: block_dim = threads-per-warp makes each task own a whole warp;
  // the worker then masks down to one active thread (warp affinity).
  uint32_t block_dim = vx_num_threads();
  vx_spawn_threads(1, &arg->num_tasks, &block_dim,
                   (vx_kernel_func_cb)ghash_worker, &task_args);
#endif
  status->completed_tasks = arg->num_tasks;
  return 0;
}
