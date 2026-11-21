/*
 * RV-281 (owner, 2026-11-14: "Alt+Enter가 파일의 속성 보기 메뉴입니다 ... Rubraview 자체 구현 속성창(일반, 디지털서명, 보안,
 * 자세히, 이전버젼 ...)이 뜨고, 그 안에 윈도 기본 속성창 보기 버튼을 달아서 윈도 기본 속성창으로도 볼 수 있게"):
 * Rubraview's own properties window: General and Details here; Digital Signatures and Security in propsec.c (phase 2);
 * Windows' own window a button away.
 *
 * General is read in the viewer through Windows' system calls (pal_fileprops.h). Details is read and written by the
 * child (`--shell-menu-host props-read|props-write`, owner: "탐색기 메뉴처럼 별도 프로세스로 읽게"): Windows' property
 * handlers are other programs' code too. The window asks and goes on; the answer is looked for each pass.
 */
#include "app.h"
#include "rubraview/pal/pal_fileprops.h"

#include "propsview.h"

props_view_t *V(app_state_t *app) { return (props_view_t *)app->props; }

static bool tab_shown(const props_view_t *v, int32_t tab) {
    if (tab == RUBRAVIEW_PROPS_GENERAL || tab == RUBRAVIEW_PROPS_DETAILS) return true;
    if (tab == RUBRAVIEW_PROPS_SIGNATURES) return propsec_sig_tab(v);   /* signed files only, as Windows */
    if (tab == RUBRAVIEW_PROPS_SECURITY) return v->count == 1;          /* several files: Windows has none either */
    return propsver_tab(v);   /* Previous Versions: phase 3 (propsver.c) */
}

static void props_free(props_view_t *v) {
    if (!v) return;
    for (size_t i = 0; i < v->count; ++i) rubraview_free(v->paths[i]);
    rubraview_free(v->paths);
    rubraview_free(v->files);
    rubraview_free(v->rows);
    propsec_free(v);
    if (v->details == ASK_WAITING) rubraview_pal_shellhost_forget(v->read_ticket);
    if (v->writing) rubraview_pal_shellhost_forget(v->write_ticket);
    rubraview_free(v);
}

void props_close(app_state_t *app) {
    if (!app->props_open) return;
    rubraview_pal_window_get_frame(app->props_window, &app->props_frame[0], &app->props_frame[1], &app->props_frame[2], &app->props_frame[3]);
    if (app->props_renderer) rubraview_pal_render_destroy(app->props_renderer);
    if (app->props_window) rubraview_pal_window_destroy(app->props_window);
    window_block_give(app, &app->props_block);
    app->props_renderer = NULL;
    app->props_window = NULL;
    app->props_open = false;
    props_free(V(app));
    app->props = NULL;
}

void pw_say(props_view_t *v, const char *text) { (void)rubraview_fmt(v->message, sizeof v->message, "{}", PROVEN_ARG(text)); }

/* General: every file read again, the boxes set from what they are now. */
static void read_general(app_state_t *app) {
    props_view_t *v = V(app);
    size_t kept = 0;
    for (size_t i = 0; i < v->count; ++i)
        if (rubraview_pal_fileprops_read(v->paths[i], &v->files[kept], 1500)) kept++;
    rubraview_fileprops_sum(v->files, kept, &v->sum);
    for (uint32_t b = 0; b < 6; ++b) v->want[b] = rubraview_fileprops_attr_state(v->files, kept, 1u << b);
    v->last_on = 0;
    v->unblock = false;
    if (kept < v->count) pw_say(v, T("some of the files are no longer there"));
    app->props_dirty = true;
}

static void ask_details(app_state_t *app) {
    props_view_t *v = V(app);
    v->row_count = 0;
    v->editing = -1;
    v->details = rubraview_pal_shellhost_ask("props-read", (const char *const *)v->paths, 1, (proven_u8str_view_t){0}, &v->read_ticket)
                 ? ASK_WAITING : ASK_FAILED;
    v->asked_at = rubraview_pal_time_now_seconds();
}

/* Alt+Enter, or a list's Properties row: these files (on disk; an archive for its pages, as explorermenu.c finds them). */
void props_show(app_state_t *app, const char *const *paths, size_t count, int32_t origin, size_t origin_index) {
    if (count == 0) { osd_say(app, TU8("there is no file on disk to show the properties of")); return; }
    if (app->props_open) props_close(app);   /* one window: the newest files */
    props_view_t *v = (props_view_t *)rubraview_alloc_zero(1, sizeof *v);
    if (!v) return;
    if (count > PROPS_FILES_MAX) count = PROPS_FILES_MAX;
    v->paths = (char **)rubraview_alloc_zero(count, sizeof(char *));
    v->files = (rubraview_fileprops_file_t *)rubraview_alloc_zero(count, sizeof *v->files);
    v->rows = (rubraview_props_row_t *)rubraview_alloc_zero(PROPS_ROWS_MAX, sizeof *v->rows);
    if (!v->paths || !v->files || !v->rows) { props_free(v); return; }
    for (size_t i = 0; i < count; ++i) if ((v->paths[v->count] = rubraview_strdup(paths[i])) != NULL) v->count++;
    v->origin = origin;
    v->origin_index = origin_index;
    v->editing = -1;
    rubraview_window_config_t config = { .title = T("Rubraview - properties"), .width = 520, .height = 640, .owner = app->window };
    app->props_window = rubraview_pal_window_create(window_block_take(app, &app->props_block), &config);
    if (!app->props_window) { props_free(v); osd_say(app, TU8("the properties window could not be opened")); return; }
    if (app->props_frame[2] > 0 && app->props_frame[3] > 0)
        rubraview_pal_window_set_frame(app->props_window, app->props_frame[0], app->props_frame[1], app->props_frame[2], app->props_frame[3]);
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->props_window, &w, &h);
    app->props_renderer = rubraview_pal_render_create(window_block_arena(app, &app->props_block), rubraview_pal_window_native_handle(app->props_window), w, h);
    ui_font_apply_all(app);
    if (!app->props_renderer) {
        rubraview_pal_window_destroy(app->props_window); window_block_give(app, &app->props_block);
        app->props_window = NULL;
        props_free(v);
        osd_say(app, TU8("the properties window could not be drawn"));
        return;
    }
    app->props = v;
    app->props_open = true;
    read_general(app);
    propsec_read(app);
    ask_details(app);
    app_log(app, "properties", rubraview_bytes_view(v->paths[0], proven_cstr_len(v->paths[0])));
}

/* ---- Apply ---- */

static bool details_changed(const props_view_t *v) {
    for (size_t i = 0; i < v->row_count; ++i) if (v->rows[i].flags & PROPS_ROW_CHANGED) return true;
    return false;
}

