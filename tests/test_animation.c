#include "rubraview/animation.h"
#include <stdio.h>
#include <assert.h>
#include <math.h>

static bool approx(double a, double b) { return fabs(a - b) < 1e-9; }

int main(void) {
    printf("[test_animation] Starting animated image and sub-page unit tests...\n");

    const rubraview_frame_t GIF_FRAMES[] = {
        { .delay_seconds = 0.10, .width = 100, .height = 100 },
        { .delay_seconds = 0.20, .width = 100, .height = 100 },
        { .delay_seconds = 0.10, .width = 100, .height = 100 },
    };

    /* Test 1: Frames advance on their own timing and report a loop when
       they wrap (§3.20.1). */
    {
        rubraview_animation_t anim = rubraview_animation_create(RUBRAVIEW_FRAMES_ANIMATION, GIF_FRAMES, 3);
        assert(!anim.paused);
        assert(anim.current == 0);

        assert(rubraview_animation_tick(&anim, 0.05) == RUBRAVIEW_ANIMATION_NONE);
        assert(rubraview_animation_tick(&anim, 0.06) == RUBRAVIEW_ANIMATION_FRAME_CHANGED);
        assert(anim.current == 1);

        assert(rubraview_animation_tick(&anim, 0.25) == RUBRAVIEW_ANIMATION_FRAME_CHANGED);
        assert(anim.current == 2);

        assert(rubraview_animation_tick(&anim, 0.15) == RUBRAVIEW_ANIMATION_LOOPED);
        assert(anim.current == 0);
        assert(anim.loops_done == 1);
    }
    printf("  [PASS] Frames advance on their own delays and report a completed loop\n");

    /* Test 2: A zero delay does not spin the frame loop — GIF writers
       leave it at zero routinely. */
    {
        const rubraview_frame_t ZERO[] = { { .delay_seconds = 0.0 }, { .delay_seconds = 0.0 } };
        rubraview_animation_t anim = rubraview_animation_create(RUBRAVIEW_FRAMES_ANIMATION, ZERO, 2);
        assert(rubraview_animation_tick(&anim, 0.01) == RUBRAVIEW_ANIMATION_NONE);
        assert(rubraview_animation_tick(&anim, 0.10) == RUBRAVIEW_ANIMATION_FRAME_CHANGED);
    }
    printf("  [PASS] A zero frame delay falls back to a sane minimum\n");

    /* Test 3: Pause halts playback; stepping moves one frame and pauses,
       wrapping in both directions (§3.20.1). */
    {
        rubraview_animation_t anim = rubraview_animation_create(RUBRAVIEW_FRAMES_ANIMATION, GIF_FRAMES, 3);
        rubraview_animation_pause(&anim);
        assert(rubraview_animation_tick(&anim, 10.0) == RUBRAVIEW_ANIMATION_NONE);
        assert(anim.current == 0);

        rubraview_animation_step(&anim, true);
        assert(anim.current == 1 && anim.paused);
        rubraview_animation_step(&anim, false);
        assert(anim.current == 0);
        rubraview_animation_step(&anim, false);
        assert(anim.current == 2); /* wraps backwards */

        rubraview_animation_resume(&anim);
        assert(!anim.paused);
    }
    printf("  [PASS] Pause, resume and frame stepping behave, including wrap-around\n");

    /* Test 4: Speed scales the clock, and the ladder stops at both ends
       instead of wrapping from fastest to slowest. */
    {
        assert(approx(rubraview_animation_step_speed(1.0, true), 1.5));
        assert(approx(rubraview_animation_step_speed(1.0, false), 0.5));
        assert(approx(rubraview_animation_step_speed(2.0, true), 2.0));   /* already fastest */
        assert(approx(rubraview_animation_step_speed(0.25, false), 0.25)); /* already slowest */

        rubraview_animation_t anim = rubraview_animation_create(RUBRAVIEW_FRAMES_ANIMATION, GIF_FRAMES, 3);
        anim.speed = 2.0;
        /* At double speed a 0.1 s frame is done in 0.05 s. */
        assert(rubraview_animation_tick(&anim, 0.06) == RUBRAVIEW_ANIMATION_FRAME_CHANGED);
    }
    printf("  [PASS] Speed scales playback and the ladder clamps at both ends\n");

    /* Test 5: §3.2.6 — one full cycle is what the slide show waits for
       before advancing past an animation. */
    {
        rubraview_animation_t anim = rubraview_animation_create(RUBRAVIEW_FRAMES_ANIMATION, GIF_FRAMES, 3);
        assert(approx(rubraview_animation_cycle_seconds(&anim), 0.4));
    }
    printf("  [PASS] The cycle length is the sum of the frame delays\n");

    /* Test 6: §3.20.2 — multi-page TIFF and ICO frames never advance by
       themselves; the reader steps them. */
    {
        const rubraview_frame_t PAGES[] = {
            { .delay_seconds = 0.0, .width = 2480, .height = 3508 },
            { .delay_seconds = 0.0, .width = 2480, .height = 3508 },
        };
        rubraview_animation_t doc = rubraview_animation_create(RUBRAVIEW_FRAMES_SUBPAGES, PAGES, 2);
        assert(doc.paused);
        assert(rubraview_animation_tick(&doc, 100.0) == RUBRAVIEW_ANIMATION_NONE);
        assert(doc.current == 0);

        rubraview_animation_resume(&doc);
        assert(doc.paused); /* resuming a document is meaningless and refused */
        assert(rubraview_animation_tick(&doc, 100.0) == RUBRAVIEW_ANIMATION_NONE);

        rubraview_animation_step(&doc, true);
        assert(doc.current == 1);
        assert(approx(rubraview_animation_cycle_seconds(&doc), 0.0));
    }
    printf("  [PASS] Multi-page containers never advance on their own, only on a step\n");

    /* Test 7: §3.20.2 — an ICO opens at its largest mipmap. */
    {
        const rubraview_frame_t ICO[] = {
            { .delay_seconds = 0.0, .width = 16, .height = 16 },
            { .delay_seconds = 0.0, .width = 256, .height = 256 },
            { .delay_seconds = 0.0, .width = 48, .height = 48 },
        };
        rubraview_animation_t icon = rubraview_animation_create(RUBRAVIEW_FRAMES_SUBPAGES, ICO, 3);
        assert(rubraview_animation_largest_frame(&icon) == 1);
    }
    printf("  [PASS] An icon reports its largest mipmap as the one to show\n");

    /* Test 8: An empty or absent frame list is inert. */
    {
        rubraview_animation_t empty = rubraview_animation_create(RUBRAVIEW_FRAMES_ANIMATION, NULL, 0);
        assert(empty.frame_count == 0);
        assert(rubraview_animation_tick(&empty, 1.0) == RUBRAVIEW_ANIMATION_NONE);
        rubraview_animation_step(&empty, true);
        assert(approx(rubraview_animation_cycle_seconds(&empty), 0.0));
        assert(rubraview_animation_largest_frame(&empty) == 0);
    }
    printf("  [PASS] An empty frame list is inert, no crash\n");

    printf("[test_animation] All tests passed successfully!\n");
    return 0;
}
