#ifndef RUBRAVIEW_VOBSUB_H
#define RUBRAVIEW_VOBSUB_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * DVD subtitles (owner, 2026-09-23: "dvd그림자막도 지원하게 해 주세요").
 *
 * A DVD does not caption with text. Each subtitle is a little picture of
 * four colours — a "subpicture" — run-length coded, with the moments it
 * appears and goes away written beside it. VobSub keeps those pictures in
 * a `.sub` file (an MPEG program stream) and an index of where each one
 * starts in a `.idx` text file, which also holds the sixteen-colour
 * palette and the size of the frame they were drawn for.
 *
 * Everything here is arithmetic on bytes: no decoder, no FFmpeg. What
 * comes out is, for each subtitle, a rectangle and one byte per pixel
 * saying which of four colours it is — the viewer turns that into a
 * bitmap it can draw over the film.
 */

/* What the index says about one subtitle: when it starts, and where its
   picture is in the `.sub`. The picture itself is decoded when it is
   shown — a feature film's worth of them at once would be hundreds of
   megabytes, and all but one of them is off screen. */
typedef struct rubraview_vobsub_entry {
    double start_seconds;
    size_t filepos;
} rubraview_vobsub_entry_t;

typedef struct rubraview_vobsub_track {
    rubraview_vobsub_entry_t *entries;   /* in the index's order */
    size_t                    count;
    int32_t                   frame_width, frame_height;  /* the `size:` line, 720x480 and such */
    u8str_t                   language;                   /* the chosen `id:` block's tag */
    size_t                    stream_count;               /* how many `id:` blocks the index has */
    uint32_t                  palette[16];                /* the `palette:` line, ARGB */
} rubraview_vobsub_track_t;

/* One subtitle, decoded: a rectangle in the frame the index describes,
   one byte per pixel saying which of four colours it is. */
typedef struct rubraview_vobsub_cue {
    double   start_seconds;
    double   end_seconds;
    int32_t  x, y;
    int32_t  width, height;
    uint8_t *indices;         /* the buffer handed to the decoder */
    uint32_t colors[4];       /* ARGB, the alpha from the subpicture */
    bool     forced;          /* a caption for signs, shown in any case */
} rubraview_vobsub_cue_t;

/** A picture is at most this wide and tall — a DVD frame, PAL or NTSC. */
#define RUBRAVIEW_VOBSUB_MAX_PIXELS (720u * 576u)

/** The languages a `.idx` lists, in order; returns how many were written. */
size_t rubraview_vobsub_languages(u8str_t idx_text, u8str_t *out, size_t max);

/**
 * Read one language's index. `stream` picks an `id:` block (0 is the
 * first). The entries are allocated from `arena`; nothing is decoded yet.
 */
rubraview_vobsub_track_t rubraview_vobsub_index(proven_arena_t *arena, u8str_t idx_text, size_t stream);

/**
 * Decode the `index`-th subtitle out of the `.sub` bytes into `pixels`
 * (RUBRAVIEW_VOBSUB_MAX_PIXELS bytes). False when that subtitle cannot be
 * read — a scratched disc costs the lines it damaged, not the film.
 */
bool rubraview_vobsub_decode(const rubraview_vobsub_track_t *track, size_t index,
                             const uint8_t *sub_bytes, size_t sub_size,
                             uint8_t *pixels, rubraview_vobsub_cue_t *out_cue);

/**
 * Which subtitle belongs on screen at `seconds`, or -1. The index only
 * says when each one starts, so one lasts until the next begins (or ten
 * seconds, for the last); the decoded picture may end it sooner.
 */
int32_t rubraview_vobsub_at(const rubraview_vobsub_track_t *track, double seconds);

/**
 * One cue as pixels, BGRA with the alpha already multiplied in (what the
 * renderer's bitmaps take). `out` holds width * height pixels.
 */
void rubraview_vobsub_pixels(const rubraview_vobsub_cue_t *cue, uint32_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_VOBSUB_H */
