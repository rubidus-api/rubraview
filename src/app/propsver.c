/*
 * RV-281 phase 3 (owner, 2026-11-14: "... 이전버젼의 파일 속성 윈도우의 모든 기능들을 자체 구현"): the properties window's
 * Previous Versions tab. A file as its volume's shadow copies hold it (restore points, backups' snapshots), read through
 * the shadows' devices, which the user may read without being administrator (VM spike, 2026-11-19); newest first, one of
 * each, none the same as the file now. Open shows a copy of the version made in %TEMP%\\rubraview-versions (in Rubraview when it shows the kind,
 * else in its Windows program); Copy... copies it to a folder chosen; Restore copies it over the file through the copy job, whose window
 * asks before anything is replaced (the file replaced goes to the Recycle Bin).
 */
#include "app.h"
#include "rubraview/pal/pal_file_dialog.h"
#include "rubraview/filemanage.h"

#include "propsview.h"

/* One file, not a folder: a folder's versions are left to Windows' own window. */
bool propsver_tab(const props_view_t *v) {
    return v->count == 1 && v->files && !v->files[0].is_folder;
}

static void versions_read(app_state_t *app) {
    props_view_t *v = V(app);
    v->versions_read = true;
    v->version_sel = -1;
    v->version_count = rubraview_pal_fileprops_versions(v->paths[0], v->versions, PROPS_VERSIONS_MAX, 1500);
    v->version_count = rubraview_props_versions_tidy(v->versions, v->version_count, v->files[0].modified, v->files[0].size);
}

double propsver_draw(app_state_t *app, double y) {
    props_view_t *v = V(app);
    rubraview_renderer_t *r = app->props_renderer;
    double dpi = rubraview_pal_window_dpi_scale(app->props_window);
    double w = pw_content_width(app);
    if (!v->versions_read) versions_read(app);
    double lx = 18.0 * dpi, avail = w - 2.0 * lx;
    if (avail < 60.0 * dpi) avail = 60.0 * dpi;
    const char *intro = T("Previous versions come from restore points and backups' copies of the drive.");
    double mw = 0.0, mh = 20.0 * dpi;
    (void)rubraview_pal_render_measure_text_wrapped(r, proven_u8str_view_from_cstr(intro), 12.0 * dpi, avail, &mw, &mh);
    pw_text_at(r, intro, (rubraview_pal_rect_t){ lx, y, avail, mh + 2.0 * dpi }, 12.0 * dpi, PW_DIM);
    y += mh + 8.0 * dpi;
    double name_w = avail * 0.42, when_x = lx + name_w, when_w = avail * 0.34, size_x = when_x + when_w, size_w = avail * 0.24;
    char fit[300];
    rubraview_pal_render_draw_text(r, fit_text_measured(r, fit, sizeof(fit), proven_u8str_view_from_cstr(T("Name")), name_w - 6.0 * dpi, 12.0 * dpi),
                                   (rubraview_pal_rect_t){ lx + 6.0 * dpi, y, name_w - 6.0 * dpi, 20.0 * dpi }, 12.0 * dpi, PW_HEADING, RUBRAVIEW_TEXT_LEFT);
    rubraview_pal_render_draw_text(r, fit_text_measured(r, fit, sizeof(fit), proven_u8str_view_from_cstr(T("Date modified")), when_w - 4.0 * dpi, 12.0 * dpi),
                                   (rubraview_pal_rect_t){ when_x, y, when_w - 4.0 * dpi, 20.0 * dpi }, 12.0 * dpi, PW_HEADING, RUBRAVIEW_TEXT_LEFT);
    rubraview_pal_render_draw_text(r, fit_text_measured(r, fit, sizeof(fit), proven_u8str_view_from_cstr(T("Size")), size_w, 12.0 * dpi),
                                   (rubraview_pal_rect_t){ size_x, y, size_w, 20.0 * dpi }, 12.0 * dpi, PW_HEADING, RUBRAVIEW_TEXT_LEFT);
    y += 24.0 * dpi;
    double rh = 24.0 * dpi;
    proven_u8str_view_t name = rubraview_path_basename(proven_u8str_view_from_cstr(v->paths[0]));
    char nm[300], when[64], size[64];
    (void)rubraview_fmt(nm, sizeof nm, "{}", PROVEN_ARG(name));
    if (v->version_count == 0) {
        const char *none = T("There are no previous versions available.");
        (void)rubraview_pal_render_measure_text_wrapped(r, proven_u8str_view_from_cstr(none), 13.0 * dpi, avail, &mw, &mh);
        pw_text_at(r, none, (rubraview_pal_rect_t){ lx + 6.0 * dpi, y + 4.0 * dpi, avail - 6.0 * dpi, mh + 2.0 * dpi }, 13.0 * dpi, PW_TEXT);
        y += mh + 12.0 * dpi;
    }
    for (size_t i = 0; i < v->version_count; ++i) {
        rubraview_pal_rect_t line = { lx, y, avail, rh };
        v->version_rect[i] = line;
        if ((int32_t)i == v->version_sel) rubraview_pal_render_fill_rect(r, line, 0xFF2D4A6Bu, 3.0 * dpi);
        else if (pal_rect_has(line, app->props_px, app->props_py)) rubraview_pal_render_fill_rect(r, line, 0x18FFFFFFu, 3.0 * dpi);
        info_local_time(when, sizeof when, v->versions[i].modified);
        rubraview_info_bytes(size, sizeof size, v->versions[i].size);
        rubraview_pal_render_draw_text(r, fit_text_measured(r, fit, sizeof(fit), proven_u8str_view_from_cstr(nm), name_w - 10.0 * dpi, 13.0 * dpi),
                                       (rubraview_pal_rect_t){ lx + 6.0 * dpi, y + 2.0 * dpi, name_w - 10.0 * dpi, rh }, 13.0 * dpi, PW_TEXT, RUBRAVIEW_TEXT_LEFT);
        rubraview_pal_render_draw_text(r, fit_text_measured(r, fit, sizeof(fit), proven_u8str_view_from_cstr(when), when_w - 6.0 * dpi, 13.0 * dpi),
                                       (rubraview_pal_rect_t){ when_x, y + 2.0 * dpi, when_w - 6.0 * dpi, rh }, 13.0 * dpi, PW_TEXT, RUBRAVIEW_TEXT_LEFT);
        rubraview_pal_render_draw_text(r, fit_text_measured(r, fit, sizeof(fit), proven_u8str_view_from_cstr(size), size_w - 4.0 * dpi, 13.0 * dpi),
                                       (rubraview_pal_rect_t){ size_x, y + 2.0 * dpi, size_w - 4.0 * dpi, rh }, 13.0 * dpi, PW_TEXT, RUBRAVIEW_TEXT_LEFT);
        y += rh;
    }
    bool one = v->version_sel >= 0 && (size_t)v->version_sel < v->version_count;
    const int bids[3] = { PB_VER_OPEN, PB_VER_COPY, PB_VER_RESTORE };
    const char *blbls[3] = { T("Open"), T("Copy..."), T("Restore...") };
    const bool bena[3] = { one, one, one };
    y += 8.0 * dpi + pw_buttons_row(app, bids, blbls, NULL, bena, 3, lx, w - lx, y + 8.0 * dpi, 76.0 * dpi, 26.0 * dpi);
    return y;
}

