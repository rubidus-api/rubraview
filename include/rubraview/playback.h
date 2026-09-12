#ifndef RUBRAVIEW_PLAYBACK_H
#define RUBRAVIEW_PLAYBACK_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The playback model (RFC-0001 §5.3, §5.5, §3.16.2), RV-056, RV-057,
 * RV-060.
 *
 * A player is a decoder and a clock. The decoder needs FFmpeg; the
 * clock does not, and neither does anything that decides *where the
 * clock should be*: what a step key does at the end of a file, where an
 * A-B loop sends it, which track a language preference picks, or what
 * the seek bar's brackets look like. Those are here, and the host suite
 * checks them — which matters because they are the parts that are wrong
 * in most players.
 */

/* ---- §5.5 A-B looping ---- */

typedef struct rubraview_ab_loop {
    double point_a;     /* negative when unset */
    double point_b;
    bool   active;      /* both points set, and B after A */
} rubraview_ab_loop_t;

rubraview_ab_loop_t rubraview_ab_create(void);

/**
 * §5.5: `[` sets A, `]` sets B. Setting B before A is a mistake worth
 * being kind about — the two are swapped rather than refused, because
 * the reader plainly meant the region between them.
 */
void rubraview_ab_set_a(rubraview_ab_loop_t *loop, double seconds);
void rubraview_ab_set_b(rubraview_ab_loop_t *loop, double seconds);
void rubraview_ab_clear(rubraview_ab_loop_t *loop);

/**
 * Where playback should jump to, or a negative number for "carry on".
 * Called once per frame with the clock's position.
 */
double rubraview_ab_wrap(const rubraview_ab_loop_t *loop, double position_seconds);

/** The bracketed region as fractions of the whole, for the seek bar. */
bool rubraview_ab_bar_region(const rubraview_ab_loop_t *loop, double duration_seconds,
                             double *out_start_fraction, double *out_end_fraction);

/* ---- §5.3 seeking and stepping ---- */

typedef enum rubraview_seek_kind {
    RUBRAVIEW_SEEK_ABSOLUTE = 0,
    RUBRAVIEW_SEEK_RELATIVE,
    RUBRAVIEW_SEEK_FRAME,      /* one frame, at the file's own rate */
} rubraview_seek_kind_t;

/**
 * Where a seek lands. Clamped into the file, and — with a loop active —
 * clamped into the loop, because seeking outside a loop the reader set
 * would silently cancel it.
 */
double rubraview_seek_target(double position_seconds, double duration_seconds,
                             const rubraview_ab_loop_t *loop,
                             rubraview_seek_kind_t kind, double amount,
                             double frame_rate);

/** Where the pointer landed on the seek bar, as a time. */
double rubraview_seekbar_time(double bar_x, double bar_width, double click_x, double duration_seconds);

/** Where a time sits on the seek bar, as a fraction. */
double rubraview_seekbar_fraction(double position_seconds, double duration_seconds);

/**
 * `hh:mm:ss.mmm`, or `mm:ss.mmm` for anything under an hour — what the
 * HUD shows. Writes into `buffer` and returns the slice.
 */
u8str_t rubraview_format_timecode(char *buffer, size_t buffer_size, double seconds, bool with_milliseconds);

/* ---- §3.16.2 track switching ---- */

typedef enum rubraview_track_kind {
    RUBRAVIEW_TRACK_VIDEO = 0,
    RUBRAVIEW_TRACK_AUDIO,
    RUBRAVIEW_TRACK_SUBTITLE,
} rubraview_track_kind_t;

typedef struct rubraview_track {
    rubraview_track_kind_t kind;
    int32_t  stream_index;   /* the container's own numbering */
    u8str_t  language;       /* "kor", "eng", "jpn"; empty when unlabelled */
    u8str_t  title;          /* "Commentary", "Director's cut" */
    u8str_t  codec;          /* "aac", "flac", "subrip" */
    int32_t  channels;       /* audio only */
    bool     is_default;     /* the container's own default flag */
    bool     is_forced;      /* subtitles: forced narrative signs */
    /* Subtitles come from two places: streams inside the container and
       files beside it. The reader cycles through both in one list, and
       picking one has to know which kind it is (D-12). */
    bool     is_external;
} rubraview_track_t;

#define RUBRAVIEW_MAX_TRACKS 32

typedef struct rubraview_track_set {
    rubraview_track_t tracks[RUBRAVIEW_MAX_TRACKS];
    size_t count;

    int32_t current_video;      /* index into `tracks`, or -1 */
    int32_t current_audio;
    int32_t current_subtitle;   /* -1 means subtitles are off, which is a real choice */
} rubraview_track_set_t;

rubraview_track_set_t rubraview_tracks_create(void);
bool rubraview_tracks_add(rubraview_track_set_t *set, rubraview_track_t track);

/**
 * §3.16.2: pick the track to start with. A preferred language wins; then
 * the container's default flag; then the first of its kind. A forced
 * subtitle track is not chosen as the main one — it exists to caption
 * signs in a film someone is watching in the original language.
 */
int32_t rubraview_tracks_choose(const rubraview_track_set_t *set, rubraview_track_kind_t kind,
                                u8str_t preferred_language);

/**
 * Step to the next track of one kind. Subtitles include "off" in the
 * cycle, since turning them off is what the reader usually wants next.
 */
int32_t rubraview_tracks_next(const rubraview_track_set_t *set, rubraview_track_kind_t kind,
                              int32_t current);

/** How many tracks of one kind there are — what the menu needs. */
size_t rubraview_tracks_count(const rubraview_track_set_t *set, rubraview_track_kind_t kind);

/** A human label: "#2  Korean (AAC 2.0)". */
u8str_t rubraview_track_label(char *buffer, size_t buffer_size,
                              const rubraview_track_set_t *set, int32_t index);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PLAYBACK_H */
