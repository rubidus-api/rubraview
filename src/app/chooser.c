/*
 * RV-310 (an outside review, 2026-11-19) and RV-311: the one-of-many list (used by tiles, panels, lists and the
 * settings) and the lists' sorting, taken out of settingswin.c as they were.
 */
#include "app.h"
#include "rubraview/keyboard.h"
#include "settingslook.h"

/* ---- 1.24.0: the one-of-many list (owner, 2026-10-19: "여러개 중에 하나를 선택해야 되는 기능이라면 ...
   하위요소 팝업창이 떠서 그 안에서 선택"). A tile's list has rows for a finger; the settings window's is
   a list of text. The keys that step through a choice (B, Shift+A ...) still step. ---- */

static rubraview_window_t *chooser_window(const app_state_t *app, int where) {
    return where == CHOOSE_IN_SETTINGS ? app->settings_window : where == CHOOSE_IN_EDIT ? app->edit_window
         : where == CHOOSE_IN_FILES ? app->files_window : where == CHOOSE_IN_BATCH ? app->batch_window
         : where == CHOOSE_IN_NAMES ? app->names_window : app->window;
}

static void chooser_word_args(chooser_t *c, double value, const char *fmt, const proven_arg_t *fmt_args, size_t fmt_count) {
    if (c->count >= CHOOSE_MAX) return;
    rubraview_fmt_length(proven_fmt_to_internal(c->words[c->count], sizeof(c->words[0]), fmt, fmt_args, fmt_count));
    c->values[c->count++] = value;
}
#define chooser_word(c, value, fmt, ...) chooser_word_args(c, value, fmt, RUBRAVIEW_FMT_ARGS(__VA_ARGS__))

static void chooser_dirty(app_state_t *app) {
    if (app->chooser.where == CHOOSE_IN_SETTINGS) app->settings_dirty = true;
    else if (app->chooser.where == CHOOSE_IN_EDIT) app->edit_dirty = true;
    else if (app->chooser.where == CHOOSE_IN_FILES) app->files_dirty = true;
    else if (app->chooser.where == CHOOSE_IN_BATCH) app->batch_dirty = true;   /* RV-194 */
    else if (app->chooser.where == CHOOSE_IN_NAMES) app->names_dirty = true;
    app->toolbox_window_drawn = 0.0;
}

void chooser_close(app_state_t *app) {
    if (!app->chooser.open) return;
    chooser_dirty(app);
    app->chooser.open = false;
}

/* Laid out under (`at_x`, `at_y`), or over `above_y` when there is no room under it. */
static void chooser_show(app_state_t *app, int where, double at_x, double at_y, double above_y) {
    chooser_t *c = &app->chooser;
    if (c->count == 0) { c->open = false; return; }
    rubraview_window_t *win = chooser_window(app, where);
    if (!win) { c->open = false; return; }
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(win, &w, &h);
    double row_h, width;
    if (where == CHOOSE_IN_SETTINGS) {   /* a list of text, a line a choice */
        size_t longest = proven_cstr_len(c->title);
        for (size_t i = 0; i < c->count; ++i) { size_t n = proven_cstr_len(c->words[i]); if (n > longest) longest = n; }
        row_h = app->settings_cell_h;
        width = app->settings_cell_w * (double)(longest + 6);
    } else {                             /* for a finger */
        double dpi = rubraview_pal_window_dpi_scale(win);
        row_h = 44.0 * dpi;
        width = 300.0 * dpi;
    }
    c->where = where;
    c->focus = c->current;
    c->layout = rubraview_chooser_place((double)w, (double)h, c->count, row_h, width, at_x, at_y, above_y,
                                        c->current >= 0 ? (size_t)c->current : 0);
    c->open = true;
    chooser_dirty(app);
}

static int32_t chooser_nearest(const chooser_t *c, double value) {
    int32_t best = -1;
    for (size_t i = 0; i < c->count; ++i)
        if (best < 0 || fabs(c->values[i] - value) < fabs(c->values[best] - value)) best = (int32_t)i;
    return best;
}

/* A tile that chooses one of several: its list instead of the next one. False for any other tile. */
/* RV-237: the output devices as the list showed them, for the row picked */
#define AUDIO_DEVICES_MAX 16
static rubraview_audio_device_t audio_devices[AUDIO_DEVICES_MAX];
static size_t audio_devices_count;

