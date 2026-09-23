#ifndef RUBRAVIEW_SUBTITLE_H
#define RUBRAVIEW_SUBTITLE_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Subtitles (RFC-0001 §3.16.1), RV-059.
 *
 * Four formats, and they have nothing in common but the idea: a piece
 * of text, a moment it appears, a moment it goes away. So that is what
 * they are all turned into here, and the differences — SubRip's
 * `00:01:02,500`, WebVTT's `00:01:02.500`, SAMI's milliseconds in an
 * HTML attribute, SubStation's centiseconds and its comma-separated
 * fields — stay inside their own readers.
 *
 * None of this needs FFmpeg: an external subtitle file is a text file.
 * Only the *embedded* streams of §3.16.1 point 2 need the demuxer.
 */

typedef enum rubraview_subtitle_format {
    RUBRAVIEW_SUBTITLE_UNKNOWN = 0,
    RUBRAVIEW_SUBTITLE_SRT,
    RUBRAVIEW_SUBTITLE_SMI,
    RUBRAVIEW_SUBTITLE_VTT,
    RUBRAVIEW_SUBTITLE_ASS,
    /* A DVD's pictures: `movie.idx` beside `movie.sub` (owner,
       2026-09-23). Not text, so `rubraview_subtitle_parse` says nothing
       about it — `vobsub.h` reads it. */
    RUBRAVIEW_SUBTITLE_VOBSUB,
    /* A Blu-ray's pictures: `movie.sup` beside the film (owner,
       2026-09-24). Not text either — `pgs.h` reads it. */
    RUBRAVIEW_SUBTITLE_PGS,
} rubraview_subtitle_format_t;

typedef struct rubraview_subtitle_cue {
    double  start_seconds;
    double  end_seconds;
    u8str_t text;        /* markup removed; a line break is '\n' */
    u8str_t language;    /* SAMI carries one per class; empty otherwise */
} rubraview_subtitle_cue_t;

typedef struct rubraview_subtitle_track {
    rubraview_subtitle_format_t format;
    rubraview_subtitle_cue_t   *cues;    /* sorted by start time */
    size_t                      count;
    u8str_t                     language;
    /* One SAMI file often holds several languages, one class each
       (owner, 2026-09-23). When this is set, only the cues of that
       language are shown; empty means every cue, as before. */
    u8str_t                     shown_language;
    double                      offset_seconds;  /* §3.16.1's Z/X adjustment */
} rubraview_subtitle_track_t;

/** Guess the format from the filename, before reading anything. */
rubraview_subtitle_format_t rubraview_subtitle_format_for_name(u8str_t filename);

/**
 * Parse a subtitle file. The format is taken from `format`, or worked
 * out from the content when `RUBRAVIEW_SUBTITLE_UNKNOWN` is passed —
 * a `.txt` holding SubRip is common enough to be worth handling.
 *
 * A malformed cue is skipped rather than aborting the file: one bad
 * timestamp in a thousand-line subtitle should cost that line, not the
 * whole track.
 */
rubraview_subtitle_track_t rubraview_subtitle_parse(proven_arena_t *arena, u8str_t text,
                                                    rubraview_subtitle_format_t format);

/**
 * The cue to show at a given moment, or NULL. The track's own offset is
 * applied here, so a caller that adjusts the sync does not have to
 * adjust anything else.
 */
const rubraview_subtitle_cue_t *rubraview_subtitle_at(const rubraview_subtitle_track_t *track,
                                                      double time_seconds);

/**
 * The languages a parsed file holds, in the order they first appear
 * (SAMI's classes; one entry, empty, for a file that names none).
 * Returns how many were written to `out`.
 */
size_t rubraview_subtitle_languages(const rubraview_subtitle_track_t *track, u8str_t *out, size_t max);

/** §3.16.1: Z and X move the whole track in half-second steps. */
void rubraview_subtitle_nudge(rubraview_subtitle_track_t *track, bool later);

/**
 * §3.16.1: the subtitle files that go with a video, found by sharing its
 * base name. Returns how many were written to `out`.
 */
typedef struct rubraview_subtitle_candidate {
    u8str_t path;
    rubraview_subtitle_format_t format;
} rubraview_subtitle_candidate_t;

/**
 * The language a subtitle file's name claims: `film.kor.srt` beside
 * `film.mkv` is "kor", `film.srt` claims nothing. §3.16.2's track list
 * shows it, so a reader with two files can tell them apart.
 */
u8str_t rubraview_subtitle_language_tag(u8str_t video_path, u8str_t subtitle_path);

size_t rubraview_subtitle_discover(u8str_t video_path,
                                   const u8str_t *sibling_paths, size_t sibling_count,
                                   rubraview_subtitle_candidate_t *out, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_SUBTITLE_H */
