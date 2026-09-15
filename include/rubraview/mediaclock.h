#ifndef RUBRAVIEW_MEDIACLOCK_H
#define RUBRAVIEW_MEDIACLOCK_H

#include <stdatomic.h>
#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * What a media player decides, apart from decoding (RFC-0001 §5.3, D-8,
 * D-9): which frame to show now, what time it is in the file, which
 * clock that time comes from, how frames cross from the decode thread to
 * the drawing thread, and which backend a file is handed to.
 *
 * None of it touches a decoder, a window or a thread of its own, so the
 * host suite covers it. The Windows backends only feed it numbers.
 */

/* ---- which frame to show ---- */

typedef enum rubraview_frame_decision {
    RUBRAVIEW_FRAME_WAIT = 0,   /* its time has not come: keep it, draw nothing new */
    RUBRAVIEW_FRAME_SHOW,       /* draw it now */
    RUBRAVIEW_FRAME_DROP,       /* its whole display interval has passed: skip it */
} rubraview_frame_decision_t;

/**
 * Decide what to do with the oldest decoded frame, given its presentation
 * time and how long it is meant to stay on screen.
 *
 * A frame whose interval is already over is dropped so the picture
 * catches up with the clock instead of running behind it for ever. The
 * caller still shows a dropped frame when it is the last one it has —
 * a late picture beats none — which is why this only advises.
 */
rubraview_frame_decision_t rubraview_media_schedule(double frame_pts, double frame_duration,
                                                     double clock_seconds);

/* ---- the playback clock ---- */

typedef enum rubraview_clock_master {
    RUBRAVIEW_CLOCK_WALL = 0,   /* the system clock: no audio, or no device to play it */
    RUBRAVIEW_CLOCK_AUDIO,      /* the audio device's own position (§5.3) */
} rubraview_clock_master_t;

/**
 * §5.3 makes audio the master. With nothing to hear — a silent file, or a
 * machine with no audio endpoint — the wall clock takes over, so video
 * still plays at its own speed.
 */
rubraview_clock_master_t rubraview_media_master_for(bool file_has_audio, bool audio_device_ready);

typedef struct rubraview_media_clock {
    rubraview_clock_master_t master;
    double anchor_media;   /* file time at the anchor */
    double anchor_wall;    /* wall time at the anchor */
    bool   paused;
    double rate;           /* D-15 playback speed: file seconds per wall second; 0 reads as 1 */
} rubraview_media_clock_t;

/** A clock that reads `media_seconds` at `wall_now` and runs from there. */
rubraview_media_clock_t rubraview_media_clock_create(rubraview_clock_master_t master,
                                                     double media_seconds, double wall_now);

/** File time now. A wall clock that steps backwards counts as no time passing. */
double rubraview_media_clock_now(const rubraview_media_clock_t *clock, double wall_now);

void rubraview_media_clock_pause(rubraview_media_clock_t *clock, double wall_now);
void rubraview_media_clock_resume(rubraview_media_clock_t *clock, double wall_now);
void rubraview_media_clock_seek(rubraview_media_clock_t *clock, double media_seconds, double wall_now);

/** D-15: run at `rate` from now on (0.25–4.0 in the viewer); the time read now does not jump. */
void rubraview_media_clock_set_rate(rubraview_media_clock_t *clock, double rate, double wall_now);

/**
 * With audio as master, the device's position is the truth: re-anchor on
 * it. A wall-clock master ignores the call.
 */
void rubraview_media_clock_sync_audio(rubraview_media_clock_t *clock, double audio_seconds,
                                      double wall_now);

/* ---- frames crossing threads ---- */

#define RUBRAVIEW_SPSC_MAX_SLOTS 16

/**
 * A single-producer, single-consumer ring of slot numbers. The decode
 * thread fills a slot and commits it; the drawing thread reads it and
 * releases it. The ring only hands out numbers — the frames themselves
 * live in an array the caller owns, so nothing is copied twice and
 * nothing is allocated while playing.
 *
 * Exactly one thread may call the write side and one the read side.
 * `reset` is for when both sides are stopped (after a seek has flushed
 * the decoder), never while either runs.
 */
typedef struct rubraview_spsc {
    _Atomic size_t head;   /* next slot the producer fills; written by the producer */
    _Atomic size_t tail;   /* next slot the consumer reads; written by the consumer */
    size_t capacity;
} rubraview_spsc_t;

bool   rubraview_spsc_init(rubraview_spsc_t *ring, size_t capacity);
void   rubraview_spsc_reset(rubraview_spsc_t *ring);
size_t rubraview_spsc_count(rubraview_spsc_t *ring);

/** Producer: the slot to fill, or false when every slot is waiting to be read. */
bool rubraview_spsc_acquire_write(rubraview_spsc_t *ring, size_t *out_slot);
/** Producer: publish the slot just filled. */
void rubraview_spsc_commit_write(rubraview_spsc_t *ring);

/** Consumer: the oldest filled slot, or false when there is none. */
bool rubraview_spsc_peek_read(rubraview_spsc_t *ring, size_t *out_slot);
/** Consumer: give the slot back to the producer. */
void rubraview_spsc_release_read(rubraview_spsc_t *ring);

/* ---- audio samples crossing threads (M5 slice 2) ---- */

