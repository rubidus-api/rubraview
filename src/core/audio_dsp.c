#include "rubraview/audio_dsp.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- FFT ---- */

/* Radix-2 Cooley-Tukey, in place. The bit-reversal permutation first,
   then log2(N) passes of butterflies. Written out because a
   1024-point transform is not enough work to justify a dependency. */
void rubraview_fft(float *real, float *imag) {
    const size_t n = RUBRAVIEW_FFT_SIZE;
    if (!real || !imag) return;

    /* Bit-reversal: swap each index with the reverse of its bits. */
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;

        if (i < j) {
            float tr = real[i]; real[i] = real[j]; real[j] = tr;
            float ti = imag[i]; imag[i] = imag[j]; imag[j] = ti;
        }
    }

    for (size_t len = 2; len <= n; len <<= 1) {
        double angle = -2.0 * M_PI / (double)len;
        double wr = cos(angle), wi = sin(angle);

        for (size_t i = 0; i < n; i += len) {
            double cur_r = 1.0, cur_i = 0.0;
            for (size_t k = 0; k < len / 2; ++k) {
                double ur = real[i + k], ui = imag[i + k];
                double vr = real[i + k + len / 2] * cur_r - imag[i + k + len / 2] * cur_i;
                double vi = real[i + k + len / 2] * cur_i + imag[i + k + len / 2] * cur_r;

                real[i + k] = (float)(ur + vr);
                imag[i + k] = (float)(ui + vi);
                real[i + k + len / 2] = (float)(ur - vr);
                imag[i + k + len / 2] = (float)(ui - vi);

                double next_r = cur_r * wr - cur_i * wi;
                cur_i = cur_r * wi + cur_i * wr;
                cur_r = next_r;
            }
        }
    }
}

void rubraview_apply_hann(float *samples, size_t count) {
    if (!samples || count < 2) return;
    for (size_t i = 0; i < count; ++i) {
        double w = 0.5 * (1.0 - cos(2.0 * M_PI * (double)i / (double)(count - 1)));
        samples[i] = (float)(samples[i] * w);
    }
}

void rubraview_spectrum_bands(const float *real, const float *imag,
                              double sample_rate, double floor_db,
                              float *out_bands, size_t band_count) {
    if (!real || !imag || !out_bands || band_count == 0 || sample_rate <= 0.0) return;

    const size_t half = RUBRAVIEW_FFT_SIZE / 2;
    double bin_hz = sample_rate / (double)RUBRAVIEW_FFT_SIZE;

    /* Logarithmic spacing from 20 Hz to just under Nyquist. Hearing is
       logarithmic: linear bands would spend three quarters of the
       display on frequencies nobody hears as distinct. */
    double low_hz = 20.0;
    double high_hz = sample_rate * 0.45;
    if (high_hz <= low_hz) high_hz = low_hz * 2.0;
    double ratio = log(high_hz / low_hz);

    for (size_t b = 0; b < band_count; ++b) {
        double from_hz = low_hz * exp(ratio * (double)b / (double)band_count);
        double to_hz = low_hz * exp(ratio * (double)(b + 1) / (double)band_count);

        size_t from_bin = (size_t)(from_hz / bin_hz);
        size_t to_bin = (size_t)(to_hz / bin_hz);
        if (to_bin <= from_bin) to_bin = from_bin + 1;   /* a band is never empty */
        if (to_bin > half) to_bin = half;
        if (from_bin >= half) { out_bands[b] = (float)floor_db; continue; }

        /* The loudest bin in the band, not the average: a single strong
           tone should show as a bar, and averaging it with its quiet
           neighbours would hide it. */
        double peak = 0.0;
        for (size_t k = from_bin; k < to_bin; ++k) {
            double magnitude = sqrt((double)real[k] * real[k] + (double)imag[k] * imag[k]);
            if (magnitude > peak) peak = magnitude;
        }

        double db = peak > 1e-12 ? 20.0 * log10(peak / (double)half) : floor_db;
        if (db < floor_db) db = floor_db;
        out_bands[b] = (float)db;
    }
}

void rubraview_spectrum_peaks(const float *bands, float *peaks, size_t band_count,
                              double delta_seconds, double decay_db_per_second) {
    if (!bands || !peaks) return;
    for (size_t i = 0; i < band_count; ++i) {
        double fallen = (double)peaks[i] - decay_db_per_second * delta_seconds;
        peaks[i] = bands[i] > (float)fallen ? bands[i] : (float)fallen;
    }
}

/* ---- the equaliser ---- */

static const double EQ_FREQUENCIES[RUBRAVIEW_EQ_BANDS] = {
    31.0, 62.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0
};

const double *rubraview_eq_frequencies(void) { return EQ_FREQUENCIES; }

