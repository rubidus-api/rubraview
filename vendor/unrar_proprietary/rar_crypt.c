/*
 * rar_crypt — converted to C from UnRAR 7.3.1 (crypt3.cpp, crypt5.cpp,
 * rijndael.cpp, sha1.cpp, sha256.cpp) by RARLAB. See rar_crypt.h.
 *
 * UnRAR source code may be used in any software to handle RAR archives
 * without limitations free of charge, but cannot be used to develop RAR
 * (WinRAR) compatible archiver and to re-create RAR compression algorithm,
 * which is proprietary. Distribution of modified UnRAR source code in
 * separate form or as a part of other software is permitted, provided that
 * full text of this paragraph, starting from "UnRAR source code" words, is
 * included in license, or in documentation if license is not available,
 * and in source code comments of resulting package.
 *
 * Differences from the original: C, no AES-NI or Neon paths (the generic
 * table code only), no encryption, no key cache (the caller keeps keys).
 */
#include "rar_crypt.h"
#include <string.h>

static uint32_t rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
static uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
static uint32_t get_be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static void put_be32(uint32_t v, uint8_t *p) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static void put_le32(uint32_t v, uint8_t *p) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

void rar_wipe(void *p, size_t n) {
    volatile uint8_t *v = (volatile uint8_t*)p;
    while (n--) *v++ = 0;
}

/* ---- rijndael.cpp ---- */

static const uint8_t S[256] = {
     99, 124, 119, 123, 242, 107, 111, 197,  48,   1, 103,  43, 254, 215, 171, 118,
    202, 130, 201, 125, 250,  89,  71, 240, 173, 212, 162, 175, 156, 164, 114, 192,
    183, 253, 147,  38,  54,  63, 247, 204,  52, 165, 229, 241, 113, 216,  49,  21,
      4, 199,  35, 195,  24, 150,   5, 154,   7,  18, 128, 226, 235,  39, 178, 117,
      9, 131,  44,  26,  27, 110,  90, 160,  82,  59, 214, 179,  41, 227,  47, 132,
     83, 209,   0, 237,  32, 252, 177,  91, 106, 203, 190,  57,  74,  76,  88, 207,
    208, 239, 170, 251,  67,  77,  51, 133,  69, 249,   2, 127,  80,  60, 159, 168,
     81, 163,  64, 143, 146, 157,  56, 245, 188, 182, 218,  33,  16, 255, 243, 210,
    205,  12,  19, 236,  95, 151,  68,  23, 196, 167, 126,  61, 100,  93,  25, 115,
     96, 129,  79, 220,  34,  42, 144, 136,  70, 238, 184,  20, 222,  94,  11, 219,
    224,  50,  58,  10,  73,   6,  36,  92, 194, 211, 172,  98, 145, 149, 228, 121,
    231, 200,  55, 109, 141, 213,  78, 169, 108,  86, 244, 234, 101, 122, 174,   8,
    186, 120,  37,  46,  28, 166, 180, 198, 232, 221, 116,  31,  75, 189, 139, 138,
    112,  62, 181, 102,  72,   3, 246,  14,  97,  53,  87, 185, 134, 193,  29, 158,
    225, 248, 152,  17, 105, 217, 142, 148, 155,  30, 135, 233, 206,  85,  40, 223,
    140, 161, 137,  13, 191, 230,  66, 104,  65, 153,  45,  15, 176,  84, 187,  22,
};

static const uint8_t rcon[] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36 };

static uint8_t S5[256];
static uint8_t T5[256][4], T6[256][4], T7[256][4], T8[256][4];
static uint8_t U1[256][4], U2[256][4], U3[256][4], U4[256][4];
static bool tables_ready;

static uint8_t gmul(uint8_t a, uint8_t b) {
    const uint8_t poly = 0x1b;
    uint8_t result = 0;
    while (b > 0) {
        if (b & 1) result ^= a;
        a = (uint8_t)((a & 0x80) ? (a << 1) ^ poly : a << 1);
        b >>= 1;
    }
    return result;
}

