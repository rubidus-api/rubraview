#include "rubraview/path.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static bool str_eq(u8str_t s, const char *lit) {
    if (!lit) return false;
    size_t lit_len = strlen(lit);
    if (s.len != lit_len) return false;
    return memcmp(s.ptr, lit, lit_len) == 0;
}

int main(void) {
    printf("[test_path] Starting u8str_t zero-copy path unit tests...\n");

    /* Allocate arena */
    size_t mem_size = 64 * 1024;
    void *raw_mem = malloc(mem_size);
    assert(raw_mem != NULL);

    proven_mem_mut_t backing = {
        .ptr = (proven_byte_t*)raw_mem,
        .size = mem_size
    };
    proven_arena_t arena = proven_arena_create(backing);

    /* Test 1: Full path deconstruction */
    const char *p1 = "D:/Photos/2026_Summer/beach_sunset.webp";
    u8str_t path1 = { .ptr = p1, .len = strlen(p1) };

    u8str_t dir1 = rubraview_path_dirname(path1);
    assert(str_eq(dir1, "D:/Photos/2026_Summer"));

    u8str_t base1 = rubraview_path_basename(path1);
    assert(str_eq(base1, "beach_sunset.webp"));

    u8str_t ext1 = rubraview_path_ext(path1);
    assert(str_eq(ext1, ".webp"));

    u8str_t stem1 = rubraview_path_stem(path1);
    assert(str_eq(stem1, "beach_sunset"));
    printf("  [PASS] Zero-copy path deconstruction (dir, base, ext, stem)\n");

    /* Test 2: Windows backslash separator */
    const char *p2 = "C:\\Games\\Retro\\sprites\\hero_walk.png";
    u8str_t path2 = { .ptr = p2, .len = strlen(p2) };
    assert(str_eq(rubraview_path_dirname(path2), "C:\\Games\\Retro\\sprites"));
    assert(str_eq(rubraview_path_basename(path2), "hero_walk.png"));
    assert(str_eq(rubraview_path_ext(path2), ".png"));
    assert(str_eq(rubraview_path_stem(path2), "hero_walk"));
    printf("  [PASS] Windows backslash separator handling\n");

    /* Test 3: Edge cases (root path, no extension, dotfile) */
    u8str_t root_path = { .ptr = "/image.png", .len = 10 };
    assert(str_eq(rubraview_path_dirname(root_path), "/"));
    assert(str_eq(rubraview_path_basename(root_path), "image.png"));

    u8str_t no_ext = { .ptr = "Makefile", .len = 8 };
    assert(str_eq(rubraview_path_ext(no_ext), ""));
    assert(str_eq(rubraview_path_stem(no_ext), "Makefile"));

    u8str_t dotfile = { .ptr = ".gitignore", .len = 10 };
    assert(str_eq(rubraview_path_ext(dotfile), ""));
    assert(str_eq(rubraview_path_stem(dotfile), ".gitignore"));
    printf("  [PASS] Edge cases (root, dotfile, no extension)\n");

    /* Test 4: Case-insensitive extension matching */
    u8str_t photo = { .ptr = "IMG_0042.JPG", .len = 12 };
    assert(rubraview_path_has_ext(photo, ".jpg"));
    assert(rubraview_path_has_ext(photo, "jpg"));
    assert(rubraview_path_has_ext(photo, ".JPG"));
    assert(!rubraview_path_has_ext(photo, ".png"));
    printf("  [PASS] Case-insensitive extension matching\n");

    /* Test 5: Path join and null-terminated allocation invariant */
    u8str_t joined = rubraview_path_join(&arena, dir1, base1);
    assert(str_eq(joined, "D:/Photos/2026_Summer/beach_sunset.webp"));
    /* Strictly verify the null-terminated allocation invariant: ptr[len] == '\0' */
    assert(joined.ptr[joined.len] == '\0');
    printf("  [PASS] Path join with null-terminated allocation invariant\n");

    /* The file beside another with a different extension keeps the folder
       (2026-09-23: building it from the stem alone made a relative path,
       and a DVD's `.sub` was then only found when the viewer happened to
       be started in the film's folder). */
    {
        u8str_t idx = { .ptr = "D:/films/holiday movie.idx", .len = 26 };
        u8str_t sub = rubraview_path_with_ext(&arena, idx, ".sub");
        assert(str_eq(sub, "D:/films/holiday movie.sub"));
        assert(sub.ptr[sub.len] == '\0');
        assert(str_eq(rubraview_path_with_ext(&arena, (u8str_t){ .ptr = "movie.idx", .len = 9 }, ".sub"), "movie.sub"));
        assert(str_eq(rubraview_path_with_ext(&arena, (u8str_t){ .ptr = "/tmp/README", .len = 11 }, ".md"), "/tmp/README.md"));
        assert(str_eq(rubraview_path_with_ext(&arena, (u8str_t){ .ptr = "a.tar.gz", .len = 8 }, ".sub"), "a.tar.sub"));
        assert(rubraview_path_with_ext(&arena, (u8str_t){ .ptr = "", .len = 0 }, ".sub").len == 0);
    }
    printf("  [PASS] A sibling path with another extension keeps the folder\n");

    /* Test: two spellings Windows calls the same file compare equal. The
       shell hands over backslashes, a listing joins with '/', and names
       differ in case; none of that makes it another file. */
    {
        u8str_t shell = { .ptr = "C:\\c\\Vol 02.cbz", .len = 15 };
        u8str_t joined = { .ptr = "C:\\c/vol 02.CBZ", .len = 15 };
        u8str_t other = { .ptr = "C:\\c/Vol 03.cbz", .len = 15 };
        assert(rubraview_path_same(shell, joined));
        assert(!rubraview_path_same(shell, other));
        assert(!rubraview_path_same(shell, (u8str_t){ .ptr = "C:\\c\\Vol 02.cb", .len = 14 }));
        assert(rubraview_path_same((u8str_t){ .ptr = "", .len = 0 }, (u8str_t){ .ptr = "", .len = 0 }));
    }
    printf("  [PASS] Paths Windows calls the same file compare equal\n");

    /* One set of view comparisons for the whole program (core.h), where
       there were a dozen private copies. An empty view may carry a NULL
       pointer; none of them may hand that to memcmp. */
    {
        u8str_t empty_null = { .ptr = NULL, .len = 0 };
        assert(rubraview_u8_eq(empty_null, U8("")));
        assert(rubraview_u8_eq_lit(empty_null, ""));
        assert(!rubraview_u8_eq_lit(empty_null, "a"));
        assert(rubraview_u8_eq(U8("next_page"), U8("next_page")));
        assert(!rubraview_u8_eq(U8("next_page"), U8("next_pagE")));
        assert(rubraview_u8_eq_lit(U8("jpeg"), "jpeg"));
        assert(rubraview_u8_eq_lit_ci(U8("JPeG"), "jpeg"));
        assert(!rubraview_u8_eq_lit_ci(U8("jpg"), "jpeg"));
        assert(rubraview_u8_starts_with(U8("[history]"), "[his"));
        assert(rubraview_u8_starts_with(empty_null, ""));
        assert(!rubraview_u8_starts_with(U8("ab"), "abc"));
        assert(rubraview_u8_starts_with_ci(U8("Dialogue: 0"), "dialogue:"));
        proven_u8str_view_t v = rubraview_u8_view(U8("abc"));
        assert(v.size == 3 && v.ptr[0] == 'a');
    }
    printf("  [PASS] One set of view comparisons, safe on an empty view\n");

    free(raw_mem);
    printf("[test_path] All tests passed successfully!\n");
    return 0;
}
