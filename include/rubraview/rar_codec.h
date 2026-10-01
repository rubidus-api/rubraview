#ifndef RUBRAVIEW_RAR_CODEC_H
#define RUBRAVIEW_RAR_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RAR's decompression and decryption, as a table rar.c asks for them; rar.c
 * reads the headers itself. The program uses no UnRAR code (owner,
 * 2026-10-01: "우리는 unrar 안쓸거니까요", D-70): the table is filled by a
 * decoder of this project's own, written from docs/specs/rar-decompression.md
 * and registered with rubraview_rar_set_codec. Until one is registered, a
 * stored, unencrypted CBR still opens; anything that needs decoding or a
 * password does not. This header is the whole contract.
 */

#define RUBRAVIEW_RAR_CODEC_VERSION 1u

/* The packed bytes come from `read` (as many as fit, 0 at the end) and the
   unpacked ones go to `write` (false stops the unpacking early). */
typedef size_t (*rubraview_rar_read_fn)(void *ctx, uint8_t *buffer, size_t capacity);
typedef bool (*rubraview_rar_write_fn)(void *ctx, const uint8_t *data, size_t size);

typedef struct rubraview_rar_codec {
    uint32_t    version;            /* RUBRAVIEW_RAR_CODEC_VERSION */
    const char *name;               /* what it is, for the information window and the help */
    uint64_t    max_dict;           /* the largest dictionary it unpacks, bytes */

    /* Unpacking: `method` 15, 20, 26, 29, 36 (RAR 1.5-4.x) or 50, 70
       (RAR 5); `solid` continues the previous file's window; the output is
       cut at `dest_size`. False when it cannot (unknown method, too large a
       dictionary, memory, data broken beyond use). */
    void *(*unpack_create)(void);
    void  (*unpack_destroy)(void *unpack);
    bool  (*unpack_file)(void *unpack, unsigned method, bool solid, uint64_t dict_size, uint64_t dest_size,
                         rubraview_rar_read_fn read, void *read_ctx, rubraview_rar_write_fn write, void *write_ctx);

    /* AES-CBC decryption, `key_bits` 128 or 256; the chain carries over
       between calls; `size` a multiple of 16. Destroy wipes the key. */
    void *(*aes_create)(const uint8_t *key, unsigned key_bits, const uint8_t iv[16]);
    void  (*aes_decrypt)(void *aes, uint8_t *data, size_t size);
    void  (*aes_destroy)(void *aes);

    /* RAR 3.x/4.x: the password as UTF-16LE bytes, an 8-byte salt or NULL;
       AES-128's key and initial vector. */
    void  (*kdf30)(const uint8_t *pwd16, size_t pwd16_len, const uint8_t *salt, uint8_t key[16], uint8_t iv[16]);
    /* RAR 5: the password as UTF-8, a 16-byte salt, log2 of the rounds;
       AES-256's key, the checksums' key, the 8-byte password check. */
    bool  (*kdf50)(const uint8_t *pwd8, size_t pwd8_len, const uint8_t salt[16], unsigned lg2_count,
                   uint8_t key[32], uint8_t hash_key[32], uint8_t psw_check[8]);
    /* RAR 5's CRC32 of an encrypted file as its header stores it. */
    uint32_t (*crc_to_mac)(uint32_t crc, const uint8_t hash_key[32]);
    void  (*sha256)(const void *data, size_t size, uint8_t digest[32]);
} rubraview_rar_codec_t;

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_RAR_CODEC_H */
