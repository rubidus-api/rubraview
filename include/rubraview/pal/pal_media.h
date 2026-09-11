#ifndef RUBRAVIEW_PAL_MEDIA_H
#define RUBRAVIEW_PAL_MEDIA_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "rubraview/core.h"
#include "rubraview/mediaclock.h"

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
} rubraview_media_info_t;

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
rubraview_media_open_result_t rubraview_pal_media_open(u8str_t path, rubraview_media_backend_t backend);

/** Stops the decode thread and frees everything. NULL is ignored. */
void rubraview_pal_media_close(rubraview_media_t *media);

/** A decoded picture: 32-bit BGRA rows, top row first. */
typedef struct rubraview_video_frame {
    const uint8_t *pixels;
    int32_t width, height;
    int32_t stride;           /* bytes from one row to the next */
    double  pts;              /* presentation time, seconds from the file's start */
    double  duration;         /* seconds; 0 when unknown */
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
 * Where the listener is and when that was measured — the §5.3 master
 * clock. False when the file's sound is not playing on a device.
 */
bool rubraview_pal_media_audio_position(rubraview_media_t *media, double *out_position, double *out_wall);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_MEDIA_H */
