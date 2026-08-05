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
  uint32_t num_streams;
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
  // Baseline: move the buffer at WORD granularity (the unprotected version of
  // the same data path), with one register fold per 64-bit word to keep the
  // traffic live. The previous byte-granular indexed-stack fold executed more
  // dynamic instructions per byte than the hardware AEAD path itself, which
  // understates "overhead". Keep this definition in sync with
  // chacha20poly1305_bench.
  uint64_t acc = 0;
  if (((((uintptr_t)in) | ((uintptr_t)out) | args->bytes_per_task) & 7) == 0) {
    const uint64_t* in64 = (const uint64_t*)in;
    uint64_t* out64 = (uint64_t*)out;
    uint32_t words = args->bytes_per_task / 8;
    for (uint32_t i = 0; i < words; ++i) {
      uint64_t w = in64[i];
      out64[i] = w;
      acc = acc * 1000003u + w;
    }
  } else {  // unaligned fallback (not hit by the standard -n sizes)
    for (uint32_t i = 0; i < args->bytes_per_task; ++i) {
      uint8_t b = in[i];
      out[i] = b;
      acc = acc * 1000003u + b;
    }
  }
  for (int i = 0; i < 16; ++i) tag[i] = (uint8_t)(acc >> ((i & 7) * 8));
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

#ifdef GCM_BENCH_STAGE
// Coalesced-layout experiment (design (1)). Each lane stages its stream from
// global memory into a local buffer, then runs AES-256-GCM on the local copy.
// The ONLY variable is the cross-lane global read stride:
//   STRIDED  : stream s byte k at gmem[s*W + k]            -> lanes stride W (uncoalesced)
//   COALESCED: stream s byte k at gmem[warp*(NL*W)+k*NL+L] -> lanes stride 1 (coalesced)
// Both stage the same logical bytes, so the GCM output (tag) is identical ->
// the per-stream-tag checksum cross-checks the two layouts. Uses hardware ids
// for stream identity so it does not depend on the spawn task->thread mapping.
#ifndef GCM_BENCH_MAX_BYTES
#define GCM_BENCH_MAX_BYTES 1024
#endif
void gcm_worker_staged(const TaskArgs* __UNIFORM__ args) {
  uint32_t NL    = vx_num_threads();
  uint32_t lane  = vx_thread_id();
  uint32_t gwarp = (uint32_t)vx_core_id() * (uint32_t)vx_num_warps() + (uint32_t)vx_warp_id();
  uint32_t s     = gwarp * NL + lane;
  uint32_t W     = args->bytes_per_task;
  if (s >= args->num_streams || W > GCM_BENCH_MAX_BYTES)
    return;

  uint8_t buf[GCM_BENCH_MAX_BYTES];
  const uint8_t* g = args->data;
#ifdef GCM_BENCH_COALESCED
  uint64_t wbase = (uint64_t)gwarp * NL * W;
  for (uint32_t k = 0; k < W; ++k)
    buf[k] = g[wbase + (uint64_t)k * NL + lane];   // lanes -> contiguous, coalesced
#else
  uint64_t sbase = (uint64_t)s * W;
  for (uint32_t k = 0; k < W; ++k)
    buf[k] = g[sbase + k];                         // lanes -> stride W, uncoalesced
#endif

  uint8_t iv[12];
  for (int i = 0; i < 8; ++i) iv[i] = 0;
  iv[8]  = (uint8_t)(s >> 24);
  iv[9]  = (uint8_t)(s >> 16);
  iv[10] = (uint8_t)(s >> 8);
  iv[11] = (uint8_t)(s);
  // Encrypt in place (GCTR then GHASH(ct)); tag is the per-stream output.
  aes256_gcm_encrypt(args->key, iv, nullptr, 0, buf, W, buf, args->tags + (uint64_t)s * 16);
}
#endif

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
    arg->num_tasks,
  };

#ifdef GCM_BENCH_STAGE
  // staged coalesced-layout experiment: always per-lane (LANE) dispatch.
  vx_spawn_threads(1, &arg->num_tasks, nullptr,
                   (vx_kernel_func_cb)gcm_worker_staged, &task_args);
#elif defined(GCM_BENCH_DISPATCH_LANE)
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
