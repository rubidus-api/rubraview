#ifndef RUBRAVIEW_NUMBER_H
#define RUBRAVIEW_NUMBER_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Numbers read out of text — settings, playlists, subtitles, tags — through
 * proven's parsers (proven_parse_double_ascii, proven_scan_i64). A view is
 * read where it lies: nothing is copied into a stack buffer first, nothing
 * past its length is looked at, and the C locale plays no part.
 *
 * Spaces and tabs around the number are allowed. Only finite values are
 * numbers: "inf", "nan" and an overflow to infinity are refused, since no
 * setting or time in the viewer means either.
 */

/* The whole view is one number. */
[[nodiscard]] bool rubraview_parse_double(u8str_t text, double *out);

/* A number at the start of the view, with whatever follows left alone
   ("-7.23 dB", "500 ms"). `consumed` counts leading spaces too; it may be NULL. */
[[nodiscard]] bool rubraview_parse_double_prefix(u8str_t text, double *out, size_t *consumed);

/* The whole view is one decimal integer that fits in int64_t. */
[[nodiscard]] bool rubraview_parse_i64(u8str_t text, int64_t *out);

/*
 * One more decimal digit onto a number being read by hand (a time stamp's
 * fields, a cue sheet's track number), through proven's checked arithmetic:
 * a run of digits too long for the type stays at the type's largest value
 * instead of overflowing, which for a signed type is undefined.
 */
static inline int64_t rubraview_digit_append(int64_t value, char digit) {
    int64_t next = 0;
    if (PROVEN_CKD_MUL(&next, value, (int64_t)10) || PROVEN_CKD_ADD(&next, next, (int64_t)(digit - '0'))) return INT64_MAX;
    return next;
}

static inline int32_t rubraview_digit_append_i32(int32_t value, char digit) {
    int32_t next = 0;
    if (PROVEN_CKD_MUL(&next, value, (int32_t)10) || PROVEN_CKD_ADD(&next, next, (int32_t)(digit - '0'))) return INT32_MAX;
    return next;
}

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_NUMBER_H */