static bool general_changed(const props_view_t *v) {
    if (v->unblock) return true;
    for (size_t i = 0; i < v->count; ++i)
        if (rubraview_fileprops_attr_apply(v->files[i].attributes, v->want, v->last_on) != (v->files[i].attributes & RUBRAVIEW_FATTR_ALL)) return true;
    return false;
}

static void edit_end(app_state_t *app, bool keep);

/* What was changed, written: General here, Details by the child (its answer comes later). True when all is done now. */
static bool props_apply(app_state_t *app) {
    props_view_t *v = V(app);
    if (v->editing >= 0) edit_end(app, true);
    v->message[0] = '\0';
    if (general_changed(v)) {
        int failed = 0;
        char why[256] = "";
        for (size_t i = 0; i < v->count; ++i) {
            uint32_t have = v->files[i].attributes & RUBRAVIEW_FATTR_ALL;
            uint32_t want = rubraview_fileprops_attr_apply(have, v->want, v->last_on);
            /* Unblock first: a file made read-only keeps its streams (VM: the mark stayed) */
            if (v->unblock && v->files[i].blocked && !rubraview_pal_fileprops_unblock(v->files[i].path)) {
                failed++;
                (void)rubraview_fmt(why, sizeof why, "{}", PROVEN_ARG(T("Unblock")));
            }
            if (want != have && !rubraview_pal_fileprops_set_attributes(v->files[i].path, have, want, why, sizeof why)) failed++;
        }
        if (failed) (void)rubraview_fmt(v->message, sizeof v->message, T("not every change could be made: {}"), PROVEN_ARG(why));
        else pw_say(v, T("the attributes are changed"));
        app_log(app, "properties", failed ? TU8("attributes: not every change could be made") : TU8("attributes changed"));
        char keep[sizeof v->message];
        proven_sys_mem_copy(keep, v->message, sizeof keep);
        read_general(app);
        proven_sys_mem_copy(v->message, keep, sizeof keep);   /* what was said stays over the reading's */
        if (failed) return false;   /* OK leaves the window up, with why (VM: it closed and said nothing) */
    }
    if (!propsec_apply(app)) return false;   /* Windows refused a permission: the window stays, with why and the shield */
    if (!details_changed(v)) return true;
    char *lines = (char *)rubraview_alloc(PROPS_ROWS_MAX * 1200);
    if (!lines) return true;
    size_t at = 0;
    for (size_t i = 0; i < v->row_count; ++i) {
        if (!(v->rows[i].flags & PROPS_ROW_CHANGED)) continue;
        if (rubraview_props_change_line(v->rows[i].name, v->rows[i].value, lines + at, PROPS_ROWS_MAX * 1200 - at)) at += proven_cstr_len(lines + at);
    }
    v->writing = rubraview_pal_shellhost_ask("props-write", (const char *const *)v->paths, v->count, rubraview_bytes_view(lines, at), &v->write_ticket);
    rubraview_free(lines);
    v->write_at = rubraview_pal_time_now_seconds();
    if (!v->writing) { pw_say(v, T("the details could not be written: the helper did not start")); return true; }
    pw_say(v, T("writing the details..."));
    app->props_dirty = true;
    return false;
}

/* The answers of the child, looked for each pass. */
static void props_poll(app_state_t *app) {
    props_view_t *v = V(app);
    double now = rubraview_pal_time_now_seconds();
    propsec_poll(app);
    if (v->details == ASK_WAITING) {
        char *text = NULL;
        size_t n = 0;
        if (rubraview_pal_shellhost_answer(v->read_ticket, &text, &n) == 1) {
            v->row_count = rubraview_props_rows_read(rubraview_bytes_view(text, n), v->rows, PROPS_ROWS_MAX);
            rubraview_free(text);
            v->details = ASK_DONE;
            app->props_dirty = true;
        } else if (now - v->asked_at > PROPS_ASK_SECONDS) {
            rubraview_pal_shellhost_forget(v->read_ticket);
            v->details = ASK_FAILED;
            app->props_dirty = true;
        }
    }
    if (v->writing) {
        char *text = NULL;
        size_t n = 0;
        if (rubraview_pal_shellhost_answer(v->write_ticket, &text, &n) == 1) {
            v->writing = false;
            bool bad = n > 0 && rubraview_cstr_str(text, "fail") != NULL;
            pw_say(v, bad ? T("not every detail could be written (the file may be read-only, open, or of a kind that keeps none)")
                       : T("the details are written"));
            app_log(app, "properties", bad ? TU8("details: not every one written") : TU8("details written"));
            rubraview_free(text);
            if (v->close_after_write && !bad) { props_close(app); return; }
            v->close_after_write = false;
            ask_details(app);
            app->props_dirty = true;
        } else if (now - v->write_at > PROPS_ASK_SECONDS) {
            rubraview_pal_shellhost_forget(v->write_ticket);
            v->writing = false;
            v->close_after_write = false;
            pw_say(v, T("the details could not be written: the helper did not answer"));
            app->props_dirty = true;
        }
    }
}

/* ---- typing in a Details value ---- */

static void edit_begin(app_state_t *app, int32_t row) {
    props_view_t *v = V(app);
    if (row < 0 || (size_t)row >= v->row_count || !(v->rows[row].flags & RUBRAVIEW_PROPS_ROW_EDITABLE)) return;
    if (v->rows[row].flags & RUBRAVIEW_PROPS_ROW_RATING) return;   /* the stars are clicked */
    v->editing = row;
    editbox_begin(&v->ec, v->edit_buf, sizeof v->edit_buf, false);
    v->view.scroll = 0.0;
    rubraview_textedit_set(&v->ec.te, proven_u8str_view_from_cstr(v->rows[row].value));
    rubraview_textedit_select_all(&v->ec.te);
    v->composing[0] = '\0';
    rubraview_pal_window_text_input(app->props_window, true);
    app->props_dirty = true;
}

void pw_name_begin(app_state_t *app, int32_t name_for) {
    props_view_t *v = V(app);
    if (v->editing >= 0) edit_end(app, true);
    v->editing = EDIT_NAME;
    v->name_for = name_for;
    editbox_begin(&v->ec, v->edit_buf, sizeof v->edit_buf, false);
    v->view.scroll = 0.0;
    v->composing[0] = '\0';
    rubraview_pal_window_text_input(app->props_window, true);
    app->props_dirty = true;
}

