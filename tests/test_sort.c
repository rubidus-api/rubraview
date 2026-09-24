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
    assert(rubraview_str_natcmp(s1, s2) < 0);
    assert(rubraview_str_natcmp(s2, s9) < 0);
    assert(rubraview_str_natcmp(s9, s10) < 0);
    assert(rubraview_str_natcmp(s10, s99) < 0);
    assert(rubraview_str_natcmp(s99, s100) < 0);

    // Symmetry
    assert(rubraview_str_natcmp(s100, s99) > 0);
    assert(rubraview_str_natcmp(s10, s9) > 0);
    assert(rubraview_str_natcmp(s1, s1) == 0);

    // Leading zeros
    u8str_t z1 = U8("img 1.png");
    u8str_t z01 = U8("img 01.png");
    assert(rubraview_str_natcmp(z1, z01) < 0);

    // Multi-number segments
    u8str_t ch1_p2 = U8("ch1_page2.png");
    u8str_t ch1_p10 = U8("ch1_page10.png");
    u8str_t ch2_p1 = U8("ch2_page1.png");
    assert(rubraview_str_natcmp(ch1_p2, ch1_p10) < 0);
    assert(rubraview_str_natcmp(ch1_p10, ch2_p1) < 0);

    printf("  [PASS] Natural numeric comparison (1 < 2 < 9 < 10 < 99 < 100)\n");
}

static void test_lexical_comparison(void) {
    u8str_t s1   = U8("scan (1).jpg");
    u8str_t s2   = U8("scan (2).jpg");
    u8str_t s10  = U8("scan (10).jpg");
    u8str_t s100 = U8("scan (100).jpg");

    // Lexical sort order: '1' < '10' < '100' < '2'
    assert(rubraview_str_lexcmp(s1, s10) < 0);
    assert(rubraview_str_lexcmp(s10, s100) < 0);
    assert(rubraview_str_lexcmp(s100, s2) < 0); // '1' comes before '2'
    assert(rubraview_str_lexcmp(s2, s10) > 0);

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
    rubraview_sort_paths(files, 6, RUBRAVIEW_SORT_NAME_NATURAL, true);
    assert(strcmp(files[0].ptr, "page (1).jpg") == 0);
    assert(strcmp(files[1].ptr, "page (2).jpg") == 0);
    assert(strcmp(files[2].ptr, "page (9).jpg") == 0);
    assert(strcmp(files[3].ptr, "page (10).jpg") == 0);
    assert(strcmp(files[4].ptr, "page (99).jpg") == 0);
    assert(strcmp(files[5].ptr, "page (100).jpg") == 0);
    printf("  [PASS] Array sorting via RUBRAVIEW_SORT_NAME_NATURAL (ascending)\n");

    // 2. Natural Sort Descending
    rubraview_sort_paths(files, 6, RUBRAVIEW_SORT_NAME_NATURAL, false);
    assert(strcmp(files[0].ptr, "page (100).jpg") == 0);
    assert(strcmp(files[1].ptr, "page (99).jpg") == 0);
    assert(strcmp(files[2].ptr, "page (10).jpg") == 0);
    assert(strcmp(files[3].ptr, "page (9).jpg") == 0);
    assert(strcmp(files[4].ptr, "page (2).jpg") == 0);
    assert(strcmp(files[5].ptr, "page (1).jpg") == 0);
    printf("  [PASS] Array sorting via RUBRAVIEW_SORT_NAME_NATURAL (descending)\n");

    // 3. Lexical Sort Ascending
    rubraview_sort_paths(files, 6, RUBRAVIEW_SORT_NAME_LEXICAL, true);
    assert(strcmp(files[0].ptr, "page (1).jpg") == 0);
    assert(strcmp(files[1].ptr, "page (10).jpg") == 0);
    assert(strcmp(files[2].ptr, "page (100).jpg") == 0);
    assert(strcmp(files[3].ptr, "page (2).jpg") == 0);
    assert(strcmp(files[4].ptr, "page (9).jpg") == 0);
    assert(strcmp(files[5].ptr, "page (99).jpg") == 0);
    printf("  [PASS] Array sorting via RUBRAVIEW_SORT_NAME_LEXICAL (ascending)\n");
}

