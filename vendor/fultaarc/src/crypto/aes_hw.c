/* aes_hw.c - FultaArc: AES-NI block operations with a software fallback. MIT.
 *
 * The round-key schedule and the single-block encrypt/decrypt use the x86 AES instructions (Intel AES-NI, as in
 * the Intel "AES Instructions Set" white paper, public). crypto.c calls fa_aes_hw_supported() once, verifies the
 * result against known answers (a self-test there), and only then routes fa_aes_encrypt/decrypt here; otherwise it
 * keeps its table-based software cipher. Only AES-128 and AES-256 are accelerated (192 and non-x86 stay software).
 *
 * Round keys travel as 16 bytes each in rk[rounds+1][16] (unaligned loads, so the caller needs no alignment). */
#include "../core/internal.h"

#if defined(__x86_64__) || defined(__i386__)

#include <immintrin.h>

int fa_aes_hw_supported(void) { return __builtin_cpu_supports("aes"); }

#define HW __attribute__((target("aes,sse4.1")))

HW static __m128i assist128(__m128i k, __m128i gen) {
    gen = _mm_shuffle_epi32(gen, 0xFF);
    __m128i t = _mm_slli_si128(k, 4);
    k = _mm_xor_si128(k, t);
    t = _mm_slli_si128(t, 4);
    k = _mm_xor_si128(k, t);
    t = _mm_slli_si128(t, 4);
    k = _mm_xor_si128(k, t);
    return _mm_xor_si128(k, gen);
}

HW static void assist256_1(__m128i *a, __m128i gen) {
    gen = _mm_shuffle_epi32(gen, 0xFF);
    __m128i t = _mm_slli_si128(*a, 4);
    *a = _mm_xor_si128(*a, t);
    t = _mm_slli_si128(t, 4);
    *a = _mm_xor_si128(*a, t);
    t = _mm_slli_si128(t, 4);
    *a = _mm_xor_si128(*a, t);
    *a = _mm_xor_si128(*a, gen);
}

HW static void assist256_2(__m128i a, __m128i *b) {
    __m128i gen = _mm_shuffle_epi32(_mm_aeskeygenassist_si128(a, 0), 0xAA);
    __m128i t = _mm_slli_si128(*b, 4);
    *b = _mm_xor_si128(*b, t);
    t = _mm_slli_si128(t, 4);
    *b = _mm_xor_si128(*b, t);
    t = _mm_slli_si128(t, 4);
    *b = _mm_xor_si128(*b, t);
    *b = _mm_xor_si128(*b, gen);
}

HW static void store(uint8_t *rk, int i, __m128i v) { _mm_storeu_si128((__m128i *)(rk + 16 * i), v); }

/* Build the encryption round keys for a 128- or 256-bit key; returns the round count, or 0 for an other width.
 * The rcon must be a compile-time immediate for aeskeygenassist, so the schedules are unrolled. */
HW static int expand_enc(const uint8_t *key, size_t keylen, uint8_t *rk) {
    if (keylen == 16) {
        __m128i k = _mm_loadu_si128((const __m128i *)key);
        store(rk, 0, k);
#define RK128(i, rcon) do { k = assist128(k, _mm_aeskeygenassist_si128(k, (rcon))); store(rk, (i), k); } while (0)
        RK128(1, 0x01); RK128(2, 0x02); RK128(3, 0x04); RK128(4, 0x08); RK128(5, 0x10);
        RK128(6, 0x20); RK128(7, 0x40); RK128(8, 0x80); RK128(9, 0x1B); RK128(10, 0x36);
#undef RK128
        return 10;
    }
    if (keylen == 32) {
        __m128i a = _mm_loadu_si128((const __m128i *)key);
        __m128i b = _mm_loadu_si128((const __m128i *)(key + 16));
        store(rk, 0, a);
        store(rk, 1, b);
#define E1(i, rcon) do { assist256_1(&a, _mm_aeskeygenassist_si128(b, (rcon))); store(rk, (i), a); } while (0)
#define E2(i)       do { assist256_2(a, &b); store(rk, (i), b); } while (0)
        E1(2, 0x01); E2(3); E1(4, 0x02); E2(5); E1(6, 0x04); E2(7); E1(8, 0x08); E2(9);
        E1(10, 0x10); E2(11); E1(12, 0x20); E2(13); E1(14, 0x40);
#undef E1
#undef E2
        return 14;
    }
    return 0;
}

/* Decryption round keys: InvMixColumns on the inner enc keys, order reversed, so aesdec runs from rk[0]. */
HW static int expand_dec(const uint8_t *key, size_t keylen, uint8_t *rk) {
    uint8_t enc[15 * 16];
    int nr = expand_enc(key, keylen, enc);
    if (!nr) return 0;
    store(rk, 0, _mm_loadu_si128((const __m128i *)(enc + 16 * nr)));
    for (int i = 1; i < nr; i++)
        store(rk, i, _mm_aesimc_si128(_mm_loadu_si128((const __m128i *)(enc + 16 * (nr - i)))));
    store(rk, nr, _mm_loadu_si128((const __m128i *)enc));
    return nr;
}

int fa_aes_hw_expand(const uint8_t *key, size_t keylen, int decrypt, uint8_t rk[15 * 16], int *rounds) {
    int nr = decrypt ? expand_dec(key, keylen, rk) : expand_enc(key, keylen, rk);
    if (rounds) *rounds = nr;
    return nr;
}

