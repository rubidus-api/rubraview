#include "rubraview/ui_chooser.h"

rubraview_chooser_layout_t rubraview_chooser_place(double win_w, double win_h, size_t rows, double row_h, double width,
                                                   double at_x, double at_y, double above_y, size_t current) {
    rubraview_chooser_layout_t l = { .row_h = row_h > 1.0 ? row_h : 1.0, .rows = rows };
    if (rows == 0 || win_w <= 0.0 || win_h <= 0.0) return l;
    size_t fit = (size_t)(win_h / l.row_h);
    l.shown = fit > 1 ? fit - 1 : 1;   /* the title row takes one */
    if (l.shown > rows) l.shown = rows;
    double h = (double)(l.shown + 1) * l.row_h;
    double w = width < win_w ? width : win_w;
    double x = at_x, y = at_y;
    if (y + h > win_h) y = above_y - h;   /* no room under it: over it */
    if (y + h > win_h) y = win_h - h;
    if (y < 0.0) y = 0.0;
    if (x + w > win_w) x = win_w - w;
    if (x < 0.0) x = 0.0;
    l.frame = (rubraview_rect_t){ x, y, w, h };
    if (current >= l.shown && current < rows) l.first = current + 1 - l.shown;
    return l;
}

rubraview_rect_t rubraview_chooser_row_rect(const rubraview_chooser_layout_t *l, size_t index) {
    if (!l || index < l->first || index >= l->first + l->shown || index >= l->rows) return (rubraview_rect_t){ 0 };
    return (rubraview_rect_t){ l->frame.x, l->frame.y + (double)(index - l->first + 1) * l->row_h, l->frame.width, l->row_h };
}

int32_t rubraview_chooser_row_at(const rubraview_chooser_layout_t *l, double x, double y) {
    if (!l || l->rows == 0 || !rubraview_rect_contains(l->frame, x, y)) return -2;
    size_t r = (size_t)((y - l->frame.y) / l->row_h);
    if (r == 0) return -1;
    size_t index = l->first + r - 1;
    return index < l->rows && r <= l->shown ? (int32_t)index : -2;
}

void rubraview_chooser_scroll(rubraview_chooser_layout_t *l, int32_t rows) {
    if (!l || l->rows <= l->shown) return;
    int64_t first = (int64_t)l->first + rows, most = (int64_t)(l->rows - l->shown);
    l->first = (size_t)(first < 0 ? 0 : first > most ? most : first);
}

int32_t rubraview_chooser_step(const rubraview_chooser_layout_t *l, int32_t at, int32_t direction) {
    if (!l || l->rows == 0) return -1;
    int32_t n = (int32_t)l->rows;
    if (at < 0 || at >= n) return direction < 0 ? n - 1 : 0;
    return ((at + (direction < 0 ? -1 : 1)) % n + n) % n;
}
