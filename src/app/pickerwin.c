/*
 * The open-file screen: its places, bar and tools, thumbnails, filter, picks, and what a pick opens
 * (RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"
#include "rubraview/archive_job.h"

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

/* RV-227 (owner, 2026-11-06: "파일 관리자 우측 상단에 장바구니 버튼(그리고 그 아래에 추가 열기를 위한 아래 화살표)"); D-286
   (owner, 2026-11-07: "장바구니와 그 화살표는 가로로 나열된 그림 아이콘으로"; "도구 줄 오른쪽 끝"): the basket's icon and its
   arrow side by side at the tool row's right end, as high as its chips; the basket puts what is picked in a basket
   (or shows them), the arrow puts a basket's files here. Not in a book's pages. */
#define PICKER_BASKET_ICON_W 48.0
bool picker_basket_shown(const app_state_t *app) { return app->picker_open && !app->picker_in_book; }

rubraview_pal_rect_t picker_basket_rect(const app_state_t *app, double dpi, double win_w, int part) {
    double y = picker_top(app) + PICKER_HEADER_HEIGHT * dpi + 4.0 * dpi, h = PICKER_TOOL_HEIGHT * dpi - 8.0 * dpi;
    double w = PICKER_BASKET_ICON_W * dpi, gap = 4.0 * dpi;
    rubraview_pal_rect_t r = { win_w - 8.0 * dpi - 2.0 * w - gap, y, 2.0 * w + gap, h };
    if (part == 1) r.width = w;
    if (part == 2) { r.x += w + gap; r.width = w; }
    return r;
}

/* Where the tool row's chips stop: before the basket's icons. */
double picker_tools_end(const app_state_t *app, double dpi, double win_w) {
    return picker_basket_shown(app) ? picker_basket_rect(app, dpi, win_w, 0).x - 6.0 * dpi : win_w;
}

/* What stands at the path's and the places' right ends. */
static double picker_right_edge(const app_state_t *app, double dpi, double win_w) {
    (void)app; (void)dpi;
    return win_w;
}

