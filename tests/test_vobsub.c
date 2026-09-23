/* DVD subtitles (owner, 2026-09-23). The fixture is built here rather
   than shipped: a `.sub` is an MPEG program stream, and a handful of
   bytes written on purpose says more about the reader than a disc rip
   would — every field is known, so every field can be checked. */
#include "rubraview/vobsub.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char memory[1 << 20];

static u8str_t lit(const char *s) { return (u8str_t){ .ptr = s, .len = strlen(s) }; }

/* A subpicture: 4x2 pixels, every one colour 1, shown at once and taken
   away at about two seconds. */
static size_t build_spu(unsigned char *out) {
    unsigned char spu[64];
    memset(spu, 0, sizeof(spu));
    /* The two fields: one run of four pixels of colour 1 per line.
       count 4, colour 1 -> 0x11, which is two nibbles. */
    spu[4] = 0x11;   /* the top field: line 0 */
    spu[5] = 0x11;   /* the bottom field: line 1 */
    size_t control = 6;
    size_t at = control;
    spu[at++] = 0x00; spu[at++] = 0x00;                    /* at once */
    size_t next_field = at; at += 2;                        /* where the second sequence is */
    spu[at++] = 0x01;                                       /* start showing */
    spu[at++] = 0x03; spu[at++] = 0x01; spu[at++] = 0x23;   /* colours 0,1,2,3 <- palette 3,2,1,0 */
    spu[at++] = 0x04; spu[at++] = 0xFF; spu[at++] = 0xF0;   /* colour 0 clear, the rest solid */
    spu[at++] = 0x05;
    spu[at++] = 0x00; spu[at++] = 0x00; spu[at++] = 0x03;   /* x 0..3 */
    spu[at++] = 0x00; spu[at++] = 0x00; spu[at++] = 0x01;   /* y 0..1 */
    spu[at++] = 0x06; spu[at++] = 0x00; spu[at++] = 0x04; spu[at++] = 0x00; spu[at++] = 0x05;
    spu[at++] = 0xFF;
    size_t second = at;
    spu[next_field] = (unsigned char)(second >> 8);
    spu[next_field + 1] = (unsigned char)(second & 0xFF);
    spu[at++] = 0x00; spu[at++] = 0xB0;                     /* 176 * 1024/90000 = 2.0025 s */
    spu[at++] = (unsigned char)(second >> 8); spu[at++] = (unsigned char)(second & 0xFF);
    spu[at++] = 0x02;                                       /* stop showing */
    spu[at++] = 0xFF;
    size_t size = at;
    spu[0] = (unsigned char)(size >> 8);
    spu[1] = (unsigned char)(size & 0xFF);
    spu[2] = (unsigned char)(control >> 8);
    spu[3] = (unsigned char)(control & 0xFF);
    memcpy(out, spu, size);
    return size;
}

/* pack header, then one private-stream-1 packet carrying the subpicture. */
static size_t build_sub(unsigned char *out, size_t *out_spu_at) {
    size_t at = 0;
    out[at++] = 0x00; out[at++] = 0x00; out[at++] = 0x01; out[at++] = 0xBA;
    out[at++] = 0x44;                                   /* MPEG-2 pack header */
    for (int i = 0; i < 8; ++i) out[at++] = 0x00;
    out[at++] = 0xF8;                                   /* no stuffing bytes */
    unsigned char spu[64];
    size_t spu_size = build_spu(spu);
    size_t payload = 1 + spu_size;                      /* the substream byte, then the picture */
    size_t header = 3 + 5;                              /* PES flags and a PTS */
    out[at++] = 0x00; out[at++] = 0x00; out[at++] = 0x01; out[at++] = 0xBD;
    size_t packet = header + payload;
    out[at++] = (unsigned char)(packet >> 8); out[at++] = (unsigned char)(packet & 0xFF);
    out[at++] = 0x81; out[at++] = 0x80; out[at++] = 0x05;
    for (int i = 0; i < 5; ++i) out[at++] = 0x00;       /* the PTS itself is not used */
    out[at++] = 0x20;                                   /* substream: the first subpicture */
    *out_spu_at = at;
    memcpy(out + at, spu, spu_size);
    at += spu_size;
    return at;
}

