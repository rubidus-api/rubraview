#include "rubraview/ui_navbox.h"
#include "rubraview/lang.h"

/* RV-277: the navigator's geometry (ui_navbox.h). */

rubraview_rect_t rubraview_navbox_middle(double view_w, double view_h) {
    double w = view_w * 0.25, h = view_h * 0.25;
    return (rubraview_rect_t){ (view_w - w) * 0.5, (view_h - h) * 0.5, w, h };
}

static double clampd(double v, double lo, double hi) {
    if (hi < lo) return lo;
    return v < lo ? lo : v > hi ? hi : v;
}

rubraview_navbox_t rubraview_navbox_place(double px, double py, double view_w, double view_h, double dpi_scale) {
    double s = dpi_scale > 0.0 ? dpi_scale : 1.0;
    rubraview_navbox_t nav = { .pad = 10.0 * s, .button = 44.0 * s, .button_h = 36.0 * s, .bar_h = 24.0 * s,
                               .gap = 14.0 * s, .label_h = 18.0 * s };
    double w = 2.0 * nav.pad + 8.0 * nav.button + 4.0 * 2.0 * s + 3.0 * nav.gap;   /* four pairs, 2 px inside a pair */
    double h = 2.0 * nav.pad + nav.bar_h + 8.0 * s + nav.button_h + nav.label_h;
    double x = clampd(px - w * 0.5, 0.0, view_w - w);
    double y = clampd(py - h * 0.5, 0.0, view_h - h);
    nav.box = (rubraview_rect_t){ x, y, w, h };
    return nav;
}

rubraview_rect_t rubraview_navbox_part_rect(const rubraview_navbox_t *nav, rubraview_navbox_part_t part) {
    rubraview_rect_t none = { 0.0, 0.0, 0.0, 0.0 };
    if (!nav) return none;
    const rubraview_rect_t b = nav->box;
    double inner = nav->pad * 0.2;   /* 2 px at 100 % (pad 10) */
    if (part == RUBRAVIEW_NAVBOX_CLOSE)
        return (rubraview_rect_t){ b.x + b.width - nav->pad - nav->bar_h, b.y + nav->pad, nav->bar_h, nav->bar_h };
    if (part == RUBRAVIEW_NAVBOX_BAR)
        return (rubraview_rect_t){ b.x + nav->pad, b.y + nav->pad, b.width - 3.0 * nav->pad - nav->bar_h, nav->bar_h };
    if (part < RUBRAVIEW_NAVBOX_PAGE_PREV || part > RUBRAVIEW_NAVBOX_BOOK_NEXT) return none;
    int32_t i = (int32_t)part - (int32_t)RUBRAVIEW_NAVBOX_PAGE_PREV;   /* 0..7 */
    int32_t pair = i / 2, side = i % 2;
    double x = b.x + nav->pad + (double)pair * (2.0 * nav->button + 2.0 * inner + nav->gap) + (double)side * (nav->button + 2.0 * inner);
    double y = b.y + nav->pad + nav->bar_h + nav->pad * 0.8;
    return (rubraview_rect_t){ x, y, nav->button, nav->button_h };
}

rubraview_navbox_part_t rubraview_navbox_hit(const rubraview_navbox_t *nav, double px, double py) {
    if (!nav || !rubraview_rect_contains(nav->box, px, py)) return RUBRAVIEW_NAVBOX_NONE;
    for (int32_t p = 0; p < RUBRAVIEW_NAVBOX_PARTS; ++p) {
        rubraview_rect_t r = rubraview_navbox_part_rect(nav, (rubraview_navbox_part_t)p);
        if (p == RUBRAVIEW_NAVBOX_BAR) { r.y -= nav->bar_h * 0.25; r.height += nav->bar_h * 0.5; }   /* easy to catch */
        if (rubraview_rect_contains(r, px, py)) return (rubraview_navbox_part_t)p;
    }
    return RUBRAVIEW_NAVBOX_NONE;
}

const char *rubraview_navbox_action(rubraview_navbox_part_t part) {
    switch (part) {
        case RUBRAVIEW_NAVBOX_PAGE_PREV:    return "prev_page";
        case RUBRAVIEW_NAVBOX_PAGE_NEXT:    return "next_page";
        case RUBRAVIEW_NAVBOX_PICTURE_PREV: return "page_over_back";   /* one picture, also in two pages (RV-224) */
        case RUBRAVIEW_NAVBOX_PICTURE_NEXT: return "page_over_next";
        case RUBRAVIEW_NAVBOX_FIRST:        return "first_page";
        case RUBRAVIEW_NAVBOX_LAST:         return "last_page";
        case RUBRAVIEW_NAVBOX_BOOK_PREV:    return "prev_archive";
        case RUBRAVIEW_NAVBOX_BOOK_NEXT:    return "next_archive";
        case RUBRAVIEW_NAVBOX_CLOSE:        return "quit";   /* owner: "X버튼도 넣어서 바로 종료할 수 있게" */
        default:                            return "";
    }
}

const char *rubraview_navbox_pair_caption(int32_t pair) {
    static const char *const WORDS[4] = { N_("Page"), N_("Picture"), N_("First / last"), N_("Archive / folder") };
    return pair >= 0 && pair < 4 ? WORDS[pair] : "";
}

bool rubraview_navbox_keep(bool shown, bool allowed, bool in_middle, bool in_box, double rested, double away) {
    if (!allowed) return false;
    if (shown) return in_box || in_middle || away < RUBRAVIEW_NAVBOX_LINGER;
    return in_middle && rested >= RUBRAVIEW_NAVBOX_REST;
}
