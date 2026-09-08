#include "rubraview/encoding.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static u8str_t lit(const char *s) {
    return (u8str_t){ .ptr = s, .len = strlen(s) };
}

int main(void) {
    printf("[test_encoding] Starting archive filename encoding detection unit tests...\n");

    /* Test 1: UTF-8 flag set -> trusted without validation, even for byte
       sequences that would otherwise fail strict validation (the flag is
       authoritative per §3.8.3 step 1). */
    const uint8_t invalid_bytes[] = { 0xFF, 0xFE };
    u8str_t garbage = { .ptr = (const char*)invalid_bytes, .len = sizeof(invalid_bytes) };
    assert(rubraview_archive_filename_detect(garbage, true) == RUBRAVIEW_ENCODING_UTF8_FLAGGED);
    printf("  [PASS] UTF-8 flag set is trusted without validation\n");

    /* Test 2: Flag unset, valid UTF-8 bytes (e.g. Korean filename) -> UTF8_VALID. */
    u8str_t korean_name = lit("\xED\x91\x9C\xEC\xA7\x80.jpg"); /* "표지.jpg" */
    assert(rubraview_archive_filename_detect(korean_name, false) == RUBRAVIEW_ENCODING_UTF8_VALID);
    printf("  [PASS] Flag unset but bytes validate as UTF-8\n");

    /* Test 3: Flag unset, invalid UTF-8 bytes (raw CP949 bytes for "가나") ->
       NEEDS_FALLBACK. 0xB0/0xB1 fall in the UTF-8 continuation-byte range
       (0x80-0xBF), so they are never a legal UTF-8 lead byte. */
    const uint8_t cp949_like[] = { 0xB0, 0xA1, 0xB1, 0xE6 };
    u8str_t legacy_name = { .ptr = (const char*)cp949_like, .len = sizeof(cp949_like) };
    assert(rubraview_archive_filename_detect(legacy_name, false) == RUBRAVIEW_ENCODING_NEEDS_FALLBACK);
    printf("  [PASS] Flag unset and invalid UTF-8 bytes need code-page fallback\n");

    /* Test 4: Flag unset, plain ASCII -> UTF8_VALID (ASCII is always valid UTF-8). */
    assert(rubraview_archive_filename_detect(lit("page001.png"), false) == RUBRAVIEW_ENCODING_UTF8_VALID);
    printf("  [PASS] Plain ASCII validates as UTF-8\n");

    /* Test 5: §3.8.3 step 3 — AUTO transcodes only bytes that failed
       both earlier steps, using the host's own code page. */
    {
        uint32_t cp = 0xDEAD;
        assert(rubraview_archive_filename_plan(legacy_name, false, RUBRAVIEW_CODEPAGE_AUTO, &cp));
        assert(cp == 0); /* 0 means the host's active code page */

        assert(!rubraview_archive_filename_plan(korean_name, false, RUBRAVIEW_CODEPAGE_AUTO, &cp));
        assert(!rubraview_archive_filename_plan(garbage, true, RUBRAVIEW_CODEPAGE_AUTO, &cp));
    }
    printf("  [PASS] AUTO transcodes only what failed the flag and the validator\n");

    /* Test 6: An explicit override overrules the byte evidence, which is
       what the menu entry exists for — a mis-flagged archive. */
    {
        uint32_t cp = 0;
        assert(rubraview_archive_filename_plan(korean_name, true, RUBRAVIEW_CODEPAGE_KOREAN, &cp));
        assert(cp == 949);

        assert(rubraview_archive_filename_plan(korean_name, false, RUBRAVIEW_CODEPAGE_JAPANESE, &cp));
        assert(cp == 932);

        /* Forcing UTF-8 means "take the bytes as they are", even when
           they do not validate. */
        assert(!rubraview_archive_filename_plan(legacy_name, false, RUBRAVIEW_CODEPAGE_UTF8, &cp));
    }
    printf("  [PASS] An explicit override overrules the byte evidence\n");

    /* Test 7: Every override in §3.8.3's list maps to its code page and
       carries a label for the menu tile. */
    {
        assert(rubraview_codepage_id(RUBRAVIEW_CODEPAGE_KOREAN) == 949);
        assert(rubraview_codepage_id(RUBRAVIEW_CODEPAGE_JAPANESE) == 932);
        assert(rubraview_codepage_id(RUBRAVIEW_CODEPAGE_SIMPLIFIED_CHINESE) == 936);
        assert(rubraview_codepage_id(RUBRAVIEW_CODEPAGE_TRADITIONAL_CHINESE) == 950);
        assert(rubraview_codepage_id(RUBRAVIEW_CODEPAGE_WESTERN) == 1252);
        assert(rubraview_codepage_id(RUBRAVIEW_CODEPAGE_UTF8) == 65001);
        assert(rubraview_codepage_id(RUBRAVIEW_CODEPAGE_AUTO) == 0);

        for (int i = RUBRAVIEW_CODEPAGE_AUTO; i <= RUBRAVIEW_CODEPAGE_WESTERN; ++i) {
            assert(rubraview_codepage_label((rubraview_codepage_t)i).len > 0);
        }
    }
    printf("  [PASS] Every override maps to its code page and has a menu label\n");

    printf("[test_encoding] All tests passed successfully!\n");
    return 0;
}
