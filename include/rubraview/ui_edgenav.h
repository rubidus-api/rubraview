#ifndef RUBRAVIEW_UI_EDGENAV_H
#define RUBRAVIEW_UI_EDGENAV_H

#include "rubraview/core.h"
#include "rubraview/ui_box.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The edge buttons (owner, 2026-09-29): three buttons stacked at the
 * vertical middle of each side of the window, shown while the pointer is
 * near them. For pictures the left stack is previous / ten back / first
 * and the right next / ten on / last; for a film or music, back 5 s /
 * back 30 s / previous file and on 5 s / on 30 s / next file. Top to
 * bottom in that order. Geometry and the actions only: the viewer paints.
 */

typedef enum rubraview_edge_side {
    RUBRAVIEW_EDGE_NONE = -1,
    RUBRAVIEW_EDGE_LEFT = 0,
    RUBRAVIEW_EDGE_RIGHT = 1,
} rubraview_edge_side_t;

#define RUBRAVIEW_EDGE_SLOTS 3

typedef struct rubraview_edge_hit {
    rubraview_edge_side_t side;
    int32_t slot;          /* 0 top .. 2 bottom; -1 with no side */
} rubraview_edge_hit_t;

typedef struct rubraview_edgenav {
    double button;         /* a button's side, scaled by DPI */
    double gap;            /* between the buttons, and from the window's edge */
    double reach;          /* how far in from the edge the pointer reveals a stack */
} rubraview_edgenav_t;

rubraview_edgenav_t rubraview_edgenav_create(double dpi_scale);

/** One button's rectangle in a `view_w` x `view_h` client area. */
rubraview_rect_t rubraview_edgenav_rect(const rubraview_edgenav_t *nav, rubraview_edge_side_t side, int32_t slot,
                                        double view_w, double view_h);

/**
 * Which stack the pointer reveals: within `reach` of that side, and
 * vertically no further from the stack than one button. NONE elsewhere,
 * and in a window too narrow for both stacks and a picture between them.
 */
rubraview_edge_side_t rubraview_edgenav_revealed(const rubraview_edgenav_t *nav, double px, double py,
                                                 double view_w, double view_h);

/** The button under the point, among the revealed stack's. */
rubraview_edge_hit_t rubraview_edgenav_hit(const rubraview_edgenav_t *nav, double px, double py,
                                           double view_w, double view_h);

/** The action a button runs: `media` when a film or music is on screen. */
const char *rubraview_edgenav_action(rubraview_edge_side_t side, int32_t slot, bool media);

/** A short caption for the button, shown beside it while hovered. */
const char *rubraview_edgenav_caption(rubraview_edge_side_t side, int32_t slot, bool media);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_UI_EDGENAV_H */
