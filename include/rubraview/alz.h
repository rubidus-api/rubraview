/*
 * ALZ archive reading. Written from docs/specs/alz-format.md, with RFC 1951
 * (deflate), bzip2's own documentation and source (bzip2 licence; the
 * decoder is vendor/bzip2, its framing changed as the spec's section 3.2
 * says) and PKWARE's APPNOTE, "Traditional PKWARE Encryption".
 * Licence: MIT (the project's, see LICENSE).
 */
#ifndef RUBRAVIEW_ALZ_H
#define RUBRAVIEW_ALZ_H

#include "rubraview/core.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ALZ archives (ESTsoft's ALZip, before it wrote EGG), shaped like the RAR
 * and 7z readers: open lists, read decodes one entry into the caller's
 * arena. Every file is compressed on its own (no solid mode), stored,
 * deflated or in ALZ's cut-down bzip2, and may be encrypted with the
 * traditional ZIP cipher. Names are left as the raw bytes of the code page
 * the archive was made in (CP949 in practice) for the caller's code-page
 * handling (rubraview_archive_filename_plan), `\` between folders.
 *
 * A split archive (`x.alz`, `x.a00`, `x.a01`, ...) is opened from all its
 * volumes, which join into one stream (spec section 2). The listing stops at
 * the first damage; what came before it can still be read.
 */

typedef enum rubraview_alz_err {
    RUBRAVIEW_ALZ_OK = 0,
    RUBRAVIEW_ALZ_ERR_NOT_AN_ALZ,      /* no "ALZ" 01 at the start */
    RUBRAVIEW_ALZ_ERR_CORRUPT_STREAM,  /* decoded, but short, malformed, or its CRC-32 does not match */
    RUBRAVIEW_ALZ_ERR_TRUNCATED,       /* its data runs past the end of the archive (or of the volumes there) */
    RUBRAVIEW_ALZ_ERR_ENCRYPTED,       /* it needs a password and none has been given */
    RUBRAVIEW_ALZ_ERR_BAD_PASSWORD,    /* the password given is not this file's */
    RUBRAVIEW_ALZ_ERR_UNSUPPORTED,     /* a compression method the spec does not describe */
    RUBRAVIEW_ALZ_ERR_TOO_LARGE,       /* over the caller's cap */
    RUBRAVIEW_ALZ_ERR_OUT_OF_MEMORY,
    RUBRAVIEW_ALZ_ERR_BAD_INDEX,
} rubraview_alz_err_t;

/* A volume: its bytes, borrowed; they must outlive the archive. */
typedef struct rubraview_alz_volume {
    const uint8_t *data;
    size_t size;
} rubraview_alz_volume_t;

enum {
    RUBRAVIEW_ALZ_METHOD_STORED = 0,
    RUBRAVIEW_ALZ_METHOD_BZIP2 = 1,    /* ALZ-bzip2, spec section 3.2 */
    RUBRAVIEW_ALZ_METHOD_DEFLATE = 2,  /* raw deflate */
};

typedef struct rubraview_alz_entry {
    u8str_t  name;          /* raw bytes in the maker's code page; `\` (or `/`) between folders */
    uint64_t size, packed_size;
    uint64_t data_offset;   /* in the joined stream, after the 12-byte encryption header */
    uint32_t crc32;
    uint32_t dos_time;      /* MS-DOS date (high 16 bits) and time (low 16) */
    uint8_t  attributes;    /* 0x01 read-only, 0x02 hidden, 0x10 folder, 0x20 file */
    uint8_t  descriptor;
    unsigned method;        /* RUBRAVIEW_ALZ_METHOD_*; others are listed but not read */
    bool     encrypted;
    bool     truncated;     /* its data runs past the end of what is there */
} rubraview_alz_entry_t;

typedef struct rubraview_alz_archive {
    const rubraview_alz_volume_t *volumes;   /* all of them, in order (a copy in the arena) */
    size_t volume_count;
    uint64_t size;          /* of the joined stream */
    rubraview_alz_entry_t *entries;          /* files only (no folders), in the archive's order */
    size_t entry_count;
    bool damaged;           /* the listing stopped at damage, before the end record */
    void *state;            /* the password, while one is set; heap */
} rubraview_alz_archive_t;

typedef struct rubraview_alz_result {
    rubraview_alz_err_t err;
    rubraview_alz_archive_t value;
} rubraview_alz_result_t;

/** "ALZ" 01 at the start: the signature decides, not the extension. */
bool rubraview_alz_is_alz(const uint8_t *data, size_t size);

[[nodiscard]] rubraview_alz_result_t rubraview_alz_open(proven_arena_t *arena, const uint8_t *data, size_t size);

/** Open from every volume of a split archive, the `.alz` first. The array is copied; the bytes are borrowed. */
[[nodiscard]] rubraview_alz_result_t rubraview_alz_open_volumes(proven_arena_t *arena, const rubraview_alz_volume_t *volumes,
                                                                size_t volume_count);

/**
 * The password for the encrypted files, as bytes (the archive's are in its
 * maker's code page; UTF-8 typed in the box matches for ASCII). Checked at
 * once: the check byte of every encrypted file, then the smallest of them
 * decoded and its CRC-32 compared. OK or ERR_BAD_PASSWORD. Kept (in memory
 * only, wiped on close) until the archive is closed.
 */
rubraview_alz_err_t rubraview_alz_set_password(rubraview_alz_archive_t *archive, u8str_t password);

/** Some file is encrypted and no password that opens it has been given. */
bool rubraview_alz_needs_password(const rubraview_alz_archive_t *archive);

typedef struct rubraview_alz_data_result {
    rubraview_alz_err_t err;
    u8str_t data;           /* in the caller's arena on success (one allocation of size + 1) */
} rubraview_alz_data_result_t;

/** Read one entry. The cap is checked against the declared size before anything is allocated. */
[[nodiscard]] rubraview_alz_data_result_t rubraview_alz_read_entry(proven_arena_t *arena, const rubraview_alz_archive_t *archive,
                                                                    size_t index, uint64_t max_entry_bytes);

/**
 * Volume `number` (1, 2, ...) of the set whose first volume is `first_path`
 * (`x.alz`): `x.a00`, `x.a01`, ..., `x.a99`, `x.b00`, ..., the letter in the
 * case of the `.alz`'s. Empty when `first_path` does not end in `.alz` or
 * the number is past `z99`.
 */
u8str_t rubraview_alz_volume_path(proven_arena_t *arena, u8str_t first_path, unsigned number);

/** Wipe the password and let it go. Safe on a failed open. */
void rubraview_alz_close(rubraview_alz_archive_t *archive);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_ALZ_H */