bool chooser_open_tile(app_state_t *app, proven_u8str_view_t action, double x, double y, double above_y) {
    chooser_t *c = &app->chooser;
    *c = (chooser_t){ .kind = CHOOSE_TILE, .current = -1 };
    if (action.size >= sizeof(c->action)) return false;
    proven_sys_mem_copy(c->action, action.ptr, action.size);
    const char *title = NULL;
    if (proven_u8str_view_eq(action, PROVEN_LIT("zoom_cycle"))) {
        title = T("Zoom");
        chooser_word(c, 0.0, "{}", PROVEN_ARG(T("Real size")));
        for (int p = 20; p <= 200; p += 20) chooser_word(c, (double)p, "{}%", PROVEN_ARG((int)p));
        c->current = at_real_size(app) ? 0 : chooser_nearest(c, app->zoom * 100.0);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("fit_cycle"))) {
        title = T("Fit");
        chooser_word(c, RUBRAVIEW_FIT_WINDOW, "{}", PROVEN_ARG(T("fit: the window")));
        chooser_word(c, RUBRAVIEW_FIT_WIDTH, "{}", PROVEN_ARG(T("fit: the width")));
        chooser_word(c, RUBRAVIEW_FIT_HEIGHT, "{}", PROVEN_ARG(T("fit: the height")));
        chooser_word(c, RUBRAVIEW_FIT_SMART, "{}", PROVEN_ARG(T("fit: smart (never enlarged)")));
        chooser_word(c, RUBRAVIEW_FIT_STRETCH, "{}", PROVEN_ARG(T("fit: stretched to fill")));
        c->current = chooser_nearest(c, (double)app->fit_mode);
        if (fabs(c->values[c->current] - (double)app->fit_mode) > 0.5) c->current = -1;
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_layout"))) {
        title = T("Layout");
        for (int i = 0; i < (int)RUBRAVIEW_LAYOUT_CHOICE_COUNT; ++i) chooser_word(c, i, "{}", PROVEN_ARG(layout_said((rubraview_layout_choice_t)i)));
        c->current = (int32_t)rubraview_layout_choice_of(app->layout_opts.mode, app->layout_opts.direction);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_speed_cycle"))) {
        title = T("Speed");
        static const double SPEEDS[] = { 0.5, 0.75, 1.0, 1.25, 1.5, 2.0 };
        for (size_t i = 0; i < 6; ++i) chooser_word(c, SPEEDS[i], "{}x", PROVEN_ARG(rubraview_g_text((char[32]){0}, (double)(SPEEDS[i]), 6)));
        c->current = chooser_nearest(c, app->media_speed > 0.0 ? app->media_speed : 1.0);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("interval_cycle"))) {
        title = T("Delay");
        static const double DELAYS[] = { 1.0, 2.0, 3.0, 5.0, 10.0 };
        for (size_t i = 0; i < 5; ++i) chooser_word(c, DELAYS[i], T("{} s"), PROVEN_ARG(rubraview_g_text((char[32]){0}, (double)(DELAYS[i]), 6)));
        c->current = chooser_nearest(c, app->slideshow.interval_seconds);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_repeat_cycle")) && anim_plays(app)) {
        title = T("At the end");
        chooser_word(c, 0, "{}", PROVEN_ARG(T("an animation goes round and round")));
        chooser_word(c, 1, "{}", PROVEN_ARG(T("an animation plays once and stops")));
        chooser_word(c, 2, "{}", PROVEN_ARG(T("after an animation, the next file")));
        c->current = anim_end_mode(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_repeat_cycle"))) {
        title = T("At the end");
        for (int i = 0; i < (int)RUBRAVIEW_REPEAT_MODE_COUNT; ++i) chooser_word(c, i, "{}", PROVEN_ARG(rubraview_repeat_sentence((rubraview_repeat_mode_t)i)));
        c->current = (int32_t)repeat_mode(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_eq_cycle"))) {
        title = T("Equaliser");
        for (int i = 0; i < RUBRAVIEW_EQWIN_PRESETS; ++i) { proven_u8str_view_t n = eq_preset_label(i); chooser_word(c, i, "{}", PROVEN_ARG(rubraview_bytes_view(n.ptr, (int)n.size))); }
        c->current = eq_preset_now(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_visualizer_cycle"))) {
        title = T("Analyser");
        chooser_word(c, 0, "{}", PROVEN_ARG(T("analyser off")));
        chooser_word(c, 1, "{}", PROVEN_ARG(T("analyser: spectrum")));
        chooser_word(c, 2, "{}", PROVEN_ARG(T("analyser: oscilloscope")));
        c->current = (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("visualizer")));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_breaks")) && app->text) {
        title = T("Line breaks");
        chooser_word(c, 0, "{}", PROVEN_ARG(T("line breaks as they are")));
        chooser_word(c, 1, "{}", PROVEN_ARG(T("all in one run, no line breaks")));
        chooser_word(c, 2, "{}", PROVEN_ARG(T("blank lines dropped")));
        chooser_word(c, 3, "{}", PROVEN_ARG(T("paragraphs found (a sentence's end and more line breaks)")));
        c->current = (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("breaks")));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_encoding")) && app->text) {
        title = T("Encoding");
        for (int i = 0; i < (int)RUBRAVIEW_TEXTENC_COUNT; ++i) {
            if (i == (int)RUBRAVIEW_TEXTENC_AUTO) continue;
            chooser_word(c, i, "{}", PROVEN_ARG(rubraview_textenc_name((rubraview_textenc_t)i)));
        }
        c->current = chooser_nearest(c, (double)text_enc_now(app));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("next_audio_track")) && app->media) {
        title = T("Sound track");
        for (size_t i = 0; i < app->tracks.count; ++i) {
            if (app->tracks.tracks[i].kind != RUBRAVIEW_TRACK_AUDIO) continue;
            char label[160];
            proven_u8str_view_t text = rubraview_track_label(rubraview_mem_mut(label, sizeof(label)), &app->tracks, (int32_t)i);
            if ((int32_t)i == app->tracks.current_audio) c->current = (int32_t)c->count;
            chooser_word(c, (double)i, "{}", PROVEN_ARG(rubraview_bytes_view(text.ptr, (int)text.size)));
        }
        if (c->count < 2) { osd_say(app, TU8("this file has only one sound track")); c->count = 0; return true; }
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("audio_device"))) {
        /* RV-237 (owner, 2026-11-07): where the sound goes — Windows' default for every app, or this app alone */
        title = T("Sound output");
        audio_devices_count = rubraview_pal_audio_devices(audio_devices, AUDIO_DEVICES_MAX);
        proven_u8str_view_t mine = rubraview_settings_get_text(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("output_device"));
        for (size_t i = 0; i < audio_devices_count; ++i)
            chooser_word(c, 100.0 + (double)i, T("{} Every app (Windows): {}"), PROVEN_ARG(audio_devices[i].is_default ? "\xE2\x97\x8F" : "\xE2\x97\x8B"),
                         PROVEN_ARG(audio_devices[i].name));
        chooser_word(c, 200.0, T("{} This app only: as Windows"), PROVEN_ARG(mine.size == 0 ? "\xE2\x97\x8F" : "\xE2\x97\x8B"));
        for (size_t i = 0; i < audio_devices_count; ++i) {
            bool on = mine.size > 0 && proven_u8str_view_eq(mine, proven_u8str_view_from_cstr(audio_devices[i].id));
            chooser_word(c, 201.0 + (double)i, T("{} This app only: {}"), PROVEN_ARG(on ? "\xE2\x97\x8F" : "\xE2\x97\x8B"), PROVEN_ARG(audio_devices[i].name));
        }
        if (audio_devices_count == 0) { osd_say(app, TU8("Windows has no sound output on")); c->count = 0; return true; }
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("send_file"))) {
        /* RV-232 (owner, 2026-11-07): where the file on screen goes, in every kind of file; the folders set are named */
        title = T("Send");
        bool page_of_archive = app->source.kind != RUBRAVIEW_PAGE_SOURCE_FOLDER;
        proven_u8str_view_t cd = app->curation.copy_dir, md = app->curation.move_dir;
        if (cd.size > 0) chooser_word(c, 0, T("Copy to {}"), PROVEN_ARG(rubraview_bytes_view(cd.ptr, (int)(cd.size < 120 ? cd.size : 120))));
        else chooser_word(c, 0, "{}", PROVEN_ARG(T("Copy to folder (choose it)")));
        if (!page_of_archive) {
            if (md.size > 0) chooser_word(c, 1, T("Move to {}"), PROVEN_ARG(rubraview_bytes_view(md.ptr, (int)(md.size < 120 ? md.size : 120))));
            else chooser_word(c, 1, "{}", PROVEN_ARG(T("Move to folder (choose it)")));
        }
        chooser_word(c, 2, "{}", PROVEN_ARG(T("Copy to another folder...")));
        if (!page_of_archive) {
            chooser_word(c, 3, "{}", PROVEN_ARG(T("Move to another folder...")));
            chooser_word(c, 4, "{}", PROVEN_ARG(T("Classify...")));
        }
    } else {
        return false;
    }
    rubraview_fmt(c->title, sizeof(c->title), "{}", PROVEN_ARG(title));
    chooser_show(app, CHOOSE_IN_MAIN, x, y, above_y);
    return true;
}

