#include "rubraview/subtitle.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>

static u8str_t lit(const char *s) { return (u8str_t){ .ptr = s, .len = strlen(s) }; }
static bool is(u8str_t s, const char *l) { return s.len == strlen(l) && memcmp(s.ptr, l, s.len) == 0; }
static bool near(double a, double b) { return fabs(a - b) < 0.002; }

int main(void) {
    printf("[test_subtitle] Starting subtitle tests...\n");

    size_t mem_size = 2 * 1024 * 1024;
    void *raw = malloc(mem_size);
    assert(raw != NULL);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw, .size = mem_size });

    /* Test 1: SubRip, including the two-line cue and the markup that
       has to come off. */
    {
        u8str_t text = lit("1\n"
                           "00:00:01,000 --> 00:00:03,500\n"
                           "First line\n"
                           "Second line\n"
                           "\n"
                           "2\n"
                           "00:01:02,250 --> 00:01:04,000\n"
                           "<i>Italic</i> and <font color=\"#ff0000\">red</font>\n"
                           "\n");
        rubraview_subtitle_track_t t = rubraview_subtitle_parse(&arena, text, RUBRAVIEW_SUBTITLE_SRT);

        assert(t.count == 2);
        assert(near(t.cues[0].start_seconds, 1.0));
        assert(near(t.cues[0].end_seconds, 3.5));
        assert(is(t.cues[0].text, "First line\nSecond line"));
        assert(near(t.cues[1].start_seconds, 62.25));
        assert(is(t.cues[1].text, "Italic and red"));
    }
    printf("  [PASS] SubRip parses, keeps its line breaks and drops its markup\n");

    /* Test 2: WebVTT — a header, a dot for the fraction, cue settings
       after the end time, and comment blocks that are not cues. */
    {
        u8str_t text = lit("WEBVTT\n"
                           "\n"
                           "NOTE this is a comment\n"
                           "\n"
                           "hello\n"
                           "00:00:02.000 --> 00:00:04.000 line:90% align:center\n"
                           "Hello there\n"
                           "\n"
                           "00:05.000 --> 00:07.000\n"
                           "No hours on this one\n");
        rubraview_subtitle_track_t t = rubraview_subtitle_parse(&arena, text, RUBRAVIEW_SUBTITLE_VTT);

        assert(t.count == 2);
        assert(near(t.cues[0].start_seconds, 2.0) && near(t.cues[0].end_seconds, 4.0));
        assert(is(t.cues[0].text, "Hello there"));
        /* WebVTT lets the hours field be left out. */
        assert(near(t.cues[1].start_seconds, 5.0) && near(t.cues[1].end_seconds, 7.0));
    }
    printf("  [PASS] WebVTT parses, with its header, cue settings and optional hours\n");

    /* Test 3: SAMI. Its captions have no end time — each lasts until
       the next one — and `&nbsp;` means nothing is showing. */
    {
        u8str_t text = lit("<SAMI>\n<BODY>\n"
                           "<SYNC Start=1000><P Class=KRCC>\xEC\x95\x88\xEB\x85\x95\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94\n"
                           "<SYNC Start=3000><P Class=KRCC>&nbsp;\n"
                           "<SYNC Start=5000><P Class=KRCC>Second line<br>and more\n"
                           "<SYNC Start=8000><P Class=KRCC>&nbsp;\n"
                           "</BODY>\n</SAMI>\n");
        rubraview_subtitle_track_t t = rubraview_subtitle_parse(&arena, text, RUBRAVIEW_SUBTITLE_SMI);

        assert(t.count == 2);
        assert(near(t.cues[0].start_seconds, 1.0));
        assert(near(t.cues[0].end_seconds, 5.0)); /* runs to the next real caption */
        assert(is(t.cues[0].language, "KRCC"));
        assert(is(t.cues[1].text, "Second line\nand more"));
    }
    printf("  [PASS] SAMI parses, ends each caption at the next, and reads its language class\n");

    /* Test 3b: one SAMI file, two languages (owner, 2026-09-23). Each
       language is its own class, and the reader picks one; only that
       one's captions are shown, at the moments that language has. */
    {
        u8str_t text = lit("<SAMI>\n<BODY>\n"
                           "<SYNC Start=1000><P Class=ENCC>Hello\n"
                           "<SYNC Start=1000><P Class=KRCC>\xEC\x95\x88\xEB\x85\x95\n"
                           "<SYNC Start=4000><P Class=ENCC>&nbsp;\n"
                           "<SYNC Start=4000><P Class=KRCC>&nbsp;\n"
                           "</BODY>\n</SAMI>\n");
        rubraview_subtitle_track_t t = rubraview_subtitle_parse(&arena, text, RUBRAVIEW_SUBTITLE_SMI);
        assert(t.count == 2);

        u8str_t languages[4];
        size_t n = rubraview_subtitle_languages(&t, languages, 4);
        assert(n == 2 && is(languages[0], "ENCC") && is(languages[1], "KRCC"));

        /* Both last until their own language's next caption — before
           2026-09-23 each ended at the next caption of any language, so
           both were of zero length and neither was ever shown. */
        assert(near(t.cues[0].end_seconds, 4.0) && near(t.cues[1].end_seconds, 4.0));

        /* With nothing chosen, whichever is last at that moment shows. */
        const rubraview_subtitle_cue_t *any = rubraview_subtitle_at(&t, 2.0);
        assert(any != NULL);

        t.shown_language = languages[1];               /* Korean */
        const rubraview_subtitle_cue_t *ko = rubraview_subtitle_at(&t, 2.0);
        assert(ko && is(ko->language, "KRCC") && !is(ko->text, "Hello"));

        t.shown_language = languages[0];               /* English */
        const rubraview_subtitle_cue_t *en = rubraview_subtitle_at(&t, 2.0);
        assert(en && is(en->text, "Hello"));
        assert(rubraview_subtitle_at(&t, 6.0) == NULL);   /* both ended at 4 s */

        /* A file of one language answers whatever is asked of it. */
        u8str_t one = lit("1\n00:00:01,000 --> 00:00:02,000\nplain\n");
        rubraview_subtitle_track_t srt = rubraview_subtitle_parse(&arena, one, RUBRAVIEW_SUBTITLE_SRT);
        assert(rubraview_subtitle_languages(&srt, languages, 4) == 1 && languages[0].len == 0);
    }
    printf("  [PASS] One SAMI file holding two languages shows only the one chosen\n");

    /* Test 4: SubStation Alpha. The text is everything after the ninth
       comma, so a line with commas in it must survive. */
    {
        u8str_t text = lit("[Script Info]\nScriptType: v4.00+\n"
                           "[Events]\n"
                           "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                           "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,{\\pos(400,570)}Hello, world, again\n"
                           "Dialogue: 0,0:00:04.00,0:00:06.00,Default,,0,0,0,,Line one\\NLine two\n");
        rubraview_subtitle_track_t t = rubraview_subtitle_parse(&arena, text, RUBRAVIEW_SUBTITLE_ASS);

        assert(t.count == 2);
        assert(near(t.cues[0].start_seconds, 1.0) && near(t.cues[0].end_seconds, 3.0));
        assert(is(t.cues[0].text, "Hello, world, again"));   /* commas kept, override dropped */
        assert(is(t.cues[1].text, "Line one\nLine two"));
    }
    printf("  [PASS] SubStation parses, keeps commas in the text and drops its overrides\n");

    /* Test 5: the format is worked out from the content when the name
       does not say — a `.txt` holding SubRip is common. */
    {
        assert(rubraview_subtitle_format_for_name(lit("movie.srt")) == RUBRAVIEW_SUBTITLE_SRT);
        assert(rubraview_subtitle_format_for_name(lit("movie.SMI")) == RUBRAVIEW_SUBTITLE_SMI);
        assert(rubraview_subtitle_format_for_name(lit("movie.ssa")) == RUBRAVIEW_SUBTITLE_ASS);
        assert(rubraview_subtitle_format_for_name(lit("movie.mkv")) == RUBRAVIEW_SUBTITLE_UNKNOWN);

        rubraview_subtitle_track_t t = rubraview_subtitle_parse(
            &arena, lit("1\n00:00:01,000 --> 00:00:02,000\nGuessed\n"), RUBRAVIEW_SUBTITLE_UNKNOWN);
        assert(t.format == RUBRAVIEW_SUBTITLE_SRT && t.count == 1);

        rubraview_subtitle_track_t vtt = rubraview_subtitle_parse(
            &arena, lit("WEBVTT\n\n00:00:01.000 --> 00:00:02.000\nGuessed\n"), RUBRAVIEW_SUBTITLE_UNKNOWN);
        assert(vtt.format == RUBRAVIEW_SUBTITLE_VTT && vtt.count == 1);
    }
    printf("  [PASS] The format is recognised from the name, or from the content\n");

    /* Test 6: one broken cue costs that cue, not the file. */
    {
        u8str_t text = lit("1\n00:00:01,000 --> 00:00:02,000\nGood\n"
                           "\n"
                           "2\nnot a timestamp --> nonsense\nBad\n"
                           "\n"
                           "3\n00:00:05,000 --> 00:00:06,000\nAlso good\n");
        rubraview_subtitle_track_t t = rubraview_subtitle_parse(&arena, text, RUBRAVIEW_SUBTITLE_SRT);
        assert(t.count == 2);
        assert(is(t.cues[0].text, "Good"));
        assert(is(t.cues[1].text, "Also good"));
    }
    printf("  [PASS] A malformed cue is skipped and the rest of the file still parses\n");

    /* Test 7: finding what is showing at a moment, and the gaps between. */
    {
        u8str_t text = lit("1\n00:00:01,000 --> 00:00:03,000\nA\n\n"
                           "2\n00:00:05,000 --> 00:00:07,000\nB\n");
        rubraview_subtitle_track_t t = rubraview_subtitle_parse(&arena, text, RUBRAVIEW_SUBTITLE_SRT);

        assert(rubraview_subtitle_at(&t, 0.5) == NULL);       /* before the first */
        assert(is(rubraview_subtitle_at(&t, 2.0)->text, "A"));
        assert(rubraview_subtitle_at(&t, 4.0) == NULL);       /* in the gap */
        assert(is(rubraview_subtitle_at(&t, 6.0)->text, "B"));
        assert(rubraview_subtitle_at(&t, 99.0) == NULL);      /* after the last */
        /* The end is exclusive: a cue is gone the moment it ends. */
        assert(rubraview_subtitle_at(&t, 3.0) == NULL);
    }
    printf("  [PASS] The showing cue is found, and the gaps stay empty\n");

    /* Test 8: §3.16.1's Z and X shift the whole track. */
    {
        u8str_t text = lit("1\n00:00:10,000 --> 00:00:12,000\nA\n");
        rubraview_subtitle_track_t t = rubraview_subtitle_parse(&arena, text, RUBRAVIEW_SUBTITLE_SRT);

        assert(rubraview_subtitle_at(&t, 9.7) == NULL);
        rubraview_subtitle_nudge(&t, false);   /* half a second earlier */
        assert(near(t.offset_seconds, -0.5));
        assert(rubraview_subtitle_at(&t, 9.7) != NULL);

        rubraview_subtitle_nudge(&t, true);
        rubraview_subtitle_nudge(&t, true);
        assert(near(t.offset_seconds, 0.5));
        assert(rubraview_subtitle_at(&t, 10.2) == NULL);   /* now it starts later */
        assert(rubraview_subtitle_at(&t, 10.7) != NULL);
    }
    printf("  [PASS] Nudging moves the whole track without touching its cues\n");

    /* Test 9: §3.16.1's discovery — subtitles are found by sharing the
       video's base name, including the ones with a language tag. */
    {
        u8str_t siblings[] = {
            lit("D:/Films/movie.srt"),
            lit("D:/Films/movie.kor.smi"),
            lit("D:/Films/movie.eng.vtt"),
            lit("D:/Films/movie.mkv"),        /* the video itself */
            lit("D:/Films/another.srt"),      /* a different film */
            lit("D:/Films/notes.txt"),
        };
        rubraview_subtitle_candidate_t found[8];
        size_t count = rubraview_subtitle_discover(lit("D:/Films/movie.mkv"), siblings, 6, found, 8);

        assert(count == 3);
        assert(found[0].format == RUBRAVIEW_SUBTITLE_SRT);
        assert(found[1].format == RUBRAVIEW_SUBTITLE_SMI);
        assert(found[2].format == RUBRAVIEW_SUBTITLE_VTT);

        /* A film whose name is a prefix of another must not collect the
           other's subtitles. */
        u8str_t tricky[] = { lit("D:/Films/movie2.srt") };
        assert(rubraview_subtitle_discover(lit("D:/Films/movie.mkv"), tricky, 1, NULL, 0) == 0);
    }
    printf("  [PASS] Subtitles are found by the video's name, tags included, prefixes excluded\n");

    /* A subtitle file's name says which language it is, or says nothing. */
    {
        assert(rubraview_subtitle_language_tag(lit("D:/Films/movie.mkv"),
                                               lit("D:/Films/movie.kor.srt")).len == 3);
        u8str_t tag = rubraview_subtitle_language_tag(lit("D:/Films/movie.mkv"),
                                                      lit("D:/Films/movie.kor.srt"));
        assert(memcmp(tag.ptr, "kor", 3) == 0);
        /* No tag between the name and the extension. */
        assert(rubraview_subtitle_language_tag(lit("D:/Films/movie.mkv"),
                                               lit("D:/Films/movie.srt")).len == 0);
        /* Another film's subtitle claims nothing here. */
        assert(rubraview_subtitle_language_tag(lit("D:/Films/movie.mkv"),
                                               lit("D:/Films/other.kor.srt")).len == 0);
        /* Two tags: the first is the language, as ffmpeg and mpv read it. */
        tag = rubraview_subtitle_language_tag(lit("D:/Films/movie.mkv"),
                                              lit("D:/Films/movie.eng.forced.srt"));
        assert(tag.len == 3 && memcmp(tag.ptr, "eng", 3) == 0);
    }
    printf("  [PASS] A subtitle file's name says which language it is, or says nothing\n");

    /* Test 10: empty and hostile input produces an empty track, not a
       crash. ASan is the real assertion here. */
    {
        assert(rubraview_subtitle_parse(&arena, lit(""), RUBRAVIEW_SUBTITLE_SRT).count == 0);
        assert(rubraview_subtitle_parse(&arena, lit("-->"), RUBRAVIEW_SUBTITLE_SRT).count == 0);
        assert(rubraview_subtitle_parse(&arena, lit("<SYNC"), RUBRAVIEW_SUBTITLE_SMI).count == 0);
        assert(rubraview_subtitle_parse(&arena, lit("Dialogue: 0"), RUBRAVIEW_SUBTITLE_ASS).count == 0);
        assert(rubraview_subtitle_parse(&arena, lit("<<<<<<<"), RUBRAVIEW_SUBTITLE_UNKNOWN).count == 0);
        assert(rubraview_subtitle_at(NULL, 1.0) == NULL);
    }
    printf("  [PASS] Empty and truncated input gives an empty track rather than a crash\n");

    free(raw);
    printf("[test_subtitle] All tests passed successfully!\n");
    return 0;
}
