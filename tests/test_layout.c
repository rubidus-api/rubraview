#include "rubraview/layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

/* Realistic comic-scan aspect ratios: a normal single page is portrait
   (AR well below the 1.15 spread threshold); a pre-merged two-page spread
   is roughly twice as wide as a single page (AR well above it). A 4:3 or
   16:9 landscape *photo* would misleadingly read as "wide" under a fixed
   1.15 threshold, so tests deliberately use comic-shaped pages instead. */
static const rubraview_page_info_t NORMAL = { .width = 800, .height = 1200 }; /* AR 0.667 */
static const rubraview_page_info_t WIDE   = { .width = 1600, .height = 1200 }; /* AR 1.333 */

int main(void) {
    printf("[test_layout] Starting layout engine unit tests...\n");

    size_t mem_size = 256 * 1024;
    void *raw_mem = malloc(mem_size);
    assert(raw_mem != NULL);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw_mem, .size = mem_size });

    /* Test 1: SINGLE mode — every page is its own spread, in order. */
    {
        rubraview_page_info_t pages[3] = { NORMAL, NORMAL, NORMAL };
        rubraview_layout_opts_t opts = rubraview_layout_opts_default(RUBRAVIEW_PAGE_LAYOUT_SINGLE, RUBRAVIEW_READING_LTR);
        rubraview_layout_result_t r = rubraview_layout_compute(&arena, pages, 3, 1000, 1000, opts);
        assert(r.count == 3);
        for (int i = 0; i < 3; ++i) {
            assert(r.spreads[i].left_index == i);
            assert(r.spreads[i].right_index == -1);
        }
    }
    printf("  [PASS] SINGLE mode: one spread per page\n");

    /* Test 2: DUAL mode pairs consecutive pages; an odd trailing page
       stands alone. */
    {
        rubraview_page_info_t pages[5] = { NORMAL, NORMAL, NORMAL, NORMAL, NORMAL };
        rubraview_layout_opts_t opts = rubraview_layout_opts_default(RUBRAVIEW_PAGE_LAYOUT_DUAL, RUBRAVIEW_READING_LTR);
        rubraview_layout_result_t r = rubraview_layout_compute(&arena, pages, 5, 1000, 1000, opts);
        assert(r.count == 3);
        assert(r.spreads[0].left_index == 0 && r.spreads[0].right_index == 1);
        assert(r.spreads[1].left_index == 2 && r.spreads[1].right_index == 3);
        assert(r.spreads[2].left_index == 4 && r.spreads[2].right_index == -1);
    }
    printf("  [PASS] DUAL mode: consecutive pairs, odd trailing page alone\n");

    /* Test 3: BOOK mode LTR — cover exception, then pairs 1-2, 3-4, 5-6
       (0-indexed), matching RFC's 1-indexed "2-3, 4-5, 6-7". */
    {
        rubraview_page_info_t pages[7];
        for (int i = 0; i < 7; ++i) pages[i] = NORMAL;
        rubraview_layout_opts_t opts = rubraview_layout_opts_default(RUBRAVIEW_PAGE_LAYOUT_BOOK, RUBRAVIEW_READING_LTR);
        rubraview_layout_result_t r = rubraview_layout_compute(&arena, pages, 7, 1000, 1000, opts);
        assert(r.count == 4);
        assert(r.spreads[0].left_index == 0 && r.spreads[0].right_index == -1); /* cover alone */
        assert(r.spreads[1].left_index == 1 && r.spreads[1].right_index == 2);
        assert(r.spreads[2].left_index == 3 && r.spreads[2].right_index == 4);
        assert(r.spreads[3].left_index == 5 && r.spreads[3].right_index == 6);
    }
    printf("  [PASS] BOOK mode LTR: cover exception then 1-2, 3-4, 5-6 pairing\n");

    /* Test 4: BOOK mode RTL — same pairing membership, left/right swapped
       within each spread (manga reading order). */
    {
        rubraview_page_info_t pages[3] = { NORMAL, NORMAL, NORMAL };
        rubraview_layout_opts_t opts = rubraview_layout_opts_default(RUBRAVIEW_PAGE_LAYOUT_BOOK, RUBRAVIEW_READING_RTL);
        rubraview_layout_result_t r = rubraview_layout_compute(&arena, pages, 3, 1000, 1000, opts);
        assert(r.count == 2);
        assert(r.spreads[0].left_index == 0 && r.spreads[0].right_index == -1); /* cover, direction-independent */
        assert(r.spreads[1].left_index == 2 && r.spreads[1].right_index == 1); /* page1 on the right, page2 on the left */
    }
    printf("  [PASS] BOOK mode RTL: pairing membership same, screen sides swapped\n");

    /* Test 5: Pre-merged spread detection — a wide page is never paired
       with its neighbor, matching the RFC diagram: "Page 4&5 spread ...
       NOT paired with Page 6". */
    {
        rubraview_page_info_t pages[4] = { NORMAL, WIDE, NORMAL, NORMAL };
        rubraview_layout_opts_t opts = rubraview_layout_opts_default(RUBRAVIEW_PAGE_LAYOUT_DUAL, RUBRAVIEW_READING_LTR);
        rubraview_layout_result_t r = rubraview_layout_compute(&arena, pages, 4, 1000, 1000, opts);
        assert(r.count == 3);
        assert(r.spreads[0].left_index == 0 && r.spreads[0].right_index == -1); /* couldn't pair with the wide page 1 */
        assert(r.spreads[1].left_index == 1 && r.spreads[1].right_index == -1 && r.spreads[1].is_premerged_spread);
        assert(r.spreads[2].left_index == 2 && r.spreads[2].right_index == 3); /* pairing resumes after the spread */
    }
    printf("  [PASS] Pre-merged wide spread stands alone, not paired with a neighbor\n");

    /* Test 6: Smart spread splitting bisects a wide page into two virtual
       single pages, ordered by reading direction (§3.3.7). */
    {
        rubraview_page_info_t pages[1] = { WIDE };
        rubraview_layout_opts_t opts_ltr = rubraview_layout_opts_default(RUBRAVIEW_PAGE_LAYOUT_DUAL, RUBRAVIEW_READING_LTR);
        opts_ltr.auto_split_wide_spreads = true;
        rubraview_layout_result_t r_ltr = rubraview_layout_compute(&arena, pages, 1, 1000, 1000, opts_ltr);
        assert(r_ltr.count == 2);
        assert(r_ltr.spreads[0].left_index == 0 && r_ltr.spreads[0].left_half == RUBRAVIEW_SPREAD_LEFT_HALF);
        assert(r_ltr.spreads[1].left_index == 0 && r_ltr.spreads[1].left_half == RUBRAVIEW_SPREAD_RIGHT_HALF);

        rubraview_layout_opts_t opts_rtl = rubraview_layout_opts_default(RUBRAVIEW_PAGE_LAYOUT_DUAL, RUBRAVIEW_READING_RTL);
        opts_rtl.auto_split_wide_spreads = true;
        rubraview_layout_result_t r_rtl = rubraview_layout_compute(&arena, pages, 1, 1000, 1000, opts_rtl);
        assert(r_rtl.count == 2);
        assert(r_rtl.spreads[0].left_half == RUBRAVIEW_SPREAD_RIGHT_HALF); /* RTL: right half shown first */
        assert(r_rtl.spreads[1].left_half == RUBRAVIEW_SPREAD_LEFT_HALF);
    }
    printf("  [PASS] Wide spread splitting respects reading direction order\n");

    /* Test 7: Portrait window auto-collapse forces effective SINGLE for
       DUAL/BOOK; a landscape window leaves the requested mode alone. */
    {
        rubraview_page_info_t pages[4] = { NORMAL, NORMAL, NORMAL, NORMAL };
        rubraview_layout_opts_t opts = rubraview_layout_opts_default(RUBRAVIEW_PAGE_LAYOUT_DUAL, RUBRAVIEW_READING_LTR);

        rubraview_layout_result_t portrait = rubraview_layout_compute(&arena, pages, 4, 300, 800, opts); /* AR 0.375 < 1.0 */
        assert(portrait.count == 4); /* collapsed to SINGLE: one spread per page */
        for (int i = 0; i < 4; ++i) assert(portrait.spreads[i].right_index == -1);

        rubraview_layout_result_t landscape = rubraview_layout_compute(&arena, pages, 4, 1000, 700, opts); /* AR > 1.0 */
        assert(landscape.count == 2); /* stays DUAL: paired */
        assert(landscape.spreads[0].right_index == 1);
    }
    printf("  [PASS] Portrait window auto-collapses DUAL/BOOK to SINGLE\n");

    /* Test 8: WEBTOON mode is unaffected by portrait auto-collapse (it is
       already a single continuous column) and never pairs pages. */
    {
        rubraview_page_info_t pages[3] = { NORMAL, WIDE, NORMAL };
        rubraview_layout_opts_t opts = rubraview_layout_opts_default(RUBRAVIEW_PAGE_LAYOUT_WEBTOON, RUBRAVIEW_READING_LTR);
        rubraview_layout_result_t r = rubraview_layout_compute(&arena, pages, 3, 300, 800, opts);
        assert(r.count == 3);
        for (int i = 0; i < 3; ++i) {
            assert(r.spreads[i].left_index == i && r.spreads[i].right_index == -1);
        }
    }
    printf("  [PASS] WEBTOON mode is unaffected by orientation and never pairs\n");

    /* Test 9: Empty and NULL input are handled without crashing. */
    {
        rubraview_layout_opts_t opts = rubraview_layout_opts_default(RUBRAVIEW_PAGE_LAYOUT_SINGLE, RUBRAVIEW_READING_LTR);
        rubraview_layout_result_t empty = rubraview_layout_compute(&arena, NULL, 0, 1000, 1000, opts);
        assert(empty.count == 0);
        rubraview_layout_result_t null_pages = rubraview_layout_compute(&arena, NULL, 3, 1000, 1000, opts);
        assert(null_pages.count == 0);
    }
    printf("  [PASS] Empty/NULL input handled without crashing\n");

    /* Test 10: A page marked to stand alone (a tagged cover, §3.8.5)
       never pairs, and pagination resumes cleanly after it — the same
       treatment a pre-merged spread gets, but from metadata rather than
       from the aspect ratio. */
    {
        rubraview_page_info_t pages[5] = { NORMAL, NORMAL, NORMAL, NORMAL, NORMAL };
        pages[2].force_standalone = true; /* an inner cover mid-volume */

        rubraview_layout_opts_t opts = rubraview_layout_opts_default(RUBRAVIEW_PAGE_LAYOUT_DUAL, RUBRAVIEW_READING_LTR);
        rubraview_layout_result_t r = rubraview_layout_compute(&arena, pages, 5, 1000, 700, opts);

        /* pair(0,1), the cover alone, then pair(3,4). */
        assert(r.count == 3);
        assert(r.spreads[0].left_index == 0 && r.spreads[0].right_index == 1);
        assert(r.spreads[1].left_index == 2 && r.spreads[1].right_index == -1); /* the cover, alone */
        assert(!r.spreads[1].is_premerged_spread); /* it is not wide, just tagged */
        assert(r.spreads[2].left_index == 3 && r.spreads[2].right_index == 4);
    }
    printf("  [PASS] A metadata-tagged cover stands alone and pagination resumes after it\n");

    /* Test 11: A page before a tagged cover cannot pair into it either,
       which is what keeps every later spread aligned. */
    {
        rubraview_page_info_t pages[3] = { NORMAL, NORMAL, NORMAL };
        pages[1].force_standalone = true;

        rubraview_layout_opts_t opts = rubraview_layout_opts_default(RUBRAVIEW_PAGE_LAYOUT_DUAL, RUBRAVIEW_READING_LTR);
        rubraview_layout_result_t r = rubraview_layout_compute(&arena, pages, 3, 1000, 700, opts);

        assert(r.count == 3);
        for (size_t i = 0; i < r.count; ++i) assert(r.spreads[i].right_index == -1);
    }
    printf("  [PASS] A tagged cover also stops the page before it from pairing\n");

    /* Test 12: Splitting bisects a genuinely wide scan but shows a
       tagged cover whole — a cover is one page, not two halves. */
    {
        rubraview_page_info_t pages[2] = { WIDE, NORMAL };
        pages[1].force_standalone = true;

        rubraview_layout_opts_t opts = rubraview_layout_opts_default(RUBRAVIEW_PAGE_LAYOUT_DUAL, RUBRAVIEW_READING_LTR);
        opts.auto_split_wide_spreads = true;
        rubraview_layout_result_t r = rubraview_layout_compute(&arena, pages, 2, 1000, 700, opts);

        assert(r.count == 3); /* two halves of the wide page, then the cover */
        assert(r.spreads[0].left_half == RUBRAVIEW_SPREAD_LEFT_HALF);
        assert(r.spreads[1].left_half == RUBRAVIEW_SPREAD_RIGHT_HALF);
        assert(r.spreads[2].left_index == 1 && r.spreads[2].left_half == RUBRAVIEW_SPREAD_WHOLE);
    }
    printf("  [PASS] Splitting bisects a wide scan but shows a tagged cover whole\n");

    free(raw_mem);
    printf("[test_layout] All tests passed successfully!\n");
    return 0;
}