static void chooser_pick_tile(app_state_t *app, size_t i) {
    chooser_t *c = &app->chooser;
    proven_u8str_view_t action = cstr(c->action);
    double v = c->values[i];
    if (proven_u8str_view_eq(action, PROVEN_LIT("zoom_cycle"))) {
        if (v == 0.0) {
            if (app->fit_mode != RUBRAVIEW_FIT_ACTUAL_SIZE) app->fit_beside_real = app->fit_mode;
            app->fit_mode = RUBRAVIEW_FIT_ACTUAL_SIZE;
            reset_view(app);
        } else {
            zoom_leave_real_fit(app);
            app->zoom = v / 100.0;
        }
        zoom_say(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("fit_cycle"))) {
        app->fit_mode = (rubraview_fit_mode_t)(int)v;
        reset_view(app);
        osd_say(app, cstr(c->words[i]));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_layout"))) {
        layout_set(app, (rubraview_layout_choice_t)(int)v, true);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_speed_cycle"))) {
        speed_set(app, v);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("interval_cycle"))) {
        interval_set(app, v);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_repeat_cycle")) && anim_plays(app)) {
        rubraview_settings_set(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("animation_end"), v);
        osd_say(app, cstr(c->words[i]));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_repeat_cycle"))) {
        rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("at_end"), v);
        app->follow_for = -1;   /* decided again, in the new mode */
        osd_say(app, cstr(c->words[i]));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_eq_cycle"))) {
        rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("eq_preset"), v);
        audio_chain_update(app);
        char line[96];
        int n = rubraview_fmt(line, sizeof(line), T("equaliser: {}"), PROVEN_ARG(c->words[i]));
        if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_visualizer_cycle"))) {
        rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("visualizer"), v);
        osd_say(app, cstr(c->words[i]));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_breaks")) && app->text) {
        rubraview_settings_set(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("breaks"), v);
        if (!text_rebuild(app)) { osd_say(app, TU8("out of memory")); return; }
        osd_say(app, cstr(c->words[i]));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_encoding")) && app->text) {
        text_encoding_set(app, (rubraview_textenc_t)(int)v);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("next_audio_track"))) {
        audio_track_set(app, (int32_t)v);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("audio_device"))) {
        int k = (int)lround(v);
        char line[200];
        int n = 0;
        if (k >= 100 && k < 100 + (int)audio_devices_count) {
            const rubraview_audio_device_t *d = &audio_devices[k - 100];
            bool ok = rubraview_pal_audio_set_system_default(d->id);
            n = ok ? rubraview_fmt(line, sizeof(line), T("Windows' sound output: {}"), PROVEN_ARG(d->name))
                   : rubraview_fmt(line, sizeof(line), "{}", PROVEN_ARG(T("Windows did not take that sound output")));
        } else if (k == 200) {
            rubraview_settings_set_text(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("output_device"), PROVEN_LIT(""));
            settings_took_effect(app);
            n = rubraview_fmt(line, sizeof(line), "{}", PROVEN_ARG(T("this app's sound goes where Windows' does")));
        } else if (k > 200 && k <= 200 + (int)audio_devices_count) {
            const rubraview_audio_device_t *d = &audio_devices[k - 201];
            rubraview_settings_set_text(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("output_device"), app_keep(app, proven_u8str_view_from_cstr(d->id)));
            settings_took_effect(app);
            n = rubraview_fmt(line, sizeof(line), T("this app's sound: {}"), PROVEN_ARG(d->name));
        }
        if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("send_file"))) {
        static const char *const SENT[] = { "copy_to_folder", "move_to_folder", "copy_to_other_folder", "move_to_other_folder", "toggle_classify" };
        int k = (int)v;
        if (k >= 0 && k < 5) handle_action(app, cstr(SENT[k]));
    }
}

/* 1.25.0: the wheel over a one-of-many tile: the choice before or after the one in force, round. */
bool chooser_wheel_tile(app_state_t *app, proven_u8str_view_t action, double notches) {
    if (proven_u8str_view_eq(action, PROVEN_LIT("next_audio_track")) || proven_u8str_view_eq(action, PROVEN_LIT("send_file")) ||
        proven_u8str_view_eq(action, PROVEN_LIT("audio_device")) ||
        notches == 0.0) return false;   /* tracks: the list; Send: never by the wheel (RV-232) */
    bool was_open = app->chooser.open;
    chooser_t keep = app->chooser;
    if (!chooser_open_tile(app, action, 0.0, 0.0, 0.0)) { app->chooser = keep; return false; }
    chooser_t *c = &app->chooser;
    c->open = false;
    if (c->count == 0) { if (was_open) app->chooser = keep; return true; }
    int32_t n = (int32_t)c->count, at = c->current < 0 ? 0 : c->current;
    at = ((at + (notches > 0.0 ? -1 : 1)) % n + n) % n;   /* away from you: the one before */
    chooser_pick_tile(app, (size_t)at);
    app->chooser.open = false;
    return true;
}

/* A panel's choice (print, convert, export, Edit): its words, as the row says them for each value. */
void chooser_open_panel(app_state_t *app, int32_t which, int32_t row, double x, double y, double above_y) {
    rubraview_panel_t *panel = which == 2 ? &app->sel_panel : &app->panel;   /* 3, 4 (RV-194): Batch's, the names' — standing in */
    if (row < 0 || (size_t)row >= panel->row_count || panel->rows[row].kind != RUBRAVIEW_ROW_CHOICE) return;
    chooser_t *c = &app->chooser;
    *c = (chooser_t){ .kind = CHOOSE_PANEL, .panel = which, .row = row, .current = (int32_t)lround(panel->rows[row].value) };
    rubraview_fmt(c->title, sizeof(c->title), "{}", PROVEN_ARG(rubraview_bytes_view(panel->rows[row].label.ptr, (int)panel->rows[row].label.size)));
    int32_t n = panel->rows[row].choice_count > 0 ? panel->rows[row].choice_count : (int32_t)panel->rows[row].max_value + 1;
    for (int32_t i = 0; i < n && i < CHOOSE_MAX; ++i) {
        rubraview_panel_row_t probe = panel->rows[row];
        probe.value = (double)i;
        char words[96];
        proven_u8str_view_t text = which == 1 && (app->panel_is_print || app->panel_is_convert) ? print_row_text(&probe, rubraview_mem_mut(words, sizeof(words)))
                                                                                     : adjust_row_text(app, &probe, rubraview_mem_mut(words, sizeof(words)));
        chooser_word(c, (double)i, "{}", PROVEN_ARG(rubraview_bytes_view(text.ptr, (int)text.size)));
    }
    chooser_show(app, which == 3 ? CHOOSE_IN_BATCH : which == 4 ? CHOOSE_IN_NAMES
                    : app->edit_window && edit_panel_open(app) ? CHOOSE_IN_EDIT : CHOOSE_IN_MAIN, x, y, above_y);
}

