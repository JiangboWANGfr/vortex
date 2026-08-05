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

  // Branchless / constant-time: data-dependent choices use byte masks instead
  // of `if`, so the routine is side-channel hardened AND does not diverge when
  // independent lanes process different data under SIMT (LANE dispatch). The
  // hardware GHASH PE is likewise branchless, so the two stay bit-identical.
  for (int i = 0; i < 128; ++i) {
    // X[i] in NIST bit numbering = (X[i/8] >> (7 - i%8)) & 1.
    int byte_idx = i >> 3;
    int bit_off = 7 - (i & 7);
    uint8_t x_bit = (uint8_t)((X[byte_idx] >> bit_off) & 1u);
    uint8_t x_mask = (uint8_t)(0u - x_bit); // 0x00 if bit clear, 0xFF if set
    for (int j = 0; j < GHASH_BLOCK_BYTES; ++j)
      Z[j] ^= (uint8_t)(V[j] & x_mask);

    // V[127] = LSB of V[15] in NIST bit numbering.
    uint8_t v_lsb = (uint8_t)(V[GHASH_BLOCK_BYTES - 1] & 1u);

    // V >>= 1 across 128 bits, MSB-first byte order.
    for (int j = GHASH_BLOCK_BYTES - 1; j > 0; --j) {
      V[j] = (uint8_t)((V[j] >> 1) | ((V[j - 1] & 1u) << 7));
    }
    V[0] = (uint8_t)(V[0] >> 1);

    // V ^= R (R = 0xe1 || 0x00*15) iff v_lsb, via mask.
    V[0] ^= (uint8_t)(0xe1u & (uint8_t)(0u - v_lsb));
  }
}

#ifdef GHASH_NATIVE
// Hardware-accelerated path: drive the GHASH PE via custom intrinsics. The
// per-warp {H, Y} state lives in hardware; the ctx struct is unused (one GHASH
// instance per warp at a time). 128-bit values map to two 64-bit words with the
// big-endian convention: word 1 = bytes[0..7] (MSB), word 0 = bytes[8..15].
#include <vx_intrinsics.h>

static inline uint64_t ghash_load64_be(const uint8_t *p) {
  return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48)
       | ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32)
       | ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16)
       | ((uint64_t)p[6] << 8)  | ((uint64_t)p[7]);
}

static inline void ghash_store64_be(uint8_t *p, uint64_t v) {
  p[0] = (uint8_t)(v >> 56); p[1] = (uint8_t)(v >> 48);
  p[2] = (uint8_t)(v >> 40); p[3] = (uint8_t)(v >> 32);
  p[4] = (uint8_t)(v >> 24); p[5] = (uint8_t)(v >> 16);
  p[6] = (uint8_t)(v >> 8);  p[7] = (uint8_t)(v);
}

static inline void ghash_init(ghash_ctx_t *ctx, const uint8_t H[GHASH_BLOCK_BYTES]) {
  (void)ctx;
  __intrin_ghash_seth(ghash_load64_be(H + 0), 1); // H[127:64] = bytes 0..7
  __intrin_ghash_seth(ghash_load64_be(H + 8), 0); // H[63:0]  = bytes 8..15
  // Clear Y: Y[w] ^= Y[w].
  __intrin_ghash_xor(__intrin_ghash_rd(0), 0);
  __intrin_ghash_xor(__intrin_ghash_rd(1), 1);
}

static inline void ghash_update_block(ghash_ctx_t *ctx,
                                      const uint8_t block[GHASH_BLOCK_BYTES]) {
  (void)ctx;
  __intrin_ghash_xor(ghash_load64_be(block + 0), 1);
  __intrin_ghash_xor(ghash_load64_be(block + 8), 0);
  __intrin_ghash_mul(); // Y = (Y ^ block) * H
}

static inline void ghash_final(const ghash_ctx_t *ctx,
                               uint8_t out[GHASH_BLOCK_BYTES]) {
  (void)ctx;
  ghash_store64_be(out + 0, __intrin_ghash_rd(1)); // bytes 0..7  = Y[127:64]
  ghash_store64_be(out + 8, __intrin_ghash_rd(0)); // bytes 8..15 = Y[63:0]
}
#else
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

static inline void ghash_final(const ghash_ctx_t *ctx,
                               uint8_t out[GHASH_BLOCK_BYTES]) {
  for (int i = 0; i < GHASH_BLOCK_BYTES; ++i)
    out[i] = ctx->Y[i];
}
#endif // GHASH_NATIVE

// Process len_bytes of data; len_bytes MUST be a multiple of GHASH_BLOCK_BYTES.
// Padding/length-block framing is the caller's responsibility (GCM layer).
static inline void ghash_update(ghash_ctx_t *ctx,
                                const uint8_t *data,
                                size_t len_bytes) {
  for (size_t off = 0; off < len_bytes; off += GHASH_BLOCK_BYTES) {
    ghash_update_block(ctx, data + off);
  }
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
