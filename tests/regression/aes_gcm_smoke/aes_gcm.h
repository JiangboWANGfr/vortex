// SPDX-License-Identifier: Apache-2.0
//
// AES-256-GCM authenticated encryption (NIST SP 800-38D), 96-bit IV.
//
// Composes the existing AES-256 block cipher (tests/kernel/aes256, with a
// hardware round path under AES_NATIVE) and the GHASH reference
// (ghash_smoke/ghash_ref.h, hardware path under GHASH_NATIVE). Define
// GCM_NATIVE to select the hardware path for both. Header-only; the caller
// links aes256-impl.c.
//
// Restriction: 96-bit IV (the GCM common case). AAD and plaintext may be any
// byte length; partial trailing blocks are handled per spec.

#ifndef AES_GCM_H
#define AES_GCM_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef GCM_NATIVE
#  ifndef AES_NATIVE
#    define AES_NATIVE
#  endif
#  ifndef GHASH_NATIVE
#    define GHASH_NATIVE
#  endif
#endif

#include "../../kernel/aes256/aes256.h"
#include "../ghash_smoke/ghash_ref.h"

#define GCM_BLOCK_BYTES 16
#define GCM_KEY_BYTES   32
#define GCM_IV_BYTES    12
#define GCM_TAG_BYTES   16
#define GCM_ROUND_WORDS (4 * (14 + 1))   // Nb*(Nr+1) for AES-256

#ifdef __cplusplus
extern "C" {
#endif

static inline void gcm_put64_be(uint8_t* p, uint64_t v) {
  p[0] = (uint8_t)(v >> 56); p[1] = (uint8_t)(v >> 48);
  p[2] = (uint8_t)(v >> 40); p[3] = (uint8_t)(v >> 32);
  p[4] = (uint8_t)(v >> 24); p[5] = (uint8_t)(v >> 16);
  p[6] = (uint8_t)(v >> 8);  p[7] = (uint8_t)(v);
}

// inc32: increment the rightmost 32 bits of a 128-bit block (big-endian).
static inline void gcm_inc32(uint8_t ctr[GCM_BLOCK_BYTES]) {
  for (int i = GCM_BLOCK_BYTES - 1; i >= GCM_BLOCK_BYTES - 4; --i) {
    if (++ctr[i] != 0) break;
  }
}

// GHASH a byte range, zero-padding a partial trailing block.
static inline void gcm_ghash_bytes(ghash_ctx_t* ctx, const uint8_t* data, size_t len) {
  uint8_t block[GCM_BLOCK_BYTES];
  size_t off = 0;
  for (; off + GCM_BLOCK_BYTES <= len; off += GCM_BLOCK_BYTES) {
    ghash_update_block(ctx, data + off);
  }
  if (off < len) {
    for (int i = 0; i < GCM_BLOCK_BYTES; ++i) block[i] = 0;
    for (size_t i = 0; off + i < len; ++i) block[i] = data[off + i];
    ghash_update_block(ctx, block);
  }
}

// AES-256-GCM encrypt + authenticate. key=32B, iv=12B, tag=16B out.
static inline void aes256_gcm_encrypt(const uint8_t key[GCM_KEY_BYTES],
                                      const uint8_t iv[GCM_IV_BYTES],
                                      const uint8_t* aad, size_t aad_len,
                                      const uint8_t* pt, size_t pt_len,
                                      uint8_t* ct, uint8_t tag[GCM_TAG_BYTES]) {
  uint32_t round_keys[GCM_ROUND_WORDS] __attribute__((aligned(4)));
  uint32_t key_words[GCM_KEY_BYTES / 4] __attribute__((aligned(4)));
  memcpy(key_words, key, GCM_KEY_BYTES);
  aes256_key_exp(key_words, round_keys, 0);

  // H = E_K(0^128).
  uint8_t H[GCM_BLOCK_BYTES] __attribute__((aligned(4)));
  uint8_t zero[GCM_BLOCK_BYTES] __attribute__((aligned(4))) = {0};
  aes256_ecb_enc(zero, round_keys, H, 1);

  // J0 = IV || 0^31 || 1.
  uint8_t J0[GCM_BLOCK_BYTES] __attribute__((aligned(4)));
  memcpy(J0, iv, GCM_IV_BYTES);
  J0[12] = 0; J0[13] = 0; J0[14] = 0; J0[15] = 1;

  // GCTR: C = P ^ E_K(inc32(J0)), E_K(inc32^2(J0)), ...
  uint8_t ctr[GCM_BLOCK_BYTES] __attribute__((aligned(4)));
  uint8_t ks[GCM_BLOCK_BYTES] __attribute__((aligned(4)));
  memcpy(ctr, J0, GCM_BLOCK_BYTES);
  size_t off = 0;
  for (; off + GCM_BLOCK_BYTES <= pt_len; off += GCM_BLOCK_BYTES) {
    gcm_inc32(ctr);
    aes256_ecb_enc(ctr, round_keys, ks, 1);
    for (int i = 0; i < GCM_BLOCK_BYTES; ++i) ct[off + i] = pt[off + i] ^ ks[i];
  }
  if (off < pt_len) {
    gcm_inc32(ctr);
    aes256_ecb_enc(ctr, round_keys, ks, 1);
    for (size_t i = 0; off + i < pt_len; ++i) ct[off + i] = pt[off + i] ^ ks[i];
  }

  // S = GHASH(AAD || C || [len(AAD)]_64 || [len(C)]_64).
  ghash_ctx_t gctx;
  ghash_init(&gctx, H);
  gcm_ghash_bytes(&gctx, aad, aad_len);
  gcm_ghash_bytes(&gctx, ct, pt_len);
  uint8_t lenblk[GCM_BLOCK_BYTES];
  gcm_put64_be(lenblk, (uint64_t)aad_len * 8);
  gcm_put64_be(lenblk + 8, (uint64_t)pt_len * 8);
  ghash_update_block(&gctx, lenblk);
  uint8_t S[GCM_BLOCK_BYTES];
  ghash_final(&gctx, S);

  // Tag = S ^ E_K(J0).
  uint8_t EJ0[GCM_BLOCK_BYTES] __attribute__((aligned(4)));
  aes256_ecb_enc(J0, round_keys, EJ0, 1);
  for (int i = 0; i < GCM_TAG_BYTES; ++i) tag[i] = S[i] ^ EJ0[i];
}

#ifdef __cplusplus
}
#endif

#endif // AES_GCM_H
