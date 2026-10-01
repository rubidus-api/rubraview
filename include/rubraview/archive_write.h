#ifndef RUBRAVIEW_ARCHIVE_WRITE_H
#define RUBRAVIEW_ARCHIVE_WRITE_H

#include "rubraview/core.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Writing ZIP / CBZ and 7z / CB7 archives (owner, 2026-10-01: "압축 지원은 7z와 zip까지"; R149: an
 * edited RAR is written as one of these). MIT, written for this project:
 *
 *   ZIP from PKWARE's APPNOTE (local headers, central directory, ZIP64 when sizes or offsets need it,
 *       the UTF-8 name flag, the extended-timestamp extra field); deflate by miniz's tdefl (MIT).
 *   7z  from the LZMA SDK's DOC/7zFormat.txt: one solid folder, LZMA (the SDK's LzmaEnc, public domain)
 *       or Copy at level 0, per-file CRCs, UTF-16 names, modification times.
 *
 * The data is in memory, entry by entry; the archive goes out through a sink, appended in order, with
 * one rewrite of bytes already written (7z's start header, which names where its header ended up).
 */

typedef enum rubraview_aw_format {
    RUBRAVIEW_AW_ZIP = 0,
    RUBRAVIEW_AW_7Z,
} rubraview_aw_format_t;

typedef enum rubraview_aw_err {
    RUBRAVIEW_AW_OK = 0,
    RUBRAVIEW_AW_ERR_BAD_NAME,       /* empty, absolute, a ".." part, or a control character */
    RUBRAVIEW_AW_ERR_BAD_ARGS,
    RUBRAVIEW_AW_ERR_WRITE,          /* the sink said no */
    RUBRAVIEW_AW_ERR_OUT_OF_MEMORY,
    RUBRAVIEW_AW_ERR_COMPRESS,       /* the compressor failed */
    RUBRAVIEW_AW_ERR_CANCELLED,      /* the progress callback said stop */
} rubraview_aw_err_t;

typedef struct rubraview_aw_entry {
    u8str_t        name;     /* UTF-8, folders separated by '/' ('\\' is taken as '/') */
    const uint8_t *data;     /* may be NULL when size is 0 */
    uint64_t       size;
    int64_t        mtime;    /* seconds since 1970-01-01 UTC; 0 when not known */
} rubraview_aw_entry_t;

typedef struct rubraview_aw_sink {
    void *ctx;
    /* Append bytes. False stops the writing. */
    bool (*write)(void *ctx, const uint8_t *data, size_t size);
    /* Write over bytes already appended, at `offset` from the start. */
    bool (*patch)(void *ctx, uint64_t offset, const uint8_t *data, size_t size);
} rubraview_aw_sink_t;

typedef struct rubraview_aw_options {
    int  level;              /* 0 stores (ZIP) / copies (7z); 1..9 compress; default 6 when out of range */
    bool zip64;              /* ZIP: ZIP64 records even when nothing needs them (tests) */
    /* Called as the bytes go in; false cancels. May be NULL. */
    bool (*progress)(void *ctx, uint64_t done, uint64_t total);
    void *progress_ctx;
} rubraview_aw_options_t;

/** Write `entries`, in their order, as one archive. `options` may be NULL (level 6). */
rubraview_aw_err_t rubraview_archive_write(rubraview_aw_format_t format, const rubraview_aw_entry_t *entries, size_t count,
                                           const rubraview_aw_options_t *options, const rubraview_aw_sink_t *sink);

/** A name as it would be stored ('/' only, no leading './'), or empty when it may not be. */
bool rubraview_aw_name_ok(u8str_t name);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_ARCHIVE_WRITE_H */
