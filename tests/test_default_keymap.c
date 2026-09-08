#include "rubraview/default_keymap.h"
#include "rubraview/keymap.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

/*
 * RFC-0001 §11.2 makes M3 done only when "every row of the §3.7.2 hotkey
 * table that belongs to Navigation, Zoom & Fit, Book & Manga, Slideshow
 * and UI & Windows performs its action". This test turns that sentence
 * into a gate: each expected chord must resolve, through the same
 * lookup the application uses, to the action named here.
 *
 * Media Playback rows are M5 and the curves/batch rows M6, so they are
 * deliberately absent — as is `Ctrl+]` (next archive), which is M4.
 */

typedef struct binding_expectation {
    const char *context;   /* the keymap section the app searches */
    uint32_t    modifiers;
    const char *key_name;
    const char *action;
} binding_expectation_t;

static u8str_t lit(const char *s) {
    return (u8str_t){ .ptr = s, .len = strlen(s) };
}

/* The application's dispatch order: the viewing context first, then the
   view section, then the slide show, then the global section. */
static u8str_t resolve(const rubraview_keymap_t *keymap, const char *context, rubraview_key_combo_t combo) {
    u8str_t action = rubraview_keymap_find_action(keymap, lit(context), combo);
    if (action.len == 0 && strcmp(context, "navigation") != 0) {
        action = rubraview_keymap_find_action(keymap, lit("navigation"), combo);
    }
    if (action.len == 0) action = rubraview_keymap_find_action(keymap, lit("view"), combo);
    if (action.len == 0) action = rubraview_keymap_find_action(keymap, lit("slideshow"), combo);
    return action;
}

