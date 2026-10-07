/* crypto.c - FultaArc: AES (FIPS 197), SHA-1 and SHA-256 (FIPS 180-4), HMAC (RFC 2104), PBKDF2 (RFC 8018),
 * LEA (KS X 3246 / KISA's specification), traditional PKWARE encryption (APPNOTE), and decrypting streams for
 * AES-CBC and the CTR variants of docs/specs/crypto.md. MIT. Written from those standards. */
#include "../core/internal.h"

/* ---- AES ---------------------------------------------------------------------------------------------------- */

static uint8_t sbox[256], inv_sbox[256];
static uint32_t te[4][256];
static bool aes_ready;

static uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1B : 0)); }
static uint8_t gmul(uint8_t a, uint8_t b) {
    uint8_t r = 0;
    while (b) { if (b & 1) r ^= a; a = xtime(a); b >>= 1; }
    return r;
}

static void aes_tables(void) {
    /* S-box from the multiplicative inverse in GF(2^8) and the affine map (FIPS 197 5.1.1) */
    uint8_t p = 1, q = 1;
    do {
        p = (uint8_t)(p ^ (p << 1) ^ ((p & 0x80) ? 0x1B : 0));
        q ^= (uint8_t)(q << 1); q ^= (uint8_t)(q << 2); q ^= (uint8_t)(q << 4);
        if (q & 0x80) q ^= 0x09;
        uint8_t x = (uint8_t)(q ^ (uint8_t)((q << 1) | (q >> 7)) ^ (uint8_t)((q << 2) | (q >> 6)) ^
                              (uint8_t)((q << 3) | (q >> 5)) ^ (uint8_t)((q << 4) | (q >> 4)));
        sbox[p] = x ^ 0x63;
    } while (p != 1);
    sbox[0] = 0x63;
    for (int i = 0; i < 256; i++) inv_sbox[sbox[i]] = (uint8_t)i;
    for (int i = 0; i < 256; i++) {
        uint8_t v = sbox[i];
        uint32_t t = (uint32_t)gmul(v, 2) | ((uint32_t)v << 8) | ((uint32_t)v << 16) | ((uint32_t)gmul(v, 3) << 24);
        te[0][i] = t;
        te[1][i] = (t << 8) | (t >> 24);
        te[2][i] = (t << 16) | (t >> 16);
        te[3][i] = (t << 24) | (t >> 8);
    }
    aes_ready = true;
}

static uint32_t sub_word(uint32_t w) {
    return (uint32_t)sbox[w & 255] | ((uint32_t)sbox[(w >> 8) & 255] << 8) | ((uint32_t)sbox[(w >> 16) & 255] << 16) |
           ((uint32_t)sbox[w >> 24] << 24);
}

