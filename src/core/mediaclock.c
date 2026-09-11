#include "rubraview/mediaclock.h"

/* A frame with no stated duration is treated as one frame of 30 fps
   video: long enough that a stray zero cannot make every frame "late". */
#define FALLBACK_FRAME_SECONDS (1.0 / 30.0)

rubraview_frame_decision_t rubraview_media_schedule(double frame_pts, double frame_duration,
                                                     double clock_seconds) {
    if (!(frame_duration > 0.0)) frame_duration = FALLBACK_FRAME_SECONDS;

    /* Its interval [pts, pts + duration) is already over. */
    if (frame_pts + frame_duration <= clock_seconds) return RUBRAVIEW_FRAME_DROP;

    /* Due now, or within half a frame: showing it a little early is
       invisible, while waiting a whole extra pass can be seen. */
    if (frame_pts <= clock_seconds + frame_duration * 0.5) return RUBRAVIEW_FRAME_SHOW;

    return RUBRAVIEW_FRAME_WAIT;
}

rubraview_clock_master_t rubraview_media_master_for(bool file_has_audio, bool audio_device_ready) {
    return (file_has_audio && audio_device_ready) ? RUBRAVIEW_CLOCK_AUDIO : RUBRAVIEW_CLOCK_WALL;
}

rubraview_media_clock_t rubraview_media_clock_create(rubraview_clock_master_t master,
                                                     double media_seconds, double wall_now) {
    return (rubraview_media_clock_t){
        .master = master,
        .anchor_media = media_seconds,
        .anchor_wall = wall_now,
        .paused = false,
    };
}

double rubraview_media_clock_now(const rubraview_media_clock_t *clock, double wall_now) {
    if (!clock) return 0.0;
    if (clock->paused) return clock->anchor_media;
    double elapsed = wall_now - clock->anchor_wall;
    if (elapsed < 0.0) elapsed = 0.0;
    return clock->anchor_media + elapsed;
}

void rubraview_media_clock_pause(rubraview_media_clock_t *clock, double wall_now) {
    if (!clock || clock->paused) return;
    clock->anchor_media = rubraview_media_clock_now(clock, wall_now);
    clock->anchor_wall = wall_now;
    clock->paused = true;
}

void rubraview_media_clock_resume(rubraview_media_clock_t *clock, double wall_now) {
    if (!clock || !clock->paused) return;
    clock->anchor_wall = wall_now;
    clock->paused = false;
}

void rubraview_media_clock_seek(rubraview_media_clock_t *clock, double media_seconds, double wall_now) {
    if (!clock) return;
    clock->anchor_media = media_seconds < 0.0 ? 0.0 : media_seconds;
    clock->anchor_wall = wall_now;
}

void rubraview_media_clock_sync_audio(rubraview_media_clock_t *clock, double audio_seconds,
                                      double wall_now) {
    if (!clock || clock->master != RUBRAVIEW_CLOCK_AUDIO || clock->paused) return;
    clock->anchor_media = audio_seconds;
    clock->anchor_wall = wall_now;
}

/* ---- SPSC ring ----
 *
 * head and tail only ever grow; a slot is `index % capacity`, and the
 * number of filled slots is `head - tail`. The producer publishes a slot
 * with a release store of head after writing the frame, and the consumer
 * reads head with an acquire load before touching it — that pair is what
 * makes the frame's bytes visible on the other thread. The same pairing
 * runs the other way for tail, so the producer never refills a slot the
 * consumer is still reading. */

bool rubraview_spsc_init(rubraview_spsc_t *ring, size_t capacity) {
    if (!ring || capacity == 0 || capacity > RUBRAVIEW_SPSC_MAX_SLOTS) return false;
    atomic_init(&ring->head, 0);
    atomic_init(&ring->tail, 0);
    ring->capacity = capacity;
    return true;
}

void rubraview_spsc_reset(rubraview_spsc_t *ring) {
    if (!ring) return;
    atomic_store(&ring->head, 0);
    atomic_store(&ring->tail, 0);
}

