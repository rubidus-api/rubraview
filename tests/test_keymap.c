#include "rubraview/keymap.h"
#include "rubraview/default_keymap.h"
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

    /* §3.22.2 / D-14: changing bindings in place. */
    {
        size_t big_size = 1024 * 1024;
        void *big = malloc(big_size);
        assert(big != NULL);
        proven_arena_t a = proven_arena_create((proven_mem_mut_t){ .ptr = big, .size = big_size });
        rubraview_key_combo_t ctrl_o = { .modifiers = RUBRAVIEW_MOD_CTRL, .key_name = lit("O") };
        rubraview_key_combo_t j = { .key_name = lit("j") };   /* names compare without case */

        /* Where two contexts meet: the three asked in turn, and a context with itself. */
        assert(rubraview_keymap_contexts_meet(lit(""), lit("navigation")));
        assert(rubraview_keymap_contexts_meet(lit("view"), lit("navigation")));
        assert(rubraview_keymap_contexts_meet(lit("animation"), lit("animation")));
        assert(!rubraview_keymap_contexts_meet(lit("animation"), lit("navigation")));
        assert(!rubraview_keymap_contexts_meet(lit("animation"), lit("subpage")));
        assert(!rubraview_keymap_contexts_meet(lit("slideshow"), lit("")));

        rubraview_keymap_t km = rubraview_keymap_parse(&a, lit("[ui]\nopen_picker = \"O, Ctrl+O\"\ntoggle_osd = \"I\"\n"
                                                              "[navigation]\nnext_page = \"Right, J\"\n"
                                                              "[animation]\nanim_toggle_pause = \"Space\"\n"));
        rubraview_keymap_t saved = rubraview_keymap_copy(&a, &km);
        assert(rubraview_keymap_equal(&km, &saved));

        const rubraview_key_binding_t *holder = NULL;
        /* toggle_osd (global) wants Ctrl+O: open_picker has it — refused, and named. */
        assert(rubraview_keymap_bind(&a, &km, 1, ctrl_o, &holder) == RUBRAVIEW_BIND_TAKEN);
        assert(holder && str_eq(holder->action, "open_picker"));
        /* J is next_page's in navigation, which meets the global section. */
        assert(rubraview_keymap_bind(&a, &km, 1, j, &holder) == RUBRAVIEW_BIND_TAKEN && str_eq(holder->action, "next_page"));
        /* But the animation context may give J a meaning of its own. */
        assert(rubraview_keymap_bind(&a, &km, 3, j, &holder) == RUBRAVIEW_BIND_ADDED && holder == NULL);
        assert(rubraview_keymap_bind(&a, &km, 3, j, NULL) == RUBRAVIEW_BIND_ALREADY);
        assert(km.bindings[3].combo_count == 2 && str_eq(km.bindings[3].combos[1].key_name, "j"));
        /* The copy Revert keeps did not move. */
        assert(saved.bindings[3].combo_count == 1 && !rubraview_keymap_equal(&km, &saved));

        rubraview_key_combo_t f7 = { .modifiers = RUBRAVIEW_MOD_SHIFT | RUBRAVIEW_MOD_ALT, .key_name = lit("F7") };
        assert(rubraview_keymap_bind(&a, &km, 1, f7, NULL) == RUBRAVIEW_BIND_ADDED);
        char text[32];
        assert(str_eq(rubraview_key_combo_format(text, sizeof(text), f7), "Shift+Alt+F7"));

        /* Written and read back, the same keymap; the global section is [ui]. */
        u8str_t written = rubraview_keymap_serialize(&a, &km);
        assert(strstr(written.ptr, "[ui]\n") == written.ptr);
        assert(strstr(written.ptr, "toggle_osd = \"I, Shift+Alt+F7\"\n"));
        assert(strstr(written.ptr, "anim_toggle_pause = \"Space, j\"\n"));
        rubraview_keymap_t reread = rubraview_keymap_parse(&a, written);
        assert(rubraview_keymap_equal(&km, &reread));

        /* Delete takes the last key off, one at a time, and says when none is left. */
        assert(rubraview_keymap_unbind_last(&km, 1) && km.bindings[1].combo_count == 1);
        assert(rubraview_keymap_unbind_last(&km, 1) && !rubraview_keymap_unbind_last(&km, 1));
        assert(!rubraview_keymap_unbind_last(&km, 99));
        assert(rubraview_keymap_bind(&a, &km, 99, f7, NULL) == RUBRAVIEW_BIND_FAILED);

        /* The whole shipped keymap goes round the writer unchanged. */
        rubraview_keymap_t shipped = rubraview_keymap_parse(&a, lit(rubraview_default_keymap()));
        rubraview_keymap_t round = rubraview_keymap_parse(&a, rubraview_keymap_serialize(&a, &shipped));
        assert(shipped.count > 60 && rubraview_keymap_equal(&shipped, &round));
        free(big);
    }
    /* D-16: a keymap.ini saved while the context was [animation] loads as [media]. */
    {
        rubraview_keymap_t old = rubraview_keymap_parse(&arena, lit("[animation]\nanim_toggle_pause = \"Space\"\n"));
        assert(old.count == 1 && str_eq(old.bindings[0].context, "media"));
        rubraview_key_combo_t space = { .key_name = lit("Space") };
        assert(str_eq(rubraview_keymap_find_action(&old, lit("media"), space), "anim_toggle_pause"));
    }
    printf("  [PASS] Bindings change in place: refused where another action is reached, written and read back unchanged\n");

    free(raw_mem);
    printf("[test_keymap] All tests passed successfully!\n");
    return 0;
}
