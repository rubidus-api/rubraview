/*
 * RV-315 (owner, 2026-11-20: "창이 너무 작다면 세로로 스크롤바가 생겨서 아래로 스크롤이 되고, 그리고 버튼들은 wrap 되어 다음 행으로
 * 넘어가거나"; "다른 외부창들도 마찬가지"): a small window's page — what it holds scrolls when the window is shorter than it,
 * with a bar at the right drawn as the clash window's, and its buttons in rows that wrap (core rubraview_flow_inline). A window draws
 * its content at `y - page.scroll`, tells the page how tall it came out, and passes it its wheel and presses.
 */
#include "app.h"

double dlgpage_width(const dlgpage_t *p, double w, double dpi) {
    return p->bar ? w - DLGPAGE_BAR * dpi - 4.0 * dpi : w;
}

bool dlgpage_end(dlgpage_t *p, rubraview_renderer_t *r, double w, double h, double content, double px, double py, double dpi) {
    bool bar = content > h + 0.5;
    bool changed = bar != p->bar;   /* the width for the content changed: drawn again */
    p->bar = bar;
    double sbw = DLGPAGE_BAR * dpi;
    p->sb = (rubraview_scrollbar_t){ .area = { w - sbw - 2.0 * dpi, 2.0 * dpi, sbw, h - 4.0 * dpi }, .button = sbw, .total = content,
                                     .shown = h, .first = p->scroll, .step = 40.0 * dpi, .min_thumb = 24.0 * dpi };
    double kept = bar ? rubraview_scrollbar_clamp(&p->sb, p->scroll) : 0.0;
    if (kept != p->scroll) { p->scroll = kept; changed = true; }
    p->sb.first = p->scroll;
    if (bar && r) {
        rubraview_scrollbar_part_t over = rubraview_scrollbar_hit(&p->sb, px, py);
        rubraview_rect_t tr = rubraview_scrollbar_track(&p->sb), th = rubraview_scrollbar_thumb(&p->sb);
        double radius = sbw * 0.5;
        rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ tr.x, tr.y, tr.width, tr.height }, 0x28FFFFFFu, radius);
        uint32_t thumb = p->hold.part == RUBRAVIEW_SCROLLBAR_THUMB ? 0xF0B0B0B0u : over != RUBRAVIEW_SCROLLBAR_NONE ? 0xE0909090u : 0xB0808080u;
        rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ th.x + 2.0 * dpi, th.y + 1.0 * dpi, th.width - 4.0 * dpi, th.height - 2.0 * dpi }, thumb, radius - 2.0 * dpi);
        for (int i = 0; i < 2; ++i) {
            rubraview_scrollbar_part_t part = i == 0 ? RUBRAVIEW_SCROLLBAR_UP : RUBRAVIEW_SCROLLBAR_DOWN;
            rubraview_rect_t b = i == 0 ? rubraview_scrollbar_up(&p->sb) : rubraview_scrollbar_down(&p->sb);
            rubraview_pal_rect_t bx = { b.x, b.y + 1.0 * dpi, b.width, b.height - 1.0 * dpi };
            rubraview_pal_render_fill_rect(r, bx, p->hold.part == part ? 0x70FFFFFFu : over == part ? 0x40FFFFFFu : 0x20FFFFFFu, 2.0 * dpi);
            rubraview_pal_render_draw_text(r, i == 0 ? PROVEN_LIT("\xE2\x96\xB2") : PROVEN_LIT("\xE2\x96\xBC"), bx, b.width * 0.62, 0xE0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
        }
    }
    return changed;
}

bool dlgpage_wheel(dlgpage_t *p, double delta, double dpi) {
    if (!p->bar) return false;
    p->scroll = rubraview_scrollbar_clamp(&p->sb, p->scroll - delta * 48.0 * dpi);
    return true;
}

bool dlgpage_press(dlgpage_t *p, double x, double y) {
    if (!p->bar) return false;
    double first = rubraview_scrollbar_press(&p->sb, &p->hold, x, y, rubraview_pal_time_now_seconds());
    if (p->hold.part == RUBRAVIEW_SCROLLBAR_NONE) return false;
    p->scroll = first;
    return true;
}

bool dlgpage_held(dlgpage_t *p, double x, double y) {
    if (p->hold.part == RUBRAVIEW_SCROLLBAR_NONE) return false;
    p->scroll = rubraview_scrollbar_held(&p->sb, &p->hold, x, y, rubraview_pal_time_now_seconds());
    return true;
}

bool dlgpage_release(dlgpage_t *p) {
    bool was = p->hold.part != RUBRAVIEW_SCROLLBAR_NONE;
    p->hold.part = RUBRAVIEW_SCROLLBAR_NONE;
    return was;
}

double dlgpage_buttons(rubraview_renderer_t *r, const char *const *words, size_t n, double left, double right, double y, double size,
                       double pad_x, double min_w, double bh, double gap, rubraview_pal_rect_t *out) {
    double w[16] = { 0 }, x[16] = { 0 };
    uint32_t row[16] = { 0 };
    if (n > 16) n = 16;
    for (size_t i = 0; i < n; ++i) {
        double tw = 0.0;
        (void)rubraview_pal_render_measure_text(r, cstr(words[i]), size, &tw);
        w[i] = tw + 2.0 * pad_x;
        if (w[i] < min_w) w[i] = min_w;
        if (w[i] > right - left) w[i] = right - left;   /* never past the window */
    }
    size_t rows = rubraview_flow_inline(w, n, gap, right - left, row, x);
    for (size_t i = 0; i < n; ) {   /* each row against the right */
        size_t end = i;
        while (end < n && row[end] == row[i]) ++end;
        double shift = (right - left) - (x[end - 1] + w[end - 1]);
        for (size_t k = i; k < end; ++k) out[k] = (rubraview_pal_rect_t){ left + x[k] + (shift > 0.0 ? shift : 0.0), y + (double)row[k] * (bh + gap), w[k], bh };
        i = end;
    }
    return rows ? (double)rows * bh + (double)(rows - 1) * gap : 0.0;
}
