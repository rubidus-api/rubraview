/* fulta/arc.h - FultaArc: read 7z, ZIP, RAR, ALZ and EGG archives; write ZIP and 7z.
 *
 * MIT licence (see LICENSE). C23. Formats are described in the project's format documents; how each decoder was
 * made is recorded in its provenance document.
 *
 * Reading in four steps:
 *
 *     fulta_arc_t *arc;
 *     fulta_arc_err_t e = fulta_arc_open_file("book.rar", nullptr, &arc);   // or fulta_arc_open(...)
 *     for (size_t i = 0; i < fulta_arc_count(arc); i++) {
 *         const fulta_arc_entry_t *en = fulta_arc_entry(arc, i);
 *         e = fulta_arc_extract(arc, i, sink);                              // stream the bytes into `sink`
 *     }
 *     fulta_arc_close(arc);
 *
 * Every function that can fail returns a fulta_arc_err_t; FULTA_ARC_OK (0) is success. The library reads its input
 * only through a fulta_arc_source_t (random access) and writes only through sinks: it does no file I/O except in
 * the *_file convenience functions.
 */
#ifndef FULTA_ARC_H
#define FULTA_ARC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FULTA_ARC_VERSION_MAJOR 0
#define FULTA_ARC_VERSION_MINOR 1
#define FULTA_ARC_VERSION_PATCH 0

/* ---- errors ------------------------------------------------------------------------------------------------- */

/* Codes 1..0x0FFF are proven_c_lib's proven_err_t values (same numbers); FultaArc's own start at 0x1000. */
typedef int32_t fulta_arc_err_t;

#define FULTA_ARC_OK                   0
#define FULTA_ARC_ERR_NOMEM            1       /* = PROVEN_ERR_NOMEM */
#define FULTA_ARC_ERR_INVALID_ARG      4       /* = PROVEN_ERR_INVALID_ARG */
#define FULTA_ARC_ERR_IO               5       /* = PROVEN_ERR_IO: the source or the sink failed */
#define FULTA_ARC_ERR_NOT_ARCHIVE      0x1000  /* no supported signature found */
#define FULTA_ARC_ERR_CORRUPT          0x1001  /* structure or compressed data is invalid */
#define FULTA_ARC_ERR_TRUNCATED        0x1002  /* the data ends before it should */
#define FULTA_ARC_ERR_UNSUPPORTED      0x1003  /* a method, filter or feature FultaArc does not read */
#define FULTA_ARC_ERR_PASSWORD_NEEDED  0x1004  /* encrypted, and no password callback gave one */
#define FULTA_ARC_ERR_PASSWORD_WRONG   0x1005  /* the password check failed */
#define FULTA_ARC_ERR_CHECKSUM         0x1006  /* CRC-32 / BLAKE2sp / MAC mismatch after decoding */
#define FULTA_ARC_ERR_VOLUME_MISSING   0x1007  /* a split archive needs a volume the resolver did not supply */
#define FULTA_ARC_ERR_LIMIT            0x1008  /* a configured limit (dictionary, memory, size) was exceeded */
#define FULTA_ARC_ERR_CANCELLED        0x1009  /* the cancel callback said stop */
#define FULTA_ARC_ERR_BAD_NAME         0x100A  /* writing: a name that may not be stored */
#define FULTA_ARC_DAMAGED              0x100B  /* with options.keep_damaged: bytes were delivered to the sink but failed the checksum or ended early */

/* A short English description of an error code (static string). */
const char *fulta_arc_strerror(fulta_arc_err_t err);

/* ---- formats ------------------------------------------------------------------------------------------------ */

