#ifndef RUBRAVIEW_AUDIO_DSP_H
#define RUBRAVIEW_AUDIO_DSP_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Audio analysis and processing (RFC-0001 §3.14.2.2, §3.14.5),
 * RV-077 and RV-080.
 *
 * None of this needs FFmpeg. A Fourier transform, a biquad filter and a
 * gain calculation take an array of samples and produce another one; the
 * only thing the decoder supplies is the samples. So all of it is here,
 * where the host suite can check it against arithmetic that is known in
 * advance — a sine wave at a known frequency has to land in a known bin,
 * and a filter set to 0 dB has to change nothing at all.
 */

/* ---- §3.14.2.2 the spectrum analyser ---- */

#define RUBRAVIEW_FFT_SIZE 1024
#define RUBRAVIEW_SPECTRUM_BANDS 64

/**
 * A real-input FFT, in place. `real` and `imag` are `RUBRAVIEW_FFT_SIZE`
 * long; `imag` should be zeroed by the caller.
 *
 * This is the ordinary radix-2 Cooley-Tukey transform. It is written out
 * rather than pulled in because a thousand-point transform sixty times a
 * second is not enough work to justify a dependency.
 */
void rubraview_fft(float *real, float *imag);

/** A Hann window, applied in place. Without one, a tone that does not sit
    exactly on a bin smears across all of them. */
void rubraview_apply_hann(float *samples, size_t count);

/**
 * §3.14.2.2's 64 bands, from a windowed FFT's magnitudes. The bands are
 * spaced logarithmically, because hearing is: an octave is an octave
 * whether it is 100 Hz wide or 10 kHz wide, and linear bands would give
 * three quarters of the display to frequencies almost nobody hears as
 * distinct.
 *
 * Output is in decibels, floored at `floor_db` so silence is a flat line
 * rather than negative infinity.
 */
void rubraview_spectrum_bands(const float *real, const float *imag,
                              double sample_rate, double floor_db,
                              float *out_bands, size_t band_count);

/**
 * §3.14.2.2's peak-hold needles: each band's peak falls at `decay_db`
 * per second, and is pushed straight back up by a louder band.
 */
void rubraview_spectrum_peaks(const float *bands, float *peaks, size_t band_count,
                              double delta_seconds, double decay_db_per_second);

/* ---- §3.14.5 the equaliser ---- */

#define RUBRAVIEW_EQ_BANDS 10

/** The centre frequencies §3.14.5 names, in hertz. */
const double *rubraview_eq_frequencies(void);

typedef enum rubraview_eq_preset {
    RUBRAVIEW_EQ_FLAT = 0,
    RUBRAVIEW_EQ_ROCK,
    RUBRAVIEW_EQ_POP,
    RUBRAVIEW_EQ_JAZZ,
    RUBRAVIEW_EQ_CLASSICAL,
    RUBRAVIEW_EQ_BASS_BOOST,
    RUBRAVIEW_EQ_VOCAL_BOOST,
    RUBRAVIEW_EQ_ACOUSTIC,
    RUBRAVIEW_EQ_PRESET_COUNT,
} rubraview_eq_preset_t;

/** A preset's ten gains, in decibels. */
void rubraview_eq_preset_gains(rubraview_eq_preset_t preset, float *out_gains_db);
u8str_t rubraview_eq_preset_name(rubraview_eq_preset_t preset);

/* One peaking-EQ biquad, and its running state. */
typedef struct rubraview_biquad {
    double b0, b1, b2, a1, a2;
    double x1, x2, y1, y2;
} rubraview_biquad_t;

/**
 * A peaking filter at `frequency` with `gain_db` and quality `q`. A gain
 * of exactly 0 dB produces the identity filter — which matters, because
 * a "flat" equaliser that still colours the sound is worse than none.
 */
rubraview_biquad_t rubraview_biquad_peaking(double sample_rate, double frequency,
                                            double gain_db, double q);

/** Run one sample through. */
double rubraview_biquad_process(rubraview_biquad_t *filter, double sample);

/** Clear the filter's memory — on a seek, where the past no longer applies. */
void rubraview_biquad_reset(rubraview_biquad_t *filter);

typedef struct rubraview_equalizer {
    rubraview_biquad_t bands[RUBRAVIEW_EQ_BANDS];
    float gains_db[RUBRAVIEW_EQ_BANDS];
    bool  enabled;
} rubraview_equalizer_t;

rubraview_equalizer_t rubraview_eq_create(double sample_rate);
void rubraview_eq_set_gain(rubraview_equalizer_t *eq, double sample_rate, size_t band, double gain_db);
void rubraview_eq_set_preset(rubraview_equalizer_t *eq, double sample_rate, rubraview_eq_preset_t preset);
void rubraview_eq_process(rubraview_equalizer_t *eq, float *samples, size_t count);

/* ---- §3.14.5 ReplayGain and night mode ---- */

typedef enum rubraview_replaygain_mode {
    RUBRAVIEW_REPLAYGAIN_OFF = 0,
    RUBRAVIEW_REPLAYGAIN_TRACK,
    RUBRAVIEW_REPLAYGAIN_ALBUM,
} rubraview_replaygain_mode_t;

typedef struct rubraview_replaygain {
    bool   has_track_gain, has_album_gain;
    double track_gain_db, album_gain_db;
    double track_peak, album_peak;   /* 1.0 is full scale */
} rubraview_replaygain_t;

/**
 * The linear gain to apply. Peak information is honoured: a gain that
 * would clip is reduced until it does not, because a track that is
 * loud *and* distorted is worse than one that is merely loud.
 */
double rubraview_replaygain_factor(const rubraview_replaygain_t *tags,
                                   rubraview_replaygain_mode_t mode,
                                   double preamp_db);

/** Parse a ReplayGain tag value such as "-7.230000 dB". */
bool rubraview_replaygain_parse_db(u8str_t text, double *out_db);

/**
 * §3.14.5's night mode: a soft-knee compressor above `threshold_db`.
 * Returns the linear gain for a sample of the given magnitude, so the
 * caller can apply it to both channels alike and keep the stereo image.
 */
double rubraview_night_mode_gain(double magnitude, double threshold_db, double ratio);

/* ---- D-15: playback speed ---- */

#define RUBRAVIEW_SPEED_MAX_CHANNELS 8

/**
 * Playing at a speed other than 1 by reading the file's samples `step`
 * frames per device frame, linearly interpolated — so the pitch moves
 * with the speed (RFC-0001 §3.7.2 "dynamic pitch-shift"). The state
 * carries the position between the last frame read and the next across
 * calls, so a stream cut into any chunks sounds the same.
 */
typedef struct rubraview_speed_resampler {
    uint32_t channels;
    double   phase;                                   /* position after `prev`, in source frames */
    float    prev[RUBRAVIEW_SPEED_MAX_CHANNELS];      /* the last source frame read */
} rubraview_speed_resampler_t;

rubraview_speed_resampler_t rubraview_speed_resampler_create(uint32_t channels);

/**
 * How many source frames to read so that at most `out_frames` device
 * frames come out at `step`, given what the stream holds (`available`).
 */
size_t rubraview_speed_source_needed(const rubraview_speed_resampler_t *r, size_t out_frames, double step,
                                     size_t available);

/**
 * Turn exactly the `src_frames` just read into device frames at `step`;
 * returns how many were written to `dst` (never more than `dst_frames`
 * when `src_frames` came from rubraview_speed_source_needed).
 */
size_t rubraview_speed_resample(rubraview_speed_resampler_t *r, const float *src, size_t src_frames,
                                double step, float *dst, size_t dst_frames);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_AUDIO_DSP_H */
