/*
 * Reading history and the window's layout kept, recent files, and opening: a source closed, an archive's names
 * read again, a RAR's password, what a folder lists, a playlist, a path, the next folder or archive
 * (§3.17, RV-106; RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* ---- reading history (§3.17) ---- */

void history_load(app_state_t *app) {
    /* The settings are read first: they decide portable mode, and the
       reader's settings apply from the first frame rather than from the
       first time the settings window is opened. */
    settings_read_file(app);

    proven_u8str_view_t appdata = PROVEN_LIT(".");
#ifdef _WIN32
    char appdata_utf8[1024];
    DWORD written = GetEnvironmentVariableA("APPDATA", appdata_utf8, (DWORD)sizeof(appdata_utf8));
    if (written > 0 && written < sizeof(appdata_utf8)) {
        appdata = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)appdata_utf8, .size = written };
    }
#endif
    app->history_path = rubraview_config_path(app->arena, app->config_mode,
                                              app->config_beside, appdata, PROVEN_LIT("history.ini"));
    app->layout_path = rubraview_config_path(app->arena, app->config_mode,
                                             app->config_beside, appdata, PROVEN_LIT("layout.ini"));
    proven_u8str_view_t text = rubraview_pal_fs_read_file(app->arena, app->history_path, 1024u * 1024u);
    app->history = rubraview_history_parse(app->arena, text);
    app->recent_path = rubraview_config_path(app->arena, app->config_mode,
                                             app->config_beside, appdata, PROVEN_LIT("recent.ini"));
    app->recent = rubraview_recent_parse(app->arena, rubraview_pal_fs_read_file(app->arena, app->recent_path, 256u * 1024u));

    /* D-43: the picker's favourites, beside the history. */
    app->favorites_path = rubraview_config_path(app->arena, app->config_mode,
                                                app->config_beside, appdata, PROVEN_LIT("favorites.ini"));
    app->favorites = rubraview_favorites_parse(app->arena,
                                               rubraview_pal_fs_read_file(app->arena, app->favorites_path, 64u * 1024u));
}

/* §3.6: "Positions persisted across sessions". The two floating boxes
   are not settings the reader edits in a dialog — they are where the
   hands left them — so they live in their own small file beside the
   reading history, under the same portable-or-AppData rule. */
static double ini_number(const rubraview_ini_doc_t *doc, proven_u8str_view_t section, proven_u8str_view_t key, double fallback) {
    const proven_u8str_view_t *text = rubraview_ini_get(doc, section, key);
    double value = 0.0;
    return (text && rubraview_parse_double_prefix(*text, &value, NULL)) ? value : fallback;
}

/* A box saved on a larger screen, or left near the edge of a window that
   then shrank, must not be out of reach: each anchor stays inside the
   window with room to grab it. A toolbox that is its own window is not
   in this window at all. */
static void boxes_keep_in_reach(app_state_t *app) {
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    /* the room an anchor takes, as toolbox_dock leaves it */
    double max_x = (double)win_w - 88.0 * dpi, max_y = (double)win_h - 48.0 * dpi;
    rubraview_box_t *boxes[] = { &app->toolbox, &app->menubox };
    for (size_t i = 0; i < 2; ++i) {
        rubraview_box_t *box = boxes[i];
        if (box->state == RUBRAVIEW_BOX_DETACHED) continue;
        if (box->anchor_x < 0.0) box->anchor_x = 0.0;
        if (box->anchor_y < 0.0) box->anchor_y = 0.0;
        if (max_x > 0.0 && box->anchor_x > max_x) box->anchor_x = max_x;
        if (max_y > 0.0 && box->anchor_y > max_y) box->anchor_y = max_y;
    }
}

/* Owner, 2026-10-11: the two boxes are always inside the window, and a
   window of another size keeps each the same part of the way across. An
   anchor that is not where this put it was moved — dragged, docked, sent
   home — and its new place is taken, brought inside. A toolbox that is a
   window of its own, or is being dragged (past the edge is how it becomes
   one), is left alone. */
void boxes_place(app_state_t *app) {
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) return;
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    rubraview_tile_metrics_t metrics = tile_metrics(dpi);
    double margin = 8.0 * dpi;
    rubraview_box_t *boxes[2] = { &app->toolbox, &app->menubox };
    for (int i = 0; i < 2; ++i) {
        rubraview_box_t *box = boxes[i];
        if (box->state == RUBRAVIEW_BOX_DETACHED || app->box_drag == box || box->headless) continue;
        rubraview_rect_t anchor = rubraview_box_anchor_rect(box, &metrics);
        if (!app->box_share_known[i] || box->anchor_x != app->box_put[i][0] || box->anchor_y != app->box_put[i][1]) {
            app->box_share[i][0] = rubraview_box_share_of(box->anchor_x, (double)win_w, anchor.width, margin);
            app->box_share[i][1] = rubraview_box_share_of(box->anchor_y, (double)win_h, anchor.height, margin);
            app->box_share_known[i] = true;
        }
        box->anchor_x = app->box_put[i][0] = floor(rubraview_box_share_place(app->box_share[i][0], (double)win_w, anchor.width, margin) + 0.5);
        box->anchor_y = app->box_put[i][1] = floor(rubraview_box_share_place(app->box_share[i][1], (double)win_h, anchor.height, margin) + 0.5);
    }
}

