// SPDX-License-Identifier: Apache-2.0
//
// ChaCha20-Poly1305 AEAD (RFC 8439 sec 2.8). Composes the ChaCha20 stream
// cipher and the Poly1305 one-time MAC -- the ARX + 2^130-5 counterpart to
// AES-256-GCM (SPN + GF(2^128)) for the algorithm-agnostic study. Header-only.
//
// The MAC input AAD||pad16||C||pad16||len is always a multiple of 16 bytes, so
// Poly1305 here only sees full blocks. It is built contiguously in a bounded
// local buffer (CHACHA_AEAD_MAX_PT cap), which suits the smoke / small chunks.

#ifndef CHACHA20POLY1305_H
#define CHACHA20POLY1305_H

#include <stdint.h>
#include <stddef.h>
#include "chacha20.h"
#include "poly1305.h"

#ifndef CHACHA_AEAD_MAX_PT
#define CHACHA_AEAD_MAX_PT 1024
#endif
#ifndef CHACHA_AEAD_MAX_AAD
#define CHACHA_AEAD_MAX_AAD 64
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline void cc20p_st64(uint8_t* p, uint64_t v) {
  for (int i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8 * i));
}

// AEAD encrypt. key=32B, nonce=12B, tag=16B out. ct must hold pt_len bytes.
static inline void chacha20poly1305_encrypt(const uint8_t key[32],
                                            const uint8_t nonce[12],
                                            const uint8_t* aad, size_t aad_len,
                                            const uint8_t* pt, size_t pt_len,
                                            uint8_t* ct, uint8_t tag[16]) {
  // One-time Poly1305 key = first 32 bytes of ChaCha20 keystream, counter 0.
  uint8_t block0[64];
  chacha20_block(key, 0, nonce, block0);

  // Ciphertext = ChaCha20 with counter starting at 1.
  chacha20_xor(key, 1, nonce, pt, pt_len, ct);

  // mac_data = AAD || pad16 || C || pad16 || le64(aad_len) || le64(ct_len)
  size_t aad_pad = (16 - (aad_len & 15)) & 15;
  size_t ct_pad  = (16 - (pt_len & 15)) & 15;
  uint8_t mac_data[CHACHA_AEAD_MAX_AAD + 16 + CHACHA_AEAD_MAX_PT + 16 + 16];
  size_t n = 0;
  for (size_t i = 0; i < aad_len; ++i) mac_data[n++] = aad[i];
  for (size_t i = 0; i < aad_pad; ++i) mac_data[n++] = 0;
  for (size_t i = 0; i < pt_len; ++i) mac_data[n++] = ct[i];
  for (size_t i = 0; i < ct_pad; ++i) mac_data[n++] = 0;
  cc20p_st64(mac_data + n, (uint64_t)aad_len); n += 8;
  cc20p_st64(mac_data + n, (uint64_t)pt_len);  n += 8;

  poly1305_mac(tag, mac_data, n, block0);
}

#ifdef __cplusplus
}
#endif

#endif // CHACHA20POLY1305_H