size_t rubraview_spsc_count(rubraview_spsc_t *ring) {
    if (!ring) return 0;
    size_t head = atomic_load_explicit(&ring->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&ring->tail, memory_order_acquire);
    return head - tail;
}

bool rubraview_spsc_acquire_write(rubraview_spsc_t *ring, size_t *out_slot) {
    if (!ring || !out_slot || ring->capacity == 0) return false;
    size_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&ring->tail, memory_order_acquire);
    if (head - tail >= ring->capacity) return false;
    *out_slot = head % ring->capacity;
    return true;
}

void rubraview_spsc_commit_write(rubraview_spsc_t *ring) {
    if (!ring) return;
    size_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);
    atomic_store_explicit(&ring->head, head + 1, memory_order_release);
}

bool rubraview_spsc_peek_read(rubraview_spsc_t *ring, size_t *out_slot) {
    if (!ring || !out_slot || ring->capacity == 0) return false;
    size_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    size_t head = atomic_load_explicit(&ring->head, memory_order_acquire);
    if (head == tail) return false;
    *out_slot = tail % ring->capacity;
    return true;
}

void rubraview_spsc_release_read(rubraview_spsc_t *ring) {
    if (!ring) return;
    size_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    atomic_store_explicit(&ring->tail, tail + 1, memory_order_release);
}

/* ---- backends ---- */

size_t rubraview_media_backend_order(rubraview_media_backend_t preferred, bool ffmpeg_available,
                                     rubraview_media_backend_t out_order[2]) {
    if (!out_order) return 0;
    if (!ffmpeg_available) {
        /* Media Foundation is part of Windows; it is always there to try. */
        out_order[0] = RUBRAVIEW_BACKEND_MEDIA_FOUNDATION;
        return 1;
    }
    if (preferred == RUBRAVIEW_BACKEND_FFMPEG) {
        out_order[0] = RUBRAVIEW_BACKEND_FFMPEG;
        out_order[1] = RUBRAVIEW_BACKEND_MEDIA_FOUNDATION;
    } else {
        out_order[0] = RUBRAVIEW_BACKEND_MEDIA_FOUNDATION;
        out_order[1] = RUBRAVIEW_BACKEND_FFMPEG;
    }
    return 2;
}

bool rubraview_media_is_hevc(uint32_t fourcc) {
    return fourcc == RUBRAVIEW_FOURCC('H', 'E', 'V', 'C') ||
           fourcc == RUBRAVIEW_FOURCC('h', 'v', 'c', '1') ||
           fourcc == RUBRAVIEW_FOURCC('h', 'e', 'v', '1') ||
           fourcc == RUBRAVIEW_FOURCC('H', '2', '6', '5');
}

rubraview_media_failure_t rubraview_media_classify(bool readable, bool container_opened,
                                                   bool video_decodable, uint32_t video_fourcc) {
    if (!readable) return RUBRAVIEW_MEDIA_FAIL_FILE;
    if (!container_opened) return RUBRAVIEW_MEDIA_FAIL_CONTAINER;
    if (!video_decodable) {
        return rubraview_media_is_hevc(video_fourcc) ? RUBRAVIEW_MEDIA_FAIL_HEVC
                                                     : RUBRAVIEW_MEDIA_FAIL_CODEC;
    }
    return RUBRAVIEW_MEDIA_OPENED;
}

u8str_t rubraview_media_failure_text(rubraview_media_failure_t failure) {
    switch (failure) {
        case RUBRAVIEW_MEDIA_FAIL_FILE:
            return U8("could not read that file");
        case RUBRAVIEW_MEDIA_FAIL_CONTAINER:
            return U8("cannot play this kind of file — FFmpeg DLLs beside rubraview would open it");
        case RUBRAVIEW_MEDIA_FAIL_CODEC:
            return U8("cannot decode this video — FFmpeg DLLs beside rubraview would play it");
        case RUBRAVIEW_MEDIA_FAIL_HEVC:
            return U8("HEVC video needs the \"HEVC Video Extensions\" ($0.99, Microsoft Store) "
                      "or FFmpeg DLLs beside rubraview");
        case RUBRAVIEW_MEDIA_OPENED:
        default:
            return U8("");
    }
}
