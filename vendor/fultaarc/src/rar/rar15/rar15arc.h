/* rar15arc.h - reader for RAR 1.5 archives on top of proven_c_lib: the container of docs/spec/rar15.md section 1
 * (blocks, volumes, solid archives, stored files) around the decompressor of unpack15.h (section 2).
 *
 * Memory comes from the proven_allocator_t given at open; files are read and written through proven's fs layer;
 * checks use proven_crc32; names and paths are proven u8str views. unpack15.c itself has no dependency.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 the fultaarc-cleanroom-rar15 authors
 *
 * Usage:
 *   rar15_archive_t a;
 *   proven_err_t e = rar15_archive_open_path(&a, proven_heap_allocator(), PROVEN_LIT("T.RAR"), 0);
 *   for (proven_size_t i = 0; i < rar15_archive_count(&a); i++) {
 *       rar15_extracted_t x = rar15_archive_extract_alloc(&a, i);      - x.err: RAR15_OK, RAR15_E_CRC, ...
 *       ... x.data ...
 *       rar15_extracted_free(&a, &x);
 *   }
 *   rar15_archive_close(&a);
 * In a solid archive (1.7) entries depend on the ones before them: extract in rising order; asking for an
 * earlier entry again decodes from the first one.
 */
#ifndef RAR15ARC_H
#define RAR15ARC_H

#include "proven/allocator.h"
#include "proven/memory.h"
#include "proven/types.h"
#include "proven/u8str.h"

#include "unpack15.h"

/* Results are proven_err_t values (below PROVEN_ERR_RESERVED_END) or one of these. */
typedef proven_i32 rar15_err_t;
enum {
    RAR15_E_NOT_ARCHIVE = PROVEN_ERR_RESERVED_END, /* the marker of 1.1 is not there */
    RAR15_E_HEADER,                                /* a block that cannot be walked (1.1, 1.4) */
    RAR15_E_ENCRYPTED,                             /* file flag 0x0004 (1.4): not decoded */
    RAR15_E_VERSION,                               /* the byte at 0x18 is not 0x0f: another format version */
    RAR15_E_DATA,                                  /* the compressed stream is damaged or cut (unpack_status) */
    RAR15_E_CRC,                                   /* the output's CRC-32 differs from the header's (1.4) */
    RAR15_E_INCOMPLETE                             /* a part of the entry lies in a volume that was not given (1.6) */
};

enum {
    RAR15_MAIN_VOLUME = 0x0001, /* 1.3 */
    RAR15_MAIN_COMMENT = 0x0002,
    RAR15_MAIN_LOCKED = 0x0004,
    RAR15_MAIN_SOLID = 0x0008,
    RAR15_MAIN_AUTHENTICITY = 0x0020,
    RAR15_FILE_FROM_PREV = 0x0001, /* 1.4 */
    RAR15_FILE_TO_NEXT = 0x0002,
    RAR15_FILE_PASSWORD = 0x0004,
    RAR15_FILE_COMMENT = 0x0008,
    RAR15_ATTR_DIRECTORY = 0x10
};

typedef struct {
    proven_u8str_view_t name; /* as stored: DOS code page bytes, '\' between path parts (not validated as UTF-8) */
    proven_u32 size;          /* unpacked size */
    proven_u32 packed_size;   /* over all volumes given */
    proven_u32 crc;           /* CRC-32 of the unpacked file (of the last part given); in the older format the
                               * 16-bit sum of rar14.md 1.3 */
    proven_u16 dos_time, dos_date;
    proven_u32 attr;
    proven_u16 flags;         /* of the last part given */
    proven_u8 method;         /* 0x30 stored, anything else compressed (1.4); older format: 0 stored */
    proven_u8 version;        /* 0x0f in what RAR 1.5x writes; 2 in the older format (rar14.md) */
    bool header_check_ok;     /* 1.2, of the first part */
    bool truncated;           /* the file ended inside this entry's packed data */
    proven_mem_view_t comment_block; /* 1.4: the inner comment block after the name (file flag 0x0008), or empty */
    /* internal */
    proven_mem_view_t packed; /* the packed bytes, joined over the volumes */
    proven_byte_t *joined;    /* owned copy when the entry spans volumes */
    proven_size_t joined_cap;
} rar15_entry_t;

