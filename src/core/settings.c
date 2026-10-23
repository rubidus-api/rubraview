#include "rubraview/settings.h"
#include "rubraview/settings_doc.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

/*
 * D-13: there is no table of settings in this file any more. The settings
 * window's document (src/core/default_settings_doc.c) declares each one
 * where it places it — type, range, default — and this file reads that.
 * One list, so the window and the store cannot disagree about what exists.
 */
#define SCHEMA       (rubraview_settings_document()->defs)
#define SCHEMA_COUNT (rubraview_settings_document()->def_count)

const rubraview_setting_def_t *rubraview_settings_schema(size_t *out_count) {
    if (out_count) *out_count = SCHEMA_COUNT;
    return SCHEMA;
}

static bool same(proven_u8str_view_t a, proven_u8str_view_t b) {
    return a.size == b.size && (a.size == 0 || memcmp(a.ptr, b.ptr, a.size) == 0);
}

static int32_t index_of(proven_u8str_view_t section, proven_u8str_view_t key) {
    for (size_t i = 0; i < SCHEMA_COUNT; ++i) {
        if (same(SCHEMA[i].key, key) && same(SCHEMA[i].section, section)) return (int32_t)i;
    }
    return -1;
}

const rubraview_setting_def_t *rubraview_settings_find(proven_u8str_view_t section, proven_u8str_view_t key) {
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

proven_u8str_view_t rubraview_settings_tab_name(rubraview_settings_tab_t tab) {
    return rubraview_settings_page_title((size_t)tab);
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
        settings.texts[i] = PROVEN_LIT("");
    }
    return settings;
}

/* Matches a stored word against a choice list; an unrecognised word
   keeps the default rather than becoming index 0, which would silently
   change the setting to something the file did not say. */
/* Words a choice was once stored under, read as the word it has now
   (owner, 2026-09-28: Sort by's orders named for what they do). */
static const struct { const char *section, *key, *old_word, *new_word; } CHOICE_RENAMES[] = {
    { "files", "sort_mode", "natural", "explorer" },
    { "files", "sort_mode", "lexical", "text" },
    { "files", "sort_mode", "date",    "modified" },
};

static bool choice_index_exact(const rubraview_setting_def_t *def, proven_u8str_view_t word, double *out);

static bool choice_index(const rubraview_setting_def_t *def, proven_u8str_view_t word, double *out) {
    if (choice_index_exact(def, word, out)) return true;
    for (size_t r = 0; r < sizeof(CHOICE_RENAMES) / sizeof(CHOICE_RENAMES[0]); ++r) {
        if (!proven_u8str_view_eq(def->section, proven_u8str_view_from_cstr(CHOICE_RENAMES[r].section)) ||
            !proven_u8str_view_eq(def->key, proven_u8str_view_from_cstr(CHOICE_RENAMES[r].key)) ||
            !proven_u8str_view_eq_ascii_ci(word, proven_u8str_view_from_cstr(CHOICE_RENAMES[r].old_word))) continue;
        proven_u8str_view_t now = { .ptr = (const proven_byte_t *)CHOICE_RENAMES[r].new_word, .size = strlen(CHOICE_RENAMES[r].new_word) };
        return choice_index_exact(def, now, out);
    }
    return false;
}

static bool choice_index_exact(const rubraview_setting_def_t *def, proven_u8str_view_t word, double *out) {
    for (int32_t i = 0; i < def->choice_count && def->choices[i]; ++i) {
        if (proven_u8str_view_eq_ascii_ci(word, proven_u8str_view_from_cstr(def->choices[i]))) { *out = (double)i; return true; }
    }
    return false;
}

