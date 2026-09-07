#include "rubraview/core.h"
#include <string.h>

int32_t rv_bytes_per_pixel(rv_pixel_format_t fmt) {
    switch (fmt) {
        case RV_PIXFMT_RGBA8:
        case RV_PIXFMT_BGRA8:
            return 4;
        case RV_PIXFMT_GRAY8:
            return 1;
        case RV_PIXFMT_RGBA16F:
            return 8;
        default:
            return 4;
    }
}

rv_pixbuf_t rv_pixbuf_create(proven_arena_t *arena, int32_t width, int32_t height, rv_pixel_format_t format) {
    if (width <= 0 || height <= 0 || arena == NULL) {
        return (rv_pixbuf_t){0};
    }

    int32_t bpp = rv_bytes_per_pixel(format);
    int32_t stride = width * bpp;
    proven_size_t total_bytes = (proven_size_t)stride * (proven_size_t)height;

    proven_result_mem_mut_t res = proven_arena_alloc(arena, total_bytes);
    if (!proven_is_ok(res.err)) {
        return (rv_pixbuf_t){0};
    }

    uint8_t *ptr = (uint8_t*)res.value.ptr;
    memset(ptr, 0, total_bytes);

    return (rv_pixbuf_t){
        .pixels = ptr,
        .width = width,
        .height = height,
        .stride = stride,
        .format = format,
        .arena = arena,
    };
}

uint8_t *rv_pixbuf_at(const rv_pixbuf_t *pb, int32_t x, int32_t y) {
    if (!rv_pixbuf_is_valid(pb)) return NULL;
    if (x < 0 || x >= pb->width || y < 0 || y >= pb->height) return NULL;
    return pb->pixels + ((ptrdiff_t)y * pb->stride) + ((ptrdiff_t)x * rv_bytes_per_pixel(pb->format));
}

void rv_pixbuf_clear(rv_pixbuf_t *pb, uint32_t color_hex) {
    if (!rv_pixbuf_is_valid(pb)) return;

    int32_t bpp = rv_bytes_per_pixel(pb->format);
    if (bpp == 4) {
        for (int32_t y = 0; y < pb->height; ++y) {
            uint32_t *row = (uint32_t*)(pb->pixels + ((ptrdiff_t)y * pb->stride));
            for (int32_t x = 0; x < pb->width; ++x) {
                row[x] = color_hex;
            }
        }
    } else if (bpp == 1) {
        uint8_t gray = (uint8_t)(color_hex & 0xFF);
        for (int32_t y = 0; y < pb->height; ++y) {
            uint8_t *row = pb->pixels + ((ptrdiff_t)y * pb->stride);
            memset(row, gray, (size_t)pb->width);
        }
    }
}

rv_pixbuf_t rv_pixbuf_clone(proven_arena_t *arena, const rv_pixbuf_t *src) {
    if (!rv_pixbuf_is_valid(src) || arena == NULL) {
        return (rv_pixbuf_t){0};
    }
    rv_pixbuf_t dst = rv_pixbuf_create(arena, src->width, src->height, src->format);
    if (!rv_pixbuf_is_valid(&dst)) {
        return (rv_pixbuf_t){0};
    }
    int32_t bpp = rv_bytes_per_pixel(src->format);
    int32_t row_bytes = src->width * bpp;
    for (int32_t y = 0; y < src->height; ++y) {
        const uint8_t *src_row = src->pixels + ((ptrdiff_t)y * src->stride);
        uint8_t *dst_row = dst.pixels + ((ptrdiff_t)y * dst.stride);
        memcpy(dst_row, src_row, (size_t)row_bytes);
    }
    return dst;
}

rv_pixbuf_t rv_pixbuf_subview(const rv_pixbuf_t *src, int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!rv_pixbuf_is_valid(src)) return (rv_pixbuf_t){0};
    if (x < 0 || y < 0 || w <= 0 || h <= 0) return (rv_pixbuf_t){0};
    if (x >= src->width || y >= src->height) return (rv_pixbuf_t){0};

    if (x + w > src->width) {
        w = src->width - x;
    }
    if (y + h > src->height) {
        h = src->height - y;
    }
    if (w <= 0 || h <= 0) return (rv_pixbuf_t){0};

    int32_t bpp = rv_bytes_per_pixel(src->format);
    uint8_t *sub_pixels = src->pixels + ((ptrdiff_t)y * src->stride) + ((ptrdiff_t)x * bpp);

    return (rv_pixbuf_t){
        .pixels = sub_pixels,
        .width = w,
        .height = h,
        .stride = src->stride,
        .format = src->format,
        .arena = NULL, /* Non-owning view */
    };
}