typedef enum fulta_arc_format {
    FULTA_ARC_FORMAT_UNKNOWN = 0,
    FULTA_ARC_FORMAT_ZIP,
    FULTA_ARC_FORMAT_7Z,
    FULTA_ARC_FORMAT_RAR,      /* RAR 1.4 to 7.0 archives (RAR 1.4, RAR 4 and RAR 5 containers) */
    FULTA_ARC_FORMAT_ALZ,
    FULTA_ARC_FORMAT_EGG,
    FULTA_ARC_FORMAT_GZIP,     /* gzip stream (.gz); the outer wrapper of tar.gz */
    FULTA_ARC_FORMAT_BZIP2,    /* bzip2 stream (.bz2); the outer wrapper of tar.bz2 */
    FULTA_ARC_FORMAT_XZ,       /* xz stream (.xz); the outer wrapper of tar.xz */
    FULTA_ARC_FORMAT_ZSTD,     /* Zstandard stream (.zst); the outer wrapper of tar.zst */
    FULTA_ARC_FORMAT_TAR,      /* Unix tape archive (.tar); ustar/POSIX pax/GNU. A gzip/bzip2/xz/zstd/compress stream
                                  wrapping a tar opens as TAR directly (the payload is decompressed into memory, up to
                                  1 GiB, at open; a larger wrapped payload stays a single stored entry of its type). */
    FULTA_ARC_FORMAT_COMPRESS, /* Unix compress stream (.Z); the outer wrapper of tar.Z */
    FULTA_ARC_FORMAT_LZIP,     /* lzip stream (.lz); LZMA1 with a CRC-32 trailer; the outer wrapper of tar.lz */
    FULTA_ARC_FORMAT_AR,       /* Unix ar archive (.a static library, .deb) */
    FULTA_ARC_FORMAT_CPIO,     /* cpio archive (newc/crc/odc/old-binary) */
    FULTA_ARC_FORMAT_ISO,      /* ISO 9660 (ECMA-119) CD/DVD image */
    FULTA_ARC_FORMAT_CAB,      /* Microsoft Cabinet (.cab); Store, MSZIP, LZX and Quantum; cabinet sets */
    FULTA_ARC_FORMAT_UDF,      /* UDF (ECMA-167 / OSTA UDF) optical-disc filesystem */
    FULTA_ARC_FORMAT_LHA,      /* LHA/LZH (.lzh, .lha; LArc .lzs, PMarc .pma; self-extractors); -lh0/1/2/3/4/5/6/7/x-,
                                  -lhd-, -lk7-, -lz4/5-, -lzs-, -pm0/1/2-; an unknown method is listed unsupported */
} fulta_arc_format_t;

const char *fulta_arc_format_name(fulta_arc_format_t format);

/* ---- input: random-access sources and volumes --------------------------------------------------------------- */

typedef struct fulta_arc_source {
    void *ctx;
    uint64_t size;   /* total size in bytes */
    /* Read up to `n` bytes at `offset` into `buf`; set *got (0 only at or past the end). */
    fulta_arc_err_t (*read_at)(void *ctx, uint64_t offset, void *buf, size_t n, size_t *got);
    /* Release `ctx` (called by the library when it is done with the source). May be NULL. */
    void (*close)(void *ctx);
} fulta_arc_source_t;

/* Open volume `index` (1 for the second volume, 2 for the third, ...) of a split archive. `name` is the name the
 * format gives that volume, derived from options->name (e.g. "book.part2.rar", "book.r00", "book.a00",
 * "book.vol2.egg", "book.7z.002", "book.z01"); it is NULL when options->name is NULL. Return
 * FULTA_ARC_ERR_VOLUME_MISSING when it does not exist. */
typedef fulta_arc_err_t (*fulta_arc_volume_fn)(void *ctx, uint32_t index, const char *name, fulta_arc_source_t *out);

/* Give the password for an encrypted archive or entry, as UTF-8 (the library converts it to each format's form).
 * `attempt` counts calls for the same archive from 0; return FULTA_ARC_ERR_PASSWORD_NEEDED to give up. The bytes
 * must stay valid until the next call or the archive is closed. */
typedef fulta_arc_err_t (*fulta_arc_password_fn)(void *ctx, uint32_t attempt, const char **utf8_out);

/* Code pages for names that an archive stores without saying they are Unicode (ZIP without the UTF-8 flag, RAR 4
 * without the Unicode flag, ALZ). */