/* Enable AES-NI only after it reproduces the FIPS-197 known answers (AES-128 and AES-256, both directions). */
static int aes_hw = -1;
static int aes_hw_ok(void) {
    if (aes_hw >= 0) return aes_hw;
    aes_hw = 0;
    if (!fa_aes_hw_supported()) return 0;
    static const uint8_t pt[16] = {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
    static const uint8_t k128[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    static const uint8_t c128[16] = {0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a};
    static const uint8_t k256[32] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};
    static const uint8_t c256[16] = {0x8e,0xa2,0xb7,0xca,0x51,0x67,0x45,0xbf,0xea,0xfc,0x49,0x90,0x4b,0x49,0x60,0x89};
    uint8_t rk[15 * 16], out[16];
    int nr;
    struct { const uint8_t *k; size_t kl; const uint8_t *c; } v[2] = {{k128, 16, c128}, {k256, 32, c256}};
    for (int i = 0; i < 2; i++) {
        if (!fa_aes_hw_expand(v[i].k, v[i].kl, 0, rk, &nr)) return 0;
        fa_aes_hw_encrypt(rk, nr, pt, out);
        if (memcmp(out, v[i].c, 16) != 0) return 0;
        if (!fa_aes_hw_expand(v[i].k, v[i].kl, 1, rk, &nr)) return 0;
        fa_aes_hw_decrypt(rk, nr, v[i].c, out);
        if (memcmp(out, pt, 16) != 0) return 0;
    }
    aes_hw = 1;
    return 1;
}

void fa_aes_init_enc(fa_aes_t *a, const uint8_t *key, size_t keylen) {
    if (!aes_ready) aes_tables();
    int nk = (int)keylen / 4;
    a->rounds = nk + 6;
    int total = 4 * (a->rounds + 1);
    for (int i = 0; i < nk; i++) a->rk[i] = fa_le32(key + 4 * i);
    uint8_t rcon = 1;
    for (int i = nk; i < total; i++) {
        uint32_t t = a->rk[i - 1];
        if (i % nk == 0) {
            t = sub_word((t >> 8) | (t << 24)) ^ rcon;
            rcon = xtime(rcon);
        } else if (nk > 6 && i % nk == 4) {
            t = sub_word(t);
        }
        a->rk[i] = a->rk[i - nk] ^ t;
    }
    /* AES-NI (when available and the width is 128/256): keep both schedules so this one context can do either
     * direction, as the software cipher does from the forward key schedule. */
    if (aes_hw_ok() && (keylen == 16 || keylen == 32)) {
        fa_aes_hw_expand(key, keylen, 0, a->hwenc, &a->rounds);
        fa_aes_hw_expand(key, keylen, 1, a->hwdec, &a->rounds);
        a->hw = 1;
    } else {
        a->hw = 0;
    }
}

void fa_aes_init_dec(fa_aes_t *a, const uint8_t *key, size_t keylen) {
    fa_aes_init_enc(a, key, keylen);   /* identical: software decrypt uses the forward keys; AES-NI has both schedules */
}

static void add_round_key(uint8_t s[16], const uint32_t *rk) {
    for (int c = 0; c < 4; c++) {
        uint32_t k = rk[c];
        s[4 * c] ^= (uint8_t)k; s[4 * c + 1] ^= (uint8_t)(k >> 8); s[4 * c + 2] ^= (uint8_t)(k >> 16); s[4 * c + 3] ^= (uint8_t)(k >> 24);
    }
}

void fa_aes_encrypt(const fa_aes_t *a, const uint8_t in[16], uint8_t out[16]) {
    if (a->hw) { fa_aes_hw_encrypt(a->hwenc, a->rounds, in, out); return; }
    uint32_t s0 = fa_le32(in) ^ a->rk[0], s1 = fa_le32(in + 4) ^ a->rk[1], s2 = fa_le32(in + 8) ^ a->rk[2],
             s3 = fa_le32(in + 12) ^ a->rk[3];
    const uint32_t *rk = a->rk + 4;
    for (int r = 1; r < a->rounds; r++, rk += 4) {
        uint32_t t0 = te[0][s0 & 255] ^ te[1][(s1 >> 8) & 255] ^ te[2][(s2 >> 16) & 255] ^ te[3][s3 >> 24] ^ rk[0];
        uint32_t t1 = te[0][s1 & 255] ^ te[1][(s2 >> 8) & 255] ^ te[2][(s3 >> 16) & 255] ^ te[3][s0 >> 24] ^ rk[1];
        uint32_t t2 = te[0][s2 & 255] ^ te[1][(s3 >> 8) & 255] ^ te[2][(s0 >> 16) & 255] ^ te[3][s1 >> 24] ^ rk[2];
        uint32_t t3 = te[0][s3 & 255] ^ te[1][(s0 >> 8) & 255] ^ te[2][(s1 >> 16) & 255] ^ te[3][s2 >> 24] ^ rk[3];
        s0 = t0; s1 = t1; s2 = t2; s3 = t3;
    }
    uint32_t o0 = sub_word((s0 & 255) | (s1 & 0xFF00) | (s2 & 0xFF0000) | (s3 & 0xFF000000)) ^ rk[0];
    uint32_t o1 = sub_word((s1 & 255) | (s2 & 0xFF00) | (s3 & 0xFF0000) | (s0 & 0xFF000000)) ^ rk[1];
    uint32_t o2 = sub_word((s2 & 255) | (s3 & 0xFF00) | (s0 & 0xFF0000) | (s1 & 0xFF000000)) ^ rk[2];
    uint32_t o3 = sub_word((s3 & 255) | (s0 & 0xFF00) | (s1 & 0xFF0000) | (s2 & 0xFF000000)) ^ rk[3];
    fa_put_le32(out, o0); fa_put_le32(out + 4, o1); fa_put_le32(out + 8, o2); fa_put_le32(out + 12, o3);
}

void fa_aes_decrypt(const fa_aes_t *a, const uint8_t in[16], uint8_t out[16]) {
    if (a->hw) { fa_aes_hw_decrypt(a->hwdec, a->rounds, in, out); return; }
    /* straightforward inverse cipher (FIPS 197 5.3) on a byte state; state[r + 4c] */
    uint8_t s[16];
    memcpy(s, in, 16);
    add_round_key(s, a->rk + 4 * a->rounds);
    for (int r = a->rounds - 1; r >= 0; r--) {
        uint8_t t[16];
        /* InvShiftRows */
        for (int c = 0; c < 4; c++)
            for (int row = 0; row < 4; row++) t[4 * ((c + row) % 4) + row] = s[4 * c + row];
        /* InvSubBytes */
        for (int i = 0; i < 16; i++) s[i] = inv_sbox[t[i]];
        add_round_key(s, a->rk + 4 * r);
        if (r == 0) break;
        /* InvMixColumns */
        for (int c = 0; c < 4; c++) {
            uint8_t *col = s + 4 * c;
            uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
            col[0] = gmul(a0, 14) ^ gmul(a1, 11) ^ gmul(a2, 13) ^ gmul(a3, 9);
            col[1] = gmul(a0, 9) ^ gmul(a1, 14) ^ gmul(a2, 11) ^ gmul(a3, 13);
            col[2] = gmul(a0, 13) ^ gmul(a1, 9) ^ gmul(a2, 14) ^ gmul(a3, 11);
            col[3] = gmul(a0, 11) ^ gmul(a1, 13) ^ gmul(a2, 9) ^ gmul(a3, 14);
        }
    }
    memcpy(out, s, 16);
}

/* ---- SHA-1 -------------------------------------------------------------------------------------------------- */

static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static void sha1_block(fa_sha1_t *c, const uint8_t *p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = fa_be32(p + 4 * i);
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & cc) | (~b & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ cc ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDC; }
        else { f = b ^ cc ^ d; k = 0xCA62C1D6; }
        uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d; d = cc; cc = rol(b, 30); b = a; a = t;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}