/* 1.24.0: the favourites window's chip at the places bar's right end; the places stop short of it. */
rubraview_pal_rect_t picker_favwin_chip(const app_state_t *app, double dpi, double win_w) {
    double w = picker_chip_width(TU8("\xE2\x98\x85 Favourites"), dpi);
    return (rubraview_pal_rect_t){ picker_right_edge(app, dpi, win_w) - w - 8.0 * dpi, picker_top(app) + PICKER_CRUMB_HEIGHT * dpi + 2.0 * dpi, w, PICKER_PLACES_HEIGHT * dpi - 8.0 * dpi };
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

/* D-260 (owner, 2026-11-02: "하단에 압축하기와 압축풀기 등의 버튼들을 상황에 맞게"): the bottom shows what fits the place
   and what is picked (rubraview_picker_bottom_shows), in PICKER_BOTTOM's order; Cancel and Open always. */
int32_t picker_bottom_list(const app_state_t *app, picker_button_t *out) {
    rubraview_picker_place_t place = { .in_book = app->picker_in_book, .in_archive = picker_in_archive(app), .add_mode = app->picker_add_mode };
    for (size_t i = 0; app->picker_selected && !place.in_archive && i < app->picker_listing.count; ++i) {
        if (!app->picker_selected[i] || picker_is_up(app, i)) continue;
        const rubraview_fs_entry_t *e = &app->picker_listing.entries[i];
        place.picked++;
        if (e->is_directory) continue;
        if (rubraview_glob_match_list(e->name, PROVEN_LIT(ARCHIVE_FILTER_ALL))) place.picked_archives++;
        if (picker_viewer_opens(e->name)) place.picked_viewer++;
    }
    uint32_t shows = rubraview_picker_bottom_shows(&place);
    int32_t n = 0;
    for (int32_t i = 0; i < PICKER_BOTTOM_COUNT; ++i) {
        picker_button_t b = PICKER_BOTTOM[i];
        uint32_t need = b == PICKER_BTN_PLAYLIST ? RUBRAVIEW_PICKER_SHOW_LIST : b == PICKER_BTN_EXTRACT ? RUBRAVIEW_PICKER_SHOW_EXTRACT
                      : b == PICKER_BTN_COMPRESS ? RUBRAVIEW_PICKER_SHOW_COMPRESS
                      : b == PICKER_BTN_RECYCLE || b == PICKER_BTN_MOVE || b == PICKER_BTN_COPY ? RUBRAVIEW_PICKER_SHOW_FILES : 0;
        if (need == 0 || (shows & need)) out[n++] = b;
    }
    return n;
}

/* The slots keep their width; what acts fills from the left, Cancel and Open stay at the right. */
int32_t picker_bottom_slot(int32_t index, int32_t shown) {
    return index >= shown - 2 ? PICKER_BOTTOM_COUNT - (shown - index) : index;
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
        case PICKER_TOOL_EXT:        n = rubraview_fmt(buf, cap, ".ext \xE2\x96\xBE"); break;   /* RV-204 */
        case PICKER_TOOL_ALL:        n = rubraview_fmt(buf, cap, "{}", PROVEN_ARG(T("All"))); break;
        case PICKER_TOOL_INDIVIDUAL: n = rubraview_fmt(buf, cap, "{}", PROVEN_ARG(T("Individual"))); break;
        case PICKER_TOOL_RANGE:      n = rubraview_fmt(buf, cap, "{}", PROVEN_ARG(T("Range"))); break;
        case PICKER_TOOL_SAME:       n = rubraview_fmt(buf, cap, "{}", PROVEN_ARG(T("Same type"))); break;
        case PICKER_TOOL_MEDIA:      n = rubraview_fmt(buf, cap, "{}", PROVEN_ARG(T("Media"))); break;
        case PICKER_TOOL_SHAPE:      n = rubraview_fmt(buf, cap, "{} \xE2\x96\xBE", PROVEN_ARG(T("Shape"))); break;   /* RV-192 */
        case PICKER_TOOL_SORT:
            n = rubraview_fmt(buf, cap, "{} \xE2\x96\xBE", PROVEN_ARG(key >= 0 && key < RUBRAVIEW_PICKER_SORT_COUNT
                                                       ? rubraview_picker_sort_name((rubraview_picker_sort_key_t)key) : T("Sort by")));
            break;
        case PICKER_TOOL_ORDER:      n = rubraview_fmt(buf, cap, "{} \xE2\x96\xBE", PROVEN_ARG(desc ? T("Falling") : T("Rising"))); break;
        case PICKER_TOOL_VIEW:       n = rubraview_fmt(buf, cap, "{}", PROVEN_ARG(picker_details(app) ? T("Tiles") : T("Details"))); break;
        case PICKER_TOOL_SHOWN:      n = rubraview_fmt(buf, cap, "{}", PROVEN_ARG(picker_every_file(app) ? T("Every file") : T("Viewer's files"))); break;
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
        x += w + (i == PICKER_TOOL_EXT || i == PICKER_TOOL_ALL || i == PICKER_TOOL_SHAPE || i == PICKER_TOOL_ORDER || i == PICKER_TOOL_VIEW ? 14.0 : 4.0) * dpi;   /* gaps between the kinds */
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
    rubraview_free(app->picker_thumbs);
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

size_t picker_picked_count(const app_state_t *app) {
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
    app->picker_thumbs = (struct picker_thumb *)rubraview_alloc_zero(n ? n : 1, sizeof(*app->picker_thumbs));
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
    rubraview_free(app->picker_filtered); app->picker_filtered = NULL;
    rubraview_free(app->picker_filtered_selected); app->picker_filtered_selected = NULL;
    rubraview_free(app->picker_map); app->picker_map = NULL;
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
    proven_sys_mem_copy(app->picker_filter, text.ptr, keep);
    app->picker_filter[keep] = '\0';
    if (keep == 0) {
        app->picker_listing = app->picker_full;
        app->picker_selected = app->picker_full_selected;
        picker_reinstall(app);
        return;
    }
    size_t n = app->picker_full.count;
    app->picker_filtered = (rubraview_fs_entry_t *)rubraview_alloc((n ? n : 1) * sizeof(rubraview_fs_entry_t));
    app->picker_filtered_selected = (bool *)rubraview_alloc_zero(n ? n : 1, sizeof(bool));
    app->picker_map = (size_t *)rubraview_alloc((n ? n : 1) * sizeof(size_t));
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
    int n = app->picker_filter[0] ? rubraview_fmt(buf, cap, T("Filter: {}   \xC3\x97"), PROVEN_ARG(app->picker_filter))
                                  : rubraview_fmt(buf, cap, "{}", PROVEN_ARG(T("Filter names (Ctrl+F)")));
    *out_label = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)buf, .size = n > 0 ? (size_t)n : 0 };
    double w = picker_chip_width(*out_label, dpi);
    /* RV-229: a narrow window leaves the path some room: the chip's one word */
    if (!app->picker_filter[0] && picker_right_edge(app, dpi, win_w) - w - 8.0 * dpi < (PICKER_CRUMB_START + 160.0) * dpi) {
        n = rubraview_fmt(buf, cap, "{}", PROVEN_ARG(T("Filter")));
        *out_label = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)buf, .size = n > 0 ? (size_t)n : 0 };
        w = picker_chip_width(*out_label, dpi);
    }
    double crumb_h = PICKER_CRUMB_HEIGHT * dpi;
    return (rubraview_pal_rect_t){ picker_right_edge(app, dpi, win_w) - w - 8.0 * dpi, picker_top(app) + 6.0 * dpi, w, crumb_h - 12.0 * dpi };
}