void layout_load(app_state_t *app) {
    if (app->layout_path.size == 0) return;
    proven_u8str_view_t text = rubraview_pal_fs_read_file(app->arena, app->layout_path, 8u * 1024u);
    if (text.size == 0) return;
    rubraview_ini_doc_t doc = rubraview_ini_parse(app->arena, text);

    struct { rubraview_box_t *box; const char *x_key, *y_key; } BOXES[] = {
        { &app->toolbox, "toolbox_x", "toolbox_y" },
        { &app->menubox, "menubox_x", "menubox_y" },
    };
    for (size_t i = 0; i < sizeof(BOXES) / sizeof(BOXES[0]); ++i) {
        BOXES[i].box->anchor_x = ini_number(&doc, PROVEN_LIT("boxes"), cstr(BOXES[i].x_key), BOXES[i].box->anchor_x);
        BOXES[i].box->anchor_y = ini_number(&doc, PROVEN_LIT("boxes"), cstr(BOXES[i].y_key), BOXES[i].box->anchor_y);
    }
    boxes_keep_in_reach(app);
    /* Since 0.0.48 the places are shares of the window; a file from before has pixels, which are taken once. */
    static const char *const SHARE_KEYS[2][2] = { { "toolbox_share_x", "toolbox_share_y" }, { "menubox_share_x", "menubox_share_y" } };
    for (int i = 0; i < 2; ++i) {
        double sx = ini_number(&doc, PROVEN_LIT("boxes"), cstr(SHARE_KEYS[i][0]), -1.0), sy = ini_number(&doc, PROVEN_LIT("boxes"), cstr(SHARE_KEYS[i][1]), -1.0);
        if (sx >= 0.0 && sx <= 1.0 && sy >= 0.0 && sy <= 1.0) {
            app->box_share[i][0] = sx;
            app->box_share[i][1] = sy;
            app->box_share_known[i] = true;
            app->box_put[i][0] = BOXES[i].box->anchor_x;   /* not "moved by hand": the share stands */
            app->box_put[i][1] = BOXES[i].box->anchor_y;
        } else if (rubraview_ini_get(&doc, PROVEN_LIT("boxes"), cstr(BOXES[i].x_key))) {
            app->box_share_known[i] = false;
        }
    }
    /* Where the boxes were left, not a setting: layout.ini, read like the positions. */
    if (ini_number(&doc, PROVEN_LIT("boxes"), PROVEN_LIT("toolbox_pinned"), 0.0) > 0.5) rubraview_box_set_pinned(&app->toolbox, true);

    /* D-33: the subtitle box, as fractions of the window. */
    if (ini_number(&doc, PROVEN_LIT("subtitle_box"), PROVEN_LIT("placed"), 0.0) > 0.5) {
        double left = ini_number(&doc, PROVEN_LIT("subtitle_box"), PROVEN_LIT("left"), -1.0);
        double bottom = ini_number(&doc, PROVEN_LIT("subtitle_box"), PROVEN_LIT("bottom"), -1.0);
        double width = ini_number(&doc, PROVEN_LIT("subtitle_box"), PROVEN_LIT("width"), -1.0);
        if (left >= 0.0 && left < 1.0 && bottom > 0.0 && bottom <= 1.0 && width > 0.0 && width <= 1.0) {
            app->subbox.placed = true;
            app->subbox.left = left;
            app->subbox.bottom = bottom;
            app->subbox.width = width;
        }
    }

    /* The settings window's frame; the PAL pulls it onto a screen when it is placed. */
    static const char *const FRAME_KEYS[4] = { "x", "y", "width", "height" };
    int32_t frame[4];
    for (size_t i = 0; i < 4; ++i) {
        double v = ini_number(&doc, PROVEN_LIT("settings_window"), cstr(FRAME_KEYS[i]), 0.0);
        frame[i] = (v > -100000.0 && v < 100000.0) ? (int32_t)v : 0;
    }
    if (frame[2] >= 200 && frame[3] >= 150) memcpy(app->settings_frame, frame, sizeof(frame));
}

void layout_save(app_state_t *app) {
    if (app->layout_path.size == 0) return;
    /* Through the configuration writer, so the file is in the INI and
       TOML subset (D-13). */
    rubraview_ini_doc_t doc = {0};
    boxes_place(app);   /* a box let go a moment ago has its share taken */
    rubraview_ini_set_float(app->arena, &doc, PROVEN_LIT("boxes"), PROVEN_LIT("toolbox_share_x"), app->box_share[0][0]);
    rubraview_ini_set_float(app->arena, &doc, PROVEN_LIT("boxes"), PROVEN_LIT("toolbox_share_y"), app->box_share[0][1]);
    rubraview_ini_set_float(app->arena, &doc, PROVEN_LIT("boxes"), PROVEN_LIT("menubox_share_x"), app->box_share[1][0]);
    rubraview_ini_set_float(app->arena, &doc, PROVEN_LIT("boxes"), PROVEN_LIT("menubox_share_y"), app->box_share[1][1]);
    rubraview_ini_set_int(app->arena, &doc, PROVEN_LIT("boxes"), PROVEN_LIT("toolbox_pinned"), app->toolbox.pinned ? 1 : 0);
    if (app->subbox.placed) {
        rubraview_ini_set_int(app->arena, &doc, PROVEN_LIT("subtitle_box"), PROVEN_LIT("placed"), 1);
        rubraview_ini_set_float(app->arena, &doc, PROVEN_LIT("subtitle_box"), PROVEN_LIT("left"), app->subbox.left);
        rubraview_ini_set_float(app->arena, &doc, PROVEN_LIT("subtitle_box"), PROVEN_LIT("bottom"), app->subbox.bottom);
        rubraview_ini_set_float(app->arena, &doc, PROVEN_LIT("subtitle_box"), PROVEN_LIT("width"), app->subbox.width);
    }
    if (app->settings_frame[2] > 0 && app->settings_frame[3] > 0) {
        rubraview_ini_set_int(app->arena, &doc, PROVEN_LIT("settings_window"), PROVEN_LIT("x"), app->settings_frame[0]);
        rubraview_ini_set_int(app->arena, &doc, PROVEN_LIT("settings_window"), PROVEN_LIT("y"), app->settings_frame[1]);
        rubraview_ini_set_int(app->arena, &doc, PROVEN_LIT("settings_window"), PROVEN_LIT("width"), app->settings_frame[2]);
        rubraview_ini_set_int(app->arena, &doc, PROVEN_LIT("settings_window"), PROVEN_LIT("height"), app->settings_frame[3]);
    }
    proven_u8str_view_t text = rubraview_ini_serialize(app->arena, &doc);
    if (text.size > 0) rubraview_pal_fs_write_file_atomic(app->layout_path, text);   /* D-99: the last viewer closed leaves its places */
}

