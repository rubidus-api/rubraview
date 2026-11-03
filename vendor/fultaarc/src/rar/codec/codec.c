/*
 * Rubraview's RAR codec (MIT): the rar_codec.h table. Written from docs/specs/rar-decompression.md;
 * the primitives behind it from FIPS 197, FIPS 180-4, RFC 2104, RFC 8018, RFC 7693 and the BLAKE2 paper.
 */
#include "../rarcodec.h"
#include "rc_internal.h"
#include <stdlib.h>

/* The objects carry the allocator they came from (the state first, so the codec's pointer is the object's). */
typedef struct { rc_aes_t aes; proven_allocator_t alloc; } rc_aes_obj_t;
typedef struct { rc_blake2sp_t b2; proven_allocator_t alloc; } rc_b2_obj_t;

static void *aes_create(proven_allocator_t alloc, const uint8_t *key, unsigned key_bits, const uint8_t iv[16]) {
    rc_aes_obj_t *a = fa_alloc(alloc, sizeof(*a));
    if (!a) return NULL;
    a->alloc = alloc;
    if (!rc_aes_init(&a->aes, key, key_bits, iv)) { fa_release(alloc, a); return NULL; }
    return a;
}

static void aes_decrypt(void *aes, uint8_t *data, size_t size) { rc_aes_cbc_decrypt(&((rc_aes_obj_t*)aes)->aes, data, size); }

static void aes_destroy(void *aes) {
    if (!aes) return;
    proven_allocator_t alloc = ((rc_aes_obj_t*)aes)->alloc;
    rc_wipe(aes, sizeof(rc_aes_obj_t));
    fa_release(alloc, aes);
}

static void *b2_create(proven_allocator_t alloc) {
    rc_b2_obj_t *s = fa_alloc(alloc, sizeof(*s));
    if (!s) return NULL;
    s->alloc = alloc;
    rc_blake2sp_init(&s->b2);
    return s;
}

static void b2_update(void *s, const void *data, size_t size) { rc_blake2sp_update(&((rc_b2_obj_t*)s)->b2, data, size); }

static void b2_final(void *s, uint8_t digest[32]) {
    rc_blake2sp_final(&((rc_b2_obj_t*)s)->b2, digest);
    fa_release(((rc_b2_obj_t*)s)->alloc, s);
}

static const fa_rar_codec_t CODEC = {
    .version = FA_RAR_CODEC_VERSION,
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

const fa_rar_codec_t *fa_rarcodec(void) { return &CODEC; }