typedef enum fulta_arc_codepage {
    FULTA_ARC_CP_AUTO = 0,     /* UTF-8 if the bytes are valid UTF-8, else the format's default (CP437 for ZIP,
                                  CP949 for ALZ, the archiver's OEM page for RAR: CP437) */
    FULTA_ARC_CP_UTF8,
    FULTA_ARC_CP_437,
    FULTA_ARC_CP_949,          /* Korean */
    FULTA_ARC_CP_932,          /* Japanese (Shift-JIS) */
    FULTA_ARC_CP_936,          /* Simplified Chinese (GBK) */
    FULTA_ARC_CP_950,          /* Traditional Chinese (Big5) */
    FULTA_ARC_CP_1252,         /* Western */
    FULTA_ARC_CP_866,          /* Russian, DOS (what Russian Windows writes into ZIP names) */
    FULTA_ARC_CP_1251,         /* Russian, Windows ANSI */
    FULTA_ARC_CP_KOI8R,        /* Russian, KOI8-R (RFC 1489; Unix) */
} fulta_arc_codepage_t;

/* A memory allocator for the decompressor buffers that `max_memory` caps. Supply all three functions together (or
   leave all NULL to use the C library's malloc); supplying one or two is rejected with FULTA_ARC_ERR_INVALID_ARG.
   The library never mixes allocators: every block these functions return is reallocated and freed only by them.
   `align` is a power of two, at most alignof(max_align_t). `alloc` returns NULL on failure (reported as
   FULTA_ARC_ERR_NOMEM, or FULTA_ARC_ERR_LIMIT when max_memory was the cause). `realloc` with ptr==NULL behaves as
   alloc and with new_size==0 frees and returns NULL; `old_size` is the size last requested for ptr. `free(NULL)` is
   a no-op. This backs only the codec buffers; the fulta_arc_extract_alloc result is always a C-library malloc block
   (release it with free()), independent of this hook. The current codec-budget path exercises alloc and free;
   realloc completes the contract for future buffered paths. */
typedef struct fulta_arc_allocator {
    void *(*alloc)(void *ctx, size_t size, size_t align);
    void *(*realloc)(void *ctx, void *ptr, size_t old_size, size_t new_size, size_t align);
    void (*free)(void *ctx, void *ptr);
    void *ctx;
} fulta_arc_allocator_t;

typedef struct fulta_arc_options {
    const char *name;                    /* the first volume's file name (for volume names); may be NULL */
    fulta_arc_volume_fn volume;          /* may be NULL: then split archives fail with VOLUME_MISSING */
    void *volume_ctx;
    fulta_arc_password_fn password;      /* may be NULL */
    void *password_ctx;
    fulta_arc_codepage_t codepage;       /* default FULTA_ARC_CP_AUTO */
    uint64_t max_dictionary;             /* refuse windows/dictionaries/models larger than this; 0 = 1 GiB */
    /* Cap the total live memory the vendored decompressors (PPMd/LZMA/bzip2/zstd) may hold while opening or
       extracting, so a hostile archive cannot drive unbounded allocation; a decode that would exceed it stops with
       FULTA_ARC_ERR_LIMIT. 0 = unlimited (NB: unlike max_dictionary, whose 0 means 1 GiB). This bounds the codec
       buffers sized by archive-declared parameters -- the PPMd model, the LZMA/LZMA2 state and window, the zstd
       window, the bzip2 block table -- which are the attacker-driven allocations. It does not account for the
       library's own small bookkeeping or fixed-size scratch (the deflate window and codec input buffers are a
       constant 64 KiB each and go uncounted). max_dictionary still caps each single request and is checked first. */
    uint64_t max_memory;
    /* Back the decompressor buffers that max_memory caps with a caller-supplied allocator (see
       fulta_arc_allocator_t). All-NULL (the default) uses the C library's malloc. */
    fulta_arc_allocator_t allocator;
    /* Polled during long operations; return nonzero to cancel. May be NULL. */
    int (*cancel)(void *ctx);
    void *cancel_ctx;
    /* Reports decoding progress during an extraction, including the skip over earlier entries of a solid unit.
       `done` is cumulative bytes decoded in the current fulta_arc_extract call; `total` is the entry's unpacked
       size when known (0 otherwise), so `done` may exceed it across a solid skip. May be NULL. */
    void (*progress)(void *ctx, uint64_t done, uint64_t total);
    void *progress_ctx;
    /* The code page for a name that is not flagged Unicode when `codepage` is AUTO; 0 = the format's default
       (CP437 for ZIP, the OEM page for RAR). Lets a caller pick the machine's ANSI page for legacy names. */
    fulta_arc_codepage_t auto_codepage;
    /* Keep the bytes that did decode when an entry fails its checksum or ends early: fulta_arc_extract then writes
       what it has to the sink and returns FULTA_ARC_DAMAGED instead of a hard error. */
    bool keep_damaged;
} fulta_arc_options_t;

