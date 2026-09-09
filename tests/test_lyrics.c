#include "rubraview/lyrics.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>

static u8str_t lit(const char *s) { return (u8str_t){ .ptr = s, .len = strlen(s) }; }
static bool is(u8str_t s, const char *l) { return s.len == strlen(l) && memcmp(s.ptr, l, s.len) == 0; }
static bool near(double a, double b) { return fabs(a - b) < 0.002; }

int main(void) {
    printf("[test_lyrics] Starting lyrics and cue sheet tests...\n");

    size_t mem_size = 1024 * 1024;
    void *raw = malloc(mem_size);
    assert(raw != NULL);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw, .size = mem_size });

    /* Test 1: an ordinary `.lrc`, with its metadata tags. */
    {
        u8str_t text = lit("[ti:Song Title]\n"
                           "[ar:The Artist]\n"
                           "[al:The Album]\n"
                           "[00:12.00]First line\n"
                           "[00:17.20]Second line\n"
                           "[01:02.50]Third line\n");
        rubraview_lyrics_t l = rubraview_lyrics_parse(&arena, text);

        assert(is(l.title, "Song Title"));
        assert(is(l.artist, "The Artist"));
        assert(is(l.album, "The Album"));
        assert(l.count == 3);
        assert(near(l.lines[0].time_seconds, 12.0));
        assert(is(l.lines[0].text, "First line"));
        assert(near(l.lines[2].time_seconds, 62.5));
    }
    printf("  [PASS] An .lrc parses, with its title, artist and album\n");

    /* Test 2: one line with several timestamps — a chorus is written
       once and pointed at from each place it occurs. */
    {
        u8str_t text = lit("[00:20.00][01:20.00][02:20.00]The chorus\n"
                           "[00:40.00]A verse\n");
        rubraview_lyrics_t l = rubraview_lyrics_parse(&arena, text);

        assert(l.count == 4);
        /* Sorted by time, so the verse comes second. */
        assert(near(l.lines[0].time_seconds, 20.0) && is(l.lines[0].text, "The chorus"));
        assert(near(l.lines[1].time_seconds, 40.0) && is(l.lines[1].text, "A verse"));
        assert(near(l.lines[2].time_seconds, 80.0) && is(l.lines[2].text, "The chorus"));
        assert(near(l.lines[3].time_seconds, 140.0));
    }
    printf("  [PASS] A line with several timestamps becomes several lines, in order\n");

    /* Test 3: the fraction is hundredths in some files and milliseconds
       in others. */
    {
        rubraview_lyrics_t hundredths = rubraview_lyrics_parse(&arena, lit("[00:01.50]A\n"));
        assert(hundredths.count == 1 && near(hundredths.lines[0].time_seconds, 1.5));

        rubraview_lyrics_t millis = rubraview_lyrics_parse(&arena, lit("[00:01.500]A\n"));
        assert(millis.count == 1 && near(millis.lines[0].time_seconds, 1.5));

        rubraview_lyrics_t none = rubraview_lyrics_parse(&arena, lit("[00:01]A\n"));
        assert(none.count == 1 && near(none.lines[0].time_seconds, 1.0));
    }
    printf("  [PASS] Hundredths, milliseconds and no fraction all read correctly\n");

    /* Test 4: which line is highlighted, and clicking one to seek. */
    {
        u8str_t text = lit("[00:10.00]One\n[00:20.00]Two\n[00:30.00]Three\n");
        rubraview_lyrics_t l = rubraview_lyrics_parse(&arena, text);

        assert(rubraview_lyrics_index_at(&l, 5.0) == -1);    /* before the first line */
        assert(rubraview_lyrics_index_at(&l, 10.0) == 0);    /* exactly on it */
        assert(rubraview_lyrics_index_at(&l, 19.9) == 0);    /* still on it */
        assert(rubraview_lyrics_index_at(&l, 20.0) == 1);
        assert(rubraview_lyrics_index_at(&l, 999.0) == 2);   /* the last line stays */

        assert(near(rubraview_lyrics_time_of(&l, 1), 20.0));
        assert(rubraview_lyrics_time_of(&l, 99) < 0.0);
    }
    printf("  [PASS] The highlighted line is found, and a line gives back its time\n");

    /* Test 5: the `[offset:]` tag shifts everything. Positive means the
       words come earlier, which is the opposite of what it looks like. */
    {
        u8str_t text = lit("[offset:500]\n[00:10.00]One\n");
        rubraview_lyrics_t l = rubraview_lyrics_parse(&arena, text);
        assert(near(l.offset_seconds, -0.5));
        assert(rubraview_lyrics_index_at(&l, 10.4) == 0);
    }
    printf("  [PASS] The offset tag shifts the whole file the way the format means it\n");

    /* Test 6: rubbish gives an empty result rather than a crash. */
    {
        assert(rubraview_lyrics_parse(&arena, lit("")).count == 0);
        assert(rubraview_lyrics_parse(&arena, lit("no timestamps here\n")).count == 0);
        assert(rubraview_lyrics_parse(&arena, lit("[unterminated\n")).count == 0);
        assert(rubraview_lyrics_parse(&arena, lit("[ab:cd]text\n")).count == 0);
        assert(rubraview_lyrics_index_at(NULL, 1.0) == -1);
    }
    printf("  [PASS] A file with no timestamps gives no lines rather than a crash\n");

    /* Test 7: a cue sheet. The times are mm:ss:ff with 75 frames to the
       second — the detail every naive parser gets wrong. */
    {
        u8str_t text = lit("PERFORMER \"The Band\"\n"
                           "TITLE \"The Album\"\n"
                           "FILE \"album.flac\" WAVE\n"
                           "  TRACK 01 AUDIO\n"
                           "    TITLE \"Opening\"\n"
                           "    PERFORMER \"The Band\"\n"
                           "    INDEX 01 00:00:00\n"
                           "  TRACK 02 AUDIO\n"
                           "    TITLE \"Second Song\"\n"
                           "    INDEX 00 03:41:00\n"
                           "    INDEX 01 03:42:37\n"
                           "  TRACK 03 AUDIO\n"
                           "    TITLE \"Finale\"\n"
                           "    INDEX 01 07:15:00\n");
        rubraview_cue_sheet_t sheet = rubraview_cue_parse(&arena, text, 600.0);

        assert(is(sheet.album_title, "The Album"));
        assert(is(sheet.album_performer, "The Band"));
        assert(is(sheet.audio_file, "album.flac"));
        assert(sheet.count == 3);

        assert(sheet.tracks[0].number == 1);
        assert(is(sheet.tracks[0].title, "Opening"));
        assert(near(sheet.tracks[0].start_seconds, 0.0));

        /* 03:42:37 is 3 minutes, 42 seconds and 37 *frames* — and a
           frame is a 75th of a second, not a hundredth. */
        assert(near(sheet.tracks[1].start_seconds, 222.0 + 37.0 / 75.0));
        assert(is(sheet.tracks[1].title, "Second Song"));

        /* A track ends where the next begins; the last runs to the end. */
        assert(near(sheet.tracks[0].end_seconds, sheet.tracks[1].start_seconds));
        assert(near(sheet.tracks[2].end_seconds, 600.0));
    }
    printf("  [PASS] A cue sheet parses, with 75 frames to the second\n");

    /* Test 8: INDEX 00 is the pre-gap — the silence before the song —
       and a listener asking for a track expects the music, not it. */
    {
        u8str_t text = lit("FILE \"a.flac\" WAVE\n"
                           "  TRACK 01 AUDIO\n"
                           "    INDEX 01 00:00:00\n"
                           "  TRACK 02 AUDIO\n"
                           "    INDEX 00 01:00:00\n"
                           "    INDEX 01 01:02:00\n");
        rubraview_cue_sheet_t sheet = rubraview_cue_parse(&arena, text, 300.0);
        assert(sheet.count == 2);
        assert(near(sheet.tracks[1].start_seconds, 62.0));   /* INDEX 01, not 00 */
    }
    printf("  [PASS] The pre-gap is skipped and a track starts where its music does\n");

    /* Test 9: finding which track a moment falls in. */
    {
        u8str_t text = lit("FILE \"a.flac\" WAVE\n"
                           "  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n"
                           "  TRACK 02 AUDIO\n    INDEX 01 02:00:00\n"
                           "  TRACK 03 AUDIO\n    INDEX 01 04:00:00\n");
        rubraview_cue_sheet_t sheet = rubraview_cue_parse(&arena, text, 360.0);

        assert(rubraview_cue_track_at(&sheet, 0.0) == 0);
        assert(rubraview_cue_track_at(&sheet, 119.9) == 0);
        assert(rubraview_cue_track_at(&sheet, 120.0) == 1);
        assert(rubraview_cue_track_at(&sheet, 300.0) == 2);
        assert(rubraview_cue_track_at(&sheet, 999.0) == -1);   /* past the album */
    }
    printf("  [PASS] A moment maps to the track it falls in\n");

    /* Test 10: a cue sheet with no tracks, and one with no length yet. */
    {
        assert(rubraview_cue_parse(&arena, lit(""), 0.0).count == 0);
        assert(rubraview_cue_parse(&arena, lit("FILE \"a.flac\" WAVE\n"), 0.0).count == 0);

        /* With no total length the last track is open-ended, and a time
           inside it still resolves. */
        u8str_t text = lit("FILE \"a.flac\" WAVE\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n");
        rubraview_cue_sheet_t open = rubraview_cue_parse(&arena, text, 0.0);
        assert(open.count == 1);
        assert(rubraview_cue_track_at(&open, 5000.0) == 0);
    }
    printf("  [PASS] An empty or open-ended cue sheet behaves rather than breaking\n");

    free(raw);
    printf("[test_lyrics] All tests passed successfully!\n");
    return 0;
}