/* Only the decryption tables: the encryption ones are never used here.
   Idempotent, so two threads that build them at once build the same bytes. */
static void generate_tables(void) {
    if (tables_ready) return;
    for (int i = 0; i < 256; i++) S5[S[i]] = (uint8_t)i;
    for (int i = 0; i < 256; i++) {
        uint8_t b = S5[i];
        U1[b][3] = U2[b][0] = U3[b][1] = U4[b][2] = T5[i][3] = T6[i][0] = T7[i][1] = T8[i][2] = gmul(b, 0xb);
        U1[b][1] = U2[b][2] = U3[b][3] = U4[b][0] = T5[i][1] = T6[i][2] = T7[i][3] = T8[i][0] = gmul(b, 0x9);
        U1[b][2] = U2[b][3] = U3[b][0] = U4[b][1] = T5[i][2] = T6[i][3] = T7[i][0] = T8[i][1] = gmul(b, 0xd);
        U1[b][0] = U2[b][1] = U3[b][2] = U4[b][3] = T5[i][0] = T6[i][1] = T7[i][2] = T8[i][3] = gmul(b, 0xe);
    }
    tables_ready = true;
}

static void key_sched(rar_aes_t *aes, uint8_t key[8][4]) {
    int j, rconpointer = 0;
    int key_columns = aes->rounds - 6;
    uint8_t temp[8][4];
    memcpy(temp, key, sizeof(temp));
    int r = 0, t = 0;
    for (j = 0; (j < key_columns) && (r <= aes->rounds);) {
        for (; (j < key_columns) && (t < 4); j++, t++)
            for (int k = 0; k < 4; k++) aes->key[r][t][k] = temp[j][k];
        if (t == 4) { r++; t = 0; }
    }
    while (r <= aes->rounds) {
        temp[0][0] ^= S[temp[key_columns - 1][1]];
        temp[0][1] ^= S[temp[key_columns - 1][2]];
        temp[0][2] ^= S[temp[key_columns - 1][3]];
        temp[0][3] ^= S[temp[key_columns - 1][0]];
        temp[0][0] ^= rcon[rconpointer++];
        if (key_columns != 8) {
            for (j = 1; j < key_columns; j++)
                for (int k = 0; k < 4; k++) temp[j][k] ^= temp[j - 1][k];
        } else {
            for (j = 1; j < key_columns / 2; j++)
                for (int k = 0; k < 4; k++) temp[j][k] ^= temp[j - 1][k];
            temp[key_columns / 2][0] ^= S[temp[key_columns / 2 - 1][0]];
            temp[key_columns / 2][1] ^= S[temp[key_columns / 2 - 1][1]];
            temp[key_columns / 2][2] ^= S[temp[key_columns / 2 - 1][2]];
            temp[key_columns / 2][3] ^= S[temp[key_columns / 2 - 1][3]];
            for (j = key_columns / 2 + 1; j < key_columns; j++)
                for (int k = 0; k < 4; k++) temp[j][k] ^= temp[j - 1][k];
        }
        for (j = 0; (j < key_columns) && (r <= aes->rounds);) {
            for (; (j < key_columns) && (t < 4); j++, t++)
                for (int k = 0; k < 4; k++) aes->key[r][t][k] = temp[j][k];
            if (t == 4) { r++; t = 0; }
        }
    }
    rar_wipe(temp, sizeof(temp));
}

static void key_enc_to_dec(rar_aes_t *aes) {
    for (int r = 1; r < aes->rounds; r++) {
        uint8_t n[4][4];
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                const uint8_t *w = aes->key[r][j];
                n[j][i] = U1[w[0]][i] ^ U2[w[1]][i] ^ U3[w[2]][i] ^ U4[w[3]][i];
            }
        memcpy(aes->key[r], n, sizeof(aes->key[0]));
    }
}