HW void fa_aes_hw_encrypt(const uint8_t *rk, int rounds, const uint8_t in[16], uint8_t out[16]) {
    __m128i m = _mm_xor_si128(_mm_loadu_si128((const __m128i *)in), _mm_loadu_si128((const __m128i *)rk));
    for (int i = 1; i < rounds; i++) m = _mm_aesenc_si128(m, _mm_loadu_si128((const __m128i *)(rk + 16 * i)));
    m = _mm_aesenclast_si128(m, _mm_loadu_si128((const __m128i *)(rk + 16 * rounds)));
    _mm_storeu_si128((__m128i *)out, m);
}

HW void fa_aes_hw_decrypt(const uint8_t *rk, int rounds, const uint8_t in[16], uint8_t out[16]) {
    __m128i m = _mm_xor_si128(_mm_loadu_si128((const __m128i *)in), _mm_loadu_si128((const __m128i *)rk));
    for (int i = 1; i < rounds; i++) m = _mm_aesdec_si128(m, _mm_loadu_si128((const __m128i *)(rk + 16 * i)));
    m = _mm_aesdeclast_si128(m, _mm_loadu_si128((const __m128i *)(rk + 16 * rounds)));
    _mm_storeu_si128((__m128i *)out, m);
}

/* Eight CTR keystream blocks at once (the throughput path): keystream[j] = AES(ctr0 + j) for j in 0..n-1, n <= 8.
 * `ctr0` is the 16-byte counter for the first block; the caller advances it by n after the call. */
HW void fa_aes_hw_ctr8(const uint8_t *rk, int rounds, const uint8_t ctr0[16], unsigned n, uint8_t *out) {
    __m128i c[8];
    uint8_t ctr[16];
    memcpy(ctr, ctr0, 16);
    for (unsigned j = 0; j < n; j++) {
        c[j] = _mm_xor_si128(_mm_loadu_si128((const __m128i *)ctr), _mm_loadu_si128((const __m128i *)rk));
        /* the counter is a little-endian 128-bit integer (WinZip / 7z AES): add 1, carry up */
        for (int b = 0; b < 16; b++) { if (++ctr[b]) break; }
    }
    for (int i = 1; i < rounds; i++) {
        __m128i k = _mm_loadu_si128((const __m128i *)(rk + 16 * i));
        for (unsigned j = 0; j < n; j++) c[j] = _mm_aesenc_si128(c[j], k);
    }
    __m128i kl = _mm_loadu_si128((const __m128i *)(rk + 16 * rounds));
    for (unsigned j = 0; j < n; j++) _mm_storeu_si128((__m128i *)(out + 16 * j), _mm_aesenclast_si128(c[j], kl));
}

/* Up to 8 CBC blocks decrypted at once (the aesdec pipeline hides its latency): out[j] = Dec(in[j]) ^ prev, where
 * prev is the previous ciphertext block (iv for the first). iv is advanced to the last ciphertext block. */
HW void fa_aes_hw_cbc_decrypt(const uint8_t *rk, int rounds, uint8_t iv[16], const uint8_t *in, uint8_t *out, unsigned nb) {
    __m128i c[8], d[8];
    for (unsigned j = 0; j < nb; j++) c[j] = _mm_loadu_si128((const __m128i *)(in + 16 * j));
    for (unsigned j = 0; j < nb; j++) d[j] = _mm_xor_si128(c[j], _mm_loadu_si128((const __m128i *)rk));
    for (int i = 1; i < rounds; i++) {
        __m128i k = _mm_loadu_si128((const __m128i *)(rk + 16 * i));
        for (unsigned j = 0; j < nb; j++) d[j] = _mm_aesdec_si128(d[j], k);
    }
    __m128i kl = _mm_loadu_si128((const __m128i *)(rk + 16 * rounds));
    __m128i prev = _mm_loadu_si128((const __m128i *)iv);
    for (unsigned j = 0; j < nb; j++) {
        __m128i p = _mm_xor_si128(_mm_aesdeclast_si128(d[j], kl), prev);
        _mm_storeu_si128((__m128i *)(out + 16 * j), p);
        prev = c[j];
    }
    _mm_storeu_si128((__m128i *)iv, prev);
}

#else /* not x86: no hardware AES */

int fa_aes_hw_supported(void) { return 0; }
int fa_aes_hw_expand(const uint8_t *key, size_t keylen, int decrypt, uint8_t rk[15 * 16], int *rounds) {
    (void)key; (void)keylen; (void)decrypt; (void)rk; if (rounds) *rounds = 0; return 0;
}
void fa_aes_hw_encrypt(const uint8_t *rk, int rounds, const uint8_t in[16], uint8_t out[16]) { (void)rk; (void)rounds; (void)in; (void)out; }
void fa_aes_hw_decrypt(const uint8_t *rk, int rounds, const uint8_t in[16], uint8_t out[16]) { (void)rk; (void)rounds; (void)in; (void)out; }
void fa_aes_hw_ctr8(const uint8_t *rk, int rounds, const uint8_t ctr0[16], unsigned n, uint8_t *out) { (void)rk; (void)rounds; (void)ctr0; (void)n; (void)out; }
void fa_aes_hw_cbc_decrypt(const uint8_t *rk, int rounds, uint8_t iv[16], const uint8_t *in, uint8_t *out, unsigned nb) { (void)rk; (void)rounds; (void)iv; (void)in; (void)out; (void)nb; }

#endif
