#include "rubraview/ui_chrome.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>

static bool approx(double a, double b) { return fabs(a - b) < 1e-9; }

/* The button that rescues a wandered-off floating box has to be
   reachable, and the owner asked for it at the left end of the icon
   row. A control nobody can find is not a rescue. */
static void test_snap_button_placement(void) {
    rubraview_titlebar_t bar = rubraview_titlebar_create(1.0);
    bar.shown = true;
    double win_w = 1280.0;

    rubraview_rect_t snap = rubraview_titlebar_button_rect(&bar, RUBRAVIEW_TITLEBAR_SNAP_BOXES, win_w);
    rubraview_rect_t minimise = rubraview_titlebar_button_rect(&bar, RUBRAVIEW_TITLEBAR_MINIMIZE, win_w);
    rubraview_rect_t close = rubraview_titlebar_button_rect(&bar, RUBRAVIEW_TITLEBAR_CLOSE, win_w);

    assert(snap.width > 0.0);
    assert(snap.x < minimise.x);            /* leftmost of the icons */
    assert(close.x > minimise.x);           /* close stays at the far right */
    assert(snap.x + snap.width <= win_w);

    /* And it is hit where it is drawn. */
    assert(rubraview_titlebar_hit(&bar, snap.x + 2.0, bar.height * 0.5, win_w) == RUBRAVIEW_TITLEBAR_SNAP_BOXES);
    /* Not when the bar is hidden. */
    bar.shown = false;
    assert(rubraview_titlebar_hit(&bar, snap.x + 2.0, bar.height * 0.5, win_w) == RUBRAVIEW_TITLEBAR_NONE);

    printf("  [PASS] The put-the-boxes-back button is leftmost in the icon row and hit where drawn\n");
}

