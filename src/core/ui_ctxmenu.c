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

/* ---- RV-205: the ☰ grid ---- */

/* Cell `index`'s line and column: each group begins a line. `*out_groups_before` counts the group changes up to it. */
static void grid_place(const rubraview_gridmenu_metrics_t *m, const int32_t *groups, size_t index,
                       size_t *out_line, size_t *out_column, size_t *out_gaps) {
    size_t columns = m->columns > 0 ? (size_t)m->columns : 1;
    size_t line = 0, in_group = 0, gaps = 0;
    for (size_t i = 1; i <= index; ++i) {
        if (groups[i] != groups[i - 1]) {
            line += in_group / columns + 1;
            in_group = 0;
            gaps++;
        } else {
            in_group++;
        }
    }
    *out_line = line + in_group / columns;
    *out_column = in_group % columns;
    *out_gaps = gaps;
}

rubraview_rect_t rubraview_gridmenu_frame(double x, double y, const rubraview_gridmenu_metrics_t *m,
                                          const int32_t *groups, size_t count) {
    if (!m || !groups || count == 0) return (rubraview_rect_t){ x, y, 0.0, 0.0 };
    size_t columns = m->columns > 0 ? (size_t)m->columns : 1, line = 0, column = 0, gaps = 0;
    grid_place(m, groups, count - 1, &line, &column, &gaps);
    double width = m->pad * 2.0 + m->cell_width * (double)columns + m->gap * (double)(columns - 1);
    double height = m->pad * 2.0 + m->cell_height * (double)(line + 1) + m->gap * (double)gaps;
    return (rubraview_rect_t){ x < 0.0 ? 0.0 : x, y < 0.0 ? 0.0 : y, width, height };
}

rubraview_rect_t rubraview_gridmenu_cell(rubraview_rect_t frame, const rubraview_gridmenu_metrics_t *m,
                                         const int32_t *groups, size_t index) {
    if (!m || !groups) return (rubraview_rect_t){0};
    size_t line = 0, column = 0, gaps = 0;
    grid_place(m, groups, index, &line, &column, &gaps);
    return (rubraview_rect_t){ frame.x + m->pad + (m->cell_width + m->gap) * (double)column,
                               frame.y + m->pad + m->cell_height * (double)line + m->gap * (double)gaps, m->cell_width, m->cell_height };
}

int32_t rubraview_gridmenu_at(rubraview_rect_t frame, const rubraview_gridmenu_metrics_t *m,
                              const int32_t *groups, size_t count, double x, double y) {
    if (!m || !groups) return -1;
    for (size_t i = 0; i < count; ++i) {
        rubraview_rect_t r = rubraview_gridmenu_cell(frame, m, groups, i);
        if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) return (int32_t)i;
    }
    return -1;
}

int32_t rubraview_gridmenu_step_vertical(rubraview_rect_t frame, const rubraview_gridmenu_metrics_t *m,
                                         const int32_t *groups, const bool *enabled, size_t count, int32_t from, int direction) {
    if (!m || !groups || !enabled || count == 0) return -1;
    if (from < 0 || (size_t)from >= count) {
        for (size_t i = 0; i < count; ++i) if (enabled[i]) return (int32_t)i;
        return -1;
    }
    rubraview_rect_t at = rubraview_gridmenu_cell(frame, m, groups, (size_t)from);
    int32_t best = from;
    double best_d = 0.0;
    for (size_t i = 0; i < count; ++i) {
        if (!enabled[i] || (int32_t)i == from) continue;
        rubraview_rect_t r = rubraview_gridmenu_cell(frame, m, groups, i);
        if (r.x != at.x) continue;
        double d = (r.y - at.y) * (direction > 0 ? 1.0 : -1.0);
        if (d <= 0.0) continue;
        if (best == from || d < best_d) { best = (int32_t)i; best_d = d; }
    }
    return best;
}
