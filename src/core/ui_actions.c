#include "rubraview/ui_actions.h"
#include <string.h>

static bool is(u8str_t action, const char *name) {
    size_t n = strlen(name);
    return action.len == n && memcmp(action.ptr, name, n) == 0;
}

/* Actions that work on the picture on screen and do nothing without one. */
static const char *const NEEDS_PAGE[] = {
    "rename_file", "delete_file",
    "layout_single", "layout_dual", "layout_book", "toggle_layout",
    "toggle_reading_order", "toggle_spread_detect",
    "fit_window", "fit_width", "fit_height", "actual_size", "smart_fit", "fit_stretch", "toggle_fit_lock",
    "rotate_cw", "rotate_ccw", "flip_horizontal", "flip_vertical",
    "zoom_in", "zoom_out", "toggle_nearest", "toggle_pixel_grid",
};

rubraview_action_state_t rubraview_action_state(u8str_t action, const rubraview_action_facts_t *f) {
    rubraview_action_state_t st = { .enabled = true, .mark = RUBRAVIEW_MARK_NONE };
    if (!f) return st;

    for (size_t i = 0; i < sizeof(NEEDS_PAGE) / sizeof(NEEDS_PAGE[0]); ++i) {
        if (is(action, NEEDS_PAGE[i])) st.enabled = f->has_page;
    }
    if (is(action, "next_archive") || is(action, "prev_archive")) st.enabled = f->archive_series;
    /* Export writes a picture: a film or a sound has none to write. */
    if (is(action, "quick_export")) st.enabled = f->has_page && !f->media;
    /* Sound has no frames to step. */
    if (is(action, "anim_step_forward") || is(action, "anim_step_back")) {
        st.enabled = f->media ? f->video : f->frames;
    }
    if (is(action, "next_audio_track")) st.enabled = f->other_audio_track;
    if (is(action, "next_subtitle_track")) st.enabled = f->other_subtitle;
    if (is(action, "subtitle_earlier") || is(action, "subtitle_later")) st.enabled = f->subtitle_shown;

    static const struct { const char *action; size_t offset; } TOGGLES[] = {
        { "toggle_slideshow",      offsetof(rubraview_action_facts_t, slideshow) },
        { "toggle_filmstrip",      offsetof(rubraview_action_facts_t, filmstrip) },
        { "toggle_osd",            offsetof(rubraview_action_facts_t, osd) },
        { "toggle_toolbox_pin",    offsetof(rubraview_action_facts_t, toolbox_pinned) },
        { "toggle_toolbox_detach", offsetof(rubraview_action_facts_t, toolbox_detached) },
        { "toggle_fullscreen",     offsetof(rubraview_action_facts_t, fullscreen) },
        { "toggle_nearest",        offsetof(rubraview_action_facts_t, nearest) },
        { "toggle_pixel_grid",     offsetof(rubraview_action_facts_t, pixel_grid) },
        { "toggle_spread_detect",  offsetof(rubraview_action_facts_t, spread_detect) },
        { "toggle_fit_lock",       offsetof(rubraview_action_facts_t, fit_lock) },
        { "toggle_always_on_top",  offsetof(rubraview_action_facts_t, always_on_top) },
        { "media_mute",            offsetof(rubraview_action_facts_t, muted) },
    };
    for (size_t i = 0; i < sizeof(TOGGLES) / sizeof(TOGGLES[0]); ++i) {
        if (is(action, TOGGLES[i].action)) {
            bool on = *(const bool*)((const char*)f + TOGGLES[i].offset);
            st.mark = on ? RUBRAVIEW_MARK_ON : RUBRAVIEW_MARK_OFF;
        }
    }

    if (is(action, "toggle_reading_order")) st.value = f->rtl ? "R>L" : "L>R";

    static const struct { const char *action; rubraview_page_layout_t layout; } LAYOUTS[] = {
        { "layout_single", RUBRAVIEW_PAGE_LAYOUT_SINGLE },
        { "layout_dual",   RUBRAVIEW_PAGE_LAYOUT_DUAL },
        { "layout_book",   RUBRAVIEW_PAGE_LAYOUT_BOOK },
    };
    for (size_t i = 0; i < sizeof(LAYOUTS) / sizeof(LAYOUTS[0]); ++i) {
        if (is(action, LAYOUTS[i].action) && f->layout == LAYOUTS[i].layout) st.mark = RUBRAVIEW_MARK_CURRENT;
    }
    static const struct { const char *action; rubraview_fit_mode_t fit; } FITS[] = {
        { "fit_window",  RUBRAVIEW_FIT_WINDOW },
        { "fit_width",   RUBRAVIEW_FIT_WIDTH },
        { "fit_height",  RUBRAVIEW_FIT_HEIGHT },
        { "actual_size", RUBRAVIEW_FIT_ACTUAL_SIZE },
        { "smart_fit",   RUBRAVIEW_FIT_SMART },
        { "fit_stretch", RUBRAVIEW_FIT_STRETCH },
    };
    for (size_t i = 0; i < sizeof(FITS) / sizeof(FITS[0]); ++i) {
        if (is(action, FITS[i].action) && f->fit == FITS[i].fit) st.mark = RUBRAVIEW_MARK_CURRENT;
    }
    return st;
}

u8str_t rubraview_tile_caption(u8str_t label, bool submenu, rubraview_tile_mark_t mark, const char *value,
                               char *buffer, size_t buffer_size) {
    const char *sep = submenu ? " >" : (value || mark == RUBRAVIEW_MARK_ON || mark == RUBRAVIEW_MARK_OFF) ? ": " : "";
    const char *word = submenu ? ""
                     : value ? value
                     : mark == RUBRAVIEW_MARK_ON ? "on"
                     : mark == RUBRAVIEW_MARK_OFF ? "off" : "";
    size_t a = strlen(sep), b = strlen(word);
    if (a + b == 0 || !buffer || label.len + a + b + 1 > buffer_size) return label;
    memcpy(buffer, label.ptr, label.len);
    memcpy(buffer + label.len, sep, a);
    memcpy(buffer + label.len + a, word, b + 1);
    return (u8str_t){ .ptr = buffer, .len = label.len + a + b };
}

bool rubraview_confirm_press(rubraview_confirm_t *c, int64_t key, double now) {
    if (!c) return true;
    if (rubraview_confirm_armed(c, key, now)) {
        rubraview_confirm_clear(c);
        return true;
    }
    c->key = key;
    c->until = now + RUBRAVIEW_CONFIRM_SECONDS;
    return false;
}

bool rubraview_confirm_armed(const rubraview_confirm_t *c, int64_t key, double now) {
    return c && c->key == key && now <= c->until && c->until > 0.0;
}

void rubraview_confirm_clear(rubraview_confirm_t *c) {
    if (c) *c = (rubraview_confirm_t){0};
}
