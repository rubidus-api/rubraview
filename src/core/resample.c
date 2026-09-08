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

static void resample_nearest(const rubraview_pixbuf_t *src, rubraview_pixbuf_t *dst) {
    int32_t bpp = rubraview_bytes_per_pixel(src->format);
    float scale_x = (float)src->width / (float)dst->width;
    float scale_y = (float)src->height / (float)dst->height;

    for (int32_t dy = 0; dy < dst->height; ++dy) {
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

static void resample_bilinear(const rubraview_pixbuf_t *src, rubraview_pixbuf_t *dst) {
    int32_t bpp = rubraview_bytes_per_pixel(src->format);
    float scale_x = (float)src->width / (float)dst->width;
    float scale_y = (float)src->height / (float)dst->height;

    for (int32_t dy = 0; dy < dst->height; ++dy) {
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

static void resample_bicubic(const rubraview_pixbuf_t *src, rubraview_pixbuf_t *dst) {
    int32_t bpp = rubraview_bytes_per_pixel(src->format);
    float scale_x = (float)src->width / (float)dst->width;
    float scale_y = (float)src->height / (float)dst->height;

    for (int32_t dy = 0; dy < dst->height; ++dy) {
        float sy = (dy + 0.5f) * scale_y - 0.5f;
        int32_t y_base = (int32_t)floorf(sy);

        float wy[4];
        float sum_wy = 0.0f;
        for (int i = 0; i < 4; ++i) {
            wy[i] = bicubic_weight(sy - (float)(y_base - 1 + i));
            sum_wy += wy[i];
        }
        if (fabsf(sum_wy) > 1e-6f) {
            for (int i = 0; i < 4; ++i) wy[i] /= sum_wy;
        }

        uint8_t *dst_row = dst->pixels + ((ptrdiff_t)dy * dst->stride);

        for (int32_t dx = 0; dx < dst->width; ++dx) {
            float sx = (dx + 0.5f) * scale_x - 0.5f;
            int32_t x_base = (int32_t)floorf(sx);

            float wx[4];
            float sum_wx = 0.0f;
            for (int j = 0; j < 4; ++j) {
                wx[j] = bicubic_weight(sx - (float)(x_base - 1 + j));
                sum_wx += wx[j];
            }
            if (fabsf(sum_wx) > 1e-6f) {
                for (int j = 0; j < 4; ++j) wx[j] /= sum_wx;
            }

            float accum[8] = {0};
            for (int i = 0; i < 4; ++i) {
                int32_t cy = clamp_coord(y_base - 1 + i, src->height);
                const uint8_t *srow = src->pixels + ((ptrdiff_t)cy * src->stride);
                float row_weight = wy[i];

                for (int j = 0; j < 4; ++j) {
                    int32_t cx = clamp_coord(x_base - 1 + j, src->width);
                    const uint8_t *spx = srow + (cx * bpp);
                    float w = row_weight * wx[j];
                    for (int c = 0; c < bpp; ++c) {
                        accum[c] += spx[c] * w;
                    }
                }
            }

            uint8_t *dpx = dst_row + (dx * bpp);
            for (int c = 0; c < bpp; ++c) {
                int ival = (int)(accum[c] + 0.5f);
                dpx[c] = (uint8_t)(ival < 0 ? 0 : (ival > 255 ? 255 : ival));
            }
        }
    }
}

static void resample_lanczos3(const rubraview_pixbuf_t *src, rubraview_pixbuf_t *dst) {
    int32_t bpp = rubraview_bytes_per_pixel(src->format);
    float scale_x = (float)src->width / (float)dst->width;
    float scale_y = (float)src->height / (float)dst->height;

    for (int32_t dy = 0; dy < dst->height; ++dy) {
        float sy = (dy + 0.5f) * scale_y - 0.5f;
        int32_t y_base = (int32_t)floorf(sy);

        float wy[6];
        float sum_wy = 0.0f;
        for (int i = 0; i < 6; ++i) {
            wy[i] = lanczos3_weight(sy - (float)(y_base - 2 + i));
            sum_wy += wy[i];
        }
        if (fabsf(sum_wy) > 1e-6f) {
            for (int i = 0; i < 6; ++i) wy[i] /= sum_wy;
        }

        uint8_t *dst_row = dst->pixels + ((ptrdiff_t)dy * dst->stride);

        for (int32_t dx = 0; dx < dst->width; ++dx) {
            float sx = (dx + 0.5f) * scale_x - 0.5f;
            int32_t x_base = (int32_t)floorf(sx);

            float wx[6];
            float sum_wx = 0.0f;
            for (int j = 0; j < 6; ++j) {
                wx[j] = lanczos3_weight(sx - (float)(x_base - 2 + j));
                sum_wx += wx[j];
            }
            if (fabsf(sum_wx) > 1e-6f) {
                for (int j = 0; j < 6; ++j) wx[j] /= sum_wx;
            }

            float accum[8] = {0};
            for (int i = 0; i < 6; ++i) {
                int32_t cy = clamp_coord(y_base - 2 + i, src->height);
                const uint8_t *srow = src->pixels + ((ptrdiff_t)cy * src->stride);
                float row_weight = wy[i];

                for (int j = 0; j < 6; ++j) {
                    int32_t cx = clamp_coord(x_base - 2 + j, src->width);
                    const uint8_t *spx = srow + (cx * bpp);
                    float w = row_weight * wx[j];
                    for (int c = 0; c < bpp; ++c) {
                        accum[c] += spx[c] * w;
                    }
                }
            }

            uint8_t *dpx = dst_row + (dx * bpp);
            for (int c = 0; c < bpp; ++c) {
                int ival = (int)(accum[c] + 0.5f);
                dpx[c] = (uint8_t)(ival < 0 ? 0 : (ival > 255 ? 255 : ival));
            }
        }
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

    switch (filter) {
        case RUBRAVIEW_FILTER_NEAREST:
            resample_nearest(src, &dst);
            break;
        case RUBRAVIEW_FILTER_BILINEAR:
            resample_bilinear(src, &dst);
            break;
        case RUBRAVIEW_FILTER_BICUBIC:
            resample_bicubic(src, &dst);
            break;
        case RUBRAVIEW_FILTER_LANCZOS3:
            resample_lanczos3(src, &dst);
            break;
        default:
            resample_bilinear(src, &dst);
            break;
    }

    return dst;
}
