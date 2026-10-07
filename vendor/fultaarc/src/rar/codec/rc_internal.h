/*
 * Rubraview's RAR codec (MIT), written from docs/specs/rar-decompression.md; the cryptographic
 * primitives from FIPS 197 (AES), FIPS 180-4 (SHA-1, SHA-256), RFC 2104 (HMAC), RFC 8018 (PBKDF2),
 * RFC 7693 and the BLAKE2 paper (BLAKE2s, BLAKE2sp). Internal to src/core/rarcodec/.
 */
#ifndef FA_RARCODEC_INTERNAL_H
#define FA_RARCODEC_INTERNAL_H

#include "../rar_codec.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- checksums and hashes (crypto.c) ---- */

uint32_t rc_crc32(uint32_t crc, const void *data, size_t size);   /* crc = 0 to start (spec 5.1) */

typedef struct rc_sha1 {
    uint32_t h[5];
    uint64_t length;
    uint8_t block[64];
    size_t fill;
} rc_sha1_t;
void rc_sha1_init(rc_sha1_t *s);
void rc_sha1_update(rc_sha1_t *s, const void *data, size_t size);
void rc_sha1_final(const rc_sha1_t *s, uint8_t digest[20]);       /* `s` is left as it was */

typedef struct rc_sha256 {
    uint32_t h[8];
    uint64_t length;
    uint8_t block[64];
    size_t fill;
} rc_sha256_t;
void rc_sha256_init(rc_sha256_t *s);
void rc_sha256_update(rc_sha256_t *s, const void *data, size_t size);
void rc_sha256_final(rc_sha256_t *s, uint8_t digest[32]);
void rc_sha256(const void *data, size_t size, uint8_t digest[32]);
void rc_hmac_sha256(const uint8_t *key, size_t key_len, const void *data, size_t size, uint8_t mac[32]);

typedef struct rc_aes {
    uint32_t rk[60];   /* decryption round keys */
    unsigned rounds;
    uint8_t iv[16];
} rc_aes_t;
bool rc_aes_init(rc_aes_t *a, const uint8_t *key, unsigned key_bits, const uint8_t iv[16]);
void rc_aes_cbc_decrypt(rc_aes_t *a, uint8_t *data, size_t size);
void rc_aes_encrypt_block(const uint8_t *key, unsigned key_bits, const uint8_t in[16], uint8_t out[16]);  /* tests */

void rc_kdf30(const uint8_t *pwd16, size_t pwd16_len, const uint8_t *salt, uint8_t key[16], uint8_t iv[16]);
bool rc_kdf50(const uint8_t *pwd8, size_t pwd8_len, const uint8_t salt[16], unsigned lg2_count,
              uint8_t key[32], uint8_t hash_key[32], uint8_t psw_check[8]);
uint32_t rc_crc_to_mac(uint32_t crc, const uint8_t hash_key[32]);

typedef struct rc_blake2s {
    uint32_t h[8];
    uint32_t t[2];
    uint8_t buf[64];
    size_t fill;
    bool last_node;
} rc_blake2s_t;

typedef struct rc_blake2sp {
    rc_blake2s_t leaf[8];
    uint8_t buf[512];     /* up to eight 64-byte chunks not yet given to the leaves */
    size_t fill;
} rc_blake2sp_t;
void rc_blake2sp_init(rc_blake2sp_t *s);
void rc_blake2sp_update(rc_blake2sp_t *s, const void *data, size_t size);
void rc_blake2sp_final(rc_blake2sp_t *s, uint8_t digest[32]);

void rc_wipe(void *p, size_t n);

/* ---- unpacking (unpack.c and the per-version files) ---- */

fa_rar_unpack_status_t rc_unpack_file(void *unpack, const fa_rar_unpack_params_t *params,
                                             fa_rar_read_fn read, void *read_ctx,
                                             fa_rar_write_fn write, void *write_ctx);
void *rc_unpack_create(void);
void rc_unpack_destroy(void *unpack);

#endif