/* ---- entries ------------------------------------------------------------------------------------------------ */

#define FULTA_ARC_ENTRY_DIR        0x0001u   /* a folder (no data) */
#define FULTA_ARC_ENTRY_ENCRYPTED  0x0002u   /* data is encrypted */
#define FULTA_ARC_ENTRY_SOLID      0x0004u   /* decoding needs the entries before it */
#define FULTA_ARC_ENTRY_SYMLINK    0x0008u   /* data is the link target */
#define FULTA_ARC_ENTRY_SPLIT      0x0010u   /* data spans volumes */
#define FULTA_ARC_ENTRY_HAS_CRC32  0x0020u
#define FULTA_ARC_ENTRY_HAS_MTIME  0x0040u
#define FULTA_ARC_ENTRY_UNSUPPORTED 0x0080u  /* listed, but its method/encryption cannot be decoded */
#define FULTA_ARC_ENTRY_UNKNOWN_SIZE 0x0100u /* the stored size is "unknown": decode to the stream's end */
#define FULTA_ARC_ENTRY_NAME_NOT_UNICODE 0x0200u /* the name was not stored as Unicode; it was decoded through a code page (see name_raw, options.auto_codepage) */

typedef struct fulta_arc_entry {
    const char *name;                 /* UTF-8, '/' between folders, NUL-terminated; never absolute, no ".." */
    const uint8_t *name_raw;          /* the name's bytes as stored */
    size_t name_raw_size;
    uint64_t size;                    /* unpacked size */
    uint64_t packed_size;             /* bytes in the archive (0 when not known per entry, e.g. 7z solid) */
    int64_t mtime;                    /* modification time, nanoseconds since 1970-01-01 UTC (or local time, when the
                                         format stores local time: ZIP/ALZ/RAR4 DOS times, EGG) */
    uint32_t crc32;                   /* when FULTA_ARC_ENTRY_HAS_CRC32 */
    uint32_t attributes;              /* Windows attributes, or (attributes >> 16) = Unix mode when unix_mode != 0 */
    uint32_t unix_mode;               /* Unix st_mode when the archive gives one, else 0 */
    uint32_t solid_group;             /* entries decoded from one solid unit (7z folder, RAR solid run) share this; the first entry of a group is where decoding starts. Distinct per entry when not solid. */
    uint32_t flags;                   /* FULTA_ARC_ENTRY_* */
    const char *method;               /* short method name, e.g. "Deflate", "LZMA2:BCJ", "RAR5", "AZO" */
} fulta_arc_entry_t;

/* ---- output ------------------------------------------------------------------------------------------------- */

typedef struct fulta_arc_sink {
    void *ctx;
    /* Take `n` bytes. Return FULTA_ARC_OK, or any error to stop the extraction (it is passed back). */
    fulta_arc_err_t (*write)(void *ctx, const void *data, size_t n);
} fulta_arc_sink_t;

/* ---- reading ------------------------------------------------------------------------------------------------ */

typedef struct fulta_arc fulta_arc_t;

/* Open an archive. `options` may be NULL. The source is owned by the archive from here (closed with it), also on
 * failure. */
