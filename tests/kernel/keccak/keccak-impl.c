#include "keccak.h"

#ifdef KECCAK_NATIVE
#include <vx_intrinsics.h>
#endif

#define KECCAK_STATE_LANES 25
#define KECCAK_ROUNDS 24

static const uint64_t k_round_constants[KECCAK_ROUNDS] = {
    0x0000000000000001ULL, 0x0000000000008082ULL,
    0x800000000000808aULL, 0x8000000080008000ULL,
    0x000000000000808bULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008aULL, 0x0000000000000088ULL,
    0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL,
    0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL,
    0x8000000080008081ULL, 0x8000000000008080ULL,
    0x0000000080000001ULL, 0x8000000080008008ULL,
};

static uint64_t rol64(uint64_t x, unsigned int offset) {
    return (x << offset) | (x >> (64U - offset));
}

static uint64_t and_not(uint64_t x, uint64_t y) {
    return (~x) & y;
}

static void zero_bytes(uint8_t *dst, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        dst[i] = 0;
    }
}

static void copy_bytes(uint8_t *dst, const uint8_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        dst[i] = src[i];
    }
}

#ifdef KECCAK_NATIVE
static void keccak_f1600_permute(uint64_t *s) {
    for (uint32_t i = 0; i < KECCAK_STATE_LANES; ++i) {
        __intrin_keccak_write_lane(s[i], i);
    }

    __intrin_keccak_f1600();

    for (uint32_t i = 0; i < KECCAK_STATE_LANES; ++i) {
        s[i] = __intrin_keccak_read_lane(i);
    }
}
#else
static void keccak_f1600_permute(uint64_t *s) {
    for (uint32_t round = 0; round < KECCAK_ROUNDS; ++round) {
        uint64_t c0 = s[0] ^ s[5] ^ s[10] ^ s[15] ^ s[20];
        uint64_t c1 = s[1] ^ s[6] ^ s[11] ^ s[16] ^ s[21];
        uint64_t c3 = s[4] ^ s[9] ^ s[14] ^ s[19] ^ s[24];

        uint64_t c2 = rol64(c1, 1) ^ c3;
        s[0] ^= c2;
        s[5] ^= c2;
        s[10] ^= c2;
        s[15] ^= c2;
        s[20] ^= c2;

        c2 = s[2] ^ s[7] ^ s[12] ^ s[17] ^ s[22];

        c3 = rol64(c3, 1) ^ c2;
        c2 = rol64(c2, 1) ^ c0;

        s[1] ^= c2;
        s[6] ^= c2;
        s[11] ^= c2;
        s[16] ^= c2;
        s[21] ^= c2;

        c2 = s[3] ^ s[8] ^ s[13] ^ s[18] ^ s[23];

        c0 = rol64(c0, 1) ^ c2;
        c2 = rol64(c2, 1) ^ c1;

        s[4] ^= c0;
        s[9] ^= c0;
        s[14] ^= c0;
        s[19] ^= c0;
        s[24] ^= c0;

        s[3] ^= c3;
        s[8] ^= c3;
        s[13] ^= c3;
        s[18] ^= c3;
        s[23] ^= c3;

        s[2] ^= c2;
        s[7] ^= c2;
        s[12] ^= c2;
        s[17] ^= c2;
        s[22] ^= c2;

        c1 = s[5];
        s[5] = rol64(s[3], 28);
        s[3] = rol64(s[18], 21);
        s[18] = rol64(s[17], 15);
        s[17] = rol64(s[11], 10);
        s[11] = rol64(s[7], 6);
        s[7] = rol64(s[10], 3);
        s[10] = rol64(s[1], 1);
        s[1] = rol64(s[6], 44);
        s[6] = rol64(s[9], 20);
        s[9] = rol64(s[22], 61);
        s[22] = rol64(s[14], 39);
        s[14] = rol64(s[20], 18);
        s[20] = rol64(s[2], 62);
        s[2] = rol64(s[12], 43);
        s[12] = rol64(s[13], 25);
        s[13] = rol64(s[19], 8);
        s[19] = rol64(s[23], 56);
        s[23] = rol64(s[15], 41);
        s[15] = rol64(s[4], 27);
        s[4] = rol64(s[24], 14);
        s[24] = rol64(s[21], 2);
        s[21] = rol64(s[8], 55);
        s[8] = rol64(s[16], 45);
        s[16] = rol64(c1, 36);

        c0 = (~s[3]) & s[4];
        s[4] ^= and_not(s[0], s[1]);
        s[1] ^= and_not(s[2], s[3]);
        s[3] ^= and_not(s[4], s[0]);
        s[0] ^= and_not(s[1], s[2]);
        s[2] ^= c0;

        c0 = (~s[8]) & s[9];
        s[9] ^= and_not(s[5], s[6]);
        s[6] ^= and_not(s[7], s[8]);
        s[8] ^= and_not(s[9], s[5]);
        s[5] ^= and_not(s[6], s[7]);
        s[7] ^= c0;

        c0 = (~s[13]) & s[14];
        s[14] ^= and_not(s[10], s[11]);
        s[11] ^= and_not(s[12], s[13]);
        s[13] ^= and_not(s[14], s[10]);
        s[10] ^= and_not(s[11], s[12]);
        s[12] ^= c0;

        c0 = (~s[18]) & s[19];
        s[19] ^= and_not(s[15], s[16]);
        s[16] ^= and_not(s[17], s[18]);
        s[18] ^= and_not(s[19], s[15]);
        s[15] ^= and_not(s[16], s[17]);
        s[17] ^= c0;

        c0 = (~s[23]) & s[24];
        s[24] ^= and_not(s[20], s[21]);
        s[21] ^= and_not(s[22], s[23]);
        s[23] ^= and_not(s[24], s[20]);
        s[20] ^= and_not(s[21], s[22]);
        s[22] ^= c0;

        s[0] ^= k_round_constants[round];
    }
}
#endif

