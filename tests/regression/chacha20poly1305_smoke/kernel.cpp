// SPDX-License-Identifier: Apache-2.0
//
// Device-side ChaCha20-Poly1305 known-answer test (RFC 8439). Validates the
// Poly1305 MAC (sec 2.5.2) and the full AEAD ciphertext+tag (sec 2.8.2) on the
// Vortex SIMT GPGPU. Pure software reference (no crypto PE) -- this establishes
// the second AEAD; hardware ChaCha/Poly1305 PEs are a follow-up.

#include <stdint.h>
#include <vx_intrinsics.h>
#include "chacha20poly1305.h"
#include "kat_vectors.h"
#include "common.h"

namespace {

bool eq(const uint8_t* a, const uint8_t* b, uint32_t n) {
  uint8_t d = 0;
  for (uint32_t i = 0; i < n; ++i) d |= (a[i] ^ b[i]);
  return d == 0;
}

} // namespace

int main() {
  kernel_arg_t* __UNIFORM__ arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);
  ccp_smoke_status_t* status = (ccp_smoke_status_t*)(uintptr_t)arg->status_addr;

  uint32_t mask = 0;

  // 1. Poly1305 standalone (RFC 8439 sec 2.5.2)
  uint8_t ptag[16];
  poly1305_mac(ptag, POLY_MSG, sizeof(POLY_MSG), POLY_KEY);
  if (!eq(ptag, POLY_TAG, 16)) mask |= CCP_FAIL_POLY1305;

  // 2. ChaCha20-Poly1305 AEAD (RFC 8439 sec 2.8.2)
  uint8_t ct[114];
  uint8_t tag[16];
  chacha20poly1305_encrypt(AEAD_KEY, AEAD_NONCE, AEAD_AAD, sizeof(AEAD_AAD),
                           AEAD_PT, sizeof(AEAD_PT), ct, tag);
  if (!eq(ct, AEAD_CT, sizeof(AEAD_CT))) mask |= CCP_FAIL_AEAD_CT;
  if (!eq(tag, AEAD_TAG, 16)) mask |= CCP_FAIL_AEAD_TAG;

  status->failed_mask = mask;
  status->errors = (mask != 0);
  return 0;
}
