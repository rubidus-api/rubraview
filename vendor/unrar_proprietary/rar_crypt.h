/*
 * rar_crypt — the decryption RAR 3.x/4.x and RAR 5.0 archives use, converted
 * to C for Rubraview from UnRAR 7.3.1's source (crypt.cpp, crypt3.cpp,
 * crypt5.cpp, rijndael.cpp, sha1.cpp, sha256.cpp) by RARLAB, 2026-10-01.
 * Decryption only; RAR 1.5 and 2.0 encryption is not converted.
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
 * UnRAR's AES is based on Szymon Stefanek's public domain implementation;
 * its SHA-1 on Steve Reid's public domain one.
 */
#ifndef RAR_CRYPT_H
#define RAR_CRYPT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RAR_SALT30          8
#define RAR_SALT50          16
#define RAR_INITV           16
#define RAR_PSWCHECK        8
#define RAR_PSWCHECK_CSUM   4
#define RAR_KDF50_LG2_MAX   24     /* UnRAR's CRYPT5_KDF_LG2_COUNT_MAX */
#define RAR_MAX_PASSWORD    127    /* characters; longer ones are cut, as RAR cuts them */

/* ---- AES, CBC, decryption ---- */

typedef struct rar_aes {
    int     rounds;
    uint8_t key[15][4][4];          /* the expanded key, turned for decryption */
    uint8_t iv[16];                 /* the chain: the last cipher block */
} rar_aes_t;

/* `key_bits` 128 or 256. */
void rar_aes_init(rar_aes_t *aes, const uint8_t *key, unsigned key_bits, const uint8_t iv[16]);

/* In place; `size` a multiple of 16 (the rest is left alone). The chain
   carries over to the next call. */
void rar_aes_decrypt(rar_aes_t *aes, uint8_t *data, size_t size);

/* ---- hashes ---- */

typedef struct rar_sha1 {
    uint32_t state[5];
    uint64_t count;
    uint8_t  buffer[64];
} rar_sha1_t;

void rar_sha1_init(rar_sha1_t *c);
void rar_sha1_process(rar_sha1_t *c, const uint8_t *data, size_t len);
/* RAR 2.9's variant: whole blocks taken from `data` are written back into
   it, transformed, and later rounds hash what was written. RAR 3.x's key
   derivation depends on it. */
void rar_sha1_process_rar29(rar_sha1_t *c, uint8_t *data, size_t len);
void rar_sha1_done(rar_sha1_t *c, uint32_t digest[5]);

typedef struct rar_sha256 {
    uint32_t h[8];
    uint64_t count;
    uint8_t  buffer[64];
} rar_sha256_t;

void rar_sha256_init(rar_sha256_t *c);
void rar_sha256_process(rar_sha256_t *c, const void *data, size_t size);
void rar_sha256_done(rar_sha256_t *c, uint8_t digest[32]);
void rar_sha256(const void *data, size_t size, uint8_t digest[32]);

void rar_hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t digest[32]);

/* PBKDF2-HMAC-SHA256 for a 32-byte key after `rounds`, and the two values
   RAR 5 takes at rounds+16 (the checksums' key) and rounds+32 (the
   password check). */
void rar_pbkdf2(const uint8_t *pwd, size_t pwd_len, const uint8_t *salt, size_t salt_len,
                uint8_t key[32], uint8_t v1[32], uint8_t v2[32], uint32_t rounds);

/* ---- the two key derivations ---- */

/* RAR 3.x/4.x: the password as UTF-16LE bytes (`pwd16`, `pwd16_len` bytes),
   an 8-byte salt or NULL. AES-128's key and its initial vector. */
void rar_kdf30(const uint8_t *pwd16, size_t pwd16_len, const uint8_t *salt, uint8_t key[16], uint8_t iv[16]);

/* RAR 5.0: the password as UTF-8, a 16-byte salt, log2 of the rounds.
   AES-256's key, the checksums' key and the 8-byte password check. False
   when the rounds are more than RAR allows. */
bool rar_kdf50(const uint8_t *pwd8, size_t pwd8_len, const uint8_t salt[16], unsigned lg2_count,
               uint8_t key[32], uint8_t hash_key[32], uint8_t psw_check[8]);

/* RAR 5.0's CRC32 of an encrypted file, as the header stores it when the
   archive turns checksums into MACs (UnRAR's ConvertHashToMAC). */
uint32_t rar_crc_to_mac(uint32_t crc, const uint8_t hash_key[32]);

/* Zero memory in a way the compiler keeps. */
void rar_wipe(void *p, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* RAR_CRYPT_H */
