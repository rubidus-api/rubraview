#ifndef RUBRAVIEW_RAR_H
#define RUBRAVIEW_RAR_H

#include "rubraview/core.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * CBR / RAR archives (owner, 2026-09-30, plan 2026-09-30-rar-reader):
 * RAR 1.5-4.x and RAR 5.0/7.0, stored and compressed, solid or not,
 * decoded by the C conversion of UnRAR in vendor/unrar-c. Encrypted
 * archives and files, and files split across volumes, are listed as such
 * and refused. Shaped like the 7z reader: open lists, read decodes one
 * entry (a solid archive from the start of its chain, or on from where the
 * last read stopped), and a long read can be cancelled and watched.
 */

typedef enum rubraview_rar_err {
    RUBRAVIEW_RAR_OK = 0,
    RUBRAVIEW_RAR_ERR_NOT_A_RAR,
    RUBRAVIEW_RAR_ERR_CORRUPT,          /* the headers do not parse */
    RUBRAVIEW_RAR_ERR_CORRUPT_STREAM,   /* decoded, but short or its CRC does not match */
    RUBRAVIEW_RAR_ERR_ENCRYPTED,        /* headers or the file need a password */
    RUBRAVIEW_RAR_ERR_UNSUPPORTED,      /* split across volumes, or an unknown method */
    RUBRAVIEW_RAR_ERR_TOO_LARGE,
    RUBRAVIEW_RAR_ERR_OUT_OF_MEMORY,
    RUBRAVIEW_RAR_ERR_BAD_INDEX,
    RUBRAVIEW_RAR_ERR_CANCELLED,
} rubraview_rar_err_t;

typedef struct rubraview_rar_entry {
    u8str_t  name;          /* '/' between folders; UTF-8 unless `name_is_legacy` */
    bool     name_is_legacy;/* RAR 4 without Unicode: the bytes of a code page, as ZIP's are */
    uint64_t size, packed_size;
    uint64_t data_offset;   /* in the archive */
    uint32_t crc32;
    bool     has_crc;
    unsigned method;        /* 0 stored; else the decoder's: 15, 20, 26, 29, 36, 50, 70 */
    uint64_t dict_size;
    bool     solid;         /* continues the previous file's stream */
    bool     encrypted;
    bool     split;         /* part of it is in another volume */
} rubraview_rar_entry_t;

typedef struct rubraview_rar_archive {
    const uint8_t *data;    /* borrowed; must outlive the archive */
    size_t size;
    bool rar5, solid_archive, headers_encrypted;
    rubraview_rar_entry_t *entries;   /* files only, in the archive's order */
    size_t entry_count;
    void *state;            /* the decoder and where a solid stream got to; heap */
} rubraview_rar_archive_t;

typedef struct rubraview_rar_result {
    rubraview_rar_err_t err;
    rubraview_rar_archive_t value;
} rubraview_rar_result_t;

/** A RAR signature at the start (or in the first megabyte, after an SFX stub). */
bool rubraview_rar_is_rar(const uint8_t *data, size_t size);

[[nodiscard]] rubraview_rar_result_t rubraview_rar_open(proven_arena_t *arena, const uint8_t *data, size_t size);

typedef struct rubraview_rar_data_result {
    rubraview_rar_err_t err;
    u8str_t data;           /* in the caller's arena on success */
} rubraview_rar_data_result_t;

[[nodiscard]] rubraview_rar_data_result_t rubraview_rar_read_entry(proven_arena_t *arena, rubraview_rar_archive_t *archive,
                                                                    size_t index, uint64_t max_entry_bytes);

/** Bytes to be decoded to read `index` now (the chain before it in a solid archive included). */
uint64_t rubraview_rar_read_cost(const rubraview_rar_archive_t *archive, size_t index);

/** Stop a read under way from another thread (true), or allow reads again (false). */
void rubraview_rar_cancel(rubraview_rar_archive_t *archive, bool cancel);

/** How far the read under way has got, in decoded bytes. */
void rubraview_rar_progress(const rubraview_rar_archive_t *archive, uint64_t *out_done, uint64_t *out_total);

void rubraview_rar_close(rubraview_rar_archive_t *archive);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_RAR_H */
