#include "rubraview/filter.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define MAX_KERNEL_RADIUS 48

static inline int32_t clamp_coord(int32_t c, int32_t max_val) {
    if (c < 0) return 0;
    if (c >= max_val) return max_val - 1;
    return c;
}

static inline uint8_t clamp_u8(int val) {
    if (val < 0) return 0;
    if (val > 255) return 255;
    return (uint8_t)val;
}

static inline uint8_t get_pixel_lum(const uint8_t *px, rv_pixel_format_t fmt) {
    if (fmt == RV_PIXFMT_GRAY8) return px[0];
    bool is_bgra = (fmt == RV_PIXFMT_BGRA8);
    uint32_t r = is_bgra ? px[2] : px[0];
    uint32_t g = px[1];
    uint32_t b = is_bgra ? px[0] : px[2];
    return (uint8_t)((2126 * r + 7152 * g + 722 * b + 5000) / 10000);
}

rv_pixbuf_t rv_filter_gaussian_blur(proven_arena_t *arena, const rv_pixbuf_t *src, float sigma) {
    if (!rv_pixbuf_is_valid(src) || arena == NULL) return (rv_pixbuf_t){0};
    if (sigma <= 0.1f) return rv_pixbuf_clone(arena, src);

    int32_t radius = (int32_t)ceilf(3.0f * sigma);
    if (radius < 1) radius = 1;
    if (radius > MAX_KERNEL_RADIUS) radius = MAX_KERNEL_RADIUS;
    int32_t klen = 2 * radius + 1;

    float kernel[2 * MAX_KERNEL_RADIUS + 1];
    float two_sig_sq = 2.0f * sigma * sigma;
    float sum = 0.0f;
    for (int32_t i = -radius; i <= radius; ++i) {
        float w = expf(-((float)(i * i)) / two_sig_sq);
        kernel[i + radius] = w;
        sum += w;
    }
    for (int32_t i = 0; i < klen; ++i) {
        kernel[i] /= sum;
    }

    int32_t w = src->width;
    int32_t h = src->height;
    int32_t bpp = rv_bytes_per_pixel(src->format);

    rv_pixbuf_t tmp = rv_pixbuf_create(arena, w, h, src->format);
    rv_pixbuf_t dst = rv_pixbuf_create(arena, w, h, src->format);
    if (!rv_pixbuf_is_valid(&tmp) || !rv_pixbuf_is_valid(&dst)) return (rv_pixbuf_t){0};

    /* Pass 1: Horizontal blur from src -> tmp */
    for (int32_t y = 0; y < h; ++y) {
        const uint8_t *srow = src->pixels + ((ptrdiff_t)y * src->stride);
        uint8_t *trow = tmp.pixels + ((ptrdiff_t)y * tmp.stride);

        for (int32_t x = 0; x < w; ++x) {
            float accum[4] = {0};
            for (int32_t i = -radius; i <= radius; ++i) {
                int32_t cx = clamp_coord(x + i, w);
                const uint8_t *spx = srow + (cx * bpp);
                float kw = kernel[i + radius];
                for (int c = 0; c < (bpp == 4 ? 3 : 1); ++c) {
                    accum[c] += (float)spx[c] * kw;
                }
            }

            uint8_t *tpx = trow + (x * bpp);
            for (int c = 0; c < (bpp == 4 ? 3 : 1); ++c) {
                tpx[c] = clamp_u8((int)(accum[c] + 0.5f));
            }
            if (bpp == 4) {
                tpx[3] = srow[x * 4 + 3]; /* Preserve alpha */
            }
        }
    }

    /* Pass 2: Vertical blur from tmp -> dst */
    for (int32_t x = 0; x < w; ++x) {
        for (int32_t y = 0; y < h; ++y) {
            float accum[4] = {0};
            for (int32_t j = -radius; j <= radius; ++j) {
                int32_t cy = clamp_coord(y + j, h);
                const uint8_t *tpx = tmp.pixels + ((ptrdiff_t)cy * tmp.stride) + (x * bpp);
                float kw = kernel[j + radius];
                for (int c = 0; c < (bpp == 4 ? 3 : 1); ++c) {
                    accum[c] += (float)tpx[c] * kw;
                }
            }

            uint8_t *dpx = dst.pixels + ((ptrdiff_t)y * dst.stride) + (x * bpp);
            for (int c = 0; c < (bpp == 4 ? 3 : 1); ++c) {
                dpx[c] = clamp_u8((int)(accum[c] + 0.5f));
            }
            if (bpp == 4) {
                dpx[3] = tmp.pixels[(ptrdiff_t)y * tmp.stride + x * 4 + 3];
            }
        }
    }

    return dst;
}