static const float EQ_PRESETS[RUBRAVIEW_EQ_PRESET_COUNT][RUBRAVIEW_EQ_BANDS] = {
    /* Flat        */ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    /* Rock        */ { 5, 4, 2, -1, -2, 0, 3, 5, 6, 6 },
    /* Pop         */ { -1, 1, 3, 4, 4, 2, 0, -1, -1, -1 },
    /* Jazz        */ { 4, 3, 1, 2, -1, -1, 0, 1, 3, 4 },
    /* Classical   */ { 4, 3, 2, 0, -1, -1, 0, 2, 3, 4 },
    /* Bass boost  */ { 8, 7, 5, 3, 1, 0, 0, 0, 0, 0 },
    /* Vocal boost */ { -2, -2, 0, 2, 5, 5, 4, 2, 0, -1 },
    /* Acoustic    */ { 3, 3, 2, 1, 1, 1, 2, 3, 3, 2 },
};

void rubraview_eq_preset_gains(rubraview_eq_preset_t preset, float *out_gains_db) {
    if (!out_gains_db) return;
    if ((int)preset < 0 || preset >= RUBRAVIEW_EQ_PRESET_COUNT) preset = RUBRAVIEW_EQ_FLAT;
    memcpy(out_gains_db, EQ_PRESETS[preset], sizeof(EQ_PRESETS[0]));
}

u8str_t rubraview_eq_preset_name(rubraview_eq_preset_t preset) {
    switch (preset) {
        case RUBRAVIEW_EQ_FLAT: return U8("Flat");
        case RUBRAVIEW_EQ_ROCK: return U8("Rock");
        case RUBRAVIEW_EQ_POP: return U8("Pop");
        case RUBRAVIEW_EQ_JAZZ: return U8("Jazz");
        case RUBRAVIEW_EQ_CLASSICAL: return U8("Classical");
        case RUBRAVIEW_EQ_BASS_BOOST: return U8("Bass boost");
        case RUBRAVIEW_EQ_VOCAL_BOOST: return U8("Vocal boost");
        case RUBRAVIEW_EQ_ACOUSTIC: return U8("Acoustic");
        default: return U8("");
    }
}

rubraview_biquad_t rubraview_biquad_peaking(double sample_rate, double frequency,
                                            double gain_db, double q) {
    rubraview_biquad_t filter = {0};

    /* Identity: a "flat" equaliser that still colours the sound is worse
       than no equaliser, so 0 dB is exactly a pass-through. */
    filter.b0 = 1.0; filter.b1 = 0.0; filter.b2 = 0.0;
    filter.a1 = 0.0; filter.a2 = 0.0;

    if (sample_rate <= 0.0 || frequency <= 0.0 || frequency >= sample_rate * 0.5) return filter;
    if (gain_db == 0.0) return filter;
    if (q <= 0.0) q = 1.0;

    /* The standard peaking-EQ biquad (Robert Bristow-Johnson's cookbook
       formulae), normalised by a0. */
    double amplitude = pow(10.0, gain_db / 40.0);
    double omega = 2.0 * M_PI * frequency / sample_rate;
    double sn = sin(omega), cs = cos(omega);
    double alpha = sn / (2.0 * q);

    double b0 = 1.0 + alpha * amplitude;
    double b1 = -2.0 * cs;
    double b2 = 1.0 - alpha * amplitude;
    double a0 = 1.0 + alpha / amplitude;
    double a1 = -2.0 * cs;
    double a2 = 1.0 - alpha / amplitude;

    filter.b0 = b0 / a0;
    filter.b1 = b1 / a0;
    filter.b2 = b2 / a0;
    filter.a1 = a1 / a0;
    filter.a2 = a2 / a0;
    return filter;
}

double rubraview_biquad_process(rubraview_biquad_t *f, double sample) {
    if (!f) return sample;

    double out = f->b0 * sample + f->b1 * f->x1 + f->b2 * f->x2
                 - f->a1 * f->y1 - f->a2 * f->y2;

    f->x2 = f->x1; f->x1 = sample;
    f->y2 = f->y1; f->y1 = out;
    return out;
}

void rubraview_biquad_reset(rubraview_biquad_t *f) {
    if (!f) return;
    f->x1 = f->x2 = f->y1 = f->y2 = 0.0;
}

rubraview_equalizer_t rubraview_eq_create(double sample_rate) {
    rubraview_equalizer_t eq = {0};
    for (size_t i = 0; i < RUBRAVIEW_EQ_BANDS; ++i) {
        eq.gains_db[i] = 0.0f;
        /* A Q of about 1.4 gives ten bands that overlap enough to be
           smooth and not so much that one slider moves its neighbours. */
        eq.bands[i] = rubraview_biquad_peaking(sample_rate, EQ_FREQUENCIES[i], 0.0, 1.4);
    }
    eq.enabled = false;
    return eq;
}