static void test_multi_criteria_sort(void) {
    /* Date modified, ascending. */
    rubraview_sort_item_t by_date[3] = {
        { .name = U8("c.jpg"), .mtime = 300, .ctime = 300, .size_bytes = 10 },
        { .name = U8("a.jpg"), .mtime = 100, .ctime = 100, .size_bytes = 30 },
        { .name = U8("b.jpg"), .mtime = 200, .ctime = 200, .size_bytes = 20 },
    };
    rubraview_sort_items(by_date, 3, RUBRAVIEW_SORT_DATE_MODIFIED, true, NULL);
    assert(by_date[0].mtime == 100 && by_date[1].mtime == 200 && by_date[2].mtime == 300);
    printf("  [PASS] Multi-criteria sort by RUBRAVIEW_SORT_DATE_MODIFIED (ascending)\n");

    /* File size, descending. */
    rubraview_sort_item_t by_size[3] = {
        { .name = U8("c.jpg"), .mtime = 0, .ctime = 0, .size_bytes = 10 },
        { .name = U8("a.jpg"), .mtime = 0, .ctime = 0, .size_bytes = 30 },
        { .name = U8("b.jpg"), .mtime = 0, .ctime = 0, .size_bytes = 20 },
    };
    rubraview_sort_items(by_size, 3, RUBRAVIEW_SORT_FILE_SIZE, false, NULL);
    assert(by_size[0].size_bytes == 30 && by_size[1].size_bytes == 20 && by_size[2].size_bytes == 10);
    printf("  [PASS] Multi-criteria sort by RUBRAVIEW_SORT_FILE_SIZE (descending)\n");

    /* Date created is independent of date modified. */
    rubraview_sort_item_t by_ctime[2] = {
        { .name = U8("newer_created.jpg"), .mtime = 999, .ctime = 50, .size_bytes = 0 },
        { .name = U8("older_created.jpg"), .mtime = 1, .ctime = 100, .size_bytes = 0 },
    };
    rubraview_sort_items(by_ctime, 2, RUBRAVIEW_SORT_DATE_CREATED, true, NULL);
    assert(strcmp(by_ctime[0].name.ptr, "newer_created.jpg") == 0);
    printf("  [PASS] RUBRAVIEW_SORT_DATE_CREATED sorts independently of mtime\n");

    /* Seeded shuffle: same seed -> identical permutation every time
       (seed preservation for the slideshow "Previous" key). */
    rubraview_sort_item_t base[8];
    for (int i = 0; i < 8; ++i) {
        base[i] = (rubraview_sort_item_t){ .name = { .ptr = NULL, .len = 0 }, .mtime = i, .ctime = 0, .size_bytes = 0 };
    }
    rubraview_sort_item_t run_a[8], run_b[8];
    memcpy(run_a, base, sizeof(base));
    memcpy(run_b, base, sizeof(base));

    rubraview_shuffle_state_t seed = { .seed = 0xC0FFEEULL };
    rubraview_sort_items(run_a, 8, RUBRAVIEW_SORT_RANDOM, true, &seed);
    rubraview_sort_items(run_b, 8, RUBRAVIEW_SORT_RANDOM, true, &seed);
    for (int i = 0; i < 8; ++i) {
        assert(run_a[i].mtime == run_b[i].mtime);
    }
    printf("  [PASS] Seeded shuffle reproduces the identical permutation from the same seed\n");

    /* A different seed (very likely) produces a different order. */
    rubraview_sort_item_t run_c[8];
    memcpy(run_c, base, sizeof(base));
    rubraview_shuffle_state_t other_seed = { .seed = 0x1234ULL };
    rubraview_sort_items(run_c, 8, RUBRAVIEW_SORT_RANDOM, true, &other_seed);
    bool any_different = false;
    for (int i = 0; i < 8; ++i) {
        if (run_c[i].mtime != run_a[i].mtime) { any_different = true; break; }
    }
    assert(any_different);

    /* The shuffle is a true permutation: every original element appears
       exactly once (no duplication, nothing lost). */
    bool seen[8] = {0};
    for (int i = 0; i < 8; ++i) {
        assert(run_a[i].mtime >= 0 && run_a[i].mtime < 8);
        assert(!seen[run_a[i].mtime]);
        seen[run_a[i].mtime] = true;
    }
    printf("  [PASS] Different seeds diverge; shuffle output is a true permutation\n");
}

