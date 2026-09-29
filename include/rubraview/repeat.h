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
void rubraview_shuffle_reset(rubraview_shuffle_t *sh, uint8_t *played, size_t count, uint64_t seed);

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
 * The page to play after `current` ends: -1 to stop (STOP, or NEXT past the
 * last), `current` again for ONE (and for ALL or SHUFFLE when it is the
 * only one that plays), the next playing page for NEXT and ALL (ALL going
 * round to the first), a shuffle pick for SHUFFLE (`sh` may be NULL
 * otherwise).
 */
int32_t rubraview_repeat_follow(rubraview_repeat_mode_t mode, int32_t current, size_t count,
                                rubraview_repeat_plays_fn plays, void *ctx, rubraview_shuffle_t *sh);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_REPEAT_H */
