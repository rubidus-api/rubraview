/*
 * The list windows: the playlist (its filter, order, saving and editing), recent files, favourites,
 * and the file list of an archive (RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* ---- The playlist window (owner, 2026-09-29) ---- */

/* RV-197: the floating lists stay below our own title bar, pinned or shown on hover (it is drawn over them), so
   their own title and buttons are never under it. */
static double list_view_top(const app_state_t *app) {
    return app->frameless && !app->chrome_hidden ? app->titlebar.height : 0.0;
}

/* Places the floating list; while Edit is open, left of its panel and
   below its curve box, so the list's bar ("Edit to all") is not under them. */
void listwin_place_clear(app_state_t *app, double win_w, double win_h) {
    rubraview_listwin_t *lw = &app->listwin;
    lw->view_top = list_view_top(app);
    double view_w = win_w;
    if (edit_panel_open(app) && !app->edit_window && app->panel.open && app->panel.bounds.x > win_w * 0.3 && app->panel.bounds.x < win_w)
        view_w = app->panel.bounds.x - 4.0;
    rubraview_listwin_place(lw, view_w, win_h);
    if (!edit_panel_open(app) || app->edit_window) return;
    rubraview_pal_rect_t curve = curve_widget_rect(app);
    double below = curve.y + curve.height + 4.0;
    if (curve.width <= 0.0 || lw->y >= below || curve.x >= lw->x + lw->width || curve.x + curve.width <= lw->x) return;
    if (below > win_h * 0.6) return;   /* no room under it: leave the list where it is */
    lw->y = below;
    if (lw->y + lw->height > win_h) lw->height = win_h - lw->y;
}

/* RV-197: a floating list window's maximize button — the whole view below a pinned title bar, or back. */
void listwin_maximize_toggle(app_state_t *app, rubraview_listwin_t *lw) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    lw->view_top = list_view_top(app);
    rubraview_listwin_toggle_maximized(lw, (double)w, (double)h);
    if (lw == &app->listwin) listwin_place_clear(app, (double)w, (double)h);
}

void draw_listwin_maximize(app_state_t *app, rubraview_renderer_t *rr, const rubraview_listwin_t *lw) {
    listwin_sign_button(app, rr, rubraview_listwin_maximize_rect(lw), lw->maximized ? RUBRAVIEW_ICON_RESTORE : RUBRAVIEW_ICON_MAXIMIZE, "[]",
                        lw->title_height * 0.4, false);
}

void listwin_toggle(app_state_t *app) {
    app->listwin.open = !app->listwin.open;
    if (!app->listwin.open) return;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    listwin_place_clear(app, (double)w, (double)h);
    app->listwin_shown_page = -1;   /* the next frame scrolls to the file on screen */
}

/* A press on the window: true when it took it. */
/* ---- D-111: the list changed from its window ---- */

/* A book's pages are not a list of files: only a folder's files or a
   playlist can be reordered, taken out of the list or added to. */
bool list_editable(const app_state_t *app) {
    return page_count(app) > 0 && app->source.archive_path.size == 0;
}

/* The window's shape for what is open, and a check mark for every page. */
void listwin_sync(app_state_t *app) {
    app->listwin.editable = list_editable(app);
    app->listwin.tools = app->files_list.tools = true;   /* 1.25.0 */
    size_t count = page_count(app);
    if (app->list_checked_count != count) {
        free(app->list_checked);
        app->list_checked = (bool*)calloc(count ? count : 1, sizeof(bool));
        app->list_checked_count = app->list_checked ? count : 0;
    }
}

/* ---- 1.25.0 (owner, 2026-10-20: "필터 에디트 박스(버튼 포함), 목록 정렬 ..., 목록 내 특정 확장자 파일만 보기"):
   the rows a list shows are a view of its pages. Without a filter or a hidden extension
   row `i` is page `i`; with one, rows may not be carried (their order is the pages'). ---- */

bool list_ext_is_off(const app_state_t *app, proven_u8str_view_t ext) {
    for (size_t k = 0; k < app->list_ext_off_n; ++k) {
        proven_u8str_view_t off = cstr(app->list_ext_off[k]);
        if (proven_u8str_view_eq_ascii_ci(off, ext)) return true;
    }
    return false;
}

bool list_viewed(const app_state_t *app) {
    return app->list_filter[0] != '\0' || app->list_ext_off_n > 0;
}

static void list_map_build(app_state_t *app) {
    size_t count = page_count(app);
    if (app->list_map_pages == (const void*)app->source.pages && app->list_map_count == count &&
        app->list_map_gen == app->list_view_gen && (app->list_map || count == 0)) return;
    if (app->list_map_cap < count || !app->list_map) {
        size_t *grown = (size_t*)realloc(app->list_map, (count ? count : 1) * sizeof(size_t));
        if (!grown) { app->list_map_n = 0; return; }
        app->list_map = grown;
        app->list_map_cap = count ? count : 1;
    }
    proven_u8str_view_t part = cstr(app->list_filter);
    size_t n = 0;
    for (size_t i = 0; i < count; ++i) {
        proven_u8str_view_t name = page_display_name(app, i);
        if (app->list_ext_off_n > 0) {
            proven_u8str_view_t e = rubraview_path_ext(app->source.pages[i].path);
            if (list_ext_is_off(app, e.size > 0 ? e : PROVEN_LIT("."))) continue;
        }
        if (part.size > 0 && !name_holds(name, part)) continue;
        app->list_map[n++] = i;
    }
    app->list_map_n = n;
    app->list_map_pages = (const void*)app->source.pages;
    app->list_map_count = count;
    app->list_map_gen = app->list_view_gen;
}

/* How many rows; the page at a row (SIZE_MAX past them); the row of a page (SIZE_MAX: not shown). */
size_t list_rows(app_state_t *app) {
    if (!list_viewed(app)) return page_count(app);
    list_map_build(app);
    return app->list_map_n;
}

size_t list_page(app_state_t *app, size_t row) {
    if (!list_viewed(app)) return row < page_count(app) ? row : SIZE_MAX;
    list_map_build(app);
    return row < app->list_map_n ? app->list_map[row] : SIZE_MAX;
}

size_t list_row_of(app_state_t *app, size_t page) {
    if (!list_viewed(app)) return page < page_count(app) ? page : SIZE_MAX;
    list_map_build(app);
    for (size_t r = 0; r < app->list_map_n; ++r) if (app->list_map[r] == page) return r;
    return SIZE_MAX;
}

void list_view_changed(app_state_t *app) {
    app->list_view_gen++;
    app->listwin.first = 0;
    app->files_list.first = 0;
    app->files_dirty = true;
    app->listwin_shown_page = -1;   /* the file on screen brought into view again */
    app->files_followed = -1;
}

void list_filter_set(app_state_t *app, proven_u8str_view_t text) {
    size_t keep = text.size < sizeof(app->list_filter) ? text.size : sizeof(app->list_filter) - 1;
    while (keep > 0 && keep < text.size && ((unsigned char)text.ptr[keep] & 0xC0u) == 0x80u) --keep;   /* whole characters */
    if (keep == strlen(app->list_filter) && memcmp(app->list_filter, text.ptr, keep) == 0) return;
    memcpy(app->list_filter, text.ptr, keep);
    app->list_filter[keep] = '\0';
    list_view_changed(app);
}

/* The filter typed: in the viewer's window in its box (the rows follow each letter); in the
   file window in the tools row itself, with that window's keys. */
static void list_filter_begin(app_state_t *app, bool in_files) {
    if (in_files) {
        app->list_filter_in_files = true;
        rubraview_pal_window_text_input(app->files_window, true);
        app->files_dirty = true;
        return;
    }
    if (app->rename_active) rename_end(app);
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    rubraview_textedit_set(&app->rename_edit, cstr(app->list_filter));
    rubraview_textedit_select_all(&app->rename_edit);
    app->rename_active = true;
    app->rename_is_listfilter = true;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, true);
}

void list_filter_follow(app_state_t *app) {
    if (!app->rename_active || !app->rename_is_listfilter) return;
    list_filter_set(app, rubraview_textedit_text(&app->rename_edit));
}

void list_filter_files_end(app_state_t *app) {
    if (!app->list_filter_in_files) return;
    app->list_filter_in_files = false;
    if (app->files_window) rubraview_pal_window_text_input(app->files_window, false);
    app->files_dirty = true;
}

size_t list_checked_total(const app_state_t *app) {
    size_t n = 0;
    for (size_t i = 0; i < app->list_checked_count; ++i) n += app->list_checked[i] ? 1 : 0;
    return n;
}

