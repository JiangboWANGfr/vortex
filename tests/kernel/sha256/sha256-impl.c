#include "sha256.h"

#ifdef SHA_NATIVE
#include <vx_intrinsics.h>
#endif

static const uint32_t K[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

static const uint32_t H0[8] = {
    0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
    0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
};

static uint32_t padded_size_bytes(uint32_t n_bytes) {
    uint32_t mod = n_bytes & 63U;
    uint32_t pad = (mod < 56U) ? (56U - mod) : (120U - mod);
    return n_bytes + pad + 8U;
}

static void pad_message(uint8_t *buf, uint32_t n_bytes) {
    uint32_t total = padded_size_bytes(n_bytes);
    uint8_t *p = buf + n_bytes;
    *p++ = 0x80;
    while (p < buf + total - 8U) {
        *p++ = 0;
    }

    uint32_t n_bits_hi = n_bytes >> 29;
    uint32_t n_bits_lo = n_bytes << 3;
    p[0] = (uint8_t)(n_bits_hi >> 24);
    p[1] = (uint8_t)(n_bits_hi >> 16);
    p[2] = (uint8_t)(n_bits_hi >> 8);
    p[3] = (uint8_t)n_bits_hi;
    p[4] = (uint8_t)(n_bits_lo >> 24);
    p[5] = (uint8_t)(n_bits_lo >> 16);
    p[6] = (uint8_t)(n_bits_lo >> 8);
    p[7] = (uint8_t)n_bits_lo;
}

static uint32_t load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24)
         | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)
         | (uint32_t)p[3];
}

static void store_be32(uint8_t *p, uint32_t x) {
    p[0] = (uint8_t)(x >> 24);
    p[1] = (uint8_t)(x >> 16);
    p[2] = (uint8_t)(x >> 8);
    p[3] = (uint8_t)x;
}

static uint32_t rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32U - n));
}

static uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

static uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

static uint32_t Sigma0(uint32_t x) {
#ifdef SHA_NATIVE
    return __intrin_sha256sum0(x);
#else
    return rotr32(x, 2) ^ rotr32(x, 13) ^ rotr32(x, 22);
#endif
}

static uint32_t Sigma1(uint32_t x) {
#ifdef SHA_NATIVE
    return __intrin_sha256sum1(x);
#else
    return rotr32(x, 6) ^ rotr32(x, 11) ^ rotr32(x, 25);
#endif
}

static uint32_t sigma0(uint32_t x) {
#ifdef SHA_NATIVE
    return __intrin_sha256sig0(x);
#else
    return rotr32(x, 7) ^ rotr32(x, 18) ^ (x >> 3);
#endif
}

static uint32_t sigma1(uint32_t x) {
#ifdef SHA_NATIVE
    return __intrin_sha256sig1(x);
#else
    return rotr32(x, 17) ^ rotr32(x, 19) ^ (x >> 10);
#endif
}

void sha256(uint8_t *buf, uint32_t n_bytes, uint8_t *digest_out) {
    pad_message(buf, n_bytes);
    uint32_t n_blocks = padded_size_bytes(n_bytes) >> 6;
    uint32_t H[8];
    for (uint32_t i = 0; i < 8; ++i) {
        H[i] = H0[i];
    }

    for (uint32_t block = 0; block < n_blocks; ++block) {
        uint32_t W[64];
        const uint8_t *M = buf + (block << 6);
        for (uint32_t t = 0; t < 16; ++t) {
            W[t] = load_be32(M + (t << 2));
        }
        for (uint32_t t = 16; t < 64; ++t) {
            W[t] = sigma1(W[t - 2]) + W[t - 7] + sigma0(W[t - 15]) + W[t - 16];
        }

        uint32_t a = H[0];
        uint32_t b = H[1];
        uint32_t c = H[2];
        uint32_t d = H[3];
        uint32_t e = H[4];
        uint32_t f = H[5];
        uint32_t g = H[6];
        uint32_t h = H[7];

        for (uint32_t t = 0; t < 64; ++t) {
            uint32_t T1 = h + Sigma1(e) + ch(e, f, g) + K[t] + W[t];
            uint32_t T2 = Sigma0(a) + maj(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + T1;
            d = c;
            c = b;
            b = a;
            a = T1 + T2;
        }

        H[0] += a;
        H[1] += b;
        H[2] += c;
        H[3] += d;
        H[4] += e;
        H[5] += f;
        H[6] += g;
        H[7] += h;
    }

    for (uint32_t i = 0; i < 8; ++i) {
        store_be32(digest_out + (i << 2), H[i]);
    }
}
