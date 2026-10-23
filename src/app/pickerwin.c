/*
 * The open-file screen: its places, bar and tools, thumbnails, filter, picks, and what a pick opens
 * (RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* ---- Metro file picker (§3.15.2) ---- */


/* D-110: the picker as Explorer's details view — a row a file, with its
   type, size and date — when Settings > Files asks for it or the picker's
   own button does. */

bool picker_details(const app_state_t *app) {
    return rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_details")) > 0.5;
}

/* 1.24.0 (owner, 2026-10-19: "화면 좌우 폭에 따라 가로에 몇개씩 보여주는지가 적절하게"): as many tiles a row
   as are near PICKER_TILE_WIDE wide, at least two. */
int32_t picker_tile_columns(const app_state_t *app) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    int32_t n = (int32_t)((double)w / (PICKER_TILE_WIDE * dpi) + 0.5);
    return n < 2 ? 2 : n > 12 ? 12 : n;
}

int32_t picker_columns(const app_state_t *app) {
    return picker_details(app) ? 1 : picker_tile_columns(app);
}

/* A band above the path, the title bar's height, holding nothing to press:
   the floating title bar comes down over it, not over the path (owner,
   2026-10-09). With Windows' own title bar there is none. */
double picker_top(const app_state_t *app) {
    return app->frameless ? app->titlebar.height : 0.0;
}

/* Where the tiles or the rows begin, below the path, the places and — in
   the details view — the columns' names. */
double picker_grid_top(const app_state_t *app, double dpi) {
    return picker_top(app) + (PICKER_HEADER_HEIGHT + PICKER_TOOL_HEIGHT + PICKER_DETAIL_HEAD) * dpi;   /* 1.24.0: the tool row, then the heads */
}

/* A chip in the path or the places bar, as wide as its label. 1.24.0 (owner, 2026-10-19: narrower, but not
   too small for a finger): its letters counted (a Hangul or a Han one as two), a little room, 44 at least. */
double picker_chip_width(proven_u8str_view_t label, double dpi) {
    double units = 0.0;
    for (size_t i = 0; i < label.size;) {
        unsigned char b = (unsigned char)label.ptr[i];
        size_t n = b < 0x80 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
        uint32_t cp = b < 0x80 ? b : 0;
        if (n == 3 && i + 2 < label.size) cp = ((uint32_t)(b & 0x0F) << 12) | ((uint32_t)(label.ptr[i + 1] & 0x3F) << 6) | (uint32_t)(label.ptr[i + 2] & 0x3F);
        units += cp >= 0x1100 && n >= 3 ? 1.9 : 1.0;
        i += n;
    }
    double w = (units * 7.4 + 22.0) * dpi;
    return w < 44.0 * dpi ? 44.0 * dpi : w;
}

/* The places bar's items in order: the reader's favourites (D-43), then
   the PC page's places. False past the end. */

bool picker_bar_item(const app_state_t *app, size_t index, picker_bar_item_t *out) {
    if (index < app->favorites.count) {
        *out = (picker_bar_item_t){ rubraview_favorites_title(&app->favorites, index),
                                    app->favorites.paths[index], true };
        return true;
    }
    index -= app->favorites.count;
    if (index == 0) {   /* the recent files, a place of their own (owner, 2026-10-16) */
        *out = (picker_bar_item_t){ TU8("Recent"), PROVEN_LIT(RUBRAVIEW_PICKER_RECENT), false };
        return true;
    }
    index -= 1;
    if (index >= app->picker_places.count) return false;
    *out = (picker_bar_item_t){ rubraview_tr_u8(app->picker_places.entries[index].name), app->picker_places.entries[index].path, false };
    return true;
}

/* The star after the path: lit when the folder on screen is a favourite.
   None on the PC page, which is not a folder. */
proven_u8str_view_t picker_star_label(const app_state_t *app) {
    if (rubraview_picker_is_places(app->picker_dir) || rubraview_picker_is_recent(app->picker_dir) || app->picker_in_book) return PROVEN_LIT("");
    return rubraview_favorites_contains(&app->favorites, app->picker_dir) ? PROVEN_LIT("\xE2\x98\x85") : PROVEN_LIT("\xE2\x98\x86");
}

void osd_say(app_state_t *app, proven_u8str_view_t text);

/* The star pressed, or Ctrl+D: the folder on screen in or out, and the
   file written at once, so a crash later does not lose it. */
bool favorite_toggle_path(app_state_t *app, proven_u8str_view_t path);
void picker_toggle_favorite(app_state_t *app) {
    if (rubraview_picker_is_places(app->picker_dir) || rubraview_picker_is_recent(app->picker_dir) ||
        app->picker_dir.size == 0 || app->picker_in_book) return;
    (void)favorite_toggle_path(app, app->picker_dir);
}

void favorites_save(app_state_t *app) {
    if (app->favorites_path.size == 0) return;
    bool locked = rubraview_pal_config_lock(CONFIG_LOCK_MS);
    bool written = rubraview_pal_fs_write_file_atomic(app->favorites_path, rubraview_favorites_serialize(app->arena, &app->favorites));
    if (locked) rubraview_pal_config_unlock();
    if (!written) osd_say(app, TU8("could not write favorites.ini"));
}

