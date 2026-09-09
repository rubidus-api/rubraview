#include "rubraview/settings.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

/* The choice lists §3.22 names. Written out rather than generated so the
   order a tab shows is the order stored in the file. */
static const char *const FIT_MODES[] = { "window", "width", "height", "actual", "smart", "stretch", NULL };
static const char *const LAYOUTS[] = { "single", "dual", "book", "webtoon", NULL };
static const char *const INTERP[] = { "nearest", "bilinear", "bicubic", "lanczos3", NULL };
static const char *const SORT_MODES[] = { "natural", "lexical", "date", "size", NULL };
static const char *const CODEPAGES[] = { "auto", "utf8", "cp949", "shift_jis", "gbk", "big5", "cp1252", NULL };
static const char *const STARTUP[] = { "blank", "last_file", "last_folder", NULL };
static const char *const CURATION_MODES[] = { "move", "copy", NULL };
static const char *const ACCENTS[] = { "crimson", "cobalt", "emerald", "amber", "teal", "purple", NULL };
static const char *const TILE_SIZES[] = { "48", "64", "96", NULL };
static const char *const REPLAYGAIN[] = { "off", "track", "album", NULL };

/* U8() expands to a compound literal, which C forbids in a file-scope
   initialiser, so the slices are built from the literal directly — the
   same trick main.c's menu table uses, and it still keeps the lengths
   out of anyone's hands. The macro parameter is `tab_` rather than `tab`
   because `tab` is also a field name, and a designator is not immune to
   macro substitution. */
#define S(lit) { .ptr = (lit), .len = sizeof(lit) - 1 }

#define BOOL_ROW(k, sec, lab, tab_, def, wired_) \
    { .key = S(k), .section = S(sec), .label = S(lab), .tab = (tab_), \
      .type = RUBRAVIEW_SETTING_BOOL, .default_value = (def), .min_value = 0.0, .max_value = 1.0, \
      .step = 1.0, .choices = NULL, .choice_count = 0, .wired = (wired_) }

#define NUM_ROW(k, sec, lab, tab_, type_, def, lo, hi, st, wired_) \
    { .key = S(k), .section = S(sec), .label = S(lab), .tab = (tab_), \
      .type = (type_), .default_value = (def), .min_value = (lo), .max_value = (hi), \
      .step = (st), .choices = NULL, .choice_count = 0, .wired = (wired_) }

#define CHOICE_ROW(k, sec, lab, tab_, list, n, def, wired_) \
    { .key = S(k), .section = S(sec), .label = S(lab), .tab = (tab_), \
      .type = RUBRAVIEW_SETTING_CHOICE, .default_value = (def), .min_value = 0.0, \
      .max_value = (double)((n) - 1), .step = 1.0, .choices = (list), .choice_count = (n), \
      .wired = (wired_) }

#define PATH_ROW(k, sec, lab, tab_, wired_) \
    { .key = S(k), .section = S(sec), .label = S(lab), .tab = (tab_), \
      .type = RUBRAVIEW_SETTING_PATH, .default_value = 0.0, .min_value = 0.0, .max_value = 0.0, \
      .step = 0.0, .choices = NULL, .choice_count = 0, .wired = (wired_) }

/*
 * The schema. `wired` says whether anything reads the key today; a
 * declared-but-unwired setting is shown greyed rather than hidden,
 * because a gap nobody can see does not get closed.
 */
