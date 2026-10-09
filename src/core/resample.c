#include "rubraview/resample.h"
#include <string.h>
#include <stdlib.h>
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

/* Bicubic and Lanczos-3 are one loop with a different kernel, done in two
   passes (D-37). For a block of RESAMPLE_BLOCK destination columns the
   horizontal weights and source offsets are worked out once; each source
   row the block needs is filtered horizontally once, into a small cache of
   float rows, and each destination pixel is then the vertical sum of the
   cached rows — taps + taps multiplications a pixel instead of taps x taps
   (12 against 36 for Lanczos-3). The intermediate stays in float, so there
   is one rounding, at the end. Consecutive destination rows share most of
   their source rows, which the cache keeps. Everything lives on the stack:
   this runs on worker threads that own no arena (RV-067). A band computes
   the same values whatever row it starts at, so bands and one pass agree
   byte for byte (T042). */
#define RESAMPLE_BLOCK 128
#define RESAMPLE_MAX_TAPS 6
#define RESAMPLE_MAX_BPP 8
#define RESAMPLE_CACHE_ROWS (RESAMPLE_MAX_TAPS * 2)

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

typedef struct resample_row_cache {
    int32_t row[RESAMPLE_CACHE_ROWS];      /* the source row each slot holds, -1 for none */
    uint32_t used[RESAMPLE_CACHE_ROWS];    /* when it was last wanted, for eviction */
    float values[RESAMPLE_CACHE_ROWS][RESAMPLE_BLOCK * RESAMPLE_MAX_BPP];
} resample_row_cache_t;

static inline void resample_kernel(const rubraview_pixbuf_t *src, rubraview_pixbuf_t *dst, int32_t y0, int32_t y1,
                                   int taps, int first, float (*weight)(float)) {
    int32_t bpp = rubraview_bytes_per_pixel(src->format);
    if (bpp <= 0 || bpp > RESAMPLE_MAX_BPP) return;
    float scale_x = (float)src->width / (float)dst->width;
    float scale_y = (float)src->height / (float)dst->height;

    int32_t x_off[RESAMPLE_BLOCK][RESAMPLE_MAX_TAPS];   /* byte offset of each tap's pixel in a row */
    float wx[RESAMPLE_BLOCK][RESAMPLE_MAX_TAPS];
    resample_row_cache_t cache;

    for (int32_t bx = 0; bx < dst->width; bx += RESAMPLE_BLOCK) {
        int32_t block = dst->width - bx < RESAMPLE_BLOCK ? dst->width - bx : RESAMPLE_BLOCK;
        for (int32_t k = 0; k < block; ++k) {
            float sx = (float)(bx + k + 0.5f) * scale_x - 0.5f;
            int32_t x_base = (int32_t)floorf(sx);
            kernel_weights(sx, x_base, taps, first, weight, wx[k]);
            for (int j = 0; j < taps; ++j) x_off[k][j] = clamp_coord(x_base + first + j, src->width) * bpp;
        }
        for (int i = 0; i < RESAMPLE_CACHE_ROWS; ++i) { cache.row[i] = -1; cache.used[i] = 0; }
        uint32_t clock = 0;

        for (int32_t dy = y0; dy < y1; ++dy) {
            float sy = (dy + 0.5f) * scale_y - 0.5f;
            int32_t y_base = (int32_t)floorf(sy);
            float wy[RESAMPLE_MAX_TAPS];
            kernel_weights(sy, y_base, taps, first, weight, wy);

            /* The rows this destination row needs, filtered horizontally. */
            const float *rows[RESAMPLE_MAX_TAPS];
            ++clock;
            for (int i = 0; i < taps; ++i) {
                int32_t cy = clamp_coord(y_base + first + i, src->height);
                int slot = -1;
                for (int c = 0; c < RESAMPLE_CACHE_ROWS; ++c) {
                    if (cache.row[c] == cy) { slot = c; break; }
                }
                if (slot < 0) {
                    /* the slot wanted longest ago, never one this row already took */
                    for (int c = 0; c < RESAMPLE_CACHE_ROWS; ++c) {
                        if (cache.used[c] == clock) continue;
                        if (slot < 0 || cache.row[c] < 0 || cache.used[c] < cache.used[slot]) slot = c;
                        if (cache.row[c] < 0) break;
                    }
                    const uint8_t *srow = src->pixels + ((ptrdiff_t)cy * src->stride);
                    float *out = cache.values[slot];
                    for (int32_t k = 0; k < block; ++k) {
                        float acc[RESAMPLE_MAX_BPP] = {0};
                        for (int j = 0; j < taps; ++j) {
                            const uint8_t *spx = srow + x_off[k][j];
                            float w = wx[k][j];
                            for (int c = 0; c < bpp; ++c) acc[c] += spx[c] * w;
                        }
                        for (int c = 0; c < bpp; ++c) out[k * bpp + c] = acc[c];
                    }
                    cache.row[slot] = cy;
                }
                cache.used[slot] = clock;
                rows[i] = cache.values[slot];
            }

            uint8_t *dst_row = dst->pixels + ((ptrdiff_t)dy * dst->stride);
            for (int32_t k = 0; k < block; ++k) {
                uint8_t *dpx = dst_row + ((bx + k) * bpp);
                for (int c = 0; c < bpp; ++c) {
                    float v = 0.0f;
                    for (int i = 0; i < taps; ++i) v += rows[i][k * bpp + c] * wy[i];
                    int ival = (int)(v + 0.5f);
                    dpx[c] = (uint8_t)(ival < 0 ? 0 : (ival > 255 ? 255 : ival));
                }
            }
        }
    }
}