int main(void) {
    printf("[test_vobsub] Starting DVD subtitle tests...\n");

    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = memory, .size = sizeof(memory) });

    unsigned char sub[512];
    size_t spu_at = 0;
    size_t sub_size = build_sub(sub, &spu_at);

    static const char IDX[] =
        "# a hand-made index\n"
        "size: 720x480\n"
        "palette: 000000, ff0000, 00ff00, 0000ff, 111111, 222222, 333333, 444444,"
        " 555555, 666666, 777777, 888888, 999999, aaaaaa, bbbbbb, cccccc\n"
        "id: en, index: 0\n"
        "timestamp: 00:00:10:500, filepos: 000000000\n"
        "id: ko, index: 1\n"
        "timestamp: 00:00:20:000, filepos: 000000000\n";

    /* 1. The languages the index lists, in its own order. */
    {
        u8str_t langs[4];
        size_t n = rubraview_vobsub_languages(lit(IDX), langs, 4);
        assert(n == 2);
        assert(langs[0].len == 2 && memcmp(langs[0].ptr, "en", 2) == 0);
        assert(langs[1].len == 2 && memcmp(langs[1].ptr, "ko", 2) == 0);
    }
    printf("  [PASS] The index's languages are listed in its own order\n");

    /* 2. One subtitle read whole: when, where, what colours, which pixels.
          The index is read first, the picture only when it is wanted. */
    static uint8_t pixels[RUBRAVIEW_VOBSUB_MAX_PIXELS];
    {
        rubraview_vobsub_track_t track = rubraview_vobsub_index(&arena, lit(IDX), 0);
        assert(track.stream_count == 2);
        assert(track.language.len == 2 && memcmp(track.language.ptr, "en", 2) == 0);
        assert(track.frame_width == 720 && track.frame_height == 480);
        assert(track.count == 1);

        rubraview_vobsub_cue_t decoded;
        assert(rubraview_vobsub_decode(&track, 0, sub, sub_size, pixels, &decoded));
        const rubraview_vobsub_cue_t *cue = &decoded;
        assert(cue->start_seconds > 10.49 && cue->start_seconds < 10.51);
        assert(cue->end_seconds > 12.49 && cue->end_seconds < 12.51);   /* 10.5 + 2.0025 */
        assert(cue->x == 0 && cue->y == 0 && cue->width == 4 && cue->height == 2);
        assert(!cue->forced);

        /* Every pixel is colour 1, on both fields — the bottom field is
           line 1, so a reader that forgot it would leave that line 0. */
        for (int i = 0; i < 4 * 2; ++i) assert(cue->indices[i] == 1);

        /* Colour 1 took palette entry 2 (00ff00) and full alpha; colour 0
           is the transparent one the picture never uses. */
        assert(cue->colors[1] == 0xFF00FF00u);
        assert((cue->colors[0] >> 24) == 0);
        assert(cue->colors[3] == 0xFF000000u);   /* the order names them backwards: colour 3 took entry 0 */
        assert(cue->colors[2] == 0xFFFF0000u);   /* entry 1 */

        uint32_t bgra[4 * 2];
        rubraview_vobsub_pixels(cue, bgra);
        for (int i = 0; i < 4 * 2; ++i) assert(bgra[i] == 0xFF00FF00u);

        /* The index alone says when: from its own timestamp until the
           next subtitle, or ten seconds for the last one. */
        assert(rubraview_vobsub_at(&track, 10.0) == -1);
        assert(rubraview_vobsub_at(&track, 11.0) == 0);
        assert(rubraview_vobsub_at(&track, 21.0) == -1);
    }
    printf("  [PASS] A subpicture is read whole: time, place, colours and both fields\n");

    /* 3. The second language is its own list, and `delay:` moves it. */
    {
        rubraview_vobsub_track_t track = rubraview_vobsub_index(&arena, lit(IDX), 1);
        assert(track.count == 1 && track.language.len == 2 && memcmp(track.language.ptr, "ko", 2) == 0);
        assert(track.entries[0].start_seconds > 19.99 && track.entries[0].start_seconds < 20.01);

        static const char DELAYED[] =
            "id: en, index: 0\n"
            "delay: -00:00:02:000\n"
            "timestamp: 00:00:10:000, filepos: 000000000\n";
        rubraview_vobsub_track_t late = rubraview_vobsub_index(&arena, lit(DELAYED), 0);
        assert(late.count == 1);
        assert(late.entries[0].start_seconds > 7.99 && late.entries[0].start_seconds < 8.01);
    }
    printf("  [PASS] Each language keeps its own list, and `delay` moves the times\n");

    /* 4. Rubbish is refused rather than guessed at. */
    {
        rubraview_vobsub_track_t track = rubraview_vobsub_index(&arena, lit(IDX), 0);
        rubraview_vobsub_cue_t cue;
        assert(!rubraview_vobsub_decode(&track, 0, sub, 8, pixels, &cue));        /* the file stops short */
        assert(!rubraview_vobsub_decode(&track, 5, sub, sub_size, pixels, &cue)); /* no such subtitle */

        unsigned char torn[512];
        memcpy(torn, sub, sub_size);
        torn[spu_at + 10] = 0x77;   /* the first order, replaced by one the format does not have */
        assert(!rubraview_vobsub_decode(&track, 0, torn, sub_size, pixels, &cue));

        rubraview_vobsub_track_t missing = rubraview_vobsub_index(&arena, lit("size: 720x480\n"), 0);
        assert(missing.count == 0 && missing.entries == NULL);
    }
    printf("  [PASS] A torn file costs its own lines, not the reader\n");

    printf("[test_vobsub] All tests passed successfully!\n");
    return 0;
}
