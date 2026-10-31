#ifndef RUBRAVIEW_MP4_H
#define RUBRAVIEW_MP4_H

#include "rubraview/core.h"
#include "rubraview/subtitle.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * MP4, read by our own code (owner, 2026-10-14, RV-115): enough of the ISO
 * base media file format (ISO/IEC 14496-12) to find a film's subtitle
 * tracks and take the text of the 3GPP timed-text ones (`tx3g`, 3GPP TS
 * 26.245) out as SubRip. Written from those specifications alone.
 *
 * The file is bytes and a size (a mapping serves); the boxes of `moov` are
 * read, and a subtitle's samples are found through its tables and read
 * where they lie. Every size and count in the file is a claim checked
 * against what is there. A fragmented file (`moof`) is not read.
 */

#define RUBRAVIEW_MP4_MAX_TRACKS 32

typedef enum rubraview_mp4_sub {
    RUBRAVIEW_MP4_SUB_NONE = 0,   /* not a subtitle track */
    RUBRAVIEW_MP4_SUB_TX3G,       /* 3GPP timed text: written as SubRip */
    RUBRAVIEW_MP4_SUB_OTHER,      /* WebVTT, TTML, CEA-608, QuickTime text: not taken out */
} rubraview_mp4_sub_t;

typedef struct rubraview_mp4_track {
    uint32_t            id;
    uint32_t            handler;      /* 'vide', 'soun', 'sbtl', 'text', 'subt', ... */
    uint32_t            format;       /* the first sample entry's four letters */
    char                language[4];  /* from `mdhd`: "und" when not said */
    uint32_t            timescale;    /* ticks a second */
    rubraview_mp4_sub_t subtitle;
    /* the sample tables, where they stand in the file (their entries) */
    uint64_t stts_at, stsc_at, stsz_at, stco_at;
    uint32_t stts_count, stsc_count, stsz_count, stco_count;
    uint32_t stsz_size;           /* one size for every sample, or 0: the table has them */
    bool     co64;
    /* RV-202: `tkhd`'s size as shown (16.16 fixed point, before its matrix), and whether the matrix turns it a
       quarter (a phone's upright film is stored wide, turned by its matrix) */
    double   shown_width, shown_height;
    bool     quarter_turn;
} rubraview_mp4_track_t;

typedef struct rubraview_mp4 {
    const uint8_t        *data;
    uint64_t              size;
    rubraview_mp4_track_t tracks[RUBRAVIEW_MP4_MAX_TRACKS];
    size_t                track_count;
    uint64_t              moov_at, moov_end;
    uint32_t              movie_timescale;   /* RV-202: `mvhd`'s clock and the film's length in it; 0 when not said */
    uint64_t              movie_duration;
    uint64_t              fragment_duration; /* RV-210: `mvex/mehd`'s, read by rubraview_mp4_open_moov */
} rubraview_mp4_t;

/**
 * Read an MP4's head: its tracks and their tables. False, with a reason,
 * when it is not one, is fragmented, or its `moov` cannot be read.
 */
bool rubraview_mp4_open(rubraview_mp4_t *mp4, const uint8_t *data, uint64_t size, const char **out_why);

/* RV-210: a `moov` box alone (its bytes, from its header), for its facts: a fragmented file's is read
   too, its length from `mvex/mehd` when `mvhd` gives none. The sample walk is not for such an open. */
bool rubraview_mp4_open_moov(rubraview_mp4_t *mp4, const uint8_t *moov, uint64_t size);

/**
 * RV-202: the first video track's size as it is shown (its matrix's quarter turn applied) and the
 * film's length in seconds. False when there is no video track with a size; `*out_seconds` is
 * set whenever `mvhd` says it (0 otherwise), a song's too.
 */
bool rubraview_mp4_frame(const rubraview_mp4_t *mp4, int32_t *out_width, int32_t *out_height, double *out_seconds);

/* One sample of a track: where its bytes are, when it starts and for how long (in the track's ticks). */
typedef struct rubraview_mp4_sample {
    uint64_t offset;
    uint32_t size;
    uint64_t start, duration;
} rubraview_mp4_sample_t;

