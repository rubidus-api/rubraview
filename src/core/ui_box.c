#include "rubraview/ui_box.h"
#include <math.h>

rubraview_tile_metrics_t rubraview_tile_metrics_default(double dpi_scale) {
    if (dpi_scale <= 0.0) dpi_scale = 1.0;
    /* §3.6.4: 48 px is the touch minimum, 64 px the desktop target, with
       8 px gutters; everything scales with the monitor's DPI (§4.2). */
    return (rubraview_tile_metrics_t){
        .anchor_size = 40.0 * dpi_scale,
        .tile_size = 64.0 * dpi_scale,
        .gutter = 8.0 * dpi_scale,
        .padding = 8.0 * dpi_scale,
        .columns = 4,
    };
}

rubraview_box_t rubraview_box_create(rubraview_box_kind_t kind, double anchor_x, double anchor_y, int32_t tile_count) {
    return (rubraview_box_t){
        .kind = kind,
        .state = RUBRAVIEW_BOX_COLLAPSED,
        .anchor_x = anchor_x,
        .anchor_y = anchor_y,
        .pinned = false,
        .idle_seconds = 0.0,
        .tile_count = tile_count > 0 ? tile_count : 0,
        /* §3.6.2's menu box lives top-left and the toolbox bottom-right,
           which is also where "put it back" returns them. */
        .home = kind == RUBRAVIEW_BOX_MENU ? RUBRAVIEW_BOX_HOME_TOP_LEFT
                                           : RUBRAVIEW_BOX_HOME_BOTTOM_RIGHT,
    };
}

static bool is_open(const rubraview_box_t *box) {
    return box->state == RUBRAVIEW_BOX_EXPANDED ||
           box->state == RUBRAVIEW_BOX_LOCKED_OPEN ||
           box->state == RUBRAVIEW_BOX_DETACHED;
}

static int32_t grid_columns(const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics) {
    int32_t columns = metrics->columns > 0 ? metrics->columns : 1;
    if (box->tile_count < columns) columns = box->tile_count > 0 ? box->tile_count : 1;
    return columns;
}

static int32_t grid_rows(const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics) {
    int32_t columns = grid_columns(box, metrics);
    if (box->tile_count <= 0) return 0;
    return (box->tile_count + columns - 1) / columns;
}

/* The collapsed anchor is two squares wide: a click button and a hover
   button, side by side. */
rubraview_rect_t rubraview_box_anchor_rect(const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics) {
    if (!box || !metrics) return (rubraview_rect_t){0};
    return (rubraview_rect_t){
        .x = box->anchor_x, .y = box->anchor_y,
        .width = metrics->anchor_size * 2.0, .height = metrics->anchor_size,
    };
}

rubraview_rect_t rubraview_box_anchor_half_rect(const rubraview_box_t *box,
                                                const rubraview_tile_metrics_t *metrics,
                                                rubraview_anchor_half_t half) {
    if (!box || !metrics || half == RUBRAVIEW_ANCHOR_NONE) return (rubraview_rect_t){0};
    return (rubraview_rect_t){
        .x = box->anchor_x + (half == RUBRAVIEW_ANCHOR_HOVER ? metrics->anchor_size : 0.0),
        .y = box->anchor_y,
        .width = metrics->anchor_size,
        .height = metrics->anchor_size,
    };
}

rubraview_anchor_half_t rubraview_box_anchor_half_at(const rubraview_box_t *box,
                                                      const rubraview_tile_metrics_t *metrics,
                                                      double px, double py) {
    if (!box || !metrics) return RUBRAVIEW_ANCHOR_NONE;

    /* The anchor stays where it is while the box is open, so the two
       buttons remain reachable — closing by leaving is what the hover
       half is for, and a control that moves out from under the pointer
       cannot be clicked. */
    if (rubraview_rect_contains(rubraview_box_anchor_half_rect(box, metrics, RUBRAVIEW_ANCHOR_CLICK), px, py)) {
        return RUBRAVIEW_ANCHOR_CLICK;
    }
    if (rubraview_rect_contains(rubraview_box_anchor_half_rect(box, metrics, RUBRAVIEW_ANCHOR_HOVER), px, py)) {
        return RUBRAVIEW_ANCHOR_HOVER;
    }
    return RUBRAVIEW_ANCHOR_NONE;
}

bool rubraview_box_pointer(rubraview_box_t *box, const rubraview_tile_metrics_t *metrics,
                           double px, double py) {
    if (!box || !metrics) return false;

    rubraview_anchor_half_t half = rubraview_box_anchor_half_at(box, metrics, px, py);
    bool inside_body = rubraview_rect_contains(rubraview_box_bounds(box, metrics), px, py);

    /* Only the hover half opens. Resting on the click half deliberately
       does nothing: that is the difference the reader can rely on. */
    if (half == RUBRAVIEW_ANCHOR_HOVER) {
        if (box->state == RUBRAVIEW_BOX_COLLAPSED) {
            rubraview_box_hover_enter(box);
            return true;
        }
        box->idle_seconds = 0.0;
        return false;
    }

    if (inside_body || half == RUBRAVIEW_ANCHOR_CLICK) {
        /* Over the box: it is in use, so the collapse timer waits. */
        box->idle_seconds = 0.0;
        return false;
    }

    /* Outside: the timer runs. It is *not* restarted here — the grace
       counts from when the pointer last left the box, and restarting it
       on every stray movement elsewhere would keep an abandoned box open
       for as long as the mouse kept moving. */
    return false;
}

