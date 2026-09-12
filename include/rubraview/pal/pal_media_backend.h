#ifndef RUBRAVIEW_PAL_MEDIA_BACKEND_H
#define RUBRAVIEW_PAL_MEDIA_BACKEND_H

#include "rubraview/pal/pal_media.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * One media backend, behind the public media PAL (D-8). Media Foundation
 * and FFmpeg each fill in this table; `pal_media_win32.c` holds the
 * public calls, remembers which backend opened a file, and forwards.
 *
 * `impl` is the backend's own handle — each backend knows only its own.
 */
typedef struct rubraview_media_backend_api {
    /** Usable on this machine at all (the DLLs are there, at a version we know). */
    bool (*available)(void);

    /** Opens a file, or returns NULL and says why. */
    void *(*open)(u8str_t path, rubraview_media_failure_t *out_failure, rubraview_media_info_t *out_info);

    void  (*close)(void *impl);
    bool  (*peek_frame)(void *impl, rubraview_video_frame_t *out_frame);
    void  (*pop_frame)(void *impl);
    size_t (*frames_ready)(void *impl);
    void  (*seek)(void *impl, double seconds);
    bool  (*finished)(void *impl);
    void  (*set_paused)(void *impl, bool paused);
    bool  (*audio_position)(void *impl, double *out_position, double *out_wall);

    /** The container's tracks, as found while opening (§3.16.2). */
    bool  (*tracks)(void *impl, rubraview_track_set_t *out_set);

    /** Ask the decode thread to play another sound track (§3.16.2). */
    bool  (*select_audio_track)(void *impl, int32_t stream_index);

    /** A subtitle stream inside the file, as SubRip text (D-12). */
    u8str_t (*read_subtitle_stream)(void *impl, proven_arena_t *arena, int32_t stream_index);
} rubraview_media_backend_api_t;

const rubraview_media_backend_api_t *rubraview_media_backend_mf(void);
const rubraview_media_backend_api_t *rubraview_media_backend_ffmpeg(void);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_MEDIA_BACKEND_H */
