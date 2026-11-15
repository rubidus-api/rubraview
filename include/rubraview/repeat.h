#ifndef RUBRAVIEW_REPEAT_H
#define RUBRAVIEW_REPEAT_H

#include "rubraview/core.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What happens when a film or a song ends (owner, 2026-09-29): stop there,
 * play the next file, play this one again, go round the whole folder or
 * playlist, or shuffle — every file once before any comes again. Only the
 * pages that play (films and songs) take part; pictures between them are
 * passed over. Host-tested; the viewer supplies which pages play.
 */

typedef enum rubraview_repeat_mode {
    RUBRAVIEW_REPEAT_STOP = 0,   /* play this file, then stop */
    RUBRAVIEW_REPEAT_NEXT,       /* then the next one, and stop after the last */
    RUBRAVIEW_REPEAT_ONE,        /* this file over and over */
    RUBRAVIEW_REPEAT_ALL,        /* the whole list, round and round */
    RUBRAVIEW_REPEAT_SHUFFLE,    /* the whole list in a random order, each once a round */
    RUBRAVIEW_REPEAT_MODE_COUNT,
} rubraview_repeat_mode_t;

/** The mode after this one, for the button that cycles them. */
rubraview_repeat_mode_t rubraview_repeat_cycle(rubraview_repeat_mode_t mode);

/** A button's short caption ("Once", "Next", "1 loop", "Loop", "Shuffle") and a sentence for the OSD. */
const char *rubraview_repeat_caption(rubraview_repeat_mode_t mode);
const char *rubraview_repeat_sentence(rubraview_repeat_mode_t mode);

/** Whether page `index` plays (a film or a song). */
typedef bool (*rubraview_repeat_plays_fn)(void *ctx, size_t index);

/*
 * Shuffle. One flag a page says whether it has played in this round — so n
 * flags for n pages — and the page that played last is remembered besides
 * them, so a new round never starts with the one that has just ended: n
 * flags and one more index. The caller owns the flags (`count` bytes).
 */
typedef struct rubraview_shuffle {
    uint8_t *played;
    size_t   count;
    size_t   played_count;   /* flags set in this round */
    int32_t  last;           /* the page that played last, -1 before any */
    uint64_t rng;
} rubraview_shuffle_t;

/** Start over with no page played; `seed` 0 picks a fixed seed. */
void rubraview_shuffle_reset(rubraview_shuffle_t *sh, proven_mem_mut_t played_view, uint64_t seed);

/** Page `index` is playing: flagged for this round and remembered as the last. */
void rubraview_shuffle_mark(rubraview_shuffle_t *sh, size_t index);

/**
 * The next page to play: at random among the playing pages not yet played
 * in this round, never `current`. When every one has played, a new round
 * starts (the flags clear) and the pick still avoids `current`. -1 when no
 * page plays; `current` itself when it is the only one.
 */
int32_t rubraview_shuffle_pick(rubraview_shuffle_t *sh, int32_t current, rubraview_repeat_plays_fn plays, void *ctx);

/**
 * RV-288: a pick among some of the pages (`plays`: the films and songs) inside a round of all of them: as
 * rubraview_shuffle_pick, but when every one of those has played, any of them but `current` at random and the
 * round goes on (the pictures' flags are not cleared by the songs' round).
 */
int32_t rubraview_shuffle_pick_within(rubraview_shuffle_t *sh, int32_t current, rubraview_repeat_plays_fn plays, void *ctx);

/*
 * RV-288 (owner, 2026-11-15: "랜덤하게 볼 때, 뒤로가기 앞으로가기가 어느정도까지는 재생순서를 따라갈 수가 있어야"; the
 * length: "기록 길이는 재생목록 길이만큼"): the pages shown in a shuffle, in the order shown, as a browser's history —
 * Back goes to the one before, Forward again over the same ones, a page reached any other way cuts what was ahead
 * and is added. At most `cap` (the list's length) are kept; past that the oldest goes. The caller owns the
 * `cap` slots.
 */
typedef struct rubraview_shuffle_history {
    int32_t *pages;
    size_t   cap;
    size_t   first;   /* the oldest's slot */
    size_t   len;
    size_t   at;      /* the page shown, 0..len-1 from the oldest (len 0: none) */
} rubraview_shuffle_history_t;

void rubraview_shuffle_history_reset(rubraview_shuffle_history_t *h, int32_t *slots, size_t cap);
/** Page `page` is now shown: the one ahead or behind becomes the place; else what was ahead goes and it is added. */
void rubraview_shuffle_history_note(rubraview_shuffle_history_t *h, int32_t page);
/** A new page picked: what was ahead goes and it is added, even when it is the one behind (no step is guessed). */
void rubraview_shuffle_history_push(rubraview_shuffle_history_t *h, int32_t page);
/** Back (-1) or Forward (+1) taken: the place moves one, when there is one that way. */
void rubraview_shuffle_history_step(rubraview_shuffle_history_t *h, int direction);
/** The page Back goes to, or -1 at the oldest; the page Forward goes to, or -1 at the newest. */
int32_t rubraview_shuffle_history_back(const rubraview_shuffle_history_t *h);
int32_t rubraview_shuffle_history_forward(const rubraview_shuffle_history_t *h);
/** The page shown as the history has it, or -1. */
int32_t rubraview_shuffle_history_current(const rubraview_shuffle_history_t *h);
/** Page `index` left the list: its entries go (two of one page side by side are one), those after it move down. */
void rubraview_shuffle_history_removed(rubraview_shuffle_history_t *h, size_t index);
/** `src`'s pages and place into `dst` (its slots and cap as reset); when `dst` holds fewer, the oldest stay behind. */
void rubraview_shuffle_history_copy(rubraview_shuffle_history_t *dst, const rubraview_shuffle_history_t *src);

/**
 * The page to play after `current` ends: -1 to stop (STOP, or NEXT past the
 * last), `current` again for ONE (and for ALL or SHUFFLE when it is the
 * only one that plays), the next playing page for NEXT and ALL (ALL going
 * round to the first), a shuffle pick for SHUFFLE (`sh` may be NULL
 * otherwise).
 */
int32_t rubraview_repeat_follow(rubraview_repeat_mode_t mode, int32_t current, size_t count,
                                rubraview_repeat_plays_fn plays, void *ctx, rubraview_shuffle_t *sh);

/**
 * A-B repeat: a point just taken at `t` (owner, 2026-10-15). When it
 * crosses the other one, the other gives way: a new A at or past B moves
 * B to the end (`duration`, a tenth of a second short of it so the loop
 * comes before the file ends; unset when the length is not known); a new
 * B at or before A, or with no A, moves A to the start. Points are file
 * seconds, -1 unset.
 */
void rubraview_ab_take(double *a, double *b, double t, bool is_a, double duration);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_REPEAT_H */
