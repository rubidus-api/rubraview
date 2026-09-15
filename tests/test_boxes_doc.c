#include "rubraview/boxes_doc.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static u8str_t lit(const char *s) { return (u8str_t){ .ptr = s, .len = strlen(s) }; }
static bool is(u8str_t s, const char *t) { return s.len == strlen(t) && memcmp(s.ptr, t, s.len) == 0; }

/* Children contiguous and in range, as rubraview_menu_tree_t needs. */
static void assert_tree_sound(const rubraview_menu_tree_t *t) {
    for (size_t i = 0; i < t->item_count; ++i) {
        const rubraview_menu_item_t *it = &t->items[i];
        if (it->child_count > 0) {
            assert(it->first_child > (int32_t)i);
            assert((size_t)(it->first_child + it->child_count) <= t->item_count);
        }
    }
}

static const rubraview_menu_item_t *root_named(const rubraview_menu_tree_t *t, const char *label) {
    for (int32_t i = 0; i < t->root_count; ++i) if (is(t->items[t->root_first + i].label, label)) return &t->items[t->root_first + i];
    return NULL;
}

static const rubraview_menu_item_t *child_named(const rubraview_menu_tree_t *t, const rubraview_menu_item_t *m, const char *label) {
    for (int32_t i = 0; i < m->child_count; ++i) if (is(t->items[m->first_child + i].label, label)) return &t->items[m->first_child + i];
    return NULL;
}

