#ifdef _WIN32
#include <stdlib.h>
#include "rubraview/pal/pal_media_backend.h"

/*
 * The public media PAL: which backend opened a file, and forwarding to it
 * (D-8, D-9). Media Foundation is one backend, FFmpeg the other; neither
 * knows about the other, and this file is the only place that knows both.
 */

struct rubraview_media {
    const rubraview_media_backend_api_t *api;
    void *impl;
};

static const rubraview_media_backend_api_t *backend_api(rubraview_media_backend_t backend) {
    return backend == RUBRAVIEW_BACKEND_FFMPEG ? rubraview_media_backend_ffmpeg()
                                               : rubraview_media_backend_mf();
}

bool rubraview_pal_media_backend_available(rubraview_media_backend_t backend) {
    const rubraview_media_backend_api_t *api = backend_api(backend);
    return api && api->available && api->available();
}

rubraview_media_open_result_t rubraview_pal_media_open(u8str_t path, rubraview_media_backend_t backend,
                                                       const rubraview_media_gpu_t *gpu) {
    rubraview_media_open_result_t result = { .media = NULL, .failure = RUBRAVIEW_MEDIA_FAIL_FILE };
    const rubraview_media_backend_api_t *api = backend_api(backend);
    if (!api || !api->open) return result;

    rubraview_media_info_t info = {0};
    void *impl = api->open(path, &result.failure, &info, gpu);
    if (!impl) return result;

    rubraview_media_t *media = (rubraview_media_t*)calloc(1, sizeof(*media));
    if (!media) {
        api->close(impl);
        result.failure = RUBRAVIEW_MEDIA_FAIL_FILE;
        return result;
    }
    media->api = api;
    media->impl = impl;
    info.backend = backend;
    result.media = media;
    result.info = info;
    result.failure = RUBRAVIEW_MEDIA_OPENED;
    return result;
}

void rubraview_pal_media_close(rubraview_media_t *media) {
    if (!media) return;
    media->api->close(media->impl);
    free(media);
}

bool rubraview_pal_media_peek_frame(rubraview_media_t *media, rubraview_video_frame_t *out_frame) {
    return media && media->api->peek_frame(media->impl, out_frame);
}

void rubraview_pal_media_pop_frame(rubraview_media_t *media) {
    if (media) media->api->pop_frame(media->impl);
}

size_t rubraview_pal_media_frames_ready(rubraview_media_t *media) {
    return media ? media->api->frames_ready(media->impl) : 0;
}

void rubraview_pal_media_seek(rubraview_media_t *media, double seconds) {
    if (media) media->api->seek(media->impl, seconds);
}

bool rubraview_pal_media_finished(rubraview_media_t *media) {
    return !media || media->api->finished(media->impl);
}

void rubraview_pal_media_set_paused(rubraview_media_t *media, bool paused) {
    if (media) media->api->set_paused(media->impl, paused);
}

bool rubraview_pal_media_audio_position(rubraview_media_t *media, double *out_position, double *out_wall) {
    return media && media->api->audio_position(media->impl, out_position, out_wall);
}

bool rubraview_pal_media_select_audio_track(rubraview_media_t *media, int32_t stream_index) {
    if (!media || !media->api->select_audio_track) return false;
    return media->api->select_audio_track(media->impl, stream_index);
}

u8str_t rubraview_pal_media_read_subtitle_stream(rubraview_media_t *media, proven_arena_t *arena,
                                                 int32_t stream_index) {
    u8str_t empty = { .ptr = "", .len = 0 };
    if (!media || !arena || !media->api->read_subtitle_stream) return empty;
    return media->api->read_subtitle_stream(media->impl, arena, stream_index);
}

bool rubraview_pal_media_tracks(rubraview_media_t *media, rubraview_track_set_t *out_set) {
    /* A backend that does not fill this member leaves it NULL — the
       table is written with designated initialisers, so the compiler
       will not say a word about the gap. */
    if (!media || !out_set || !media->api->tracks) return false;
    return media->api->tracks(media->impl, out_set);
}

#endif /* _WIN32 */
