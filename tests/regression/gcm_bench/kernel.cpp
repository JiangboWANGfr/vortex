// SPDX-License-Identifier: Apache-2.0
//
// End-to-end confidential-ML throughput benchmark (scenario A: all-reduce
// GMAC). Each task protects one buffer with AES-256-GCM; parallelism is one
// stream per warp (WARP) or one per thread (LANE multi-chain). Three modes let
// us measure the cost of confidentiality:
//   UNPROTECTED : just move the data (read + memcpy) -- the all-reduce baseline
//                 without crypto.
//   SOFTWARE    : software AES-256-GCM over the buffer.
//   NATIVE      : hardware AES rounds + hardware GHASH AES-256-GCM.
// A ring all-reduce of an N-byte buffer across P GPUs performs ~2N bytes of
// such per-chunk GCM per GPU, so per-buffer GCM throughput drives the
// confidential all-reduce cost.

#include <stdint.h>
#include <vx_spawn.h>
#include <vx_intrinsics.h>
#ifndef GCM_BENCH_UNPROTECTED
#include "../aes_gcm_smoke/aes_gcm.h"
#endif
#include "common.h"

namespace {

struct TaskArgs {
  const uint8_t* data;
  uint8_t* out;
  uint8_t* tags;
  const uint8_t* key;
  uint32_t bytes_per_task;
};

void gcm_worker(const TaskArgs* __UNIFORM__ args) {
#ifndef GCM_BENCH_DISPATCH_LANE
  vx_tmc_one();   // WARP mode: one stream per warp
#endif
  uint32_t task = blockIdx.x;
  const uint8_t* in = args->data + (uint64_t)task * args->bytes_per_task;
  uint8_t* out = args->out + (uint64_t)task * args->bytes_per_task;
  uint8_t* tag = args->tags + (uint64_t)task * 16;

#ifdef GCM_BENCH_UNPROTECTED
  // Baseline: move the buffer (read + write) and fold a checksum. Represents
  // the all-reduce data movement without authenticated encryption.
  uint8_t acc[16];
  for (int i = 0; i < 16; ++i) acc[i] = 0;
  for (uint32_t i = 0; i < args->bytes_per_task; ++i) {
    uint8_t b = in[i];
    out[i] = b;
    acc[i & 15] ^= b;
  }
  for (int i = 0; i < 16; ++i) tag[i] = acc[i];
#else
  // Shared key, per-stream 96-bit IV derived from the task id.
  uint8_t iv[12];
  for (int i = 0; i < 8; ++i) iv[i] = 0;
  iv[8]  = (uint8_t)(task >> 24);
  iv[9]  = (uint8_t)(task >> 16);
  iv[10] = (uint8_t)(task >> 8);
  iv[11] = (uint8_t)(task);
  aes256_gcm_encrypt(args->key, iv, nullptr, 0, in, args->bytes_per_task, out, tag);
#endif
}

} // namespace

int main() {
  kernel_arg_t* __UNIFORM__ arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);
  gcm_bench_status_t* status = (gcm_bench_status_t*)(uintptr_t)arg->status_addr;

  status->errors = 0;
  status->completed_tasks = 0;
  status->total_bytes = (uint64_t)arg->num_tasks * arg->bytes_per_task;

  if (arg->num_tasks == 0 || arg->bytes_per_task == 0) {
    status->errors = 1;
    return 1;
  }

  TaskArgs task_args = {
    (const uint8_t*)(uintptr_t)arg->data_addr,
    (uint8_t*)(uintptr_t)arg->out_addr,
    (uint8_t*)(uintptr_t)arg->tag_addr,
    (const uint8_t*)(uintptr_t)arg->key_addr,
    arg->bytes_per_task,
  };

#ifdef GCM_BENCH_DISPATCH_LANE
  vx_spawn_threads(1, &arg->num_tasks, nullptr,
                   (vx_kernel_func_cb)gcm_worker, &task_args);
#else
  uint32_t block_dim = vx_num_threads();
  vx_spawn_threads(1, &arg->num_tasks, &block_dim,
                   (vx_kernel_func_cb)gcm_worker, &task_args);
#endif
  status->completed_tasks = arg->num_tasks;
  return 0;
}