/* The pages become `n` files: `order[i]` is the page now at `i`, or
   SIZE_MAX for the next of the `added` files. The page being read stays
   on screen — the one after it when it left the list — and a film or a
   song goes on from where it was. */
void list_apply(app_state_t *app, const size_t *order, size_t n, const rubraview_fs_entry_t *added, const char *said) {
    size_t have = page_count(app);
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, n ? n : 1, sizeof(rubraview_fs_entry_t));
    bool *checked = (bool*)calloc(n ? n : 1, sizeof(bool));
    if (n == 0 || !proven_is_ok(res.err) || !checked) { free(checked); return; }
    rubraview_fs_entry_t *entries = (rubraview_fs_entry_t*)(void*)res.value.ptr;

    int32_t at = current_page_index(app);
    proven_u8str_view_t start = PROVEN_LIT("");
    bool reading_stays = false;
    size_t next_added = 0, after = SIZE_MAX;
    for (size_t i = 0; i < n; ++i) {
        if (order[i] == SIZE_MAX) {
            entries[i] = added[next_added++];
            continue;
        }
        size_t old = order[i];
        entries[i] = (rubraview_fs_entry_t){ .name = app->source.pages[old].name, .path = app->source.pages[old].path };
        checked[i] = old < app->list_checked_count && app->list_checked[old];
        if (at >= 0 && old == (size_t)at) { start = entries[i].path; reading_stays = true; }
        if (at >= 0 && old > (size_t)at && after == SIZE_MAX) after = i;
    }
    if (!reading_stays) start = entries[after != SIZE_MAX ? after : n - 1].path;

    bool playing = reading_stays && app->media != NULL && app->media_page == at && at >= 0 && (size_t)at < have;
    double position = playing ? play_position(app) : 0.0;
    bool paused = app->media_paused;
    size_t first = app->listwin.first;

    open_set_ordered(app, entries, n, cstr(said), true, start);
    if (page_count(app) == n) {
        free(app->list_checked);
        app->list_checked = checked;
        app->list_checked_count = n;
    } else {
        free(checked);
    }
    app->listwin.first = first;                              /* the rows stay where they were */
    rubraview_listwin_scroll(&app->listwin, 0, page_count(app));
    app->listwin_shown_page = current_page_index(app);
    if (playing && app->media && position > 1.0) {
        play_seek(app, position);
        if (paused && !app->media_paused) media_toggle_pause(app);
    }
}

/* The rows marked in `gone` leave the list; the files stay on the disk. */
void list_remove(app_state_t *app, const bool *gone) {
    size_t count = page_count(app), n = 0;
    size_t *order = (size_t*)rubraview_malloc_product((count ? count : 1), sizeof(size_t), 1);
    if (!order) return;
    for (size_t i = 0; i < count; ++i) if (!gone[i]) order[n++] = i;
    if (n == count) {
        osd_say(app, TU8("check the rows to take out of the list first"));
    } else if (n == 0) {
        osd_say(app, TU8("nothing taken out: a list cannot be left empty (uncheck one row)"));
    } else {
        char line[128];
        snprintf(line, sizeof(line), T("%zu out of the list (%zu in it); the files are where they were"), count - n, n);
        list_apply(app, order, n, NULL, line);
    }
    free(order);
}

/* The rows marked in `moving` go, together, before what is row `to`. */
static void list_move(app_state_t *app, const bool *moving, size_t to) {
    size_t count = page_count(app);
    size_t *order = (size_t*)rubraview_malloc_product((count ? count : 1), sizeof(size_t), 1);
    if (!order) return;
    if (rubraview_list_move(order, count, moving, to)) list_apply(app, order, count, NULL, T("the list's order changed"));
    free(order);
}

/* Files from outside go into the list before what is row `to`: those
   the viewer opens, that are not in it already. */
void list_insert_paths(app_state_t *app, const proven_u8str_view_t *paths, size_t count, size_t to) {
    size_t have = page_count(app);
    if (to > have) to = have;
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, count ? count : 1, sizeof(rubraview_fs_entry_t));
    size_t *order = (size_t*)rubraview_malloc_product((have + count + 1), sizeof(size_t), 1);
    if (!proven_is_ok(res.err) || !order) { free(order); return; }
    rubraview_fs_entry_t *added = (rubraview_fs_entry_t*)(void*)res.value.ptr;
    size_t n = 0, skipped = 0;
    for (size_t i = 0; i < count; ++i) {
        rubraview_fs_entry_t entry;
        if (!rubraview_pal_fs_stat(app->arena, paths[i], &entry) || entry.is_directory || !path_is_listed_kind(entry.path)) {
            skipped++;
            continue;
        }
        bool there = false;
        for (size_t k = 0; k < have && !there; ++k) there = rubraview_path_same(app->source.pages[k].path, entry.path);
        for (size_t k = 0; k < n && !there; ++k) there = rubraview_path_same(added[k].path, entry.path);
        if (there) { skipped++; continue; }
        entry.path = app_keep(app, entry.path);
        entry.name = rubraview_path_basename(entry.path);
        added[n++] = entry;
    }
    if (n == 0) {
        osd_say(app, TU8("nothing added: those are in the list already, or not files the viewer opens"));
        free(order);
        return;
    }
    size_t m = 0;
    for (size_t i = 0; i < to; ++i) order[m++] = i;
    for (size_t i = 0; i < n; ++i) order[m++] = SIZE_MAX;
    for (size_t i = to; i < have; ++i) order[m++] = i;
    char line[128];
    if (skipped > 0) snprintf(line, sizeof(line), T("%zu added at row %zu (%zu left out)"), n, to + 1, skipped);
    else snprintf(line, sizeof(line), T("%zu added at row %zu"), n, to + 1);
    list_apply(app, order, m, added, line);
    free(order);
}

/* Settings > Files: a saved list names its files from its own folder, or whole. */
bool list_saves_whole_paths(const app_state_t *app) {
    return lround(rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("list_paths"))) == 1;
}

static const rubraview_file_filter_t LIST_FILTERS[] = {
    { "Playlist (*.m3u8)", "*.m3u8" }, { "Rubraview list (*.rvlist)", "*.rvlist" },
};

static void list_save(app_state_t *app) {
    size_t count = page_count(app);
    if (count == 0) return;
    rubraview_file_dialog_opts_t opts = {
        .title = T("Save the list"), .filters = LIST_FILTERS, .filter_count = 2,
        .parent_window_handle = rubraview_pal_window_native_handle(app->window),
    };
    rubraview_file_dialog_result_t picked = rubraview_pal_file_dialog_save(app->arena, &opts);
    if (!picked.accepted || picked.count == 0) return;
    proven_u8str_view_t target = picked.paths[0];
    if (!path_is_playlist(target)) target = rubraview_path_with_ext(app->arena, target, ".m3u8");
    target = rubraview_pal_fs_absolute(app->arena, target);
    proven_u8str_view_t dir = rubraview_path_dirname(target);

    rubraview_playlist_t list = {0};
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, count, sizeof(rubraview_playlist_entry_t));
    if (!proven_is_ok(res.err)) return;
    list.entries = (rubraview_playlist_entry_t*)(void*)res.value.ptr;
    bool whole = list_saves_whole_paths(app);
    for (size_t i = 0; i < count; ++i) {
        if (is_station_path(app->source.pages[i].path)) {   /* RV-157: the address as it is, with its title */
            list.entries[i] = (rubraview_playlist_entry_t){ .path = app->source.pages[i].path, .duration_seconds = -1.0,
                                                            .title = app->source.pages[i].name };
            continue;
        }
        proven_u8str_view_t path = rubraview_pal_fs_absolute(app->arena, app->source.pages[i].path);
        list.entries[i] = (rubraview_playlist_entry_t){ .path = whole ? path : rubraview_path_relative(app->arena, dir, path),
                                                        .duration_seconds = -1.0 };
    }
    list.count = count;
    proven_u8str_view_t text = rubraview_path_has_ext(rubraview_path_basename(target), ".rvlist")
                 ? rubraview_playlist_serialize_rvlist(app->arena, &list)
                 : rubraview_playlist_serialize_m3u8(app->arena, &list);
    if (text.size == 0 || !rubraview_pal_fs_write_file(target, text)) {
        osd_say(app, TU8("could not write the list"));
        return;
    }
    char line[160];
    proven_u8str_view_t name = rubraview_path_basename(target);
    snprintf(line, sizeof(line), T("%zu files in %.*s, %s paths"), count, (int)(name.size < 80 ? name.size : 80), name.ptr,
             whole ? T("whole") : T("relative"));
    osd_say(app, cstr(line));
}