static const rubraview_setting_def_t SCHEMA[] = {
    /* Tab 1: General (§3.22.2.1) */
    CHOICE_ROW("startup", "", "On startup", RUBRAVIEW_TAB_GENERAL, STARTUP, 3, 2, false),
    BOOL_ROW("single_instance", "", "Reuse the open window", RUBRAVIEW_TAB_GENERAL, 1.0, true),
    BOOL_ROW("frameless", "", "Frameless window", RUBRAVIEW_TAB_GENERAL, 1.0, false),
    NUM_ROW("titlebar_trigger_px", "", "Titlebar trigger height", RUBRAVIEW_TAB_GENERAL,
            RUBRAVIEW_SETTING_INT, 12.0, 4.0, 40.0, 1.0, false),
    NUM_ROW("titlebar_hide_ms", "", "Titlebar hide delay", RUBRAVIEW_TAB_GENERAL,
            RUBRAVIEW_SETTING_INT, 500.0, 100.0, 3000.0, 50.0, false),

    /* Tab 2: Viewer and layout (§3.22.2.2) */
    CHOICE_ROW("fit_mode", "viewer", "Default fit", RUBRAVIEW_TAB_VIEWER, FIT_MODES, 6, 0, false),
    CHOICE_ROW("layout", "viewer", "Default layout", RUBRAVIEW_TAB_VIEWER, LAYOUTS, 4, 0, false),
    BOOL_ROW("spread_autosplit", "viewer", "Split wide spreads", RUBRAVIEW_TAB_VIEWER, 1.0, false),
    NUM_ROW("gutter_px", "viewer", "Gutter", RUBRAVIEW_TAB_VIEWER,
            RUBRAVIEW_SETTING_INT, 8.0, 0.0, 32.0, 1.0, false),
    BOOL_ROW("portrait_collapse", "viewer", "Collapse in a portrait window", RUBRAVIEW_TAB_VIEWER, 1.0, false),
    NUM_ROW("zoom_step_percent", "viewer", "Zoom step", RUBRAVIEW_TAB_VIEWER,
            RUBRAVIEW_SETTING_INT, 10.0, 5.0, 50.0, 1.0, false),
    CHOICE_ROW("interpolation", "viewer", "Scaling filter", RUBRAVIEW_TAB_VIEWER, INTERP, 4, 2, false),
    BOOL_ROW("pixel_grid", "viewer", "Pixel grid past 400%", RUBRAVIEW_TAB_VIEWER, 0.0, false),

    /* Tab 3: Files and comic archives (§3.22.2.3) */
    CHOICE_ROW("sort_mode", "files", "Sort by", RUBRAVIEW_TAB_FILES, SORT_MODES, 4, 0, false),
    BOOL_ROW("sort_ascending", "files", "Ascending", RUBRAVIEW_TAB_FILES, 1.0, false),
    CHOICE_ROW("archive_codepage", "files", "Archive filenames", RUBRAVIEW_TAB_FILES, CODEPAGES, 7, 0, false),
    BOOL_ROW("comicinfo", "files", "Read ComicInfo.xml", RUBRAVIEW_TAB_FILES, 1.0, false),
    BOOL_ROW("reading_history", "files", "Remember the page", RUBRAVIEW_TAB_FILES, 1.0, false),
    BOOL_ROW("resume_prompt", "files", "Offer to resume", RUBRAVIEW_TAB_FILES, 1.0, false),
    CHOICE_ROW("curation_mode", "curation", "Number keys", RUBRAVIEW_TAB_FILES, CURATION_MODES, 2, 0, true),
    PATH_ROW("dir_1", "curation", "Folder 1", RUBRAVIEW_TAB_FILES, true),
    PATH_ROW("dir_2", "curation", "Folder 2", RUBRAVIEW_TAB_FILES, true),
    PATH_ROW("dir_3", "curation", "Folder 3", RUBRAVIEW_TAB_FILES, true),
    PATH_ROW("dir_4", "curation", "Folder 4", RUBRAVIEW_TAB_FILES, true),
    PATH_ROW("dir_5", "curation", "Folder 5", RUBRAVIEW_TAB_FILES, true),
    PATH_ROW("dir_6", "curation", "Folder 6", RUBRAVIEW_TAB_FILES, true),
    PATH_ROW("dir_7", "curation", "Folder 7", RUBRAVIEW_TAB_FILES, true),
    PATH_ROW("dir_8", "curation", "Folder 8", RUBRAVIEW_TAB_FILES, true),
    PATH_ROW("dir_9", "curation", "Folder 9", RUBRAVIEW_TAB_FILES, true),

    /* Tab 4: Music and audio (§3.22.2.4). Nothing reads these yet — the
       audio engine is M5/M8 — but they are the keys those milestones
       will use, and showing them greyed says so out loud. */
    BOOL_ROW("gapless", "audio", "Gapless playback", RUBRAVIEW_TAB_AUDIO, 1.0, false),
    NUM_ROW("crossfade_seconds", "audio", "Crossfade", RUBRAVIEW_TAB_AUDIO,
            RUBRAVIEW_SETTING_FLOAT, 0.0, 0.0, 5.0, 0.1, false),
    CHOICE_ROW("replaygain", "audio", "Volume levelling", RUBRAVIEW_TAB_AUDIO, REPLAYGAIN, 3, 0, false),
    NUM_ROW("wasapi_latency_ms", "audio", "Audio latency", RUBRAVIEW_TAB_AUDIO,
            RUBRAVIEW_SETTING_INT, 40.0, 20.0, 100.0, 5.0, false),
    BOOL_ROW("bgm_pause_on_video", "audio", "Pause music during video", RUBRAVIEW_TAB_AUDIO, 1.0, false),

    /* Tab 5: Video and subtitles (§3.22.2.5) */
    BOOL_ROW("hardware_decode", "video", "GPU decoding", RUBRAVIEW_TAB_VIDEO, 1.0, false),
    NUM_ROW("subtitle_size", "video", "Subtitle size", RUBRAVIEW_TAB_VIDEO,
            RUBRAVIEW_SETTING_INT, 24.0, 10.0, 72.0, 1.0, false),
    NUM_ROW("subtitle_outline", "video", "Subtitle outline", RUBRAVIEW_TAB_VIDEO,
            RUBRAVIEW_SETTING_INT, 2.0, 0.0, 8.0, 1.0, false),
    NUM_ROW("ab_step_seconds", "video", "A-B step", RUBRAVIEW_TAB_VIDEO,
            RUBRAVIEW_SETTING_FLOAT, 0.5, 0.1, 1.0, 0.1, false),
    BOOL_ROW("video_wheel_zoom", "video", "Wheel zoom during video", RUBRAVIEW_TAB_VIDEO, 1.0, false),

    /* Tab 6: Display, HiDPI and colour (§3.22.2.6) */
    CHOICE_ROW("tile_base_px", "display", "Touch tile size", RUBRAVIEW_TAB_DISPLAY, TILE_SIZES, 3, 1, false),
    BOOL_ROW("color_management", "display", "Use embedded ICC profiles", RUBRAVIEW_TAB_DISPLAY, 1.0, false),
    CHOICE_ROW("accent", "display", "Accent colour", RUBRAVIEW_TAB_DISPLAY, ACCENTS, 6, 0, false),

    /* Tab 7: Cache, memory and privacy (§3.22.2.7) */
    NUM_ROW("memory_cap_mb", "cache", "Memory cap", RUBRAVIEW_TAB_CACHE,
            RUBRAVIEW_SETTING_INT, 512.0, 256.0, 4096.0, 64.0, false),
    NUM_ROW("lookahead", "cache", "Pages read ahead", RUBRAVIEW_TAB_CACHE,
            RUBRAVIEW_SETTING_INT, 2.0, 1.0, 16.0, 1.0, false),
    NUM_ROW("lookbehind", "cache", "Pages kept behind", RUBRAVIEW_TAB_CACHE,
            RUBRAVIEW_SETTING_INT, 1.0, 0.0, 8.0, 1.0, false),
    BOOL_ROW("privacy_clean", "cache", "Always strip metadata on export", RUBRAVIEW_TAB_CACHE, 0.0, false),

    /* Tab 8: Keyboard (§3.22.2.8). The bindings live in keymap.ini, not
       here; what belongs in settings.ini is whether that file is used. */
    BOOL_ROW("use_keymap_file", "keys", "Use keymap.ini", RUBRAVIEW_TAB_KEYS, 1.0, false),
};