/* A setting's choice: its words, a line each, under its row. */
void chooser_open_setting(app_state_t *app) {
    const rubraview_settings_view_t *v = &app->settings_view;
    const rubraview_setting_def_t *def = rubraview_settings_view_focused_def(v);
    if (!def || def->type != RUBRAVIEW_SETTING_CHOICE) return;
    chooser_t *c = &app->chooser;
    *c = (chooser_t){ .kind = CHOOSE_SETTING, .def = def,
                      .current = (int32_t)lround(rubraview_settings_get(&app->settings, def->section, def->key)) };
    proven_u8str_view_t label = rubraview_tr_u8(def->label);
    rubraview_fmt(c->title, sizeof(c->title), "{}", PROVEN_ARG(rubraview_bytes_view(label.ptr, (int)label.size)));
    for (int32_t i = 0; i < def->choice_count && i < CHOOSE_MAX; ++i) {
        proven_u8str_view_t w = rubraview_settings_choice_word(def, i);
        chooser_word(c, (double)i, "{}", PROVEN_ARG(rubraview_bytes_view(w.ptr, (int)w.size)));
    }
    int32_t row = rubraview_settings_view_screen_row(v, (size_t)v->focus_line);
    double x = app->settings_cell_w * (double)(v->content_col + v->label_cols);
    double ry = settings_row_y(app, row);
    chooser_show(app, CHOOSE_IN_SETTINGS, x, ry + app->settings_cell_h, ry);
}

/* The open-file screen's sorting: by which column, and which way. */
void chooser_open_sort(app_state_t *app, bool order, double x, double y, double above_y) {
    chooser_t *c = &app->chooser;
    *c = (chooser_t){ .kind = order ? CHOOSE_ORDER : CHOOSE_SORT };
    int key = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort"))) - 1;
    bool desc = rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort_desc")) > 0.5;
    if (order) {
        rubraview_fmt(c->title, sizeof(c->title), "{}", PROVEN_ARG(T("Order")));
        chooser_word(c, 0, "{} \xE2\x96\xB2", PROVEN_ARG(T("Rising")));
        chooser_word(c, 1, "{} \xE2\x96\xBC", PROVEN_ARG(T("Falling")));
        c->current = desc ? 1 : 0;
    } else {
        rubraview_fmt(c->title, sizeof(c->title), "{}", PROVEN_ARG(T("Sort by")));
        for (int k = 0; k < RUBRAVIEW_PICKER_SORT_COUNT; ++k) chooser_word(c, k, "{}", PROVEN_ARG(rubraview_picker_sort_name((rubraview_picker_sort_key_t)k)));
        c->current = key;
    }
    chooser_show(app, CHOOSE_IN_MAIN, x, y, above_y);
}

/* 1.25.0: the list's .ext — every extension in it, each with its box; a pick turns one on or off
   and the list stays open. The first row shows them all again. */
void chooser_open_listext(app_state_t *app, int where, double x, double y, double above_y) {
    chooser_t *c = &app->chooser;
    size_t first = c->open && c->kind == CHOOSE_LISTEXT ? c->layout.first : 0;
    *c = (chooser_t){ .kind = CHOOSE_LISTEXT, .current = -1 };
    rubraview_fmt(c->title, sizeof(c->title), "{}", PROVEN_ARG(T("Extensions shown")));
    chooser_word(c, -1.0, "{}", PROVEN_ARG(T("All")));
    char seen[CHOOSE_MAX][16];
    size_t counts[CHOOSE_MAX] = {0}, kinds = 0, count = page_count(app);
    for (size_t i = 0; i < count; ++i) {
        proven_u8str_view_t e = rubraview_path_ext(app->source.pages[i].path);
        if (e.size == 0 || e.size >= sizeof(seen[0])) e = PROVEN_LIT(".");
        size_t k = 0;
        for (; k < kinds; ++k) {
            proven_u8str_view_t have = cstr(seen[k]);
            if (proven_u8str_view_eq_ascii_ci(have, e)) break;
        }
        if (k == kinds) {
            if (kinds + 1 >= CHOOSE_MAX) continue;
            for (size_t j = 0; j < e.size; ++j) seen[kinds][j] = rubraview_ascii_lower(e.ptr[j]);
            seen[kinds][e.size] = '\0';
            kinds++;
        }
        counts[k]++;
    }
    for (size_t k = 0; k < kinds; ++k)
        chooser_word(c, (double)k, "{}  {}  ({})", PROVEN_ARG(list_ext_is_off(app, cstr(seen[k])) ? "\xE2\x98\x90" : "\xE2\x98\x91"),
                     PROVEN_ARG(seen[k][1] ? seen[k] : T("(none)")), PROVEN_ARG((int)counts[k]));
    proven_sys_mem_copy(app->list_ext_seen, seen, sizeof(seen));
    chooser_show(app, where, x, y, above_y);
    if (first > 0 && first < c->count) c->layout.first = first;
}

/* RV-204 (owner, 2026-10-31: "파일 브라우저 창에서 확장자별로 선택하는 기능 ... 맨 왼쪽 열에"): the open-file
   screen's extensions, each with how many of its files are picked of how many; a pick takes them all, or lets
   them go when all already were, and the list stays open. Folders are not counted. */
void chooser_open_pickext(app_state_t *app, double x, double y, double above_y) {
    chooser_t *c = &app->chooser;
    size_t first = c->open && c->kind == CHOOSE_PICKEXT ? c->layout.first : 0;
    *c = (chooser_t){ .kind = CHOOSE_PICKEXT, .current = -1 };
    rubraview_fmt(c->title, sizeof(c->title), "{}", PROVEN_ARG(T("Pick by extension")));
    char seen[CHOOSE_MAX][16];
    size_t counts[CHOOSE_MAX] = {0}, picked[CHOOSE_MAX] = {0}, kinds = 0;
    for (size_t i = 0; i < app->picker_listing.count; ++i) {
        const rubraview_fs_entry_t *entry = &app->picker_listing.entries[i];
        if (entry->is_directory) continue;
        proven_u8str_view_t e = rubraview_path_ext(entry->name);
        if (e.size == 0 || e.size >= sizeof(seen[0])) e = PROVEN_LIT(".");
        size_t k = 0;
        for (; k < kinds; ++k) if (proven_u8str_view_eq_ascii_ci(cstr(seen[k]), e)) break;
        if (k == kinds) {
            if (kinds >= CHOOSE_MAX) continue;
            for (size_t j = 0; j < e.size; ++j) seen[kinds][j] = rubraview_ascii_lower(e.ptr[j]);
            seen[kinds][e.size] = '\0';
            kinds++;
        }
        counts[k]++;
        if (app->picker_selected && app->picker_selected[i]) picked[k]++;
    }
    if (kinds == 0) { osd_say(app, TU8("no files here")); return; }
    for (size_t k = 0; k < kinds; ++k)
        chooser_word(c, (double)k, "{}  {}  {} / {}", PROVEN_ARG(picked[k] == counts[k] ? "\xE2\x98\x91" : "\xE2\x98\x90"),
                     PROVEN_ARG(seen[k][1] ? seen[k] : T("(none)")), PROVEN_ARG((int)picked[k]), PROVEN_ARG((int)counts[k]));
    proven_sys_mem_copy(app->picker_ext_seen, seen, sizeof(seen));
    chooser_show(app, CHOOSE_IN_MAIN, x, y, above_y);
    if (first > 0 && first < c->count) c->layout.first = first;
}