/* D-104: Settings > Files > "Folders that are not written down". */
size_t recent_skip_dirs(const app_state_t *app, proven_u8str_view_t *out) {
    /* Each by its own name, so the settings check sees every row is read. */
    const rubraview_settings_t *st = &app->settings;
    size_t n = rubraview_folder_list_split(rubraview_settings_get_text(st, PROVEN_LIT("recent"), PROVEN_LIT("skip_dirs")), out + 8, RECENT_SKIP_DIRS - 8);
    out[0] = rubraview_settings_get_text(st, PROVEN_LIT("recent"), PROVEN_LIT("skip_dir_1"));
    out[1] = rubraview_settings_get_text(st, PROVEN_LIT("recent"), PROVEN_LIT("skip_dir_2"));
    out[2] = rubraview_settings_get_text(st, PROVEN_LIT("recent"), PROVEN_LIT("skip_dir_3"));
    out[3] = rubraview_settings_get_text(st, PROVEN_LIT("recent"), PROVEN_LIT("skip_dir_4"));
    out[4] = rubraview_settings_get_text(st, PROVEN_LIT("recent"), PROVEN_LIT("skip_dir_5"));
    out[5] = rubraview_settings_get_text(st, PROVEN_LIT("recent"), PROVEN_LIT("skip_dir_6"));
    out[6] = rubraview_settings_get_text(st, PROVEN_LIT("recent"), PROVEN_LIT("skip_dir_7"));
    out[7] = rubraview_settings_get_text(st, PROVEN_LIT("recent"), PROVEN_LIT("skip_dir_8"));
    return 8 + n;
}

bool recent_kept_out(const app_state_t *app, proven_u8str_view_t path) {
    proven_u8str_view_t dirs[RECENT_SKIP_DIRS];
    return rubraview_recent_is_kept_out(path, dirs, recent_skip_dirs(app, dirs));
}

/* D-104: recent.ini and this viewer's list are made one, under the lock
   every viewer takes: what another opened since is taken in, `opened`
   (if any) becomes the newest, or the list is emptied. The file is
   written only when that changed it. */
/* RFC-0006: the recent list is built in the idle half of its two, emptied first
   (the list before the one in use: nobody holds it now). */
static proven_arena_t *recent_arena(app_state_t *app) {
    if (!app->mem) return app->arena;
    proven_arena_t *a = rubraview_mem_recent_next(app->mem);
    if (!rubraview_region_ensure(&app->mem->recent[app->mem->recent_live ^ 1], 2u << 20)) return app->arena;
    return a;
}

/* ... and moved there whole before it becomes the list, so the half before can go. */
static void recent_settle(app_state_t *app, proven_arena_t *built, rubraview_recent_t *all) {
    if (!app->mem || built == app->arena) return;
    if (rubraview_recent_rehome(built, all)) { rubraview_mem_recent_swap(app->mem); return; }
    if (!rubraview_recent_rehome(app->arena, all)) app->mem->full = true;   /* no room there: kept with the session's */
}

void recent_sync(app_state_t *app, proven_u8str_view_t opened, bool clear) {
    if (app->recent_path.size == 0) return;
    proven_arena_t *ra = recent_arena(app);
    proven_u8str_view_t dirs[RECENT_SKIP_DIRS];
    size_t dir_count = recent_skip_dirs(app, dirs);
    bool locked = rubraview_pal_config_lock(CONFIG_LOCK_MS);
    proven_u8str_view_t before = rubraview_pal_fs_read_file(ra, app->recent_path, 256u * 1024u);
    rubraview_recent_t all = rubraview_recent_parse(ra, before);
    rubraview_recent_merge(ra, &all, &app->recent, RECENT_KEPT);
    if (clear) rubraview_recent_clear(&all, (int64_t)time(NULL));
    if (opened.size > 0) rubraview_recent_add(ra, &all, opened, (int64_t)time(NULL), RECENT_KEPT);   /* copied into its home below */
    (void)rubraview_history_drop_within(&all.list, dirs, dir_count);
    proven_u8str_view_t after = rubraview_recent_serialize(ra, &all);
    bool same = after.size == before.size && (after.size == 0 || memcmp(after.ptr, before.ptr, after.size) == 0);
    if (!same && (before.size > 0 || all.list.count > 0 || all.cleared > 0)) {
        (void)rubraview_pal_fs_write_file_atomic(app->recent_path, after);
    }
    if (locked) rubraview_pal_config_unlock();
    recent_settle(app, ra, &all);
    app->recent = all;
}

/* The owner (2026-10-17): files taken off the recent list (not off the disk),
   under the same lock, so the other viewers do not bring them back. */
void recent_forget(app_state_t *app, const proven_u8str_view_t *paths, size_t count) {
    if (app->recent_path.size == 0 || count == 0) return;
    proven_arena_t *ra = recent_arena(app);
    bool locked = rubraview_pal_config_lock(CONFIG_LOCK_MS);
    proven_u8str_view_t before = rubraview_pal_fs_read_file(ra, app->recent_path, 256u * 1024u);
    rubraview_recent_t all = rubraview_recent_parse(ra, before);
    rubraview_recent_merge(ra, &all, &app->recent, RECENT_KEPT);
    int64_t now = (int64_t)time(NULL);
    for (size_t i = 0; i < count; ++i) rubraview_recent_remove(ra, &all, paths[i], now);
    (void)rubraview_pal_fs_write_file_atomic(app->recent_path, rubraview_recent_serialize(ra, &all));
    if (locked) rubraview_pal_config_unlock();
    recent_settle(app, ra, &all);
    app->recent = all;
}

/* The reader opened `path` by name: a file, a book, a folder, a playlist. */
void recent_note(app_state_t *app, proven_u8str_view_t path) {
    if (path.size == 0) return;
    if (rubraview_settings_get(&app->settings, PROVEN_LIT("recent"), PROVEN_LIT("keep")) < 0.5) return;
    if (recent_kept_out(app, path)) return;
    recent_sync(app, path, false);
}

/* Of a set of files the reader named, the one it opened on is the one opened. */
void recent_note_shown(app_state_t *app) {
    int32_t at = current_page_index(app);
    if (at >= 0 && (size_t)at < page_count(app) && app->source.archive_path.size == 0) recent_note(app, app->source.pages[at].path);
}