void rar_aes_init(rar_aes_t *aes, const uint8_t *key, unsigned key_bits, const uint8_t iv[16]) {
    generate_tables();
    memset(aes, 0, sizeof(*aes));
    unsigned bytes = key_bits == 256 ? 32u : key_bits == 192 ? 24u : 16u;
    aes->rounds = key_bits == 256 ? 14 : key_bits == 192 ? 12 : 10;
    uint8_t matrix[8][4];
    memset(matrix, 0, sizeof(matrix));
    for (unsigned i = 0; i < bytes; i++) matrix[i >> 2][i & 3] = key[i];
    if (iv) memcpy(aes->iv, iv, 16);
    key_sched(aes, matrix);
    key_enc_to_dec(aes);
    rar_wipe(matrix, sizeof(matrix));
}

static void xor4(uint8_t *d, const uint8_t *a, const uint8_t *b, const uint8_t *c, const uint8_t *e) {
    for (int i = 0; i < 4; i++) d[i] = a[i] ^ b[i] ^ c[i] ^ e[i];
}

void rar_aes_decrypt(rar_aes_t *aes, uint8_t *data, size_t size) {
    uint8_t block[16], iv[16];
    memcpy(iv, aes->iv, 16);
    for (size_t n = size / 16; n > 0; n--, data += 16) {
        uint8_t temp[4][4];
        for (int i = 0; i < 16; i++) ((uint8_t*)temp)[i] = data[i] ^ ((const uint8_t*)aes->key[aes->rounds])[i];
        xor4(block,      T5[temp[0][0]], T6[temp[3][1]], T7[temp[2][2]], T8[temp[1][3]]);
        xor4(block + 4,  T5[temp[1][0]], T6[temp[0][1]], T7[temp[3][2]], T8[temp[2][3]]);
        xor4(block + 8,  T5[temp[2][0]], T6[temp[1][1]], T7[temp[0][2]], T8[temp[3][3]]);
        xor4(block + 12, T5[temp[3][0]], T6[temp[2][1]], T7[temp[1][2]], T8[temp[0][3]]);
        for (int r = aes->rounds - 1; r > 1; r--) {
            for (int i = 0; i < 16; i++) ((uint8_t*)temp)[i] = block[i] ^ ((const uint8_t*)aes->key[r])[i];
            xor4(block,      T5[temp[0][0]], T6[temp[3][1]], T7[temp[2][2]], T8[temp[1][3]]);
            xor4(block + 4,  T5[temp[1][0]], T6[temp[0][1]], T7[temp[3][2]], T8[temp[2][3]]);
            xor4(block + 8,  T5[temp[2][0]], T6[temp[1][1]], T7[temp[0][2]], T8[temp[3][3]]);
            xor4(block + 12, T5[temp[3][0]], T6[temp[2][1]], T7[temp[1][2]], T8[temp[0][3]]);
        }
        for (int i = 0; i < 16; i++) ((uint8_t*)temp)[i] = block[i] ^ ((const uint8_t*)aes->key[1])[i];
        block[0] = S5[temp[0][0]];  block[1] = S5[temp[3][1]];  block[2] = S5[temp[2][2]];  block[3] = S5[temp[1][3]];
        block[4] = S5[temp[1][0]];  block[5] = S5[temp[0][1]];  block[6] = S5[temp[3][2]];  block[7] = S5[temp[2][3]];
        block[8] = S5[temp[2][0]];  block[9] = S5[temp[1][1]];  block[10] = S5[temp[0][2]]; block[11] = S5[temp[3][3]];
        block[12] = S5[temp[3][0]]; block[13] = S5[temp[2][1]]; block[14] = S5[temp[1][2]]; block[15] = S5[temp[0][3]];
        for (int i = 0; i < 16; i++) block[i] ^= ((const uint8_t*)aes->key[0])[i] ^ iv[i];
        memcpy(iv, data, 16);          /* CBC: this cipher block chains the next */
        memcpy(data, block, 16);
    }
    memcpy(aes->iv, iv, 16);
    rar_wipe(block, sizeof(block));
}