/* RV-227 (D-282): the baskets' list — to put the waiting paths in one (`put` false), or (`put`) with Copy here | Move
   here above it to put one's files here, and a row to empty each that holds any. Values: 0..4 a basket, -1 the
   choice of copy or move (the list stays open), -2-k empty basket k. */
void chooser_open_basket(app_state_t *app, int mode, double x, double y, double above_y) {
    chooser_t *c = &app->chooser;
    bool put = mode == BASKET_LIST_PUT_HERE, see = mode == BASKET_LIST_SEE;
    *c = (chooser_t){ .kind = CHOOSE_BASKET, .current = -1, .panel = mode };
    rubraview_fmt(c->title, sizeof(c->title), "{}", PROVEN_ARG(see ? T("Baskets: one shows its files") : put ? T("Put a basket's files here") : T("Put them in a basket")));
    if (put)
        chooser_word(c, -1.0, "{} {}   {} {}", PROVEN_ARG(app->basket_moving ? "\xE2\x97\x8B" : "\xE2\x97\x8F"), PROVEN_ARG(T("Copy here")),
                     PROVEN_ARG(app->basket_moving ? "\xE2\x97\x8F" : "\xE2\x97\x8B"), PROVEN_ARG(T("Move here")));
    for (int k = 0; k < RUBRAVIEW_BASKETS; ++k) {
        size_t n = rubraview_basket_count(&app->baskets, k);
        double value = see ? -10.0 - (double)k : (double)k;   /* D-286: seen, its files */
        if (n == 0) { chooser_word(c, value, T("Basket {} \xE2\x80\x94 empty"), PROVEN_ARG(k + 1)); continue; }
        proven_u8str_view_t first = rubraview_path_basename(rubraview_basket_path(&app->baskets, k, 0));
        int shown = (int)(first.size < 40 ? first.size : 40);
        chooser_word(c, value, T("Basket {} \xE2\x80\x94 {}: {}{}"), PROVEN_ARG(k + 1), PROVEN_ARG(n),
                     PROVEN_ARG(rubraview_bytes_view(first.ptr, shown)), PROVEN_ARG(n > 1 ? " ..." : ""));
    }
    if (!see)   /* D-286: each basket's files to see, from either list */
        for (int k = 0; k < RUBRAVIEW_BASKETS; ++k)
            if (rubraview_basket_count(&app->baskets, k) > 0) chooser_word(c, -10.0 - (double)k, T("Show basket {}'s files"), PROVEN_ARG(k + 1));
    if (put)
        for (int k = 0; k < RUBRAVIEW_BASKETS; ++k)
            if (rubraview_basket_count(&app->baskets, k) > 0) chooser_word(c, -2.0 - (double)k, T("Empty basket {}"), PROVEN_ARG(k + 1));
    chooser_show(app, CHOOSE_IN_MAIN, x, y, above_y);
}

/* RV-286 (owner, 2026-11-15: "우클릭 드래그하면 이동 또는 복사를 선택할 수 있는 컨텍스트 메뉴"): files right-dragged onto
   a folder of the open-file screen, as Explorer's menu at the drop. */
void chooser_open_drop(app_state_t *app, double x, double y) {
    chooser_t *c = &app->chooser;
    *c = (chooser_t){ .kind = CHOOSE_DROP, .current = -1 };
    proven_u8str_view_t name = rubraview_path_basename(app->drop_dir);
    rubraview_fmt(c->title, sizeof(c->title), T("{} file(s) into {}"), PROVEN_ARG((uint32_t)app->drop_pending_count), PROVEN_ARG(name));
    chooser_word(c, 1.0, "{}", PROVEN_ARG(T("Move here")));
    chooser_word(c, 2.0, "{}", PROVEN_ARG(T("Copy here")));
    chooser_word(c, 0.0, "{}", PROVEN_ARG(T("Cancel")));
    c->focus = 0;
    chooser_show(app, CHOOSE_IN_MAIN, x, y, y);
}

/* RV-293: a field's right click — Undo, Cut, Copy, Paste, Delete, Select all (a hidden password is never copied) */
void chooser_open_editmenu(app_state_t *app, double x, double y) {
    chooser_t *c = &app->chooser;
    *c = (chooser_t){ .kind = CHOOSE_EDITMENU, .current = -1 };
    rubraview_fmt(c->title, sizeof(c->title), "{}", PROVEN_ARG(T("Edit")));
    bool secret = app->rename_is_password;
    if (rubraview_editctl_can_undo(&app->rename_ec)) chooser_word(c, (double)EDITBOX_UNDO, "{}", PROVEN_ARG(T("Undo")));
    bool sel = rubraview_textedit_has_selection(&app->rename_ec.te);
    if (sel && !secret) chooser_word(c, (double)EDITBOX_CUT, "{}", PROVEN_ARG(T("Cut")));
    if (sel && !secret) chooser_word(c, (double)EDITBOX_COPY, "{}", PROVEN_ARG(T("Copy")));
    chooser_word(c, (double)EDITBOX_PASTE, "{}", PROVEN_ARG(T("Paste")));
    if (sel) chooser_word(c, (double)EDITBOX_DELETE, "{}", PROVEN_ARG(T("Delete")));
    chooser_word(c, (double)EDITBOX_ALL, "{}", PROVEN_ARG(T("Select all")));
    c->focus = 0;
    chooser_show(app, CHOOSE_IN_MAIN, x, y, y);
}

/* D-286 (owner, 2026-11-07: "장바구니 버튼에서 누르면 그 안의 파일들 목록을 볼 수 있는 기능"; "보기 + 하나씩 빼기"): basket
   `which`'s files, each its name and the folder it is in; a click takes that one out of the basket (the file stays where
   it is) and the list stays open. */
void chooser_open_basket_files(app_state_t *app, int which, double x, double y, double above_y) {
    chooser_t *c = &app->chooser;
    *c = (chooser_t){ .kind = CHOOSE_BASKET_FILES, .current = -1, .panel = which };
    size_t n = rubraview_basket_count(&app->baskets, which);
    rubraview_fmt(c->title, sizeof(c->title), T("Basket {}: {} \xE2\x80\x94 a click takes one out"), PROVEN_ARG(which + 1), PROVEN_ARG(n));
    for (size_t i = 0; i < n && c->count < CHOOSE_MAX - 1; ++i) {
        proven_u8str_view_t path = rubraview_basket_path(&app->baskets, which, i);
        proven_u8str_view_t name = rubraview_path_basename(path);
        proven_u8str_view_t folder = rubraview_path_basename(rubraview_path_dirname(path));
        int nn = (int)(name.size < 48 ? name.size : 48), fn = (int)(folder.size < 24 ? folder.size : 24);
        chooser_word(c, (double)i, "\xC3\x97  {}   \xC2\xB7 {}", PROVEN_ARG(rubraview_bytes_view(name.ptr, nn)), PROVEN_ARG(rubraview_bytes_view(folder.ptr, fn)));
    }
    if (n > c->count) chooser_word(c, -1.0, T("... and {} more"), PROVEN_ARG(n - c->count));
    chooser_show(app, CHOOSE_IN_MAIN, x, y, above_y);
}