static void list_load(app_state_t *app) {
    static const rubraview_file_filter_t FILTERS[] = { { "Lists (*.m3u8;*.m3u;*.pls;*.asx;*.rvlist)", LIST_FILTER } };
    rubraview_file_dialog_opts_t opts = {
        .title = T("Open a list"), .filters = FILTERS, .filter_count = 1,
        .parent_window_handle = rubraview_pal_window_native_handle(app->window),
    };
    rubraview_file_dialog_result_t picked = rubraview_pal_file_dialog_open(app->arena, &opts);
    if (!picked.accepted || picked.count == 0) return;
    open_playlist(app, picked.paths[0]);
}

void list_row_menu(app_state_t *app, size_t row, double x, double y, bool in_files);

/* RV-117: Add — files picked in the open dialog go to the end of the list. */
/* 1.22.0 (owner, 2026-10-19: "재생목록의 위에 추가 버튼에 대해, 현재 열린 파일의 위치에서 자체 파일 탐색기로"):
   the picker, at the folder of the file on screen, picking; its Add to the list joins them to the list. */
static void list_add_picked(app_state_t *app, rubraview_window_t *owner) {
    (void)owner;
    proven_u8str_view_t here = current_file_path(app);
    proven_u8str_view_t dir = here.size > 0 ? rubraview_path_dirname(here) : app->source_dir;
    if (app->source.archive_path.size > 0) dir = rubraview_path_dirname(app->source.archive_path);
    if (dir.size > 0) app->picker_dir = app_keep(app, dir);
    if (app->picker_open) app->picker_open = false;
    picker_open(app);
    if (!app->picker_open) return;
    app->picker_add_mode = true;
    rubraview_picker_set_mode(&app->picker, RUBRAVIEW_PICK_INDIVIDUAL);   /* a tap picks */
    osd_say(app, TU8("pick what to add, then Add to the list"));
}

/* RV-117: a file of the list to the recycle bin — warned and asked first, in a
   question of Windows' own over the window it was asked from; No is the default. */
void list_delete_row(app_state_t *app, size_t row, rubraview_window_t *owner) {
    size_t count = page_count(app);
    if (row >= count || !list_editable(app)) return;
    char path[1024];
    proven_u8str_view_t from = app->source.pages[row].path;
    if (from.size == 0 || from.size >= sizeof(path)) return;
    memcpy(path, from.ptr, from.size);                       /* the list it points into is made again below */
    path[from.size] = '\0';
    proven_u8str_view_t name = rubraview_path_basename((proven_u8str_view_t){ .ptr = (const proven_byte_t *)path, .size = from.size });
    char text[1400];
    int n = snprintf(text, sizeof(text), T("Move this file to the recycle bin?\n\n%.*s\n\nIt leaves the list and its folder; "
                     "the recycle bin can give it back."), (int)(name.size < 600 ? name.size : 600), name.ptr);
    WCHAR wide[1500];
    if (n <= 0 || MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, 1500) <= 0) return;
    int answer = MessageBoxW((HWND)rubraview_pal_window_native_handle(owner), wide, L"Rubraview - delete a file",
                             MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (answer != IDYES) { osd_say(app, TU8("not deleted")); return; }
    if (app->media && app->media_page == (int32_t)row) media_close(app);   /* a film playing holds its file */
    proven_u8str_view_t gone = { .ptr = (const proven_byte_t *)path, .size = from.size };
    if (!rubraview_pal_fs_recycle(gone)) {
        osd_say(app, TU8("could not move it to the recycle bin (is it open in another program?)"));
        return;
    }
    if (page_count(app) <= 1) {
        reopen_after_removal(app, 0);
    } else {
        bool *one = (bool*)calloc(page_count(app), sizeof(bool));
        if (one) {
            one[row < page_count(app) ? row : page_count(app) - 1] = true;
            list_remove(app, one);
            free(one);
        }
    }
    char line[700];
    int m = snprintf(line, sizeof(line), T("moved to the recycle bin: %.*s"), (int)(name.size < 600 ? name.size : 600), name.ptr);
    if (m > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)m < sizeof(line) ? (size_t)m : sizeof(line) - 1 });
}

/* A press on either list (RV-117: the floating one, or the file window's,
   `in_files`, which its own frame moves, sizes and closes): true when it took it. */
bool list_press(app_state_t *app, rubraview_listwin_t *lw, double x, double y, bool right, bool in_files) {
    size_t row = 0, count = page_count(app);
    listwin_sync(app);
    lw->editable = list_editable(app);
    lw->tools = true;
    if (!right && scrollbar_press(app, in_files ? RVSB_FILES : RVSB_LIST, x, y)) {   /* RV-170: its scroll bar moves it */
        if (in_files) app->files_dirty = true;
        return true;
    }
    rubraview_listwin_part_t part = rubraview_listwin_hit(lw, x, y, list_rows(app), &row);
    if (part == RUBRAVIEW_LISTWIN_NONE) return false;
    note_activity(app);
    size_t index = list_page(app, row);   /* 1.25.0: the page of that row */
    bool viewed = list_viewed(app);       /* rows of a filtered list keep their pages' order */
    if (!right && part == RUBRAVIEW_LISTWIN_SHAPE) {   /* RV-192 */
        rubraview_rect_t t = rubraview_listwin_tool_rect(lw, part);
        shape_begin(app, SHAPE_FOR_LIST, in_files ? CHOOSE_IN_FILES : CHOOSE_IN_MAIN, t.x, t.y + t.height, t.y);
        if (in_files) app->files_dirty = true;
        return true;
    }
    if (!right && part >= RUBRAVIEW_LISTWIN_EXT && part <= RUBRAVIEW_LISTWIN_ORDER) {
        rubraview_rect_t t = rubraview_listwin_tool_rect(lw, part);
        int where = in_files ? CHOOSE_IN_FILES : CHOOSE_IN_MAIN;
        if (part == RUBRAVIEW_LISTWIN_EXT) chooser_open_listext(app, where, t.x, t.y + t.height, t.y);
        else if (part == RUBRAVIEW_LISTWIN_SORT) chooser_open_listsort(app, where, t.x, t.y + t.height, t.y);
        else if (part == RUBRAVIEW_LISTWIN_ORDER) list_sort(app, app->list_sort_key > 0 ? app->list_sort_key : 1, !app->list_sort_desc);
        else if (rubraview_rect_contains((rubraview_rect_t){ t.x + t.width - t.height, t.y, t.height, t.height }, x, y) && app->list_filter[0])
            list_filter_set(app, PROVEN_LIT(""));   /* its X */
        else list_filter_begin(app, in_files);
        if (in_files) app->files_dirty = true;
        return true;
    }
    if (right) {   /* RV-117: a file's own list — its path, Explorer, out of the list, the recycle bin */
        if ((part == RUBRAVIEW_LISTWIN_ROW || (part >= RUBRAVIEW_LISTWIN_ROW_CHECK && part <= RUBRAVIEW_LISTWIN_ROW_DOWN)) && index < count && list_editable(app)) {
            list_row_menu(app, index, x, y, in_files);
        }
        return true;
    }
    bool *one = NULL;
    switch (part) {
        case RUBRAVIEW_LISTWIN_CLOSE: lw->open = false; break;
        case RUBRAVIEW_LISTWIN_TITLE: if (!in_files) rubraview_listwin_drag_begin(lw, x, y); break;
        case RUBRAVIEW_LISTWIN_RESIZE: rubraview_listwin_resize_begin(lw, x, y); break;
        case RUBRAVIEW_LISTWIN_MAXIMIZE: if (!in_files) listwin_maximize_toggle(app, lw); break;
        case RUBRAVIEW_LISTWIN_ROW:
            /* Known when it is let go: not moved, it goes to that file; carried, the row moves. */
            if (index >= count) break;
            app->list_held_in_files = in_files;
            app->list_row_pressed = true;
            app->list_row_dragging = false;
            app->list_press_row = index;
            app->list_press_y = y;
            break;
        case RUBRAVIEW_LISTWIN_ROW_CHECK:
            if (index < app->list_checked_count) app->list_checked[index] = !app->list_checked[index];
            break;
        case RUBRAVIEW_LISTWIN_ROW_REMOVE:
        case RUBRAVIEW_LISTWIN_ROW_UP:
        case RUBRAVIEW_LISTWIN_ROW_DOWN:
            one = (bool*)calloc(count ? count : 1, sizeof(bool));
            if (!one || index >= count) break;
            one[index] = true;
            if (part == RUBRAVIEW_LISTWIN_ROW_REMOVE) list_remove(app, one);
            else if (viewed) osd_say(app, TU8("a filtered list keeps its order: clear the filter to move rows"));
            else if (part == RUBRAVIEW_LISTWIN_ROW_UP) { if (index > 0) list_move(app, one, index - 1); }
            else if (index + 1 < count) list_move(app, one, index + 2);
            break;
        case RUBRAVIEW_LISTWIN_DEL:
            if (app->list_checked && app->list_checked_count == count) list_remove(app, app->list_checked);
            break;
        case RUBRAVIEW_LISTWIN_ALL: {   /* every row checked, or, when they all are, none (owner, 2026-10-14); 1.25.0: the rows shown */
            size_t rows = list_rows(app), on = 0;
            for (size_t r = 0; r < rows; ++r) { size_t p = list_page(app, r); on += p < app->list_checked_count && app->list_checked[p]; }
            bool all = rows > 0 && on == rows;
            for (size_t r = 0; r < rows; ++r) { size_t p = list_page(app, r); if (p < app->list_checked_count) app->list_checked[p] = !all; }
            break;
        }
        case RUBRAVIEW_LISTWIN_ADD: list_add_picked(app, in_files ? app->files_window : app->window); break;
        case RUBRAVIEW_LISTWIN_URL: address_begin(app); break;   /* RV-158 */
        case RUBRAVIEW_LISTWIN_SAVE: list_save(app); break;
        case RUBRAVIEW_LISTWIN_LOAD: list_load(app); break;
        case RUBRAVIEW_LISTWIN_APPLY: apply_all_begin(app); break;
        case RUBRAVIEW_LISTWIN_PATHS: {
            bool whole = !list_saves_whole_paths(app);
            rubraview_settings_set(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("list_paths"), whole ? 1.0 : 0.0);
            osd_say(app, whole ? TU8("a saved list will name its files by their whole paths")
                               : TU8("a saved list will name its files from its own folder"));
            break;
        }
        default: break;
    }
    free(one);
    if (in_files) app->files_dirty = true;
    return true;
}