/* ---- sha1.cpp (Steve Reid's public domain SHA-1) ---- */

/* The block as sixteen big-endian words, expanded in place as the rounds
   run; `w` is what RAR 2.9's variant writes back afterwards. */
#define BLK0(i) (w[i])
#define BLK(i) (w[(i) & 15] = rotl32(w[((i) + 13) & 15] ^ w[((i) + 8) & 15] ^ w[((i) + 2) & 15] ^ w[(i) & 15], 1))
#define R0(v, x, y, z, u, i) { u += ((x & (y ^ z)) ^ z) + BLK0(i) + 0x5A827999 + rotl32(v, 5); x = rotl32(x, 30); }
#define R1(v, x, y, z, u, i) { u += ((x & (y ^ z)) ^ z) + BLK(i) + 0x5A827999 + rotl32(v, 5); x = rotl32(x, 30); }
#define R2(v, x, y, z, u, i) { u += (x ^ y ^ z) + BLK(i) + 0x6ED9EBA1 + rotl32(v, 5); x = rotl32(x, 30); }
#define R3(v, x, y, z, u, i) { u += (((x | y) & z) | (x & y)) + BLK(i) + 0x8F1BBCDC + rotl32(v, 5); x = rotl32(x, 30); }
#define R4(v, x, y, z, u, i) { u += (x ^ y ^ z) + BLK(i) + 0xCA62C1D6 + rotl32(v, 5); x = rotl32(x, 30); }