/* 1.24.0: the favourites window's chip at the places bar's right end; the places stop short of it. */
rubraview_pal_rect_t picker_favwin_chip(const app_state_t *app, double dpi, double win_w) {
    double w = picker_chip_width(TU8("\xE2\x98\x85 Favourites"), dpi);
    return (rubraview_pal_rect_t){ win_w - w - 8.0 * dpi, picker_top(app) + PICKER_CRUMB_HEIGHT * dpi + 2.0 * dpi, w, PICKER_PLACES_HEIGHT * dpi - 8.0 * dpi };
}

/* Where the bar's chip `index` is, as drawn: false past the window's edge. */
bool picker_chip_span(const app_state_t *app, size_t index, double dpi, double win_w, double *out_x, double *out_w) {
    double x = 8.0 * dpi;
    picker_bar_item_t item;
    for (size_t i = 0; picker_bar_item(app, i, &item); ++i) {
        double w = picker_chip_width(item.label, dpi);
        if (!item.favorite && i == app->favorites.count && i > 0) x += 10.0 * dpi;
        if (x + w > picker_favwin_chip(app, dpi, win_w).x - 6.0 * dpi) return false;
        if (i == index) { *out_x = x; *out_w = w; return true; }
        x += w + 4.0 * dpi;
    }
    return false;
}

/* The favourite a drag let go at `x` goes to: the one whose middle it passed. */
size_t favorite_drop_index(const app_state_t *app, double x, double dpi, double win_w) {
    size_t to = 0;
    for (size_t i = 0; i < app->favorites.count; ++i) {
        double cx = 0.0, cw = 0.0;
        if (!picker_chip_span(app, i, dpi, win_w, &cx, &cw)) break;
        if (x >= cx + cw * 0.5) to = i;
    }
    if (app->fav_press >= 0 && (size_t)app->fav_press > to) {
        /* moving left: before the first chip whose middle is past x */
        to = app->favorites.count - 1;
        for (size_t i = 0; i < app->favorites.count; ++i) {
            double cx = 0.0, cw = 0.0;
            if (!picker_chip_span(app, i, dpi, win_w, &cx, &cw)) break;
            if (x < cx + cw * 0.5) { to = i; break; }
        }
    }
    return to;
}

/* A right press on a favourite (owner, 2026-09-30): the rename box with its name, all of it chosen. */
void favorite_rename_begin(app_state_t *app, size_t index) {
    if (index >= app->favorites.count || app->rename_active) return;
    proven_u8str_view_t now = rubraview_favorites_title(&app->favorites, index);
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    rubraview_textedit_set(&app->rename_edit, now.size < sizeof(app->rename_buffer) ? now : PROVEN_LIT(""));
    rubraview_textedit_select_all(&app->rename_edit);
    app->rename_active = true;
    app->rename_favorite = (int32_t)index;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, true);
    osd_say(app, TU8("a name for this favourite; empty gives the folder's name back"));
}

void rename_insert(app_state_t *app, proven_u8str_view_t text);
void rename_end(app_state_t *app);
void favorite_rename_commit(app_state_t *app) {
    rename_insert(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->rename_composing, .size = app->rename_composing_length });
    size_t index = (size_t)app->rename_favorite;
    proven_u8str_view_t name = app_keep(app, rubraview_textedit_text(&app->rename_edit));
    rename_end(app);
    if (rubraview_favorites_rename(&app->favorites, index, name)) {
        favorites_save(app);
        osd_say(app, TU8("favourite renamed"));
    }
}

/* The view asked for and the window as it is now, put into the picker's
   geometry: called before it is drawn, pressed or moved by a key, so a
   window sized since, or the view changed, never leaves it stale. */
void picker_layout(app_state_t *app) {
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    app->picker.columns = picker_columns(app);
    app->picker.tile_extent = (picker_details(app) ? PICKER_DETAIL_ROW : PICKER_TILE) * dpi;
    app->picker.viewport_extent = (double)win_h - picker_grid_top(app, dpi) - PICKER_ACTION_HEIGHT * dpi;
    rubraview_picker_scroll_by(&app->picker, 0.0);   /* inside what there is to scroll */
}

/* The buttons along the bottom of the picker (owner, 2026-09-23). The
   first three are modes and switches; the rest act on what is picked. */

/* 1.24.0 (owner, 2026-10-19): the bottom holds what acts on the picks; picking and sorting are over the list. */
/* 1.25.0 (owner, 2026-10-20: "우하단에 취소와 열기(또는 맥락에 따라서는 확인) 버튼"): Cancel and Open last, at the right. */

/* Where the bottom's button `index` (0..PICKER_BOTTOM_COUNT-1) sits, in client coordinates. */
rubraview_pal_rect_t picker_button_rect(double win_w, double win_h, double dpi, int32_t index) {
    double row_h = PICKER_BUTTON_ROW * dpi;
    double top = win_h - PICKER_ACTION_HEIGHT * dpi;
    double gap = 6.0 * dpi;
    double width = (win_w - gap * (PICKER_BOTTOM_COUNT + 1)) / (double)PICKER_BOTTOM_COUNT;
    return (rubraview_pal_rect_t){ gap + (double)index * (width + gap), top + gap * 0.5,
                                   width, row_h - gap };
}

