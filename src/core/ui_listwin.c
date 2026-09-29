#include "rubraview/ui_listwin.h"

rubraview_listwin_t rubraview_listwin_create(double dpi_scale) {
    if (dpi_scale <= 0.0) dpi_scale = 1.0;
    return (rubraview_listwin_t){
        .title_height = 30.0 * dpi_scale,
        .row_height = 24.0 * dpi_scale,
        .width = 360.0 * dpi_scale,
        .height = 480.0 * dpi_scale,
    };
}

static double clampd(double v, double lo, double hi) {
    if (v > hi) v = hi;
    if (v < lo) v = lo;
    return v;
}

void rubraview_listwin_place(rubraview_listwin_t *lw, double view_w, double view_h) {
    if (!lw || view_w <= 0.0 || view_h <= 0.0) return;
    double min_w = lw->row_height * 8.0, min_h = lw->title_height + lw->row_height * 3.0;
    if (!lw->placed) {
        lw->width = clampd(view_w / 3.0, min_w, lw->row_height * 24.0);
        lw->height = view_h * 0.75;
        lw->x = view_w - lw->width - lw->row_height;
        lw->y = view_h * 0.1;
        lw->placed = true;
    }
    lw->width = clampd(lw->width, min_w < view_w ? min_w : view_w, view_w);
    lw->height = clampd(lw->height, min_h < view_h ? min_h : view_h, view_h);
    lw->x = clampd(lw->x, 0.0, view_w - lw->width);
    lw->y = clampd(lw->y, 0.0, view_h - lw->height);
}

rubraview_rect_t rubraview_listwin_rect(const rubraview_listwin_t *lw) {
    if (!lw) return (rubraview_rect_t){0};
    return (rubraview_rect_t){ lw->x, lw->y, lw->width, lw->height };
}

rubraview_rect_t rubraview_listwin_close_rect(const rubraview_listwin_t *lw) {
    if (!lw) return (rubraview_rect_t){0};
    double s = lw->title_height;
    return (rubraview_rect_t){ lw->x + lw->width - s, lw->y, s, s };
}

size_t rubraview_listwin_rows_visible(const rubraview_listwin_t *lw) {
    if (!lw || lw->row_height <= 0.0 || lw->height <= lw->title_height) return 0;
    return (size_t)((lw->height - lw->title_height) / lw->row_height);
}

rubraview_rect_t rubraview_listwin_row_rect(const rubraview_listwin_t *lw, size_t index, size_t count) {
    size_t rows = rubraview_listwin_rows_visible(lw);
    if (!lw || index >= count || index < lw->first || index - lw->first >= rows) return (rubraview_rect_t){0};
    return (rubraview_rect_t){ lw->x, lw->y + lw->title_height + (double)(index - lw->first) * lw->row_height,
                               lw->width, lw->row_height };
}

rubraview_listwin_part_t rubraview_listwin_hit(const rubraview_listwin_t *lw, double px, double py, size_t count,
                                               size_t *out_index) {
    if (!lw || !lw->open || !rubraview_rect_contains(rubraview_listwin_rect(lw), px, py)) return RUBRAVIEW_LISTWIN_NONE;
    if (rubraview_rect_contains(rubraview_listwin_close_rect(lw), px, py)) return RUBRAVIEW_LISTWIN_CLOSE;
    if (py < lw->y + lw->title_height) return RUBRAVIEW_LISTWIN_TITLE;
    size_t row = (size_t)((py - lw->y - lw->title_height) / lw->row_height);
    size_t index = lw->first + row;
    if (row < rubraview_listwin_rows_visible(lw) && index < count) {
        if (out_index) *out_index = index;
        return RUBRAVIEW_LISTWIN_ROW;
    }
    return RUBRAVIEW_LISTWIN_BODY;
}

static size_t last_first(const rubraview_listwin_t *lw, size_t count) {
    size_t rows = rubraview_listwin_rows_visible(lw);
    return count > rows ? count - rows : 0;
}

void rubraview_listwin_scroll(rubraview_listwin_t *lw, long rows, size_t count) {
    if (!lw) return;
    long first = (long)lw->first + rows;
    if (first < 0) first = 0;
    size_t f = (size_t)first, max = last_first(lw, count);
    lw->first = f > max ? max : f;
}

void rubraview_listwin_reveal(rubraview_listwin_t *lw, size_t index, size_t count) {
    if (!lw || index >= count) return;
    size_t rows = rubraview_listwin_rows_visible(lw);
    if (rows == 0) return;
    if (index < lw->first) lw->first = index;
    else if (index >= lw->first + rows) lw->first = index - rows + 1;
    size_t max = last_first(lw, count);
    if (lw->first > max) lw->first = max;
}

void rubraview_listwin_drag_begin(rubraview_listwin_t *lw, double px, double py) {
    if (!lw) return;
    lw->dragging = true;
    lw->grab_dx = px - lw->x;
    lw->grab_dy = py - lw->y;
}

void rubraview_listwin_drag_to(rubraview_listwin_t *lw, double px, double py, double view_w, double view_h) {
    if (!lw || !lw->dragging) return;
    lw->x = px - lw->grab_dx;
    lw->y = py - lw->grab_dy;
    rubraview_listwin_place(lw, view_w, view_h);
}
