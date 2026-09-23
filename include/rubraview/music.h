#ifndef RUBRAVIEW_MUSIC_H
#define RUBRAVIEW_MUSIC_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * One track running into the next (RFC-0001 §3.14.1, RV-075).
 *
 * A record is not a row of separate songs: a live set or a symphony is
 * cut into tracks that were never meant to have silence between them.
 * Making that work is a matter of *timing*, and timing is what goes
 * wrong silently — so the deciding is here, as a function of where the
 * track is, and the viewer only carries it out.
 *
 * Two things are decided:
 *
 *   - **when to open the next track.** Opening a file takes long enough
 *     to be heard, so it is done while the current one still plays.
 *   - **how loud each of the two should be now.** With no crossfade the
 *     answer is "all of the first, then all of the second". With one,
 *     they overlap on an equal-power curve — `cos` and `sin` of the same
 *     quarter turn — because two sounds at half volume each are not half
 *     as loud together, and a linear fade dips audibly in the middle.
 */

typedef struct rubraview_track_change {
    bool   gapless;             /* false: the old behaviour, one track then the next */
    double crossfade_seconds;   /* 0 .. 5; 0 means gapless with no overlap */
} rubraview_track_change_t;

typedef struct rubraview_track_plan {
    bool   open_next;       /* open it now, so it is ready when it is wanted */
    bool   start_next;      /* let it play now */
    bool   close_current;   /* the current one has nothing left to give */
    double gain_current;    /* 0..1 */
    double gain_next;       /* 0..1 */
} rubraview_track_plan_t;

/** How long before the overlap the next track is opened. */
#define RUBRAVIEW_TRACK_LEAD_SECONDS 2.0

/**
 * What should be happening now. `duration_seconds` of 0 or less means
 * the file does not say how long it is — nothing is planned then, since
 * a transition can only be timed against an end that is known.
 */
rubraview_track_plan_t rubraview_track_plan(rubraview_track_change_t how,
                                            double position_seconds,
                                            double duration_seconds,
                                            bool has_next);

/**
 * The gains of a crossfade `t` of the way through, 0 to 1: equal power,
 * so the two together stay as loud as either alone.
 */
void rubraview_crossfade_gains(double t, double *out_current, double *out_next);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_MUSIC_H */