/* RV-213 (owner, 2026-10-31: "더블클릭하면 해당 파일 열기 또는 폴더 열기"): a click on tile `index` is its first (1) or,
   quickly on the same one, its second (2); 0 when the first went into a folder and this one is not for what is there. */
int picker_taps(app_state_t *app, size_t index) {
    double now = rubraview_pal_time_now_seconds();
    if (app->picker_tap_generation != app->picker_generation && now - app->picker_tap_at < PICKER_DOUBLE_TAP) {
        app->picker_tap_at = 0.0;
        return 0;
    }
    bool twice = index == app->picker_tap_index && app->picker_tap_generation == app->picker_generation &&
                 now - app->picker_tap_at < PICKER_DOUBLE_TAP;
    app->picker_tap_at = twice ? 0.0 : now;
    app->picker_tap_index = index;
    app->picker_tap_generation = app->picker_generation;
    return twice ? 2 : 1;
}

/* D-286 (owner, 2026-11-07: "클릭 한번에 폴더나 파일이 바로 열리는데, 더블클릭해야 열리게"; "그것 하나만 고르기"): one
   click picks that one alone, the others let go; `..` is only where the focus is. */
void picker_pick_alone(app_state_t *app, size_t index) {
    if (!app->picker_selected || index >= app->picker_listing.count || picker_is_up(app, index)) return;
    for (size_t i = 0; i < app->picker_listing.count; ++i) app->picker_selected[i] = i == index;
    app->picker_anchor = index;
}

/* RV-263 (D-304 follow-up): the file keys on the open-file screen act on what is picked there, or — nothing picked —
   on the tile with the focus (picked for it, so the screen shows what they act on): Delete as the Recycle button
   (pressed twice), rename in the rename box, copy / move to a folder as their buttons. Never on what is behind. */
bool picker_file_key(app_state_t *app, proven_u8str_view_t action) {
    bool del = proven_u8str_view_eq(action, PROVEN_LIT("delete_file")), ren = proven_u8str_view_eq(action, PROVEN_LIT("rename_file"));
    bool cpy = proven_u8str_view_eq(action, PROVEN_LIT("copy_to_folder")), mov = proven_u8str_view_eq(action, PROVEN_LIT("move_to_folder"));
    if (!del && !ren && !cpy && !mov) return false;
    size_t focus = app->picker.focus;
    bool on_tile = focus < app->picker_listing.count && !picker_is_up(app, focus) && !rubraview_picker_is_places(app->picker_dir);
    if (ren) {
        if (on_tile) rename_picker_begin(app, focus);
        return true;
    }
    if (picker_picked_count(app) == 0 && on_tile) picker_pick_alone(app, focus);
    picker_button(app, del ? PICKER_BTN_RECYCLE : mov ? PICKER_BTN_MOVE : PICKER_BTN_COPY);
    return true;
}

/* RV-229: the first of the path's crumbs drawn — the last ones kept between the arrows and the filter's chip (the star
   after them, left out too when the last crumb would not fit beside it: Ctrl+D stars), a chip standing for those left
   out before them. render.c and events.c lay them out alike. */