static void sha1_transform(uint32_t state[5], uint32_t w[16], const uint8_t buffer[64]) {
    for (int i = 0; i < 16; i++) w[i] = get_be32(buffer + 4 * i);
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    for (int i = 0; ; i += 5) {
        R0(a, b, c, d, e, i + 0); if (i == 15) break;
        R0(e, a, b, c, d, i + 1); R0(d, e, a, b, c, i + 2);
        R0(c, d, e, a, b, i + 3); R0(b, c, d, e, a, i + 4);
    }
    R1(e, a, b, c, d, 16); R1(d, e, a, b, c, 17); R1(c, d, e, a, b, 18); R1(b, c, d, e, a, 19);
    for (int i = 20; i <= 35; i += 5) {
        R2(a, b, c, d, e, i + 0); R2(e, a, b, c, d, i + 1); R2(d, e, a, b, c, i + 2);
        R2(c, d, e, a, b, i + 3); R2(b, c, d, e, a, i + 4);
    }
    for (int i = 40; i <= 55; i += 5) {
        R3(a, b, c, d, e, i + 0); R3(e, a, b, c, d, i + 1); R3(d, e, a, b, c, i + 2);
        R3(c, d, e, a, b, i + 3); R3(b, c, d, e, a, i + 4);
    }
    for (int i = 60; i <= 75; i += 5) {
        R4(a, b, c, d, e, i + 0); R4(e, a, b, c, d, i + 1); R4(d, e, a, b, c, i + 2);
        R4(c, d, e, a, b, i + 3); R4(b, c, d, e, a, i + 4);
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

void rar_sha1_init(rar_sha1_t *c) {
    c->count = 0;
    c->state[0] = 0x67452301; c->state[1] = 0xEFCDAB89; c->state[2] = 0x98BADCFE;
    c->state[3] = 0x10325476; c->state[4] = 0xC3D2E1F0;
}

static void sha1_update(rar_sha1_t *c, uint8_t *data_rw, const uint8_t *data, size_t len) {
    size_t i, j = (size_t)(c->count & 63);
    c->count += len;
    uint32_t w[16];
    if (j + len > 63) {
        memcpy(c->buffer + j, data, (i = 64 - j));
        sha1_transform(c->state, w, c->buffer);
        for (; i + 63 < len; i += 64) {
            sha1_transform(c->state, w, data + i);
            /* RAR 2.9: the expanded words go back where the block came from.
               (UnRAR writes them in the machine's little-endian order.) */
            if (data_rw) for (int k = 0; k < 16; k++) put_le32(w[k], data_rw + i + 4 * k);
        }
        j = 0;
    } else {
        i = 0;
    }
    if (len > i) memcpy(c->buffer + j, data + i, len - i);
}

void rar_sha1_process(rar_sha1_t *c, const uint8_t *data, size_t len) { sha1_update(c, NULL, data, len); }
void rar_sha1_process_rar29(rar_sha1_t *c, uint8_t *data, size_t len) { sha1_update(c, data, data, len); }

void rar_sha1_done(rar_sha1_t *c, uint32_t digest[5]) {
    uint32_t w[16];
    uint64_t bits = c->count * 8;
    unsigned pos = (unsigned)c->count & 0x3f;
    c->buffer[pos++] = 0x80;
    if (pos != 56) {
        if (pos > 56) {
            while (pos < 64) c->buffer[pos++] = 0;
            pos = 0;
        }
        if (pos == 0) sha1_transform(c->state, w, c->buffer);
        memset(c->buffer + pos, 0, 56 - pos);
    }
    put_be32((uint32_t)(bits >> 32), c->buffer + 56);
    put_be32((uint32_t)bits, c->buffer + 60);
    sha1_transform(c->state, w, c->buffer);
    for (int i = 0; i < 5; i++) digest[i] = c->state[i];
    rar_sha1_init(c);
}

/* ---- sha256.cpp ---- */

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define CH(x, y, z)  ((x & y) ^ (~x & z))
#define MAJ(x, y, z) ((x & y) ^ (x & z) ^ (y & z))
#define SG0(x) (rotr32(x, 2) ^ rotr32(x, 13) ^ rotr32(x, 22))
#define SG1(x) (rotr32(x, 6) ^ rotr32(x, 11) ^ rotr32(x, 25))
#define sg0(x) (rotr32(x, 7) ^ rotr32(x, 18) ^ (x >> 3))
#define sg1(x) (rotr32(x, 17) ^ rotr32(x, 19) ^ (x >> 10))

void rar_sha256_init(rar_sha256_t *c) {
    c->h[0] = 0x6a09e667; c->h[1] = 0xbb67ae85; c->h[2] = 0x3c6ef372; c->h[3] = 0xa54ff53a;
    c->h[4] = 0x510e527f; c->h[5] = 0x9b05688c; c->h[6] = 0x1f83d9ab; c->h[7] = 0x5be0cd19;
    c->count = 0;
}

static void sha256_transform(rar_sha256_t *c) {
    uint32_t W[64], v[8];
    for (int i = 0; i < 16; i++) W[i] = get_be32(c->buffer + i * 4);
    for (int i = 16; i < 64; i++) W[i] = sg1(W[i - 2]) + W[i - 7] + sg0(W[i - 15]) + W[i - 16];
    for (int i = 0; i < 8; i++) v[i] = c->h[i];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = v[7] + SG1(v[4]) + CH(v[4], v[5], v[6]) + K256[i] + W[i];
        v[7] = v[6]; v[6] = v[5]; v[5] = v[4]; v[4] = v[3] + t1;
        uint32_t t2 = SG0(v[0]) + MAJ(v[0], v[1], v[2]);
        v[3] = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++) c->h[i] += v[i];
}

void rar_sha256_process(rar_sha256_t *c, const void *data, size_t size) {
    const uint8_t *src = (const uint8_t*)data;
    size_t pos = (size_t)(c->count & 0x3f);
    c->count += size;
    while (size > 0) {
        size_t space = 64 - pos, n = size > space ? space : size;
        memcpy(c->buffer + pos, src, n);
        src += n; pos += n; size -= n;
        if (pos == 64) { pos = 0; sha256_transform(c); }
    }
}

void rar_sha256_done(rar_sha256_t *c, uint8_t digest[32]) {
    uint64_t bits = c->count * 8;
    unsigned pos = (unsigned)c->count & 0x3f;
    c->buffer[pos++] = 0x80;
    if (pos != 56) {
        if (pos > 56) {
            while (pos < 64) c->buffer[pos++] = 0;
            pos = 0;
        }
        if (pos == 0) sha256_transform(c);
        memset(c->buffer + pos, 0, 56 - pos);
    }
    put_be32((uint32_t)(bits >> 32), c->buffer + 56);
    put_be32((uint32_t)bits, c->buffer + 60);
    sha256_transform(c);
    for (int i = 0; i < 8; i++) put_be32(c->h[i], digest + 4 * i);
    rar_sha256_init(c);
}

void rar_sha256(const void *data, size_t size, uint8_t digest[32]) {
    rar_sha256_t c;
    rar_sha256_init(&c);
    rar_sha256_process(&c, data, size);
    rar_sha256_done(&c, digest);
}

/* ---- crypt5.cpp ---- */

/* HMAC-SHA256; `inner` / `outer` keep the padded key's first block for the
   thousands of rounds PBKDF2 makes with one password (UnRAR's ICtxOpt). */
static void hmac_sha256_opt(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t digest[32],
                            rar_sha256_t *inner, bool *inner_set, rar_sha256_t *outer, bool *outer_set) {
    uint8_t key_hash[32];
    if (key_len > 64) {
        rar_sha256(key, key_len, key_hash);
        key = key_hash;
        key_len = 32;
    }
    uint8_t pad[64];
    rar_sha256_t ictx, rctx;
    if (inner && *inner_set) {
        ictx = *inner;
    } else {
        for (size_t i = 0; i < key_len; i++) pad[i] = key[i] ^ 0x36;
        for (size_t i = key_len; i < 64; i++) pad[i] = 0x36;
        rar_sha256_init(&ictx);
        rar_sha256_process(&ictx, pad, 64);
    }
    if (inner && !*inner_set) { *inner = ictx; *inner_set = true; }
    rar_sha256_process(&ictx, data, data_len);
    uint8_t idig[32];
    rar_sha256_done(&ictx, idig);
    if (outer && *outer_set) {
        rctx = *outer;
    } else {
        for (size_t i = 0; i < key_len; i++) pad[i] = key[i] ^ 0x5c;
        for (size_t i = key_len; i < 64; i++) pad[i] = 0x5c;
        rar_sha256_init(&rctx);
        rar_sha256_process(&rctx, pad, 64);
    }
    if (outer && !*outer_set) { *outer = rctx; *outer_set = true; }
    rar_sha256_process(&rctx, idig, 32);
    rar_sha256_done(&rctx, digest);
    rar_wipe(pad, sizeof(pad));
    rar_wipe(key_hash, sizeof(key_hash));
}

void rar_hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t digest[32]) {
    hmac_sha256_opt(key, key_len, data, data_len, digest, NULL, NULL, NULL, NULL);
}

