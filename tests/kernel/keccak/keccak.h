#ifndef KECCAK_H
#define KECCAK_H

#include <stdint.h>

#define KECCAK_SHA3_256_RATE_BITS 1088
#define KECCAK_SHA3_256_CAPACITY_BITS 512
#define KECCAK_SHA3_256_DIGEST_BYTES 32

#ifdef __cplusplus
extern "C" {
#endif

void keccak(unsigned int rate,
            unsigned int capacity,
            const uint8_t *input,
            uint64_t input_byte_len,
            uint8_t delimited_suffix,
            uint8_t *output,
            uint64_t output_byte_len);

void sha3_256(const uint8_t *input, uint64_t input_byte_len, uint8_t *digest_out);

#ifdef __cplusplus
}
#endif

#endif
