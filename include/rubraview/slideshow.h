#ifndef RUBRAVIEW_SLIDESHOW_H
#define RUBRAVIEW_SLIDESHOW_H

#include "rubraview/core.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Slide-show sequencer state machine (RFC-0001 §3.2.1, §3.2.2, §3.2.4,
 * §3.2.6): a pure state machine over an already-built item list. The
 * hardware timer that drives `delta_seconds` (§3.2.1) and the directory/
 * playlist/selection scope that builds the item list (§3.2.2) are M2/M3
 * concerns; extension/type filtering (§3.2.4) is
 * `rubraview_glob_match_list` (rubraview/glob.h), applied by the caller
 * while building that list — this module starts from the filtered
 * sequence.
 *
 * Note: a single rubraview_slideshow_tick call advances at most one item
 * even if `delta_seconds` is large enough to have covered several dwell
 * periods; callers drive this with small per-frame deltas (as the RFC's
 * own high-resolution timer does), not with large or irregular jumps.
 */

typedef enum rubraview_media_kind {
    RUBRAVIEW_MEDIA_STILL = 0,
    RUBRAVIEW_MEDIA_ANIMATED,
    RUBRAVIEW_MEDIA_VIDEO,
} rubraview_media_kind_t;

typedef enum rubraview_loop_policy {
    RUBRAVIEW_LOOP_ALL = 0,
    RUBRAVIEW_LOOP_PLAY_ONCE,
    RUBRAVIEW_LOOP_REPEAT_SINGLE,
} rubraview_loop_policy_t;

typedef struct rubraview_slideshow_item {
    rubraview_media_kind_t kind;
    double duration_seconds; /* VIDEO: full length; ANIMATED: one animation cycle; STILL: unused */
} rubraview_slideshow_item_t;

typedef struct rubraview_slideshow {
    double interval_seconds; /* T_slide; always clamped to [0.1, 300.0] */
    rubraview_loop_policy_t loop;
    bool   paused;
    size_t current_index;
    size_t item_count;
    double elapsed_in_item;
} rubraview_slideshow_t;

/** Clamps interval_seconds to [0.1, 300.0]. */
rubraview_slideshow_t rubraview_slideshow_create(size_t item_count, double interval_seconds, rubraview_loop_policy_t loop);

/** Re-clamped to [0.1, 300.0] (used by the `[`/`]`/Shift+`[`/`]` step hotkeys, §3.2.1, §3.7.2). */
void rubraview_slideshow_set_interval(rubraview_slideshow_t *s, double seconds);

void rubraview_slideshow_pause(rubraview_slideshow_t *s);
void rubraview_slideshow_resume(rubraview_slideshow_t *s);

/**
 * §3.2.6: the time an item must remain on screen before auto-advancing.
 * STILL -> interval_seconds. ANIMATED -> max(interval_seconds,
 * duration_seconds) (never cut off mid-cycle). VIDEO -> duration_seconds
 * (plays in its entirety, independent of the interval).
 */
double rubraview_slideshow_required_dwell(const rubraview_slideshow_item_t *item, double interval_seconds);

typedef enum rubraview_slideshow_event {
    RUBRAVIEW_SLIDESHOW_NONE = 0,    /* still dwelling on the current item, or paused */
    RUBRAVIEW_SLIDESHOW_ADVANCED,    /* moved to the next item (or restarted the current one, under REPEAT_SINGLE) */
    RUBRAVIEW_SLIDESHOW_ENDED,       /* reached the end under LOOP_PLAY_ONCE; the slideshow is now paused on the last item */
} rubraview_slideshow_event_t;

/**
 * Advance the internal clock by delta_seconds and apply the loop policy
 * when an item's dwell time elapses. No-op (returns NONE) while paused or
 * when item_count is 0.
 */
rubraview_slideshow_event_t rubraview_slideshow_tick(rubraview_slideshow_t *s, const rubraview_slideshow_item_t *items, double delta_seconds);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_SLIDESHOW_H */