/* Every order is total. Photos copied from a camera share one modified
   time and a folder's files often share one size; the order among them
   must not depend on the order the filesystem listed them in, or the
   same folder opens on a different page each time. Ties go to the
   natural name, then the bytes of the name, then the listing order. */
static void test_ties_and_scale(void) {
    rubraview_sort_item_t a[4] = {
        { .name = U8("c.jpg"),  .mtime = 50, .tag = 0 },
        { .name = U8("a10.jpg"), .mtime = 50, .tag = 1 },
        { .name = U8("a2.jpg"), .mtime = 50, .tag = 2 },
        { .name = U8("b.jpg"),  .mtime = 10, .tag = 3 },
    };
    rubraview_sort_item_t b[4] = { a[3], a[2], a[1], a[0] };  /* listed the other way round */
    rubraview_sort_items(a, 4, RUBRAVIEW_SORT_DATE_MODIFIED, true, NULL);
    rubraview_sort_items(b, 4, RUBRAVIEW_SORT_DATE_MODIFIED, true, NULL);
    const char *want[4] = { "b.jpg", "a2.jpg", "a10.jpg", "c.jpg" };
    for (int i = 0; i < 4; ++i) {
        assert(a[i].name.len == strlen(want[i]) && memcmp(a[i].name.ptr, want[i], a[i].name.len) == 0);
        assert(b[i].name.len == a[i].name.len && memcmp(b[i].name.ptr, a[i].name.ptr, a[i].name.len) == 0);
    }

    /* Same size everywhere: the name decides, both directions. */
    rubraview_sort_item_t s[3] = {
        { .name = U8("x2"), .size_bytes = 7 }, { .name = U8("x10"), .size_bytes = 7 }, { .name = U8("x1"), .size_bytes = 7 },
    };
    rubraview_sort_items(s, 3, RUBRAVIEW_SORT_FILE_SIZE, true, NULL);
    assert(s[0].name.len == 2 && s[0].name.ptr[1] == '1' && s[2].name.len == 3);

    /* Large inputs in the shapes that hurt a plain quicksort (sorted,
       reversed, all equal, organ pipe) finish and come out ordered. */
    enum { N = 200000 };
    static rubraview_sort_item_t big[N];
    for (int shape = 0; shape < 4; ++shape) {
        for (int i = 0; i < N; ++i) {
            int64_t k = shape == 0 ? i : shape == 1 ? N - i : shape == 2 ? 5 : (i < N / 2 ? i : N - i);
            big[i] = (rubraview_sort_item_t){ .name = U8("same"), .size_bytes = (uint64_t)k, .tag = (uint64_t)i };
        }
        rubraview_sort_items(big, N, RUBRAVIEW_SORT_FILE_SIZE, true, NULL);
        for (int i = 1; i < N; ++i) {
            assert(big[i - 1].size_bytes < big[i].size_bytes ||
                   (big[i - 1].size_bytes == big[i].size_bytes && big[i - 1].tag < big[i].tag));
        }
    }
    printf("  [PASS] Every order is total: ties go to the name, then the listing; large inputs of any shape\n");
}

int main(void) {
    printf("[test_sort] Starting sorting algorithm unit tests...\n");
    test_natural_comparison();
    test_lexical_comparison();
    test_path_array_sorting();
    test_multi_criteria_sort();
    test_ties_and_scale();
    printf("[test_sort] All tests passed successfully!\n");
    return 0;
}
