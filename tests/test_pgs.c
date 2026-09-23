/* Blu-ray subtitles (owner, 2026-09-24). The fixture is written here
   byte by byte rather than ripped from a disc: every field is known, so
   every field can be checked — and the awkward cases (a line that fills
   the width, a picture sent in two pieces, a set that only changes the
   palette) can be put in on purpose. */
#include "rubraview/pgs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char memory[1 << 20];

/* One segment: "PG", the moment, the kind, the body. */
static size_t segment(unsigned char *out, size_t at, unsigned char type,
                      double seconds, const unsigned char *body, size_t len) {
    unsigned long pts = (unsigned long)(seconds * 90000.0 + 0.5);
    out[at++] = 'P'; out[at++] = 'G';
    out[at++] = (unsigned char)(pts >> 24); out[at++] = (unsigned char)(pts >> 16);
    out[at++] = (unsigned char)(pts >> 8);  out[at++] = (unsigned char)pts;
    for (int i = 0; i < 4; ++i) out[at++] = 0x00;          /* the decode time, unused */
    out[at++] = type;
    out[at++] = (unsigned char)(len >> 8); out[at++] = (unsigned char)(len & 0xFF);
    memcpy(out + at, body, len);
    return at + len;
}

/* A composition: the frame size, and one object placed at x,y. */
static size_t pcs(unsigned char *out, size_t at, double seconds, bool draws,
                  bool palette_only, int x, int y) {
    unsigned char body[19];
    memset(body, 0, sizeof(body));
    body[0] = 1920 >> 8; body[1] = 1920 & 0xFF;
    body[2] = 1080 >> 8; body[3] = 1080 & 0xFF;
    body[4] = 0x10;                       /* frame rate */
    body[7] = 0x80;                       /* the start of an epoch */
    body[8] = palette_only ? 1 : 0;
    body[10] = draws ? 1 : 0;
    if (!draws) return segment(out, at, 0x16, seconds, body, 11);
    body[11] = 0; body[12] = 0;           /* object 0 */
    body[13] = 0;                         /* window 0 */
    body[14] = 0x40;                      /* forced */
    body[15] = (unsigned char)(x >> 8); body[16] = (unsigned char)(x & 0xFF);
    body[17] = (unsigned char)(y >> 8); body[18] = (unsigned char)(y & 0xFF);
    return segment(out, at, 0x16, seconds, body, 19);
}

/* Two colours: 1 opaque white, 2 black and entirely clear. */
static size_t pds(unsigned char *out, size_t at, double seconds) {
    static const unsigned char body[12] = {
        0, 0,
        1, 235, 128, 128, 255,
        2,  16, 128, 128,   0,
    };
    return segment(out, at, 0x14, seconds, body, sizeof(body));
}

/* 8x2: the first line filled with colour 1 (and its own end of line, which
   is what a reader that wraps by itself gets wrong), the second line three
   pixels of colour 2 and then nothing. */
static const unsigned char RLE[] = {
    0x00, 0x88, 0x01,   /* eight of colour 1 */
    0x00, 0x00,         /* end of line */
    0x02, 0x02, 0x02,   /* three literal pixels */
    0x00, 0x00,         /* end of line */
};

/* The picture, in `pieces` segments (one, or a first and a continuation). */
static size_t ods(unsigned char *out, size_t at, double seconds, int pieces) {
    unsigned char body[64];
    size_t data_len = sizeof(RLE) + 4;    /* the run-length bytes and the size fields */
    size_t split = pieces > 1 ? 4 : sizeof(RLE);

    memset(body, 0, sizeof(body));
    body[0] = 0; body[1] = 0;             /* object 0 */
    body[2] = 0;                          /* version */
    body[3] = pieces > 1 ? 0x80 : 0xC0;   /* first, and last too when whole */
    body[4] = (unsigned char)(data_len >> 16);
    body[5] = (unsigned char)(data_len >> 8);
    body[6] = (unsigned char)(data_len & 0xFF);
    body[7] = 0; body[8] = 8;             /* 8 wide */
    body[9] = 0; body[10] = 2;            /* 2 high */
    memcpy(body + 11, RLE, split);
    at = segment(out, at, 0x15, seconds, body, 11 + split);
    if (pieces > 1) {
        body[3] = 0x40;                   /* the last piece */
        memcpy(body + 4, RLE + split, sizeof(RLE) - split);
        at = segment(out, at, 0x15, seconds, body, 4 + sizeof(RLE) - split);
    }
    return at;
}

/* A whole little film's worth: one subtitle from 1s to 4s, a fade step in
   the middle of it, and the set that clears it. */