rubraview_settings_t rubraview_settings_load(proven_arena_t *arena, proven_u8str_view_t ini_text) {
    rubraview_settings_t settings = rubraview_settings_defaults();
    if (!arena || ini_text.size == 0) return settings;

    rubraview_ini_doc_t doc = rubraview_ini_parse(arena, ini_text);

    for (size_t i = 0; i < SCHEMA_COUNT; ++i) {
        const rubraview_setting_def_t *def = &SCHEMA[i];
        proven_u8str_view_t section = def->section;
        const proven_u8str_view_t *raw = rubraview_ini_get(&doc, section, def->key);
        /* D-13 moved the General tab's keys under [general]; a file from
           before still has them above the first section. */
        if (!raw && proven_u8str_view_eq(section, PROVEN_LIT("general"))) {
            section = PROVEN_LIT("");
            raw = rubraview_ini_get(&doc, section, def->key);
        }
        if (!raw || raw->size == 0) continue;

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
                settings.values[i] = rubraview_ini_get_bool(&doc, section, def->key,
                                                            def->default_value > 0.5) ? 1.0 : 0.0;
                break;
            case RUBRAVIEW_SETTING_INT:
            case RUBRAVIEW_SETTING_FLOAT:
            default: {
                double value = rubraview_ini_get_float(&doc, section, def->key, def->default_value);
                /* A hand-edited file must not be able to ask for a 40 GB
                   cache: out of range is clamped, not obeyed. */
                settings.values[i] = clamp_to(def, value);
                break;
            }
        }
    }

    return settings;
}

proven_u8str_view_t rubraview_settings_save(proven_arena_t *arena, const rubraview_settings_t *settings,
                                proven_u8str_view_t existing_ini_text) {
    proven_u8str_view_t empty = PROVEN_LIT("");
    if (!arena || !settings) return empty;

    /* Start from what is already in the file so a key this version does
       not know about survives being opened by it. */
    rubraview_ini_doc_t doc = rubraview_ini_parse(arena, existing_ini_text);

    /* D-13: nothing may sit above the first section. What an older file
       kept there moves under [general] — where the General tab's keys
       live now — and when [general] already has the same key, the later
       one in the file wins, as it did when the file was read. */
    size_t kept = 0;
    for (size_t i = 0; i < doc.count; ++i) {
        rubraview_ini_entry_t e = doc.entries[i];
        if (e.section.size == 0) e.section = PROVEN_LIT("general");
        bool replaced = false;
        for (size_t k = 0; k < kept; ++k) {
            if (doc.entries[k].section.size == e.section.size &&
                memcmp(doc.entries[k].section.ptr, e.section.ptr, e.section.size) == 0 &&
                doc.entries[k].key.size == e.key.size &&
                memcmp(doc.entries[k].key.ptr, e.key.ptr, e.key.size) == 0) {
                doc.entries[k] = e;
                replaced = true;
                break;
            }
        }
        if (!replaced) doc.entries[kept++] = e;
    }
    doc.count = kept;

    for (size_t i = 0; i < SCHEMA_COUNT; ++i) {
        const rubraview_setting_def_t *def = &SCHEMA[i];
        /* D-13: each value is written with its own type, so the file is
           in the INI and TOML subset — a folder is a string even when its
           name is a number, a choice is its quoted name. */
        switch (def->type) {
            case RUBRAVIEW_SETTING_PATH:
                if (settings->texts[i].size == 0) {
                    /* An unset folder is left out entirely — unless the file holds one, which is
                       emptied, or it would come back when the file is read (owner, 2026-10-16). */
                    if (rubraview_ini_get(&doc, def->section, def->key)) rubraview_ini_set_string(arena, &doc, def->section, def->key, settings->texts[i]);
                    continue;
                }
                rubraview_ini_set_string(arena, &doc, def->section, def->key, settings->texts[i]);
                break;
            case RUBRAVIEW_SETTING_CHOICE: {
                int32_t index = (int32_t)settings->values[i];
                if (index < 0 || index >= def->choice_count || !def->choices[index]) continue;
                rubraview_ini_set_string(arena, &doc, def->section, def->key,
                                         (proven_u8str_view_t){ .ptr = (const proven_byte_t *)def->choices[index], .size = strlen(def->choices[index]) });
                break;
            }
            case RUBRAVIEW_SETTING_BOOL:
                rubraview_ini_set_bool(arena, &doc, def->section, def->key, settings->values[i] > 0.5);
                break;
            case RUBRAVIEW_SETTING_FLOAT:
                rubraview_ini_set_float(arena, &doc, def->section, def->key, settings->values[i]);
                break;
            case RUBRAVIEW_SETTING_INT:
            default:
                rubraview_ini_set_int(arena, &doc, def->section, def->key, (long long)settings->values[i]);
                break;
        }
    }

    return rubraview_ini_serialize(arena, &doc);
}

double rubraview_settings_get(const rubraview_settings_t *settings, proven_u8str_view_t section, proven_u8str_view_t key) {
    int32_t index = index_of(section, key);
    if (!settings || index < 0) return 0.0;
    return settings->values[index];
}

