/* The resampler as it was before D-37 (per-column weights, a 2-D sum per
   pixel): kept as the reference tools/resample_compare.c measures the
   separable one against. Not part of the viewer. */
#include "rubraview/resample.h"
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline int32_t clamp_coord(int32_t c, int32_t max_val) {
    if (c < 0) return 0;
    if (c >= max_val) return max_val - 1;
    return c;
}

static inline float bicubic_weight(float x) {
    x = fabsf(x);
    if (x <= 1.0f) {
        return (1.5f * x - 2.5f) * x * x + 1.0f;
    } else if (x < 2.0f) {
        return ((-0.5f * x + 2.5f) * x - 4.0f) * x + 2.0f;
    }
    return 0.0f;
}

static inline float sinc(float x) {
    if (fabsf(x) < 1e-5f) return 1.0f;
    x *= (float)M_PI;
    return sinf(x) / x;
}

static inline float lanczos3_weight(float x) {
    x = fabsf(x);
    if (x < 1e-5f) return 1.0f;
    if (x < 3.0f) {
        return sinc(x) * sinc(x / 3.0f);
    }
    return 0.0f;
}

static void resample_nearest(const rubraview_pixbuf_t *src, rubraview_pixbuf_t *dst, int32_t y0, int32_t y1) {
    int32_t bpp = rubraview_bytes_per_pixel(src->format);
    float scale_x = (float)src->width / (float)dst->width;
    float scale_y = (float)src->height / (float)dst->height;

    for (int32_t dy = y0; dy < y1; ++dy) {
        int32_t sy = clamp_coord((int32_t)((dy + 0.5f) * scale_y), src->height);
        const uint8_t *src_row = src->pixels + ((ptrdiff_t)sy * src->stride);
        uint8_t *dst_row = dst->pixels + ((ptrdiff_t)dy * dst->stride);

        for (int32_t dx = 0; dx < dst->width; ++dx) {
            int32_t sx = clamp_coord((int32_t)((dx + 0.5f) * scale_x), src->width);
            const uint8_t *spx = src_row + (sx * bpp);
            uint8_t *dpx = dst_row + (dx * bpp);
            memcpy(dpx, spx, (size_t)bpp);
        }
    }
}

static void resample_bilinear(const rubraview_pixbuf_t *src, rubraview_pixbuf_t *dst, int32_t y0, int32_t y1) {
    int32_t bpp = rubraview_bytes_per_pixel(src->format);
    float scale_x = (float)src->width / (float)dst->width;
    float scale_y = (float)src->height / (float)dst->height;

    for (int32_t dy = y0; dy < y1; ++dy) {
        float sy = (dy + 0.5f) * scale_y - 0.5f;
        int32_t y0 = (int32_t)floorf(sy);
        int32_t y1 = y0 + 1;
        float fy = sy - (float)y0;
        float ify = 1.0f - fy;

        int32_t cy0 = clamp_coord(y0, src->height);
        int32_t cy1 = clamp_coord(y1, src->height);

        const uint8_t *row0 = src->pixels + ((ptrdiff_t)cy0 * src->stride);
        const uint8_t *row1 = src->pixels + ((ptrdiff_t)cy1 * src->stride);
        uint8_t *dst_row = dst->pixels + ((ptrdiff_t)dy * dst->stride);

        for (int32_t dx = 0; dx < dst->width; ++dx) {
            float sx = (dx + 0.5f) * scale_x - 0.5f;
            int32_t x0 = (int32_t)floorf(sx);
            int32_t x1 = x0 + 1;
            float fx = sx - (float)x0;
            float ifx = 1.0f - fx;

            int32_t cx0 = clamp_coord(x0, src->width);
            int32_t cx1 = clamp_coord(x1, src->width);

            float w00 = ifx * ify;
            float w10 = fx  * ify;
            float w01 = ifx * fy;
            float w11 = fx  * fy;

            const uint8_t *p00 = row0 + (cx0 * bpp);
            const uint8_t *p10 = row0 + (cx1 * bpp);
            const uint8_t *p01 = row1 + (cx0 * bpp);
            const uint8_t *p11 = row1 + (cx1 * bpp);

            uint8_t *dpx = dst_row + (dx * bpp);
            for (int c = 0; c < bpp; ++c) {
                float val = p00[c] * w00 + p10[c] * w10 + p01[c] * w01 + p11[c] * w11;
                int ival = (int)(val + 0.5f);
                dpx[c] = (uint8_t)(ival < 0 ? 0 : (ival > 255 ? 255 : ival));
            }
        }
    }
}

/* Bicubic and Lanczos-3 are one loop with a different kernel. The
   horizontal weights depend only on the column, so they are worked out
   once per column for a block of RESAMPLE_BLOCK columns and reused down
   every row of the band — before, each was recomputed for every pixel of
   every row (Lanczos: 24 sinf a pixel). Every pixel still sees the same
   weights added in the same order, so the output is byte-identical to the
   per-pixel version; the block lives on the stack because this runs on
   worker threads that own no arena (RV-067). */
