#include "rubraview/ui_actions.h"
#include "rubraview/lang.h"
#include "rubraview/repeat.h"
#include "rubraview/aspect.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

/* Actions that work on the picture on screen and do nothing without one. */
static const char *const NEEDS_PAGE[] = {
    "rename_file", "delete_file", "reveal_in_explorer", "print", "copy_to_folder", "move_to_folder", "toggle_file_list",
    "layout_single", "layout_dual", "layout_dual_rtl", "layout_book", "layout_book_rtl", "layout_webtoon",
    "layout_comic", "layout_comic_rtl", "toggle_layout",
    "toggle_spread_detect",
    "fit_window", "fit_width", "fit_height", "actual_size", "smart_fit", "fit_stretch", "fit_cycle", "toggle_fit_lock",
    "rotate_cw", "rotate_ccw", "flip_horizontal", "flip_vertical",
    "zoom_in", "zoom_out", "zoom_reset", "zoom_cycle", "hide_boxes", "toggle_nearest", "toggle_pixel_grid",
};

rubraview_action_state_t rubraview_action_state(u8str_t action, const rubraview_action_facts_t *f) {
    rubraview_action_state_t st = { .enabled = true, .mark = RUBRAVIEW_MARK_NONE };
    if (!f) return st;

    for (size_t i = 0; i < sizeof(NEEDS_PAGE) / sizeof(NEEDS_PAGE[0]); ++i) {
        if (rubraview_u8_eq_lit(action, NEEDS_PAGE[i])) st.enabled = f->has_page;
    }
    if (rubraview_u8_eq_lit(action, "next_archive") || rubraview_u8_eq_lit(action, "prev_archive")) st.enabled = f->archive_series;
    /* A page inside an archive is not a file to take away: it can be copied out, not moved. */
    if (rubraview_u8_eq_lit(action, "move_to_folder")) st.enabled = f->has_page && !f->archive_series;
    /* Export writes a picture: a film or a sound has none to write. */
    if (rubraview_u8_eq_lit(action, "quick_export")) st.enabled = f->has_page && !f->media;
    /* Sound has no frames to step. */
    if (rubraview_u8_eq_lit(action, "anim_step_forward") || rubraview_u8_eq_lit(action, "anim_step_back")) {
        st.enabled = f->media ? f->video : f->frames;
    }
    if (rubraview_u8_eq_lit(action, "next_audio_track")) st.enabled = f->other_audio_track;
    if (rubraview_u8_eq_lit(action, "next_subtitle_track")) st.enabled = f->other_subtitle;
    if (rubraview_u8_eq_lit(action, "toggle_subtitles") || rubraview_u8_eq_lit(action, "choose_subtitle_track")) {
        st.enabled = f->has_subtitles;
    }
    if (rubraview_u8_eq_lit(action, "toggle_subtitle_window")) st.enabled = f->media;
    if (rubraview_u8_eq_lit(action, "extract_subtitles")) st.enabled = f->media && f->video;
    if (rubraview_u8_eq_lit(action, "subtitle_earlier") || rubraview_u8_eq_lit(action, "subtitle_later") ||
        rubraview_u8_eq_lit(action, "subtitle_earlier_fine") || rubraview_u8_eq_lit(action, "subtitle_later_fine") ||
        rubraview_u8_eq_lit(action, "subtitle_sync_reset")) st.enabled = f->subtitle_shown;

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
        { "toggle_titlebar_pin",   offsetof(rubraview_action_facts_t, titlebar_pinned) },   /* 1.20.0 */
        { "media_mute",            offsetof(rubraview_action_facts_t, muted) },
        { "toggle_subtitles",      offsetof(rubraview_action_facts_t, subtitle_shown) },
    };
    for (size_t i = 0; i < sizeof(TOGGLES) / sizeof(TOGGLES[0]); ++i) {
        if (rubraview_u8_eq_lit(action, TOGGLES[i].action)) {
            bool on = *(const bool*)((const char*)f + TOGGLES[i].offset);
            st.mark = on ? RUBRAVIEW_MARK_ON : RUBRAVIEW_MARK_OFF;
        }
    }

    /* The layout and the way its pages run are one choice (owner, 2026-10-11). */
    static const struct { const char *action; const char *value; } LAYOUTS[RUBRAVIEW_LAYOUT_CHOICE_COUNT] = {
        /* the short names are the settings' own (Settings › Viewer › Layout), owner, 2026-10-14 */
        [RUBRAVIEW_LAYOUT_CHOICE_SINGLE]    = { "layout_single",    "single" },
        [RUBRAVIEW_LAYOUT_CHOICE_DUAL]      = { "layout_dual",      "dual" },
        [RUBRAVIEW_LAYOUT_CHOICE_DUAL_RTL]  = { "layout_dual_rtl",  "dual-rtl" },
        [RUBRAVIEW_LAYOUT_CHOICE_BOOK]      = { "layout_book",      "book" },
        [RUBRAVIEW_LAYOUT_CHOICE_BOOK_RTL]  = { "layout_book_rtl",  "book-rtl" },
        [RUBRAVIEW_LAYOUT_CHOICE_WEBTOON]   = { "layout_webtoon",   "webtoon" },
        [RUBRAVIEW_LAYOUT_CHOICE_COMIC]     = { "layout_comic",     "comic" },
        [RUBRAVIEW_LAYOUT_CHOICE_COMIC_RTL] = { "layout_comic_rtl", "comic-rtl" },
    };
    rubraview_layout_choice_t choice = rubraview_layout_choice_of(f->layout, f->rtl ? RUBRAVIEW_READING_RTL : RUBRAVIEW_READING_LTR);
    if (rubraview_u8_eq_lit(action, "toggle_layout")) st.value = LAYOUTS[choice].value;
    if (rubraview_u8_eq_lit(action, LAYOUTS[choice].action)) st.mark = RUBRAVIEW_MARK_CURRENT;
    /* The shape the picture is shown in (owner, 2026-10-11). */
    rubraview_aspect_t aspect = rubraview_aspect_from_action(action);
    if (aspect != RUBRAVIEW_ASPECT_COUNT) {
        st.enabled = f->has_page;
        if ((int32_t)aspect == f->aspect) st.mark = RUBRAVIEW_MARK_CURRENT;
    }
    if (rubraview_u8_eq_lit(action, "aspect_cycle")) {
        st.enabled = f->has_page;
        st.value = rubraview_aspect_label((rubraview_aspect_t)f->aspect);
    }
    if (rubraview_u8_eq_lit(action, "aspect_flip")) {
        st.enabled = f->has_page && f->aspect != (int32_t)RUBRAVIEW_ASPECT_ORIGINAL;
        st.mark = f->aspect_on_its_side ? RUBRAVIEW_MARK_ON : RUBRAVIEW_MARK_OFF;
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
        if (rubraview_u8_eq_lit(action, FITS[i].action) && f->fit == FITS[i].fit) st.mark = RUBRAVIEW_MARK_CURRENT;
    }
    return st;
}