fulta_arc_err_t fulta_arc_open(const fulta_arc_source_t *source, const fulta_arc_options_t *options, fulta_arc_t **out);

/* Convenience: a file (with its volumes found next to it by name), or bytes in memory (not copied; they must stay
 * valid until the archive is closed). */
fulta_arc_err_t fulta_arc_open_file(const char *path, const fulta_arc_options_t *options, fulta_arc_t **out);
fulta_arc_err_t fulta_arc_open_memory(const void *data, size_t size, const fulta_arc_options_t *options,
                                      fulta_arc_t **out);

/* A random-access source over a file or memory (for fulta_arc_open, or for a volume callback). */
fulta_arc_err_t fulta_arc_source_file(const char *path, fulta_arc_source_t *out);
fulta_arc_source_t fulta_arc_source_memory(const void *data, size_t size);

fulta_arc_format_t fulta_arc_format(const fulta_arc_t *arc);
size_t fulta_arc_count(const fulta_arc_t *arc);
const fulta_arc_entry_t *fulta_arc_entry(const fulta_arc_t *arc, size_t index);   /* NULL when out of range */

/* Decode entry `index` into `sink`, checking its size and checksum. Directories write nothing. In a solid archive
 * the entries before it are decoded first (and discarded) unless they were just extracted in order. */
fulta_arc_err_t fulta_arc_extract(fulta_arc_t *arc, size_t index, const fulta_arc_sink_t *sink);

/* Check a password against entry `index` without decoding it, using the format's check value.
   FULTA_ARC_OK: a supplied password matched (and is cached for extraction); FULTA_ARC_ERR_PASSWORD_WRONG: none
   matched; FULTA_ARC_ERR_PASSWORD_NEEDED: no password callback; FULTA_ARC_ERR_UNSUPPORTED: this entry/format has
   no cheap check value (extract it to find out). The entry need not be encrypted (then FULTA_ARC_OK). */
fulta_arc_err_t fulta_arc_check_password(fulta_arc_t *arc, size_t index);

/* Extract into memory: *out_data is allocated with malloc (free it with free()). */
fulta_arc_err_t fulta_arc_extract_alloc(fulta_arc_t *arc, size_t index, void **out_data, size_t *out_size);

void fulta_arc_close(fulta_arc_t *arc);

/* ---- writing (ZIP and 7z) ----------------------------------------------------------------------------------- */

typedef struct fulta_arc_write_entry {
    const char *name;                 /* UTF-8, '/' between folders ('\\' is taken as '/'); a trailing '/' or
                                         is_dir makes a folder */
    const void *data;                 /* may be NULL when size is 0 */
    uint64_t size;
    int64_t mtime;                    /* seconds since 1970-01-01 UTC; 0 when not known */
    int is_dir;
} fulta_arc_write_entry_t;

typedef struct fulta_arc_write_sink {
    void *ctx;
    fulta_arc_err_t (*write)(void *ctx, const void *data, size_t n);                 /* append */
    fulta_arc_err_t (*patch)(void *ctx, uint64_t offset, const void *data, size_t n); /* overwrite earlier bytes */
} fulta_arc_write_sink_t;

/* How to encrypt written entries. Only used when `password` is set; all schemes are strong AES (no legacy
   ZipCrypto on write). AES primitives are FultaArc's own (MIT); key derivation is PBKDF2-HMAC-SHA1 (WinZip AES)
   or 7-Zip's SHA-256 scheme (7zAES). */
typedef enum fulta_arc_encrypt {
    FULTA_ARC_ENCRYPT_DEFAULT = 0,    /* with a password: ZIP -> WinZip AES-256, 7z -> 7zAES (AES-256) */
    FULTA_ARC_ENCRYPT_ZIP_AES128,
    FULTA_ARC_ENCRYPT_ZIP_AES192,
    FULTA_ARC_ENCRYPT_ZIP_AES256,
    FULTA_ARC_ENCRYPT_7Z_AES256
} fulta_arc_encrypt_t;

