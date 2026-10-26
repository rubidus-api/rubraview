#ifndef RUBRAVIEW_PAL_MEDIA_HOST_H
#define RUBRAVIEW_PAL_MEDIA_HOST_H

#include "rubraview/media_host.h"
#include "rubraview/pal/pal_media_backend.h"
#include "rubraview/pal/pal_ffilter.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-176 (D-208): FFmpeg in a process of its own. `video.ffmpeg_isolated` (on as it comes) makes the viewer hand
 * every use of FFmpeg to a child, `rubraview.exe --media-host`: films, songs and stations to one child each,
 * the one-shot uses (a JPEG XL read, an AVIF or JPEG XL written, a filter, a list fetched, what FFmpeg can do) to
 * one long-lived service child. A film for an HDR screen in HDR output stays in the viewer for now.
 */

/** Isolation on or off (the setting); off loads FFmpeg into the viewer as before. */
void rubraview_pal_ffmpeg_set_isolated(bool on);
/** True in the viewer while isolation is on: FFmpeg's functions go to a child. False in the child itself. */
bool rubraview_pal_ffmpeg_remote(void);

/** The child's whole life: `--media-host <mapping> <request event> <reply event>`. Its exit code. */
int rubraview_pal_media_host_main(const wchar_t *mapping, const wchar_t *request_event, const wchar_t *reply_event);

/** Why the last film handed to a child was not opened there, for --diag; empty when it was. */
const char *rubraview_pal_media_host_why(void);

/** The backend that plays through a child. */
const rubraview_media_backend_api_t *rubraview_media_backend_remote(void);

/* Test builds: the next playback child crashes (1) or stops answering (2) after `after` pictures. */
void rubraview_pal_media_host_fault(int32_t kind, int32_t after);

/* The viewer's sound settings, told to every film's child too (pal_audio's setters call these); the child's latest
   samples for the analyser, when a film's child plays (true, and how many). */
void rubraview_mhost_note_speed(double speed);
void rubraview_mhost_note_volume(double volume, bool muted);
void rubraview_mhost_note_latency(uint32_t milliseconds);
void rubraview_mhost_note_chain(const rubraview_audio_chain_config_t *config);
bool rubraview_mhost_remote_tap(float *out, size_t count, size_t *out_count);

/* ---- inside the FFmpeg module, for the child ---- */

typedef struct rubraview_ffmpeg_hosting {
    rubraview_spsc_t *ring;                  /* in the block: the viewer reads it */
    rubraview_mhost_slot_t *slots;           /* each picture's word, in the block */
    uint8_t *(*pixels)(void *context, size_t bytes_each);   /* the four pictures' memory, shared */
    void *context;
} rubraview_ffmpeg_hosting_t;

void *rubraview_ffmpeg_host_open(proven_u8str_view_t path, const rubraview_media_gpu_t *gpu,
                                 const rubraview_ffmpeg_hosting_t *hosting, rubraview_media_failure_t *out_failure,
                                 rubraview_media_info_t *out_info);
/** What the viewer wants, applied (pause, seek, gain, a track). `applied` is the child's own memory of it. */
typedef struct rubraview_ffmpeg_applied {
    int32_t  paused;
    uint64_t seek_generation;
    int32_t  gain_millionths;
    uint32_t audio_serial;
} rubraview_ffmpeg_applied_t;
void rubraview_ffmpeg_host_apply(void *handle, rubraview_mhost_wanted_t *wanted, rubraview_ffmpeg_applied_t *applied);
void rubraview_ffmpeg_host_observe(void *handle, rubraview_mhost_seen_t *out);
/** Pictures stored so far (the test builds' fault switch counts them). */
uint64_t rubraview_ffmpeg_host_pictures(void *handle);

/* The local bodies of the public functions, for the child (and for the viewer with isolation off). */
bool     rubraview_ffmpeg_local_available(void);
int      rubraview_ffmpeg_local_place(void);
rubraview_ffmpeg_refusal_t rubraview_ffmpeg_local_refused(void);
int      rubraview_ffmpeg_local_jxl_state(void);
uint32_t rubraview_ffmpeg_local_image_encoders(char *names, size_t size);
rubraview_ffilter_state_t rubraview_ffilter_local_state(void);
bool     rubraview_ffilter_local_list(proven_arena_t *arena, rubraview_ffilter_catalog_t *out);
bool     rubraview_ffilter_local_run(const char *graph, const uint8_t *bgra, int32_t width, int32_t height,
                                     uint8_t **out, int32_t *out_width, int32_t *out_height, char *said, size_t said_size);

/* The viewer's side of the one-shot uses (pal_media_host.c). */
bool     rubraview_mhost_remote_available(void);
int      rubraview_mhost_remote_place(void);
rubraview_ffmpeg_refusal_t rubraview_mhost_remote_refused(void);
int      rubraview_mhost_remote_jxl_state(void);
uint32_t rubraview_mhost_remote_image_encoders(char *names, size_t size);
void     rubraview_mhost_remote_folders(proven_u8str_view_t imported, proven_u8str_view_t chosen);
int64_t  rubraview_mhost_remote_fetch(proven_u8str_view_t address, uint8_t *out, size_t cap);
bool     rubraview_mhost_remote_jxl_decode(const uint8_t *data, size_t size, int32_t max_side,
                                           uint8_t *(*room)(void *context, int32_t width, int32_t height),
                                           void *context, int32_t *out_width, int32_t *out_height);
bool     rubraview_mhost_remote_jxl_frames(const uint8_t *data, size_t size, int32_t max_side,
                                           uint8_t *(*room)(void *context, int32_t width, int32_t height, double seconds),
                                           void *context);
uint8_t *rubraview_mhost_remote_pictures_encode(int32_t width, int32_t height, size_t count,
                                                bool (*picture)(void *context, size_t index, uint8_t *bgra, double *seconds),
                                                void *context, const rubraview_ffmpeg_still_t *how,
                                                size_t *out_size, const char **why);
uint8_t *rubraview_mhost_remote_image_encode(const rubraview_pixbuf_t *pixels, const rubraview_ffmpeg_still_t *how,
                                             size_t *out_size, const char **why);
rubraview_ffilter_state_t rubraview_mhost_remote_filter_state(void);
bool     rubraview_mhost_remote_filter_list(proven_arena_t *arena, rubraview_ffilter_catalog_t *out);
bool     rubraview_mhost_remote_filter_run(const char *graph, const uint8_t *bgra, int32_t width, int32_t height,
                                           uint8_t **out, int32_t *out_width, int32_t *out_height, char *said, size_t said_size);

#ifdef __cplusplus
}
#endif

#endif
