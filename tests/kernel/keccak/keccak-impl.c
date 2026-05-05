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

static uint8_t low_bits_mask(uint32_t n_bits) {
    if (n_bits == 0) {
        return 0;
    }
    if (n_bits >= 8) {
        return 0xff;
    }
    return (uint8_t)((1u << n_bits) - 1u);
}

#ifdef KECCAK_NATIVE
static uint64_t load64_le(const uint8_t *src) {
    uint64_t value = 0;
    for (uint32_t i = 0; i < 8; ++i) {
        value |= (uint64_t)src[i] << (8U * i);
    }
    return value;
}

static uint64_t load64_le_partial(const uint8_t *src, uint32_t n) {
    uint64_t value = 0;
    for (uint32_t i = 0; i < n; ++i) {
        value |= (uint64_t)src[i] << (8U * i);
    }
    return value;
}

static void keccak_xor_byte(uint32_t byte_idx, uint8_t value) {
    uint32_t lane = byte_idx >> 3;
    uint32_t shift = (byte_idx & 7U) * 8U;
    __intrin_keccak_xor_lane((uint64_t)value << shift, lane);
}

static void keccak_native_zero_state(void) {
    for (uint32_t i = 0; i < KECCAK_STATE_LANES; ++i) {
        __intrin_keccak_write_lane(0, i);
    }
}

static void keccak_native_absorb_bytes(const uint8_t *input, uint32_t n) {
    uint32_t lane = 0;
    while (n >= 8U) {
        __intrin_keccak_xor_lane(load64_le(input), lane);
        input += 8;
        n -= 8;
        ++lane;
    }
    if (n != 0U) {
        __intrin_keccak_xor_lane(load64_le_partial(input, n), lane);
    }
}

static void keccak_native_squeeze_bytes(uint8_t *output, uint32_t n) {
    uint32_t lane = 0;
    while (n != 0U) {
        uint64_t value = __intrin_keccak_read_lane(lane);
        uint32_t chunk = n < 8U ? n : 8U;
        for (uint32_t i = 0; i < chunk; ++i) {
            output[i] = (uint8_t)(value >> (8U * i));
        }
        output += chunk;
        n -= chunk;
        ++lane;
    }
}
#endif

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
    keccak_bits(rate,
                capacity,
                input,
                input_byte_len * 8U,
                delimited_suffix,
                output,
                output_byte_len);
}

