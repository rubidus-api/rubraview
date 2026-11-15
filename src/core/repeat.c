#include "rubraview/core.h"
#include "rubraview/repeat.h"
#include "rubraview/lang.h"
#include <string.h>

rubraview_repeat_mode_t rubraview_repeat_cycle(rubraview_repeat_mode_t mode) {
    int next = ((int)mode + 1) % RUBRAVIEW_REPEAT_MODE_COUNT;
    return next < 0 ? RUBRAVIEW_REPEAT_STOP : (rubraview_repeat_mode_t)next;
}

const char *rubraview_repeat_caption(rubraview_repeat_mode_t mode) {
    switch (mode) {
        case RUBRAVIEW_REPEAT_STOP:    return T("Once");
        case RUBRAVIEW_REPEAT_NEXT:    return T("Next");
        case RUBRAVIEW_REPEAT_ONE:     return T("1 loop");
        case RUBRAVIEW_REPEAT_ALL:     return T("Loop");
        case RUBRAVIEW_REPEAT_SHUFFLE: return T("Shuffle");
        default:                       return "";
    }
}

const char *rubraview_repeat_sentence(rubraview_repeat_mode_t mode) {
    switch (mode) {
        case RUBRAVIEW_REPEAT_STOP:    return T("at the end: stop");
        case RUBRAVIEW_REPEAT_NEXT:    return T("at the end: the next file");
        case RUBRAVIEW_REPEAT_ONE:     return T("repeat: this file");
        case RUBRAVIEW_REPEAT_ALL:     return T("repeat: all of them");
        case RUBRAVIEW_REPEAT_SHUFFLE: return T("shuffle: each once, then again");
        default:                       return "";
    }
}

/* xorshift64*: plenty for choosing a song. */
static uint64_t next_random(rubraview_shuffle_t *sh) {
    uint64_t x = sh->rng ? sh->rng : 0x9E3779B97F4A7C15ull;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    sh->rng = x;
    return x * 0x2545F4914F6CDD1Dull;
}

void rubraview_shuffle_reset(rubraview_shuffle_t *sh, proven_mem_mut_t played_view, uint64_t seed) {
    uint8_t *played = (uint8_t *)played_view.ptr;
    size_t count = (size_t)played_view.size;
    if (!sh) return;
    sh->played = played;
    sh->count = played ? count : 0;
    sh->played_count = 0;
    sh->last = -1;
    sh->rng = seed ? seed : 0x9E3779B97F4A7C15ull;
    if (played && count) proven_sys_mem_set(played, 0, count);
}

void rubraview_shuffle_mark(rubraview_shuffle_t *sh, size_t index) {
    if (!sh || index >= sh->count) return;
    if (!sh->played[index]) {
        sh->played[index] = 1;
        sh->played_count++;
    }
    sh->last = (int32_t)index;
}

/* The candidates: playing pages, not `current`, and (when `fresh`) not yet played. */
static size_t candidates(const rubraview_shuffle_t *sh, int32_t current, bool fresh,
                         rubraview_repeat_plays_fn plays, void *ctx, size_t pick, int32_t *out) {
    size_t n = 0;
    for (size_t i = 0; i < sh->count; ++i) {
        if ((int32_t)i == current || (fresh && sh->played[i]) || !plays(ctx, i)) continue;
        if (out && n == pick) { *out = (int32_t)i; return n + 1; }
        n++;
    }
    return n;
}

int32_t rubraview_shuffle_pick(rubraview_shuffle_t *sh, int32_t current, rubraview_repeat_plays_fn plays, void *ctx) {
    if (!sh || !plays || sh->count == 0) return -1;
    size_t n = candidates(sh, current, true, plays, ctx, 0, NULL);
    if (n == 0) {
        /* Every one has played: a new round. `current` (the last one) stays
           out of the first pick, so it is not heard twice in a row. */
        proven_sys_mem_set(sh->played, 0, sh->count);
        sh->played_count = 0;
        n = candidates(sh, current, false, plays, ctx, 0, NULL);
        if (n == 0) return current >= 0 && (size_t)current < sh->count && plays(ctx, (size_t)current) ? current : -1;
    }
    int32_t chosen = -1;
    (void)candidates(sh, current, true, plays, ctx, (size_t)(next_random(sh) % n), &chosen);
    return chosen;
}

/* ---- RV-288: the shuffle's history ---- */

static int32_t *hist_slot(const rubraview_shuffle_history_t *h, size_t i) { return &h->pages[(h->first + i) % h->cap]; }

void rubraview_shuffle_history_reset(rubraview_shuffle_history_t *h, int32_t *slots, size_t cap) {
    if (!h) return;
    *h = (rubraview_shuffle_history_t){ .pages = slots, .cap = slots ? cap : 0 };
}

void rubraview_shuffle_history_note(rubraview_shuffle_history_t *h, int32_t page) {
    if (!h || h->cap == 0 || page < 0) return;
    if (h->len) {
        if (*hist_slot(h, h->at) == page) return;
        if (h->at + 1 < h->len && *hist_slot(h, h->at + 1) == page) { h->at++; return; }
        if (h->at > 0 && *hist_slot(h, h->at - 1) == page) { h->at--; return; }
    }
    rubraview_shuffle_history_push(h, page);
}

