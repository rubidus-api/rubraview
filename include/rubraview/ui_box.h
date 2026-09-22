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

/*
 * The collapsed anchor is a bar twice as wide as it is tall: two square
 * halves that behave differently (owner, 2026-09-10).
 *
 * The two ways a box can open used to be one control, which meant the
 * pointer merely passing over it opened something the reader did not
 * ask for. Splitting them means each half does exactly one thing, and
 * the reader can see which is which before touching it.
 */
typedef enum rubraview_anchor_half {
    RUBRAVIEW_ANCHOR_NONE = -1,
    RUBRAVIEW_ANCHOR_CLICK = 0,  /* the left square: opens only when clicked */
    RUBRAVIEW_ANCHOR_HOVER = 1,  /* the right square: opens when the pointer rests on it */
} rubraview_anchor_half_t;

/*
 * Where a box goes when the reader asks for it back.
 *
 * A floating box that has been dragged somewhere unhelpful — behind
 * another monitor's edge, or off the window entirely — is a box the
 * reader cannot get to. Each box has a corner it belongs to, and one
 * button puts both of them back there.
 */
typedef enum rubraview_box_home {
    RUBRAVIEW_BOX_HOME_TOP_LEFT = 0,
    RUBRAVIEW_BOX_HOME_BOTTOM_RIGHT,
} rubraview_box_home_t;

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
    /* The toolbox strip and the boxes' header row (owner, 2026-09-22). */
    double button_size;  /* a toolbox button: half a tile wide, a quarter of its area */
    double header_height;/* the row holding the pin (and, in the toolbox, the seek bar) */
    double title_height; /* the toolbox row with the file's name */
    int32_t strip_columns; /* toolbox buttons per row */
} rubraview_tile_metrics_t;

rubraview_tile_metrics_t rubraview_tile_metrics_default(double dpi_scale);

typedef struct rubraview_box {
    rubraview_box_kind_t kind;
    rubraview_box_state_t state;
    double anchor_x, anchor_y;   /* the anchor's top-left, in client coordinates */
    bool   pinned;               /* §3.6.1: pinned boxes ignore the idle collapse timer */
    double idle_seconds;         /* time since the pointer left the box */
    int32_t tile_count;          /* how many tiles the expanded grid holds */
    rubraview_box_home_t home;   /* the corner "put it back" returns it to */
    /* The client area the box opens in, set by the caller each frame; 0
       when unknown. Known, an open grid sits beside its anchor instead of
       over it — below, or above when there is no room below — and is
       shifted to stay inside (RFC-0002 §4: ten tiles by a corner). */
    double view_width, view_height;
    bool timeline;               /* toolbox: a seek bar in the header row (a film or music) */
} rubraview_box_t;

/*
 * The toolbox strip (owner, 2026-09-22): the pin and, for a film or
 * music, the seek bar on the top row; the file's name under them; then
 * the buttons, `strip_columns` to a row, as many rows as they need. All
 * rectangles are relative to the body's top-left, so the detached window
 * lays itself out with the same numbers.
 */
typedef struct rubraview_toolbox_layout {
    double width, height;
    rubraview_rect_t pin, timeline, title;
    double buttons_y;
    int32_t columns, rows;
} rubraview_toolbox_layout_t;

rubraview_toolbox_layout_t rubraview_toolbox_layout(const rubraview_tile_metrics_t *metrics, int32_t count, bool timeline);
rubraview_rect_t rubraview_toolbox_button_rect(const rubraview_toolbox_layout_t *layout,
                                               const rubraview_tile_metrics_t *metrics, int32_t index);

rubraview_box_t rubraview_box_create(rubraview_box_kind_t kind, double anchor_x, double anchor_y, int32_t tile_count);

/**
 * The collapsed anchor bar: two squares side by side, so twice
 * `anchor_size` wide and one `anchor_size` tall.
 */
rubraview_rect_t rubraview_box_anchor_rect(const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics);

/** One half of that bar, for painting the two buttons. */
rubraview_rect_t rubraview_box_anchor_half_rect(const rubraview_box_t *box,
                                                const rubraview_tile_metrics_t *metrics,
                                                rubraview_anchor_half_t half);

