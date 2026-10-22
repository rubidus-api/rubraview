#ifndef RUBRAVIEW_CORE_H
#define RUBRAVIEW_CORE_H

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "proven.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef proven_arena_t prv_arena_t;

/* RV-159 (owner, 2026-10-22: "proven c lib 의 문자열 스타일을 적극적으로 도입하여 활용하고, 중복된 유사한 구현은
   proven 쪽으로 합쳐 하나로"): text is proven's view, `proven_u8str_view_t` (bytes and a size), literals are
   `PROVEN_LIT`, comparisons are proven's (`proven_u8str_view_eq`, `_starts_with`, `_find`, ...). A cast to
   `const char *` is made only where the bytes meet the CRT or Win32. */

/* Text built in an arena by proven's growing string; false when the arena is full. */
static inline bool rubraview_text_append(proven_arena_t *arena, proven_u8str_t *text, const void *bytes, proven_size_t size) {
    return size == 0 || proven_is_ok(proven_u8str_append_grow(proven_arena_as_allocator(arena), text, proven_u8str_view_from_bytes(bytes, size)));
}

/* What was built, NUL-terminated in the arena; "" when nothing was. */
static inline proven_u8str_view_t rubraview_text_view(const proven_u8str_t *text) {
    proven_u8str_view_t v = proven_u8str_as_view(text);
    return v.size > 0 ? v : PROVEN_LIT("");
}

/* RV-160: proven v0.76.0 has what D-191 asked of it (ASCII case set aside: `proven_u8str_view_eq_ascii_ci`,
   `_starts_with_ascii_ci`; `proven_u8str_view_from_bytes`; `proven_utf8_encode_one`, `proven_utf8_validate`). One
   letter lowered, for sorting and grouping by it: */

static inline char rubraview_ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
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

/*
 * D-121: the same for memory from the heap — a decoder's thread, a buffer
 * that outlives the frame. `a * b * c` bytes, or NULL when the product does
 * not fit in a size (a picture's width and height come out of a file).
 * Pass 1 for a factor that is not there.
 */
[[nodiscard]]
static inline void *rubraview_malloc_product(proven_size_t a, proven_size_t b, proven_size_t c) {
    proven_size_t bytes = 0;
    if (PROVEN_CKD_MUL(&bytes, a, b) || PROVEN_CKD_MUL(&bytes, bytes, c)) return NULL;
    return malloc(bytes);
}

/* RV-141: the size alone, checked: `*out = a * b * c`, false when it does not fit. */
[[nodiscard]]
static inline bool rubraview_size_product(proven_size_t *out, proven_size_t a, proven_size_t b, proven_size_t c) {
    proven_size_t bytes = 0;
    if (PROVEN_CKD_MUL(&bytes, a, b) || PROVEN_CKD_MUL(&bytes, bytes, c)) return false;
    *out = bytes;
    return true;
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