/* Compression method for written entries. DEFAULT keeps the historical choice (ZIP -> Deflate, 7z -> LZMA).
   Each method is valid only for some formats; an invalid method x format is FULTA_ARC_ERR_INVALID_ARG.
   ZIP accepts STORE, DEFLATE, BZIP2, LZMA, PPMD, ZSTD; 7z accepts STORE(=Copy), DEFLATE, BZIP2, LZMA, LZMA2, PPMD
   (7z follows stock 7-Zip: no zstd-in-7z). */
typedef enum fulta_arc_method {
    FULTA_ARC_METHOD_DEFAULT = 0,
    FULTA_ARC_METHOD_STORE,           /* ZIP method 0 / 7z Copy */
    FULTA_ARC_METHOD_DEFLATE,         /* ZIP method 8 / 7z 040108 */
    FULTA_ARC_METHOD_BZIP2,           /* ZIP method 12 / 7z 040202 */
    FULTA_ARC_METHOD_LZMA,            /* ZIP method 14 / 7z 030101 */
    FULTA_ARC_METHOD_LZMA2,           /* 7z 21 (not a ZIP method) */
    FULTA_ARC_METHOD_PPMD,            /* ZIP method 98 (var.I) / 7z 030401 (var.H) */
    FULTA_ARC_METHOD_ZSTD             /* ZIP method 93 (not a stock-7z method) */
} fulta_arc_method_t;

/* Optional 7z content filter, chained before the compression method. Ignored for ZIP. DELTA uses filter_delta_dist
   (1..256; 0 treated as 1). The branch filters suit executables of that architecture. */
typedef enum fulta_arc_filter {
    FULTA_ARC_FILTER_NONE = 0,
    FULTA_ARC_FILTER_DELTA,
    FULTA_ARC_FILTER_BCJ_X86,
    FULTA_ARC_FILTER_BCJ2,
    FULTA_ARC_FILTER_ARM, FULTA_ARC_FILTER_ARM64, FULTA_ARC_FILTER_PPC, FULTA_ARC_FILTER_SPARC, FULTA_ARC_FILTER_IA64
} fulta_arc_filter_t;

/* 7z solid grouping (7-Zip's `-ms`). ZIP entries are always independent, so this is ignored for ZIP. */
typedef enum fulta_arc_solid {
    FULTA_ARC_SOLID_DEFAULT = 0,      /* = ON: one solid folder for all files (best ratio; the previous behaviour) */
    FULTA_ARC_SOLID_ON,               /* one solid folder for all files */
    FULTA_ARC_SOLID_OFF               /* one folder per file: the result can be edited in place (no reconvert) */
} fulta_arc_solid_t;

typedef struct fulta_arc_write_options {
    int level;                        /* 0 store/copy, 1..9 compress; out of range = 6 */
    int zip64;                        /* ZIP: write Zip64 records even when nothing needs them */
    int (*progress)(void *ctx, uint64_t done, uint64_t total);   /* nonzero cancels; may be NULL */
    void *progress_ctx;
    const char *password;             /* NULL = no encryption; otherwise every file entry is encrypted (UTF-8) */
    fulta_arc_encrypt_t encryption;   /* scheme when `password` is set; must match the output format */
    fulta_arc_method_t method;        /* 0 = DEFAULT; compression method (validated against the output format) */
    fulta_arc_filter_t filter;        /* 7z only; 0 = none; a content filter chained before `method` */
    uint32_t filter_delta_dist;       /* DELTA filter byte distance (1..256); 0 = 1 */
    fulta_arc_solid_t solid;          /* 7z only; 0 = default (solid); SOLID_OFF = one folder per file (editable) */
} fulta_arc_write_options_t;

/* Write `entries`, in order, as one ZIP or 7z archive. `options` may be NULL. */
fulta_arc_err_t fulta_arc_write(fulta_arc_format_t format, const fulta_arc_write_entry_t *entries, size_t count,
                                const fulta_arc_write_options_t *options, const fulta_arc_write_sink_t *sink);

