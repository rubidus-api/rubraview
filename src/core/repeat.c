#include "rubraview/repeat.h"
#include <string.h>

rubraview_repeat_mode_t rubraview_repeat_cycle(rubraview_repeat_mode_t mode) {
    int next = ((int)mode + 1) % RUBRAVIEW_REPEAT_MODE_COUNT;
    return next < 0 ? RUBRAVIEW_REPEAT_STOP : (rubraview_repeat_mode_t)next;
}

const char *rubraview_repeat_caption(rubraview_repeat_mode_t mode) {
    switch (mode) {
        case RUBRAVIEW_REPEAT_STOP:    return "Once";
        case RUBRAVIEW_REPEAT_NEXT:    return "Next";
        case RUBRAVIEW_REPEAT_ONE:     return "1 loop";
        case RUBRAVIEW_REPEAT_ALL:     return "Loop";
        case RUBRAVIEW_REPEAT_SHUFFLE: return "Shuffle";
        default:                       return "";
    }
}

const char *rubraview_repeat_sentence(rubraview_repeat_mode_t mode) {
    switch (mode) {
        case RUBRAVIEW_REPEAT_STOP:    return "at the end: stop";
        case RUBRAVIEW_REPEAT_NEXT:    return "at the end: the next file";
        case RUBRAVIEW_REPEAT_ONE:     return "repeat: this file";
        case RUBRAVIEW_REPEAT_ALL:     return "repeat: all of them";
        case RUBRAVIEW_REPEAT_SHUFFLE: return "shuffle: each once, then again";
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

void rubraview_shuffle_reset(rubraview_shuffle_t *sh, uint8_t *played, size_t count, uint64_t seed) {
    if (!sh) return;
    sh->played = played;
    sh->count = played ? count : 0;
    sh->played_count = 0;
    sh->last = -1;
    sh->rng = seed ? seed : 0x9E3779B97F4A7C15ull;
    if (played && count) memset(played, 0, count);
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
        memset(sh->played, 0, sh->count);
        sh->played_count = 0;
        n = candidates(sh, current, false, plays, ctx, 0, NULL);
        if (n == 0) return current >= 0 && (size_t)current < sh->count && plays(ctx, (size_t)current) ? current : -1;
    }
    int32_t chosen = -1;
    (void)candidates(sh, current, true, plays, ctx, (size_t)(next_random(sh) % n), &chosen);
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
