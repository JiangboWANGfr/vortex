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

#define SHA256_ROUND(a, b, c, d, e, f, g, h, k, w) do { \
    uint32_t t1 = (h) + Sigma1(e) + ch((e), (f), (g)) + (k) + (w); \
    uint32_t t2 = Sigma0(a) + maj((a), (b), (c)); \
    (d) += t1; \
    (h) = t1 + t2; \
} while (0)

#define SHA256_SCHED(w0, w1, w9, w14) \
    ((w0) += sigma1(w14) + (w9) + sigma0(w1))

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

        uint32_t w0  = load_be32(M + 0);
        uint32_t w1  = load_be32(M + 4);
        uint32_t w2  = load_be32(M + 8);
        uint32_t w3  = load_be32(M + 12);
        uint32_t w4  = load_be32(M + 16);
        uint32_t w5  = load_be32(M + 20);
        uint32_t w6  = load_be32(M + 24);
        uint32_t w7  = load_be32(M + 28);
        uint32_t w8  = load_be32(M + 32);
        uint32_t w9  = load_be32(M + 36);
        uint32_t w10 = load_be32(M + 40);
        uint32_t w11 = load_be32(M + 44);
        uint32_t w12 = load_be32(M + 48);
        uint32_t w13 = load_be32(M + 52);
        uint32_t w14 = load_be32(M + 56);
        uint32_t w15 = load_be32(M + 60);

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
         * Compression function: 64 rounds per block.  Keep the 16-word
         * schedule in scalars so the compiler can avoid a stack-resident W[64]
         * array and loop index arithmetic in the hot path.
         */
        SHA256_ROUND(a, b, c, d, e, f, g, h, K[0],  w0);
        SHA256_ROUND(h, a, b, c, d, e, f, g, K[1],  w1);
        SHA256_ROUND(g, h, a, b, c, d, e, f, K[2],  w2);
        SHA256_ROUND(f, g, h, a, b, c, d, e, K[3],  w3);
        SHA256_ROUND(e, f, g, h, a, b, c, d, K[4],  w4);
        SHA256_ROUND(d, e, f, g, h, a, b, c, K[5],  w5);
        SHA256_ROUND(c, d, e, f, g, h, a, b, K[6],  w6);
        SHA256_ROUND(b, c, d, e, f, g, h, a, K[7],  w7);
        SHA256_ROUND(a, b, c, d, e, f, g, h, K[8],  w8);
        SHA256_ROUND(h, a, b, c, d, e, f, g, K[9],  w9);
        SHA256_ROUND(g, h, a, b, c, d, e, f, K[10], w10);
        SHA256_ROUND(f, g, h, a, b, c, d, e, K[11], w11);
        SHA256_ROUND(e, f, g, h, a, b, c, d, K[12], w12);
        SHA256_ROUND(d, e, f, g, h, a, b, c, K[13], w13);
        SHA256_ROUND(c, d, e, f, g, h, a, b, K[14], w14);
        SHA256_ROUND(b, c, d, e, f, g, h, a, K[15], w15);

        SHA256_ROUND(a, b, c, d, e, f, g, h, K[16], SHA256_SCHED(w0,  w1,  w9,  w14));
        SHA256_ROUND(h, a, b, c, d, e, f, g, K[17], SHA256_SCHED(w1,  w2,  w10, w15));
        SHA256_ROUND(g, h, a, b, c, d, e, f, K[18], SHA256_SCHED(w2,  w3,  w11, w0));
        SHA256_ROUND(f, g, h, a, b, c, d, e, K[19], SHA256_SCHED(w3,  w4,  w12, w1));
        SHA256_ROUND(e, f, g, h, a, b, c, d, K[20], SHA256_SCHED(w4,  w5,  w13, w2));
        SHA256_ROUND(d, e, f, g, h, a, b, c, K[21], SHA256_SCHED(w5,  w6,  w14, w3));
        SHA256_ROUND(c, d, e, f, g, h, a, b, K[22], SHA256_SCHED(w6,  w7,  w15, w4));
        SHA256_ROUND(b, c, d, e, f, g, h, a, K[23], SHA256_SCHED(w7,  w8,  w0,  w5));
        SHA256_ROUND(a, b, c, d, e, f, g, h, K[24], SHA256_SCHED(w8,  w9,  w1,  w6));
        SHA256_ROUND(h, a, b, c, d, e, f, g, K[25], SHA256_SCHED(w9,  w10, w2,  w7));
        SHA256_ROUND(g, h, a, b, c, d, e, f, K[26], SHA256_SCHED(w10, w11, w3,  w8));
        SHA256_ROUND(f, g, h, a, b, c, d, e, K[27], SHA256_SCHED(w11, w12, w4,  w9));
        SHA256_ROUND(e, f, g, h, a, b, c, d, K[28], SHA256_SCHED(w12, w13, w5,  w10));
        SHA256_ROUND(d, e, f, g, h, a, b, c, K[29], SHA256_SCHED(w13, w14, w6,  w11));
        SHA256_ROUND(c, d, e, f, g, h, a, b, K[30], SHA256_SCHED(w14, w15, w7,  w12));
        SHA256_ROUND(b, c, d, e, f, g, h, a, K[31], SHA256_SCHED(w15, w0,  w8,  w13));

        SHA256_ROUND(a, b, c, d, e, f, g, h, K[32], SHA256_SCHED(w0,  w1,  w9,  w14));
        SHA256_ROUND(h, a, b, c, d, e, f, g, K[33], SHA256_SCHED(w1,  w2,  w10, w15));
        SHA256_ROUND(g, h, a, b, c, d, e, f, K[34], SHA256_SCHED(w2,  w3,  w11, w0));
        SHA256_ROUND(f, g, h, a, b, c, d, e, K[35], SHA256_SCHED(w3,  w4,  w12, w1));
        SHA256_ROUND(e, f, g, h, a, b, c, d, K[36], SHA256_SCHED(w4,  w5,  w13, w2));
        SHA256_ROUND(d, e, f, g, h, a, b, c, K[37], SHA256_SCHED(w5,  w6,  w14, w3));
        SHA256_ROUND(c, d, e, f, g, h, a, b, K[38], SHA256_SCHED(w6,  w7,  w15, w4));
        SHA256_ROUND(b, c, d, e, f, g, h, a, K[39], SHA256_SCHED(w7,  w8,  w0,  w5));
        SHA256_ROUND(a, b, c, d, e, f, g, h, K[40], SHA256_SCHED(w8,  w9,  w1,  w6));
        SHA256_ROUND(h, a, b, c, d, e, f, g, K[41], SHA256_SCHED(w9,  w10, w2,  w7));
        SHA256_ROUND(g, h, a, b, c, d, e, f, K[42], SHA256_SCHED(w10, w11, w3,  w8));
        SHA256_ROUND(f, g, h, a, b, c, d, e, K[43], SHA256_SCHED(w11, w12, w4,  w9));
        SHA256_ROUND(e, f, g, h, a, b, c, d, K[44], SHA256_SCHED(w12, w13, w5,  w10));
        SHA256_ROUND(d, e, f, g, h, a, b, c, K[45], SHA256_SCHED(w13, w14, w6,  w11));
        SHA256_ROUND(c, d, e, f, g, h, a, b, K[46], SHA256_SCHED(w14, w15, w7,  w12));
        SHA256_ROUND(b, c, d, e, f, g, h, a, K[47], SHA256_SCHED(w15, w0,  w8,  w13));

        SHA256_ROUND(a, b, c, d, e, f, g, h, K[48], SHA256_SCHED(w0,  w1,  w9,  w14));
        SHA256_ROUND(h, a, b, c, d, e, f, g, K[49], SHA256_SCHED(w1,  w2,  w10, w15));
        SHA256_ROUND(g, h, a, b, c, d, e, f, K[50], SHA256_SCHED(w2,  w3,  w11, w0));
        SHA256_ROUND(f, g, h, a, b, c, d, e, K[51], SHA256_SCHED(w3,  w4,  w12, w1));
        SHA256_ROUND(e, f, g, h, a, b, c, d, K[52], SHA256_SCHED(w4,  w5,  w13, w2));
        SHA256_ROUND(d, e, f, g, h, a, b, c, K[53], SHA256_SCHED(w5,  w6,  w14, w3));
        SHA256_ROUND(c, d, e, f, g, h, a, b, K[54], SHA256_SCHED(w6,  w7,  w15, w4));
        SHA256_ROUND(b, c, d, e, f, g, h, a, K[55], SHA256_SCHED(w7,  w8,  w0,  w5));
        SHA256_ROUND(a, b, c, d, e, f, g, h, K[56], SHA256_SCHED(w8,  w9,  w1,  w6));
        SHA256_ROUND(h, a, b, c, d, e, f, g, K[57], SHA256_SCHED(w9,  w10, w2,  w7));
        SHA256_ROUND(g, h, a, b, c, d, e, f, K[58], SHA256_SCHED(w10, w11, w3,  w8));
        SHA256_ROUND(f, g, h, a, b, c, d, e, K[59], SHA256_SCHED(w11, w12, w4,  w9));
        SHA256_ROUND(e, f, g, h, a, b, c, d, K[60], SHA256_SCHED(w12, w13, w5,  w10));
        SHA256_ROUND(d, e, f, g, h, a, b, c, K[61], SHA256_SCHED(w13, w14, w6,  w11));
        SHA256_ROUND(c, d, e, f, g, h, a, b, K[62], SHA256_SCHED(w14, w15, w7,  w12));
        SHA256_ROUND(b, c, d, e, f, g, h, a, K[63], SHA256_SCHED(w15, w0,  w8,  w13));

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