void rar_pbkdf2(const uint8_t *pwd, size_t pwd_len, const uint8_t *salt, size_t salt_len,
                uint8_t key[32], uint8_t v1[32], uint8_t v2[32], uint32_t rounds) {
    uint8_t salt_data[64 + 4];
    if (salt_len > 64) salt_len = 64;
    memcpy(salt_data, salt, salt_len);
    salt_data[salt_len + 0] = 0;
    salt_data[salt_len + 1] = 0;
    salt_data[salt_len + 2] = 0;
    salt_data[salt_len + 3] = 1;
    uint8_t u1[32], u2[32], fn[32];
    rar_hmac_sha256(pwd, pwd_len, salt_data, salt_len + 4, u1);
    memcpy(fn, u1, sizeof(fn));
    uint32_t counts[3] = { rounds - 1, 16, 16 };
    uint8_t *values[3] = { key, v1, v2 };
    rar_sha256_t ictx, rctx;
    bool iset = false, rset = false;
    for (int i = 0; i < 3; i++) {
        for (uint32_t j = 0; j < counts[i]; j++) {
            hmac_sha256_opt(pwd, pwd_len, u1, sizeof(u1), u2, &ictx, &iset, &rctx, &rset);
            memcpy(u1, u2, sizeof(u1));
            for (int k = 0; k < 32; k++) fn[k] ^= u1[k];
        }
        if (values[i]) memcpy(values[i], fn, 32);
    }
    rar_wipe(salt_data, sizeof(salt_data));
    rar_wipe(fn, sizeof(fn));
    rar_wipe(u1, sizeof(u1));
    rar_wipe(u2, sizeof(u2));
    rar_wipe(&ictx, sizeof(ictx));
    rar_wipe(&rctx, sizeof(rctx));
}

