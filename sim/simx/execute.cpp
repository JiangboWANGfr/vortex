// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <bitset>
#include <climits>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <util.h>
#include <rvfloats.h>
#include "emulator.h"
#include "instr.h"
#include "core.h"
#include "types.h"
#ifdef EXT_V_ENABLE
#include "processor_impl.h"
#endif
#include "VX_types.h"

using namespace vortex;

inline uint64_t nan_box(uint32_t value) {
  return value | 0xffffffff00000000;
}

inline bool is_nan_boxed(uint64_t value) {
  return (uint32_t(value >> 32) == 0xffffffff);
}

inline int64_t check_boxing(int64_t a) {
  if (is_nan_boxed(a))
    return a;
  return nan_box(0x7fc00000); // NaN
}

static inline uint32_t ror32(uint32_t value, uint32_t shamt) {
  shamt &= 31;
  return (value >> shamt) | (value << ((32 - shamt) & 31));
}

static inline uint32_t sha256sig0(uint32_t value) {
  return ror32(value, 7) ^ ror32(value, 18) ^ (value >> 3);
}

static inline uint32_t sha256sig1(uint32_t value) {
  return ror32(value, 17) ^ ror32(value, 19) ^ (value >> 10);
}

static inline uint32_t sha256sum0(uint32_t value) {
  return ror32(value, 2) ^ ror32(value, 13) ^ ror32(value, 22);
}

static inline uint32_t sha256sum1(uint32_t value) {
  return ror32(value, 6) ^ ror32(value, 11) ^ ror32(value, 25);
}

static inline uint64_t rotl64(uint64_t value, uint32_t shamt) {
  shamt &= 63;
  return (value << shamt) | (value >> ((64 - shamt) & 63));
}

static inline void keccak_f1600(std::array<uint64_t, 25>& state) {
  static const uint64_t round_constants[24] = {
    0x0000000000000001ull, 0x0000000000008082ull,
    0x800000000000808aull, 0x8000000080008000ull,
    0x000000000000808bull, 0x0000000080000001ull,
    0x8000000080008081ull, 0x8000000000008009ull,
    0x000000000000008aull, 0x0000000000000088ull,
    0x0000000080008009ull, 0x000000008000000aull,
    0x000000008000808bull, 0x800000000000008bull,
    0x8000000000008089ull, 0x8000000000008003ull,
    0x8000000000008002ull, 0x8000000000000080ull,
    0x000000000000800aull, 0x800000008000000aull,
    0x8000000080008081ull, 0x8000000000008080ull,
    0x0000000080000001ull, 0x8000000080008008ull,
  };

  for (uint32_t round = 0; round < 24; ++round) {
    uint64_t c0 = state[0] ^ state[5] ^ state[10] ^ state[15] ^ state[20];
    uint64_t c1 = state[1] ^ state[6] ^ state[11] ^ state[16] ^ state[21];
    uint64_t c3 = state[4] ^ state[9] ^ state[14] ^ state[19] ^ state[24];

    uint64_t c2 = rotl64(c1, 1) ^ c3;
    state[0] ^= c2;
    state[5] ^= c2;
    state[10] ^= c2;
    state[15] ^= c2;
    state[20] ^= c2;

    c2 = state[2] ^ state[7] ^ state[12] ^ state[17] ^ state[22];

    c3 = rotl64(c3, 1) ^ c2;
    c2 = rotl64(c2, 1) ^ c0;

    state[1] ^= c2;
    state[6] ^= c2;
    state[11] ^= c2;
    state[16] ^= c2;
    state[21] ^= c2;

    c2 = state[3] ^ state[8] ^ state[13] ^ state[18] ^ state[23];

    c0 = rotl64(c0, 1) ^ c2;
    c2 = rotl64(c2, 1) ^ c1;

    state[4] ^= c0;
    state[9] ^= c0;
    state[14] ^= c0;
    state[19] ^= c0;
    state[24] ^= c0;

    state[3] ^= c3;
    state[8] ^= c3;
    state[13] ^= c3;
    state[18] ^= c3;
    state[23] ^= c3;

    state[2] ^= c2;
    state[7] ^= c2;
    state[12] ^= c2;
    state[17] ^= c2;
    state[22] ^= c2;

    c1 = state[5];
    state[5] = rotl64(state[3], 28);
    state[3] = rotl64(state[18], 21);
    state[18] = rotl64(state[17], 15);
    state[17] = rotl64(state[11], 10);
    state[11] = rotl64(state[7], 6);
    state[7] = rotl64(state[10], 3);
    state[10] = rotl64(state[1], 1);
    state[1] = rotl64(state[6], 44);
    state[6] = rotl64(state[9], 20);
    state[9] = rotl64(state[22], 61);
    state[22] = rotl64(state[14], 39);
    state[14] = rotl64(state[20], 18);
    state[20] = rotl64(state[2], 62);
    state[2] = rotl64(state[12], 43);
    state[12] = rotl64(state[13], 25);
    state[13] = rotl64(state[19], 8);
    state[19] = rotl64(state[23], 56);
    state[23] = rotl64(state[15], 41);
    state[15] = rotl64(state[4], 27);
    state[4] = rotl64(state[24], 14);
    state[24] = rotl64(state[21], 2);
    state[21] = rotl64(state[8], 55);
    state[8] = rotl64(state[16], 45);
    state[16] = rotl64(c1, 36);

    c0 = (~state[3]) & state[4];
    state[4] ^= (~state[0]) & state[1];
    state[1] ^= (~state[2]) & state[3];
    state[3] ^= (~state[4]) & state[0];
    state[0] ^= (~state[1]) & state[2];
    state[2] ^= c0;

    c0 = (~state[8]) & state[9];
    state[9] ^= (~state[5]) & state[6];
    state[6] ^= (~state[7]) & state[8];
    state[8] ^= (~state[9]) & state[5];
    state[5] ^= (~state[6]) & state[7];
    state[7] ^= c0;

    c0 = (~state[13]) & state[14];
    state[14] ^= (~state[10]) & state[11];
    state[11] ^= (~state[12]) & state[13];
    state[13] ^= (~state[14]) & state[10];
    state[10] ^= (~state[11]) & state[12];
    state[12] ^= c0;

    c0 = (~state[18]) & state[19];
    state[19] ^= (~state[15]) & state[16];
    state[16] ^= (~state[17]) & state[18];
    state[18] ^= (~state[19]) & state[15];
    state[15] ^= (~state[16]) & state[17];
    state[17] ^= c0;

    c0 = (~state[23]) & state[24];
    state[24] ^= (~state[20]) & state[21];
    state[21] ^= (~state[22]) & state[23];
    state[23] ^= (~state[24]) & state[20];
    state[20] ^= (~state[21]) & state[22];
    state[22] ^= c0;

    state[0] ^= round_constants[round];
  }
}

static inline uint8_t aes_xtime(uint8_t byte) {
  return ((byte << 1) & 0xff) ^ ((byte & 0x80) ? 0x1b : 0x00);
}

static inline uint8_t aes_sbox(uint8_t byte) {
  static const uint8_t table[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
  };
  return table[byte];
}

static inline uint8_t aes_inv_sbox(uint8_t byte) {
  static const uint8_t table[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d,
  };
  return table[byte];
}

static inline uint32_t aes32esi(bool mix_columns, uint32_t byte_select, uint32_t word) {
  auto victim = (word >> (byte_select << 3)) & 0xff;
  auto value = aes_sbox(victim);
  if (!mix_columns)
    return uint32_t(value) << (byte_select << 3);

  auto xval = aes_xtime(value);
  auto x21val = xval ^ value;
  switch (byte_select) {
  case 0: return (uint32_t(x21val) << 24) | (uint32_t(value) << 16) | (uint32_t(value) << 8) | xval;
  case 1: return (uint32_t(value) << 24) | (uint32_t(value) << 16) | (uint32_t(xval) << 8) | x21val;
  case 2: return (uint32_t(value) << 24) | (uint32_t(xval) << 16) | (uint32_t(x21val) << 8) | value;
  case 3: return (uint32_t(xval) << 24) | (uint32_t(x21val) << 16) | (uint32_t(value) << 8) | value;
  default:
    std::abort();
  }
}

static inline uint32_t aes32dsi(bool inv_mix_columns, uint32_t byte_select, uint32_t word) {
  auto victim = (word >> (byte_select << 3)) & 0xff;
  auto value = aes_inv_sbox(victim);
  if (!inv_mix_columns)
    return uint32_t(value) << (byte_select << 3);

  auto xval = aes_xtime(value);
  auto x2val = aes_xtime(xval);
  auto x3val = aes_xtime(x2val);
  auto x321val = xval ^ x2val ^ x3val;
  auto x310val = value ^ xval ^ x3val;
  auto x320val = value ^ x2val ^ x3val;
  auto x30val = value ^ x3val;
  switch (byte_select) {
  case 0: return (uint32_t(x310val) << 24) | (uint32_t(x320val) << 16) | (uint32_t(x30val) << 8) | x321val;
  case 1: return (uint32_t(x320val) << 24) | (uint32_t(x30val) << 16) | (uint32_t(x321val) << 8) | x310val;
  case 2: return (uint32_t(x30val) << 24) | (uint32_t(x321val) << 16) | (uint32_t(x310val) << 8) | x320val;
  case 3: return (uint32_t(x321val) << 24) | (uint32_t(x310val) << 16) | (uint32_t(x320val) << 8) | x30val;
  default:
    std::abort();
  }
}

static inline uint32_t aes_subbytes_fwd(uint32_t word) {
  uint32_t result = 0;
  for (uint32_t i = 0; i < 4; ++i) {
    auto byte = (word >> (i * 8)) & 0xff;
    result |= uint32_t(aes_sbox(byte)) << (i * 8);
  }
  return result;
}

static inline uint32_t aes_subbytes_inv(uint32_t word) {
  uint32_t result = 0;
  for (uint32_t i = 0; i < 4; ++i) {
    auto byte = (word >> (i * 8)) & 0xff;
    result |= uint32_t(aes_inv_sbox(byte)) << (i * 8);
  }
  return result;
}

static inline uint8_t aes_col_byte(uint32_t word, uint32_t idx) {
  return (word >> (idx * 8)) & 0xff;
}

static inline uint32_t aes_pack_bytes(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
  return uint32_t(b0)
       | (uint32_t(b1) << 8)
       | (uint32_t(b2) << 16)
       | (uint32_t(b3) << 24);
}

