#include "rubraview/slideshow.h"
#include <stdio.h>
#include <assert.h>
#include <math.h>

static bool approx(double a, double b) { return fabs(a - b) < 1e-9; }

int main(void) {
    printf("[test_slideshow] Starting slide-show sequencer state machine unit tests...\n");

    /* Test 1: Interval is clamped to [0.1, 300.0] at creation and on set. */
    {
        rubraview_slideshow_t s_low = rubraview_slideshow_create(3, 0.0, RUBRAVIEW_LOOP_ALL);
        assert(approx(s_low.interval_seconds, 0.1));
        rubraview_slideshow_t s_high = rubraview_slideshow_create(3, 9999.0, RUBRAVIEW_LOOP_ALL);
        assert(approx(s_high.interval_seconds, 300.0));

        rubraview_slideshow_t s = rubraview_slideshow_create(3, 3.0, RUBRAVIEW_LOOP_ALL);
        rubraview_slideshow_set_interval(&s, -5.0);
        assert(approx(s.interval_seconds, 0.1));
        rubraview_slideshow_set_interval(&s, 1000.0);
        assert(approx(s.interval_seconds, 300.0));
        rubraview_slideshow_set_interval(&s, 5.5);
        assert(approx(s.interval_seconds, 5.5));
    }
    printf("  [PASS] Interval clamped to [0.1, 300.0] on create and set_interval\n");

    /* Test 2: Required dwell per media kind. */
    {
        rubraview_slideshow_item_t still = { .kind = RUBRAVIEW_MEDIA_STILL, .duration_seconds = 0.0 };
        assert(approx(rubraview_slideshow_required_dwell(&still, 3.0), 3.0));

        rubraview_slideshow_item_t short_anim = { .kind = RUBRAVIEW_MEDIA_ANIMATED, .duration_seconds = 1.0 };
        assert(approx(rubraview_slideshow_required_dwell(&short_anim, 3.0), 3.0)); /* interval wins: not cut short */

        rubraview_slideshow_item_t long_anim = { .kind = RUBRAVIEW_MEDIA_ANIMATED, .duration_seconds = 5.0 };
        assert(approx(rubraview_slideshow_required_dwell(&long_anim, 3.0), 5.0)); /* cycle wins: never cut mid-cycle */

        rubraview_slideshow_item_t video = { .kind = RUBRAVIEW_MEDIA_VIDEO, .duration_seconds = 45.0 };
        assert(approx(rubraview_slideshow_required_dwell(&video, 3.0), 45.0)); /* plays in full, ignoring the interval */
    }
    printf("  [PASS] Required dwell: still=interval, animated=max(interval,cycle), video=full length\n");

    /* Test 3: Before dwell elapses, tick reports NONE and does not advance. */
    {
        rubraview_slideshow_item_t items[3] = {
            { .kind = RUBRAVIEW_MEDIA_STILL, .duration_seconds = 0 },
            { .kind = RUBRAVIEW_MEDIA_STILL, .duration_seconds = 0 },
            { .kind = RUBRAVIEW_MEDIA_STILL, .duration_seconds = 0 },
        };
        rubraview_slideshow_t s = rubraview_slideshow_create(3, 3.0, RUBRAVIEW_LOOP_ALL);
        rubraview_slideshow_event_t e = rubraview_slideshow_tick(&s, items, 1.0);
        assert(e == RUBRAVIEW_SLIDESHOW_NONE);
        assert(s.current_index == 0);
    }
    printf("  [PASS] Tick before dwell elapses returns NONE, no advance\n");

    /* Test 4: Once dwell elapses, tick advances and resets elapsed. */
    {
        rubraview_slideshow_item_t items[3] = {
            { .kind = RUBRAVIEW_MEDIA_STILL, .duration_seconds = 0 },
            { .kind = RUBRAVIEW_MEDIA_STILL, .duration_seconds = 0 },
            { .kind = RUBRAVIEW_MEDIA_STILL, .duration_seconds = 0 },
        };
        rubraview_slideshow_t s = rubraview_slideshow_create(3, 3.0, RUBRAVIEW_LOOP_ALL);
        rubraview_slideshow_tick(&s, items, 2.0);
        rubraview_slideshow_event_t e = rubraview_slideshow_tick(&s, items, 1.5); /* total 3.5 >= 3.0 */
        assert(e == RUBRAVIEW_SLIDESHOW_ADVANCED);
        assert(s.current_index == 1);
        assert(approx(s.elapsed_in_item, 0.0));
    }
    printf("  [PASS] Tick past dwell advances to the next item and resets elapsed time\n");

    /* Test 5: LOOP_ALL wraps back to the first item at the end. */
    {
        rubraview_slideshow_item_t items[2] = {
            { .kind = RUBRAVIEW_MEDIA_STILL, .duration_seconds = 0 },
            { .kind = RUBRAVIEW_MEDIA_STILL, .duration_seconds = 0 },
        };
        rubraview_slideshow_t s = rubraview_slideshow_create(2, 1.0, RUBRAVIEW_LOOP_ALL);
        rubraview_slideshow_tick(&s, items, 1.0); /* -> index 1 */
        assert(s.current_index == 1);
        rubraview_slideshow_event_t e = rubraview_slideshow_tick(&s, items, 1.0); /* wraps */
        assert(e == RUBRAVIEW_SLIDESHOW_ADVANCED);
        assert(s.current_index == 0);
    }
    printf("  [PASS] LOOP_ALL wraps back to the first item\n");

    /* Test 6: LOOP_PLAY_ONCE stops and pauses on the last item; further
       ticks are no-ops. */
    {
        rubraview_slideshow_item_t items[2] = {
            { .kind = RUBRAVIEW_MEDIA_STILL, .duration_seconds = 0 },
            { .kind = RUBRAVIEW_MEDIA_STILL, .duration_seconds = 0 },
        };
        rubraview_slideshow_t s = rubraview_slideshow_create(2, 1.0, RUBRAVIEW_LOOP_PLAY_ONCE);
        rubraview_slideshow_tick(&s, items, 1.0); /* -> index 1 */
        rubraview_slideshow_event_t e = rubraview_slideshow_tick(&s, items, 1.0); /* end reached */
        assert(e == RUBRAVIEW_SLIDESHOW_ENDED);
        assert(s.current_index == 1);
        assert(s.paused == true);

        rubraview_slideshow_event_t after = rubraview_slideshow_tick(&s, items, 100.0);
        assert(after == RUBRAVIEW_SLIDESHOW_NONE);
        assert(s.current_index == 1); /* still parked on the last item */
    }
    printf("  [PASS] LOOP_PLAY_ONCE ends and pauses on the last item\n");

    /* Test 7: LOOP_REPEAT_SINGLE stays on the same item but restarts its
       dwell timer each cycle. */
    {
        rubraview_slideshow_item_t items[2] = {
            { .kind = RUBRAVIEW_MEDIA_STILL, .duration_seconds = 0 },
            { .kind = RUBRAVIEW_MEDIA_STILL, .duration_seconds = 0 },
        };
        rubraview_slideshow_t s = rubraview_slideshow_create(2, 1.0, RUBRAVIEW_LOOP_REPEAT_SINGLE);
        rubraview_slideshow_event_t e1 = rubraview_slideshow_tick(&s, items, 1.0);
        assert(e1 == RUBRAVIEW_SLIDESHOW_ADVANCED);
        assert(s.current_index == 0); /* unchanged */
        rubraview_slideshow_event_t e2 = rubraview_slideshow_tick(&s, items, 1.0);
        assert(e2 == RUBRAVIEW_SLIDESHOW_ADVANCED);
        assert(s.current_index == 0); /* still unchanged, cycles indefinitely */
    }
    printf("  [PASS] LOOP_REPEAT_SINGLE stays on one item, restarting its dwell timer\n");

    /* Test 8: Pausing halts advancement; resuming lets it continue from
       where it left off. */
    {
        rubraview_slideshow_item_t items[2] = {
            { .kind = RUBRAVIEW_MEDIA_STILL, .duration_seconds = 0 },
            { .kind = RUBRAVIEW_MEDIA_STILL, .duration_seconds = 0 },
        };
        rubraview_slideshow_t s = rubraview_slideshow_create(2, 1.0, RUBRAVIEW_LOOP_ALL);
        rubraview_slideshow_tick(&s, items, 0.5);
        rubraview_slideshow_pause(&s);
        rubraview_slideshow_event_t paused_e = rubraview_slideshow_tick(&s, items, 10.0);
        assert(paused_e == RUBRAVIEW_SLIDESHOW_NONE);
        assert(s.current_index == 0);

        rubraview_slideshow_resume(&s);
        rubraview_slideshow_event_t resumed_e = rubraview_slideshow_tick(&s, items, 0.6); /* 0.5+0.6=1.1 >= 1.0 */
        assert(resumed_e == RUBRAVIEW_SLIDESHOW_ADVANCED);
    }
    printf("  [PASS] Pause halts advancement; resume continues from the accumulated elapsed time\n");

    /* Test 9: A mixed-media sequence — the video item's own duration
       governs its dwell regardless of a much shorter slide interval. */
    {
        rubraview_slideshow_item_t items[2] = {
            { .kind = RUBRAVIEW_MEDIA_VIDEO, .duration_seconds = 10.0 },
            { .kind = RUBRAVIEW_MEDIA_STILL, .duration_seconds = 0 },
        };
        rubraview_slideshow_t s = rubraview_slideshow_create(2, 0.5, RUBRAVIEW_LOOP_PLAY_ONCE);
        rubraview_slideshow_event_t early = rubraview_slideshow_tick(&s, items, 1.0); /* well past the 0.5s interval */
        assert(early == RUBRAVIEW_SLIDESHOW_NONE); /* but the video isn't done yet */
        assert(s.current_index == 0);

        rubraview_slideshow_event_t after_full = rubraview_slideshow_tick(&s, items, 9.5); /* total 10.5 >= 10.0 */
        assert(after_full == RUBRAVIEW_SLIDESHOW_ADVANCED);
        assert(s.current_index == 1);
    }
    printf("  [PASS] Mixed-media sequence: video dwell overrides a shorter slide interval\n");

    /* Test 10: A zero-item sequence never crashes and always reports NONE. */
    {
        rubraview_slideshow_t s = rubraview_slideshow_create(0, 3.0, RUBRAVIEW_LOOP_ALL);
        rubraview_slideshow_event_t e = rubraview_slideshow_tick(&s, NULL, 5.0);
        assert(e == RUBRAVIEW_SLIDESHOW_NONE);
    }
    printf("  [PASS] Zero-item sequence handled without crashing\n");

    printf("[test_slideshow] All tests passed successfully!\n");
    return 0;
}
