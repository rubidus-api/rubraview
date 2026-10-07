/* internal.h - FultaArc internals shared by the core, the format readers and the codecs. MIT. */
#ifndef FULTA_ARC_INTERNAL_H
#define FULTA_ARC_INTERNAL_H

#include "fulta/arc.h"

#include "proven/hash.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ---- byte helpers ------------------------------------------------------------------------------------------- */

static inline uint16_t fa_le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t fa_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint64_t fa_le64(const uint8_t *p) { return (uint64_t)fa_le32(p) | ((uint64_t)fa_le32(p + 4) << 32); }
static inline uint32_t fa_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static inline void fa_put_le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void fa_put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static inline void fa_put_le64(uint8_t *p, uint64_t v) { fa_put_le32(p, (uint32_t)v); fa_put_le32(p + 4, (uint32_t)(v >> 32)); }

/* ---- memory ------------------------------------------------------------------------------------------------- */

void *fa_malloc(size_t n);                 /* NULL on failure; n == 0 gives a valid pointer */
void *fa_calloc(size_t count, size_t n);   /* checks the multiplication */
void *fa_realloc(void *p, size_t n);
void fa_free(void *p);
char *fa_strndup(const char *s, size_t n);

/* A growable byte buffer. */
typedef struct fa_buf { uint8_t *data; size_t size, cap; } fa_buf_t;
fulta_arc_err_t fa_buf_append(fa_buf_t *b, const void *data, size_t n);
fulta_arc_err_t fa_buf_byte(fa_buf_t *b, uint8_t v);
void fa_buf_free(fa_buf_t *b);

/* ---- checksums ---------------------------------------------------------------------------------------------- */

uint32_t fa_crc32(uint32_t crc, const void *data, size_t n);   /* ISO 3309; start with 0 */

/* ---- input: a "segment list" over one or more sources ------------------------------------------------------- */

/* A logical byte range made of pieces of sources (one volume's data, or several volumes' parts joined). */
typedef struct fa_piece { const fulta_arc_source_t *src; uint64_t offset, size; } fa_piece_t;

typedef struct fa_range {
    fa_piece_t *pieces;
    size_t count;
    uint64_t size;        /* sum of the pieces */
} fa_range_t;

fulta_arc_err_t fa_range_read(const fa_range_t *r, uint64_t offset, void *buf, size_t n, size_t *got);
fulta_arc_err_t fa_range_read_exact(const fa_range_t *r, uint64_t offset, void *buf, size_t n);
fulta_arc_err_t fa_source_read_exact(const fulta_arc_source_t *s, uint64_t offset, void *buf, size_t n);

/* A whole source as bytes in memory, for readers that index their input directly (RAR, ALZ, EGG): memory sources
 * as they are, files mapped (proven_c_lib's mmap), other sources read in once. */
typedef struct fa_view { const uint8_t *data; size_t size; int kind; void *priv; } fa_view_t;
fulta_arc_err_t fa_view_open(const fulta_arc_source_t *s, fa_view_t *v);
void fa_view_close(fa_view_t *v);

/* ---- streams: every decoder is a pull stream ---------------------------------------------------------------- */

typedef struct fa_stream fa_stream_t;
struct fa_stream {
    /* Produce up to `n` bytes; *got = 0 means the end. */
    fulta_arc_err_t (*read)(fa_stream_t *s, void *buf, size_t n, size_t *got);
    void (*destroy)(fa_stream_t *s);
};

fulta_arc_err_t fa_stream_read_exact(fa_stream_t *s, void *buf, size_t n);   /* TRUNCATED if it ends early */
fulta_arc_err_t fa_stream_skip(fa_stream_t *s, uint64_t n);
void fa_stream_destroy(fa_stream_t *s);

/* A stream over a range (owns a copy of the piece list). */
fulta_arc_err_t fa_stream_range(const fa_range_t *r, uint64_t offset, uint64_t size, fa_stream_t **out);
/* A stream over memory (not copied). */
fulta_arc_err_t fa_stream_memory(const void *data, size_t size, fa_stream_t **out);

/* Buffered byte reader over a stream, for decoders that read byte by byte. */
typedef struct fa_bytes {
    fa_stream_t *in;
    uint8_t buf[16384];
    size_t pos, len;
    bool eof;
    fulta_arc_err_t err;
    uint64_t consumed;
} fa_bytes_t;
void fa_bytes_init(fa_bytes_t *b, fa_stream_t *in);
int fa_bytes_get(fa_bytes_t *b);      /* 0..255, or -1 at the end or on error (b->err set on error) */

