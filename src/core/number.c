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

static u8str_t trim_blanks(u8str_t s, size_t *leading) {
    size_t start = 0, end = s.len;
    while (start < end && is_blank(s.ptr[start])) start++;
    while (end > start && is_blank(s.ptr[end - 1])) end--;
    if (leading) *leading = start;
    return (u8str_t){ .ptr = s.ptr + start, .len = end - start };
}

bool rubraview_parse_double_prefix(u8str_t text, double *out, size_t *consumed) {
    if (!out || text.len == 0 || !text.ptr) return false;
    size_t leading = 0;
    while (leading < text.len && is_blank(text.ptr[leading])) leading++;
    u8str_t rest = { .ptr = text.ptr + leading, .len = text.len - leading };
    if (rest.len == 0) return false;

    proven_parse_double_result_t r = proven_parse_double_ascii(rubraview_u8_view(rest));
    if (!proven_is_ok(r.err) || r.consumed == 0 || !isfinite(r.val)) return false;
    *out = r.val;
    if (consumed) *consumed = leading + (size_t)r.consumed;
    return true;
}

bool rubraview_parse_double(u8str_t text, double *out) {
    if (!out || text.len == 0 || !text.ptr) return false;
    u8str_t core = trim_blanks(text, NULL);
    if (core.len == 0) return false;
    double value = 0.0;
    size_t used = 0;
    if (!rubraview_parse_double_prefix(core, &value, &used) || used != core.len) return false;
    *out = value;
    return true;
}

bool rubraview_parse_i64(u8str_t text, int64_t *out) {
    if (!out || text.len == 0 || !text.ptr) return false;
    u8str_t core = trim_blanks(text, NULL);
    if (core.len == 0) return false;
    proven_scan_t scan = proven_scan_init(rubraview_u8_view(core));
    proven_result_i64_t r = proven_scan_i64(&scan);
    if (!proven_is_ok(r.err) || scan.cursor != core.len) return false;
    *out = (int64_t)r.val;
    return true;
}