proven_u8str_view_t rubraview_settings_get_text(const rubraview_settings_t *settings, proven_u8str_view_t section, proven_u8str_view_t key) {
    proven_u8str_view_t empty = PROVEN_LIT("");
    int32_t index = index_of(section, key);
    if (!settings || index < 0) return empty;
    return settings->texts[index];
}

void rubraview_settings_set(rubraview_settings_t *settings, proven_u8str_view_t section, proven_u8str_view_t key, double value) {
    int32_t index = index_of(section, key);
    if (!settings || index < 0) return;
    double clamped = clamp_to(&SCHEMA[index], value);
    if (clamped == settings->values[index]) return;   /* not a change: nothing to redraw */
    settings->values[index] = clamped;
    settings->revision[index]++;
    settings->revision_total++;
}

void rubraview_settings_set_text(rubraview_settings_t *settings, proven_u8str_view_t section, proven_u8str_view_t key, proven_u8str_view_t text) {
    int32_t index = index_of(section, key);
    if (!settings || index < 0) return;
    const proven_u8str_view_t old = settings->texts[index];
    if (old.size == text.size && (text.size == 0 || memcmp(old.ptr, text.ptr, text.size) == 0)) return;
    settings->texts[index] = text;
    settings->revision[index]++;
    settings->revision_total++;
}

void rubraview_settings_reset(rubraview_settings_t *settings) {
    if (!settings) return;
    /* Reset is a change like any other: whatever is watching the counters
       must see it, so they move on rather than start again from zero. */
    uint32_t total = settings->revision_total;
    rubraview_settings_t fresh = rubraview_settings_defaults();
    for (size_t i = 0; i < SCHEMA_COUNT; ++i) {
        bool same_value = fresh.values[i] == settings->values[i];
        bool same_text = fresh.texts[i].size == settings->texts[i].size;
        fresh.revision[i] = settings->revision[i] + ((same_value && same_text) ? 0u : 1u);
    }
    fresh.revision_total = total + 1u;
    *settings = fresh;
}

rubraview_settings_t rubraview_settings_merge_changed(proven_arena_t *arena, const rubraview_settings_t *mine,
                                                      const rubraview_settings_t *baseline, proven_u8str_view_t existing_ini_text) {
    if (!mine) return rubraview_settings_defaults();
    if (!arena || !baseline || existing_ini_text.size == 0) return *mine;
    rubraview_settings_t merged = rubraview_settings_load(arena, existing_ini_text);
    for (size_t i = 0; i < SCHEMA_COUNT; ++i) {
        if (mine->values[i] != baseline->values[i]) merged.values[i] = mine->values[i];
        if (!same(mine->texts[i], baseline->texts[i])) merged.texts[i] = mine->texts[i];
    }
    return merged;
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

            /* Two contexts claiming the same chord is a conflict only
               where one of them can never be reached (D-14): `Space`
               meaning one thing while a GIF plays and another everywhere
               else is by design (§3.20.1), `navigation` and `view` both
               claiming it is not. */
            if (!rubraview_keymap_contexts_meet(first->context, second->context)) continue;

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

static void reset_where(rubraview_settings_t *settings, int32_t tab, const rubraview_setting_def_t *only) {
    if (!settings) return;
    size_t count = 0;
    const rubraview_setting_def_t *schema = rubraview_settings_schema(&count);
    rubraview_settings_t fresh = rubraview_settings_defaults();
    for (size_t i = 0; i < count && i < RUBRAVIEW_SETTINGS_MAX; ++i) {
        const rubraview_setting_def_t *d = &schema[i];
        bool mine = only ? (d == only || (only->pair != 0 && d == &schema[only->pair])) : (int32_t)d->tab == tab;
        if (!mine) continue;
        if (d->type == RUBRAVIEW_SETTING_PATH) rubraview_settings_set_text(settings, d->section, d->key, fresh.texts[i]);
        else rubraview_settings_set(settings, d->section, d->key, fresh.values[i]);
    }
}

void rubraview_settings_reset_page(rubraview_settings_t *settings, rubraview_settings_tab_t tab) {
    reset_where(settings, (int32_t)tab, NULL);
}

void rubraview_settings_reset_one(rubraview_settings_t *settings, proven_u8str_view_t section, proven_u8str_view_t key) {
    const rubraview_setting_def_t *d = rubraview_settings_find(section, key);
    if (d) reset_where(settings, -1, d);
}
