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
void rubraview_head_src_reset(rubraview_head_src_t *src, rubraview_head_read_fn read, proven_mem_mut_t context_view);
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

/* RV-319 (R362): the MIME type Media Foundation is told for a file whose ending it has no handler for although
   one of its handlers reads the bytes (".m4b", an MPEG-4 audiobook: "audio/mp4"); NULL for any other name. */
const char *rubraview_media_mf_content_type(proven_u8str_view_t name);

/* RV-319 (R365): a Sony Wave64 file (".w64": RIFF WAVE with GUIDs for chunk names and 64-bit sizes) seen by Media
   Foundation, which has no handler for it, as the WAVE file it is: `header` holds a RIFF WAVE head (RF64 when the
   sound passes 4 GB) whose "fmt " is the file's, then "data", and the sound follows it as the file's bytes
   [data_at, data_at + data_len). From the file's first bytes `head` (its fmt and the data chunk's header there) and
   its size; false when it is no Wave64 file or they are not there. */
#define RUBRAVIEW_W64_HEADER_MAX 400
typedef struct rubraview_w64_wave {
    uint8_t  header[RUBRAVIEW_W64_HEADER_MAX];
    uint32_t header_len;
    uint64_t data_at, data_len;
    uint16_t format_tag, block_align;
    uint32_t rate, byte_rate;
} rubraview_w64_wave_t;
bool rubraview_media_w64_is(const uint8_t *head, size_t head_len);
bool rubraview_media_w64_wave(const uint8_t *head, size_t head_len, uint64_t file_size, rubraview_w64_wave_t *out);

/* RV-319 (R367): an AIFF or AIFF-C of PCM (big-endian, or "sowt" little-endian) or of 32/64-bit floats seen by
   Media Foundation, which has no handler for AIFF, as a WAVE: `header` a RIFF WAVE head (RF64 past 4 GB), then the
   sound bytes [data_at, data_at + data_len) of the file, each sample turned as rubraview_media_aiff_fix says. False
   for another compression (FFmpeg's), or when COMM and SSND's header are not in `head`. */
typedef struct rubraview_aiff_wave {
    uint8_t  header[RUBRAVIEW_W64_HEADER_MAX];
    uint32_t header_len;
    uint64_t data_at, data_len;
    uint16_t sample_bytes;   /* 1 to 8 */
    bool     swap;           /* big-endian samples: their bytes reversed */
    bool     sign8;          /* 8-bit: AIFF's signed to WAVE's unsigned */
} rubraview_aiff_wave_t;
bool rubraview_media_aiff_wave(const uint8_t *head, size_t head_len, uint64_t file_size, rubraview_aiff_wave_t *out);
/* Whole samples of the file's sound (`len` a multiple of sample_bytes, from a sample's start) turned to the WAVE's. */
void rubraview_media_aiff_fix(const rubraview_aiff_wave_t *w, uint8_t *buf, size_t len);

/* R365, R366: a WAVE's (RIFF, RF64 or Wave64) "fmt " body and "data" place, from its first bytes and its size;
   `fact` its frames when it says (ds64's or fact's), else 0. A data size of 0 or past the end (a recording cut
   short) is the bytes that are there. */
typedef struct rubraview_wave_chunks {
    const uint8_t *fmt;
    uint32_t fmt_len;
    uint64_t data_at, data_len, fact;
    bool w64;
} rubraview_wave_chunks_t;
bool rubraview_media_wave_chunks(const uint8_t *head, size_t head_len, uint64_t file_size, rubraview_wave_chunks_t *out);
/* A RIFF WAVE head (RF64 with ds64 when it would pass 4 GB) for `fmt` and `data_len` bytes of sound, into `h`;
   its length, 0 when `cap` is short. */
uint32_t rubraview_media_wave_head(uint8_t *h, size_t cap, const uint8_t *fmt, uint32_t fmt_len, uint64_t data_len,
                                   uint16_t block_align);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_MEDIA_HEAD_H */
