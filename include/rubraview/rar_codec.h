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
 *
 * Version 2 (rar-decoder clean-room session, 2026-10-02): unpacking says why
 * it failed, decodes a solid archive's stream to its end (spec 7.5), takes an
 * unknown size, and the table gained BLAKE2sp and HMAC-SHA-256 (spec 5.3,
 * 6.2.5).
 */

#define RUBRAVIEW_RAR_CODEC_VERSION 2u

/* An unpacked size the header does not know (spec 2.3, 3.5): decode to the
   stream's end-of-file signal. */
#define RUBRAVIEW_RAR_SIZE_UNKNOWN UINT64_MAX

/* The packed bytes come from `read` (as many as fit, 0 at the end) and the
   unpacked ones go to `write` (false stops the unpacking early). */
typedef size_t (*rubraview_rar_read_fn)(void *ctx, uint8_t *buffer, size_t capacity);
typedef bool (*rubraview_rar_write_fn)(void *ctx, const uint8_t *data, size_t size);

typedef enum rubraview_rar_unpack_status {
    RUBRAVIEW_RAR_UNPACK_OK = 0,
    RUBRAVIEW_RAR_UNPACK_CORRUPT,       /* the stream is broken, or ends before the size */
    RUBRAVIEW_RAR_UNPACK_UNSUPPORTED,   /* a method or a filter program it does not do */
    RUBRAVIEW_RAR_UNPACK_TOO_LARGE,     /* the dictionary is over `max_dict` */
    RUBRAVIEW_RAR_UNPACK_NO_MEMORY,
    RUBRAVIEW_RAR_UNPACK_STOPPED,       /* `write` returned false */
} rubraview_rar_unpack_status_t;

typedef struct rubraview_rar_unpack_params {
    unsigned method;        /* 15, 20, 26, 29, 36 (RAR 1.5-4.x) or 50, 70 (RAR 5) */
    bool     solid;         /* continue the previous file's window and state */
    bool     drain;         /* a solid archive: decode the stream to its end past dest_size (spec 7.5) */
    uint64_t dict_size;
    uint64_t dest_size;     /* the output is cut here; RUBRAVIEW_RAR_SIZE_UNKNOWN to run to the end */
} rubraview_rar_unpack_params_t;

typedef struct rubraview_rar_codec {
    uint32_t    version;            /* RUBRAVIEW_RAR_CODEC_VERSION */
    const char *name;               /* what it is, for the information window and the help */
    uint64_t    max_dict;           /* the largest dictionary it unpacks, bytes */

    /* Unpacking, one file at a time; a failed call leaves the state unfit
       to continue a solid stream (start again from a non-solid file). */
    void *(*unpack_create)(void);
    void  (*unpack_destroy)(void *unpack);
    rubraview_rar_unpack_status_t (*unpack_file)(void *unpack, const rubraview_rar_unpack_params_t *params,
                                                 rubraview_rar_read_fn read, void *read_ctx,
                                                 rubraview_rar_write_fn write, void *write_ctx);

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

    /* BLAKE2sp (spec 5.3), fed in pieces; final frees the state. */
    void *(*blake2sp_create)(void);
    void  (*blake2sp_update)(void *state, const void *data, size_t size);
    void  (*blake2sp_final)(void *state, uint8_t digest[32]);
    /* HMAC-SHA-256 (RFC 2104): RAR 5's BLAKE2sp of an encrypted file as stored. */
    void  (*hmac_sha256)(const uint8_t *key, size_t key_len, const void *data, size_t size, uint8_t mac[32]);
} rubraview_rar_codec_t;

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_RAR_CODEC_H */