#define SCHEMA_COUNT (sizeof(SCHEMA) / sizeof(SCHEMA[0]))

const rubraview_setting_def_t *rubraview_settings_schema(size_t *out_count) {
    if (out_count) *out_count = SCHEMA_COUNT;
    return SCHEMA;
}

static bool same(u8str_t a, u8str_t b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0);
}

static int32_t index_of(u8str_t section, u8str_t key) {
    for (size_t i = 0; i < SCHEMA_COUNT; ++i) {
        if (same(SCHEMA[i].key, key) && same(SCHEMA[i].section, section)) return (int32_t)i;
    }
    return -1;
}

const rubraview_setting_def_t *rubraview_settings_find(u8str_t section, u8str_t key) {
    int32_t index = index_of(section, key);
    return index < 0 ? NULL : &SCHEMA[index];
}

size_t rubraview_settings_for_tab(rubraview_settings_tab_t tab,
                                  const rubraview_setting_def_t **out, size_t capacity) {
    size_t count = 0;
    for (size_t i = 0; i < SCHEMA_COUNT; ++i) {
        if (SCHEMA[i].tab != tab) continue;
        if (out && count < capacity) out[count] = &SCHEMA[i];
        count++;
    }
    return count;
}

u8str_t rubraview_settings_tab_name(rubraview_settings_tab_t tab) {
    switch (tab) {
        case RUBRAVIEW_TAB_GENERAL: return U8("General");
        case RUBRAVIEW_TAB_VIEWER:  return U8("Viewer");
        case RUBRAVIEW_TAB_FILES:   return U8("Files");
        case RUBRAVIEW_TAB_AUDIO:   return U8("Audio");
        case RUBRAVIEW_TAB_VIDEO:   return U8("Video");
        case RUBRAVIEW_TAB_DISPLAY: return U8("Display");
        case RUBRAVIEW_TAB_CACHE:   return U8("Cache");
        case RUBRAVIEW_TAB_KEYS:    return U8("Keys");
        default: return U8("");
    }
}

