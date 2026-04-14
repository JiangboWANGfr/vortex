#include "sha256.h"

#ifdef SHA_NATIVE
#include <vx_intrinsics.h>
#endif

/*
 * SHA-256 uses 64 fixed 32-bit constants, one for each compression round.
 * They are part of the SHA-256 specification and are mixed with the message
 * schedule W[t] in the T1 calculation.
 */
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

/*
 * Initial hash value H0[0..7].  During hashing the working variables
 * a..h start from the current H state, process one 64-byte block, and then
 * are added back into H.  After all blocks, H[0..7] is the final digest.
 */
static const uint32_t H0[8] = {
    0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
    0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
};

static uint32_t padded_size_bytes(uint32_t n_bytes) {
    /*
     * SHA-256 pads the message so that the last 8 bytes of the final block can
     * store the original message length in bits.  Therefore, before those 8
     * bytes, the padded message must end at byte offset 56 inside a 64-byte
     * block.
     *
     * n_bytes & 63U is the same as n_bytes % 64 because 64 is 2^6.
     * If the current block has fewer than 56 bytes, stay in this block.
     * Otherwise, add enough padding to move to byte 56 of the next block.
     */
    uint32_t mod = n_bytes & 63U;
    uint32_t pad = (mod < 56U) ? (56U - mod) : (120U - mod);
    return n_bytes + pad + 8U;
}

static void pad_message(uint8_t *buf, uint32_t n_bytes) {
    /*
     * Padding format:
     *   original message || 0x80 || zero bytes || 64-bit big-endian bit length
     *
     * 0x80 is binary 10000000, so it appends the required single '1' bit and
     * starts the following zero padding.  This function writes into buf
     * in-place, so the caller must provide enough space for the padded message.
     */
    uint32_t total = padded_size_bytes(n_bytes);
    uint8_t *p = buf + n_bytes;
    *p++ = 0x80;
    while (p < buf + total - 8U) {
        *p++ = 0;
    }

    uint32_t n_bits_hi = n_bytes >> 29;
    uint32_t n_bits_lo = n_bytes << 3;
    /*
     * n_bytes * 8 is the message length in bits.  The code writes it as a
     * 64-bit big-endian value using two 32-bit halves:
     *   high half = n_bytes >> 29
     *   low half  = n_bytes << 3
     */
    p[0] = (uint8_t)(n_bits_hi >> 24);
    p[1] = (uint8_t)(n_bits_hi >> 16);
    p[2] = (uint8_t)(n_bits_hi >> 8);
    p[3] = (uint8_t)n_bits_hi;
    p[4] = (uint8_t)(n_bits_lo >> 24);
    p[5] = (uint8_t)(n_bits_lo >> 16);
    p[6] = (uint8_t)(n_bits_lo >> 8);
    p[7] = (uint8_t)n_bits_lo;
}

/* Read four bytes as one 32-bit word in big-endian byte order. */
static uint32_t load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24)
         | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)
         | (uint32_t)p[3];
}

/* Write one 32-bit word as four bytes in big-endian byte order. */
static void store_be32(uint8_t *p, uint32_t x) {
    p[0] = (uint8_t)(x >> 24);
    p[1] = (uint8_t)(x >> 16);
    p[2] = (uint8_t)(x >> 8);
    p[3] = (uint8_t)x;
}

/* Rotate right by n bits.  SHA-256 uses rotate, not only logical shift. */
static uint32_t rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32U - n));
}

static uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
    /* Choose: for each bit, pick y when x is 1, otherwise pick z. */
    return (x & y) ^ (~x & z);
}

static uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
    /* Majority: for each bit, output the bit value held by at least two inputs. */
    return (x & y) ^ (x & z) ^ (y & z);
}

static uint32_t Sigma0(uint32_t x) {
    /*
     * Upper-case Sigma functions are used in the 64 compression rounds.
     * With SHA_NATIVE, Vortex SHA intrinsics replace the software rotates.
     */
#ifdef SHA_NATIVE
    return __intrin_sha256sum0(x);
#else
    return rotr32(x, 2) ^ rotr32(x, 13) ^ rotr32(x, 22);
#endif
}

static uint32_t Sigma1(uint32_t x) {
    /* Upper-case Sigma1 used by T1 in the compression round. */
#ifdef SHA_NATIVE
    return __intrin_sha256sum1(x);
#else
    return rotr32(x, 6) ^ rotr32(x, 11) ^ rotr32(x, 25);
#endif
}

