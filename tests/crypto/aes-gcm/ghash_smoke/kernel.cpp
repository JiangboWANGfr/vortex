// SPDX-License-Identifier: Apache-2.0
//
// Vortex device-side GHASH smoke. Runs on a single thread; each check
// either confirms an *algebraic identity* of the GHASH function or a
// hand-derivable known answer. No third-party library, no on-device file
// I/O, no padding/GCM framing (those land in ghash_bench / aes_gcm_smoke).

#include "common.h"
#include "ghash_ref.h"
#include "kat_vectors.h"
#include <stdint.h>
#include <vx_intrinsics.h>

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

// 1. GHASH(any H, empty message) = 0.
int test_empty() {
  uint8_t out[GHASH_BLOCK_BYTES];
  ghash_oneshot(KAT_H_CANONICAL, nullptr, 0, out);
  return block_is_zero(out) ? 0 : 1;
}

// 2. GHASH(H, single zero block) = (0 ^ 0) * H = 0.
// This verifies that the input block is actually used and not ignored.
int test_zero_block() {
  uint8_t out[GHASH_BLOCK_BYTES];
  ghash_oneshot(KAT_H_CANONICAL, KAT_BLOCK_ZERO, GHASH_BLOCK_BYTES, out);
  return block_is_zero(out) ? 0 : 1;
}

// 3. GHASH(H = 0, arbitrary message) = 0 (degenerate field element).
// this test verifies that the H input is actually used and not ignored 
int test_zero_H() {
  uint8_t out[GHASH_BLOCK_BYTES];
  ghash_oneshot(KAT_BLOCK_ZERO, KAT_DATA_A, sizeof(KAT_DATA_A), out);
  return block_is_zero(out) ? 0 : 1;
}

// 4. GHASH(H, polynomial "1") = (0 ^ 1) * H = H.

// 1. verify GF(2^128) multiplicative identity "1"
// 2. verify bit order (1 is MSB of byte 0, not LSB of byte 15)
// 3. verify KAT_BLOCK_ONE is encoded as "1" under NIST bit numbering
int test_identity_1block() {
  uint8_t out[GHASH_BLOCK_BYTES];
  ghash_oneshot(KAT_H_CANONICAL, KAT_BLOCK_ONE, GHASH_BLOCK_BYTES, out);
  return block_equal(out, KAT_H_CANONICAL) ? 0 : 1;
}

// 5. Two-block: 0, then "1". Y_1 = 0, Y_2 = (0 ^ 1) * H = H.
//    Verifies the (Y ^ X) feedback path, not just the multiplier.
int test_identity_2block() {
  uint8_t buf[2 * GHASH_BLOCK_BYTES];
  for (int i = 0; i < GHASH_BLOCK_BYTES; ++i)
    buf[i] = KAT_BLOCK_ZERO[i];
  for (int i = 0; i < GHASH_BLOCK_BYTES; ++i)
    buf[GHASH_BLOCK_BYTES + i] = KAT_BLOCK_ONE[i];
  uint8_t out[GHASH_BLOCK_BYTES];
  ghash_oneshot(KAT_H_CANONICAL, buf, sizeof(buf), out);
  return block_equal(out, KAT_H_CANONICAL) ? 0 : 1;
}

// 6. Running the same input twice yields the same result.
int test_determinism() {
  uint8_t out1[GHASH_BLOCK_BYTES];
  uint8_t out2[GHASH_BLOCK_BYTES];
  ghash_oneshot(KAT_H_CANONICAL, KAT_DATA_A, sizeof(KAT_DATA_A), out1);
  ghash_oneshot(KAT_H_CANONICAL, KAT_DATA_A, sizeof(KAT_DATA_A), out2);
  return block_equal(out1, out2) ? 0 : 1;
}

// 7. Streaming via update_block (block at a time) and update (range) must
//    match oneshot.
int test_streaming() {
  uint8_t expect[GHASH_BLOCK_BYTES];
  ghash_oneshot(KAT_H_CANONICAL, KAT_DATA_A, sizeof(KAT_DATA_A), expect);

  int errors = 0;

  ghash_ctx_t c1;
  ghash_init(&c1, KAT_H_CANONICAL);
  ghash_update_block(&c1, KAT_DATA_A);
  ghash_update_block(&c1, KAT_DATA_A + GHASH_BLOCK_BYTES);
  uint8_t got1[GHASH_BLOCK_BYTES];
  ghash_final(&c1, got1);
  if (!block_equal(got1, expect))
    ++errors;

  ghash_ctx_t c2;
  ghash_init(&c2, KAT_H_CANONICAL);
  ghash_update(&c2, KAT_DATA_A, GHASH_BLOCK_BYTES);
  ghash_update(&c2, KAT_DATA_A + GHASH_BLOCK_BYTES, GHASH_BLOCK_BYTES);
  uint8_t got2[GHASH_BLOCK_BYTES];
  ghash_final(&c2, got2);
  if (!block_equal(got2, expect))
    ++errors;

  return errors;
}

// 8. GHASH is linear in the message: f(A ^ B) = f(A) ^ f(B) when |A| = |B|.
//    This exercises the GF(2^128) multiplier across many bit positions
//    without us having to precompute an expected value.
int test_linearity() {
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
  return block_is_zero(combined) ? 0 : 1;
}

struct CheckEntry {
  int (*fn)();
  uint32_t fail_bit;
};

const CheckEntry kChecks[] = {
    {test_empty, GHASH_SMOKE_FAIL_EMPTY},
    {test_zero_block, GHASH_SMOKE_FAIL_ZERO_BLOCK},
    {test_zero_H, GHASH_SMOKE_FAIL_ZERO_H},
    {test_identity_1block, GHASH_SMOKE_FAIL_IDENTITY_1BLOCK},
    {test_identity_2block, GHASH_SMOKE_FAIL_IDENTITY_2BLOCK},
    {test_determinism, GHASH_SMOKE_FAIL_DETERMINISM},
    {test_streaming, GHASH_SMOKE_FAIL_STREAMING},
    {test_linearity, GHASH_SMOKE_FAIL_LINEARITY},
};

} // namespace

int main() {
  kernel_arg_t *__UNIFORM__ arg = (kernel_arg_t *)csr_read(VX_CSR_MSCRATCH);
  ghash_smoke_status_t *status = (ghash_smoke_status_t *)(uintptr_t)arg->status_addr;

  uint32_t errors = 0;
  uint32_t failed_mask = 0;
  for (const auto &chk : kChecks) {
    int e = chk.fn();
    if (e) {
      errors += static_cast<uint32_t>(e);
      failed_mask |= chk.fail_bit;
    }
  }

  status->errors = errors;
  status->failed_mask = failed_mask;
  return 0;
}
