#include "rubraview/nfc.h"
#include "rubraview/utf8.h"
#include <string.h>

/* Hangul Jamo composition constants (Unicode §3.12). */
#define HANGUL_SBASE 0xAC00u
#define HANGUL_LBASE 0x1100u
#define HANGUL_VBASE 0x1161u
#define HANGUL_TBASE 0x11A7u
#define HANGUL_LCOUNT 19u
#define HANGUL_VCOUNT 21u
#define HANGUL_TCOUNT 28u
#define HANGUL_NCOUNT (HANGUL_VCOUNT * HANGUL_TCOUNT)
#define HANGUL_SCOUNT (HANGUL_LCOUNT * HANGUL_NCOUNT)

typedef struct latin_pair {
    uint32_t base;
    uint32_t mark;
    uint32_t composed;
} latin_pair_t;

/* Common Latin base + combining diacritical mark -> precomposed form.
   Intentionally a curated subset (vowels + n/c/y), not the full Unicode
   NFC decomposition table — see the header comment. */
static const latin_pair_t LATIN_PAIRS[] = {
    { 'a', 0x0300, 0xE0 }, { 'a', 0x0301, 0xE1 }, { 'a', 0x0302, 0xE2 }, { 'a', 0x0303, 0xE3 }, { 'a', 0x0308, 0xE4 }, { 'a', 0x030A, 0xE5 },
    { 'e', 0x0300, 0xE8 }, { 'e', 0x0301, 0xE9 }, { 'e', 0x0302, 0xEA }, { 'e', 0x0308, 0xEB },
    { 'i', 0x0300, 0xEC }, { 'i', 0x0301, 0xED }, { 'i', 0x0302, 0xEE }, { 'i', 0x0308, 0xEF },
    { 'o', 0x0300, 0xF2 }, { 'o', 0x0301, 0xF3 }, { 'o', 0x0302, 0xF4 }, { 'o', 0x0303, 0xF5 }, { 'o', 0x0308, 0xF6 },
    { 'u', 0x0300, 0xF9 }, { 'u', 0x0301, 0xFA }, { 'u', 0x0302, 0xFB }, { 'u', 0x0308, 0xFC },
    { 'n', 0x0303, 0xF1 }, { 'c', 0x0327, 0xE7 },
    { 'y', 0x0301, 0xFD }, { 'y', 0x0308, 0xFF },
    { 'A', 0x0300, 0xC0 }, { 'A', 0x0301, 0xC1 }, { 'A', 0x0302, 0xC2 }, { 'A', 0x0303, 0xC3 }, { 'A', 0x0308, 0xC4 }, { 'A', 0x030A, 0xC5 },
    { 'E', 0x0300, 0xC8 }, { 'E', 0x0301, 0xC9 }, { 'E', 0x0302, 0xCA }, { 'E', 0x0308, 0xCB },
    { 'I', 0x0300, 0xCC }, { 'I', 0x0301, 0xCD }, { 'I', 0x0302, 0xCE }, { 'I', 0x0308, 0xCF },
    { 'O', 0x0300, 0xD2 }, { 'O', 0x0301, 0xD3 }, { 'O', 0x0302, 0xD4 }, { 'O', 0x0303, 0xD5 }, { 'O', 0x0308, 0xD6 },
    { 'U', 0x0300, 0xD9 }, { 'U', 0x0301, 0xDA }, { 'U', 0x0302, 0xDB }, { 'U', 0x0308, 0xDC },
    { 'N', 0x0303, 0xD1 }, { 'C', 0x0327, 0xC7 },
    { 'Y', 0x0301, 0xDD }, { 'Y', 0x0308, 0x178 },
};
#define LATIN_PAIR_COUNT (sizeof(LATIN_PAIRS) / sizeof(LATIN_PAIRS[0]))

static bool latin_compose(uint32_t base, uint32_t mark, uint32_t *out_composed) {
    for (size_t i = 0; i < LATIN_PAIR_COUNT; ++i) {
        if (LATIN_PAIRS[i].base == base && LATIN_PAIRS[i].mark == mark) {
            *out_composed = LATIN_PAIRS[i].composed;
            return true;
        }
    }
    return false;
}