static size_t build_sup(unsigned char *out, int pieces) {
    size_t at = 0;
    unsigned char window[10] = { 1, 0, 0, 50, 1, 144, 0, 8, 0, 2 };
    at = pcs(out, at, 1.0, true, false, 50, 400);
    at = segment(out, at, 0x17, 1.0, window, sizeof(window));
    at = pds(out, at, 1.0);
    at = ods(out, at, 1.0, pieces);
    at = segment(out, at, 0x80, 1.0, window, 0);

    at = pcs(out, at, 1.5, true, true, 50, 400);          /* a new palette only */
    at = pds(out, at, 1.5);
    at = segment(out, at, 0x80, 1.5, window, 0);

    at = pcs(out, at, 4.0, false, false, 0, 0);           /* nothing on screen now */
    at = segment(out, at, 0x80, 4.0, window, 0);
    return at;
}

int main(void) {
    printf("[test_pgs] Starting Blu-ray subtitle tests...\n");

    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = memory, .size = sizeof(memory) });
    static unsigned char pixels[RUBRAVIEW_PGS_MAX_PIXELS];
    static unsigned char sup[1024];

    /* 1. One subtitle, from the moment it is composed to the one that
          clears it, with the frame it was authored for. */
    size_t size = build_sup(sup, 1);
    rubraview_pgs_track_t track = rubraview_pgs_index(&arena, sup, size);
    assert(track.count == 1);
    assert(track.frame_width == 1920 && track.frame_height == 1080);
    assert(track.entries[0].start_seconds > 0.99 && track.entries[0].start_seconds < 1.01);
    assert(track.entries[0].end_seconds > 3.99 && track.entries[0].end_seconds < 4.01);
    printf("  [PASS] A display set is one subtitle, ended by the set that clears it\n");

    /* 2. The moment on screen decides which one is shown. */
    assert(rubraview_pgs_at(&track, 0.5) == -1);
    assert(rubraview_pgs_at(&track, 1.0) == 0);
    assert(rubraview_pgs_at(&track, 1.7) == 0);    /* across the fade step */
    assert(rubraview_pgs_at(&track, 3.9) == 0);
    assert(rubraview_pgs_at(&track, 4.0) == -1);
    printf("  [PASS] A set that only sends a palette does not end the subtitle\n");

    /* 3. The picture: where it goes, how big, and every pixel of it. */
    rubraview_pgs_cue_t cue;
    assert(rubraview_pgs_decode(&track, 0, sup, size, pixels, &cue));
    assert(cue.x == 50 && cue.y == 400);
    assert(cue.width == 8 && cue.height == 2);
    assert(cue.forced);
    for (int i = 0; i < 8; ++i) assert(pixels[i] == 1);          /* the line that fills the width */
    assert(pixels[8] == 2 && pixels[9] == 2 && pixels[10] == 2);
    for (int i = 11; i < 16; ++i) assert(pixels[i] == 0);
    printf("  [PASS] A line that fills the width still ends where the stream says\n");

    /* 4. Y'CbCr becomes a colour, and the alpha is kept. */
    assert(cue.colors[1] == 0xFFFFFFFFu);      /* 235,128,128 solid: white */
    assert((cue.colors[2] >> 24) == 0);        /* entirely clear */
    {
        static uint32_t bgra[16];
        rubraview_pgs_pixels(&cue, bgra);
        assert(bgra[0] == 0xFFFFFFFFu);
        assert(bgra[8] == 0);                  /* clear, so nothing of it is left */
    }
    printf("  [PASS] The palette arrives as Y'CbCr and leaves as a colour with its alpha\n");

    /* 5. The same picture, sent in two pieces, reads the same. */
    {
        static unsigned char split[1024];
        size_t split_size = build_sup(split, 2);
        rubraview_pgs_track_t two = rubraview_pgs_index(&arena, split, split_size);
        assert(two.count == 1);
        rubraview_pgs_cue_t cue2;
        memset(pixels, 0xEE, 32);
        assert(rubraview_pgs_decode(&two, 0, split, split_size, pixels, &cue2));
        assert(cue2.width == 8 && cue2.height == 2);
        for (int i = 0; i < 8; ++i) assert(pixels[i] == 1);
        assert(pixels[8] == 2 && pixels[15] == 0);
    }
    printf("  [PASS] A picture split over several segments is put back together\n");

    /* 6. A file that stops in the middle is read as far as it goes. */
    {
        rubraview_pgs_track_t cut = rubraview_pgs_index(&arena, sup, size - 30);
        assert(cut.count <= 1);
        if (cut.count == 1) {
            rubraview_pgs_cue_t torn;
            (void)rubraview_pgs_decode(&cut, 0, sup, size - 30, pixels, &torn);
        }
        assert(rubraview_pgs_index(&arena, sup, 4).count == 0);
        assert(rubraview_pgs_index(&arena, NULL, 0).count == 0);
        assert(rubraview_pgs_at(NULL, 1.0) == -1);
        assert(!rubraview_pgs_decode(&track, 9, sup, size, pixels, &cue));
    }
    printf("  [PASS] A torn file costs that subtitle and nothing else\n");

    printf("[test_pgs] All tests passed successfully!\n");
    return 0;
}
