#include "rubraview/ui_ctxmenu.h"

/* How many group changes come before row `index` (all of them for `count`). */
static size_t gaps_before(const int32_t *groups, size_t index) {
    size_t gaps = 0;
    for (size_t i = 1; i <= index; ++i) {
        if (groups[i] != groups[i - 1]) gaps++;
    }
    return gaps;
}

rubraview_rect_t rubraview_ctxmenu_frame(double window_width, double window_height, double x, double y,
                                         const rubraview_ctxmenu_metrics_t *m, const int32_t *groups, size_t count) {
    if (!m || !groups || count == 0) return (rubraview_rect_t){ x, y, 0.0, 0.0 };
    double height = m->pad * 2.0 + m->row_height * (double)count + m->gap * (double)gaps_before(groups, count - 1);
    double width = m->width;
    double left = x, top = y;
    if (left + width > window_width) left = x - width;         /* to the left of the point */
    if (top + height > window_height) top = y - height;        /* above it */
    if (left + width > window_width) left = window_width - width;
    if (top + height > window_height) top = window_height - height;
    if (left < 0.0) left = 0.0;
    if (top < 0.0) top = 0.0;
    return (rubraview_rect_t){ left, top, width, height };
}

rubraview_rect_t rubraview_ctxmenu_row(rubraview_rect_t frame, const rubraview_ctxmenu_metrics_t *m,
                                       const int32_t *groups, size_t index) {
    double top = frame.y + m->pad + m->row_height * (double)index + m->gap * (double)gaps_before(groups, index);
    return (rubraview_rect_t){ frame.x, top, frame.width, m->row_height };
}

int32_t rubraview_ctxmenu_row_at(rubraview_rect_t frame, const rubraview_ctxmenu_metrics_t *m,
                                 const int32_t *groups, size_t count, double x, double y) {
    if (!m || !groups || x < frame.x || x >= frame.x + frame.width) return -1;
    for (size_t i = 0; i < count; ++i) {
        rubraview_rect_t row = rubraview_ctxmenu_row(frame, m, groups, i);
        if (y >= row.y && y < row.y + row.height) return (int32_t)i;
    }
    return -1;
}

int32_t rubraview_ctxmenu_step(const bool *enabled, size_t count, int32_t from, int direction) {
    if (!enabled || count == 0) return -1;
    int32_t n = (int32_t)count;
    int32_t at = from;
    if (at < 0 || at >= n) at = direction > 0 ? -1 : n;   /* none chosen: from the end it starts at */
    for (int32_t k = 0; k < n; ++k) {
        at += direction > 0 ? 1 : -1;
        if (at >= n) at = 0;
        if (at < 0) at = n - 1;
        if (enabled[at]) return at;
    }
    return -1;
}