#define RESAMPLE_BLOCK 128
#define RESAMPLE_MAX_TAPS 6

static inline void kernel_weights(float s, int32_t base, int taps, int first, float (*weight)(float), float *w) {
    float sum = 0.0f;
    for (int i = 0; i < taps; ++i) {
        w[i] = weight(s - (float)(base + first + i));
        sum += w[i];
    }
    if (fabsf(sum) > 1e-6f) {
        for (int i = 0; i < taps; ++i) w[i] /= sum;
    }
}

static inline void resample_kernel(const rubraview_pixbuf_t *src, rubraview_pixbuf_t *dst, int32_t y0, int32_t y1,
                                   int taps, int first, float (*weight)(float)) {
    int32_t bpp = rubraview_bytes_per_pixel(src->format);
    float scale_x = (float)src->width / (float)dst->width;
    float scale_y = (float)src->height / (float)dst->height;

    int32_t x_off[RESAMPLE_BLOCK][RESAMPLE_MAX_TAPS];   /* byte offset of each tap's pixel in a row */
    float wx[RESAMPLE_BLOCK][RESAMPLE_MAX_TAPS];

    for (int32_t bx = 0; bx < dst->width; bx += RESAMPLE_BLOCK) {
        int32_t block = dst->width - bx < RESAMPLE_BLOCK ? dst->width - bx : RESAMPLE_BLOCK;
        for (int32_t k = 0; k < block; ++k) {
            float sx = (float)(bx + k + 0.5f) * scale_x - 0.5f;
            int32_t x_base = (int32_t)floorf(sx);
            kernel_weights(sx, x_base, taps, first, weight, wx[k]);
            for (int j = 0; j < taps; ++j) x_off[k][j] = clamp_coord(x_base + first + j, src->width) * bpp;
        }

        for (int32_t dy = y0; dy < y1; ++dy) {
            float sy = (dy + 0.5f) * scale_y - 0.5f;
            int32_t y_base = (int32_t)floorf(sy);
            float wy[RESAMPLE_MAX_TAPS];
            kernel_weights(sy, y_base, taps, first, weight, wy);

            const uint8_t *srows[RESAMPLE_MAX_TAPS];
            for (int i = 0; i < taps; ++i) {
                int32_t cy = clamp_coord(y_base + first + i, src->height);
                srows[i] = src->pixels + ((ptrdiff_t)cy * src->stride);
            }

            uint8_t *dst_row = dst->pixels + ((ptrdiff_t)dy * dst->stride);
            for (int32_t k = 0; k < block; ++k) {
                float accum[8] = {0};
                for (int i = 0; i < taps; ++i) {
                    float row_weight = wy[i];
                    for (int j = 0; j < taps; ++j) {
                        const uint8_t *spx = srows[i] + x_off[k][j];
                        float w = row_weight * wx[k][j];
                        for (int c = 0; c < bpp; ++c) {
                            accum[c] += spx[c] * w;
                        }
                    }
                }

                uint8_t *dpx = dst_row + ((bx + k) * bpp);
                for (int c = 0; c < bpp; ++c) {
                    int ival = (int)(accum[c] + 0.5f);
                    dpx[c] = (uint8_t)(ival < 0 ? 0 : (ival > 255 ? 255 : ival));
                }
            }
        }
    }
}

static void resample_bicubic(const rubraview_pixbuf_t *src, rubraview_pixbuf_t *dst, int32_t y0, int32_t y1) {
    resample_kernel(src, dst, y0, y1, 4, -1, bicubic_weight);
}

static void resample_lanczos3(const rubraview_pixbuf_t *src, rubraview_pixbuf_t *dst, int32_t y0, int32_t y1) {
    resample_kernel(src, dst, y0, y1, 6, -2, lanczos3_weight);
}

/* One horizontal band of the destination. The scale factors still come
   from the *whole* destination, so a band computed on its own is
   byte-identical to the same rows computed in one pass — which is what
   lets RV-067 hand bands to worker threads (§6.5.5). */
void rubraview_resample_band_reference(const rubraview_pixbuf_t *src, rubraview_pixbuf_t *dst,
                             rubraview_resample_filter_t filter, int32_t y0, int32_t y1) {
    if (!rubraview_pixbuf_is_valid(src) || !rubraview_pixbuf_is_valid(dst)) return;
    if (y0 < 0) y0 = 0;
    if (y1 > dst->height) y1 = dst->height;
    if (y0 >= y1) return;

    switch (filter) {
        case RUBRAVIEW_FILTER_NEAREST:  resample_nearest(src, dst, y0, y1); break;
        case RUBRAVIEW_FILTER_BILINEAR: resample_bilinear(src, dst, y0, y1); break;
        case RUBRAVIEW_FILTER_BICUBIC:  resample_bicubic(src, dst, y0, y1); break;
        case RUBRAVIEW_FILTER_LANCZOS3: resample_lanczos3(src, dst, y0, y1); break;
        default:                        resample_bilinear(src, dst, y0, y1); break;
    }
}


