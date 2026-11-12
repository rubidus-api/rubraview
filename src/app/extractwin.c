/* RV-272 (owner, 2026-11-12: "압축파일을 루브라뷰로 바로 풀어버릴 때, 메인 창 없이 카드만 보이게(카드는 외부 창으로 독립시키는게
   맞을 듯 합니다) 하고 압축 푸는 것이 끝나면 (창에 바로 종료 체크박스가 있으며, 기존 설정을 기억하고, 설정창에서도 옵션 설정
   가능) 옵션에 따라 바로 꺼지던가 아니면 미리 지정한 동작(바로 끄기, 카드 상태로 대기하기, 압축 푼 폴더 열기)을 수행"; asked:
   stay by default, the folder in Explorer or in Rubraview by choice, its own window when a viewer is open too):
   Explorer's Extract, Extract here and Extract to... run in a small window of their own, the progress card's rows,
   Stop and a "Close when done" box in it. A launch for them alone makes the viewer's window without showing it, and
   ends when this window closes. A locked archive's password is asked in it; names already there go to the clashes
   window. What follows is `archive.after_extract` (`rubraview_extract_after`); a failure or a stop stays. */
#include "app.h"
#include "rubraview/archive_job.h"

void archwork_password_typed(app_state_t *app, proven_u8str_view_t password);   /* archwork.c */

#define XW_W 560
#define XW_H 262
#define XW_QUEUE 8

typedef struct { rubraview_shell_verb_t verb; char **paths; size_t count; } xw_req_t;
static xw_req_t g_queue[XW_QUEUE];
static size_t g_queued;

bool shell_verb_extracts(rubraview_shell_verb_t verb) {
    return verb == RUBRAVIEW_SHELL_VERB_EXTRACT || verb == RUBRAVIEW_SHELL_VERB_EXTRACT_HERE || verb == RUBRAVIEW_SHELL_VERB_EXTRACT_TO;
}

void main_window_show(app_state_t *app) {
    if (!app->main_hidden) return;
    app->main_hidden = false;
    rubraview_pal_window_set_visible(app->window, true);
    app_log(app, "window", PROVEN_LIT("the viewer's window shown"));
}

static rubraview_extract_after_t after_setting(const app_state_t *app, const char *key) {
    bool kept = key[proven_cstr_len("after_extract")] != '\0';   /* "after_extract_open" */
    return (rubraview_extract_after_t)lround(kept ? rubraview_settings_get(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("after_extract_open"))
                                                  : rubraview_settings_get(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("after_extract")));
}

static bool xw_make(app_state_t *app) {
    if (app->xw_open) return true;
    rubraview_window_config_t config = { .title = T("Rubraview - extracting"), .width = XW_W, .height = XW_H, .stands_alone = true };
    app->xw_window = rubraview_pal_window_create(window_block_take(app, &app->xw_block), &config);
    if (!app->xw_window) return false;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->xw_window, &w, &h);
    app->xw_renderer = rubraview_pal_render_create(window_block_arena(app, &app->xw_block), rubraview_pal_window_native_handle(app->xw_window), w, h);
    ui_font_apply_all(app);
    if (!app->xw_renderer) {
        rubraview_pal_window_destroy(app->xw_window);
        window_block_give(app, &app->xw_block);
        app->xw_window = NULL;
        return false;
    }
    app->xw_open = true;
    app->xw_dirty = true;
    return true;
}

static void xw_wipe_password(app_state_t *app) {
    secure_wipe(rubraview_mem_mut(app->xw_pw, sizeof app->xw_pw));
    secure_wipe(rubraview_mem_mut(app->xw_composing, sizeof app->xw_composing));
    app->xw_pw_len = app->xw_composing_len = 0;
    app->xw_pw_shown = false;
}

/* The window goes; with the viewer's window never shown, so does this launch. */
static void xw_close(app_state_t *app) {
    if (app->xw_open) {
        if (app->xw_renderer) rubraview_pal_render_destroy(app->xw_renderer);
        if (app->xw_window) rubraview_pal_window_destroy(app->xw_window);
        window_block_give(app, &app->xw_block);
    }
    app->xw_renderer = NULL;
    app->xw_window = NULL;
    app->xw_open = false;
    app->xw_close_after = false;
    xw_wipe_password(app);
    settings_write_if_changed(app, false);   /* the box's choice, kept for the next */
    extractwin_check_end(app);
}

