#include "rubraview/ini.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static u8str_t lit(const char *s) {
    return (u8str_t){ .ptr = s, .len = strlen(s) };
}

static bool str_eq(u8str_t s, const char *l) {
    size_t n = strlen(l);
    return s.len == n && (n == 0 || memcmp(s.ptr, l, n) == 0);
}

int main(void) {
    printf("[test_ini] Starting INI reader/writer unit tests...\n");

    size_t mem_size = 128 * 1024;
    void *raw_mem = malloc(mem_size);
    assert(raw_mem != NULL);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw_mem, .size = mem_size });

    /* Test 1: Global section, [section] headers, comments, blank lines, CRLF */
    const char *text =
        "; leading comment\r\n"
        "portable_mode = true\r\n"
        "\r\n"
        "[curation]\n"
        "dir_1 = D:\\Curation\\Keep\n"
        "# another comment\n"
        "dir_2 = D:\\Curation\\Best\n"
        "curation_mode = move\n"
        "\n"
        "[navigation]\n"
        "next_page = Right, PageDown, Space, J, D\n";

    rubraview_ini_doc_t doc = rubraview_ini_parse(&arena, lit(text));
    assert(doc.count == 5);

    const u8str_t *portable = rubraview_ini_get(&doc, lit(""), lit("portable_mode"));
    assert(portable && str_eq(*portable, "true"));

    const u8str_t *dir1 = rubraview_ini_get(&doc, lit("curation"), lit("dir_1"));
    assert(dir1 && str_eq(*dir1, "D:\\Curation\\Keep"));

    const u8str_t *next_page = rubraview_ini_get(&doc, lit("navigation"), lit("next_page"));
    assert(next_page && str_eq(*next_page, "Right, PageDown, Space, J, D"));

    assert(rubraview_ini_get(&doc, lit("curation"), lit("does_not_exist")) == NULL);
    printf("  [PASS] Sections, global keys, comments, CRLF, comma-list values\n");

    /* Test 2: Typed getters with defaults */
    assert(rubraview_ini_get_bool(&doc, lit(""), lit("portable_mode"), false) == true);
    assert(rubraview_ini_get_bool(&doc, lit(""), lit("missing"), true) == true);
    assert(rubraview_ini_get_int(&doc, lit("curation"), lit("missing_int"), -7) == -7);

    rubraview_ini_set(&arena, &doc, lit("slideshow"), lit("interval"), lit("3.5"));
    rubraview_ini_set(&arena, &doc, lit("slideshow"), lit("count"), lit("42"));
    rubraview_ini_set(&arena, &doc, lit("slideshow"), lit("negative"), lit("-12"));
    assert(rubraview_ini_get_float(&doc, lit("slideshow"), lit("interval"), 0.0) == 3.5);
    assert(rubraview_ini_get_int(&doc, lit("slideshow"), lit("count"), 0) == 42);
    assert(rubraview_ini_get_int(&doc, lit("slideshow"), lit("negative"), 0) == -12);
    printf("  [PASS] Typed getters (bool/int/float) with defaults\n");

    /* Test 3: ini_set updates an existing key in place (count unchanged) */
    size_t count_before = doc.count;
    rubraview_ini_set(&arena, &doc, lit("curation"), lit("dir_1"), lit("D:\\NewKeep"));
    assert(doc.count == count_before);
    const u8str_t *updated = rubraview_ini_get(&doc, lit("curation"), lit("dir_1"));
    assert(updated && str_eq(*updated, "D:\\NewKeep"));
    printf("  [PASS] ini_set updates existing key in place\n");

    /* Test 4: Serialize then re-parse round-trips all values */
    u8str_t serialized = rubraview_ini_serialize(&arena, &doc);
    assert(serialized.len > 0);
    assert(serialized.ptr[serialized.len] == '\0');

    rubraview_ini_doc_t reparsed = rubraview_ini_parse(&arena, serialized);
    const u8str_t *reparsed_dir1 = rubraview_ini_get(&reparsed, lit("curation"), lit("dir_1"));
    assert(reparsed_dir1 && str_eq(*reparsed_dir1, "D:\\NewKeep"));
    const u8str_t *reparsed_interval = rubraview_ini_get(&reparsed, lit("slideshow"), lit("interval"));
    assert(reparsed_interval && str_eq(*reparsed_interval, "3.5"));
    const u8str_t *reparsed_portable = rubraview_ini_get(&reparsed, lit(""), lit("portable_mode"));
    assert(reparsed_portable && str_eq(*reparsed_portable, "true"));
    printf("  [PASS] Serialize -> re-parse round-trip preserves all values\n");

    /* Test 5: Growth beyond initial small capacity (forces ensure_capacity to reallocate) */
    rubraview_ini_doc_t grown = {0};
    char key_buf[16][8];
    for (int i = 0; i < 16; ++i) {
        snprintf(key_buf[i], sizeof(key_buf[i]), "k%02d", i);
        rubraview_ini_set(&arena, &grown, lit("bulk"), lit(key_buf[i]), lit("v"));
    }
    assert(grown.count == 16);
    for (int i = 0; i < 16; ++i) {
        assert(rubraview_ini_get(&grown, lit("bulk"), lit(key_buf[i])) != NULL);
    }
    printf("  [PASS] Entry array grows correctly past initial capacity\n");

    free(raw_mem);
    printf("[test_ini] All tests passed successfully!\n");
    return 0;
}
