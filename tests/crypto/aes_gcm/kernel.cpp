#include <vx_spawn2.h>
#include "common.h"

// One CTA per core, one independent AES-128-GCM message per thread.
// The key schedule, the four T-tables and the GHASH table are copied into
// local memory once by the whole CTA and then read by every thread. That
// placement is the point of the baseline: a table lookup is a data-dependent
// gather across the SIMD width, and local memory banks as wide as the warp
// while the L1 data cache is single-banked.

namespace {

struct lmem_layout_t {
  uint32_t te[AES_TE_TABLES * AES_TE_ENTRIES];
  uint32_t rk[AES128_RK_BYTES / 4];
  uint8_t htable[GHASH_TABLE_BYTES];
};

inline uint32_t load_be32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
       | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

inline void store_be32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}

// T-table AES-128 encryption of one block, state held as four big-endian
// column words.
inline void aes128_encrypt(const lmem_layout_t* lm, const uint8_t in[16],
                           uint8_t out[16]) {
  const uint32_t* te0 = lm->te;
  const uint32_t* te1 = lm->te + AES_TE_ENTRIES;
  const uint32_t* te2 = lm->te + 2 * AES_TE_ENTRIES;
  const uint32_t* te3 = lm->te + 3 * AES_TE_ENTRIES;
  const uint32_t* rk = lm->rk;

  uint32_t s0 = load_be32(in) ^ rk[0];
  uint32_t s1 = load_be32(in + 4) ^ rk[1];
  uint32_t s2 = load_be32(in + 8) ^ rk[2];
  uint32_t s3 = load_be32(in + 12) ^ rk[3];

  for (int round = 1; round < AES128_ROUNDS; ++round) {
    const uint32_t* k = rk + 4 * round;
    const uint32_t t0 = te0[s0 >> 24] ^ te1[(s1 >> 16) & 0xff]
                      ^ te2[(s2 >> 8) & 0xff] ^ te3[s3 & 0xff] ^ k[0];
    const uint32_t t1 = te0[s1 >> 24] ^ te1[(s2 >> 16) & 0xff]
                      ^ te2[(s3 >> 8) & 0xff] ^ te3[s0 & 0xff] ^ k[1];
    const uint32_t t2 = te0[s2 >> 24] ^ te1[(s3 >> 16) & 0xff]
                      ^ te2[(s0 >> 8) & 0xff] ^ te3[s1 & 0xff] ^ k[2];
    const uint32_t t3 = te0[s3 >> 24] ^ te1[(s0 >> 16) & 0xff]
                      ^ te2[(s1 >> 8) & 0xff] ^ te3[s2 & 0xff] ^ k[3];
    s0 = t0; s1 = t1; s2 = t2; s3 = t3;
  }

  // The last round drops MixColumns, so take the plain S-box out of Te0.
  const uint32_t* k = rk + 4 * AES128_ROUNDS;
  const uint32_t f0 = (((te0[s0 >> 24] >> 16) & 0xff) << 24)
                    | (((te0[(s1 >> 16) & 0xff] >> 16) & 0xff) << 16)
                    | (((te0[(s2 >> 8) & 0xff] >> 16) & 0xff) << 8)
                    | ((te0[s3 & 0xff] >> 16) & 0xff);
  const uint32_t f1 = (((te0[s1 >> 24] >> 16) & 0xff) << 24)
                    | (((te0[(s2 >> 16) & 0xff] >> 16) & 0xff) << 16)
                    | (((te0[(s3 >> 8) & 0xff] >> 16) & 0xff) << 8)
                    | ((te0[s0 & 0xff] >> 16) & 0xff);
  const uint32_t f2 = (((te0[s2 >> 24] >> 16) & 0xff) << 24)
                    | (((te0[(s3 >> 16) & 0xff] >> 16) & 0xff) << 16)
                    | (((te0[(s0 >> 8) & 0xff] >> 16) & 0xff) << 8)
                    | ((te0[s1 & 0xff] >> 16) & 0xff);
  const uint32_t f3 = (((te0[s3 >> 24] >> 16) & 0xff) << 24)
                    | (((te0[(s0 >> 16) & 0xff] >> 16) & 0xff) << 16)
                    | (((te0[(s1 >> 8) & 0xff] >> 16) & 0xff) << 8)
                    | ((te0[s2 & 0xff] >> 16) & 0xff);

  store_be32(out, f0 ^ k[0]);
  store_be32(out + 4, f1 ^ k[1]);
  store_be32(out + 8, f2 ^ k[2]);
  store_be32(out + 12, f3 ^ k[3]);
}

// Y = Y * H over GF(2^128), four bits of Y per step against the precomputed
// table. Reduction constants for a four-bit shift, SP 800-38D section 6.3.
static const uint16_t kRem4[16] = {
  0x0000, 0x1c20, 0x3840, 0x2460, 0x7080, 0x6ca0, 0x48c0, 0x54e0,
  0xe100, 0xfd20, 0xd940, 0xc560, 0x9180, 0x8da0, 0xa9c0, 0xb5e0,
};

