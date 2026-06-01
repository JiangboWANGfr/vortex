// SPDX-License-Identifier: Apache-2.0
//
// Poly1305 one-time MAC (RFC 8439 sec 2.5). Software reference using the
// well-known radix-2^26, 5-limb formulation (poly1305-donna-32): the
// accumulator a = (a + block) * r mod (2^130 - 5), a Horner chain in the prime
// field 2^130-5 -- the structural analogue of GHASH's GF(2^128) Horner chain,
// which makes it the natural second MAC for the algorithm-agnostic study.

#ifndef POLY1305_H
#define POLY1305_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t poly_le32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
       | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void poly_st32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

// mac[16] = Poly1305(m[0..len), key[32]).  key = r(16) || s(16).
static inline void poly1305_mac(uint8_t mac[16], const uint8_t* m, size_t len,
                                const uint8_t key[32]) {
  uint32_t t0 = poly_le32(key + 0),  t1 = poly_le32(key + 4);
  uint32_t t2 = poly_le32(key + 8),  t3 = poly_le32(key + 12);

  // clamp r and split into 26-bit limbs
  uint32_t r0 =  t0                       & 0x3ffffff;
  uint32_t r1 = ((t0 >> 26) | (t1 << 6))  & 0x3ffff03;
  uint32_t r2 = ((t1 >> 20) | (t2 << 12)) & 0x3ffc0ff;
  uint32_t r3 = ((t2 >> 14) | (t3 << 18)) & 0x3f03fff;
  uint32_t r4 =  (t3 >> 8)                & 0x00fffff;
  uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;

  uint32_t h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0;

  while (len > 0) {
    size_t want = (len < 16) ? len : 16;
    uint8_t block[16];
    for (int i = 0; i < 16; ++i) block[i] = 0;
    for (size_t i = 0; i < want; ++i) block[i] = m[i];
    if (want < 16) block[want] = 1;   // partial: high bit at byte `want`

    uint32_t b0 = poly_le32(block),     b1 = poly_le32(block + 4);
    uint32_t b2 = poly_le32(block + 8), b3 = poly_le32(block + 12);

    h0 += b0 & 0x3ffffff;
    h1 += ((b0 >> 26) | (b1 << 6))  & 0x3ffffff;
    h2 += ((b1 >> 20) | (b2 << 12)) & 0x3ffffff;
    h3 += ((b2 >> 14) | (b3 << 18)) & 0x3ffffff;
    h4 += (b3 >> 8) | (want == 16 ? (1u << 24) : 0u);  // full block: 2^128

    uint64_t d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * s4 + (uint64_t)h2 * s3 + (uint64_t)h3 * s2 + (uint64_t)h4 * s1;
    uint64_t d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0 + (uint64_t)h2 * s4 + (uint64_t)h3 * s3 + (uint64_t)h4 * s2;
    uint64_t d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1 + (uint64_t)h2 * r0 + (uint64_t)h3 * s4 + (uint64_t)h4 * s3;
    uint64_t d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2 + (uint64_t)h2 * r1 + (uint64_t)h3 * r0 + (uint64_t)h4 * s4;
    uint64_t d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3 + (uint64_t)h2 * r2 + (uint64_t)h3 * r1 + (uint64_t)h4 * r0;

    uint32_t c;
    c = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x3ffffff; d1 += c;
    c = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x3ffffff; d2 += c;
    c = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x3ffffff; d3 += c;
    c = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x3ffffff; d4 += c;
    c = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x3ffffff; h0 += c * 5;
    c = h0 >> 26;             h0 = h0 & 0x3ffffff;           h1 += c;

    m += want;
    len -= want;
  }

  // final carry propagation
  uint32_t c;
  c = h1 >> 26; h1 &= 0x3ffffff; h2 += c;
  c = h2 >> 26; h2 &= 0x3ffffff; h3 += c;
  c = h3 >> 26; h3 &= 0x3ffffff; h4 += c;
  c = h4 >> 26; h4 &= 0x3ffffff; h0 += c * 5;
  c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;

  // compute h + -p (i.e. h - (2^130-5)) and select if no borrow
  uint32_t g0 = h0 + 5;            c = g0 >> 26; g0 &= 0x3ffffff;
  uint32_t g1 = h1 + c;            c = g1 >> 26; g1 &= 0x3ffffff;
  uint32_t g2 = h2 + c;            c = g2 >> 26; g2 &= 0x3ffffff;
  uint32_t g3 = h3 + c;            c = g3 >> 26; g3 &= 0x3ffffff;
  uint32_t g4 = h4 + c - (1u << 26);

  uint32_t mask = (g4 >> 31) - 1;  // 0 if g<0 (keep h), all-ones if g>=0 (use g)
  g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
  mask = ~mask;
  h0 = (h0 & mask) | g0; h1 = (h1 & mask) | g1; h2 = (h2 & mask) | g2;
  h3 = (h3 & mask) | g3; h4 = (h4 & mask) | g4;

  // pack 26-bit limbs into 32-bit words (mod 2^128)
  h0 = (h0)        | (h1 << 26);
  h1 = (h1 >> 6)   | (h2 << 20);
  h2 = (h2 >> 12)  | (h3 << 14);
  h3 = (h3 >> 18)  | (h4 << 8);

  // tag = (h + s) mod 2^128
  uint64_t f;
  f = (uint64_t)h0 + poly_le32(key + 16);             h0 = (uint32_t)f;
  f = (uint64_t)h1 + poly_le32(key + 20) + (f >> 32); h1 = (uint32_t)f;
  f = (uint64_t)h2 + poly_le32(key + 24) + (f >> 32); h2 = (uint32_t)f;
  f = (uint64_t)h3 + poly_le32(key + 28) + (f >> 32); h3 = (uint32_t)f;

  poly_st32(mac + 0, h0); poly_st32(mac + 4, h1);
  poly_st32(mac + 8, h2); poly_st32(mac + 12, h3);
}

#ifdef __cplusplus
}
#endif

#endif // POLY1305_H