static uint32_t sigma0(uint32_t x) {
    /*
     * Lower-case sigma functions are used only to expand the message schedule
     * from W[0..15] to W[16..63].
     */
#ifdef SHA_NATIVE
    return __intrin_sha256sig0(x);
#else
    return rotr32(x, 7) ^ rotr32(x, 18) ^ (x >> 3);
#endif
}

static uint32_t sigma1(uint32_t x) {
    /* Lower-case sigma1 used by the message schedule recurrence. */
#ifdef SHA_NATIVE
    return __intrin_sha256sig1(x);
#else
    return rotr32(x, 17) ^ rotr32(x, 19) ^ (x >> 10);
#endif
}

void sha256(uint8_t *buf, uint32_t n_bytes, uint8_t *digest_out) {
    /*
     * After padding, the total byte count is always a multiple of 64.  SHA-256
     * processes one 64-byte block at a time, so the number of blocks is:
     *
     *   padded_size_bytes(n_bytes) / 64
     *
     * The code uses >> 6 instead of / 64 because 64 == 2^6.  For unsigned
     * integers this is an exact division here because padded_size_bytes()
     * returns a multiple of 64.
     */
    pad_message(buf, n_bytes);
    uint32_t n_blocks = padded_size_bytes(n_bytes) >> 6;

    /*
     * H is the current 256-bit hash state stored as eight 32-bit words.
     * It starts from the SHA-256 initial constants, then each message block
     * updates it by the compression function below.
     */
    uint32_t H[8];
    for (uint32_t i = 0; i < 8; ++i) {
        H[i] = H0[i];
    }

    for (uint32_t block = 0; block < n_blocks; ++block) {
        uint32_t W[64];

        /*
         * M points to the start of the current 64-byte block inside buf.
         *
         * block << 6 means block * 64:
         *   block 0 -> buf + 0
         *   block 1 -> buf + 64
         *   block 2 -> buf + 128
         *
         * Pointer arithmetic is byte-based here because buf/M are uint8_t *.
         */
        const uint8_t *M = buf + (block << 6);

        /*
         * Split the 64-byte block into the first 16 words of the message
         * schedule.  Each W[t] is 4 bytes, so t << 2 means t * 4:
         *   W[0]  reads M + 0
         *   W[1]  reads M + 4
         *   ...
         *   W[15] reads M + 60
         *
         * SHA-256 defines these words in big-endian order, so load_be32()
         * combines bytes as p[0]<<24 | p[1]<<16 | p[2]<<8 | p[3].
         */
        for (uint32_t t = 0; t < 16; ++t) {
            W[t] = load_be32(M + (t << 2));
        }

        /*
         * Expand 16 input words into the full 64-word message schedule.
         * All additions are modulo 2^32 because the variables are uint32_t.
         */
        for (uint32_t t = 16; t < 64; ++t) {
            W[t] = sigma1(W[t - 2]) + W[t - 7] + sigma0(W[t - 15]) + W[t - 16];
        }

        /*
         * Initialize the 8 working variables from the current hash state.
         * The compression loop mutates a..h for this block only; after the
         * 64 rounds, those values are added back into H.
         */
        uint32_t a = H[0];
        uint32_t b = H[1];
        uint32_t c = H[2];
        uint32_t d = H[3];
        uint32_t e = H[4];
        uint32_t f = H[5];
        uint32_t g = H[6];
        uint32_t h = H[7];

        /*
         * Compression function: 64 rounds per block.
         *
         * T1 mixes the previous h, the e/f/g choose function, one round
         * constant K[t], and one schedule word W[t].
         *
         * T2 mixes a/b/c through the majority function.  The assignments below
         * shift the pipeline of working variables and inject the new values at
         * a and e, matching the SHA-256 round definition.
         */
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

        /*
         * Feed-forward step.  The compressed block result is added into the
         * previous hash state, producing the state for the next block.
         */
        H[0] += a;
        H[1] += b;
        H[2] += c;
        H[3] += d;
        H[4] += e;
        H[5] += f;
        H[6] += g;
        H[7] += h;
    }

    /*
     * Convert the final eight 32-bit H words into the 32-byte digest.
     *
     * Each H[i] contributes 4 bytes, so i << 2 means i * 4:
     *   H[0] -> digest_out[0..3]
     *   H[1] -> digest_out[4..7]
     *   ...
     *   H[7] -> digest_out[28..31]
     *
     * SHA-256 outputs the digest in big-endian order, so store_be32() writes
     * the most significant byte first.
     */
    for (uint32_t i = 0; i < 8; ++i) {
        store_be32(digest_out + (i << 2), H[i]);
    }
}
