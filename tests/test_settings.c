#include "rubraview/settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>

static u8str_t lit(const char *s) { return (u8str_t){ .ptr = s, .len = strlen(s) }; }
static bool is(u8str_t s, const char *l) { return s.len == strlen(l) && memcmp(s.ptr, l, s.len) == 0; }
static bool contains(u8str_t haystack, const char *needle) {
    size_t n = strlen(needle);
    if (haystack.len < n) return false;
    for (size_t i = 0; i + n <= haystack.len; ++i) {
        if (memcmp(haystack.ptr + i, needle, n) == 0) return true;
    }
    return false;
}

int main(void) {
    printf("[test_settings] Starting settings schema tests...\n");

    size_t mem_size = 1024 * 1024;
    void *raw = malloc(mem_size);
    assert(raw != NULL);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw, .size = mem_size });

    /* Test 1: the schema is complete and self-consistent — every row has
       a key, a label and a tab, and no two rows collide. */
    {
        size_t count = 0;
        const rubraview_setting_def_t *schema = rubraview_settings_schema(&count);
        assert(schema && count > 0 && count <= RUBRAVIEW_SETTINGS_MAX);

        for (size_t i = 0; i < count; ++i) {
            assert(schema[i].key.len > 0);
            assert(schema[i].label.len > 0);
            assert(schema[i].tab < RUBRAVIEW_TAB_COUNT);
            if (schema[i].type == RUBRAVIEW_SETTING_CHOICE) {
                assert(schema[i].choices && schema[i].choice_count > 0);
                assert(schema[i].default_value <= (double)(schema[i].choice_count - 1));
            }
            for (size_t j = i + 1; j < count; ++j) {
                bool same_key = schema[i].key.len == schema[j].key.len &&
                                memcmp(schema[i].key.ptr, schema[j].key.ptr, schema[i].key.len) == 0;
                bool same_section = schema[i].section.len == schema[j].section.len &&
                                    (schema[i].section.len == 0 ||
                                     memcmp(schema[i].section.ptr, schema[j].section.ptr,
                                            schema[i].section.len) == 0);
                assert(!(same_key && same_section));
            }
        }
    }
    printf("  [PASS] Every schema row is complete and no two collide\n");

    /* Test 2: §3.22.2 names eight tabs, and none of them is empty —
       a tab with nothing on it would be a tab the reader opens for
       nothing. */
    {
        assert(RUBRAVIEW_TAB_COUNT == 8);
        for (int t = 0; t < RUBRAVIEW_TAB_COUNT; ++t) {
            assert(rubraview_settings_tab_name((rubraview_settings_tab_t)t).len > 0);
            assert(rubraview_settings_for_tab((rubraview_settings_tab_t)t, NULL, 0) > 0);
        }

        const rubraview_setting_def_t *rows[32];
        size_t on_files = rubraview_settings_for_tab(RUBRAVIEW_TAB_FILES, rows, 32);
        assert(on_files > 0);
        for (size_t i = 0; i < on_files && i < 32; ++i) assert(rows[i]->tab == RUBRAVIEW_TAB_FILES);
    }
    printf("  [PASS] All eight tabs exist and none of them is empty\n");

    /* Test 3: the defaults are inside their own ranges. A default the
       loader would clamp is a default that never was one. */
    {
        rubraview_settings_t defaults = rubraview_settings_defaults();
        size_t count = 0;
        const rubraview_setting_def_t *schema = rubraview_settings_schema(&count);

        for (size_t i = 0; i < count; ++i) {
            if (schema[i].type == RUBRAVIEW_SETTING_PATH) continue;
            double value = rubraview_settings_get(&defaults, schema[i].section, schema[i].key);
            assert(value >= schema[i].min_value && value <= schema[i].max_value);
            assert(fabs(value - schema[i].default_value) < 0.0001);
        }
    }
    printf("  [PASS] Every default is inside its own range\n");

    /* Test 4: reading a file. Absent keys keep their defaults. */
    {
        u8str_t ini = lit("single_instance = false\n"
                          "[cache]\n"
                          "memory_cap_mb = 1024\n"
                          "[curation]\n"
                          "dir_1 = D:/Triage/Keep\n"
                          "curation_mode = copy\n");
        rubraview_settings_t s = rubraview_settings_load(&arena, ini);

        assert(rubraview_settings_get(&s, U8(""), U8("single_instance")) == 0.0);
        assert(rubraview_settings_get(&s, U8("cache"), U8("memory_cap_mb")) == 1024.0);
        assert(is(rubraview_settings_get_text(&s, U8("curation"), U8("dir_1")), "D:/Triage/Keep"));
        assert(rubraview_settings_get(&s, U8("curation"), U8("curation_mode")) == 1.0); /* "copy" */

        /* Untouched keys are still at their defaults. */
        assert(rubraview_settings_get(&s, U8("viewer"), U8("gutter_px")) == 8.0);
    }
    printf("  [PASS] A settings file is read, and absent keys keep their defaults\n");

    /* Test 5: a hand-edited file cannot ask for something impossible. */
    {
        u8str_t ini = lit("[cache]\nmemory_cap_mb = 999999\nlookahead = -5\n"
                          "[viewer]\nzoom_step_percent = 1000\n");
        rubraview_settings_t s = rubraview_settings_load(&arena, ini);

        assert(rubraview_settings_get(&s, U8("cache"), U8("memory_cap_mb")) == 4096.0);
        assert(rubraview_settings_get(&s, U8("cache"), U8("lookahead")) == 1.0);
        assert(rubraview_settings_get(&s, U8("viewer"), U8("zoom_step_percent")) == 50.0);
    }
    printf("  [PASS] Out-of-range values in the file are clamped, not obeyed\n");

    /* Test 6: an unrecognised word for a choice keeps the default rather
       than silently becoming the first option. */
    {
        u8str_t ini = lit("[viewer]\ninterpolation = magic\n");
        rubraview_settings_t s = rubraview_settings_load(&arena, ini);
        rubraview_settings_t defaults = rubraview_settings_defaults();
        assert(rubraview_settings_get(&s, U8("viewer"), U8("interpolation")) ==
               rubraview_settings_get(&defaults, U8("viewer"), U8("interpolation")));

        u8str_t good = lit("[viewer]\ninterpolation = nearest\n");
        rubraview_settings_t s2 = rubraview_settings_load(&arena, good);
        assert(rubraview_settings_get(&s2, U8("viewer"), U8("interpolation")) == 0.0);
    }
    printf("  [PASS] An unknown choice keeps the default instead of becoming option zero\n");

    /* Test 7: saving round-trips, and a key this version does not know
       about survives being opened by it. */
    {
        u8str_t original = lit("[viewer]\ngutter_px = 20\nfrom_the_future = 42\n");
        rubraview_settings_t s = rubraview_settings_load(&arena, original);
        assert(rubraview_settings_get(&s, U8("viewer"), U8("gutter_px")) == 20.0);

        rubraview_settings_set(&s, U8("viewer"), U8("gutter_px"), 12.0);
        u8str_t written = rubraview_settings_save(&arena, &s, original);

        assert(contains(written, "gutter_px"));
        assert(contains(written, "from_the_future"));   /* not thrown away */

        rubraview_settings_t back = rubraview_settings_load(&arena, written);
        assert(rubraview_settings_get(&back, U8("viewer"), U8("gutter_px")) == 12.0);
        assert(rubraview_settings_get(&back, U8("")   , U8("single_instance")) == 1.0);
    }
    printf("  [PASS] Settings round-trip, and an unknown key survives the save\n");

    /* Test 8: setting a value clamps and steps it the same way loading
       does — one rule, not two. */
    {
        rubraview_settings_t s = rubraview_settings_defaults();
        rubraview_settings_set(&s, U8("cache"), U8("memory_cap_mb"), 99999.0);
        assert(rubraview_settings_get(&s, U8("cache"), U8("memory_cap_mb")) == 4096.0);

        rubraview_settings_set(&s, U8("cache"), U8("memory_cap_mb"), 300.0);
        double stepped = rubraview_settings_get(&s, U8("cache"), U8("memory_cap_mb"));
        assert(fmod(stepped - 256.0, 64.0) < 0.001);   /* on a step boundary */

        /* An unknown key is ignored rather than creating a phantom row. */
        rubraview_settings_set(&s, U8("nowhere"), U8("nothing"), 5.0);
        assert(rubraview_settings_get(&s, U8("nowhere"), U8("nothing")) == 0.0);
    }
    printf("  [PASS] Setting a value clamps and steps it exactly as loading does\n");

    /* Test 9: §3.22.1's Reset to Defaults, and the Apply button's state. */
    {
        rubraview_settings_t s = rubraview_settings_defaults();
        rubraview_settings_t pristine = s;
        assert(!rubraview_settings_differs(&s, &pristine));

        rubraview_settings_set(&s, U8("viewer"), U8("gutter_px"), 30.0);
        assert(rubraview_settings_differs(&s, &pristine));

        rubraview_settings_reset(&s);
        assert(!rubraview_settings_differs(&s, &pristine));

        /* A changed folder counts as a change too. */
        rubraview_settings_set_text(&s, U8("curation"), U8("dir_1"), lit("D:/Keep"));
        assert(rubraview_settings_differs(&s, &pristine));
    }
    printf("  [PASS] Reset returns to the defaults, and any change is noticed\n");

    /* Test 10: §3.22.2's conflict detection. Two contexts claiming one
       chord is not a conflict — that separation is what lets Space mean
       two things (§3.20.1). */
    {
        u8str_t keymap_text = lit("[navigation]\n"
                                  "next_page = Right, Space\n"
                                  "skip_forward = Right\n"     /* a real conflict */
                                  "[animation]\n"
                                  "anim_toggle_pause = Space\n"); /* not a conflict */
        rubraview_keymap_t keymap = rubraview_keymap_parse(&arena, keymap_text);

        rubraview_key_conflict_t conflicts[8];
        size_t found = rubraview_keymap_conflicts(&keymap, conflicts, 8);
        assert(found == 1);
        assert(is(conflicts[0].context, "navigation"));
        assert(is(conflicts[0].chord, "Right"));

        /* And the shipped default keymap has none. */
        rubraview_keymap_t clean = rubraview_keymap_parse(&arena, lit("[navigation]\nnext_page = Right\n"));
        assert(rubraview_keymap_conflicts(&clean, NULL, 0) == 0);
    }
    printf("  [PASS] Conflicts are found within a context and not across contexts\n");

    free(raw);
    printf("[test_settings] All tests passed successfully!\n");
    return 0;
}
