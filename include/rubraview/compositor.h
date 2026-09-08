#ifndef RUBRAVIEW_COMPOSITOR_H
#define RUBRAVIEW_COMPOSITOR_H

#include "rubraview/core.h"
#include "rubraview/layout.h"
#include "rubraview/viewport.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Multi-page compositor (RFC-0001 §4.1.3). Turns one spread from the
 * layout engine (RV-021) plus the loaded pages' pixel sizes into the one
 * or two draw commands the renderer issues — each carrying its own
 * source sub-rectangle and its own transform, so both pages are drawn in
 * a single pass with no CPU stitching and no intermediate buffer.
 *
 * The maths is the §4.1.3 pair, generalised to unequal page sizes:
 *   M_left  = M_viewport . T(0, centre_left)
 *   M_right = M_viewport . T(W_left + gutter, centre_right)
 * where M_viewport fits the combined spread into the window (RV-022) and
 * then applies the interactive zoom and pan.
 *
 * This is portable geometry with no renderer dependency, which is why it
 * lives in core rather than behind the PAL.
 */

typedef struct rubraview_page_size {
    double width, height; /* the loaded page's pixel dimensions */
} rubraview_page_size_t;

typedef struct rubraview_draw_command {
    int32_t page_index;              /* source page index this command draws */
    double  src_left, src_top;       /* sub-rectangle of that page, in its own pixels */
    double  src_right, src_bottom;   /* the whole page unless the spread was split (§3.3.7) */
    rubraview_mat3x2_t transform;    /* maps the sub-rectangle's own pixel space to client pixels */
} rubraview_draw_command_t;

typedef struct rubraview_composition {
    rubraview_draw_command_t commands[2];
    size_t count;                    /* 0 when nothing can be drawn, 1 single page, 2 side-by-side */
    double content_width, content_height; /* the composed spread's size in page pixels, before fitting */
    double scale;                    /* the on-screen scale actually applied (fit scale x zoom) */
} rubraview_composition_t;

/**
 * Compose one spread.
 *
 * `left_size` and `right_size` describe the pages named by
 * spread->left_index and spread->right_index; pass NULL for a side the
 * spread does not use or whose page has not loaded. `gutter` is the gap
 * between paired pages in page pixels (§3.3 point 2). `zoom` multiplies
 * the fit scale (1.0 = exactly the fit mode), and `pan_x`/`pan_y` shift
 * the result in client pixels.
 */
rubraview_composition_t rubraview_compose_spread(const rubraview_spread_t *spread,
                                                 const rubraview_page_size_t *left_size,
                                                 const rubraview_page_size_t *right_size,
                                                 double window_width, double window_height,
                                                 rubraview_fit_mode_t fit_mode,
                                                 double gutter,
                                                 double zoom,
                                                 double pan_x, double pan_y);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_COMPOSITOR_H */
