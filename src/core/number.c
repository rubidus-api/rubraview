/*
 * Numbers out of text, through proven's parsers. See number.h.
 */
#include "rubraview/number.h"
#include "proven/float_parse.h"
#include "proven/scan.h"
#include <math.h>

static bool is_blank(char c) {
    return c == ' ' || c == '\t';
}

static proven_u8str_view_t trim_blanks(proven_u8str_view_t s, size_t *leading) {
    size_t start = 0, end = s.size;
    while (start < end && is_blank(s.ptr[start])) start++;
    while (end > start && is_blank(s.ptr[end - 1])) end--;
    if (leading) *leading = start;
    return (proven_u8str_view_t){ .ptr = s.ptr + start, .size = end - start };
}

bool rubraview_parse_double_prefix(proven_u8str_view_t text, double *out, size_t *consumed) {
    if (!out || text.size == 0 || !text.ptr) return false;
    size_t leading = 0;
    while (leading < text.size && is_blank(text.ptr[leading])) leading++;
    proven_u8str_view_t rest = { .ptr = text.ptr + leading, .size = text.size - leading };
    if (rest.size == 0) return false;

    proven_parse_double_result_t r = proven_parse_double_ascii(rest);
    if (!proven_is_ok(r.err) || r.consumed == 0 || !isfinite(r.val)) return false;
    *out = r.val;
    if (consumed) *consumed = leading + (size_t)r.consumed;
    return true;
}

bool rubraview_parse_double(proven_u8str_view_t text, double *out) {
    if (!out || text.size == 0 || !text.ptr) return false;
    proven_u8str_view_t core = trim_blanks(text, NULL);
    if (core.size == 0) return false;
    double value = 0.0;
    size_t used = 0;
    if (!rubraview_parse_double_prefix(core, &value, &used) || used != core.size) return false;
    *out = value;
    return true;
}

bool rubraview_parse_i64(proven_u8str_view_t text, int64_t *out) {
    if (!out || text.size == 0 || !text.ptr) return false;
    proven_u8str_view_t core = trim_blanks(text, NULL);
    if (core.size == 0) return false;
    proven_scan_t scan = proven_scan_init(core);
    proven_result_i64_t r = proven_scan_i64(&scan);
    if (!proven_is_ok(r.err) || scan.cursor != core.size) return false;
    *out = (int64_t)r.val;
    return true;
}
