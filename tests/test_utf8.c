#include "rubraview/utf8.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static u8str_t lit(const char *s) {
    return (u8str_t){ .ptr = s, .len = strlen(s) };
}

int main(void) {
    printf("[test_utf8] Starting strict UTF-8 decode/encode/validate unit tests...\n");

    /* Test 1: ASCII and valid multi-byte sequences */
    assert(rubraview_utf8_validate(lit("Hello, world!")));
    assert(rubraview_utf8_validate(lit("\xEC\x95\x88\xEB\x85\x95"))); /* "안녕" */
    assert(rubraview_utf8_validate(lit("\xF0\x9F\x93\x96")));         /* U+1F4D6 */
    assert(rubraview_utf8_validate((u8str_t){ .ptr = "", .len = 0 }));
    printf("  [PASS] Valid ASCII, Hangul, and 4-byte emoji sequences\n");

    /* Test 2: Overlong encodings rejected */
    const uint8_t overlong2[] = { 0xC0, 0x80 }; /* overlong NUL */
    size_t n;
    assert(rubraview_utf8_decode(overlong2, 2, &n) == RUBRAVIEW_UTF8_INVALID);
    const uint8_t overlong3[] = { 0xE0, 0x80, 0x80 };
    assert(rubraview_utf8_decode(overlong3, 3, &n) == RUBRAVIEW_UTF8_INVALID);
    printf("  [PASS] Overlong encodings rejected\n");

    /* Test 3: Encoded surrogate halves rejected */
    const uint8_t surrogate[] = { 0xED, 0xA0, 0x80 }; /* U+D800 */
    assert(rubraview_utf8_decode(surrogate, 3, &n) == RUBRAVIEW_UTF8_INVALID);
    printf("  [PASS] Surrogate-half encodings rejected\n");

    /* Test 4: Codepoints beyond U+10FFFF rejected */
    const uint8_t too_big[] = { 0xF4, 0x90, 0x80, 0x80 };
    assert(rubraview_utf8_decode(too_big, 4, &n) == RUBRAVIEW_UTF8_INVALID);
    printf("  [PASS] Codepoints beyond U+10FFFF rejected\n");

    /* Test 5: Truncated sequences rejected */
    const uint8_t truncated[] = { 0xE2, 0x82 }; /* missing 3rd byte of a 3-byte sequence */
    assert(rubraview_utf8_decode(truncated, 2, &n) == RUBRAVIEW_UTF8_INVALID);
    assert(!rubraview_utf8_validate((u8str_t){ .ptr = (const char*)truncated, .len = 2 }));
    printf("  [PASS] Truncated sequences rejected\n");

    /* Test 6: Bare continuation byte rejected */
    const uint8_t bare_cont[] = { 0x80 };
    assert(rubraview_utf8_decode(bare_cont, 1, &n) == RUBRAVIEW_UTF8_INVALID);
    printf("  [PASS] Bare continuation byte rejected\n");

    /* Test 7: Round-trip encode/decode across all four lengths */
    uint32_t sample_codepoints[] = { 0x24, 0xA9, 0x1112, 0xAC00, 0x1F600 };
    for (size_t i = 0; i < sizeof(sample_codepoints) / sizeof(sample_codepoints[0]); ++i) {
        uint8_t buf[8];
        size_t pos = 0;
        size_t written = rubraview_utf8_encode(sample_codepoints[i], buf, sizeof(buf), &pos);
        assert(written > 0);
        assert(pos == written);
        size_t consumed = 0;
        uint32_t decoded = rubraview_utf8_decode(buf, written, &consumed);
        assert(decoded == sample_codepoints[i]);
        assert(consumed == written);
    }
    printf("  [PASS] Round-trip encode/decode across 1-4 byte codepoints\n");

    /* Test 8: Encode rejects surrogate halves and codepoints beyond U+10FFFF */
    uint8_t buf[8];
    size_t pos = 0;
    assert(rubraview_utf8_encode(0xD800, buf, sizeof(buf), &pos) == 0);
    assert(rubraview_utf8_encode(0x110000, buf, sizeof(buf), &pos) == 0);
    assert(pos == 0);
    printf("  [PASS] Encode rejects surrogate halves and out-of-range codepoints\n");

    /* Test 9: Encode respects destination capacity */
    uint8_t small_buf[2];
    size_t small_pos = 0;
    assert(rubraview_utf8_encode(0x1F600, small_buf, sizeof(small_buf), &small_pos) == 0);
    printf("  [PASS] Encode refuses to overflow a bounded buffer\n");

    printf("[test_utf8] All tests passed successfully!\n");
    return 0;
}
