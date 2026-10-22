#include "rubraview/utf8.h"
#include "proven/utf.h"

/* RV-159: proven's decoder, the one in the program; an invalid byte still moves the caller on by one. */
uint32_t rubraview_utf8_decode(const uint8_t *s, size_t len, size_t *out_len) {
    if (out_len) *out_len = 1;
    if (!s || len == 0) return RUBRAVIEW_UTF8_INVALID;
    proven_utf8_char_t c = proven_utf8_decode_next(proven_u8str_view_from_bytes(s, len), 0);
    if (c.err != PROVEN_OK) return RUBRAVIEW_UTF8_INVALID;
    if (out_len) *out_len = c.len;
    return c.cp;
}
