#include "rubraview/ui_ctxmenu.h"

#include <math.h>

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

/* ---- RV-287: the ☰ laid as inline buttons ---- */

rubraview_rect_t rubraview_flowmenu_layout(double x, double y, double max_width, const rubraview_flowmenu_metrics_t *m,
                                           const double *widths, const int32_t *groups, size_t count, rubraview_rect_t *out_cells) {
    /* (x, y) as given, above the window too: a scrolled menu is laid out from above its top */
    if (!m || !widths || !groups || count == 0) return (rubraview_rect_t){ x, y, 0.0, 0.0 };
    double inner = max_width - m->pad * 2.0;
    if (inner < 1.0) inner = 1.0;
    double cx = 0.0, widest = 0.0;
    size_t line = 0;
    for (size_t i = 0; i < count; ++i) {
        double w = widths[i] < 0.0 ? 0.0 : (widths[i] > inner ? inner : widths[i]);
        bool starts = i == 0 || groups[i] != groups[i - 1];
        double space = cx > 0.0 ? (starts ? m->group_gap : m->gap) : 0.0;
        if (starts && cx > 0.0) {   /* the whole group on the next line when it does not fit here but fits there */
            double gw = 0.0;
            for (size_t k = i; k < count && groups[k] == groups[i]; ++k)
                gw += (k > i ? m->gap : 0.0) + (widths[k] > inner ? inner : (widths[k] < 0.0 ? 0.0 : widths[k]));
            if (cx + space + gw > inner && gw <= inner) { line++; cx = 0.0; space = 0.0; }
        }
        if (cx > 0.0 && cx + space + w > inner) { line++; cx = 0.0; space = 0.0; }
        double left = cx + space;
        if (out_cells) out_cells[i] = (rubraview_rect_t){ x + m->pad + left, y + m->pad + (m->cell_height + m->gap) * (double)line, w, m->cell_height };
        cx = left + w;
        if (cx > widest) widest = cx;
    }
    double height = m->pad * 2.0 + m->cell_height * (double)(line + 1) + m->gap * (double)line;
    return (rubraview_rect_t){ x, y, widest + m->pad * 2.0, height };
}

int32_t rubraview_flowmenu_at(const rubraview_rect_t *cells, size_t count, double x, double y) {
    if (!cells) return -1;
    for (size_t i = 0; i < count; ++i) {
        rubraview_rect_t r = cells[i];
        if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) return (int32_t)i;
    }
    return -1;
}

int32_t rubraview_flowmenu_step_vertical(const rubraview_rect_t *cells, const bool *enabled, size_t count, int32_t from, int direction) {
    if (!cells || !enabled || count == 0) return -1;
    if (from < 0 || (size_t)from >= count) {
        for (size_t i = 0; i < count; ++i) if (enabled[i]) return (int32_t)i;
        return -1;
    }
    rubraview_rect_t at = cells[from];
    double mid = at.x + at.width * 0.5, sign = direction > 0 ? 1.0 : -1.0;
    double line_d = -1.0;   /* the nearest line that way with a button that can act */
    for (size_t i = 0; i < count; ++i) {
        double d = (cells[i].y - at.y) * sign;
        if (enabled[i] && d > 0.0 && (line_d < 0.0 || d < line_d)) line_d = d;
    }
    if (line_d < 0.0) return from;
    int32_t best = from;
    double best_d = 0.0;
    for (size_t i = 0; i < count; ++i) {
        if (!enabled[i] || (cells[i].y - at.y) * sign != line_d) continue;
        double d = fabs(cells[i].x + cells[i].width * 0.5 - mid);
        if (best == from || d < best_d) { best = (int32_t)i; best_d = d; }
    }
    return best;
}

double rubraview_flowmenu_scroll_clamp(double scroll, double content, double view, double pitch) {
    if (pitch <= 0.0 || content <= view) return 0.0;
    double most = ceil((content - view) / pitch - 1e-9) * pitch;
    double s = round(scroll / pitch) * pitch;
    return s < 0.0 ? 0.0 : (s > most ? most : s);
}

double rubraview_flowmenu_scroll_to(double scroll, double top, double content, double view, double pitch, double pad) {
    double s = rubraview_flowmenu_scroll_clamp(scroll, content, view, pitch);
    if (pitch <= 0.0) return s;
    double line_top = top - pad;   /* the line's place among whole lines */
    if (line_top < s) s = line_top;
    else if (line_top + pitch > s + (view - pad * 2.0) + 1e-9) s = line_top + pitch - (view - pad * 2.0);
    return rubraview_flowmenu_scroll_clamp(ceil(s / pitch - 1e-9) * pitch, content, view, pitch);
}