bool listwin_press(app_state_t *app, double x, double y, bool right) {
    return list_press(app, &app->listwin, x, y, right, false);
}

/* The pointer moved with something of a list held (`in_files`: over the file window). */
void list_motion(app_state_t *app, double x, double y, bool in_files) {
    if (!in_files) {
        int32_t w = 0, h = 0;
        rubraview_pal_window_get_size(app->window, &w, &h);
        if (app->listwin.dragging) rubraview_listwin_drag_to(&app->listwin, x, y, (double)w, (double)h);
        if (app->listwin.resizing) {
            rubraview_listwin_resize_to(&app->listwin, x, y, (double)w, (double)h);
            rubraview_listwin_scroll(&app->listwin, 0, page_count(app));
        }
    }
    if (app->list_row_pressed && app->list_held_in_files == in_files && list_editable(app)) {
        rubraview_listwin_t *lw = in_files ? &app->files_list : &app->listwin;
        rubraview_window_t *win = in_files ? app->files_window : app->window;
        if (!app->list_row_dragging && !list_viewed(app) && fabs(y - app->list_press_y) > 4.0 * rubraview_pal_window_dpi_scale(win)) {
            app->list_row_dragging = true;
        }
        if (app->list_row_dragging) app->list_drop_at = rubraview_listwin_insert_index(lw, y, page_count(app));
        if (in_files) app->files_dirty = true;
    }
}

void listwin_motion(app_state_t *app, double x, double y) {
    list_motion(app, x, y, false);
}

/* The button let go: true when the list window had hold of something. */
bool listwin_release(app_state_t *app) {
    bool held = app->listwin.dragging || app->listwin.resizing || app->list_row_pressed;
    app->listwin.dragging = false;
    app->listwin.resizing = false;
    if (!app->list_row_pressed) return held;
    app->list_row_pressed = false;
    size_t count = page_count(app), row = app->list_press_row;
    if (row >= count) return held;
    if (!app->list_row_dragging) {
        if ((int32_t)row != current_page_index(app)) {
            go_to_spread(app, spread_for_chosen_page(app, (int32_t)row));
            update_precache(app);
        }
        if (app->list_held_in_files) { app->files_selected = -1; app->files_dirty = true; }
        return held;
    }
    app->list_row_dragging = false;
    /* A checked row carries every checked row with it; an unchecked one goes alone. */
    bool *moving = (bool*)calloc(count, sizeof(bool));
    if (!moving) return held;
    if (row < app->list_checked_count && app->list_checked[row]) memcpy(moving, app->list_checked, count * sizeof(bool));
    else moving[row] = true;
    list_move(app, moving, app->list_drop_at);
    free(moving);
    return held;
}

/* ---- the recent files, in a list window (owner, 2026-10-17) ----
   "recent files 가 너무 보기 안좋다 ... 목록창 띄워서 ... 전체삭제나 삭제(목록 상에서만)
   ... 파일 이름 옆에 날짜나 용량, 위치". Each row: the name, when it was last
   opened, its size, its folder. A click opens it; the box and the X at its left,
   the bar's Remove and Clear all take files off the list only. */

void recentwin_build(app_state_t *app) {
    recent_sync(app, PROVEN_LIT(""), false);   /* what other viewers opened, too */
    size_t newest[RECENT_KEPT];
    size_t n = rubraview_recent_newest(&app->recent, newest, RECENT_KEPT);
    app->recent_row_count = 0;
    for (size_t i = 0; i < n; ++i) {
        const rubraview_history_entry_t *e = &app->recent.list.entries[newest[i]];
        struct recent_row *row = &app->recent_rows[app->recent_row_count++];
        rubraview_fs_entry_t st;
        *row = (struct recent_row){ .path = app_keep(app, e->path), .opened = e->timestamp };
        row->there = rubraview_pal_fs_stat(app->arena, row->path, &st);
        row->size = row->there ? st.size_bytes : 0;
    }
    if (app->recentwin.first > app->recent_row_count) app->recentwin.first = 0;
}

void recentwin_toggle(app_state_t *app) {
    app->recentwin.open = !app->recentwin.open;
    if (!app->recentwin.open) return;
    if (app->recentwin.title_height <= 0.0) {
        app->recentwin = rubraview_listwin_create(rubraview_pal_window_dpi_scale(app->window));
        app->recentwin.open = true;
    }
    app->recentwin.editable = true;   /* the bar and the boxes */
    recentwin_build(app);
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    if (!app->recentwin.placed) {   /* wide enough for four columns */
        double dpi = rubraview_pal_window_dpi_scale(app->window);
        app->recentwin.width = (double)w * 0.8 < 900.0 * dpi ? (double)w * 0.8 : 900.0 * dpi;
        app->recentwin.height = (double)h * 0.7;
        app->recentwin.x = ((double)w - app->recentwin.width) / 2.0;
        app->recentwin.y = (double)h * 0.12;
        app->recentwin.placed = true;
    }
    app->recentwin.view_top = list_view_top(app);
    rubraview_listwin_place(&app->recentwin, (double)w, (double)h);
}

static size_t recentwin_checked(const app_state_t *app) {
    size_t n = 0;
    for (size_t i = 0; i < app->recent_row_count; ++i) n += app->recent_rows[i].checked;
    return n;
}

