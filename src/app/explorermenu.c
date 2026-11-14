/*
 * RV-279 (owner, 2026-11-14): Explorer's menu inside Rubraview. "기본은 레지스트리 읽어서 보여주기, 그리고 탐색기
 * 메뉴(윈11)와 구 탐색기 메뉴(윈10이전) 을 하위항목으로 ... 쉬프트 엔터면 구버젼 우클릭 메뉴" — "레지스트리 쪽은
 * 우클릭 메뉴에서 바로 보이고, 그리고 공식api쪽은 별개프로세스로".
 *
 * A right click on a file remembers the files it is about (the targets) and reads their registry verbs
 * (pal_shellverbs.h); popups.c shows those as rows before the two rows that open Explorer's own menus, which the
 * child process shows. Shift with the right click, or Shift+F10, opens the classic one at once.
 */
#include "app.h"

#define TARGETS_MAX 64

static bool explorer_items_on(const app_state_t *app) {
    return rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("explorer_items")) > 0.5;
}

static void targets_clear(app_state_t *app) {
    for (size_t i = 0; i < app->shell_target_count; ++i) rubraview_free(app->shell_targets[i]);
    rubraview_free(app->shell_targets);
    app->shell_targets = NULL;
    app->shell_target_count = 0;
    app->shell_verb_count = 0;
}

/* The file on disk a path stands for: itself, or the archive a page is inside. "" for nothing on disk. */
static char *disk_file(app_state_t *app, proven_u8str_view_t path, bool *is_folder) {
    *is_folder = false;
    if (path.size == 0 || is_station_path(path)) return NULL;
    proven_u8str_view_t p = rubraview_pal_fs_absolute(app->arena, path);
    for (int up = 0; up < 32 && p.size > 0; ++up) {
        rubraview_fs_entry_t e = {0};
        if (rubraview_pal_fs_stat(app->arena, p, &e)) {
            *is_folder = e.is_directory;
            return rubraview_strndup_view(p);
        }
        proven_u8str_view_t parent = rubraview_path_dirname(p);
        if (parent.size >= p.size) break;
        p = parent;
    }
    return NULL;
}

static void targets_add(app_state_t *app, proven_u8str_view_t path, bool *folder) {
    if (app->shell_target_count >= TARGETS_MAX) return;
    bool is_folder = false;
    char *file = disk_file(app, path, &is_folder);
    if (!file) return;
    for (size_t i = 0; i < app->shell_target_count; ++i)   /* pages of one archive: the archive once */
        if (rubraview_cstr_cmp_ci(app->shell_targets[i], file) == 0) { rubraview_free(file); return; }
    if (!app->shell_targets) app->shell_targets = (char **)rubraview_alloc_zero(TARGETS_MAX, sizeof(char *));
    if (!app->shell_targets) { rubraview_free(file); return; }
    if (app->shell_target_count == 0) *folder = is_folder;
    app->shell_targets[app->shell_target_count++] = file;
}

/* The verbs of the first target, for them all (Explorer takes the item clicked). */
static void targets_read_verbs(app_state_t *app, bool folder) {
    app->shell_verb_count = 0;
    app->shell_folder = folder;
    if (app->shell_target_count == 0 || !explorer_items_on(app)) return;
    if (!app->shell_verbs) app->shell_verbs = (rubraview_shellverb_t *)rubraview_alloc_zero(RUBRAVIEW_SHELLVERBS_MAX, sizeof(rubraview_shellverb_t));
    if (!app->shell_verbs) return;
    double t0 = rubraview_pal_time_now_seconds();
    app->shell_verb_count = rubraview_pal_shellverbs_list(app->shell_targets[0], folder, false, app->shell_target_count,
                                                          app->shell_verbs, RUBRAVIEW_SHELLVERBS_MAX);
    char line[160];   /* the time it takes, for the log (the plan's measure: the click to the list) */
    int n = rubraview_fmt(line, sizeof line, "{} verbs of the registry read in {:.2} ms", PROVEN_ARG((uint32_t)app->shell_verb_count),
                          PROVEN_ARG((rubraview_pal_time_now_seconds() - t0) * 1000.0));
    if (n > 0) app_log(app, "explorer", rubraview_bytes_view(line, (size_t)n < sizeof line ? (size_t)n : sizeof line - 1));
}

/* The page on screen, or the book's row in a list book being read. */
static void targets_for_page(app_state_t *app) {
    targets_clear(app);
    bool folder = false;
    int32_t at = current_page_index(app);
    if (app->source.archive_path.size > 0) targets_add(app, app->source.archive_path, &folder);
    else if (at >= 0 && (size_t)at < page_count(app)) targets_add(app, app->source.pages[at].path, &folder);
    targets_read_verbs(app, folder);
}