/* ---- values ---- */

static double clamp_to(const rubraview_setting_def_t *def, double value) {
    if (def->type == RUBRAVIEW_SETTING_PATH) return 0.0;
    if (value < def->min_value) value = def->min_value;
    if (value > def->max_value) value = def->max_value;
    if (def->step > 0.0) {
        double steps = (value - def->min_value) / def->step;
        value = def->min_value + floor(steps + 0.5) * def->step;
        if (value > def->max_value) value = def->max_value;
    }
    return value;
}

rubraview_settings_t rubraview_settings_defaults(void) {
    rubraview_settings_t settings = {0};
    settings.count = SCHEMA_COUNT;
    for (size_t i = 0; i < SCHEMA_COUNT; ++i) {
        settings.values[i] = SCHEMA[i].default_value;
        settings.texts[i] = (u8str_t){ .ptr = "", .len = 0 };
    }
    return settings;
}

/* Matches a stored word against a choice list; an unrecognised word
   keeps the default rather than becoming index 0, which would silently
   change the setting to something the file did not say. */
static bool choice_index(const rubraview_setting_def_t *def, u8str_t word, double *out) {
    for (int32_t i = 0; i < def->choice_count && def->choices[i]; ++i) {
        size_t n = strlen(def->choices[i]);
        if (word.len != n) continue;

        bool equal = true;
        for (size_t k = 0; k < n; ++k) {
            char a = word.ptr[k];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (a != def->choices[i][k]) { equal = false; break; }
        }
        if (equal) { *out = (double)i; return true; }
    }
    return false;
}

rubraview_settings_t rubraview_settings_load(proven_arena_t *arena, u8str_t ini_text) {
    rubraview_settings_t settings = rubraview_settings_defaults();
    if (!arena || ini_text.len == 0) return settings;

    rubraview_ini_doc_t doc = rubraview_ini_parse(arena, ini_text);

    for (size_t i = 0; i < SCHEMA_COUNT; ++i) {
        const rubraview_setting_def_t *def = &SCHEMA[i];
        const u8str_t *raw = rubraview_ini_get(&doc, def->section, def->key);
        if (!raw || raw->len == 0) continue;

        switch (def->type) {
            case RUBRAVIEW_SETTING_PATH:
                settings.texts[i] = *raw;
                break;
            case RUBRAVIEW_SETTING_CHOICE: {
                double index = def->default_value;
                if (choice_index(def, *raw, &index)) settings.values[i] = index;
                break;
            }
            case RUBRAVIEW_SETTING_BOOL:
                settings.values[i] = rubraview_ini_get_bool(&doc, def->section, def->key,
                                                            def->default_value > 0.5) ? 1.0 : 0.0;
                break;
            case RUBRAVIEW_SETTING_INT:
            case RUBRAVIEW_SETTING_FLOAT:
            default: {
                double value = rubraview_ini_get_float(&doc, def->section, def->key, def->default_value);
                /* A hand-edited file must not be able to ask for a 40 GB
                   cache: out of range is clamped, not obeyed. */
                settings.values[i] = clamp_to(def, value);
                break;
            }
        }
    }

    return settings;
}