/* The viewer ends: the window goes with it, nothing else asked. */
void extractwin_window_close(app_state_t *app) {
    if (!app->xw_open) return;
    if (app->xw_renderer) rubraview_pal_render_destroy(app->xw_renderer);
    if (app->xw_window) rubraview_pal_window_destroy(app->xw_window);
    window_block_give(app, &app->xw_block);
    app->xw_renderer = NULL;
    app->xw_window = NULL;
    app->xw_open = false;
    xw_wipe_password(app);
}

void extractwin_check_end(app_state_t *app) {
    if (app->main_hidden && !app->xw_open && !archwork_busy(app) && g_queued == 0) {
        app_log(app, "window", PROVEN_LIT("the extraction's window closed: this launch ends"));
        rubraview_pal_window_request_close(app->window);
    }
}

static void free_req(xw_req_t *r) {
    for (size_t i = 0; r->paths && i < r->count; ++i) rubraview_free(r->paths[i]);
    rubraview_free(r->paths);
    r->paths = NULL;
    r->count = 0;
}

static void start(app_state_t *app, rubraview_shell_verb_t verb, const proven_u8str_view_t *paths, size_t count) {
    if (!xw_make(app)) {   /* no window of its own: the viewer's card, as before */
        main_window_show(app);
        archwork_extract_paths(app, paths, count, verb == RUBRAVIEW_SHELL_VERB_EXTRACT_HERE ? RUBRAVIEW_EXTRACT_SMART : RUBRAVIEW_EXTRACT_FOLDER,
                               verb == RUBRAVIEW_SHELL_VERB_EXTRACT_TO);
        return;
    }
    app->xw_state = XW_WORKING;
    app->xw_ok = false;
    app->xw_line[0] = app->xw_path[0] = '\0';
    rubraview_fmt(app->xw_title, sizeof app->xw_title, "{}", PROVEN_ARG(T("Extracting")));
    app->xw_dirty = true;
    proven_u8str_view_t dest = PROVEN_LIT("");
    if (verb == RUBRAVIEW_SHELL_VERB_EXTRACT_TO) {   /* Windows' own folder picker, over this window */
        rubraview_file_dialog_opts_t opts = { .title = T("Extract into which folder"), .folder_mode = true,
                                              .parent_window_handle = rubraview_pal_window_native_handle(app->xw_window) };
        rubraview_file_dialog_result_t picked = rubraview_pal_file_dialog_pick_folder(app->arena, &opts);
        if (!picked.accepted || picked.count == 0) {
            extractwin_finished(app, false, false, TU8("no folder was chosen"), PROVEN_LIT(""));
            return;
        }
        dest = picked.paths[0];
    }
    archwork_extract_windowed(app, paths, count, verb == RUBRAVIEW_SHELL_VERB_EXTRACT_HERE ? RUBRAVIEW_EXTRACT_SMART : RUBRAVIEW_EXTRACT_FOLDER, dest);
}

/* Explorer's Extract: now, or after the one under way (in the order asked). */
void extractwin_request(app_state_t *app, rubraview_shell_verb_t verb, const proven_u8str_view_t *paths, size_t count) {
    bool busy = archwork_busy(app) || archwork_windowed(app) || (app->xw_open && app->xw_state != XW_DONE);
    if (!busy) { start(app, verb, paths, count); return; }
    if (g_queued >= XW_QUEUE) { osd_say(app, TU8("too much is waiting: that request was dropped")); return; }
    xw_req_t *r = &g_queue[g_queued];
    r->verb = verb;
    r->paths = rubraview_alloc_zero(count ? count : 1, sizeof *r->paths);
    if (!r->paths) return;
    for (size_t i = 0; i < count; ++i) {
        r->paths[i] = rubraview_alloc(paths[i].size + 1);
        if (!r->paths[i]) { free_req(r); return; }
        proven_sys_mem_copy(r->paths[i], paths[i].ptr, paths[i].size);
        r->paths[i][paths[i].size] = '\0';
        r->count = i + 1;
    }
    g_queued++;
    app_log(app, "extract", PROVEN_LIT("waits for the one under way"));
}

