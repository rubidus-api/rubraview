#include "rubraview/audio_dsp.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static u8str_t lit(const char *s) { return (u8str_t){ .ptr = s, .len = strlen(s) }; }

int main(void) {
    printf("[test_audio_dsp] Starting audio analysis and DSP tests...\n");

    /* Test 1: the transform. A sine wave at a known frequency has to
       land in the bin that frequency belongs to — this is the one
       assertion that proves the FFT is a transform and not just an
       array shuffle. */
    {
        static float real[RUBRAVIEW_FFT_SIZE], imag[RUBRAVIEW_FFT_SIZE];
        double sample_rate = 48000.0;
        double frequency = 1500.0;   /* exactly 32 bins in at this size */

        for (size_t i = 0; i < RUBRAVIEW_FFT_SIZE; ++i) {
            real[i] = (float)sin(2.0 * M_PI * frequency * (double)i / sample_rate);
            imag[i] = 0.0f;
        }
        rubraview_fft(real, imag);

        size_t expected = (size_t)(frequency * RUBRAVIEW_FFT_SIZE / sample_rate);
        size_t loudest = 1;
        double best = 0.0;
        for (size_t k = 1; k < RUBRAVIEW_FFT_SIZE / 2; ++k) {
            double magnitude = sqrt((double)real[k] * real[k] + (double)imag[k] * imag[k]);
            if (magnitude > best) { best = magnitude; loudest = k; }
        }
        assert(loudest == expected);
        assert(best > 100.0);   /* a real peak, not noise */
    }
    printf("  [PASS] A 1500 Hz tone lands in the bin 1500 Hz belongs to\n");

    /* Test 2: silence in, silence out — and no NaN, which is what a
       naive log of zero would produce. */
    {
        static float real[RUBRAVIEW_FFT_SIZE], imag[RUBRAVIEW_FFT_SIZE];
        memset(real, 0, sizeof(real));
        memset(imag, 0, sizeof(imag));
        rubraview_fft(real, imag);

        float bands[RUBRAVIEW_SPECTRUM_BANDS];
        rubraview_spectrum_bands(real, imag, 48000.0, -90.0, bands, RUBRAVIEW_SPECTRUM_BANDS);
        for (size_t i = 0; i < RUBRAVIEW_SPECTRUM_BANDS; ++i) {
            assert(bands[i] == -90.0f);
            assert(!isnan(bands[i]));
        }
    }
    printf("  [PASS] Silence gives a flat floor rather than negative infinity\n");

    /* Test 3: the bands are logarithmic, so a bass tone lights a low
       band and a treble tone a high one — with linear spacing they would
       both land in the bottom quarter. */
    {
        static float real[RUBRAVIEW_FFT_SIZE], imag[RUBRAVIEW_FFT_SIZE];
        float low_bands[RUBRAVIEW_SPECTRUM_BANDS], high_bands[RUBRAVIEW_SPECTRUM_BANDS];

        for (int pass = 0; pass < 2; ++pass) {
            double frequency = pass == 0 ? 100.0 : 8000.0;
            for (size_t i = 0; i < RUBRAVIEW_FFT_SIZE; ++i) {
                real[i] = (float)sin(2.0 * M_PI * frequency * (double)i / 48000.0);
                imag[i] = 0.0f;
            }
            rubraview_apply_hann(real, RUBRAVIEW_FFT_SIZE);
            rubraview_fft(real, imag);
            rubraview_spectrum_bands(real, imag, 48000.0, -90.0,
                                     pass == 0 ? low_bands : high_bands, RUBRAVIEW_SPECTRUM_BANDS);
        }

        size_t low_peak = 0, high_peak = 0;
        for (size_t i = 1; i < RUBRAVIEW_SPECTRUM_BANDS; ++i) {
            if (low_bands[i] > low_bands[low_peak]) low_peak = i;
            if (high_bands[i] > high_bands[high_peak]) high_peak = i;
        }
        assert(low_peak < RUBRAVIEW_SPECTRUM_BANDS / 3);
        assert(high_peak > RUBRAVIEW_SPECTRUM_BANDS * 2 / 3);
    }
    printf("  [PASS] Bass and treble light bands at opposite ends\n");

    /* Test 4: §3.14.2.2's peak-hold needles fall, and are pushed back up. */
    {
        float bands[4] = { -10.0f, -60.0f, -60.0f, -60.0f };
        float peaks[4] = { -90.0f, -90.0f, -90.0f, -90.0f };

        rubraview_spectrum_peaks(bands, peaks, 4, 0.0, 20.0);
        assert(peaks[0] == -10.0f);   /* jumps straight up */

        bands[0] = -80.0f;
        rubraview_spectrum_peaks(bands, peaks, 4, 1.0, 20.0);
        assert(fabsf(peaks[0] - (-30.0f)) < 0.001f);   /* fell 20 dB in a second */

        bands[0] = -20.0f;
        rubraview_spectrum_peaks(bands, peaks, 4, 0.1, 20.0);
        assert(peaks[0] == -20.0f);   /* a louder band pushes it back up */
    }
    printf("  [PASS] Peak needles fall at their rate and are pushed straight back up\n");

    /* Test 5: a flat equaliser is *exactly* a pass-through. One that
       still colours the sound is worse than no equaliser at all. */
    {
        rubraview_equalizer_t eq = rubraview_eq_create(48000.0);
        assert(!eq.enabled);

        float samples[64];
        float original[64];
        for (size_t i = 0; i < 64; ++i) {
            samples[i] = (float)sin((double)i * 0.3);
            original[i] = samples[i];
        }
        rubraview_eq_process(&eq, samples, 64);
        assert(memcmp(samples, original, sizeof(samples)) == 0);

        /* And a band explicitly set to 0 dB is the identity filter. */
        rubraview_biquad_t identity = rubraview_biquad_peaking(48000.0, 1000.0, 0.0, 1.4);
        assert(identity.b0 == 1.0 && identity.b1 == 0.0 && identity.b2 == 0.0);
        assert(identity.a1 == 0.0 && identity.a2 == 0.0);
    }
    printf("  [PASS] A flat equaliser changes nothing at all\n");

    /* Test 6: a boost at a frequency raises that frequency, and a cut
       lowers it. Measured through the filter rather than assumed. */
    {
        double sample_rate = 48000.0;
        double frequency = 1000.0;

        for (int direction = 0; direction < 2; ++direction) {
            double gain_db = direction == 0 ? 12.0 : -12.0;
            rubraview_biquad_t filter = rubraview_biquad_peaking(sample_rate, frequency, gain_db, 1.4);

            /* Run a tone through and compare the settled amplitude. */
            double peak_in = 0.0, peak_out = 0.0;
            for (size_t i = 0; i < 4096; ++i) {
                double sample = sin(2.0 * M_PI * frequency * (double)i / sample_rate);
                double out = rubraview_biquad_process(&filter, sample);
                if (i > 2048) {   /* after the filter has settled */
                    if (fabs(sample) > peak_in) peak_in = fabs(sample);
                    if (fabs(out) > peak_out) peak_out = fabs(out);
                }
            }

            double measured_db = 20.0 * log10(peak_out / peak_in);
            assert(fabs(measured_db - gain_db) < 1.0);
        }
    }
    printf("  [PASS] A boost and a cut move the tone by the decibels they asked for\n");

    /* Test 7: §3.14.5's range and presets. */
    {
        rubraview_equalizer_t eq = rubraview_eq_create(48000.0);

        rubraview_eq_set_gain(&eq, 48000.0, 0, 99.0);
        assert(eq.gains_db[0] == 12.0f);
        rubraview_eq_set_gain(&eq, 48000.0, 0, -99.0);
        assert(eq.gains_db[0] == -12.0f);
        assert(eq.enabled);

        rubraview_eq_set_preset(&eq, 48000.0, RUBRAVIEW_EQ_FLAT);
        assert(!eq.enabled);   /* flat means off */

        rubraview_eq_set_preset(&eq, 48000.0, RUBRAVIEW_EQ_BASS_BOOST);
        assert(eq.enabled);
        assert(eq.gains_db[0] > eq.gains_db[9]);   /* bass above treble */

        const double *frequencies = rubraview_eq_frequencies();
        assert(frequencies[0] == 31.0 && frequencies[9] == 16000.0);
        for (size_t i = 1; i < RUBRAVIEW_EQ_BANDS; ++i) assert(frequencies[i] > frequencies[i - 1]);

        for (int p = 0; p < RUBRAVIEW_EQ_PRESET_COUNT; ++p) {
            assert(rubraview_eq_preset_name((rubraview_eq_preset_t)p).len > 0);
            float gains[RUBRAVIEW_EQ_BANDS];
            rubraview_eq_preset_gains((rubraview_eq_preset_t)p, gains);
            for (size_t i = 0; i < RUBRAVIEW_EQ_BANDS; ++i) assert(gains[i] >= -12.0f && gains[i] <= 12.0f);
        }
    }
    printf("  [PASS] Gains clamp to ±12 dB and every preset is inside the range\n");

    /* Test 8: §3.14.5's ReplayGain, including the clipping guard. */
    {
        rubraview_replaygain_t tags = {
            .has_track_gain = true, .track_gain_db = -6.0, .track_peak = 0.9,
            .has_album_gain = true, .album_gain_db = -3.0, .album_peak = 0.95,
        };

        assert(rubraview_replaygain_factor(&tags, RUBRAVIEW_REPLAYGAIN_OFF, 0.0) == 1.0);

        double track = rubraview_replaygain_factor(&tags, RUBRAVIEW_REPLAYGAIN_TRACK, 0.0);
        assert(fabs(track - pow(10.0, -6.0 / 20.0)) < 0.001);

        double album = rubraview_replaygain_factor(&tags, RUBRAVIEW_REPLAYGAIN_ALBUM, 0.0);
        assert(album > track);   /* -3 dB is louder than -6 dB */

        /* A pre-amp that would push the peak past full scale is reduced
           until it does not: loud and distorted is worse than loud. */
        double boosted = rubraview_replaygain_factor(&tags, RUBRAVIEW_REPLAYGAIN_TRACK, 24.0);
        assert(boosted * tags.track_peak <= 1.0001);

        /* Album gain asked for but absent falls back to track gain. */
        rubraview_replaygain_t track_only = { .has_track_gain = true, .track_gain_db = -6.0, .track_peak = 0.5 };
        double fallback = rubraview_replaygain_factor(&track_only, RUBRAVIEW_REPLAYGAIN_ALBUM, 0.0);
        assert(fabs(fallback - pow(10.0, -6.0 / 20.0)) < 0.001);

        rubraview_replaygain_t nothing = {0};
        assert(rubraview_replaygain_factor(&nothing, RUBRAVIEW_REPLAYGAIN_TRACK, 0.0) == 1.0);
    }
    printf("  [PASS] ReplayGain levels the volume and never lets the result clip\n");

    /* Test 9: parsing the tag as it is actually written. */
    {
        double value = 0.0;
        assert(rubraview_replaygain_parse_db(lit("-7.230000 dB"), &value) && fabs(value + 7.23) < 0.001);
        assert(rubraview_replaygain_parse_db(lit("+3.5 dB"), &value) && fabs(value - 3.5) < 0.001);
        assert(rubraview_replaygain_parse_db(lit("-2.0"), &value) && fabs(value + 2.0) < 0.001);
        assert(!rubraview_replaygain_parse_db(lit("loud"), &value));
        assert(!rubraview_replaygain_parse_db(lit("3.0 volts"), &value));
        assert(!rubraview_replaygain_parse_db(lit(""), &value));
    }
    printf("  [PASS] A ReplayGain tag parses, and nonsense in it is refused\n");

    /* Test 10: §3.14.5's night mode leaves quiet passages alone and
       pulls loud ones down. */
    {
        assert(rubraview_night_mode_gain(0.01, -20.0, 4.0) == 1.0);   /* well below the knee */
        assert(rubraview_night_mode_gain(0.0, -20.0, 4.0) == 1.0);    /* silence */

        double loud = rubraview_night_mode_gain(1.0, -20.0, 4.0);
        assert(loud < 1.0 && loud > 0.0);

        /* Louder input means more reduction, never less. */
        double louder = rubraview_night_mode_gain(1.0, -20.0, 8.0);
        assert(louder < loud);

        /* A ratio of 1 is no compression at all. */
        assert(fabs(rubraview_night_mode_gain(1.0, -20.0, 1.0) - 1.0) < 0.0001);
    }
    printf("  [PASS] Night mode leaves quiet passages alone and pulls loud ones down\n");

    /* D-15: speed. Step 1 hands the samples through; step 2 takes every
       other one; step 0.5 puts the midpoint between each pair; and cutting
       the stream into chunks changes nothing. */
    {
        float src[64];
        for (int i = 0; i < 32; ++i) { src[2 * i] = (float)i; src[2 * i + 1] = (float)(-i); }
        float out[256];

        rubraview_speed_resampler_t same = rubraview_speed_resampler_create(2);
        size_t need = rubraview_speed_source_needed(&same, 32, 1.0, 32);
        assert(need == 32);
        assert(rubraview_speed_resample(&same, src, need, 1.0, out, 32) == 32);
        for (int i = 0; i < 32; ++i) assert(out[2 * i] == (float)i && out[2 * i + 1] == (float)(-i));

        rubraview_speed_resampler_t fast = rubraview_speed_resampler_create(2);
        need = rubraview_speed_source_needed(&fast, 16, 2.0, 32);
        assert(need == 31);
        size_t made = rubraview_speed_resample(&fast, src, need, 2.0, out, 16);
        assert(made == 16);
        for (int i = 0; i < 16; ++i) assert(out[2 * i] == (float)(2 * i));

        rubraview_speed_resampler_t slow = rubraview_speed_resampler_create(2);
        need = rubraview_speed_source_needed(&slow, 7, 0.5, 32);
        assert(need == 4);
        made = rubraview_speed_resample(&slow, src, need, 0.5, out, 7);
        assert(made == 7);
        static const float want[7] = { 0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f };
        for (int i = 0; i < 7; ++i) assert(fabs(out[2 * i] - want[i]) < 1e-6);

        /* 1.25x over the whole stream, and in chunks of 5 device frames. */
        float whole[256], chunked[256];
        rubraview_speed_resampler_t a = rubraview_speed_resampler_create(2), b = rubraview_speed_resampler_create(2);
        size_t n_whole = rubraview_speed_resample(&a, src, rubraview_speed_source_needed(&a, 24, 1.25, 32), 1.25, whole, 24);
        size_t n_chunk = 0, read = 0;
        while (n_chunk < n_whole) {
            size_t k = rubraview_speed_source_needed(&b, 5, 1.25, 32 - read);
            size_t m = rubraview_speed_resample(&b, src + 2 * read, k, 1.25, chunked + 2 * n_chunk, 5);
            if (m == 0) break;
            read += k;
            n_chunk += m;
        }
        assert(n_chunk >= n_whole);
        for (size_t i = 0; i < n_whole * 2; ++i) assert(fabs(whole[i] - chunked[i]) < 1e-5);
    }
    printf("  [PASS] Speed resampling: 1x passes through, 2x and 0.5x interpolate, chunks change nothing\n");

    printf("[test_audio_dsp] All tests passed successfully!\n");
    return 0;
}
