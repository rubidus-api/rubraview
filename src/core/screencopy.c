#include "rubraview/screencopy.h"
#include <math.h>
#include <string.h>

/* Exactly the same draw: every field, the doubles bit for bit. */
static bool same_key(const rubraview_screencopy_key_t *x, const rubraview_screencopy_key_t *y) {
    return x->texture == y->texture && x->content == y->content &&
           x->a == y->a && x->b == y->b && x->c == y->c && x->d == y->d && x->e == y->e && x->f == y->f &&
           x->src_w == y->src_w && x->src_h == y->src_h && x->filter == y->filter &&
           x->target_w == y->target_w && x->target_h == y->target_h;
}

bool rubraview_screencopy_eligible(const rubraview_screencopy_key_t *key, bool software, bool video, bool hdr,
                                   double opacity, bool smooth) {
    if (!key || !key->texture || !software || video || hdr || !smooth || opacity < 1.0) return false;
    if (key->src_w <= 0.0 || key->src_h <= 0.0 || key->target_w <= 0 || key->target_h <= 0) return false;
    double area = fabs(key->a * key->d - key->b * key->c);   /* target pixels per texture pixel */
    return isfinite(area) && area > 0.0 && area < 1.0;
}

bool rubraview_screencopy_bounds(const rubraview_screencopy_key_t *key,
                                 int32_t *x, int32_t *y, int32_t *w, int32_t *h) {
    if (!key) return false;
    const double px[4] = { 0.0, key->src_w, 0.0, key->src_w };
    const double py[4] = { 0.0, 0.0, key->src_h, key->src_h };
    double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
    for (int i = 0; i < 4; ++i) {
        double tx = key->a * px[i] + key->c * py[i] + key->e;
        double ty = key->b * px[i] + key->d * py[i] + key->f;
        if (!isfinite(tx) || !isfinite(ty)) return false;
        x0 = fmin(x0, tx); x1 = fmax(x1, tx);
        y0 = fmin(y0, ty); y1 = fmax(y1, ty);
    }
    x0 = fmax(floor(x0), 0.0);
    y0 = fmax(floor(y0), 0.0);
    x1 = fmin(ceil(x1), (double)key->target_w);
    y1 = fmin(ceil(y1), (double)key->target_h);
    if (x1 <= x0 || y1 <= y0) return false;
    *x = (int32_t)x0;
    *y = (int32_t)y0;
    *w = (int32_t)(x1 - x0);
    *h = (int32_t)(y1 - y0);
    return true;
}

static void let_go(rubraview_screencopy_slot_t *s, bool *drop) {
    *drop = s->used && s->made;
    memset(s, 0, sizeof(*s));
}

void rubraview_screencopy_frame(rubraview_screencopy_t *sc, bool drop[RUBRAVIEW_SCREENCOPY_SLOTS]) {
    for (int i = 0; i < RUBRAVIEW_SCREENCOPY_SLOTS; ++i) {
        rubraview_screencopy_slot_t *s = &sc->slots[i];
        drop[i] = false;
        if (!s->used) continue;
        if (!s->touched) { let_go(s, &drop[i]); continue; }
        s->seen_before = true;
        s->touched = false;
    }
}

rubraview_screencopy_action_t rubraview_screencopy_step(rubraview_screencopy_t *sc,
                                                        const rubraview_screencopy_key_t *key,
                                                        int *slot, bool *evict) {
    *slot = -1;
    *evict = false;
    for (int i = 0; i < RUBRAVIEW_SCREENCOPY_SLOTS; ++i) {
        rubraview_screencopy_slot_t *s = &sc->slots[i];
        if (!s->used || !same_key(&s->key, key)) continue;
        if (s->touched) return RUBRAVIEW_SCREENCOPY_DIRECT;   /* the same draw twice in a frame: the first has it */
        s->touched = true;
        *slot = i;
        if (s->made) return RUBRAVIEW_SCREENCOPY_USE;
        if (s->failed || !s->seen_before) return RUBRAVIEW_SCREENCOPY_DIRECT;
        return RUBRAVIEW_SCREENCOPY_MAKE;
    }
    /* A new key: a free slot, else one not drawn in this frame. */
    int pick = -1;
    for (int i = 0; i < RUBRAVIEW_SCREENCOPY_SLOTS && pick < 0; ++i) if (!sc->slots[i].used) pick = i;
    for (int i = 0; i < RUBRAVIEW_SCREENCOPY_SLOTS && pick < 0; ++i) if (!sc->slots[i].touched) pick = i;
    if (pick < 0) return RUBRAVIEW_SCREENCOPY_DIRECT;
    rubraview_screencopy_slot_t *s = &sc->slots[pick];
    let_go(s, evict);
    s->key = *key;
    s->used = true;
    s->touched = true;
    *slot = pick;
    return RUBRAVIEW_SCREENCOPY_DIRECT;
}

void rubraview_screencopy_made(rubraview_screencopy_t *sc, int slot, bool ok,
                               int32_t x, int32_t y, int32_t w, int32_t h) {
    if (slot < 0 || slot >= RUBRAVIEW_SCREENCOPY_SLOTS || !sc->slots[slot].used) return;
    rubraview_screencopy_slot_t *s = &sc->slots[slot];
    s->made = ok;
    s->failed = !ok;
    s->x = ok ? x : 0;
    s->y = ok ? y : 0;
    s->w = ok ? w : 0;
    s->h = ok ? h : 0;
}

void rubraview_screencopy_forget(rubraview_screencopy_t *sc, const void *texture,
                                 bool drop[RUBRAVIEW_SCREENCOPY_SLOTS]) {
    for (int i = 0; i < RUBRAVIEW_SCREENCOPY_SLOTS; ++i) {
        drop[i] = false;
        if (sc->slots[i].used && sc->slots[i].key.texture == texture) let_go(&sc->slots[i], &drop[i]);
    }
}

void rubraview_screencopy_reset(rubraview_screencopy_t *sc, bool drop[RUBRAVIEW_SCREENCOPY_SLOTS]) {
    for (int i = 0; i < RUBRAVIEW_SCREENCOPY_SLOTS; ++i) let_go(&sc->slots[i], &drop[i]);
}