/* "3.4 MB", "820 KB", "512 B" */
static void recent_size_text(char *b, size_t cap, uint64_t bytes) {
    if (bytes >= 1024ull * 1024ull * 1024ull) snprintf(b, cap, "%.1f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= 1024ull * 1024ull) snprintf(b, cap, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024ull) snprintf(b, cap, "%.0f KB", (double)bytes / 1024.0);
    else snprintf(b, cap, "%llu B", (unsigned long long)bytes);
}

/* 1.25.0: in its place's face (Settings › General) */
static void draw_recentwin_in(app_state_t *app, double win_w, double win_h);
void draw_recentwin(app_state_t *app, double win_w, double win_h) {
    int before = place_font_begin(app, PF_LIST, NULL);
    draw_recentwin_in(app, win_w, win_h);
    place_font_end(app, before);
}

static void draw_recentwin_in(app_state_t *app, double win_w, double win_h) {
    rubraview_listwin_t *lw = &app->recentwin;
    if (!lw->open) return;
    lw->view_top = list_view_top(app);
    rubraview_listwin_place(lw, win_w, win_h);
    size_t count = app->recent_row_count;
    rubraview_renderer_t *rr = app->renderer;
    rubraview_pal_rect_t body = { lw->x, lw->y, lw->width, lw->height };
    rubraview_pal_render_fill_rect(rr, body, (COLOR_BOX_FILL & 0x00FFFFFFu) | 0xF0000000u, 4.0);
    rubraview_pal_render_stroke_rect(rr, body, COLOR_BOX_BORDER, 1.0, 4.0);
    char title[96];
    int n = snprintf(title, sizeof(title), T("Recent files   %zu"), count);
    rubraview_pal_rect_t head = { lw->x + lw->row_height * 0.4, lw->y, rubraview_listwin_title_text_right(lw) - lw->x - lw->row_height * 0.4, lw->title_height };
    if (n > 0) rubraview_pal_render_draw_text(rr, (proven_u8str_view_t){ (const proven_byte_t *)title, (size_t)n }, head, lw->title_height * 0.45, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    rubraview_rect_t cr = rubraview_listwin_close_rect(lw);
    rubraview_pal_rect_t close = { cr.x, cr.y, cr.width, cr.height };
    if (rubraview_rect_contains(cr, app->pointer_x, app->pointer_y)) rubraview_pal_render_fill_rect(rr, close, COLOR_CLOSE_HOVER, 4.0);
    draw_sign(rr, RUBRAVIEW_ICON_CLOSE, "X", close, lw->title_height * 0.4, COLOR_TEXT);   /* RV-181 */
    listwin_sign_button(app, rr, rubraview_listwin_resize_rect(lw), RUBRAVIEW_ICON_RESIZE, "R", lw->title_height * 0.4, lw->resizing);
    draw_listwin_maximize(app, rr, lw);   /* RV-197 */

    /* the bar: all, Remove (the checked), Clear all */
    double small = lw->row_height * 0.5 * place_size(app, PF_LIST);   /* 1.25.0 */
    size_t checked = recentwin_checked(app);
    char del[48];
    if (checked > 0) snprintf(del, sizeof(del), T("Remove (%zu)"), checked); else snprintf(del, sizeof(del), "%s", T("Remove"));
    list_check_mark(rr, rubraview_listwin_bar_rect(lw, RUBRAVIEW_LISTWIN_ALL), count > 0 && checked == count, checked > 0);
    list_button(rr, app->pointer_x, app->pointer_y, rubraview_listwin_bar_rect(lw, RUBRAVIEW_LISTWIN_DEL), del, small, false,
                checked > 0 ? COLOR_TEXT : COLOR_BOX_BORDER);
    list_button(rr, app->pointer_x, app->pointer_y, rubraview_listwin_bar_rect(lw, RUBRAVIEW_LISTWIN_SAVE), T("Clear all"), small, false,
                count > 0 ? COLOR_TEXT : COLOR_BOX_BORDER);
    double head_h = rubraview_listwin_head_height(lw);
    rubraview_pal_render_fill_rect(rr, (rubraview_pal_rect_t){ lw->x + 2.0, lw->y + head_h - 1.0, lw->width - 4.0, 1.0 }, COLOR_BOX_BORDER, 0.0);

    size_t hovered = SIZE_MAX, index = 0;
    rubraview_listwin_part_t under = rubraview_listwin_hit(lw, app->pointer_x, app->pointer_y, count, &index);
    if (under == RUBRAVIEW_LISTWIN_ROW) hovered = index;
    size_t rows = rubraview_listwin_rows_visible(lw);
    /* the row's own parts: its box and its X; the text from after them */
    double text_x = lw->x + lw->row_height * 2.2;
    double avail = lw->x + lw->width - text_x - lw->row_height * 0.6;
    double c_name = avail * 0.36, c_date = avail * 0.2, c_size = avail * 0.11, c_dir = avail - c_name - c_date - c_size;
    for (size_t i = lw->first; i < count && i < lw->first + rows; ++i) {
        const struct recent_row *row = &app->recent_rows[i];
        rubraview_rect_t r0 = rubraview_listwin_row_rect(lw, i, count);
        rubraview_pal_rect_t rect = { r0.x + 2.0, r0.y, r0.width - 4.0, r0.height };
        if (i == hovered) rubraview_pal_render_fill_rect(rr, rect, COLOR_TILE_FILL, 2.0);
        list_check_mark(rr, rubraview_listwin_row_part_rect(lw, i, count, RUBRAVIEW_LISTWIN_ROW_CHECK), row->checked, false);
        list_button(rr, app->pointer_x, app->pointer_y, rubraview_listwin_row_part_rect(lw, i, count, RUBRAVIEW_LISTWIN_ROW_REMOVE), "x",
                    small, false, COLOR_TEXT);
        uint32_t ink = row->there ? COLOR_TEXT : COLOR_BOX_BORDER;   /* gone from the disk: dimmed */
        char fit[512], when[40], size[32];
        proven_u8str_view_t name = rubraview_path_basename(row->path), dir = rubraview_path_dirname(row->path);
        info_local_time(when, sizeof(when), row->opened);
        when[16] = '\0';   /* to the minute */
        if (row->there) recent_size_text(size, sizeof(size), row->size); else snprintf(size, sizeof(size), "%s", T("gone"));
        double x = text_x;
        rubraview_pal_rect_t c1 = { x, rect.y, c_name - small, rect.height };
        rubraview_pal_render_draw_text(rr, files_fit_text(fit, sizeof(fit), name, c1.width, small), c1, small, ink, RUBRAVIEW_TEXT_LEFT);
        x += c_name;
        rubraview_pal_render_draw_text(rr, cstr(when), (rubraview_pal_rect_t){ x, rect.y, c_date - small, rect.height }, small, ink, RUBRAVIEW_TEXT_LEFT);
        x += c_date;
        rubraview_pal_render_draw_text(rr, cstr(size), (rubraview_pal_rect_t){ x, rect.y, c_size - small, rect.height }, small, ink, RUBRAVIEW_TEXT_RIGHT);
        x += c_size + small;
        rubraview_pal_rect_t c4 = { x, rect.y, c_dir - small, rect.height };
        rubraview_pal_render_draw_text(rr, files_fit_text(fit, sizeof(fit), dir, c4.width, small), c4, small,
                                       (ink & 0x00FFFFFFu) | 0xB0000000u, RUBRAVIEW_TEXT_LEFT);
    }
    if (count == 0) {
        rubraview_pal_rect_t empty = { lw->x, lw->y + head_h, lw->width, lw->row_height * 2.0 };
        rubraview_pal_render_draw_text(rr, TU8("nothing opened lately"), empty, small, COLOR_BOX_BORDER, RUBRAVIEW_TEXT_CENTER);
    }
    draw_scrollbar(app, rr, RVSB_RECENT, app->pointer_x, app->pointer_y);   /* RV-170: where the rows are, and it moves them */
}

/* A press on the recent window: true when it took it. */
bool recentwin_press(app_state_t *app, double x, double y) {
    rubraview_listwin_t *lw = &app->recentwin;
    if (!lw->open || !rubraview_rect_contains(rubraview_listwin_rect(lw), x, y)) return false;
    if (scrollbar_press(app, RVSB_RECENT, x, y)) return true;   /* RV-170 */
    size_t count = app->recent_row_count, index = 0;
    switch (rubraview_listwin_hit(lw, x, y, count, &index)) {
        case RUBRAVIEW_LISTWIN_CLOSE:  lw->open = false; break;
        case RUBRAVIEW_LISTWIN_TITLE:  rubraview_listwin_drag_begin(lw, x, y); break;
        case RUBRAVIEW_LISTWIN_RESIZE: rubraview_listwin_resize_begin(lw, x, y); break;
        case RUBRAVIEW_LISTWIN_MAXIMIZE: listwin_maximize_toggle(app, lw); break;
        case RUBRAVIEW_LISTWIN_ALL: {
            bool all = recentwin_checked(app) < count;
            for (size_t i = 0; i < count; ++i) app->recent_rows[i].checked = all;
            break;
        }
        case RUBRAVIEW_LISTWIN_ROW_CHECK:
            if (index < count) app->recent_rows[index].checked = !app->recent_rows[index].checked;
            break;
        case RUBRAVIEW_LISTWIN_ROW_REMOVE:
            if (index < count) {
                recent_forget(app, &app->recent_rows[index].path, 1);
                recentwin_build(app);
                menu_rebuild(app);
                osd_say(app, TU8("taken off the list (the file is where it was)"));
            }
            break;
        case RUBRAVIEW_LISTWIN_DEL: {
            proven_u8str_view_t gone[RECENT_KEPT];
            size_t n = 0;
            for (size_t i = 0; i < count; ++i) if (app->recent_rows[i].checked) gone[n++] = app->recent_rows[i].path;
            if (n == 0) { osd_say(app, TU8("tick the files to take off first")); break; }
            recent_forget(app, gone, n);
            recentwin_build(app);
            menu_rebuild(app);
            osd_say(app, TU8("taken off the list (the files are where they were)"));
            break;
        }
        case RUBRAVIEW_LISTWIN_SAVE:   /* Clear all */
            if (count == 0) break;
            recent_sync(app, PROVEN_LIT(""), true);
            recentwin_build(app);
            menu_rebuild(app);
            osd_say(app, TU8("the recent list is empty"));
            break;
        case RUBRAVIEW_LISTWIN_ROW:
        case RUBRAVIEW_LISTWIN_ROW_UP: case RUBRAVIEW_LISTWIN_ROW_DOWN:   /* no arrows here: the name starts there */
            if (index < count) {
                proven_u8str_view_t path = app->recent_rows[index].path;
                if (!rubraview_pal_fs_exists(path)) { osd_say(app, TU8("that file is no longer there")); break; }
                lw->open = false;
                open_path(app, path);
            }
            break;
        default: break;   /* the bar's other places, a row's arrows: nothing here */
    }
    return true;
}

void recentwin_motion(app_state_t *app, double x, double y) {
    rubraview_listwin_t *lw = &app->recentwin;
    if (!lw->open || (!lw->dragging && !lw->resizing)) return;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    if (lw->dragging) rubraview_listwin_drag_to(lw, x, y, (double)w, (double)h);
    if (lw->resizing) { rubraview_listwin_resize_to(lw, x, y, (double)w, (double)h); rubraview_listwin_scroll(lw, 0, app->recent_row_count); }
}

bool recentwin_release(app_state_t *app) {
    bool held = app->recentwin.dragging || app->recentwin.resizing;
    app->recentwin.dragging = false;
    app->recentwin.resizing = false;
    return held;
}

/* ---- 1.24.0: favourites, files and folders, in a list window (owner, 2026-10-19: "파일이나 폴더의
   즐겨찾기 기능 ... 재생목록이나 최근 파일 창이랑 비슷하게"; "순서를 서로 바꿀 수 있게"). A row: its box,
   its X, ▲ and ▼ that move it, its name, folder or file, where it is. A click opens it: a folder in the
   open-file screen, a file in the viewer. The bar: all, Add this (the file on screen), Remove. ---- */

/* In or out of the favourites, written at once (another viewer's changes read first, D-99). */
bool favorite_toggle_path(app_state_t *app, proven_u8str_view_t path) {
    if (path.size == 0) return false;
    bool locked = rubraview_pal_config_lock(CONFIG_LOCK_MS);
    if (app->favorites_path.size > 0) {
        proven_u8str_view_t text = rubraview_pal_fs_read_file(app->arena, app->favorites_path, 64u * 1024u);
        if (text.size > 0) app->favorites = rubraview_favorites_parse(app->arena, text);
    }
    bool was = rubraview_favorites_contains(&app->favorites, path);
    bool now = rubraview_favorites_toggle(app->arena, &app->favorites, app_keep(app, path));
    bool written = (!was && !now) || app->favorites_path.size == 0 ||
        rubraview_pal_fs_write_file_atomic(app->favorites_path, rubraview_favorites_serialize(app->arena, &app->favorites));
    if (locked) rubraview_pal_config_unlock();
    if (!was && !now) { osd_say(app, TU8("favourites are full")); return false; }
    osd_say(app, now ? TU8("added to favourites") : TU8("removed from favourites"));
    if (!written) osd_say(app, TU8("could not write favorites.ini"));
    return now;
}

void favwin_build(app_state_t *app) {
    for (size_t i = 0; i < app->favorites.count && i < RUBRAVIEW_FAVORITES_MAX; ++i) {
        rubraview_fs_entry_t st;
        app->fav_kind[i] = rubraview_pal_fs_stat(app->arena, app->favorites.paths[i], &st) ? (st.is_directory ? 1 : 2) : 0;
        if (i >= app->fav_checked_n) app->fav_checked[i] = false;
    }
    app->fav_checked_n = app->favorites.count;
    if (app->favwin.first > app->favorites.count) app->favwin.first = 0;
}

void favwin_toggle(app_state_t *app) {
    app->favwin.open = !app->favwin.open;
    if (!app->favwin.open) return;
    if (app->favwin.title_height <= 0.0) {
        app->favwin = rubraview_listwin_create(rubraview_pal_window_dpi_scale(app->window));
        app->favwin.open = true;
    }
    app->favwin.editable = true;
    for (size_t i = 0; i < RUBRAVIEW_FAVORITES_MAX; ++i) app->fav_checked[i] = false;
    favwin_build(app);
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    if (!app->favwin.placed) {
        double dpi = rubraview_pal_window_dpi_scale(app->window);
        app->favwin.width = (double)w * 0.8 < 860.0 * dpi ? (double)w * 0.8 : 860.0 * dpi;
        app->favwin.height = (double)h * 0.66;
        app->favwin.x = ((double)w - app->favwin.width) / 2.0;
        app->favwin.y = (double)h * 0.14;
        app->favwin.placed = true;
    }
    app->favwin.view_top = list_view_top(app);
    rubraview_listwin_place(&app->favwin, (double)w, (double)h);
}

static size_t favwin_checked(const app_state_t *app) {
    size_t n = 0;
    for (size_t i = 0; i < app->favorites.count; ++i) n += app->fav_checked[i];
    return n;
}

/* 1.25.0: in its place's face (Settings › General) */
static void draw_favwin_in(app_state_t *app, double win_w, double win_h);
void draw_favwin(app_state_t *app, double win_w, double win_h) {
    int before = place_font_begin(app, PF_LIST, NULL);
    draw_favwin_in(app, win_w, win_h);
    place_font_end(app, before);
}

static void draw_favwin_in(app_state_t *app, double win_w, double win_h) {
    rubraview_listwin_t *lw = &app->favwin;
    if (!lw->open) return;
    lw->view_top = list_view_top(app);
    rubraview_listwin_place(lw, win_w, win_h);
    size_t count = app->favorites.count;
    rubraview_renderer_t *rr = app->renderer;
    rubraview_pal_rect_t body = { lw->x, lw->y, lw->width, lw->height };
    rubraview_pal_render_fill_rect(rr, body, (COLOR_BOX_FILL & 0x00FFFFFFu) | 0xF0000000u, 4.0);
    rubraview_pal_render_stroke_rect(rr, body, COLOR_BOX_BORDER, 1.0, 4.0);
    char title[96];
    int n = snprintf(title, sizeof(title), T("Favourites   %zu"), count);
    rubraview_pal_rect_t head = { lw->x + lw->row_height * 0.4, lw->y, rubraview_listwin_title_text_right(lw) - lw->x - lw->row_height * 0.4, lw->title_height };
    if (n > 0) rubraview_pal_render_draw_text(rr, (proven_u8str_view_t){ (const proven_byte_t *)title, (size_t)n }, head, lw->title_height * 0.45, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    rubraview_rect_t cr = rubraview_listwin_close_rect(lw);
    rubraview_pal_rect_t close = { cr.x, cr.y, cr.width, cr.height };
    if (rubraview_rect_contains(cr, app->pointer_x, app->pointer_y)) rubraview_pal_render_fill_rect(rr, close, COLOR_CLOSE_HOVER, 4.0);
    draw_sign(rr, RUBRAVIEW_ICON_CLOSE, "X", close, lw->title_height * 0.4, COLOR_TEXT);   /* RV-181 */
    listwin_sign_button(app, rr, rubraview_listwin_resize_rect(lw), RUBRAVIEW_ICON_RESIZE, "R", lw->title_height * 0.4, lw->resizing);
    draw_listwin_maximize(app, rr, lw);   /* RV-197 */

    double small = lw->row_height * 0.5 * place_size(app, PF_LIST);   /* 1.25.0 */
    size_t checked = favwin_checked(app);
    char del[48];
    if (checked > 0) snprintf(del, sizeof(del), T("Remove (%zu)"), checked); else snprintf(del, sizeof(del), "%s", T("Remove"));
    list_check_mark(rr, rubraview_listwin_bar_rect(lw, RUBRAVIEW_LISTWIN_ALL), count > 0 && checked == count, checked > 0);
    proven_u8str_view_t here = current_file_path(app);
    list_button(rr, app->pointer_x, app->pointer_y, rubraview_listwin_bar_rect(lw, RUBRAVIEW_LISTWIN_ADD), T("Add this file"), small, false,
                here.size > 0 ? COLOR_TEXT : COLOR_BOX_BORDER);
    list_button(rr, app->pointer_x, app->pointer_y, rubraview_listwin_bar_rect(lw, RUBRAVIEW_LISTWIN_DEL), del, small, false,
                checked > 0 ? COLOR_TEXT : COLOR_BOX_BORDER);
    double head_h = rubraview_listwin_head_height(lw);
    rubraview_pal_render_fill_rect(rr, (rubraview_pal_rect_t){ lw->x + 2.0, lw->y + head_h - 1.0, lw->width - 4.0, 1.0 }, COLOR_BOX_BORDER, 0.0);

    size_t hovered = SIZE_MAX, index = 0;
    if (rubraview_listwin_hit(lw, app->pointer_x, app->pointer_y, count, &index) == RUBRAVIEW_LISTWIN_ROW) hovered = index;
    size_t rows = rubraview_listwin_rows_visible(lw);
    double text_x = lw->x + lw->row_height * 4.3;   /* after the box, the X, ▲ and ▼ */
    double avail = lw->x + lw->width - text_x - lw->row_height * 0.6;
    double c_name = avail * 0.38, c_kind = avail * 0.12, c_dir = avail - c_name - c_kind;
    for (size_t i = lw->first; i < count && i < lw->first + rows; ++i) {
        rubraview_rect_t r0 = rubraview_listwin_row_rect(lw, i, count);
        rubraview_pal_rect_t rect = { r0.x + 2.0, r0.y, r0.width - 4.0, r0.height };
        if (i == hovered) rubraview_pal_render_fill_rect(rr, rect, COLOR_TILE_FILL, 2.0);
        list_check_mark(rr, rubraview_listwin_row_part_rect(lw, i, count, RUBRAVIEW_LISTWIN_ROW_CHECK), app->fav_checked[i], false);
        list_button(rr, app->pointer_x, app->pointer_y, rubraview_listwin_row_part_rect(lw, i, count, RUBRAVIEW_LISTWIN_ROW_REMOVE), "x", small, false, COLOR_TEXT);
        list_button(rr, app->pointer_x, app->pointer_y, rubraview_listwin_row_part_rect(lw, i, count, RUBRAVIEW_LISTWIN_ROW_UP), "\xE2\x96\xB2", small, false,
                    i > 0 ? COLOR_TEXT : COLOR_BOX_BORDER);
        list_button(rr, app->pointer_x, app->pointer_y, rubraview_listwin_row_part_rect(lw, i, count, RUBRAVIEW_LISTWIN_ROW_DOWN), "\xE2\x96\xBC", small, false,
                    i + 1 < count ? COLOR_TEXT : COLOR_BOX_BORDER);
        int kind = app->fav_kind[i];
        uint32_t ink = kind ? COLOR_TEXT : COLOR_BOX_BORDER;   /* gone from the disk: dimmed */
        char fit[512];
        proven_u8str_view_t name = rubraview_favorites_title(&app->favorites, i), dir = rubraview_path_dirname(app->favorites.paths[i]);
        double x = text_x;
        rubraview_pal_rect_t c1 = { x, rect.y, c_name - small, rect.height };
        rubraview_pal_render_draw_text(rr, files_fit_text(fit, sizeof(fit), name, c1.width, small), c1, small, kind == 1 ? 0xFFB8D8FFu : ink, RUBRAVIEW_TEXT_LEFT);
        x += c_name;
        rubraview_pal_render_draw_text(rr, kind == 1 ? TU8("folder") : kind == 2 ? TU8("file") : TU8("gone"),
                                       (rubraview_pal_rect_t){ x, rect.y, c_kind - small, rect.height }, small, ink, RUBRAVIEW_TEXT_LEFT);
        x += c_kind;
        rubraview_pal_rect_t c3 = { x, rect.y, c_dir - small, rect.height };
        rubraview_pal_render_draw_text(rr, files_fit_text(fit, sizeof(fit), dir, c3.width, small), c3, small,
                                       (ink & 0x00FFFFFFu) | 0xB0000000u, RUBRAVIEW_TEXT_LEFT);
    }
    if (count == 0) {
        rubraview_pal_rect_t empty = { lw->x, lw->y + head_h, lw->width, lw->row_height * 2.0 };
        rubraview_pal_render_draw_text(rr, TU8("no favourites yet: Add this file, the open-file screen's star, or a right click there"),
                                       empty, small, COLOR_BOX_BORDER, RUBRAVIEW_TEXT_CENTER);
    }
    draw_scrollbar(app, rr, RVSB_FAVS, app->pointer_x, app->pointer_y);   /* RV-170 */
}

bool favwin_press(app_state_t *app, double x, double y) {
    rubraview_listwin_t *lw = &app->favwin;
    if (!lw->open || !rubraview_rect_contains(rubraview_listwin_rect(lw), x, y)) return false;
    if (scrollbar_press(app, RVSB_FAVS, x, y)) return true;   /* RV-170 */
    size_t count = app->favorites.count, index = 0;
    switch (rubraview_listwin_hit(lw, x, y, count, &index)) {
        case RUBRAVIEW_LISTWIN_CLOSE:  lw->open = false; break;
        case RUBRAVIEW_LISTWIN_TITLE:  rubraview_listwin_drag_begin(lw, x, y); break;
        case RUBRAVIEW_LISTWIN_RESIZE: rubraview_listwin_resize_begin(lw, x, y); break;
        case RUBRAVIEW_LISTWIN_MAXIMIZE: listwin_maximize_toggle(app, lw); break;
        case RUBRAVIEW_LISTWIN_ALL: {
            bool all = favwin_checked(app) < count;
            for (size_t i = 0; i < count; ++i) app->fav_checked[i] = all;
            break;
        }
        case RUBRAVIEW_LISTWIN_ADD: {
            proven_u8str_view_t here = current_file_path(app);
            if (here.size == 0) { osd_say(app, TU8("open a file first")); break; }
            if (rubraview_favorites_contains(&app->favorites, here)) { osd_say(app, TU8("already a favourite")); break; }
            favorite_toggle_path(app, here);
            favwin_build(app);
            break;
        }
        case RUBRAVIEW_LISTWIN_ROW_CHECK:
            if (index < count) app->fav_checked[index] = !app->fav_checked[index];
            break;
        case RUBRAVIEW_LISTWIN_ROW_REMOVE:
            if (index < count) {
                for (size_t i = index; i + 1 < count; ++i) app->fav_checked[i] = app->fav_checked[i + 1];
                favorite_toggle_path(app, app->favorites.paths[index]);
                favwin_build(app);
            }
            break;
        case RUBRAVIEW_LISTWIN_ROW_UP: case RUBRAVIEW_LISTWIN_ROW_DOWN: {
            bool up = rubraview_listwin_hit(lw, x, y, count, &index) == RUBRAVIEW_LISTWIN_ROW_UP;
            if (index >= count || (up && index == 0) || (!up && index + 1 >= count)) break;
            size_t to = up ? index - 1 : index + 1;
            if (rubraview_favorites_move(&app->favorites, index, to)) {
                bool c = app->fav_checked[index];
                app->fav_checked[index] = app->fav_checked[to];
                app->fav_checked[to] = c;
                favorites_save(app);
                favwin_build(app);
            }
            break;
        }
        case RUBRAVIEW_LISTWIN_DEL: {
            if (favwin_checked(app) == 0) { osd_say(app, TU8("tick the favourites to take off first")); break; }
            proven_u8str_view_t gone[RUBRAVIEW_FAVORITES_MAX];
            size_t n = 0;
            for (size_t i = 0; i < count; ++i) if (app->fav_checked[i]) gone[n++] = app->favorites.paths[i];
            for (size_t i = 0; i < n; ++i) favorite_toggle_path(app, gone[i]);
            for (size_t i = 0; i < RUBRAVIEW_FAVORITES_MAX; ++i) app->fav_checked[i] = false;
            favwin_build(app);
            break;
        }
        case RUBRAVIEW_LISTWIN_ROW:
            if (index < count) {
                proven_u8str_view_t path = app_keep(app, app->favorites.paths[index]);
                int kind = app->fav_kind[index];
                if (!kind) { osd_say(app, TU8("that is no longer there")); break; }
                lw->open = false;
                if (kind == 1) {   /* a folder: the open-file screen there */
                    if (!app->picker_open) picker_open(app);
                    picker_navigate(app, path);
                    app->picker.focus = 0;
                    app->picker_open = app->picker_listing.count > 0;
                } else {
                    app->picker_open = false;
                    open_path(app, path);
                }
            }
            break;
        default: break;
    }
    return true;
}

void favwin_motion(app_state_t *app, double x, double y) {
    rubraview_listwin_t *lw = &app->favwin;
    if (!lw->open || (!lw->dragging && !lw->resizing)) return;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    if (lw->dragging) rubraview_listwin_drag_to(lw, x, y, (double)w, (double)h);
    if (lw->resizing) { rubraview_listwin_resize_to(lw, x, y, (double)w, (double)h); rubraview_listwin_scroll(lw, 0, app->favorites.count); }
}

bool favwin_release(app_state_t *app) {
    bool held = app->favwin.dragging || app->favwin.resizing;
    app->favwin.dragging = false;
    app->favwin.resizing = false;
    return held;
}

/* ---- The file list window (owner, 2026-10-08) ----
   "압축파일 내부 파일들 목록들을 볼 수 있고 거기서 이동도 가능하며 프리뷰도
   보여주는 … 모달리스 … 원래 창은 원래대로 계속 보여주고 있고 외부의 창으로
   파일을 제어". A window of its own, owned by the viewer and never in its
   way: the viewer goes on showing its page while this one lists what the
   archive (or the folder, or the playlist) holds. Pointing at a row, or
   moving to it with the arrow keys, shows that picture on the right; a
   click or Enter turns the viewer to it. The preview is decoded by this
   window's own renderer from the same bytes the viewer would read. */


#define FILES_HOVER_REST 0.12   /* seconds the pointer rests on a row before its picture is read */

void files_preview_drop(app_state_t *app) {
    if (app->files_preview) rubraview_pal_texture_destroy(app->files_preview);
    app->files_preview = NULL;
    app->files_preview_page = -1;
    app->files_preview_w = app->files_preview_h = 0;
    app->files_preview_state = FILES_PREVIEW_NONE;
}

void files_close(app_state_t *app) {
    if (!app->files_open) return;
    app->list_filter_in_files = false;   /* 1.25.0 */
    if (app->chooser.open && app->chooser.where == CHOOSE_IN_FILES) app->chooser.open = false;
    rubraview_pal_window_get_frame(app->files_window, &app->files_frame[0], &app->files_frame[1],
                                   &app->files_frame[2], &app->files_frame[3]);
    files_preview_drop(app);
    if (app->files_renderer) rubraview_pal_render_destroy(app->files_renderer);
    if (app->files_window) rubraview_pal_window_destroy(app->files_window);
    window_block_give(app, &app->files_block);
    app->files_renderer = NULL;
    app->files_window = NULL;
    app->files_open = false;
}

void files_layout(app_state_t *app) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->files_window, &w, &h);
    app->files_preview_rect = rubraview_filewin_layout(&app->files_list, (double)w, (double)h,
                                                       rubraview_pal_window_dpi_scale(app->files_window));
    app->files_dirty = true;
}

void files_show(app_state_t *app) {
    if (app->files_open) { files_close(app); return; }   /* the same key puts it away */
    if (page_count(app) == 0) {
        osd_say(app, TU8("nothing is open to list"));
        return;
    }
    rubraview_window_config_t config = { .title = T("Rubraview files"), .width = 760, .height = 560, .owner = app->window };
    app->files_window = rubraview_pal_window_create(window_block_take(app, &app->files_block), &config);
    if (!app->files_window) {
        osd_say(app, TU8("the file list could not be opened"));
        return;
    }
    if (app->files_frame[2] > 0 && app->files_frame[3] > 0) {
        rubraview_pal_window_set_frame(app->files_window, app->files_frame[0], app->files_frame[1],
                                       app->files_frame[2], app->files_frame[3]);
    }
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->files_window, &w, &h);
    app->files_renderer = rubraview_pal_render_create(window_block_arena(app, &app->files_block), rubraview_pal_window_native_handle(app->files_window), w, h);
    ui_font_apply_all(app);
    if (!app->files_renderer) {
        rubraview_pal_window_destroy(app->files_window); window_block_give(app, &app->files_block);
        app->files_window = NULL;
        osd_say(app, TU8("the file list could not be drawn"));
        return;
    }
    app->files_open = true;
    app->files_list = (rubraview_listwin_t){0};
    app->files_pages = NULL;                     /* the first frame takes the list as new */
    app->files_selected = app->files_hover = -1;
    app->files_followed = -1;
    app->files_want = -1;
    files_preview_drop(app);
    rubraview_pal_window_accept_drops(app->files_window, true);   /* RV-117: files dragged in join the list */
    files_layout(app);
}