/* ---- decoders (src/codec) ----------------------------------------------------------------------------------- */

typedef struct fa_limits { uint64_t max_dictionary; int (*cancel)(void *); void *cancel_ctx; } fa_limits_t;

/* Each takes ownership of `in` (destroyed with the decoder). `out_size` is the expected output size (UINT64_MAX:
 * unknown, decode to the stream's own end). */
fulta_arc_err_t fa_dec_deflate(fa_stream_t *in, bool deflate64, uint64_t out_size, fa_stream_t **out);
fulta_arc_err_t fa_dec_bzip2(fa_stream_t *in, uint64_t out_size, fa_stream_t **out);
fulta_arc_err_t fa_dec_lzma(fa_stream_t *in, const uint8_t props[5], uint64_t out_size, const fa_limits_t *lim,
                            fa_stream_t **out);
fulta_arc_err_t fa_dec_lzma2(fa_stream_t *in, uint8_t prop, uint64_t out_size, const fa_limits_t *lim,
                             fa_stream_t **out);
fulta_arc_err_t fa_dec_ppmd7z(fa_stream_t *in, const uint8_t *props, size_t nprops, uint64_t out_size,
                              const fa_limits_t *lim, fa_stream_t **out);

typedef enum fa_filter_kind {
    FA_FILTER_X86, FA_FILTER_ARM, FA_FILTER_ARMT, FA_FILTER_ARM64, FA_FILTER_PPC, FA_FILTER_SPARC,
    FA_FILTER_IA64, FA_FILTER_RISCV, FA_FILTER_DELTA, FA_FILTER_SWAP2, FA_FILTER_SWAP4
} fa_filter_kind_t;
fulta_arc_err_t fa_dec_filter(fa_stream_t *in, fa_filter_kind_t kind, uint32_t param, fa_stream_t **out);
fulta_arc_err_t fa_dec_bcj2(fa_stream_t *ins[4], uint64_t out_size, fa_stream_t **out);
/* PPMd var.I rev. 1 as ZIP method 98 stores it (2-byte header first). */
fulta_arc_err_t fa_dec_ppmd8_zip(fa_stream_t *in, uint64_t out_size, const fa_limits_t *lim, fa_stream_t **out);
/* The .xz container over a byte range (ZIP method 95), blocks located through its index. */
fulta_arc_err_t fa_dec_xz_range(const fa_range_t *r, uint64_t off, uint64_t len, const fa_limits_t *lim, fa_stream_t **out);
/* AZO (EGG method 3; docs/specs/azo.md), decoding only. */
/* PKZIP 1.x (zip.md 6.2-6.4): Shrink, Reduce with factor 1-4, Implode with the ZIP entry's general flags and a
 * minimum match length (0: the APPNOTE's rule, 3 with a literal tree, else 2) */
fulta_arc_err_t fa_dec_unshrink(fa_stream_t *in, uint64_t out_size, fa_stream_t **out);
fulta_arc_err_t fa_dec_unreduce(fa_stream_t *in, int factor, uint64_t out_size, fa_stream_t **out);
fulta_arc_err_t fa_dec_explode(fa_stream_t *in, uint16_t flags, int min_len, uint64_t out_size, fa_stream_t **out);
fulta_arc_err_t fa_dec_zstd(fa_stream_t *in, uint64_t out_size, const fa_limits_t *lim, fa_stream_t **out);
fulta_arc_err_t fa_dec_azo(fa_stream_t *in, fa_stream_t **out);

/* Limit a stream to exactly `size` bytes (TRUNCATED if it ends sooner; the rest of the inner stream is ignored). */
fulta_arc_err_t fa_stream_limit(fa_stream_t *in, uint64_t size, fa_stream_t **out);

/* ---- crypto (src/crypto) ------------------------------------------------------------------------------------ */