void fa_sha1_init(fa_sha1_t *c) {
    static const uint32_t iv[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    memcpy(c->h, iv, sizeof iv);
    c->len = 0;
    c->n = 0;
}

void fa_sha1_update(fa_sha1_t *c, const void *data, size_t n) {
    const uint8_t *p = data;
    c->len += n;
    while (n) {
        size_t k = 64 - c->n < n ? 64 - c->n : n;
        memcpy(c->buf + c->n, p, k);
        c->n += k; p += k; n -= k;
        if (c->n == 64) { sha1_block(c, c->buf); c->n = 0; }
    }
}

void fa_sha1_final(fa_sha1_t *c, uint8_t out[20]) {
    uint64_t bitlen = c->len * 8;
    uint8_t pad = 0x80;
    fa_sha1_update(c, &pad, 1);
    pad = 0;
    while (c->n != 56) fa_sha1_update(c, &pad, 1);
    uint8_t l[8];
    for (int i = 0; i < 8; i++) l[i] = (uint8_t)(bitlen >> (56 - 8 * i));
    fa_sha1_update(c, l, 8);
    for (int i = 0; i < 5; i++) {
        out[4 * i] = (uint8_t)(c->h[i] >> 24); out[4 * i + 1] = (uint8_t)(c->h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(c->h[i] >> 8); out[4 * i + 3] = (uint8_t)c->h[i];
    }
}

/* ---- SHA-256 (FIPS 180-4): proven's implementation (vendor/proven, hash.h) ---------------------------------- */
void fa_sha256_init(fa_sha256_t *c) { proven_sha256_init(&c->pv); }
void fa_sha256_update(fa_sha256_t *c, const void *data, size_t n) {
    proven_sha256_update(&c->pv, (proven_mem_view_t){.ptr = data, .size = n});
}
void fa_sha256_final(fa_sha256_t *c, uint8_t out[32]) { proven_sha256_final(&c->pv, out); }

/* ---- HMAC, PBKDF2 ------------------------------------------------------------------------------------------- */

void fa_hmac_sha1(const uint8_t *key, size_t keylen, const uint8_t *msg, size_t n, uint8_t out[20]) {
    uint8_t k[64] = {0}, pad[64];
    if (keylen > 64) { fa_sha1_t c; fa_sha1_init(&c); fa_sha1_update(&c, key, keylen); fa_sha1_final(&c, k); }
    else if (keylen) memcpy(k, key, keylen);
    fa_sha1_t c;
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    fa_sha1_init(&c); fa_sha1_update(&c, pad, 64); fa_sha1_update(&c, msg, n);
    uint8_t inner[20];
    fa_sha1_final(&c, inner);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5C;
    fa_sha1_init(&c); fa_sha1_update(&c, pad, 64); fa_sha1_update(&c, inner, 20);
    fa_sha1_final(&c, out);
}

void fa_hmac_sha256(const uint8_t *key, size_t keylen, const uint8_t *msg, size_t n, uint8_t out[32]) {
    uint8_t k[64] = {0}, pad[64];
    if (keylen > 64) { fa_sha256_t c; fa_sha256_init(&c); fa_sha256_update(&c, key, keylen); fa_sha256_final(&c, k); }
    else if (keylen) memcpy(k, key, keylen);
    fa_sha256_t c;
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    fa_sha256_init(&c); fa_sha256_update(&c, pad, 64); fa_sha256_update(&c, msg, n);
    uint8_t inner[32];
    fa_sha256_final(&c, inner);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5C;
    fa_sha256_init(&c); fa_sha256_update(&c, pad, 64); fa_sha256_update(&c, inner, 32);
    fa_sha256_final(&c, out);
}

/* Precomputed HMAC: the inner and outer contexts after the key block, so each iteration costs two blocks. */
typedef struct hmac1 { fa_sha1_t in, out; } hmac1_t;
static void hmac1_init(hmac1_t *h, const uint8_t *key, size_t keylen) {
    uint8_t k[64] = {0}, pad[64];
    if (keylen > 64) { fa_sha1_t c; fa_sha1_init(&c); fa_sha1_update(&c, key, keylen); fa_sha1_final(&c, k); }
    else if (keylen) memcpy(k, key, keylen);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    fa_sha1_init(&h->in); fa_sha1_update(&h->in, pad, 64);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5C;
    fa_sha1_init(&h->out); fa_sha1_update(&h->out, pad, 64);
}
static void hmac1_run(const hmac1_t *h, const uint8_t *msg, size_t n, uint8_t out[20]) {
    fa_sha1_t c = h->in;
    uint8_t inner[20];
    fa_sha1_update(&c, msg, n); fa_sha1_final(&c, inner);
    c = h->out;
    fa_sha1_update(&c, inner, 20); fa_sha1_final(&c, out);
}

typedef struct hmac256 { fa_sha256_t in, out; } hmac256_t;
static void hmac256_init(hmac256_t *h, const uint8_t *key, size_t keylen) {
    uint8_t k[64] = {0}, pad[64];
    if (keylen > 64) { fa_sha256_t c; fa_sha256_init(&c); fa_sha256_update(&c, key, keylen); fa_sha256_final(&c, k); }
    else if (keylen) memcpy(k, key, keylen);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    fa_sha256_init(&h->in); fa_sha256_update(&h->in, pad, 64);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5C;
    fa_sha256_init(&h->out); fa_sha256_update(&h->out, pad, 64);
}
static void hmac256_run(const hmac256_t *h, const uint8_t *msg, size_t n, uint8_t out[32]) {
    fa_sha256_t c = h->in;
    uint8_t inner[32];
    fa_sha256_update(&c, msg, n); fa_sha256_final(&c, inner);
    c = h->out;
    fa_sha256_update(&c, inner, 32); fa_sha256_final(&c, out);
}

void fa_pbkdf2_sha1(const uint8_t *pw, size_t pwlen, const uint8_t *salt, size_t saltlen, uint32_t iter,
                    uint8_t *out, size_t outlen) {
    hmac1_t h;
    hmac1_init(&h, pw, pwlen);
    uint8_t msg[256 + 4];
    if (saltlen > 256) saltlen = 256;
    memcpy(msg, salt, saltlen);
    for (uint32_t blk = 1; outlen; blk++) {
        msg[saltlen] = (uint8_t)(blk >> 24); msg[saltlen + 1] = (uint8_t)(blk >> 16);
        msg[saltlen + 2] = (uint8_t)(blk >> 8); msg[saltlen + 3] = (uint8_t)blk;
        uint8_t u[20], t[20];
        hmac1_run(&h, msg, saltlen + 4, u);
        memcpy(t, u, 20);
        for (uint32_t i = 1; i < iter; i++) {
            hmac1_run(&h, u, 20, u);
            for (int k = 0; k < 20; k++) t[k] ^= u[k];
        }
        size_t k = outlen < 20 ? outlen : 20;
        memcpy(out, t, k);
        out += k;
        outlen -= k;
    }
}

void fa_pbkdf2_sha256(const uint8_t *pw, size_t pwlen, const uint8_t *salt, size_t saltlen, uint32_t iter,
                      uint8_t *out, size_t outlen) {
    hmac256_t h;
    hmac256_init(&h, pw, pwlen);
    uint8_t msg[256 + 4];
    if (saltlen > 256) saltlen = 256;
    memcpy(msg, salt, saltlen);
    for (uint32_t blk = 1; outlen; blk++) {
        msg[saltlen] = (uint8_t)(blk >> 24); msg[saltlen + 1] = (uint8_t)(blk >> 16);
        msg[saltlen + 2] = (uint8_t)(blk >> 8); msg[saltlen + 3] = (uint8_t)blk;
        uint8_t u[32], t[32];
        hmac256_run(&h, msg, saltlen + 4, u);
        memcpy(t, u, 32);
        for (uint32_t i = 1; i < iter; i++) {
            hmac256_run(&h, u, 32, u);
            for (int k = 0; k < 32; k++) t[k] ^= u[k];
        }
        size_t k = outlen < 32 ? outlen : 32;
        memcpy(out, t, k);
        out += k;
        outlen -= k;
    }
}

/* ---- LEA (encryption direction) ----------------------------------------------------------------------------- */

static const uint32_t DELTA[8] = {0xc3efe9db, 0x44626b02, 0x79e27c8a, 0x78df30ec,
                                  0x715ea49e, 0xc785da0a, 0xe04ef22a, 0xe5c40957};

static uint32_t rolx(uint32_t v, unsigned n) { n &= 31; return n ? (v << n) | (v >> (32 - n)) : v; }

void fa_lea_init(fa_lea_t *l, const uint8_t *key, size_t keylen) {
    static const unsigned R[6] = {1, 3, 6, 11, 13, 17};
    uint32_t t[8];
    for (size_t i = 0; i < keylen / 4; i++) t[i] = fa_le32(key + 4 * i);
    if (keylen == 16) {
        l->rounds = 24;
        for (unsigned i = 0; i < 24; i++) {
            uint32_t d = DELTA[i % 4];
            t[0] = rolx(t[0] + rolx(d, i), 1);
            t[1] = rolx(t[1] + rolx(d, i + 1), 3);
            t[2] = rolx(t[2] + rolx(d, i + 2), 6);
            t[3] = rolx(t[3] + rolx(d, i + 3), 11);
            uint32_t rk[6] = {t[0], t[1], t[2], t[1], t[3], t[1]};
            memcpy(l->rk[i], rk, sizeof rk);
        }
    } else if (keylen == 24) {
        l->rounds = 28;
        for (unsigned i = 0; i < 28; i++) {
            for (unsigned j = 0; j < 6; j++) t[j] = rolx(t[j] + rolx(DELTA[i % 6], i + j), R[j]);
            memcpy(l->rk[i], t, 6 * sizeof *t);
        }
    } else {
        l->rounds = 32;
        for (unsigned i = 0; i < 32; i++) {
            for (unsigned j = 0; j < 6; j++) {
                unsigned k = (6 * i + j) % 8;
                t[k] = rolx(t[k] + rolx(DELTA[i % 8], i + j), R[j]);
            }
            for (unsigned j = 0; j < 6; j++) l->rk[i][j] = t[(6 * i + j) % 8];
        }
    }
}

void fa_lea_encrypt(const fa_lea_t *l, const uint8_t in[16], uint8_t out[16]) {
    uint32_t x0 = fa_le32(in), x1 = fa_le32(in + 4), x2 = fa_le32(in + 8), x3 = fa_le32(in + 12);
    for (int i = 0; i < l->rounds; i++) {
        const uint32_t *k = l->rk[i];
        uint32_t n0 = rolx((x0 ^ k[0]) + (x1 ^ k[1]), 9);
        uint32_t n1 = rolx((x1 ^ k[2]) + (x2 ^ k[3]), 27);
        uint32_t n2 = rolx((x2 ^ k[4]) + (x3 ^ k[5]), 29);
        x3 = x0; x0 = n0; x1 = n1; x2 = n2;
    }
    fa_put_le32(out, x0); fa_put_le32(out + 4, x1); fa_put_le32(out + 8, x2); fa_put_le32(out + 12, x3);
}

/* ---- traditional PKWARE encryption -------------------------------------------------------------------------- */

static uint32_t crc_byte(uint32_t k, uint8_t b) {
    /* one step of the reflected CRC-32 table update, no inversions */
    uint32_t c = (k ^ b) & 0xFF;
    for (int i = 0; i < 8; i++) c = (c >> 1) ^ ((c & 1) ? 0xEDB88320u : 0);
    return c ^ (k >> 8);
}

static void zc_update(fa_zipcrypto_t *z, uint8_t b) {
    z->k[0] = crc_byte(z->k[0], b);
    z->k[1] = (z->k[1] + (z->k[0] & 0xFF)) * 134775813u + 1;
    z->k[2] = crc_byte(z->k[2], (uint8_t)(z->k[1] >> 24));
}

void fa_zipcrypto_init(fa_zipcrypto_t *z, const uint8_t *pw, size_t n) {
    z->k[0] = 0x12345678; z->k[1] = 0x23456789; z->k[2] = 0x34567890;
    for (size_t i = 0; i < n; i++) zc_update(z, pw[i]);
}

void fa_zipcrypto_decrypt(fa_zipcrypto_t *z, uint8_t *data, size_t n) {
    for (size_t i = 0; i < n; i++) {
        uint32_t t = (z->k[2] | 2) & 0xFFFF;
        uint8_t p = data[i] ^ (uint8_t)((t * (t ^ 1)) >> 8);
        zc_update(z, p);
        data[i] = p;
    }
}

/* ---- decrypting streams ------------------------------------------------------------------------------------- */

typedef struct zc_stream { fa_stream_t base; fa_stream_t *in; fa_zipcrypto_t keys; } zc_stream_t;

static fulta_arc_err_t zc_read(fa_stream_t *s, void *buf, size_t n, size_t *got) {
    zc_stream_t *z = (zc_stream_t *)s;
    fulta_arc_err_t e = z->in->read(z->in, buf, n, got);
    if (e) return e;
    fa_zipcrypto_decrypt(&z->keys, buf, *got);
    return FULTA_ARC_OK;
}
static void zc_destroy(fa_stream_t *s) { zc_stream_t *z = (zc_stream_t *)s; fa_stream_destroy(z->in); fa_free(z); }

fulta_arc_err_t fa_dec_zipcrypto(fa_stream_t *in, const fa_zipcrypto_t *keys, fa_stream_t **out) {
    zc_stream_t *z = fa_calloc(1, sizeof *z);
    if (!z) { fa_stream_destroy(in); return FULTA_ARC_ERR_NOMEM; }
    z->in = in;
    z->keys = *keys;
    z->base.read = zc_read;
    z->base.destroy = zc_destroy;
    *out = &z->base;
    return FULTA_ARC_OK;
}

typedef struct block_stream {
    fa_stream_t base;
    fa_stream_t *in;
    int mode;                   /* 0 CBC decrypt, 1 CTR AES LE1, 2 CTR LEA BE0 */
    fa_aes_t aes;
    fa_lea_t lea;
    uint8_t iv[16], ctr[16];
    uint8_t out[128];           /* up to 8 decrypted/keystream blocks for the AES-NI batch */
    size_t opos, olen;
    bool eof;
} block_stream_t;

static void inc16le(uint8_t c[16]) { for (int i = 0; i < 16; i++) if (++c[i]) break; }

static void ctr_next(block_stream_t *b) {
    if (b->mode == 1) {
        for (int i = 0; i < 16; i++) if (++b->ctr[i]) break;     /* little-endian increment */
        fa_aes_encrypt(&b->aes, b->ctr, b->out);
    } else {
        fa_lea_encrypt(&b->lea, b->ctr, b->out);
        for (int i = 15; i >= 0; i--) if (++b->ctr[i]) break;    /* big-endian, used after the block */
    }
}

static fulta_arc_err_t block_read(fa_stream_t *s, void *buf, size_t n, size_t *got) {
    block_stream_t *b = (block_stream_t *)s;
    uint8_t *o = buf;
    *got = 0;
    if (b->mode != 0) {
        size_t g = 0;
        fulta_arc_err_t e = b->in->read(b->in, buf, n, &g);
        if (e) return e;
        size_t i = 0;
        if (b->mode == 1 && b->aes.hw) {
            /* AES-NI CTR: finish any leftover keystream, then 8 counter blocks at a time */
            while (i < g && b->opos < 16) o[i++] ^= b->out[b->opos++];
            while (g - i >= 16) {
                unsigned nb = (g - i) / 16 < 8 ? (unsigned)((g - i) / 16) : 8;
                uint8_t ks[128], ctr0[16];
                memcpy(ctr0, b->ctr, 16);
                inc16le(ctr0);                                  /* the counter is used after +1 (ctr_next) */
                fa_aes_hw_ctr8(b->aes.hwenc, b->aes.rounds, ctr0, nb, ks);
                for (unsigned k = 0; k < 16u * nb; k++) o[i + k] ^= ks[k];
                for (unsigned j = 0; j < nb; j++) inc16le(b->ctr);
                i += 16u * nb;
                b->opos = 16;                                   /* no keystream held over */
            }
        }
        for (; i < g; i++) {
            if (b->opos == 16) { ctr_next(b); b->opos = 0; }
            o[i] ^= b->out[b->opos++];
        }
        *got = g;
        return FULTA_ARC_OK;
    }
    size_t cap = b->aes.hw ? 128 : 16;                       /* AES-NI decrypts up to 8 CBC blocks at once */
    while (*got < n) {
        if (b->opos < b->olen) { o[(*got)++] = b->out[b->opos++]; continue; }
        if (b->eof) break;
        uint8_t c[128];
        size_t have = 0;
        while (have < cap) {
            size_t g = 0;
            fulta_arc_err_t e = b->in->read(b->in, c + have, cap - have, &g);
            if (e) return e;
            if (!g) break;
            have += g;
        }
        size_t nb = have / 16;
        if (nb == 0) { b->eof = true; break; }   /* a partial last block is ignored (sizes cut the padding) */
        if (have % 16) b->eof = true;            /* only full blocks; a short read ends the stream */
        if (b->aes.hw) {
            fa_aes_hw_cbc_decrypt(b->aes.hwdec, b->aes.rounds, b->iv, c, b->out, (unsigned)nb);
        } else {
            for (size_t j = 0; j < nb; j++) {
                fa_aes_decrypt(&b->aes, c + 16 * j, b->out + 16 * j);
                for (int i = 0; i < 16; i++) b->out[16 * j + i] ^= b->iv[i];
                memcpy(b->iv, c + 16 * j, 16);
            }
        }
        b->opos = 0;
        b->olen = 16 * nb;
    }
    return FULTA_ARC_OK;
}

static void block_destroy(fa_stream_t *s) {
    block_stream_t *b = (block_stream_t *)s;
    fa_stream_destroy(b->in);
    memset(b, 0, sizeof *b);
    fa_free(b);
}

fulta_arc_err_t fa_dec_aes_cbc(fa_stream_t *in, const uint8_t *key, size_t keylen, const uint8_t iv[16],
                               fa_stream_t **out) {
    block_stream_t *b = fa_calloc(1, sizeof *b);
    if (!b) { fa_stream_destroy(in); return FULTA_ARC_ERR_NOMEM; }
    b->in = in;
    b->mode = 0;
    fa_aes_init_dec(&b->aes, key, keylen);
    memcpy(b->iv, iv, 16);
    b->base.read = block_read;
    b->base.destroy = block_destroy;
    *out = &b->base;
    return FULTA_ARC_OK;
}

fulta_arc_err_t fa_dec_ctr(fa_stream_t *in, fa_ctr_kind_t kind, const uint8_t *key, size_t keylen, fa_stream_t **out) {
    block_stream_t *b = fa_calloc(1, sizeof *b);
    if (!b) { fa_stream_destroy(in); return FULTA_ARC_ERR_NOMEM; }
    b->in = in;
    b->opos = 16;
    if (kind == FA_CTR_AES_LE1) { b->mode = 1; fa_aes_init_enc(&b->aes, key, keylen); }   /* ctr = 0, +1 before use */
    else { b->mode = 2; fa_lea_init(&b->lea, key, keylen); }                            /* ctr = 0, used then +1 */
    b->base.read = block_read;
    b->base.destroy = block_destroy;
    *out = &b->base;
    return FULTA_ARC_OK;
}

/* ---- incremental HMAC-SHA1 ---------------------------------------------------------------------------------- */

void fa_hmac_sha1_init(fa_hmac_sha1_t *h, const uint8_t *key, size_t keylen) {
    uint8_t k[64] = {0}, pad[64];
    if (keylen > 64) { fa_sha1_t c; fa_sha1_init(&c); fa_sha1_update(&c, key, keylen); fa_sha1_final(&c, k); }
    else if (keylen) memcpy(k, key, keylen);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    fa_sha1_init(&h->in); fa_sha1_update(&h->in, pad, 64);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5C;
    fa_sha1_init(&h->out); fa_sha1_update(&h->out, pad, 64);
}

void fa_hmac_sha1_update(fa_hmac_sha1_t *h, const void *data, size_t n) { fa_sha1_update(&h->in, data, n); }

void fa_hmac_sha1_final(fa_hmac_sha1_t *h, uint8_t out[20]) {
    uint8_t inner[20];
    fa_sha1_final(&h->in, inner);
    fa_sha1_update(&h->out, inner, 20);
    fa_sha1_final(&h->out, out);
}

/* A stream that passes bytes through and feeds them to an HMAC-SHA1 (for MACs over ciphertext). */
typedef struct tap_stream { fa_stream_t base; fa_stream_t *in; fa_hmac_sha1_t *mac; } tap_stream_t;

static fulta_arc_err_t tap_read(fa_stream_t *s, void *buf, size_t n, size_t *got) {
    tap_stream_t *t = (tap_stream_t *)s;
    fulta_arc_err_t e = t->in->read(t->in, buf, n, got);
    if (!e) fa_hmac_sha1_update(t->mac, buf, *got);
    return e;
}
static void tap_destroy(fa_stream_t *s) { tap_stream_t *t = (tap_stream_t *)s; fa_stream_destroy(t->in); fa_free(t); }

fulta_arc_err_t fa_stream_hmac_tap(fa_stream_t *in, fa_hmac_sha1_t *mac, fa_stream_t **out) {
    tap_stream_t *t = fa_calloc(1, sizeof *t);
    if (!t) { fa_stream_destroy(in); return FULTA_ARC_ERR_NOMEM; }
    t->in = in;
    t->mac = mac;
    t->base.read = tap_read;
    t->base.destroy = tap_destroy;
    *out = &t->base;
    return FULTA_ARC_OK;
}
