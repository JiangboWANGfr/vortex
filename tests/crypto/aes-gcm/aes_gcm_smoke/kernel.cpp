// SPDX-License-Identifier: Apache-2.0
//
// Device-side AES-256-GCM known-answer test. For each NIST/GCM-spec vector,
// run aes256_gcm_encrypt and check the ciphertext and tag. SOFTWARE vs NATIVE
// is selected by the build (GCM_NATIVE -> hardware AES rounds + hardware GHASH).

#include <stdint.h>
#include <vx_intrinsics.h>
#include "aes_gcm.h"
#include "kat_vectors.h"
#include "common.h"

namespace {

bool bytes_equal(const uint8_t* a, const uint8_t* b, uint32_t n) {
  uint8_t diff = 0;
  for (uint32_t i = 0; i < n; ++i) diff |= (a[i] ^ b[i]);
  return diff == 0;
}

} // namespace

int main() {
  kernel_arg_t* __UNIFORM__ arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);
  gcm_smoke_status_t* status = (gcm_smoke_status_t*)(uintptr_t)arg->status_addr;

  uint32_t errors = 0;
  uint32_t failed_mask = 0;

  for (uint32_t k = 0; k < GCM_NUM_KATS; ++k) {
    const gcm_kat_t* kat = &kGcmKats[k];
    uint8_t ct[64] __attribute__((aligned(4)));
    uint8_t tag[GCM_TAG_BYTES] __attribute__((aligned(4)));

    aes256_gcm_encrypt(kat->key, kat->iv, kat->aad, kat->aad_len,
                       kat->pt, kat->pt_len, ct, tag);

    bool ok = bytes_equal(tag, kat->tag, GCM_TAG_BYTES);
    if (kat->pt_len != 0) {
      ok = ok && bytes_equal(ct, kat->ct, kat->pt_len);
    }
    if (!ok) {
      ++errors;
      failed_mask |= (1u << k);
    }
  }

  status->errors = errors;
  status->failed_mask = failed_mask;
  status->num_cases = GCM_NUM_KATS;
  return 0;
}