/* The files of extension row `k` all picked, or all let go when they all were. */
static void picker_ext_pick(app_state_t *app, size_t k) {
    if (k >= CHOOSE_MAX || !app->picker_selected) return;
    proven_u8str_view_t want = cstr(app->picker_ext_seen[k]);
    size_t total = 0, on = 0;
    for (int pass = 0; pass < 2; ++pass) {
        bool pick = on < total;
        for (size_t i = 0; i < app->picker_listing.count; ++i) {
            const rubraview_fs_entry_t *entry = &app->picker_listing.entries[i];
            if (entry->is_directory) continue;
            proven_u8str_view_t e = rubraview_path_ext(entry->name);
            if (e.size == 0 || e.size >= 16) e = PROVEN_LIT(".");
            if (!proven_u8str_view_eq_ascii_ci(e, want)) continue;
            if (pass == 0) { total++; if (app->picker_selected[i]) on++; }
            else app->picker_selected[i] = pick;
        }
        if (pass == 0 && total == 0) return;
        if (pass == 1) {
            if (app->picker.mode == RUBRAVIEW_PICK_SINGLE) rubraview_picker_set_mode(&app->picker, RUBRAVIEW_PICK_INDIVIDUAL);   /* as Same type does */
            char line[96];
            int n = rubraview_fmt(line, sizeof(line), pick ? T("{} {} picked") : T("{} {} let go"), PROVEN_ARG((size_t)total), PROVEN_ARG(want.size > 1 ? app->picker_ext_seen[k] : T("(none)")));
            if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
        }
    }
}

/* RV-192: Shape — wide, tall, square, each with how many are picked of how many; a pick takes all
   of that shape, or lets them go when all already were, and the list stays open. */
void chooser_open_shape(app_state_t *app, int where, double x, double y, double above_y) {
    uint8_t *shapes = NULL;
    size_t *rows = NULL, n = 0;
    bool *selected = NULL;
    if (!shape_table(app, app->shape_for, &shapes, &rows, &n, &selected)) return;
    size_t total[RUBRAVIEW_SHAPE_COUNT], picked[RUBRAVIEW_SHAPE_COUNT];
    rubraview_shape_count(selected, shapes, rows, n, total, picked);
    rubraview_free(shapes);
    rubraview_free(rows);
    chooser_t *c = &app->chooser;
    *c = (chooser_t){ .kind = CHOOSE_SHAPE, .current = -1 };
    rubraview_fmt(c->title, sizeof(c->title), "{}", PROVEN_ARG(T("Pick by shape")));
    for (int k = RUBRAVIEW_SHAPE_WIDE; k < RUBRAVIEW_SHAPE_COUNT; ++k) {
        bool all = total[k] > 0 && picked[k] == total[k];
        chooser_word(c, (double)k, "{}  {}  {} / {}", PROVEN_ARG(all ? "\xE2\x98\x91" : "\xE2\x98\x90"), PROVEN_ARG(shape_name(k)), PROVEN_ARG((int)picked[k]), PROVEN_ARG((int)total[k]));
    }
    if (total[RUBRAVIEW_SHAPE_UNKNOWN] > 0)   /* folders, music, text, what could not be read: never picked by shape */
        chooser_word(c, 0.0, "    {}  ({})", PROVEN_ARG(shape_name(RUBRAVIEW_SHAPE_UNKNOWN)), PROVEN_ARG((int)total[RUBRAVIEW_SHAPE_UNKNOWN]));
    chooser_show(app, where, x, y, above_y);
}

static void list_ext_toggle(app_state_t *app, int32_t k) {
    if (k < 0) { app->list_ext_off_n = 0; list_view_changed(app); return; }
    proven_u8str_view_t e = cstr(app->list_ext_seen[k]);
    for (size_t i = 0; i < app->list_ext_off_n; ++i) {
        if (!proven_u8str_view_eq(cstr(app->list_ext_off[i]), e)) continue;
        proven_sys_mem_move(app->list_ext_off[i], app->list_ext_off[i + 1], (app->list_ext_off_n - i - 1) * sizeof(app->list_ext_off[0]));
        app->list_ext_off_n--;
        list_view_changed(app);
        return;
    }
    if (app->list_ext_off_n >= sizeof(app->list_ext_off) / sizeof(app->list_ext_off[0]) || e.size >= sizeof(app->list_ext_off[0])) return;
    proven_sys_mem_copy(app->list_ext_off[app->list_ext_off_n], e.ptr, e.size);
    app->list_ext_off[app->list_ext_off_n][e.size] = '\0';
    app->list_ext_off_n++;
    list_view_changed(app);
}

const char *list_sort_word(int key) {
    switch (key) {
        case 1: return T("Name");
        case 2: return T("Extension");
        case 3: return T("Size");
        case 4: return T("Modified");
        case 5: return T("Created");
        case 6: return T("Length");   /* RV-202: a film's or a song's */
        default: return T("Sort");
    }
}

void chooser_open_listsort(app_state_t *app, int where, double x, double y, double above_y) {
    chooser_t *c = &app->chooser;
    *c = (chooser_t){ .kind = CHOOSE_LISTSORT, .current = app->list_sort_key - 1 };
    rubraview_fmt(c->title, sizeof(c->title), "{}", PROVEN_ARG(T("Sort the list by")));
    for (int k = 1; k <= 6; ++k) chooser_word(c, (double)k, "{}", PROVEN_ARG(list_sort_word(k)));
    chooser_show(app, where, x, y, above_y);
}

/* The list put in order by `key`, rising or falling; the file on screen stays. */
static struct list_sort_key { proven_u8str_view_t name, ext; uint64_t size; int64_t mtime, ctime; double seconds; } *g_list_keys;
static int g_list_key, g_list_desc;
static int list_sort_cmp(const void *pa, const void *pb) {
    size_t a = *(const size_t*)pa, b = *(const size_t*)pb;
    const struct list_sort_key *x = &g_list_keys[a], *y = &g_list_keys[b];
    int r = 0;
    switch (g_list_key) {
        case 2: r = rubraview_str_lexcmp(x->ext, y->ext); break;
        case 3: r = x->size < y->size ? -1 : x->size > y->size ? 1 : 0; break;
        case 4: r = x->mtime < y->mtime ? -1 : x->mtime > y->mtime ? 1 : 0; break;
        case 5: r = x->ctime < y->ctime ? -1 : x->ctime > y->ctime ? 1 : 0; break;
        case 6: {   /* RV-202: a length not known (a picture, a song not read) after the known ones, either way */
            bool kx = x->seconds > 0.0, ky = y->seconds > 0.0;
            if (kx != ky) return kx ? -1 : 1;
            r = x->seconds < y->seconds ? -1 : x->seconds > y->seconds ? 1 : 0;
            break;
        }
        default: break;
    }
    if (r == 0) r = rubraview_str_natcmp(x->name, y->name);
    if (r == 0) r = a < b ? -1 : a > b ? 1 : 0;   /* stable */
    return g_list_desc ? -r : r;
}

