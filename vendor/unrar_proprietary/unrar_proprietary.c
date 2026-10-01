/*
 * unrar_proprietary — the module's one export: UnRAR 7.3.1's
 * decompression and decryption, converted to C (rar_unpack.c,
 * rar_crypt.c), behind the table Rubraview's include/rubraview/rar_codec.h
 * describes. Built on Windows as unrar_proprietary.dll.
 *
 * UnRAR source code may be used in any software to handle RAR archives
 * without limitations free of charge, but cannot be used to develop RAR
 * (WinRAR) compatible archiver and to re-create RAR compression algorithm,
 * which is proprietary. Distribution of modified UnRAR source code in
 * separate form or as a part of other software is permitted, provided that
 * full text of this paragraph, starting from "UnRAR source code" words, is
 * included in license, or in documentation if license is not available,
 * and in source code comments of resulting package.
 */
#include "unrar_proprietary.h"
#include "rar_unpack.h"
#include "rar_crypt.h"
#include <stdlib.h>

#if defined(_WIN32) && defined(UNRAR_PROPRIETARY_DLL)
#define UNRAR_EXPORT __declspec(dllexport)
#else
#define UNRAR_EXPORT
#endif

static void *unpack_create(void) { return rar_unpack_create(); }
static void unpack_destroy(void *u) { rar_unpack_destroy((rar_unpack_t*)u); }
static bool unpack_file(void *u, unsigned method, bool solid, uint64_t dict_size, uint64_t dest_size,
                        rubraview_rar_read_fn read, void *read_ctx, rubraview_rar_write_fn write, void *write_ctx) {
    return rar_unpack_file((rar_unpack_t*)u, method, solid, dict_size, dest_size, read, read_ctx, write, write_ctx);
}

static void *aes_create(const uint8_t *key, unsigned key_bits, const uint8_t iv[16]) {
    rar_aes_t *aes = (rar_aes_t*)malloc(sizeof(rar_aes_t));
    if (aes) rar_aes_init(aes, key, key_bits, iv);
    return aes;
}
static void aes_decrypt(void *aes, uint8_t *data, size_t size) { rar_aes_decrypt((rar_aes_t*)aes, data, size); }
static void aes_destroy(void *aes) {
    if (!aes) return;
    rar_wipe(aes, sizeof(rar_aes_t));
    free(aes);
}

static const rubraview_rar_codec_t CODEC = {
    .version = RUBRAVIEW_RAR_CODEC_VERSION,
    .name = "UnRAR 7.3.1, converted to C (UnRAR licence)",
    .max_dict = RAR_UNPACK_MAX_DICT,
    .unpack_create = unpack_create,
    .unpack_destroy = unpack_destroy,
    .unpack_file = unpack_file,
    .aes_create = aes_create,
    .aes_decrypt = aes_decrypt,
    .aes_destroy = aes_destroy,
    .kdf30 = rar_kdf30,
    .kdf50 = rar_kdf50,
    .crc_to_mac = rar_crc_to_mac,
    .sha256 = rar_sha256,
};

UNRAR_EXPORT const rubraview_rar_codec_t *unrar_proprietary_api(uint32_t version) {
    return version == RUBRAVIEW_RAR_CODEC_VERSION ? &CODEC : NULL;
}