/** Which half the point is over, or NONE. */
rubraview_anchor_half_t rubraview_box_anchor_half_at(const rubraview_box_t *box,
                                                      const rubraview_tile_metrics_t *metrics,
                                                      double px, double py);

/**
 * Feed the pointer. Only the hover half opens the box; resting on the
 * click half does nothing, which is the point of having two.
 * Returns true when the state changed.
 */
bool rubraview_box_pointer(rubraview_box_t *box, const rubraview_tile_metrics_t *metrics,
                           double px, double py);

/**
 * Feed a click. The click half toggles the box open and locked; the
 * hover half pins what hovering already opened, so it stays when the
 * pointer leaves. A click anywhere else is not this box's business.
 * Returns true when the click was taken.
 */
bool rubraview_box_click(rubraview_box_t *box, const rubraview_tile_metrics_t *metrics,
                         double px, double py);

/**
 * Put the box back in its corner, fully inside the window, and dock it
 * if it had been dragged out. This is the answer to a floating box that
 * has wandered somewhere the reader cannot reach.
 */
void rubraview_box_snap_home(rubraview_box_t *box, const rubraview_tile_metrics_t *metrics,
                             double window_width, double window_height);

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

/* The pin at an open box's top-left (owner, 2026-09-22), in client
   coordinates; empty while the box is folded. The toolbox's seek bar and
   name rows likewise (empty for the menu, or with no film). */
rubraview_rect_t rubraview_box_pin_rect(const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics);
rubraview_rect_t rubraview_box_timeline_rect(const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics);
rubraview_rect_t rubraview_box_title_rect(const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics);

/** The pin reads "on" for a pinned box and for one the left half's click opened. */
bool rubraview_box_pin_shown_on(const rubraview_box_t *box);

/**
 * A click on the pin: on, the box stays open when the pointer leaves;
 * off, it is an ordinary hover-opened box again and folds when left.
 * False (nothing done) when the click is not on the pin.
 */
bool rubraview_box_pin_click(rubraview_box_t *box, const rubraview_tile_metrics_t *metrics, double px, double py);

/** Dragging a detached toolbox back over the canvas docks it again. */
bool rubraview_box_dock(rubraview_box_t *box);

void rubraview_box_hover_enter(rubraview_box_t *box);
void rubraview_box_hover_leave(rubraview_box_t *box);
void rubraview_box_click_anchor(rubraview_box_t *box); /* toggles the click-to-lock state */
void rubraview_box_dismiss(rubraview_box_t *box);      /* Esc, or a click outside; a pinned box stays */

/** §3.6.1 pin: pinned, the box is open and stays open — no idle collapse, no dismissal. */
void rubraview_box_set_pinned(rubraview_box_t *box, bool pinned);

/**
 * True once for each time the box goes from collapsed to open, however it
 * was opened (a click, hovering, a key). `was_open` is the caller's memory
 * of the last answer. The menu box starts again at its root there: a menu
 * that reopened where it was left put the reader's next tap on whatever
 * sat in that place one level down (Delete, where the root had Show).
 */
bool rubraview_box_just_opened(const rubraview_box_t *box, bool *was_open);

/**
 * §3.6.3: advance the idle timer; an unpinned, hover-expanded box
 * collapses after `grace_seconds` (0.5 s in the RFC). Returns true when
 * this call collapsed it.
 */
bool rubraview_box_tick(rubraview_box_t *box, double delta_seconds, double grace_seconds);

/* ---- D-15: how see-through a box is ---- */

#define RUBRAVIEW_BOX_OPACITY_MIN 30.0    /* percent: never so faint the box cannot be found */
#define RUBRAVIEW_BOX_OPACITY_MAX 100.0
#define RUBRAVIEW_BOX_OPACITY_STEP 5.0

/**
 * Alt + wheel over a box: `notches` wheel notches (positive away from the
 * reader, more opaque) from `percent`, on the 5 % grid, kept within
 * 30–100 %.
 */
double rubraview_box_opacity_step(double percent, double notches);

/** A colour with its alpha scaled by `percent` (of what it had); red, green and blue untouched. */
uint32_t rubraview_box_fade(uint32_t argb, double percent);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_UI_BOX_H */
