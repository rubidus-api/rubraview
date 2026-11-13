/*
 * RV-276 (D-341): books as rows of the list, and folders pasted into it. Owner, 2026-11-13: "만화파일도 재생목록에 넣을
 * 수 있게 ... 여러개의 만화파일이 이미지 파일들과 같이 재생목록에 들어가면 그냥 마치 하나의 만화책처럼 파일들이 주욱 연이어서
 * 보이게"; asked how, "(나)가 좋겠네요": a list above the sources. A comic (or an archive the reader lets in) is one row;
 * turned to, it opens in the row's place, and past its last page (or before its first) the list is opened again on
 * the row after (or before), at once. The page counter, the filmstrip and two-page spreads are the book's own.
 */
#include "app.h"
#include "rubraview/sort.h"

#define LIST_WALK_MAX 20000u   /* files a folder pasted brings at most */

bool path_is_book_row(proven_u8str_view_t path) {
    return rubraview_list_row_kind(rubraview_path_basename(path)) != RUBRAVIEW_LIST_ROW_FILE;
}

/* What a list takes as a row: what a folder shows (pictures, films, music, text, stations), and books. */
bool path_is_list_row_kind(proven_u8str_view_t path) {
    return path_is_listed_kind(path) || path_is_book_row(path);
}

bool list_row_is_book(const app_state_t *app, size_t page) {
    return app->list_is_set && app->source.archive_path.size == 0 && page < page_count(app) &&
           path_is_book_row(app->source.pages[page].path);
}

void book_forget(app_state_t *app) {
    app->book_list = NULL;   /* in the working arena */
    app->book_list_n = 0;
    app->book_active = app->book_wanted = false;
}

/* A turn landed (go_to_spread) or a list opened: looked at once the pass is done, never in the middle of a turn. */
void book_settle_request(app_state_t *app, bool back) {
    if (!app->list_is_set) return;
    app->book_settle = true;
    app->book_settle_back = back;
}

static void say_book(app_state_t *app, const char *format, proven_u8str_view_t name) {
    char line[700];
    int m = rubraview_fmt(line, sizeof(line), format, PROVEN_ARG((size_t)(app->book_row + 1)), PROVEN_ARG(app->book_list_n),
                          PROVEN_ARG(rubraview_bytes_view(name.ptr, (int)(name.size < 400 ? name.size : 400))));
    if (m > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)m < sizeof(line) ? (size_t)m : sizeof(line) - 1 });
}

/* finish_open's last step: the book a row asked for is open (its first page, or its last when reached going back);
   anything else opened ends the list's hold on it. */
void book_after_open(app_state_t *app) {
    if (app->book_list && !app->book_restoring) {
        proven_u8str_view_t want = app->book_list[app->book_row].path;
        if (app->source.archive_path.size > 0 && rubraview_path_same(app->source.archive_path, want)) {
            bool first = app->book_wanted;
            app->book_wanted = false;
            app->book_active = true;
            if (first) {
                app->resume_offer = false;   /* the list reads it from its start (or its end), not where it was left */
                if (app->book_back && app->layout.count > 1) go_to_spread(app, app->layout.count - 1);
                else if (app->layout.count > 0) go_to_spread(app, spread_for_first_page(app));
                update_precache(app);
                say_book(app, T("row {} of {} in the list, a book: {}"), rubraview_path_basename(want));
            }
        } else {
            book_forget(app);
        }
    }
    app->book_hold = 0;
    book_settle_request(app, app->book_restoring && app->book_back);
}

/* The list again, on row `to`: leaving a book, or one that could not be opened. */
static void book_restore(app_state_t *app, size_t to, bool back) {
    rubraview_fs_entry_t *entries = app->book_list;
    size_t n = app->book_list_n;
    if (!entries || to >= n) { book_forget(app); return; }
    char said[96];
    rubraview_fmt(said, sizeof(said), T("the list, row {} of {}"), PROVEN_ARG((size_t)(to + 1)), PROVEN_ARG(n));
    app->book_restoring = true;
    app->book_back = back;
    history_remember(app);
    unload_all_pages(app);
    open_set_ordered(app, entries, n, cstr(said), true, entries[to].path);
    app->book_restoring = false;
    book_forget(app);
}

