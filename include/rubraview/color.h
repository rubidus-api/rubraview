#ifndef RUBRAVIEW_COLOR_H
#define RUBRAVIEW_COLOR_H

#include "rubraview/core.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rubraview_color_adjust_params {
    float exposure_ev;  /* Exposure value shift: e.g. -4.0f to +4.0f (0.0f = neutral) */
    float contrast;     /* Contrast shift: -100.0f to +100.0f (0.0f = neutral) */
    float saturation;   /* Saturation factor: 0.0f (grayscale) to 2.0f (200%), 1.0f = neutral */
    float gamma;        /* Gamma exponent: 0.2f to 3.0f (1.0f = neutral) */
} rubraview_color_adjust_params_t;

typedef struct rubraview_curve_point {
    float x;            /* Input value: 0.0f to 255.0f */
    float y;            /* Output value: 0.0f to 255.0f */
} rubraview_curve_point_t;

/**
 * Initialize sRGB <-> Linear RGB lookup tables.
 * Safe to call multiple times.
 */
void rubraview_color_lut_init(void);

/**
 * Convert an 8-bit sRGB value (0..255) to linear float (0.0f..1.0f).
 */
float rubraview_srgb_to_linear(uint8_t srgb);

/**
 * Convert a linear float value (0.0f..1.0f) to 8-bit sRGB (0..255).
 */
uint8_t rubraview_linear_to_srgb(float lin);

/**
 * Apply exposure, contrast, saturation, and gamma adjustments in-place on a pixel buffer.
 * Supports RGBA8 and BGRA8 formats.
 */
void rubraview_color_adjust(rubraview_pixbuf_t *pb, const rubraview_color_adjust_params_t *params);

/**
 * Compute 256-bin histograms for Red, Green, Blue, and Luminance channels.
 * Luminance is computed using standard Rec. 709 coefficients.
 */
void rubraview_histogram_compute(const rubraview_pixbuf_t *pb,
                          uint32_t hist_r[256],
                          uint32_t hist_g[256],
                          uint32_t hist_b[256],
                          uint32_t hist_lum[256]);

/**
 * Build a 256-entry monotonic cubic spline lookup table (Fritsch-Carlson)
 * from an array of sorted control points.
 */
void rubraview_curve_build_lut(uint8_t lut_out[256], const rubraview_curve_point_t *points, size_t count);

/**
 * Build a 256-entry Levels adjustment lookup table.
 * black_point: input values <= black_point map to 0.
 * white_point: input values >= white_point map to 255.
 * gamma: midtone gamma curve (1.0f = linear).
 */
void rubraview_levels_build_lut(uint8_t lut_out[256], uint8_t black_point, uint8_t white_point, float gamma);

/**
 * Apply per-channel lookup tables to a pixel buffer in-place.
 * lut_r, lut_g, lut_b may be identical (e.g. Master curve) or distinct.
 */
void rubraview_lut_apply(rubraview_pixbuf_t *pb,
                  const uint8_t lut_r[256],
                  const uint8_t lut_g[256],
                  const uint8_t lut_b[256]);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_COLOR_H */
