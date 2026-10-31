#ifndef RUBRAVIEW_MEDIA_HEAD_H
#define RUBRAVIEW_MEDIA_HEAD_H

#include "rubraview/core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-210 (owner, 2026-10-31: "외부 파서에 기대지 말고 내부 자체 파서로"): a picture's, a film's or
 * a song's size as shown and its length, read by our own parsers from a few small pieces of the file
 * — its first 64 KB, and where those say the rest is (an MP4's `moov` at the end, the frames past an
 * MP3's ID3 tag, an Ogg's or a transport stream's last pages) — never the whole file.
 *
 * Reading and parsing are apart: the parsers ask a source for bytes at an offset; the source holds a
 * few windows of the file and reads a new one through the caller's function only when asked past
 * them, within a budget of reads and bytes. The source is the only thing that allocates; a parser
 * keeps nothing past the call.
 */

#define RUBRAVIEW_HEAD_FIRST      65536u          /* the first read: the file's first 64 KB */
#define RUBRAVIEW_HEAD_LATER      131072u         /* a later read: 128 KB, or what was asked if more */
#define RUBRAVIEW_HEAD_MAX_READS  4u              /* reads of one file, the first included */
#define RUBRAVIEW_HEAD_MAX_BYTES  (16u << 20)     /* bytes read of one file (a long film's `moov`) */

/* Reads `len` bytes at `at` into `dst`; `*out_got` is how many there were (fewer only at the end). */
typedef bool (*rubraview_head_read_fn)(void *context, uint64_t at, uint8_t *dst, size_t len, size_t *out_got);

typedef struct rubraview_head_window {
    uint8_t *data;
    size_t   len, cap;
    uint64_t at;
} rubraview_head_window_t;

typedef struct rubraview_head_src {
    rubraview_head_read_fn read;
    void                  *context;
    uint64_t               size;          /* the file's size */
    rubraview_head_window_t win[3];       /* [0] the head, kept; [1] and [2] taken in turn */
    unsigned               next;          /* the window the next later read goes to */
    unsigned               reads;         /* this file's reads so far */
    uint64_t               bytes;         /* and their bytes */
    void                  *scratch;       /* room for a parser's large tables (an MP4's or a Matroska's tracks) */
    size_t                 scratch_cap;
} rubraview_head_src_t;

/* Ready for a file of `size` bytes read by `read`; the windows of the file before are kept for their room. */
void rubraview_head_src_reset(rubraview_head_src_t *src, rubraview_head_read_fn read, void *context, uint64_t size);
void rubraview_head_src_free(rubraview_head_src_t *src);

/* `len` bytes at `at`: a pointer good until the next call (one into the head stays good), or NULL when
   the file has not that many there, a read fails or the budget is spent. */
const uint8_t *rubraview_head_bytes(rubraview_head_src_t *src, uint64_t at, size_t len);
/* Up to `want` bytes at `at`, as many as one window holds (`*out_got`, at least 1), or NULL. */
const uint8_t *rubraview_head_view(rubraview_head_src_t *src, uint64_t at, size_t want, size_t *out_got);

typedef struct rubraview_media_facts {
    int32_t     width, height;   /* as shown (an EXIF or a matrix turn applied); 0 when not known or none */
    double      seconds;         /* the length; 0 when not known or none */
    bool        estimated;       /* the length from a bit rate, not counted */
    const char *format;          /* "jpeg", "mp4", ...: what the bytes were taken for */
} rubraview_media_facts_t;

/* The facts of the file behind `src`. `name` (its file name, may be empty) only settles kinds whose first bytes
   say little (an icon, a bare MP3 or AAC, a transport stream). False when it is no kind known here. */
bool rubraview_media_head_read(rubraview_head_src_t *src, proven_u8str_view_t name, rubraview_media_facts_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_MEDIA_HEAD_H */