typedef struct {
    proven_allocator_t alloc;
    proven_u16 main_flags;      /* as in 1.3 for both formats (the older format's flag byte is mapped: rar14.md 1.2) */
    bool old_comment_packed;    /* older format: main flag 0x10 */
    bool old_format;            /* the older container (marker 52 45 7e 5e, RAR 1.40: docs/spec/rar14.md) */
    bool main_check_ok;
    proven_mem_view_t comment_block; /* 1.3: the inner comment block of the main header (flag 0x0002), or empty */
    proven_mem_view_t av_block;      /* 1.9: the authenticity block (type 0x76) when main flag 0x0020 points at one;
                                      * recognised only: its contents cannot be verified here */
    proven_size_t sfx_size;          /* bytes before the marker in the first volume (a self-extracting module, 1.1) */
    rar15_entry_t *entries;
    proven_size_t count, cap;
    proven_mem_mut_t *owned;    /* volume buffers read by rar15_archive_open_path(s) */
    proven_size_t owned_count;
    rar15_state *state;         /* decoder state (2.4), allocated on first use */
    proven_size_t next;         /* solid: the entry the state is ready for */
    bool usable;                /* solid: the state follows the archive so far */
    proven_u8str_view_t password; /* rar15_archive_set_password; used for the older format only (rar14.md 1.7) */
    proven_size_t max_unpacked; /* when not 0: extract_alloc refuses an entry that needs a larger buffer */
} rar15_archive_t;

typedef struct {
    rar15_err_t err;         /* RAR15_OK or what went wrong; with RAR15_E_CRC and RAR15_E_DATA `data` is still set */
    int unpack_status;       /* enum rar15_status of the decompressor (unpack15.h); 0 for stored entries */
    proven_mem_mut_t data;   /* the bytes produced (size <= the entry's size) */
    proven_size_t cap;       /* internal: allocated size */
} rar15_extracted_t;

/* Open an archive held in memory: `volumes` in order (T.RAR, T.R00, ...; one for a plain archive). The memory is
 * borrowed and must stay valid until rar15_archive_close. */
[[nodiscard]] rar15_err_t rar15_archive_open_mem(rar15_archive_t *a, proven_allocator_t alloc,
                                                 const proven_mem_view_t *volumes, proven_size_t nvol);

/* Open the given volume files, in order. Each is read whole (at most max_bytes each; 0 = no limit). */
[[nodiscard]] rar15_err_t rar15_archive_open_paths(rar15_archive_t *a, proven_allocator_t alloc,
                                                   const proven_u8str_view_t *paths, proven_size_t npaths,
                                                   proven_size_t max_bytes);

/* Open an archive by the path of its first volume; when it is a volume (main flag 0x0001) the files NAME.R00,
 * NAME.R01, ... beside it are read as long as they exist (1.6). */
[[nodiscard]] rar15_err_t rar15_archive_open_path(rar15_archive_t *a, proven_allocator_t alloc, proven_u8str_view_t path,
                                                  proven_size_t max_bytes);

void rar15_archive_close(rar15_archive_t *a);

/* The password for encrypted entries (file flag 0x0004). It is borrowed: keep the bytes valid while entries are
 * extracted. Only the older format's encryption is known (docs/spec/rar14.md 1.7); an encrypted entry of a 1.5
 * archive still gives RAR15_E_ENCRYPTED. A wrong password shows as RAR15_E_CRC or RAR15_E_DATA. */
void rar15_archive_set_password(rar15_archive_t *a, proven_u8str_view_t password);

static inline proven_size_t rar15_archive_count(const rar15_archive_t *a) { return a->count; }
static inline const rar15_entry_t *rar15_archive_entry(const rar15_archive_t *a, proven_size_t i) {
    return i < a->count ? &a->entries[i] : (const rar15_entry_t *)0;
}
static inline bool rar15_entry_is_dir(const rar15_entry_t *e) { return (e->attr & RAR15_ATTR_DIRECTORY) != 0; }

/* Decode entry i into dest (dest.size >= the entry's size). The result's data is the part of dest written. */
/* (An entry whose header claims more than its packed data can unpack to needs only that much: rar15arc.c.) */
[[nodiscard]] rar15_extracted_t rar15_archive_extract(rar15_archive_t *a, proven_size_t i, proven_mem_mut_t dest);

/* The same into a buffer taken from the archive's allocator; give it back with rar15_extracted_free. The buffer
 * is never larger than the packed data can fill, whatever the header says. */
[[nodiscard]] rar15_extracted_t rar15_archive_extract_alloc(rar15_archive_t *a, proven_size_t i);
void rar15_extracted_free(rar15_archive_t *a, rar15_extracted_t *x);

/* Comments (1.3, 1.4). The text is decoded into a buffer from the archive's allocator (give it back with
 * rar15_extracted_free); the decoder state of the files is not touched. err: PROVEN_ERR_NOT_FOUND when there is no
 * comment, RAR15_E_HEADER for a block that cannot be read, RAR15_E_CRC when the text's check differs (the text is
 * still returned). The text is in the DOS code page of whoever wrote it; lines end with CR LF. */
[[nodiscard]] rar15_extracted_t rar15_archive_comment(rar15_archive_t *a);
[[nodiscard]] rar15_extracted_t rar15_entry_comment(rar15_archive_t *a, proven_size_t i);

/* A short English text for a result value (proven's codes included). */
const char *rar15_err_text(rar15_err_t e);

#endif