void history_remember(app_state_t *app) {
    if (app->source_dir.size == 0 || page_count(app) == 0) return;
    /* Settings › Files › Remember the page: off, nothing is written down. */
    if (rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("reading_history")) < 0.5) return;

    int32_t current = current_page_index(app);
    if (current < 0) return;

    /* An archive is remembered by its own path; a folder by the folder. */
    proven_u8str_view_t key = app->source.archive_path.size > 0 ? app->source.archive_path : app->source_dir;
    if (recent_kept_out(app, key)) return;   /* D-104: nothing of such a folder is written down */
    rubraview_history_record(app->arena, &app->history, key, current, (int32_t)page_count(app),
                             (int64_t)time(NULL));   /* a date: pal_time is a monotonic clock, which restarts with the machine */
    rubraview_history_prune(&app->history, HISTORY_MAX_ENTRIES);
}

/* ---- opening ---- */

/* The source is let go of: a CB7's decoded block (heap), and the archive's
   mapping. Every way out of a source comes through here. */
void source_close(app_state_t *app) {
    page_job_stop(app);   /* the thread reads this source */
    rubraview_page_source_close(&app->source);
    rubraview_pal_fs_unmap(&app->archive_map);
    app->archive_bytes = PROVEN_LIT("");
}

/* ---- the open archive's names, read again (owner, 2026-10-01) ---- */

/* "shift-jis 나 중국, 대만에서 널리 사용되는 인코딩으로 ... 특정 파일 열 때마다
   일회성으로": the archive on screen is opened again with its names read in
   `codepage`, the same page kept on screen; the global setting is not
   touched, and the next archive goes back to it. Only names an archive
   stores without saying they are UTF-8 change (ZIP without the flag, RAR 4
   without Unicode); 7z's are always Unicode. */
void archive_names_reread(app_state_t *app, rubraview_codepage_t codepage) {
    if (app->source.kind == RUBRAVIEW_PAGE_SOURCE_FOLDER || app->source.archive_path.size == 0) {
        osd_say(app, TU8("names: only an archive's can be read again"));
        return;
    }
    proven_u8str_view_t path = app_keep(app, app->source.archive_path);
    int32_t current = current_page_index(app);
    size_t entry = current >= 0 && (size_t)current < page_count(app) ? app->source.pages[current].entry_index : SIZE_MAX;
    app->names_codepage = (int32_t)codepage;
    history_remember(app);
    unload_all_pages(app);
    if (!open_archive(app, path)) return;
    size_t start = 0;
    for (size_t i = 0; i < page_count(app); ++i) if (app->source.pages[i].entry_index == entry) { start = i; break; }
    app->resume_offer = false;
    finish_open(app, start);
    char line[512];
    proven_u8str_view_t label = rubraview_codepage_label(codepage);
    proven_u8str_view_t name = page_count(app) > 0 ? app->source.pages[start].name : PROVEN_LIT("");
    int n = snprintf(line, sizeof(line), T("names read as %.*s:  %.*s"), (int)label.size, label.ptr,
                     (int)(name.size < 300 ? name.size : 300), name.ptr);
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)(n < (int)sizeof(line) ? n : (int)sizeof(line) - 1) });
}

/* ---- a RAR's password (owner, 2026-10-01) ---- */

void secure_wipe(void *p, size_t n) {
    volatile unsigned char *v = (volatile unsigned char*)p;
    while (n--) *v++ = 0;
}

void password_remember(app_state_t *app, proven_u8str_view_t password) {
    if (password.size == 0 || password.size >= sizeof(app->passwords[0])) return;
    for (size_t i = 0; i < app->password_count; ++i) {
        if (app->password_lens[i] == password.size && memcmp(app->passwords[i], password.ptr, password.size) == 0) return;
    }
    size_t slot = app->password_count < 8 ? app->password_count++ : 7;   /* full: the newest takes the last place */
    secure_wipe(app->passwords[slot], sizeof(app->passwords[slot]));
    memcpy(app->passwords[slot], password.ptr, password.size);
    app->password_lens[slot] = password.size;
}

/* The box, empty, over a locked archive; `wrong` when the last one typed was not it. */
void password_ask(app_state_t *app, proven_u8str_view_t archive_path, bool wrong) {
    if (archive_path.size >= sizeof(app->password_for)) return;
    memcpy(app->password_for, archive_path.ptr, archive_path.size);
    app->password_for_len = archive_path.size;
    if (app->rename_active) rename_end(app);
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    app->rename_active = true;
    app->rename_is_password = true;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, true);
    osd_say(app, wrong ? TU8("wrong password") : TU8("this archive is locked: type its password, then Enter"));
}

/* Enter in the password box: the archive is opened again with it. */
void password_commit(app_state_t *app) {
    rename_insert(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->rename_composing, .size = app->rename_composing_length });
    app->rename_composing_length = 0;
    char typed[sizeof(app->rename_buffer)];
    proven_u8str_view_t text = rubraview_textedit_text(&app->rename_edit);
    size_t n = text.size < sizeof(typed) ? text.size : sizeof(typed) - 1;
    memcpy(typed, text.ptr, n);
    char path[sizeof(app->password_for)];
    memcpy(path, app->password_for, app->password_for_len);
    size_t path_len = app->password_for_len;
    rename_end(app);                                      /* wipes the box */
    if (n == 0) { secure_wipe(typed, sizeof(typed)); return; }
    app->open_password = typed;
    app->open_password_len = n;
    open_path(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)path, .size = path_len });
    app->open_password = NULL;
    app->open_password_len = 0;
    secure_wipe(typed, sizeof(typed));
}

