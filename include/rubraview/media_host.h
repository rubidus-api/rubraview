#ifndef RUBRAVIEW_MEDIA_HOST_H
#define RUBRAVIEW_MEDIA_HOST_H

#include "rubraview/core.h"
#include "rubraview/detail.h"
#include "rubraview/audio_chain.h"
#include "rubraview/audio_dsp.h"
#include "rubraview/ffilter.h"
#include "rubraview/mediaclock.h"
#include "rubraview/playback.h"
#include "rubraview/pal/pal_media.h"
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-176 (D-208, owner 2026-10-26: "FFmpeg를 별도 프로세스로", every use at once): FFmpeg's DLLs are loaded
 * only by a child, `rubraview.exe --media-host`, which shares one file mapping with the viewer — this block. A
 * child is somebody else's code fed a file that may be hostile, so the viewer treats all it writes as hostile:
 * it copies what it needs out of the block first and only then checks it (counts, offsets, indices, lengths,
 * text, finite numbers), with the functions here. The block holds no pointer and no `bool`: its wire structs
 * are integers and fixed text, turned into the program's own types by the `_take` functions.
 *
 * Playback needs no requests: the viewer writes what it wants (pause, seek, gain, a track, quit) and reads what
 * the child observed (where the sound is, the end, the rates, a station's title); the decoder's frame ring and
 * its slots live in the block, the slot pictures in a second mapping the child makes (`frame_map`), pictures
 * on the card as their textures' shared handles. The mailbox carries the few real requests: open, a subtitle
 * stream, and the one-shot jobs of the service child (a JPEG XL read, a picture written, a filter, a list).
 */

#define RUBRAVIEW_MHOST_MAGIC     0x4D485652u   /* "RVHM" */
#define RUBRAVIEW_MHOST_VERSION   1u
#define RUBRAVIEW_MHOST_SLOTS     4
#define RUBRAVIEW_MHOST_MAX_SIDE  16384
#define RUBRAVIEW_MHOST_TEXT      64
#define RUBRAVIEW_MHOST_PATH      1024
#define RUBRAVIEW_MHOST_NAME      64            /* a mapping's name */
#define RUBRAVIEW_MHOST_INLINE    65536         /* the mailbox's own bytes */
#define RUBRAVIEW_MHOST_LOG       4096
#define RUBRAVIEW_MHOST_ICY       256
#define RUBRAVIEW_MHOST_SUBTITLE_MAX (4u * 1024u * 1024u)
#define RUBRAVIEW_MHOST_MAP_MAX   ((uint64_t)1 << 32)   /* no payload mapping is larger */

typedef enum rubraview_mhost_mode {
    RUBRAVIEW_MHOST_PLAYBACK = 1,   /* one film, song or station */
    RUBRAVIEW_MHOST_SERVICE  = 2,   /* the one-shot jobs */
} rubraview_mhost_mode_t;

typedef enum rubraview_mhost_request {
    RUBRAVIEW_MHOST_REQ_NONE = 0,
    RUBRAVIEW_MHOST_REQ_OPEN,             /* inline: the path */
    RUBRAVIEW_MHOST_REQ_SUBTITLE,         /* args[0] the stream; reply: the SubRip text in reply_map */
    RUBRAVIEW_MHOST_REQ_CLOSE,
    RUBRAVIEW_MHOST_REQ_PROBE,            /* reply: args[0] available, [1] refusal, [2] place, [3] JPEG XL state,
                                             [4] encoders' bits, [5] filters' state; inline: the encoders' names */
    RUBRAVIEW_MHOST_REQ_JXL_DECODE,       /* request_map: the file; args[0] max side; reply_map: RGBA, args w, h */
    RUBRAVIEW_MHOST_REQ_JXL_FRAMES,       /* request_map: the file; args[0] max side; reply_map: frames (below) */
    RUBRAVIEW_MHOST_REQ_IMAGE_ENCODE,     /* request_map: the pixels; args w, h, format, still; reply_map: bytes */
    RUBRAVIEW_MHOST_REQ_PICTURES_ENCODE,  /* args w, h, count, still; frames asked one by one; reply_map: bytes */
    RUBRAVIEW_MHOST_REQ_PICTURE_GIVEN,    /* the viewer's answer to RUBRAVIEW_MHOST_NEED_PICTURE */
    RUBRAVIEW_MHOST_REQ_FETCH,            /* inline: the address; args[0] cap; reply_map: bytes */
    RUBRAVIEW_MHOST_REQ_FILTER_LIST,      /* reply_map: the catalog, packed */
    RUBRAVIEW_MHOST_REQ_FILTER_RUN,       /* inline: the graph; request_map: BGRA; args w, h; reply_map: BGRA */
    RUBRAVIEW_MHOST_REQ_COUNT
} rubraview_mhost_request_t;

typedef enum rubraview_mhost_status {
    RUBRAVIEW_MHOST_FAILED = -1,
    RUBRAVIEW_MHOST_OK = 0,
    RUBRAVIEW_MHOST_NEED_PICTURE = 1,     /* PICTURES_ENCODE: args[0] the index; request_map is where it goes */
} rubraview_mhost_status_t;

/* rubraview_media_info_t across the boundary. */
typedef struct rubraview_mhost_info {
    int32_t  backend;
    int32_t  width, height;
    int32_t  decode_threads, hdr_transfer;
    uint32_t video_fourcc, audio_sample_rate, audio_channels, bitrate_kbps;
    uint8_t  has_video, has_audio, audio_output, hardware_decode, hdr_tone_mapped, hdr_output, live, pad;
    double   duration_seconds, frame_rate;
    char     path_decoder[128], path_colours[128], path_handover[128], path_why_software[128], path_hdr_why[128];
} rubraview_mhost_info_t;

typedef struct rubraview_mhost_track {
    int32_t kind, stream_index, channels;
    uint8_t is_default, is_forced, pad[2];
    char    language[RUBRAVIEW_MHOST_TEXT], title[RUBRAVIEW_MHOST_TEXT], codec[RUBRAVIEW_MHOST_TEXT];
} rubraview_mhost_track_t;

typedef struct rubraview_mhost_detail {
    uint8_t heading;
    char    label[24];
    char    value[136];
} rubraview_mhost_detail_t;

/* One decoded picture's slot: what the decoder says of it. */
typedef struct rubraview_mhost_slot {
    double   pts, duration;
    uint64_t generation;
    uint64_t shared_handle;   /* the slot texture's (IDXGIResource::GetSharedHandle); 0 until made */
    uint32_t on_gpu;
    uint32_t pad;
} rubraview_mhost_slot_t;

/* Written by the viewer. */
typedef struct rubraview_mhost_wanted {
    _Atomic int32_t  paused;
    _Atomic int32_t  quit;
    _Atomic int64_t  seek_100ns;
    _Atomic uint64_t seek_generation;    /* set after seek_100ns; the frames of older ones are skipped */
    _Atomic int32_t  gain_millionths;
    _Atomic int32_t  audio_stream;
    _Atomic uint32_t audio_serial;       /* a new track asked */
} rubraview_mhost_wanted_t;

/* The viewer's process-wide sound settings, for the child's own sound (written by the viewer, `serial` last). */
typedef struct rubraview_mhost_audio {
    _Atomic uint32_t serial;
    uint32_t speed_permille, volume_permille, muted, latency_ms;
    int32_t  eq_preset;
    uint32_t eq_custom, night_mode;
    float    eq_gains_db[RUBRAVIEW_EQ_BANDS];
    double   gain;
} rubraview_mhost_audio_t;

/* The latest sound the child played, for the viewer's analyser (written by the child under `seq`). */
#define RUBRAVIEW_MHOST_TAP 4096
typedef struct rubraview_mhost_tap {
    _Atomic uint32_t seq;
    uint32_t count;
    float    samples[RUBRAVIEW_MHOST_TAP];
} rubraview_mhost_tap_t;

/* Written by the child, under `seq` (odd while it writes). */
typedef struct rubraview_mhost_observed {
    _Atomic uint32_t seq;
    int32_t  position_known;
    double   position, wall;             /* where the sound is, and when (the system's clock, the same in both) */
    uint64_t end_generation;             /* the decoder reached the end of this seek's run */
    int32_t  drained;                    /* the device holds no sound to play */
    uint32_t played_rate, played_channels;
    double   tone_peak;
    uint32_t icy_serial;
    char     icy_title[RUBRAVIEW_MHOST_ICY];
} rubraview_mhost_observed_t;

typedef struct rubraview_mhost_block {
    uint32_t magic, version;
    uint64_t bytes;                      /* the mapping's size as the viewer made it (the viewer keeps its own) */
    uint32_t mode;
    uint32_t pad0;

    /* the handshake, written by the viewer before the child starts */
    uint64_t adapter_luid;               /* the card the renderer uses; 0: none */
    int32_t  gpu_mode, tone_mapping;
    double   hdr_screen_peak, hdr_sdr_white;
    char     ffmpeg_imported[RUBRAVIEW_MHOST_PATH], ffmpeg_chosen[RUBRAVIEW_MHOST_PATH];
    int32_t  fault_kind, fault_after;    /* test builds only: crash (1) or hang (2) after so many frames */

    /* the mailbox: the viewer raises request_serial and signals; the child answers with reply_serial equal */
    _Atomic uint32_t request_serial;
    uint32_t request_kind;
    int64_t  request_args[8];
    double   request_real[4];
    uint64_t request_inline_bytes;
    char     request_map[RUBRAVIEW_MHOST_NAME];
    uint64_t request_map_bytes;
    _Atomic uint32_t reply_serial;
    int32_t  reply_status;
    int64_t  reply_args[8];
    uint64_t reply_inline_bytes;
    char     reply_map[RUBRAVIEW_MHOST_NAME];
    uint64_t reply_map_bytes;
    char     reply_why[256];
    uint8_t  inline_data[RUBRAVIEW_MHOST_INLINE];

    /* playback */
    rubraview_mhost_wanted_t   wanted;
    rubraview_mhost_observed_t observed;
    rubraview_mhost_audio_t    audio;
    rubraview_mhost_tap_t      tap;
    rubraview_spsc_t           ring;     /* head the child's, tail the viewer's */
    rubraview_mhost_slot_t     slots[RUBRAVIEW_MHOST_SLOTS];
    char     frame_map[RUBRAVIEW_MHOST_NAME];
    uint64_t frame_bytes;                /* one slot's picture: width * height * 4 */
    int32_t  open_failure;
    int32_t  track_count, current_video, current_audio, current_subtitle;
    rubraview_mhost_info_t  info;
    rubraview_mhost_track_t tracks[RUBRAVIEW_MAX_TRACKS];
    uint32_t detail_count;
    rubraview_mhost_detail_t details[RUBRAVIEW_DETAIL_MAX];

    _Atomic uint64_t heartbeat;          /* the child counts while it lives */
    _Atomic uint32_t log_used;
    char     log[RUBRAVIEW_MHOST_LOG];   /* FFmpeg's last words */
} rubraview_mhost_block_t;

/** The block's header, ready for a child of `mode`. */
void rubraview_mhost_init(rubraview_mhost_block_t *b, rubraview_mhost_mode_t mode);

/** The child's check of the block it was given. */
bool rubraview_mhost_block_ok(const rubraview_mhost_block_t *b, uint64_t mapped_bytes);

/* ---- what the child writes (it trusts itself) ---- */

void rubraview_mhost_info_put(rubraview_mhost_info_t *w, const rubraview_media_info_t *info);
void rubraview_mhost_tracks_put(rubraview_mhost_block_t *b, const rubraview_track_set_t *tracks);
void rubraview_mhost_details_put(rubraview_mhost_block_t *b, const rubraview_details_t *details);
/** Text into a fixed field, cut on a UTF-8 boundary, always ended. */
void rubraview_mhost_text_put(char *dst, size_t cap, const char *src);
/** Appends to the log ring's tail; old lines go first. */
void rubraview_mhost_log(rubraview_mhost_block_t *b, const char *line);

/* ---- what the viewer reads (all of it hostile) ---- */

/** A fixed text field made safe: ended within its size, valid UTF-8 (anything else a '?'), no control bytes. */
void rubraview_mhost_text_take(char *dst, size_t cap, const char *src, size_t src_cap);
bool rubraview_mhost_info_take(const rubraview_mhost_info_t *w, rubraview_media_info_t *out);

/** The open reply: copied out of the block, then checked. `text` holds the tracks' words, which `tracks` points to. */
typedef struct rubraview_mhost_opened {
    rubraview_media_failure_t failure;
    rubraview_media_info_t    info;
    rubraview_track_set_t     tracks;
    char                      text[RUBRAVIEW_MAX_TRACKS][3][RUBRAVIEW_MHOST_TEXT];
    rubraview_details_t       details;
    char                      frame_map[RUBRAVIEW_MHOST_NAME];
    uint64_t                  frame_bytes;
    const char               *why;   /* refused: which part (a literal) */
} rubraview_mhost_opened_t;

/** True when the child said it opened and every part of what it said is in range. */
bool rubraview_mhost_opened_take(const rubraview_mhost_block_t *b, rubraview_mhost_opened_t *out);
/** Tracks and details from a copy, alone (the fuzzer's way in). */
bool rubraview_mhost_tracks_take(const rubraview_mhost_track_t *w, int32_t count, int32_t current_video, int32_t current_audio,
                                 int32_t current_subtitle, rubraview_track_set_t *out,
                                 char text[RUBRAVIEW_MAX_TRACKS][3][RUBRAVIEW_MHOST_TEXT]);
bool rubraview_mhost_details_take(const rubraview_mhost_detail_t *w, uint32_t count, rubraview_details_t *out);

/** A slot's word, copied and checked; false when it is not a picture the viewer can show. */
bool rubraview_mhost_slot_take(const rubraview_mhost_slot_t *w, rubraview_mhost_slot_t *out);

/** How many pictures wait, from the child's head and the viewer's own tail; -1 when the head is impossible. */
int32_t rubraview_mhost_ring_waiting(size_t head, size_t tail);

typedef struct rubraview_mhost_seen {
    bool     position_known;
    double   position, wall;
    uint64_t end_generation;
    bool     drained;
    uint32_t played_rate, played_channels;
    double   tone_peak;
    uint32_t icy_serial;
    char     icy_title[RUBRAVIEW_MHOST_ICY];
} rubraview_mhost_seen_t;

/** What the child observed, read consistently (a few tries) and checked. */
bool rubraview_mhost_observed_take(const rubraview_mhost_observed_t *o, rubraview_mhost_seen_t *out);
/** The child's side of it. */
void rubraview_mhost_observed_put(rubraview_mhost_observed_t *o, const rubraview_mhost_seen_t *in);

/** The sound settings as the child applies them: in range whatever the block held. */
void rubraview_mhost_audio_take(const rubraview_mhost_audio_t *w, rubraview_audio_chain_config_t *chain, double *speed,
                                double *volume, bool *muted, uint32_t *latency_ms);
/** The child's latest samples into `out` (at most `count`), read consistently; each finite and within ±4. How many. */
size_t rubraview_mhost_tap_take(const rubraview_mhost_tap_t *t, float *out, size_t count);
void rubraview_mhost_tap_put(rubraview_mhost_tap_t *t, const float *samples, size_t count);

/** A mapping's name for this process, unguessable: `Local\rubraview-mh-<pid>-<32 hex>`. */
void rubraview_mhost_map_name(char out[RUBRAVIEW_MHOST_NAME], uint32_t pid, const uint8_t random16[16]);
/** A name the child gave: only that form, nothing else is opened. */
bool rubraview_mhost_map_name_ok(const char *name, size_t cap);

/** A reply's size: within its mapping kind's limit and not past `limit`. */
bool rubraview_mhost_size_ok(uint64_t bytes, uint64_t limit);

/* ---- payloads ---- */

/* JPEG XL's moving pictures: u32 count, then per picture i32 width, i32 height, f64 seconds, u64 offset; the
   pictures (RGBA) at their offsets. */
typedef struct rubraview_mhost_frame_ref {
    int32_t  width, height;
    double   seconds;
    uint64_t offset;
} rubraview_mhost_frame_ref_t;

#define RUBRAVIEW_MHOST_FRAMES_MAX 4096
/** The table's size for `count` pictures. */
size_t rubraview_mhost_frames_head(uint32_t count);
/** Picture `index` of a frames payload of `size` bytes, checked; false when it does not lie inside. */
bool rubraview_mhost_frame_at(const uint8_t *payload, size_t size, uint32_t index, int32_t max_side,
                              rubraview_mhost_frame_ref_t *out);
uint32_t rubraview_mhost_frame_count(const uint8_t *payload, size_t size);

/* FFmpeg's filter catalog as bytes: the child packs it, the viewer unpacks it into its arena. */
size_t rubraview_mhost_catalog_pack(const rubraview_ffilter_catalog_t *catalog, uint8_t *out, size_t cap);
bool rubraview_mhost_catalog_unpack(proven_arena_t *arena, const uint8_t *data, size_t size,
                                    rubraview_ffilter_catalog_t *out);

#ifdef __cplusplus
}
#endif

#endif
