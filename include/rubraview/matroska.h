#ifndef RUBRAVIEW_MATROSKA_H
#define RUBRAVIEW_MATROSKA_H

#include "rubraview/core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Matroska, read by our own code (owner, 2026-10-14, RV-115): enough of
 * it to find a film's subtitle tracks and take their text out, with no
 * FFmpeg. Written from the specifications alone — RFC 8794 (EBML) and
 * RFC 9559 (Matroska) — and nothing of anyone's implementation.
 *
 * The file is bytes and a size (a mapping serves; nothing is copied), and
 * only headers are read: a film of many gigabytes is walked by stepping
 * from one element to the next. Every size in the file is taken as a
 * claim to be checked against what is left of it.
 */

#define RUBRAVIEW_MKV_MAX_TRACKS 64

/* What a subtitle track holds, as far as taking it out goes. */
typedef enum rubraview_mkv_sub {
    RUBRAVIEW_MKV_SUB_NONE = 0,   /* not a subtitle track */
    RUBRAVIEW_MKV_SUB_SRT,        /* S_TEXT/UTF8: plain text, written as SubRip */
    RUBRAVIEW_MKV_SUB_ASS,        /* S_TEXT/ASS, S_TEXT/SSA: SubStation, its header in the track */
    RUBRAVIEW_MKV_SUB_PICTURE,    /* VobSub, PGS, DVB: pictures, not taken out */
    RUBRAVIEW_MKV_SUB_OTHER,      /* a subtitle codec this does not write (WebVTT, ...) */
} rubraview_mkv_sub_t;

typedef struct rubraview_mkv_track {
    uint64_t            number;        /* as blocks name it */
    uint8_t             type;          /* 1 video, 2 sound, 0x11 subtitles */
    u8str_t             codec;         /* CodecID, in the file */
    u8str_t             language;      /* BCP 47 when given, else the three letters; "eng" when neither (the format's default) */
    u8str_t             name;
    const uint8_t      *private_data;  /* CodecPrivate, in the file */
    size_t              private_size;
    bool                encoded;       /* compressed or encrypted (ContentEncodings): its blocks are not text as they stand */
    rubraview_mkv_sub_t subtitle;
} rubraview_mkv_track_t;

typedef struct rubraview_mkv {
    const uint8_t        *data;
    uint64_t              size;
    uint64_t              body_at, body_end;   /* the Segment's children */
    uint64_t              timestamp_scale;     /* nanoseconds a tick */
    rubraview_mkv_track_t tracks[RUBRAVIEW_MKV_MAX_TRACKS];
    size_t                track_count;
} rubraview_mkv_t;

/**
 * Read the head of a Matroska (or WebM) file: its segment, the scale of
 * its clock and its tracks. False, with a reason, when it is not one or
 * its head cannot be read. A file cut short opens if its tracks are whole.
 */
bool rubraview_mkv_open(rubraview_mkv_t *mkv, const uint8_t *data, uint64_t size, const char **out_why);

/** The track's place in `tracks` for a block's track number, or -1. */
int32_t rubraview_mkv_track_index(const rubraview_mkv_t *mkv, uint64_t number);

/* One block of the film: whose it is, when, and its bytes in the file. */
typedef struct rubraview_mkv_block {
    uint64_t       track;
    int64_t        start_ns;
    int64_t        duration_ns;   /* -1 when the file gives none (a SimpleBlock) */
    const uint8_t *payload;
    size_t         size;
    bool           laced;         /* several frames in one block: not one text */
} rubraview_mkv_block_t;

/* Where a walk through the clusters stands. */
typedef struct rubraview_mkv_walk {
    uint64_t at;
    uint64_t cluster_end;
    uint64_t cluster_time;
    bool     in_cluster;
    bool     cluster_open;   /* a cluster of unknown size: it ends where something that is not its own begins */
} rubraview_mkv_walk_t;

void rubraview_mkv_walk_begin(const rubraview_mkv_t *mkv, rubraview_mkv_walk_t *walk);

/**
 * The next block, of any track, in the file's order. False at the end of
 * the file, or where it cannot be read further (cut short, or damaged):
 * what came before stands. A caller with a large file calls this a slice
 * at a time; `rubraview_mkv_walk_share` says how far it is.
 */
bool rubraview_mkv_walk_next(const rubraview_mkv_t *mkv, rubraview_mkv_walk_t *walk, rubraview_mkv_block_t *out);

/** How much of the file the walk has passed, 0 to 1. */
double rubraview_mkv_walk_share(const rubraview_mkv_t *mkv, const rubraview_mkv_walk_t *walk);

/**
 * A subtitle track written out as a file, piece by piece. Each call
 * writes into `out` when it fits and returns the bytes it needs (so a
 * caller grows its room and asks again); 0 when there is nothing to write.
 *
 * The head: nothing for SubRip; for SubStation the track's own header,
 * with an `[Events]` section if it brought none.
 * A cue: `counter` is its number in a SubRip file (from 1). A block
 * without a length is given three seconds. SubStation blocks carry
 * `ReadOrder,Layer,Style,...,Text`; the line written is
 * `Dialogue: Layer,Start,End,Style,...,Text`.
 */
size_t rubraview_mkv_subtitle_head(const rubraview_mkv_track_t *track, char *out, size_t cap);
size_t rubraview_mkv_subtitle_cue(const rubraview_mkv_track_t *track, unsigned counter, const rubraview_mkv_block_t *block,
                                  char *out, size_t cap);

/**
 * The name a track is taken out under, beside the film: the film's name
 * with its extension replaced by the language and the subtitle's own —
 * `film.kor.srt`; `film.srt` when the language is not said ("und");
 * `film.kor.2.srt` for the second track of a language (`ordinal` 1).
 * Only letters, digits and '-' of the language are used. Returns the
 * length written (with a terminator), 0 when it does not fit.
 */
size_t rubraview_mkv_subtitle_name(const rubraview_mkv_track_t *track, u8str_t film_path, unsigned ordinal, char *out, size_t cap);

/** ".srt" or ".ass" for a track that can be taken out; "" otherwise. */
const char *rubraview_mkv_subtitle_ext(const rubraview_mkv_track_t *track);

#ifdef __cplusplus
}
#endif

#endif
