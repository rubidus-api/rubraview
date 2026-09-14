#ifndef RUBRAVIEW_SETTINGS_H
#define RUBRAVIEW_SETTINGS_H

#include "rubraview/core.h"
#include "rubraview/ini.h"
#include "rubraview/keymap.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The settings schema (RFC-0001 §3.22), RV-082.
 *
 * §3.22 describes a window with eight tabs. What that window really is,
 * underneath, is a *list of settings*: each with a name, a place in
 * `settings.ini`, a type, a legal range, a default, and the tab it
 * belongs on. Everything else — the tab strip, the sliders, the Apply
 * button — is drawing.
 *
 * Putting the list here rather than in the window has one concrete
 * consequence, and it is the reason §11.2 makes it M9's completion
 * criterion: a setting some other module reads but no tab shows is a
 * setting a reader cannot change. That is checkable, and
 * `scripts/check-settings.py` checks it against the source rather than
 * against anyone's memory.
 */

typedef enum rubraview_settings_tab {
    RUBRAVIEW_TAB_GENERAL = 0,
    RUBRAVIEW_TAB_VIEWER,
    RUBRAVIEW_TAB_FILES,
    RUBRAVIEW_TAB_AUDIO,
    RUBRAVIEW_TAB_VIDEO,
    RUBRAVIEW_TAB_DISPLAY,
    RUBRAVIEW_TAB_CACHE,
    RUBRAVIEW_TAB_KEYS,
    RUBRAVIEW_TAB_COUNT,
} rubraview_settings_tab_t;

typedef enum rubraview_setting_type {
    RUBRAVIEW_SETTING_BOOL = 0,
    RUBRAVIEW_SETTING_INT,
    RUBRAVIEW_SETTING_FLOAT,
    RUBRAVIEW_SETTING_CHOICE,   /* an index into `choices` */
    RUBRAVIEW_SETTING_PATH,     /* free text; a folder */
} rubraview_setting_type_t;

typedef struct rubraview_setting_def {
    u8str_t key;         /* the name in settings.ini */
    u8str_t section;     /* the [section]; empty for the file's global part */
    u8str_t label;       /* what the tab shows */
    rubraview_settings_tab_t tab;
    rubraview_setting_type_t type;

    double  default_value;
    double  min_value, max_value;
    double  step;

    const char *const *choices;   /* CHOICE only; NULL-terminated */
    int32_t     choice_count;
    u8str_t     unit;             /* shown after a number: "px", "ms"; may be empty */

    /**
     * True when some module actually reads this key today. A setting
     * that is declared but not yet wired is still listed — the tab shows
     * it greyed — because hiding it would make the gap invisible, and an
     * invisible gap does not get closed.
     */
    bool wired;
} rubraview_setting_def_t;

/** The whole schema, and how many entries it has. */
const rubraview_setting_def_t *rubraview_settings_schema(size_t *out_count);

/** Find one setting by key (and section), or NULL. */
const rubraview_setting_def_t *rubraview_settings_find(u8str_t section, u8str_t key);

/** The settings on one tab, written into `out`; returns how many there are. */
size_t rubraview_settings_for_tab(rubraview_settings_tab_t tab,
                                  const rubraview_setting_def_t **out, size_t capacity);

/** The tab's name, for the tab strip. */
u8str_t rubraview_settings_tab_name(rubraview_settings_tab_t tab);

/* ---- values ---- */

#define RUBRAVIEW_SETTINGS_MAX 64

typedef struct rubraview_settings {
    double   values[RUBRAVIEW_SETTINGS_MAX];   /* numeric and boolean settings */
    u8str_t  texts[RUBRAVIEW_SETTINGS_MAX];    /* PATH settings */
    size_t   count;                            /* mirrors the schema's length */
    /* D-13: how many times each setting has changed, and all of them
       together. The settings window and the viewer compare these to know
       what to redraw — cheaper and surer than comparing every value. */
    uint32_t revision[RUBRAVIEW_SETTINGS_MAX];
    uint32_t revision_total;
} rubraview_settings_t;

/** Every setting at its default. */
rubraview_settings_t rubraview_settings_defaults(void);

/**
 * Read `settings.ini`. A key that is absent keeps its default, and one
 * that is out of range is clamped rather than obeyed — a hand-edited
 * file must not be able to ask for a 40 GB cache.
 */
rubraview_settings_t rubraview_settings_load(proven_arena_t *arena, u8str_t ini_text);

/**
 * Write the settings back out, preserving anything in the file this
 * schema does not know about. A future version's key must survive being
 * opened by this one.
 */
u8str_t rubraview_settings_save(proven_arena_t *arena, const rubraview_settings_t *settings,
                                u8str_t existing_ini_text);

double  rubraview_settings_get(const rubraview_settings_t *settings, u8str_t section, u8str_t key);
u8str_t rubraview_settings_get_text(const rubraview_settings_t *settings, u8str_t section, u8str_t key);

/** Set a value, clamped and stepped to the schema's range. */
void rubraview_settings_set(rubraview_settings_t *settings, u8str_t section, u8str_t key, double value);
void rubraview_settings_set_text(rubraview_settings_t *settings, u8str_t section, u8str_t key, u8str_t text);

/** §3.22.1's Reset to Defaults. */
void rubraview_settings_reset(rubraview_settings_t *settings);

/** Whether anything differs from what was loaded — the Apply button's state. */
bool rubraview_settings_differs(const rubraview_settings_t *a, const rubraview_settings_t *b);

/* ---- §3.22.2 tab 8: rebinding ---- */

typedef struct rubraview_key_conflict {
    u8str_t context;
    u8str_t chord;       /* the chord two actions both claim */
    u8str_t action_a;
    u8str_t action_b;
} rubraview_key_conflict_t;

/**
 * Find chords bound to more than one action where the two can meet
 * (§3.22.2's conflict detection; rubraview_keymap_contexts_meet says
 * where). Contexts that take over only while something is playing keep
 * their own meaning for a key — that is what lets `Space` mean two
 * things (§3.20.1) — so those are not reported.
 */
size_t rubraview_keymap_conflicts(const rubraview_keymap_t *keymap,
                                  rubraview_key_conflict_t *out, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_SETTINGS_H */