static void edit_end(app_state_t *app, bool keep) {
    props_view_t *v = V(app);
    if (v->editing == EDIT_NAME) {   /* the Security tab's name field: what was typed goes to propsec.c */
        char typed[512];
        proven_u8str_view_t t = rubraview_textedit_text(&v->ec.te);
        size_t n = t.size < sizeof typed - 1 ? t.size : sizeof typed - 1;
        proven_sys_mem_copy(typed, t.ptr, n);
        typed[n] = '\0';
        v->editing = -1;
        v->composing[0] = '\0';
        rubraview_pal_window_text_input(app->props_window, false);
        if (keep && n > 0) propsec_name_done(app, typed);
        v->name_for = NAME_NONE;
        app->props_dirty = true;
        return;
    }
    if (v->editing < 0) return;
    rubraview_props_row_t *r = &v->rows[v->editing];
    proven_u8str_view_t t = rubraview_textedit_text(&v->ec.te);
    if (keep && !proven_u8str_view_eq(t, proven_u8str_view_from_cstr(r->value))) {
        size_t n = t.size < sizeof r->value - 1 ? t.size : sizeof r->value - 1;
        proven_sys_mem_copy(r->value, t.ptr, n);
        r->value[n] = '\0';
        r->flags |= PROPS_ROW_CHANGED;
    }
    v->editing = -1;
    v->composing[0] = '\0';
    rubraview_pal_window_text_input(app->props_window, false);
    app->props_dirty = true;
}

static bool edit_key(app_state_t *app, rubraview_key_combo_t k) {
    props_view_t *v = V(app);
    if (v->editing == -1) return false;
    if (key_is(k, "Enter")) edit_end(app, true);
    else if (key_is(k, "Escape")) edit_end(app, false);
    else if (!editbox_key(app->props_window, &v->ec, k, false)) return true;   /* RV-293: every editing key; others are the text's, as TEXT events */
    app->props_dirty = true;
    return true;
}

/* ---- drawing ---- */

double pw_content_width(app_state_t *app) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->props_window, &w, &h);
    double dpi = rubraview_pal_window_dpi_scale(app->props_window);
    return dlgpage_width(&V(app)->page, (double)w, dpi);
}

void pw_text_at(rubraview_renderer_t *r, const char *s, rubraview_pal_rect_t box, double size, uint32_t color) {
    if (box.width <= 0.0 || box.height <= 0.0) return;
    rubraview_pal_render_draw_text(r, proven_u8str_view_from_cstr(s), box, size, color, RUBRAVIEW_TEXT_LEFT);
}

static void pw_fit_text_at(rubraview_renderer_t *r, const char *s, rubraview_pal_rect_t box, double size, uint32_t color) {
    if (box.width <= 0.0 || box.height <= 0.0) return;
    char fit[600];
    proven_u8str_view_t t = fit_text_measured(r, fit, sizeof(fit), proven_u8str_view_from_cstr(s), box.width, size);
    if (t.size > 0) rubraview_pal_render_draw_text(r, t, box, size, color, RUBRAVIEW_TEXT_LEFT);
}

void pw_button_at(app_state_t *app, int which, const char *words, rubraview_pal_rect_t b, bool accent, bool enabled) {
    props_view_t *v = V(app);
    rubraview_renderer_t *r = app->props_renderer;
    double dpi = rubraview_pal_window_dpi_scale(app->props_window);
    v->button[which] = b;
    if (b.width <= 0.0 || b.height <= 0.0) return;
    bool over = enabled && pal_rect_has(b, app->props_px, app->props_py);
    rubraview_pal_render_fill_rect(r, b, over ? 0xFF3A4350u : accent ? 0xFF2F5F95u : 0xFF2A2F36u, 4.0 * dpi);
    rubraview_pal_render_stroke_rect(r, b, accent ? PW_ACCENT : 0xFF4A525Cu, 1.0, 4.0 * dpi);
    char fit[128];
    proven_u8str_view_t text = b.width > 8.0 * dpi ? fit_text_measured(r, fit, sizeof(fit), proven_u8str_view_from_cstr(words), b.width - 8.0 * dpi, 13.0 * dpi) : PROVEN_LIT("");
    if (text.size > 0) rubraview_pal_render_draw_text(r, text, b, 13.0 * dpi, enabled ? PW_TEXT : PW_DIM, RUBRAVIEW_TEXT_CENTER);
}

double pw_buttons_row(app_state_t *app, const int *which, const char *const *words, const bool *accent, const bool *enabled, size_t n,
                      double left, double right, double y, double min_w, double bh) {
    rubraview_renderer_t *r = app->props_renderer;
    double dpi = rubraview_pal_window_dpi_scale(app->props_window);
    rubraview_pal_rect_t rect[16] = { {0} };
    if (n > 16) n = 16;
    if (right < left + 20.0 * dpi) right = left + 20.0 * dpi;
    double h = dlgpage_buttons(r, words, n, left, right, y, 13.0 * dpi, 10.0 * dpi, min_w, bh, 6.0 * dpi, rect);
    for (size_t i = 0; i < n; ++i)
        pw_button_at(app, which[i], words[i], rect[i], accent ? accent[i] : false, enabled ? enabled[i] : true);
    return h;
}

double pw_check_at(app_state_t *app, int which, const char *words, rubraview_tristate_t state, double x, double y, double max_x, bool enabled) {
    props_view_t *v = V(app);
    rubraview_renderer_t *r = app->props_renderer;
    double dpi = rubraview_pal_window_dpi_scale(app->props_window);
    double s = 16.0 * dpi;
    double avail_tw = max_x - (x + s + 8.0 * dpi);
    if (avail_tw < 40.0 * dpi) avail_tw = 40.0 * dpi;
    double tw = 0.0, th = 20.0 * dpi;
    proven_u8str_view_t wv = proven_u8str_view_from_cstr(words);
    if (!rubraview_pal_render_measure_text_wrapped(r, wv, 13.0 * dpi, avail_tw, &tw, &th) || th < 20.0 * dpi) th = 20.0 * dpi;
    double row_h = th + 2.0 * dpi;
    rubraview_pal_rect_t box = { x, y + 2.0 * dpi, s, s };
    v->box[which] = (rubraview_pal_rect_t){ x, y, s + 8.0 * dpi + (tw < avail_tw ? tw : avail_tw), row_h };
    rubraview_pal_render_fill_rect(r, box, 0xFF15181Cu, 2.0 * dpi);
    rubraview_pal_render_stroke_rect(r, box, enabled ? 0xFF6A737Du : PW_LINE, 1.0, 2.0 * dpi);
    if (state == RUBRAVIEW_TRI_ON) rubraview_pal_render_draw_text(r, PROVEN_LIT("\xe2\x9c\x93"), box, 14.0 * dpi, enabled ? PW_TEXT : PW_DIM, RUBRAVIEW_TEXT_CENTER);
    if (state == RUBRAVIEW_TRI_MIXED)
        rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ x + 4.0 * dpi, y + 6.0 * dpi, s - 8.0 * dpi, s - 8.0 * dpi }, enabled ? PW_TEXT : PW_DIM, 1.0);
    pw_text_at(r, words, (rubraview_pal_rect_t){ x + s + 8.0 * dpi, y, avail_tw, row_h }, 13.0 * dpi, enabled ? PW_TEXT : PW_DIM);
    return y + row_h + 4.0 * dpi;
}

