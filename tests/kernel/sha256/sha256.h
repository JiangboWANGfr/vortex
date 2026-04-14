#ifndef SHA256_H
#define SHA256_H

#include <stdint.h>

#define SHA256_DIGEST_BYTES 32

void sha256(uint8_t *buf, uint32_t n_bytes, uint8_t *digest_out);

#endif
