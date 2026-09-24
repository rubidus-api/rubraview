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
} rubraview_thumb_result_t;

typedef void (*rubraview_thumbs_wake_fn)(void *context);

/* Filters are glob lists ("*.jpg;*.png"); they are copied. `wake` is
   called from the worker after each result. NULL when no thread could start. */
rubraview_thumbs_t *rubraview_pal_thumbs_start(u8str_t picture_filter, u8str_t film_filter, u8str_t archive_filter,
                                               rubraview_thumbs_wake_fn wake, void *context);
void rubraview_pal_thumbs_stop(rubraview_thumbs_t *thumbs);

/* A new listing: queued requests and unread results of the old one go. */
void rubraview_pal_thumbs_generation(rubraview_thumbs_t *thumbs, uint32_t generation);
bool rubraview_pal_thumbs_request(rubraview_thumbs_t *thumbs, uint32_t generation, size_t index,
                                  bool folder, double aspect, u8str_t path);
/* A finished one of the current generation, or false. */
bool rubraview_pal_thumbs_take(rubraview_thumbs_t *thumbs, rubraview_thumb_result_t *out);
/* Requests queued or being made. */
bool rubraview_pal_thumbs_busy(rubraview_thumbs_t *thumbs);

/* The picture Windows' shell has for a file — a photo's or a film's
   thumbnail, a record's cover — as top-row-first BGRA to free(), or NULL.
   Any thread with COM started. */
uint8_t *rubraview_pal_shell_thumbnail_bgra(u8str_t path, int32_t side, int32_t *out_w, int32_t *out_h);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_THUMBS_H */
