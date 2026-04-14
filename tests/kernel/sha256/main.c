#include <stdint.h>
#include <vx_print.h>
#include "sha256.h"

#ifdef SHA_NATIVE
#include <vx_intrinsics.h>
#endif

static const uint8_t k_digest_empty[SHA256_DIGEST_BYTES] = {
    0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
    0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
    0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
    0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
};

static const uint8_t k_digest_abc[SHA256_DIGEST_BYTES] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
    0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
    0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
};

static const uint8_t k_digest_abcdbc[SHA256_DIGEST_BYTES] = {
    0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8,
    0xe5, 0xc0, 0x26, 0x93, 0x0c, 0x3e, 0x60, 0x39,
    0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff, 0x21, 0x67,
    0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1
};

static uint32_t rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32U - n));
}

static uint32_t ref_sig0(uint32_t x) {
    return rotr32(x, 7) ^ rotr32(x, 18) ^ (x >> 3);
}

static uint32_t ref_sig1(uint32_t x) {
    return rotr32(x, 17) ^ rotr32(x, 19) ^ (x >> 10);
}

static uint32_t ref_sum0(uint32_t x) {
    return rotr32(x, 2) ^ rotr32(x, 13) ^ rotr32(x, 22);
}

static uint32_t ref_sum1(uint32_t x) {
    return rotr32(x, 6) ^ rotr32(x, 11) ^ rotr32(x, 25);
}

static int digest_matches(const uint8_t *got, const uint8_t *expected) {
    for (uint32_t i = 0; i < SHA256_DIGEST_BYTES; ++i) {
        if (got[i] != expected[i]) {
            return 0;
        }
    }
    return 1;
}

static void copy_msg(uint8_t *dst, const char *src, uint32_t len) {
    for (uint32_t i = 0; i < len; ++i) {
        dst[i] = (uint8_t)src[i];
    }
}

static int run_case(const char *name, const char *msg, uint32_t len, const uint8_t *expected) {
    uint8_t buf[128];
    uint8_t digest[SHA256_DIGEST_BYTES];
    for (uint32_t i = 0; i < sizeof(buf); ++i) {
        buf[i] = 0;
    }
    copy_msg(buf, msg, len);
    sha256(buf, len, digest);
    if (!digest_matches(digest, expected)) {
        vx_printf("SHA256 %s mismatch: got=%08x%08x...\n",
                  name,
                  ((uint32_t)digest[0] << 24) | ((uint32_t)digest[1] << 16) | ((uint32_t)digest[2] << 8) | digest[3],
                  ((uint32_t)digest[4] << 24) | ((uint32_t)digest[5] << 16) | ((uint32_t)digest[6] << 8) | digest[7]);
        return 1;
    }
    return 0;
}

static int check_intrinsics(void) {
#ifdef SHA_NATIVE
    int errors = 0;
    const uint32_t values[] = {
        0x00000000U, 0x00000001U, 0x80000000U, 0xa1b2c3d4U, 0xffffffffU
    };
    for (uint32_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        uint32_t x = values[i];
        errors += (__intrin_sha256sig0(x) != ref_sig0(x));
        errors += (__intrin_sha256sig1(x) != ref_sig1(x));
        errors += (__intrin_sha256sum0(x) != ref_sum0(x));
        errors += (__intrin_sha256sum1(x) != ref_sum1(x));
    }
    if (errors) {
        vx_printf("SHA256 intrinsic smoke failed: errors=%d\n", errors);
    }
    return errors;
#else
    return 0;
#endif
}

int main(void) {
    int errors = 0;
    errors += check_intrinsics();
    errors += run_case("empty", "", 0, k_digest_empty);
    errors += run_case("abc", "abc", 3, k_digest_abc);
    errors += run_case("abcdbc",
                       "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
                       56,
                       k_digest_abcdbc);

    if (errors) {
        vx_printf("SHA256 Failed (%d errors)\n", errors);
        return errors;
    }

#ifdef SHA_NATIVE
    vx_printf("SHA256 Passed! mode=NATIVE\n");
#else
    vx_printf("SHA256 Passed! mode=SOFTWARE\n");
#endif
    return 0;
}
