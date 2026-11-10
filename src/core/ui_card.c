#include "rubraview/ui_card.h"

/* RV-265, R296: the input card's geometry and focus; see the header. */

static double card_max(double a, double b) { return a > b ? a : b; }
static double card_min(double a, double b) { return a < b ? a : b; }

/* Kept inside the host, at least its floor unless the host is smaller. */
static void card_clamp(rubraview_card_t *card, double host_w, double host_h) {
    double m = card->margin;
    double room_w = card_max(1.0, host_w - 2.0 * m), room_h = card_max(1.0, host_h - 2.0 * m);
    rubraview_rect_t *r = &card->rect;
    r->width = card_min(card_max(r->width, card->min_width), room_w);
    r->height = card_min(card_max(r->height, card->min_height), room_h);
    if (r->x + r->width > host_w - m) r->x = host_w - m - r->width;
    if (r->y + r->height > host_h - m) r->y = host_h - m - r->height;
    if (r->x < m) r->x = m;
    if (r->y < m) r->y = m;
}

void rubraview_card_open(rubraview_card_t *card, double host_w, double host_h, double want_w, double want_h, double rise,
                         const rubraview_card_share_t *kept) {
    card->open = true;
    card->focused = true;
    card->drag = 0;
    card->sized = false;
    card->min_width = want_w;
    card->min_height = want_h;
    rubraview_rect_t r = { 0.0, 0.0, want_w, want_h };
    if (kept && rubraview_card_share_valid(kept)) {
        if (kept->sized) {
            r.width = card_max(want_w, kept->width * host_w);
            r.height = card_max(want_h, kept->height * host_h);
            card->sized = true;
        }
        r.x = kept->left * host_w;
        r.y = kept->top * host_h;
    } else {
        if (rise < 0.0) rise = 0.0;
        if (rise > 1.0) rise = 1.0;
        r.x = (host_w - r.width) * 0.5;
        r.y = (host_h - r.height) * rise;
    }
    card->rect = r;
    card_clamp(card, host_w, host_h);
}

void rubraview_card_close(rubraview_card_t *card) {
    card->open = false;
    card->focused = false;
    card->drag = 0;
}

void rubraview_card_fit(rubraview_card_t *card, double host_w, double host_h, double min_w, double min_h) {
    if (!card->open) return;
    card->min_width = min_w;
    card->min_height = min_h;
    card_clamp(card, host_w, host_h);
}

int32_t rubraview_card_hit(const rubraview_card_t *card, double x, double y) {
    if (!card->open || !rubraview_rect_contains(card->rect, x, y)) return RUBRAVIEW_CARD_OUTSIDE;
    const rubraview_rect_t r = card->rect;
    int32_t edges = 0;
    if (x < r.x + card->edge) edges |= RUBRAVIEW_CARD_LEFT;
    else if (x >= r.x + r.width - card->edge) edges |= RUBRAVIEW_CARD_RIGHT;
    if (y < r.y + card->edge) edges |= RUBRAVIEW_CARD_TOP;
    else if (y >= r.y + r.height - card->edge) edges |= RUBRAVIEW_CARD_BOTTOM;
    if (edges) return edges;
    if (y < r.y + card->title_height) return RUBRAVIEW_CARD_TITLE;
    return RUBRAVIEW_CARD_BODY;
}

bool rubraview_card_press(rubraview_card_t *card, double x, double y) {
    int32_t part = rubraview_card_hit(card, x, y);
    if (part == RUBRAVIEW_CARD_OUTSIDE) {
        card->focused = false;
        card->drag = 0;
        return false;
    }
    card->focused = true;
    card->drag = part == RUBRAVIEW_CARD_BODY ? 0 : part;
    card->grab_x = x;
    card->grab_y = y;
    card->grab_rect = card->rect;
    return true;
}

bool rubraview_card_motion(rubraview_card_t *card, double x, double y, double host_w, double host_h) {
    if (!card->open || card->drag == 0) return false;
    const rubraview_rect_t g = card->grab_rect, before = card->rect;
    const double dx = x - card->grab_x, dy = y - card->grab_y, m = card->margin;
    rubraview_rect_t r = g;
    if (card->drag == RUBRAVIEW_CARD_TITLE) {
        r.x = g.x + dx;
        r.y = g.y + dy;
    } else {
        if (card->drag & RUBRAVIEW_CARD_LEFT) {   /* the right edge stays where it was */
            double right = g.x + g.width;
            r.x = card_min(card_max(g.x + dx, m), right - card->min_width);
            r.width = right - r.x;
        } else if (card->drag & RUBRAVIEW_CARD_RIGHT) {
            r.width = card_min(card_max(g.width + dx, card->min_width), host_w - m - g.x);
        }
        if (card->drag & RUBRAVIEW_CARD_TOP) {
            double bottom = g.y + g.height;
            r.y = card_min(card_max(g.y + dy, m), bottom - card->min_height);
            r.height = bottom - r.y;
        } else if (card->drag & RUBRAVIEW_CARD_BOTTOM) {
            r.height = card_min(card_max(g.height + dy, card->min_height), host_h - m - g.y);
        }
        card->sized = true;
    }
    card->rect = r;
    card_clamp(card, host_w, host_h);
    return card->rect.x != before.x || card->rect.y != before.y || card->rect.width != before.width || card->rect.height != before.height;
}

bool rubraview_card_release(rubraview_card_t *card) {
    bool had = card->drag != 0;
    card->drag = 0;
    return had;
}

rubraview_card_share_t rubraview_card_share_of(const rubraview_card_t *card, double host_w, double host_h) {
    rubraview_card_share_t s = {0};
    if (host_w <= 0.0 || host_h <= 0.0) return s;
    s.left = card->rect.x / host_w;
    s.top = card->rect.y / host_h;
    s.width = card->rect.width / host_w;
    s.height = card->rect.height / host_h;
    s.sized = card->sized;
    s.known = rubraview_card_share_valid(&(rubraview_card_share_t){ s.left, s.top, s.width, s.height, true, s.sized });
    return s;
}

bool rubraview_card_share_valid(const rubraview_card_share_t *share) {
    return share->known && share->left >= 0.0 && share->left < 1.0 && share->top >= 0.0 && share->top < 1.0 &&
           share->width > 0.0 && share->width <= 1.0 && share->height > 0.0 && share->height <= 1.0;
}
