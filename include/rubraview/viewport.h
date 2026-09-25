#ifndef RUBRAVIEW_VIEWPORT_H
#define RUBRAVIEW_VIEWPORT_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Viewport fit-mode and affine-matrix math (RFC-0001 §3.4, §4.1.2). Shared
 * by the still-image canvas (M2) and video playback (§5.6): given image
 * and window dimensions, compute the scale and centering offset for each
 * of the six deterministic fit modes, and build/compose the 3x2 affine
 * transform used for interactive pan/zoom.
 */

typedef enum rubraview_fit_mode {
    RUBRAVIEW_FIT_WINDOW = 0, /* Scale to fit entirely inside the window (letterbox/pillarbox); default */
    RUBRAVIEW_FIT_WIDTH,      /* Match image width to window width; height may exceed (scrollable) */
    RUBRAVIEW_FIT_HEIGHT,     /* Match image height to window height; width may exceed (scrollable) */
    RUBRAVIEW_FIT_STRETCH,    /* Fill the window exactly, ignoring aspect ratio */
    RUBRAVIEW_FIT_ACTUAL_SIZE,/* 1:1 pixel mapping */
    RUBRAVIEW_FIT_SMART,      /* Downscale to fit only if larger than the window; else 1:1 */
} rubraview_fit_mode_t;

typedef struct rubraview_viewport_transform {
    double scale_x, scale_y; /* equal for every mode except STRETCH */
    double offset_x, offset_y; /* top-left of the scaled image within the window (centered; may be negative when the scaled image exceeds the window) */
} rubraview_viewport_transform_t;

/**
 * Compute the fit transform. Degenerate input (non-positive image or
 * window dimensions) returns an identity transform (scale 1, offset 0).
 */
rubraview_viewport_transform_t rubraview_fit_compute(rubraview_fit_mode_t mode, double img_w, double img_h, double win_w, double win_h);

/**
 * A 3x2 affine transform: x' = a*x + c*y + e, y' = b*x + d*y + f.
 */
typedef struct rubraview_mat3x2 {
    double a, b, c, d, e, f;
} rubraview_mat3x2_t;

rubraview_mat3x2_t rubraview_mat3x2_identity(void);
rubraview_mat3x2_t rubraview_mat3x2_translate(double dx, double dy);
rubraview_mat3x2_t rubraview_mat3x2_scale(double sx, double sy);

/**
 * Compose two transforms: applying the result to a point is equivalent to
 * applying `first`, then applying `second` to that result (i.e. this is
 * "second . first" in function-composition notation).
 */
rubraview_mat3x2_t rubraview_mat3x2_multiply(rubraview_mat3x2_t first, rubraview_mat3x2_t second);

void rubraview_mat3x2_apply(rubraview_mat3x2_t m, double x, double y, double *out_x, double *out_y);

/**
 * The interactive pan/zoom viewport matrix from §4.1.2:
 *   M = T(dx,dy) . S(scale,scale) . T(-cx,-cy)
 * i.e. translate the pivot point (cx,cy) to the origin, scale, then
 * translate to the pan target (dx,dy). The pivot point always maps to
 * exactly (dx,dy) regardless of scale — this is "zoom centered on
 * (cx,cy), panned so that point lands at (dx,dy)".
 */
rubraview_mat3x2_t rubraview_viewport_matrix(double cx, double cy, double scale, double dx, double dy);

/**
 * The size a picture of `width` x `height` may be held at: no side over
 * `max_side` (the graphics device's largest bitmap) and no more than
 * `max_pixels` in all, the aspect kept, never enlarged and never below
 * one pixel a side. Returns true when that is smaller than the picture —
 * the page is then shown reduced (the very large picture's fall-back).
 */
bool rubraview_fit_within_limits(int32_t width, int32_t height, int32_t max_side, uint64_t max_pixels,
                                 int32_t *out_width, int32_t *out_height);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_VIEWPORT_H */