bool rubraview_box_click(rubraview_box_t *box, const rubraview_tile_metrics_t *metrics,
                         double px, double py) {
    if (!box || !metrics) return false;

    switch (rubraview_box_anchor_half_at(box, metrics, px, py)) {
        case RUBRAVIEW_ANCHOR_CLICK:
            rubraview_box_click_anchor(box);
            return true;
        case RUBRAVIEW_ANCHOR_HOVER:
            /* Hover already opened it; clicking says "keep it". */
            if (box->state == RUBRAVIEW_BOX_EXPANDED) {
                box->state = RUBRAVIEW_BOX_LOCKED_OPEN;
            } else if (box->state == RUBRAVIEW_BOX_LOCKED_OPEN) {
                box->state = RUBRAVIEW_BOX_COLLAPSED;
            } else {
                rubraview_box_click_anchor(box);
            }
            return true;
        case RUBRAVIEW_ANCHOR_NONE:
        default:
            return false;
    }
}

void rubraview_box_snap_home(rubraview_box_t *box, const rubraview_tile_metrics_t *metrics,
                             double window_width, double window_height) {
    if (!box || !metrics) return;

    /* A toolbox that had been dragged out of the window comes back in:
       leaving it outside would defeat the purpose of the button. */
    if (box->state == RUBRAVIEW_BOX_DETACHED) box->state = RUBRAVIEW_BOX_COLLAPSED;

    rubraview_rect_t bounds = rubraview_box_bounds(box, metrics);
    double margin = metrics->gutter;

    double x = margin;
    double y = margin;
    if (box->home == RUBRAVIEW_BOX_HOME_BOTTOM_RIGHT) {
        x = window_width - bounds.width - margin;
        y = window_height - bounds.height - margin;
    }

    /* A window smaller than the box still gets the box's top-left
       corner on screen, which is the part with the buttons on it. */
    if (x < 0.0) x = 0.0;
    if (y < 0.0) y = 0.0;

    box->anchor_x = x;
    box->anchor_y = y;
}

rubraview_rect_t rubraview_box_bounds(const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics) {
    if (!box || !metrics) return (rubraview_rect_t){0};

    if (!is_open(box) || box->tile_count <= 0) {
        /* Collapsed, the box *is* the two-button anchor bar. */
        return (rubraview_rect_t){
            .x = box->anchor_x, .y = box->anchor_y,
            .width = metrics->anchor_size * 2.0, .height = metrics->anchor_size,
        };
    }

    int32_t columns = grid_columns(box, metrics);
    int32_t rows = grid_rows(box, metrics);

    double width = metrics->padding * 2.0 + columns * metrics->tile_size + (columns - 1) * metrics->gutter;
    double height = metrics->padding * 2.0 + rows * metrics->tile_size + (rows - 1) * metrics->gutter;

    return (rubraview_rect_t){ .x = box->anchor_x, .y = box->anchor_y, .width = width, .height = height };
}

rubraview_rect_t rubraview_box_tile_rect(const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics, int32_t tile_index) {
    if (!box || !metrics || tile_index < 0 || tile_index >= box->tile_count || !is_open(box)) {
        return (rubraview_rect_t){0};
    }

    int32_t columns = grid_columns(box, metrics);
    int32_t row = tile_index / columns;
    int32_t column = tile_index % columns;

    return (rubraview_rect_t){
        .x = box->anchor_x + metrics->padding + column * (metrics->tile_size + metrics->gutter),
        .y = box->anchor_y + metrics->padding + row * (metrics->tile_size + metrics->gutter),
        .width = metrics->tile_size,
        .height = metrics->tile_size,
    };
}

int32_t rubraview_box_tile_at(const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics, double px, double py) {
    if (!box || !metrics || !is_open(box)) return -1;

    for (int32_t i = 0; i < box->tile_count; ++i) {
        if (rubraview_rect_contains(rubraview_box_tile_rect(box, metrics, i), px, py)) {
            return i;
        }
    }
    return -1;
}

void rubraview_box_drag_to(rubraview_box_t *box, const rubraview_tile_metrics_t *metrics,
                           double new_x, double new_y, double window_width, double window_height) {
    if (!box || !metrics) return;

    box->anchor_x = new_x;
    box->anchor_y = new_y;

    /* §3.6.2: the Menu Box's coordinates are clamped at all times so the
       whole body stays within the client area — it must never spill off
       a small remote-desktop screen. The Toolbox is deliberately not
       clamped: dragging it past the edge is how it detaches. */
    if (box->kind != RUBRAVIEW_BOX_MENU) return;

    rubraview_rect_t bounds = rubraview_box_bounds(box, metrics);
    double max_x = window_width - bounds.width;
    double max_y = window_height - bounds.height;
    if (max_x < 0.0) max_x = 0.0;
    if (max_y < 0.0) max_y = 0.0;

    if (box->anchor_x < 0.0) box->anchor_x = 0.0;
    if (box->anchor_y < 0.0) box->anchor_y = 0.0;
    if (box->anchor_x > max_x) box->anchor_x = max_x;
    if (box->anchor_y > max_y) box->anchor_y = max_y;
}