static inline uint32_t aes_mixcolumn_fwd(uint32_t word) {
  uint8_t b0 = aes_col_byte(word, 0);
  uint8_t b1 = aes_col_byte(word, 1);
  uint8_t b2 = aes_col_byte(word, 2);
  uint8_t b3 = aes_col_byte(word, 3);
  return aes_pack_bytes(
    aes_xtime(b0) ^ b1 ^ aes_xtime(b1) ^ b2 ^ b3,
    b0 ^ aes_xtime(b1) ^ b2 ^ aes_xtime(b2) ^ b3,
    b0 ^ b1 ^ aes_xtime(b2) ^ b3 ^ aes_xtime(b3),
    b0 ^ aes_xtime(b0) ^ b1 ^ b2 ^ aes_xtime(b3)
  );
}

static inline uint32_t aes_mixcolumn_inv(uint32_t word) {
  uint8_t b0 = aes_col_byte(word, 0);
  uint8_t b1 = aes_col_byte(word, 1);
  uint8_t b2 = aes_col_byte(word, 2);
  uint8_t b3 = aes_col_byte(word, 3);

  uint8_t x0 = aes_xtime(b0), x1 = aes_xtime(b1), x2 = aes_xtime(b2), x3 = aes_xtime(b3);
  uint8_t x20 = aes_xtime(x0), x21 = aes_xtime(x1), x22 = aes_xtime(x2), x23 = aes_xtime(x3);
  uint8_t x30 = aes_xtime(x20), x31 = aes_xtime(x21), x32 = aes_xtime(x22), x33 = aes_xtime(x23);

  return aes_pack_bytes(
    x0 ^ x20 ^ x30 ^ b1 ^ x1 ^ x31 ^ b2 ^ x22 ^ x32 ^ b3 ^ x33,
    b0 ^ x30 ^ x1 ^ x21 ^ x31 ^ b2 ^ x2 ^ x32 ^ b3 ^ x23 ^ x33,
    b0 ^ x20 ^ x30 ^ b1 ^ x31 ^ x2 ^ x22 ^ x32 ^ b3 ^ x3 ^ x33,
    b0 ^ x0 ^ x30 ^ b1 ^ x21 ^ x31 ^ b2 ^ x32 ^ x3 ^ x23 ^ x33
  );
}

static inline uint64_t aes_pack_cols(uint32_t c0, uint32_t c1) {
  return uint64_t(c0) | (uint64_t(c1) << 32);
}

static inline uint64_t aes64_shiftrows_fwd(uint64_t rs1, uint64_t rs2) {
  uint32_t a0 = rs1 & 0xffffffffu;
  uint32_t a1 = (rs1 >> 32) & 0xffffffffu;
  uint32_t b0 = rs2 & 0xffffffffu;
  uint32_t b1 = (rs2 >> 32) & 0xffffffffu;
  uint32_t o0 = aes_pack_bytes(
    aes_col_byte(a0, 0), aes_col_byte(a1, 1), aes_col_byte(b0, 2), aes_col_byte(b1, 3));
  uint32_t o1 = aes_pack_bytes(
    aes_col_byte(a1, 0), aes_col_byte(b0, 1), aes_col_byte(b1, 2), aes_col_byte(a0, 3));
  return aes_pack_cols(o0, o1);
}

static inline uint64_t aes64_shiftrows_inv(uint64_t rs1, uint64_t rs2) {
  uint32_t a0 = rs1 & 0xffffffffu;
  uint32_t a1 = (rs1 >> 32) & 0xffffffffu;
  uint32_t b0 = rs2 & 0xffffffffu;
  uint32_t b1 = (rs2 >> 32) & 0xffffffffu;
  uint32_t o0 = aes_pack_bytes(
    aes_col_byte(a0, 0), aes_col_byte(b1, 1), aes_col_byte(b0, 2), aes_col_byte(a1, 3));
  uint32_t o1 = aes_pack_bytes(
    aes_col_byte(a1, 0), aes_col_byte(a0, 1), aes_col_byte(b1, 2), aes_col_byte(b0, 3));
  return aes_pack_cols(o0, o1);
}

static inline uint64_t aes64_apply_sbox_fwd(uint64_t value) {
  uint32_t lo = aes_subbytes_fwd(value & 0xffffffffu);
  uint32_t hi = aes_subbytes_fwd((value >> 32) & 0xffffffffu);
  return aes_pack_cols(lo, hi);
}

static inline uint64_t aes64_apply_sbox_inv(uint64_t value) {
  uint32_t lo = aes_subbytes_inv(value & 0xffffffffu);
  uint32_t hi = aes_subbytes_inv((value >> 32) & 0xffffffffu);
  return aes_pack_cols(lo, hi);
}

static inline uint32_t aes_rcon(uint32_t round) {
  static const uint32_t table[] = {
    0x00000000, 0x00000001, 0x00000002, 0x00000004,
    0x00000008, 0x00000010, 0x00000020, 0x00000040,
    0x00000080, 0x0000001b, 0x00000036
  };
  return table[round];
}

void Emulator::fetch_registers(std::vector<reg_data_t>& out, uint32_t wid, uint32_t src_index, const RegOpd& reg) {
  __unused(src_index);
  auto& warp = warps_.at(wid);
  uint32_t num_threads = warp.tmask.size();
  out.resize(num_threads);
  switch (reg.type) {
  case RegType::None:
#ifdef EXT_V_ENABLE
  case RegType::Vector:
    DPH(2, "Src" << src_index << " Reg: " << reg << "={");
    for (uint32_t t = 0; t < num_threads; ++t) {
      if (t) DPN(2, ", ");
      if (!warp.tmask.test(t)) {
        DPN(2, "-");
        continue;
      }
      DPN(2, vec_unit_->dumpRegister(wid, t, reg.idx));
    }
    DPN(2, "}" << std::endl);
#endif
    break;
  case RegType::Integer: {
    DPH(2, "Src" << src_index << " Reg: " << reg << "={");
    auto& reg_data = warp.ireg_file.at(reg.idx);
    for (uint32_t t = 0; t < num_threads; ++t) {
      if (t) DPN(2, ", ");
      if (!warp.tmask.test(t)) {
        DPN(2, "-");
        continue;
      }
      auto& value = out[t];
      value.u = reg_data.at(t);
      DPN(2, "0x" << std::hex << value.u << std::dec);
    }
    DPN(2, "}" << std::endl);
  } break;
  case RegType::Float: {
    DPH(2, "Src" << src_index << " Reg: " << reg << "={");
    auto& reg_data = warp.freg_file.at(reg.idx);
    for (uint32_t t = 0; t < num_threads; ++t) {
      if (t) DPN(2, ", ");
      if (!warp.tmask.test(t)) {
        DPN(2, "-");
        continue;
      }
      auto& value = out[t];
      value.u64 = reg_data.at(t);
      if ((value.u64 >> 32) == 0xffffffff) {
        DPN(2, "0x" << std::hex << value.u32 << std::dec);
      } else {
        DPN(2, "0x" << std::hex << value.u64 << std::dec);
      }
    }
    DPN(2, "}" << std::endl);
  } break;
  default:
    std::abort();
    break;
  }
}

// GF(2^128) carry-less multiply with GHASH reduction, operating on 128-bit
// big-endian integers (NIST polynomial bit i = integer bit [127-i]). Bit-for-bit
// identical to gf128_mul() in ghash_smoke/ghash_ref.h and to VX_crypto_ghash.sv.
static inline unsigned __int128 ghash_gfmul_be(unsigned __int128 x, unsigned __int128 v) {
  unsigned __int128 z = 0;
  const unsigned __int128 R = ((unsigned __int128)0xE1) << 120;
  for (int i = 0; i < 128; ++i) {
    if ((x >> (127 - i)) & 1)
      z ^= v;
    bool lsb = (v & 1) != 0;
    v >>= 1;
    if (lsb)
      v ^= R;
  }
  return z;
}