/* Write to a file (created or replaced). */
fulta_arc_err_t fulta_arc_write_file(const char *path, fulta_arc_format_t format,
                                     const fulta_arc_write_entry_t *entries, size_t count,
                                     const fulta_arc_write_options_t *options);

/* ---- editing (ZIP and 7z) ----------------------------------------------------------------------------------- */

/* Whether an open archive can be edited in its own format, and if not, why. The reason tells the caller whether to
   offer conversion: SOLID/MULTIVOLUME/FORMAT_NOT_WRITABLE can proceed by converting into a ZIP or 7z (see
   fulta_arc_edit_write with a different format); ENCRYPTED is refused outright (editing an encrypted archive is not
   supported, by design — neither in place nor by conversion). */
typedef enum fulta_arc_editable {
    FULTA_ARC_EDIT_OK = 0,              /* ZIP, or 7z where every folder holds one file: edit in place */
    FULTA_ARC_EDIT_SOLID,               /* 7z with a shared (solid) folder: convert to proceed */
    FULTA_ARC_EDIT_MULTIVOLUME,         /* split archive: convert to proceed */
    FULTA_ARC_EDIT_FORMAT_NOT_WRITABLE, /* RAR/ALZ/EGG/...: convert to proceed */
    FULTA_ARC_EDIT_ENCRYPTED            /* the archive is encrypted: editing is refused */
} fulta_arc_editable_t;

fulta_arc_editable_t fulta_arc_editable(const fulta_arc_t *arc);

/* A pull source for a new or replaced file's bytes. read() returns FULTA_ARC_OK with *got set (0 == end of data),
   or an error to abort the write. */
typedef struct fulta_arc_reader {
    void *ctx;
    fulta_arc_err_t (*read)(void *ctx, void *buf, size_t cap, size_t *got);
    uint64_t size;                    /* the file's size, or UINT64_MAX when not known ahead of time */
    int64_t mtime;                    /* seconds since 1970-01-01 UTC; 0 when not known */
    uint32_t attributes;              /* Windows attributes; 0 for none */
} fulta_arc_reader_t;

/* An edit in progress. Begin from an open archive; every existing entry is copied unless removed or replaced. */
typedef struct fulta_arc_edit fulta_arc_edit_t;

fulta_arc_err_t fulta_arc_edit_begin(fulta_arc_t *arc, fulta_arc_edit_t **out);

/* Remove / replace existing entry `index`; add a new entry. `name` on replace is NULL to keep the old name.
   `reader`'s bytes are pulled during the write, not now. */
fulta_arc_err_t fulta_arc_edit_remove(fulta_arc_edit_t *edit, size_t index);
fulta_arc_err_t fulta_arc_edit_replace(fulta_arc_edit_t *edit, size_t index, const char *name,
                                       const fulta_arc_reader_t *reader);
fulta_arc_err_t fulta_arc_edit_add(fulta_arc_edit_t *edit, const char *name, const fulta_arc_reader_t *reader);

/* Write the edited archive. When `format` is the source's own format the untouched entries are copied without
   re-encoding (requires fulta_arc_editable(arc) == FULTA_ARC_EDIT_OK); a different ZIP/7z re-encodes everything
   (the convert route, streamed). `opts` carries the level, progress/cancel, and a password to encrypt the output:
   with a password every file entry is re-encoded and encrypted (the convert route, also for the source's own
   format), as fulta_arc_write would encrypt it; a 7z output's temporary spill is made private and nameless at once.
   The source archive is never modified. */
fulta_arc_err_t fulta_arc_edit_write(fulta_arc_edit_t *edit, fulta_arc_format_t format,
                                     const fulta_arc_write_sink_t *sink, const fulta_arc_write_options_t *opts);
/* As above, to a file: written to a temporary beside `path` and renamed over it only on success. */
fulta_arc_err_t fulta_arc_edit_write_file(fulta_arc_edit_t *edit, fulta_arc_format_t format, const char *path,
                                          const fulta_arc_write_options_t *opts);

void fulta_arc_edit_free(fulta_arc_edit_t *edit);

#ifdef __cplusplus
}
#endif

#endif /* FULTA_ARC_H */
