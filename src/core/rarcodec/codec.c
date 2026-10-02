/*
 * Rubraview's RAR codec (MIT): the rar_codec.h table. Written from docs/specs/rar-decompression.md;
 * the primitives behind it from FIPS 197, FIPS 180-4, RFC 2104, RFC 8018, RFC 7693 and the BLAKE2 paper.
 */
#include "rubraview/rarcodec.h"
#include "rc_internal.h"
#include <stdlib.h>

static void *aes_create(const uint8_t *key, unsigned key_bits, const uint8_t iv[16]) {
    rc_aes_t *a = (rc_aes_t*)malloc(sizeof(*a));
    if (a && !rc_aes_init(a, key, key_bits, iv)) { free(a); a = NULL; }
    return a;
}

static void aes_decrypt(void *aes, uint8_t *data, size_t size) { rc_aes_cbc_decrypt((rc_aes_t*)aes, data, size); }

static void aes_destroy(void *aes) {
    if (!aes) return;
    rc_wipe(aes, sizeof(rc_aes_t));
    free(aes);
}

static void *b2_create(void) {
    rc_blake2sp_t *s = (rc_blake2sp_t*)malloc(sizeof(*s));
    if (s) rc_blake2sp_init(s);
    return s;
}

static void b2_update(void *s, const void *data, size_t size) { rc_blake2sp_update((rc_blake2sp_t*)s, data, size); }

static void b2_final(void *s, uint8_t digest[32]) {
    rc_blake2sp_final((rc_blake2sp_t*)s, digest);
    free(s);
}

static const rubraview_rar_codec_t CODEC = {
    .version = RUBRAVIEW_RAR_CODEC_VERSION,
    .name = "Rubraview RAR decoder (MIT, written from the project's RAR specification)",
    .max_dict = (uint64_t)1 << 36,   /* 64 GiB, the format's ceiling; memory decides below that */
    .unpack_create = rc_unpack_create,
    .unpack_destroy = rc_unpack_destroy,
    .unpack_file = rc_unpack_file,
    .aes_create = aes_create,
    .aes_decrypt = aes_decrypt,
    .aes_destroy = aes_destroy,
    .kdf30 = rc_kdf30,
    .kdf50 = rc_kdf50,
    .crc_to_mac = rc_crc_to_mac,
    .sha256 = rc_sha256,
    .blake2sp_create = b2_create,
    .blake2sp_update = b2_update,
    .blake2sp_final = b2_final,
    .hmac_sha256 = rc_hmac_sha256,
};

const rubraview_rar_codec_t *rubraview_rarcodec(void) { return &CODEC; }