bool propsver_press(app_state_t *app, double x, double y) {
    props_view_t *v = V(app);
    for (size_t i = 0; i < v->version_count; ++i)
        if (v->version_rect[i].width > 0 && pal_rect_has(v->version_rect[i], x, y)) {
            v->version_sel = (int32_t)i;
            app->props_dirty = true;
            return true;
        }
    return false;
}

static char g_version_from[1200];   /* RV-314: the version whose folder is being chosen */

static void version_dir_chosen(app_state_t *app, void *ctx, bool accepted, const proven_u8str_view_t *paths, size_t count, uint32_t filter) {
    (void)ctx; (void)filter;
    if (!accepted || count == 0 || !g_version_from[0]) return;
    proven_u8str_view_t from = proven_u8str_view_from_cstr(g_version_from);
    bool started = filejob_start(app, paths[0], &from, 1, false, -1, false);
    if (app->props_open) {
        pw_say(V(app), started ? T("copying the version: its window shows how it goes") : T("the copy could not be started"));
        app->props_dirty = true;
    }
    g_version_from[0] = '\0';
}

bool propsver_button(app_state_t *app, int which) {
    props_view_t *v = V(app);
    if (v->version_sel < 0 || (size_t)v->version_sel >= v->version_count) return true;
    proven_u8str_view_t from = proven_u8str_view_from_cstr(v->versions[v->version_sel].path);
    if (which == PB_VER_OPEN) {
        /* a copy in %TEMP% first: the viewer's folder listing and other programs do not take the shadow's device path
           (VM T483); the copy is named as the file, in a folder of its shadow's number */
        proven_u8str_view_t name = rubraview_path_basename(from);
        char folder[96], whole[1200];
        (void)rubraview_fmt(folder, sizeof folder, "%TEMP%\\rubraview-versions\\{}", PROVEN_ARG(v->versions[v->version_sel].shadow));
        proven_u8str_view_t dir = rubraview_pal_fs_expand_env(app->arena, proven_u8str_view_from_cstr(folder));
        int k = rubraview_fmt(whole, sizeof whole, "{}\\{}", PROVEN_ARG(dir), PROVEN_ARG(name));
        if (dir.size == 0 || k <= 0 || (size_t)k >= sizeof whole || !rubraview_pal_fs_make_dirs(dir) ||
            !rubraview_pal_fs_copy(from, proven_u8str_view_from_cstr(whole))) {
            pw_say(v, T("the version could not be copied to open it"));
            return true;
        }
        proven_u8str_view_t copy = app_keep(app, proven_u8str_view_from_cstr(whole));
        if (picker_viewer_opens(name)) {
            props_close(app);   /* the viewer shows it */
            open_path(app, copy);
            return false;
        }
        if (!rubraview_pal_shell_open_file(copy, (int)rubraview_shell_launch_kind(name))) pw_say(v, T("Windows would not open it"));
        return true;
    }
    proven_u8str_view_t dir;
    if (which == PB_VER_COPY) {   /* RV-314: Rubraview's own folder window; the copy starts when it answers */
        (void)rubraview_fmt(g_version_from, sizeof g_version_from, "{}", PROVEN_ARG(from));
        choose_opts_t o = { .mode = RUBRAVIEW_CHOOSE_FOLDER, .title = T("Copy the version to"),
                            .start_dir = rubraview_path_dirname(proven_u8str_view_from_cstr(v->paths[0])),
                            .over = rubraview_pal_window_native_handle(app->props_window) };
        (void)choose_ask(app, &o, version_dir_chosen, NULL);
        return true;
    } else {
        dir = rubraview_path_dirname(proven_u8str_view_from_cstr(v->paths[0]));   /* over the file: the job asks first */
    }
    if (!filejob_start(app, dir, &from, 1, false, -1, false)) { pw_say(v, T("the copy could not be started")); return true; }
    pw_say(v, which == PB_VER_COPY ? T("copying the version: its window shows how it goes")
                                   : T("restoring: the copy's window asks before the file is replaced"));
    /* the list stays as read (a restore's file is read again the next time the window opens) */
    return true;
}