/* The row's book did not open (unreadable, no pictures in it, its password not given): the list goes on past it the
   way the reader was going; with nothing past it, the list stays on that row and does not try it again. */
static void book_failed(app_state_t *app, bool password) {
    size_t row = app->book_row, n = app->book_list_n;
    bool back = app->book_back;
    size_t to = rubraview_list_row_after(row, n, !back, false);
    proven_u8str_view_t name = app->book_list && row < n ? rubraview_path_basename(app->book_list[row].path) : PROVEN_LIT("");
    char held[700];
    size_t keep = name.size < sizeof(held) - 1 ? name.size : sizeof(held) - 1;
    proven_sys_mem_copy(held, name.ptr, keep);
    held[keep] = '\0';
    book_restore(app, to == SIZE_MAX ? row : to, back);
    if (to == SIZE_MAX) app->book_hold = row + 1;
    char line[800];
    int m = rubraview_fmt(line, sizeof(line), password ? T("not opened (no password): {}; the list goes on") : T("could not be read as a book: {}; the list goes on"),
                          PROVEN_ARG(rubraview_bytes_view(held, (int)keep)));
    if (m > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)m < sizeof(line) ? (size_t)m : sizeof(line) - 1 });
}

/* The book of list row `page` opened in the row's place; the list kept to come back to. */
static void book_enter(app_state_t *app, size_t page, bool back) {
    size_t n = page_count(app);
    if (page >= n || app->arc_job || !subs_may_leave(app)) return;
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, n, sizeof(rubraview_fs_entry_t));
    if (!proven_is_ok(res.err)) return;
    rubraview_fs_entry_t *entries = (rubraview_fs_entry_t*)(void*)res.value.ptr;
    for (size_t i = 0; i < n; ++i) entries[i] = (rubraview_fs_entry_t){ .name = app->source.pages[i].name, .path = app->source.pages[i].path };
    app->book_list = entries;
    app->book_list_n = n;
    app->book_row = page;
    app->book_back = back;
    app->book_wanted = true;
    app->book_active = false;
    proven_u8str_view_t path = entries[page].path;
    app_log(app, "open", path);
    history_remember(app);
    unload_all_pages(app);
    app->names_codepage = -1;
    if (open_archive(app, path)) {
        recent_note(app, path);
        finish_open(app, 0);   /* book_after_open turns to its first or last page */
        return;
    }
    if (app->rename_active && app->rename_is_password) return;   /* its password asked: book_pump watches the answer */
    book_failed(app, false);
}

/* An archive's row the reader skips (Settings, or the card): the row after it, the way the reader was going. */
static void book_step_over(app_state_t *app, size_t page, bool back) {
    size_t to = rubraview_list_row_after(page, page_count(app), !back, false);
    if (to == SIZE_MAX) {
        app->book_hold = page + 1;
        osd_say(app, back ? TU8("an archive skipped, and nothing before it") : TU8("an archive skipped, and nothing after it"));
        return;
    }
    app->book_hold = 0;
    go_to_spread(app, spread_for_chosen_page(app, (int32_t)to));
    update_precache(app);
}

/* past_the_end, in a book of the list: the list's next row (or the one before), whatever the setting says of a
   book alone; round to the list's other end when it says go round. */
bool book_leave(app_state_t *app, bool forward) {
    if (!app->book_active || !app->book_list) return false;
    int mode = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("at_ends")));
    size_t to = rubraview_list_row_after(app->book_row, app->book_list_n, forward, mode == 2);
    if (to == SIZE_MAX) {
        osd_say(app, forward ? TU8("the last of the list") : TU8("the first of the list"));
        return true;
    }
    if (!subs_may_leave(app)) return true;
    book_restore(app, to, !forward);
    return true;
}

