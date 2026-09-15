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
        /* Owner, 2026-09-09: paging is these keys and nothing else. The
           arrows, J/K and A/D used to be here and were removed on
           request; §3.7.2 lists them, and this row set is the deliberate
           departure from it. */
        { "navigation", RUBRAVIEW_MOD_NONE,  "PageDown",     "next_page" },
        { "navigation", RUBRAVIEW_MOD_NONE,  "Space",        "next_page" },
        { "navigation", RUBRAVIEW_MOD_NONE,  "Enter",        "next_page" },
        { "navigation", RUBRAVIEW_MOD_NONE,  "PageUp",       "prev_page" },
        { "navigation", RUBRAVIEW_MOD_NONE,  "Backspace",    "prev_page" },
        { "navigation", RUBRAVIEW_MOD_SHIFT, "Space",        "prev_page" },
        { "navigation", RUBRAVIEW_MOD_NONE,  "Home",         "first_page" },
        { "navigation", RUBRAVIEW_MOD_CTRL,  "Home",         "first_page" },
        { "navigation", RUBRAVIEW_MOD_NONE,  "End",          "last_page" },
        { "navigation", RUBRAVIEW_MOD_CTRL,  "End",          "last_page" },
        { "navigation", RUBRAVIEW_MOD_SHIFT, "Right",        "skip_forward" },
        { "navigation", RUBRAVIEW_MOD_CTRL,  "PageDown",     "skip_forward" },
        { "navigation", RUBRAVIEW_MOD_SHIFT, "Left",         "skip_backward" },
        { "navigation", RUBRAVIEW_MOD_CTRL,  "PageUp",       "skip_backward" },
        { "navigation", RUBRAVIEW_MOD_CTRL,  "Backspace",    "up_to_folder" },   /* D-16: Ctrl+Up sizes the window */

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
        /* D-16: Alt+arrows move the window, Ctrl+arrows size it. */
        { "view", RUBRAVIEW_MOD_ALT,  "Left",   "window_move_left" },
        { "view", RUBRAVIEW_MOD_ALT,  "Up",     "window_move_up" },
        { "view", RUBRAVIEW_MOD_CTRL, "Right",  "window_wider" },
        { "view", RUBRAVIEW_MOD_CTRL, "Down",   "window_taller" },
        /* D-16: while a video or music page is on screen the arrows play. */
        { "media", RUBRAVIEW_MOD_NONE, "Right", "media_seek_forward" },
        { "media", RUBRAVIEW_MOD_NONE, "Left",  "media_seek_back" },
        { "media", RUBRAVIEW_MOD_NONE, "Up",    "media_volume_up" },
        { "media", RUBRAVIEW_MOD_NONE, "Down",  "media_volume_down" },
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
        { "view", RUBRAVIEW_MOD_NONE, "F2",     "rename_file" },     /* not the toolbox: D-14 */
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
            /* Flushed before the abort: an assert kills the process
               with the message still sitting in the buffer, and a
               failure nobody can read is barely a failure report. */
            printf("  [FAIL] %s is not bound to anything\n", EXPECTED[i].key_name);
            fflush(stdout);
            assert(action.len != 0);
        }
        size_t expected_len = strlen(EXPECTED[i].action);
        if (action.len != expected_len || memcmp(action.ptr, EXPECTED[i].action, expected_len) != 0) {
            printf("  [FAIL] %s resolved to '%.*s', expected '%s'\n",
                   EXPECTED[i].key_name, (int)action.len, action.ptr, EXPECTED[i].action);
            fflush(stdout);
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

        /* A and D no longer page — the owner asked for a short, explicit
           list of keys that move between files, and these are not on it.
           They are free now; S still starts the slide show. */
        assert(a_action.len == 0);
        assert(d_action.len == 0);
        assert(s_action.len == 16 && memcmp(s_action.ptr, "toggle_slideshow", 16) == 0);
    }
    printf("  [PASS] A and D no longer page; S still starts the slide show\n");

    /* §3.20's rows exist, and the chords the specification spends twice
       resolve by context rather than by one of the two meanings being
       dropped: Space and Ctrl+[ / Ctrl+] mean the animation while one is
       playing, and page turning and archive stepping everywhere else. */
    {
        rubraview_key_combo_t space = { .modifiers = RUBRAVIEW_MOD_NONE, .key_name = lit("Space") };
        rubraview_key_combo_t ctrl_rb = { .modifiers = RUBRAVIEW_MOD_CTRL, .key_name = lit("BracketRight") };
        rubraview_key_combo_t ctrl_lb = { .modifiers = RUBRAVIEW_MOD_CTRL, .key_name = lit("BracketLeft") };
        rubraview_key_combo_t period = { .modifiers = RUBRAVIEW_MOD_NONE, .key_name = lit("Period") };
        rubraview_key_combo_t comma = { .modifiers = RUBRAVIEW_MOD_NONE, .key_name = lit("Comma") };

        u8str_t nav_space = resolve(&keymap, "navigation", space);
        assert(nav_space.len == 9 && memcmp(nav_space.ptr, "next_page", 9) == 0);
        u8str_t anim_space = resolve(&keymap, "media", space);
        assert(anim_space.len == 16 && memcmp(anim_space.ptr, "media_play_pause", 16) == 0);

        u8str_t nav_next = resolve(&keymap, "navigation", ctrl_rb);
        assert(nav_next.len == 12 && memcmp(nav_next.ptr, "next_archive", 12) == 0);
        u8str_t nav_prev = resolve(&keymap, "navigation", ctrl_lb);
        assert(nav_prev.len == 12 && memcmp(nav_prev.ptr, "prev_archive", 12) == 0);

        u8str_t anim_faster = resolve(&keymap, "media", ctrl_rb);
        assert(anim_faster.len == 13 && memcmp(anim_faster.ptr, "anim_speed_up", 13) == 0);
        u8str_t anim_slower = resolve(&keymap, "media", ctrl_lb);
        assert(anim_slower.len == 15 && memcmp(anim_slower.ptr, "anim_speed_down", 15) == 0);

        u8str_t step_fwd = resolve(&keymap, "media", period);
        assert(step_fwd.len == 17 && memcmp(step_fwd.ptr, "anim_step_forward", 17) == 0);
        u8str_t sub_next = resolve(&keymap, "subpage", period);
        assert(sub_next.len == 12 && memcmp(sub_next.ptr, "subpage_next", 12) == 0);
        u8str_t sub_prev = resolve(&keymap, "subpage", comma);
        assert(sub_prev.len == 12 && memcmp(sub_prev.ptr, "subpage_prev", 12) == 0);
    }
    printf("  [PASS] §3.20's animation and sub-page rows resolve, by context, without losing the global meanings\n");

    /* The wheel must not turn pages (owner, 2026-09-09). That is not a
       binding, so it is checked where it lives — in the pointer model —
       but the keys that *do* turn pages are asserted here, and this is
       the list. Anything not on it must not page. */
    {
        const char *must_not_page[] = { "Right", "Left", "J", "K", "A", "D", "Up", "Down" };
        for (size_t i = 0; i < sizeof(must_not_page) / sizeof(must_not_page[0]); ++i) {
            rubraview_key_combo_t combo = { .modifiers = RUBRAVIEW_MOD_NONE,
                                            .key_name = lit(must_not_page[i]) };
            u8str_t action = resolve(&keymap, "navigation", combo);
            bool pages = (action.len == 9 && memcmp(action.ptr, "next_page", 9) == 0) ||
                         (action.len == 9 && memcmp(action.ptr, "prev_page", 9) == 0);
            if (pages) {
                printf("  [FAIL] %s still turns pages\n", must_not_page[i]);
                fflush(stdout);
                assert(false);
            }
        }
    }
    printf("  [PASS] Only the named keys turn pages; the arrows and letters do not\n");

    free(raw);
    printf("[test_default_keymap] All tests passed successfully!\n");
    return 0;
}
