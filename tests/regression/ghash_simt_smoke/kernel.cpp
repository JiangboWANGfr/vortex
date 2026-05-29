// SPDX-License-Identifier: Apache-2.0
//
// SIMT version of the GHASH smoke. Each spawned task re-runs the same
// algebraic identity suite on the same inputs. Two things matter:
//   1. correctness under SIMT (no shared-state corruption, deterministic
//      across lanes) — the per-task error byte tells us per lane;
//   2. *cycles for N parallel GHASH instances* on the existing software
//      pipeline — this is the baseline that hardware GHASH and warp-level
//      multi-chain will need to beat.
//
// We reuse the spec-faithful reference and KAT data from ghash_smoke; the
// reference is header-only so it inlines into each task without any extra
// linking.

#include "../ghash_smoke/ghash_ref.h"
#include "../ghash_smoke/kat_vectors.h"
#include "common.h"
#include <stdint.h>
#include <vx_spawn.h>

namespace {

inline bool block_equal(const uint8_t a[GHASH_BLOCK_BYTES],
                        const uint8_t b[GHASH_BLOCK_BYTES]) {
  uint8_t diff = 0;
  for (int i = 0; i < GHASH_BLOCK_BYTES; ++i)
    diff |= (a[i] ^ b[i]);
  return diff == 0;
}

inline bool block_is_zero(const uint8_t a[GHASH_BLOCK_BYTES]) {
  uint8_t diff = 0;
  for (int i = 0; i < GHASH_BLOCK_BYTES; ++i)
    diff |= a[i];
  return diff == 0;
}

// Run the eight algebraic checks; return per-task bitmask of failures.
uint8_t run_checks() {
  uint8_t mask = 0;

  // 1. empty
  {
    uint8_t out[GHASH_BLOCK_BYTES];
    ghash_oneshot(KAT_H_CANONICAL, nullptr, 0, out);
    if (!block_is_zero(out))
      mask |= GHASH_SIMT_FAIL_EMPTY;
  }
  // 2. single zero block
  {
    uint8_t out[GHASH_BLOCK_BYTES];
    ghash_oneshot(KAT_H_CANONICAL, KAT_BLOCK_ZERO, GHASH_BLOCK_BYTES, out);
    if (!block_is_zero(out))
      mask |= GHASH_SIMT_FAIL_ZERO_BLOCK;
  }
  // 3. zero H
  {
    uint8_t out[GHASH_BLOCK_BYTES];
    ghash_oneshot(KAT_BLOCK_ZERO, KAT_DATA_A, sizeof(KAT_DATA_A), out);
    if (!block_is_zero(out))
      mask |= GHASH_SIMT_FAIL_ZERO_H;
  }
  // 4. "1" -> H
  {
    uint8_t out[GHASH_BLOCK_BYTES];
    ghash_oneshot(KAT_H_CANONICAL, KAT_BLOCK_ONE, GHASH_BLOCK_BYTES, out);
    if (!block_equal(out, KAT_H_CANONICAL))
      mask |= GHASH_SIMT_FAIL_IDENTITY_1BLOCK;
  }
  // 5. 0||"1" -> H
  {
    uint8_t buf[2 * GHASH_BLOCK_BYTES];
    for (int i = 0; i < GHASH_BLOCK_BYTES; ++i)
      buf[i] = KAT_BLOCK_ZERO[i];
    for (int i = 0; i < GHASH_BLOCK_BYTES; ++i)
      buf[GHASH_BLOCK_BYTES + i] = KAT_BLOCK_ONE[i];
    uint8_t out[GHASH_BLOCK_BYTES];
    ghash_oneshot(KAT_H_CANONICAL, buf, sizeof(buf), out);
    if (!block_equal(out, KAT_H_CANONICAL))
      mask |= GHASH_SIMT_FAIL_IDENTITY_2BLOCK;
  }
  // 6. determinism
  {
    uint8_t out1[GHASH_BLOCK_BYTES], out2[GHASH_BLOCK_BYTES];
    ghash_oneshot(KAT_H_CANONICAL, KAT_DATA_A, sizeof(KAT_DATA_A), out1);
    ghash_oneshot(KAT_H_CANONICAL, KAT_DATA_A, sizeof(KAT_DATA_A), out2);
    if (!block_equal(out1, out2))
      mask |= GHASH_SIMT_FAIL_DETERMINISM;
  }
  // 7. streaming == oneshot
  {
    uint8_t expect[GHASH_BLOCK_BYTES];
    ghash_oneshot(KAT_H_CANONICAL, KAT_DATA_A, sizeof(KAT_DATA_A), expect);
    ghash_ctx_t c;
    ghash_init(&c, KAT_H_CANONICAL);
    ghash_update_block(&c, KAT_DATA_A);
    ghash_update_block(&c, KAT_DATA_A + GHASH_BLOCK_BYTES);
    uint8_t got[GHASH_BLOCK_BYTES];
    ghash_final(&c, got);
    if (!block_equal(got, expect))
      mask |= GHASH_SIMT_FAIL_STREAMING;
  }
  // 8. linearity: f(A^B) ^ f(A) ^ f(B) == 0
  {
    uint8_t xor_buf[sizeof(KAT_DATA_A)];
    for (size_t i = 0; i < sizeof(KAT_DATA_A); ++i)
      xor_buf[i] = KAT_DATA_A[i] ^ KAT_DATA_B[i];
    uint8_t fA[GHASH_BLOCK_BYTES], fB[GHASH_BLOCK_BYTES], fAB[GHASH_BLOCK_BYTES];
    ghash_oneshot(KAT_H_CANONICAL, KAT_DATA_A, sizeof(KAT_DATA_A), fA);
    ghash_oneshot(KAT_H_CANONICAL, KAT_DATA_B, sizeof(KAT_DATA_B), fB);
    ghash_oneshot(KAT_H_CANONICAL, xor_buf, sizeof(xor_buf), fAB);
    uint8_t combined[GHASH_BLOCK_BYTES];
    for (int i = 0; i < GHASH_BLOCK_BYTES; ++i)
      combined[i] = fA[i] ^ fB[i] ^ fAB[i];
    if (!block_is_zero(combined))
      mask |= GHASH_SIMT_FAIL_LINEARITY;
  }

  return mask;
}

struct TaskArgs {
  task_status_t *task_status;
};

void ghash_worker(const TaskArgs *__UNIFORM__ args) {
  uint32_t tid = blockIdx.x;
  args->task_status[tid] = run_checks();
}

} // namespace

int main() {
  kernel_arg_t *__UNIFORM__ arg = (kernel_arg_t *)csr_read(VX_CSR_MSCRATCH);
  ghash_simt_status_t *status =
      (ghash_simt_status_t *)(uintptr_t)arg->status_addr;
  task_status_t *task_status =
      (task_status_t *)(uintptr_t)arg->task_status_addr;

  status->errors = 0;
  status->failed_mask = 0;
  status->completed_tasks = 0;

  if (arg->num_tasks == 0) {
    return 0;
  }

  TaskArgs task_args = {task_status};
  vx_spawn_threads(1, &arg->num_tasks, nullptr,
                   (vx_kernel_func_cb)ghash_worker, &task_args);
  status->completed_tasks = arg->num_tasks;
  // Aggregation runs on a single warp after spawn returns to avoid races.
  uint32_t errs = 0;
  uint32_t mask = 0;
  for (uint32_t i = 0; i < arg->num_tasks; ++i) {
    uint8_t v = task_status[i];
    mask |= v;
    while (v) {
      errs += (v & 1);
      v >>= 1;
    }
  }
  status->errors = errs;
  status->failed_mask = mask;
  return 0;
}
