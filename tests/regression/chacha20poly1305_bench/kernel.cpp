// SPDX-License-Identifier: Apache-2.0
//
// End-to-end ChaCha20-Poly1305 AEAD throughput / overhead benchmark -- the
// ARX + 2^130-5 counterpart to gcm_bench (SPN + GF(2^128)). One stream per warp
// (WARP) or per thread (LANE). Modes:
//   UNPROTECTED : move the buffer only (read + memcpy + fold) -- AEAD-free baseline.
//   SOFTWARE    : software ChaCha20-Poly1305.
//   NATIVE      : hardware ChaCha PE + hardware Poly1305 PE.
//
// CHACHA_BENCH_COMPUTE_ONLY isolates the ChaCha20 block function (repeated calls,
// no message streaming, no Poly1305) so the CHACHA_QR_RADIX design-space sweep is
// dominated by per-block crypto work -- the counterpart to the GHASH-only
// ghash_bench radix study. Measure on rtlsim: simx is insensitive to the radix
// (its coarse timing model does not expose the per-op BLOCK result latency).

#include <stdint.h>
#include <vx_spawn.h>
#include <vx_intrinsics.h>

#ifndef CHACHA_BENCH_MAX_BYTES
#define CHACHA_BENCH_MAX_BYTES 1024
#endif

#ifndef CHACHA20POLY1305_BENCH_UNPROTECTED
#ifdef CHACHA_BENCH_COMPUTE_ONLY
#include "../chacha20poly1305_smoke/chacha20.h"
#else
#define CHACHA_AEAD_MAX_PT  CHACHA_BENCH_MAX_BYTES
#define CHACHA_AEAD_MAX_AAD 16
#include "../chacha20poly1305_smoke/chacha20poly1305.h"
#endif
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

void ccp_worker(const TaskArgs* __UNIFORM__ args) {
#ifndef CHACHA20POLY1305_BENCH_DISPATCH_LANE
  vx_tmc_one();   // WARP mode: one stream per warp
#endif
  uint32_t task = blockIdx.x;
  const uint8_t* in = args->data + (uint64_t)task * args->bytes_per_task;
  uint8_t* out = args->out + (uint64_t)task * args->bytes_per_task;
  uint8_t* tag = args->tags + (uint64_t)task * 16;

#ifdef CHACHA20POLY1305_BENCH_UNPROTECTED
  // Baseline: move the buffer at WORD granularity (the unprotected version of
  // the same data path), with one register fold per 64-bit word to keep the
  // traffic live. Byte-granular folds are NOT a fair "no crypto" baseline:
  // they execute more dynamic instructions per byte than the whole hardware
  // AEAD path (measured 12.5 vs 9.7 instrs/B on the FPGA prototype), which
  // pushes "overhead" below 1x. Keep this definition in sync with gcm_bench.
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
  // Shared key, per-stream 96-bit nonce derived from the task id.
  uint8_t nonce[12];
  for (int i = 0; i < 8; ++i) nonce[i] = 0;
  nonce[8]  = (uint8_t)(task >> 24);
  nonce[9]  = (uint8_t)(task >> 16);
  nonce[10] = (uint8_t)(task >> 8);
  nonce[11] = (uint8_t)(task);

#ifdef CHACHA_BENCH_COMPUTE_ONLY
  // Compute-isolating: repeatedly run the ChaCha20 block function over a fixed
  // 64-byte keystream (no DRAM per iter, no Poly1305). The block counter varies
  // each iteration so the compiler/HW cannot hoist or eliminate the work; the
  // folded keystream is written to `tag` so SOFTWARE and NATIVE cross-check.
  uint8_t ks[64];
  uint8_t acc[16];
  for (int i = 0; i < 16; ++i) acc[i] = 0;
  for (uint32_t it = 0; it < CHACHA_BENCH_ITERS; ++it) {
    chacha20_block(args->key, task + it, nonce, ks);
    for (int i = 0; i < 64; ++i) acc[i & 15] ^= ks[i];
  }
  for (int i = 0; i < 16; ++i) tag[i] = acc[i];
  if (args->bytes_per_task) out[0] = acc[0];  // keep `out` live
#else
  chacha20poly1305_encrypt(args->key, nonce, nullptr, 0, in, args->bytes_per_task, out, tag);
#endif
#endif
}

} // namespace

int main() {
  kernel_arg_t* __UNIFORM__ arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);
  ccp_bench_status_t* status = (ccp_bench_status_t*)(uintptr_t)arg->status_addr;

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

#ifdef CHACHA20POLY1305_BENCH_DISPATCH_LANE
  vx_spawn_threads(1, &arg->num_tasks, nullptr,
                   (vx_kernel_func_cb)ccp_worker, &task_args);
#else
  uint32_t block_dim = vx_num_threads();
  vx_spawn_threads(1, &arg->num_tasks, &block_dim,
                   (vx_kernel_func_cb)ccp_worker, &task_args);
#endif
  status->completed_tasks = arg->num_tasks;
  return 0;
}
