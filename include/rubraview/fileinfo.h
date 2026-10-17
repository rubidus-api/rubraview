#ifndef RUBRAVIEW_FILEINFO_H
#define RUBRAVIEW_FILEINFO_H

#include "rubraview/core.h"
#include "rubraview/exif.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The file's information window (owner, 2026-09-29: "지금 재생중인 파일의
 * 용량/픽셀단위크기/코덱/EXIF/기타 자세한 정보를 보여주는 기능 ... 드래그해서
 * 텍스트로 원하는 부분만 복사"). What is known is gathered as lines —
 * headings, and label/value pairs — and laid out as rows of text, values in
 * a column of their own, so a drag down the values copies only values.
 */

typedef struct rubraview_info_line {
    bool    heading;
    u8str_t label;   /* the heading's title, or the pair's label */
    u8str_t value;
} rubraview_info_line_t;

typedef struct rubraview_info {
    rubraview_info_line_t *lines;
    size_t count, capacity;
} rubraview_info_t;

void rubraview_info_heading(proven_arena_t *arena, rubraview_info_t *info, const char *title);
/* An empty value adds nothing: a line saying nothing is noise. */
void rubraview_info_add(proven_arena_t *arena, rubraview_info_t *info, const char *label, u8str_t value);
void rubraview_info_addf(proven_arena_t *arena, rubraview_info_t *info, const char *label, const char *format, ...);

/* Everything an EXIF block says, a line each, in words. */
void rubraview_info_add_exif(proven_arena_t *arena, rubraview_info_t *info, const rubraview_exif_info_t *exif);

/* RV-130: what an IPTC block says, under its own heading. */
void rubraview_info_add_iptc(proven_arena_t *arena, rubraview_info_t *info, const rubraview_iptc_info_t *iptc);

/* The rows: a heading as it is, a pair as two spaces, the label padded to
   `label_cols`, then the value. Returns how many (at most `capacity`). */
size_t rubraview_info_rows(proven_arena_t *arena, const rubraview_info_t *info, int32_t label_cols,
                           u8str_t *rows, size_t capacity);

/* Formatters; each writes into `buf` and returns the length. */
size_t rubraview_info_bytes(char *buf, size_t cap, uint64_t bytes);                 /* "3.45 MB (3,617,412 bytes)" */
size_t rubraview_info_exposure(char *buf, size_t cap, uint32_t num, uint32_t den);  /* "1/250 s", "2.5 s" */
size_t rubraview_info_gps(char *buf, size_t cap, double latitude, double longitude);/* 37°33'59.4"N 126°58'40.8"E */
size_t rubraview_info_duration(char *buf, size_t cap, double seconds);              /* "1:23:45.678", "0:10.000" */
size_t rubraview_info_fourcc(char *buf, size_t cap, uint32_t fourcc);               /* "H264", or "0x00000055" */
/* RV-130 */
size_t rubraview_info_aspect(char *buf, size_t cap, int32_t width, int32_t height); /* "4:3 (1.333)", "1.778 (about 16:9)" */
size_t rubraview_info_print_size(char *buf, size_t cap, int32_t width, int32_t height,
                                 double dpi_x, double dpi_y);                       /* "21.0 x 29.7 cm (8.27 x 11.69 in)" */

/* How many different colours (RGB, alpha left out) an RGBA8 or BGRA8
   picture holds: a 2 MB bit set from `arena`. 0 when it cannot count. */
uint32_t rubraview_info_unique_colours(proven_arena_t *arena, const rubraview_pixbuf_t *pixels);

/* A container that gives no four-letter code for a stream (Matroska gives
   0), or one that is not letters: the code is made of the codec's name,
   its first four letters in capitals, blanks after a shorter one ("h264"
   is 'H264', "vp9" 'VP9 '). A code of four printable letters is kept. */
uint32_t rubraview_info_fourcc_or_name(uint32_t fourcc, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_FILEINFO_H */
