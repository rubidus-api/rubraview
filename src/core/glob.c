#include "rubraview/glob.h"
#include <ctype.h>

bool rubraview_glob_match(u8str_t name, u8str_t pattern) {
    if (!name.ptr && name.len > 0) return false;
    if (!pattern.ptr && pattern.len > 0) return false;

    size_t n = 0, p = 0;
    size_t star_p = pattern.len + 1; /* sentinel: no '*' seen yet */
    size_t star_n = 0;

    while (n < name.len) {
        if (p < pattern.len &&
            (pattern.ptr[p] == '?' ||
             tolower((unsigned char)pattern.ptr[p]) == tolower((unsigned char)name.ptr[n]))) {
            n++;
            p++;
        } else if (p < pattern.len && pattern.ptr[p] == '*') {
            star_p = p;
            star_n = n;
            p++;
        } else if (star_p <= pattern.len) {
            p = star_p + 1;
            star_n++;
            n = star_n;
        } else {
            return false;
        }
    }

    while (p < pattern.len && pattern.ptr[p] == '*') p++;
    return p == pattern.len;
}

bool rubraview_glob_match_list(u8str_t name, u8str_t pattern_list) {
    if (pattern_list.len == 0) return true;
    if (!pattern_list.ptr) return true;

    size_t start = 0;
    for (size_t i = 0; i <= pattern_list.len; ++i) {
        if (i == pattern_list.len || pattern_list.ptr[i] == ';') {
            size_t seg_start = start, seg_end = i;
            while (seg_start < seg_end && pattern_list.ptr[seg_start] == ' ') seg_start++;
            while (seg_end > seg_start && pattern_list.ptr[seg_end - 1] == ' ') seg_end--;

            if (seg_end > seg_start) {
                u8str_t pat = { .ptr = pattern_list.ptr + seg_start, .len = seg_end - seg_start };
                if (rubraview_glob_match(name, pat)) return true;
            }
            start = i + 1;
        }
    }
    return false;
}
