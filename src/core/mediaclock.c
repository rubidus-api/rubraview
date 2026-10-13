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

/* How far the clock may be from the picture and the picture still be
   "where the film is": a late frame or two, not a stretch without any. */
#define POSITION_SLACK_SECONDS 0.5

double rubraview_media_position_running(double shown_pts, double shown_duration, double clock_seconds,
                                        double file_seconds) {
    if (!(shown_duration > 0.0)) shown_duration = FALLBACK_FRAME_SECONDS;
    if (clock_seconds >= shown_pts - POSITION_SLACK_SECONDS &&
        clock_seconds <= shown_pts + shown_duration + POSITION_SLACK_SECONDS) return shown_pts;
    if (file_seconds > 0.0 && clock_seconds > file_seconds) clock_seconds = file_seconds;
    return clock_seconds < 0.0 ? 0.0 : clock_seconds;
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
        .rate = 1.0,
    };
}

static double clock_rate(const rubraview_media_clock_t *clock) {
    return clock->rate > 0.0 ? clock->rate : 1.0;
}

void rubraview_media_clock_set_rate(rubraview_media_clock_t *clock, double rate, double wall_now) {
    if (!clock || !(rate > 0.0)) return;
    clock->anchor_media = rubraview_media_clock_now(clock, wall_now);
    clock->anchor_wall = wall_now;
    clock->rate = rate;
}

