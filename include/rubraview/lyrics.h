#ifndef RUBRAVIEW_LYRICS_H
#define RUBRAVIEW_LYRICS_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Synced lyrics and cue sheets (RFC-0001 §3.14.3, §3.14.4), RV-078 and
 * RV-079. Both are text files that sit next to an audio file, so
 * neither needs the decoder.
 */

/* ---- §3.14.3 `.lrc` ---- */

typedef struct rubraview_lyric_line {
    double  time_seconds;
    u8str_t text;
} rubraview_lyric_line_t;

typedef struct rubraview_lyrics {
    rubraview_lyric_line_t *lines;   /* sorted by time */
    size_t count;

    u8str_t title, artist, album;    /* from the `[ti:]`, `[ar:]`, `[al:]` tags */
    double  offset_seconds;          /* the `[offset:]` tag, in seconds */
    bool    untimed;                 /* words with no times (ID3 USLT and the like): every line's time is -1 */
} rubraview_lyrics_t;

/**
 * Parse an `.lrc`. One line may carry several timestamps — a chorus is
 * written once and pointed at from each place it occurs — and each of
 * them becomes its own entry.
 */
rubraview_lyrics_t rubraview_lyrics_parse(proven_arena_t *arena, u8str_t text);

/**
 * Words stored in the music file itself (owner, 2026-10-01): text that is
 * LRC is read as timed; any other text becomes untimed lines, blank lines
 * kept out.
 */
rubraview_lyrics_t rubraview_lyrics_from_text(proven_arena_t *arena, u8str_t text);

/** Timed lines from elsewhere (ID3 SYLT), copied and put in time order. */
rubraview_lyrics_t rubraview_lyrics_from_lines(proven_arena_t *arena, const rubraview_lyric_line_t *lines, size_t count);

/**
 * Untimed words follow the song through its length: the line about as far
 * into the words as the song is into itself. -1 when nothing fits.
 */
int32_t rubraview_lyrics_untimed_index(const rubraview_lyrics_t *lyrics, double time_seconds, double duration_seconds);

/**
 * The line that should be highlighted at a given moment (the song's own
 * position; the `[offset:]` is applied here), or -1. Untimed words: -1.
 */
int32_t rubraview_lyrics_index_at(const rubraview_lyrics_t *lyrics, double time_seconds);

/**
 * §3.14.3: clicking a line seeks to it. Returns the time, or a negative
 * number when the index is not a line.
 */
double rubraview_lyrics_time_of(const rubraview_lyrics_t *lyrics, int32_t index);

/* ---- §3.14.4 `.cue` ---- */

typedef struct rubraview_cue_track {
    int32_t  number;
    u8str_t  title;
    u8str_t  performer;
    double   start_seconds;   /* from INDEX 01 */
    double   end_seconds;     /* the next track's start; the file's length for the last */
} rubraview_cue_track_t;

typedef struct rubraview_cue_sheet {
    u8str_t  album_title;
    u8str_t  album_performer;
    u8str_t  audio_file;      /* the single image the tracks are cut from */
    rubraview_cue_track_t *tracks;
    size_t   count;
} rubraview_cue_sheet_t;

/**
 * Parse a `.cue`. `total_seconds` closes the last track; pass 0 when the
 * length is not known yet and the last track is left open-ended.
 *
 * The times are `mm:ss:ff`, where the third field is *frames* — 75 of
 * them to a second, because the format was written for audio CDs. That
 * is the detail every naive cue parser gets wrong.
 */
rubraview_cue_sheet_t rubraview_cue_parse(proven_arena_t *arena, u8str_t text, double total_seconds);

/**
 * Does the sheet's `FILE` name this audio file (its name, without the
 * folder)? Letter case is ignored, and so is the extension when the rest
 * matches: a sheet written for `Album.wav` still cuts `Album.flac`.
 */
bool rubraview_cue_names_audio(const rubraview_cue_sheet_t *sheet, u8str_t audio_name);

/** Which track a moment falls in, or -1. */
int32_t rubraview_cue_track_at(const rubraview_cue_sheet_t *sheet, double time_seconds);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_LYRICS_H */
