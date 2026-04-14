#include <stdint.h>
#include <vx_intrinsics.h>
#include <vx_print.h>

static uint8_t sbox(uint8_t byte) {
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

static uint8_t inv_sbox(uint8_t byte) {
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

static uint8_t xtime(uint8_t byte) {
  return ((byte << 1) & 0xff) ^ ((byte & 0x80) ? 0x1b : 0x00);
}

static uint32_t pack_bytes(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
  return uint32_t(b0) | (uint32_t(b1) << 8) | (uint32_t(b2) << 16) | (uint32_t(b3) << 24);
}

static uint64_t pack_cols(uint32_t c0, uint32_t c1) {
  return uint64_t(c0) | (uint64_t(c1) << 32);
}

static uint8_t col_byte(uint32_t col, uint32_t idx) {
  return (col >> (idx * 8)) & 0xff;
}

static uint32_t subbytes_fwd(uint32_t word) {
  return pack_bytes(
    sbox(col_byte(word, 0)),
    sbox(col_byte(word, 1)),
    sbox(col_byte(word, 2)),
    sbox(col_byte(word, 3)));
}

static uint32_t subbytes_inv(uint32_t word) {
  return pack_bytes(
    inv_sbox(col_byte(word, 0)),
    inv_sbox(col_byte(word, 1)),
    inv_sbox(col_byte(word, 2)),
    inv_sbox(col_byte(word, 3)));
}

static uint32_t mixcolumn_fwd(uint32_t word) {
  uint8_t b0 = col_byte(word, 0);
  uint8_t b1 = col_byte(word, 1);
  uint8_t b2 = col_byte(word, 2);
  uint8_t b3 = col_byte(word, 3);
  return pack_bytes(
    xtime(b0) ^ b1 ^ xtime(b1) ^ b2 ^ b3,
    b0 ^ xtime(b1) ^ b2 ^ xtime(b2) ^ b3,
    b0 ^ b1 ^ xtime(b2) ^ b3 ^ xtime(b3),
    b0 ^ xtime(b0) ^ b1 ^ b2 ^ xtime(b3));
}

static uint32_t mixcolumn_inv(uint32_t word) {
  uint8_t b0 = col_byte(word, 0);
  uint8_t b1 = col_byte(word, 1);
  uint8_t b2 = col_byte(word, 2);
  uint8_t b3 = col_byte(word, 3);
  uint8_t x0 = xtime(b0);
  uint8_t x1 = xtime(b1);
  uint8_t x2 = xtime(b2);
  uint8_t x3 = xtime(b3);
  uint8_t x20 = xtime(x0);
  uint8_t x21 = xtime(x1);
  uint8_t x22 = xtime(x2);
  uint8_t x23 = xtime(x3);
  uint8_t x30 = xtime(x20);
  uint8_t x31 = xtime(x21);
  uint8_t x32 = xtime(x22);
  uint8_t x33 = xtime(x23);
  return pack_bytes(
    x0 ^ x20 ^ x30 ^ b1 ^ x1 ^ x31 ^ b2 ^ x22 ^ x32 ^ b3 ^ x33,
    b0 ^ x30 ^ x1 ^ x21 ^ x31 ^ b2 ^ x2 ^ x32 ^ b3 ^ x23 ^ x33,
    b0 ^ x20 ^ x30 ^ b1 ^ x31 ^ x2 ^ x22 ^ x32 ^ b3 ^ x3 ^ x33,
    b0 ^ x0 ^ x30 ^ b1 ^ x21 ^ x31 ^ b2 ^ x32 ^ x3 ^ x23 ^ x33);
}

static uint32_t aes_rcon_ref(uint32_t round) {
  static const uint32_t table[] = {
    0x00000000, 0x00000001, 0x00000002, 0x00000004,
    0x00000008, 0x00000010, 0x00000020, 0x00000040,
    0x00000080, 0x0000001b, 0x00000036
  };
  return table[round];
}

static uint64_t aes64_shiftrows_fwd_ref(uint64_t rs1, uint64_t rs2) {
  uint32_t a0 = uint32_t(rs1);
  uint32_t a1 = uint32_t(rs1 >> 32);
  uint32_t b0 = uint32_t(rs2);
  uint32_t b1 = uint32_t(rs2 >> 32);
  uint32_t o0 = pack_bytes(col_byte(a0, 0), col_byte(a1, 1), col_byte(b0, 2), col_byte(b1, 3));
  uint32_t o1 = pack_bytes(col_byte(a1, 0), col_byte(b0, 1), col_byte(b1, 2), col_byte(a0, 3));
  return pack_cols(o0, o1);
}

static uint64_t aes64_shiftrows_inv_ref(uint64_t rs1, uint64_t rs2) {
  uint32_t a0 = uint32_t(rs1);
  uint32_t a1 = uint32_t(rs1 >> 32);
  uint32_t b0 = uint32_t(rs2);
  uint32_t b1 = uint32_t(rs2 >> 32);
  uint32_t o0 = pack_bytes(col_byte(a0, 0), col_byte(b1, 1), col_byte(b0, 2), col_byte(a1, 3));
  uint32_t o1 = pack_bytes(col_byte(a1, 0), col_byte(a0, 1), col_byte(b1, 2), col_byte(b0, 3));
  return pack_cols(o0, o1);
}

static int check_scalar_ops64() {
#ifdef XLEN_64
  int errors = 0;
  const uint64_t rs1 = 0x8899aabb00112233ull;
  const uint64_t rs2 = 0xccddeeff44556677ull;
  auto check = [&](const char* name, uint64_t got, uint32_t ref_hi, uint32_t ref_lo) {
    uint32_t got_hi = uint32_t(got >> 32);
    uint32_t got_lo = uint32_t(got);
    if (got_hi != ref_hi || got_lo != ref_lo) {
      vx_printf("%s mismatch: got=%08x%08x ref=%08x%08x\n",
                name,
                got_hi, got_lo,
                ref_hi, ref_lo);
      ++errors;
    }
  };

  check("AES64ES", __intrin_aes64es(rs1, rs2), 0x63c133ea, 0x4bfcacc3);
  check("AES64ESM", __intrin_aes64esm(rs1, rs2), 0x11e5b738, 0x9851d4c5);
  check("AES64DS", __intrin_aes64ds(rs1, rs2), 0x86c994fe, 0x97ed9966);
  check("AES64DSM", __intrin_aes64dsm(rs1, rs2), 0x289d70e0, 0x97c28858);
  check("AES64IM", __intrin_aes64im(rs1), 0x66334411, 0xeebbcc99);
  check("AES64KS1I", __intrin_aes64ks1i(rs1, 0x4), 0xeac4eea4, 0xeac4eea4);
  check("AES64KS2", __intrin_aes64ks2(rs1, rs2), 0x00112233, 0xcccccccc);
  return errors;
#else
  return 0;
#endif
}

static uint32_t aes32esi_ref(uint32_t acc, uint32_t word, uint32_t byte_select, int mix_columns) {
  uint8_t victim = (word >> (byte_select << 3)) & 0xff;
  uint8_t value = sbox(victim);
  if (!mix_columns) {
    return acc ^ (uint32_t(value) << (byte_select << 3));
  }

  uint8_t xval = xtime(value);
  uint8_t x21val = xval ^ value;
  uint32_t mix = 0;
  switch (byte_select & 0x3) {
  case 0: mix = (uint32_t(x21val) << 24) | (uint32_t(value) << 16) | (uint32_t(value) << 8) | xval; break;
  case 1: mix = (uint32_t(value) << 24) | (uint32_t(value) << 16) | (uint32_t(xval) << 8) | x21val; break;
  case 2: mix = (uint32_t(value) << 24) | (uint32_t(xval) << 16) | (uint32_t(x21val) << 8) | value; break;
  default: mix = (uint32_t(xval) << 24) | (uint32_t(x21val) << 16) | (uint32_t(value) << 8) | value; break;
  }
  return acc ^ mix;
}

static uint32_t aes32dsi_ref(uint32_t acc, uint32_t word, uint32_t byte_select, int inv_mix_columns) {
  uint8_t victim = (word >> (byte_select << 3)) & 0xff;
  uint8_t value = inv_sbox(victim);
  if (!inv_mix_columns) {
    return acc ^ (uint32_t(value) << (byte_select << 3));
  }

  uint8_t xval = xtime(value);
  uint8_t x2val = xtime(xval);
  uint8_t x3val = xtime(x2val);
  uint8_t x321val = xval ^ x2val ^ x3val;
  uint8_t x310val = value ^ xval ^ x3val;
  uint8_t x320val = value ^ x2val ^ x3val;
  uint8_t x30val = value ^ x3val;
  uint32_t mix = 0;
  switch (byte_select & 0x3) {
  case 0: mix = (uint32_t(x310val) << 24) | (uint32_t(x320val) << 16) | (uint32_t(x30val) << 8) | x321val; break;
  case 1: mix = (uint32_t(x320val) << 24) | (uint32_t(x30val) << 16) | (uint32_t(x321val) << 8) | x310val; break;
  case 2: mix = (uint32_t(x30val) << 24) | (uint32_t(x321val) << 16) | (uint32_t(x310val) << 8) | x320val; break;
  default: mix = (uint32_t(x321val) << 24) | (uint32_t(x310val) << 16) | (uint32_t(x320val) << 8) | x30val; break;
  }
  return acc ^ mix;
}

static int check_scalar_ops() {
#ifdef XLEN_64
  return 0;
#else
  int errors = 0;
  const uint32_t acc = 0x11223344;
  const uint32_t word = 0xa1b2c3d4;

  for (uint32_t byte_select = 0; byte_select < 4; ++byte_select) {
    uint32_t got = __intrin_aes32esi(acc, word, byte_select);
    uint32_t ref = aes32esi_ref(acc, word, byte_select, 0);
    if (got != ref) {
      vx_printf("AES32ESI mismatch at byte %d: got=%08x ref=%08x\n", byte_select, got, ref);
      ++errors;
    }

    got = __intrin_aes32esmi(acc, word, byte_select);
    ref = aes32esi_ref(acc, word, byte_select, 1);
    if (got != ref) {
      vx_printf("AES32ESMI mismatch at byte %d: got=%08x ref=%08x\n", byte_select, got, ref);
      ++errors;
    }

    got = __intrin_aes32dsi(acc, word, byte_select);
    ref = aes32dsi_ref(acc, word, byte_select, 0);
    if (got != ref) {
      vx_printf("AES32DSI mismatch at byte %d: got=%08x ref=%08x\n", byte_select, got, ref);
      ++errors;
    }

    got = __intrin_aes32dsmi(acc, word, byte_select);
    ref = aes32dsi_ref(acc, word, byte_select, 1);
    if (got != ref) {
      vx_printf("AES32DSMI mismatch at byte %d: got=%08x ref=%08x\n", byte_select, got, ref);
      ++errors;
    }
  }

  return errors;
#endif
}

static int check_round_helpers() {
  int errors = 0;
  const uint32_t state[4] = {0x00112233, 0x44556677, 0x8899aabb, 0xccddeeff};
  const uint32_t round_key[4] = {0x0f1e2d3c, 0x4b5a6978, 0x8796a5b4, 0xc3d2e1f0};
  uint32_t got[4];
  uint32_t ref[4];

  __intrin_aes_last_enc_round(got, state, round_key);
  ref[0] = aes32esi_ref(aes32esi_ref(aes32esi_ref(aes32esi_ref(round_key[0], state[0], 0, 0), state[1], 1, 0), state[2], 2, 0), state[3], 3, 0);
  ref[1] = aes32esi_ref(aes32esi_ref(aes32esi_ref(aes32esi_ref(round_key[1], state[1], 0, 0), state[2], 1, 0), state[3], 2, 0), state[0], 3, 0);
  ref[2] = aes32esi_ref(aes32esi_ref(aes32esi_ref(aes32esi_ref(round_key[2], state[2], 0, 0), state[3], 1, 0), state[0], 2, 0), state[1], 3, 0);
  ref[3] = aes32esi_ref(aes32esi_ref(aes32esi_ref(aes32esi_ref(round_key[3], state[3], 0, 0), state[0], 1, 0), state[1], 2, 0), state[2], 3, 0);
  for (int i = 0; i < 4; ++i) {
    if (got[i] != ref[i]) {
      vx_printf("AES enc-last mismatch at col %d: got=%08x ref=%08x\n", i, got[i], ref[i]);
      ++errors;
    }
  }

  __intrin_aes_last_dec_round(got, state, round_key);
  ref[0] = aes32dsi_ref(aes32dsi_ref(aes32dsi_ref(aes32dsi_ref(round_key[0], state[0], 0, 0), state[3], 1, 0), state[2], 2, 0), state[1], 3, 0);
  ref[1] = aes32dsi_ref(aes32dsi_ref(aes32dsi_ref(aes32dsi_ref(round_key[1], state[1], 0, 0), state[0], 1, 0), state[3], 2, 0), state[2], 3, 0);
  ref[2] = aes32dsi_ref(aes32dsi_ref(aes32dsi_ref(aes32dsi_ref(round_key[2], state[2], 0, 0), state[1], 1, 0), state[0], 2, 0), state[3], 3, 0);
  ref[3] = aes32dsi_ref(aes32dsi_ref(aes32dsi_ref(aes32dsi_ref(round_key[3], state[3], 0, 0), state[2], 1, 0), state[1], 2, 0), state[0], 3, 0);
  for (int i = 0; i < 4; ++i) {
    if (got[i] != ref[i]) {
      vx_printf("AES dec-last mismatch at col %d: got=%08x ref=%08x\n", i, got[i], ref[i]);
      ++errors;
    }
  }

  return errors;
}

int main() {
  int errors = 0;
  errors += check_scalar_ops();
  errors += check_scalar_ops64();
  errors += check_round_helpers();

  if (errors == 0) {
    vx_printf("AES smoke Passed!\n");
  } else {
    vx_printf("AES smoke Failed (%d errors)\n", errors);
  }

  return errors;
}
