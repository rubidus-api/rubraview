#ifndef RUBRAVIEW_PAL_TILES_H
#define RUBRAVIEW_PAL_TILES_H

#include "rubraview/core.h"
#include "rubraview/tiles.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Tiles of a page shown reduced, decoded on a thread of their own (D-40;
 * owner, 2026-09-25: "타일 디코딩으로 확대 선명하게"). The thread reads the
 * page's file once, then decodes what is asked for from those bytes —
 * one band per tile row, cut into tiles — with its own COM apartment and
 * WIC factory. It never touches the renderer; the main thread turns each
 * tile into a texture. Only the newest request counts: the view moves on
 * faster than tiles are made.
 */

typedef struct rubraview_tiles rubraview_tiles_t;

typedef struct rubraview_tile_result {
    uint32_t generation;
    rubraview_tile_key_t key;
    uint8_t *bgra;          /* premultiplied, top row first; NULL: could not be made. free() it */
    int32_t  width, height;
} rubraview_tile_result_t;

typedef void (*rubraview_tiles_wake_fn)(void *context);

/* `wake` is called from the thread after each tile. NULL when no thread could start. */
rubraview_tiles_t *rubraview_pal_tiles_start(rubraview_tiles_wake_fn wake, void *context);
void rubraview_pal_tiles_stop(rubraview_tiles_t *tiles);

/* A new page (a file on disk) under a new generation: what was wanted and
   what is unread go. The picture size is the upright one the page has. */
void rubraview_pal_tiles_source(rubraview_tiles_t *tiles, uint32_t generation, u8str_t path,
                                bool apply_exif_orientation, int32_t picture_w, int32_t picture_h);
/* The tiles wanted now, replacing what was wanted before (at most 128). */
void rubraview_pal_tiles_want(rubraview_tiles_t *tiles, uint32_t generation,
                              const rubraview_tile_key_t *keys, size_t count);
/* A finished tile of the current generation, or false. */
bool rubraview_pal_tiles_take(rubraview_tiles_t *tiles, rubraview_tile_result_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_TILES_H */
