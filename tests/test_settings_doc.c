#include "rubraview/settings_doc.h"
#include "rubraview/ini.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u8str_t lit(const char *s) { return (u8str_t){ .ptr = s, .len = strlen(s) }; }

static bool eq(u8str_t s, const char *l) {
    size_t n = strlen(l);
    return s.len == n && (n == 0 || memcmp(s.ptr, l, n) == 0);
}

/* What the C table in settings.c said, extracted from it mechanically when
   the table was replaced by the document (D-13) — so retyping 48 rows by
   hand could not quietly change one. General-tab keys moved under
   [general] in the same change; that move is written in here. */
typedef struct expected {
    const char *section, *key;
    rubraview_setting_type_t type;
    rubraview_settings_tab_t tab;
    double def, lo, hi, step;
    const char *choices;   /* "a|b|c" */
    bool wired;
} expected_t;

static const expected_t EXPECTED[] = {
    { "general", "startup", RUBRAVIEW_SETTING_CHOICE, RUBRAVIEW_TAB_GENERAL, 2.0, 0.0, 2.0, 1.0, "blank|last_file|last_folder", false },
    { "general", "single_instance", RUBRAVIEW_SETTING_BOOL, RUBRAVIEW_TAB_GENERAL, 1.0, 0.0, 1.0, 1.0, "", true },
    { "general", "frameless", RUBRAVIEW_SETTING_BOOL, RUBRAVIEW_TAB_GENERAL, 1.0, 0.0, 1.0, 1.0, "", false },
    { "general", "titlebar_trigger_px", RUBRAVIEW_SETTING_INT, RUBRAVIEW_TAB_GENERAL, 12.0, 4.0, 40.0, 1.0, "", false },
    { "general", "titlebar_hide_ms", RUBRAVIEW_SETTING_INT, RUBRAVIEW_TAB_GENERAL, 500.0, 100.0, 3000.0, 50.0, "", false },
    { "viewer", "fit_mode", RUBRAVIEW_SETTING_CHOICE, RUBRAVIEW_TAB_VIEWER, 0.0, 0.0, 5.0, 1.0, "window|width|height|actual|smart|stretch", false },
    { "viewer", "layout", RUBRAVIEW_SETTING_CHOICE, RUBRAVIEW_TAB_VIEWER, 0.0, 0.0, 3.0, 1.0, "single|dual|book|webtoon", false },
    { "viewer", "spread_autosplit", RUBRAVIEW_SETTING_BOOL, RUBRAVIEW_TAB_VIEWER, 1.0, 0.0, 1.0, 1.0, "", false },
    { "viewer", "gutter_px", RUBRAVIEW_SETTING_INT, RUBRAVIEW_TAB_VIEWER, 8.0, 0.0, 32.0, 1.0, "", false },
    { "viewer", "portrait_collapse", RUBRAVIEW_SETTING_BOOL, RUBRAVIEW_TAB_VIEWER, 1.0, 0.0, 1.0, 1.0, "", false },
    { "viewer", "zoom_step_percent", RUBRAVIEW_SETTING_INT, RUBRAVIEW_TAB_VIEWER, 10.0, 5.0, 50.0, 1.0, "", false },
    { "viewer", "interpolation", RUBRAVIEW_SETTING_CHOICE, RUBRAVIEW_TAB_VIEWER, 2.0, 0.0, 3.0, 1.0, "nearest|bilinear|bicubic|lanczos3", false },
    { "viewer", "pixel_grid", RUBRAVIEW_SETTING_BOOL, RUBRAVIEW_TAB_VIEWER, 0.0, 0.0, 1.0, 1.0, "", false },
    { "files", "sort_mode", RUBRAVIEW_SETTING_CHOICE, RUBRAVIEW_TAB_FILES, 0.0, 0.0, 3.0, 1.0, "natural|lexical|date|size", false },
    { "files", "sort_ascending", RUBRAVIEW_SETTING_BOOL, RUBRAVIEW_TAB_FILES, 1.0, 0.0, 1.0, 1.0, "", false },
    { "files", "archive_codepage", RUBRAVIEW_SETTING_CHOICE, RUBRAVIEW_TAB_FILES, 0.0, 0.0, 6.0, 1.0, "auto|utf8|cp949|shift_jis|gbk|big5|cp1252", false },
    { "files", "comicinfo", RUBRAVIEW_SETTING_BOOL, RUBRAVIEW_TAB_FILES, 1.0, 0.0, 1.0, 1.0, "", false },
    { "files", "reading_history", RUBRAVIEW_SETTING_BOOL, RUBRAVIEW_TAB_FILES, 1.0, 0.0, 1.0, 1.0, "", false },
    { "files", "resume_prompt", RUBRAVIEW_SETTING_BOOL, RUBRAVIEW_TAB_FILES, 1.0, 0.0, 1.0, 1.0, "", false },
    { "curation", "curation_mode", RUBRAVIEW_SETTING_CHOICE, RUBRAVIEW_TAB_FILES, 0.0, 0.0, 1.0, 1.0, "move|copy", true },
    { "curation", "dir_1", RUBRAVIEW_SETTING_PATH, RUBRAVIEW_TAB_FILES, 0.0, 0.0, 0.0, 0.0, "", true },
    { "curation", "dir_2", RUBRAVIEW_SETTING_PATH, RUBRAVIEW_TAB_FILES, 0.0, 0.0, 0.0, 0.0, "", true },
    { "curation", "dir_3", RUBRAVIEW_SETTING_PATH, RUBRAVIEW_TAB_FILES, 0.0, 0.0, 0.0, 0.0, "", true },
    { "curation", "dir_4", RUBRAVIEW_SETTING_PATH, RUBRAVIEW_TAB_FILES, 0.0, 0.0, 0.0, 0.0, "", true },
    { "curation", "dir_5", RUBRAVIEW_SETTING_PATH, RUBRAVIEW_TAB_FILES, 0.0, 0.0, 0.0, 0.0, "", true },
    { "curation", "dir_6", RUBRAVIEW_SETTING_PATH, RUBRAVIEW_TAB_FILES, 0.0, 0.0, 0.0, 0.0, "", true },
    { "curation", "dir_7", RUBRAVIEW_SETTING_PATH, RUBRAVIEW_TAB_FILES, 0.0, 0.0, 0.0, 0.0, "", true },
    { "curation", "dir_8", RUBRAVIEW_SETTING_PATH, RUBRAVIEW_TAB_FILES, 0.0, 0.0, 0.0, 0.0, "", true },
    { "curation", "dir_9", RUBRAVIEW_SETTING_PATH, RUBRAVIEW_TAB_FILES, 0.0, 0.0, 0.0, 0.0, "", true },
    { "audio", "volume", RUBRAVIEW_SETTING_INT, RUBRAVIEW_TAB_AUDIO, 100.0, 0.0, 100.0, 5.0, "", true },
    { "audio", "mute", RUBRAVIEW_SETTING_BOOL, RUBRAVIEW_TAB_AUDIO, 0.0, 0.0, 1.0, 1.0, "", true },
    { "audio", "gapless", RUBRAVIEW_SETTING_BOOL, RUBRAVIEW_TAB_AUDIO, 1.0, 0.0, 1.0, 1.0, "", false },
    { "audio", "crossfade_seconds", RUBRAVIEW_SETTING_FLOAT, RUBRAVIEW_TAB_AUDIO, 0.0, 0.0, 5.0, 0.1, "", false },
    { "audio", "replaygain", RUBRAVIEW_SETTING_CHOICE, RUBRAVIEW_TAB_AUDIO, 0.0, 0.0, 2.0, 1.0, "off|track|album", false },
    { "audio", "wasapi_latency_ms", RUBRAVIEW_SETTING_INT, RUBRAVIEW_TAB_AUDIO, 40.0, 20.0, 100.0, 5.0, "", false },
    { "audio", "bgm_pause_on_video", RUBRAVIEW_SETTING_BOOL, RUBRAVIEW_TAB_AUDIO, 1.0, 0.0, 1.0, 1.0, "", false },
    { "video", "decoder", RUBRAVIEW_SETTING_CHOICE, RUBRAVIEW_TAB_VIDEO, 0.0, 0.0, 1.0, 1.0, "windows|ffmpeg", true },
    { "video", "hardware_decode", RUBRAVIEW_SETTING_BOOL, RUBRAVIEW_TAB_VIDEO, 1.0, 0.0, 1.0, 1.0, "", false },
    { "video", "subtitle_size", RUBRAVIEW_SETTING_INT, RUBRAVIEW_TAB_VIDEO, 24.0, 10.0, 72.0, 1.0, "", true },
    { "video", "subtitle_outline", RUBRAVIEW_SETTING_INT, RUBRAVIEW_TAB_VIDEO, 2.0, 0.0, 8.0, 1.0, "", true },
    { "video", "ab_step_seconds", RUBRAVIEW_SETTING_FLOAT, RUBRAVIEW_TAB_VIDEO, 0.5, 0.1, 1.0, 0.1, "", false },
    { "video", "video_wheel_zoom", RUBRAVIEW_SETTING_BOOL, RUBRAVIEW_TAB_VIDEO, 1.0, 0.0, 1.0, 1.0, "", false },
    { "ui", "menubox_opacity", RUBRAVIEW_SETTING_INT, RUBRAVIEW_TAB_DISPLAY, 90.0, 30.0, 100.0, 5.0, "", false },
    { "ui", "toolbox_opacity", RUBRAVIEW_SETTING_INT, RUBRAVIEW_TAB_DISPLAY, 90.0, 30.0, 100.0, 5.0, "", false },
    { "display", "tile_base_px", RUBRAVIEW_SETTING_CHOICE, RUBRAVIEW_TAB_DISPLAY, 1.0, 0.0, 2.0, 1.0, "48|64|96", false },
    { "display", "color_management", RUBRAVIEW_SETTING_BOOL, RUBRAVIEW_TAB_DISPLAY, 1.0, 0.0, 1.0, 1.0, "", false },
    { "display", "accent", RUBRAVIEW_SETTING_CHOICE, RUBRAVIEW_TAB_DISPLAY, 0.0, 0.0, 5.0, 1.0, "crimson|cobalt|emerald|amber|teal|purple", false },
    { "cache", "memory_cap_mb", RUBRAVIEW_SETTING_INT, RUBRAVIEW_TAB_CACHE, 512.0, 256.0, 4096.0, 64.0, "", false },
    { "cache", "lookahead", RUBRAVIEW_SETTING_INT, RUBRAVIEW_TAB_CACHE, 2.0, 1.0, 16.0, 1.0, "", false },
    { "cache", "lookbehind", RUBRAVIEW_SETTING_INT, RUBRAVIEW_TAB_CACHE, 1.0, 0.0, 8.0, 1.0, "", false },
    { "cache", "privacy_clean", RUBRAVIEW_SETTING_BOOL, RUBRAVIEW_TAB_CACHE, 0.0, 0.0, 1.0, 1.0, "", false },
    { "keys", "use_keymap_file", RUBRAVIEW_SETTING_BOOL, RUBRAVIEW_TAB_KEYS, 1.0, 0.0, 1.0, 1.0, "", false },
};

