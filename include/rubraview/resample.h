#ifndef RUBRAVIEW_RESAMPLE_H
#define RUBRAVIEW_RESAMPLE_H

#include "rubraview/core.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum rubraview_resample_filter {
    RUBRAVIEW_FILTER_NEAREST = 0,   /* Fast nearest neighbor (pixel-art integer scale) */
    RUBRAVIEW_FILTER_BILINEAR,      /* 2x2 area-weighted bilinear interpolation */
    RUBRAVIEW_FILTER_BICUBIC,       /* Catmull-Rom cubic spline interpolation */
    RUBRAVIEW_FILTER_LANCZOS3,      /* High-fidelity 3-lobe windowed sinc filter */
} rubraview_resample_filter_t;

/**
 * Resample a source pixel buffer to arbitrary target dimensions (dst_width x dst_height)
 * using the requested interpolation filter.
 *
 * Allocates the destination pixbuf within the provided arena.
 * Clamps coordinates at boundaries to prevent edge artifacts.
 */
rubraview_pixbuf_t rubraview_pixbuf_resample(proven_arena_t *arena,
                               const rubraview_pixbuf_t *src,
                               int32_t dst_width,
                               int32_t dst_height,
                               rubraview_resample_filter_t filter);

/**
 * Resample only the destination rows `[y0, y1)`, into a destination the
 * caller already allocated. The scale is taken from the destination's
 * full size, so bands computed separately join up exactly as if they had
 * been computed together. This is what RV-067's parallel resize is built
 * on (§6.5.5); ordinary callers want rubraview_pixbuf_resample.
 */
void rubraview_resample_band(const rubraview_pixbuf_t *src, rubraview_pixbuf_t *dst,
                             rubraview_resample_filter_t filter, int32_t y0, int32_t y1);

/* ---- D-38: resizing on the graphics card ----
 *
 * The card runs the same two passes as bicubic and Lanczos-3 do here, with
 * the same weights: this computes them, so the card only multiplies and
 * adds. For each of the `dst_len` positions along one axis, `taps` source
 * indices (already clamped to the edge) and `taps` weights (normalised the
 * way the CPU normalises them) — `dst_len * taps` entries in each array.
 */
int32_t rubraview_resample_taps(rubraview_resample_filter_t filter);   /* 4, 6, or 0 */
bool rubraview_resample_axis_weights(rubraview_resample_filter_t filter, int32_t src_len, int32_t dst_len,
                                     int32_t *out_index, float *out_weight);

/*
 * An accelerator the platform may install: it resizes `src` into the
 * already-allocated `dst` and returns true, or returns false and the CPU
 * does it. rubraview_pixbuf_resample and rubraview_pixbuf_resample_mt ask it
 * first when rubraview_resample_accel_wanted says the job is worth it. It is
 * called from whichever thread resizes, so it must lock what it shares.
 */
typedef bool (*rubraview_resample_accel_fn)(void *context, const rubraview_pixbuf_t *src,
                                            rubraview_pixbuf_t *dst, rubraview_resample_filter_t filter);
void rubraview_resample_set_accel(rubraview_resample_accel_fn fn, void *context, uint64_t min_pixels);

/* Bicubic or Lanczos-3, four bytes a pixel, and the larger of the two
   images at least `min_pixels`: below that, sending the picture to the card
   and back costs more than the CPU takes. */
bool rubraview_resample_accel_wanted(rubraview_resample_filter_t filter, rubraview_pixel_format_t format,
                                     int32_t src_w, int32_t src_h, int32_t dst_w, int32_t dst_h,
                                     uint64_t min_pixels);

/* Tries the installed accelerator; false when there is none, it is not
   wanted, or it failed (the caller then resizes on the CPU). */
bool rubraview_resample_try_accel(const rubraview_pixbuf_t *src, rubraview_pixbuf_t *dst,
                                  rubraview_resample_filter_t filter);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_RESAMPLE_H */