/* Back, up and forward at the path bar's left end (which 0, 1, 2). */
rubraview_pal_rect_t picker_nav_rect(const app_state_t *app, double dpi, int which) {
    double crumb_h = PICKER_CRUMB_HEIGHT * dpi;
    return (rubraview_pal_rect_t){ (8.0 + (double)which * (PICKER_NAV_W + 4.0)) * dpi, picker_top(app) + 6.0 * dpi,
                                   PICKER_NAV_W * dpi, crumb_h - 12.0 * dpi };
}

/* The tool row over the list: what each chip is, in order. */

proven_u8str_view_t picker_tool_label(const app_state_t *app, int which, char *buf, size_t cap) {
    int key = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort"))) - 1;
    bool desc = rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort_desc")) > 0.5;
    int n = 0;
    switch (which) {
        case PICKER_TOOL_ALL:        n = snprintf(buf, cap, "%s", T("All")); break;
        case PICKER_TOOL_INDIVIDUAL: n = snprintf(buf, cap, "%s", T("Individual")); break;
        case PICKER_TOOL_RANGE:      n = snprintf(buf, cap, "%s", T("Range")); break;
        case PICKER_TOOL_SAME:       n = snprintf(buf, cap, "%s", T("Same type")); break;
        case PICKER_TOOL_MEDIA:      n = snprintf(buf, cap, "%s", T("Media")); break;
        case PICKER_TOOL_SORT:
            n = snprintf(buf, cap, "%s \xE2\x96\xBE", key >= 0 && key < RUBRAVIEW_PICKER_SORT_COUNT
                                                       ? rubraview_picker_sort_name((rubraview_picker_sort_key_t)key) : T("Sort by"));
            break;
        case PICKER_TOOL_ORDER:      n = snprintf(buf, cap, "%s \xE2\x96\xBE", desc ? T("Falling") : T("Rising")); break;
        case PICKER_TOOL_VIEW:       n = snprintf(buf, cap, "%s", picker_details(app) ? T("Tiles") : T("Details")); break;
        default: break;
    }
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)buf, .size = n > 0 ? (size_t)n : 0 };
}

/* Where the tool row's chip `which` is; the all box is a square with a word. */
rubraview_pal_rect_t picker_tool_rect(const app_state_t *app, double dpi, int which) {
    double y = picker_top(app) + PICKER_HEADER_HEIGHT * dpi, h = PICKER_TOOL_HEIGHT * dpi;
    double x = 8.0 * dpi;
    for (int i = 0; i <= which; ++i) {
        char buf[96];
        double w = picker_chip_width(picker_tool_label(app, i, buf, sizeof(buf)), dpi) + (i == PICKER_TOOL_ALL ? 26.0 * dpi : 0.0);
        if (i == which) return (rubraview_pal_rect_t){ x, y + 4.0 * dpi, w, h - 8.0 * dpi };
        x += w + (i == PICKER_TOOL_ALL || i == PICKER_TOOL_MEDIA || i == PICKER_TOOL_ORDER ? 14.0 : 4.0) * dpi;   /* gaps between the kinds */
    }
    return (rubraview_pal_rect_t){ 0 };
}

/* Lists a directory and orders it the way the viewer orders pages, so
   the picker and the page sequence agree. */
/* ---- D-34: the picker's thumbnails ---- */

void picker_thumbs_free(app_state_t *app) {
    for (size_t i = 0; i < app->picker_thumb_count; ++i) {
        if (app->picker_thumbs[i].texture) rubraview_pal_texture_destroy(app->picker_thumbs[i].texture);
    }
    free(app->picker_thumbs);
    app->picker_thumbs = NULL;
    app->picker_thumb_count = 0;
}

/* The tile's inner rectangle, as draw_picker draws it. */
void picker_tile_size(app_state_t *app, double *out_w, double *out_h) {
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    *out_w = (double)win_w / (double)picker_tile_columns(app) - 12.0 * dpi;
    if (picker_details(app)) { *out_w = *out_h = (PICKER_DETAIL_ROW - 4.0) * dpi; return; }   /* a small square in the row */
    *out_h = app->picker.tile_extent - 12.0 * dpi;
}

void thumbs_wake(void *context) {
    rubraview_pal_window_wake((rubraview_window_t*)context);
}

/* ---- 1.22.0 (owner, 2026-10-19): the picker's own work — Add from a list, the media picked at once,
   a filter on the names, Ctrl+A, folders picked, a check box on every tile, a right click (one, or many),
   a long press taken as a right click ---- */

bool picker_is_up(const app_state_t *app, size_t index) {
    return index < app->picker_listing.count && app->picker_listing.entries[index].is_directory &&
           proven_u8str_view_eq(app->picker_listing.entries[index].name, PROVEN_LIT(".."));
}