u8str_t rubraview_tile_caption(u8str_t label, bool submenu, rubraview_tile_mark_t mark, const char *value,
                               char *buffer, size_t buffer_size) {
    const char *sep = submenu ? " >" : (value || mark == RUBRAVIEW_MARK_ON || mark == RUBRAVIEW_MARK_OFF) ? ": " : "";
    const char *word = submenu ? ""
                     : value ? value
                     : mark == RUBRAVIEW_MARK_ON ? T("on")
                     : mark == RUBRAVIEW_MARK_OFF ? T("off") : "";
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

u8str_t rubraview_scale_caption(char *buffer, size_t buffer_size, double scale) {
    u8str_t none = { .ptr = buffer, .len = 0 };
    if (!buffer || !(scale > 0.0)) return none;
    double percent = floor(scale * 100.0 + 0.5);
    if (percent < 1.0) percent = 1.0;
    if (percent > 999999.0) percent = 999999.0;
    int n = snprintf(buffer, buffer_size, "%.0f%%", percent);
    if (n <= 0 || (size_t)n >= buffer_size) return none;
    return (u8str_t){ .ptr = buffer, .len = (size_t)n };
}

/* ---- toolbox icons (Segoe MDL2 Assets) ---- */

uint32_t rubraview_action_icon(u8str_t action, const rubraview_action_facts_t *f) {
    static const struct { const char *action; uint32_t icon; } ICONS[] = {
        { "media_stop", 0xE71A },            /* Stop */
        { "prev_page", 0xE892 },             /* Previous */
        { "next_page", 0xE893 },             /* Next */
        { "media_seek_back", 0xEB9E },       /* Rewind */
        { "media_seek_forward", 0xEB9D },    /* FastForward */
        { "media_volume_down", 0xE993 },     /* Volume1 */
        { "media_volume_up", 0xE995 },       /* Volume3 */
        { "next_subtitle_track", 0xE7F0 },   /* ClosedCaption */
        { "toggle_subtitles", 0xE7F0 },      /* ClosedCaption: the Sub tile (D-33) */
        { "next_audio_track", 0xE8D6 },      /* Audio */
        { "zoom_in", 0xE8A3 },               /* ZoomIn */
        { "zoom_out", 0xE71F },              /* ZoomOut */
        { "rotate_cw", 0xE7AD },             /* Rotate */
        { "toggle_slideshow", 0xE786 },      /* Slideshow */
        { "anim_step_back", 0xE76B },        /* ChevronLeft */
        { "anim_step_forward", 0xE76C },     /* ChevronRight */
        { "subpage_prev", 0xE76B },
        { "subpage_next", 0xE76C },
        { "prev_archive", 0xE8AC },          /* folder with a way back: "Back" to the previous book */
        { "next_archive", 0xE8AD },
        { "fit_window", 0xE9A6 },            /* FitPage */
        /* The fuller toolbox (owner, 2026-09-29). */
        { "toggle_info", 0xE946 },           /* Info */
        { "open_picker", 0xE8E5 },           /* OpenFile */
        { "open_folder", 0xE838 },           /* FolderOpen */
        { "recent_clear", 0xE74D },          /* Delete: File > Recent is emptied */
        { "open_settings", 0xE713 },         /* Settings */
        { "toggle_help", 0xE897 },           /* Help */
        { "open_edit", 0xE70F },             /* Edit */
        { "quick_export", 0xE74E },          /* Save */
        { "toggle_playlist", 0xE8FD },       /* BulletedList */
        { "toggle_always_on_top", 0xE718 },  /* Pin */
        { "toggle_titlebar_pin", 0xE840 },   /* Pinned: the title bar kept shown (1.20.0) */
        { "toggle_osd", 0xE7C3 },            /* Page: the information bar (1.20.0) */
        { "rename_file", 0xE8AC },           /* Rename */
        { "toggle_file_list", 0xE8A9 },      /* ViewAll: the files in the archive, with a preview */
        { "toggle_subtitle_window", 0xE8BD },  /* Message: the subtitles as a list (RV-115) */
        { "extract_subtitles", 0xE8DE },     /* MoveToFolder: a film's own subtitles written beside it */
        { "copy_to_folder", 0xE8C8 },        /* Copy */
        { "toggle_classify", 0xE8EC },       /* Tag */
        { "move_to_folder", 0xE8DE },        /* MoveToFolder */
    };
    if (!f) return 0;
    /* D-123: while the slide show runs its tile is the way to stop it. */
    if (rubraview_u8_eq_lit(action, "toggle_slideshow") && f->slideshow) return 0xE71A;   /* Stop */
    if (rubraview_u8_eq_lit(action, "media_play_pause")) return f->playing ? 0xE769 : 0xE768;   /* Pause / Play */
    if (rubraview_u8_eq_lit(action, "media_mute")) return f->muted ? 0xE767 : 0xE74F;           /* Volume / Mute */
    if (rubraview_u8_eq_lit(action, "toggle_fullscreen")) return f->fullscreen ? 0xE73F : 0xE740; /* BackToWindow / FullScreen */
    if (rubraview_u8_eq_lit(action, "toggle_layout")) {
        /* The layout it is in now: Page, TwoPage, ReadingMode (a book), ScrollUpDown (the strip). */
        switch (f->layout) {
            case RUBRAVIEW_PAGE_LAYOUT_DUAL:    return 0xE89A;
            case RUBRAVIEW_PAGE_LAYOUT_BOOK:    return 0xE736;
            case RUBRAVIEW_PAGE_LAYOUT_WEBTOON: return 0xEC8F;
            case RUBRAVIEW_PAGE_LAYOUT_COMIC:   return 0xE8A1;   /* PreviewLink: a page cut down the middle */
            default:                            return 0xE7C3;
        }
    }
    if (rubraview_u8_eq_lit(action, "fit_cycle")) {
        /* The fit it is in now (owner, 2026-10-15): FitPage, Switch (side to side), Sort (top to
           bottom), LightningBolt, FullScreen; real size has none and says it in words. */
        switch (f->fit) {
            case RUBRAVIEW_FIT_WINDOW:  return 0xE9A6;
            case RUBRAVIEW_FIT_WIDTH:   return 0xE8AB;
            case RUBRAVIEW_FIT_HEIGHT:  return 0xE8CB;
            case RUBRAVIEW_FIT_SMART:   return 0xE945;
            case RUBRAVIEW_FIT_STRETCH: return 0xE740;
            default:                    return 0;
        }
    }
    if (rubraview_u8_eq_lit(action, "media_repeat_cycle")) {
        /* RepeatOne / RepeatAll / Shuffle; "Once" and "Next" say it in words. */
        switch (f->repeat_mode) {
            case RUBRAVIEW_REPEAT_ONE:     return 0xE8ED;
            case RUBRAVIEW_REPEAT_ALL:     return 0xE8EE;
            case RUBRAVIEW_REPEAT_SHUFFLE: return 0xE8B1;
            default:                       return 0;
        }
    }
    for (size_t i = 0; i < sizeof(ICONS) / sizeof(ICONS[0]); ++i) {
        if (rubraview_u8_eq_lit(action, ICONS[i].action)) return ICONS[i].icon;
    }
    return 0;
}

uint32_t rubraview_pin_icon(bool on) {
    return on ? 0xE840 : 0xE718;   /* Pinned / Pin */
}

const char *rubraview_action_hint(u8str_t action) {
    const struct { const char *action; const char *hint; } HINTS[] = {
        { "toggle_nearest", T("Crisp - enlarged, every pixel stays a sharp square instead of being smoothed (pixel art, icons, old games). Click: on / off (N)") },
        { "toggle_pixel_grid", T("Grid - zoomed past 400 %, a thin line around every pixel, to see each one. Click: on / off (G)") },
    };
    for (size_t i = 0; i < sizeof(HINTS) / sizeof(HINTS[0]); ++i) {
        if (rubraview_u8_eq_lit(action, HINTS[i].action)) return HINTS[i].hint;
    }
    return NULL;
}