size_t picker_crumbs_first(const app_state_t *app, const rubraview_breadcrumbs_t *crumbs, double dpi, double win_w, bool *out_star) {
    double widths[RUBRAVIEW_PICKER_MAX_CRUMBS];
    for (size_t i = 0; i < crumbs->count; ++i) widths[i] = picker_chip_width(crumbs->items[i].label, dpi);
    char buf[200];
    proven_u8str_view_t label;
    double room = picker_filter_chip(app, win_w, dpi, &label, buf, sizeof(buf)).x - 6.0 * dpi - PICKER_CRUMB_START * dpi;
    double gap = 4.0 * dpi, more_w = picker_chip_width(PROVEN_LIT(PICKER_CRUMBS_MORE), dpi);
    proven_u8str_view_t star = picker_star_label(app);
    double star_w = star.size > 0 ? picker_chip_width(star, dpi) + gap : 0.0;
    double least = crumbs->count == 0 ? 0.0 : widths[crumbs->count - 1] + gap + (crumbs->count > 1 ? more_w + gap : 0.0);
    *out_star = star.size > 0 && room - star_w >= least;
    if (*out_star) room -= star_w;
    return rubraview_picker_crumbs_first(widths, crumbs->count, gap, room, more_w);
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
    int k = all ? rubraview_fmt(line, sizeof(line), "{}", PROVEN_ARG(T("nothing picked"))) : rubraview_fmt(line, sizeof(line), T("{} picked"), PROVEN_ARG((size_t)rows));
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
    int k = rubraview_fmt(line, sizeof(line), pick ? T("{} pictures, films, songs and texts picked") : T("{} let go"), PROVEN_ARG((size_t)found));
    if (k > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)k });
}

/* The picked files, and in a picked folder its pictures, films, music and text, joined to the list on screen. */
void picker_add_to_list(app_state_t *app) {
    rubraview_fs_entry_t *entries = NULL;
    size_t n = picker_picked_entries(app, &entries);
    if (n == 0) { osd_say(app, TU8("pick some files first")); return; }
    size_t cap = 4096, count = 0;
    proven_u8str_view_t *paths = (proven_u8str_view_t *)rubraview_alloc(cap * sizeof(proven_u8str_view_t));
    if (!paths) return;
    char kinds_buf[1024];
    const char *const KINDS = (const char * const)kinds_with_text(kinds_buf, sizeof(kinds_buf), IMAGE_FILTER ";" MEDIA_FILTER).ptr;
    for (size_t i = 0; i < n && count < cap; ++i) {
        if (!entries[i].is_directory) {   /* D-258: what the viewer does not show stays out */
            if (picker_viewer_opens(entries[i].name)) paths[count++] = app_keep(app, entries[i].path);
            continue;
        }
        rubraview_fs_listing_t inside = rubraview_pal_fs_list_dir(app->arena, entries[i].path);
        for (size_t k = 0; k < inside.count && count < cap; ++k) {
            const rubraview_fs_entry_t *e = &inside.entries[k];
            if (!e->is_directory && rubraview_glob_match_list(e->name, cstr(KINDS))) paths[count++] = e->path;
        }
    }
    if (count == 0) { rubraview_free(paths); osd_say(app, TU8("nothing in them to add")); return; }
    bool closing = app->picker_add_mode;
    if (closing) { app->picker_open = false; app->picker_add_mode = false; }
    listwin_sync(app);
    list_insert_paths(app, paths, count, page_count(app));
    char line[64];
    int k = rubraview_fmt(line, sizeof(line), T("{} added to the list"), PROVEN_ARG((size_t)count));
    if (k > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)k });
    rubraview_free(paths);
}

/* The whole paths of what is picked, a line each, to the clipboard. */
void picker_copy_paths(app_state_t *app) {
    rubraview_fs_entry_t *entries = NULL;
    size_t n = picker_picked_entries(app, &entries);
    if (n == 0) return;
    size_t len = 0;
    for (size_t i = 0; i < n; ++i) len += entries[i].path.size + 2u;
    char *text = (char *)rubraview_alloc(len + 1u);
    if (!text) return;
    size_t at = 0;
    for (size_t i = 0; i < n; ++i) {
        proven_sys_mem_copy(text + at, entries[i].path.ptr, entries[i].path.size);
        at += entries[i].path.size;
        if (i + 1 < n) { text[at++] = '\r'; text[at++] = '\n'; }
    }
    bool ok = rubraview_pal_clipboard_set_text(app->window, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)text, .size = at });
    rubraview_free(text);
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
    if (explorer_shift_click_picker(app, index, x, y)) return;   /* RV-279: Shift, Explorer's classic menu at once */
    const rubraview_toolbox_profile_t *list = rubraview_boxes_context(rubraview_boxes_document(), many ? PROVEN_LIT("pickermany") : PROVEN_LIT("pickerone"));
    if (!list || list->tile_count <= 0) return;
    app->ctx_open = true;
    app->ctx_in_files = false;
    app->ctx_list = list;
    app->ctx_x = x;
    app->ctx_y = y;
    app->picker_ctx_index = index;
    explorer_targets_picker(app, index);   /* RV-279: the registry's verbs of what is picked */
}

