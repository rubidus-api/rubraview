#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "rubraview/sort.h"

static void test_natural_comparison(void) {
    u8str_t s1   = U8("scan (1).jpg");
    u8str_t s2   = U8("scan (2).jpg");
    u8str_t s9   = U8("scan (9).jpg");
    u8str_t s10  = U8("scan (10).jpg");
    u8str_t s99  = U8("scan (99).jpg");
    u8str_t s100 = U8("scan (100).jpg");

    // Natural sort order: 1 < 2 < 9 < 10 < 99 < 100
    assert(rv_str_natcmp(s1, s2) < 0);
    assert(rv_str_natcmp(s2, s9) < 0);
    assert(rv_str_natcmp(s9, s10) < 0);
    assert(rv_str_natcmp(s10, s99) < 0);
    assert(rv_str_natcmp(s99, s100) < 0);

    // Symmetry
    assert(rv_str_natcmp(s100, s99) > 0);
    assert(rv_str_natcmp(s10, s9) > 0);
    assert(rv_str_natcmp(s1, s1) == 0);

    // Leading zeros
    u8str_t z1 = U8("img 1.png");
    u8str_t z01 = U8("img 01.png");
    assert(rv_str_natcmp(z1, z01) < 0);

    // Multi-number segments
    u8str_t ch1_p2 = U8("ch1_page2.png");
    u8str_t ch1_p10 = U8("ch1_page10.png");
    u8str_t ch2_p1 = U8("ch2_page1.png");
    assert(rv_str_natcmp(ch1_p2, ch1_p10) < 0);
    assert(rv_str_natcmp(ch1_p10, ch2_p1) < 0);

    printf("  [PASS] Natural numeric comparison (1 < 2 < 9 < 10 < 99 < 100)\n");
}

static void test_lexical_comparison(void) {
    u8str_t s1   = U8("scan (1).jpg");
    u8str_t s2   = U8("scan (2).jpg");
    u8str_t s10  = U8("scan (10).jpg");
    u8str_t s100 = U8("scan (100).jpg");

    // Lexical sort order: '1' < '10' < '100' < '2'
    assert(rv_str_lexcmp(s1, s10) < 0);
    assert(rv_str_lexcmp(s10, s100) < 0);
    assert(rv_str_lexcmp(s100, s2) < 0); // '1' comes before '2'
    assert(rv_str_lexcmp(s2, s10) > 0);

    printf("  [PASS] Strict lexical comparison (1 < 10 < 100 < 2)\n");
}

static void test_path_array_sorting(void) {
    u8str_t files[6] = {
        U8("page (100).jpg"),
        U8("page (2).jpg"),
        U8("page (1).jpg"),
        U8("page (10).jpg"),
        U8("page (99).jpg"),
        U8("page (9).jpg"),
    };

    // 1. Natural Sort Ascending
    rv_sort_paths(files, 6, RV_SORT_NAME_NATURAL, true);
    assert(strcmp(files[0].ptr, "page (1).jpg") == 0);
    assert(strcmp(files[1].ptr, "page (2).jpg") == 0);
    assert(strcmp(files[2].ptr, "page (9).jpg") == 0);
    assert(strcmp(files[3].ptr, "page (10).jpg") == 0);
    assert(strcmp(files[4].ptr, "page (99).jpg") == 0);
    assert(strcmp(files[5].ptr, "page (100).jpg") == 0);
    printf("  [PASS] Array sorting via RV_SORT_NAME_NATURAL (ascending)\n");

    // 2. Natural Sort Descending
    rv_sort_paths(files, 6, RV_SORT_NAME_NATURAL, false);
    assert(strcmp(files[0].ptr, "page (100).jpg") == 0);
    assert(strcmp(files[1].ptr, "page (99).jpg") == 0);
    assert(strcmp(files[2].ptr, "page (10).jpg") == 0);
    assert(strcmp(files[3].ptr, "page (9).jpg") == 0);
    assert(strcmp(files[4].ptr, "page (2).jpg") == 0);
    assert(strcmp(files[5].ptr, "page (1).jpg") == 0);
    printf("  [PASS] Array sorting via RV_SORT_NAME_NATURAL (descending)\n");

    // 3. Lexical Sort Ascending
    rv_sort_paths(files, 6, RV_SORT_NAME_LEXICAL, true);
    assert(strcmp(files[0].ptr, "page (1).jpg") == 0);
    assert(strcmp(files[1].ptr, "page (10).jpg") == 0);
    assert(strcmp(files[2].ptr, "page (100).jpg") == 0);
    assert(strcmp(files[3].ptr, "page (2).jpg") == 0);
    assert(strcmp(files[4].ptr, "page (9).jpg") == 0);
    assert(strcmp(files[5].ptr, "page (99).jpg") == 0);
    printf("  [PASS] Array sorting via RV_SORT_NAME_LEXICAL (ascending)\n");
}

int main(void) {
    printf("[test_sort] Starting sorting algorithm unit tests...\n");
    test_natural_comparison();
    test_lexical_comparison();
    test_path_array_sorting();
    printf("[test_sort] All tests passed successfully!\n");
    return 0;
}
