#ifndef RUBRAVIEW_CORE_H
#define RUBRAVIEW_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "proven.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef proven_arena_t prv_arena_t;

typedef enum rv_pixel_format {
    RV_PIXFMT_RGBA8 = 0,   /* Standard 32-bit RGBA (8 bits per channel) */
    RV_PIXFMT_BGRA8,       /* Direct2D/WIC native layout */
    RV_PIXFMT_GRAY8,       /* 8-bit grayscale luminance */
    RV_PIXFMT_RGBA16F,     /* 64-bit half-float HDR */
} rv_pixel_format_t;

typedef struct rv_pixbuf {
    uint8_t           *pixels;
    int32_t            width;
    int32_t            height;
    int32_t            stride;       /* Bytes per scanline */
    rv_pixel_format_t  format;
    proven_arena_t    *arena;        /* Owning arena (NULL if non-owning view) */
} rv_pixbuf_t;

/**
 * Allocate a new pixel buffer within the provided memory arena.
 */
rv_pixbuf_t rv_pixbuf_create(proven_arena_t *arena, int32_t width, int32_t height, rv_pixel_format_t format);

/**
 * Check if the pixel buffer is valid and has allocated storage.
 */
static inline bool rv_pixbuf_is_valid(const rv_pixbuf_t *pb) {
    return pb != NULL && pb->pixels != NULL && pb->width > 0 && pb->height > 0;
}

/**
 * Obtain a pointer to a specific pixel coordinate (clamps or returns NULL if out of bounds).
 */
uint8_t *rv_pixbuf_at(const rv_pixbuf_t *pb, int32_t x, int32_t y);

/**
 * Fill pixel buffer with a solid 32-bit RGBA/BGRA color.
 */
void rv_pixbuf_clear(rv_pixbuf_t *pb, uint32_t color_hex);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_CORE_H */
