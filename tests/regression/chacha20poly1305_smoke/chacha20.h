// SPDX-License-Identifier: Apache-2.0
//
// ChaCha20 stream cipher (RFC 8439 sec 2.3-2.4). Software reference for the
// algorithm-agnostic study: ChaCha20 is an ARX cipher (add-rotate-xor on
// 32-bit words), structurally the opposite of AES's S-box/GF SPN. Header-only,
// usable from the Vortex kernel and host.

#ifndef CHACHA20_H
#define CHACHA20_H

#include <stdint.h>
#include <stddef.h>

#ifdef CHACHA20_NATIVE
#include <vx_intrinsics.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t cc20_rotl32(uint32_t x, int n) {
  return (x << n) | (x >> (32 - n));
}

static inline uint32_t cc20_le32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
       | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void cc20_st32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

#define CC20_QR(a, b, c, d)                  \
  a += b; d ^= a; d = cc20_rotl32(d, 16);    \
  c += d; b ^= c; b = cc20_rotl32(b, 12);    \
  a += b; d ^= a; d = cc20_rotl32(d, 8);     \
  c += d; b ^= c; b = cc20_rotl32(b, 7);

// One 64-byte ChaCha20 keystream block for the given 32-bit block counter.
static inline void chacha20_block(const uint8_t key[32], uint32_t counter,
                                  const uint8_t nonce[12], uint8_t out[64]) {
  uint32_t st[16];
  st[0] = 0x61707865; st[1] = 0x3320646e; st[2] = 0x79622d32; st[3] = 0x6b206574;
  for (int i = 0; i < 8; ++i) st[4 + i] = cc20_le32(key + 4 * i);
  st[12] = counter;
  for (int i = 0; i < 3; ++i) st[13 + i] = cc20_le32(nonce + 4 * i);

#ifdef CHACHA20_NATIVE
  // Hardware ChaCha PE: load the 16 words, run permute+feedforward, read back.
  // The PE returns post-feedforward keystream words; byte order stays in SW.
  for (int i = 0; i < 16; ++i) __intrin_chacha_wr((uint32_t)i, st[i]);
  __intrin_chacha_block();
  for (int i = 0; i < 16; ++i) cc20_st32(out + 4 * i, __intrin_chacha_rd((uint32_t)i));
#else
  uint32_t x[16];
  for (int i = 0; i < 16; ++i) x[i] = st[i];
  for (int i = 0; i < 10; ++i) {
    CC20_QR(x[0], x[4], x[8],  x[12])
    CC20_QR(x[1], x[5], x[9],  x[13])
    CC20_QR(x[2], x[6], x[10], x[14])
    CC20_QR(x[3], x[7], x[11], x[15])
    CC20_QR(x[0], x[5], x[10], x[15])
    CC20_QR(x[1], x[6], x[11], x[12])
    CC20_QR(x[2], x[7], x[8],  x[13])
    CC20_QR(x[3], x[4], x[9],  x[14])
  }
  for (int i = 0; i < 16; ++i) cc20_st32(out + 4 * i, x[i] + st[i]);
#endif
}

// Encrypt/decrypt len bytes with the keystream starting at block counter0.
static inline void chacha20_xor(const uint8_t key[32], uint32_t counter0,
                                const uint8_t nonce[12], const uint8_t* in,
                                size_t len, uint8_t* out) {
  uint8_t ks[64];
  size_t off = 0;
  uint32_t ctr = counter0;
  while (off < len) {
    chacha20_block(key, ctr, nonce, ks);
    size_t n = (len - off < 64) ? (len - off) : 64;
    for (size_t i = 0; i < n; ++i) out[off + i] = in[off + i] ^ ks[i];
    off += n;
    ++ctr;
  }
}

#ifdef __cplusplus
}
#endif

#endif // CHACHA20_H
