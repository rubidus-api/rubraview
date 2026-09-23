#ifndef RUBRAVIEW_TAGS_H
#define RUBRAVIEW_TAGS_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * What a music file says about itself (RFC-0001 §3.14.2.1 and §3.14.2.4,
 * RV-076): its name, who made it, the record it came from — and the cover
 * that is stored inside it.
 *
 * This is read out of the file's own bytes, the way the subtitle pictures
 * are (D-22, D-27): no decoder, no FFmpeg, nothing that is only there on
 * one backend. Media Foundation exposes some of this and FFmpeg exposes
 * more, but neither exposes the same set, and a cover has to reach the
 * renderer as bytes in the end whichever way it is found.
 *
 * Only the head of the file is needed — tags and covers live there — so
 * the caller reads a few megabytes rather than a 300 MB record, and the
 * last 128 bytes when it wants ID3v1 as well.
 *
 * Every string points into the arena or into the bytes the caller passed,
 * and the cover always points into those bytes: it is not copied, so it
 * lives exactly as long as they do.
 */

typedef struct rubraview_tags_source {
    const uint8_t *head;       /* the file's first bytes; 4 MB is plenty */
    size_t         head_size;
    const uint8_t *tail;       /* the last 128 bytes, or NULL: ID3v1 lives there */
    size_t         tail_size;
    uint64_t       file_size;  /* the whole file, for the bitrate when nothing says */
} rubraview_tags_source_t;

typedef struct rubraview_tags {
    /* What the OSD says. Empty when the file does not say. */
    u8str_t  title;
    u8str_t  artist;
    u8str_t  album;
    u8str_t  year;
    u8str_t  track_number;     /* "7" or "7/12", as written */
    u8str_t  genre;

    /* What the file is. Zero when it does not say. */
    u8str_t  codec;            /* "FLAC", "MP3", "AAC", "Vorbis", "Opus", "WAV" */
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bits_per_sample;  /* 0 for a compressed format that does not say */
    uint32_t bitrate_kbps;     /* measured where it can be, else size and duration */
    double   duration_seconds; /* 0 when the head does not say */

    /* The cover, exactly as it is stored — JPEG or PNG bytes for the
       image loader. `art_size` is 0 when the file carries none. */
    const uint8_t *art;
    size_t         art_size;
    u8str_t        art_mime;
} rubraview_tags_t;

/** A cover larger than this is left alone: it is not a cover. */
#define RUBRAVIEW_TAGS_MAX_ART (16u * 1024u * 1024u)

/**
 * Read what the file says. Never fails: a file that says nothing, or one
 * whose tag is damaged, gives back an empty answer rather than nothing at
 * all, and a damaged tag costs that tag and not the rest.
 */
rubraview_tags_t rubraview_tags_read(proven_arena_t *arena, rubraview_tags_source_t source);

/** Is this a name a music file would have? — for the caller's own sorting. */
bool rubraview_tags_is_music_name(u8str_t filename);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_TAGS_H */
