#include "rubraview/core.h"
#include <string.h>

static int32_t rv_bytes_per_pixel(rv_pixel_format_t fmt) {
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