bool open_archive(app_state_t *app, proven_u8str_view_t archive_path) {
    if (app->arc_job) return false;   /* D-88: the archive on screen is being written again */
    /* Leaving one archive for another gives back what the old one held;
       a CB7's decoded solid block is heap memory, not arena memory. */
    source_close(app);

    /* Mapped, not read into the 64 MB working arena: an archive over
       about 60 MB did not open at all, each one kept its bytes for the
       rest of the session, and nothing over 2 GB was read (owner,
       2026-09-28: comics of several GB). */
    if (!rubraview_pal_fs_map(archive_path, &app->archive_map)) return false;
    if (app->archive_map.size > (uint64_t)SIZE_MAX) { rubraview_pal_fs_unmap(&app->archive_map); return false; }
    proven_u8str_view_t bytes = { .ptr = (const proven_byte_t *)(const char*)app->archive_map.data, .size = (size_t)app->archive_map.size };

    app->archive_bytes = bytes;
    app->list_is_set = false;
    static char kinds_buf[512];   /* lasting: the source may keep the kinds it was opened with */
    proven_u8str_view_t given = proven_u8str_view_from_bytes(app->open_password ? app->open_password : "", app->open_password ? app->open_password_len : 0);
    rubraview_codepage_t names = app->names_codepage >= 0 ? (rubraview_codepage_t)app->names_codepage : app->archive_codepage;
    app->source = rubraview_page_source_from_archive_password(app->arena,
                                                              (const uint8_t*)bytes.ptr, bytes.size,
                                                              archive_path, kinds_with_text(kinds_buf, sizeof(kinds_buf), IMAGE_FILTER),
                                                              names, MAX_PAGE_BYTES,
                                                              PAGE_CACHE_BUDGET, given);
    /* Locked (owner, 2026-10-01): the passwords that worked this session
       first, then the reader is asked. */
    for (size_t i = 0; app->source.needs_password && given.size == 0 && i < app->password_count; ++i) {
        rubraview_page_source_close(&app->source);
        app->source = rubraview_page_source_from_archive_password(app->arena,
                                                                  (const uint8_t*)bytes.ptr, bytes.size,
                                                                  archive_path, kinds_with_text(kinds_buf, sizeof(kinds_buf), IMAGE_FILTER),
                                                                  names, MAX_PAGE_BYTES, PAGE_CACHE_BUDGET,
                                                                  (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->passwords[i], .size = app->password_lens[i] });
    }
    app->source_dir = rubraview_path_dirname(archive_path);
    if (app->source.needs_password) {
        password_ask(app, archive_path, given.size > 0);
        return false;
    }
    if (given.size > 0) password_remember(app, given);
    return app->source.page_count > 0;
}

/* ---- RV-106: what a folder lists ---- */

#define SUBFOLDER_DEPTH 8
#define SUBFOLDER_FILES 200000

typedef struct folder_files {
    rubraview_fs_entry_t *entries;
    size_t count, cap;
} folder_files_t;

/* Settings > Files > Show and Only names like: the kinds listed, and the names among them. */
static bool folder_lists(const app_state_t *app, proven_u8str_view_t name, bool filtered) {
    char kinds_buf[1024];
    const char *const KINDS[] = { (const char *)kinds_with_text(kinds_buf, sizeof(kinds_buf), IMAGE_FILTER ";" MEDIA_FILTER).ptr, IMAGE_FILTER, VIDEO_FILTER, AUDIO_FILTER };
    name = rubraview_path_basename(name);
    if (!filtered) return rubraview_glob_match_list(name, cstr(KINDS[0]));
    int show = (int)rubraview_folder_show_for((int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("show"))),
                                              app->folder_opened_kind,
                                              rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("same_kind")) > 0.5);
    if (!rubraview_glob_match_list(name, cstr(KINDS[show]))) return false;
    proven_u8str_view_t pattern = rubraview_settings_get_text(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("pattern"));
    return pattern.size == 0 || rubraview_glob_match_list(name, pattern);
}

/* The files of `dir` (and, with subfolders, of the folders under it), each
   named by its path from `root` so that a sort by name goes folder by folder. */
static void folder_gather(app_state_t *app, proven_u8str_view_t root, proven_u8str_view_t dir, int depth, bool deep, folder_files_t *out) {
    rubraview_fs_listing_t listing = rubraview_pal_fs_list_dir(app->arena, dir);
    for (size_t i = 0; i < listing.count && out->count < SUBFOLDER_FILES; ++i) {
        rubraview_fs_entry_t e = listing.entries[i];
        if (e.is_directory) {
            if (deep && depth < SUBFOLDER_DEPTH) folder_gather(app, root, e.path, depth + 1, true, out);
            continue;
        }
        if (out->count == out->cap) {
            size_t cap = out->cap ? out->cap * 2 : 256;
            rubraview_fs_entry_t *grown = (rubraview_fs_entry_t*)realloc(out->entries, cap * sizeof(*grown));
            if (!grown) return;
            out->entries = grown;
            out->cap = cap;
        }
        if (depth > 0 && e.path.size > root.size + 1) {
            size_t skip = root.size;
            if (e.path.ptr[skip] == '/' || e.path.ptr[skip] == '\\') skip++;
            e.name = (proven_u8str_view_t){ .ptr = e.path.ptr + skip, .size = e.path.size - skip };
        }
        out->entries[out->count++] = e;
    }
}

/* Another order each time: the fixed seed of the listing's shuffle is for tests. */
static void source_shuffle(app_state_t *app) {
    uint64_t state = (uint64_t)(rubraview_pal_time_now_seconds() * 1000000.0) ^ ((uint64_t)GetCurrentProcessId() << 32);
    for (size_t i = app->source.page_count; i > 1; --i) {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        size_t j = (size_t)((state >> 33) % (uint64_t)i);
        rubraview_page_ref_t tmp = app->source.pages[i - 1];
        app->source.pages[i - 1] = app->source.pages[j];
        app->source.pages[j] = tmp;
    }
}

