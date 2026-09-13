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

    /* Test 6 (D-13): what older files and hand edits hold still reads —
       a byte-order mark, bare text, `;` comments — and a quoted string
       means its contents, with its two escapes undone. */
    {
        const char *old =
            "\xEF\xBB\xBF[video]\n"
            "; an old comment\n"
            "decoder = ffmpeg\n"
            "subtitle_size = 48\n"
            "path = \"C:\\\\Films\\\\a \\\"b\\\"\"\n"
            "zero_led = 010\n"
            "ratio = 0.5\n"
            "flag = true\n";
        rubraview_ini_doc_t d = rubraview_ini_parse(&arena, lit(old));
        const u8str_t *decoder = rubraview_ini_get(&d, lit("video"), lit("decoder"));
        assert(decoder && str_eq(*decoder, "ffmpeg"));            /* the BOM did not hide [video] */
        const u8str_t *path = rubraview_ini_get(&d, lit("video"), lit("path"));
        assert(path && str_eq(*path, "C:\\Films\\a \"b\""));
        for (size_t i = 0; i < d.count; ++i) {
            u8str_t k = d.entries[i].key;
            if (str_eq(k, "decoder"))       assert(d.entries[i].kind == RUBRAVIEW_INI_STRING);
            if (str_eq(k, "subtitle_size")) assert(d.entries[i].kind == RUBRAVIEW_INI_INT);
            if (str_eq(k, "zero_led"))      assert(d.entries[i].kind == RUBRAVIEW_INI_STRING); /* TOML refuses 010 */
            if (str_eq(k, "ratio"))         assert(d.entries[i].kind == RUBRAVIEW_INI_FLOAT);
            if (str_eq(k, "flag"))          assert(d.entries[i].kind == RUBRAVIEW_INI_BOOL);
        }
    }
    printf("  [PASS] Old files read: BOM, bare text, ';' comments; quoted strings mean their contents\n");

    /* Test 7 (D-13): what is written is the subset — text quoted with its
       two escapes, typed values bare, a repeated key once with its last
       value, floats with a digit on each side of the point. */
    {
        rubraview_ini_doc_t w = {0};
        rubraview_ini_set_string(&arena, &w, lit("video"), lit("decoder"), lit("ffmpeg"));
        rubraview_ini_set_string(&arena, &w, lit("files"), lit("folder"), lit("2024"));
        rubraview_ini_set_string(&arena, &w, lit("files"), lit("path"), lit("C:\\a \"b\""));
        rubraview_ini_set_int(&arena, &w, lit("video"), lit("subtitle_size"), 48);
        rubraview_ini_set_float(&arena, &w, lit("video"), lit("step"), 2.0);
        rubraview_ini_set_float(&arena, &w, lit("video"), lit("half"), 0.5);
        rubraview_ini_set_bool(&arena, &w, lit("video"), lit("gpu"), true);
        rubraview_ini_set(&arena, &w, lit("video"), lit("guessed"), lit("12"));

        u8str_t out = rubraview_ini_serialize(&arena, &w);
        const char *want =
            "[video]\n"
            "decoder = \"ffmpeg\"\n"
            "subtitle_size = 48\n"
            "step = 2.0\n"
            "half = 0.5\n"
            "gpu = true\n"
            "guessed = 12\n"
            "[files]\n"
            "folder = \"2024\"\n"
            "path = \"C:\\\\a \\\"b\\\"\"\n";
        if (!str_eq(out, want)) {
            fprintf(stderr, "---- got ----\n%.*s---- want ----\n%s", (int)out.len, out.ptr, want);
        }
        assert(str_eq(out, want));

        /* A key a hand edit repeated is written once, with its last value
           — TOML refuses a repeat. */
        rubraview_ini_doc_t rep = rubraview_ini_parse(&arena, lit("[s]\na = 1\nb = 2\na = 3\n"));
        u8str_t once = rubraview_ini_serialize(&arena, &rep);
        assert(str_eq(once, "[s]\nb = 2\na = 3\n"));

        assert(rubraview_ini_name_ok(lit("subtitle_size")));
        assert(!rubraview_ini_name_ok(lit("Subtitle")));   /* INI folds case; the subset is lower case */
        assert(!rubraview_ini_name_ok(lit("a.b")));        /* a dotted key is a TOML table */
        assert(!rubraview_ini_name_ok(lit("")));
    }
    printf("  [PASS] Written files are the subset: quoted text, bare typed values, one key once\n");

    free(raw_mem);
    printf("[test_ini] All tests passed successfully!\n");
    return 0;
}