void keccak(unsigned int rate,
            unsigned int capacity,
            const uint8_t *input,
            uint64_t input_byte_len,
            uint8_t delimited_suffix,
            uint8_t *output,
            uint64_t output_byte_len) {
    uint64_t state[KECCAK_STATE_LANES];
    uint8_t *state_bytes = (uint8_t *)state;
    unsigned int rate_in_bytes = rate / 8U;
    unsigned int block_size = 0;

    if (((rate + capacity) != 1600U) || ((rate % 8U) != 0U)) {
        return;
    }

    zero_bytes(state_bytes, sizeof(state));

    while (input_byte_len > 0) {
        block_size = input_byte_len < rate_in_bytes
                         ? (unsigned int)input_byte_len
                         : rate_in_bytes;
        for (unsigned int i = 0; i < block_size; ++i) {
            state_bytes[i] ^= input[i];
        }
        input += block_size;
        input_byte_len -= block_size;

        if (block_size == rate_in_bytes) {
            keccak_f1600_permute(state);
            block_size = 0;
        }
    }

    state_bytes[block_size] ^= delimited_suffix;
    if (((delimited_suffix & 0x80U) != 0U) && (block_size == (rate_in_bytes - 1U))) {
        keccak_f1600_permute(state);
    }
    state_bytes[rate_in_bytes - 1U] ^= 0x80U;
    keccak_f1600_permute(state);

    while (output_byte_len > 0) {
        block_size = output_byte_len < rate_in_bytes
                         ? (unsigned int)output_byte_len
                         : rate_in_bytes;
        copy_bytes(output, state_bytes, block_size);
        output += block_size;
        output_byte_len -= block_size;

        if (output_byte_len > 0) {
            keccak_f1600_permute(state);
        }
    }
}

void sha3_256(const uint8_t *input, uint64_t input_byte_len, uint8_t *digest_out) {
    keccak(KECCAK_SHA3_256_RATE_BITS,
           KECCAK_SHA3_256_CAPACITY_BITS,
           input,
           input_byte_len,
           0x06,
           digest_out,
           KECCAK_SHA3_256_DIGEST_BYTES);
}