/* `keep` is a file listed whatever the filters say: the one that was opened by name. */
static bool open_folder_keeping(app_state_t *app, proven_u8str_view_t dir, proven_u8str_view_t keep) {
    if (app->arc_job) return false;   /* D-88 */
    source_close(app);

    bool deep = g_cli_subfolders || rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("subfolders")) > 0.5;
    folder_files_t all = {0};
    folder_gather(app, dir, dir, 0, deep, &all);
    if (all.count == 0) { free(all.entries); return false; }

    /* The filters; when they leave nothing, the folder as it is — and a word about it. */
    size_t kept = 0;
    for (int pass = 0; pass < 2 && kept == 0; ++pass) {
        for (size_t i = 0; i < all.count; ++i) {
            const rubraview_fs_entry_t *e = &all.entries[i];
            if (folder_lists(app, e->name, pass == 0) ||
                (keep.size > 0 && rubraview_path_same(e->path, keep) && folder_lists(app, e->name, false))) {
                all.entries[kept++] = *e;
            }
        }
        if (kept > 0 && pass == 1) osd_say(app, TU8("nothing here passes Settings > Files > Show: everything is shown"));
    }
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, kept ? kept : 1, sizeof(rubraview_fs_entry_t));
    if (kept == 0 || !proven_is_ok(res.err)) { free(all.entries); return false; }
    rubraview_fs_entry_t *entries = (rubraview_fs_entry_t*)(void*)res.value.ptr;
    memcpy(entries, all.entries, kept * sizeof(*entries));
    free(all.entries);

    app->archive_bytes = PROVEN_LIT("");
    bool ascending = true;
    rubraview_sort_mode_t mode = folder_sort_mode(app, &ascending);
    /* By name through folders: folder by folder, each in its own order. */
    if (deep && (mode == RUBRAVIEW_SORT_NAME_NATURAL || mode == RUBRAVIEW_SORT_NAME_LEXICAL)) mode = RUBRAVIEW_SORT_PATH_NATURAL;
    app->list_is_set = false;
    rubraview_fs_listing_t listing = { .entries = entries, .count = kept };
    app->source = rubraview_page_source_from_listing(app->arena, &listing, PROVEN_LIT("*"), mode, ascending);
    if (mode == RUBRAVIEW_SORT_RANDOM) source_shuffle(app);
    app->source_dir = dir;
    return app->source.page_count > 0;
}

bool open_folder(app_state_t *app, proven_u8str_view_t dir) {
    return open_folder_keeping(app, dir, PROVEN_LIT(""));
}

/* The folder being read, listed again — its order or its filters changed —
   and back on the file it was on. A set of files and a book are left alone. */
void folder_relist(app_state_t *app) {
    if (page_count(app) == 0 || app->list_is_set || app->source.archive_path.size > 0 ||
        app->source.kind != RUBRAVIEW_PAGE_SOURCE_FOLDER || app->source_dir.size == 0 || app->arc_job) return;
    int32_t at = current_page_index(app);
    proven_u8str_view_t reading = at >= 0 ? app_keep(app, app->source.pages[at].path) : PROVEN_LIT("");
    proven_u8str_view_t dir = app_keep(app, app->source_dir);
    if (!open_folder_keeping(app, dir, reading)) return;
    int32_t page = reading.size > 0 ? rubraview_page_source_find(&app->source, reading) : -1;
    finish_open(app, page > 0 ? (size_t)page : 0);
}

/* A playlist file (`.m3u8`, `.m3u`, `.rvlist`): its files, in its order.
   A path that is not whole is from the playlist's own folder. */
bool path_is_playlist(proven_u8str_view_t path) {
    return rubraview_glob_match_list(rubraview_path_basename(path), PROVEN_LIT(LIST_FILTER));
}

/* RV-157: a list's text as UTF-8 — a .pls or .m3u from a station's site is often in its country's code page,
   or UTF-16. Into the arena; empty when it cannot be read. */
/* The text decoded to UTF-8, malloc'd; NULL when it is UTF-8 already (a BOM is the parser's to pass over) or cannot
   be. Any thread (RV-158: a list fetched by a station's job). */
char *list_text_decode(proven_u8str_view_t raw, size_t *len) {
    size_t bom = 0;
    char tag[96];
    const char *lang = rubraview_pal_process_ui_language(tag, sizeof(tag)) ? rubraview_lang_pick(tag, LANG_PACKS, 5) : NULL;
    rubraview_textenc_t enc = rubraview_textenc_guess((const uint8_t*)raw.ptr, raw.size, lang, &bom);
    *len = 0;
    if (enc == RUBRAVIEW_TEXTENC_UTF8) return NULL;
    if (enc == RUBRAVIEW_TEXTENC_UTF16LE || enc == RUBRAVIEW_TEXTENC_UTF16BE)
        return rubraview_textenc_utf16_to_utf8((const uint8_t*)raw.ptr + bom, raw.size - bom, enc == RUBRAVIEW_TEXTENC_UTF16BE, len);
    return rubraview_pal_text_decode((const uint8_t*)raw.ptr + bom, raw.size - bom, rubraview_textenc_codepage(enc), len);
}

static proven_u8str_view_t playlist_text_utf8(app_state_t *app, proven_u8str_view_t raw) {
    size_t len = 0;
    char *text = list_text_decode(raw, &len);
    if (!text) return raw;
    proven_u8str_view_t kept = app_keep(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)text, .size = len });
    free(text);
    return kept;
}

