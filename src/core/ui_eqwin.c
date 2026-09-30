#include "rubraview/ui_eqwin.h"
#include <math.h>

rubraview_eqwin_t rubraview_eqwin_create(double dpi_scale) {
    if (dpi_scale <= 0.0) dpi_scale = 1.0;
    return (rubraview_eqwin_t){
        .title_height = 30.0 * dpi_scale,
        .preset_height = 30.0 * dpi_scale,
        .label_height = 20.0 * dpi_scale,
        .width = 560.0 * dpi_scale,
        .height = 300.0 * dpi_scale,
        .sliding = -1,
    };
}

static double clampd(double v, double lo, double hi) {
    if (v > hi) v = hi;
    if (v < lo) v = lo;
    return v;
}

void rubraview_eqwin_place(rubraview_eqwin_t *w, double view_w, double view_h) {
    if (!w || view_w <= 0.0 || view_h <= 0.0) return;
    if (!w->placed) {
        w->x = (view_w - w->width) * 0.5;
        w->y = view_h - w->height - view_h * 0.12;
        w->placed = true;
    }
    w->x = clampd(w->x, 0.0, view_w > w->width ? view_w - w->width : 0.0);
    w->y = clampd(w->y, 0.0, view_h > w->height ? view_h - w->height : 0.0);
}

rubraview_rect_t rubraview_eqwin_rect(const rubraview_eqwin_t *w) {
    if (!w) return (rubraview_rect_t){0};
    return (rubraview_rect_t){ w->x, w->y, w->width, w->height };
}

rubraview_rect_t rubraview_eqwin_close_rect(const rubraview_eqwin_t *w) {
    if (!w) return (rubraview_rect_t){0};
    double s = w->title_height;
    return (rubraview_rect_t){ w->x + w->width - s, w->y, s, s };
}

static double pad(const rubraview_eqwin_t *w) { return w->label_height * 0.5; }

rubraview_rect_t rubraview_eqwin_preset_rect(const rubraview_eqwin_t *w, size_t preset) {
    if (!w || preset >= RUBRAVIEW_EQWIN_PRESETS) return (rubraview_rect_t){0};
    double inner = w->width - 2.0 * pad(w);
    double each = inner / (double)RUBRAVIEW_EQWIN_PRESETS;
    return (rubraview_rect_t){ w->x + pad(w) + (double)preset * each + 1.0, w->y + w->title_height,
                               each - 2.0, w->preset_height - 4.0 };
}

rubraview_rect_t rubraview_eqwin_band_rect(const rubraview_eqwin_t *w, size_t band) {
    if (!w || band >= RUBRAVIEW_EQ_BANDS) return (rubraview_rect_t){0};
    double inner = w->width - 2.0 * pad(w);
    double each = inner / (double)RUBRAVIEW_EQ_BANDS;
    double top = w->y + w->title_height + w->preset_height;
    return (rubraview_rect_t){ w->x + pad(w) + (double)band * each, top, each, w->y + w->height - pad(w) - top };
}

rubraview_rect_t rubraview_eqwin_track_rect(const rubraview_eqwin_t *w, size_t band) {
    rubraview_rect_t col = rubraview_eqwin_band_rect(w, band);
    if (col.width <= 0.0) return col;
    double h = col.height - 2.0 * w->label_height;
    return (rubraview_rect_t){ col.x, col.y + w->label_height, col.width, h > 0.0 ? h : 0.0 };
}

rubraview_eqwin_part_t rubraview_eqwin_hit(const rubraview_eqwin_t *w, double px, double py, size_t *out_index) {
    if (!w || !w->open || !rubraview_rect_contains(rubraview_eqwin_rect(w), px, py)) return RUBRAVIEW_EQWIN_NONE;
    if (rubraview_rect_contains(rubraview_eqwin_close_rect(w), px, py)) return RUBRAVIEW_EQWIN_CLOSE;
    if (py < w->y + w->title_height) return RUBRAVIEW_EQWIN_TITLE;
    for (size_t i = 0; i < RUBRAVIEW_EQWIN_PRESETS; ++i) {
        if (rubraview_rect_contains(rubraview_eqwin_preset_rect(w, i), px, py)) {
            if (out_index) *out_index = i;
            return RUBRAVIEW_EQWIN_PRESET;
        }
    }
    for (size_t i = 0; i < RUBRAVIEW_EQ_BANDS; ++i) {
        if (rubraview_rect_contains(rubraview_eqwin_band_rect(w, i), px, py)) {
            if (out_index) *out_index = i;
            return RUBRAVIEW_EQWIN_SLIDER;
        }
    }
    return RUBRAVIEW_EQWIN_BODY;
}

double rubraview_eqwin_step(double db, int steps) {
    double v = round(db * 2.0) / 2.0 + 0.5 * (double)steps;
    return clampd(v, -RUBRAVIEW_EQWIN_MAX_DB, RUBRAVIEW_EQWIN_MAX_DB);
}

double rubraview_eqwin_db_at(const rubraview_eqwin_t *w, size_t band, double py) {
    rubraview_rect_t t = rubraview_eqwin_track_rect(w, band);
    if (t.height <= 0.0) return 0.0;
    double f = clampd((py - t.y) / t.height, 0.0, 1.0);            /* 0 at the top: +12 */
    double db = RUBRAVIEW_EQWIN_MAX_DB - f * 2.0 * RUBRAVIEW_EQWIN_MAX_DB;
    return rubraview_eqwin_step(db, 0);
}

double rubraview_eqwin_y_of(const rubraview_eqwin_t *w, size_t band, double db) {
    rubraview_rect_t t = rubraview_eqwin_track_rect(w, band);
    db = clampd(db, -RUBRAVIEW_EQWIN_MAX_DB, RUBRAVIEW_EQWIN_MAX_DB);
    return t.y + (RUBRAVIEW_EQWIN_MAX_DB - db) / (2.0 * RUBRAVIEW_EQWIN_MAX_DB) * t.height;
}

void rubraview_eqwin_drag_begin(rubraview_eqwin_t *w, double px, double py) {
    if (!w) return;
    w->dragging = true;
    w->grab_dx = px - w->x;
    w->grab_dy = py - w->y;
}

void rubraview_eqwin_drag_to(rubraview_eqwin_t *w, double px, double py, double view_w, double view_h) {
    if (!w || !w->dragging) return;
    w->x = px - w->grab_dx;
    w->y = py - w->grab_dy;
    rubraview_eqwin_place(w, view_w, view_h);
}
