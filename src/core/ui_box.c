#include "rubraview/ui_box.h"
#include "rubraview/lang.h"
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
        .button_size = 32.0 * dpi_scale,
        .header_height = 20.0 * dpi_scale,
        .title_height = 18.0 * dpi_scale,
        .strip_columns = 8,
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

rubraview_tile_metrics_t rubraview_box_metrics(const rubraview_box_t *box, const rubraview_tile_metrics_t *base) {
    rubraview_tile_metrics_t m = *base;
    if (!box || base->sized) return m;
    double k = box->scale > 0.0 ? box->scale : 1.0;
    m.tile_size *= k;
    m.button_size *= k;
    if (box->columns > 0) {
        if (box->kind == RUBRAVIEW_BOX_TOOLBOX) m.strip_columns = box->columns;
        else m.columns = box->columns;
    }
    m.sized = true;
    return m;
}

static int32_t max_columns(const rubraview_box_t *box) {
    return box->kind == RUBRAVIEW_BOX_TOOLBOX ? 24 : 8;
}

void rubraview_box_set_size(rubraview_box_t *box, int32_t columns, double scale) {
    if (!box) return;
    int32_t lo = box->kind == RUBRAVIEW_BOX_TOOLBOX ? 2 : 1;
    if (columns != 0 && columns < lo) columns = lo;
    if (columns > max_columns(box)) columns = max_columns(box);
    if (!(scale >= RUBRAVIEW_BOX_SCALE_MIN)) scale = scale > 0.0 ? RUBRAVIEW_BOX_SCALE_MIN : 1.0;
    if (scale > RUBRAVIEW_BOX_SCALE_MAX) scale = RUBRAVIEW_BOX_SCALE_MAX;
    box->columns = columns;
    box->scale = scale;
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
    if (box->headless) return (rubraview_rect_t){ .x = box->anchor_x, .y = box->anchor_y };
    return (rubraview_rect_t){
        .x = box->anchor_x, .y = box->anchor_y,
        .width = metrics->anchor_size * 2.0, .height = metrics->anchor_size,
    };
}

rubraview_rect_t rubraview_box_anchor_half_rect(const rubraview_box_t *box,
                                                const rubraview_tile_metrics_t *metrics,
                                                rubraview_anchor_half_t half) {
    if (!box || !metrics || half == RUBRAVIEW_ANCHOR_NONE || box->headless) return (rubraview_rect_t){0};
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
    if (!box || !metrics || box->headless) return RUBRAVIEW_ANCHOR_NONE;

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
    bool inside_body = rubraview_rect_contains(rubraview_box_bounds(box, metrics), px, py) ||
                       rubraview_rect_contains(rubraview_box_pin_rect(box, metrics), px, py) ||
                       rubraview_rect_contains(rubraview_box_grip_rect(box, metrics), px, py) ||
                       box->grip_active;   /* sizing: the pointer may run past the box */

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
            /* The hover half only hovers (owner, 2026-09-22): the click is
               taken, so it does not reach the canvas, and changes nothing.
               Keeping a box open is the pin's job. */
            return true;
        case RUBRAVIEW_ANCHOR_NONE:
        default:
            return false;
    }
}

double rubraview_box_share_place(double share, double window, double size, double margin) {
    double room = window - size - 2.0 * margin;
    if (room <= 0.0) return window > size ? (window - size) / 2.0 : 0.0;
    if (!(share > 0.0)) share = 0.0;
    if (share > 1.0) share = 1.0;
    return margin + share * room;
}

