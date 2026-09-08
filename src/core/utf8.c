#include "rubraview/utf8.h"

uint32_t rubraview_utf8_decode(const uint8_t *s, size_t len, size_t *out_len) {
    if (out_len) *out_len = 1;
    if (!s || len == 0) return RUBRAVIEW_UTF8_INVALID;

    uint8_t b0 = s[0];

    if (b0 < 0x80) {
        return b0;
    }

    if (b0 < 0xC2 || b0 > 0xF4) {
        /* Continuation byte, overlong two-byte lead (0xC0/0xC1), or byte > 0xF4 */
        return RUBRAVIEW_UTF8_INVALID;
    }

    size_t need;
    uint32_t cp;
    uint8_t lo1 = 0x80, hi1 = 0xBF; /* valid range for the second byte, tightened below */

    if (b0 < 0xE0) {
        need = 1;
        cp = (uint32_t)(b0 & 0x1Fu);
    } else if (b0 < 0xF0) {
        need = 2;
        cp = (uint32_t)(b0 & 0x0Fu);
        if (b0 == 0xE0) lo1 = 0xA0;      /* reject overlong 3-byte */
        else if (b0 == 0xED) hi1 = 0x9F; /* reject encoded surrogate halves */
    } else {
        need = 3;
        cp = (uint32_t)(b0 & 0x07u);
        if (b0 == 0xF0) lo1 = 0x90;      /* reject overlong 4-byte */
        else if (b0 == 0xF4) hi1 = 0x8F; /* reject codepoints beyond U+10FFFF */
    }

    if (len < need + 1) return RUBRAVIEW_UTF8_INVALID;

    for (size_t i = 0; i < need; ++i) {
        uint8_t b = s[1 + i];
        uint8_t lo = (i == 0) ? lo1 : 0x80;
        uint8_t hi = (i == 0) ? hi1 : 0xBF;
        if (b < lo || b > hi) return RUBRAVIEW_UTF8_INVALID;
        cp = (cp << 6) | (uint32_t)(b & 0x3Fu);
    }

    if (out_len) *out_len = need + 1;
    return cp;
}

bool rubraview_utf8_validate(u8str_t bytes) {
    if (bytes.len == 0) return true;
    if (!bytes.ptr) return false;

    size_t i = 0;
    while (i < bytes.len) {
        size_t consumed = 1;
        uint32_t cp = rubraview_utf8_decode((const uint8_t*)bytes.ptr + i, bytes.len - i, &consumed);
        if (cp == RUBRAVIEW_UTF8_INVALID) return false;
        i += consumed;
    }
    return true;
}

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
