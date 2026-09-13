#include "rubraview/ui_settings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static u8str_t lit(const char *s) { return (u8str_t){ .ptr = s, .len = strlen(s) }; }

static bool has(u8str_t s, const char *needle) {
    size_t n = strlen(needle);
    for (size_t i = 0; i + n <= s.len; ++i) if (memcmp(s.ptr + i, needle, n) == 0) return true;
    return false;
}

/* Cells, the way the grid counts them: one per code point here (the
   strings in this test hold no wide characters). */
static int32_t code_points(u8str_t s) {
    int32_t n = 0;
    for (size_t i = 0; i < s.len; ++i) if (((unsigned char)s.ptr[i] & 0xC0) != 0x80) n++;
    return n;
}

static int32_t line_for(const rubraview_settings_view_t *v, const char *section, const char *key) {
    for (size_t i = 0; i < v->line_count; ++i) {
        const rubraview_settings_node_t *n = &v->doc->nodes[v->lines[i].node];
        if (n->kind != RUBRAVIEW_NODE_SETTING) continue;
        const rubraview_setting_def_t *d = &v->doc->defs[n->setting];
        if (d->section.len == strlen(section) && memcmp(d->section.ptr, section, d->section.len) == 0 &&
            d->key.len == strlen(key) && memcmp(d->key.ptr, key, d->key.len) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

static size_t fake_info(void *user, u8str_t source, char *buffer, size_t capacity) {
    (void)user;
    const char *text = (source.len == 12 && memcmp(source.ptr, "media.ffmpeg", 12) == 0) ? "8.1, beside the program" : "";
    size_t n = strlen(text);
    if (n >= capacity) n = capacity - 1;
    memcpy(buffer, text, n);
    return n;
}

int main(void) {
    printf("[test_ui_settings] Starting settings window interpreter tests...\n");
    const rubraview_settings_doc_t *doc = rubraview_settings_document();
    assert(!doc->error);

    /* 1. The first page is laid out from the document: headings with a
          blank row before all but the first, one row per setting, and the
          focus on the first setting. */
    {
        rubraview_settings_view_t v = rubraview_settings_view_create(doc, 80, 24);
        assert(v.page == 0 && v.list_cols == 12 && v.content_col == 14);
        assert(v.line_count == 11);
        assert(v.lines[0].kind == RUBRAVIEW_LINE_SECTION && v.lines[0].row == 0);
        assert(v.lines[1].kind == RUBRAVIEW_LINE_SETTING && v.lines[1].row == 1);
        assert(v.lines[4].kind == RUBRAVIEW_LINE_ACTION && v.lines[4].row == 4);
        assert(v.lines[6].kind == RUBRAVIEW_LINE_SECTION && v.lines[6].row == 7);   /* a blank row at 6 */
        assert(v.lines[10].kind == RUBRAVIEW_LINE_INFO && v.lines[10].row == 12);
        assert(v.content_height == 13);
        assert(v.focus_line == 1 && v.focus_button == RUBRAVIEW_BUTTON_NONE);
        assert(rubraview_settings_view_screen_row(&v, 1) == 3);
    }
    printf("  [PASS] A page is laid out from the document, focus on its first setting\n");

    /* 2. Keys move the focus and change values, and a key that changes
          nothing says so. */
    {
        rubraview_settings_t s = rubraview_settings_defaults();
        rubraview_settings_view_t v = rubraview_settings_view_create(doc, 80, 24);
        /* on "On startup", already at its last choice */
        assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_RIGHT) == RUBRAVIEW_SEVENT_NONE);
        assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_LEFT) == RUBRAVIEW_SEVENT_CHANGED);
        assert(rubraview_settings_get(&s, lit("general"), lit("startup")) == 1.0);
        assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_ENTER) == RUBRAVIEW_SEVENT_CHANGED);   /* cycles on */
        assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_ENTER) == RUBRAVIEW_SEVENT_CHANGED);   /* and round */
        assert(rubraview_settings_get(&s, lit("general"), lit("startup")) == 0.0);

        assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_DOWN) == RUBRAVIEW_SEVENT_MOVED);
        assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_SPACE) == RUBRAVIEW_SEVENT_CHANGED);
        assert(rubraview_settings_get(&s, lit("general"), lit("single_instance")) == 0.0);

        /* the numbers further down step by their own step, ten at a time on PageUp */
        int32_t hide = line_for(&v, "general", "titlebar_hide_ms");
        while (v.focus_line != hide) assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_DOWN) == RUBRAVIEW_SEVENT_MOVED);
        assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_RIGHT) == RUBRAVIEW_SEVENT_CHANGED);
        assert(rubraview_settings_get(&s, lit("general"), lit("titlebar_hide_ms")) == 550.0);
        assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_PAGE_UP) == RUBRAVIEW_SEVENT_CHANGED);
        assert(rubraview_settings_get(&s, lit("general"), lit("titlebar_hide_ms")) == 1050.0);

        /* past the last setting, the buttons; Up comes back */
        assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_DOWN) == RUBRAVIEW_SEVENT_MOVED);
        assert(v.focus_line == -1 && v.focus_button == RUBRAVIEW_BUTTON_REVERT);
        assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_RIGHT) == RUBRAVIEW_SEVENT_MOVED);
        assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_ENTER) == RUBRAVIEW_SEVENT_DEFAULTS);
        assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_UP) == RUBRAVIEW_SEVENT_MOVED);
        assert(v.focus_line == hide);

        assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_TAB) == RUBRAVIEW_SEVENT_MOVED && v.page == 1);
        assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_SHIFT_TAB) == RUBRAVIEW_SEVENT_MOVED && v.page == 0);
        assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_SHIFT_TAB) == RUBRAVIEW_SEVENT_MOVED &&
               v.page == (int32_t)doc->page_count - 1);   /* wraps */
        assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_ESCAPE) == RUBRAVIEW_SEVENT_CLOSE);
    }
    printf("  [PASS] Keys move the focus, change values by their step, and reach the buttons\n");

    /* 3. The pointer: a page from the list, a toggle by a click, a number
          by where it is clicked and dragged, a button by its cells. */
    {
        rubraview_settings_t s = rubraview_settings_defaults();
        rubraview_settings_view_t v = rubraview_settings_view_create(doc, 80, 24);
        assert(rubraview_settings_view_press(&v, &s, 3, 2 + RUBRAVIEW_TAB_VIDEO) == RUBRAVIEW_SEVENT_MOVED);
        assert(v.page == RUBRAVIEW_TAB_VIDEO);

        int32_t size = line_for(&v, "video", "subtitle_size");
        int32_t row = rubraview_settings_view_screen_row(&v, (size_t)size);
        assert(row >= 2);
        assert(rubraview_settings_view_press(&v, &s, 78, row) == RUBRAVIEW_SEVENT_CHANGED);     /* far right: the top */
        assert(rubraview_settings_get(&s, lit("video"), lit("subtitle_size")) == 72.0);
        assert(rubraview_settings_view_drag(&v, &s, v.content_col + v.label_cols + 1) == RUBRAVIEW_SEVENT_CHANGED);
        assert(rubraview_settings_get(&s, lit("video"), lit("subtitle_size")) == 10.0);
        rubraview_settings_view_release(&v);
        assert(rubraview_settings_view_drag(&v, &s, 78) == RUBRAVIEW_SEVENT_NONE);   /* released: no longer follows */

        int32_t gpu = line_for(&v, "video", "hardware_decode");
        assert(rubraview_settings_view_press(&v, &s, v.content_col + v.label_cols + 1,
                                             rubraview_settings_view_screen_row(&v, (size_t)gpu)) == RUBRAVIEW_SEVENT_CHANGED);
        assert(rubraview_settings_get(&s, lit("video"), lit("hardware_decode")) == 0.0);

        int32_t col = 0, width = 0;
        rubraview_settings_view_button_cells(&v, RUBRAVIEW_BUTTON_CLOSE, &col, &width);
        assert(width == 9);
        assert(rubraview_settings_view_press(&v, &s, col + 2, 23) == RUBRAVIEW_SEVENT_CLOSE);
        rubraview_settings_view_button_cells(&v, RUBRAVIEW_BUTTON_REVERT, &col, &width);
        assert(rubraview_settings_view_press(&v, &s, col, 23) == RUBRAVIEW_SEVENT_REVERT);
    }
    printf("  [PASS] Clicks pick a page, flip a toggle, set and drag a number, press a button\n");

    /* 4. Each line's text is exactly as wide as the page, whatever it holds. */
    {
        rubraview_settings_t s = rubraview_settings_defaults();
        rubraview_settings_set_text(&s, lit("curation"), lit("dir_1"), lit("D:\\Keep"));
        rubraview_settings_view_t v = rubraview_settings_view_create(doc, 80, 24);
        rubraview_settings_sources_t sources = { .info = fake_info };
        char buffer[512];
        int32_t width = 80 - v.content_col - 1;

        rubraview_settings_view_set_page(&v, RUBRAVIEW_TAB_VIDEO);
        for (size_t i = 0; i < v.line_count; ++i) {
            u8str_t t = rubraview_settings_line_text(&v, &s, &sources, i, 0, buffer, sizeof(buffer));
            if (code_points(t) != width) fprintf(stderr, "line %zu is %d cells: [%.*s]\n", i, code_points(t), (int)t.len, t.ptr);
            assert(code_points(t) == width);
        }
        u8str_t section = rubraview_settings_line_text(&v, &s, &sources, 0, 0, buffer, sizeof(buffer));
        assert(has(section, "\xE2\x94\x80\xE2\x94\x80 Decoding "));
        u8str_t decoder = rubraview_settings_line_text(&v, &s, &sources, (size_t)line_for(&v, "video", "decoder"), 0, buffer, sizeof(buffer));
        assert(has(decoder, "Decoder") && has(decoder, "< windows >  1/2"));
        u8str_t gpu = rubraview_settings_line_text(&v, &s, &sources, (size_t)line_for(&v, "video", "hardware_decode"), 0, buffer, sizeof(buffer));
        assert(has(gpu, "[x] on"));
        u8str_t size = rubraview_settings_line_text(&v, &s, &sources, (size_t)line_for(&v, "video", "subtitle_size"), 0, buffer, sizeof(buffer));
        assert(has(size, "[####") && has(size, "] 24 pt"));
        u8str_t info = rubraview_settings_line_text(&v, &s, &sources, 3, 0, buffer, sizeof(buffer));
        assert(has(info, "FFmpeg") && has(info, "8.1, beside the program"));

        rubraview_settings_view_set_page(&v, RUBRAVIEW_TAB_FILES);
        u8str_t folder = rubraview_settings_line_text(&v, &s, &sources, (size_t)line_for(&v, "curation", "dir_1"), 0, buffer, sizeof(buffer));
        assert(has(folder, "[D:\\Keep") && code_points(folder) == width);
        u8str_t empty = rubraview_settings_line_text(&v, &s, &sources, (size_t)line_for(&v, "curation", "dir_2"), 0, buffer, sizeof(buffer));
        assert(has(empty, "(not set)"));

        u8str_t tab = rubraview_settings_page_text(&v, RUBRAVIEW_TAB_FILES, buffer, sizeof(buffer));
        assert(has(tab, " > Files") && code_points(tab) == v.list_cols - 1);
        tab = rubraview_settings_page_text(&v, RUBRAVIEW_TAB_VIDEO, buffer, sizeof(buffer));
        assert(has(tab, "   Video"));
    }
    printf("  [PASS] Every line's text is exactly the page's width, with what it shows\n");

    /* 5. A short window scrolls, and the focus never leaves the screen. */
    {
        rubraview_settings_t s = rubraview_settings_defaults();
        rubraview_settings_view_t v = rubraview_settings_view_create(doc, 80, 10);
        rubraview_settings_view_set_page(&v, RUBRAVIEW_TAB_FILES);
        assert(rubraview_settings_view_visible_rows(&v) == 6 && v.content_height > 6);
        while (rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_DOWN) == RUBRAVIEW_SEVENT_MOVED && v.focus_line >= 0) {
            assert(rubraview_settings_view_screen_row(&v, (size_t)v.focus_line) >= 2);
        }
        assert(v.focus_button == RUBRAVIEW_BUTTON_REVERT && v.scroll > 0);
        assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_HOME) == RUBRAVIEW_SEVENT_MOVED);
        assert(v.scroll <= 1 && rubraview_settings_view_screen_row(&v, (size_t)v.focus_line) >= 2);

        /* growing the window keeps what is in sight */
        rubraview_settings_view_resize(&v, 100, 40);
        assert(v.scroll == 0 && v.cols == 100);
        assert(rubraview_settings_view_scroll(&v, 5) == RUBRAVIEW_SEVENT_NONE);   /* nothing to scroll */
    }
    printf("  [PASS] A short window scrolls and keeps the focus on screen\n");

    /* 6. An action line is focused like a setting and pressed like a
          button; Delete empties a path and nothing else. */
    {
        rubraview_settings_t s = rubraview_settings_defaults();
        rubraview_settings_view_t v = rubraview_settings_view_create(doc, 80, 24);
        while (!(v.focus_line >= 0 && v.lines[v.focus_line].kind == RUBRAVIEW_LINE_ACTION)) {
            assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_DOWN) == RUBRAVIEW_SEVENT_MOVED);
        }
        const rubraview_settings_node_t *focused = rubraview_settings_view_focused_node(&v);
        assert(focused && focused->kind == RUBRAVIEW_NODE_ACTION && focused->name.len == 14 &&
               memcmp(focused->name.ptr, "shell.register", 14) == 0);
        uint32_t before = s.revision_total;
        assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_ENTER) == RUBRAVIEW_SEVENT_ACTION);
        assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_RIGHT) == RUBRAVIEW_SEVENT_NONE);
        assert(s.revision_total == before);   /* an action holds no value */
        char buffer[256];
        u8str_t text = rubraview_settings_line_text(&v, &s, NULL, (size_t)v.focus_line, 0, buffer, sizeof(buffer));
        assert(has(text, "< Register file types >"));

        size_t next = (size_t)v.focus_line + 1;
        assert(rubraview_settings_view_press(&v, &s, v.content_col + v.label_cols + 2,
                                             rubraview_settings_view_screen_row(&v, next)) == RUBRAVIEW_SEVENT_ACTION);
        assert(rubraview_settings_view_focused_node(&v)->name.len == 16);   /* shell.unregister */

        assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_DELETE) == RUBRAVIEW_SEVENT_NONE);
        rubraview_settings_view_set_page(&v, RUBRAVIEW_TAB_FILES);
        int32_t folder = line_for(&v, "curation", "dir_1");
        while (v.focus_line != folder) assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_DOWN) == RUBRAVIEW_SEVENT_MOVED);
        assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_DELETE) == RUBRAVIEW_SEVENT_CLEAR_TEXT);
        assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_ENTER) == RUBRAVIEW_SEVENT_EDIT_TEXT);
        assert(rubraview_settings_view_key(&v, &s, RUBRAVIEW_SKEY_LEFT) == RUBRAVIEW_SEVENT_NONE);
    }
    printf("  [PASS] Actions are focused and pressed; Delete empties a path and nothing else\n");

    /* 7. A table is as tall as the rows the viewer says it has, so the
          last binding can be scrolled to — not only the first dozen. */
    {
        rubraview_settings_view_t v = rubraview_settings_view_create(doc, 80, 24);
        rubraview_settings_view_set_page(&v, RUBRAVIEW_TAB_KEYS);
        size_t table = v.line_count;
        for (size_t i = 0; i < v.line_count; ++i) if (v.lines[i].kind == RUBRAVIEW_LINE_TABLE) table = i;
        assert(table < v.line_count && v.lines[table].height == RUBRAVIEW_TABLE_ROWS);
        rubraview_settings_view_set_table_rows(&v, 67);
        assert(v.lines[table].height == 67);
        assert(v.content_height == v.lines[table].row + 67);
        int32_t visible = rubraview_settings_view_visible_rows(&v);
        assert(rubraview_settings_view_scroll(&v, 1000) == RUBRAVIEW_SEVENT_MOVED);
        assert(v.scroll == v.content_height - visible);    /* the last row is the bottom one on screen */
        rubraview_settings_view_set_table_rows(&v, 0);
        assert(v.lines[table].height == 1 && v.scroll <= v.content_height);
    }
    printf("  [PASS] A table is as tall as its rows, and its last row can be scrolled to\n");

    printf("[test_ui_settings] All tests passed successfully!\n");
    return 0;
}