void rubraview_shuffle_history_step(rubraview_shuffle_history_t *h, int direction) {
    if (!h || h->len == 0) return;
    if (direction < 0 && h->at > 0) h->at--;
    else if (direction > 0 && h->at + 1 < h->len) h->at++;
}

int32_t rubraview_shuffle_history_current(const rubraview_shuffle_history_t *h) {
    return h && h->len ? *hist_slot(h, h->at) : -1;
}

int32_t rubraview_shuffle_history_back(const rubraview_shuffle_history_t *h) {
    return h && h->len && h->at > 0 ? *hist_slot(h, h->at - 1) : -1;
}

int32_t rubraview_shuffle_history_forward(const rubraview_shuffle_history_t *h) {
    return h && h->len && h->at + 1 < h->len ? *hist_slot(h, h->at + 1) : -1;
}

void rubraview_shuffle_history_push(rubraview_shuffle_history_t *h, int32_t page) {
    if (!h || h->cap == 0 || page < 0) return;
    if (h->len) {
        if (*hist_slot(h, h->at) == page) return;
        h->len = h->at + 1;   /* what was ahead goes */
    }
    if (h->len == h->cap) {   /* full: the oldest goes */
        h->first = (h->first + 1) % h->cap;
        h->len--;
    }
    *hist_slot(h, h->len) = page;
    h->at = h->len;
    h->len++;
}

void rubraview_shuffle_history_removed(rubraview_shuffle_history_t *h, size_t index) {
    if (!h || h->len == 0) return;
    size_t kept = 0, at = 0;
    bool at_set = false;
    for (size_t i = 0; i < h->len; ++i) {
        int32_t p = *hist_slot(h, i);
        if (p == (int32_t)index) {
            if (i == h->at && !at_set) { at = kept ? kept - 1 : 0; at_set = true; }   /* the place falls back to the one before */
            continue;
        }
        if (p > (int32_t)index) p--;
        if (kept > 0 && *hist_slot(h, kept - 1) == p) {   /* the same page twice in a row: once */
            if (i == h->at && !at_set) { at = kept - 1; at_set = true; }
            continue;
        }
        *hist_slot(h, kept) = p;
        if (i == h->at && !at_set) { at = kept; at_set = true; }
        kept++;
    }
    h->len = kept;
    h->at = kept == 0 ? 0 : (at < kept ? at : kept - 1);
}

void rubraview_shuffle_history_copy(rubraview_shuffle_history_t *dst, const rubraview_shuffle_history_t *src) {
    if (!dst || dst->cap == 0) return;
    dst->first = 0; dst->len = 0; dst->at = 0;
    if (!src || src->len == 0) return;
    size_t drop = src->len > dst->cap ? src->len - dst->cap : 0;
    for (size_t i = drop; i < src->len; ++i) dst->pages[dst->len++] = *hist_slot(src, i);
    dst->at = src->at >= drop ? src->at - drop : 0;
}

int32_t rubraview_shuffle_pick_within(rubraview_shuffle_t *sh, int32_t current, rubraview_repeat_plays_fn plays, void *ctx) {
    if (!sh || !plays || sh->count == 0) return -1;
    bool fresh = candidates(sh, current, true, plays, ctx, 0, NULL) > 0;
    size_t n = candidates(sh, current, fresh, plays, ctx, 0, NULL);
    if (n == 0) return current >= 0 && (size_t)current < sh->count && plays(ctx, (size_t)current) ? current : -1;
    int32_t chosen = -1;
    (void)candidates(sh, current, fresh, plays, ctx, (size_t)(next_random(sh) % n), &chosen);
    return chosen;
}

int32_t rubraview_repeat_follow(rubraview_repeat_mode_t mode, int32_t current, size_t count,
                                rubraview_repeat_plays_fn plays, void *ctx, rubraview_shuffle_t *sh) {
    if (!plays || current < 0 || (size_t)current >= count) return -1;
    switch (mode) {
        case RUBRAVIEW_REPEAT_STOP: return -1;
        case RUBRAVIEW_REPEAT_ONE:  return current;
        case RUBRAVIEW_REPEAT_SHUFFLE: return sh ? rubraview_shuffle_pick(sh, current, plays, ctx) : -1;
        case RUBRAVIEW_REPEAT_NEXT:
        case RUBRAVIEW_REPEAT_ALL:
            for (size_t step = 1; step <= count; ++step) {
                size_t i = (size_t)current + step;
                if (i >= count) {
                    if (mode == RUBRAVIEW_REPEAT_NEXT) return -1;
                    i -= count;   /* round to the first */
                }
                if (plays(ctx, i)) return (int32_t)i;   /* at step == count this is `current`: the only one */
            }
            return -1;
        default: return -1;
    }
}

void rubraview_ab_take(double *a, double *b, double t, bool is_a, double duration) {
    if (!a || !b) return;
    if (t < 0.0) t = 0.0;
    if (is_a) {
        *a = t;
        if (*b >= 0.0 && *b <= t) {
            double end = duration - 0.1;
            *b = end > t ? end : -1.0;
        }
    } else {
        *b = t;
        if (*a < 0.0 || *a >= t) *a = 0.0;
    }
}