/* The same kernel as one 2-D sum a pixel (the resampler before D-37). It
   wins when shrinking a lot: then each destination row needs several new
   source rows, the cache of filtered rows is seldom reused, and the two
   passes cost more than taps x taps. */
static inline void resample_kernel_direct(const rubraview_pixbuf_t *src, rubraview_pixbuf_t *dst, int32_t y0, int32_t y1,
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

/* Two passes when a filtered source row is reused enough to pay for
   itself — the image shrinks vertically by less than taps - 1 — and the 2-D
   sum otherwise (measured: tools/resample_compare.c). The choice depends
   only on the sizes, so bands still agree with one pass. */
static inline void resample_choose(const rubraview_pixbuf_t *src, rubraview_pixbuf_t *dst, int32_t y0, int32_t y1,
                                   int taps, int first, float (*weight)(float)) {
    double shrink = (double)src->height / (double)dst->height;
    if (shrink >= (double)(taps - 1)) resample_kernel_direct(src, dst, y0, y1, taps, first, weight);
    else resample_kernel(src, dst, y0, y1, taps, first, weight);
}

static void resample_bicubic(const rubraview_pixbuf_t *src, rubraview_pixbuf_t *dst, int32_t y0, int32_t y1) {
    resample_choose(src, dst, y0, y1, 4, -1, bicubic_weight);
}

static void resample_lanczos3(const rubraview_pixbuf_t *src, rubraview_pixbuf_t *dst, int32_t y0, int32_t y1) {
    resample_choose(src, dst, y0, y1, 6, -2, lanczos3_weight);
}

/* One horizontal band of the destination. The scale factors still come
   from the *whole* destination, so a band computed on its own is
   byte-identical to the same rows computed in one pass — which is what
   lets RV-067 hand bands to worker threads (§6.5.5). */
void rubraview_resample_band(const rubraview_pixbuf_t *src, rubraview_pixbuf_t *dst,
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

rubraview_pixbuf_t rubraview_pixbuf_resample(proven_arena_t *arena,
                               const rubraview_pixbuf_t *src,
                               int32_t dst_width,
                               int32_t dst_height,
                               rubraview_resample_filter_t filter) {
    if (!rubraview_pixbuf_is_valid(src) || arena == NULL || dst_width <= 0 || dst_height <= 0) {
        return (rubraview_pixbuf_t){0};
    }

    /* Fast path: identical dimensions */
    if (src->width == dst_width && src->height == dst_height) {
        return rubraview_pixbuf_clone(arena, src);
    }

    rubraview_pixbuf_t dst = rubraview_pixbuf_create(arena, dst_width, dst_height, src->format);
    if (!rubraview_pixbuf_is_valid(&dst)) {
        return (rubraview_pixbuf_t){0};
    }

    if (rubraview_resample_try_accel(src, &dst, filter)) return dst;   /* D-38: the card, when it is worth it */
    rubraview_resample_band(src, &dst, filter, 0, dst_height);

    return dst;
}

/* ---- D-38 ---- */

int32_t rubraview_resample_taps(rubraview_resample_filter_t filter) {
    return filter == RUBRAVIEW_FILTER_BICUBIC ? 4 : filter == RUBRAVIEW_FILTER_LANCZOS3 ? 6 : 0;
}

bool rubraview_resample_axis_weights(rubraview_resample_filter_t filter, int32_t src_len, int32_t dst_len,
                                     int32_t *out_index, float *out_weight) {
    int32_t taps = rubraview_resample_taps(filter);
    if (taps == 0 || src_len <= 0 || dst_len <= 0 || !out_index || !out_weight) return false;
    int first = taps == 4 ? -1 : -2;
    float (*weight)(float) = taps == 4 ? bicubic_weight : lanczos3_weight;
    /* Exactly the expressions resample_kernel uses, so the weights are the same floats. */
    float scale = (float)src_len / (float)dst_len;
    for (int32_t k = 0; k < dst_len; ++k) {
        float s = (float)(k + 0.5f) * scale - 0.5f;
        int32_t base = (int32_t)floorf(s);
        kernel_weights(s, base, taps, first, weight, out_weight + (size_t)k * (size_t)taps);
        for (int j = 0; j < taps; ++j) out_index[(size_t)k * (size_t)taps + (size_t)j] = clamp_coord(base + first + j, src_len);
    }
    return true;
}

static rubraview_resample_accel_fn g_accel;
static void *g_accel_context;
static uint64_t g_accel_min_pixels;

void rubraview_resample_set_accel(rubraview_resample_accel_fn fn, void *context, uint64_t min_pixels) {
    g_accel = fn;
    g_accel_context = context;
    g_accel_min_pixels = min_pixels;
}

bool rubraview_resample_accel_wanted(rubraview_resample_filter_t filter, rubraview_pixel_format_t format,
                                     int32_t src_w, int32_t src_h, int32_t dst_w, int32_t dst_h,
                                     uint64_t min_pixels) {
    if (rubraview_resample_taps(filter) == 0) return false;
    if (rubraview_bytes_per_pixel(format) != 4) return false;
    if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) return false;
    uint64_t a = (uint64_t)src_w * (uint64_t)src_h, b = (uint64_t)dst_w * (uint64_t)dst_h;
    return (a > b ? a : b) >= min_pixels;
}

bool rubraview_resample_try_accel(const rubraview_pixbuf_t *src, rubraview_pixbuf_t *dst,
                                  rubraview_resample_filter_t filter) {
    rubraview_resample_accel_fn fn = g_accel;
    if (!fn || !src || !dst) return false;
    if (!rubraview_resample_accel_wanted(filter, src->format, src->width, src->height, dst->width, dst->height,
                                         g_accel_min_pixels)) {
        return false;
    }
    return fn(g_accel_context, src, dst, filter);
}

/* ---- D-81: enlarging for the screen ---- */

bool rubraview_enlarge_region(const uint8_t *src, int32_t sw, int32_t sh, size_t src_stride, int32_t factor,
                              int32_t out_x, int32_t out_y, int32_t out_w, int32_t out_h,
                              uint8_t *dst, size_t dst_stride) {
    if (!src || !dst || sw <= 0 || sh <= 0 || (factor != 2 && factor != 4 && factor != 8)) return false;
    if (sw > 8192 || sh > 8192) return false;
    int32_t dw = sw * factor, dh = sh * factor;
    if (out_x < 0 || out_y < 0 || out_w <= 0 || out_h <= 0 || out_x + out_w > dw || out_y + out_h > dh) return false;

    /* The blur's reach, in output pixels: only that much around the part
       asked for has to be made. */
    float sigma = 0.3f * (float)factor;
    int32_t reach = (int32_t)ceilf(3.0f * sigma);
    int32_t x0 = out_x - reach - 1, y0 = out_y - reach - 1;
    int32_t x1 = out_x + out_w + reach + 1, y1 = out_y + out_h + reach + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > dw) x1 = dw;
    if (y1 > dh) y1 = dh;
    int32_t ww = x1 - x0, wh = y1 - y0;   /* the working window */

    const int taps = 6;
    int32_t *xi = (int32_t*)malloc((size_t)dw * taps * sizeof(int32_t));
    float *xw = (float*)malloc((size_t)dw * taps * sizeof(float));
    int32_t *yi = (int32_t*)malloc((size_t)dh * taps * sizeof(int32_t));
    float *yw = (float*)malloc((size_t)dh * taps * sizeof(float));
    float *rows = (float*)malloc((size_t)sh * (size_t)ww * 4u * sizeof(float));   /* source rows, enlarged across */
    float *up = (float*)malloc((size_t)wh * (size_t)ww * 4u * sizeof(float));     /* enlarged, echoes removed */
    float *blur = (float*)malloc((size_t)wh * (size_t)ww * 4u * sizeof(float));
    float *tmp = (float*)malloc((size_t)wh * (size_t)ww * 4u * sizeof(float));
    float kernel[64];
    bool ok = xi && xw && yi && yw && rows && up && blur && tmp && reach < 32 &&
              rubraview_resample_axis_weights(RUBRAVIEW_FILTER_LANCZOS3, sw, dw, xi, xw) &&
              rubraview_resample_axis_weights(RUBRAVIEW_FILTER_LANCZOS3, sh, dh, yi, yw);
    if (ok) {
        /* 1. Lanczos-3: across, for the source rows the window's rows need... */
        int32_t sy_lo = yi[(size_t)y0 * taps], sy_hi = yi[(size_t)(y1 - 1) * taps + (taps - 1)];
        for (int32_t sy = sy_lo; sy <= sy_hi; ++sy) {
            const uint8_t *row = src + (size_t)sy * src_stride;
            float *out = rows + (size_t)sy * (size_t)ww * 4u;
            for (int32_t x = 0; x < ww; ++x) {
                const int32_t *index = xi + (size_t)(x0 + x) * taps;
                const float *weight = xw + (size_t)(x0 + x) * taps;
                float acc[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                for (int j = 0; j < taps; ++j) {
                    const uint8_t *p = row + (size_t)index[j] * 4u;
                    for (int c = 0; c < 4; ++c) acc[c] += weight[j] * (float)p[c];
                }
                memcpy(out + (size_t)x * 4u, acc, sizeof(acc));
            }
        }
        /* ...then down, and 2. within the four picture pixels around. */
        for (int32_t y = 0; y < wh; ++y) {
            const int32_t *index = yi + (size_t)(y0 + y) * taps;
            const float *weight = yw + (size_t)(y0 + y) * taps;
            float fy = ((float)(y0 + y) + 0.5f) / (float)factor - 0.5f;
            int32_t ya = clamp_coord((int32_t)floorf(fy), sh), yb = clamp_coord((int32_t)floorf(fy) + 1, sh);
            float *out = up + (size_t)y * (size_t)ww * 4u;
            for (int32_t x = 0; x < ww; ++x) {
                float fx = ((float)(x0 + x) + 0.5f) / (float)factor - 0.5f;
                int32_t xa = clamp_coord((int32_t)floorf(fx), sw), xb = clamp_coord((int32_t)floorf(fx) + 1, sw);
                const uint8_t *q[4] = {
                    src + (size_t)ya * src_stride + (size_t)xa * 4u, src + (size_t)ya * src_stride + (size_t)xb * 4u,
                    src + (size_t)yb * src_stride + (size_t)xa * 4u, src + (size_t)yb * src_stride + (size_t)xb * 4u,
                };
                for (int c = 0; c < 4; ++c) {
                    float acc = 0.0f;
                    for (int j = 0; j < taps; ++j) acc += weight[j] * rows[((size_t)index[j] * (size_t)ww + (size_t)x) * 4u + (size_t)c];
                    uint8_t lo = q[0][c], hi = q[0][c];
                    for (int k = 1; k < 4; ++k) {
                        if (q[k][c] < lo) lo = q[k][c];
                        if (q[k][c] > hi) hi = q[k][c];
                    }
                    if (acc < (float)lo) acc = (float)lo;
                    if (acc > (float)hi) acc = (float)hi;
                    out[(size_t)x * 4u + (size_t)c] = acc;
                }
            }
        }
        /* 3. The unsharp mask: a Gaussian across, then down... */
        float sum = 0.0f;
        for (int32_t i = -reach; i <= reach; ++i) {
            kernel[i + reach] = expf(-(float)(i * i) / (2.0f * sigma * sigma));
            sum += kernel[i + reach];
        }
        for (int32_t i = 0; i <= 2 * reach; ++i) kernel[i] /= sum;
        for (int32_t y = 0; y < wh; ++y) {
            for (int32_t x = 0; x < ww; ++x) {
                float acc[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                for (int32_t i = -reach; i <= reach; ++i) {
                    const float *p = up + ((size_t)y * (size_t)ww + (size_t)clamp_coord(x + i, ww)) * 4u;
                    for (int c = 0; c < 4; ++c) acc[c] += kernel[i + reach] * p[c];
                }
                memcpy(tmp + ((size_t)y * (size_t)ww + (size_t)x) * 4u, acc, sizeof(acc));
            }
        }
        for (int32_t y = 0; y < wh; ++y) {
            for (int32_t x = 0; x < ww; ++x) {
                float acc[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                for (int32_t i = -reach; i <= reach; ++i) {
                    const float *p = tmp + ((size_t)clamp_coord(y + i, wh) * (size_t)ww + (size_t)x) * 4u;
                    for (int c = 0; c < 4; ++c) acc[c] += kernel[i + reach] * p[c];
                }
                memcpy(blur + ((size_t)y * (size_t)ww + (size_t)x) * 4u, acc, sizeof(acc));
            }
        }
        /* ...and the difference added back, within the neighbours' range. */
        for (int32_t y = 0; y < out_h; ++y) {
            int32_t wy = out_y + y - y0;
            uint8_t *out = dst + (size_t)y * dst_stride;
            for (int32_t x = 0; x < out_w; ++x) {
                int32_t wx = out_x + x - x0;
                for (int c = 0; c < 4; ++c) {
                    float lo = 255.0f, hi = 0.0f;
                    for (int32_t ny = wy - 1; ny <= wy + 1; ++ny) {
                        for (int32_t nx = wx - 1; nx <= wx + 1; ++nx) {
                            float v = up[((size_t)clamp_coord(ny, wh) * (size_t)ww + (size_t)clamp_coord(nx, ww)) * 4u + (size_t)c];
                            if (v < lo) lo = v;
                            if (v > hi) hi = v;
                        }
                    }
                    size_t at = ((size_t)wy * (size_t)ww + (size_t)wx) * 4u + (size_t)c;
                    float v = up[at] + (up[at] - blur[at]);
                    if (v < lo) v = lo;
                    if (v > hi) v = hi;
                    out[(size_t)x * 4u + (size_t)c] = (uint8_t)(v + 0.5f);
                }
            }
        }
    }
    free(xi); free(xw); free(yi); free(yw); free(rows); free(up); free(blur); free(tmp);
    return ok;
}