double rubraview_box_share_of(double position, double window, double size, double margin) {
    double room = window - size - 2.0 * margin;
    if (room <= 0.0) return 0.5;
    double share = (position - margin) / room;
    return share < 0.0 ? 0.0 : share > 1.0 ? 1.0 : share;
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
    double y = margin + (box->home_top > 0.0 ? box->home_top : 0.0);
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

/* Where an open grid of this size goes. Without a known view it starts at
   the anchor, as it always did; with one it clears the anchor bar and
   stays inside the view. */
static void grid_origin(const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics,
                        double width, double height, double *out_x, double *out_y) {
    *out_x = box->anchor_x;
    *out_y = box->anchor_y;
    if (box->view_width <= 0.0 || box->view_height <= 0.0) return;
    if (box->headless) {
        if (*out_x + width > box->view_width) *out_x = box->view_width - width;
        if (*out_x < 0.0) *out_x = 0.0;
        if (*out_y + height > box->view_height) *out_y = box->view_height - height;
        if (*out_y < 0.0) *out_y = 0.0;
        return;
    }
    double below = box->anchor_y + metrics->anchor_size + metrics->gutter;
    double above = box->anchor_y - metrics->gutter - height;
    if (box->open_side != 0) {
        /* the side it opened on, pulled inside when it has grown past the edge */
        *out_y = box->open_side > 0 ? below : above;
        if (*out_x + width > box->view_width) *out_x = box->view_width - width;
        if (*out_x < 0.0) *out_x = 0.0;
        if (*out_y + height > box->view_height) *out_y = box->view_height - height;
        if (*out_y < 0.0) *out_y = 0.0;
        return;
    }
    *out_y = (below + height <= box->view_height || above < 0.0) ? below : above;
    if (*out_x + width > box->view_width) *out_x = box->view_width - width;
    if (*out_x < 0.0) *out_x = 0.0;
    if (*out_y + height > box->view_height && above >= 0.0) *out_y = above;
    if (*out_y < 0.0) *out_y = 0.0;
}

rubraview_toolbox_shape_t rubraview_box_toolbox_shape(const rubraview_box_t *box) {
    rubraview_toolbox_shape_t s = {0};
    if (!box) return s;
    s.count = box->tile_count;
    s.timeline = box->timeline;
    s.title_lines = box->title_lines;
    s.group_count = box->group_count;
    if (s.group_count > RUBRAVIEW_TOOLBOX_MAX_GROUPS) s.group_count = RUBRAVIEW_TOOLBOX_MAX_GROUPS;
    for (int32_t i = 0; i < s.group_count; ++i) s.group_sizes[i] = box->group_sizes[i];
    return s;
}

rubraview_toolbox_layout_t rubraview_toolbox_layout_shaped(const rubraview_tile_metrics_t *m,
                                                          const rubraview_toolbox_shape_t *shape) {
    rubraview_toolbox_layout_t l = {0};
    if (!m || !shape) return l;
    double pad = m->padding, g = m->gutter / 2.0, b = m->button_size, head = m->header_height;
    l.columns = m->strip_columns > 0 ? m->strip_columns : 8;
    l.group_gap = g;
    l.group_pad = g;

    /* The groups as given, without the empty ones and never more buttons
       than there are; what they leave over is one more group. */
    int32_t left = shape->count > 0 ? shape->count : 0;
    for (int32_t i = 0; i < shape->group_count && i < RUBRAVIEW_TOOLBOX_MAX_GROUPS && left > 0; ++i) {
        int32_t n = shape->group_sizes[i] < left ? shape->group_sizes[i] : left;
        if (n <= 0) continue;
        l.group_sizes[l.group_count++] = n;
        left -= n;
    }
    if (left > 0) {
        if (l.group_count < RUBRAVIEW_TOOLBOX_MAX_GROUPS) l.group_sizes[l.group_count++] = left;
        else l.group_sizes[l.group_count - 1] += left;
    }

    /* The pin sits beside the anchor, not in here (owner, 2026-09-22):
       the top row is the seek bar's alone, and without one the name
       comes first. */
    double top = pad;
    if (shape->timeline) top = pad + head + g;
    l.title_lines = shape->title_lines < 1 ? 1
                  : shape->title_lines > RUBRAVIEW_TOOLBOX_MAX_TITLE_LINES ? RUBRAVIEW_TOOLBOX_MAX_TITLE_LINES : shape->title_lines;
    l.title_line_height = m->title_height;
    l.buttons_y = top + m->title_height * l.title_lines + g;

    /* The groups along lines, as words wrap (owner, 2026-10-15). */
    double fp = l.group_pad, x = pad, y = l.buttons_y, line_h = 0.0, widest = 0.0;
    int32_t used = 0, line_rows = 0;
    for (int32_t k = 0; k < l.group_count; ++k) {
        int32_t n = l.group_sizes[k];
        int32_t cols = n < l.columns ? n : l.columns;
        int32_t rows = (n + cols - 1) / cols;
        if (used > 0 && used + n > l.columns) {
            y += line_h + l.group_gap;
            l.rows += line_rows;
            x = pad; used = 0; line_h = 0.0; line_rows = 0;
        }
        double w = 2.0 * fp + cols * b + (cols - 1) * g, h = 2.0 * fp + rows * b + (rows - 1) * g;
        l.group_columns[k] = cols;
        l.group_rects[k] = (rubraview_rect_t){ x, y, w, h };
        x += w + l.group_gap;
        used += cols;
        if (h > line_h) line_h = h;
        if (rows > line_rows) line_rows = rows;
        if (x - l.group_gap - pad > widest) widest = x - l.group_gap - pad;
    }
    l.rows += line_rows;
    double least = 2.0 * fp + l.columns * b + (l.columns - 1) * g;
    l.width = pad * 2.0 + (widest > least ? widest : least);

    if (shape->timeline) l.timeline = (rubraview_rect_t){ pad, pad + head * 0.25, l.width - pad * 2.0, head * 0.5 };
    l.title = (rubraview_rect_t){ pad, top, l.width - pad * 2.0, m->title_height * l.title_lines };
    l.height = l.group_count > 0 ? y + line_h + pad : l.title.y + l.title.height + pad;
    return l;
}

rubraview_toolbox_layout_t rubraview_toolbox_layout(const rubraview_tile_metrics_t *m, int32_t count, bool timeline) {
    return rubraview_toolbox_layout_shaped(m, &(rubraview_toolbox_shape_t){ .count = count, .timeline = timeline });
}

rubraview_rect_t rubraview_toolbox_button_rect(const rubraview_toolbox_layout_t *l,
                                               const rubraview_tile_metrics_t *m, int32_t i) {
    if (!l || !m || i < 0 || l->columns <= 0) return (rubraview_rect_t){0};
    double g = m->gutter / 2.0, b = m->button_size;
    int32_t first = 0;
    for (int32_t k = 0; k < l->group_count; ++k) {
        int32_t n = l->group_sizes[k];
        if (i < first + n) {
            int32_t at = i - first, cols = l->group_columns[k] > 0 ? l->group_columns[k] : 1;
            rubraview_rect_t f = l->group_rects[k];
            return (rubraview_rect_t){ f.x + l->group_pad + (at % cols) * (b + g),
                                       f.y + l->group_pad + (at / cols) * (b + g), b, b };
        }
        first += n;
    }
    return (rubraview_rect_t){0};
}

/* The strip of this box, with its groups and its name's rows. */
static rubraview_toolbox_layout_t box_layout(const rubraview_box_t *box, const rubraview_tile_metrics_t *sized) {
    rubraview_toolbox_shape_t shape = rubraview_box_toolbox_shape(box);
    return rubraview_toolbox_layout_shaped(sized, &shape);
}

rubraview_rect_t rubraview_box_bounds(const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics) {
    if (!box || !metrics) return (rubraview_rect_t){0};
    rubraview_tile_metrics_t sized = rubraview_box_metrics(box, metrics);
    metrics = &sized;

    if (!is_open(box) || box->tile_count <= 0) {
        /* Collapsed, the box *is* the two-button anchor bar (none when headless). */
        return rubraview_box_anchor_rect(box, metrics);
    }

    double width = 0.0, height = 0.0;
    if (box->kind == RUBRAVIEW_BOX_TOOLBOX) {
        rubraview_toolbox_layout_t l = box_layout(box, metrics);
        width = l.width;
        height = l.height;
    } else {
        int32_t columns = grid_columns(box, metrics);
        int32_t rows = grid_rows(box, metrics);
        width = metrics->padding * 2.0 + columns * metrics->tile_size + (columns - 1) * metrics->gutter;
        height = metrics->padding * 2.0 + rows * metrics->tile_size + (rows - 1) * metrics->gutter;
    }
    double x = 0.0, y = 0.0;
    grid_origin(box, metrics, width, height, &x, &y);

    return (rubraview_rect_t){ .x = x, .y = y, .width = width, .height = height };
}

void rubraview_box_keep_side(rubraview_box_t *box, const rubraview_tile_metrics_t *metrics) {
    if (!box || !metrics) return;
    if (!is_open(box) || box->state == RUBRAVIEW_BOX_DETACHED || box->tile_count <= 0 ||
        box->view_width <= 0.0 || box->view_height <= 0.0) {
        box->open_side = 0;
        return;
    }
    if (box->open_side != 0) return;
    rubraview_tile_metrics_t sized = rubraview_box_metrics(box, metrics);
    rubraview_rect_t r = rubraview_box_bounds(box, metrics);
    box->open_side = r.y >= box->anchor_y + sized.anchor_size ? 1 : -1;
}

rubraview_rect_t rubraview_box_tile_rect(const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics, int32_t tile_index) {
    if (!box || !metrics || tile_index < 0 || tile_index >= box->tile_count || !is_open(box)) {
        return (rubraview_rect_t){0};
    }
    rubraview_tile_metrics_t sized = rubraview_box_metrics(box, metrics);
    metrics = &sized;

    rubraview_rect_t body = rubraview_box_bounds(box, metrics);
    if (box->kind == RUBRAVIEW_BOX_TOOLBOX) {
        rubraview_toolbox_layout_t l = box_layout(box, metrics);
        rubraview_rect_t b = rubraview_toolbox_button_rect(&l, metrics, tile_index);
        return (rubraview_rect_t){ body.x + b.x, body.y + b.y, b.width, b.height };
    }
    int32_t columns = grid_columns(box, metrics);
    int32_t row = tile_index / columns;
    int32_t column = tile_index % columns;

    return (rubraview_rect_t){
        .x = body.x + metrics->padding + column * (metrics->tile_size + metrics->gutter),
        .y = body.y + metrics->padding + row * (metrics->tile_size + metrics->gutter),
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

    /* The anchor, not the open grid: the grid is kept inside the view
       (RFC-0002 §4), so only the anchor can tell it was dragged out. */
    rubraview_rect_t bounds = rubraview_box_anchor_rect(box, metrics);
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
    /* Closing it by its own anchor is the reader's explicit word: the pin goes too. */
    if (box->state == RUBRAVIEW_BOX_COLLAPSED) box->pinned = false;
}

void rubraview_box_click_open(rubraview_box_t *box) {
    if (!box || box->state == RUBRAVIEW_BOX_DETACHED) return;
    box->idle_seconds = 0.0;
    if (box->state == RUBRAVIEW_BOX_COLLAPSED) {
        box->state = box->pinned ? RUBRAVIEW_BOX_LOCKED_OPEN : RUBRAVIEW_BOX_EXPANDED;
    } else {
        box->state = RUBRAVIEW_BOX_COLLAPSED;
        box->pinned = false;
    }
}

void rubraview_box_dismiss(rubraview_box_t *box) {
    if (!box) return;
    if (box->state == RUBRAVIEW_BOX_DETACHED) return; /* a separate window is closed, not dismissed */
    if (box->pinned) return;                          /* §3.6.1: a pinned box stays until unpinned */
    box->state = RUBRAVIEW_BOX_COLLAPSED;
    box->idle_seconds = 0.0;
}

bool rubraview_box_tick(rubraview_box_t *box, double delta_seconds, double grace_seconds) {
    if (!box) return false;
    /* Only a hover-expanded, unpinned box collapses on its own: pinned
       and click-locked boxes stay put (§3.6.1 pin, §3.6.3 lock). */
    if (box->state != RUBRAVIEW_BOX_EXPANDED || box->pinned || box->grip_active) return false;   /* not while being sized */

    box->idle_seconds += delta_seconds;
    if (box->idle_seconds < grace_seconds) return false;

    box->state = RUBRAVIEW_BOX_COLLAPSED;
    box->idle_seconds = 0.0;
    return true;
}

double rubraview_box_widget_size_clamp(double percent) {
    if (!(percent == percent)) return 100.0;   /* NaN */
    return percent < RUBRAVIEW_WIDGET_SIZE_MIN ? RUBRAVIEW_WIDGET_SIZE_MIN : percent > RUBRAVIEW_WIDGET_SIZE_MAX ? RUBRAVIEW_WIDGET_SIZE_MAX : percent;
}

double rubraview_box_widget_size_step(double percent, double notches) {
    long steps = (long)(notches > 0.0 ? notches + 0.5 : notches - 0.5);
    double value = round(rubraview_box_widget_size_clamp(percent) / RUBRAVIEW_WIDGET_SIZE_STEP) * RUBRAVIEW_WIDGET_SIZE_STEP
                   + (double)steps * RUBRAVIEW_WIDGET_SIZE_STEP;
    return rubraview_box_widget_size_clamp(value);
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

void rubraview_box_set_pinned(rubraview_box_t *box, bool pinned) {
    if (!box) return;
    box->pinned = pinned;
    box->idle_seconds = 0.0;
    if (pinned && (box->state == RUBRAVIEW_BOX_COLLAPSED || box->state == RUBRAVIEW_BOX_EXPANDED)) {
        box->state = RUBRAVIEW_BOX_LOCKED_OPEN;
    }
}

bool rubraview_box_just_opened(const rubraview_box_t *box, bool *was_open) {
    if (!box || !was_open) return false;
    bool open = box->state != RUBRAVIEW_BOX_COLLAPSED;
    bool opened = open && !*was_open;
    *was_open = open;
    return opened;
}

/* ---- the pin and the toolbox's rows (owner, 2026-09-22) ---- */

static rubraview_rect_t body_relative(const rubraview_box_t *box, const rubraview_tile_metrics_t *m, rubraview_rect_t r) {
    if (!box || !m || !is_open(box) || r.width <= 0.0) return (rubraview_rect_t){0};
    rubraview_rect_t body = rubraview_box_bounds(box, m);
    return (rubraview_rect_t){ body.x + r.x, body.y + r.y, r.width, r.height };
}

/* The pin is a third square on the anchor's row, there only while the box
   is open (owner, 2026-09-22): to the anchor's right, or to its left when
   the window has no room on the right. */
rubraview_rect_t rubraview_box_pin_rect(const rubraview_box_t *box, const rubraview_tile_metrics_t *m) {
    if (!box || !m || !is_open(box) || box->state == RUBRAVIEW_BOX_DETACHED || box->headless) return (rubraview_rect_t){0};
    double a = m->anchor_size;
    double x = box->anchor_x + a * 2.0;
    if (box->view_width > 0.0 && x + a > box->view_width && box->anchor_x - a >= 0.0) x = box->anchor_x - a;
    return (rubraview_rect_t){ x, box->anchor_y, a, a };
}

/* The grip sits past the pin, on the side the pin went: right of it, or
   left of it when the pin had to go left or the window has no room. */
rubraview_rect_t rubraview_box_grip_rect(const rubraview_box_t *box, const rubraview_tile_metrics_t *m) {
    rubraview_rect_t pin = rubraview_box_pin_rect(box, m);
    if (pin.width <= 0.0) return (rubraview_rect_t){0};
    double a = m->anchor_size;
    double x = pin.x > box->anchor_x ? pin.x + a : pin.x - a;
    if (box->view_width > 0.0 && x + a > box->view_width) x = (pin.x > box->anchor_x ? box->anchor_x : pin.x) - a;
    if (x < 0.0) x = pin.x > box->anchor_x ? pin.x + a : box->anchor_x + a * 2.0;
    return (rubraview_rect_t){ x, box->anchor_y, a, a };
}

bool rubraview_box_grip_begin(rubraview_box_t *box, const rubraview_tile_metrics_t *m, double px, double py) {
    if (!box || !m) return false;
    rubraview_rect_t grip = rubraview_box_grip_rect(box, m);
    if (!rubraview_rect_contains(grip, px, py)) return false;
    rubraview_rect_t body = rubraview_box_bounds(box, m);
    rubraview_tile_metrics_t sized = rubraview_box_metrics(box, m);
    box->grip_active = true;
    box->grip_x0 = px;
    box->grip_y0 = py;
    box->grip_scale0 = box->scale > 0.0 ? box->scale : 1.0;
    box->grip_columns0 = box->kind == RUBRAVIEW_BOX_TOOLBOX ? sized.strip_columns : sized.columns;
    /* Outward: the way the body grows. It hangs from the anchor's left
       edge, growing right, unless the window pushed it left, when it grows
       left; it hangs below the anchor or stands above it. */
    box->grip_sx = body.x >= box->anchor_x - 0.5 ? 1.0 : -1.0;
    box->grip_sy = body.y + body.height * 0.5 >= grip.y + grip.height * 0.5 ? 1.0 : -1.0;   /* body below: down */
    return true;
}

bool rubraview_box_grip_drag(rubraview_box_t *box, const rubraview_tile_metrics_t *m, double px, double py) {
    if (!box || !m || !box->grip_active) return false;
    bool toolbox = box->kind == RUBRAVIEW_BOX_TOOLBOX;
    double k0 = box->grip_scale0;
    double unit = (toolbox ? m->button_size + m->gutter / 2.0 : m->tile_size + m->gutter) * k0;
    int32_t columns = box->grip_columns0 + (int32_t)lround((px - box->grip_x0) * box->grip_sx / unit);
    /* A tile's own height dragged outward doubles the size's step: a quarter of the designed size. */
    double base = toolbox ? m->button_size : m->tile_size;
    double scale = k0 + (py - box->grip_y0) * box->grip_sy / (base * 4.0);
    scale = round(scale * 20.0) / 20.0;   /* 5 % steps */
    int32_t old_columns = box->columns;
    double old_scale = box->scale;
    rubraview_box_set_size(box, columns < 1 ? 1 : columns, scale);
    return box->columns != old_columns || box->scale != old_scale;
}

void rubraview_box_grip_end(rubraview_box_t *box) {
    if (box) box->grip_active = false;
}

rubraview_rect_t rubraview_box_timeline_rect(const rubraview_box_t *box, const rubraview_tile_metrics_t *m) {
    if (!box || !m || box->kind != RUBRAVIEW_BOX_TOOLBOX) return (rubraview_rect_t){0};
    rubraview_tile_metrics_t sized = rubraview_box_metrics(box, m);
    return body_relative(box, &sized, box_layout(box, &sized).timeline);
}

rubraview_rect_t rubraview_box_title_rect(const rubraview_box_t *box, const rubraview_tile_metrics_t *m) {
    if (!box || !m || box->kind != RUBRAVIEW_BOX_TOOLBOX) return (rubraview_rect_t){0};
    rubraview_tile_metrics_t sized = rubraview_box_metrics(box, m);
    return body_relative(box, &sized, box_layout(box, &sized).title);
}

bool rubraview_box_pin_shown_on(const rubraview_box_t *box) {
    return box && (box->pinned || box->state == RUBRAVIEW_BOX_LOCKED_OPEN || box->state == RUBRAVIEW_BOX_DETACHED);
}

bool rubraview_box_pin_click(rubraview_box_t *box, const rubraview_tile_metrics_t *m, double px, double py) {
    if (!box || !m || box->state == RUBRAVIEW_BOX_DETACHED) return false;
    if (!rubraview_rect_contains(rubraview_box_pin_rect(box, m), px, py)) return false;
    if (rubraview_box_pin_shown_on(box)) {
        box->pinned = false;
        box->state = RUBRAVIEW_BOX_EXPANDED;   /* folds once the pointer leaves */
    } else {
        box->pinned = true;
        box->state = RUBRAVIEW_BOX_LOCKED_OPEN;
    }
    box->idle_seconds = 0.0;
    return true;
}

/* ---- the seek buttons' choices (owner, 2026-10-09) ---- */

static const double SEEKFAN_SECONDS[RUBRAVIEW_SEEKFAN_COUNT] = { 5.0, 10.0, 30.0, 60.0, 300.0 };
static const char *const SEEKFAN_LABELS[RUBRAVIEW_SEEKFAN_COUNT] = { "5s", "10s", "30s", "1m", "5m" };

double rubraview_seekfan_seconds(int32_t i) {
    return i >= 0 && i < RUBRAVIEW_SEEKFAN_COUNT ? SEEKFAN_SECONDS[i] : 0.0;
}

const char *rubraview_seekfan_label(int32_t i) {
    return i >= 0 && i < RUBRAVIEW_SEEKFAN_COUNT ? SEEKFAN_LABELS[i] : "";
}

void rubraview_seekfan_press(rubraview_seekfan_t *fan, bool forward, double now, rubraview_rect_t tile) {
    if (!fan) return;
    *fan = (rubraview_seekfan_t){ .down = true, .open = false, .forward = forward,
                                  .pressed_at = now, .tile = tile, .hover = -1 };
}

bool rubraview_seekfan_tick(rubraview_seekfan_t *fan, double now) {
    if (!fan || !fan->down || fan->open) return false;
    if (now - fan->pressed_at < RUBRAVIEW_SEEKFAN_HOLD_SECONDS) return false;
    fan->open = true;
    fan->hover = -1;
    return true;
}

rubraview_rect_t rubraview_seekfan_button(const rubraview_seekfan_t *fan, int32_t i, double gap,
                                          double bounds_width, double bounds_height) {
    rubraview_rect_t none = { 0.0, 0.0, 0.0, 0.0 };
    if (!fan || i < 0 || i >= RUBRAVIEW_SEEKFAN_COUNT) return none;
    double w = fan->tile.width, h = fan->tile.height;
    double row = w * RUBRAVIEW_SEEKFAN_COUNT + gap * (RUBRAVIEW_SEEKFAN_COUNT - 1);
    double x = fan->tile.x + w * 0.5 - row * 0.5;
    if (x + row > bounds_width) x = bounds_width - row;
    if (x < 0.0) x = 0.0;
    double y = fan->tile.y - gap - h;
    if (y < 0.0) y = fan->tile.y + h + gap;                 /* no room over it: under it */
    if (y + h > bounds_height) y = bounds_height - h;       /* nor under: as low as it goes */
    if (y < 0.0) y = 0.0;
    /* Back is mirrored: the smallest step on the right, next to nothing further. */
    int32_t place = fan->forward ? i : RUBRAVIEW_SEEKFAN_COUNT - 1 - i;
    return (rubraview_rect_t){ x + (double)place * (w + gap), y, w, h };
}

void rubraview_seekfan_pointer(rubraview_seekfan_t *fan, double x, double y, double gap,
                               double bounds_width, double bounds_height) {
    if (!fan || !fan->open) return;
    fan->hover = -1;
    for (int32_t i = 0; i < RUBRAVIEW_SEEKFAN_COUNT; ++i) {
        if (rubraview_rect_contains(rubraview_seekfan_button(fan, i, gap, bounds_width, bounds_height), x, y)) fan->hover = i;
    }
}

double rubraview_seekfan_release(rubraview_seekfan_t *fan) {
    if (!fan || !fan->down) return 0.0;
    double seconds = !fan->open ? SEEKFAN_SECONDS[0] : rubraview_seekfan_seconds(fan->hover);
    double sign = fan->forward ? 1.0 : -1.0;
    *fan = (rubraview_seekfan_t){ .hover = -1 };
    return seconds * sign;
}

static bool hint_in(rubraview_rect_t r, double x, double y) {
    return r.width > 0.0 && x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
}

proven_u8str_view_t rubraview_box_hint(const rubraview_box_t *box, const rubraview_tile_metrics_t *m, double px, double py) {
    static const char *const NONE = "";
    const char *text = NONE;
    if (!box || !m || box->state == RUBRAVIEW_BOX_DETACHED) return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)NONE, .size = 0 };
    bool menu = box->kind == RUBRAVIEW_BOX_MENU;
    /* the anchor's words, folded or open: the open grid sits beside the anchor, not over it
       (owner, 2026-10-15: both halves say what they do) */
    rubraview_anchor_half_t half = rubraview_box_anchor_half_at(box, m, px, py);
    if (half == RUBRAVIEW_ANCHOR_CLICK) {
        text = menu ? T("Click: open the menu and keep it open (again: close). Drag: move it")
                    : T("Floating Widget - Click: open or fold its toolbar. Drag: move it; past the window's edge it "
                      "becomes a window of its own. Wheel: the file before or after; Alt+wheel: see-through");
    } else if (half == RUBRAVIEW_ANCHOR_HOVER) {
        text = menu ? T("Rest the pointer here: the menu opens, and folds when the pointer leaves")
                    : T("Toolbar - Rest the pointer here: the toolbar opens, and folds when the pointer leaves "
                      "(the pin keeps it open). Drag up or down: the files; sideways: the folders or archives. Wheel: button size");
    } else if (hint_in(rubraview_box_pin_rect(box, m), px, py)) {
        text = T("Pin: on, the toolbar stays open; off, it folds when the pointer leaves");
    } else if (hint_in(rubraview_box_grip_rect(box, m), px, py)) {
        text = T("Resize: drag sideways for more or fewer buttons a row, away or back for bigger or smaller. Wheel: the Floating Widget's size");
    } else if (!menu && hint_in(rubraview_box_timeline_rect(box, m), px, py)) {
        text = T("Click or drag: go to that moment (or page)");
    }
    size_t n = 0;
    while (text[n]) n++;
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)text, .size = n };
}