/* The file whose picture the right side shows: the one pointed at, else the
   one the keys chose, else the one on screen. */
int32_t files_target(const app_state_t *app) {
    if (app->files_hover >= 0) return app->files_hover;
    if (app->files_selected >= 0) return app->files_selected;
    return current_page_index(app);
}

/* Turn the viewer to a file of the list. */
void files_go_to(app_state_t *app, int32_t index) {
    if (index < 0 || (size_t)index >= page_count(app)) return;
    if (index != current_page_index(app)) {
        go_to_spread(app, spread_for_chosen_page(app, index));
        update_precache(app);
    }
    app->files_selected = -1;                    /* the keys start again from the file on screen */
    app->files_dirty = true;
}

/* Read the wanted file's picture, once the pointer has rested on its row.
   A page that costs a long decode to reach (far inside a solid archive) is
   not read for a glance; while the viewer's own far read runs, this waits. */
void files_preview_step(app_state_t *app, double now) {
    if (!app->files_open) return;
    int32_t target = files_target(app);
    if (target != app->files_want) {
        app->files_want = target;
        app->files_want_since = now;
    }
    if (app->files_want == app->files_preview_page) return;
    if (!app->files_want_now && app->files_hover >= 0 && now - app->files_want_since < FILES_HOVER_REST) return;
    if (app->page_job) return;
    app->files_want_now = false;

    int32_t page = app->files_want;
    files_preview_drop(app);
    app->files_preview_page = page;
    app->files_dirty = true;
    if (page < 0 || (size_t)page >= page_count(app)) return;
    if (is_media_path(app->source.pages[page].path)) { app->files_preview_state = FILES_PREVIEW_MEDIA; return; }
    if (page_read_cost(app, (size_t)page) > PAGE_JOB_MIN_COST) { app->files_preview_state = FILES_PREVIEW_FAR; return; }

    void *mem = NULL;
    rubraview_page_bytes_t bytes = page_read_owned(&app->source, &app->source_lock, (size_t)page, &mem);
    rubraview_image_load_result_t loaded = {0};
    if (bytes.ok) {
        loaded = bytes.from_disk
            ? rubraview_pal_image_load_texture(app->files_renderer, app->source.pages[page].path, true)
            : rubraview_pal_image_load_texture_from_memory(app->files_renderer, (const uint8_t*)bytes.data.ptr, bytes.data.size, true);
    }
    free(mem);
    if (!loaded.ok || !loaded.texture) { app->files_preview_state = FILES_PREVIEW_FAILED; return; }
    app->files_preview = loaded.texture;
    app->files_preview_w = loaded.full_width > 0 ? loaded.full_width : loaded.width;
    app->files_preview_h = loaded.full_height > 0 ? loaded.full_height : loaded.height;
    app->files_preview_state = FILES_PREVIEW_SHOWN;
}