/**
 * A single-producer, single-consumer ring of float samples: the decode
 * thread writes decoded audio, the audio device thread reads it. The
 * storage belongs to the caller; the ring only counts. Writes and reads
 * move as many samples as fit and say how many that was, so neither side
 * ever blocks the other.
 *
 * `discard` is the consumer throwing away everything written so far — a
 * seek, done while the producer is known to be waiting (see pal_audio).
 */
typedef struct rubraview_pcm_ring {
    float *samples;
    size_t capacity;          /* in samples, not frames */
    _Atomic size_t written;   /* total ever written; producer only */
    _Atomic size_t read;      /* total ever read; consumer only */
} rubraview_pcm_ring_t;

bool   rubraview_pcm_ring_init(rubraview_pcm_ring_t *ring, float *storage, size_t capacity);
size_t rubraview_pcm_ring_count(rubraview_pcm_ring_t *ring);   /* samples waiting */
size_t rubraview_pcm_ring_space(rubraview_pcm_ring_t *ring);   /* samples that fit */
size_t rubraview_pcm_ring_write(rubraview_pcm_ring_t *ring, const float *src, size_t count);
size_t rubraview_pcm_ring_read(rubraview_pcm_ring_t *ring, float *dst, size_t count);
void   rubraview_pcm_ring_discard(rubraview_pcm_ring_t *ring);

/**
 * Where the listener is, in file time: the seek base plus the frames
 * handed to the device minus the frames still waiting in its buffer.
 * Never before the base, whatever the numbers say.
 */
double rubraview_audio_heard_seconds(double base_seconds, uint64_t frames_submitted,
                                     uint64_t frames_pending, uint32_t sample_rate);

/**
 * The audio thread records (position, wall time) when it can; the
 * drawing thread asks where playback is *now*. Between two records the
 * position runs on with the wall clock — but not while paused, and not
 * by more than `max_extrapolation`, so a stalled device freezes the
 * picture instead of racing ahead of the sound.
 */
double rubraview_audio_position_now(double recorded_position, double recorded_wall,
                                    double wall_now, bool playing, double max_extrapolation);

/** The same, for playback at `rate`: between records the file runs `rate` seconds per second. */
double rubraview_audio_position_now_at_rate(double recorded_position, double recorded_wall,
                                            double wall_now, bool playing, double max_extrapolation,
                                            double rate);

/**
 * D-15: where in the file the listener is when playback runs at `speed`:
 * `source_frames` of the file have gone to the device, and `pending_frames`
 * of *device* frames are still queued — each of those stands for `speed`
 * file frames.
 */
double rubraview_audio_heard_media_seconds(double base_seconds, double source_frames,
                                           uint64_t pending_frames, double speed, uint32_t sample_rate);

/* ---- which backend opens a file (D-8, D-9) ---- */

typedef enum rubraview_media_backend {
    RUBRAVIEW_BACKEND_MEDIA_FOUNDATION = 0,
    RUBRAVIEW_BACKEND_FFMPEG,
} rubraview_media_backend_t;

/**
 * D-9: the preferred backend first, then the other one — so a file the
 * preferred backend cannot open is retried automatically. FFmpeg is
 * skipped when its DLLs are not there. Returns how many were written.
 */
size_t rubraview_media_backend_order(rubraview_media_backend_t preferred, bool ffmpeg_available,
                                     rubraview_media_backend_t out_order[2]);

typedef enum rubraview_media_failure {
    RUBRAVIEW_MEDIA_OPENED = 0,
    RUBRAVIEW_MEDIA_FAIL_FILE,        /* could not be read at all */
    RUBRAVIEW_MEDIA_FAIL_CONTAINER,   /* no backend knows the container */
    RUBRAVIEW_MEDIA_FAIL_CODEC,       /* the container opened, a stream has no decoder */
    RUBRAVIEW_MEDIA_FAIL_HEVC,        /* ...and that stream is HEVC (D-9: name both remedies) */
} rubraview_media_failure_t;

/** Four-character codes as backends report them, first byte in the low bits. */
#define RUBRAVIEW_FOURCC(a, b, c, d) \
    ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) | \
     ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24))

/** Whether a video codec code names HEVC (`HEVC`, `hvc1`, `hev1`, `H265`). */
bool rubraview_media_is_hevc(uint32_t video_fourcc);

/**
 * Why the last backend tried gave up. `readable` is false when the file
 * itself could not be read; `container_opened` is false when no backend
 * recognised it; `video_decodable` is false when the video stream has no
 * decoder, and `video_fourcc` says which codec that was.
 */
rubraview_media_failure_t rubraview_media_classify(bool readable, bool container_opened,
                                                   bool video_decodable, uint32_t video_fourcc);

/**
 * The on-screen line for a failure (D-9: say so, then move to the next
 * file). HEVC names both remedies. Empty for RUBRAVIEW_MEDIA_OPENED.
 */
u8str_t rubraview_media_failure_text(rubraview_media_failure_t failure);

/**
 * When both backends fail, the more specific reason is the one worth
 * reporting: Media Foundation saying "HEVC without the extension" must
 * not be buried under FFmpeg saying it is not installed.
 * HEVC > codec > container > file.
 */
rubraview_media_failure_t rubraview_media_failure_pick(rubraview_media_failure_t a,
                                                       rubraview_media_failure_t b);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_MEDIACLOCK_H */
