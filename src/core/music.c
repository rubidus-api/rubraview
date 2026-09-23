#include "rubraview/music.h"
#include <math.h>

void rubraview_crossfade_gains(double t, double *out_current, double *out_next) {
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    double turn = t * (3.14159265358979323846 / 2.0);
    if (out_current) *out_current = cos(turn);
    if (out_next) *out_next = sin(turn);
}

rubraview_track_plan_t rubraview_track_plan(rubraview_track_change_t how,
                                            double position_seconds,
                                            double duration_seconds,
                                            bool has_next) {
    rubraview_track_plan_t plan = {
        .open_next = false,
        .start_next = false,
        .close_current = false,
        .gain_current = 1.0,
        .gain_next = 0.0,
    };

    /* An end that is not known cannot be aimed at. The track still plays
       and still ends; it simply ends the way it always did. */
    if (duration_seconds <= 0.0) return plan;

    double remaining = duration_seconds - position_seconds;
    if (remaining < 0.0) remaining = 0.0;
    plan.close_current = remaining <= 0.0;

    if (!has_next || !how.gapless) return plan;

    double crossfade = how.crossfade_seconds;
    if (crossfade < 0.0) crossfade = 0.0;
    if (crossfade > 5.0) crossfade = 5.0;
    /* A crossfade never takes more than half a track: on a short one —
       an interlude, a spoken introduction — five seconds of overlap
       would mean it was never heard by itself at all. */
    if (crossfade > duration_seconds * 0.5) crossfade = duration_seconds * 0.5;

    plan.open_next = remaining <= crossfade + RUBRAVIEW_TRACK_LEAD_SECONDS;
    plan.start_next = remaining <= crossfade;

    if (crossfade > 0.0 && plan.start_next) {
        rubraview_crossfade_gains((crossfade - remaining) / crossfade,
                                  &plan.gain_current, &plan.gain_next);
    } else if (plan.start_next) {
        /* No overlap: the moment the first ends the second is whole. */
        plan.gain_current = 0.0;
        plan.gain_next = 1.0;
    }
    return plan;
}

/* ---- §3.14.6: who gets the speakers ---- */

rubraview_bgm_action_t rubraview_bgm_event(rubraview_bgm_t *state,
                                            rubraview_bgm_event_t event,
                                            bool pause_for_sound) {
    if (!state) return RUBRAVIEW_BGM_DO_NOTHING;

    switch (event) {
        case RUBRAVIEW_BGM_MUSIC_OPENED:
            state->holds_music = true;
            state->reader_paused = false;
            state->stood_aside = false;
            /* Opened while a film is already sounding: it stands aside
               at once rather than talking over it. */
            if (state->page_sounds && pause_for_sound) {
                state->stood_aside = true;
                return RUBRAVIEW_BGM_DO_PAUSE;
            }
            return RUBRAVIEW_BGM_DO_NOTHING;

        case RUBRAVIEW_BGM_MUSIC_CLOSED:
            state->holds_music = false;
            state->reader_paused = false;
            state->stood_aside = false;
            return RUBRAVIEW_BGM_DO_NOTHING;

        case RUBRAVIEW_BGM_PAGE_SOUNDS:
            state->page_sounds = true;
            if (!pause_for_sound || !state->holds_music) return RUBRAVIEW_BGM_DO_NOTHING;
            /* Already paused by the listener: leave it alone, and do not
               pretend the arbiter owes it a resume. */
            if (state->reader_paused || state->stood_aside) return RUBRAVIEW_BGM_DO_NOTHING;
            state->stood_aside = true;
            return RUBRAVIEW_BGM_DO_PAUSE;

        case RUBRAVIEW_BGM_PAGE_QUIET: {
            state->page_sounds = false;
            bool owed = state->stood_aside && state->holds_music && !state->reader_paused;
            state->stood_aside = false;
            return owed ? RUBRAVIEW_BGM_DO_RESUME : RUBRAVIEW_BGM_DO_NOTHING;
        }

        case RUBRAVIEW_BGM_READER_PAUSED:
            state->reader_paused = true;
            /* The listener has taken it over; the arbiter owes nothing. */
            state->stood_aside = false;
            return RUBRAVIEW_BGM_DO_NOTHING;

        case RUBRAVIEW_BGM_READER_RESUMED:
            state->reader_paused = false;
            state->stood_aside = false;
            return RUBRAVIEW_BGM_DO_NOTHING;
    }
    return RUBRAVIEW_BGM_DO_NOTHING;
}