/* "1.2 MB": a size short enough for a row. */
int files_size_text(char *buf, size_t cap, uint64_t bytes) {
    if (bytes < 1024u) return snprintf(buf, cap, "%u B", (unsigned)bytes);
    if (bytes < 1024u * 1024u) return snprintf(buf, cap, T("%.0f KB"), (double)bytes / 1024.0);
    if (bytes < 1024ull * 1024ull * 1024ull) return snprintf(buf, cap, T("%.1f MB"), (double)bytes / (1024.0 * 1024.0));
    return snprintf(buf, cap, T("%.2f GB"), (double)bytes / (1024.0 * 1024.0 * 1024.0));
}

/* As much of `text` as about fits `width` at `size`, cut at a whole
   character with "..." (there is no clipping to lean on). */
proven_u8str_view_t files_fit_text(char *buf, size_t cap, proven_u8str_view_t text, double width, double size) {
    size_t room = size > 0.0 ? (size_t)(width / (size * 0.56)) : 0;
    size_t chars = 0, at = 0;
    while (at < text.size) {
        size_t next = at + 1;
        while (next < text.size && ((unsigned char)text.ptr[next] & 0xC0) == 0x80) next++;
        /* a CJK character is about twice a Latin one wide */
        chars += (unsigned char)text.ptr[at] >= 0xE0 ? 2 : 1;
        if (chars > room) break;
        at = next;
    }
    if (at >= text.size) return text;
    while (at > 0 && chars + 3 > room) {         /* make room for the dots */
        size_t prev = at - 1;
        while (prev > 0 && ((unsigned char)text.ptr[prev] & 0xC0) == 0x80) prev--;
        chars -= (unsigned char)text.ptr[prev] >= 0xE0 ? 2 : 1;
        at = prev;
    }
    if (at + 4 > cap) at = cap > 4 ? cap - 4 : 0;
    memcpy(buf, text.ptr, at);
    memcpy(buf + at, "...", 3);
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)buf, .size = at + 3 };
}