int main(void) {
    printf("[test_ui_chrome] Starting OSD and hover titlebar unit tests...\n");

    /* Test 1: The OSD is opaque while visible, then fades to nothing
       (§3.1: "fades out after 2 seconds of inactivity"). */
    {
        rubraview_osd_t osd = rubraview_osd_create(2.0, 0.5);
        assert(approx(rubraview_osd_opacity(&osd), 1.0));

        rubraview_osd_tick(&osd, 1.9);
        assert(approx(rubraview_osd_opacity(&osd), 1.0));

        rubraview_osd_tick(&osd, 0.35); /* 2.25 s: 0.25 into a 0.5 s fade */
        assert(approx(rubraview_osd_opacity(&osd), 0.5));

        rubraview_osd_tick(&osd, 1.0);
        assert(approx(rubraview_osd_opacity(&osd), 0.0));
    }
    printf("  [PASS] OSD stays opaque, then fades to zero over the fade window\n");

    /* Test 2: Activity restores it immediately. */
    {
        rubraview_osd_t osd = rubraview_osd_create(2.0, 0.5);
        rubraview_osd_tick(&osd, 10.0);
        assert(approx(rubraview_osd_opacity(&osd), 0.0));

        rubraview_osd_notify_activity(&osd);
        assert(approx(rubraview_osd_opacity(&osd), 1.0));
    }
    printf("  [PASS] Any activity restores the OSD to full opacity\n");

    /* Test 3: Pinned open (`I`), it ignores the timer entirely. */
    {
        rubraview_osd_t osd = rubraview_osd_create(2.0, 0.5);
        osd.always_on = true;
        rubraview_osd_tick(&osd, 100.0);
        assert(approx(rubraview_osd_opacity(&osd), 1.0));
    }
    printf("  [PASS] Pinned OSD ignores the fade timer\n");

    /* Test 4: The status line carries name, resolution, zoom and index. */
    {
        char buffer[160];
        u8str_t line = rubraview_osd_format(buffer, sizeof(buffer),
                                            U8("page (2).jpg"), 1920, 1080, 100.0, 1, 12);
        assert(line.len > 0);
        assert(line.ptr[line.len] == '\0');
        assert(strstr(buffer, "page (2).jpg") != NULL);
        assert(strstr(buffer, "1920 x 1080") != NULL);
        assert(strstr(buffer, "100%") != NULL);
        assert(strstr(buffer, "2 / 12") != NULL); /* index is shown 1-based */
    }
    printf("  [PASS] OSD status line carries name, resolution, zoom and 1-based index\n");

    /* Test 5: The status line truncates safely into a small buffer, and
       a non-terminated name slice is not over-read. */
    {
        char small[16];
        u8str_t line = rubraview_osd_format(small, sizeof(small), U8("averylongfilename.jpg"),
                                            4000, 3000, 250.0, 0, 1);
        assert(line.len < sizeof(small));
        assert(small[line.len] == '\0');

        const char *backing = "abcdefXXXX";
        u8str_t slice = { .ptr = backing, .len = 6 }; /* "abcdef", no terminator */
        char buffer[64];
        rubraview_osd_format(buffer, sizeof(buffer), slice, 10, 10, 100.0, 0, 1);
        assert(strstr(buffer, "abcdef") != NULL);
        assert(strstr(buffer, "abcdefX") == NULL); /* stopped at the slice length */
    }
    printf("  [PASS] Status line truncates safely and respects slice length\n");

    /* Test 6: The titlebar reveals when the pointer nears the top edge
       and hides after the grace period once it leaves (§3.21.2). */
    {
        rubraview_titlebar_t bar = rubraview_titlebar_create(1.0);
        assert(approx(bar.height, 36.0) && approx(bar.trigger_zone, 12.0));
        assert(!bar.shown);

        assert(rubraview_titlebar_pointer_moved(&bar, 5.0)); /* inside the trigger band */
        assert(bar.shown);

        /* Still over the revealed bar: it must not start hiding. The
           pointer is re-stated every pass, as the viewer does — a still
           mouse sends no events of its own. */
        for (int i = 0; i < 4; ++i) {
            rubraview_titlebar_pointer_moved(&bar, 20.0);
            assert(!rubraview_titlebar_tick(&bar, 0.1));
        }
        assert(bar.shown);

        /* Pointer moves well below the bar: the countdown starts. */
        rubraview_titlebar_pointer_moved(&bar, 400.0);
        assert(!rubraview_titlebar_tick(&bar, 0.3));
        assert(bar.shown);
        assert(rubraview_titlebar_tick(&bar, 0.3)); /* 0.6 s > 0.5 s delay */
        assert(!bar.shown);
    }
    printf("  [PASS] Titlebar reveals on the top edge and hides after the grace period\n");

    /* Test: the bar stays while the pointer rests on it, and a pointer
       wandering elsewhere does not keep it up. The caller re-states the
       pointer every pass, because a still mouse sends no events. */
    {
        rubraview_titlebar_t bar = rubraview_titlebar_create(1.0);
        rubraview_titlebar_pointer_moved(&bar, 2.0);
        assert(bar.shown);

        for (int i = 0; i < 10; ++i) {                 /* a second of resting on it */
            rubraview_titlebar_pointer_moved(&bar, 2.0);
            assert(!rubraview_titlebar_tick(&bar, 0.1));
        }
        assert(bar.shown);

        bool hidden = false;
        for (int i = 0; i < 10 && !hidden; ++i) {      /* away, still moving about */
            rubraview_titlebar_pointer_moved(&bar, 400.0 + i);
            hidden = rubraview_titlebar_tick(&bar, 0.1);
        }
        assert(hidden && !bar.shown);
    }
    printf("  [PASS] The titlebar stays under a resting pointer and hides once left\n");

    /* Test 7: The control cluster sits at the right edge in the order
       minimize, maximize, fullscreen, close (§3.21.3). */
    {
        rubraview_titlebar_t bar = rubraview_titlebar_create(1.0);
        rubraview_titlebar_pointer_moved(&bar, 2.0);
        double win_w = 1000.0;

        rubraview_rect_t close = rubraview_titlebar_button_rect(&bar, RUBRAVIEW_TITLEBAR_CLOSE, win_w);
        rubraview_rect_t full = rubraview_titlebar_button_rect(&bar, RUBRAVIEW_TITLEBAR_FULLSCREEN, win_w);
        rubraview_rect_t maxb = rubraview_titlebar_button_rect(&bar, RUBRAVIEW_TITLEBAR_MAXIMIZE, win_w);
        rubraview_rect_t minb = rubraview_titlebar_button_rect(&bar, RUBRAVIEW_TITLEBAR_MINIMIZE, win_w);

        assert(approx(close.x + close.width, win_w)); /* close is flush right */
        assert(full.x < close.x && maxb.x < full.x && minb.x < maxb.x);
        assert(approx(close.height, bar.height));

        assert(rubraview_titlebar_hit(&bar, close.x + 1.0, 5.0, win_w) == RUBRAVIEW_TITLEBAR_CLOSE);
        assert(rubraview_titlebar_hit(&bar, minb.x + 1.0, 5.0, win_w) == RUBRAVIEW_TITLEBAR_MINIMIZE);
        assert(rubraview_titlebar_hit(&bar, 20.0, 5.0, win_w) == RUBRAVIEW_TITLEBAR_CAPTION);
    }
    printf("  [PASS] Window controls sit right-aligned in order and hit-test correctly\n");

    /* Test 8: Nothing is hit while the bar is hidden, or below it. */
    {
        rubraview_titlebar_t bar = rubraview_titlebar_create(1.0);
        assert(rubraview_titlebar_hit(&bar, 990.0, 5.0, 1000.0) == RUBRAVIEW_TITLEBAR_NONE);

        rubraview_titlebar_pointer_moved(&bar, 2.0);
        assert(rubraview_titlebar_hit(&bar, 990.0, 500.0, 1000.0) == RUBRAVIEW_TITLEBAR_NONE);
    }
    printf("  [PASS] A hidden titlebar and points below it hit nothing\n");

    /* Test 9: Titlebar metrics scale with DPI (§3.21.2, §4.2). */
    {
        rubraview_titlebar_t bar = rubraview_titlebar_create(1.5);
        assert(approx(bar.height, 54.0));
        assert(approx(bar.trigger_zone, 18.0));
    }
    printf("  [PASS] Titlebar metrics scale with DPI\n");

    /* Test 10: A short slide interval forces the instant cut (§3.2.5),
       however the transition is configured. */
    {
        assert(rubraview_transition_for_interval(RUBRAVIEW_TRANSITION_CROSSFADE, 0.2, 0.5) == RUBRAVIEW_TRANSITION_CUT);
        assert(rubraview_transition_for_interval(RUBRAVIEW_TRANSITION_CROSSFADE, 3.0, 0.5) == RUBRAVIEW_TRANSITION_CROSSFADE);
    }
    printf("  [PASS] Intervals below the threshold force an instant cut\n");

    /* Test 11: A cross-fade runs from 0 to 1 and then stops. */
    {
        rubraview_transition_t t = rubraview_transition_create(RUBRAVIEW_TRANSITION_CROSSFADE, 0.4);
        rubraview_transition_start(&t);
        assert(rubraview_transition_active(&t));
        assert(approx(rubraview_transition_progress(&t), 0.0));

        rubraview_transition_tick(&t, 0.2);
        assert(approx(rubraview_transition_progress(&t), 0.5));

        rubraview_transition_tick(&t, 0.5); /* overshoot clamps at 1.0 */
        assert(approx(rubraview_transition_progress(&t), 1.0));
        assert(!rubraview_transition_active(&t));
    }
    printf("  [PASS] A cross-fade runs 0 to 1 and clamps when it finishes\n");

    /* Test 12: An instant cut is never "in progress". */
    {
        rubraview_transition_t t = rubraview_transition_create(RUBRAVIEW_TRANSITION_CUT, 0.4);
        rubraview_transition_start(&t);
        assert(!rubraview_transition_active(&t));
        assert(approx(rubraview_transition_progress(&t), 1.0));
    }
    printf("  [PASS] An instant cut is never in progress\n");

    /* Test 13: The cursor hides after the idle delay and any motion
       brings it straight back (§3.2.5). */
    {
        rubraview_cursor_hide_t cursor = rubraview_cursor_hide_create(1.5);
        assert(!cursor.hidden);

        assert(!rubraview_cursor_hide_tick(&cursor, 1.0));
        assert(!cursor.hidden);
        assert(rubraview_cursor_hide_tick(&cursor, 0.6)); /* 1.6 s > 1.5 s */
        assert(cursor.hidden);

        assert(rubraview_cursor_hide_notify_motion(&cursor)); /* reported the change */
        assert(!cursor.hidden);
        assert(!rubraview_cursor_hide_notify_motion(&cursor)); /* already visible */
    }
    printf("  [PASS] Cursor hides after the idle delay and returns on motion\n");

    test_snap_button_placement();

    printf("[test_ui_chrome] All tests passed successfully!\n");
    return 0;
}
