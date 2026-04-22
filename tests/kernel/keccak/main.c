#include <stdint.h>
#include <vx_intrinsics.h>
#include <vx_print.h>
#include "keccak.h"

static const uint8_t k_digest_empty[KECCAK_SHA3_256_DIGEST_BYTES] = {
    0xa7, 0xff, 0xc6, 0xf8, 0xbf, 0x1e, 0xd7, 0x66,
    0x51, 0xc1, 0x47, 0x56, 0xa0, 0x61, 0xd6, 0x62,
    0xf5, 0x80, 0xff, 0x4d, 0xe4, 0x3b, 0x49, 0xfa,
    0x82, 0xd8, 0x0a, 0x4b, 0x80, 0xf8, 0x43, 0x4a,
};

static const uint8_t k_digest_abc[KECCAK_SHA3_256_DIGEST_BYTES] = {
    0x3a, 0x98, 0x5d, 0xa7, 0x4f, 0xe2, 0x25, 0xb2,
    0x04, 0x5c, 0x17, 0x2d, 0x6b, 0xd3, 0x90, 0xbd,
    0x85, 0x5f, 0x08, 0x6e, 0x3e, 0x9d, 0x52, 0x5b,
    0x46, 0xbf, 0xe2, 0x45, 0x11, 0x43, 0x15, 0x32,
};

static const uint8_t k_digest_abcdbc[KECCAK_SHA3_256_DIGEST_BYTES] = {
    0x41, 0xc0, 0xdb, 0xa2, 0xa9, 0xd6, 0x24, 0x08,
    0x49, 0x10, 0x03, 0x76, 0xa8, 0x23, 0x5e, 0x2c,
    0x82, 0xe1, 0xb9, 0x99, 0x8a, 0x99, 0x9e, 0x21,
    0xdb, 0x32, 0xdd, 0x97, 0x49, 0x6d, 0x33, 0x76,
};

static int digest_matches(const uint8_t *got, const uint8_t *expected) {
    for (uint32_t i = 0; i < KECCAK_SHA3_256_DIGEST_BYTES; ++i) {
        if (got[i] != expected[i]) {
            return 0;
        }
    }
    return 1;
}

static uint32_t digest_word(const uint8_t *digest, uint32_t offset) {
    return ((uint32_t)digest[offset] << 24)
         | ((uint32_t)digest[offset + 1] << 16)
         | ((uint32_t)digest[offset + 2] << 8)
         | (uint32_t)digest[offset + 3];
}

static int run_case(const char *name,
                    const char *msg,
                    uint64_t len,
                    const uint8_t *expected) {
    uint8_t digest[KECCAK_SHA3_256_DIGEST_BYTES];
    sha3_256((const uint8_t *)msg, len, digest);
    if (!digest_matches(digest, expected)) {
        vx_printf("Keccak/SHA3-256 %s mismatch: got=%08x%08x...\n",
                  name,
                  digest_word(digest, 0),
                  digest_word(digest, 4));
        return 1;
    }
    return 0;
}

#ifdef KECCAK_NATIVE
static int run_native_lane_sanity(void) {
    enum { kKeccakStateLanes = 25 };
    const uint64_t seed = 0x0123456789abcdefULL;
    const uint64_t delta = 0xfedcba9876543210ULL;
    const uint32_t lane = 7;

    __intrin_keccak_write_lane(seed, lane);
    if (__intrin_keccak_read_lane(lane) != seed) {
        vx_printf("Keccak lane write/read mismatch\n");
        return 1;
    }

    __intrin_keccak_xor_lane(delta, lane);
    if (__intrin_keccak_read_lane(lane) != (seed ^ delta)) {
        vx_printf("Keccak lane xor/read mismatch\n");
        return 1;
    }

    for (uint32_t i = 0; i < kKeccakStateLanes; ++i) {
        __intrin_keccak_write_lane(0, i);
    }

    return 0;
}
#endif

int main(void) {
    if (vx_core_id() != 0 || vx_warp_id() != 0 || vx_thread_id() != 0) {
        return 0;
    }

    int errors = 0;

#ifdef KECCAK_NATIVE
    errors += run_native_lane_sanity();
#endif
    errors += run_case("empty", "", 0, k_digest_empty);
    errors += run_case("abc", "abc", 3, k_digest_abc);
    errors += run_case("abcdbc",
                       "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
                       56,
                       k_digest_abcdbc);

    if (errors) {
        vx_printf("Keccak/SHA3-256 Failed (%d errors)\n", errors);
        return errors;
    }

    #ifdef KECCAK_NATIVE
    vx_printf("Keccak/SHA3-256 Passed! mode=NATIVE\n");
    #else
    vx_printf("Keccak/SHA3-256 Passed! mode=SOFTWARE\n");
    #endif
    return 0;
}
