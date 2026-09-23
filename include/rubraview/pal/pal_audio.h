#ifndef RUBRAVIEW_PAL_AUDIO_H
#define RUBRAVIEW_PAL_AUDIO_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "rubraview/core.h"
#include "rubraview/mediaclock.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Audio output PAL (RFC-0001 §5.4, M5 slice 2). The Windows backend is
 * WASAPI in shared mode; any media backend (Media Foundation now, FFmpeg
 * later) feeds it the same way — interleaved 32-bit float samples through
 * a rubraview_pcm_ring_t. No Win32 type appears here (D-1).
 *
 * The output runs its own thread, which owns COM and the device. The
 * other threads talk to it only through the ring and the calls below,
 * all of which are safe from any thread.
 */

typedef struct rubraview_audio_out rubraview_audio_out_t;

/**
 * Open the default output device for `channels` channels at `sample_rate`
 * (Windows converts to the device's own format). Returns NULL when there
 * is no device to play on — a machine with no audio endpoint, like the
 * test VM — and the caller then plays without sound on the wall clock.
 * The output starts paused.
 */
rubraview_audio_out_t *rubraview_pal_audio_open(uint32_t sample_rate, uint32_t channels,
                                                rubraview_pcm_ring_t *ring);

void rubraview_pal_audio_close(rubraview_audio_out_t *out);

void rubraview_pal_audio_set_playing(rubraview_audio_out_t *out, bool playing);

/**
 * RV-075: how loud this one output is, 0.0 to 1.0, on top of the
 * program's own volume. It exists so two tracks can be heard at once
 * while one gives way to the other — the process-wide volume of D-15
 * cannot do that, being one for the whole program. Safe from any thread;
 * it takes effect on the next buffer the device asks for.
 */
void rubraview_pal_audio_set_gain(rubraview_audio_out_t *out, double gain);

/**
 * A seek: the device buffer and the ring are emptied, and the heard
 * position restarts at `base_seconds`. Blocks until the output thread has
 * done it. Call it from the producer while it is not writing — that is
 * what makes emptying the ring safe.
 */
void rubraview_pal_audio_flush(rubraview_audio_out_t *out, double base_seconds);

/** Where the listener is, and the wall time (pal_time seconds) that was measured at. */
bool rubraview_pal_audio_position(rubraview_audio_out_t *out, double *out_position, double *out_wall);

/** Everything written has been played: the ring is empty and so is the device buffer. */
bool rubraview_pal_audio_drained(rubraview_audio_out_t *out);

/**
 * D-15: the program's volume, 0.0–1.0, and whether it is muted. It is the
 * Windows audio session's (`ISimpleAudioVolume`) — one for the whole
 * process, so it holds across files and outputs — and every open output,
 * and every one opened later, applies it. Safe from any thread.
 */
void rubraview_pal_audio_set_volume(double volume, bool muted);

/**
 * D-15: playback speed, 0.25–4.0, for every output. The samples are read
 * that much faster or slower (so the pitch follows), and the heard
 * position is in file time, which is what the picture's clock follows.
 */
void rubraview_pal_audio_set_speed(double speed);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_AUDIO_H */
