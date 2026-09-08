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

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_RESAMPLE_H */