/* "label    value" a row; the value wraps when the window is narrow */
double pw_info_row(app_state_t *app, double y, const char *label, const char *value) {
    rubraview_renderer_t *r = app->props_renderer;
    double dpi = rubraview_pal_window_dpi_scale(app->props_window);
    double w = pw_content_width(app);
    double lx = 18.0 * dpi, avail = w - 2.0 * lx;
    double vx = lx + (avail * 0.36 < 122.0 * dpi ? avail * 0.36 : 122.0 * dpi);
    double val_w = w - vx - lx;
    if (val_w < 40.0 * dpi) val_w = 40.0 * dpi;
    double mw = 0.0, mh = 20.0 * dpi;
    proven_u8str_view_t vv = proven_u8str_view_from_cstr(value);
    if (vv.size > 0) (void)rubraview_pal_render_measure_text_wrapped(r, vv, 13.0 * dpi, val_w, &mw, &mh);
    if (mh < 20.0 * dpi) mh = 20.0 * dpi;
    pw_fit_text_at(r, label, (rubraview_pal_rect_t){ lx, y, vx - lx - 6.0 * dpi, 20.0 * dpi }, 13.0 * dpi, PW_DIM);
    pw_text_at(r, value, (rubraview_pal_rect_t){ vx, y, val_w, mh + 2.0 * dpi }, 13.0 * dpi, PW_TEXT);
    return y + mh + 5.0 * dpi;
}

double pw_separator(app_state_t *app, double y) {
    double dpi = rubraview_pal_window_dpi_scale(app->props_window);
    double w = pw_content_width(app);
    if (w > 36.0 * dpi)
        rubraview_pal_render_fill_rect(app->props_renderer, (rubraview_pal_rect_t){ 18.0 * dpi, y + 5.0 * dpi, w - 36.0 * dpi, 1.0 * dpi }, PW_LINE, 0.0);
    return y + 12.0 * dpi;
}

static double draw_general(app_state_t *app, double y, double cw) {
    props_view_t *v = V(app);
    const rubraview_fileprops_file_t *f = &v->sum;
    double dpi = rubraview_pal_window_dpi_scale(app->props_window);
    rubraview_renderer_t *r = app->props_renderer;
    double lx = 18.0 * dpi, right = cw - lx;
    char text[1100], a[96], b[96];
    bool one = v->count == 1;
    /* the name, and Rename */
    if (one) {
        proven_u8str_view_t name = rubraview_path_basename(proven_u8str_view_from_cstr(f->path));
        (void)rubraview_fmt(text, sizeof text, "{}", PROVEN_ARG(name));
    } else {
        (void)rubraview_fmt(text, sizeof text, T("{} files and folders"), PROVEN_ARG((uint32_t)v->count));
    }
    bool has_ren = one && v->origin != PROPS_FROM_NONE;
    double ren_w = 0.0;
    if (has_ren) {
        (void)rubraview_pal_render_measure_text(r, cstr(T("Rename...")), 13.0 * dpi, &ren_w);
        ren_w += 22.0 * dpi;
        if (ren_w < 84.0 * dpi) ren_w = 84.0 * dpi;
    }
    if (has_ren && cw >= 290.0 * dpi) {
        char fit[300];
        double tw = right - lx - ren_w - 8.0 * dpi;
        rubraview_pal_render_draw_text(r, files_fit_text(fit, sizeof(fit), proven_u8str_view_from_cstr(text), tw, 16.0 * dpi),
                                       (rubraview_pal_rect_t){ lx, y, tw, 26.0 * dpi }, 16.0 * dpi, PW_TEXT, RUBRAVIEW_TEXT_LEFT);
        pw_button_at(app, PB_RENAME, T("Rename..."), (rubraview_pal_rect_t){ right - ren_w, y, ren_w, 26.0 * dpi }, false, true);
        y += 30.0 * dpi;
    } else {
        char fit[300];
        rubraview_pal_render_draw_text(r, files_fit_text(fit, sizeof(fit), proven_u8str_view_from_cstr(text), right - lx, 16.0 * dpi),
                                       (rubraview_pal_rect_t){ lx, y, right - lx, 26.0 * dpi }, 16.0 * dpi, PW_TEXT, RUBRAVIEW_TEXT_LEFT);
        y += 28.0 * dpi;
        if (has_ren) {
            const int id = PB_RENAME; const char *lbl = T("Rename...");
            y += pw_buttons_row(app, &id, &lbl, NULL, NULL, 1, lx, right, y, 80.0 * dpi, 26.0 * dpi) + 4.0 * dpi;
        } else v->button[PB_RENAME] = (rubraview_pal_rect_t){0};
    }
    y = pw_separator(app, y);
    if (one) {
        y = pw_info_row(app, y, T("Type of file:"), f->type_name[0] ? f->type_name : T("(unknown)"));
        if (!f->is_folder) {
            y = pw_info_row(app, y, T("Opens with:"), f->opens_with[0] ? f->opens_with : T("(none)"));
            const int id = PB_CHANGE; const char *lbl = T("Change...");
            y += pw_buttons_row(app, &id, &lbl, NULL, NULL, 1, lx, right, y, 80.0 * dpi, 24.0 * dpi) + 4.0 * dpi;
        } else v->button[PB_CHANGE] = (rubraview_pal_rect_t){0};
        y = pw_separator(app, y);
        proven_u8str_view_t dir = rubraview_path_dirname(proven_u8str_view_from_cstr(f->path));
        (void)rubraview_fmt(text, sizeof text, "{}", PROVEN_ARG(dir));
        y = pw_info_row(app, y, T("Location:"), text);
    } else {
        v->button[PB_CHANGE] = (rubraview_pal_rect_t){0};
        y = pw_info_row(app, y, T("Type:"), T("several kinds"));
    }
    rubraview_info_bytes(a, sizeof a, f->size);
    y = pw_info_row(app, y, T("Size:"), a);
    rubraview_info_bytes(b, sizeof b, f->on_disk);
    y = pw_info_row(app, y, T("Size on disk:"), b);
    if (f->is_folder || !one) {
        (void)rubraview_fmt(text, sizeof text, T("{} files, {} folders{}"), PROVEN_ARG(f->files), PROVEN_ARG(f->folders),
                            PROVEN_ARG(f->counted_all ? "" : T(" (counting stopped: too many)")));
        y = pw_info_row(app, y, T("Contains:"), text);
    }
    y = pw_separator(app, y);
    if (one) {
        info_local_time(a, sizeof a, f->created);  y = pw_info_row(app, y, T("Created:"), a);
        if (!f->is_folder) { info_local_time(a, sizeof a, f->modified); y = pw_info_row(app, y, T("Modified:"), a); }
        info_local_time(a, sizeof a, f->accessed); y = pw_info_row(app, y, T("Accessed:"), a);
        y = pw_separator(app, y);
    }
    bool stack_labels = cw < 380.0 * dpi;
    double cx0 = stack_labels ? (lx + 12.0 * dpi) : 140.0 * dpi;
    pw_fit_text_at(r, T("Attributes:"), (rubraview_pal_rect_t){ lx, y, stack_labels ? right - lx : cx0 - lx - 6.0 * dpi, 20.0 * dpi }, 13.0 * dpi, PW_DIM);
    if (stack_labels) y += 22.0 * dpi;
    double w_ro = 0.0, w_hid = 0.0;
    (void)rubraview_pal_render_measure_text(r, cstr(T("Read-only")), 13.0 * dpi, &w_ro);
    (void)rubraview_pal_render_measure_text(r, cstr(T("Hidden")), 13.0 * dpi, &w_hid);
    double ro_box_w = 24.0 * dpi + w_ro + 16.0 * dpi;
    if (cx0 + ro_box_w + 24.0 * dpi + w_hid <= right) {
        double y1 = pw_check_at(app, 0, T("Read-only"), v->want[0], cx0, y, cx0 + ro_box_w, true);
        double y2 = pw_check_at(app, 1, T("Hidden"), v->want[1], cx0 + ro_box_w, y, right, true);
        y = y1 > y2 ? y1 : y2;
    } else {
        y = pw_check_at(app, 0, T("Read-only"), v->want[0], cx0, y, right, true);
        y = pw_check_at(app, 1, T("Hidden"), v->want[1], cx0, y, right, true);
    }
    y += 4.0 * dpi;
    pw_fit_text_at(r, T("Advanced:"), (rubraview_pal_rect_t){ lx, y, stack_labels ? right - lx : cx0 - lx - 6.0 * dpi, 20.0 * dpi }, 13.0 * dpi, PW_DIM);
    if (stack_labels) y += 22.0 * dpi;
    y = pw_check_at(app, 2, T("Ready for archiving"), v->want[2], cx0, y, right, true);
    y = pw_check_at(app, 3, T("Contents indexed besides its properties"), v->want[3], cx0, y, right, true);
    y = pw_check_at(app, 4, T("Compress contents to save disk space"), v->want[4], cx0, y, right, f->can_compress);
    y = pw_check_at(app, 5, T("Encrypt contents to secure data"), v->want[5], cx0, y, right, f->can_encrypt);
    if (f->blocked) {
        y = pw_separator(app, y);
        pw_fit_text_at(r, T("Security:"), (rubraview_pal_rect_t){ lx, y, stack_labels ? right - lx : cx0 - lx - 6.0 * dpi, 20.0 * dpi }, 13.0 * dpi, PW_DIM);
        if (stack_labels) y += 22.0 * dpi;
        const char *warn = T("It came from another computer and may be blocked to protect this one.");
        double mw = 0.0, mh = 20.0 * dpi;
        (void)rubraview_pal_render_measure_text_wrapped(r, proven_u8str_view_from_cstr(warn), 12.0 * dpi, right - cx0, &mw, &mh);
        pw_text_at(r, warn, (rubraview_pal_rect_t){ cx0, y, right - cx0, mh + 2.0 * dpi }, 12.0 * dpi, PW_TEXT);
        y = pw_check_at(app, 6, T("Unblock"), v->unblock ? RUBRAVIEW_TRI_ON : RUBRAVIEW_TRI_OFF, cx0, y + mh + 4.0 * dpi, right, true);
    } else v->box[6] = (rubraview_pal_rect_t){0};
    return y;
}