void keccak_bits(unsigned int rate,
                 unsigned int capacity,
                 const uint8_t *input,
                 uint64_t input_bit_len,
                 uint8_t delimited_suffix,
                 uint8_t *output,
                 uint64_t output_byte_len) {
#ifdef KECCAK_NATIVE
    unsigned int rate_in_bytes = rate / 8U;
    uint64_t input_byte_len = input_bit_len / 8U;
    uint32_t rem_bits = (uint32_t)(input_bit_len & 7U);
    unsigned int block_size = 0;

    if (((rate + capacity) != 1600U) || ((rate % 8U) != 0U)) {
        return;
    }

    keccak_native_zero_state();

    while (input_byte_len >= rate_in_bytes) {
        keccak_native_absorb_bytes(input, rate_in_bytes);
        input += rate_in_bytes;
        input_byte_len -= rate_in_bytes;
        __intrin_keccak_f1600();
    }

    block_size = (unsigned int)input_byte_len;
    if (input_byte_len != 0U) {
        keccak_native_absorb_bytes(input, block_size);
        input += input_byte_len;
    }

    if (rem_bits != 0U) {
        keccak_xor_byte(block_size, input[0] & low_bits_mask(rem_bits));
    }

    {
        uint16_t suffix = (uint16_t)delimited_suffix << rem_bits;
        keccak_xor_byte(block_size, (uint8_t)(suffix & 0xffU));
        if ((((suffix & 0x80U) != 0U) || ((suffix >> 8) != 0U)) &&
            (block_size == (rate_in_bytes - 1U))) {
            __intrin_keccak_f1600();
        }
        if ((suffix >> 8) != 0U) {
            uint32_t suffix_idx = (block_size == (rate_in_bytes - 1U))
                                ? 0U
                                : (block_size + 1U);
            keccak_xor_byte(suffix_idx, (uint8_t)(suffix >> 8));
        }
    }

    keccak_xor_byte(rate_in_bytes - 1U, 0x80U);
    __intrin_keccak_f1600();

    while (output_byte_len > 0) {
        block_size = output_byte_len < rate_in_bytes
                         ? (unsigned int)output_byte_len
                         : rate_in_bytes;
        keccak_native_squeeze_bytes(output, block_size);
        output += block_size;
        output_byte_len -= block_size;

        if (output_byte_len > 0) {
            __intrin_keccak_f1600();
        }
    }
#else
    uint64_t state[KECCAK_STATE_LANES];
    uint8_t *state_bytes = (uint8_t *)state;
    // 对 SHA3-256：
    // rate = 1088 bits
    // rate_in_bytes = 1088 / 8 = 136 bytes
    // 也就是每一轮 absorb 最多吃 136 字节。
    unsigned int rate_in_bytes = rate / 8U;  // rate 必须是 8 的倍数，对于 SHA3-256 来说是 1088 bits，也就是 136 bytes
    unsigned int block_size = 0;
    uint64_t input_byte_len = input_bit_len / 8U;
    // 输入中剩余的非整字节 bit 数，input_bit_len & 7也就是 input_bit_len % 8。
    uint32_t rem_bits = (uint32_t)(input_bit_len & 7U); 

    if (((rate + capacity) != 1600U) || ((rate % 8U) != 0U)) {
        return;
    }

    zero_bytes(state_bytes, sizeof(state));

    // 每次最多吸收 rate_in_bytes 字节；
    // 吸收方式是 XOR：
    //     state_bytes[i] ^= input[i]
    // 如果刚好吸满一个 rate block：
    //     执行 Keccak-f1600 permutation
    while (input_byte_len > 0) {
        block_size = input_byte_len < rate_in_bytes
                         ? (unsigned int)input_byte_len
                         : rate_in_bytes;
        for (unsigned int i = 0; i < block_size; ++i) {
            state_bytes[i] ^= input[i];
        }
        input += block_size; // 输入指针前移 block_size 字节
        input_byte_len -= block_size;

        if (block_size == rate_in_bytes) {
            keccak_f1600_permute(state);
            block_size = 0; // 输入长度刚好等于 rate 时，suffix 加在新 block 开头
        }
    }
    // 吸收最后剩余的非整字节部分。前面 while 已经吸收了完整的 1 个 byte。这里还要吸收下一字节的低 5 bit
    if (rem_bits != 0U) {
        //input[0] 不是原始的第一个 byte，而是剩余 bit 所在的那个 byte。 
        state_bytes[block_size] ^= input[0] & low_bits_mask(rem_bits);
    }
    // 加 delimited_suffix
    {
        // 左移 rem_bits :如果消息不是整字节对齐，比如还有 5 个剩余 bit：rem_bits = 5
        // 那么 suffix 不能从 byte 的 bit0 开始，而要紧跟在这 5 个 bit 后面。所以要：suffix = 0x06 << 5
        // 也就是把 suffix 放到当前 byte 的更高 bit 位置。
        // 左移以后可能超过 8 bit。所以用 uint16_t 来保存这个值，防止溢出。比如 0x06 << 5 = 0xC0，左移后已经占满一个 byte，如果再有剩余 bit 比如 rem_bits=6，那么 suffix 就是 0x06 << 6 = 0x180，已经超过一个 byte，所以需要 uint16_t 来保存。
        uint16_t suffix = (uint16_t)delimited_suffix << rem_bits;
        // 把 suffix 的低 8 bit XOR 到当前 byte。
        state_bytes[block_size] ^= (uint8_t)(suffix & 0xffU);
        // 如果 suffix 已经占用当前 block 的最后一 bit，或者溢出到了下一 byte，
        // 并且当前 byte 是 rate block 的最后一个 byte，需要先开始一个新 block。
        if ((((suffix & 0x80U) != 0U) || ((suffix >> 8) != 0U)) &&
            (block_size == (rate_in_bytes - 1U))) {
            keccak_f1600_permute(state);
        }
        if ((suffix >> 8) != 0U) {
            if (block_size == (rate_in_bytes - 1U)) {
                state_bytes[0] ^= (uint8_t)(suffix >> 8);
            } else {
                state_bytes[block_size + 1U] ^= (uint8_t)(suffix >> 8);
            }
        }
    }

    state_bytes[rate_in_bytes - 1U] ^= 0x80U; // 最后的 padding 规则：在当前 block 的最后一个 byte 的最高 bit 位置添加一个 1。也就是把这个 byte 和 0x80 做 XOR。
    keccak_f1600_permute(state);
    // 从 state_bytes 里每次输出 rate_in_bytes 字节，直到输出完 output_byte_len 字节。
    // 对于 SHA3-256 来说，rate_in_bytes=136，digest_bytes=32，所以只需要输出一次就能得到完整 digest，不需要第二次 permutation。
    while (output_byte_len > 0) {
        block_size = output_byte_len < rate_in_bytes
                         ? (unsigned int)output_byte_len
                         : rate_in_bytes;
        copy_bytes(output, state_bytes, block_size);
        output += block_size; // 输出指针前移 block_size 字节
        output_byte_len -= block_size;

        if (output_byte_len > 0) {
            keccak_f1600_permute(state);
        }
    }
#endif
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

void sha3_256_bits(const uint8_t *input, uint64_t input_bit_len, uint8_t *digest_out) {
    keccak_bits(KECCAK_SHA3_256_RATE_BITS,
                KECCAK_SHA3_256_CAPACITY_BITS,
                input,
                input_bit_len,
                0x06,
                digest_out,
                KECCAK_SHA3_256_DIGEST_BYTES);
}