typedef struct fa_aes { uint32_t rk[60]; int rounds; int hw; uint8_t hwenc[15 * 16], hwdec[15 * 16]; } fa_aes_t;
/* AES-NI (aes_hw.c); crypto.c self-tests fa_aes_hw_supported() before using these. 128/256 only, x86 only. */
int fa_aes_hw_supported(void);
int fa_aes_hw_expand(const uint8_t *key, size_t keylen, int decrypt, uint8_t rk[15 * 16], int *rounds);
void fa_aes_hw_encrypt(const uint8_t *rk, int rounds, const uint8_t in[16], uint8_t out[16]);
void fa_aes_hw_decrypt(const uint8_t *rk, int rounds, const uint8_t in[16], uint8_t out[16]);
void fa_aes_hw_ctr8(const uint8_t *rk, int rounds, const uint8_t ctr0[16], unsigned n, uint8_t *out);
void fa_aes_hw_cbc_decrypt(const uint8_t *rk, int rounds, uint8_t iv[16], const uint8_t *in, uint8_t *out, unsigned nb);
void fa_aes_init_enc(fa_aes_t *a, const uint8_t *key, size_t keylen);
void fa_aes_init_dec(fa_aes_t *a, const uint8_t *key, size_t keylen);
void fa_aes_encrypt(const fa_aes_t *a, const uint8_t in[16], uint8_t out[16]);
void fa_aes_decrypt(const fa_aes_t *a, const uint8_t in[16], uint8_t out[16]);

typedef struct fa_sha1 { uint32_t h[5]; uint64_t len; uint8_t buf[64]; size_t n; } fa_sha1_t;
void fa_sha1_init(fa_sha1_t *c);
void fa_sha1_update(fa_sha1_t *c, const void *data, size_t n);
void fa_sha1_final(fa_sha1_t *c, uint8_t out[20]);

typedef struct fa_sha256 { proven_sha256_t pv; } fa_sha256_t;   /* SHA-256 is proven's (hash.h) */
void fa_sha256_init(fa_sha256_t *c);
void fa_sha256_update(fa_sha256_t *c, const void *data, size_t n);
void fa_sha256_final(fa_sha256_t *c, uint8_t out[32]);

void fa_hmac_sha1(const uint8_t *key, size_t keylen, const uint8_t *msg, size_t n, uint8_t out[20]);
void fa_hmac_sha256(const uint8_t *key, size_t keylen, const uint8_t *msg, size_t n, uint8_t out[32]);
void fa_pbkdf2_sha1(const uint8_t *pw, size_t pwlen, const uint8_t *salt, size_t saltlen, uint32_t iter,
                    uint8_t *out, size_t outlen);
void fa_pbkdf2_sha256(const uint8_t *pw, size_t pwlen, const uint8_t *salt, size_t saltlen, uint32_t iter,
                      uint8_t *out, size_t outlen);

typedef struct fa_hmac_sha1 { fa_sha1_t in, out; } fa_hmac_sha1_t;
void fa_hmac_sha1_init(fa_hmac_sha1_t *h, const uint8_t *key, size_t keylen);
void fa_hmac_sha1_update(fa_hmac_sha1_t *h, const void *data, size_t n);
void fa_hmac_sha1_final(fa_hmac_sha1_t *h, uint8_t out[20]);
/* Pass-through stream that feeds every byte read to `mac` (which must outlive the stream). */
fulta_arc_err_t fa_stream_hmac_tap(fa_stream_t *in, fa_hmac_sha1_t *mac, fa_stream_t **out);

typedef struct fa_lea { uint32_t rk[32][6]; int rounds; } fa_lea_t;
void fa_lea_init(fa_lea_t *l, const uint8_t *key, size_t keylen);
void fa_lea_encrypt(const fa_lea_t *l, const uint8_t in[16], uint8_t out[16]);

/* Decrypting streams (each owns `in`). */
typedef struct fa_zipcrypto { uint32_t k[3]; } fa_zipcrypto_t;
void fa_zipcrypto_init(fa_zipcrypto_t *z, const uint8_t *pw, size_t n);
void fa_zipcrypto_decrypt(fa_zipcrypto_t *z, uint8_t *data, size_t n);
fulta_arc_err_t fa_dec_zipcrypto(fa_stream_t *in, const fa_zipcrypto_t *keys, fa_stream_t **out);
fulta_arc_err_t fa_dec_aes_cbc(fa_stream_t *in, const uint8_t *key, size_t keylen, const uint8_t iv[16],
                               fa_stream_t **out);
