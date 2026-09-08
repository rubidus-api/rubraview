#include "rubraview/keymap.h"
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
    printf("[test_keymap] Starting keymap action table / dispatcher unit tests...\n");

    size_t mem_size = 64 * 1024;
    void *raw_mem = malloc(mem_size);
    assert(raw_mem != NULL);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw_mem, .size = mem_size });

    /* The exact keymap.ini example from RFC-0001 §3.7.5, plus one global
       (no-section) binding and one multi-modifier combo to exercise the
       fuller parsing surface. */
    const char *ini_text =
        "toggle_pixel_grid = G\n"
        "\n"
        "[navigation]\n"
        "next_page = Right, PageDown, Space, J, D\n"
        "prev_page = Left, PageUp, Shift+Space, K, A\n"
        "skip_forward = Shift+Right\n"
        "skip_backward = Shift+Left\n"
        "open_folder = Ctrl+Shift+O\n"
        "\n"
        "[media]\n"
        "play_pause = Space, P\n"
        "frame_step_fwd = Period\n"
        "frame_step_back = Comma\n"
        "set_loop_a = BracketLeft\n"
        "set_loop_b = BracketRight\n"
        "clear_loop = Backslash\n";

    rubraview_keymap_t keymap = rubraview_keymap_parse(&arena, lit(ini_text));

    /* Test 1: next_page has 5 combos, all with no modifiers. */
    {
        const rubraview_key_binding_t *b = rubraview_keymap_find_binding(&keymap, lit("navigation"), lit("next_page"));
        assert(b != NULL);
        assert(b->combo_count == 5);
        assert(b->combos[0].modifiers == RUBRAVIEW_MOD_NONE);
        assert(str_eq(b->combos[0].key_name, "Right"));
        assert(str_eq(b->combos[3].key_name, "J"));
    }
    printf("  [PASS] Comma-separated combo list parses into 5 unmodified combos\n");

    /* Test 2: A single-modifier combo ("Shift+Space") parses correctly
       among prev_page's combos. */
    {
        const rubraview_key_binding_t *b = rubraview_keymap_find_binding(&keymap, lit("navigation"), lit("prev_page"));
        assert(b != NULL);
        bool found_shift_space = false;
        for (size_t i = 0; i < b->combo_count; ++i) {
            if (b->combos[i].modifiers == RUBRAVIEW_MOD_SHIFT && str_eq(b->combos[i].key_name, "Space")) {
                found_shift_space = true;
            }
        }
        assert(found_shift_space);
    }
    printf("  [PASS] Single-modifier combo (Shift+Space) parses correctly\n");

    /* Test 3: A multi-modifier combo ("Ctrl+Shift+O") accumulates both
       modifier bits and leaves the correct terminal key name. */
    {
        const rubraview_key_binding_t *b = rubraview_keymap_find_binding(&keymap, lit("navigation"), lit("open_folder"));
        assert(b != NULL && b->combo_count == 1);
        assert(b->combos[0].modifiers == (RUBRAVIEW_MOD_CTRL | RUBRAVIEW_MOD_SHIFT));
        assert(str_eq(b->combos[0].key_name, "O"));
    }
    printf("  [PASS] Multi-modifier combo (Ctrl+Shift+O) accumulates both modifiers\n");

    /* Test 4: Reverse lookup — pressing "Right" (no modifiers) in the
       "navigation" context resolves to "next_page". */
    {
        rubraview_key_combo_t pressed = { .modifiers = RUBRAVIEW_MOD_NONE, .key_name = lit("Right") };
        u8str_t action = rubraview_keymap_find_action(&keymap, lit("navigation"), pressed);
        assert(str_eq(action, "next_page"));
    }
    printf("  [PASS] Reverse lookup resolves a plain key press to its action\n");

    /* Test 5: Reverse lookup is case-insensitive on the key name. */
    {
        rubraview_key_combo_t pressed = { .modifiers = RUBRAVIEW_MOD_NONE, .key_name = lit("right") };
        u8str_t action = rubraview_keymap_find_action(&keymap, lit("navigation"), pressed);
        assert(str_eq(action, "next_page"));
    }
    printf("  [PASS] Reverse lookup is case-insensitive on the key name\n");

    /* Test 6: Modifiers must match exactly — plain "Space" does not
       resolve to prev_page (which requires Shift+Space) in navigation;
       it resolves to next_page's own plain "Space" binding instead. */
    {
        rubraview_key_combo_t pressed = { .modifiers = RUBRAVIEW_MOD_NONE, .key_name = lit("Space") };
        u8str_t action = rubraview_keymap_find_action(&keymap, lit("navigation"), pressed);
        assert(str_eq(action, "next_page"));

        rubraview_key_combo_t shift_pressed = { .modifiers = RUBRAVIEW_MOD_SHIFT, .key_name = lit("Space") };
        u8str_t shift_action = rubraview_keymap_find_action(&keymap, lit("navigation"), shift_pressed);
        assert(str_eq(shift_action, "prev_page"));
    }
    printf("  [PASS] Modifier bits are matched exactly, not ignored\n");

    /* Test 7: A binding in the ini's global section (before any
       [section]) is found even when querying an unrelated context —
       global bindings are the fallback tier. */
    {
        rubraview_key_combo_t pressed = { .modifiers = RUBRAVIEW_MOD_NONE, .key_name = lit("G") };
        u8str_t action = rubraview_keymap_find_action(&keymap, lit("media"), pressed);
        assert(str_eq(action, "toggle_pixel_grid"));
    }
    printf("  [PASS] Global (no-section) bindings resolve as a fallback in any context\n");

    /* Test 8: A context-specific binding takes precedence over a global
       binding for the identical key combo. */
    {
        rubraview_keymap_t shadow_keymap = rubraview_keymap_parse(&arena, lit(
            "toggle = G\n"
            "[special]\n"
            "special_action = G\n"
        ));
        rubraview_key_combo_t pressed = { .modifiers = RUBRAVIEW_MOD_NONE, .key_name = lit("G") };
        u8str_t action = rubraview_keymap_find_action(&shadow_keymap, lit("special"), pressed);
        assert(str_eq(action, "special_action")); /* context-specific wins, not the global "toggle" */
    }
    printf("  [PASS] Context-specific binding shadows a global binding for the same combo\n");

    /* Test 9: An unbound key combo resolves to an empty action, no crash. */
    {
        rubraview_key_combo_t pressed = { .modifiers = RUBRAVIEW_MOD_ALT, .key_name = lit("Z") };
        u8str_t action = rubraview_keymap_find_action(&keymap, lit("navigation"), pressed);
        assert(action.len == 0);
    }
    printf("  [PASS] Unbound key combo resolves to an empty action\n");

    /* Test 10: A symbolic key name that isn't a real modifier word (e.g.
       "BracketLeft") is left whole, not mis-parsed. */
    {
        const rubraview_key_binding_t *b = rubraview_keymap_find_binding(&keymap, lit("media"), lit("set_loop_a"));
        assert(b != NULL && b->combo_count == 1);
        assert(b->combos[0].modifiers == RUBRAVIEW_MOD_NONE);
        assert(str_eq(b->combos[0].key_name, "BracketLeft"));
    }
    printf("  [PASS] Non-modifier symbolic key names are left intact\n");

    free(raw_mem);
    printf("[test_keymap] All tests passed successfully!\n");
    return 0;
}
