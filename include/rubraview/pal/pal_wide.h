#ifndef RUBRAVIEW_PAL_WIDE_H
#define RUBRAVIEW_PAL_WIDE_H

#include <wchar.h>
#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-220 (D-275): Windows' UTF-16 text (WCHAR, 16 bits there) handled the proven way: copies and appends bounded by the
 * buffer they go into, lengths and searches over proven's views and block copies, and wide formatting through proven's
 * formatter (UTF-8 first, then UTF-16 by proven_utf8_to_utf16). For Windows' own sources only: wchar_t is 32 bits
 * elsewhere.
 */

/* The element count of an array; a pointer does not compile (its size is not the buffer's). */
#define RUBRAVIEW_COUNT_OF(a) \
    (sizeof(a) / sizeof((a)[0]) + 0 * sizeof(struct { int an_array_not_a_pointer : \
        __builtin_types_compatible_p(__typeof__(a), __typeof__(&(a)[0])) ? -1 : 1; }))

static inline size_t rubraview_wlen(const wchar_t *w) {
    return rubraview_wide_len((const uint16_t *)w);
}

/* `text` into `dst` of `cap` units, NUL-ended; false (and as much as fits) when it did not fit. */
static inline bool rubraview_wcopy(wchar_t *dst, size_t cap, const wchar_t *text) {
    if (!dst || cap == 0) return false;
    size_t n = text ? rubraview_wlen(text) : 0;
    bool fits = n < cap;
    if (!fits) n = cap - 1;
    proven_sys_mem_move(dst, text, n * sizeof(wchar_t));
    dst[n] = 0;
    return fits;
}
static inline bool rubraview_wcat(wchar_t *dst, size_t cap, const wchar_t *text) {
    if (!dst || cap == 0) return false;
    size_t at = rubraview_wlen(dst);
    if (at >= cap) return false;
    return rubraview_wcopy(dst + at, cap - at, text);
}
#define rubraview_wcopy_a(dst, text) rubraview_wcopy((dst), RUBRAVIEW_COUNT_OF(dst), (text))
#define rubraview_wcat_a(dst, text) rubraview_wcat((dst), RUBRAVIEW_COUNT_OF(dst), (text))

static inline int rubraview_wcmp(const wchar_t *a, const wchar_t *b) {
    for (;; ++a, ++b) {
        if (*a != *b) return (uint16_t)*a < (uint16_t)*b ? -1 : 1;
        if (!*a) return 0;
    }
}
static inline wchar_t rubraview_wascii_lower(wchar_t c) { return c >= L'A' && c <= L'Z' ? (wchar_t)(c + (L'a' - L'A')) : c; }
/* ASCII letters folded (what the program compares this way: extensions, switches, drive letters) */
static inline int rubraview_wcmp_ci(const wchar_t *a, const wchar_t *b) {
    for (;; ++a, ++b) {
        wchar_t x = rubraview_wascii_lower(*a), y = rubraview_wascii_lower(*b);
        if (x != y) return (uint16_t)x < (uint16_t)y ? -1 : 1;
        if (!x) return 0;
    }
}
/* the first `n` units at most (wcsncmp / _wcsnicmp) */
static inline int rubraview_wcmp_n(const wchar_t *a, const wchar_t *b, size_t n, bool fold) {
    for (size_t i = 0; i < n; ++i, ++a, ++b) {
        wchar_t x = fold ? rubraview_wascii_lower(*a) : *a, y = fold ? rubraview_wascii_lower(*b) : *b;
        if (x != y) return (uint16_t)x < (uint16_t)y ? -1 : 1;
        if (!x) return 0;
    }
    return 0;
}
static inline wchar_t *rubraview_wchr(const wchar_t *s, wchar_t c) {
    for (; s && *s; ++s) if (*s == c) return (wchar_t *)s;
    return s && c == 0 ? (wchar_t *)s : NULL;
}
static inline wchar_t *rubraview_wrchr(const wchar_t *s, wchar_t c) {
    const wchar_t *last = NULL;
    for (; s && *s; ++s) if (*s == c) last = s;
    return (wchar_t *)last;
}
static inline wchar_t *rubraview_wstr(const wchar_t *s, const wchar_t *needle) {
    if (!s || !needle) return NULL;
    size_t n = rubraview_wlen(needle);
    for (; *s; ++s) if (proven_sys_mem_cmp(s, needle, n * sizeof(wchar_t)) == 0) return (wchar_t *)s;
    return n == 0 ? (wchar_t *)s : NULL;
}

/* Wide formatting: rubraview_wfmt(dst, cap, "{}\\{}.bin", PROVEN_ARG(rubraview_wide_view(folder)), ...) — a UTF-8 format,
   as rubraview_fmt takes, written into `dst` as UTF-16, NUL-ended. Returns the units written, or -1 when it did not fit
   or was refused (dst then empty), as _snwprintf said. */
char *rubraview_wfmt_scratch(void);
int rubraview_wfmt_finish(wchar_t *dst, size_t cap, int utf8_length, const char *utf8);
#define RUBRAVIEW_WFMT_SCRATCH 8192
#define rubraview_wfmt(dst, cap, fmt, ...) \
    rubraview_wfmt_finish((dst), (cap), rubraview_fmt(rubraview_wfmt_scratch(), RUBRAVIEW_WFMT_SCRATCH, (fmt) __VA_OPT__(,) __VA_ARGS__), \
                          rubraview_wfmt_scratch())

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_WIDE_H */
