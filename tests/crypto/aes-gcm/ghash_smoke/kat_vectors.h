// SPDX-License-Identifier: Apache-2.0
//
// Known-answer test data for the GHASH smoke.
//
// The smoke deliberately avoids depending on a third-party library at
// validation time: most cases are *algebraic identities* exercised in
// kernel.cpp (linearity, streaming==oneshot, etc.). The single fixed-value
// KAT here is one the reader can verify against NIST SP 800-38D
// §6.3 Algorithm 1 by hand:
//
//   Input X = 0x80 followed by fifteen 0x00 bytes
//     This encodes the polynomial element "1" under NIST bit numbering
//     (bit 0 of the polynomial is the MSB of byte 0).
//   GHASH(H, X) = (Y_prev ^ X) * H = (0 ^ 1) * H = 1 * H = H.
//
// The canonical H constant we use is AES-128(K=0, P=0), which equals
// 0x66e94bd4ef8a2c3b884cfa59ca342b2e. This value appears in NIST's GCM
// test vector workspaces (e.g. SP 800-38D Test Cases 1 and 2 with the
// all-zero key) and is the most commonly cited "reference H" in the
// literature, making it easy for a reader to cross-check.

#ifndef GHASH_SMOKE_KAT_VECTORS_H
#define GHASH_SMOKE_KAT_VECTORS_H

#include <stdint.h>
#include "ghash_ref.h"

// H = AES-128_ENC(key = 0^128, plaintext = 0^128).
static const uint8_t KAT_H_CANONICAL[GHASH_BLOCK_BYTES] = {
  0x66, 0xe9, 0x4b, 0xd4, 0xef, 0x8a, 0x2c, 0x3b,
  0x88, 0x4c, 0xfa, 0x59, 0xca, 0x34, 0x2b, 0x2e,
};

// Polynomial "1" under NIST bit numbering.
static const uint8_t KAT_BLOCK_ONE[GHASH_BLOCK_BYTES] = {
  0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// Polynomial "x" under NIST bit numbering.
static const uint8_t KAT_BLOCK_X[GHASH_BLOCK_BYTES] = {
  0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t KAT_BLOCK_ZERO[GHASH_BLOCK_BYTES] = {0};

// Arbitrary non-trivial inputs used by the algebraic-property tests
// (linearity, determinism, streaming==oneshot). These are not "answers" —
// they are inputs whose results we check against derived identities.
static const uint8_t KAT_DATA_A[2 * GHASH_BLOCK_BYTES] = {
  0xfe, 0xed, 0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef,
  0xfe, 0xed, 0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef,
  0xab, 0xad, 0xda, 0xd2, 0xca, 0xfe, 0xba, 0xbe,
  0xfa, 0xce, 0xdb, 0xad, 0xde, 0xca, 0xf8, 0x88,
};

static const uint8_t KAT_DATA_B[2 * GHASH_BLOCK_BYTES] = {
  0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
  0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
  0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88,
  0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00,
};

#endif // GHASH_SMOKE_KAT_VECTORS_H
