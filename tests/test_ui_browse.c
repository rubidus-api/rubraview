#include "rubraview/ui_virtual.h"
#include "rubraview/filmstrip.h"
#include "rubraview/picker.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>

static bool approx(double a, double b) { return fabs(a - b) < 1e-9; }

static bool str_eq(u8str_t s, const char *l) {
    size_t n = strlen(l);
    return s.len == n && (n == 0 || memcmp(s.ptr, l, n) == 0);
}

static void test_virtual(void) {
    /* Test 1: A huge list only realises a viewport's worth of items —
       the §3.15.2 promise about 10,000-file directories. */
    {
        rubraview_virtual_range_t r = rubraview_virtual_range(10000, 100.0, 800.0, 0.0, 0);
        assert(r.first == 0);
        assert(r.count <= 12); /* 8 fully visible + the two straddling edges */
        assert(r.count >= 8);
    }
    printf("  [PASS] A 10,000-item list realises only a viewport's worth of tiles\n");

    /* Test 2: Scrolling moves the window; the lookahead widens it on
       both sides without running past either end. */
    {
        rubraview_virtual_range_t r = rubraview_virtual_range(1000, 100.0, 800.0, 5000.0, 3);
        assert(r.first == 50 - 3);
        assert(r.first + r.count <= 1000);

        rubraview_virtual_range_t at_start = rubraview_virtual_range(1000, 100.0, 800.0, 0.0, 3);
        assert(at_start.first == 0); /* clamped, never negative */

        rubraview_virtual_range_t at_end = rubraview_virtual_range(20, 100.0, 800.0, 100000.0, 2);
        assert(at_end.first + at_end.count <= 20);
    }
    printf("  [PASS] Scrolling and lookahead stay inside the list bounds\n");

    /* Test 3: Offsets clamp to the scrollable span, and a list that fits
       entirely cannot scroll at all. */
    {
        assert(approx(rubraview_virtual_clamp_offset(20, 100.0, 800.0, -50.0), 0.0));
        assert(approx(rubraview_virtual_clamp_offset(20, 100.0, 800.0, 99999.0), 2000.0 - 800.0));
        assert(approx(rubraview_virtual_clamp_offset(3, 100.0, 800.0, 500.0), 0.0)); /* all 300 px fits */
    }
    printf("  [PASS] Scroll offsets clamp to the scrollable span\n");

    /* Test 4: scroll_to moves the minimum distance, and leaves an
       already-visible item alone. */
    {
        assert(approx(rubraview_virtual_scroll_to(4, 100.0, 800.0, 0.0), 0.0)); /* already visible */
        assert(approx(rubraview_virtual_scroll_to(20, 100.0, 800.0, 0.0), 2100.0 - 800.0)); /* scroll down just enough */
        assert(approx(rubraview_virtual_scroll_to(2, 100.0, 800.0, 1000.0), 200.0)); /* scroll up to its top */
    }
    printf("  [PASS] scroll_to moves the minimum distance and respects visibility\n");
}

static void test_filmstrip(void) {
    size_t mem_size = 256 * 1024;
    void *raw = malloc(mem_size);
    assert(raw != NULL);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw, .size = mem_size });

    /* Test 5: The strip reveals the current page and holds only a
       bounded set of thumbnails, whatever the directory size. */
    {
        rubraview_filmstrip_t strip = rubraview_filmstrip_create(5000, 120.0, 960.0);
        rubraview_filmstrip_reveal(&strip, 2500);

        rubraview_virtual_range_t visible = rubraview_filmstrip_visible(&strip);
        assert(visible.count > 0 && visible.count < 40);
        assert(visible.first <= 2500 && 2500 < visible.first + visible.count);
    }
    printf("  [PASS] Filmstrip reveals the current page within a bounded window\n");

    /* Test 6: Syncing reports which thumbnails must be decoded, and the
       second sync asks for nothing new. */
    {
        rubraview_filmstrip_t strip = rubraview_filmstrip_create(100, 120.0, 600.0);
        rubraview_lru_cache_t cache = rubraview_lru_create(&arena, 64, 64u * 1024u * 1024u);

        size_t needed[64];
        size_t needed_count = 0;
        uint64_t evicted[64];

        rubraview_filmstrip_sync_cache(&strip, &cache, 0, evicted, 64, needed, 64, &needed_count);
        assert(needed_count > 0);

        size_t second_round = 0;
        rubraview_filmstrip_sync_cache(&strip, &cache, 0, evicted, 64, needed, 64, &second_round);
        assert(second_round == 0); /* everything visible is already cached */
    }
    printf("  [PASS] Filmstrip reports thumbnails to decode, then nothing on a repeat sync\n");

    /* Test 7: Under a tight budget the cache evicts, and the thumbnail
       at the reader's current page survives — it is touched last, so it
       is the most-recently-used entry (§7.4). */
    {
        rubraview_filmstrip_t strip = rubraview_filmstrip_create(200, 120.0, 960.0);
        strip.estimated_bytes = 1024 * 1024;
        /* Room for only four thumbnails: eviction is guaranteed. */
        rubraview_lru_cache_t cache = rubraview_lru_create(&arena, 64, 4u * 1024u * 1024u);

        rubraview_filmstrip_reveal(&strip, 100);
        size_t needed[64];
        size_t needed_count = 0;
        uint64_t evicted[64];
        size_t evicted_count = rubraview_filmstrip_sync_cache(&strip, &cache, 100, evicted, 64, needed, 64, &needed_count);

        assert(evicted_count > 0);
        assert(rubraview_lru_contains(&cache, 100)); /* the current page survived */
        assert(rubraview_lru_used_bytes(&cache) <= 4u * 1024u * 1024u);
    }
    printf("  [PASS] Under a tight budget the current page's thumbnail survives eviction\n");

    free(raw);
}

