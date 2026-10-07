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

/* A short English description of an error code (static string). */
const char *fulta_arc_strerror(fulta_arc_err_t err);

/* ---- formats ------------------------------------------------------------------------------------------------ */

typedef enum fulta_arc_format {
    FULTA_ARC_FORMAT_UNKNOWN = 0,
    FULTA_ARC_FORMAT_ZIP,
    FULTA_ARC_FORMAT_7Z,
    FULTA_ARC_FORMAT_RAR,      /* RAR 1.5 to 7.0 archives (RAR 4 and RAR 5 containers) */
    FULTA_ARC_FORMAT_ALZ,
    FULTA_ARC_FORMAT_EGG,
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

typedef struct fulta_arc_options {
    const char *name;                    /* the first volume's file name (for volume names); may be NULL */
    fulta_arc_volume_fn volume;          /* may be NULL: then split archives fail with VOLUME_MISSING */
    void *volume_ctx;
    fulta_arc_password_fn password;      /* may be NULL */
    void *password_ctx;
    fulta_arc_codepage_t codepage;       /* default FULTA_ARC_CP_AUTO */
    uint64_t max_dictionary;             /* refuse windows/dictionaries/models larger than this; 0 = 1 GiB */
    /* Polled during long operations; return nonzero to cancel. May be NULL. */
    int (*cancel)(void *ctx);
    void *cancel_ctx;
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

typedef struct fulta_arc_write_options {
    int level;                        /* 0 store/copy, 1..9 compress; out of range = 6 */
    int zip64;                        /* ZIP: write Zip64 records even when nothing needs them */
    int (*progress)(void *ctx, uint64_t done, uint64_t total);   /* nonzero cancels; may be NULL */
    void *progress_ctx;
} fulta_arc_write_options_t;

/* Write `entries`, in order, as one ZIP or 7z archive. `options` may be NULL. */
fulta_arc_err_t fulta_arc_write(fulta_arc_format_t format, const fulta_arc_write_entry_t *entries, size_t count,
                                const fulta_arc_write_options_t *options, const fulta_arc_write_sink_t *sink);

/* Write to a file (created or replaced). */
fulta_arc_err_t fulta_arc_write_file(const char *path, fulta_arc_format_t format,
                                     const fulta_arc_write_entry_t *entries, size_t count,
                                     const fulta_arc_write_options_t *options);

#ifdef __cplusplus
}
#endif

#endif /* FULTA_ARC_H */
