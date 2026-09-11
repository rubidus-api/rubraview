#include "rubraview/mediaclock.h"
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include <threads.h>

static bool near(double a, double b) { return fabs(a - b) < 1e-9; }

/* ---- the two-thread ring test ---- */

#define STRESS_FRAMES 200000u

typedef struct stress {
    rubraview_spsc_t ring;
    uint32_t slots[RUBRAVIEW_SPSC_MAX_SLOTS];
} stress_t;

static int producer(void *arg) {
    stress_t *s = (stress_t*)arg;
    for (uint32_t value = 1; value <= STRESS_FRAMES; ) {
        size_t slot;
        if (!rubraview_spsc_acquire_write(&s->ring, &slot)) { thrd_yield(); continue; }
        s->slots[slot] = value;          /* the "frame" is written before it is published */
        rubraview_spsc_commit_write(&s->ring);
        value++;
    }
    return 0;
}

int main(void) {
    printf("[test_mediaclock] Starting media clock tests...\n");

    /* Test 1: §5.3 frame scheduling — early frames wait, due frames show,
       frames whose interval has passed are dropped. */
    {
        const double d = 1.0 / 24.0;
        assert(rubraview_media_schedule(1.0, d, 0.5) == RUBRAVIEW_FRAME_WAIT);
        assert(rubraview_media_schedule(1.0, d, 1.0) == RUBRAVIEW_FRAME_SHOW);
        /* Up to half a frame early still shows. */
        assert(rubraview_media_schedule(1.0, d, 1.0 - d * 0.4) == RUBRAVIEW_FRAME_SHOW);
        assert(rubraview_media_schedule(1.0, d, 1.0 - d * 0.6) == RUBRAVIEW_FRAME_WAIT);
        /* Late but still inside its own interval: show it. */
        assert(rubraview_media_schedule(1.0, d, 1.0 + d * 0.9) == RUBRAVIEW_FRAME_SHOW);
        /* Its interval is over: drop it. */
        assert(rubraview_media_schedule(1.0, d, 1.0 + d) == RUBRAVIEW_FRAME_DROP);
        assert(rubraview_media_schedule(1.0, d, 5.0) == RUBRAVIEW_FRAME_DROP);
        /* A zero or negative duration must not make every frame late. */
        assert(rubraview_media_schedule(1.0, 0.0, 1.0) == RUBRAVIEW_FRAME_SHOW);
        assert(rubraview_media_schedule(1.0, -1.0, 1.01) == RUBRAVIEW_FRAME_SHOW);
    }
    printf("  [PASS] Frames wait, show, or drop by their own interval\n");

    /* Test 2: the master clock — audio only when there is audio and a
       device to play it. */
    {
        assert(rubraview_media_master_for(true, true) == RUBRAVIEW_CLOCK_AUDIO);
        assert(rubraview_media_master_for(true, false) == RUBRAVIEW_CLOCK_WALL);  /* no endpoint, as on the test VM */
        assert(rubraview_media_master_for(false, true) == RUBRAVIEW_CLOCK_WALL);  /* a silent file */
        assert(rubraview_media_master_for(false, false) == RUBRAVIEW_CLOCK_WALL);
    }
    printf("  [PASS] Audio is master only when something can be heard\n");

    /* Test 3: the clock runs, pauses, resumes and seeks. */
    {
        rubraview_media_clock_t c = rubraview_media_clock_create(RUBRAVIEW_CLOCK_WALL, 10.0, 100.0);
        assert(near(rubraview_media_clock_now(&c, 100.0), 10.0));
        assert(near(rubraview_media_clock_now(&c, 102.5), 12.5));

        rubraview_media_clock_pause(&c, 103.0);
        assert(near(rubraview_media_clock_now(&c, 110.0), 13.0));   /* frozen while paused */
        rubraview_media_clock_pause(&c, 111.0);                      /* pausing twice changes nothing */
        assert(near(rubraview_media_clock_now(&c, 112.0), 13.0));

        rubraview_media_clock_resume(&c, 120.0);
        assert(near(rubraview_media_clock_now(&c, 121.0), 14.0));    /* the pause is not counted */

        rubraview_media_clock_seek(&c, 42.0, 130.0);
        assert(near(rubraview_media_clock_now(&c, 131.0), 43.0));
        rubraview_media_clock_seek(&c, -5.0, 140.0);
        assert(near(rubraview_media_clock_now(&c, 140.0), 0.0));     /* never before the start */

        /* A wall clock that steps backwards does not run the file backwards. */
        assert(near(rubraview_media_clock_now(&c, 139.0), 0.0));

        /* Seeking while paused stays paused at the new place. */
        rubraview_media_clock_pause(&c, 150.0);
        rubraview_media_clock_seek(&c, 7.0, 151.0);
        assert(near(rubraview_media_clock_now(&c, 160.0), 7.0));
    }
    printf("  [PASS] The clock pauses, resumes and seeks without counting paused time\n");

    /* Test 4: with audio as master the device position wins; a wall
       clock ignores it. */
    {
        rubraview_media_clock_t a = rubraview_media_clock_create(RUBRAVIEW_CLOCK_AUDIO, 0.0, 0.0);
        rubraview_media_clock_sync_audio(&a, 4.9, 5.0);              /* the device is 0.1 s behind */
        assert(near(rubraview_media_clock_now(&a, 6.0), 5.9));

        rubraview_media_clock_t w = rubraview_media_clock_create(RUBRAVIEW_CLOCK_WALL, 0.0, 0.0);
        rubraview_media_clock_sync_audio(&w, 4.9, 5.0);
        assert(near(rubraview_media_clock_now(&w, 6.0), 6.0));
    }
    printf("  [PASS] An audio-master clock follows the device, a wall clock does not\n");

    /* Test 5: the ring on one thread — order, full, empty, capacity. */
    {
        rubraview_spsc_t r;
        assert(!rubraview_spsc_init(&r, 0));
        assert(!rubraview_spsc_init(&r, RUBRAVIEW_SPSC_MAX_SLOTS + 1));
        assert(rubraview_spsc_init(&r, 3));

        size_t slot;
        assert(!rubraview_spsc_peek_read(&r, &slot));                /* empty */
        uint32_t store[3] = {0};
        for (uint32_t v = 1; v <= 3; ++v) {
            assert(rubraview_spsc_acquire_write(&r, &slot));
            store[slot] = v;
            rubraview_spsc_commit_write(&r);
        }
        assert(rubraview_spsc_count(&r) == 3);
        assert(!rubraview_spsc_acquire_write(&r, &slot));            /* full: the producer must wait */

        assert(rubraview_spsc_peek_read(&r, &slot) && store[slot] == 1);
        assert(rubraview_spsc_peek_read(&r, &slot) && store[slot] == 1);  /* peeking does not consume */
        rubraview_spsc_release_read(&r);
        assert(rubraview_spsc_acquire_write(&r, &slot));             /* a released slot is writable again */
        store[slot] = 4;
        rubraview_spsc_commit_write(&r);

        for (uint32_t want = 2; want <= 4; ++want) {
            assert(rubraview_spsc_peek_read(&r, &slot) && store[slot] == want);
            rubraview_spsc_release_read(&r);
        }
        assert(rubraview_spsc_count(&r) == 0);

        rubraview_spsc_reset(&r);
        assert(rubraview_spsc_count(&r) == 0 && rubraview_spsc_acquire_write(&r, &slot) && slot == 0);
    }
    printf("  [PASS] The ring keeps order and refuses to overfill\n");

    /* Test 6: the ring across two threads — every value arrives once, in
       order. A frame published before its bytes were visible would show
       up here as a wrong or repeated value. */
    {
        static stress_t s;
        assert(rubraview_spsc_init(&s.ring, 4));
        thrd_t t;
        assert(thrd_create(&t, producer, &s) == thrd_success);

        uint32_t expect = 1;
        while (expect <= STRESS_FRAMES) {
            size_t slot;
            if (!rubraview_spsc_peek_read(&s.ring, &slot)) { thrd_yield(); continue; }
            assert(s.slots[slot] == expect);
            rubraview_spsc_release_read(&s.ring);
            expect++;
        }
        int result = 1;
        thrd_join(t, &result);
        assert(result == 0 && rubraview_spsc_count(&s.ring) == 0);
    }
    printf("  [PASS] %u frames cross two threads in order, none lost or repeated\n", STRESS_FRAMES);

    /* Test 7: D-9 — preferred backend first, the other as automatic
       fallback, FFmpeg only when its DLLs are present. */
    {
        rubraview_media_backend_t order[2];
        assert(rubraview_media_backend_order(RUBRAVIEW_BACKEND_MEDIA_FOUNDATION, true, order) == 2);
        assert(order[0] == RUBRAVIEW_BACKEND_MEDIA_FOUNDATION && order[1] == RUBRAVIEW_BACKEND_FFMPEG);

        assert(rubraview_media_backend_order(RUBRAVIEW_BACKEND_FFMPEG, true, order) == 2);
        assert(order[0] == RUBRAVIEW_BACKEND_FFMPEG && order[1] == RUBRAVIEW_BACKEND_MEDIA_FOUNDATION);

        /* No FFmpeg DLLs: Media Foundation alone, whatever the setting says. */
        assert(rubraview_media_backend_order(RUBRAVIEW_BACKEND_FFMPEG, false, order) == 1);
        assert(order[0] == RUBRAVIEW_BACKEND_MEDIA_FOUNDATION);
        assert(rubraview_media_backend_order(RUBRAVIEW_BACKEND_MEDIA_FOUNDATION, false, order) == 1);
    }
    printf("  [PASS] Backends are tried preferred-first, FFmpeg only when present\n");

    /* Test 8: why a file failed, with HEVC singled out (D-9). The code
       Media Foundation reported for the HEVC clip on the VM was
       0x43564548 — "HEVC" with the first byte low. */
    {
        assert(RUBRAVIEW_FOURCC('H', 'E', 'V', 'C') == 0x43564548u);
        assert(rubraview_media_is_hevc(0x43564548u));
        assert(rubraview_media_is_hevc(RUBRAVIEW_FOURCC('h', 'v', 'c', '1')));
        assert(rubraview_media_is_hevc(RUBRAVIEW_FOURCC('h', 'e', 'v', '1')));
        assert(!rubraview_media_is_hevc(RUBRAVIEW_FOURCC('H', '2', '6', '4')));

        uint32_t hevc = RUBRAVIEW_FOURCC('H', 'E', 'V', 'C');
        uint32_t theora = RUBRAVIEW_FOURCC('t', 'h', 'e', 'o');
        assert(rubraview_media_classify(false, false, false, 0) == RUBRAVIEW_MEDIA_FAIL_FILE);
        assert(rubraview_media_classify(true, false, false, 0) == RUBRAVIEW_MEDIA_FAIL_CONTAINER);
        assert(rubraview_media_classify(true, true, false, hevc) == RUBRAVIEW_MEDIA_FAIL_HEVC);
        assert(rubraview_media_classify(true, true, false, theora) == RUBRAVIEW_MEDIA_FAIL_CODEC);
        assert(rubraview_media_classify(true, true, true, hevc) == RUBRAVIEW_MEDIA_OPENED);

        /* D-9: the HEVC line names both remedies; every failure says
           something; success says nothing. */
        u8str_t h = rubraview_media_failure_text(RUBRAVIEW_MEDIA_FAIL_HEVC);
        assert(strstr(h.ptr, "$0.99") && strstr(h.ptr, "Microsoft Store") && strstr(h.ptr, "FFmpeg"));
        assert(strlen(h.ptr) == h.len);
        assert(rubraview_media_failure_text(RUBRAVIEW_MEDIA_FAIL_FILE).len > 0);
        assert(rubraview_media_failure_text(RUBRAVIEW_MEDIA_FAIL_CONTAINER).len > 0);
        assert(rubraview_media_failure_text(RUBRAVIEW_MEDIA_FAIL_CODEC).len > 0);
        assert(rubraview_media_failure_text(RUBRAVIEW_MEDIA_OPENED).len == 0);
    }
    printf("  [PASS] Failures are told apart, HEVC is recognised, and its message names both remedies\n");

    /* Test 9: two failures, one report — the more specific wins. */
    {
        assert(rubraview_media_failure_pick(RUBRAVIEW_MEDIA_FAIL_HEVC, RUBRAVIEW_MEDIA_FAIL_CONTAINER) == RUBRAVIEW_MEDIA_FAIL_HEVC);
        assert(rubraview_media_failure_pick(RUBRAVIEW_MEDIA_FAIL_CONTAINER, RUBRAVIEW_MEDIA_FAIL_HEVC) == RUBRAVIEW_MEDIA_FAIL_HEVC);
        assert(rubraview_media_failure_pick(RUBRAVIEW_MEDIA_FAIL_FILE, RUBRAVIEW_MEDIA_FAIL_CODEC) == RUBRAVIEW_MEDIA_FAIL_CODEC);
        assert(rubraview_media_failure_pick(RUBRAVIEW_MEDIA_FAIL_CONTAINER, RUBRAVIEW_MEDIA_FAIL_FILE) == RUBRAVIEW_MEDIA_FAIL_CONTAINER);
    }
    printf("  [PASS] Of two failures, the more specific one is reported\n");

    printf("[test_mediaclock] All tests passed successfully!\n");
    return 0;
}