bool rubraview_box_update_detach(rubraview_box_t *box, const rubraview_tile_metrics_t *metrics,
                                 double window_width, double window_height, double threshold) {
    if (!box || !metrics || box->kind != RUBRAVIEW_BOX_TOOLBOX) return false;
    if (box->state == RUBRAVIEW_BOX_DETACHED) return false;
    if (threshold < 0.0) threshold = 0.0;

    rubraview_rect_t bounds = rubraview_box_bounds(box, metrics);
    bool beyond =
        bounds.x + bounds.width < -threshold ||
        bounds.y + bounds.height < -threshold ||
        bounds.x > window_width + threshold ||
        bounds.y > window_height + threshold;

    if (!beyond) return false;

    box->state = RUBRAVIEW_BOX_DETACHED;
    return true;
}

bool rubraview_box_dock(rubraview_box_t *box) {
    if (!box || box->state != RUBRAVIEW_BOX_DETACHED) return false;
    box->state = RUBRAVIEW_BOX_LOCKED_OPEN; /* docking leaves it open, not collapsed */
    box->idle_seconds = 0.0;
    return true;
}

void rubraview_box_hover_enter(rubraview_box_t *box) {
    if (!box) return;
    box->idle_seconds = 0.0;
    if (box->state == RUBRAVIEW_BOX_COLLAPSED) {
        box->state = RUBRAVIEW_BOX_EXPANDED; /* §3.6.3: hover unfolds instantly */
    }
}

void rubraview_box_hover_leave(rubraview_box_t *box) {
    if (!box) return;
    /* Nothing to do: `idle_seconds` counts since the pointer was last
       over the box, so leaving simply stops refreshing it. */
}

void rubraview_box_click_anchor(rubraview_box_t *box) {
    if (!box) return;
    box->idle_seconds = 0.0;
    /* §3.6.3 click-to-lock: a click holds the box open until dismissed;
       clicking again releases it. */
    box->state = (box->state == RUBRAVIEW_BOX_LOCKED_OPEN)
        ? RUBRAVIEW_BOX_COLLAPSED
        : RUBRAVIEW_BOX_LOCKED_OPEN;
}

void rubraview_box_dismiss(rubraview_box_t *box) {
    if (!box) return;
    if (box->state == RUBRAVIEW_BOX_DETACHED) return; /* a separate window is closed, not dismissed */
    box->state = RUBRAVIEW_BOX_COLLAPSED;
    box->idle_seconds = 0.0;
}

bool rubraview_box_tick(rubraview_box_t *box, double delta_seconds, double grace_seconds) {
    if (!box) return false;
    /* Only a hover-expanded, unpinned box collapses on its own: pinned
       and click-locked boxes stay put (§3.6.1 pin, §3.6.3 lock). */
    if (box->state != RUBRAVIEW_BOX_EXPANDED || box->pinned) return false;

    box->idle_seconds += delta_seconds;
    if (box->idle_seconds < grace_seconds) return false;

    box->state = RUBRAVIEW_BOX_COLLAPSED;
    box->idle_seconds = 0.0;
    return true;
}

double rubraview_box_opacity_step(double percent, double notches) {
    if (!(percent == percent)) percent = RUBRAVIEW_BOX_OPACITY_MAX;   /* NaN */
    long steps = (long)(notches > 0.0 ? notches + 0.5 : notches - 0.5);
    double value = round(percent / RUBRAVIEW_BOX_OPACITY_STEP) * RUBRAVIEW_BOX_OPACITY_STEP
                   + (double)steps * RUBRAVIEW_BOX_OPACITY_STEP;
    if (value < RUBRAVIEW_BOX_OPACITY_MIN) value = RUBRAVIEW_BOX_OPACITY_MIN;
    if (value > RUBRAVIEW_BOX_OPACITY_MAX) value = RUBRAVIEW_BOX_OPACITY_MAX;
    return value;
}

uint32_t rubraview_box_fade(uint32_t argb, double percent) {
    if (percent < RUBRAVIEW_BOX_OPACITY_MIN) percent = RUBRAVIEW_BOX_OPACITY_MIN;
    if (percent > RUBRAVIEW_BOX_OPACITY_MAX) percent = RUBRAVIEW_BOX_OPACITY_MAX;
    uint32_t alpha = (argb >> 24) & 0xFFu;
    uint32_t faded = (uint32_t)lround((double)alpha * percent / 100.0);
    return (faded << 24) | (argb & 0x00FFFFFFu);
}