/* Where a walk through a track's samples stands. */
typedef struct rubraview_mp4_walk {
    uint32_t sample;          /* the next sample, from 0 */
    uint32_t chunk;           /* its chunk, from 0 */
    uint32_t left_in_chunk;   /* samples of that chunk not yet given */
    uint32_t stsc_index;
    uint64_t offset;          /* where the next sample is */
    uint32_t stts_index, stts_left;
    uint64_t time;
    bool     started;
} rubraview_mp4_walk_t;

/** The next sample of `track`; false after the last, or where the tables stop making sense. */
bool rubraview_mp4_sample_next(const rubraview_mp4_t *mp4, const rubraview_mp4_track_t *track, rubraview_mp4_walk_t *walk,
                               rubraview_mp4_sample_t *out);

/**
 * A `tx3g` sample written as a SubRip cue (numbered `counter`), into `out`
 * when it fits; returns the bytes it needs, 0 for a sample with no text
 * (a gap between subtitles), one cut short, or text that is not UTF-8.
 */
size_t rubraview_mp4_subtitle_cue(const rubraview_mp4_t *mp4, const rubraview_mp4_track_t *track, unsigned counter,
                                  const rubraview_mp4_sample_t *sample, char *out, size_t cap);

/* ---- a subtitle put into an MP4 film (RV-115, D-130) ----
 *
 * Nothing of the film moves: its `moov` becomes a `free` box of the same
 * size where it stands (four bytes change), every other byte stays at its
 * offset, and after the film's end come an `mdat` with the subtitle's
 * samples and a new `moov` — the old one's boxes as they were (`mvhd`'s
 * next track id raised), and one more track: 3GPP timed text (`tx3g`), as
 * FFmpeg writes it, its gaps filled with empty samples. The text is the
 * subtitle's words, its markup gone (an MP4 subtitle carries no tags).
 */

typedef struct rubraview_mp4_put_sample { uint32_t at, size; uint64_t start, duration; } rubraview_mp4_put_sample_t;

typedef struct rubraview_mp4_put {
    const rubraview_mp4_t       *mp4;
    uint64_t                     type_at;      /* where the old moov's four letters are */
    uint8_t                     *tail;         /* the mdat and the new moov */
    size_t                       tail_size;
    rubraview_mp4_put_sample_t  *samples;      /* the new track's, `at` inside `tail` */
    size_t                       sample_count;
    uint32_t                     track_id;
    uint64_t                     total;
    size_t                       index;        /* where `rubraview_mp4_put_step` stands */
    uint64_t                     offset;
} rubraview_mp4_put_t;

/**
 * Lay the new film out from a subtitle track read by `subtitle.h` (its
 * words, any format; a SAMI's language on screen only). `language` is three
 * letters or empty. False, with a reason, for a film this does not write:
 * fragmented, or one whose last box runs "to the end of the file".
 */
bool rubraview_mp4_put_plan(rubraview_mp4_put_t *put, const rubraview_mp4_t *mp4, const rubraview_subtitle_track_t *subtitle,
                            proven_u8str_view_t language, proven_u8str_view_t name, const char **out_why);
void rubraview_mp4_put_free(rubraview_mp4_put_t *put);

typedef bool (*rubraview_mp4_emit_fn)(void *ctx, const void *bytes, size_t size);
/** The next part of the new file, about `budget` bytes: 1 more, 0 written, -1 `emit` failed. */
int rubraview_mp4_put_step(rubraview_mp4_put_t *put, rubraview_mp4_emit_fn emit, void *ctx, size_t budget);

/**
 * The new film against the old (the owner's rule before a film is replaced):
 * every byte of the film the same but the four letters of `free`; the new
 * file's tracks the film's and one more; each old track's tables and
 * samples the same; the new track's samples the subtitle's; the size the
 * plan said. A slice at a time: 1 more, 0 sound, -1 not (`why`).
 */
typedef struct rubraview_mp4_check {
    rubraview_mp4_t made;
    uint64_t        at;
    int             phase;
    const char     *why;
} rubraview_mp4_check_t;

bool rubraview_mp4_check_begin(rubraview_mp4_check_t *check, const rubraview_mp4_put_t *put, const uint8_t *made, uint64_t made_size);
int rubraview_mp4_check_step(rubraview_mp4_check_t *check, const rubraview_mp4_put_t *put, const uint8_t *made, size_t budget);

#ifdef __cplusplus
}
#endif

#endif