u8str_t rubraview_nfc_fold(proven_arena_t *arena, u8str_t utf8_in) {
    if (!arena) return (u8str_t){ .ptr = "", .len = 0 };
    if (utf8_in.len == 0) return (u8str_t){ .ptr = "", .len = 0 };

    /* Pass 1: decode to codepoints. Upper bound: one codepoint per byte. */
    proven_result_mem_mut_t decoded_res = rubraview_arena_alloc_array(arena, utf8_in.len, sizeof(uint32_t));
    if (!proven_is_ok(decoded_res.err)) return (u8str_t){ .ptr = "", .len = 0 };
    uint32_t *decoded = (uint32_t*)(void*)decoded_res.value.ptr;
    size_t decoded_count = 0;

    size_t i = 0;
    while (i < utf8_in.len) {
        size_t consumed = 1;
        uint32_t cp = rubraview_utf8_decode((const uint8_t*)utf8_in.ptr + i, utf8_in.len - i, &consumed);
        if (cp == RUBRAVIEW_UTF8_INVALID) {
            decoded[decoded_count++] = 0xFFFD; /* replacement character */
        } else {
            decoded[decoded_count++] = cp;
        }
        i += consumed;
    }

    /* Pass 2: compose. Output is never longer than the input in codepoints. */
    proven_result_mem_mut_t composed_res = rubraview_arena_alloc_array(arena, decoded_count, sizeof(uint32_t));
    if (!proven_is_ok(composed_res.err)) return (u8str_t){ .ptr = "", .len = 0 };
    uint32_t *composed = (uint32_t*)(void*)composed_res.value.ptr;
    size_t composed_count = 0;

    size_t k = 0;
    while (k < decoded_count) {
        uint32_t cp = decoded[k];

        /* Hangul: Lead Jamo + Vowel Jamo (+ optional Trailing Jamo). */
        if (cp >= HANGUL_LBASE && cp < HANGUL_LBASE + HANGUL_LCOUNT &&
            k + 1 < decoded_count &&
            decoded[k + 1] >= HANGUL_VBASE && decoded[k + 1] < HANGUL_VBASE + HANGUL_VCOUNT) {
            uint32_t l_index = cp - HANGUL_LBASE;
            uint32_t v_index = decoded[k + 1] - HANGUL_VBASE;
            uint32_t lv_index = l_index * HANGUL_VCOUNT + v_index;
            uint32_t syllable = HANGUL_SBASE + lv_index * HANGUL_TCOUNT;
            size_t consumed_cps = 2;

            if (k + 2 < decoded_count &&
                decoded[k + 2] > HANGUL_TBASE && decoded[k + 2] < HANGUL_TBASE + HANGUL_TCOUNT) {
                syllable += decoded[k + 2] - HANGUL_TBASE;
                consumed_cps = 3;
            }

            composed[composed_count++] = syllable;
            k += consumed_cps;
            continue;
        }

        /* Hangul: a precomposed LV syllable (no trailing consonant yet) followed by a Trailing Jamo. */
        if (cp >= HANGUL_SBASE && cp < HANGUL_SBASE + HANGUL_SCOUNT &&
            ((cp - HANGUL_SBASE) % HANGUL_TCOUNT) == 0 &&
            k + 1 < decoded_count &&
            decoded[k + 1] > HANGUL_TBASE && decoded[k + 1] < HANGUL_TBASE + HANGUL_TCOUNT) {
            composed[composed_count++] = cp + (decoded[k + 1] - HANGUL_TBASE);
            k += 2;
            continue;
        }

        /* Latin base + combining diacritical mark. */
        if (k + 1 < decoded_count) {
            uint32_t out_cp;
            if (latin_compose(cp, decoded[k + 1], &out_cp)) {
                composed[composed_count++] = out_cp;
                k += 2;
                continue;
            }
        }

        composed[composed_count++] = cp;
        k += 1;
    }

    /* Pass 3: re-encode to UTF-8. Upper bound: 4 bytes per codepoint + NUL. */
    size_t max_bytes = composed_count * 4 + 1;
    proven_result_mem_mut_t out_res = proven_arena_alloc(arena, max_bytes);
    if (!proven_is_ok(out_res.err)) return (u8str_t){ .ptr = "", .len = 0 };

    size_t pos = 0;
    for (size_t c = 0; c < composed_count; ++c) {
        rubraview_utf8_encode(composed[c], out_res.value.ptr, max_bytes, &pos);
    }

    /* Shrink the tail allocation to the exact byte count (it is the most
       recent arena allocation, so this is an in-place, non-copying trim). */
    proven_result_mem_mut_t shrunk = proven_arena_realloc_aligned(
        arena, out_res.value.ptr, max_bytes, pos + 1, PROVEN_DEFAULT_ALIGNMENT);
    uint8_t *final_ptr = proven_is_ok(shrunk.err) ? shrunk.value.ptr : out_res.value.ptr;
    final_ptr[pos] = '\0';

    return (u8str_t){ .ptr = (const char*)final_ptr, .len = pos };
}
