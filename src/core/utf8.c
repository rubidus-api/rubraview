#include "rubraview/utf8.h"
#include "proven/utf.h"

/* RV-159: proven's decoder, the one in the program; an invalid byte still moves the caller on by one. */
uint32_t rubraview_utf8_decode(const uint8_t *s, size_t len, size_t *out_len) {
    if (out_len) *out_len = 1;
    if (!s || len == 0) return RUBRAVIEW_UTF8_INVALID;
    proven_utf8_char_t c = proven_utf8_decode_next(rubraview_u8(s, len), 0);
    if (c.err != PROVEN_OK) return RUBRAVIEW_UTF8_INVALID;
    if (out_len) *out_len = c.len;
    return c.cp;
}

bool rubraview_utf8_validate(proven_u8str_view_t bytes) {
    if (bytes.size == 0) return true;
    if (!bytes.ptr) return false;
    for (proven_size_t pos = 0; pos < bytes.size;) {
        proven_utf8_char_t c = proven_utf8_decode_next(bytes, pos);
        if (c.err != PROVEN_OK) return false;
        pos += c.len;
    }
    return true;
}

/* Not in proven yet (asked of proven_c_lib, 2026-10-22): one character written. */
size_t rubraview_utf8_encode(uint32_t codepoint, uint8_t *dst, size_t dst_cap, size_t *inout_pos) {
    if (!dst || !inout_pos) return 0;
    if (codepoint > 0x10FFFFu) return 0;
    if (codepoint >= 0xD800u && codepoint <= 0xDFFFu) return 0;

    size_t pos = *inout_pos;
    size_t need;
    if (codepoint < 0x80u) need = 1;
    else if (codepoint < 0x800u) need = 2;
    else if (codepoint < 0x10000u) need = 3;
    else need = 4;

    if (pos + need > dst_cap) return 0;

    switch (need) {
        case 1:
            dst[pos] = (uint8_t)codepoint;
            break;
        case 2:
            dst[pos]     = (uint8_t)(0xC0u | (codepoint >> 6));
            dst[pos + 1] = (uint8_t)(0x80u | (codepoint & 0x3Fu));
            break;
        case 3:
            dst[pos]     = (uint8_t)(0xE0u | (codepoint >> 12));
            dst[pos + 1] = (uint8_t)(0x80u | ((codepoint >> 6) & 0x3Fu));
            dst[pos + 2] = (uint8_t)(0x80u | (codepoint & 0x3Fu));
            break;
        default:
            dst[pos]     = (uint8_t)(0xF0u | (codepoint >> 18));
            dst[pos + 1] = (uint8_t)(0x80u | ((codepoint >> 12) & 0x3Fu));
            dst[pos + 2] = (uint8_t)(0x80u | ((codepoint >> 6) & 0x3Fu));
            dst[pos + 3] = (uint8_t)(0x80u | (codepoint & 0x3Fu));
            break;
    }

    *inout_pos = pos + need;
    return need;
}
