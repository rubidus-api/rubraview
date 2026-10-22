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

/* ---- RV-158 ---- */

bool rubraview_stream_is_list_address(proven_u8str_view_t address) {
    if (!rubraview_stream_is_address(address)) return false;
    size_t end = address.size;
    for (size_t i = scheme_len(address) + 3; i < address.size; ++i) {
        if (address.ptr[i] == '?' || address.ptr[i] == '#') { end = i; break; }
    }
    proven_u8str_view_t path = { .ptr = address.ptr, .size = end };
    static const char *const LISTS[] = { ".pls", ".m3u", ".asx", ".wax", ".wvx" };
    for (size_t k = 0; k < sizeof(LISTS) / sizeof(LISTS[0]); ++k) {
        size_t n = strlen(LISTS[k]);
        if (path.size > n && proven_u8str_view_eq_ascii_ci((proven_u8str_view_t){ .ptr = path.ptr + path.size - n, .size = n },
                                                           proven_u8str_view_from_cstr(LISTS[k])))
            return true;
    }
    return false;
}

static size_t find_from(proven_u8str_view_t s, size_t from, const char *what) {
    size_t n = strlen(what);
    for (size_t i = from; i + n <= s.size; ++i) if (memcmp(s.ptr + i, what, n) == 0) return i;
    return SIZE_MAX;
}

size_t rubraview_stream_icy_title(proven_u8str_view_t packet, char *out, size_t cap) {
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!packet.ptr) return 0;
    size_t at = find_from(packet, 0, "StreamTitle='");
    if (at == SIZE_MAX) return 0;
    size_t start = at + 13, end = SIZE_MAX;
    size_t url = find_from(packet, start, "';StreamUrl=");
    if (url != SIZE_MAX) end = url;
    else {
        for (size_t i = start; i + 2 <= packet.size; ++i) if (packet.ptr[i] == '\'' && packet.ptr[i + 1] == ';') end = i;
        if (end == SIZE_MAX) end = packet.size;
    }
    proven_u8str_view_t v = { .ptr = packet.ptr + start, .size = end - start };
    bool utf8 = proven_utf8_validate(v, NULL) == PROVEN_OK;
    size_t n = 0;
    bool space = false;
    for (size_t i = 0; i < v.size; ++i) {
        unsigned char c = v.ptr[i];
        if (c < 0x20 || c == 0x7F) { space = true; continue; }
        if (c == ' ') { space = true; continue; }
        /* the next character's bytes, a space before it when one was left out */
        proven_byte_t ch[4];
        size_t k = 1;
        if (!utf8 && c >= 0x80) k = proven_utf8_encode_one(c, ch);   /* Latin-1 */
        else if (utf8 && c >= 0x80) { proven_utf8_char_t d = proven_utf8_decode_next(v, i); k = d.len; memcpy(ch, v.ptr + i, k); i += k - 1; }
        else ch[0] = c;
        size_t need = k + (space && n > 0 ? 1 : 0);
        if (n + need >= cap) break;
        if (space && n > 0) out[n++] = ' ';
        space = false;
        memcpy(out + n, ch, k);
        n += k;
    }
    out[n] = '\0';
    return n;
}

bool rubraview_stream_split_typed(proven_u8str_view_t typed, proven_u8str_view_t *address, proven_u8str_view_t *title) {
    proven_u8str_view_t t = proven_u8str_view_trim(typed);
    size_t cut = 0;
    while (cut < t.size && t.ptr[cut] != ' ' && t.ptr[cut] != '\t') cut++;
    proven_u8str_view_t a = { .ptr = t.ptr, .size = cut };
    if (!rubraview_stream_is_address(a)) return false;
    if (address) *address = a;
    if (title) *title = cut < t.size ? proven_u8str_view_trim((proven_u8str_view_t){ .ptr = t.ptr + cut, .size = t.size - cut })
                                     : (proven_u8str_view_t){ 0 };
    return true;
}