static double draw_details(app_state_t *app, double y, double cw) {
    props_view_t *v = V(app);
    double dpi = rubraview_pal_window_dpi_scale(app->props_window);
    rubraview_renderer_t *r = app->props_renderer;
    int32_t wh = 0, ww = 0;
    rubraview_pal_window_get_size(app->props_window, &ww, &wh);
    v->rows_drawn = 0;
    v->rows_drawn_from = 0;
    if (v->details != ASK_DONE) {
        const char *say_it = v->details == ASK_WAITING ? T("reading the details...") : T("the details could not be read");
        double mw = 0.0, mh = 20.0 * dpi;
        (void)rubraview_pal_render_measure_text_wrapped(r, proven_u8str_view_from_cstr(say_it), 13.0 * dpi, cw - 36.0 * dpi, &mw, &mh);
        pw_text_at(r, say_it, (rubraview_pal_rect_t){ 18.0 * dpi, y, cw - 36.0 * dpi, mh + 2.0 * dpi }, 13.0 * dpi, PW_DIM);
        return y + mh + 8.0 * dpi;
    }
    double rh = 24.0 * dpi, lx = 18.0 * dpi, avail = cw - 2.0 * lx;
    double vx = lx + (avail * 0.42 < 172.0 * dpi ? avail * 0.42 : 172.0 * dpi);
    pw_fit_text_at(r, T("Property"), (rubraview_pal_rect_t){ lx, y, vx - lx - 6.0 * dpi, 20.0 * dpi }, 12.0 * dpi, PW_DIM);
    pw_fit_text_at(r, T("Value"), (rubraview_pal_rect_t){ vx, y, cw - vx - lx, 20.0 * dpi }, 12.0 * dpi, PW_DIM);
    y = pw_separator(app, y + 20.0 * dpi);
    v->rows_drawn = v->row_count;
    for (size_t at = 0; at < v->row_count; ++at) {
        rubraview_props_row_t *row = &v->rows[at];
        rubraview_pal_rect_t line = { lx - 6.0 * dpi, y, cw - 2.0 * lx + 12.0 * dpi, rh };
        bool in_view = y + rh > 0.0 && y < (double)wh;
        v->row_rect[at] = in_view ? line : (rubraview_pal_rect_t){0};
        for (int s = 0; s < 5; ++s) v->star_rect[at][s] = (rubraview_pal_rect_t){0};
        if (in_view) {
            if (row->flags & RUBRAVIEW_PROPS_ROW_HEADING) {
                pw_fit_text_at(r, row->label, (rubraview_pal_rect_t){ lx, y + 2.0 * dpi, cw - 2.0 * lx, rh }, 13.0 * dpi, PW_HEADING);
            } else {
                bool editable = (row->flags & RUBRAVIEW_PROPS_ROW_EDITABLE) != 0;
                if (editable && pal_rect_has(line, app->props_px, app->props_py) && (int32_t)at != v->editing)
                    rubraview_pal_render_fill_rect(r, line, 0x18FFFFFFu, 3.0 * dpi);
                pw_fit_text_at(r, row->label, (rubraview_pal_rect_t){ lx, y + 2.0 * dpi, vx - lx - 8.0 * dpi, rh }, 13.0 * dpi, PW_DIM);
                rubraview_pal_rect_t val = { vx, y + 2.0 * dpi, cw - vx - lx, rh - 4.0 * dpi };
                if ((int32_t)at == v->editing) {
                    rubraview_pal_render_fill_rect(r, val, 0xFF101215u, 2.0 * dpi);
                    rubraview_pal_render_stroke_rect(r, val, PW_ACCENT, 1.0, 2.0 * dpi);
                    v->view.box = val;   /* RV-293: the shared field */
                    v->view.size = 13.0 * dpi;
                    editbox_draw(r, &v->ec, &v->view, proven_u8str_view_from_cstr(v->composing), true, PW_TEXT, dpi);
                } else if (row->flags & RUBRAVIEW_PROPS_ROW_RATING) {
                    int32_t stars = rubraview_props_rating_stars((int32_t)rubraview_parse_long(row->value, NULL, 10));
                    for (int s = 0; s < 5; ++s) {
                        rubraview_pal_rect_t sr = { vx + (double)s * 22.0 * dpi, y + 1.0 * dpi, 20.0 * dpi, rh - 2.0 * dpi };
                        v->star_rect[at][s] = sr;
                        rubraview_pal_render_draw_text(r, s < stars ? PROVEN_LIT("\xe2\x98\x85") : PROVEN_LIT("\xe2\x98\x86"), sr, 15.0 * dpi,
                                                       s < stars ? 0xFFE8C46Fu : PW_DIM, RUBRAVIEW_TEXT_CENTER);
                    }
                } else {
                    pw_fit_text_at(r, row->value, val, 13.0 * dpi, (row->flags & PROPS_ROW_CHANGED) ? 0xFFE8C46Fu : PW_TEXT);
                }
            }
        }
        y += rh;
    }
    return y;
}