bool rar_kdf50(const uint8_t *pwd8, size_t pwd8_len, const uint8_t salt[16], unsigned lg2_count,
               uint8_t key[32], uint8_t hash_key[32], uint8_t psw_check[8]) {
    if (lg2_count > RAR_KDF50_LG2_MAX) return false;
    uint8_t check_value[32], hash_value[32], k[32];
    rar_pbkdf2(pwd8, pwd8_len, salt, RAR_SALT50, k, hash_value, check_value, (uint32_t)1 << lg2_count);
    if (key) memcpy(key, k, 32);
    if (hash_key) memcpy(hash_key, hash_value, 32);
    if (psw_check) {
        memset(psw_check, 0, RAR_PSWCHECK);
        for (int i = 0; i < 32; i++) psw_check[i % RAR_PSWCHECK] ^= check_value[i];
    }
    rar_wipe(k, sizeof(k));
    rar_wipe(check_value, sizeof(check_value));
    rar_wipe(hash_value, sizeof(hash_value));
    return true;
}

uint32_t rar_crc_to_mac(uint32_t crc, const uint8_t hash_key[32]) {
    uint8_t raw[4], digest[32];
    put_le32(crc, raw);
    rar_hmac_sha256(hash_key, 32, raw, sizeof(raw), digest);
    uint32_t mac = 0;
    for (int i = 0; i < 32; i++) mac ^= (uint32_t)digest[i] << ((i & 3) * 8);
    return mac;
}

/* ---- crypt3.cpp ---- */

void rar_kdf30(const uint8_t *pwd16, size_t pwd16_len, const uint8_t *salt, uint8_t key[16], uint8_t iv[16]) {
    uint8_t raw[2 * RAR_MAX_PASSWORD + 2 + RAR_SALT30];
    if (pwd16_len > 2 * RAR_MAX_PASSWORD) pwd16_len = 2 * RAR_MAX_PASSWORD;
    size_t raw_len = pwd16_len;
    memcpy(raw, pwd16, pwd16_len);
    if (salt) {
        memcpy(raw + raw_len, salt, RAR_SALT30);
        raw_len += RAR_SALT30;
    }
    rar_sha1_t c;
    rar_sha1_init(&c);
    const uint32_t rounds = 0x40000;
    for (uint32_t i = 0; i < rounds; i++) {
        rar_sha1_process_rar29(&c, raw, raw_len);
        uint8_t num[3] = { (uint8_t)i, (uint8_t)(i >> 8), (uint8_t)(i >> 16) };
        rar_sha1_process(&c, num, 3);
        if (i % (rounds / 16) == 0) {
            rar_sha1_t temp = c;
            uint32_t digest[5];
            rar_sha1_done(&temp, digest);
            iv[i / (rounds / 16)] = (uint8_t)digest[4];
        }
    }
    uint32_t digest[5];
    rar_sha1_done(&c, digest);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) key[i * 4 + j] = (uint8_t)(digest[i] >> (j * 8));
    rar_wipe(raw, sizeof(raw));
    rar_wipe(digest, sizeof(digest));
    rar_wipe(&c, sizeof(c));
}
