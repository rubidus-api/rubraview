#ifndef RUBRAVIEW_FILTER_H
#define RUBRAVIEW_FILTER_H

#include "rubraview/core.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Apply separable Gaussian blur with specified standard deviation (sigma).
 * Kernel radius is dynamically computed as ceil(3.0 * sigma).
 * Allocates destination buffer in arena.
 */
rv_pixbuf_t rv_filter_gaussian_blur(proven_arena_t *arena, const rv_pixbuf_t *src, float sigma);

/**
 * Apply fast box blur with given pixel radius using separable passes.
 */
rv_pixbuf_t rv_filter_box_blur(proven_arena_t *arena, const rv_pixbuf_t *src, int32_t radius);

/**
 * Apply threshold-gated unsharp mask sharpening:
 * dst = src + amount * (src - blur) when |src - blur| >= threshold.
 *
 * sigma: Gaussian blur radius parameter (e.g. 1.0f to 3.0f).
 * amount: sharpening intensity multiplier (e.g. 0.5f to 2.5f).
 * threshold: minimum difference (0..255) to sharpen (prevents amplifying noise).
 */
rv_pixbuf_t rv_filter_unsharp_mask(proven_arena_t *arena,
                                   const rv_pixbuf_t *src,
                                   float sigma,
                                   float amount,
                                   uint8_t threshold);

/**
 * Automatically detect and crop uniform boundary margins (white scan margins or black letterboxes).
 * detect_white: true for scanned document/manga borders (Y >= threshold),
 *               false for letterboxed black borders (Y <= threshold).
 */
rv_pixbuf_t rv_filter_autotrim(proven_arena_t *arena,
                               const rv_pixbuf_t *src,
                               uint8_t bg_threshold,
                               bool detect_white);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_FILTER_H */
