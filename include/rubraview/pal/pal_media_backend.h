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
    void *(*open)(proven_u8str_view_t path, rubraview_media_failure_t *out_failure, rubraview_media_info_t *out_info,
                  const rubraview_media_gpu_t *gpu);

    void  (*close)(void *impl);
    bool  (*peek_frame)(void *impl, rubraview_video_frame_t *out_frame);
    void  (*pop_frame)(void *impl);
    size_t (*frames_ready)(void *impl);
    void  (*seek)(void *impl, double seconds);
    bool  (*finished)(void *impl);
    bool  (*sound_over)(void *impl);   /* the device holds no sound to play; NULL: never */
    void  (*set_paused)(void *impl, bool paused);
    bool  (*audio_position)(void *impl, double *out_position, double *out_wall);
    /** D-120: the rate and channels the sound is played at now — not always
        what it was opened as. May be NULL; false when no sound is played. */
    bool  (*audio_format)(void *impl, uint32_t *out_rate, uint32_t *out_channels);
    /** D-120: the pictures' rate as measured from their times, when it is
        not what the file was opened as. May be NULL; false until known. */
    bool  (*picture_rate)(void *impl, double *out_rate);
    /** RV-150: the peak an HDR film is tone mapped from. May be NULL. */
    bool  (*hdr_peak)(void *impl, double *out_nits);
    /** RV-158: what a station says is playing (ICY). May be NULL. */
    bool  (*stream_title)(void *impl, char *out, size_t cap);

    /** The container's tracks, as found while opening (§3.16.2). */
    bool  (*tracks)(void *impl, rubraview_track_set_t *out_set);

    /** What the file says about itself, as found while opening. May be NULL. */
    const rubraview_details_t *(*details)(void *impl);

    /** Ask the decode thread to play another sound track (§3.16.2). */
    bool  (*select_audio_track)(void *impl, int32_t stream_index);
    /* RV-075: this player's own level, for a crossfade. May be NULL —
       a backend without one simply plays at full. */
    void  (*set_gain)(void *impl, double gain);

    /** A subtitle stream inside the file, as SubRip text (D-12). */
    proven_u8str_view_t (*read_subtitle_stream)(void *impl, proven_arena_t *arena, int32_t stream_index);
} rubraview_media_backend_api_t;

const rubraview_media_backend_api_t *rubraview_media_backend_mf(void);
const rubraview_media_backend_api_t *rubraview_media_backend_ffmpeg(void);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_MEDIA_BACKEND_H */