/* RV-207: a right click past the tiles, on the folder itself: a new folder in it. */
void picker_context_here(app_state_t *app, double x, double y) {
    if (app->picker_in_book) return;
    ctx_close(app);
    slidepop_close(app);
    const rubraview_toolbox_profile_t *list = rubraview_boxes_context(rubraview_boxes_document(), PROVEN_LIT("pickerhere"));
    if (!list || list->tile_count <= 0) return;
    app->ctx_open = true;
    app->ctx_in_files = false;
    app->ctx_list = list;
    app->ctx_x = x;
    app->ctx_y = y;
    app->picker_ctx_index = app->picker_listing.count;   /* no tile */
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
#define PICKER_SHELL_OPEN_MAX 20

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
    size_t n = picker_picked_entries(app, &entries), files = 0, shell = 0, refused = 0, others = 0;
    /* D-268 (owner, 2026-11-03: ".ext 메뉴 정리"): what the viewer does not show goes to its Windows program, as one
       such file opened alone does (D-258); a program or script never, and twenty at most */
    for (size_t i = 0; i < n; ++i) {
        if (entries[i].is_directory) continue;
        if (picker_viewer_opens(entries[i].name)) { entries[files++] = entries[i]; continue; }
        if (++others > PICKER_SHELL_OPEN_MAX) continue;
        bool dangerous = false;
        if (rubraview_shell_runs_program(entries[i].name) || !rubraview_pal_shell_open_file(entries[i].path, &dangerous)) refused++;
        else shell++;
    }
    char line[160];
    int k = files == 0 ? rubraview_fmt(line, sizeof(line), T("{} opened in their Windows programs"), PROVEN_ARG((size_t)shell))
          : shell == 0 ? rubraview_fmt(line, sizeof(line), T("{} files opened together"), PROVEN_ARG((size_t)files))
                       : rubraview_fmt(line, sizeof(line), T("{} files opened together, {} in their Windows programs"), PROVEN_ARG((size_t)files), PROVEN_ARG((size_t)shell));
    if (k > 0 && (size_t)k < sizeof line && refused > 0)
        k += rubraview_fmt(line + k, sizeof line - (size_t)k, T(";  {} not opened (programs, or Windows would not)"), PROVEN_ARG((size_t)refused));
    if (k > 0 && (size_t)k < sizeof line && others > PICKER_SHELL_OPEN_MAX)
        k += rubraview_fmt(line + k, sizeof line - (size_t)k, T(";  only the first {} of the others"), PROVEN_ARG((int)PICKER_SHELL_OPEN_MAX));
    if (k >= (int)sizeof line) k = (int)sizeof line - 1;
    if (files == 0) {
        if (others == 0) osd_say(app, TU8("pick some files first"));   /* folders alone */
        else osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = k > 0 ? (size_t)k : 0 });
        return;
    }
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
    /* RV-207: inside an archive only what reads it and picks in it; its names are no files of this disk */
    if (picker_in_archive(app)) {
        if (button == PICKER_BTN_OPEN) { if (app->picker.focus < app->picker_listing.count) picker_activate(app, app->picker.focus); return; }
        if (button == PICKER_BTN_EXTRACT) {
            archwork_extract_inside(app, (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("extract_mode"))) == 1
                                         ? RUBRAVIEW_EXTRACT_SMART : RUBRAVIEW_EXTRACT_FOLDER, true);   /* RV-240: where, asked */
            return;
        }
        if (button != PICKER_BTN_INDIVIDUAL && button != PICKER_BTN_RANGE && button != PICKER_BTN_SAME_TYPE && button != PICKER_BTN_MEDIA &&
            button != PICKER_BTN_CLEAR && button != PICKER_BTN_VIEW && button != PICKER_BTN_CANCEL) {
            osd_say(app, TU8("inside an archive: extract what you need first"));
            return;
        }
    } else if (button == PICKER_BTN_EXTRACT) {
        archwork_extract(app, (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("extract_mode"))) == 1
                              ? RUBRAVIEW_EXTRACT_SMART : RUBRAVIEW_EXTRACT_FOLDER, true);   /* RV-240: where, asked */
        return;
    } else if (button == PICKER_BTN_COMPRESS) {
        archwork_compress_ask(app);
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
            int written = rubraview_fmt(line, sizeof(line), T("{} {} file(s) picked"), PROVEN_ARG((size_t)n), PROVEN_ARG(rubraview_bytes_view(ext.ptr, (int)ext.size)));
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
                int k = rubraview_fmt(line, sizeof(line), T("{} files picked"), PROVEN_ARG((size_t)(rubraview_picker_select_all(&app->picker))));
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
        /* RV-207: nothing open either, the folder it was left in last time (layout.ini) */
        if (app->source_dir.size == 0 && app->picker_last.size > 0) dir = app->picker_last;
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

/* RV-207: into an archive, its whole tree listed as folders and files, without reading it (owner: "압축파일 열기";
   right-click Look inside, or Shift+Enter). A picture or a film in it opens the book there. */
bool picker_enter_archive(app_state_t *app, proven_u8str_view_t path) {
    char *p = rubraview_alloc(path.size + 1);
    rubraview_archive_index_t *ix = rubraview_alloc_zero(1, sizeof *ix);
    if (!p || !ix) { rubraview_free(p); rubraview_free(ix); return false; }
    proven_sys_mem_copy(p, path.ptr, path.size);
    p[path.size] = '\0';
    rubraview_archive_result_t r = (rubraview_archive_result_t)picker_index_read(app, p, app->archive_codepage, ix);   /* names locked: the session's passwords */
    if (r != RUBRAVIEW_ARCHIVE_DONE) {
        rubraview_free(p); rubraview_free(ix);
        osd_say(app, r == RUBRAVIEW_ARCHIVE_PASSWORD_NEEDED || r == RUBRAVIEW_ARCHIVE_PASSWORD_WRONG
                     ? TU8("its names are locked: extract it, and its password is asked") : TU8("that archive cannot be read"));
        return false;
    }
    picker_leave_archive(app);
    app->picker_arc_path = p;
    app->picker_arc_index = ix;
    picker_navigate(app, proven_u8str_view_from_cstr(p));
    app->picker.focus = 0;
    app->picker_open = true;
    return true;
}

bool picker_enter_book(app_state_t *app, size_t index) {
    if (index >= app->picker_listing.count || picker_in_archive(app)) return false;
    const rubraview_fs_entry_t *entry = &app->picker_listing.entries[index];
    if (entry->is_directory || !rubraview_glob_match_list(entry->name, PROVEN_LIT(ARCHIVE_FILTER_ALL))) return false;
    return picker_enter_archive(app, entry->path);
}

/* An archive with nothing the viewer shows (no picture, film, song or text): its tree, not a book of no pages. */
static bool archive_has_nothing_to_show(app_state_t *app, proven_u8str_view_t path) {
    char p[RUBRAVIEW_ARCHIVE_PATH];
    if (path.size >= sizeof p) return false;
    proven_sys_mem_copy(p, path.ptr, path.size);
    p[path.size] = '\0';
    rubraview_archive_index_t ix;
    if (rubraview_archive_index_read(p, app->archive_codepage, NULL, &ix) != RUBRAVIEW_ARCHIVE_DONE) return false;
    char kinds_buf[1024];
    proven_u8str_view_t kinds = kinds_with_text(kinds_buf, sizeof(kinds_buf), IMAGE_FILTER ";" MEDIA_FILTER);
    bool none = true;
    for (size_t i = 0; i < ix.count && none; ++i)
        if (!ix.items[i].is_dir && rubraview_glob_match_list(rubraview_path_basename(proven_u8str_view_from_cstr(ix.items[i].name)), kinds)) none = false;
    rubraview_archive_index_free(&ix);
    return none;
}

/* A file inside the archive looked into: a page opens the book there; anything else is extracted first. */
static void archive_open_entry(app_state_t *app, const rubraview_fs_entry_t *entry) {
    char rel[RUBRAVIEW_ARCHIVE_PATH];
    char kinds_buf[1024];
    if (!picker_archive_rel(app, entry->path, rel, sizeof rel)) return;
    if (!rubraview_glob_match_list(entry->name, kinds_with_text(kinds_buf, sizeof(kinds_buf), IMAGE_FILTER ";" MEDIA_FILTER))) {
        osd_say(app, TU8("extract it to open it (right click, Extract)"));
        return;
    }
    proven_u8str_view_t archive = app_keep(app, proven_u8str_view_from_cstr(app->picker_arc_path));
    /* opened there by open_path, now or after its password is typed */
    rubraview_free(app->open_wanted_archive);
    rubraview_free(app->open_wanted_entry);
    app->open_wanted_archive = rubraview_strdup(app->picker_arc_path);
    app->open_wanted_entry = rubraview_strdup(rel);
    app->picker_open = false;
    open_path(app, archive);
}

/* D-256 (owner, 2026-11-02: "cbz cb7 cbr 같은건 만화보기로, zip 7z rar 같은건 압축파일 보기로"; "위에 것들도 다
   압축파일로 ... 다음 압축파일은 만화로"): an archive that is not a comic, opened by the reader, lists its tree in the
   open-file screen; a comic (CBZ, CB7, CBR) is left to open_path, a book. False when it is not such an archive or its
   tree cannot be read (its names locked): the caller opens it as a book, which asks the password. */
bool open_archive_tree(app_state_t *app, proven_u8str_view_t path) {
    proven_u8str_view_t name = rubraview_path_basename(path);
    if (!rubraview_glob_match_list(name, PROVEN_LIT(ARCHIVE_FILTER_ALL)) ||
        rubraview_shell_kind_of(rubraview_path_ext(name)) == RUBRAVIEW_SHELL_COMICS) return false;
    if (rubraview_page_source_is_later_volume(name)) return false;   /* D-258: a RAR set's later volume, the book from its first */
    if (app->picker_places.count == 0) app->picker_places = rubraview_pal_fs_list_places(app->arena);
    return picker_enter_archive(app, app_keep(app, path));
}

/* What the reader opens (Explorer, a drop, the Open box, Recent, a favourite, a typed path): an archive's tree or
   open_path. The next archive, a book's own reopening and the start's last file stay open_path. */
void open_path_or_tree(app_state_t *app, proven_u8str_view_t path) {
    if (!open_archive_tree(app, path)) open_path(app, path);
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

    if (picker_in_archive(app)) { archive_open_entry(app, entry); return; }   /* RV-207 */
    if (open_archive_tree(app, entry->path)) return;   /* D-256 */
    /* D-254: a comic holding nothing the viewer shows, its tree too */
    if (rubraview_shell_kind_of(rubraview_path_ext(entry->name)) == RUBRAVIEW_SHELL_COMICS &&
        archive_has_nothing_to_show(app, entry->path) && picker_enter_archive(app, entry->path)) return;
    if (app->picker_in_book && app->picker_page_of && index >= app->picker_page_skip) {
        size_t page = app->picker_page_of[index - app->picker_page_skip];
        if (page != RUBRAVIEW_PICKER_NOT_A_PAGE && page < page_count(app)) {
            app->picker_open = false;
            go_to_spread(app, spread_for_chosen_page(app, (int32_t)page));
            update_precache(app);
            return;
        }
        /* D-258: in the book, a file that is no page has no file of its own on disk */
        osd_say(app, TU8("not a page: Look inside the archive to extract it"));
        return;
    }
    picker_open_file(app, entry->path);
}

/* D-258: a file on disk chosen in the open-file screen: the viewer's own kinds in the viewer, any other in the
   program Windows opens it with, never a program itself. */
void picker_open_file(app_state_t *app, proven_u8str_view_t path) {
    proven_u8str_view_t name = rubraview_path_basename(path);
    /* RV-255: a playlist asks how: as text, added to the list, or in place of it */
    if (rubraview_shell_kind_of(rubraview_path_ext(name)) == RUBRAVIEW_SHELL_PLAYLISTS) { playlist_ask_open(app, path); return; }
    if (picker_viewer_opens(name)) { app->picker_open = false; open_path(app, path); return; }
    if (rubraview_shell_runs_program(name)) { osd_say(app, TU8("a program is not run from here")); return; }
    bool dangerous = false;
    if (!rubraview_pal_shell_open_file(path, &dangerous))
        osd_say(app, dangerous ? TU8("a program is not run from here") : TU8("Windows would not open it"));
}