void rubraview_eq_set_gain(rubraview_equalizer_t *eq, double sample_rate, size_t band, double gain_db) {
    if (!eq || band >= RUBRAVIEW_EQ_BANDS) return;

    if (gain_db < -12.0) gain_db = -12.0;   /* §3.14.5's range */
    if (gain_db > 12.0) gain_db = 12.0;

    eq->gains_db[band] = (float)gain_db;

    /* Rebuilding keeps the filter's memory, so changing a slider during
       playback does not click. */
    rubraview_biquad_t rebuilt = rubraview_biquad_peaking(sample_rate, EQ_FREQUENCIES[band], gain_db, 1.4);
    rebuilt.x1 = eq->bands[band].x1;
    rebuilt.x2 = eq->bands[band].x2;
    rebuilt.y1 = eq->bands[band].y1;
    rebuilt.y2 = eq->bands[band].y2;
    eq->bands[band] = rebuilt;

    eq->enabled = false;
    for (size_t i = 0; i < RUBRAVIEW_EQ_BANDS; ++i) {
        if (eq->gains_db[i] != 0.0f) { eq->enabled = true; break; }
    }
}

void rubraview_eq_set_preset(rubraview_equalizer_t *eq, double sample_rate, rubraview_eq_preset_t preset) {
    if (!eq) return;
    float gains[RUBRAVIEW_EQ_BANDS];
    rubraview_eq_preset_gains(preset, gains);
    for (size_t i = 0; i < RUBRAVIEW_EQ_BANDS; ++i) {
        rubraview_eq_set_gain(eq, sample_rate, i, gains[i]);
    }
}

void rubraview_eq_process(rubraview_equalizer_t *eq, float *samples, size_t count) {
    if (!eq || !samples || !eq->enabled) return;

    for (size_t i = 0; i < count; ++i) {
        double sample = samples[i];
        for (size_t b = 0; b < RUBRAVIEW_EQ_BANDS; ++b) {
            if (eq->gains_db[b] == 0.0f) continue;   /* a flat band costs nothing */
            sample = rubraview_biquad_process(&eq->bands[b], sample);
        }
        /* Ten boosted bands can push a loud passage past full scale. */
        if (sample > 1.0) sample = 1.0;
        if (sample < -1.0) sample = -1.0;
        samples[i] = (float)sample;
    }
}

/* ---- ReplayGain ---- */

double rubraview_replaygain_factor(const rubraview_replaygain_t *tags,
                                   rubraview_replaygain_mode_t mode,
                                   double preamp_db) {
    if (!tags || mode == RUBRAVIEW_REPLAYGAIN_OFF) return 1.0;

    double gain_db = 0.0;
    double peak = 0.0;

    if (mode == RUBRAVIEW_REPLAYGAIN_ALBUM && tags->has_album_gain) {
        gain_db = tags->album_gain_db;
        peak = tags->album_peak;
    } else if (tags->has_track_gain) {
        /* Album gain asked for but absent falls back to track gain,
           which is better than no levelling at all. */
        gain_db = tags->track_gain_db;
        peak = tags->track_peak;
    } else {
        return 1.0;
    }

    gain_db += preamp_db;
    double factor = pow(10.0, gain_db / 20.0);

    /* A gain that would clip is reduced until it does not: a track that
       is loud *and* distorted is worse than one that is merely loud. */
    if (peak > 0.0 && factor * peak > 1.0) factor = 1.0 / peak;

    return factor;
}

bool rubraview_replaygain_parse_db(u8str_t text, double *out_db) {
    if (!out_db || text.len == 0 || text.len > 64) return false;

    char buffer[72];
    memcpy(buffer, text.ptr, text.len);
    buffer[text.len] = '\0';

    char *end = NULL;
    double value = strtod(buffer, &end);
    if (end == buffer) return false;

    /* The tag is conventionally "-7.230000 dB"; anything after the
       number that is not a unit means it is not a gain. */
    while (*end == ' ' || *end == '\t') end++;
    if (*end != '\0') {
        if (!((end[0] == 'd' || end[0] == 'D') && (end[1] == 'b' || end[1] == 'B'))) return false;
    }

    *out_db = value;
    return true;
}

double rubraview_night_mode_gain(double magnitude, double threshold_db, double ratio) {
    if (magnitude <= 1e-9) return 1.0;
    if (ratio < 1.0) ratio = 1.0;

    double level_db = 20.0 * log10(magnitude);
    if (level_db <= threshold_db) return 1.0;   /* below the knee, nothing happens */

    /* Above the threshold, every decibel of input becomes 1/ratio of a
       decibel of output — which is what makes a whisper and an explosion
       sit closer together. */
    double over = level_db - threshold_db;
    double target_db = threshold_db + over / ratio;
    return pow(10.0, (target_db - level_db) / 20.0);
}
