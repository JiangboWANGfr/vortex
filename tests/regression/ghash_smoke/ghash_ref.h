// SPDX-License-Identifier: Apache-2.0
//
// Spec-faithful GHASH reference for Vortex confidential-compute work.
//
// Implements GHASH and the underlying GF(2^128) multiplication exactly as
// defined in NIST SP 800-38D ("Recommendation for Block Cipher Modes of
// Operation: Galois/Counter Mode (GCM) and GMAC"), §6.3 Algorithm 1.
//
// Design choices (deliberately the slowest-but-clearest variant):
//   * Bit-by-bit multiplier, no precomputed tables. This is what RTL will
//     mirror cycle-by-cycle, so the C model can later serve as a gold
//     standard for hardware verification.
//   * Big-endian byte order throughout. NIST defines bit 0 of the
//     polynomial as the MOST-significant bit of byte 0; uint8_t[16] avoids
//     any host-endianness ambiguity.
//   * Header-only; identical translation unit usable from both Vortex
//     kernel (no malloc, no libc beyond <string.h>) and host code.

#ifndef GHASH_REF_H
#define GHASH_REF_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GHASH_BLOCK_BYTES 16

typedef struct {
  uint8_t H[GHASH_BLOCK_BYTES]; // hash subkey
  uint8_t Y[GHASH_BLOCK_BYTES]; // running tag state
} ghash_ctx_t;

// Z = X * Y in GF(2^128) with reduction polynomial
//     R(x) = x^128 + x^7 + x^2 + x + 1.
// NIST SP 800-38D §6.3 Algorithm 1, line-by-line.
static inline void gf128_mul(uint8_t Z[GHASH_BLOCK_BYTES],
                             const uint8_t X[GHASH_BLOCK_BYTES],
                             const uint8_t Y[GHASH_BLOCK_BYTES]) {
  uint8_t V[GHASH_BLOCK_BYTES];
  for (int i = 0; i < GHASH_BLOCK_BYTES; ++i)
    V[i] = Y[i];
  for (int i = 0; i < GHASH_BLOCK_BYTES; ++i)
    Z[i] = 0;

  for (int i = 0; i < 128; ++i) {
    // X[i] in NIST bit numbering = (X[i/8] >> (7 - i%8)) & 1.
    int byte_idx = i >> 3;
    int bit_off = 7 - (i & 7);
    if ((X[byte_idx] >> bit_off) & 1u) {
      for (int j = 0; j < GHASH_BLOCK_BYTES; ++j)
        Z[j] ^= V[j];
    }

    // V[127] = LSB of V[15] in NIST bit numbering.
    int v_lsb = V[GHASH_BLOCK_BYTES - 1] & 1u;

    // V >>= 1 across 128 bits, MSB-first byte order.
    for (int j = GHASH_BLOCK_BYTES - 1; j > 0; --j) {
      V[j] = (uint8_t)((V[j] >> 1) | ((V[j - 1] & 1u) << 7));
    }
    V[0] = (uint8_t)(V[0] >> 1);

    if (v_lsb) {
      // V ^= R, where R = 0xe1 || 0x00 * 15.
      V[0] ^= 0xe1;
    }
  }
}

static inline void ghash_init(ghash_ctx_t *ctx, const uint8_t H[GHASH_BLOCK_BYTES]) {
  for (int i = 0; i < GHASH_BLOCK_BYTES; ++i)
    ctx->H[i] = H[i];
  for (int i = 0; i < GHASH_BLOCK_BYTES; ++i)
    ctx->Y[i] = 0;
}

// Process one 16-byte block: Y := (Y ^ X) * H.
static inline void ghash_update_block(ghash_ctx_t *ctx,
                                      const uint8_t block[GHASH_BLOCK_BYTES]) {
  uint8_t tmp[GHASH_BLOCK_BYTES];
  for (int i = 0; i < GHASH_BLOCK_BYTES; ++i)
    tmp[i] = ctx->Y[i] ^ block[i];
  gf128_mul(ctx->Y, tmp, ctx->H);
}

// Process len_bytes of data; len_bytes MUST be a multiple of GHASH_BLOCK_BYTES.
// Padding/length-block framing is the caller's responsibility (GCM layer).
static inline void ghash_update(ghash_ctx_t *ctx,
                                const uint8_t *data,
                                size_t len_bytes) {
  for (size_t off = 0; off < len_bytes; off += GHASH_BLOCK_BYTES) {
    ghash_update_block(ctx, data + off);
  }
}

static inline void ghash_final(const ghash_ctx_t *ctx,
                               uint8_t out[GHASH_BLOCK_BYTES]) {
  for (int i = 0; i < GHASH_BLOCK_BYTES; ++i)
    out[i] = ctx->Y[i];
}

// Convenience: GHASH(H, data) in one call.
static inline void ghash_oneshot(const uint8_t H[GHASH_BLOCK_BYTES],
                                 const uint8_t *data,
                                 size_t len_bytes,
                                 uint8_t out[GHASH_BLOCK_BYTES]) {
  ghash_ctx_t ctx;
  ghash_init(&ctx, H);
  ghash_update(&ctx, data, len_bytes);
  ghash_final(&ctx, out);
}

#ifdef __cplusplus
}
#endif

#endif // GHASH_REF_H
