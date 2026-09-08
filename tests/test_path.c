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

    u8str_t dir1 = rv_path_dirname(path1);
    assert(str_eq(dir1, "D:/Photos/2026_Summer"));

    u8str_t base1 = rv_path_basename(path1);
    assert(str_eq(base1, "beach_sunset.webp"));

    u8str_t ext1 = rv_path_ext(path1);
    assert(str_eq(ext1, ".webp"));

    u8str_t stem1 = rv_path_stem(path1);
    assert(str_eq(stem1, "beach_sunset"));
    printf("  [PASS] Zero-copy path deconstruction (dir, base, ext, stem)\n");

    /* Test 2: Windows backslash separator */
    const char *p2 = "C:\\Games\\Retro\\sprites\\hero_walk.png";
    u8str_t path2 = { .ptr = p2, .len = strlen(p2) };
    assert(str_eq(rv_path_dirname(path2), "C:\\Games\\Retro\\sprites"));
    assert(str_eq(rv_path_basename(path2), "hero_walk.png"));
    assert(str_eq(rv_path_ext(path2), ".png"));
    assert(str_eq(rv_path_stem(path2), "hero_walk"));
    printf("  [PASS] Windows backslash separator handling\n");

    /* Test 3: Edge cases (root path, no extension, dotfile) */
    u8str_t root_path = { .ptr = "/image.png", .len = 10 };
    assert(str_eq(rv_path_dirname(root_path), "/"));
    assert(str_eq(rv_path_basename(root_path), "image.png"));

    u8str_t no_ext = { .ptr = "Makefile", .len = 8 };
    assert(str_eq(rv_path_ext(no_ext), ""));
    assert(str_eq(rv_path_stem(no_ext), "Makefile"));

    u8str_t dotfile = { .ptr = ".gitignore", .len = 10 };
    assert(str_eq(rv_path_ext(dotfile), ""));
    assert(str_eq(rv_path_stem(dotfile), ".gitignore"));
    printf("  [PASS] Edge cases (root, dotfile, no extension)\n");

    /* Test 4: Case-insensitive extension matching */
    u8str_t photo = { .ptr = "IMG_0042.JPG", .len = 12 };
    assert(rv_path_has_ext(photo, ".jpg"));
    assert(rv_path_has_ext(photo, "jpg"));
    assert(rv_path_has_ext(photo, ".JPG"));
    assert(!rv_path_has_ext(photo, ".png"));
    printf("  [PASS] Case-insensitive extension matching\n");

    /* Test 5: Path join and null-terminated allocation invariant */
    u8str_t joined = rv_path_join(&arena, dir1, base1);
    assert(str_eq(joined, "D:/Photos/2026_Summer/beach_sunset.webp"));
    /* Strictly verify the null-terminated allocation invariant: ptr[len] == '\0' */
    assert(joined.ptr[joined.len] == '\0');
    printf("  [PASS] Path join with null-terminated allocation invariant\n");

    free(raw_mem);
    printf("[test_path] All tests passed successfully!\n");
    return 0;
}