static size_t picker_picked_count(const app_state_t *app) {
    size_t n = 0;
    for (size_t i = 0; app->picker_selected && i < app->picker_listing.count; ++i) n += app->picker_selected[i] ? 1u : 0u;
    return n;
}

/* The picker made again over the rows in `picker_listing` and `picker_selected`, as a folder read anew is. */
static void picker_reinstall(app_state_t *app) {
    picker_thumbs_free(app);
    app->picker_generation++;
    if (app->thumbs) rubraview_pal_thumbs_generation(app->thumbs, app->picker_generation);
    size_t n = app->picker_listing.count;
    app->picker_thumbs = (struct picker_thumb *)calloc(n ? n : 1, sizeof(*app->picker_thumbs));
    app->picker_thumb_count = app->picker_thumbs ? n : 0;
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    rubraview_picker_mode_t mode = app->picker.mode;
    app->picker = rubraview_picker_create(&app->picker_listing, PICKER_TILE * dpi,
                                          (double)win_h - picker_top(app) - (PICKER_HEADER_HEIGHT + PICKER_ACTION_HEIGHT) * dpi,
                                          picker_columns(app));
    picker_layout(app);
    app->picker.selected = app->picker_selected;
    rubraview_picker_set_mode(&app->picker, mode);
    for (size_t i = 0; i < n; ++i) {
        if (!app->picker_listing.entries[i].is_directory) { app->picker.focus = i; break; }
    }
}

/* What is picked among the rows shown, written back to the folder's own rows: kept when the filter changes. */
static void picker_filter_sync(app_state_t *app) {
    if (!app->picker_map || !app->picker_full_selected || !app->picker_selected) return;
    for (size_t i = 0; i < app->picker_listing.count; ++i) app->picker_full_selected[app->picker_map[i]] = app->picker_selected[i];
}

static void picker_filter_free(app_state_t *app) {
    free(app->picker_filtered); app->picker_filtered = NULL;
    free(app->picker_filtered_selected); app->picker_filtered_selected = NULL;
    free(app->picker_map); app->picker_map = NULL;
}

/* A folder read: it is the whole of what a filter is taken from; no filter yet. */
void picker_filter_forget(app_state_t *app) {
    picker_filter_free(app);
    app->picker_full = app->picker_listing;
    app->picker_full_selected = app->picker_selected;
    app->picker_filter[0] = '\0';
}

bool name_holds(proven_u8str_view_t name, proven_u8str_view_t part) {
    if (part.size == 0) return true;
    for (size_t i = 0; i + part.size <= name.size; ++i) {
        if (proven_u8str_view_eq_ascii_ci(proven_u8str_view_slice(name, i, part.size), part)) return true;
    }
    return false;
}

/* The rows whose names hold `text` (the case of letters aside), `..` always; empty: all of them. What
   was picked stays picked, shown or not, and is there again when the filter is cleared. */
void picker_filter_set(app_state_t *app, proven_u8str_view_t text) {
    if (app->picker_in_book || !app->picker_full.entries) return;
    picker_filter_sync(app);
    picker_filter_free(app);
    size_t keep = text.size < sizeof(app->picker_filter) ? text.size : sizeof(app->picker_filter) - 1;
    memcpy(app->picker_filter, text.ptr, keep);
    app->picker_filter[keep] = '\0';
    if (keep == 0) {
        app->picker_listing = app->picker_full;
        app->picker_selected = app->picker_full_selected;
        picker_reinstall(app);
        return;
    }
    size_t n = app->picker_full.count;
    app->picker_filtered = (rubraview_fs_entry_t *)malloc((n ? n : 1) * sizeof(rubraview_fs_entry_t));
    app->picker_filtered_selected = (bool *)calloc(n ? n : 1, sizeof(bool));
    app->picker_map = (size_t *)malloc((n ? n : 1) * sizeof(size_t));
    if (!app->picker_filtered || !app->picker_filtered_selected || !app->picker_map) {
        picker_filter_free(app);
        app->picker_filter[0] = '\0';
        app->picker_listing = app->picker_full;
        app->picker_selected = app->picker_full_selected;
        picker_reinstall(app);
        return;
    }
    proven_u8str_view_t part = { .ptr = (const proven_byte_t *)app->picker_filter, .size = keep };
    size_t k = 0;
    for (size_t i = 0; i < n; ++i) {
        const rubraview_fs_entry_t *e = &app->picker_full.entries[i];
        bool up = e->is_directory && proven_u8str_view_eq(e->name, PROVEN_LIT(".."));
        if (!up && !name_holds(e->name, part)) continue;
        app->picker_filtered[k] = *e;
        app->picker_filtered_selected[k] = app->picker_full_selected ? app->picker_full_selected[i] : false;
        app->picker_map[k] = i;
        k++;
    }
    app->picker_listing = app->picker_full;
    app->picker_listing.entries = app->picker_filtered;
    app->picker_listing.count = k;
    app->picker_selected = app->picker_filtered_selected;
    picker_reinstall(app);
}

