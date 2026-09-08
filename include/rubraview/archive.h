#ifndef RUBRAVIEW_ARCHIVE_H
#define RUBRAVIEW_ARCHIVE_H

#include "rubraview/core.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * CBZ (ZIP) virtual archive streaming (RFC-0001 §3.8.1, §10.2): parses
 * only the End-of-Central-Directory and Central Directory records (an
 * O(1) index relative to archive size). STORED entries are returned as a
 * zero-copy view into the caller-owned buffer — "decompressing" a stored
 * entry is the identity function, so there is nothing to copy — and
 * DEFLATE entries are inflated into the caller's arena through the
 * vendored miniz (RV-051, owner decision D-2). CB7 is RV-052 and CBR is
 * post-1.0 (D-3).
 *
 * The archive never writes to disk and never copies the whole archive
 * into memory — the caller owns `data` for as long as the archive (and
 * any slice returned by rubraview_zip_read_stored) is used.
 */

typedef enum rubraview_zip_err {
    RUBRAVIEW_ZIP_OK = 0,
    RUBRAVIEW_ZIP_ERR_NOT_A_ZIP,             /* no End-of-Central-Directory signature found */
    RUBRAVIEW_ZIP_ERR_TRUNCATED,             /* a record or its payload runs past the buffer end */
    RUBRAVIEW_ZIP_ERR_CORRUPT,               /* a record signature or length is internally inconsistent */
    RUBRAVIEW_ZIP_ERR_UNSUPPORTED_COMPRESSION, /* entry method is neither STORED (0) nor DEFLATE (8) */
    RUBRAVIEW_ZIP_ERR_CORRUPT_STREAM,          /* the compressed stream did not inflate to its declared size */
    RUBRAVIEW_ZIP_ERR_TOO_LARGE,             /* entry uncompressed size exceeds the caller's cap, §10.2 */
    RUBRAVIEW_ZIP_ERR_OUT_OF_MEMORY,
    RUBRAVIEW_ZIP_ERR_BAD_INDEX,
} rubraview_zip_err_t;

typedef struct rubraview_zip_entry {
    u8str_t  name;               /* raw bytes from the Central Directory; encoding per RV-026 (utf8_flag + rubraview_archive_filename_detect) */
    uint32_t uncompressed_size;
    uint32_t compressed_size;
    uint16_t compression_method; /* 0 = STORED, 8 = deflate (indexed, not yet readable) */
    uint32_t local_header_offset;
    bool     utf8_flag;          /* ZIP General Purpose Bit 11 */
} rubraview_zip_entry_t;

typedef struct rubraview_zip_archive {
    const uint8_t       *data;   /* borrowed; caller must outlive the archive */
    size_t                size;
    rubraview_zip_entry_t *entries; /* arena-allocated, in Central Directory order */
    size_t                entry_count;
} rubraview_zip_archive_t;

typedef struct rubraview_zip_result {
    rubraview_zip_err_t     err;
    rubraview_zip_archive_t value;
} rubraview_zip_result_t;

/**
 * Index a ZIP archive's Central Directory. Entries are returned in their
 * on-disk order — pages are typically already numerically sorted on disk,
 * but callers that need guaranteed natural order should sort entry names
 * with rubraview_sort_paths before display.
 */
[[nodiscard]] rubraview_zip_result_t rubraview_zip_open(proven_arena_t *arena, const uint8_t *data, size_t size);

typedef struct rubraview_zip_data_result {
    rubraview_zip_err_t err;
    u8str_t              data; /* zero-copy view into the archive's `data` buffer on success */
} rubraview_zip_data_result_t;

/**
 * Read a STORED entry's bytes as a zero-copy view. Rejects deflate
 * entries and any entry whose uncompressed size exceeds
 * `max_uncompressed_bytes` (the §10.2 per-frame size cap; pass
 * SIZE_MAX-equivalent, e.g. UINT32_MAX, to disable the cap).
 */
[[nodiscard]] rubraview_zip_data_result_t rubraview_zip_read_stored(const rubraview_zip_archive_t *zip, size_t entry_index, uint32_t max_uncompressed_bytes);

/**
 * Read any supported entry, whichever way it is stored: a STORED entry
 * comes back as the same zero-copy view rubraview_zip_read_stored gives,
 * and a DEFLATE entry is inflated into `arena`. This is the call the
 * page loader uses; the stored-only variant remains for callers that
 * must not allocate.
 *
 * `max_uncompressed_bytes` is the §10.2 zip-bomb guard and is enforced
 * before any memory is committed — the declared size is checked first,
 * and the inflate is then held to exactly that many bytes, so a lying
 * header cannot make the decompressor run away.
 */
[[nodiscard]] rubraview_zip_data_result_t rubraview_zip_read_entry(proven_arena_t *arena,
                                                                   const rubraview_zip_archive_t *zip,
                                                                   size_t entry_index,
                                                                   uint32_t max_uncompressed_bytes);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_ARCHIVE_H */
