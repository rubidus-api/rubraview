#ifndef RUBRAVIEW_CORE_H
#define RUBRAVIEW_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
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

/* ---- views: one set of comparisons for the whole program ----
   An empty view may carry a NULL pointer, and memcmp must not be handed
   one even for zero bytes; these check the length first. */

static inline bool rubraview_u8_eq(u8str_t a, u8str_t b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0);
}

static inline bool rubraview_u8_eq_lit(u8str_t s, const char *lit) {
    size_t n = strlen(lit);
    return s.len == n && (n == 0 || memcmp(s.ptr, lit, n) == 0);
}

static inline char rubraview_ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* ASCII letters ignore case; every other byte must match exactly. */
static inline bool rubraview_u8_eq_lit_ci(u8str_t s, const char *lit) {
    size_t n = strlen(lit);
    if (s.len != n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (rubraview_ascii_lower(s.ptr[i]) != rubraview_ascii_lower(lit[i])) return false;
    }
    return true;
}

static inline bool rubraview_u8_starts_with(u8str_t s, const char *prefix) {
    size_t n = strlen(prefix);
    return s.len >= n && (n == 0 || memcmp(s.ptr, prefix, n) == 0);
}

static inline bool rubraview_u8_starts_with_ci(u8str_t s, const char *prefix) {
    size_t n = strlen(prefix);
    if (s.len < n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (rubraview_ascii_lower(s.ptr[i]) != rubraview_ascii_lower(prefix[i])) return false;
    }
    return true;
}

/* The same bytes as proven's view type, for proven's own algorithms
   (the parser, the scanner). u8str_t stays char-typed because WinAPI
   and the rest of the viewer speak char; D-32. */
static inline proven_u8str_view_t rubraview_u8_view(u8str_t s) {
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)s.ptr, .size = s.len };
}

/**
 * `count` elements of `size` bytes from the arena, the product checked
 * with proven's PROVEN_CKD_MUL (manual chapter 1, section 4). A product
 * that does not fit is refused with PROVEN_ERR_OVERFLOW instead of
 * wrapping into a small block that the caller then writes past. Every
 * array the viewer takes from an arena goes through here.
 */
[[nodiscard]]
static inline proven_result_mem_mut_t rubraview_arena_alloc_array(proven_arena_t *arena,
                                                                  proven_size_t count,
                                                                  proven_size_t size) {
    proven_size_t bytes = 0;
    if (PROVEN_CKD_MUL(&bytes, count, size)) {
        return (proven_result_mem_mut_t){ .err = PROVEN_ERR_OVERFLOW };
    }
    return proven_arena_alloc(arena, bytes);
}

typedef enum rubraview_pixel_format {
    RUBRAVIEW_PIXFMT_RGBA8 = 0,   /* Standard 32-bit RGBA (8 bits per channel) */
    RUBRAVIEW_PIXFMT_BGRA8,       /* Direct2D/WIC native layout */
    RUBRAVIEW_PIXFMT_GRAY8,       /* 8-bit grayscale luminance */
    RUBRAVIEW_PIXFMT_RGBA16F,     /* 64-bit half-float HDR */
} rubraview_pixel_format_t;

typedef struct rubraview_pixbuf {
    uint8_t           *pixels;
    int32_t            width;
    int32_t            height;
    int32_t            stride;       /* Bytes per scanline */
    rubraview_pixel_format_t  format;
    proven_arena_t    *arena;        /* Owning arena (NULL if non-owning view) */
} rubraview_pixbuf_t;

/**
 * Allocate a new pixel buffer within the provided memory arena.
 */
rubraview_pixbuf_t rubraview_pixbuf_create(proven_arena_t *arena, int32_t width, int32_t height, rubraview_pixel_format_t format);

/**
 * Check if the pixel buffer is valid and has allocated storage.
 */
static inline bool rubraview_pixbuf_is_valid(const rubraview_pixbuf_t *pb) {
    return pb != NULL && pb->pixels != NULL && pb->width > 0 && pb->height > 0;
}

/**
 * Obtain a pointer to a specific pixel coordinate (clamps or returns NULL if out of bounds).
 */
uint8_t *rubraview_pixbuf_at(const rubraview_pixbuf_t *pb, int32_t x, int32_t y);

/**
 * Fill pixel buffer with a solid 32-bit RGBA/BGRA color.
 */
void rubraview_pixbuf_clear(rubraview_pixbuf_t *pb, uint32_t color_hex);

/**
 * Return bytes per pixel for a given pixel format.
 */
int32_t rubraview_bytes_per_pixel(rubraview_pixel_format_t fmt);

/**
 * Deep clone a pixel buffer into the specified memory arena.
 */
rubraview_pixbuf_t rubraview_pixbuf_clone(proven_arena_t *arena, const rubraview_pixbuf_t *src);

/**
 * Create a non-owning subview into a rectangular region of src.
 * Shares the underlying pixel buffer with an adjusted pointer and parent stride.
 */
rubraview_pixbuf_t rubraview_pixbuf_subview(const rubraview_pixbuf_t *src, int32_t x, int32_t y, int32_t w, int32_t h);

/**
 * Extract a rectangular subregion into a tightly packed newly allocated pixbuf in arena.
 */
rubraview_pixbuf_t rubraview_pixbuf_crop(proven_arena_t *arena, const rubraview_pixbuf_t *src, int32_t x, int32_t y, int32_t w, int32_t h);

/**
 * Convert a pixel buffer between formats (RGBA8, BGRA8, GRAY8) into arena.
 */
rubraview_pixbuf_t rubraview_pixbuf_convert(proven_arena_t *arena, const rubraview_pixbuf_t *src, rubraview_pixel_format_t dst_format);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_CORE_H */
