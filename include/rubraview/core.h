#ifndef RUBRAVIEW_CORE_H
#define RUBRAVIEW_CORE_H

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "proven.h"
#include "proven_sys_mem.h"   /* RV-220: proven_sys_mem_copy / _set / _move, the platform's block copy */

#ifdef __cplusplus
extern "C" {
#endif

typedef proven_arena_t prv_arena_t;

/* RV-159 (owner, 2026-10-22: "proven c lib 의 문자열 스타일을 적극적으로 도입하여 활용하고, 중복된 유사한 구현은
   proven 쪽으로 합쳐 하나로"): text is proven's view, `proven_u8str_view_t` (bytes and a size), literals are
   `PROVEN_LIT`, comparisons are proven's (`proven_u8str_view_eq`, `_starts_with`, `_find`, ...). A cast to
   `const char *` is made only where the bytes meet the CRT or Win32. */

/* Text built in an arena by proven's growing string; false when the arena is full. */
static inline bool rubraview_text_append(proven_arena_t *arena, proven_u8str_t *text, proven_mem_view_t bytes_view) {
    const void *bytes = (const void *)bytes_view.ptr;
    proven_size_t size = (proven_size_t)bytes_view.size;
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
/*
 * RV-220 (D-275, owner 2026-11-03: "proven c lib의 메모리 할당과 문자열 방식을 제대로 잘 쓰는지 확인 ... 출력 함수들도
 * proven c lib 쪽으로"): the program's heap is proven's allocator trait. Its blocks come from the C runtime's malloc
 * family, so they mix safely with what Windows and the libraries hand back (proven's own heap allocator keeps every
 * Windows block on _aligned_malloc, which free() and a library's free must never meet). Use these, never malloc,
 * calloc, realloc, free or strdup themselves (scripts/check-proven-ways.py).
 */
proven_allocator_t rubraview_heap(void);
[[nodiscard]] void *rubraview_alloc(proven_size_t bytes);                         /* malloc's: 0 bytes gives a block */
[[nodiscard]] void *rubraview_alloc_zero(proven_size_t count, proven_size_t size);  /* calloc's: the product checked */
[[nodiscard]] void *rubraview_realloc(proven_mem_mut_t block_view);           /* realloc's */
void rubraview_free(void *block);
[[nodiscard]] char *rubraview_strdup(const char *text);
[[nodiscard]] char *rubraview_strndup_view(proven_u8str_view_t text);              /* NUL-ended copy of a view */

[[nodiscard]]
static inline void *rubraview_malloc_product(proven_size_t a, proven_size_t b, proven_size_t c) {
    proven_size_t bytes = 0;
    if (PROVEN_CKD_MUL(&bytes, a, b) || PROVEN_CKD_MUL(&bytes, bytes, c)) return NULL;
    return rubraview_alloc(bytes);
}

/*
 * RV-220: formatting is proven's (`{}` placeholders, `{:.2}`, `{:02x}`, `{:<8}`, `{0}` ...). rubraview_fmt is
 * snprintf's shape over proven_fmt_to: the length the whole would need (so `n < cap` still says it fitted), or -1 when
 * the format or its arguments were refused. Each argument goes in as PROVEN_ARG(x), cast to the type it is meant as.
 */
static inline int rubraview_fmt_length(proven_fmt_result_t r) {
    return r.err == PROVEN_OK || r.err == PROVEN_ERR_OUT_OF_BOUNDS ? (int)r.required : -1;
}
#define rubraview_fmt(buf, cap, fmt, ...) rubraview_fmt_length(proven_fmt_to((char *)(buf), (cap), (fmt) __VA_OPT__(,) __VA_ARGS__))
/* A function of ours taking a format and its arguments takes them as proven does: RUBRAVIEW_FMT_ARGS(...) at the call. */
#define RUBRAVIEW_FMT_ARGS(...) \
    ((const proven_arg_t[]){ proven_arg_none() __VA_OPT__(,) __VA_ARGS__ }), \
    (sizeof((proven_arg_t[]){ proven_arg_none() __VA_OPT__(,) __VA_ARGS__ }) / sizeof(proven_arg_t))
/* Bytes and a count as a view (what printf's %.*s took), and Windows' UTF-16 text as proven's u16 view. */
#define rubraview_bytes_view(p, n) ((proven_u8str_view_t){ .ptr = (const proven_byte_t *)(p), .size = (proven_size_t)(n) })
/* RV-220: a pointer and its length as one slice, the way our functions take them (text, bytes, bytes to write) */
#define rubraview_mem_view(p, n) ((proven_mem_view_t){ .ptr = (const proven_byte_t *)(p), .size = (proven_size_t)(n) })
#define rubraview_mem_mut(p, n) ((proven_mem_mut_t){ .ptr = (proven_byte_t *)(p), .size = (proven_size_t)(n) })
#define rubraview_wide_view(w) ((proven_u16str_view_t){ .ptr = (const proven_u16 *)(w), .size = rubraview_wide_len((const uint16_t *)(w)) })
static inline proven_size_t rubraview_wide_len(const uint16_t *w) {
    proven_size_t n = 0;
    while (w && w[n]) n++;
    return n;
}

/* RV-220: C strings compared as proven views (the CRT's strcmp, strncmp and _stricmp, with their signs). */
static inline int rubraview_cstr_cmp(const char *a, const char *b) {
    return proven_u8str_view_cmp(proven_u8str_view_from_cstr(a ? a : ""), proven_u8str_view_from_cstr(b ? b : ""));
}
int rubraview_cstr_cmp_n(const char *a, proven_u8str_view_t b_view);
int rubraview_cstr_cmp_ci(const char *a, const char *b);

/* RV-220: C strings searched through proven (memcmp and memcpy are proven_sys_mem_cmp / _copy themselves) (strchr, strrchr, strstr,
   memchr: a pointer into the text, or NULL), and ASCII's classes and cases (the "C" locale's isdigit, tolower ...). */
static inline void *rubraview_mem_chr(const void *p, int c, proven_size_t n) {
    return (void *)proven_sys_mem_chr(p, c, n);
}
static inline char *rubraview_cstr_chr(const char *s, int c) {
    if (!s) return NULL;
    if ((char)c == '\0') return (char *)s + proven_cstr_len(s);
    return (char *)rubraview_mem_chr(s, c, proven_cstr_len(s));
}
static inline char *rubraview_cstr_rchr(const char *s, int c) {
    if (!s) return NULL;
    proven_size_t n = proven_cstr_len(s);
    if ((char)c == '\0') return (char *)s + n;
    proven_byte_t ch = (proven_byte_t)c;
    proven_size_t at = proven_u8str_view_find_last(rubraview_bytes_view(s, n), rubraview_bytes_view(&ch, 1));
    return at == PROVEN_INDEX_NOT_FOUND ? NULL : (char *)s + at;
}
static inline char *rubraview_cstr_str(const char *s, const char *needle) {
    if (!s || !needle) return NULL;
    proven_size_t at = proven_u8str_view_find(proven_u8str_view_from_cstr(s), 0, proven_u8str_view_from_cstr(needle));
    return at == PROVEN_INDEX_NOT_FOUND ? NULL : (char *)s + at;
}
static inline int rubraview_ascii_tolower(int c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
static inline int rubraview_ascii_toupper(int c) { return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c; }
static inline bool rubraview_ascii_isdigit(int c) { return c >= '0' && c <= '9'; }
static inline bool rubraview_ascii_isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static inline bool rubraview_ascii_isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

/* RV-220: an integer read from text as strtol reads it (spaces, a sign, digits of `base` 10 or 16); `*end` after it, or
   at `s` when there was none. And text appended to a NUL-ended buffer of `cap` bytes, cut to fit (strncat's use). */
static inline long rubraview_parse_long(const char *s, char **end, int base) {
    const char *p = s;
    while (p && rubraview_ascii_isspace((unsigned char)*p)) p++;
    bool negative = p && (*p == '-');
    if (p && (*p == '-' || *p == '+')) p++;
    long v = 0;
    const char *digits = p;
    for (; p && *p; ++p) {
        int c = rubraview_ascii_tolower((unsigned char)*p), d = c >= '0' && c <= '9' ? c - '0' : base == 16 && c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (d < 0) break;
        v = v * base + d;
    }
    if (end) *end = (char *)(p == digits ? s : p);
    return negative ? -v : v;
}
static inline void rubraview_cstr_append(char *dst, proven_size_t cap, const char *text) {
    proven_size_t at = proven_cstr_len(dst), n = text ? proven_cstr_len(text) : 0;
    if (at + 1 >= cap) return;
    if (n > cap - 1 - at) n = cap - 1 - at;
    proven_sys_mem_copy(dst + at, text, n);
    dst[at + n] = '\0';
}

/* RV-220: `n` spaces (64 at most) as a view: what printf's %*s of "" padded with. */
static inline proven_u8str_view_t rubraview_spaces(int n) {
    static const char SPACES[] = "                                                                ";
    if (n < 0) n = 0;
    if (n > 64) n = 64;
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)SPACES, .size = (proven_size_t)n };
}

/* RV-220: printf's %.Ng (`sig` significant digits, the exponent form outside 1e-4 .. 1e`sig`, trailing zeros dropped),
   which proven's `{:g}` (the shortest that reads back) is not, written into `buf` (32 bytes are enough); returns buf.
   For a format's argument: PROVEN_ARG(rubraview_g_text((char[32]){0}, x, 4)). */
const char *rubraview_g_text(char *buf, double value, int sig);

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