void list_sort(app_state_t *app, int key, bool desc) {
    size_t count = page_count(app);
    if (count < 2 || !list_editable(app)) return;
    /* RV-202: by length, the films' and songs' lengths read first (on the size reader's thread); sorted when they are */
    if (key == 6 && facts_read_for_sort(app, SHAPE_FOR_LIST, FACTS_THEN_LIST_SORT, key, desc)) return;
    app->list_sort_key = key;
    app->list_sort_desc = desc;
    size_t *order = (size_t*)rubraview_malloc_product(count, sizeof(size_t), 1);
    struct list_sort_key *keys = (struct list_sort_key*)rubraview_malloc_product(count, sizeof(*keys), 1);
    if (!order || !keys) { rubraview_free(order); rubraview_free(keys); return; }
    rubraview_scratch_t sc;
    proven_arena_t *pa = scratch_begin(app, &sc, 4096u + count * 64u);
    for (size_t i = 0; i < count; ++i) {
        proven_u8str_view_t path = app->source.pages[i].path;
        keys[i] = (struct list_sort_key){ .name = rubraview_path_basename(path), .ext = rubraview_path_ext(path) };
        rubraview_fs_entry_t st;
        if (key >= 3 && key <= 5 && rubraview_pal_fs_stat(pa, path, &st)) { keys[i].size = st.size_bytes; keys[i].mtime = st.mtime; keys[i].ctime = st.ctime; }
        if (key == 6) (void)rubraview_shape_cache_length(&app->shape_cache, path, 0, &keys[i].seconds);
        order[i] = i;
    }
    scratch_end(app, &sc);
    g_list_keys = keys; g_list_key = key; g_list_desc = desc;
    qsort(order, count, sizeof(size_t), list_sort_cmp);
    g_list_keys = NULL;
    char line[96];
    rubraview_fmt(line, sizeof(line), T("the list by {}, {}"), PROVEN_ARG(list_sort_word(key)), PROVEN_ARG(desc ? T("falling") : T("rising")));
    list_apply(app, order, count, NULL, line);
    rubraview_free(order);
    rubraview_free(keys);
    app->files_dirty = true;
}

static void chooser_pick(app_state_t *app, int32_t index) {
    chooser_t *c = &app->chooser;
    if (!c->open || index < 0 || (size_t)index >= c->count) return;
    chooser_close(app);
    switch (c->kind) {
        case CHOOSE_TILE: chooser_pick_tile(app, (size_t)index); break;
        case CHOOSE_PANEL: {
            bool owned = (c->panel == 3 || c->panel == 4) && !owned_panel_active();   /* RV-194: its panel stands in for the change */
            if (owned) owned_panel_in(app, c->panel == 3 ? 1 : 2);
            rubraview_panel_t *panel = c->panel == 2 ? &app->sel_panel : &app->panel;
            if ((size_t)c->row < panel->row_count) {
                panel->rows[c->row].value = c->values[index];
                if (c->panel == 2) edit_sel_apply_row(app, c->row); else panel_apply_row(app, c->row);
            }
            if (owned) { owned_panel_out(app); if (c->panel == 4) batch_refresh(app); }
            app->edit_dirty = true;
            break;
        }
        case CHOOSE_SETTING:
            if (!c->def) break;
            rubraview_settings_set(&app->settings, c->def->section, c->def->key, c->values[index]);
            settings_event(app, RUBRAVIEW_SEVENT_CHANGED);
            app->settings_dirty = true;
            break;
        case CHOOSE_SORT: {
            bool desc = rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort_desc")) > 0.5;
            picker_sort_set(app, (int)c->values[index], desc);
            break;
        }
        case CHOOSE_ORDER: {
            int key = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort"))) - 1;
            picker_sort_set(app, key < 0 ? 0 : key, c->values[index] > 0.5);
            break;
        }
        case CHOOSE_LISTEXT: {   /* stays open: the next box is a click away */
            int where = c->where;
            rubraview_rect_t f = c->layout.frame;
            list_ext_toggle(app, (int32_t)lround(c->values[index]));
            c->open = true;
            chooser_open_listext(app, where, f.x, f.y, f.y);
            break;
        }
        case CHOOSE_LISTSORT: list_sort(app, (int)lround(c->values[index]), app->list_sort_desc); break;
        case CHOOSE_PICKEXT: {   /* RV-204: stays open, its counts new */
            rubraview_rect_t f = c->layout.frame;
            picker_ext_pick(app, (size_t)lround(c->values[index]));
            c->open = true;
            chooser_open_pickext(app, f.x, f.y, f.y);
            break;
        }
        case CHOOSE_BASKET: {   /* RV-227 */
            int value = (int)lround(c->values[index]);
            rubraview_rect_t f = c->layout.frame;
            if (value == -1) {   /* copy or move: the list stays open */
                app->basket_moving = !app->basket_moving;
                c->open = true;
                chooser_open_basket(app, BASKET_LIST_PUT_HERE, f.x, f.y, f.y);
            } else if (value <= -10) {   /* D-286: its files to see */
                int which = -10 - value;
                app->basket_pending_count = 0;   /* what waited for a basket is not put in */
                if (rubraview_basket_count(&app->baskets, which) == 0) { osd_say(app, TU8("that basket is empty")); break; }
                c->open = true;
                chooser_open_basket_files(app, which, f.x, f.y, f.y);
            } else if (value <= -2) {
                rubraview_basket_empty(&app->baskets, -2 - value);
                char line[64];
                rubraview_fmt(line, sizeof line, T("basket {} emptied"), PROVEN_ARG(-1 - value));
                osd_say(app, cstr(line));
            } else {
                basket_chosen(app, value);
            }
            break;
        }
        case CHOOSE_EDITMENU:   /* RV-293 */
            if (app->rename_active) editbox_menu_do(app->window, &app->rename_ec, (int)lround(c->values[index]), app->rename_is_password);
            break;
        case CHOOSE_DROP: {   /* RV-286 */
            int value = (int)lround(c->values[index]);
            if (value == 1 || value == 2)
                (void)filejob_start(app, app->drop_dir, app->drop_pending, app->drop_pending_count, value == 1, -1, false);
            app->drop_pending_count = 0;
            break;
        }
        case CHOOSE_BASKET_FILES: {   /* D-286: one taken out, the list stays open */
            int value = (int)lround(c->values[index]), which = c->panel;
            rubraview_rect_t f = c->layout.frame;
            if (value < 0) { c->open = true; break; }
            proven_u8str_view_t name = rubraview_path_basename(rubraview_basket_path(&app->baskets, which, (size_t)value));
            char line[160];
            rubraview_fmt(line, sizeof line, T("taken out of basket {}: {}"), PROVEN_ARG(which + 1),
                          PROVEN_ARG(rubraview_bytes_view(name.ptr, (int)(name.size < 100 ? name.size : 100))));
            if (!rubraview_basket_remove(&app->baskets, which, (size_t)value)) break;
            osd_say(app, cstr(line));
            if (rubraview_basket_count(&app->baskets, which) == 0) break;
            c->open = true;
            chooser_open_basket_files(app, which, f.x, f.y, f.y);
            break;
        }
        case CHOOSE_SHAPE: {   /* RV-192: stays open, its counts new */
            int32_t which = (int32_t)lround(c->values[index]);
            if (which == RUBRAVIEW_SHAPE_UNKNOWN) { c->open = true; break; }
            int where = c->where;
            rubraview_rect_t f = c->layout.frame;
            shape_pick(app, which);
            c->open = true;
            chooser_open_shape(app, where, f.x, f.y, f.y);
            break;
        }
        default: break;
    }
}