void draw_props_window(app_state_t *app) {
    if (!app->props_open) return;
    props_poll(app);
    if (!app->props_open || !app->props_dirty) return;
    app->props_dirty = false;
    props_view_t *v = V(app);
    rubraview_renderer_t *r = app->props_renderer;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->props_window, &w, &h);
    double dpi = rubraview_pal_window_dpi_scale(app->props_window);
    double cw = dlgpage_width(&v->page, (double)w, dpi), sy = v->page.scroll;
    rubraview_pal_render_begin(r, PW_BACK);

    /* Wrapping tabs at top (scroll with the page when very short). */
    static const char *const NAMES[RUBRAVIEW_PROPS_TAB_COUNT] = { N_("General"), N_("Digital Signatures"), N_("Security"), N_("Details"), N_("Previous Versions") };
    int32_t tab_ids[RUBRAVIEW_PROPS_TAB_COUNT] = { 0 };
    double tab_w[RUBRAVIEW_PROPS_TAB_COUNT] = { 0 }, tab_x[RUBRAVIEW_PROPS_TAB_COUNT] = { 0 };
    uint32_t tab_row[RUBRAVIEW_PROPS_TAB_COUNT] = { 0 };
    size_t tn = 0;
    for (int32_t t = 0; t < RUBRAVIEW_PROPS_TAB_COUNT; ++t) {
        v->tab_rect[t] = (rubraview_pal_rect_t){0};
        if (!tab_shown(v, t)) continue;
        double tw = 0.0;
        (void)rubraview_pal_render_measure_text(r, proven_u8str_view_from_cstr(rubraview_tr(NAMES[t])), 13.0 * dpi, &tw);
        tab_ids[tn] = t;
        tab_w[tn] = tw + 22.0 * dpi;
        if (tab_w[tn] > cw - 20.0 * dpi && cw > 40.0 * dpi) tab_w[tn] = cw - 20.0 * dpi;
        tn++;
    }
    double room = cw - 20.0 * dpi;
    if (room < 40.0 * dpi) room = 40.0 * dpi;
    size_t trows = rubraview_flow_inline(tab_w, tn, 4.0 * dpi, room, tab_row, tab_x);
    double tab_h = 26.0 * dpi, y0 = 8.0 * dpi - sy;
    for (size_t k = 0; k < tn; ++k) {
        int32_t t = tab_ids[k];
        rubraview_pal_rect_t tr = { 10.0 * dpi + tab_x[k], y0 + (double)tab_row[k] * (tab_h + 4.0 * dpi), tab_w[k], tab_h };
        v->tab_rect[t] = tr;
        bool on = v->tab == t;
        rubraview_pal_render_fill_rect(r, tr, on ? PW_PANEL : pal_rect_has(tr, app->props_px, app->props_py) ? 0xFF2A2F36u : PW_BACK, 4.0 * dpi);
        if (on) rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ tr.x, tr.y + tr.height - 2.0 * dpi, tr.width, 2.0 * dpi }, PW_ACCENT, 0.0);
        char fit[64];
        proven_u8str_view_t label = tr.width > 8.0 * dpi ? fit_text_measured(r, fit, sizeof(fit), proven_u8str_view_from_cstr(rubraview_tr(NAMES[t])), tr.width - 8.0 * dpi, 13.0 * dpi) : PROVEN_LIT("");
        if (label.size > 0) rubraview_pal_render_draw_text(r, label, tr, 13.0 * dpi, on ? PW_TEXT : PW_DIM, RUBRAVIEW_TEXT_CENTER);
    }
    double tabs_end = y0 + (trows ? (double)trows * tab_h + (double)(trows - 1) * 4.0 * dpi : 0.0) + 4.0 * dpi;
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ 0.0, tabs_end, cw, 1.0 * dpi }, PW_LINE, 0.0);

    for (int b = 0; b < PB_COUNT; ++b) v->button[b] = (rubraview_pal_rect_t){0};   /* only what this tab draws answers */
    for (int b = 0; b < 8; ++b) v->box[b] = (rubraview_pal_rect_t){0};
    if (!tab_shown(v, v->tab)) v->tab = RUBRAVIEW_PROPS_GENERAL;
    double body_y = tabs_end + 10.0 * dpi;
    if (v->tab == RUBRAVIEW_PROPS_DETAILS) body_y = draw_details(app, body_y, cw);
    else if (v->tab == RUBRAVIEW_PROPS_SIGNATURES) body_y = propsec_draw_signatures(app, body_y);
    else if (v->tab == RUBRAVIEW_PROPS_SECURITY) body_y = propsec_draw_security(app, body_y);
    else if (v->tab == RUBRAVIEW_PROPS_VERSIONS) body_y = propsver_draw(app, body_y);
    else body_y = draw_general(app, body_y, cw);

    /* Foot message and wrapping buttons (anchored at the bottom when tall enough, or below content when short). */
    const char *fwords[4] = { T("Windows properties"), T("OK"), T("Cancel"), T("Apply") };
    rubraview_pal_rect_t frel[4] = { {0} };
    double msg_w = cw - 36.0 * dpi;
    if (msg_w < 40.0 * dpi) msg_w = 40.0 * dpi;
    double mw = 0.0, mh = 0.0;
    if (v->message[0]) {
        (void)rubraview_pal_render_measure_text_wrapped(r, proven_u8str_view_from_cstr(v->message), 12.0 * dpi, msg_w, &mw, &mh);
        if (mh < 18.0 * dpi) mh = 18.0 * dpi;
    }
    double btn_left = 12.0 * dpi, btn_right = cw - 12.0 * dpi;
    if (btn_right < btn_left + 40.0 * dpi) btn_right = btn_left + 40.0 * dpi;
    double fbtns_h = dlgpage_buttons(r, fwords, 4, btn_left, btn_right, 0.0, 13.0 * dpi, 10.0 * dpi, 76.0 * dpi, 28.0 * dpi, 6.0 * dpi, frel);
    if (frel[3].y - frel[0].y < 1.0 && frel[0].x > btn_left + 12.0 * dpi) frel[0].x = btn_left;   /* flush left when all 4 fit on 1 row */
    double foot_block_h = (mh > 0.0 ? mh + 8.0 * dpi : 0.0) + 8.0 * dpi + fbtns_h + 12.0 * dpi;
    double foot_y = body_y + 10.0 * dpi;
    double min_foot_y = (double)h - foot_block_h - sy;
    if (foot_y < min_foot_y) foot_y = min_foot_y;
    double cur = foot_y;
    if (v->message[0]) {
        pw_text_at(r, v->message, (rubraview_pal_rect_t){ 18.0 * dpi, cur, msg_w, mh + 2.0 * dpi }, 12.0 * dpi, 0xFFE8C46Fu);
        cur += mh + 8.0 * dpi;
    }
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ 0.0, cur, cw, 1.0 * dpi }, PW_LINE, 0.0);
    cur += 8.0 * dpi;
    bool changed = general_changed(v) || details_changed(v) || propsec_changed(v) || v->editing >= 0;
    const int fids[4] = { PB_WINDOWS, PB_OK, PB_CANCEL, PB_APPLY };
    const bool facc[4] = { false, true, false, false };
    const bool fena[4] = { true, !v->writing, true, changed && !v->writing };
    for (int i = 0; i < 4; ++i) {
        frel[i].y += cur;
        pw_button_at(app, fids[i], fwords[i], frel[i], facc[i], fena[i]);
    }
    double total_content = (cur + fbtns_h + 12.0 * dpi) + sy;
    if (dlgpage_end(&v->page, r, (double)w, (double)h, total_content, app->props_px, app->props_py, dpi))
        app->props_dirty = true;
    if (!rubraview_pal_render_end(r)) app->props_dirty = true;
}

