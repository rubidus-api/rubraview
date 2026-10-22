#include "rubraview/stream.h"
#include <stdio.h>
#include <string.h>

static const char *const SCHEMES[] = { "http", "https", "mms", "mmsh", "mmst" };

/* The scheme's length when `text` starts with one of ours and `://`; 0 otherwise. */
static size_t scheme_len(proven_u8str_view_t text) {
    for (size_t s = 0; s < sizeof(SCHEMES) / sizeof(SCHEMES[0]); ++s) {
        size_t n = strlen(SCHEMES[s]);
        if (text.size < n + 3 || memcmp(text.ptr + n, "://", 3) != 0) continue;
        bool same = true;
        for (size_t i = 0; i < n && same; ++i) {
            char c = text.ptr[i];
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            same = c == SCHEMES[s][i];
        }
        if (same) return n;
    }
    return 0;
}

bool rubraview_stream_is_address(proven_u8str_view_t text) {
    if (!text.ptr || text.size >= RUBRAVIEW_STREAM_URL_MAX) return false;
    size_t n = scheme_len(text);
    if (n == 0) return false;
    size_t host = n + 3;
    if (host >= text.size || text.ptr[host] == '/' || text.ptr[host] == '?' || text.ptr[host] == '#') return false;
    for (size_t i = 0; i < text.size; ++i) {
        unsigned char c = (unsigned char)text.ptr[i];
        if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
}

size_t rubraview_stream_ffmpeg_urls(proven_u8str_view_t address, char out[2][RUBRAVIEW_STREAM_URL_MAX]) {
    if (!rubraview_stream_is_address(address)) return 0;
    size_t n = scheme_len(address);
    proven_u8str_view_t rest = { .ptr = address.ptr + n, .size = address.size - n };   /* "://host/..." */
    if (n == 3) {   /* mms: the two FFmpeg has, HTTP first */
        int a = snprintf(out[0], RUBRAVIEW_STREAM_URL_MAX, "mmsh%.*s", (int)rest.size, rest.ptr);
        int b = snprintf(out[1], RUBRAVIEW_STREAM_URL_MAX, "mmst%.*s", (int)rest.size, rest.ptr);
        if (a <= 0 || (size_t)a >= RUBRAVIEW_STREAM_URL_MAX || b <= 0 || (size_t)b >= RUBRAVIEW_STREAM_URL_MAX) return 0;
        return 2;
    }
    memcpy(out[0], address.ptr, address.size);
    out[0][address.size] = '\0';
    for (size_t i = 0; i < n; ++i) if (out[0][i] >= 'A' && out[0][i] <= 'Z') out[0][i] = (char)(out[0][i] - 'A' + 'a');
    return 1;
}

proven_u8str_view_t rubraview_stream_display_name(proven_u8str_view_t address) {
    size_t n = scheme_len(address);
    if (n == 0) return address;
    size_t start = n + 3, end = address.size;
    /* user:password@ — only before the path begins */
    for (size_t i = start; i < end && address.ptr[i] != '/'; ++i) {
        if (address.ptr[i] == '@') start = i + 1;
    }
    for (size_t i = start; i < end; ++i) {
        if (address.ptr[i] == '?' || address.ptr[i] == '#') { end = i; break; }
    }
    while (end > start && address.ptr[end - 1] == '/') end--;
    if (end <= start) return address;
    return (proven_u8str_view_t){ .ptr = address.ptr + start, .size = end - start };
}
