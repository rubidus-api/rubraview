#ifndef RUBRAVIEW_UI_BOX_H
#define RUBRAVIEW_UI_BOX_H

#include "rubraview/core.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The Dual Floating Box UI (RFC-0001 §3.6): two draggable,
 * semi-transparent anchors that expand into grids of Metro square tiles.
 * This module is the geometry and state machine — tile layout, hit
 * testing, dragging with clamping, hover expansion with an idle
 * collapse delay, pinning, and the toolbox's detach threshold. Painting
 * is the renderer's job; nothing here touches a window.
 *
 * The two boxes differ in one rule (§3.6.2): the Menu Box is clamped
 * strictly inside the client area and never becomes its own window,
 * while the Toolbox may be dragged out to become a detached tool window.
 */

typedef struct rubraview_rect {
    double x, y, width, height;
} rubraview_rect_t;

static inline bool rubraview_rect_contains(rubraview_rect_t r, double px, double py) {
    return px >= r.x && px < r.x + r.width && py >= r.y && py < r.y + r.height;
}

typedef enum rubraview_box_kind {
    RUBRAVIEW_BOX_TOOLBOX = 0, /* §3.6.1: may detach into its own window */
    RUBRAVIEW_BOX_MENU,        /* §3.6.2: strictly inside the client area */
} rubraview_box_kind_t;

typedef enum rubraview_box_state {
    RUBRAVIEW_BOX_COLLAPSED = 0, /* the compact anchor tile */
    RUBRAVIEW_BOX_EXPANDED,      /* unfolded by hover; collapses again when the pointer leaves */
    RUBRAVIEW_BOX_LOCKED_OPEN,   /* click-to-lock: stays until dismissed */
    RUBRAVIEW_BOX_DETACHED,      /* toolbox only: now an independent window */
} rubraview_box_state_t;

/** §3.6.4 tile metrics, in physical pixels (the caller scales by DPI). */
typedef struct rubraview_tile_metrics {
    double anchor_size;  /* collapsed anchor, e.g. 40 */
    double tile_size;    /* expanded tile, 48 (touch minimum) to 64 */
    double gutter;       /* spacing between tiles, e.g. 8 */
    double padding;      /* box padding around the grid */
    int32_t columns;     /* tiles per row when expanded */
} rubraview_tile_metrics_t;

rubraview_tile_metrics_t rubraview_tile_metrics_default(double dpi_scale);

typedef struct rubraview_box {
    rubraview_box_kind_t kind;
    rubraview_box_state_t state;
    double anchor_x, anchor_y;   /* the anchor's top-left, in client coordinates */
    bool   pinned;               /* §3.6.1: pinned boxes ignore the idle collapse timer */
    double idle_seconds;         /* time since the pointer left the box */
    int32_t tile_count;          /* how many tiles the expanded grid holds */
} rubraview_box_t;

rubraview_box_t rubraview_box_create(rubraview_box_kind_t kind, double anchor_x, double anchor_y, int32_t tile_count);

/** The box's current outer rectangle: the anchor when collapsed, the whole grid when expanded. */
rubraview_rect_t rubraview_box_bounds(const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics);

/** The rectangle of one tile in the expanded grid, in client coordinates. */
rubraview_rect_t rubraview_box_tile_rect(const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics, int32_t tile_index);

/** Which tile is under the point, or -1 for none (including when collapsed). */
int32_t rubraview_box_tile_at(const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics, double px, double py);

/**
 * §3.6.3: move the anchor. The Menu Box is clamped so its whole
 * expanded body stays inside the client area; the Toolbox may be
 * dragged past the edge, which is how it detaches.
 */
void rubraview_box_drag_to(rubraview_box_t *box, const rubraview_tile_metrics_t *metrics,
                           double new_x, double new_y, double window_width, double window_height);

/**
 * §3.6.1: the toolbox becomes a detached window once dragged more than
 * `threshold` pixels beyond the client edge. Returns true if the state
 * changed. The menu box never detaches.
 */
bool rubraview_box_update_detach(rubraview_box_t *box, const rubraview_tile_metrics_t *metrics,
                                 double window_width, double window_height, double threshold);

/** Dragging a detached toolbox back over the canvas docks it again. */
bool rubraview_box_dock(rubraview_box_t *box);

void rubraview_box_hover_enter(rubraview_box_t *box);
void rubraview_box_hover_leave(rubraview_box_t *box);
void rubraview_box_click_anchor(rubraview_box_t *box); /* toggles the click-to-lock state */
void rubraview_box_dismiss(rubraview_box_t *box);      /* Esc, or a click outside */

/**
 * §3.6.3: advance the idle timer; an unpinned, hover-expanded box
 * collapses after `grace_seconds` (0.5 s in the RFC). Returns true when
 * this call collapsed it.
 */
bool rubraview_box_tick(rubraview_box_t *box, double delta_seconds, double grace_seconds);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_UI_BOX_H */
