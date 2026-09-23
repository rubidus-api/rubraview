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

/* ---- §3.14.6: who gets the speakers (RV-081) ---- */

/**
 * Music goes on playing while pictures are looked at or a comic is read.
 * Then a film with sound is opened, and two things want the speakers at
 * once. The arbiter decides: the music stands aside, and comes back when
 * the film is over or closed.
 *
 * The rule that matters, and the one players get wrong, is that **the
 * listener's own pause outranks the arbiter**. Music the listener
 * stopped by hand must not come back to life because a film ended. So
 * the arbiter remembers *who* paused it, not merely that it is paused.
 */

typedef enum rubraview_bgm_event {
    RUBRAVIEW_BGM_MUSIC_OPENED = 0,  /* a track is playing as background */
    RUBRAVIEW_BGM_MUSIC_CLOSED,      /* there is no background music now */
    RUBRAVIEW_BGM_PAGE_SOUNDS,       /* a page with sound of its own is on screen */
    RUBRAVIEW_BGM_PAGE_QUIET,        /* that page has ended, or is gone */
    RUBRAVIEW_BGM_READER_PAUSED,     /* the listener stopped the music by hand */
    RUBRAVIEW_BGM_READER_RESUMED,    /* and started it again */
} rubraview_bgm_event_t;

typedef enum rubraview_bgm_action {
    RUBRAVIEW_BGM_DO_NOTHING = 0,
    RUBRAVIEW_BGM_DO_PAUSE,
    RUBRAVIEW_BGM_DO_RESUME,
} rubraview_bgm_action_t;

typedef struct rubraview_bgm {
    bool holds_music;    /* there is something to arbitrate over */
    bool reader_paused;  /* the listener's own pause: it outranks the arbiter */
    bool stood_aside;    /* the arbiter paused it, and owes it a resume */
    bool page_sounds;    /* a page with sound is on screen now */
} rubraview_bgm_t;

/**
 * What should happen to the music. `pause_for_sound` is the reader's
 * setting (`audio.bgm_pause_on_video`); with it off the arbiter never
 * stands the music aside and the two simply play together.
 */
rubraview_bgm_action_t rubraview_bgm_event(rubraview_bgm_t *state,
                                            rubraview_bgm_event_t event,
                                            bool pause_for_sound);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_MUSIC_H */