void picker_filter_begin(app_state_t *app) {
    if (!app->picker_open || app->picker_in_book) return;
    if (app->rename_active) rename_end(app);
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    rubraview_textedit_set(&app->rename_edit, cstr(app->picker_filter));
    rubraview_textedit_select_all(&app->rename_edit);
    app->rename_active = true;
    app->rename_is_filter = true;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, true);
}

/* Each pass while the filter is typed: the rows follow what is in the box. */
void picker_filter_follow(app_state_t *app) {
    if (!app->picker_open || !app->rename_active || !app->rename_is_filter) return;
    proven_u8str_view_t typed = rubraview_textedit_text(&app->rename_edit);
    if (!proven_u8str_view_eq(typed, cstr(app->picker_filter))) picker_filter_set(app, typed);
}

/* The filter's chip at the path bar's right end: the words, and where its X is. */
rubraview_pal_rect_t picker_filter_chip(const app_state_t *app, double win_w, double dpi, proven_u8str_view_t *out_label, char *buf, size_t cap) {
    int n = app->picker_filter[0] ? snprintf(buf, cap, T("Filter: %s   \xC3\x97"), app->picker_filter)
                                  : snprintf(buf, cap, "%s", T("Filter names (Ctrl+F)"));
    *out_label = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)buf, .size = n > 0 ? (size_t)n : 0 };
    double w = picker_chip_width(*out_label, dpi);
    double crumb_h = PICKER_CRUMB_HEIGHT * dpi;
    return (rubraview_pal_rect_t){ win_w - w - 8.0 * dpi, picker_top(app) + 6.0 * dpi, w, crumb_h - 12.0 * dpi };
}

/* A tile's check box: its top left corner (a row's left end in the details view). */
rubraview_pal_rect_t picker_check_rect(const app_state_t *app, size_t index, double win_w, double dpi) {
    double header_h = picker_grid_top(app, dpi);
    double cell = app->picker.tile_extent;
    double side = 20.0 * dpi;
    if (picker_details(app)) {
        double y = header_h + (double)index * cell - app->picker.scroll_offset;
        return (rubraview_pal_rect_t){ 6.0 * dpi, y + (cell - side) * 0.5, side, side };
    }
    size_t columns = (size_t)picker_tile_columns(app);
    double cell_w = win_w / (double)columns;
    size_t row = index / columns, column = index % columns;
    double x = (double)column * cell_w, y = header_h + (double)row * cell - app->picker.scroll_offset;
    return (rubraview_pal_rect_t){ x + 12.0 * dpi, y + 12.0 * dpi, side, side };
}

void draw_picker_check(app_state_t *app, size_t index, double win_w, double dpi) {
    if (app->picker_in_book || picker_is_up(app, index)) return;
    rubraview_pal_rect_t box = picker_check_rect(app, index, win_w, dpi);
    bool on = app->picker_selected && app->picker_selected[index];
    rubraview_pal_render_fill_rect(app->renderer, box, on ? 0xFF2E7BD6u : 0xB0101010u, 3.0 * dpi);
    rubraview_pal_render_stroke_rect(app->renderer, box, on ? 0xFFFFFFFFu : 0xC0FFFFFFu, 1.5 * dpi, 3.0 * dpi);
    if (on) rubraview_pal_render_draw_text(app->renderer, PROVEN_LIT("\xE2\x9C\x93"), box, box.height * 0.8, 0xFFFFFFFFu, RUBRAVIEW_TEXT_CENTER);
}

/* Every row (folders too, not `..`) picked, or, when all are, none. */
void picker_select_everything(app_state_t *app) {
    if (!app->picker_selected || app->picker_in_book) return;
    bool all = true;
    size_t rows = 0;
    for (size_t i = 0; i < app->picker_listing.count; ++i) {
        if (picker_is_up(app, i)) continue;
        rows++;
        if (!app->picker_selected[i]) all = false;
    }
    for (size_t i = 0; i < app->picker_listing.count; ++i) app->picker_selected[i] = !all && !picker_is_up(app, i);
    char line[64];
    int k = all ? snprintf(line, sizeof(line), "%s", T("nothing picked")) : snprintf(line, sizeof(line), T("%zu picked"), rows);
    if (k > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)k });
}

/* The pictures, films, songs and text files here picked at once; again, let go (owner: "누를 때마다 토글"). */
static void picker_select_media(app_state_t *app) {
    if (!app->picker_selected || app->picker_in_book) return;
    char kinds_buf[1024];
    const char *const KINDS = (const char * const)kinds_with_text(kinds_buf, sizeof(kinds_buf), IMAGE_FILTER ";" MEDIA_FILTER).ptr;
    size_t found = 0, on = 0;
    for (size_t i = 0; i < app->picker_listing.count; ++i) {
        const rubraview_fs_entry_t *e = &app->picker_listing.entries[i];
        if (e->is_directory || !rubraview_glob_match_list(e->name, cstr(KINDS))) continue;
        found++;
        on += app->picker_selected[i] ? 1u : 0u;
    }
    if (found == 0) { osd_say(app, TU8("no pictures, films, music or text here")); return; }
    bool pick = on < found;
    for (size_t i = 0; i < app->picker_listing.count; ++i) {
        const rubraview_fs_entry_t *e = &app->picker_listing.entries[i];
        if (!e->is_directory && rubraview_glob_match_list(e->name, cstr(KINDS))) app->picker_selected[i] = pick;
    }
    char line[96];
    int k = snprintf(line, sizeof(line), pick ? T("%zu pictures, films, songs and texts picked") : T("%zu let go"), found);
    if (k > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)k });
}

