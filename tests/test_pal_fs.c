#include "rubraview/pal/pal_fs.h"
#include "rubraview/path.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#endif

static u8str_t lit(const char *s) {
    return (u8str_t){ .ptr = s, .len = strlen(s) };
}

static bool str_eq(u8str_t s, const char *l) {
    size_t n = strlen(l);
    return s.len == n && (n == 0 || memcmp(s.ptr, l, n) == 0);
}

/* The fixture lives under build/tests/, which docs/agents/build.md
   designates for test-generated artifacts. */
#define FIXTURE_DIR "build/tests/fixture_fs"

static void make_dir(const char *path) {
#ifdef _WIN32
    (void)path;
#else
    mkdir(path, 0777);
#endif
}

static void write_file(const char *path, size_t byte_count) {
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    for (size_t i = 0; i < byte_count; ++i) fputc('x', f);
    fclose(f);
}

static const rubraview_fs_entry_t *find_entry(const rubraview_fs_listing_t *listing, const char *name) {
    for (size_t i = 0; i < listing->count; ++i) {
        if (str_eq(listing->entries[i].name, name)) return &listing->entries[i];
    }
    return NULL;
}

int main(void) {
    printf("[test_pal_fs] Starting filesystem PAL unit tests...\n");

    size_t mem_size = 512 * 1024;
    void *raw_mem = malloc(mem_size);
    assert(raw_mem != NULL);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw_mem, .size = mem_size });

    /* Build a fixture directory: three pages needing natural order, one
       non-image file, and a subdirectory. */
    make_dir("build");
    make_dir("build/tests");
    make_dir(FIXTURE_DIR);
    make_dir(FIXTURE_DIR "/sub");
    write_file(FIXTURE_DIR "/page (1).jpg", 30);
    write_file(FIXTURE_DIR "/page (2).jpg", 20);
    write_file(FIXTURE_DIR "/page (10).jpg", 10);
    write_file(FIXTURE_DIR "/notes.txt", 5);

    /* Test 1: Enumeration finds every child, excludes "." and "..", and
       flags the subdirectory. */
    rubraview_fs_listing_t listing = rubraview_pal_fs_list_dir(&arena, lit(FIXTURE_DIR));
    assert(listing.count == 5); /* 4 files + 1 directory */

    const rubraview_fs_entry_t *sub = find_entry(&listing, "sub");
    assert(sub != NULL && sub->is_directory);

    const rubraview_fs_entry_t *page1 = find_entry(&listing, "page (1).jpg");
    assert(page1 != NULL && !page1->is_directory);
    assert(page1->size_bytes == 30);
    printf("  [PASS] Directory enumeration lists children, flags directories, reports sizes\n");

    /* Test 2: Every returned name and path honours the null-terminated
       allocation invariant (§7.2.3), so it can go straight to an OS call. */
    for (size_t i = 0; i < listing.count; ++i) {
        assert(listing.entries[i].name.ptr[listing.entries[i].name.len] == '\0');
        assert(listing.entries[i].path.ptr[listing.entries[i].path.len] == '\0');
    }
    printf("  [PASS] Names and paths carry the null-terminated allocation invariant\n");

    /* Test 3: Full paths are the directory joined with the name. */
    assert(str_eq(page1->path, FIXTURE_DIR "/page (1).jpg"));
    printf("  [PASS] Entry paths are the directory joined with the entry name\n");

    /* Test 4: Sibling indexing filters to images and orders them
       naturally: (1) < (2) < (10), with notes.txt and sub/ excluded. */
    u8str_t current = lit(FIXTURE_DIR "/page (2).jpg");
    rubraview_sibling_index_t index = rubraview_fs_index_siblings(
        &arena, &listing, current, lit("*.jpg;*.png"), RUBRAVIEW_SORT_NAME_NATURAL, true);

    assert(index.count == 3);
    assert(str_eq(index.paths[0], FIXTURE_DIR "/page (1).jpg"));
    assert(str_eq(index.paths[1], FIXTURE_DIR "/page (2).jpg"));
    assert(str_eq(index.paths[2], FIXTURE_DIR "/page (10).jpg"));
    printf("  [PASS] Sibling indexing filters by extension and sorts naturally\n");

    /* Test 5: The opened file's position within the ordered siblings is
       reported, which is what page-flip navigation starts from. */
    assert(index.found);
    assert(index.current == 1);
    printf("  [PASS] Current file located at its index within the ordered siblings\n");

    /* Test 6: A file outside the filtered set reports found == false
       without breaking the listing. */
    rubraview_sibling_index_t missing = rubraview_fs_index_siblings(
        &arena, &listing, lit(FIXTURE_DIR "/notes.txt"), lit("*.jpg"), RUBRAVIEW_SORT_NAME_NATURAL, true);
    assert(missing.count == 3);
    assert(!missing.found);
    assert(missing.current == 0);
    printf("  [PASS] A file outside the filter reports found == false\n");

    /* Test 7: Ordering by file size uses the metadata the PAL collected
       (page (10).jpg is smallest at 10 bytes, page (1).jpg largest at 30). */
    rubraview_sibling_index_t by_size = rubraview_fs_index_siblings(
        &arena, &listing, current, lit("*.jpg"), RUBRAVIEW_SORT_FILE_SIZE, true);
    assert(by_size.count == 3);
    assert(str_eq(by_size.paths[0], FIXTURE_DIR "/page (10).jpg"));
    assert(str_eq(by_size.paths[2], FIXTURE_DIR "/page (1).jpg"));
    printf("  [PASS] Ordering by file size uses PAL-collected metadata\n");

    /* Test 8: An empty extension filter keeps every file (but still no
       directories). */
    rubraview_sibling_index_t unfiltered = rubraview_fs_index_siblings(
        &arena, &listing, current, (u8str_t){ .ptr = "", .len = 0 }, RUBRAVIEW_SORT_NAME_NATURAL, true);
    assert(unfiltered.count == 4); /* the 4 files, not the subdirectory */
    printf("  [PASS] Empty filter keeps all files and still excludes directories\n");

    /* Test 9: stat() and exists() agree with the enumeration. */
    rubraview_fs_entry_t stat_entry;
    assert(rubraview_pal_fs_stat(&arena, lit(FIXTURE_DIR "/page (1).jpg"), &stat_entry));
    assert(stat_entry.size_bytes == 30);
    assert(!stat_entry.is_directory);
    assert(str_eq(stat_entry.name, "page (1).jpg"));

    assert(rubraview_pal_fs_exists(lit(FIXTURE_DIR "/page (1).jpg")));
    assert(!rubraview_pal_fs_exists(lit(FIXTURE_DIR "/does_not_exist.jpg")));
    printf("  [PASS] stat() and exists() agree with the enumeration\n");

    /* Test 10: A missing directory yields an empty listing, not a crash. */
    rubraview_fs_listing_t missing_dir = rubraview_pal_fs_list_dir(&arena, lit(FIXTURE_DIR "/no_such_dir"));
    assert(missing_dir.count == 0);
    printf("  [PASS] Missing directory yields an empty listing without crashing\n");

    free(raw_mem);
    printf("[test_pal_fs] All tests passed successfully!\n");
    return 0;
}