inline void ghash_mul(const uint8_t* htable, uint8_t y[16]) {
  // Y*H = sum over nibbles k of x^(4k) * (nibble_k * H), evaluated by Horner
  // from the highest k down, so the loop walks the last nibble first.
  uint8_t z[16] = {0};
  for (int i = 31; i >= 0; --i) {
    const uint8_t byte = y[i >> 1];
    const uint8_t nib = (i & 1) ? (uint8_t)(byte & 0x0f) : (uint8_t)(byte >> 4);
    if (i != 31) {
      // z >>= 4, folding the four bits that fall off back in.
      const uint8_t low = (uint8_t)(z[15] & 0x0f);
      for (int j = 15; j > 0; --j) {
        z[j] = (uint8_t)((z[j] >> 4) | (z[j - 1] << 4));
      }
      z[0] >>= 4;
      const uint16_t rem = kRem4[low];
      z[0] ^= (uint8_t)(rem >> 8);
      z[1] ^= (uint8_t)rem;
    }
    const uint8_t* h = htable + 16 * nib;
    for (int j = 0; j < 16; ++j) {
      z[j] ^= h[j];
    }
  }
  for (int j = 0; j < 16; ++j) {
    y[j] = z[j];
  }
}

inline void inc32(uint8_t ctr[16]) {
  for (int i = 15; i >= 12; --i) {
    if (++ctr[i] != 0) {
      break;
    }
  }
}

} // namespace

__kernel void aes_gcm_sw_ttable(kernel_arg_t* __UNIFORM__ arg) {
  lmem_layout_t* lm = (lmem_layout_t*)__local_mem();

  // Cooperative fill: the CTA spans every warp on the core, so the tables
  // must be filled by all of them and fenced before any thread reads them.
  {
    const uint32_t* te_src = (const uint32_t*)arg->te_addr;
    const uint32_t* rk_src = (const uint32_t*)arg->rk_addr;
    const uint32_t* ht_src = (const uint32_t*)arg->htable_addr;
    const uint32_t tid = threadIdx.x;
    const uint32_t nthreads = blockDim.x;
    for (uint32_t i = tid; i < AES_TE_TABLES * AES_TE_ENTRIES; i += nthreads) {
      lm->te[i] = te_src[i];
    }
    for (uint32_t i = tid; i < AES128_RK_BYTES / 4; i += nthreads) {
      lm->rk[i] = rk_src[i];
    }
    uint32_t* ht_dst = (uint32_t*)lm->htable;
    for (uint32_t i = tid; i < GHASH_TABLE_BYTES / 4; i += nthreads) {
      ht_dst[i] = ht_src[i];
    }
  }
  __syncthreads();

  const uint32_t num_msgs = arg->num_msgs;
  const uint32_t blocks = arg->blocks_per_msg;
  const uint8_t* iv_base = (const uint8_t*)arg->iv_addr;
  const uint8_t* src_base = (const uint8_t*)arg->src_addr;
  uint8_t* dst_base = (uint8_t*)arg->dst_addr;
  uint8_t* tag_base = (uint8_t*)arg->tag_addr;

  const uint32_t stride = gridDim.x * blockDim.x;
  for (uint32_t msg = blockIdx.x * blockDim.x + threadIdx.x; msg < num_msgs;
       msg += stride) {
    uint8_t j0[16];
    const uint8_t* iv = iv_base + GCM_IV_BYTES * msg;
    for (int i = 0; i < GCM_IV_BYTES; ++i) {
      j0[i] = iv[i];
    }
    j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;

    uint8_t ctr[16];
    for (int i = 0; i < 16; ++i) {
      ctr[i] = j0[i];
    }

    uint8_t y[16] = {0};
    const uint8_t* pt = src_base + (size_t)AES_BLOCK_BYTES * blocks * msg;
    uint8_t* ct = dst_base + (size_t)AES_BLOCK_BYTES * blocks * msg;
    for (uint32_t b = 0; b < blocks; ++b) {
      inc32(ctr);
      uint8_t ks[16];
      aes128_encrypt(lm, ctr, ks);
      for (int i = 0; i < 16; ++i) {
        const uint8_t c = (uint8_t)(pt[16 * b + i] ^ ks[i]);
        ct[16 * b + i] = c;
        y[i] ^= c;
      }
      ghash_mul(lm->htable, y);
    }

    const uint64_t cbits = (uint64_t)blocks * 128u;
    for (int i = 0; i < 8; ++i) {
      y[15 - i] ^= (uint8_t)(cbits >> (8 * i));
    }
    ghash_mul(lm->htable, y);

    uint8_t ej0[16];
    aes128_encrypt(lm, j0, ej0);
    uint8_t* tag = tag_base + GCM_TAG_BYTES * msg;
    for (int i = 0; i < 16; ++i) {
      tag[i] = (uint8_t)(y[i] ^ ej0[i]);
    }
  }
}
