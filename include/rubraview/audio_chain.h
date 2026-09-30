#ifndef RUBRAVIEW_AUDIO_CHAIN_H
#define RUBRAVIEW_AUDIO_CHAIN_H

#include "rubraview/core.h"
#include "rubraview/audio_dsp.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What happens to the sound between the decoder and the device (owner,
 * 2026-09-30: the music features, all of them): the ten-band equaliser on
 * each channel, a gain (ReplayGain's), night mode's compressor riding a
 * smoothed level so it lowers loud passages without distorting them, and a
 * tap that keeps the last samples, mixed to one channel, for the analyser
 * to draw. Runs on the output's own thread; set up from the viewer's.
 */

#define RUBRAVIEW_CHAIN_MAX_CHANNELS 8
#define RUBRAVIEW_TAP_SAMPLES 2048

typedef struct rubraview_audio_chain_config {
    rubraview_eq_preset_t eq_preset;   /* FLAT: the equaliser does nothing */
    /* The equaliser window's own ten gains (owner, 2026-10-01): used in
       place of the preset when `eq_custom` is set; all zero does nothing. */
    bool   eq_custom;
    float  eq_gains_db[RUBRAVIEW_EQ_BANDS];
    bool   night_mode;
    double gain;                       /* linear; 1.0 leaves the level alone */
} rubraview_audio_chain_config_t;

typedef struct rubraview_audio_chain {
    rubraview_audio_chain_config_t config;
    double   sample_rate;
    uint32_t channels;
    rubraview_equalizer_t eq[RUBRAVIEW_CHAIN_MAX_CHANNELS];
    double   envelope;                 /* night mode's smoothed level */
} rubraview_audio_chain_t;

void rubraview_audio_chain_init(rubraview_audio_chain_t *chain, double sample_rate, uint32_t channels);

/** A new set-up: the equaliser is rebuilt only when its preset or its gains changed. */
void rubraview_audio_chain_configure(rubraview_audio_chain_t *chain, const rubraview_audio_chain_config_t *config);

/** A seek: the filters' memory and the level are of the sound that is gone. */
void rubraview_audio_chain_reset(rubraview_audio_chain_t *chain);

/** `frames` interleaved frames, in place. */
void rubraview_audio_chain_process(rubraview_audio_chain_t *chain, float *interleaved, size_t frames);

/* The analyser's tap: the last RUBRAVIEW_TAP_SAMPLES samples, one channel. */
typedef struct rubraview_audio_tap {
    float  samples[RUBRAVIEW_TAP_SAMPLES];
    size_t write;       /* where the next sample goes */
    uint64_t total;     /* samples ever written */
} rubraview_audio_tap_t;

void rubraview_audio_tap_write(rubraview_audio_tap_t *tap, const float *interleaved, size_t frames, uint32_t channels);

/** The newest `count` samples, oldest first (fewer when fewer were written); returns how many. */
size_t rubraview_audio_tap_latest(const rubraview_audio_tap_t *tap, float *out, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_AUDIO_CHAIN_H */