/* The picked files, and in a picked folder its pictures, films, music and text, joined to the list on screen. */
void picker_add_to_list(app_state_t *app) {
    rubraview_fs_entry_t *entries = NULL;
    size_t n = picker_picked_entries(app, &entries);
    if (n == 0) { osd_say(app, TU8("pick some files first")); return; }
    size_t cap = 4096, count = 0;
    proven_u8str_view_t *paths = (proven_u8str_view_t *)malloc(cap * sizeof(proven_u8str_view_t));
    if (!paths) return;
    char kinds_buf[1024];
    const char *const KINDS = (const char * const)kinds_with_text(kinds_buf, sizeof(kinds_buf), IMAGE_FILTER ";" MEDIA_FILTER).ptr;
    for (size_t i = 0; i < n && count < cap; ++i) {
        if (!entries[i].is_directory) { paths[count++] = app_keep(app, entries[i].path); continue; }
        rubraview_fs_listing_t inside = rubraview_pal_fs_list_dir(app->arena, entries[i].path);
        for (size_t k = 0; k < inside.count && count < cap; ++k) {
            const rubraview_fs_entry_t *e = &inside.entries[k];
            if (!e->is_directory && rubraview_glob_match_list(e->name, cstr(KINDS))) paths[count++] = e->path;
        }
    }
    if (count == 0) { free(paths); osd_say(app, TU8("nothing in them to add")); return; }
    bool closing = app->picker_add_mode;
    if (closing) { app->picker_open = false; app->picker_add_mode = false; }
    listwin_sync(app);
    list_insert_paths(app, paths, count, page_count(app));
    char line[64];
    int k = snprintf(line, sizeof(line), T("%zu added to the list"), count);
    if (k > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)k });
    free(paths);
}

/* The whole paths of what is picked, a line each, to the clipboard. */
void picker_copy_paths(app_state_t *app) {
    rubraview_fs_entry_t *entries = NULL;
    size_t n = picker_picked_entries(app, &entries);
    if (n == 0) return;
    size_t len = 0;
    for (size_t i = 0; i < n; ++i) len += entries[i].path.size + 2u;
    char *text = (char *)malloc(len + 1u);
    if (!text) return;
    size_t at = 0;
    for (size_t i = 0; i < n; ++i) {
        memcpy(text + at, entries[i].path.ptr, entries[i].path.size);
        at += entries[i].path.size;
        if (i + 1 < n) { text[at++] = '\r'; text[at++] = '\n'; }
    }
    bool ok = rubraview_pal_clipboard_set_text(app->window, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)text, .size = at });
    free(text);
    osd_say(app, ok ? (n == 1 ? TU8("its whole path is on the clipboard") : TU8("their whole paths are on the clipboard"))
                    : TU8("the clipboard would not take it"));
}

/* A right click (or a long press) on a tile: one thing — that one, picked — or the many picked. */
void picker_context(app_state_t *app, size_t index, double x, double y) {
    if (app->picker_in_book || index >= app->picker_listing.count || picker_is_up(app, index)) return;
    bool many = app->picker_selected && app->picker_selected[index] && picker_picked_count(app) > 1;
    if (!many && app->picker_selected) {
        for (size_t i = 0; i < app->picker_listing.count; ++i) app->picker_selected[i] = false;
        app->picker_selected[index] = true;
    }
    app->picker.focus = index;
    ctx_close(app);
    slidepop_close(app);
    const rubraview_toolbox_profile_t *list = rubraview_boxes_context(rubraview_boxes_document(), many ? PROVEN_LIT("pickermany") : PROVEN_LIT("pickerone"));
    if (!list || list->tile_count <= 0) return;
    app->ctx_open = true;
    app->ctx_in_files = false;
    app->ctx_list = list;
    app->ctx_x = x;
    app->ctx_y = y;
    app->picker_ctx_index = index;
}

/* A press held still on a tile is a right click (owner: "손가락 오래 누르는건 우클릭"). */
#define PICKER_HOLD_SECONDS 0.55
/* 1.25.0 (owner, 2026-10-20: "제자리에서 오래 누르고 있을 때도 우클릭과 동일한 동작"): a press on the picture held
   still for a while is a right click there; its release is spent. */
void view_hold_tick(app_state_t *app) {
    if (!app->view_pressed || app->view_dragging || app->picker_open) return;
    if (rubraview_pal_time_now_seconds() - app->view_press_at < PICKER_HOLD_SECONDS) return;
    app->view_pressed = false;
    ctx_open_at(app, app->view_press_x, app->view_press_y);
}