rv_pixbuf_t rv_filter_box_blur(proven_arena_t *arena, const rv_pixbuf_t *src, int32_t radius) {
    if (!rv_pixbuf_is_valid(src) || arena == NULL) return (rv_pixbuf_t){0};
    if (radius <= 0) return rv_pixbuf_clone(arena, src);
    if (radius > MAX_KERNEL_RADIUS) radius = MAX_KERNEL_RADIUS;

    int32_t klen = 2 * radius + 1;
    float kw = 1.0f / (float)klen;

    int32_t w = src->width;
    int32_t h = src->height;
    int32_t bpp = rv_bytes_per_pixel(src->format);

    rv_pixbuf_t tmp = rv_pixbuf_create(arena, w, h, src->format);
    rv_pixbuf_t dst = rv_pixbuf_create(arena, w, h, src->format);
    if (!rv_pixbuf_is_valid(&tmp) || !rv_pixbuf_is_valid(&dst)) return (rv_pixbuf_t){0};

    /* Horizontal pass */
    for (int32_t y = 0; y < h; ++y) {
        const uint8_t *srow = src->pixels + ((ptrdiff_t)y * src->stride);
        uint8_t *trow = tmp.pixels + ((ptrdiff_t)y * tmp.stride);

        for (int32_t x = 0; x < w; ++x) {
            float accum[4] = {0};
            for (int32_t i = -radius; i <= radius; ++i) {
                int32_t cx = clamp_coord(x + i, w);
                const uint8_t *spx = srow + (cx * bpp);
                for (int c = 0; c < (bpp == 4 ? 3 : 1); ++c) {
                    accum[c] += (float)spx[c];
                }
            }
            uint8_t *tpx = trow + (x * bpp);
            for (int c = 0; c < (bpp == 4 ? 3 : 1); ++c) {
                tpx[c] = clamp_u8((int)(accum[c] * kw + 0.5f));
            }
            if (bpp == 4) tpx[3] = srow[x * 4 + 3];
        }
    }

    /* Vertical pass */
    for (int32_t x = 0; x < w; ++x) {
        for (int32_t y = 0; y < h; ++y) {
            float accum[4] = {0};
            for (int32_t j = -radius; j <= radius; ++j) {
                int32_t cy = clamp_coord(y + j, h);
                const uint8_t *tpx = tmp.pixels + ((ptrdiff_t)cy * tmp.stride) + (x * bpp);
                for (int c = 0; c < (bpp == 4 ? 3 : 1); ++c) {
                    accum[c] += (float)tpx[c];
                }
            }
            uint8_t *dpx = dst.pixels + ((ptrdiff_t)y * dst.stride) + (x * bpp);
            for (int c = 0; c < (bpp == 4 ? 3 : 1); ++c) {
                dpx[c] = clamp_u8((int)(accum[c] * kw + 0.5f));
            }
            if (bpp == 4) dpx[3] = tmp.pixels[(ptrdiff_t)y * tmp.stride + x * 4 + 3];
        }
    }

    return dst;
}

