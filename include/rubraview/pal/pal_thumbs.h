#ifndef RUBRAVIEW_PAL_THUMBS_H
#define RUBRAVIEW_PAL_THUMBS_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The picker's thumbnails, made on a thread of their own (D-34; owner,
 * 2026-09-25: show the list at once, keep answering input, and add each
 * picture to its tile as it is made).
 *
 * The thread owns everything it touches: its own COM apartment, its own
 * WIC factory, its own scratch arena. It never touches the app's arena or
 * the renderer (LESSONS 2026-09-11). It hands back top-row-first BGRA,
 * already cut, softened and darkened (thumb.h); the main thread only turns
 * that into a texture.
 *
 * What it shows: a picture's or a film's shell thumbnail; a folder's first
 * picture, else its first film; an archive's first page, read in pieces
 * under a time limit and given up when the reading looks too slow.
 */

typedef struct rubraview_thumbs rubraview_thumbs_t;

typedef struct rubraview_thumb_result {
    uint32_t generation;
    size_t   index;
    uint8_t *bgra;          /* NULL: nothing to show; else free() it */
    int32_t  width, height;
    /* What the file itself is, for the details view (owner, 2026-10-16): its own
       size in pixels and, for a film or a song, its length. 0 when not known. */
    int32_t  source_width, source_height;
    double   seconds;
} rubraview_thumb_result_t;

typedef void (*rubraview_thumbs_wake_fn)(void *context);

/* Filters are glob lists ("*.jpg;*.png"); they are copied. `wake` is
   called from the worker after each result. NULL when no thread could start. */
rubraview_thumbs_t *rubraview_pal_thumbs_start(proven_u8str_view_t picture_filter, proven_u8str_view_t film_filter, proven_u8str_view_t archive_filter,
                                               rubraview_thumbs_wake_fn wake, void *context);
void rubraview_pal_thumbs_stop(rubraview_thumbs_t *thumbs);

/* A new listing: queued requests and unread results of the old one go. */
void rubraview_pal_thumbs_generation(rubraview_thumbs_t *thumbs, uint32_t generation);
/* A page inside a book (owner, 2026-09-29): its bytes, owned by the thread
   from here — freed if the request is refused or dropped. */
bool rubraview_pal_thumbs_request_bytes(rubraview_thumbs_t *thumbs, uint32_t generation, size_t index,
                                        double aspect, uint8_t *bytes, size_t len);
bool rubraview_pal_thumbs_request(rubraview_thumbs_t *thumbs, uint32_t generation, size_t index,
                                  bool folder, double aspect, proven_u8str_view_t path);
/* A finished one of the current generation, or false. */
bool rubraview_pal_thumbs_take(rubraview_thumbs_t *thumbs, rubraview_thumb_result_t *out);
/* Requests queued or being made. */
bool rubraview_pal_thumbs_busy(rubraview_thumbs_t *thumbs);

/* The picture Windows' shell has for a file — a photo's or a film's
   thumbnail, a record's cover — as top-row-first BGRA to free(), or NULL.
   Any thread with COM started. */
uint8_t *rubraview_pal_shell_thumbnail_bgra(proven_u8str_view_t path, int32_t side, int32_t *out_w, int32_t *out_h);

/*
 * RV-192, RV-210: the facts of many files — a picture's or a film's size as shown, a film's or a song's length —
 * read by our own parsers (media_head.h) from a few small pieces of each file, on threads of their own: four on
 * a drive without a seek penalty (an SSD), one on a hard disk, a network share or a drive not known. The paths
 * are copied; an empty one is skipped. A file is answered when ready, in any order. `expect` (may be NULL):
 * for a file whose facts were kept on disk, the time they were read at; when the file's time is still that,
 * it is not read and is answered as confirmed. `background`: at the lowest priority until hurried.
 */
typedef struct rubraview_sizes rubraview_sizes_t;

rubraview_sizes_t *rubraview_pal_sizes_start(const proven_u8str_view_t *paths, const int64_t *expect, size_t count,
                                             bool background, rubraview_thumbs_wake_fn wake, void *context);
/* How many are answered so far (for a line saying how far it is). */
size_t rubraview_pal_sizes_done(rubraview_sizes_t *sizes);
size_t rubraview_pal_sizes_count(const rubraview_sizes_t *sizes);
/* The index-th file's answer: false when not yet. Its size as shown and its length (0 when not known or none),
   its time as read (seconds since 1970; 0 when it could not be opened), and whether it was only confirmed. */
bool rubraview_pal_sizes_get(const rubraview_sizes_t *sizes, size_t index, int32_t *out_width, int32_t *out_height,
                             double *out_seconds, int64_t *out_mtime, bool *out_confirmed);
/* The index-th path as it was given (its own copy). */
proven_u8str_view_t rubraview_pal_sizes_path(const rubraview_sizes_t *sizes, size_t index);
/* A background reading asked for now: its threads at their usual priority. */
void rubraview_pal_sizes_hurry(rubraview_sizes_t *sizes);
/* Stops the threads where they are and lets everything go. */
void rubraview_pal_sizes_stop(rubraview_sizes_t *sizes);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_THUMBS_H */