static bool start_next(app_state_t *app) {
    if (g_queued == 0 || archwork_busy(app)) return false;
    xw_req_t r = g_queue[0];
    for (size_t i = 1; i < g_queued; ++i) g_queue[i - 1] = g_queue[i];
    g_queued--;
    proven_u8str_view_t views[64] = {0};
    size_t n = r.count < 64 ? r.count : 64;
    for (size_t i = 0; i < n; ++i) views[i] = proven_u8str_view_from_cstr(r.paths[i]);
    start(app, r.verb, views, n);
    free_req(&r);
    return true;
}

void extractwin_ask_password(app_state_t *app, proven_u8str_view_t archive, bool wrong) {
    if (!xw_make(app)) return;
    xw_wipe_password(app);
    proven_u8str_view_t name = rubraview_path_basename(archive);
    rubraview_fmt(app->xw_title, sizeof app->xw_title, wrong ? T("Wrong password for {}") : T("{} is locked"),
                  PROVEN_ARG(rubraview_bytes_view((const char *)name.ptr, (int)name.size)));
    app->xw_state = XW_PASSWORD;
    rubraview_pal_window_text_input(app->xw_window, true);
    rubraview_pal_window_text_ime(app->xw_window, false);   /* D-262: hidden, plain letters; the eye brings the IME */
    rubraview_pal_window_set_visible(app->xw_window, true);
    app->xw_dirty = true;
}

static void password_answer(app_state_t *app, bool given) {
    rubraview_pal_window_text_input(app->xw_window, false);
    app->xw_state = XW_WORKING;
    rubraview_fmt(app->xw_title, sizeof app->xw_title, "{}", PROVEN_ARG(T("Extracting")));
    app->xw_dirty = true;
    if (given && app->xw_pw_len > 0) {
        archwork_password_typed(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->xw_pw, .size = app->xw_pw_len });
        xw_wipe_password(app);
        return;
    }
    xw_wipe_password(app);
    archwork_password_typed(app, PROVEN_LIT(""));   /* let go */
    extractwin_finished(app, false, false, TU8("not extracted: it needs its password"), PROVEN_LIT(""));
}

static void open_in_viewer(app_state_t *app, proven_u8str_view_t path) {
    main_window_show(app);
    if (picker_in_archive(app)) picker_leave_archive(app);
    if (!app->picker_open) handle_action(app, PROVEN_LIT("open_picker"));
    picker_navigate(app, path);
    app->picker.focus = 0;
}

void extractwin_finished(app_state_t *app, bool succeeded, bool names_asked, proven_u8str_view_t line, proven_u8str_view_t root) {
    app_log(app, "extract", line);
    rubraview_fmt(app->xw_line, sizeof app->xw_line, "{}", PROVEN_ARG(rubraview_bytes_view((const char *)line.ptr, (int)line.size)));
    rubraview_fmt(app->xw_path, sizeof app->xw_path, "{}", PROVEN_ARG(rubraview_bytes_view((const char *)root.ptr, (int)root.size)));
    app->xw_ok = succeeded;
    if (app->xw_close_after) { xw_close(app); if (!start_next(app)) extractwin_check_end(app); return; }   /* closed while it worked */
    if (start_next(app)) return;   /* more asked meanwhile: the last one's result decides */
    proven_u8str_view_t path = app_keep(app, cstr(app->xw_path));
    rubraview_extract_after_t after = rubraview_extract_after(after_setting(app, "after_extract"), succeeded && path.size > 0, names_asked);
    switch (after) {
        case RUBRAVIEW_AFTER_CLOSE:
            xw_close(app);
            return;
        case RUBRAVIEW_AFTER_EXPLORER:
            if (!rubraview_pal_shell_open_folder(path, false)) break;   /* it could not: the card stays to say so */
            xw_close(app);
            return;
        case RUBRAVIEW_AFTER_RUBRAVIEW:
            open_in_viewer(app, path);
            xw_close(app);
            return;
        case RUBRAVIEW_AFTER_STAY:
        default:
            break;
    }
    if (!app->xw_open && !xw_make(app)) { extractwin_check_end(app); return; }
    app->xw_state = XW_DONE;
    rubraview_fmt(app->xw_title, sizeof app->xw_title, "{}", PROVEN_ARG(succeeded ? T("Extracted") : app->xw_path[0] ? T("Extracted, not all of it") : T("Not extracted")));
    rubraview_pal_window_set_visible(app->xw_window, true);
    app->xw_dirty = true;
}

