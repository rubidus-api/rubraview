#ifndef RUBRAVIEW_SEVENZIP_H
#define RUBRAVIEW_SEVENZIP_H

#include "rubraview/core.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * CB7 (7-Zip) virtual archive reading (RFC-0001 §3.8.2, §10.2, owner
 * decision D-3, RV-052), on the vendored LZMA SDK (`vendor/lzma`,
 * public domain).
 *
 * A 7z archive differs from a ZIP in one way that decides this whole
 * interface: its files are not compressed one by one. They are grouped
 * into *folders* — the format's word for a solid block — and a folder
 * decompresses as a single unit. Reading page 3 of a solid block that
 * holds pages 1 to 40 costs the whole block; reading page 4 afterwards
 * should cost nothing. So the decoded block is kept between calls, which
 * is what §3.8.2 means by a persistent decoder, and what makes flipping
 * through a solid CB7 bearable.
 *
 * That same property is why the §10.2 size guard here is not the file's
 * size but the *folder's*: the folder is what gets allocated, and a
 * small page inside a huge solid block is still a huge allocation.
 *
 * The archive borrows the caller's buffer and never writes to disk.
 * Unlike the ZIP reader, this one owns heap memory — the SDK's index and
 * the decoded block — so `rubraview_sz_close` is not optional.
 */

typedef enum rubraview_sz_err {
    RUBRAVIEW_SZ_OK = 0,
    RUBRAVIEW_SZ_ERR_NOT_A_7Z,          /* the 6-byte signature is absent */
    RUBRAVIEW_SZ_ERR_CORRUPT,           /* the header does not parse */
    RUBRAVIEW_SZ_ERR_CORRUPT_STREAM,    /* the block decoded, but its CRC does not match */
    RUBRAVIEW_SZ_ERR_UNSUPPORTED_CODER, /* an encrypted or unknown coder — see the note in the .c */
    RUBRAVIEW_SZ_ERR_TOO_LARGE,         /* the solid block exceeds the caller's cap, §10.2 */
    RUBRAVIEW_SZ_ERR_OUT_OF_MEMORY,
    RUBRAVIEW_SZ_ERR_BAD_INDEX,
} rubraview_sz_err_t;

typedef struct rubraview_sz_entry {
    u8str_t  name;        /* UTF-8, converted from the archive's UTF-16 */
    uint64_t size;        /* uncompressed */
    uint32_t file_index;  /* the index inside the archive, which is not the entry index */
} rubraview_sz_entry_t;

typedef struct rubraview_sz_archive {
    const uint8_t *data;   /* borrowed; the caller must outlive the archive */
    size_t          size;
    rubraview_sz_entry_t *entries; /* arena-allocated; directories are not listed */
    size_t          entry_count;
    uint64_t        max_folder_bytes; /* the §10.2 cap, remembered from open */
    void           *state;            /* heap-owned SDK state; freed by rubraview_sz_close */
} rubraview_sz_archive_t;

typedef struct rubraview_sz_result {
    rubraview_sz_err_t     err;
    rubraview_sz_archive_t value;
} rubraview_sz_result_t;

/**
 * Index a 7z archive held in memory. `max_folder_bytes` is the §10.2
 * guard applied later, when a block is actually decoded — indexing
 * itself allocates nothing proportional to the content.
 */
[[nodiscard]] rubraview_sz_result_t rubraview_sz_open(proven_arena_t *arena,
                                                      const uint8_t *data, size_t size,
                                                      uint64_t max_folder_bytes);

typedef struct rubraview_sz_data_result {
    rubraview_sz_err_t err;
    u8str_t             data; /* copied into the caller's arena on success */
} rubraview_sz_data_result_t;

/**
 * Read one entry. The solid block holding it is decoded if it is not
 * already the one in hand, and kept for the next call — reading a run of
 * pages out of one block decodes it once.
 *
 * Both caps are checked before anything is allocated: the block against
 * `max_folder_bytes` from open, and this entry against `max_entry_bytes`.
 */
[[nodiscard]] rubraview_sz_data_result_t rubraview_sz_read_entry(proven_arena_t *arena,
                                                                  rubraview_sz_archive_t *archive,
                                                                  size_t entry_index,
                                                                  uint64_t max_entry_bytes);

/** Release the SDK index and the cached block. Safe on a failed open. */
void rubraview_sz_close(rubraview_sz_archive_t *archive);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_SEVENZIP_H */