/* Once a pass: a password given up; a turn that landed on a book's row. */
size_t book_pump(app_state_t *app) {
    if (app->book_wanted && !app->book_active && !(app->rename_active && app->rename_is_password)) {
        book_failed(app, true);
        return 1;
    }
    if (!app->book_settle || window_taking_input(app) || app->book_ask) return 0;
    app->book_settle = false;
    int32_t page = current_page_index(app);
    if (page < 0 || !list_row_is_book(app, (size_t)page)) { app->book_hold = 0; return 0; }
    if (app->book_hold == (size_t)page + 1) return 0;
    app->book_hold = 0;
    bool back = app->book_settle_back;
    if (rubraview_list_row_kind(rubraview_path_basename(app->source.pages[page].path)) == RUBRAVIEW_LIST_ROW_ARCHIVE) {
        int how = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("archive_in_list")));
        if (how == 0) {
            app->book_ask = true;
            app->book_ask_page = page;
            app->book_ask_back = back;
            return 1;
        }
        if (how == 2) { book_step_over(app, (size_t)page, back); return 1; }
    }
    book_enter(app, (size_t)page, back);
    return 1;
}

/* ---- the card: an archive (not a comic) reached in the list ---- */

static const char *BOOK_ASK_WORDS[4] = { N_("Read it as a book"), N_("Skip it"), N_("Always as books"), N_("Always skip") };

void book_ask_answer(app_state_t *app, int which) {
    if (!app->book_ask) return;
    app->book_ask = false;
    if (which == 2 || which == 3) {
        rubraview_settings_set(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("archive_in_list"), which == 2 ? 1.0 : 2.0);
        settings_write_if_changed(app, false);
    }
    int32_t page = app->book_ask_page;
    if (page != current_page_index(app) || !list_row_is_book(app, (size_t)page)) return;   /* the list changed meanwhile */
    if (which == 0 || which == 2) book_enter(app, (size_t)page, app->book_ask_back);
    else book_step_over(app, (size_t)page, app->book_ask_back);
}

bool book_ask_press(app_state_t *app, double x, double y) {
    if (!app->book_ask) return false;
    for (int i = 0; i < 4; ++i) if (pal_rect_has(app->book_ask_btn[i], x, y)) { book_ask_answer(app, i); return true; }
    return true;   /* nothing under it while it asks */
}

