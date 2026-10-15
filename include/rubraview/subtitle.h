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

/**
 * Where a cue stands in the file's text as it was read (RV-115, D-126):
 * the start time as written, the end time, and the text with its markup.
 * Saving copies the file and writes only these places of the cues that
 * changed, so everything else is the original's, byte for byte. A length
 * of 0 means that part cannot be written: SAMI has no end of its own — a
 * caption ends at the next SYNC, and `end` is that SYNC's number only
 * when it is an empty one that does nothing but close this caption.
 */
typedef struct rubraview_subtitle_place {
    size_t start_at, start_len;
    size_t end_at, end_len;
    size_t text_at, text_len;
    /* RV-115: the whole cue — a SubRip or WebVTT block with the blank line
       after it, a `Dialogue:` line with its line end, a SAMI caption from
       its SYNC to the next — and, for SAMI, the empty SYNC that closes it
       when there is one. Taken out whole when the cue is removed; a cue
       added is made from a neighbour's. */
    size_t block_at, block_len;
    size_t close_at, close_len;
} rubraview_subtitle_place_t;

typedef struct rubraview_subtitle_cue {
    double  start_seconds;
    double  end_seconds;
    u8str_t text;        /* markup removed; a line break is '\n' */
    u8str_t language;    /* SAMI carries one per class; empty otherwise */
    rubraview_subtitle_place_t place;
    bool    time_changed;   /* moved since it was read */
    bool    removed;        /* RV-115: taken out; kept, unseen, for the save to take out its block */
    bool    added;          /* RV-115: new; the save writes it in, made from a neighbour's block */
    bool    text_changed;   /* edited: `written` is what goes into the file */
    u8str_t written;
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
    /* RV-115: the text the cues' places point into, kept for saving; the
       file it came from (empty for a subtitle held in a film) and whether
       it was not UTF-8 and was read in the machine's code page — both set
       by whoever read the file. */
    u8str_t                     source;
    u8str_t                     source_path;
    bool                        source_transcoded;
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
 * A cue's text as it is edited: what the file holds for it, markup and
 * all, on one line. A line break of a SubRip or WebVTT cue is shown as
 * SubStation writes one, `\N`; SAMI's own line ends are spaces (they mean
 * nothing there; `<br>` breaks a line). Empty when the cue has no place.
 */
u8str_t rubraview_subtitle_cue_typed(proven_arena_t *arena, const rubraview_subtitle_track_t *track, size_t index);

/**
 * Put an edited line in a cue's place: the cue shows it at once (markup
 * removed, as when read) and remembers what to write. `\N` typed is a
 * line break in every format. False when the cue has no place, or nothing
 * would be left to show.
 */
bool rubraview_subtitle_cue_retype(proven_arena_t *arena, rubraview_subtitle_track_t *track, size_t index, u8str_t typed);

/**
 * The track with its Z / X offset written into its times, for a save
 * (RV-115): a copy of the cues, each moved by `offset_seconds` (none before
 * the film's beginning) and marked moved, the offset 0. The track itself
 * is left as it is. With no offset, the track as it is.
 */
rubraview_subtitle_track_t rubraview_subtitle_offset_folded(proven_arena_t *arena, const rubraview_subtitle_track_t *track);

/** A time as typed: `1:02:03.45`, `02:03,5`, `63.5` (seconds). */
bool rubraview_subtitle_parse_time(u8str_t text, double *out_seconds);

/**
 * RV-115: a cue's times typed. `end` after `start`, neither below 0. The
 * cues stay in order of start, so the cue may change place: its new index
 * is returned, -1 when refused. (A SAMI caption's end is where the next
 * caption starts unless an empty SYNC closes it; only then is it written.)
 */
int32_t rubraview_subtitle_cue_set_time(rubraview_subtitle_track_t *track, size_t index, double start, double end);

/** RV-115: a cue taken out (unseen at once, its block out of the file when saved). False for none. */
bool rubraview_subtitle_cue_remove(rubraview_subtitle_track_t *track, size_t index);

/**
 * RV-115: a new cue, its text typed as `rubraview_subtitle_cue_retype`
 * takes it, in the language shown. Written, when saved, as a neighbour's
 * block is (its number, style, class and settings), after the cue before
 * it or before the first. Its index, or -1: no cue in the file to make it
 * from, nothing to show, or no room.
 */
int32_t rubraview_subtitle_cue_add(proven_arena_t *arena, rubraview_subtitle_track_t *track, double start, double end, u8str_t typed);

/** Has any cue been moved or edited since the track was read? */
bool rubraview_subtitle_changed(const rubraview_subtitle_track_t *track);

/**
 * The file's text with the changed cues written in (see
 * `rubraview_subtitle_place_t`): times in the format's own way — SubRip
 * `00:01:02,500`, WebVTT `00:01:02.500`, SubStation `0:01:02.50`, SAMI
 * milliseconds — and edited texts with the file's own line ends. With
 * nothing changed it is the source itself. Empty, and `out_why` says
 * why, when it cannot be done.
 */
u8str_t rubraview_subtitle_patched(proven_arena_t *arena, const rubraview_subtitle_track_t *track, const char **out_why);

/**
 * The name the file before is kept under when a subtitle is saved:
 * `film.smi` with 2 is `film.old2.smi`. The caller takes the first
 * number whose name is free.
 */
u8str_t rubraview_subtitle_backup_path(proven_arena_t *arena, u8str_t path, unsigned number);

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
 * The sync moved by `seconds` (owner, 2026-10-16: a tenth of a second with
 * Shift+Z / Shift+X), kept to the millisecond so that steps of 0.1 and 0.5
 * come back to 0 exactly. 0 seconds puts the sync back where the file has it.
 */
void rubraview_subtitle_shift(rubraview_subtitle_track_t *track, double seconds);

/** The sync back to the file's own times (Alt+Z). */
void rubraview_subtitle_sync_reset(rubraview_subtitle_track_t *track);

/**
 * What the playback line says of the sync: "sub +0.6 s" (a minus when
 * earlier), empty when it is 0. Written into `buffer`.
 */
u8str_t rubraview_subtitle_offset_text(char *buffer, size_t buffer_size, double offset_seconds);

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
