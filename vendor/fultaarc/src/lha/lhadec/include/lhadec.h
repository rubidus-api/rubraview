/*
 * lhadec - read-only LHA/LZH archive decoder.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 FultaArc contributors
 *
 * Written from the clean-room specification in docs/spec/ (lzh-container.md, lzh-methods.md).
 * Every rule implemented here is cited there to the step of docs/cleanroom/log.md that tested it.
 *
 * Usage: open a reader on a byte source, call lha_next() for each entry, then either lha_extract()
 * (decodes and checks the data CRC) or lha_skip().  Pointers inside lha_entry stay valid until the
 * next call of lha_next() or lha_close().
 */
#ifndef LHADEC_H
#define LHADEC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return codes (negative = error). */
enum {
    LHA_OK = 0,
    LHA_END = 1,                 /* lha_next(): no more entries */
    LHA_ERR_IO = -1,             /* read or write callback failed */
    LHA_ERR_FORMAT = -2,         /* malformed header */
    LHA_ERR_HEADER_SUM = -3,     /* level 0/1 header sum mismatch */
    LHA_ERR_HEADER_CRC = -4,     /* level 2/3 header CRC (unit 0x00) mismatch */
    LHA_ERR_UNSUPPORTED = -5,    /* method recognised but not decoded */
    LHA_ERR_DATA = -6,           /* malformed compressed data */
    LHA_ERR_TRUNCATED = -7,      /* data ends before the entry is complete */
    LHA_ERR_CRC = -8,            /* decoded data does not match the stored CRC-16 */
    LHA_ERR_NOMEM = -9,
    LHA_ERR_STATE = -10,         /* API used out of order */
    LHA_ERR_NOT_FOUND = -11      /* no archive header found */
};

/* Methods (spec lzh-methods.md). */
typedef enum {
    LHA_M_UNKNOWN = 0,
    LHA_M_LH0, LHA_M_LHD, LHA_M_LH1, LHA_M_LH2, LHA_M_LH3, LHA_M_LH4, LHA_M_LH5, LHA_M_LH6,
    LHA_M_LH7, LHA_M_LHX, LHA_M_LK7, LHA_M_LZ4, LHA_M_LZ5, LHA_M_LZS, LHA_M_PM0, LHA_M_PM1,
    LHA_M_PM2
} lha_method;

/* Which optional fields of lha_entry are present. */
enum {
    LHA_HAS_UNIX_TIME = 1u << 0,   /* unix_time valid (level 2/3, unit 0x54, or level-0 'U'/'K' area) */
    LHA_HAS_DOS_TIME = 1u << 1,    /* dos_time valid (level 0/1 base header) */
    LHA_HAS_UNIX_PERM = 1u << 2,
    LHA_HAS_UNIX_IDS = 1u << 3,
    LHA_HAS_OS9_PERM = 1u << 4,
    LHA_HAS_WIN_TIMES = 1u << 5,
    LHA_HAS_HEADER_CRC = 1u << 6,  /* unit 0x00 present and verified */
    LHA_HAS_SYMLINK = 1u << 7,
    LHA_IS_DIR = 1u << 8
};

typedef struct {
    char method_id[6];             /* the 5-byte method string, NUL-terminated, as stored */
    lha_method method;             /* decoding selected (e.g. -lh7- with os 0x20 -> LHA_M_LK7) */
    int level;                     /* header level 0..3 */
    unsigned os_type;              /* os byte, 0 if none */
    uint64_t packed_size;          /* bytes of compressed data */
    uint64_t original_size;
    uint16_t crc;                  /* CRC-16 of the original data */
    unsigned flags;                /* LHA_HAS_* / LHA_IS_DIR */
    uint32_t dos_time;             /* raw DOS date-time (level 0/1) */
    uint32_t unix_time;
    uint16_t unix_perm, unix_uid, unix_gid, os9_perm;
    uint64_t win_ctime, win_mtime, win_atime;  /* 100 ns units since 1601-01-01 (spec container §4.2) */
    const unsigned char *path;     /* directory part with '/' separators, may be empty */
    size_t path_len;
    const unsigned char *name;     /* file name (raw bytes, case unchanged) */
    size_t name_len;
    const unsigned char *link;     /* symbolic-link target if LHA_HAS_SYMLINK */
    size_t link_len;
    uint64_t header_offset;        /* offset of this entry in the source */
} lha_entry;

/* Byte source: read up to n bytes into buf; return bytes read (0 at end), or (size_t)-1 on error. */
typedef size_t (*lha_read_fn)(void *ctx, void *buf, size_t n);
/* Byte sink for lha_extract(): return 0 on success, non-zero to abort. */
typedef int (*lha_write_fn)(void *ctx, const void *buf, size_t n);

typedef struct lha_reader lha_reader;

/* Create a reader.  If scan_limit > 0 and the source does not start with an entry, up to scan_limit
 * leading bytes are searched for the first entry (self-extracting archives, spec container §6). */
int lha_open(lha_reader **out, lha_read_fn rd, void *ctx, size_t scan_limit);
int lha_next(lha_reader *r, lha_entry *e);              /* LHA_OK, LHA_END or error */
int lha_extract(lha_reader *r, lha_write_fn wr, void *ctx);
int lha_skip(lha_reader *r);
void lha_close(lha_reader *r);

const char *lha_strerror(int code);
const char *lha_method_name(lha_method m);
uint16_t lha_crc16(uint16_t crc, const void *data, size_t n);

/* DOS date-time to calendar fields; returns 0 if any field is out of range (spec container §2). */
int lha_dos_time_fields(uint32_t dos, int *year, int *mon, int *day, int *hour, int *min, int *sec);

/* Raw-buffer method decoder (for tests and embedding): decodes `in` (packed) into exactly `out_len`
 * bytes of `out`.  Returns LHA_OK or an error code. */
int lha_decode_buffer(lha_method m, const unsigned char *in, size_t in_len,
                      unsigned char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
#endif /* LHADEC_H */