instr_trace_t* Emulator::execute(const Instr &instr, uint32_t wid) {
  auto& warp = warps_.at(wid);
  assert(warp.tmask.any());

  auto next_pc = warp.PC + 4;
  auto next_tmask = warp.tmask;

  auto fu_type = instr.getFUType();
  auto op_type = instr.getOpType();
  auto instrArgs = instr.getArgs();
  auto rdest  = instr.getDestReg();
  auto rsrc0  = instr.getSrcReg(0);
  auto rsrc1  = instr.getSrcReg(1);
  auto rsrc2  = instr.getSrcReg(2);

  auto num_threads = arch_.num_threads();

  // create instruction trace
  auto trace_alloc = core_->trace_pool().allocate(1);
  auto trace = new (trace_alloc) instr_trace_t(instr.getUUID(), arch_);
  trace->fu_type  = fu_type;
  trace->op_type  = op_type;
  trace->cid      = core_->id();
  trace->wid      = wid;
  trace->PC       = warp.PC;
  trace->tmask    = warp.tmask;
  trace->dst_reg  = rdest;
  trace->src_regs = {rsrc0, rsrc1, rsrc2};

  std::vector<reg_data_t> rd_data(num_threads);
  std::vector<reg_data_t> rs1_data;
  std::vector<reg_data_t> rs2_data;
  std::vector<reg_data_t> rs3_data;

  DP(1, "Instr: " << instr << ", cid=" << core_->id() << ", wid=" << wid << ", tmask=" << warp.tmask
         << ", PC=0x" << std::hex << warp.PC << std::dec << " (#" << instr.getUUID() << ")");

  // fetch register values
  if (rsrc0.type != RegType::None) fetch_registers(rs1_data, wid, 0, rsrc0);
  if (rsrc1.type != RegType::None) fetch_registers(rs2_data, wid, 1, rsrc1);
  if (rsrc2.type != RegType::None) fetch_registers(rs3_data, wid, 2, rsrc2);

  uint32_t thread_start = 0;
  for (; thread_start < num_threads; ++thread_start) {
    if (warp.tmask.test(thread_start))
      break;
  }

  int32_t thread_last = num_threads - 1;
  for (; thread_last >= 0; --thread_last) {
    if (warp.tmask.test(thread_last))
      break;
  }

  bool is_w_enabled = false;
#ifdef XLEN_64
  is_w_enabled = true;
#endif // XLEN_64

  bool rd_write = false;

  visit_var(op_type,
    [&](AluType alu_type) {
      auto aluArgs = std::get<IntrAluArgs>(instrArgs);
      Word imm = sext<Word>(aluArgs.imm, 32);
      switch (alu_type) {
      case AluType::LUI: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          rd_data[t].i = imm;
        }
      } break;
      case AluType::AUIPC: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          rd_data[t].i = imm + warp.PC;
        }
      } break;
      case AluType::ADD: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          if (is_w_enabled && aluArgs.is_w) {
            auto result = rs1_data[t].i32 + (int32_t)(aluArgs.is_imm ? aluArgs.imm : rs2_data[t].i32);
            rd_data[t].i = sext((uint64_t)result, 32);
          } else {
            rd_data[t].i = rs1_data[t].i + (aluArgs.is_imm ? imm : rs2_data[t].i);
          }
        }
      } break;
      case AluType::SUB: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          if (is_w_enabled && aluArgs.is_w) {
            auto result = rs1_data[t].i32 - (int32_t)(aluArgs.is_imm ? aluArgs.imm : rs2_data[t].i32);
            rd_data[t].i = sext((uint64_t)result, 32);
          } else {
            rd_data[t].i = rs1_data[t].i - (aluArgs.is_imm ? imm : rs2_data[t].i);
          }
        }
      } break;
      case AluType::SLT: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          rd_data[t].i = rs1_data[t].i < (aluArgs.is_imm ? WordI(imm) : rs2_data[t].i);
        }
      } break;
      case AluType::SLTU: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          rd_data[t].i = rs1_data[t].u < (aluArgs.is_imm ? imm : rs2_data[t].u);
        }
      } break;
      case AluType::SLL: {
        Word shamt_mask = (Word(1) << log2up(XLEN)) - 1;
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          if (is_w_enabled && aluArgs.is_w) {
            uint32_t shamt = (aluArgs.is_imm ? aluArgs.imm : rs2_data[t].i32) & shamt_mask;
            uint32_t result = (uint32_t)rs1_data[t].i << shamt;
            rd_data[t].i = sext((uint64_t)result, 32);
          } else {
            Word shamt = (aluArgs.is_imm ? imm : rs2_data[t].i) & shamt_mask;
            rd_data[t].i = rs1_data[t].i << shamt;
          }
        }
      } break;
      case AluType::SRA: {
        Word shamt_mask = (Word(1) << log2up(XLEN)) - 1;
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          if (is_w_enabled && aluArgs.is_w) {
            uint32_t shamt = (aluArgs.is_imm ? aluArgs.imm : rs2_data[t].i32) & shamt_mask;
            uint32_t result = (int32_t)rs1_data[t].i >> shamt;
            rd_data[t].i = sext((uint64_t)result, 32);
          } else {
            Word shamt = (aluArgs.is_imm ? imm : rs2_data[t].i) & shamt_mask;
            rd_data[t].i = rs1_data[t].i >> shamt;
          }
        }
      } break;
      case AluType::SRL: {
        Word shamt_mask = (Word(1) << log2up(XLEN)) - 1;
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          if (is_w_enabled && aluArgs.is_w) {
            uint32_t shamt = (aluArgs.is_imm ? aluArgs.imm : rs2_data[t].i32) & shamt_mask;
            uint32_t result = (uint32_t)rs1_data[t].i >> shamt;
            rd_data[t].i = sext((uint64_t)result, 32);
          } else {
            Word shamt = (aluArgs.is_imm ? imm : rs2_data[t].i) & shamt_mask;
            rd_data[t].i = rs1_data[t].u >> shamt;
          }
        }
      } break;
      case AluType::AND: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          rd_data[t].i = rs1_data[t].i & (aluArgs.is_imm ? imm : rs2_data[t].i);
        }
      } break;
      case AluType::OR: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          rd_data[t].i = rs1_data[t].i | (aluArgs.is_imm ? imm : rs2_data[t].i);
        }
      } break;
      case AluType::XOR: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          rd_data[t].i = rs1_data[t].i ^ (aluArgs.is_imm ? imm : rs2_data[t].i);
        }
      } break;
      case AluType::CZERO: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          bool cond = (rs2_data[t].i == 0) ^ aluArgs.imm;
          rd_data[t].i = cond ? 0 : rs1_data[t].i;
        }
      } break;
      default:
        std::abort();
      }
      rd_write = true;
    },
    [&](VoteType vote_type) {
      bool has_vote_true = false;
      bool has_vote_false = false;
      Word ballot = 0;
      // compute votes
      for (uint32_t t = thread_start; t < num_threads; ++t) {
        if (!warp.tmask.test(t))
          continue;
        auto is_pred = rs1_data[t].i & 0x1;
        if (is_pred) {
          has_vote_true = true;
          ballot |= (Word(1) << t);
        } else {
          has_vote_false = true;
        }
      }
      for (uint32_t t = thread_start; t < num_threads; ++t) {
        switch (vote_type) {
        case VoteType::ALL:
          rd_data[t].i = !has_vote_false;
          break;
        case VoteType::ANY:
          rd_data[t].i = has_vote_true;
          break;
        case VoteType::UNI:
          rd_data[t].i = !has_vote_true || !has_vote_false;
          break;
        case VoteType::BAL:
          rd_data[t].i = ballot;
          break;
        default:
          std::abort();
        }
      }
      rd_write = true;
    },
    [&](ShflType shfl_type) {
      for (uint32_t t = thread_start; t < num_threads; ++t) {
        if (!warp.tmask.test(t))
          continue;
        auto bc  = rs2_data[t].i;
        int bval = (bc >>  0) & 0x3f;
        int cval = (bc >>  6) & 0x3f;
        int mask = (bc >> 12) & 0x3f;
        int maxLane = (t & mask) | (cval & ~mask);
        int minLane = (t & mask);
        int lane = 0;
        int pval = 0;
        switch (shfl_type) {
        case ShflType::UP: {
          lane = t - bval;
          pval = (lane >= minLane);
        } break;
        case ShflType::DOWN: {
          lane = t + bval;
          pval = (lane <= maxLane);
        } break;
        case ShflType::BFLY: {
          lane = t ^ bval;
          pval = (lane <= maxLane);
        } break;
        case ShflType::IDX: {
          lane = minLane | (bval & ~mask);
          pval = (lane <= maxLane);
        } break;
        default:
          std::abort();
        }
        if (!pval)
          lane = t;
        if (lane < num_threads) {
          rd_data[t].i = rs1_data[lane].i;
        } else {
          rd_data[t].i = rs1_data[t].i;
        }
      }
      rd_write = true;
    },
    [&](BrType br_type) {
      auto brArgs = std::get<IntrBrArgs>(instrArgs);
      Word offset = sext<Word>(brArgs.offset, 32);
      switch (br_type) {
      case BrType::BR: {
        bool all_taken = false;
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          bool curr_taken = false;
          switch (brArgs.cmp) {
          case 0: { // RV32I: BEQ
            if (rs1_data[t].i == rs2_data[t].i) {
              next_pc = warp.PC + offset;
              curr_taken = true;
            }
            break;
          }
          case 1: { // RV32I: BNE
            if (rs1_data[t].i != rs2_data[t].i) {
              next_pc = warp.PC + offset;
              curr_taken = true;
            }
            break;
          }
          case 4: { // RV32I: BLT
            if (rs1_data[t].i < rs2_data[t].i) {
              next_pc = warp.PC + offset;
              curr_taken = true;
            }
            break;
          }
          case 5: { // RV32I: BGE
            if (rs1_data[t].i >= rs2_data[t].i) {
              next_pc = warp.PC + offset;
              curr_taken = true;
            }
            break;
          }
          case 6: { // RV32I: BLTU
            if (rs1_data[t].u < rs2_data[t].u) {
              next_pc = warp.PC + offset;
              curr_taken = true;
            }
            break;
          }
          case 7: { // RV32I: BGEU
            if (rs1_data[t].u >= rs2_data[t].u) {
              next_pc = warp.PC + offset;
              curr_taken = true;
            }
            break;
          }
          default:
            std::abort();
          }
          if (t == thread_start) {
            all_taken = curr_taken;
          } else {
            if (all_taken != curr_taken) {
              std::cout << "divergent branch! PC=0x" << std::hex << warp.PC << std::dec << " (#" << trace->uuid << ")\n" << std::flush;
              std::abort();
            }
          }
        }
        trace->fetch_stall = true;
      } break;
      case BrType::JAL: { // RV32I: JAL
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          rd_data[t].i = next_pc;
        }
        next_pc = warp.PC + offset;
        trace->fetch_stall = true;
        rd_write = true;
      } break;
      case BrType::JALR: { // RV32I: JALR
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          rd_data[t].i = next_pc;
        }
        next_pc = rs1_data[thread_last].i + offset;
        trace->fetch_stall = true;
        rd_write = true;
      } break;
      case BrType::SYS:
        switch (brArgs.offset) {
        case 0x000: // RV32I: ECALL
          this->trigger_ecall();
          break;
        case 0x001: // RV32I: EBREAK
          this->trigger_ebreak();
          break;
        case 0x002: // RV32I: URET
        case 0x102: // RV32I: SRET
        case 0x302: // RV32I: MRET
          break;
        default:
          std::abort();
        }
        break;
      default:
        std::abort();
      }
    },
    [&](MdvType mdv_type) {
      auto mdvArgs = std::get<IntrMdvArgs>(instrArgs);
      switch (mdv_type) {
      case MdvType::MUL: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          if (is_w_enabled && mdvArgs.is_w) {
            auto product = rs1_data[t].i32 * rs2_data[t].i32;
            rd_data[t].i = sext((uint64_t)product, 32);
          } else {
            rd_data[t].i = rs1_data[t].i * rs2_data[t].i;
          }
        }
      } break;
      case MdvType::MULH: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          auto first = static_cast<DWordI>(rs1_data[t].i);
          auto second = static_cast<DWordI>(rs2_data[t].i);
          rd_data[t].i = (first * second) >> XLEN;
        }
      } break;
      case MdvType::MULHSU: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          auto first = static_cast<DWordI>(rs1_data[t].i);
          auto second = static_cast<DWord>(rs2_data[t].u);
          rd_data[t].i = (first * second) >> XLEN;
        }
      } break;
      case MdvType::MULHU: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          auto first = static_cast<DWord>(rs1_data[t].u);
          auto second = static_cast<DWord>(rs2_data[t].u);
          rd_data[t].i = (first * second) >> XLEN;
        }
      } break;
      case MdvType::DIV: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          if (is_w_enabled && mdvArgs.is_w) {
            auto dividen = rs1_data[t].i32;
            auto divisor = rs2_data[t].i32;
            int32_t largest_negative = 0x80000000;
            int32_t quotient;
            if (divisor == 0){
              quotient = -1;
            } else if (dividen == largest_negative && divisor == -1) {
              quotient = dividen;
            } else {
              quotient = dividen / divisor;
            }
            rd_data[t].i = sext((uint64_t)quotient, 32);
          } else {
            auto dividen = rs1_data[t].i;
            auto divisor = rs2_data[t].i;
            auto largest_negative = WordI(1) << (XLEN-1);
            WordI quotient;
            if (divisor == 0) {
              quotient = -1;
            } else if (dividen == largest_negative && divisor == -1) {
              quotient = dividen;
            } else {
              quotient = dividen / divisor;
            }
            rd_data[t].i = quotient;
          }
        }
      } break;
      case MdvType::DIVU: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          if (is_w_enabled && mdvArgs.is_w) {
            auto dividen = rs1_data[t].u32;
            auto divisor = rs2_data[t].u32;
            uint32_t quotient;
            if (divisor != 0){
              quotient = dividen / divisor;
            } else {
              quotient = -1;
            }
            rd_data[t].i = sext((uint64_t)quotient, 32);
          } else {
            auto dividen = rs1_data[t].u;
            auto divisor = rs2_data[t].u;
            Word quotient;
            if (divisor != 0) {
              quotient = dividen / divisor;
            } else {
              quotient = -1;
            }
            rd_data[t].i = quotient;
          }
        }
      } break;
      case MdvType::REM: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          if (is_w_enabled && mdvArgs.is_w) {
            auto dividen = rs1_data[t].i32;
            auto divisor = rs2_data[t].i32;
            int32_t largest_negative = 0x80000000;
            int32_t remainder;
            if (divisor == 0){
              remainder = dividen;
            } else if (dividen == largest_negative && divisor == -1) {
              remainder = 0;
            } else {
              remainder = dividen % divisor;
            }
            rd_data[t].i = sext((uint64_t)remainder, 32);
          } else {
            auto dividen = rs1_data[t].i;
            auto divisor = rs2_data[t].i;
            auto largest_negative = WordI(1) << (XLEN-1);
            WordI remainder;
            if (rs2_data[t].i == 0) {
              remainder = dividen;
            } else if (dividen == largest_negative && divisor == -1) {
              remainder = 0;
            } else {
              remainder = dividen % divisor;
            }
            rd_data[t].i = remainder;
          }
        }
      } break;
      case MdvType::REMU: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          if (is_w_enabled && mdvArgs.is_w) {
            auto dividen = (uint32_t)rs1_data[t].u32;
            auto divisor = (uint32_t)rs2_data[t].u32;
            uint32_t remainder;
            if (divisor != 0){
              remainder = dividen % divisor;
            } else {
              remainder = dividen;
            }
            rd_data[t].i = sext((uint64_t)remainder, 32);
          } else {
            auto dividen = rs1_data[t].u;
            auto divisor = rs2_data[t].u;
            Word remainder;
            if (rs2_data[t].i != 0) {
              remainder = dividen % divisor;
            } else {
              remainder = dividen;
            }
            rd_data[t].i = remainder;
          }
        }
      } break;
      default:
        std::abort();
      }
      rd_write = true;
    },
    [&](ShaType sha_type) {
      for (uint32_t t = thread_start; t < num_threads; ++t) {
        if (!warp.tmask.test(t))
          continue;
        uint32_t result;
        switch (sha_type) {
        case ShaType::SHA256SIG0:
          result = sha256sig0(rs1_data[t].u32);
          break;
        case ShaType::SHA256SIG1:
          result = sha256sig1(rs1_data[t].u32);
          break;
        case ShaType::SHA256SUM0:
          result = sha256sum0(rs1_data[t].u32);
          break;
        case ShaType::SHA256SUM1:
          result = sha256sum1(rs1_data[t].u32);
          break;
        default:
          std::abort();
        }
        rd_data[t].i = sext((uint64_t)result, 32);
      }
      rd_write = true;
    },
    [&](KeccakType keccak_type) {
      auto& keccak_state = keccak_state_.at(wid);
      switch (keccak_type) {
      case KeccakType::WR: {
        auto lane_idx = rs2_data[0].u32 & 0x1f;
        #if (XLEN == 32)
        if (rs2_data[0].u32 & 0x20) {
          keccak_state.at(lane_idx) =
              (keccak_state.at(lane_idx) & 0x00000000ffffffffull)
              | (uint64_t(rs1_data[0].u32) << 32);
        } else {
          keccak_state.at(lane_idx) =
              (keccak_state.at(lane_idx) & 0xffffffff00000000ull)
              | uint64_t(rs1_data[0].u32);
        }
        #else
        keccak_state.at(lane_idx) = rs1_data[0].u64;
        #endif
        rd_write = false;
      } break;
      case KeccakType::XOR: {
        auto lane_idx = rs2_data[0].u32 & 0x1f;
        #if (XLEN == 32)
        if (rs2_data[0].u32 & 0x20) {
          keccak_state.at(lane_idx) ^= uint64_t(rs1_data[0].u32) << 32;
        } else {
          keccak_state.at(lane_idx) ^= uint64_t(rs1_data[0].u32);
        }
        #else
        keccak_state.at(lane_idx) ^= rs1_data[0].u64;
        #endif
        rd_write = false;
      } break;
      case KeccakType::RD: {
        auto lane_idx = rs1_data[0].u32 & 0x1f;
        auto value = keccak_state.at(lane_idx);
        #if (XLEN == 32)
        if (rs1_data[0].u32 & 0x20) {
          value >>= 32;
        }
        #endif
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          #if (XLEN == 32)
          rd_data[t].u = Word(value);
          #else
          rd_data[t].u64 = value;
          #endif
        }
        rd_write = true;
      } break;
      case KeccakType::F1600:
        keccak_f1600(keccak_state);
        rd_write = false;
        break;
      default:
        std::abort();
      }
    },
    [&](GhashType ghash_type) {
      // Per-lane multi-chain: each thread owns an independent GHASH chain.
      // ghash[t] = {H, Y} (128-bit big-endian integers). A 128-bit value spans
      // (128/XLEN) XLEN-wide words; the word index selects the slice, mirroring
      // the +:XLEN part-select in VX_crypto_ghash.sv.
      auto& ghash_warp = ghash_state_.at(wid);
      const unsigned __int128 word_mask = (((unsigned __int128)1) << XLEN) - 1;
      for (uint32_t t = thread_start; t < num_threads; ++t) {
        if (!warp.tmask.test(t))
          continue;
        auto& H = ghash_warp.at(t)[0];
        auto& Y = ghash_warp.at(t)[1];
        switch (ghash_type) {
        case GhashType::SETH: {
          uint32_t word = rs2_data[t].u32 & ((128 / XLEN) - 1);
          uint32_t shift = word * XLEN;
          unsigned __int128 val = ((unsigned __int128)(rs1_data[t].u) & word_mask) << shift;
          H = (H & ~(word_mask << shift)) | val;
        } break;
        case GhashType::XOR: {
          uint32_t word = rs2_data[t].u32 & ((128 / XLEN) - 1);
          uint32_t shift = word * XLEN;
          Y ^= ((unsigned __int128)(rs1_data[t].u) & word_mask) << shift;
        } break;
        case GhashType::RD: {
          uint32_t word = rs1_data[t].u32 & ((128 / XLEN) - 1);
          uint32_t shift = word * XLEN;
          rd_data[t].u = Word((uint64_t)((Y >> shift) & word_mask));
        } break;
        case GhashType::MUL:
          // Y = Y * H  (scan Y bits, shift H), matching ghash_update_block.
          Y = ghash_gfmul_be(Y, H);
          break;
        default:
          std::abort();
        }
      }
      rd_write = (ghash_type == GhashType::RD);
    },
    [&](PolyType poly_type) {
      // Per-lane Poly1305 (radix-2^26, 5 limbs). poly[t] = {r[5], s[5], acc[5]}
      // (indices 0-4,5-9,10-14). Math is bit-for-bit identical to the donna-32
      // block loop in poly1305.h and to VX_crypto_poly1305.sv. SETR loads a
      // pre-clamped r; BLOCK does acc=(acc+block+2^128)*r mod 2^130-5 for a full
      // 16-byte block; RD reads back an accumulator limb.
      auto& poly_warp = poly1305_state_.at(wid);
      for (uint32_t t = thread_start; t < num_threads; ++t) {
        if (!warp.tmask.test(t))
          continue;
        uint64_t* r   = &poly_warp.at(t)[0];
        uint64_t* s   = &poly_warp.at(t)[5];
        uint64_t* acc = &poly_warp.at(t)[10];
        // The 128-bit SETR/BLOCK operand: RV64 packs it into {rs2,rs1}; RV32 has
        // only 64 bits of source register, so SETR stages it a word at a time into
        // buf and SETRB/BLOCK consume that instead. Mirrors VX_crypto_poly1305.sv.
        // NOTE: rs2_data is only populated for instructions that declare a second
        // source register, so it must not be touched outside those cases.
      #if (XLEN == 32)
        uint64_t* buf = &poly_warp.at(t)[15];
      #endif
        switch (poly_type) {
      #if (XLEN == 32)
        case PolyType::SETR: { // stage one 32-bit word: buf[rs2[1:0]] = rs1
          uint32_t w = rs2_data[t].u32 & 0x3;
          uint64_t d = (uint64_t)(uint32_t)rs1_data[t].u;
          uint64_t& half = buf[w >> 1];
          uint32_t sh = 32 * (w & 1);
          half = (half & ~((uint64_t)0xffffffffull << sh)) | (d << sh);
        } break;
        case PolyType::SETRB: {
          unsigned __int128 opnd = ((unsigned __int128)buf[1] << 64) | buf[0];
      #else
        case PolyType::SETR: {
          unsigned __int128 opnd = ((unsigned __int128)(uint64_t)rs2_data[t].u << 64)
                                 | (uint64_t)rs1_data[t].u;
      #endif
          for (int k = 0; k < 5; ++k) {
            r[k] = (uint64_t)((opnd >> (26 * k)) & 0x3ffffff);
            s[k] = r[k] * 5;
            acc[k] = 0;
          }
        } break;
        case PolyType::BLOCK: {
      #if (XLEN == 32)
          unsigned __int128 b = ((unsigned __int128)buf[1] << 64) | buf[0];
      #else
          unsigned __int128 b = ((unsigned __int128)(uint64_t)rs2_data[t].u << 64)
                              | (uint64_t)rs1_data[t].u;
      #endif
          uint64_t h0 = acc[0] + (uint64_t)((b >> 0)   & 0x3ffffff);
          uint64_t h1 = acc[1] + (uint64_t)((b >> 26)  & 0x3ffffff);
          uint64_t h2 = acc[2] + (uint64_t)((b >> 52)  & 0x3ffffff);
          uint64_t h3 = acc[3] + (uint64_t)((b >> 78)  & 0x3ffffff);
          uint64_t h4 = acc[4] + (uint64_t)((b >> 104) & 0x3ffffff) + (1u << 24); // +2^128
          uint64_t d0 = h0*r[0] + h1*s[4] + h2*s[3] + h3*s[2] + h4*s[1];
          uint64_t d1 = h0*r[1] + h1*r[0] + h2*s[4] + h3*s[3] + h4*s[2];
          uint64_t d2 = h0*r[2] + h1*r[1] + h2*r[0] + h3*s[4] + h4*s[3];
          uint64_t d3 = h0*r[3] + h1*r[2] + h2*r[1] + h3*r[0] + h4*s[4];
          uint64_t d4 = h0*r[4] + h1*r[3] + h2*r[2] + h3*r[1] + h4*r[0];
          uint64_t c;
          c = d0 >> 26; h0 = d0 & 0x3ffffff; d1 += c;
          c = d1 >> 26; h1 = d1 & 0x3ffffff; d2 += c;
          c = d2 >> 26; h2 = d2 & 0x3ffffff; d3 += c;
          c = d3 >> 26; h3 = d3 & 0x3ffffff; d4 += c;
          c = d4 >> 26; h4 = d4 & 0x3ffffff; h0 += c * 5;
          c = h0 >> 26; h0 = h0 & 0x3ffffff; h1 += c;
          acc[0] = h0; acc[1] = h1; acc[2] = h2; acc[3] = h3; acc[4] = h4;
        } break;
        case PolyType::RD: {
          uint32_t idx = rs1_data[t].u32 & 0x7;
          rd_data[t].u = Word((uint64_t)acc[idx]);
        } break;
        default:
          std::abort();
        }
      }
      rd_write = (poly_type == PolyType::RD);
    },
    [&](ChaChaType chacha_type) {
      // Per-lane ChaCha20: chacha[t] = 16 x 32-bit words. WR loads a word, BLOCK
      // runs the 10 double-round permutation + feedforward add, RD reads a word.
      // Math is bit-for-bit identical to chacha20.h and VX_crypto_chacha.sv.
      auto& chacha_warp = chacha_state_.at(wid);
      for (uint32_t t = thread_start; t < num_threads; ++t) {
        if (!warp.tmask.test(t))
          continue;
        uint32_t* s = chacha_warp.at(t).data();
        switch (chacha_type) {
        case ChaChaType::WR: {
          s[rs2_data[t].u32 & 0xf] = (uint32_t)rs1_data[t].u;
        } break;
        case ChaChaType::BLOCK: {
          uint32_t x[16], st[16];
          for (int i = 0; i < 16; ++i) { x[i] = s[i]; st[i] = s[i]; }
          for (int r = 0; r < 10; ++r) {
            #define CC20_QR(a,b,c,d) \
              x[a]+=x[b]; x[d]^=x[a]; x[d]=(x[d]<<16)|(x[d]>>16); \
              x[c]+=x[d]; x[b]^=x[c]; x[b]=(x[b]<<12)|(x[b]>>20); \
              x[a]+=x[b]; x[d]^=x[a]; x[d]=(x[d]<< 8)|(x[d]>>24); \
              x[c]+=x[d]; x[b]^=x[c]; x[b]=(x[b]<< 7)|(x[b]>>25);
            CC20_QR(0,4,8,12)  CC20_QR(1,5,9,13)  CC20_QR(2,6,10,14) CC20_QR(3,7,11,15)
            CC20_QR(0,5,10,15) CC20_QR(1,6,11,12) CC20_QR(2,7,8,13)  CC20_QR(3,4,9,14)
            #undef CC20_QR
          }
          for (int i = 0; i < 16; ++i) s[i] = x[i] + st[i];
        } break;
        case ChaChaType::RD: {
          rd_data[t].u = Word((uint64_t)s[rs1_data[t].u32 & 0xf]);
        } break;
        default:
          std::abort();
        }
      }
      rd_write = (chacha_type == ChaChaType::RD);
    },
    [&](AesType aes_type) {
      auto aesArgs = std::get<IntrAesArgs>(instrArgs);
      for (uint32_t t = thread_start; t < num_threads; ++t) {
        if (!warp.tmask.test(t))
          continue;
        switch (aes_type) {
        case AesType::ESI:
          rd_data[t].u = rs1_data[t].u ^ aes32esi(false, aesArgs.imm, rs2_data[t].u32);
          break;
        case AesType::ESMI:
          rd_data[t].u = rs1_data[t].u ^ aes32esi(true, aesArgs.imm, rs2_data[t].u32);
          break;
        case AesType::DSI:
          rd_data[t].u = rs1_data[t].u ^ aes32dsi(false, aesArgs.imm, rs2_data[t].u32);
          break;
        case AesType::DSMI:
          rd_data[t].u = rs1_data[t].u ^ aes32dsi(true, aesArgs.imm, rs2_data[t].u32);
          break;
        case AesType::ES64: {
          auto shifted = aes64_shiftrows_fwd(rs1_data[t].u64, rs2_data[t].u64);
          rd_data[t].u64 = aes64_apply_sbox_fwd(shifted);
        } break;
        case AesType::ESM64: {
          auto shifted = aes64_shiftrows_fwd(rs1_data[t].u64, rs2_data[t].u64);
          auto subbed = aes64_apply_sbox_fwd(shifted);
          uint32_t lo = aes_mixcolumn_fwd(subbed & 0xffffffffu);
          uint32_t hi = aes_mixcolumn_fwd((subbed >> 32) & 0xffffffffu);
          rd_data[t].u64 = aes_pack_cols(lo, hi);
        } break;
        case AesType::DS64: {
          auto shifted = aes64_shiftrows_inv(rs1_data[t].u64, rs2_data[t].u64);
          rd_data[t].u64 = aes64_apply_sbox_inv(shifted);
        } break;
        case AesType::DSM64: {
          auto shifted = aes64_shiftrows_inv(rs1_data[t].u64, rs2_data[t].u64);
          auto subbed = aes64_apply_sbox_inv(shifted);
          uint32_t lo = aes_mixcolumn_inv(subbed & 0xffffffffu);
          uint32_t hi = aes_mixcolumn_inv((subbed >> 32) & 0xffffffffu);
          rd_data[t].u64 = aes_pack_cols(lo, hi);
        } break;
        case AesType::IM64: {
          uint32_t lo = aes_mixcolumn_inv(rs1_data[t].u64 & 0xffffffffu);
          uint32_t hi = aes_mixcolumn_inv((rs1_data[t].u64 >> 32) & 0xffffffffu);
          rd_data[t].u64 = aes_pack_cols(lo, hi);
        } break;
        case AesType::KS1I64: {
          uint32_t tmp1 = (rs1_data[t].u64 >> 32) & 0xffffffffu;
          uint32_t tmp2 = (aesArgs.imm == 0xA)
                        ? tmp1
                        : ((tmp1 >> 8) | (tmp1 << 24));
          uint32_t tmp3 = aes_subbytes_fwd(tmp2) ^ aes_rcon(aesArgs.imm);
          rd_data[t].u64 = aes_pack_cols(tmp3, tmp3);
        } break;
        case AesType::KS2_64: {
          uint32_t rs1_hi = (rs1_data[t].u64 >> 32) & 0xffffffffu;
          uint32_t rs2_lo = rs2_data[t].u64 & 0xffffffffu;
          uint32_t rs2_hi = (rs2_data[t].u64 >> 32) & 0xffffffffu;
          uint32_t w0 = rs1_hi ^ rs2_lo;
          uint32_t w1 = w0 ^ rs2_hi;
          rd_data[t].u64 = aes_pack_cols(w0, w1);
        } break;
        default:
          std::abort();
        }
      }
      rd_write = true;
    },
    [&](LsuType lsu_type) {
      auto lsuArgs = std::get<IntrLsuArgs>(instrArgs);
      switch (lsu_type) {
      case LsuType::LOAD: {
        auto trace_data = std::make_shared<LsuTraceData>(num_threads);
        trace->data = trace_data;
        uint32_t data_bytes = 1 << (lsuArgs.width & 0x3);
        uint32_t data_width = 8 * data_bytes;
        Word offset = sext<Word>(lsuArgs.offset, 32);
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint64_t mem_addr = rs1_data[t].i + offset;
          uint64_t read_data = 0;
          this->dcache_read(&read_data, mem_addr, data_bytes);
          trace_data->mem_addrs.at(t) = {mem_addr, data_bytes};
          switch (lsuArgs.width) {
          case 0: // RV32I: LB
          case 1: // RV32I: LH
            rd_data[t].i = sext((Word)read_data, data_width);
            break;
          case 2:
            if (lsuArgs.is_float) {
              // RV32F: FLW
              rd_data[t].u64 = nan_box((uint32_t)read_data);
            } else {
              // RV32I: LW
              rd_data[t].i = sext((Word)read_data, data_width);
            }
            break;
          case 3: // RV64I: LD
                  // RV32D: FLD
          case 4: // RV32I: LBU
          case 5: // RV32I: LHU
          case 6: // RV64I: LWU
            rd_data[t].u64 = read_data;
            break;
          default:
            std::abort();
          }
        }
        rd_write = true;
      } break;
      case LsuType::STORE: {
        auto trace_data = std::make_shared<LsuTraceData>(num_threads);
        trace->data = trace_data;
        uint32_t data_bytes = 1 << (lsuArgs.width & 0x3);
        Word offset = sext<Word>(lsuArgs.offset, 32);
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint64_t mem_addr = rs1_data[t].i + offset;
          uint64_t write_data = rs2_data[t].u64;
          trace_data->mem_addrs.at(t) = {mem_addr, data_bytes};
          switch (lsuArgs.width) {
          case 0:
          case 1:
          case 2:
          case 3:
            this->dcache_write(&write_data, mem_addr, data_bytes);
            break;
          default:
            std::abort();
          }
        }
      } break;
      case LsuType::FENCE: {
        // no compute
      } break;
      default:
        std::abort();
      }
    },
    [&](AmoType amo_type) {
      auto amoArgs = std::get<IntrAmoArgs>(instrArgs);
      auto trace_data = std::make_shared<LsuTraceData>(num_threads);
      trace->data = trace_data;
      uint32_t data_bytes = 1 << (amoArgs.width & 0x3);
      uint32_t data_width = 8 * data_bytes;
      switch (amo_type) {
      case AmoType::LR: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint64_t mem_addr = rs1_data[t].u;
          trace_data->mem_addrs.at(t) = {mem_addr, data_bytes};
          uint64_t read_data = 0;
          this->dcache_read(&read_data, mem_addr, data_bytes);
          this->dcache_amo_reserve(mem_addr);
          rd_data[t].i = sext((Word)read_data, data_width);
        }
      } break;
      case AmoType::SC: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint64_t mem_addr = rs1_data[t].u;
          trace_data->mem_addrs.at(t) = {mem_addr, data_bytes};
          if (this->dcache_amo_check(mem_addr)) {
            this->dcache_write(&rs2_data[t].u64, mem_addr, data_bytes);
            rd_data[t].i = 0;
          } else {
            rd_data[t].i = 1;
          }
        }
      } break;
      case AmoType::AMOADD: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint64_t mem_addr = rs1_data[t].u;
          trace_data->mem_addrs.at(t) = {mem_addr, data_bytes};
          uint64_t read_data = 0;
          this->dcache_read(&read_data, mem_addr, data_bytes);
          auto read_data_i = sext((WordI)read_data, data_width);
          auto rs1_data_i  = sext((WordI)rs2_data[t].u64, data_width);
          uint64_t result = read_data_i + rs1_data_i;
          this->dcache_write(&result, mem_addr, data_bytes);
          rd_data[t].i = read_data_i;
        }
      } break;
      case AmoType::AMOSWAP: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint64_t mem_addr = rs1_data[t].u;
          trace_data->mem_addrs.at(t) = {mem_addr, data_bytes};
          uint64_t read_data = 0;
          this->dcache_read(&read_data, mem_addr, data_bytes);
          auto read_data_i = sext((WordI)read_data, data_width);
          auto rs1_data_u  = zext((Word)rs2_data[t].u64, data_width);
          uint64_t result = rs1_data_u;
          this->dcache_write(&result, mem_addr, data_bytes);
          rd_data[t].i = read_data_i;
        }
      } break;
      case AmoType::AMOXOR: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint64_t mem_addr = rs1_data[t].u;
          trace_data->mem_addrs.at(t) = {mem_addr, data_bytes};
          uint64_t read_data = 0;
          this->dcache_read(&read_data, mem_addr, data_bytes);
          auto read_data_i = sext((WordI)read_data, data_width);
          auto read_data_u = zext((Word)read_data, data_width);
          auto rs1_data_u  = zext((Word)rs2_data[t].u64, data_width);
          uint64_t result = read_data_u ^ rs1_data_u;
          this->dcache_write(&result, mem_addr, data_bytes);
          rd_data[t].i = read_data_i;
        }
      } break;
      case AmoType::AMOOR: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint64_t mem_addr = rs1_data[t].u;
          trace_data->mem_addrs.at(t) = {mem_addr, data_bytes};
          uint64_t read_data = 0;
          this->dcache_read(&read_data, mem_addr, data_bytes);
          auto read_data_i = sext((WordI)read_data, data_width);
          auto read_data_u = zext((Word)read_data, data_width);
          auto rs1_data_u  = zext((Word)rs2_data[t].u64, data_width);
          uint64_t result = read_data_u | rs1_data_u;
          this->dcache_write(&result, mem_addr, data_bytes);
          rd_data[t].i = read_data_i;
        }
      } break;
      case AmoType::AMOAND: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint64_t mem_addr = rs1_data[t].u;
          trace_data->mem_addrs.at(t) = {mem_addr, data_bytes};
          uint64_t read_data = 0;
          this->dcache_read(&read_data, mem_addr, data_bytes);
          auto read_data_i = sext((WordI)read_data, data_width);
          auto read_data_u = zext((Word)read_data, data_width);
          auto rs1_data_u  = zext((Word)rs2_data[t].u64, data_width);
          uint64_t result = read_data_u & rs1_data_u;
          this->dcache_write(&result, mem_addr, data_bytes);
          rd_data[t].i = read_data_i;
        }
      } break;
      case AmoType::AMOMIN: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint64_t mem_addr = rs1_data[t].u;
          trace_data->mem_addrs.at(t) = {mem_addr, data_bytes};
          uint64_t read_data = 0;
          this->dcache_read(&read_data, mem_addr, data_bytes);
          auto read_data_i = sext((WordI)read_data, data_width);
          auto rs1_data_i  = sext((WordI)rs2_data[t].u64, data_width);
          uint64_t result = std::min(read_data_i, rs1_data_i);
          this->dcache_write(&result, mem_addr, data_bytes);
          rd_data[t].i = read_data_i;
        }
      } break;
      case AmoType::AMOMAX: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint64_t mem_addr = rs1_data[t].u;
          trace_data->mem_addrs.at(t) = {mem_addr, data_bytes};
          uint64_t read_data = 0;
          this->dcache_read(&read_data, mem_addr, data_bytes);
          auto read_data_i = sext((WordI)read_data, data_width);
          auto rs1_data_i  = sext((WordI)rs2_data[t].u64, data_width);
          uint64_t result = std::max(read_data_i, rs1_data_i);
          this->dcache_write(&result, mem_addr, data_bytes);
          rd_data[t].i = read_data_i;
        }
      } break;
      case AmoType::AMOMINU: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint64_t mem_addr = rs1_data[t].u;
          trace_data->mem_addrs.at(t) = {mem_addr, data_bytes};
          uint64_t read_data = 0;
          this->dcache_read(&read_data, mem_addr, data_bytes);
          auto read_data_i = sext((WordI)read_data, data_width);
          auto read_data_u = zext((Word)read_data, data_width);
          auto rs1_data_u  = zext((Word)rs2_data[t].u64, data_width);
          uint64_t result = std::min(read_data_u, rs1_data_u);
          this->dcache_write(&result, mem_addr, data_bytes);
          rd_data[t].i = read_data_i;
        }
      } break;
      case AmoType::AMOMAXU: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint64_t mem_addr = rs1_data[t].u;
          trace_data->mem_addrs.at(t) = {mem_addr, data_bytes};
          uint64_t read_data = 0;
          this->dcache_read(&read_data, mem_addr, data_bytes);
          auto read_data_i = sext((WordI)read_data, data_width);
          auto read_data_u = zext((Word)read_data, data_width);
          auto rs1_data_u  = zext((Word)rs2_data[t].u64, data_width);
          uint64_t result = std::max(read_data_u, rs1_data_u);
          this->dcache_write(&result, mem_addr, data_bytes);
          rd_data[t].i = read_data_i;
        }
      } break;
      default:
        std::abort();
      }
      rd_write = true;
    },
    [&](FpuType fpu_type) {
      auto fpuArgs = std::get<IntrFpuArgs>(instrArgs);
      switch (fpu_type) {
      case FpuType::FADD: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint32_t frm = this->get_fpu_rm(fpuArgs.frm, wid, t);
          uint32_t fflags = 0;
          if (fpuArgs.is_f64) {
            rd_data[t].u64 = rv_fadd_d(rs1_data[t].u64, rs2_data[t].u64, frm, &fflags);
          } else {
            rd_data[t].u64 = nan_box(rv_fadd_s(check_boxing(rs1_data[t].u64), check_boxing(rs2_data[t].u64), frm, &fflags));
          }
          this->update_fcrs(fflags, wid, t);
        }
      } break;
      case FpuType::FSUB: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint32_t frm = this->get_fpu_rm(fpuArgs.frm, wid, t);
          uint32_t fflags = 0;
          if (fpuArgs.is_f64) {
            rd_data[t].u64 = rv_fsub_d(rs1_data[t].u64, rs2_data[t].u64, frm, &fflags);
          } else {
            rd_data[t].u64 = nan_box(rv_fsub_s(check_boxing(rs1_data[t].u64), check_boxing(rs2_data[t].u64), frm, &fflags));
          }
          this->update_fcrs(fflags, wid, t);
        }
      } break;
      case FpuType::FMUL: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint32_t frm = this->get_fpu_rm(fpuArgs.frm, wid, t);
          uint32_t fflags = 0;
          if (fpuArgs.is_f64) {
            rd_data[t].u64 = rv_fmul_d(rs1_data[t].u64, rs2_data[t].u64, frm, &fflags);
          } else {
            rd_data[t].u64 = nan_box(rv_fmul_s(check_boxing(rs1_data[t].u64), check_boxing(rs2_data[t].u64), frm, &fflags));
          }
          this->update_fcrs(fflags, wid, t);
        }
      } break;
      case FpuType::FDIV: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint32_t frm = this->get_fpu_rm(fpuArgs.frm, wid, t);
          uint32_t fflags = 0;
          if (fpuArgs.is_f64) {
            rd_data[t].u64 = rv_fdiv_d(rs1_data[t].u64, rs2_data[t].u64, frm, &fflags);
          } else {
            rd_data[t].u64 = nan_box(rv_fdiv_s(check_boxing(rs1_data[t].u64), check_boxing(rs2_data[t].u64), frm, &fflags));
          }
          this->update_fcrs(fflags, wid, t);
        }
      } break;
      case FpuType::FSQRT: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint32_t frm = this->get_fpu_rm(fpuArgs.frm, wid, t);
          uint32_t fflags = 0;
          if (fpuArgs.is_f64) {
            rd_data[t].u64 = rv_fsqrt_d(rs1_data[t].u64, frm, &fflags);
          } else {
            rd_data[t].u64 = nan_box(rv_fsqrt_s(check_boxing(rs1_data[t].u64), frm, &fflags));
          }
          this->update_fcrs(fflags, wid, t);
        }
      } break;
      case FpuType::FSGNJ: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint32_t fflags = 0;
          if (fpuArgs.is_f64) {
            switch (fpuArgs.frm) {
            case 0: // RV32D: FSGNJ.D
              rd_data[t].u64 = rv_fsgnj_d(rs1_data[t].u64, rs2_data[t].u64);
              break;
            case 1: // RV32D: FSGNJN.D
              rd_data[t].u64 = rv_fsgnjn_d(rs1_data[t].u64, rs2_data[t].u64);
              break;
            case 2: // RV32D: FSGNJX.D
              rd_data[t].u64 = rv_fsgnjx_d(rs1_data[t].u64, rs2_data[t].u64);
              break;
            }
          } else {
            switch (fpuArgs.frm) {
            case 0: // RV32F: FSGNJ.S
              rd_data[t].u64 = nan_box(rv_fsgnj_s(check_boxing(rs1_data[t].u64), check_boxing(rs2_data[t].u64)));
              break;
            case 1: // RV32F: FSGNJN.S
              rd_data[t].u64 = nan_box(rv_fsgnjn_s(check_boxing(rs1_data[t].u64), check_boxing(rs2_data[t].u64)));
              break;
            case 2: // RV32F: FSGNJX.S
              rd_data[t].u64 = nan_box(rv_fsgnjx_s(check_boxing(rs1_data[t].u64), check_boxing(rs2_data[t].u64)));
              break;
            }
          }
          this->update_fcrs(fflags, wid, t);
        }
      } break;
      case FpuType::FMINMAX: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint32_t fflags = 0;
          if (fpuArgs.is_f64) {
            if (fpuArgs.frm) {
              rd_data[t].u64 = rv_fmax_d(rs1_data[t].u64, rs2_data[t].u64, &fflags);
            } else {
              rd_data[t].u64 = rv_fmin_d(rs1_data[t].u64, rs2_data[t].u64, &fflags);
            }
          } else {
            if (fpuArgs.frm) {
              rd_data[t].u64 = nan_box(rv_fmax_s(check_boxing(rs1_data[t].u64), check_boxing(rs2_data[t].u64), &fflags));
            } else {
              rd_data[t].u64 = nan_box(rv_fmin_s(check_boxing(rs1_data[t].u64), check_boxing(rs2_data[t].u64), &fflags));
            }
          }
          this->update_fcrs(fflags, wid, t);
        }
      } break;
      case FpuType::FCMP: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint32_t fflags = 0;
          if (fpuArgs.is_f64) {
            switch (fpuArgs.frm) {
            case 0: // RV32D: FLE.D
              rd_data[t].i = rv_fle_d(rs1_data[t].u64, rs2_data[t].u64, &fflags);
              break;
            case 1: // RV32D: FLT.D
              rd_data[t].i = rv_flt_d(rs1_data[t].u64, rs2_data[t].u64, &fflags);
              break;
            case 2: // RV32D: FEQ.D
              rd_data[t].i = rv_feq_d(rs1_data[t].u64, rs2_data[t].u64, &fflags);
              break;
            }
          } else {
            switch (fpuArgs.frm) {
            case 0: // RV32F: FLE.S
              rd_data[t].i = rv_fle_s(check_boxing(rs1_data[t].u64), check_boxing(rs2_data[t].u64), &fflags);
              break;
            case 1: // RV32F: FLT.S
              rd_data[t].i = rv_flt_s(check_boxing(rs1_data[t].u64), check_boxing(rs2_data[t].u64), &fflags);
              break;
            case 2: // RV32F: FEQ.S
              rd_data[t].i = rv_feq_s(check_boxing(rs1_data[t].u64), check_boxing(rs2_data[t].u64), &fflags);
              break;
            }
          }
          this->update_fcrs(fflags, wid, t);
        }
      } break;
      case FpuType::F2I: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint32_t frm = this->get_fpu_rm(fpuArgs.frm, wid, t);
          uint32_t fflags = 0;
          if (fpuArgs.is_f64) {
            switch (fpuArgs.cvt) {
            case 0: // RV32D: FCVT.W.D
              rd_data[t].i = sext((uint64_t)rv_ftoi_d(rs1_data[t].u64, frm, &fflags), 32);
              break;
            case 1: // RV32D: FCVT.WU.D
              rd_data[t].i = sext((uint64_t)rv_ftou_d(rs1_data[t].u64, frm, &fflags), 32);
              break;
            case 2: // RV64D: FCVT.L.D
              rd_data[t].i = rv_ftol_d(rs1_data[t].u64, frm, &fflags);
              break;
            case 3: // RV64D: FCVT.LU.D
              rd_data[t].i = rv_ftolu_d(rs1_data[t].u64, frm, &fflags);
              break;
            }
          } else {
            switch (fpuArgs.cvt) {
            case 0:
              // RV32F: FCVT.W.S
              rd_data[t].i = sext((uint64_t)rv_ftoi_s(check_boxing(rs1_data[t].u64), frm, &fflags), 32);
              break;
            case 1:
              // RV32F: FCVT.WU.S
              rd_data[t].i = sext((uint64_t)rv_ftou_s(check_boxing(rs1_data[t].u64), frm, &fflags), 32);
              break;
            case 2:
              // RV64F: FCVT.L.S
              rd_data[t].i = rv_ftol_s(check_boxing(rs1_data[t].u64), frm, &fflags);
              break;
            case 3:
              // RV64F: FCVT.LU.S
              rd_data[t].i = rv_ftolu_s(check_boxing(rs1_data[t].u64), frm, &fflags);
              break;
            }
          }
          this->update_fcrs(fflags, wid, t);
        }
      } break;
      case FpuType::I2F: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint32_t frm = this->get_fpu_rm(fpuArgs.frm, wid, t);
          uint32_t fflags = 0;
          if (fpuArgs.is_f64) {
            switch (fpuArgs.cvt) {
            case 0: // RV32D: FCVT.D.W
              rd_data[t].u64 = rv_itof_d(rs1_data[t].i, frm, &fflags);
              break;
            case 1: // RV32D: FCVT.D.WU
              rd_data[t].u64 = rv_utof_d(rs1_data[t].i, frm, &fflags);
              break;
            case 2: // RV64D: FCVT.D.L
              rd_data[t].u64 = rv_ltof_d(rs1_data[t].i, frm, &fflags);
              break;
            case 3: // RV64D: FCVT.D.LU
              rd_data[t].u64 = rv_lutof_d(rs1_data[t].i, frm, &fflags);
              break;
            }
          } else {
            switch (fpuArgs.cvt) {
            case 0: // RV32F: FCVT.S.W
              rd_data[t].u64 = nan_box(rv_itof_s(rs1_data[t].i, frm, &fflags));
              break;
            case 1: // RV32F: FCVT.S.WU
              rd_data[t].u64 = nan_box(rv_utof_s(rs1_data[t].i, frm, &fflags));
              break;
            case 2: // RV64F: FCVT.S.L
              rd_data[t].u64 = nan_box(rv_ltof_s(rs1_data[t].i, frm, &fflags));
              break;
            case 3: // RV64F: FCVT.S.LU
              rd_data[t].u64 = nan_box(rv_lutof_s(rs1_data[t].i, frm, &fflags));
              break;
            }
          }
          this->update_fcrs(fflags, wid, t);
        }
      } break;
      case FpuType::F2F: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint32_t fflags = 0;
          if (fpuArgs.is_f64) {
            rd_data[t].u64 = rv_ftod(check_boxing(rs1_data[t].u64));
          } else {
            rd_data[t].u64 = nan_box(rv_dtof(rs1_data[t].u64));
          }
          this->update_fcrs(fflags, wid, t);
        }
      } break;
      case FpuType::FCLASS: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint32_t fflags = 0;
          if (fpuArgs.is_f64) {
            rd_data[t].i = rv_fclss_d(rs1_data[t].u64);
          } else {
            rd_data[t].i = rv_fclss_s(check_boxing(rs1_data[t].u64));
          }
          this->update_fcrs(fflags, wid, t);
        }
      } break;
      case FpuType::FMVXW: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint32_t fflags = 0;
          if (fpuArgs.is_f64) { // RV64D: FMV.X.D
            rd_data[t].u64 = rs1_data[t].u64;
          } else { // RV32F: FMV.X.S
            uint32_t result = (uint32_t)rs1_data[t].u64;
            rd_data[t].i = sext((uint64_t)result, 32);
          }
          this->update_fcrs(fflags, wid, t);
        }
      } break;
      case FpuType::FMVWX: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint32_t fflags = 0;
          if (fpuArgs.is_f64) { // RV64D: FMV.D.X
            rd_data[t].u64 = rs1_data[t].i;
          } else { // RV32F: FMV.S.X
            rd_data[t].u64 = nan_box((uint32_t)rs1_data[t].i);
          }
          this->update_fcrs(fflags, wid, t);
        }
      } break;
      case FpuType::FMADD:
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint32_t frm = this->get_fpu_rm(fpuArgs.frm, wid, t);
          uint32_t fflags = 0;
            if (fpuArgs.is_f64) {
            rd_data[t].u64 = rv_fmadd_d(rs1_data[t].u64, rs2_data[t].u64, rs3_data[t].u64, frm, &fflags);
          } else {
            rd_data[t].u64 = nan_box(rv_fmadd_s(check_boxing(rs1_data[t].u64), check_boxing(rs2_data[t].u64), check_boxing(rs3_data[t].u64), frm, &fflags));
          }
          this->update_fcrs(fflags, wid, t);
        }
        break;
      case FpuType::FMSUB: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint32_t frm = this->get_fpu_rm(fpuArgs.frm, wid, t);
          uint32_t fflags = 0;
          if (fpuArgs.is_f64) {
            rd_data[t].u64 = rv_fmsub_d(rs1_data[t].u64, rs2_data[t].u64, rs3_data[t].u64, frm, &fflags);
          } else {
            rd_data[t].u64 = nan_box(rv_fmsub_s(check_boxing(rs1_data[t].u64), check_boxing(rs2_data[t].u64), check_boxing(rs3_data[t].u64), frm, &fflags));
          }
          this->update_fcrs(fflags, wid, t);
        }
      } break;
      case FpuType::FNMADD: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint32_t frm = this->get_fpu_rm(fpuArgs.frm, wid, t);
          uint32_t fflags = 0;
          if (fpuArgs.is_f64) {
            rd_data[t].u64 = rv_fnmadd_d(rs1_data[t].u64, rs2_data[t].u64, rs3_data[t].u64, frm, &fflags);
          } else {
            rd_data[t].u64 = nan_box(rv_fnmadd_s(check_boxing(rs1_data[t].u64), check_boxing(rs2_data[t].u64), check_boxing(rs3_data[t].u64), frm, &fflags));
          }
          this->update_fcrs(fflags, wid, t);
        }
      } break;
      case FpuType::FNMSUB: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          uint32_t frm = this->get_fpu_rm(fpuArgs.frm, wid, t);
          uint32_t fflags = 0;
          if (fpuArgs.is_f64) {
            rd_data[t].u64 = rv_fnmsub_d(rs1_data[t].u64, rs2_data[t].u64, rs3_data[t].u64, frm, &fflags);
          } else {
            rd_data[t].u64 = nan_box(rv_fnmsub_s(check_boxing(rs1_data[t].u64), check_boxing(rs2_data[t].u64), check_boxing(rs3_data[t].u64), frm, &fflags));
          }
          this->update_fcrs(fflags, wid, t);
        }
      } break;
      default:
        std::abort();
      }
      rd_write = true;
    },
    [&](CsrType csr_type) {
      auto csrArgs = std::get<IntrCsrArgs>(instrArgs);
      uint32_t csr_addr = csrArgs.csr;
      switch (csr_type) {
      case CsrType::CSRRW: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          Word csr_value = this->get_csr(csr_addr, wid, t);
          auto src_data = csrArgs.is_imm ? csrArgs.imm : rs1_data[t].i;
          this->set_csr(csr_addr, src_data, wid, t);
          rd_data[t].i = csr_value;
        }
      } break;
      case CsrType::CSRRS: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          Word csr_value = this->get_csr(csr_addr, wid, t);
          auto src_data = csrArgs.is_imm ? csrArgs.imm : rs1_data[t].i;
          if (src_data != 0) {
            this->set_csr(csr_addr, csr_value | src_data, wid, t);
          }
          rd_data[t].i = csr_value;
        }
      } break;
      case CsrType::CSRRC: {
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          Word csr_value = this->get_csr(csr_addr, wid, t);
          auto src_data = csrArgs.is_imm ? csrArgs.imm : rs1_data[t].i;
          if (src_data != 0) {
            this->set_csr(csr_addr, csr_value & ~src_data, wid, t);
          }
          rd_data[t].i = csr_value;
        }
      } break;
      default:
        std::abort();
      }
      trace->fetch_stall = (csr_addr <= VX_CSR_FCSR);
      rd_write = true;
    },
    [&](WctlType wctl_type) {
      auto wctlArgs = std::get<IntrWctlArgs>(instrArgs);
      switch (wctl_type) {
      case WctlType::TMC: {
        trace->fetch_stall = true;
        next_tmask.reset();
        for (uint32_t t = 0; t < num_threads; ++t) {
          next_tmask.set(t, rs1_data.at(thread_last).u & (1 << t));
        }
      } break;
      case WctlType::WSPAWN: {
        trace->fetch_stall = true;
        trace->data = std::make_shared<SfuTraceData>(rs1_data.at(thread_last).u, rs2_data.at(thread_last).u);
      } break;
      case WctlType::SPLIT: {
        trace->fetch_stall = true;
        auto stack_size = warp.ipdom_stack.size();

        ThreadMask then_tmask(num_threads);
        ThreadMask else_tmask(num_threads);
        auto not_pred = wctlArgs.is_neg;
        for (uint32_t t = 0; t < num_threads; ++t) {
          auto cond = (rs1_data.at(t).i & 0x1) ^ not_pred;
          then_tmask[t] = warp.tmask.test(t) && cond;
          else_tmask[t] = warp.tmask.test(t) && !cond;
        }

        bool is_divergent = then_tmask.any() && else_tmask.any();
        if (is_divergent) {
          if (stack_size == ipdom_size_) {
            std::cout << "IPDOM stack is full! size=" << stack_size << ", PC=0x" << std::hex << warp.PC << std::dec << " (#" << trace->uuid << ")\n" << std::flush;
            std::abort();
          }
          // set new thread mask to the larger set
          if (then_tmask.count() >= else_tmask.count()) {
            next_tmask = then_tmask;
          } else {
            next_tmask = else_tmask;
          }
          // push reconvergence and not-taken thread mask onto the stack
          auto ntaken_tmask = ~next_tmask & warp.tmask;
          warp.ipdom_stack.emplace(warp.tmask, ntaken_tmask, next_pc);
        }
        // return divergent state
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          rd_data[t].i = stack_size;
        }
        rd_write = true;
      } break;
      case WctlType::JOIN: {
        trace->fetch_stall = true;
        auto stack_ptr = rs1_data.at(thread_last).u;
        if (stack_ptr != warp.ipdom_stack.size()) {
          if (warp.ipdom_stack.empty()) {
            std::cout << "IPDOM stack is empty!\n" << std::flush;
            std::abort();
          }
          if (warp.ipdom_stack.top().fallthrough) {
            next_tmask = warp.ipdom_stack.top().orig_tmask;
            warp.ipdom_stack.pop();
          } else {
            next_tmask = warp.ipdom_stack.top().else_tmask;
            next_pc = warp.ipdom_stack.top().PC;
            warp.ipdom_stack.top().fallthrough = true;
          }
        }
      } break;
      case WctlType::BAR: {
        trace->fetch_stall = true;
        trace->data = std::make_shared<SfuTraceData>(rs1_data[thread_last].i, rs2_data[thread_last].i);
      } break;
      case WctlType::PRED: {
        trace->fetch_stall = true;
        ThreadMask pred(num_threads);
        auto not_pred = wctlArgs.is_neg;
        for (uint32_t t = 0; t < num_threads; ++t) {
          auto cond = (rs1_data.at(t).i & 0x1) ^ not_pred;
          pred[t] = warp.tmask.test(t) && cond;
        }
        if (pred.any()) {
          next_tmask &= pred;
        } else {
          next_tmask = ThreadMask(num_threads, rs2_data.at(thread_last).u);
        }
      } break;
      default:
        std::abort();
      }
    }
  #ifdef EXT_V_ENABLE
    ,[&](VsetType /*vset_type*/) {
      auto trace_data = std::make_shared<VecUnit::ExeTraceData>();
      trace->data = trace_data;
      for (uint32_t t = thread_start; t < num_threads; ++t) {
        if (!warp.tmask.test(t))
          continue;
        vec_unit_->configure(instr, wid, t, rs1_data, rs2_data, rd_data, trace_data.get());
      }
      rd_write = true;
    },
    [&](VlsType vls_type) {
      switch (vls_type) {
      case VlsType::VL:
      case VlsType::VLS:
      case VlsType::VLX: {
        auto trace_data = std::make_shared<VecUnit::MemTraceData>(num_threads);
        trace->data = trace_data;
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          vec_unit_->load(instr, wid, t, rs1_data, rs2_data, trace_data.get());
        }
        rd_write = true;
      } break;
      case VlsType::VS:
      case VlsType::VSS:
      case VlsType::VSX: {
        auto trace_data = std::make_shared<VecUnit::MemTraceData>(num_threads);
        trace->data = trace_data;
        for (uint32_t t = thread_start; t < num_threads; ++t) {
          if (!warp.tmask.test(t))
            continue;
          vec_unit_->store(instr, wid, t, rs1_data, rs2_data, trace_data.get());
        }
      } break;
      default:
        std::abort();
      }
    },
    [&](VopType /*vop_type*/) {
      auto trace_data = std::make_shared<VecUnit::ExeTraceData>();
      trace->data = trace_data;
      for (uint32_t t = thread_start; t < num_threads; ++t) {
        if (!warp.tmask.test(t))
          continue;
        vec_unit_->execute(instr, wid, t, rs1_data, rd_data, trace_data.get());
      }
      rd_write = true;
    }
  #endif // EXT_V_ENABLE
  #ifdef EXT_TCU_ENABLE
    ,[&](TcuType tcu_type) {
      auto tpuArgs = std::get<IntrTcuArgs>(instrArgs);
      switch (tcu_type) {
      case TcuType::WMMA: {
        auto trace_data = std::make_shared<TensorUnit::ExeTraceData>();
        trace->data = trace_data;
        assert(warp.tmask.count() == num_threads);
        tensor_unit_->wmma(wid, tpuArgs.fmt_s, tpuArgs.fmt_d, tpuArgs.step_m, tpuArgs.step_n, rs1_data, rs2_data, rs3_data, rd_data, trace_data.get());
        rd_write = true;
      } break;
      default:
        std::abort();
      }
    }
  #endif // EXT_TCU_ENABLE
  );

  if (rd_write) {
    trace->wb = true;
    switch (rdest.type) {
    case RegType::None:
      break;
    case RegType::Integer:
      if (rdest.idx != 0) {
        DPH(2, "Dest Reg: " << rdest << "={");
        for (uint32_t t = 0; t < num_threads; ++t) {
          if (t) DPN(2, ", ");
          if (!warp.tmask.test(t)) {
            DPN(2, "-");
            continue;
          }
          warp.ireg_file.at(rdest.idx).at(t) = rd_data[t].i;
          DPN(2, "0x" << std::hex << rd_data[t].u << std::dec);
        }
        DPN(2, "}" << std::endl);
      } else {
        // disable writes to x0
        trace->wb = false;
      }
      break;
    case RegType::Float:
      DPH(2, "Dest Reg: " << rdest << "={");
      for (uint32_t t = 0; t < num_threads; ++t) {
        if (t) DPN(2, ", ");
        if (!warp.tmask.test(t)) {
          DPN(2, "-");
          continue;
        }
        warp.freg_file.at(rdest.idx).at(t) = rd_data[t].u64;
        if ((rd_data[t].u64 >> 32) == 0xffffffff) {
          DPN(2, "0x" << std::hex << rd_data[t].u32 << std::dec);
        } else {
          DPN(2, "0x" << std::hex << rd_data[t].u64 << std::dec);
        }
      }
      DPN(2, "}" << std::endl);
      break;
  #ifdef EXT_V_ENABLE
    case RegType::Vector:
      DPH(2, "Dest Reg: " << rdest << "={");
      for (uint32_t t = 0; t < num_threads; ++t) {
        if (t) DPN(2, ", ");
        if (!warp.tmask.test(t)) {
          DPN(2, "-");
          continue;
        }
        DPN(2, vec_unit_->dumpRegister(wid, t, rdest.idx));
      }
      DPN(2, "}" << std::endl);
      break;
  #endif
    default:
      std::cout << "Unrecognized register write back type: " << rdest.type << std::endl;
      std::abort();
      break;
    }
  }

  warp.PC += 4;

  if (warp.PC != next_pc) {
    DP(3, "*** Next PC=0x" << std::hex << next_pc << std::dec);
    warp.PC = next_pc;
  }

  if (warp.tmask != next_tmask) {
    DP(3, "*** New Tmask=" << next_tmask);
    warp.tmask = next_tmask;
    if (!next_tmask.any()) {
      active_warps_.reset(wid);
    }
  }

  DP(5, "Register state:");
  for (uint32_t i = 0; i < MAX_NUM_REGS; ++i) {
    DPN(5, "  %r" << std::setfill('0') << std::setw(2) << i << ':' << std::hex);
    // Integer register file
    for (uint32_t j = 0; j < arch_.num_threads(); ++j) {
      DPN(5, ' ' << std::setfill('0') << std::setw(XLEN/4) << warp.ireg_file.at(i).at(j) << std::setfill(' ') << ' ');
    }
    DPN(5, '|');
    // Floating point register file
    for (uint32_t j = 0; j < arch_.num_threads(); ++j) {
      DPN(5, ' ' << std::setfill('0') << std::setw(16) << warp.freg_file.at(i).at(j) << std::setfill(' ') << ' ');
    }
    DPN(5, std::dec << std::endl);
  }

  return trace;
}