/* A press in `where`: the list's when it is open there (a row picks; away closes it). */
bool chooser_press(app_state_t *app, int where, double x, double y) {
    chooser_t *c = &app->chooser;
    if (!c->open) return false;
    if (c->where != where) { chooser_close(app); return false; }
    if (scrollbar_press(app, RVSB_CHOOSER, x, y)) { chooser_dirty(app); return true; }   /* RV-170 */
    int32_t row = rubraview_chooser_row_at(&c->layout, x, y);
    if (row >= 0) chooser_pick(app, row);
    else if (row == -2) chooser_close(app);
    return true;
}

/* RV-170: the list's scroll bar, inside its frame at the right, under its title row. */
bool chooser_sb(app_state_t *app, rubraview_scrollbar_t *sb, double bw, double dpi) {
    const chooser_t *c = &app->chooser;
    if (!c->open) return false;
    const rubraview_chooser_layout_t *l = &c->layout;
    *sb = (rubraview_scrollbar_t){ .area = { l->frame.x + l->frame.width - bw - 3.0 * dpi, l->frame.y + l->row_h + 2.0 * dpi,
                                             bw, l->frame.height - l->row_h - 4.0 * dpi },
                                   .button = bw, .total = (double)l->rows, .shown = (double)l->shown, .first = (double)l->first,
                                   .step = 1.0, .min_thumb = 16.0 * dpi };
    return true;
}

void chooser_sb_to(app_state_t *app, size_t first) {
    rubraview_chooser_layout_t *l = &app->chooser.layout;
    size_t last = l->rows > l->shown ? l->rows - l->shown : 0;
    l->first = first > last ? last : first;
    chooser_dirty(app);
}

rubraview_window_t *chooser_sb_window(app_state_t *app) {
    return app->chooser.open ? chooser_window(app, app->chooser.where) : NULL;
}

bool chooser_key(app_state_t *app, int where, rubraview_key_combo_t combo) {
    chooser_t *c = &app->chooser;
    if (!c->open || c->where != where) return false;
    if (key_is(combo, "Escape")) { chooser_close(app); return true; }
    if (key_is(combo, "Enter")) { chooser_pick(app, c->focus); return true; }
    if (key_is(combo, "Down") || key_is(combo, "Up")) {
        c->focus = rubraview_chooser_step(&c->layout, c->focus, key_is(combo, "Up") ? -1 : 1);
        if (c->focus >= 0 && (size_t)c->focus < c->layout.first) c->layout.first = (size_t)c->focus;
        if (c->focus >= 0 && (size_t)c->focus >= c->layout.first + c->layout.shown) c->layout.first = (size_t)c->focus + 1 - c->layout.shown;
        chooser_dirty(app);
        return true;
    }
    return true;   /* while it is open the other keys wait */
}

bool chooser_wheel(app_state_t *app, int where, double notches) {
    chooser_t *c = &app->chooser;
    if (!c->open || c->where != where) return false;
    rubraview_chooser_scroll(&c->layout, notches > 0.0 ? -1 : 1);
    chooser_dirty(app);
    return true;
}

void draw_chooser(app_state_t *app, rubraview_renderer_t *r, int where, double px, double py) {
    chooser_t *c = &app->chooser;
    if (!c->open || c->where != where || !r) return;
    const rubraview_chooser_layout_t *l = &c->layout;
    rubraview_pal_rect_t f = { l->frame.x, l->frame.y, l->frame.width, l->frame.height };
    int32_t hover = rubraview_chooser_row_at(l, px, py);
    if (hover < 0) hover = c->focus;
    if (where == CHOOSE_IN_SETTINGS) {   /* text, as the settings window writes it */
        double fs = app->settings_font, ch = app->settings_cell_h, cw = app->settings_cell_w;
        rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ f.x - 2.0, f.y - 2.0, f.width + 4.0, f.height + 4.0 }, SETTINGS_ACCENT, 0.0);
        rubraview_pal_render_fill_rect(r, f, SETTINGS_PANE, 0.0);
        rubraview_pal_render_draw_text_mono(r, cstr(c->title), f.x + cw, f.y, fs, SETTINGS_ACCENT);
        for (size_t i = l->first; i < l->first + l->shown && i < c->count; ++i) {
            rubraview_rect_t row = rubraview_chooser_row_rect(l, i);
            if ((int32_t)i == hover) rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ row.x, row.y, row.width, ch }, SETTINGS_FOCUS, 0.0);
            char line[112];
            rubraview_fmt(line, sizeof(line), "{} {}", PROVEN_ARG((int32_t)i == c->current ? "\xE2\x97\x8F" : " "), PROVEN_ARG(c->words[i]));
            rubraview_pal_render_draw_text_mono(r, cstr(line), row.x + cw, row.y, fs, SETTINGS_TEXT);
        }
        draw_scrollbar(app, r, RVSB_CHOOSER, px, py);   /* RV-170 */
        return;
    }
    double dpi = rubraview_pal_window_dpi_scale(chooser_window(app, where));
    rubraview_pal_render_fill_rect(r, f, 0xF6202020u, 6.0 * dpi);
    rubraview_pal_render_stroke_rect(r, f, COLOR_TILE_CURRENT, 1.5 * dpi, 6.0 * dpi);
    rubraview_pal_render_draw_text(r, cstr(c->title), (rubraview_pal_rect_t){ f.x + 14.0 * dpi, f.y, f.width - 28.0 * dpi, l->row_h },
                                   15.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
    for (size_t i = l->first; i < l->first + l->shown && i < c->count; ++i) {
        rubraview_rect_t row = rubraview_chooser_row_rect(l, i);
        rubraview_pal_rect_t cell = { row.x + 5.0 * dpi, row.y + 3.0 * dpi, row.width - 10.0 * dpi, row.height - 6.0 * dpi };
        if ((int32_t)i == c->current) rubraview_pal_render_fill_rect(r, cell, COLOR_TILE_CURRENT, 4.0 * dpi);
        else if ((int32_t)i == hover) rubraview_pal_render_fill_rect(r, cell, 0x40FFFFFFu, 4.0 * dpi);
        rubraview_pal_rect_t words = { cell.x + 12.0 * dpi, cell.y, cell.width - 20.0 * dpi, cell.height };
        rubraview_pal_render_draw_text(r, cstr(c->words[i]), words, 16.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    }
    draw_scrollbar(app, r, RVSB_CHOOSER, px, py);   /* more above or below: where, and RV-170 it moves them */
}
