#ifndef RUBRAVIEW_UTF8_H
#define RUBRAVIEW_UTF8_H

#include "rubraview/core.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Shared UTF-8 primitives used by RV-024 (NFC fold) and RV-026 (archive
 * filename encoding detection): strict decode/encode and validation.
 */

#define RUBRAVIEW_UTF8_INVALID ((uint32_t)0xFFFFFFFFu)

/**
 * Decode one codepoint starting at s[0]. On success returns the codepoint
 * and writes the number of bytes consumed (1-4) to *out_len. On any
 * malformed sequence (bad leading byte, truncated input, missing/extra
 * continuation bytes, overlong encoding, surrogate half, or a codepoint
 * beyond U+10FFFF) returns RUBRAVIEW_UTF8_INVALID and *out_len is set to 1
 * so callers can skip forward without looping.
 */
uint32_t rubraview_utf8_decode(const uint8_t *s, size_t len, size_t *out_len);

/**
 * Strict UTF-8 validation: every byte must belong to exactly one
 * well-formed, minimal-length sequence with no surrogate halves and no
 * codepoint above U+10FFFF. Empty input is valid.
 */
bool rubraview_utf8_validate(u8str_t bytes);

/**
 * Append the UTF-8 encoding of a codepoint (U+0000..U+10FFFF, excluding
 * surrogate halves) to dst at *inout_pos, bounded by dst_cap. Returns the
 * number of bytes written (1-4), or 0 if the codepoint is invalid or the
 * buffer has insufficient room; *inout_pos is unchanged on failure.
 */
size_t rubraview_utf8_encode(uint32_t codepoint, uint8_t *dst, size_t dst_cap, size_t *inout_pos);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_UTF8_H */