static void test_picker(void) {
    /* Test 8: A path splits into tappable crumbs, each carrying the
       prefix that navigating to it opens (§3.15.2). */
    {
        u8str_t path = U8("C:/Comics/Berserk/Vol01");
        rubraview_breadcrumbs_t crumbs = rubraview_picker_breadcrumbs(path);
        assert(crumbs.count == 4);
        assert(str_eq(crumbs.items[0].label, "C:"));
        assert(str_eq(crumbs.items[1].label, "Comics"));
        assert(str_eq(crumbs.items[3].label, "Vol01"));
        assert(str_eq(crumbs.items[1].prefix, "C:/Comics"));
        assert(str_eq(crumbs.items[3].prefix, "C:/Comics/Berserk/Vol01"));

        /* Backslashes and repeated separators are handled the same way. */
        rubraview_breadcrumbs_t win = rubraview_picker_breadcrumbs(U8("D:\\Manga\\\\Ch01"));
        assert(win.count == 3);
        assert(str_eq(win.items[2].label, "Ch01"));

        assert(rubraview_picker_breadcrumbs(U8("")).count == 0);
    }
    printf("  [PASS] Breadcrumbs split a path into tappable segments with prefixes\n");

    /* Build a small listing to browse. */
    rubraview_fs_entry_t entries[6] = {
        { .name = U8("apple.jpg"),  .path = U8("/d/apple.jpg"),  .size_bytes = 100 },
        { .name = U8("banana.jpg"), .path = U8("/d/banana.jpg"), .size_bytes = 200 },
        { .name = U8("cherry.png"), .path = U8("/d/cherry.png"), .size_bytes = 300 },
        { .name = U8("apricot.gif"),.path = U8("/d/apricot.gif"),.size_bytes = 400 },
        { .name = U8("durian.webp"),.path = U8("/d/durian.webp"),.size_bytes = 500 },
        { .name = U8("elder.bmp"),  .path = U8("/d/elder.bmp"),  .size_bytes = 600 },
    };
    rubraview_fs_listing_t listing = { .entries = entries, .count = 6 };

    /* Test 9: The grid virtualises by row, not by item. */
    {
        rubraview_picker_t picker = rubraview_picker_create(&listing, 160.0, 200.0, 3);
        rubraview_virtual_range_t visible = rubraview_picker_visible(&picker);
        assert(visible.first == 0);
        assert(visible.count == 6); /* 2 rows of 3, both realised */
        assert(visible.first + visible.count <= listing.count);
    }
    printf("  [PASS] Picker grid virtualises by row and clamps to the listing\n");

    /* Test 10: Type-ahead jumps to the next match and cycles through
       repeats (§3.15.4). */
    {
        rubraview_picker_t picker = rubraview_picker_create(&listing, 160.0, 400.0, 3);

        assert(rubraview_picker_type_ahead(&picker, 'b'));
        assert(picker.focus == 1); /* banana */

        assert(rubraview_picker_type_ahead(&picker, 'a'));
        assert(picker.focus == 3); /* apricot: the next 'a' after banana */
        assert(rubraview_picker_type_ahead(&picker, 'a'));
        assert(picker.focus == 0); /* wraps back to apple */

        assert(!rubraview_picker_type_ahead(&picker, 'z')); /* no match: focus unchanged */
        assert(picker.focus == 0);

        /* Case-insensitive. */
        assert(rubraview_picker_type_ahead(&picker, 'D'));
        assert(picker.focus == 4); /* durian */
    }
    printf("  [PASS] Type-ahead cycles through matches, wraps, and ignores case\n");

    /* Test 11: Multi-select tracks count and total bytes for the bottom
       bar; single-select mode ignores toggles. */
    {
        bool selected[6] = { false };
        rubraview_picker_t picker = rubraview_picker_create(&listing, 160.0, 400.0, 3);
        picker.multi_select = true;
        picker.selected = selected;

        rubraview_picker_toggle(&picker, 1);
        rubraview_picker_toggle(&picker, 4);

        size_t count = 0;
        uint64_t bytes = 0;
        rubraview_picker_selection_metrics(&picker, &count, &bytes);
        assert(count == 2);
        assert(bytes == 200 + 500);

        rubraview_picker_toggle(&picker, 1); /* toggling again deselects */
        rubraview_picker_selection_metrics(&picker, &count, &bytes);
        assert(count == 1 && bytes == 500);

        rubraview_picker_toggle(&picker, 99); /* out of range: ignored */
        rubraview_picker_selection_metrics(&picker, &count, &bytes);
        assert(count == 1);

        picker.multi_select = false;
        rubraview_picker_toggle(&picker, 0);
        rubraview_picker_selection_metrics(&picker, &count, &bytes);
        assert(count == 1); /* unchanged: single-select ignores toggles */
    }
    printf("  [PASS] Multi-select tracks selection count and total bytes\n");

    /* Test 12: An empty listing is inert rather than a crash. */
    {
        rubraview_fs_listing_t empty = { .entries = NULL, .count = 0 };
        rubraview_picker_t picker = rubraview_picker_create(&empty, 160.0, 400.0, 3);
        assert(rubraview_picker_visible(&picker).count == 0);
        assert(!rubraview_picker_type_ahead(&picker, 'a'));
        rubraview_picker_reveal_focus(&picker); /* must not divide by zero */
    }
    printf("  [PASS] An empty listing leaves the picker inert, no crash\n");
}

int main(void) {
    printf("[test_ui_browse] Starting virtual scrolling, filmstrip and picker unit tests...\n");
    test_virtual();
    test_filmstrip();
    test_picker();
    printf("[test_ui_browse] All tests passed successfully!\n");
    return 0;
}