void open_playlist(app_state_t *app, proven_u8str_view_t path) {
    proven_u8str_view_t text = rubraview_pal_fs_read_file(app->arena, path, 8u * 1024u * 1024u);
    if (text.size == 0) { osd_say(app, TU8("the playlist is empty or could not be read")); return; }
    rubraview_playlist_t list = rubraview_playlist_parse_named(app->arena, rubraview_path_basename(path), playlist_text_utf8(app, text));
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, list.count ? list.count : 1, sizeof(rubraview_fs_entry_t));
    if (list.count == 0 || !proven_is_ok(res.err)) { osd_say(app, TU8("the playlist names no file")); return; }
    rubraview_fs_entry_t *entries = (rubraview_fs_entry_t*)(void*)res.value.ptr;
    proven_u8str_view_t dir = rubraview_path_dirname(path);
    size_t n = 0, missing = 0, stations = 0;
    for (size_t i = 0; i < list.count; ++i) {
        proven_u8str_view_t item = list.entries[i].path;
        if (item.size == 0) continue;
        /* RV-157: an internet radio station, by the title the list gives it */
        if (is_station_path(item)) {
            proven_u8str_view_t title = list.entries[i].title;
            entries[n++] = (rubraview_fs_entry_t){ .name = title.size > 0 ? title : rubraview_stream_display_name(item), .path = item };
            station_alts_note(&list.entries[i]);   /* RV-158: tried when it does not answer */
            stations++;
            continue;
        }
        if (rubraview_glob_match(item, PROVEN_LIT("*://*"))) { missing++; continue; }   /* another kind of address: not ours to open */
        bool whole = item.ptr[0] == '/' || item.ptr[0] == '\\' || (item.size > 2 && item.ptr[1] == ':');
        if (!whole) item = rubraview_path_join(app->arena, dir, item);
        rubraview_fs_entry_t entry;
        if (!rubraview_pal_fs_stat(app->arena, item, &entry) || entry.is_directory || !path_is_listed_kind(entry.path)) {
            missing++;
            continue;
        }
        entry.name = rubraview_path_basename(entry.path);
        entries[n++] = entry;
    }
    if (n == 0) { osd_say(app, TU8("none of the playlist's files is there")); return; }
    char line[128];
    if (stations > 0 && missing > 0) snprintf(line, sizeof(line), T("playlist: %zu files and stations (%zu stations), %zu not found"), n, stations, missing);
    else if (stations > 0) snprintf(line, sizeof(line), T("playlist: %zu files and stations (%zu stations)"), n, stations);
    else if (missing > 0) snprintf(line, sizeof(line), T("playlist: %zu files, %zu not found"), n, missing);
    else snprintf(line, sizeof(line), T("playlist: %zu files"), n);
    open_set_ordered(app, entries, n, cstr(line), true, PROVEN_LIT(""));
    if (page_count(app) > 0) {
        if (stations > 0) app->source_dir = dir;   /* a station has no folder: the list's own is where Rename and the rest look */
        recent_note(app, path);
    }
}

/* Finishes opening whichever source was just built: allocate the page
   table, apply any ComicInfo, lay out, and start the ring. */
void finish_open(app_state_t *app, size_t start_page) {
    media_close(app); /* the playing video belongs to the source being replaced */
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, page_count(app), sizeof(app_page_t));
    if (!proven_is_ok(res.err)) {
        app->source.page_count = 0;
        return;
    }
    app->pages = (app_page_t*)(void*)res.value.ptr;
    memset(app->pages, 0, page_count(app) * sizeof(app_page_t));
    app->layout_exempt = -1;       /* D-98: it named a page of the old source */
    app->pending_decode_count = 0; /* those indices named the old source's pages */

    app->page_cache = rubraview_lru_create(app->arena, page_count(app) + 8,
                                           app->cache_budget > 0 ? app->cache_budget : PAGE_CACHE_BUDGET);
    app->precache = rubraview_precache_create(app->jobs, &app->page_cache,
                                              page_count(app), ESTIMATED_PAGE_BYTES);
    /* Settings › Cache: how many pages are read ahead and kept behind. */
    app->precache.want_ahead = app->precache.lookahead = (size_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("cache"), PROVEN_LIT("lookahead")));
    app->precache.want_behind = app->precache.lookbehind = (size_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("cache"), PROVEN_LIT("lookbehind")));

    if (start_page >= page_count(app)) start_page = 0;
    ensure_page_loaded(app, (int32_t)start_page);
    rebuild_layout(app);

    /* §3.8.5: let the archive's own manifest set the reading direction
       and mark its covers before the first spread is chosen. */
    if (app->source.has_comicinfo && rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("comicinfo")) > 0.5) {
        rubraview_comicinfo_t info = rubraview_comicinfo_parse(app->arena, app->source.comicinfo_xml);
        proven_result_mem_mut_t infos_res =
            rubraview_arena_alloc_array(app->arena, page_count(app), sizeof(rubraview_page_info_t));
        if (proven_is_ok(infos_res.err)) {
            rubraview_page_info_t *infos = (rubraview_page_info_t*)(void*)infos_res.value.ptr;
            memset(infos, 0, page_count(app) * sizeof(rubraview_page_info_t));
            rubraview_comicinfo_apply(&info, &app->layout_opts, infos, page_count(app));
            app->comic_page_flags = infos;
            rebuild_layout(app);
        }
    }

    app->spread_index = spread_for_chosen_page(app, (int32_t)start_page);

    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    double scale = rubraview_pal_window_dpi_scale(app->window);
    bool strip_kept = app->filmstrip.visible;   /* the menu's toggle outlives a book */
    app->filmstrip = rubraview_filmstrip_create(page_count(app), FILMSTRIP_THUMB * scale, (double)win_w);
    app->filmstrip.visible = strip_kept;
    app->osd_size_page = -1;
    strip_cells_reset(app);
    app->strip_peek_until = 0.0;
    strip_follow(app, start_page);
    app->filmstrip.scroll_offset = app->strip_target;
    build_slides(app);
    update_precache(app);
    /* A file opened straight onto an animation: it plays from the start,
       not only once a page has been turned (found 2026-10-09, D-82). */
    animation_prepare(app);
    media_prepare(app);
    update_window_title(app);
}