double rubraview_media_clock_now(const rubraview_media_clock_t *clock, double wall_now) {
    if (!clock) return 0.0;
    if (clock->paused) return clock->anchor_media;
    double elapsed = wall_now - clock->anchor_wall;
    if (elapsed < 0.0) elapsed = 0.0;
    return clock->anchor_media + elapsed * clock_rate(clock);
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

/* ---- PCM ring ----
 *
 * The same pairing as the frame ring: the producer's release store of
 * `written` publishes the samples it copied, the consumer's release store
 * of `read` hands the space back. Totals only grow; positions are totals
 * modulo capacity. */

bool rubraview_pcm_ring_init(rubraview_pcm_ring_t *ring, float *storage, size_t capacity) {
    if (!ring || !storage || capacity == 0) return false;
    ring->samples = storage;
    ring->capacity = capacity;
    atomic_init(&ring->written, 0);
    atomic_init(&ring->read, 0);
    return true;
}

size_t rubraview_pcm_ring_count(rubraview_pcm_ring_t *ring) {
    if (!ring) return 0;
    size_t w = atomic_load_explicit(&ring->written, memory_order_acquire);
    size_t r = atomic_load_explicit(&ring->read, memory_order_acquire);
    return w - r;
}

size_t rubraview_pcm_ring_space(rubraview_pcm_ring_t *ring) {
    if (!ring) return 0;
    return ring->capacity - rubraview_pcm_ring_count(ring);
}

size_t rubraview_pcm_ring_write(rubraview_pcm_ring_t *ring, const float *src, size_t count) {
    if (!ring || !src || ring->capacity == 0) return 0;
    size_t w = atomic_load_explicit(&ring->written, memory_order_relaxed);
    size_t r = atomic_load_explicit(&ring->read, memory_order_acquire);
    size_t space = ring->capacity - (w - r);
    if (count > space) count = space;
    for (size_t i = 0; i < count; ++i) ring->samples[(w + i) % ring->capacity] = src[i];
    atomic_store_explicit(&ring->written, w + count, memory_order_release);
    return count;
}

size_t rubraview_pcm_ring_read(rubraview_pcm_ring_t *ring, float *dst, size_t count) {
    if (!ring || !dst || ring->capacity == 0) return 0;
    size_t r = atomic_load_explicit(&ring->read, memory_order_relaxed);
    size_t w = atomic_load_explicit(&ring->written, memory_order_acquire);
    size_t have = w - r;
    if (count > have) count = have;
    for (size_t i = 0; i < count; ++i) dst[i] = ring->samples[(r + i) % ring->capacity];
    atomic_store_explicit(&ring->read, r + count, memory_order_release);
    return count;
}

void rubraview_pcm_ring_discard(rubraview_pcm_ring_t *ring) {
    if (!ring) return;
    size_t w = atomic_load_explicit(&ring->written, memory_order_acquire);
    atomic_store_explicit(&ring->read, w, memory_order_release);
}

double rubraview_audio_heard_seconds(double base_seconds, uint64_t frames_submitted,
                                     uint64_t frames_pending, uint32_t sample_rate) {
    if (sample_rate == 0 || frames_pending >= frames_submitted) return base_seconds;
    return base_seconds + (double)(frames_submitted - frames_pending) / (double)sample_rate;
}

double rubraview_audio_position_now(double recorded_position, double recorded_wall,
                                    double wall_now, bool playing, double max_extrapolation) {
    return rubraview_audio_position_now_at_rate(recorded_position, recorded_wall, wall_now, playing,
                                                max_extrapolation, 1.0);
}

double rubraview_audio_position_now_at_rate(double recorded_position, double recorded_wall,
                                            double wall_now, bool playing, double max_extrapolation,
                                            double rate) {
    if (!playing) return recorded_position;
    double elapsed = wall_now - recorded_wall;
    if (elapsed < 0.0) elapsed = 0.0;
    if (max_extrapolation >= 0.0 && elapsed > max_extrapolation) elapsed = max_extrapolation;
    return recorded_position + elapsed * (rate > 0.0 ? rate : 1.0);
}

double rubraview_audio_heard_media_seconds(double base_seconds, double source_frames,
                                           uint64_t pending_frames, double speed, uint32_t sample_rate) {
    if (sample_rate == 0) return base_seconds;
    if (!(speed > 0.0)) speed = 1.0;
    double heard = source_frames - (double)pending_frames * speed;
    return heard > 0.0 ? base_seconds + heard / (double)sample_rate : base_seconds;
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

u8str_t rubraview_media_failure_text_with(rubraview_media_failure_t failure, rubraview_ffmpeg_refusal_t refusal) {
    if (refusal == RUBRAVIEW_FFMPEG_NOT_REFUSED) return rubraview_media_failure_text(failure);
    if (refusal == RUBRAVIEW_FFMPEG_NOT_WHOLE) {
        switch (failure) {
            case RUBRAVIEW_MEDIA_FAIL_CONTAINER:
                return U8("cannot play this kind of file — the FFmpeg 9.0 DLLs beside rubraview could not be loaded (all five are needed)");
            case RUBRAVIEW_MEDIA_FAIL_CODEC:
                return U8("cannot decode this video — the FFmpeg 9.0 DLLs beside rubraview could not be loaded (all five are needed)");
            case RUBRAVIEW_MEDIA_FAIL_HEVC:
                return U8("HEVC video needs the \"HEVC Video Extensions\" ($0.99, Microsoft Store) "
                          "or FFmpeg 9.0 DLLs — the ones beside rubraview could not be loaded (all five are needed)");
            default:
                return rubraview_media_failure_text(failure);
        }
    }
    switch (failure) {
        case RUBRAVIEW_MEDIA_FAIL_CONTAINER:
            return U8("cannot play this kind of file — the FFmpeg DLLs beside rubraview are not 9.0 (avcodec-63) and were not loaded");
        case RUBRAVIEW_MEDIA_FAIL_CODEC:
            return U8("cannot decode this video — the FFmpeg DLLs beside rubraview are not 9.0 (avcodec-63) and were not loaded");
        case RUBRAVIEW_MEDIA_FAIL_HEVC:
            return U8("HEVC video needs the \"HEVC Video Extensions\" ($0.99, Microsoft Store) "
                      "or FFmpeg 9.0 DLLs — the ones beside rubraview are another version and were not loaded");
        default:
            return rubraview_media_failure_text(failure);
    }
}

u8str_t rubraview_jxl_failure_text(int jxl_state, rubraview_ffmpeg_refusal_t refusal) {
    if (jxl_state == 2) return U8("JPEG XL: this picture could not be read");
    if (jxl_state == 1) return U8("JPEG XL: the FFmpeg beside the program has no decoder for it (libjxl)");
    if (refusal == RUBRAVIEW_FFMPEG_NOT_WHOLE) return U8("JPEG XL: the FFmpeg 9.0 DLLs beside the program could not be loaded (all five are needed)");
    if (refusal == RUBRAVIEW_FFMPEG_OTHER_VERSION) return U8("JPEG XL: the FFmpeg DLLs beside the program are not 9.0 (avcodec-63) and were not loaded");
    return U8("JPEG XL: no decoder for it in this Windows, and no FFmpeg beside the program");
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

static int failure_rank(rubraview_media_failure_t f) {
    switch (f) {
        case RUBRAVIEW_MEDIA_FAIL_HEVC:      return 4;
        case RUBRAVIEW_MEDIA_FAIL_CODEC:     return 3;
        case RUBRAVIEW_MEDIA_FAIL_CONTAINER: return 2;
        case RUBRAVIEW_MEDIA_FAIL_FILE:      return 1;
        case RUBRAVIEW_MEDIA_OPENED:
        default:                             return 0;
    }
}

rubraview_media_failure_t rubraview_media_failure_pick(rubraview_media_failure_t a,
                                                       rubraview_media_failure_t b) {
    return failure_rank(b) > failure_rank(a) ? b : a;
}

/* ---- redraw pacing ---- */

bool rubraview_media_redraw_due(bool new_picture, double seconds_since_render) {
    return new_picture || rubraview_media_redraw_wait(seconds_since_render) <= 0.0;
}

double rubraview_media_redraw_wait(double seconds_since_render) {
    if (!(seconds_since_render >= 0.0)) return 0.0;   /* backwards or NaN */
    double left = RUBRAVIEW_MEDIA_HUD_REDRAW_SECONDS - seconds_since_render;
    return left > 0.0 ? left : 0.0;
}

/* ---- RV-062: hardware decode ---- */

bool rubraview_media_gpu_attach(int32_t mode, bool has_device, uint32_t decoder_profiles) {
    if (!has_device) return false;
    return mode == 2 || (mode == 1 && decoder_profiles > 0);
}

bool rubraview_rate_meter_add(rubraview_rate_meter_t *meter, double pts, double *out_rate) {
    if (!meter) return false;
    if (!meter->primed) {
        *meter = (rubraview_rate_meter_t){ .last = pts, .primed = true };
        return false;
    }
    double step = pts - meter->last;
    meter->last = pts;
    if (step <= 0.0 || step >= 0.5) {
        meter->sum = 0.0;
        meter->steps = 0;
        return false;
    }
    meter->sum += step;
    if (++meter->steps < 16) return false;
    double rate = (double)meter->steps / meter->sum;
    meter->sum = 0.0;
    meter->steps = 0;
    if (out_rate) *out_rate = rate;
    return true;
}

bool rubraview_media_rate_differs(double named, double measured) {
    if (measured <= 0.0) return false;
    if (named <= 0.0) return true;
    double ratio = measured / named;
    return ratio < 0.95 || ratio > 1.05;
}