/* RV-255's card (the playlist's question), its buttons flowing to a second line in a narrow window. */
void draw_book_ask(app_state_t *app, double win_w, double win_h, double dpi) {
    if (!app->book_ask) return;
    double cw = fmin(640.0 * dpi, win_w - 16.0 * dpi), pad = 20.0 * dpi, bh = 38.0 * dpi, gap = 10.0 * dpi;
    double bw[4];
    for (int i = 0; i < 4; ++i) {
        double tw = 0.0;
        (void)rubraview_pal_render_measure_text(app->renderer, cstr(rubraview_tr(BOOK_ASK_WORDS[i])), 14.0 * dpi, &tw);
        bw[i] = fmin(fmax(96.0 * dpi, tw + 32.0 * dpi), cw - 2.0 * pad);
    }
    uint32_t line_of[4];
    double x_of[4];
    size_t lines = rubraview_flow_inline(bw, 4, gap, cw - 2.0 * pad, line_of, x_of);
    double ch = pad + 30.0 * dpi + 26.0 * dpi + 14.0 * dpi + (double)lines * (bh + gap) - gap + pad + 24.0 * dpi;
    rubraview_pal_rect_t card = { (win_w - cw) * 0.5, (win_h - ch) * 0.45, cw, ch };
    rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ 0.0, 0.0, win_w, win_h }, 0x90000000u, 0.0);
    rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ card.x + 3.0 * dpi, card.y + 5.0 * dpi, cw, ch }, 0x60000000u, 8.0 * dpi);
    rubraview_pal_render_fill_rect(app->renderer, card, 0xFA242424u, 8.0 * dpi);
    rubraview_pal_render_stroke_rect(app->renderer, card, COLOR_BOX_BORDER, 1.0 * dpi, 8.0 * dpi);
    rubraview_pal_rect_t tr = { card.x + pad, card.y + pad - 4.0 * dpi, cw - 2.0 * pad, 30.0 * dpi };
    proven_u8str_view_t title = TU8("An archive in the list: read its pictures as a book?");
    rubraview_pal_render_draw_text(app->renderer, title, tr, 18.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    rubraview_pal_render_draw_text(app->renderer, title, (rubraview_pal_rect_t){ tr.x + 0.6 * dpi, tr.y, tr.width, tr.height }, 18.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    int32_t page = app->book_ask_page;
    proven_u8str_view_t shown = page >= 0 && (size_t)page < page_count(app) ? rubraview_path_basename(app->source.pages[page].path) : PROVEN_LIT("");
    char fit[1200];
    proven_u8str_view_t name = files_fit_text(fit, sizeof fit, shown, tr.width, 13.0 * dpi);
    rubraview_pal_render_draw_text(app->renderer, name, (rubraview_pal_rect_t){ tr.x, tr.y + 30.0 * dpi, tr.width, 22.0 * dpi },
                                   13.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
    double y0 = tr.y + 30.0 * dpi + 26.0 * dpi + 14.0 * dpi;
    for (int i = 0; i < 4; ++i) {
        rubraview_pal_rect_t b = { card.x + pad + x_of[i], y0 + (double)line_of[i] * (bh + gap), bw[i], bh };
        app->book_ask_btn[i] = b;
        bool over = pal_rect_has(b, app->pointer_x, app->pointer_y), main = i == 0;   /* Enter's: read it */
        rubraview_pal_render_fill_rect(app->renderer, b, main ? COLOR_TILE_CURRENT : over ? 0x50FFFFFFu : COLOR_TILE_FILL, 5.0 * dpi);
        if (main && over) rubraview_pal_render_fill_rect(app->renderer, b, 0x30FFFFFFu, 5.0 * dpi);
        if (!main) rubraview_pal_render_stroke_rect(app->renderer, b, COLOR_BOX_BORDER, 1.0, 5.0 * dpi);
        rubraview_pal_render_draw_text(app->renderer, cstr(rubraview_tr(BOOK_ASK_WORDS[i])), b, 14.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
    rubraview_pal_render_draw_text(app->renderer, TU8("Enter reads it, Esc skips it; the choice for always is in Settings > Files."),
                                   (rubraview_pal_rect_t){ card.x + pad, card.y + ch - pad - 20.0 * dpi, cw - 2.0 * pad, 20.0 * dpi },
                                   12.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
}

/* ---- folders pasted or dropped into the list ---- */

size_t folder_files_deep(app_state_t *app, proven_u8str_view_t dir, rubraview_fs_entry_t **out);

/* The paths given, each folder among them in place of the files under it (folders walked down) that a list takes,
   by folder and then by name; a file the list does not take is left out. In the working arena; how many. */
size_t list_walk_paths(app_state_t *app, const proven_u8str_view_t *paths, size_t count, proven_u8str_view_t **out) {
    *out = NULL;
    size_t cap = count + 64, n = 0;
    proven_u8str_view_t *all = (proven_u8str_view_t*)rubraview_malloc_product(cap, sizeof(proven_u8str_view_t), 1);
    if (!all) return 0;
    bool capped = false;
    for (size_t i = 0; i < count && !capped; ++i) {
        rubraview_fs_entry_t entry;
        if (is_station_path(paths[i])) { all[n++] = paths[i]; continue; }
        if (!rubraview_pal_fs_stat(app->arena, paths[i], &entry)) continue;
        if (!entry.is_directory) {
            if (n == cap) break;
            if (path_is_list_row_kind(entry.path)) all[n++] = entry.path;
            continue;
        }
        rubraview_fs_entry_t *under = NULL;
        size_t m = folder_files_deep(app, entry.path, &under);
        size_t from = n;
        for (size_t k = 0; k < m; ++k) {
            if (under[k].is_directory || !path_is_list_row_kind(under[k].path)) continue;
            if (n >= LIST_WALK_MAX) { capped = true; break; }
            if (n == cap) {
                size_t grow = cap * 2;
                proven_u8str_view_t *more = (proven_u8str_view_t*)rubraview_realloc(rubraview_mem_mut(all, grow * sizeof(*all)));
                if (!more) { capped = true; break; }
                all = more;
                cap = grow;
            }
            all[n++] = under[k].path;
        }
        rubraview_free(under);
        rubraview_sort_paths(all + from, n - from, RUBRAVIEW_SORT_PATH_NATURAL, true);   /* folder by folder, by name */
    }
    if (capped) {
        char line[128];
        rubraview_fmt(line, sizeof(line), T("only the first {} files are taken"), PROVEN_ARG((size_t)LIST_WALK_MAX));
        osd_say(app, cstr(line));
    }
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, n ? n : 1, sizeof(proven_u8str_view_t));
    if (!proven_is_ok(res.err)) { rubraview_free(all); return 0; }
    *out = (proven_u8str_view_t*)(void*)res.value.ptr;
    proven_sys_mem_copy(*out, all, n * sizeof(*all));
    rubraview_free(all);
    return n;
}

/* In a book of the list, files added go to the end of the list it came from (the book stays on screen). */
bool book_add_paths(app_state_t *app, const proven_u8str_view_t *paths, size_t count) {
    if (!app->book_active || !app->book_list) return false;
    size_t have = app->book_list_n;
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, have + count, sizeof(rubraview_fs_entry_t));
    if (!proven_is_ok(res.err)) return true;
    rubraview_fs_entry_t *entries = (rubraview_fs_entry_t*)(void*)res.value.ptr;
    proven_sys_mem_copy(entries, app->book_list, have * sizeof(*entries));
    size_t n = have;
    for (size_t i = 0; i < count; ++i) {
        if (!path_is_list_row_kind(paths[i])) continue;
        bool there = false;
        for (size_t k = 0; k < n && !there; ++k) there = rubraview_path_same(entries[k].path, paths[i]);
        if (there) continue;
        proven_u8str_view_t kept = app_keep(app, paths[i]);
        entries[n++] = (rubraview_fs_entry_t){ .name = rubraview_path_basename(kept), .path = kept };
    }
    app->book_list = entries;
    app->book_list_n = n;
    char line[128];
    rubraview_fmt(line, sizeof(line), T("{} added to the list ({} in it)"), PROVEN_ARG((size_t)(n - have)), PROVEN_ARG(n));
    osd_say(app, cstr(line));
    return true;
}

/* Ctrl+V in the viewer: files and folders copied in Explorer join the list (a folder's films, music, pictures, comics
   and archives, folders walked down); with nothing open they become the list. */
void list_paste(app_state_t *app) {
    enum { CLIP_BYTES = 1u << 20 };
    char *buf = (char*)rubraview_alloc(CLIP_BYTES);
    if (!buf) return;
    bool moving = false;
    size_t count = rubraview_pal_clipboard_get_files(app->window, buf, CLIP_BYTES, &moving);
    if (count == 0) { rubraview_free(buf); osd_say(app, TU8("no files on the clipboard: copy files or folders first")); return; }
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, count, sizeof(proven_u8str_view_t));
    if (!proven_is_ok(res.err)) { rubraview_free(buf); return; }
    proven_u8str_view_t *given = (proven_u8str_view_t*)(void*)res.value.ptr;
    const char *at = buf;
    for (size_t i = 0; i < count; ++i, at += proven_cstr_len(at) + 1) given[i] = app_keep(app, proven_u8str_view_from_cstr(at));
    rubraview_free(buf);
    proven_u8str_view_t *paths = NULL;
    size_t n = list_walk_paths(app, given, count, &paths);
    if (n == 0) { osd_say(app, TU8("nothing there the viewer opens")); return; }
    if (book_add_paths(app, paths, n)) return;
    if (page_count(app) > 0 && app->source.archive_path.size == 0) list_insert_paths(app, paths, n, page_count(app));
    else {   /* the order walked, not the folder's sort */
        proven_result_mem_mut_t er = rubraview_arena_alloc_array(app->arena, n, sizeof(rubraview_fs_entry_t));
        if (!proven_is_ok(er.err) || !subs_may_leave(app)) return;
        rubraview_fs_entry_t *entries = (rubraview_fs_entry_t*)(void*)er.value.ptr;
        for (size_t i = 0; i < n; ++i) entries[i] = (rubraview_fs_entry_t){ .name = rubraview_path_basename(paths[i]), .path = paths[i] };
        open_set_ordered(app, entries, n, n == 1 ? TU8("file opened as a list") : TU8("files opened as a list"), true, PROVEN_LIT(""));
    }
}