/* ---- the pointer and the keys ---- */

static void props_button(app_state_t *app, int which) {
    props_view_t *v = V(app);
    if (which == PB_WINDOWS) {
        if (!rubraview_pal_shellhost_show("properties", (const char *const *)v->paths, v->count, rubraview_pal_window_native_handle(app->props_window)))
            pw_say(v, T("Windows' properties window could not be started"));
    } else if (which == PB_CHANGE) {
        if (!rubraview_pal_shellhost_show("openwith", (const char *const *)v->paths, 1, rubraview_pal_window_native_handle(app->props_window)))
            pw_say(v, T("Windows' Open with box could not be started"));
        else pw_say(v, T("after choosing a program, the window shows it the next time it is opened"));
    } else if (which == PB_APPLY) {
        if (!v->writing) (void)props_apply(app);
    } else if (which == PB_OK) {
        if (v->writing) return;
        if (props_apply(app)) props_close(app);
        else if (v->writing) v->close_after_write = true;   /* not when a change failed: the window stays, with why */
        return;
    } else if (which == PB_CANCEL) {
        props_close(app);
        return;
    } else if (which >= PB_VER_OPEN) {
        if (!propsver_button(app, which)) return;   /* the window closed: the viewer shows the version */
    } else if (which >= PB_CERT) {
        if (!propsec_button(app, which)) return;
    } else if (which == PB_RENAME) {
        int32_t origin = v->origin;
        size_t index = v->origin_index;
        props_close(app);   /* the name is typed in the viewer's card, as Rename does */
        if (origin == PROPS_FROM_PICKER) rename_picker_begin(app, index);
        else if (origin == PROPS_FROM_PAGE) handle_action(app, PROVEN_LIT("rename_file"));
        return;
    }
    app->props_dirty = true;
}

