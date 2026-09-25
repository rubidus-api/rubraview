#ifndef RUBRAVIEW_TILES_H
#define RUBRAVIEW_TILES_H

#include "rubraview/core.h"
#include "rubraview/viewport.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Tiles for a page shown reduced (D-39, D-40). A picture too large for the
 * graphics card is held as one smaller texture; zoomed in past it, the
 * part on screen is decoded again from the file, sharper, in square
 * tiles, and drawn over it.
 *
 * A tile is RUBRAVIEW_TILE_SIDE output pixels a side at a level: level L
 * holds one output pixel for every 2^L picture pixels, so level 0 is the
 * picture itself. The level chosen is the coarsest that is still at least
 * as sharp as the screen. Everything here is in picture pixels (after the
 * picture's own EXIF turn, before the reader's rotation).
 */

#define RUBRAVIEW_TILE_SIDE 512
#define RUBRAVIEW_TILE_MAX_LEVEL 12

typedef struct rubraview_tile_key {
    int32_t level, tx, ty;
} rubraview_tile_key_t;

/** Is the reduced texture magnified on screen (by more than 5 %)? Then
 *  tiles are wanted. `screen_scale` is screen pixels per picture pixel. */
bool rubraview_tiles_wanted(double screen_scale, int32_t picture_w, int32_t texture_w);

/** The coarsest level still at least as sharp as `screen_scale`. */
int32_t rubraview_tile_level(double screen_scale);

/** The picture rectangle a tile covers, clipped to the picture, and the
 *  output size it is decoded at. False for a tile outside the picture. */
bool rubraview_tile_geometry(rubraview_tile_key_t key, int32_t picture_w, int32_t picture_h,
                             int32_t *x, int32_t *y, int32_t *w, int32_t *h,
                             int32_t *out_w, int32_t *out_h);

/** The tiles at `level` that meet the picture rectangle (x0,y0)-(x1,y1),
 *  nearest the rectangle's centre first, at most `cap`. Returns how many. */
size_t rubraview_tiles_visible(double x0, double y0, double x1, double y1, int32_t level,
                               int32_t picture_w, int32_t picture_h,
                               rubraview_tile_key_t *out, size_t cap);

/** The inverse of an affine matrix; false when it has none. */
bool rubraview_mat3x2_invert(rubraview_mat3x2_t m, rubraview_mat3x2_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_TILES_H */
