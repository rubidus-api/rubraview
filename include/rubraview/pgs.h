#ifndef RUBRAVIEW_PGS_H
#define RUBRAVIEW_PGS_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Blu-ray subtitles (owner, 2026-09-24), the `.sup` file a rip leaves
 * beside the film — PGS, "presentation graphic stream".
 *
 * Like a DVD's, a Blu-ray subtitle is a picture rather than text, but
 * the format shares nothing with VobSub: one file, no index beside it,
 * segments instead of an MPEG program stream, up to 256 colours instead
 * of four, and the colours arrive as Y'CbCr rather than RGB. What it
 * has in common is the shape of the answer — a rectangle, one byte per
 * pixel, and a palette — so the viewer draws both the same way.
 *
 * The file is read in two passes for the same reason VobSub is: the
 * pictures of a whole film are far too much to hold at once, so the
 * first pass only notes where each one is and when it is shown.
 */

/** Where one subtitle sits in the file, and when it is shown. */
typedef struct rubraview_pgs_entry {
    double start_seconds;
    double end_seconds;    /* when the next display set clears it */
    size_t offset;         /* the first segment of this display set */
} rubraview_pgs_entry_t;

typedef struct rubraview_pgs_track {
    rubraview_pgs_entry_t *entries;
    size_t                 count;
    int32_t                frame_width, frame_height;   /* what the stream says the film is */
} rubraview_pgs_track_t;

/** One subtitle, decoded. `colors` is indexed by the bytes in `indices`. */
typedef struct rubraview_pgs_cue {
    double   start_seconds;
    double   end_seconds;
    int32_t  x, y;
    int32_t  width, height;
    uint8_t *indices;        /* the buffer the caller handed the decoder */
    uint32_t colors[256];    /* ARGB, straight (not premultiplied) */
    bool     forced;
} rubraview_pgs_cue_t;

/** A Blu-ray subtitle picture is at most a full frame. */
#define RUBRAVIEW_PGS_MAX_PIXELS (1920u * 1080u)

/**
 * Walk the file and note every subtitle: when it appears, when it is
 * cleared, and where its segments begin. Nothing is decoded here.
 */
rubraview_pgs_track_t rubraview_pgs_index(proven_arena_t *arena, const uint8_t *bytes, size_t size);

/**
 * Decode the `index`-th subtitle into `pixels` (RUBRAVIEW_PGS_MAX_PIXELS
 * bytes). False when it cannot be read, which costs that subtitle only.
 */
bool rubraview_pgs_decode(const rubraview_pgs_track_t *track, size_t index,
                          const uint8_t *bytes, size_t size,
                          uint8_t *pixels, rubraview_pgs_cue_t *out_cue);

/** Which subtitle belongs on screen at `seconds`, or -1. */
int32_t rubraview_pgs_at(const rubraview_pgs_track_t *track, double seconds);

/** One cue as BGRA with the alpha multiplied in, ready for the renderer. */
void rubraview_pgs_pixels(const rubraview_pgs_cue_t *cue, uint32_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PGS_H */