int main(void) {
    printf("[test_default_keymap] Verifying the §3.7.2 rows M3 owns are all bound...\n");

    size_t mem_size = 128 * 1024;
    void *raw = malloc(mem_size);
    assert(raw != NULL);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw, .size = mem_size });

    rubraview_keymap_t keymap = rubraview_keymap_parse(&arena, lit(rubraview_default_keymap()));
    assert(keymap.count > 0);

    const binding_expectation_t EXPECTED[] = {
        /* Navigation */
        { "navigation", RUBRAVIEW_MOD_NONE,  "Right",        "next_page" },
        { "navigation", RUBRAVIEW_MOD_NONE,  "PageDown",     "next_page" },
        { "navigation", RUBRAVIEW_MOD_NONE,  "Space",        "next_page" },
        { "navigation", RUBRAVIEW_MOD_NONE,  "J",            "next_page" },
        { "navigation", RUBRAVIEW_MOD_NONE,  "D",            "next_page" },
        { "navigation", RUBRAVIEW_MOD_NONE,  "Left",         "prev_page" },
        { "navigation", RUBRAVIEW_MOD_NONE,  "PageUp",       "prev_page" },
        { "navigation", RUBRAVIEW_MOD_SHIFT, "Space",        "prev_page" },
        { "navigation", RUBRAVIEW_MOD_NONE,  "K",            "prev_page" },
        { "navigation", RUBRAVIEW_MOD_NONE,  "A",            "prev_page" },
        { "navigation", RUBRAVIEW_MOD_NONE,  "Home",         "first_page" },
        { "navigation", RUBRAVIEW_MOD_CTRL,  "Home",         "first_page" },
        { "navigation", RUBRAVIEW_MOD_NONE,  "End",          "last_page" },
        { "navigation", RUBRAVIEW_MOD_CTRL,  "End",          "last_page" },
        { "navigation", RUBRAVIEW_MOD_SHIFT, "Right",        "skip_forward" },
        { "navigation", RUBRAVIEW_MOD_CTRL,  "PageDown",     "skip_forward" },
        { "navigation", RUBRAVIEW_MOD_SHIFT, "Left",         "skip_backward" },
        { "navigation", RUBRAVIEW_MOD_CTRL,  "PageUp",       "skip_backward" },
        { "navigation", RUBRAVIEW_MOD_NONE,  "Backspace",    "up_to_folder" },

        /* Zoom & Fit */
        { "view", RUBRAVIEW_MOD_NONE, "Plus",   "zoom_in" },
        { "view", RUBRAVIEW_MOD_NONE, "Minus",  "zoom_out" },
        { "view", RUBRAVIEW_MOD_NONE, "4",      "actual_size" },
        { "view", RUBRAVIEW_MOD_NONE, "0",      "actual_size" },
        { "view", RUBRAVIEW_MOD_CTRL, "0",      "actual_size" },
        { "view", RUBRAVIEW_MOD_NONE, "1",      "fit_window" },
        { "view", RUBRAVIEW_MOD_NONE, "2",      "fit_width" },
        { "view", RUBRAVIEW_MOD_NONE, "3",      "fit_height" },
        { "view", RUBRAVIEW_MOD_CTRL, "1",      "fit_stretch" },
        { "view", RUBRAVIEW_MOD_NONE, "5",      "smart_fit" },
        { "view", RUBRAVIEW_MOD_NONE, "L",      "toggle_fit_lock" },
        { "view", RUBRAVIEW_MOD_ALT,  "Left",   "pan_left" },
        { "view", RUBRAVIEW_MOD_ALT,  "Right",  "pan_right" },
        { "view", RUBRAVIEW_MOD_ALT,  "Up",     "pan_up" },
        { "view", RUBRAVIEW_MOD_ALT,  "Down",   "pan_down" },
        { "view", RUBRAVIEW_MOD_NONE, "R",      "rotate_cw" },
        { "view", RUBRAVIEW_MOD_SHIFT,"R",      "rotate_ccw" },
        { "view", RUBRAVIEW_MOD_NONE, "H",      "flip_horizontal" },
        { "view", RUBRAVIEW_MOD_NONE, "V",      "flip_vertical" },
        { "view", RUBRAVIEW_MOD_NONE, "G",      "toggle_pixel_grid" },

        /* Book & Manga */
        { "navigation", RUBRAVIEW_MOD_NONE,  "B", "toggle_layout" },
        { "navigation", RUBRAVIEW_MOD_NONE,  "M", "toggle_reading_order" },
        { "navigation", RUBRAVIEW_MOD_SHIFT, "B", "toggle_spread_detect" },

        /* Slideshow */
        { "slideshow", RUBRAVIEW_MOD_NONE,  "S",            "toggle_slideshow" },
        { "slideshow", RUBRAVIEW_MOD_NONE,  "F5",           "toggle_slideshow" },
        { "slideshow", RUBRAVIEW_MOD_NONE,  "BracketRight", "interval_up" },
        { "slideshow", RUBRAVIEW_MOD_NONE,  "BracketLeft",  "interval_down" },
        { "slideshow", RUBRAVIEW_MOD_SHIFT, "BracketRight", "interval_up_fine" },
        { "slideshow", RUBRAVIEW_MOD_SHIFT, "BracketLeft",  "interval_down_fine" },

        /* UI & Windows */
        { "view", RUBRAVIEW_MOD_NONE, "F",      "toggle_fullscreen" },
        { "view", RUBRAVIEW_MOD_NONE, "F11",    "toggle_fullscreen" },
        { "view", RUBRAVIEW_MOD_ALT,  "Enter",  "toggle_fullscreen" },
        { "view", RUBRAVIEW_MOD_NONE, "Tab",    "toggle_menu" },
        { "view", RUBRAVIEW_MOD_NONE, "F1",     "toggle_menu" },
        { "view", RUBRAVIEW_MOD_NONE, "T",      "toggle_toolbox" },
        { "view", RUBRAVIEW_MOD_NONE, "F2",     "toggle_toolbox" },
        { "view", RUBRAVIEW_MOD_NONE, "O",      "open_picker" },
        { "view", RUBRAVIEW_MOD_CTRL, "O",      "open_picker" },
        { "view", RUBRAVIEW_MOD_CTRL | RUBRAVIEW_MOD_SHIFT, "O", "open_folder" },
        { "view", RUBRAVIEW_MOD_NONE, "F4",     "toggle_filmstrip" },
        { "view", RUBRAVIEW_MOD_NONE, "I",      "toggle_osd" },
        { "view", RUBRAVIEW_MOD_NONE, "Escape", "quit" },
    };

    size_t count = sizeof(EXPECTED) / sizeof(EXPECTED[0]);
    for (size_t i = 0; i < count; ++i) {
        rubraview_key_combo_t combo = {
            .modifiers = EXPECTED[i].modifiers,
            .key_name = lit(EXPECTED[i].key_name),
        };
        u8str_t action = resolve(&keymap, EXPECTED[i].context, combo);

        if (action.len == 0) {
            printf("  [FAIL] %s is not bound to anything\n", EXPECTED[i].key_name);
            assert(action.len != 0);
        }
        size_t expected_len = strlen(EXPECTED[i].action);
        if (action.len != expected_len || memcmp(action.ptr, EXPECTED[i].action, expected_len) != 0) {
            printf("  [FAIL] %s resolved to '%.*s', expected '%s'\n",
                   EXPECTED[i].key_name, (int)action.len, action.ptr, EXPECTED[i].action);
            assert(false);
        }
    }
    printf("  [PASS] All %zu hotkey rows in M3's scope resolve to their action\n", count);

    /* An unbound chord must stay unbound, so a passing sweep above means
       the bindings exist rather than everything matching everything. */
    {
        rubraview_key_combo_t unbound = { .modifiers = RUBRAVIEW_MOD_CTRL | RUBRAVIEW_MOD_ALT, .key_name = lit("Q") };
        assert(resolve(&keymap, "navigation", unbound).len == 0);
    }
    printf("  [PASS] An unbound chord still resolves to nothing\n");

    /* The chords the RFC double-books are resolved deliberately, not by
       accident: navigation and the slide show keep their primaries. */
    {
        rubraview_key_combo_t a = { .modifiers = RUBRAVIEW_MOD_NONE, .key_name = lit("A") };
        rubraview_key_combo_t d = { .modifiers = RUBRAVIEW_MOD_NONE, .key_name = lit("D") };
        rubraview_key_combo_t s_key = { .modifiers = RUBRAVIEW_MOD_NONE, .key_name = lit("S") };

        u8str_t a_action = resolve(&keymap, "navigation", a);
        u8str_t d_action = resolve(&keymap, "navigation", d);
        u8str_t s_action = resolve(&keymap, "navigation", s_key);

        assert(a_action.len == 9 && memcmp(a_action.ptr, "prev_page", 9) == 0);
        assert(d_action.len == 9 && memcmp(d_action.ptr, "next_page", 9) == 0);
        assert(s_action.len == 16 && memcmp(s_action.ptr, "toggle_slideshow", 16) == 0);
    }
    printf("  [PASS] A, D and S keep their primary bindings over panning\n");

    free(raw);
    printf("[test_default_keymap] All tests passed successfully!\n");
    return 0;
}