rv_pixbuf_t rv_filter_unsharp_mask(proven_arena_t *arena,
                                   const rv_pixbuf_t *src,
                                   float sigma,
                                   float amount,
                                   uint8_t threshold) {
    if (!rv_pixbuf_is_valid(src) || arena == NULL) return (rv_pixbuf_t){0};

    rv_pixbuf_t blur = rv_filter_gaussian_blur(arena, src, sigma);
    if (!rv_pixbuf_is_valid(&blur)) return (rv_pixbuf_t){0};

    rv_pixbuf_t dst = rv_pixbuf_create(arena, src->width, src->height, src->format);
    if (!rv_pixbuf_is_valid(&dst)) return (rv_pixbuf_t){0};

    int32_t bpp = rv_bytes_per_pixel(src->format);
    int32_t color_channels = (bpp == 4) ? 3 : 1;

    for (int32_t y = 0; y < src->height; ++y) {
        const uint8_t *spx = src->pixels + ((ptrdiff_t)y * src->stride);
        const uint8_t *bpx = blur.pixels + ((ptrdiff_t)y * blur.stride);
        uint8_t *dpx = dst.pixels + ((ptrdiff_t)y * dst.stride);

        for (int32_t x = 0; x < src->width; ++x) {
            for (int c = 0; c < color_channels; ++c) {
                int s_val = spx[x * bpp + c];
                int b_val = bpx[x * bpp + c];
                int diff = s_val - b_val;

                if (abs(diff) >= (int)threshold) {
                    float sharpened = (float)s_val + amount * (float)diff;
                    dpx[x * bpp + c] = clamp_u8((int)(sharpened + 0.5f));
                } else {
                    dpx[x * bpp + c] = (uint8_t)s_val;
                }
            }
            if (bpp == 4) {
                dpx[x * bpp + 3] = spx[x * bpp + 3]; /* Preserve alpha */
            }
        }
    }

    return dst;
}

rv_pixbuf_t rv_filter_autotrim(proven_arena_t *arena,
                               const rv_pixbuf_t *src,
                               uint8_t bg_threshold,
                               bool detect_white) {
    if (!rv_pixbuf_is_valid(src) || arena == NULL) return (rv_pixbuf_t){0};

    int32_t bpp = rv_bytes_per_pixel(src->format);
    int32_t top = 0;
    int32_t bottom = src->height - 1;
    int32_t left = 0;
    int32_t right = src->width - 1;

    /* Scan top */
    while (top < bottom) {
        int bg_count = 0;
        const uint8_t *row = src->pixels + ((ptrdiff_t)top * src->stride);
        for (int32_t x = 0; x < src->width; ++x) {
            uint8_t lum = get_pixel_lum(row + (x * bpp), src->format);
            bool is_bg = detect_white ? (lum >= bg_threshold) : (lum <= bg_threshold);
            if (is_bg) bg_count++;
        }
        if ((float)bg_count / (float)src->width >= 0.95f) {
            top++;
        } else {
            break;
        }
    }

    /* Scan bottom */
    while (bottom > top) {
        int bg_count = 0;
        const uint8_t *row = src->pixels + ((ptrdiff_t)bottom * src->stride);
        for (int32_t x = 0; x < src->width; ++x) {
            uint8_t lum = get_pixel_lum(row + (x * bpp), src->format);
            bool is_bg = detect_white ? (lum >= bg_threshold) : (lum <= bg_threshold);
            if (is_bg) bg_count++;
        }
        if ((float)bg_count / (float)src->width >= 0.95f) {
            bottom--;
        } else {
            break;
        }
    }

    /* Scan left */
    while (left < right) {
        int bg_count = 0;
        for (int32_t y = top; y <= bottom; ++y) {
            const uint8_t *px = src->pixels + ((ptrdiff_t)y * src->stride) + (left * bpp);
            uint8_t lum = get_pixel_lum(px, src->format);
            bool is_bg = detect_white ? (lum >= bg_threshold) : (lum <= bg_threshold);
            if (is_bg) bg_count++;
        }
        if ((float)bg_count / (float)(bottom - top + 1) >= 0.95f) {
            left++;
        } else {
            break;
        }
    }

    /* Scan right */
    while (right > left) {
        int bg_count = 0;
        for (int32_t y = top; y <= bottom; ++y) {
            const uint8_t *px = src->pixels + ((ptrdiff_t)y * src->stride) + (right * bpp);
            uint8_t lum = get_pixel_lum(px, src->format);
            bool is_bg = detect_white ? (lum >= bg_threshold) : (lum <= bg_threshold);
            if (is_bg) bg_count++;
        }
        if ((float)bg_count / (float)(bottom - top + 1) >= 0.95f) {
            right--;
        } else {
            break;
        }
    }

    int32_t crop_w = right - left + 1;
    int32_t crop_h = bottom - top + 1;
    if (crop_w <= 0 || crop_h <= 0) {
        return rv_pixbuf_clone(arena, src);
    }

    return rv_pixbuf_crop(arena, src, left, top, crop_w, crop_h);
}