u8str_t rubraview_settings_save(proven_arena_t *arena, const rubraview_settings_t *settings,
                                u8str_t existing_ini_text) {
    u8str_t empty = { .ptr = "", .len = 0 };
    if (!arena || !settings) return empty;

    /* Start from what is already in the file so a key this version does
       not know about survives being opened by it. */
    rubraview_ini_doc_t doc = rubraview_ini_parse(arena, existing_ini_text);

    for (size_t i = 0; i < SCHEMA_COUNT; ++i) {
        const rubraview_setting_def_t *def = &SCHEMA[i];
        char buffer[64];
        u8str_t value = empty;
        bool needs_copy = false;   /* the numeric cases format into `buffer` */

        switch (def->type) {
            case RUBRAVIEW_SETTING_PATH:
                value = settings->texts[i];
                if (value.len == 0) continue;   /* an unset folder is left out entirely */
                break;
            case RUBRAVIEW_SETTING_CHOICE: {
                int32_t index = (int32_t)settings->values[i];
                if (index < 0 || index >= def->choice_count || !def->choices[index]) continue;
                value = (u8str_t){ .ptr = def->choices[index], .len = strlen(def->choices[index]) };
                break;
            }
            case RUBRAVIEW_SETTING_BOOL:
                value = settings->values[i] > 0.5 ? U8("true") : U8("false");
                break;
            case RUBRAVIEW_SETTING_FLOAT: {
                int written = snprintf(buffer, sizeof(buffer), "%.3f", settings->values[i]);
                if (written <= 0) continue;
                value = (u8str_t){ .ptr = buffer, .len = (size_t)written };
                needs_copy = true;
                break;
            }
            case RUBRAVIEW_SETTING_INT:
            default: {
                int written = snprintf(buffer, sizeof(buffer), "%lld", (long long)settings->values[i]);
                if (written <= 0) continue;
                value = (u8str_t){ .ptr = buffer, .len = (size_t)written };
                needs_copy = true;
                break;
            }
        }

        /* The document keeps the slice rather than the bytes, and
           `buffer` is gone at the end of this iteration — so a formatted
           number has to be copied somewhere that outlives it. ASan is
           what pointed this out. */
        if (needs_copy) {
            proven_result_mem_mut_t res = proven_arena_alloc(arena, value.len + 1);
            if (!proven_is_ok(res.err)) continue;
            char *copy = (char*)(void*)res.value.ptr;
            memcpy(copy, value.ptr, value.len);
            copy[value.len] = '\0';
            value = (u8str_t){ .ptr = copy, .len = value.len };
        }

        rubraview_ini_set(arena, &doc, def->section, def->key, value);
    }

    return rubraview_ini_serialize(arena, &doc);
}

double rubraview_settings_get(const rubraview_settings_t *settings, u8str_t section, u8str_t key) {
    int32_t index = index_of(section, key);
    if (!settings || index < 0) return 0.0;
    return settings->values[index];
}

u8str_t rubraview_settings_get_text(const rubraview_settings_t *settings, u8str_t section, u8str_t key) {
    u8str_t empty = { .ptr = "", .len = 0 };
    int32_t index = index_of(section, key);
    if (!settings || index < 0) return empty;
    return settings->texts[index];
}

void rubraview_settings_set(rubraview_settings_t *settings, u8str_t section, u8str_t key, double value) {
    int32_t index = index_of(section, key);
    if (!settings || index < 0) return;
    settings->values[index] = clamp_to(&SCHEMA[index], value);
}

void rubraview_settings_set_text(rubraview_settings_t *settings, u8str_t section, u8str_t key, u8str_t text) {
    int32_t index = index_of(section, key);
    if (!settings || index < 0) return;
    settings->texts[index] = text;
}

void rubraview_settings_reset(rubraview_settings_t *settings) {
    if (!settings) return;
    *settings = rubraview_settings_defaults();
}

bool rubraview_settings_differs(const rubraview_settings_t *a, const rubraview_settings_t *b) {
    if (!a || !b) return false;
    for (size_t i = 0; i < SCHEMA_COUNT; ++i) {
        if (a->values[i] != b->values[i]) return true;
        if (!same(a->texts[i], b->texts[i])) return true;
    }
    return false;
}

/* ---- §3.22.2 tab 8: conflicts ---- */

static bool combo_equal(rubraview_key_combo_t a, rubraview_key_combo_t b) {
    return a.modifiers == b.modifiers && same(a.key_name, b.key_name);
}

size_t rubraview_keymap_conflicts(const rubraview_keymap_t *keymap,
                                  rubraview_key_conflict_t *out, size_t capacity) {
    if (!keymap) return 0;

    size_t found = 0;
    for (size_t i = 0; i < keymap->count; ++i) {
        const rubraview_key_binding_t *first = &keymap->bindings[i];

        for (size_t j = i + 1; j < keymap->count; ++j) {
            const rubraview_key_binding_t *second = &keymap->bindings[j];

            /* Two contexts claiming the same chord is not a conflict —
               it is how `Space` means one thing while a GIF plays and
               another everywhere else (§3.20.1). */
            if (!same(first->context, second->context)) continue;

            for (size_t a = 0; a < first->combo_count; ++a) {
                for (size_t b = 0; b < second->combo_count; ++b) {
                    if (!combo_equal(first->combos[a], second->combos[b])) continue;

                    if (out && found < capacity) {
                        out[found] = (rubraview_key_conflict_t){
                            .context = first->context,
                            .chord = first->combos[a].key_name,
                            .action_a = first->action,
                            .action_b = second->action,
                        };
                    }
                    found++;
                }
            }
        }
    }
    return found;
}
