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

typedef struct u8str {
    const char   *ptr;
    proven_size_t len;
} u8str_t;

#define U8(lit) ((u8str_t){ .ptr = ("" lit), .len = sizeof("" lit) - 1 })

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

/**
 * Return bytes per pixel for a given pixel format.
 */
int32_t rv_bytes_per_pixel(rv_pixel_format_t fmt);

/**
 * Deep clone a pixel buffer into the specified memory arena.
 */
rv_pixbuf_t rv_pixbuf_clone(proven_arena_t *arena, const rv_pixbuf_t *src);

/**
 * Create a non-owning subview into a rectangular region of src.
 * Shares the underlying pixel buffer with an adjusted pointer and parent stride.
 */
rv_pixbuf_t rv_pixbuf_subview(const rv_pixbuf_t *src, int32_t x, int32_t y, int32_t w, int32_t h);

/**
 * Extract a rectangular subregion into a tightly packed newly allocated pixbuf in arena.
 */
rv_pixbuf_t rv_pixbuf_crop(proven_arena_t *arena, const rv_pixbuf_t *src, int32_t x, int32_t y, int32_t w, int32_t h);

/**
 * Convert a pixel buffer between formats (RGBA8, BGRA8, GRAY8) into arena.
 */
rv_pixbuf_t rv_pixbuf_convert(proven_arena_t *arena, const rv_pixbuf_t *src, rv_pixel_format_t dst_format);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_CORE_H */
