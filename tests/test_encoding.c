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

    printf("[test_encoding] All tests passed successfully!\n");
    return 0;
}
