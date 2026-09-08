#ifndef RUBRAVIEW_NFC_H
#define RUBRAVIEW_NFC_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Unicode NFC (Normalization Form C) folding pass (RFC-0001 §3.8.4).
 *
 * Two composition families, run in a single left-to-right pass:
 *  1. Hangul syllable composition (Unicode §3.12), fully algorithmic:
 *     S = SBase + (L * VCount + V) * TCount + T. Composes L+V(+T) Jamo
 *     sequences, and a precomposed LV syllable followed by a trailing-
 *     consonant Jamo, into a single precomposed syllable. Pure integer
 *     arithmetic — no big lookup table.
 *  2. A curated table of common Latin base letters (a-z, A-Z of
 *     a/e/i/o/u/n/c/y) followed by a combining diacritical mark
 *     (U+0300..U+030A, U+0327) into their standard precomposed form
 *     (e.g. "e"+U+0301 -> "é"). This is intentionally the common subset
 *     named in §3.8.4, not the full Unicode NFC decomposition table.
 *
 * Malformed UTF-8 in the input is replaced codepoint-by-codepoint with
 * U+FFFD (one byte consumed per replacement) rather than failing, since
 * this runs over externally supplied filenames.
 *
 * The composition arithmetic itself performs zero allocation; the
 * returned buffer is a fresh, right-sized, null-terminated arena
 * allocation (folding can shrink byte length, so it cannot be done
 * losslessly in place on the source bytes).
 */
u8str_t rubraview_nfc_fold(proven_arena_t *arena, u8str_t utf8_in);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_NFC_H */
