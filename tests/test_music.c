/* One track running into the next (owner, 2026-09-24, RV-075). The
   timing is the whole of it, and timing is what fails quietly: a track
   opened too late is a gap, one opened too early is memory held for
   nothing, and a fade that dips in the middle is the difference between
   a player and a toy. So the deciding is a function, and it is checked
   here rather than by listening. */
#include "rubraview/music.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static bool near(double a, double b) { return fabs(a - b) < 0.0005; }

int main(void) {
    printf("[test_music] Starting track transition tests...\n");

    /* 1. Gapless with no overlap: nothing until the very end, and then
          the next track whole. */
    {
        rubraview_track_change_t how = { .gapless = true, .crossfade_seconds = 0.0 };
        rubraview_track_plan_t early = rubraview_track_plan(how, 10.0, 180.0, true);
        assert(!early.open_next && !early.start_next && !early.close_current);
        assert(near(early.gain_current, 1.0) && near(early.gain_next, 0.0));

        /* Opened two seconds out, so it is ready when it is wanted. */
        assert(!rubraview_track_plan(how, 177.9, 180.0, true).open_next);
        assert(rubraview_track_plan(how, 178.1, 180.0, true).open_next);
        assert(!rubraview_track_plan(how, 179.9, 180.0, true).start_next);

        rubraview_track_plan_t end = rubraview_track_plan(how, 180.0, 180.0, true);
        assert(end.start_next && end.close_current);
        assert(near(end.gain_current, 0.0) && near(end.gain_next, 1.0));
    }
    printf("  [PASS] With no crossfade the next track is ready early and starts at the end\n");

    /* 2. A crossfade: the overlap begins a crossfade out, and the two
          gains keep the loudness steady all the way through. */
    {
        rubraview_track_change_t how = { .gapless = true, .crossfade_seconds = 4.0 };
        assert(!rubraview_track_plan(how, 173.9, 180.0, true).open_next);   /* 4 + 2 out */
        assert(rubraview_track_plan(how, 174.1, 180.0, true).open_next);
        assert(!rubraview_track_plan(how, 175.9, 180.0, true).start_next);
        assert(rubraview_track_plan(how, 176.1, 180.0, true).start_next);

        double last_current = 2.0, last_next = -1.0;
        for (double at = 176.0; at <= 180.0; at += 0.25) {
            rubraview_track_plan_t p = rubraview_track_plan(how, at, 180.0, true);
            assert(p.start_next);
            /* Equal power: the two together are as loud as either alone. */
            assert(near(p.gain_current * p.gain_current + p.gain_next * p.gain_next, 1.0));
            /* One only ever falls, the other only ever rises. */
            assert(p.gain_current <= last_current + 0.0001);
            assert(p.gain_next >= last_next - 0.0001);
            last_current = p.gain_current;
            last_next = p.gain_next;
        }
        rubraview_track_plan_t start = rubraview_track_plan(how, 176.0, 180.0, true);
        assert(near(start.gain_current, 1.0) && near(start.gain_next, 0.0));
        rubraview_track_plan_t mid = rubraview_track_plan(how, 178.0, 180.0, true);
        assert(near(mid.gain_current, mid.gain_next));      /* halfway, the two are equal */
        assert(mid.gain_current > 0.7 && mid.gain_current < 0.71);   /* and not a half: 1/root 2 */
        rubraview_track_plan_t done = rubraview_track_plan(how, 180.0, 180.0, true);
        assert(near(done.gain_current, 0.0) && near(done.gain_next, 1.0) && done.close_current);
    }
    printf("  [PASS] A crossfade overlaps on an equal-power curve, never dipping\n");

    /* 3. Nothing to run into, or the reader does not want it. */
    {
        rubraview_track_change_t how = { .gapless = true, .crossfade_seconds = 4.0 };
        rubraview_track_plan_t last = rubraview_track_plan(how, 179.0, 180.0, false);
        assert(!last.open_next && !last.start_next);
        assert(near(last.gain_current, 1.0) && near(last.gain_next, 0.0));

        rubraview_track_change_t off = { .gapless = false, .crossfade_seconds = 4.0 };
        rubraview_track_plan_t plain = rubraview_track_plan(off, 179.0, 180.0, true);
        assert(!plain.open_next && !plain.start_next);
        assert(rubraview_track_plan(off, 180.0, 180.0, true).close_current);
    }
    printf("  [PASS] The last track, and a reader who wants none of it, are left alone\n");

    /* 4. What the files themselves do wrong. */
    {
        rubraview_track_change_t how = { .gapless = true, .crossfade_seconds = 4.0 };
        /* A file that does not say how long it is: nothing is planned,
           because a transition can only be timed against a known end. */
        rubraview_track_plan_t unknown = rubraview_track_plan(how, 10.0, 0.0, true);
        assert(!unknown.open_next && !unknown.start_next && !unknown.close_current);

        /* A crossfade longer than the track: never more than half of it,
           so a short track is still heard by itself. */
        rubraview_track_change_t long_fade = { .gapless = true, .crossfade_seconds = 5.0 };
        assert(!rubraview_track_plan(long_fade, 0.0, 3.0, true).start_next);
        assert(!rubraview_track_plan(long_fade, 1.4, 3.0, true).start_next);
        assert(rubraview_track_plan(long_fade, 1.6, 3.0, true).start_next);
        assert(rubraview_track_plan(long_fade, 3.0, 3.0, true).start_next);

        /* Past the end — the clock overshoots by a pass — is the end. */
        rubraview_track_plan_t over = rubraview_track_plan(how, 181.0, 180.0, true);
        assert(over.close_current && over.start_next);
        assert(near(over.gain_current, 0.0) && near(over.gain_next, 1.0));

        /* A crossfade out of range is brought back into it rather than
           believed. */
        rubraview_track_change_t silly = { .gapless = true, .crossfade_seconds = 500.0 };
        assert(rubraview_track_plan(silly, 176.0, 180.0, true).start_next);   /* clamped to 5 */
        rubraview_track_change_t negative = { .gapless = true, .crossfade_seconds = -3.0 };
        assert(!rubraview_track_plan(negative, 179.0, 180.0, true).start_next);
    }
    printf("  [PASS] An unknown length, a fade longer than the track, and silly numbers\n");

    /* 5. The curve itself, at the points a reader would name. */
    {
        double c = 0.0, n = 0.0;
        rubraview_crossfade_gains(0.0, &c, &n);
        assert(near(c, 1.0) && near(n, 0.0));
        rubraview_crossfade_gains(1.0, &c, &n);
        assert(near(c, 0.0) && near(n, 1.0));
        rubraview_crossfade_gains(0.5, &c, &n);
        assert(near(c, n) && near(c * c + n * n, 1.0));
        rubraview_crossfade_gains(-1.0, &c, &n);
        assert(near(c, 1.0));            /* out of range is brought back in */
        rubraview_crossfade_gains(2.0, &c, &n);
        assert(near(n, 1.0));
        rubraview_crossfade_gains(0.25, &c, NULL);   /* either half may be left out */
        assert(c > 0.92 && c < 0.93);
    }
    printf("  [PASS] The crossfade curve is equal-power at every point asked of it\n");


    /* 6. Who gets the speakers (RV-081). The round trip, and the rule
          every player gets wrong: the listener's own pause outranks the
          arbiter, so music stopped by hand does not come back to life
          because a film ended. */
    {
        rubraview_bgm_t bgm = {0};
        assert(rubraview_bgm_event(&bgm, RUBRAVIEW_BGM_MUSIC_OPENED, true) == RUBRAVIEW_BGM_DO_NOTHING);

        /* A film with sound: the music stands aside, and is owed a resume. */
        assert(rubraview_bgm_event(&bgm, RUBRAVIEW_BGM_PAGE_SOUNDS, true) == RUBRAVIEW_BGM_DO_PAUSE);
        /* Another page with sound before the first is gone does not pause
           twice, and does not owe two resumes. */
        assert(rubraview_bgm_event(&bgm, RUBRAVIEW_BGM_PAGE_SOUNDS, true) == RUBRAVIEW_BGM_DO_NOTHING);
        assert(rubraview_bgm_event(&bgm, RUBRAVIEW_BGM_PAGE_QUIET, true) == RUBRAVIEW_BGM_DO_RESUME);
        assert(rubraview_bgm_event(&bgm, RUBRAVIEW_BGM_PAGE_QUIET, true) == RUBRAVIEW_BGM_DO_NOTHING);
    }
    {
        /* The listener stopped it: a film comes and goes, and it stays stopped. */
        rubraview_bgm_t bgm = {0};
        (void)rubraview_bgm_event(&bgm, RUBRAVIEW_BGM_MUSIC_OPENED, true);
        assert(rubraview_bgm_event(&bgm, RUBRAVIEW_BGM_READER_PAUSED, true) == RUBRAVIEW_BGM_DO_NOTHING);
        assert(rubraview_bgm_event(&bgm, RUBRAVIEW_BGM_PAGE_SOUNDS, true) == RUBRAVIEW_BGM_DO_NOTHING);
        assert(rubraview_bgm_event(&bgm, RUBRAVIEW_BGM_PAGE_QUIET, true) == RUBRAVIEW_BGM_DO_NOTHING);

        /* And once the listener starts it again, the arbiter is back in
           charge from there. */
        assert(rubraview_bgm_event(&bgm, RUBRAVIEW_BGM_READER_RESUMED, true) == RUBRAVIEW_BGM_DO_NOTHING);
        assert(rubraview_bgm_event(&bgm, RUBRAVIEW_BGM_PAGE_SOUNDS, true) == RUBRAVIEW_BGM_DO_PAUSE);
        assert(rubraview_bgm_event(&bgm, RUBRAVIEW_BGM_PAGE_QUIET, true) == RUBRAVIEW_BGM_DO_RESUME);
    }
    {
        /* Music opened while a film is already sounding stands aside at
           once rather than talking over it. */
        rubraview_bgm_t bgm = {0};
        assert(rubraview_bgm_event(&bgm, RUBRAVIEW_BGM_PAGE_SOUNDS, true) == RUBRAVIEW_BGM_DO_NOTHING);
        assert(rubraview_bgm_event(&bgm, RUBRAVIEW_BGM_MUSIC_OPENED, true) == RUBRAVIEW_BGM_DO_PAUSE);
        assert(rubraview_bgm_event(&bgm, RUBRAVIEW_BGM_PAGE_QUIET, true) == RUBRAVIEW_BGM_DO_RESUME);

        /* Music that has gone is not paused or resumed. */
        assert(rubraview_bgm_event(&bgm, RUBRAVIEW_BGM_MUSIC_CLOSED, true) == RUBRAVIEW_BGM_DO_NOTHING);
        assert(rubraview_bgm_event(&bgm, RUBRAVIEW_BGM_PAGE_SOUNDS, true) == RUBRAVIEW_BGM_DO_NOTHING);
        assert(rubraview_bgm_event(&bgm, RUBRAVIEW_BGM_PAGE_QUIET, true) == RUBRAVIEW_BGM_DO_NOTHING);
    }
    {
        /* With the setting off the two simply play together. */
        rubraview_bgm_t bgm = {0};
        (void)rubraview_bgm_event(&bgm, RUBRAVIEW_BGM_MUSIC_OPENED, false);
        assert(rubraview_bgm_event(&bgm, RUBRAVIEW_BGM_PAGE_SOUNDS, false) == RUBRAVIEW_BGM_DO_NOTHING);
        assert(rubraview_bgm_event(&bgm, RUBRAVIEW_BGM_PAGE_QUIET, false) == RUBRAVIEW_BGM_DO_NOTHING);

        assert(rubraview_bgm_event(NULL, RUBRAVIEW_BGM_PAGE_SOUNDS, true) == RUBRAVIEW_BGM_DO_NOTHING);
    }
    printf("  [PASS] The music stands aside for a film, and the listener's own pause outranks that\n");

    printf("[test_music] All tests passed successfully!\n");
    return 0;
}
