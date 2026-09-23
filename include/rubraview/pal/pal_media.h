#ifndef RUBRAVIEW_PAL_MEDIA_H
#define RUBRAVIEW_PAL_MEDIA_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "rubraview/core.h"
#include "rubraview/mediaclock.h"
#include "rubraview/playback.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Media PAL (RFC-0001 §5, D-8, D-9). One interface, two backends: Windows
 * Media Foundation by default and FFmpeg as a replaceable second one.
 * No Win32 type appears here (D-1).
 *
 * Threads. `open` starts a decode thread that belongs to the media
 * object: it initialises COM and the decoder itself and never calls into
 * the imaging, drawing or arena code. Decoded frames reach the caller's
 * thread through a fixed ring of CPU buffers (rubraview_spsc_t); the
 * caller reads the oldest one, uploads it, and gives it back. Every
 * function below is called from that one caller thread.
 */

typedef struct rubraview_media rubraview_media_t;

typedef struct rubraview_media_info {
    rubraview_media_backend_t backend;   /* which backend opened it */
    double   duration_seconds;           /* 0 when the container does not say */
    double   frame_rate;                 /* 0 when unknown */
    int32_t  width, height;              /* decoded picture size */
    bool     has_video;
    bool     has_audio;
    bool     audio_output;               /* the sound reaches a device (false on a machine without one) */
    uint32_t video_fourcc;               /* the native codec, for messages (RUBRAVIEW_FOURCC order) */
    bool     hardware_decode;            /* RV-062: the pictures are decoded on the graphics card */
} rubraview_media_info_t;

/*
 * RV-062: what the caller offers for decoding on the graphics card.
 * `device` is the renderer's (rubraview_pal_render_video_device), opaque.
 * `mode`: 0 never, 1 when the card offers decoders, 2 always (diagnostic —
 * it takes the fall-back path on a card with none). A backend that cannot
 * use it, or finds it fails, decodes in software as before.
 */
typedef struct rubraview_media_gpu {
    void    *device;
    uint32_t decoder_profiles;
    int32_t  mode;
} rubraview_media_gpu_t;

typedef struct rubraview_media_open_result {
    rubraview_media_t        *media;     /* NULL on failure */
    rubraview_media_failure_t failure;   /* RUBRAVIEW_MEDIA_OPENED on success */
    rubraview_media_info_t    info;
} rubraview_media_open_result_t;

/** Whether a backend can be used on this machine (FFmpeg: are its DLLs here). */
bool rubraview_pal_media_backend_available(rubraview_media_backend_t backend);

/**
 * Open a file with one backend and start decoding from its beginning.
 * Blocks until the backend has said yes or no. The caller tries the
 * backends in rubraview_media_backend_order and reports the last failure.
 */
rubraview_media_open_result_t rubraview_pal_media_open(u8str_t path, rubraview_media_backend_t backend,
                                                       const rubraview_media_gpu_t *gpu);

/** Stops the decode thread and frees everything. NULL is ignored. */
void rubraview_pal_media_close(rubraview_media_t *media);

/** A decoded picture: 32-bit BGRA rows, top row first. */
typedef struct rubraview_video_frame {
    const uint8_t *pixels;
    int32_t width, height;
    int32_t stride;           /* bytes from one row to the next */
    double  pts;              /* presentation time, seconds from the file's start */
    double  duration;         /* seconds; 0 when unknown */
    /* RV-062: a frame still on the graphics card — `pixels` is NULL and
       this texture (opaque) at `gpu_subresource` holds the picture, BGRA,
       at least width x height. Copy it with rubraview_pal_texture_copy_video_frame. */
    const void *gpu_texture;
    uint32_t    gpu_subresource;
} rubraview_video_frame_t;

/**
 * The oldest decoded frame, without taking it. False when none is ready.
 * The pixels stay valid until rubraview_pal_media_pop_frame.
 */
bool rubraview_pal_media_peek_frame(rubraview_media_t *media, rubraview_video_frame_t *out_frame);

/** Give the oldest frame back to the decoder. */
void rubraview_pal_media_pop_frame(rubraview_media_t *media);

/**
 * How many decoded frames are waiting. A late frame is skipped only when
 * another one stands behind it — on a machine that cannot decode in real
 * time, skipping the last one would leave nothing to show at all.
 */
size_t rubraview_pal_media_frames_ready(rubraview_media_t *media);

/**
 * Seek. Asynchronous: frames decoded before the request are discarded
 * as they are read, and the first frame after it is the one whose
 * interval holds `seconds` (the decoder runs forward from the keyframe
 * before it, so the landing is exact, not "the nearest keyframe").
 */
void rubraview_pal_media_seek(rubraview_media_t *media, double seconds);

/** The decoder has reached the end and every decoded frame has been taken. */
bool rubraview_pal_media_finished(rubraview_media_t *media);

/** Pause or resume the sound (the picture is paused by the caller's clock). */
void rubraview_pal_media_set_paused(rubraview_media_t *media, bool paused);

/**
 * RV-075: how loud this player is, 0.0 to 1.0, on top of the program's
 * volume — what a crossfade between two tracks is made of. A player with
 * no sound, or none reaching a device, takes it and does nothing.
 */
void rubraview_pal_media_set_gain(rubraview_media_t *media, double gain);

/**
 * Where the listener is and when that was measured — the §5.3 master
 * clock. False when the file's sound is not playing on a device.
 */
bool rubraview_pal_media_audio_position(rubraview_media_t *media, double *out_position, double *out_wall);

/**
 * §3.16.2 / R135: the tracks the container holds. The set is worked out
 * once, on the decode thread, while the file is being opened; this call
 * copies it. The strings in it belong to the media object and stay
 * valid until it is closed.
 *
 * Text subtitle streams are not listed: neither backend decodes them
 * yet (D-10), and a track nobody can display is not a choice.
 */
bool rubraview_pal_media_tracks(rubraview_media_t *media, rubraview_track_set_t *out_set);

/**
 * §3.16.2: play a different sound track of the same file, by its
 * `stream_index` from the set above. The request is handed to the decode
 * thread, which owns the decoder; it takes effect within a frame or two.
 *
 * Seek to where the film is straight afterwards: that is what flushes
 * what the old track had already decoded, so picture and sound restart
 * together.
 *
 * False when the backend cannot: no sound is being played on this
 * machine at all, the index is not an audio track of this file, or the
 * new track cannot be decoded (the old one keeps playing).
 */
bool rubraview_pal_media_select_audio_track(rubraview_media_t *media, int32_t stream_index);

/**
 * §3.16.1 / D-12: a text subtitle stream carried inside the container,
 * handed over as SubRip text for the core parser. Empty when the
 * backend cannot read it — Media Foundation never can, and no backend
 * offers the picture-based subtitle formats, which is why those are not
 * listed as tracks at all.
 *
 * Reads the file a second time, on the calling thread, and returns when
 * it has the whole stream: the decode thread's own reading is not
 * disturbed. Long films take a moment.
 */
u8str_t rubraview_pal_media_read_subtitle_stream(rubraview_media_t *media, proven_arena_t *arena,
                                                 int32_t stream_index);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_MEDIA_H */