rv_pixbuf_t rv_pixbuf_crop(proven_arena_t *arena, const rv_pixbuf_t *src, int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!rv_pixbuf_is_valid(src) || arena == NULL) return (rv_pixbuf_t){0};
    if (x < 0 || y < 0 || w <= 0 || h <= 0) return (rv_pixbuf_t){0};
    if (x >= src->width || y >= src->height) return (rv_pixbuf_t){0};

    if (x + w > src->width) {
        w = src->width - x;
    }
    if (y + h > src->height) {
        h = src->height - y;
    }
    if (w <= 0 || h <= 0) return (rv_pixbuf_t){0};

    rv_pixbuf_t dst = rv_pixbuf_create(arena, w, h, src->format);
    if (!rv_pixbuf_is_valid(&dst)) return (rv_pixbuf_t){0};

    int32_t bpp = rv_bytes_per_pixel(src->format);
    int32_t row_bytes = w * bpp;
    for (int32_t row = 0; row < h; ++row) {
        const uint8_t *src_row = src->pixels + ((ptrdiff_t)(y + row) * src->stride) + ((ptrdiff_t)x * bpp);
        uint8_t *dst_row = dst.pixels + ((ptrdiff_t)row * dst.stride);
        memcpy(dst_row, src_row, (size_t)row_bytes);
    }
    return dst;
}

rv_pixbuf_t rv_pixbuf_convert(proven_arena_t *arena, const rv_pixbuf_t *src, rv_pixel_format_t dst_format) {
    if (!rv_pixbuf_is_valid(src) || arena == NULL) return (rv_pixbuf_t){0};
    if (src->format == dst_format) {
        return rv_pixbuf_clone(arena, src);
    }

    rv_pixbuf_t dst = rv_pixbuf_create(arena, src->width, src->height, dst_format);
    if (!rv_pixbuf_is_valid(&dst)) return (rv_pixbuf_t){0};

    for (int32_t y = 0; y < src->height; ++y) {
        const uint8_t *s = src->pixels + ((ptrdiff_t)y * src->stride);
        uint8_t *d = dst.pixels + ((ptrdiff_t)y * dst.stride);

        if (src->format == RV_PIXFMT_RGBA8 && dst_format == RV_PIXFMT_BGRA8) {
            for (int32_t x = 0; x < src->width; ++x) {
                d[x * 4 + 0] = s[x * 4 + 2]; /* B */
                d[x * 4 + 1] = s[x * 4 + 1]; /* G */
                d[x * 4 + 2] = s[x * 4 + 0]; /* R */
                d[x * 4 + 3] = s[x * 4 + 3]; /* A */
            }
        } else if (src->format == RV_PIXFMT_BGRA8 && dst_format == RV_PIXFMT_RGBA8) {
            for (int32_t x = 0; x < src->width; ++x) {
                d[x * 4 + 0] = s[x * 4 + 2]; /* R */
                d[x * 4 + 1] = s[x * 4 + 1]; /* G */
                d[x * 4 + 2] = s[x * 4 + 0]; /* B */
                d[x * 4 + 3] = s[x * 4 + 3]; /* A */
            }
        } else if ((src->format == RV_PIXFMT_RGBA8 || src->format == RV_PIXFMT_BGRA8) && dst_format == RV_PIXFMT_GRAY8) {
            bool is_bgra = (src->format == RV_PIXFMT_BGRA8);
            for (int32_t x = 0; x < src->width; ++x) {
                uint32_t r = is_bgra ? s[x * 4 + 2] : s[x * 4 + 0];
                uint32_t g = s[x * 4 + 1];
                uint32_t b = is_bgra ? s[x * 4 + 0] : s[x * 4 + 2];
                /* Rec. 709 integer luminance */
                uint32_t lum = (2126 * r + 7152 * g + 722 * b + 5000) / 10000;
                d[x] = (uint8_t)(lum > 255 ? 255 : lum);
            }
        } else if (src->format == RV_PIXFMT_GRAY8 && (dst_format == RV_PIXFMT_RGBA8 || dst_format == RV_PIXFMT_BGRA8)) {
            for (int32_t x = 0; x < src->width; ++x) {
                uint8_t g = s[x];
                d[x * 4 + 0] = g;
                d[x * 4 + 1] = g;
                d[x * 4 + 2] = g;
                d[x * 4 + 3] = 255;
            }
        } else {
            return (rv_pixbuf_t){0};
        }
    }

    return dst;
}