int main(void) {
    printf("[test_boxes_doc] Starting floating box document tests...\n");

    /* 1. The built-in document parses; each profile has what RFC-0002 §4 lists. */
    const rubraview_boxes_doc_t *doc = rubraview_boxes_document();
    if (doc->error) fprintf(stderr, "line %u: %s\n", doc->error_line, doc->error);
    assert(!doc->error);
    static const struct { const char *name; int32_t count; const char *first, *last; } PROFILES[] = {
        { "video", 12, "media_play_pause", "toggle_fullscreen" },
        { "music", 11, "prev_page", "media_speed_cycle" },
        { "animation", 8, "prev_page", "toggle_fullscreen" },
        { "multipage", 8, "prev_page", "toggle_fullscreen" },
        { "archive", 8, "prev_page", "toggle_fullscreen" },
        { "image", 8, "prev_page", "toggle_fullscreen" },
        { "slideshow", 1, "toggle_slideshow", "toggle_slideshow" },
    };
    assert(doc->profile_count == sizeof(PROFILES) / sizeof(PROFILES[0]));
    for (size_t i = 0; i < doc->profile_count; ++i) {
        const rubraview_toolbox_profile_t *p = rubraview_boxes_profile(doc, lit(PROFILES[i].name));
        assert(p && p->tile_count == PROFILES[i].count);
        assert(is(doc->tiles[p->first_tile].action, PROFILES[i].first));
        assert(is(doc->tiles[p->first_tile + p->tile_count - 1].action, PROFILES[i].last));
    }
    /* Today's toolbox, unchanged, is the image profile. */
    const rubraview_toolbox_profile_t *image = rubraview_boxes_profile(doc, lit("image"));
    static const char *const TODAY[] = { "prev_page", "next_page", "zoom_out", "zoom_in", "actual_size", "rotate_cw", "toggle_slideshow", "toggle_fullscreen" };
    for (int32_t i = 0; i < 8; ++i) assert(is(doc->tiles[image->first_tile + i].action, TODAY[i]));
    assert(rubraview_boxes_profile(doc, lit("nope")) == NULL);
    printf("  [PASS] The built-in document parses, with every toolbox profile RFC-0002 lists\n");

    /* 2. The menu for a picture: no Playback; File first, with Recent filled in. */
    static unsigned char memory[1 << 16];
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = memory, .size = sizeof(memory) });
    rubraview_recent_entry_t recent[3] = {
        { lit("One Piece 01.cbz"), lit("open_recent:0") },
        { lit("photos"), lit("open_recent:1") },
        { lit("film.mkv"), lit("open_recent:2") },
    };
    rubraview_menu_tree_t still = rubraview_boxes_menu(&arena, doc, 0, recent, 3, 11);
    assert_tree_sound(&still);
    assert(still.root_count == 4);
    assert(is(still.items[0].label, "File") && is(still.items[1].label, "View") &&
           is(still.items[2].label, "Show") && is(still.items[3].label, "Help"));
    assert(!root_named(&still, "Playback"));
    const rubraview_menu_item_t *file = root_named(&still, "File");
    assert(file->child_count == 11);
    assert(is(still.items[file->first_child].action, "open_picker"));
    const rubraview_menu_item_t *rec = child_named(&still, file, "Recent");
    assert(rec && rec->child_count == 3 && rec->action.len == 0);
    assert(is(still.items[rec->first_child + 2].label, "film.mkv") && is(still.items[rec->first_child + 2].action, "open_recent:2"));
    const rubraview_menu_item_t *view = root_named(&still, "View");
    const rubraview_menu_item_t *fit = child_named(&still, view, "Fit");
    assert(fit && fit->child_count == 7 && is(still.items[fit->first_child + 6].action, "toggle_fit_lock"));

    /* A menu module walks it as before: File, then Recent, then an entry. */
    rubraview_menu_state_t state = rubraview_menu_create(&still);
    u8str_t action = {0};
    assert(rubraview_menu_activate(&state, 0, &action) == RUBRAVIEW_MENU_DESCENDED);   /* File */
    int32_t recent_tile = 1 + 2;                                                        /* Back, Open file, Open folder, Recent */
    assert(rubraview_menu_activate(&state, recent_tile, &action) == RUBRAVIEW_MENU_DESCENDED);
    assert(rubraview_menu_activate(&state, 1, &action) == RUBRAVIEW_MENU_ACTIVATED && is(action, "open_recent:0"));
    printf("  [PASS] A picture's menu: File with Recent filled in, no Playback\n");

    /* 3. With a video or music page on screen, Playback appears third, and
          an empty reading history leaves Recent empty but present. */
    proven_arena_reset(&arena);
    rubraview_menu_tree_t media = rubraview_boxes_menu(&arena, doc, RUBRAVIEW_BOXES_WHEN_MEDIA, NULL, 0, 11);
    assert_tree_sound(&media);
    assert(media.root_count == 5 && is(media.items[2].label, "Playback"));
    const rubraview_menu_item_t *play = root_named(&media, "Playback");
    assert(play->child_count == 11 && is(media.items[play->first_child].action, "media_play_pause"));
    const rubraview_menu_item_t *vol = child_named(&media, play, "Volume");
    assert(vol && vol->child_count == 3);
    assert(child_named(&media, root_named(&media, "File"), "Recent")->child_count == 0);
    /* A level larger than the box can show keeps its first tiles. */
    proven_arena_reset(&arena);
    rubraview_menu_tree_t cut = rubraview_boxes_menu(&arena, doc, RUBRAVIEW_BOXES_WHEN_MEDIA, NULL, 0, 3);
    assert_tree_sound(&cut);
    assert(cut.root_count == 3 && root_named(&cut, "File")->child_count == 3);
    /* An arena too small gives an empty tree, not a broken one. */
    static unsigned char tiny[64];
    proven_arena_t small = proven_arena_create((proven_mem_mut_t){ .ptr = tiny, .size = sizeof(tiny) });
    assert(rubraview_boxes_menu(&small, doc, 0, NULL, 0, 11).item_count == 0);
    printf("  [PASS] Playback appears with media; long levels are cut; a small arena gives nothing\n");

    /* 4. Mistakes are named with their line. */
    static const struct { const char *text; uint32_t line; const char *says; } BAD[] = {
        { "tile zoom_in \"Zoom\"\n", 1, "before any" },
        { "toolbox a\n  tile zoom_in Zoom\n", 2, "caption" },
        { "menu \"File\"\n  item open_picker \"Open\"\n", 1, "never closed" },
        { "end\n", 1, "no open" },
        { "menu \"P\" when music\nend\n", 1, "only condition" },
        { "toolbox a\ntoolbox a\n", 2, "already defined" },
        { "menu \"F\"\n  item Open-File \"x\"\nend\n", 2, "action id" },
        { "sparkle\n", 1, "unknown keyword" },
        { "menu \"F\"\n  item open_picker \"Open\" extra\nend\n", 2, "more on the line" },
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); ++i) {
        rubraview_boxes_doc_t bad;
        assert(!rubraview_boxes_doc_parse(lit(BAD[i].text), &bad));
        if (bad.error_line != BAD[i].line || !strstr(bad.error, BAD[i].says)) {
            fprintf(stderr, "case %zu: line %u \"%s\"\n", i, bad.error_line, bad.error);
        }
        assert(bad.error_line == BAD[i].line && strstr(bad.error, BAD[i].says));
    }
    rubraview_boxes_doc_t ok;
    assert(rubraview_boxes_doc_parse(lit("# only a comment\n\nmenu \"A\"  # trailing\n  recent\nend\n"), &ok) && ok.node_count == 2);
    printf("  [PASS] A mistake names its line and what is wrong\n");

    printf("[test_boxes_doc] All tests passed successfully!\n");
    return 0;
}
