#include "rubraview/nfc.h"
#include "rubraview/utf8.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static u8str_t lit(const char *s) {
    return (u8str_t){ .ptr = s, .len = strlen(s) };
}

static bool str_eq_bytes(u8str_t s, const char *l, size_t l_len) {
    return s.len == l_len && (l_len == 0 || memcmp(s.ptr, l, l_len) == 0);
}

static bool str_eq(u8str_t s, const char *l) {
    return str_eq_bytes(s, l, strlen(l));
}

/* Append the UTF-8 encoding of a codepoint to a growable-by-caller buffer. */
static size_t append_cp(uint8_t *buf, size_t pos, uint32_t cp) {
    rubraview_utf8_encode(cp, buf, 64, &pos);
    return pos;
}

int main(void) {
    printf("[test_nfc] Starting Unicode NFC fold unit tests...\n");

    size_t mem_size = 64 * 1024;
    void *raw_mem = malloc(mem_size);
    assert(raw_mem != NULL);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw_mem, .size = mem_size });

    /* Test 1: Plain ASCII passes through unchanged. */
    u8str_t ascii_out = rubraview_nfc_fold(&arena, lit("chapter_01_page_010.png"));
    assert(str_eq(ascii_out, "chapter_01_page_010.png"));
    printf("  [PASS] Plain ASCII passes through unchanged\n");

    /* Test 2: Decomposed Hangul "\355\225\234" (han, U+D55C = ROOT.NET's RFC example)
       built from Jamo ㅎ(U+1112) + ㅏ(U+1161) + ㄴ(U+11AB) composes to the single
       precomposed syllable. Expected bytes verified independently in test_utf8. */
    {
        uint8_t jamo_buf[16];
        size_t pos = 0;
        pos = append_cp(jamo_buf, pos, 0x1112); /* Lead: ㅎ */
        pos = append_cp(jamo_buf, pos, 0x1161); /* Vowel: ㅏ */
        pos = append_cp(jamo_buf, pos, 0x11AB); /* Trail: ㄴ */
        u8str_t jamo_input = { .ptr = (const char*)jamo_buf, .len = pos };

        u8str_t composed = rubraview_nfc_fold(&arena, jamo_input);
        const char *expected_han = "\xED\x95\x9C"; /* U+D55C, precomposed "\355\225\234" */
        assert(str_eq_bytes(composed, expected_han, 3));
    }
    printf("  [PASS] Hangul Lead+Vowel+Trail Jamo compose to one precomposed syllable\n");

    /* Test 3: Two full syllables "\354\225\210\353\205\225" (annyeong start) from
       decomposed Jamo, matching the precomposed bytes independently verified in
       test_utf8.c ("\xEC\x95\x88\xEB\x85\x95"). */
    {
        uint8_t jamo_buf[32];
        size_t pos = 0;
        pos = append_cp(jamo_buf, pos, 0x110B); /* Lead: ieung (silent) */
        pos = append_cp(jamo_buf, pos, 0x1161); /* Vowel: a */
        pos = append_cp(jamo_buf, pos, 0x11AB); /* Trail: n */
        pos = append_cp(jamo_buf, pos, 0x1102); /* Lead: n */
        pos = append_cp(jamo_buf, pos, 0x1167); /* Vowel: yeo */
        pos = append_cp(jamo_buf, pos, 0x11BC); /* Trail: ng */
        u8str_t jamo_input = { .ptr = (const char*)jamo_buf, .len = pos };

        u8str_t composed = rubraview_nfc_fold(&arena, jamo_input);
        const char *expected = "\xEC\x95\x88\xEB\x85\x95";
        assert(str_eq_bytes(composed, expected, 6));
    }
    printf("  [PASS] Two-syllable decomposed sequence composes correctly (macOS HFS+/APFS NFD case)\n");

    /* Test 4: A precomposed LV syllable (no trailing consonant) followed by a
       standalone Trailing Jamo also composes: "\352\260\200"+trailing-g -> "\352\260\201". */
    {
        uint8_t buf[16];
        size_t pos = 0;
        pos = append_cp(buf, pos, 0xAC00); /* precomposed "\352\260\200" (L=0,V=0,T=0) */
        pos = append_cp(buf, pos, 0x11A8); /* Trail: g (T index 1) */
        u8str_t input = { .ptr = (const char*)buf, .len = pos };

        u8str_t composed = rubraview_nfc_fold(&arena, input);
        uint8_t expected_buf[8];
        size_t exp_pos = append_cp(expected_buf, 0, 0xAC01); /* "\352\260\201" */
        assert(str_eq_bytes(composed, (const char*)expected_buf, exp_pos));
    }
    printf("  [PASS] Precomposed LV syllable + trailing Jamo composes to LVT syllable\n");

    /* Test 5: Latin base + combining acute accent composes to precomposed e-acute. */
    {
        uint8_t buf[8];
        size_t pos = 0;
        buf[pos++] = 'e';
        pos = append_cp(buf, pos, 0x0301); /* combining acute accent */
        u8str_t input = { .ptr = (const char*)buf, .len = pos };

        u8str_t composed = rubraview_nfc_fold(&arena, input);
        const char *expected_e_acute = "\xC3\xA9"; /* U+00E9 */
        assert(str_eq_bytes(composed, expected_e_acute, 2));
    }
    printf("  [PASS] Latin base letter + combining acute accent composes to precomposed form\n");

    /* Test 6: An uncomposable base+mark pair (no table entry) is left as two codepoints. */
    {
        uint8_t buf[8];
        size_t pos = 0;
        buf[pos++] = 'z'; /* 'z' has no entry in the curated table */
        pos = append_cp(buf, pos, 0x0301);
        u8str_t input = { .ptr = (const char*)buf, .len = pos };

        u8str_t out = rubraview_nfc_fold(&arena, input);
        assert(out.len == pos); /* unchanged length: no composition happened */
    }
    printf("  [PASS] Base letter with no table entry for its mark is left uncomposed\n");

    /* Test 7: Malformed UTF-8 byte is replaced with U+FFFD, not a crash. */
    {
        uint8_t bad[] = { 'a', 0xFF, 'b' }; /* 0xFF is never a valid UTF-8 lead byte */
        u8str_t input = { .ptr = (const char*)bad, .len = sizeof(bad) };
        u8str_t out = rubraview_nfc_fold(&arena, input);

        uint8_t expected_buf[8];
        size_t exp_pos = 0;
        expected_buf[exp_pos++] = 'a';
        exp_pos = append_cp(expected_buf, exp_pos, 0xFFFD);
        expected_buf[exp_pos++] = 'b';
        assert(str_eq_bytes(out, (const char*)expected_buf, exp_pos));
    }
    printf("  [PASS] Malformed UTF-8 byte replaced with U+FFFD instead of crashing\n");

    /* Test 8: Result carries the null-terminated allocation invariant. */
    u8str_t nul_check = rubraview_nfc_fold(&arena, lit("test"));
    assert(nul_check.ptr[nul_check.len] == '\0');
    printf("  [PASS] Result carries the null-terminated allocation invariant\n");

    /* Test 9: Empty input returns an empty slice, not a crash. */
    u8str_t empty_out = rubraview_nfc_fold(&arena, (u8str_t){ .ptr = "", .len = 0 });
    assert(empty_out.len == 0);
    printf("  [PASS] Empty input handled without crashing\n");

    free(raw_mem);
    printf("[test_nfc] All tests passed successfully!\n");
    return 0;
}
