#include "rubraview/audio_chain.h"
#include <math.h>
#include <string.h>

void rubraview_audio_chain_init(rubraview_audio_chain_t *chain, double sample_rate, uint32_t channels) {
    if (!chain) return;
    memset(chain, 0, sizeof(*chain));
    chain->sample_rate = sample_rate > 0.0 ? sample_rate : 48000.0;
    chain->channels = channels == 0 ? 1 : channels > RUBRAVIEW_CHAIN_MAX_CHANNELS ? RUBRAVIEW_CHAIN_MAX_CHANNELS : channels;
    chain->config = (rubraview_audio_chain_config_t){ .eq_preset = RUBRAVIEW_EQ_FLAT, .night_mode = false, .gain = 1.0 };
    for (uint32_t c = 0; c < RUBRAVIEW_CHAIN_MAX_CHANNELS; ++c) chain->eq[c] = rubraview_eq_create(chain->sample_rate);
}

void rubraview_audio_chain_configure(rubraview_audio_chain_t *chain, const rubraview_audio_chain_config_t *config) {
    if (!chain || !config) return;
    if (config->eq_preset != chain->config.eq_preset) {
        for (uint32_t c = 0; c < chain->channels; ++c) {
            rubraview_eq_set_preset(&chain->eq[c], chain->sample_rate, config->eq_preset);
            chain->eq[c].enabled = config->eq_preset != RUBRAVIEW_EQ_FLAT;
        }
    }
    chain->config = *config;
    if (!(chain->config.gain > 0.0)) chain->config.gain = 1.0;
}

void rubraview_audio_chain_reset(rubraview_audio_chain_t *chain) {
    if (!chain) return;
    for (uint32_t c = 0; c < chain->channels; ++c) {
        for (size_t b = 0; b < RUBRAVIEW_EQ_BANDS; ++b) rubraview_biquad_reset(&chain->eq[c].bands[b]);
    }
    chain->envelope = 0.0;
}

void rubraview_audio_chain_process(rubraview_audio_chain_t *chain, float *interleaved, size_t frames) {
    if (!chain || !interleaved || frames == 0) return;
    uint32_t ch = chain->channels;
    bool eq = chain->config.eq_preset != RUBRAVIEW_EQ_FLAT;
    double gain = chain->config.gain;
    /* Night mode: -24 dB threshold, 4:1, with a level that rises in about
       5 ms and falls in about 200 ms — a compressor, not a clipper. */
    double attack = 1.0 - exp(-1.0 / (0.005 * chain->sample_rate));
    double release = 1.0 - exp(-1.0 / (0.200 * chain->sample_rate));
    for (size_t f = 0; f < frames; ++f) {
        float *frame = interleaved + f * ch;
        if (eq) {
            for (uint32_t c = 0; c < ch; ++c) rubraview_eq_process(&chain->eq[c], &frame[c], 1);
        }
        double g = gain;
        if (chain->config.night_mode) {
            double peak = 0.0;
            for (uint32_t c = 0; c < ch; ++c) {
                double a = fabs((double)frame[c] * gain);
                if (a > peak) peak = a;
            }
            double k = peak > chain->envelope ? attack : release;
            chain->envelope += (peak - chain->envelope) * k;
            g *= rubraview_night_mode_gain(chain->envelope, -24.0, 4.0);
        }
        if (g != 1.0) {
            for (uint32_t c = 0; c < ch; ++c) {
                double v = (double)frame[c] * g;
                frame[c] = (float)(v > 1.0 ? 1.0 : v < -1.0 ? -1.0 : v);
            }
        }
    }
}

void rubraview_audio_tap_write(rubraview_audio_tap_t *tap, const float *interleaved, size_t frames, uint32_t channels) {
    if (!tap || !interleaved || channels == 0) return;
    for (size_t f = 0; f < frames; ++f) {
        double sum = 0.0;
        for (uint32_t c = 0; c < channels; ++c) sum += interleaved[f * channels + c];
        tap->samples[tap->write] = (float)(sum / (double)channels);
        tap->write = (tap->write + 1) % RUBRAVIEW_TAP_SAMPLES;
        tap->total++;
    }
}

size_t rubraview_audio_tap_latest(const rubraview_audio_tap_t *tap, float *out, size_t count) {
    if (!tap || !out) return 0;
    if (count > RUBRAVIEW_TAP_SAMPLES) count = RUBRAVIEW_TAP_SAMPLES;
    if ((uint64_t)count > tap->total) count = (size_t)tap->total;
    size_t start = (tap->write + RUBRAVIEW_TAP_SAMPLES - count) % RUBRAVIEW_TAP_SAMPLES;
    for (size_t i = 0; i < count; ++i) out[i] = tap->samples[(start + i) % RUBRAVIEW_TAP_SAMPLES];
    return count;
}
