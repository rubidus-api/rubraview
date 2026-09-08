#include "rubraview/glob.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static u8str_t lit(const char *s) {
    return (u8str_t){ .ptr = s, .len = strlen(s) };
}

int main(void) {
    printf("[test_glob] Starting glob pattern matching unit tests...\n");

    /* Test 1: Simple extension wildcard */
    assert(rubraview_glob_match(lit("photo.jpg"), lit("*.jpg")));
    assert(!rubraview_glob_match(lit("photo.png"), lit("*.jpg")));
    printf("  [PASS] Simple '*' extension wildcard\n");

    /* Test 2: Case-insensitive matching */
    assert(rubraview_glob_match(lit("PHOTO.JPG"), lit("*.jpg")));
    assert(rubraview_glob_match(lit("Scan_4K.PNG"), lit("*_4k.*")));
    printf("  [PASS] Case-insensitive matching\n");

    /* Test 3: '?' matches exactly one character */
    assert(rubraview_glob_match(lit("img1.jpg"), lit("img?.jpg")));
    assert(!rubraview_glob_match(lit("img10.jpg"), lit("img?.jpg")));
    printf("  [PASS] '?' matches exactly one character\n");

    /* Test 4: Multiple '*' segments */
    assert(rubraview_glob_match(lit("chapter_01_page_042.png"), lit("chapter_*_page_*.png")));
    assert(!rubraview_glob_match(lit("chapter_01.png"), lit("chapter_*_page_*.png")));
    printf("  [PASS] Multiple '*' segments\n");

    /* Test 5: Exact match with no wildcards */
    assert(rubraview_glob_match(lit("readme"), lit("readme")));
    assert(!rubraview_glob_match(lit("readme2"), lit("readme")));
    printf("  [PASS] Exact match with no wildcards\n");

    /* Test 6: ';'-separated pattern list, first-match-wins */
    u8str_t list = lit("*.jpg;*.png;*.webp");
    assert(rubraview_glob_match_list(lit("cover.png"), list));
    assert(rubraview_glob_match_list(lit("page.webp"), list));
    assert(!rubraview_glob_match_list(lit("clip.mp4"), list));
    printf("  [PASS] ';'-separated pattern list matching\n");

    /* Test 7: Whitespace around ';' is trimmed */
    assert(rubraview_glob_match_list(lit("cover.png"), lit(" *.jpg ; *.png ")));
    printf("  [PASS] Whitespace trimmed around ';' separators\n");

    /* Test 8: Empty pattern list matches everything (no filter configured) */
    assert(rubraview_glob_match_list(lit("anything.xyz"), (u8str_t){ .ptr = "", .len = 0 }));
    printf("  [PASS] Empty pattern list matches everything\n");

    printf("[test_glob] All tests passed successfully!\n");
    return 0;
}