/* CTR with a 128-bit counter: little-endian starting at 1 (WinZip, EGG AES) or big-endian starting at 0 (EGG LEA). */
typedef enum fa_ctr_kind { FA_CTR_AES_LE1, FA_CTR_LEA_BE0 } fa_ctr_kind_t;
fulta_arc_err_t fa_dec_ctr(fa_stream_t *in, fa_ctr_kind_t kind, const uint8_t *key, size_t keylen, fa_stream_t **out);

/* ---- text --------------------------------------------------------------------------------------------------- */

bool fa_utf8_valid(const uint8_t *s, size_t n);
/* Append the UTF-8 form of code point `cp` (invalid code points become U+FFFD). */
fulta_arc_err_t fa_utf8_put(fa_buf_t *b, uint32_t cp);
/* UTF-16LE (n code units) to a NUL-terminated UTF-8 string (malloc'd). */
char *fa_utf16le_to_utf8(const uint8_t *s, size_t units);
/* Bytes in a code page to a NUL-terminated UTF-8 string (malloc'd); unknown bytes become U+FFFD. */
char *fa_codepage_to_utf8(const uint8_t *s, size_t n, fulta_arc_codepage_t cp, fulta_arc_codepage_t fallback);
/* UTF-8 to a code page (passwords stored in an archiver's code page); NULL when a character has no mapping. */
char *fa_utf8_to_codepage(const char *utf8, fulta_arc_codepage_t cp);
/* Make a stored name safe: '\\' -> '/', drop leading '/', drive letters, "." parts; ".." parts make it unsafe.
 * Returns false when nothing safe remains (the caller uses a placeholder). Edits in place. */
bool fa_sanitize_name(char *name);

/* ---- the archive object ------------------------------------------------------------------------------------- */

typedef struct fa_format_ops fa_format_ops_t;

typedef struct fa_entry {
    fulta_arc_entry_t pub;
    char *name_owned;
    uint8_t *raw_owned;
    char *method_owned;
} fa_entry_t;

struct fulta_arc {
    fulta_arc_format_t format;
    const fa_format_ops_t *ops;
    void *state;                         /* the format reader's own state */
    fulta_arc_options_t opt;
    char *name;                          /* copy of opt.name */
    fulta_arc_source_t **volumes;        /* volumes[0] is the source passed to open; each allocated, never moved */
    size_t nvolumes, capvolumes;
    fa_entry_t *entries;
    size_t count, cap;
    fa_limits_t limits;
    /* password cache: the last good UTF-8 password */
    char *password;
    uint32_t password_attempts;
};

struct fa_format_ops {
    fulta_arc_format_t format;
    /* Return true when the source starts like this format (offset: where the signature is, for SFX). */
    bool (*probe)(const uint8_t *head, size_t n, uint64_t *offset);
    fulta_arc_err_t (*open)(fulta_arc_t *arc, uint64_t offset);
    fulta_arc_err_t (*extract)(fulta_arc_t *arc, size_t index, const fulta_arc_sink_t *sink);
    void (*close)(fulta_arc_t *arc);
};

extern const fa_format_ops_t fa_zip_ops, fa_7z_ops, fa_rar_ops, fa_alz_ops, fa_egg_ops;

/* Add an entry; strings are copied. Returns the new entry or NULL (no memory). */
fa_entry_t *fa_add_entry(fulta_arc_t *arc, const uint8_t *raw_name, size_t raw_size, const char *utf8_name,
                         const char *method);
/* Volume `index` (0 = the first), asking the resolver when needed. */
fulta_arc_err_t fa_volume(fulta_arc_t *arc, uint32_t index, const char *name, const fulta_arc_source_t **out);
/* Ask for a password (UTF-8). attempt 0 returns the cached one if any. */
fulta_arc_err_t fa_password(fulta_arc_t *arc, uint32_t attempt, const char **out);
void fa_password_ok(fulta_arc_t *arc, const char *pw);
bool fa_cancelled(const fulta_arc_t *arc);

/* Copy a whole stream into a sink, checking the size (UINT64_MAX: unknown) and, when has_crc, the CRC-32. */
fulta_arc_err_t fa_pump(fa_stream_t *s, uint64_t size, bool has_crc, uint32_t crc, const fulta_arc_sink_t *sink,
                        const fulta_arc_t *arc);

/* DOS date/time (local) to nanoseconds since the epoch as if UTC; FILETIME to nanoseconds since the epoch. */
int64_t fa_dos_time_ns(uint16_t date, uint16_t time);
int64_t fa_filetime_ns(uint64_t ft);

#endif