static rubraview_settings_doc_t parse(proven_arena_t *arena, const char *text) {
    return rubraview_settings_doc_parse(arena, lit(text));
}

static void expect_error(proven_arena_t *arena, const char *text, uint32_t line, const char *fragment) {
    rubraview_settings_doc_t d = parse(arena, text);
    if (!d.error || d.error_line != line || !strstr(d.error, fragment)) {
        fprintf(stderr, "for:\n%s\nwanted line %u containing \"%s\", got line %u: %s\n",
                text, line, fragment, d.error_line, d.error ? d.error : "(no error)");
    }
    assert(d.error && d.error_line == line && strstr(d.error, fragment));
}

int main(void) {
    printf("[test_settings_doc] Starting settings document tests...\n");
    size_t size = 1u << 20;
    void *raw = malloc(size);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw, .size = size });

    /* 1. The built-in document parses, and it holds exactly what the C
          table held: every key, type, page, default, range, step, choice
          list and wired flag. */
    {
        const rubraview_settings_doc_t *doc = rubraview_settings_document();
        if (doc->error) fprintf(stderr, "built-in document, line %u: %s\n", doc->error_line, doc->error);
        assert(!doc->error);
        assert(doc->page_count == RUBRAVIEW_TAB_COUNT);
        size_t n = sizeof(EXPECTED) / sizeof(EXPECTED[0]);
        assert(doc->def_count == n);
        for (size_t i = 0; i < n; ++i) {
            const expected_t *e = &EXPECTED[i];
            const rubraview_setting_def_t *d = rubraview_settings_find(lit(e->section), lit(e->key));
            if (!d) fprintf(stderr, "missing %s.%s\n", e->section, e->key);
            assert(d);
            bool same = d->type == e->type && d->tab == e->tab && d->wired == e->wired &&
                        fabs(d->default_value - e->def) < 1e-9 && fabs(d->min_value - e->lo) < 1e-9 &&
                        fabs(d->max_value - e->hi) < 1e-9 && fabs(d->step - e->step) < 1e-9;
            char joined[256] = {0};
            for (int k = 0; k < d->choice_count; ++k) {
                if (k) strcat(joined, "|");
                strcat(joined, d->choices[k]);
            }
            same = same && strcmp(joined, e->choices) == 0;
            if (!same) {
                fprintf(stderr, "%s.%s differs: type %d/%d tab %d/%d def %g/%g range %g..%g/%g..%g step %g/%g choices %s/%s wired %d/%d\n",
                        e->section, e->key, d->type, e->type, d->tab, e->tab, d->default_value, e->def,
                        d->min_value, d->max_value, e->lo, e->hi, d->step, e->step, joined, e->choices, d->wired, e->wired);
            }
            assert(same);
            assert(rubraview_ini_name_ok(d->section) && rubraview_ini_name_ok(d->key));
        }
        assert(eq(rubraview_settings_tab_name(RUBRAVIEW_TAB_VIDEO), "Video"));
    }
    printf("  [PASS] The built-in document holds the 48 settings the C table held and the 4 D-15 added\n");

    /* 2. Every kind of line, and what each one records. */
    {
        rubraview_settings_doc_t d = parse(&arena,
            "# comment\n"
            "page video \"Video & subtitles\"   # trailing comment\n"
            "  section \"Subtitles\"\n"
            "  int    video.size \"Size\" 10..72 step 2 unit \"pt\" = 24 wired\n"
            "  float  video.step \"Step\" 0.1..1.0 unit \"s\" = 0.5\n"
            "  choice video.decoder \"Decoder\" windows | ffmpeg = ffmpeg\n"
            "  toggle video.gpu \"GPU\" = false\n"
            "  path   video.folder \"Folder\"\n"
            "  info   \"FFmpeg\" {media.ffmpeg}\n"
            "  preview subtitle 3\n"
            "page keys \"Keys\"\n"
            "  table keymap\n"
            "  action shell.register \"Register file types\"\n");
        if (d.error) fprintf(stderr, "line %u: %s\n", d.error_line, d.error);
        assert(!d.error);
        assert(d.page_count == 2 && d.def_count == 5 && d.node_count == 12);
        assert(d.nodes[0].kind == RUBRAVIEW_NODE_PAGE && eq(d.nodes[0].name, "video") && eq(d.nodes[0].text, "Video & subtitles"));
        assert(d.nodes[1].kind == RUBRAVIEW_NODE_SECTION && eq(d.nodes[1].text, "Subtitles") && d.nodes[1].line == 3);
        const rubraview_setting_def_t *size_def = &d.defs[d.nodes[2].setting];
        assert(size_def->type == RUBRAVIEW_SETTING_INT && size_def->min_value == 10.0 && size_def->max_value == 72.0);
        assert(size_def->step == 2.0 && size_def->default_value == 24.0 && size_def->wired && eq(size_def->unit, "pt"));
        const rubraview_setting_def_t *step_def = &d.defs[d.nodes[3].setting];
        assert(step_def->type == RUBRAVIEW_SETTING_FLOAT && fabs(step_def->step - 0.1) < 1e-9 && !step_def->wired);
        const rubraview_setting_def_t *dec = &d.defs[d.nodes[4].setting];
        assert(dec->choice_count == 2 && strcmp(dec->choices[1], "ffmpeg") == 0 && dec->choices[2] == NULL);
        assert(dec->default_value == 1.0);
        assert(d.defs[d.nodes[5].setting].type == RUBRAVIEW_SETTING_BOOL && d.defs[d.nodes[5].setting].default_value == 0.0);
        assert(d.defs[d.nodes[6].setting].type == RUBRAVIEW_SETTING_PATH);
        assert(d.nodes[7].kind == RUBRAVIEW_NODE_INFO && eq(d.nodes[7].name, "media.ffmpeg") && eq(d.nodes[7].text, "FFmpeg"));
        assert(d.nodes[8].kind == RUBRAVIEW_NODE_PREVIEW && eq(d.nodes[8].name, "subtitle") && d.nodes[8].rows == 3);
        assert(d.nodes[10].kind == RUBRAVIEW_NODE_TABLE && d.nodes[10].page == 1 && eq(d.nodes[10].name, "keymap"));
        assert(d.nodes[11].kind == RUBRAVIEW_NODE_ACTION && eq(d.nodes[11].name, "shell.register") &&
               eq(d.nodes[11].text, "Register file types"));
    }
    printf("  [PASS] Every kind of line is read, with what it declares\n");

    /* 3. A mistake names its line and stops; nothing is guessed. */
    expect_error(&arena, "toggle a.b \"x\" = true\n", 1, "page");
    expect_error(&arena, "page p \"P\"\n\nwidget a.b \"x\"\n", 3, "not a kind");
    expect_error(&arena, "page p \"P\"\nint a.b \"x\" 10-20 = 12\n", 2, "range");
    expect_error(&arena, "page p \"P\"\nint a.b \"x\" 10..20 = 30\n", 2, "outside the range");
    expect_error(&arena, "page p \"P\"\nchoice a.b \"x\" one | two = three\n", 2, "one of its choices");
    expect_error(&arena, "page p \"P\"\nchoice a.b \"x\" one = one\n", 2, "two choices");
    expect_error(&arena, "page p \"P\"\ntoggle a.b \"x\" = maybe\n", 2, "true or false");
    expect_error(&arena, "page p \"P\"\ntoggle a.b \"x\" = true\ntoggle a.b \"y\" = false\n", 3, "already");
    expect_error(&arena, "page p \"P\"\ntoggle A.b \"x\" = true\n", 2, "a-z");
    expect_error(&arena, "page p \"P\nsection \"S\"\n", 1, "not closed");
    expect_error(&arena, "page p \"P\"\ninfo \"FFmpeg\" {media.ffmpeg\n", 2, "not closed");
    expect_error(&arena, "page p \"P\"\nint a.b \"x\" 1..9\n", 2, "default");
    expect_error(&arena, "page p \"P\"\naction shell.register\n", 2, "action needs");
    expect_error(&arena, "# nothing\n", 0, "no page");
    printf("  [PASS] A mistake names its line and stops\n");

    /* 4. The store counts changes, so the window and the viewer redraw
          only what moved — and a set that changes nothing is not a change. */
    {
        rubraview_settings_t s = rubraview_settings_defaults();
        uint32_t before = s.revision_total;
        rubraview_settings_set(&s, lit("video"), lit("subtitle_size"), 24.0);   /* already 24 */
        assert(s.revision_total == before);
        rubraview_settings_set(&s, lit("video"), lit("subtitle_size"), 48.0);
        const rubraview_setting_def_t *def = rubraview_settings_find(lit("video"), lit("subtitle_size"));
        size_t index = (size_t)(def - rubraview_settings_schema(NULL));
        assert(s.revision_total == before + 1 && s.revision[index] == 1);
        rubraview_settings_set_text(&s, lit("curation"), lit("dir_1"), lit("D:\\Keep"));
        rubraview_settings_set_text(&s, lit("curation"), lit("dir_1"), lit("D:\\Keep"));
        assert(s.revision_total == before + 2);
        rubraview_settings_reset(&s);
        assert(s.revision_total == before + 3 && s.revision[index] == 2);
        assert(rubraview_settings_get(&s, lit("video"), lit("subtitle_size")) == 24.0);
    }
    printf("  [PASS] Changes are counted; a set that changes nothing is not one\n");

    free(raw);
    printf("[test_settings_doc] All tests passed successfully!\n");
    return 0;
}