/* ---- the box ---- */

static bool box_ticked(const app_state_t *app) { return after_setting(app, "after_extract") == RUBRAVIEW_AFTER_CLOSE; }

static void box_toggle(app_state_t *app) {
    rubraview_extract_after_t now = after_setting(app, "after_extract");
    bool tick = now != RUBRAVIEW_AFTER_CLOSE;
    if (tick) rubraview_settings_set(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("after_extract_open"), (double)now);
    rubraview_extract_after_t next = rubraview_extract_after_box(now, tick, after_setting(app, "after_extract_open"));
    rubraview_settings_set(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("after_extract"), (double)next);
    settings_write_if_changed(app, false);
    app->settings_dirty = true;
    app->xw_dirty = true;
}

/* ---- drawing ---- */

static void draw_button(app_state_t *app, rubraview_pal_rect_t b, const char *word, bool first, double dpi) {
    bool over = pal_rect_has(b, app->xw_px, app->xw_py);
    rubraview_pal_render_fill_rect(app->renderer, b, first ? COLOR_TILE_CURRENT : over ? 0x50FFFFFFu : COLOR_TILE_FILL, 5.0 * dpi);
    if (first && over) rubraview_pal_render_fill_rect(app->renderer, b, 0x30FFFFFFu, 5.0 * dpi);
    if (!first) rubraview_pal_render_stroke_rect(app->renderer, b, COLOR_BOX_BORDER, 1.0, 5.0 * dpi);
    rubraview_pal_render_draw_text(app->renderer, cstr(word), b, 14.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
}

/* Buttons right-aligned on the foot; their rectangles to `out`. */
static void draw_buttons(app_state_t *app, double w, double h, const char *const *words, int n, rubraview_pal_rect_t *out, double dpi) {
    double pad = 20.0 * dpi, bh = 38.0 * dpi, gap = 10.0 * dpi, bw[3], total = 0.0;
    for (int i = 0; i < n; ++i) {
        double tw = 0.0;
        (void)rubraview_pal_render_measure_text(app->renderer, cstr(words[i]), 14.0 * dpi, &tw);
        bw[i] = fmax(96.0 * dpi, tw + 32.0 * dpi);
        total += bw[i] + (i ? gap : 0.0);
    }
    double x = w - pad - total, y = h - pad - bh;
    for (int i = 0; i < n; ++i) {
        out[i] = (rubraview_pal_rect_t){ x, y, bw[i], bh };
        draw_button(app, out[i], words[i], i == 0, dpi);
        x += bw[i] + gap;
    }
}

/* The box at the foot's left, or, `above` the buttons, on the row over them. */
static void draw_done_box(app_state_t *app, double h, double dpi, bool above) {
    double pad = 20.0 * dpi, side = 18.0 * dpi, y = h - pad - 38.0 * dpi + (38.0 * dpi - side) * 0.5 - (above ? 44.0 * dpi : 0.0);
    const char *word = T("Close when done");
    double tw = 0.0;
    (void)rubraview_pal_render_measure_text(app->renderer, cstr(word), 14.0 * dpi, &tw);
    rubraview_pal_rect_t sq = { pad, y, side, side };
    app->xw_box = (rubraview_pal_rect_t){ pad - 4.0 * dpi, y - 8.0 * dpi, side + 16.0 * dpi + tw, side + 16.0 * dpi };
    bool on = box_ticked(app);
    rubraview_pal_render_fill_rect(app->renderer, sq, on ? COLOR_TILE_CURRENT : 0xFF141414u, 3.0 * dpi);
    rubraview_pal_render_stroke_rect(app->renderer, sq, pal_rect_has(app->xw_box, app->xw_px, app->xw_py) ? COLOR_TEXT : COLOR_BOX_BORDER, 1.0 * dpi, 3.0 * dpi);
    if (on) rubraview_pal_render_draw_text(app->renderer, PROVEN_LIT("\xE2\x9C\x93"), sq, 14.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    rubraview_pal_render_draw_text(app->renderer, cstr(word), (rubraview_pal_rect_t){ pad + side + 8.0 * dpi, y - 2.0 * dpi, tw + 8.0 * dpi, side + 4.0 * dpi },
                                   14.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
}

void draw_extract_window(app_state_t *app) {
    if (!app->xw_open) return;
    double now = rubraview_pal_time_now_seconds();
    if (app->xw_state == XW_WORKING && now - app->xw_drawn_at >= 0.25) app->xw_dirty = true;   /* the bar moves */
    if (!app->xw_dirty) return;
    app->xw_dirty = false;
    app->xw_drawn_at = now;
    int32_t iw = 0, ih = 0;
    rubraview_pal_window_get_size(app->xw_window, &iw, &ih);
    double w = (double)iw, h = (double)ih, dpi = rubraview_pal_window_dpi_scale(app->xw_window), pad = 20.0 * dpi;
    rubraview_renderer_t *main_renderer = app->renderer;
    app->renderer = app->xw_renderer;   /* the cards' drawing helpers draw with app->renderer */
    rubraview_pal_render_begin(app->renderer, 0xFF242424u);
    rubraview_pal_rect_t c = { 0.0, 0.0, w, h };
    rubraview_pal_rect_t tr = { pad, pad - 4.0 * dpi, w - 2.0 * pad, 30.0 * dpi };
    bool working = app->xw_state == XW_WORKING && app->prog.source == PROGRESS_ARCHWORK && app->prog.windowed;
    const char *title = working ? app->prog.title : app->xw_title;
    rubraview_pal_render_draw_text(app->renderer, cstr(title), tr, 18.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    rubraview_pal_render_draw_text(app->renderer, cstr(title), (rubraview_pal_rect_t){ tr.x + 0.6 * dpi, tr.y, tr.width, tr.height }, 18.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    double y = tr.y + 34.0 * dpi;
    app->xw_btn_n = 0;
    app->xw_stop = app->xw_eye = app->xw_box = (rubraview_pal_rect_t){0};   /* only what is drawn now answers */
    if (app->xw_state == XW_PASSWORD) {
        const char *hint = app->xw_pw_shown ? T("Type its password, then Enter (shown: any letter)")
                                            : T("Type its password, then Enter (English letters, digits and symbols)");
        rubraview_pal_render_draw_text(app->renderer, cstr(hint), (rubraview_pal_rect_t){ pad, y, w - 2.0 * pad, 22.0 * dpi }, 13.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
        y += 30.0 * dpi;
        double eye_w = 64.0 * dpi;
        rubraview_pal_rect_t field = { pad, y, w - 2.0 * pad - eye_w - 8.0 * dpi, 36.0 * dpi };
        rubraview_pal_render_fill_rect(app->renderer, field, 0xFF141414u, 4.0 * dpi);
        rubraview_pal_render_stroke_rect(app->renderer, field, COLOR_TILE_CURRENT, 1.5 * dpi, 4.0 * dpi);
        char shown[600];
        size_t n = 0;
        if (app->xw_pw_shown) {
            n = (size_t)rubraview_fmt(shown, sizeof shown, "{}{}", PROVEN_ARG(rubraview_bytes_view(app->xw_pw, (int)app->xw_pw_len)),
                                      PROVEN_ARG(rubraview_bytes_view(app->xw_composing, (int)app->xw_composing_len)));
        } else {
            for (size_t i = 0; i < app->xw_pw_len && n + 3 < sizeof shown; ++i) { shown[n++] = (char)0xE2; shown[n++] = (char)0x97; shown[n++] = (char)0x8F; }
        }
        if (n + 2 < sizeof shown) { shown[n++] = '|'; shown[n] = '\0'; }
        rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ (const proven_byte_t *)shown, n },
                                       (rubraview_pal_rect_t){ field.x + 10.0 * dpi, field.y, field.width - 20.0 * dpi, field.height }, 15.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
        secure_wipe(rubraview_mem_mut(shown, sizeof shown));
        app->xw_eye = (rubraview_pal_rect_t){ field.x + field.width + 8.0 * dpi, y, eye_w, field.height };
        draw_button(app, app->xw_eye, app->xw_pw_shown ? T("Hide") : T("Show"), false, dpi);
        const char *words[2] = { T("OK"), T("Cancel") };
        draw_buttons(app, w, h, words, 2, app->xw_btn, dpi);
        app->xw_btn_n = 2;
    } else if (working) {
        const char *note = app->prog.stopping ? T("stopping...") : T("Esc or Stop stops it");
        rubraview_pal_render_draw_text(app->renderer, cstr(note), (rubraview_pal_rect_t){ pad, y - 4.0 * dpi, w - 2.0 * pad, 22.0 * dpi }, 13.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
        progress_rows(app, c, y + 22.0 * dpi, dpi);
        if (!app->prog.stopping) {
            const char *words[1] = { T("Stop") };
            draw_buttons(app, w, h, words, 1, &app->xw_stop, dpi);
        }
        draw_done_box(app, h, dpi, false);
    } else if (app->xw_state == XW_WORKING) {
        rubraview_pal_render_draw_text(app->renderer, cstr(T("Starting...")), (rubraview_pal_rect_t){ pad, y, w - 2.0 * pad, 22.0 * dpi }, 14.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
        draw_done_box(app, h, dpi, false);
    } else {   /* done */
        rubraview_pal_render_draw_text(app->renderer, cstr(app->xw_line), (rubraview_pal_rect_t){ pad, y, w - 2.0 * pad, 44.0 * dpi }, 14.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
        if (app->xw_path[0]) {
            char fit[1200];
            proven_u8str_view_t path = files_fit_text(fit, sizeof fit, cstr(app->xw_path), w - 2.0 * pad, 13.0 * dpi);
            rubraview_pal_render_draw_text(app->renderer, path, (rubraview_pal_rect_t){ pad, y + 48.0 * dpi, w - 2.0 * pad, 22.0 * dpi }, 13.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
            const char *words[3] = { T("Open it here"), T("Show in Explorer"), T("Close") };
            draw_buttons(app, w, h, words, 3, app->xw_btn, dpi);
            app->xw_btn_n = 3;
        } else {
            const char *words[1] = { T("Close") };
            draw_buttons(app, w, h, words, 1, app->xw_btn, dpi);
            app->xw_btn_n = 1;
        }
        draw_done_box(app, h, dpi, app->xw_btn_n == 3);   /* over three buttons, beside one */
    }
    if (!rubraview_pal_render_end(app->renderer)) app->xw_dirty = true;
    app->renderer = main_renderer;
}

/* ---- events ---- */

static void done_button(app_state_t *app, int i) {
    proven_u8str_view_t path = app_keep(app, cstr(app->xw_path));
    if (app->xw_btn_n == 3 && i == 0) { open_in_viewer(app, path); xw_close(app); return; }
    if (app->xw_btn_n == 3 && i == 1) {
        if (!rubraview_pal_shell_open_folder(path, false)) { rubraview_fmt(app->xw_line, sizeof app->xw_line, "{}", PROVEN_ARG(T("Explorer could not show the folder"))); app->xw_dirty = true; return; }
        xw_close(app);
        return;
    }
    xw_close(app);
}

static void password_key(app_state_t *app, rubraview_key_combo_t k) {
    if (key_is(k, "Enter")) { password_answer(app, true); return; }
    if (key_is(k, "Escape")) { password_answer(app, false); return; }
    if (key_is(k, "Backspace") && app->xw_pw_len > 0) {
        size_t n = app->xw_pw_len - 1;
        while (n > 0 && ((unsigned char)app->xw_pw[n] & 0xC0u) == 0x80u) n--;   /* a whole letter */
        secure_wipe(rubraview_mem_mut(app->xw_pw + n, app->xw_pw_len - n));
        app->xw_pw_len = n;
        app->xw_dirty = true;
    }
}

static void password_text(app_state_t *app, proven_u8str_view_t text) {
    char kept[256];
    bool dropped = false;
    size_t n = app->xw_pw_shown ? (text.size < sizeof kept ? text.size : sizeof kept - 1) : rubraview_password_keep_plain(text, kept, sizeof kept, &dropped);
    if (app->xw_pw_shown) proven_sys_mem_copy(kept, text.ptr, n);
    for (size_t i = 0; i < n; ++i) if ((unsigned char)kept[i] < 0x20u) { n = i; break; }   /* no control characters */
    if (app->xw_pw_len + n < sizeof app->xw_pw) {
        proven_sys_mem_copy(app->xw_pw + app->xw_pw_len, kept, n);
        app->xw_pw_len += n;
    }
    secure_wipe(rubraview_mem_mut(kept, sizeof kept));
    app->xw_composing_len = 0;
    app->xw_dirty = true;
}

size_t extractwin_pump(app_state_t *app) {
    size_t handled = 0;
    rubraview_window_event_t event;
    while (app->xw_open && rubraview_pal_window_poll_event(app->xw_window, &event)) {
        handled++;
        switch (event.kind) {
            case RUBRAVIEW_WINDOW_EVENT_CLOSE:
                if (app->xw_state == XW_PASSWORD) { password_answer(app, false); if (app->xw_open) xw_close(app); return handled; }
                if (archwork_busy(app) && archwork_windowed(app)) {   /* closed while it works: stopped, then gone */
                    app->xw_close_after = true;
                    (void)archwork_stop(app);
                    rubraview_pal_window_set_visible(app->xw_window, false);
                    return handled;
                }
                xw_close(app);
                return handled;
            case RUBRAVIEW_WINDOW_EVENT_RESIZE:
                rubraview_pal_render_resize(app->xw_renderer, event.resize.width, event.resize.height);
                app->xw_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_PAINT:
                app->xw_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE:
                app->xw_px = event.mouse.x;
                app->xw_py = event.mouse.y;
                app->xw_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN: {
                if (event.mouse.button != RUBRAVIEW_MOUSE_LEFT) break;
                double x = event.mouse.x, y = event.mouse.y;
                app->xw_dirty = true;
                if (app->xw_state == XW_PASSWORD) {
                    if (pal_rect_has(app->xw_eye, x, y)) {
                        app->xw_pw_shown = !app->xw_pw_shown;
                        rubraview_pal_window_text_ime(app->xw_window, app->xw_pw_shown);
                    } else if (app->xw_btn_n == 2 && pal_rect_has(app->xw_btn[0], x, y)) {
                        password_answer(app, true);
                    } else if (app->xw_btn_n == 2 && pal_rect_has(app->xw_btn[1], x, y)) {
                        password_answer(app, false);
                    }
                    if (!app->xw_open) return handled;
                    break;
                }
                if (pal_rect_has(app->xw_stop, x, y) && archwork_busy(app)) { (void)archwork_stop(app); break; }
                if (pal_rect_has(app->xw_box, x, y) && app->xw_box.width > 0.0) { box_toggle(app); break; }
                if (app->xw_state == XW_DONE) {
                    for (int i = 0; i < app->xw_btn_n; ++i) if (pal_rect_has(app->xw_btn[i], x, y)) { done_button(app, i); return handled; }
                }
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_TEXT:
                if (app->xw_state == XW_PASSWORD) password_text(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)event.text.utf8, .size = event.text.length });
                break;
            case RUBRAVIEW_WINDOW_EVENT_COMPOSITION:
                if (app->xw_state == XW_PASSWORD && app->xw_pw_shown) {
                    size_t n = event.text.length < sizeof app->xw_composing ? event.text.length : sizeof app->xw_composing - 1;
                    proven_sys_mem_copy(app->xw_composing, event.text.utf8, n);
                    app->xw_composing_len = n;
                    app->xw_dirty = true;
                }
                break;
            case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN: {
                rubraview_key_combo_t k = event.key.combo;
                if (app->xw_state == XW_PASSWORD) { password_key(app, k); if (!app->xw_open) return handled; break; }
                if (key_is(k, "Escape")) {
                    if (archwork_busy(app)) (void)archwork_stop(app);
                    else { xw_close(app); return handled; }
                } else if (key_is(k, "Enter") && app->xw_state == XW_DONE && app->xw_btn_n > 0) {
                    done_button(app, 0);
                    return handled;
                }
                app->xw_dirty = true;
                break;
            }
            default:
                break;
        }
    }
    return handled;
}
