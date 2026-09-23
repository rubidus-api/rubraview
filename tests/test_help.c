/* The F1 help (owner, 2026-09-23). What matters is that the keys it
   shows are the keys that are in force — not a list written by hand that
   drifts — and that each one is called what the menu calls it. */
#include "rubraview/help.h"
#include "rubraview/default_keymap.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char memory[1 << 18];

static u8str_t lit(const char *s) { return (u8str_t){ .ptr = s, .len = strlen(s) }; }

static bool is(u8str_t s, const char *text) {
    size_t n = strlen(text);
    return s.len == n && (n == 0 || memcmp(s.ptr, text, n) == 0);
}

/* The first entry for an action, or NULL. */
static const rubraview_help_line_t *entry_for(const rubraview_help_line_t *lines, size_t count, const char *label) {
    for (size_t i = 0; i < count; ++i) {
        if (lines[i].kind == RUBRAVIEW_HELP_ENTRY && is(lines[i].text, label)) return &lines[i];
    }
    return NULL;
}

static bool has_heading(const rubraview_help_line_t *lines, size_t count, const char *text) {
    for (size_t i = 0; i < count; ++i) {
        if (lines[i].kind == RUBRAVIEW_HELP_HEADING && is(lines[i].text, text)) return true;
    }
    return false;
}

int main(void) {
    printf("[test_help] Starting help document tests...\n");

    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = memory, .size = sizeof(memory) });
    static rubraview_help_line_t lines[512];

    /* 1. Built from the viewer's own keymap and boxes document. */
    {
        rubraview_keymap_t keymap = rubraview_keymap_parse(&arena, lit(rubraview_default_keymap()));
        const rubraview_boxes_doc_t *boxes = rubraview_boxes_document();
        size_t count = rubraview_help_build(&arena, &keymap, boxes, lines, 512);
        assert(count > 20);

        /* The groups a reader looks for, in the order they are written. */
        assert(has_heading(lines, count, "Everywhere"));
        assert(has_heading(lines, count, "Film and music"));
        size_t everywhere = 0, media = 0;
        for (size_t i = 0; i < count; ++i) {
            if (lines[i].kind != RUBRAVIEW_HELP_HEADING) continue;
            if (is(lines[i].text, "Everywhere")) everywhere = i;
            if (is(lines[i].text, "Film and music")) media = i;
        }
        assert(everywhere < media);

        /* An action the menu names is called what the menu calls it, and
           its keys are the ones bound to it. */
        const rubraview_help_line_t *settings = entry_for(lines, count, "Settings");
        assert(settings && settings->keys.len > 0);
        assert(memchr(settings->keys.ptr, 'F', settings->keys.len) != NULL);   /* F10 */

        /* Every entry has both halves, and no heading is empty. */
        for (size_t i = 0; i < count; ++i) {
            if (lines[i].kind == RUBRAVIEW_HELP_ENTRY) assert(lines[i].keys.len > 0 && lines[i].text.len > 0);
            if (lines[i].kind == RUBRAVIEW_HELP_HEADING) assert(lines[i].text.len > 0);
        }
    }
    printf("  [PASS] The help is built from the keymap in force, grouped the way a reader reads\n");

    /* 2. A rebound key shows its new binding; an unbound action is absent. */
    {
        static const char INI[] =
            "[]\n"
            "quit = \"Ctrl+Q\"\n"
            "\n"
            "[media]\n"
            "media_play_pause = \"K, Space\"\n";
        rubraview_keymap_t keymap = rubraview_keymap_parse(&arena, lit(INI));
        size_t count = rubraview_help_build(&arena, &keymap, rubraview_boxes_document(), lines, 512);

        const rubraview_help_line_t *play = entry_for(lines, count, "Play/Pause");
        assert(play && is(play->keys, "K, Space"));   /* both, in the order they were bound */
        assert(entry_for(lines, count, "Next page") == NULL);
        assert(has_heading(lines, count, "Everywhere") && has_heading(lines, count, "Film and music"));
    }
    printf("  [PASS] The keys shown are the ones bound, several to an action if so\n");

    /* 3. Without the boxes document an action is named by its own id. */
    {
        static const char INI[] = "[]\nnext_page = \"Right\"\n";
        rubraview_keymap_t keymap = rubraview_keymap_parse(&arena, lit(INI));
        size_t count = rubraview_help_build(&arena, &keymap, NULL, lines, 512);
        const rubraview_help_line_t *next = entry_for(lines, count, "Next page");
        assert(next && is(next->keys, "Right"));
    }
    printf("  [PASS] An action no box names is spelled out from its own id\n");

    /* 4. It never writes past the room it is given. */
    {
        rubraview_keymap_t keymap = rubraview_keymap_parse(&arena, lit(rubraview_default_keymap()));
        size_t count = rubraview_help_build(&arena, &keymap, rubraview_boxes_document(), lines, 5);
        assert(count == 5);
        assert(rubraview_help_build(&arena, &keymap, NULL, lines, 0) == 0);
        assert(rubraview_help_build(&arena, NULL, NULL, lines, 512) == 0);
    }
    printf("  [PASS] The caller's room is respected, and nothing at all is fine\n");

    printf("[test_help] All tests passed successfully!\n");
    return 0;
}