void picker_hold_tick(app_state_t *app) {
    if (!app->picker_hold || !app->picker_open) return;
    if (rubraview_pal_time_now_seconds() - app->picker_hold_at < PICKER_HOLD_SECONDS) return;
    app->picker_hold = false;
    picker_context(app, app->picker_hold_index, app->picker_hold_x, app->picker_hold_y);
}

/* One of the buttons along the bottom was pressed. */
/* 1.23.0 (owner, 2026-10-19: "자체 파일 탐색기에서 열기 버튼이 없어요"): the Open button. One picked, or
   none and the focus on one: that, as a tap opens it (a folder is gone into). Many: their files, as one set. */
static void picker_open_picked(app_state_t *app) {
    size_t picked = 0, one = 0;
    for (size_t i = 0; app->picker_selected && i < app->picker_listing.count; ++i)
        if (app->picker_selected[i]) { if (picked++ == 0) one = i; }
    if (picked == 0) {
        if (app->picker.focus < app->picker_listing.count) picker_activate(app, app->picker.focus);
        else osd_say(app, TU8("pick something to open first"));
        return;
    }
    if (picked == 1) { picker_activate(app, one); return; }
    rubraview_fs_entry_t *entries = NULL;
    size_t n = picker_picked_entries(app, &entries), files = 0;
    for (size_t i = 0; i < n; ++i) if (!entries[i].is_directory) entries[files++] = entries[i];
    if (files == 0) { osd_say(app, TU8("pick some files first")); return; }
    char line[64];
    int k = snprintf(line, sizeof(line), T("%zu files opened together"), files);
    app->picker_open = false;
    open_set_from_entries(app, entries, files, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = k > 0 ? (size_t)k : 0 });
}

void picker_button(app_state_t *app, picker_button_t button) {
    char line[128];
    /* A page inside a book is not a file: nothing here can pick, move,
       copy or recycle it. */
    if (button == PICKER_BTN_CANCEL) {   /* 1.25.0: what was on screen before, a picture or nothing */
        if (app->rename_active) rename_end(app);
        app->picker_open = false;
        app->picker_add_mode = false;
        return;
    }
    if (button == PICKER_BTN_OPEN && app->picker_add_mode) {   /* OK: what is picked joins the list, and back */
        picker_add_to_list(app);
        app->picker_open = false;
        app->picker_add_mode = false;
        return;
    }
    if (button == PICKER_BTN_OPEN) { picker_open_picked(app); return; }
    if (app->picker_in_book && button != PICKER_BTN_CLEAR && button != PICKER_BTN_VIEW) {
        osd_say(app, TU8("inside a book: tap a page to go to it"));
        return;
    }
    switch (button) {
        case PICKER_BTN_INDIVIDUAL:
            rubraview_picker_set_mode(&app->picker, app->picker.mode == RUBRAVIEW_PICK_INDIVIDUAL
                                                    ? RUBRAVIEW_PICK_SINGLE : RUBRAVIEW_PICK_INDIVIDUAL);
            osd_say(app, app->picker.mode == RUBRAVIEW_PICK_INDIVIDUAL
                         ? TU8("tap files to pick them one by one") : TU8("a tap opens again"));
            break;
        case PICKER_BTN_RANGE:
            rubraview_picker_set_mode(&app->picker, app->picker.mode == RUBRAVIEW_PICK_RANGE
                                                    ? RUBRAVIEW_PICK_SINGLE : RUBRAVIEW_PICK_RANGE);
            osd_say(app, app->picker.mode == RUBRAVIEW_PICK_RANGE
                         ? TU8("tap two files: everything between them turns over") : TU8("a tap opens again"));
            break;
        case PICKER_BTN_SAME_TYPE: {
            proven_u8str_view_t ext = picker_focus_extension(app);
            if (ext.size == 0) { osd_say(app, TU8("put the focus on a file first")); break; }
            if (app->picker.mode == RUBRAVIEW_PICK_SINGLE) {
                rubraview_picker_set_mode(&app->picker, RUBRAVIEW_PICK_INDIVIDUAL);
            }
            size_t n = rubraview_picker_select_extension(&app->picker, ext, true);
            int written = snprintf(line, sizeof(line), T("%zu %.*s file(s) picked"), n, (int)ext.size, ext.ptr);
            if (written > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)written });
            break;
        }
        case PICKER_BTN_MEDIA:
            picker_select_media(app);
            break;
        case PICKER_BTN_EXTENSION:
            rename_extension_begin(app);
            break;
        case PICKER_BTN_PLAYLIST:
            picker_add_to_list(app);   /* 1.24.0 (owner, 2026-10-19): "재생목록에 추가"; a playlist file is on the right click */
            break;
        case PICKER_BTN_RECYCLE:
            picker_recycle(app);
            break;
        case PICKER_BTN_MOVE:
        case PICKER_BTN_COPY:
            /* No numbered folder yet: straight to the box, where any folder is typed or made. */
            if (!curation_has_numbers(app)) { target_begin(app, button == PICKER_BTN_MOVE, true); break; }
            app->picker_pending = (int32_t)button;
            app->picker_has_pending = true;
            osd_say(app, button == PICKER_BTN_MOVE ? TU8("press 1-9 for the folder to move them to, or 0 for another folder")
                                                   : TU8("press 1-9 for the folder to copy them to, or 0 for another folder"));
            break;
        case PICKER_BTN_CLEAR:
            /* one button for both (owner, 2026-10-16): Select all until all are, then Deselect all */
            app->picker_has_pending = false;
            rubraview_confirm_clear(&app->picker_confirm);
            if (rubraview_picker_all_selected(&app->picker)) {
                rubraview_picker_clear_selection(&app->picker);
                osd_say(app, TU8("nothing picked"));
            } else {
                if (app->picker.mode == RUBRAVIEW_PICK_SINGLE) rubraview_picker_set_mode(&app->picker, RUBRAVIEW_PICK_INDIVIDUAL);
                char line[48];
                int k = snprintf(line, sizeof(line), T("%zu files picked"), rubraview_picker_select_all(&app->picker));
                if (k > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)k });
            }
            break;
        case PICKER_BTN_VIEW: {
            bool details = !picker_details(app);
            rubraview_settings_set(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_details"), details ? 1.0 : 0.0);
            picker_layout(app);
            rubraview_picker_reveal_focus(&app->picker);
            /* The pictures are asked for again at the new shape. */
            for (size_t i = 0; i < app->picker_thumb_count; ++i) {
                if (app->picker_thumbs[i].texture) rubraview_pal_texture_destroy(app->picker_thumbs[i].texture);
                app->picker_thumbs[i] = (struct picker_thumb){0};
            }
            app->picker_generation++;
            if (app->thumbs) rubraview_pal_thumbs_generation(app->thumbs, app->picker_generation);
            osd_say(app, details ? TU8("details: a row a file") : TU8("tiles"));
            break;
        }
        default:
            break;
    }
}