static void props_press(app_state_t *app, double x, double y) {
    props_view_t *v = V(app);
    if (dlgpage_press(&v->page, x, y)) { app->props_dirty = true; return; }
    if (v->editing != -1 && editbox_pointer_down(app->props_renderer, &v->ec, &v->view, x, y, false)) {   /* RV-293 */
        v->composing[0] = '\0';
        app->props_dirty = true;
        return;
    }
    for (int32_t t = 0; t < RUBRAVIEW_PROPS_TAB_COUNT; ++t)
        if (v->tab_rect[t].width > 0 && pal_rect_has(v->tab_rect[t], x, y)) {
            if (v->editing >= 0) edit_end(app, true);
            v->tab = t;
            v->page.scroll = 0.0;
            v->page.bar = false;
            app->props_dirty = true;
            return;
        }
    for (int b = 0; b < PB_COUNT; ++b)
        if (v->button[b].width > 0 && pal_rect_has(v->button[b], x, y)) { props_button(app, b); return; }
    if (v->tab == RUBRAVIEW_PROPS_VERSIONS) { (void)propsver_press(app, x, y); return; }
    if (v->tab == RUBRAVIEW_PROPS_SIGNATURES || v->tab == RUBRAVIEW_PROPS_SECURITY) {
        if (v->editing == EDIT_NAME) edit_end(app, true);
        (void)propsec_press(app, x, y);
        return;
    }
    if (v->tab == RUBRAVIEW_PROPS_GENERAL) {
        for (uint32_t b = 0; b < 6; ++b) {
            if (!pal_rect_has(v->box[b], x, y)) continue;
            if ((b == 4 && !v->sum.can_compress) || (b == 5 && !v->sum.can_encrypt)) return;
            v->want[b] = rubraview_fileprops_attr_click(v->want[b]);
            if (v->want[b] == RUBRAVIEW_TRI_ON && (b == 4 || b == 5)) {   /* compress and encrypt exclude each other, as in Windows */
                v->want[b == 4 ? 5 : 4] = RUBRAVIEW_TRI_OFF;
                v->last_on = 1u << b;
            }
            app->props_dirty = true;
            return;
        }
        if (v->box[6].width > 0 && pal_rect_has(v->box[6], x, y)) { v->unblock = !v->unblock; app->props_dirty = true; }
        return;
    }
    /* Details: a value to type, a star */
    for (size_t i = v->rows_drawn_from; i < v->rows_drawn_from + v->rows_drawn && i < v->row_count; ++i) {
        if (!pal_rect_has(v->row_rect[i], x, y)) continue;
        rubraview_props_row_t *row = &v->rows[i];
        if ((row->flags & RUBRAVIEW_PROPS_ROW_RATING) && (row->flags & RUBRAVIEW_PROPS_ROW_EDITABLE)) {
            for (int s = 0; s < 5; ++s)
                if (pal_rect_has(v->star_rect[i][s], x, y)) {
                    int32_t now = rubraview_props_rating_stars((int32_t)rubraview_parse_long(row->value, NULL, 10));
                    int32_t want = now == s + 1 ? 0 : s + 1;   /* the same star again: none */
                    (void)rubraview_fmt(row->value, sizeof row->value, "{}", PROVEN_ARG(rubraview_props_rating_value(want)));
                    row->flags |= PROPS_ROW_CHANGED;
                    app->props_dirty = true;
                }
            return;
        }
        if ((int32_t)i != v->editing) {
            if (v->editing >= 0) edit_end(app, true);
            edit_begin(app, (int32_t)i);
        }
        return;
    }
    if (v->editing >= 0) edit_end(app, true);
}

size_t props_pump(app_state_t *app) {
    size_t handled = 0;
    rubraview_window_event_t event;
    if (app->props_open) props_poll(app);
    while (app->props_open && rubraview_pal_window_poll_event(app->props_window, &event)) {
        handled++;
        props_view_t *v = V(app);
        switch (event.kind) {
            case RUBRAVIEW_WINDOW_EVENT_CLOSE:
                props_close(app);
                return handled;
            case RUBRAVIEW_WINDOW_EVENT_RESIZE:
                rubraview_pal_render_resize(app->props_renderer, event.resize.width, event.resize.height);
                app->props_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_PAINT:
            case RUBRAVIEW_WINDOW_EVENT_DPI_CHANGED:
                app->props_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE:
                app->props_px = event.mouse.x;
                app->props_py = event.mouse.y;
                if (dlgpage_held(&v->page, event.mouse.x, event.mouse.y)) { app->props_dirty = true; break; }
                if (v->editing != -1) (void)editbox_pointer_move(app->props_renderer, &v->ec, &v->view, event.mouse.x);   /* RV-293 */
                app->props_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_UP:
                v->view.dragging = false;
                if (dlgpage_release(&v->page)) app->props_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_WHEEL:
                if (dlgpage_wheel(&v->page, event.mouse.wheel_delta, rubraview_pal_window_dpi_scale(app->props_window))) app->props_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN:
                if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT) props_press(app, event.mouse.x, event.mouse.y);
                if (!app->props_open) return handled;
                break;
            case RUBRAVIEW_WINDOW_EVENT_TEXT:
                if (v->editing != -1) {
                    editbox_text(&v->ec, rubraview_bytes_view(event.text.utf8, event.text.length), true);
                    v->composing[0] = '\0';
                    app->props_dirty = true;
                }
                break;
            case RUBRAVIEW_WINDOW_EVENT_COMPOSITION:
                if (v->editing != -1) {
                    size_t n = event.text.length < sizeof v->composing - 1 ? event.text.length : sizeof v->composing - 1;
                    proven_sys_mem_copy(v->composing, event.text.utf8, n);
                    v->composing[n] = '\0';
                    app->props_dirty = true;
                }
                break;
            case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN: {
                rubraview_key_combo_t k = event.key.combo;
                if (edit_key(app, k)) break;
                if (key_is(k, "Escape")) { props_close(app); return handled; }
                if (key_is(k, "Enter")) { props_button(app, PB_OK); if (!app->props_open) return handled; break; }
                if ((k.modifiers & RUBRAVIEW_MOD_CTRL) && key_is(k, "Tab")) {   /* the next tab, as Windows' */
                    int32_t t = v->tab;
                    do { t = (t + ((k.modifiers & RUBRAVIEW_MOD_SHIFT) ? RUBRAVIEW_PROPS_TAB_COUNT - 1 : 1)) % RUBRAVIEW_PROPS_TAB_COUNT; } while (!tab_shown(v, t));
                    v->tab = t;
                    v->page.scroll = 0.0;
                    v->page.bar = false;
                    app->props_dirty = true;
                }
                {
                    double dpi = rubraview_pal_window_dpi_scale(app->props_window);
                    if (key_is(k, "Down") && dlgpage_wheel(&v->page, -0.75, dpi)) app->props_dirty = true;
                    if (key_is(k, "Up") && dlgpage_wheel(&v->page, 0.75, dpi)) app->props_dirty = true;
                    if (key_is(k, "PageDown") && dlgpage_wheel(&v->page, -4.0, dpi)) app->props_dirty = true;
                    if (key_is(k, "PageUp") && dlgpage_wheel(&v->page, 4.0, dpi)) app->props_dirty = true;
                }
                break;
            }
            default:
                break;
        }
    }
    return handled;
}

/* ---- the action ---- */

/* Alt+Enter: what the right click would act on, as Explorer's menu takes it (explorermenu.c's targets). */
void props_action(app_state_t *app) {
    int32_t origin = PROPS_FROM_PAGE;
    size_t index = 0;
    if (app->picker_open) { explorer_targets_picker(app, app->picker.focus); origin = PROPS_FROM_PICKER; index = app->picker.focus; }
    else explorer_targets_page(app);
    props_show(app, (const char *const *)app->shell_targets, app->shell_target_count, origin, index);
}

/* The right-click list's Properties row: the files the list was opened for. */
void props_from_list(app_state_t *app, int32_t origin, size_t index) {
    props_show(app, (const char *const *)app->shell_targets, app->shell_target_count, origin, index);
}