/* The open-file screen: what is picked, else the tile. Nothing inside an archive. */
static void targets_for_picker(app_state_t *app, size_t index) {
    targets_clear(app);
    if (picker_in_archive(app) || app->picker_in_book) return;
    bool folder = false;
    if (index < app->picker_listing.count) targets_add(app, app->picker_listing.entries[index].path, &folder);   /* the one clicked first */
    for (size_t i = 0; app->picker_selected && i < app->picker_listing.count; ++i)
        if (app->picker_selected[i] && i != index) { bool f = false; targets_add(app, app->picker_listing.entries[i].path, &f); }
    targets_read_verbs(app, folder);
}

void explorer_targets_page(app_state_t *app) { targets_for_page(app); }
void explorer_targets_picker(app_state_t *app, size_t index) { targets_for_picker(app, index); }

void explorer_targets_list_row(app_state_t *app, size_t row) {
    targets_clear(app);
    bool folder = false;
    if (row < page_count(app)) targets_add(app, app->source.pages[row].path, &folder);
    targets_read_verbs(app, folder);
}

bool explorer_rows_shown(const app_state_t *app) { return explorer_items_on(app) && app->shell_target_count > 0; }

/* Explorer's own menu of the targets, from the child, at (x, y) in `window`. */
static void show_menu(app_state_t *app, rubraview_shellmenu_kind_t kind, rubraview_window_t *window, double x, double y) {
    if (app->shell_target_count == 0) { osd_say(app, TU8("there is no file on disk for Explorer's menu")); return; }
    if (!rubraview_pal_shellmenu_show(kind, (const char *const *)app->shell_targets, app->shell_target_count,
                                      rubraview_pal_window_native_handle(window), x, y))
        osd_say(app, TU8("Explorer's menu could not be started"));
}

/* A row of the right-click list that opens Explorer's menu: the targets the list was opened for. */
void explorer_menu_from_list(app_state_t *app, bool classic, rubraview_window_t *window, double x, double y) {
    bool shift = rubraview_pal_shellmenu_shift_held();
    show_menu(app, classic ? (shift ? RUBRAVIEW_SHELLMENU_CLASSIC_EXTENDED : RUBRAVIEW_SHELLMENU_CLASSIC) : RUBRAVIEW_SHELLMENU_WIN11, window, x, y);
}

/* The action (a key, the title bar's ☰): what is on screen now, at the pointer. */
void explorer_menu_action(app_state_t *app, bool classic) {
    if (app->picker_open) targets_for_picker(app, app->picker.focus);
    else targets_for_page(app);
    explorer_menu_from_list(app, classic, app->window, app->pointer_x, app->pointer_y);
}

/* Shift with a right click: the classic menu at once, as Explorer's Shift+right click (its extended verbs too). */
bool explorer_shift_click_page(app_state_t *app, double x, double y) {
    if (!rubraview_pal_shellmenu_shift_held()) return false;
    targets_for_page(app);
    if (app->shell_target_count == 0) return false;
    show_menu(app, RUBRAVIEW_SHELLMENU_CLASSIC_EXTENDED, app->window, x, y);
    return true;
}

bool explorer_shift_click_picker(app_state_t *app, size_t index, double x, double y) {
    if (!rubraview_pal_shellmenu_shift_held()) return false;
    if (app->picker_selected && index < app->picker_listing.count && !app->picker_selected[index]) {
        for (size_t i = 0; i < app->picker_listing.count; ++i) app->picker_selected[i] = false;
        app->picker_selected[index] = true;
    }
    targets_for_picker(app, index);
    if (app->shell_target_count == 0) return false;
    show_menu(app, RUBRAVIEW_SHELLMENU_CLASSIC_EXTENDED, app->window, x, y);
    return true;
}

bool explorer_shift_click_row(app_state_t *app, size_t row, rubraview_window_t *window, double x, double y) {
    if (!rubraview_pal_shellmenu_shift_held()) return false;
    explorer_targets_list_row(app, row);
    if (app->shell_target_count == 0) return false;
    show_menu(app, RUBRAVIEW_SHELLMENU_CLASSIC_EXTENDED, window, x, y);
    return true;
}

/* A registry verb chosen. */
void explorer_verb_run(app_state_t *app, size_t verb) {
    if (verb >= app->shell_verb_count || app->shell_target_count == 0) return;
    if (!rubraview_pal_shellverbs_run(&app->shell_verbs[verb], (const char *const *)app->shell_targets, app->shell_target_count))
        osd_say(app, TU8("Windows could not start it"));
}
