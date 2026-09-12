#include "rubraview/playback.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>

static u8str_t lit(const char *s) { return (u8str_t){ .ptr = s, .len = strlen(s) }; }
static bool is(u8str_t s, const char *l) { return s.len == strlen(l) && memcmp(s.ptr, l, s.len) == 0; }
static bool near(double a, double b) { return fabs(a - b) < 0.0001; }

int main(void) {
    printf("[test_playback] Starting playback model tests...\n");

    /* Test 1: §5.5's A-B loop, including the two points set backwards. */
    {
        rubraview_ab_loop_t loop = rubraview_ab_create();
        assert(!loop.active);

        rubraview_ab_set_a(&loop, 10.0);
        assert(!loop.active);   /* one point is not a loop */

        rubraview_ab_set_b(&loop, 20.0);
        assert(loop.active && near(loop.point_a, 10.0) && near(loop.point_b, 20.0));

        /* Setting B before A means they were set the other way round;
           the region between them is plainly what was meant. */
        rubraview_ab_loop_t backwards = rubraview_ab_create();
        rubraview_ab_set_a(&backwards, 30.0);
        rubraview_ab_set_b(&backwards, 5.0);
        assert(backwards.active);
        assert(near(backwards.point_a, 5.0) && near(backwards.point_b, 30.0));

        rubraview_ab_clear(&loop);
        assert(!loop.active);
    }
    printf("  [PASS] A-B points make a loop, and setting them backwards still works\n");

    /* Test 2: the loop wraps at B, and pulls back a clock that is
       somehow before A. */
    {
        rubraview_ab_loop_t loop = rubraview_ab_create();
        rubraview_ab_set_a(&loop, 10.0);
        rubraview_ab_set_b(&loop, 20.0);

        assert(rubraview_ab_wrap(&loop, 15.0) < 0.0);          /* inside: carry on */
        assert(near(rubraview_ab_wrap(&loop, 20.0), 10.0));    /* at B: jump to A */
        assert(near(rubraview_ab_wrap(&loop, 25.0), 10.0));    /* past B */
        assert(near(rubraview_ab_wrap(&loop, 3.0), 10.0));     /* before A */

        rubraview_ab_loop_t off = rubraview_ab_create();
        assert(rubraview_ab_wrap(&off, 999.0) < 0.0);
    }
    printf("  [PASS] The loop sends playback back to A, and never interferes when off\n");

    /* Test 3: the seek bar's bracketed region. */
    {
        rubraview_ab_loop_t loop = rubraview_ab_create();
        rubraview_ab_set_a(&loop, 30.0);
        rubraview_ab_set_b(&loop, 60.0);

        double start = 0.0, end = 0.0;
        assert(rubraview_ab_bar_region(&loop, 120.0, &start, &end));
        assert(near(start, 0.25) && near(end, 0.5));

        assert(!rubraview_ab_bar_region(&loop, 0.0, &start, &end));
    }
    printf("  [PASS] The loop's brackets land in the right place on the bar\n");

    /* Test 4: seeking stays inside the file. */
    {
        assert(near(rubraview_seek_target(50.0, 100.0, NULL, RUBRAVIEW_SEEK_RELATIVE, 10.0, 0.0), 60.0));
        assert(near(rubraview_seek_target(50.0, 100.0, NULL, RUBRAVIEW_SEEK_RELATIVE, -60.0, 0.0), 0.0));
        assert(near(rubraview_seek_target(50.0, 100.0, NULL, RUBRAVIEW_SEEK_RELATIVE, 500.0, 0.0), 100.0));
        assert(near(rubraview_seek_target(50.0, 100.0, NULL, RUBRAVIEW_SEEK_ABSOLUTE, 12.5, 0.0), 12.5));
    }
    printf("  [PASS] A seek is clamped into the file\n");

    /* Test 5: a seek with a loop set stays inside the loop. Letting it
       out would silently cancel something the reader set up. */
    {
        rubraview_ab_loop_t loop = rubraview_ab_create();
        rubraview_ab_set_a(&loop, 40.0);
        rubraview_ab_set_b(&loop, 50.0);

        assert(near(rubraview_seek_target(45.0, 100.0, &loop, RUBRAVIEW_SEEK_RELATIVE, 30.0, 0.0), 50.0));
        assert(near(rubraview_seek_target(45.0, 100.0, &loop, RUBRAVIEW_SEEK_RELATIVE, -30.0, 0.0), 40.0));
        assert(near(rubraview_seek_target(45.0, 100.0, &loop, RUBRAVIEW_SEEK_ABSOLUTE, 0.0, 0.0), 40.0));
    }
    printf("  [PASS] A seek inside a loop cannot leave it\n");

    /* Test 6: §5.3's frame step, including a file that does not state
       its rate — the step key must still move. */
    {
        double one = rubraview_seek_target(10.0, 100.0, NULL, RUBRAVIEW_SEEK_FRAME, 1.0, 25.0);
        assert(near(one, 10.04));

        double back = rubraview_seek_target(10.0, 100.0, NULL, RUBRAVIEW_SEEK_FRAME, -1.0, 25.0);
        assert(near(back, 9.96));

        double unknown = rubraview_seek_target(10.0, 100.0, NULL, RUBRAVIEW_SEEK_FRAME, 1.0, 0.0);
        assert(unknown > 10.0);   /* it moved, rather than dividing by zero */
    }
    printf("  [PASS] A frame step moves, even when the file does not state its rate\n");

    /* Test 7: the seek bar's geometry, both ways. */
    {
        assert(near(rubraview_seekbar_time(100.0, 400.0, 200.0, 60.0), 15.0));
        assert(near(rubraview_seekbar_time(100.0, 400.0, 50.0, 60.0), 0.0));    /* left of the bar */
        assert(near(rubraview_seekbar_time(100.0, 400.0, 900.0, 60.0), 60.0));  /* right of it */
        assert(near(rubraview_seekbar_fraction(15.0, 60.0), 0.25));
        assert(near(rubraview_seekbar_fraction(15.0, 0.0), 0.0));               /* no duration yet */
    }
    printf("  [PASS] The seek bar maps position to time and back\n");

    /* Test 8: the timecode the HUD shows. */
    {
        char buffer[32];
        assert(is(rubraview_format_timecode(buffer, sizeof(buffer), 74.2, true), "01:14.200"));
        assert(is(rubraview_format_timecode(buffer, sizeof(buffer), 74.2, false), "01:14"));
        assert(is(rubraview_format_timecode(buffer, sizeof(buffer), 3725.5, true), "1:02:05.500"));
        assert(is(rubraview_format_timecode(buffer, sizeof(buffer), 0.0, true), "00:00.000"));
        assert(is(rubraview_format_timecode(buffer, sizeof(buffer), -5.0, false), "00:00"));
    }
    printf("  [PASS] Timecodes drop the hours field when there are none\n");

    /* Test 9: §3.16.2's track choice — language first, then the
       container's default, then whatever is there. */
    {
        rubraview_track_set_t set = rubraview_tracks_create();
        rubraview_tracks_add(&set, (rubraview_track_t){
            .kind = RUBRAVIEW_TRACK_VIDEO, .stream_index = 0, .codec = lit("h264") });
        rubraview_tracks_add(&set, (rubraview_track_t){
            .kind = RUBRAVIEW_TRACK_AUDIO, .stream_index = 1, .language = lit("jpn"),
            .codec = lit("flac"), .channels = 6, .is_default = true });
        rubraview_tracks_add(&set, (rubraview_track_t){
            .kind = RUBRAVIEW_TRACK_AUDIO, .stream_index = 2, .language = lit("kor"),
            .codec = lit("aac"), .channels = 2 });
        rubraview_tracks_add(&set, (rubraview_track_t){
            .kind = RUBRAVIEW_TRACK_SUBTITLE, .stream_index = 3, .language = lit("eng"),
            .codec = lit("subrip"), .is_forced = true });
        rubraview_tracks_add(&set, (rubraview_track_t){
            .kind = RUBRAVIEW_TRACK_SUBTITLE, .stream_index = 4, .language = lit("kor"),
            .codec = lit("ass") });

        /* A preference wins. */
        assert(rubraview_tracks_choose(&set, RUBRAVIEW_TRACK_AUDIO, lit("kor")) == 2);
        /* "ko" and "kor" mean the same language. */
        assert(rubraview_tracks_choose(&set, RUBRAVIEW_TRACK_AUDIO, lit("ko")) == 2);
        /* With no preference, the container's default. */
        assert(rubraview_tracks_choose(&set, RUBRAVIEW_TRACK_AUDIO, lit("")) == 1);
        /* An unavailable preference falls back to the default. */
        assert(rubraview_tracks_choose(&set, RUBRAVIEW_TRACK_AUDIO, lit("fra")) == 1);

        /* A forced track captions signs; it is not what someone asking
           for subtitles wants. */
        assert(rubraview_tracks_choose(&set, RUBRAVIEW_TRACK_SUBTITLE, lit("eng")) == 4);
        assert(rubraview_tracks_choose(&set, RUBRAVIEW_TRACK_SUBTITLE, lit("")) == 4);

        assert(rubraview_tracks_count(&set, RUBRAVIEW_TRACK_AUDIO) == 2);
        assert(rubraview_tracks_count(&set, RUBRAVIEW_TRACK_SUBTITLE) == 2);
    }
    printf("  [PASS] The right track is chosen, and a forced subtitle track is not it\n");

    /* Test 10: cycling. Subtitles include "off"; audio does not. */
    {
        rubraview_track_set_t set = rubraview_tracks_create();
        rubraview_tracks_add(&set, (rubraview_track_t){ .kind = RUBRAVIEW_TRACK_AUDIO, .codec = lit("aac") });
        rubraview_tracks_add(&set, (rubraview_track_t){ .kind = RUBRAVIEW_TRACK_AUDIO, .codec = lit("ac3") });
        rubraview_tracks_add(&set, (rubraview_track_t){ .kind = RUBRAVIEW_TRACK_SUBTITLE, .codec = lit("srt") });

        assert(rubraview_tracks_next(&set, RUBRAVIEW_TRACK_AUDIO, 0) == 1);
        assert(rubraview_tracks_next(&set, RUBRAVIEW_TRACK_AUDIO, 1) == 0);   /* wraps */

        /* One subtitle track, so the cycle is: it, then off, then it. */
        assert(rubraview_tracks_next(&set, RUBRAVIEW_TRACK_SUBTITLE, -1) == 2);
        assert(rubraview_tracks_next(&set, RUBRAVIEW_TRACK_SUBTITLE, 2) == -1);

        /* D-12: subtitles come from two places and the cycle runs
           through both — a stream inside the file, then a file beside
           it, then off. Which kind a track is has to survive the trip. */
        rubraview_tracks_add(&set, (rubraview_track_t){
            .kind = RUBRAVIEW_TRACK_SUBTITLE, .codec = lit("srt"), .is_external = true });
        assert(set.tracks[2].is_external == false);
        assert(set.tracks[3].is_external == true);
        assert(rubraview_tracks_next(&set, RUBRAVIEW_TRACK_SUBTITLE, 2) == 3);
        assert(rubraview_tracks_next(&set, RUBRAVIEW_TRACK_SUBTITLE, 3) == -1);
    }
    printf("  [PASS] Audio cycles round; subtitles cycle through off, inside the file and beside it\n");

    /* Test 11: the labels a menu shows. */
    {
        rubraview_track_set_t set = rubraview_tracks_create();
        rubraview_tracks_add(&set, (rubraview_track_t){ .kind = RUBRAVIEW_TRACK_VIDEO, .codec = lit("h264") });
        rubraview_tracks_add(&set, (rubraview_track_t){
            .kind = RUBRAVIEW_TRACK_AUDIO, .language = lit("Japanese"), .codec = lit("flac"), .channels = 6 });
        rubraview_tracks_add(&set, (rubraview_track_t){
            .kind = RUBRAVIEW_TRACK_AUDIO, .language = lit("Korean"), .codec = lit("aac"), .channels = 2 });

        char buffer[128];
        /* The number is the position among tracks of the same kind: the
           second audio track is #2 even though it is stream 3. */
        assert(is(rubraview_track_label(buffer, sizeof(buffer), &set, 2), "#2 Korean (aac 2.0)"));
        assert(is(rubraview_track_label(buffer, sizeof(buffer), &set, 1), "#1 Japanese (flac 5.1)"));
        assert(is(rubraview_track_label(buffer, sizeof(buffer), &set, -1), "Off"));

        rubraview_track_set_t bare = rubraview_tracks_create();
        rubraview_tracks_add(&bare, (rubraview_track_t){ .kind = RUBRAVIEW_TRACK_SUBTITLE });
        assert(is(rubraview_track_label(buffer, sizeof(buffer), &bare, 0), "#1 unlabelled (?)"));
    }
    printf("  [PASS] A track's label numbers it among its own kind\n");

    printf("[test_playback] All tests passed successfully!\n");
    return 0;
}
