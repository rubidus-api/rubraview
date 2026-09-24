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

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_NUMBER_H */