void open_path(app_state_t *app, proven_u8str_view_t path) {
    if (app->arc_job) { osd_say(app, TU8("the archive is being written (Esc stops it)")); return; }
    if (!subs_may_leave(app)) return;
    /* The path may be a picker listing's, which goes two folders later;
       the source keeps it (its folder, its archive's path). */
    path = app_keep(app, path);
    app->names_codepage = -1;   /* a code page chosen for one archive's names stays with it */
    if (rubraview_glob_match_list(rubraview_path_basename(path), PROVEN_LIT("*.cue"))) {
        proven_u8str_view_t audio = cue_audio_path(app, path);
        if (audio.size == 0) { osd_say(app, TU8("the cue sheet's audio file is not beside it")); return; }
        path = app_keep(app, audio);
    }
    if (is_station_path(path)) {   /* RV-157: an address given on its own (the command line): a list of one station */
        proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, 1, sizeof(rubraview_fs_entry_t));
        if (!proven_is_ok(res.err)) return;
        rubraview_fs_entry_t *one = (rubraview_fs_entry_t*)(void*)res.value.ptr;
        *one = (rubraview_fs_entry_t){ .name = rubraview_stream_display_name(path), .path = path };
        open_set_ordered(app, one, 1, TU8("live: an internet radio station"), true, PROVEN_LIT(""));
        return;
    }
    rubraview_fs_entry_t entry;
    if (!rubraview_pal_fs_stat(app->arena, path, &entry)) return;
    if (!entry.is_directory && path_is_playlist(entry.path)) { open_playlist(app, entry.path); return; }   /* RV-106 */

    /* Remember where the reader was in whatever was open before. */
    history_remember(app);

    bool opened = false;
    /* The path is spelt as the caller gave it — backslashes from the
       shell, '/' from a folder listing — so every lookup by it compares
       with rubraview_path_same, never byte for byte. */
    proven_u8str_view_t key = entry.path;

    app->folder_opened_kind = 0;
    if (entry.is_directory) {
        opened = open_folder(app, entry.path);
        key = entry.path;
    } else if (rubraview_glob_match_list(rubraview_path_basename(entry.path), PROVEN_LIT(ARCHIVE_FILTER))) {
        opened = open_archive(app, entry.path);
    } else {
        /* `rubraview.exe test.jpg` has no directory part at all, and
           listing "" lists nothing. The folder it means is the one the
           command was run in. */
        proven_u8str_view_t folder = rubraview_path_dirname(entry.path);
        if (folder.size == 0) folder = PROVEN_LIT(".");
        /* RV-169: Show's numbers; an animated picture is a picture (its extension is among IMAGE_FILTER's). */
        proven_u8str_view_t base = rubraview_path_basename(entry.path);
        app->folder_opened_kind = rubraview_glob_match_list(base, PROVEN_LIT(IMAGE_FILTER)) ? 1
                                : rubraview_glob_match_list(base, PROVEN_LIT(VIDEO_FILTER)) ? 2
                                : rubraview_glob_match_list(base, PROVEN_LIT(AUDIO_FILTER)) ? 3 : 0;
        opened = open_folder_keeping(app, folder, entry.path);
        key = folder;
    }
    if (!opened) return;
    recent_note(app, entry.path);

    /* §3.17.1: reopening something read before starts where it stopped,
       rather than dropping the reader back on page one. */
    size_t start_page = 0;
    const rubraview_history_entry_t *seen =
        rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("reading_history")) > 0.5
            ? rubraview_history_find(&app->history, key) : NULL;
    if (seen && rubraview_history_should_offer_resume(seen)) {
        /* Settings › Files › Offer to resume: off, it simply opens there. */
        app->resume_offer = rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("resume_prompt")) > 0.5;
        app->resume_page = seen->page;
        if ((size_t)seen->page < app->source.page_count) start_page = (size_t)seen->page;
    } else {
        app->resume_offer = false;
    }

    /* A file that was opened directly wins over the remembered spot. */
    if (!entry.is_directory && app->source.kind == RUBRAVIEW_PAGE_SOURCE_FOLDER) {
        int32_t named = rubraview_page_source_find(&app->source, entry.path);
        if (named >= 0) {
            start_page = (size_t)named;
            app->resume_offer = false;
        }
    }
    /* --page=N (a restart asks for the page it was on): that page of a book, and no offer to resume; a folder's file is named itself. */
    if (app->launch_page > 0) {
        if (app->source.kind != RUBRAVIEW_PAGE_SOURCE_FOLDER && (size_t)(app->launch_page - 1) < app->source.page_count) {
            start_page = (size_t)(app->launch_page - 1);
            app->resume_offer = false;
        }
        app->launch_page = 0;
    }

    finish_open(app, start_page);
}

/* §3.8.1 point 4: reaching the end of a volume continues into the next
   archive in the same directory. */
bool open_sibling_archive(app_state_t *app, bool forward) {
    if (app->source.archive_path.size == 0 || app->source_dir.size == 0) return false;

    rubraview_fs_listing_t listing = rubraview_pal_fs_list_dir(app->arena, app->source_dir);
    proven_u8str_view_t next = rubraview_page_source_sibling_archive(app->arena, &listing,
                                                          app->source.archive_path, forward);
    if (next.size == 0) return false;
    if (!subs_may_leave(app)) return true;   /* held: it said why */
    open_path(app, next);
    if (!forward && app->layout.count > 1) go_to_spread(app, app->layout.count - 1);   /* going back: its last page */
    return true;
}

/* The folder beside this one, after or before it, that has something to show (owner, 2026-10-14). */
bool open_sibling_folder(app_state_t *app, bool forward) {
    proven_u8str_view_t dir = app->source_dir;
    if (dir.size == 0) return false;
    proven_u8str_view_t parent = rubraview_path_dirname(dir);
    if (parent.size == 0 || rubraview_path_same(parent, dir)) return false;
    rubraview_fs_listing_t listing = rubraview_pal_fs_list_dir(app->arena, parent);
    proven_u8str_view_t beside[64];
    size_t n = rubraview_page_source_sibling_folders(app->arena, &listing, dir, forward, beside, 64);
    for (size_t i = 0; i < n; ++i) {
        rubraview_fs_listing_t inside = rubraview_pal_fs_list_dir(app->arena, beside[i]);
        bool shows = false;
        for (size_t k = 0; k < inside.count && !shows; ++k) {
            shows = !inside.entries[k].is_directory && path_is_listed_kind(inside.entries[k].path);
        }
        if (!shows) continue;   /* empty, or nothing the viewer opens: the one after it */
        if (!subs_may_leave(app)) return true;
        open_path(app, beside[i]);
        if (!forward && app->layout.count > 1) go_to_spread(app, app->layout.count - 1);
        char line[400];
        proven_u8str_view_t name = rubraview_path_basename(beside[i]);
        int m = snprintf(line, sizeof(line), forward ? T("next folder: %.*s") : T("previous folder: %.*s"), (int)(name.size < 300 ? name.size : 300), name.ptr);
        if (m > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)m < sizeof(line) ? (size_t)m : sizeof(line) - 1 });
        return true;
    }
    return false;
}