void picker_open(app_state_t *app) {
    proven_u8str_view_t dir = app->picker_dir;
    if (dir.size == 0) {
        /* Nothing open: the PC page, not the folder the viewer happened
           to be started in (owner, 2026-09-28). */
        dir = app->source_dir.size > 0 ? app->source_dir : PROVEN_LIT(RUBRAVIEW_PICKER_PLACES);
    }
    /* Reading a book: the picker opens inside it, at the page's own folder,
       on the page (owner, 2026-09-28). */
    int32_t reading = current_page_index(app);
    if (app->source.kind != RUBRAVIEW_PAGE_SOURCE_FOLDER && app->source.archive_path.size > 0 && reading >= 0 &&
        (app->picker_dir.size == 0 || !rubraview_picker_inside_archive(app->picker_dir, app->source.archive_path, NULL))) {
        proven_u8str_view_t folder = rubraview_path_dirname(app->source.pages[reading].name);
        dir = folder.size > 0 ? rubraview_path_join(app->arena, app->source.archive_path, folder) : app->source.archive_path;
    }
    if (app->picker_places.count == 0) app->picker_places = rubraview_pal_fs_list_places(app->arena);
    picker_navigate(app, dir);
    app->picker_open = app->picker_listing.count > 0;
    if (app->picker_in_book && app->picker_page_of && reading >= 0) {
        for (size_t i = app->picker_page_skip; i < app->picker_listing.count; ++i) {
            if (app->picker_page_of[i - app->picker_page_skip] != (size_t)reading) continue;
            app->picker.focus = i;
            rubraview_picker_reveal_focus(&app->picker);
            break;
        }
    }
}

/* Into an archive without reading it first (right-click, or Shift+Enter;
   owner, 2026-09-29): the book is opened — cheap now that it is mapped —
   and the picker stays, at the book's top, to choose where to start. */
bool picker_enter_book(app_state_t *app, size_t index) {
    if (index >= app->picker_listing.count) return false;
    const rubraview_fs_entry_t *entry = &app->picker_listing.entries[index];
    if (entry->is_directory || !rubraview_glob_match_list(entry->name, PROVEN_LIT(ARCHIVE_FILTER))) return false;
    open_path(app, entry->path);
    if (app->source.kind == RUBRAVIEW_PAGE_SOURCE_FOLDER || app->source.archive_path.size == 0) return false;
    picker_navigate(app, app->source.archive_path);
    app->picker.focus = 0;
    app->picker_open = true;
    return true;
}

/* Activating a tile enters a folder or opens a file. */
void picker_activate(app_state_t *app, size_t index) {
    if (index >= app->picker_listing.count) return;
    const rubraview_fs_entry_t *entry = &app->picker_listing.entries[index];

    if (entry->is_directory) {
        picker_navigate(app, entry->path);
        app->picker.focus = 0;
        return;
    }

    app->picker_open = false;
    if (app->picker_in_book && app->picker_page_of && index >= app->picker_page_skip) {
        size_t page = app->picker_page_of[index - app->picker_page_skip];
        if (page != RUBRAVIEW_PICKER_NOT_A_PAGE && page < page_count(app)) {
            go_to_spread(app, spread_for_chosen_page(app, (int32_t)page));
            update_precache(app);
            return;
        }
    }
    open_path(app, entry->path);
}
