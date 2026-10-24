/*
 * The files window: drawn, and its own window's messages (RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

void draw_files_window(app_state_t *app) {
    if (!app->files_open) return;
    size_t count = page_count(app);
    int32_t current = current_page_index(app);
    /* Another archive, folder or playlist: its list starts afresh. */
    if (app->files_pages != app->source.pages || app->files_count != count) {
        app->files_pages = app->source.pages;
        app->files_count = count;
        app->files_list.first = 0;
        app->files_selected = app->files_hover = -1;
        app->files_followed = -1;
        app->files_want = -1;
        files_preview_drop(app);
        app->files_dirty = true;
    }
    if (current != app->files_followed && current >= 0) {     /* follow the file on screen */
        size_t at = list_row_of(app, (size_t)current);
        if (at != SIZE_MAX) rubraview_listwin_reveal(&app->files_list, at, list_rows(app));
        app->files_followed = current;
        app->files_dirty = true;
    }
    if (!app->files_dirty) return;
    app->files_dirty = false;

    rubraview_renderer_t *r = app->files_renderer;
    listwin_sync(app);
    size_t shown = list_rows(app);   /* 1.25.0: the rows the filter and .ext leave */
    app->files_list.editable = list_editable(app);   /* RV-117: changed as the floating list is, or only looked at */
    const rubraview_listwin_t *lw = &app->files_list;
    double dpi = rubraview_pal_window_dpi_scale(app->files_window);
    double font = lw->row_height * 0.5;
    rubraview_pal_render_begin(r, FILES_BACKGROUND);

    /* The header: what the list is, and where the file on screen is in it. */
    bool in_archive = app->source.archive_path.size > 0 && !app->list_is_set;
    char head[640], cut[640];
    proven_u8str_view_t what = app->list_is_set ? TU8("Playlist")
                 : in_archive ? rubraview_path_basename(app->source.archive_path)
                 : rubraview_path_basename(app->source_dir);
    int hn = current >= 0 ? snprintf(head, sizeof(head), "%d / %zu   %.*s", current + 1, count, (int)(what.size < 560 ? what.size : 560), what.ptr)
                          : snprintf(head, sizeof(head), "%zu   %.*s", count, (int)(what.size < 560 ? what.size : 560), what.ptr);
    rubraview_pal_rect_t head_box = { lw->x, lw->y, lw->width, lw->title_height };
    rubraview_pal_render_fill_rect(r, head_box, COLOR_TILE_FILL, 0.0);
    rubraview_pal_rect_t head_text = { lw->x + lw->row_height * 0.4, lw->y, lw->width - lw->row_height * 0.8, lw->title_height };
    if (hn > 0) rubraview_pal_render_draw_text(r, files_fit_text(cut, sizeof(cut), (proven_u8str_view_t){ (const proven_byte_t *)head, (size_t)hn }, head_text.width, font),
                                               head_text, font, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    if (lw->editable) {
        draw_list_bar(app, r, lw, app->files_px, app->files_py);
        draw_list_tools(app, r, lw, app->files_px, app->files_py, true);
    }
    double head_h = rubraview_listwin_head_height(lw);

    size_t rows = rubraview_listwin_rows_visible(lw);
    for (size_t at = lw->first; at < shown && at < lw->first + rows; ++at) {
        size_t i = list_page(app, at);
        if (i == SIZE_MAX) break;
        rubraview_rect_t rr = rubraview_listwin_row_rect(lw, at, shown);
        rubraview_pal_rect_t row = { rr.x + 2.0, rr.y, rr.width - 8.0, rr.height };
        bool checked = i < app->list_checked_count && app->list_checked[i];
        bool carried = app->list_row_dragging && app->list_held_in_files && (i == app->list_press_row ||
                       (checked && app->list_press_row < app->list_checked_count && app->list_checked[app->list_press_row]));
        if ((int32_t)i == current) rubraview_pal_render_fill_rect(r, row, (COLOR_TILE_CURRENT & 0x00FFFFFFu) | 0x90000000u, 2.0);
        else if (carried) rubraview_pal_render_fill_rect(r, row, 0x405B9BD5u, 2.0);
        else if ((int32_t)i == app->files_hover) rubraview_pal_render_fill_rect(r, row, COLOR_TILE_FILL, 2.0);
        if ((int32_t)i == app->files_selected) rubraview_pal_render_stroke_rect(r, row, COLOR_TEXT, 1.0, 2.0);
        if (lw->editable) draw_list_row_parts(app, r, lw, at, shown, app->files_px, app->files_py, i);
        double text_left = lw->editable ? rubraview_listwin_row_text_x(lw) : row.x;

        /* its size at the right (an archive knows it; a folder's files are not asked), its name in what is left */
        char size_text[32];
        uint64_t bytes = rubraview_page_source_entry_size(&app->source, i);
        double size_w = 0.0;
        if (bytes > 0) {
            int sn = files_size_text(size_text, sizeof(size_text), bytes);
            size_w = font * 0.56 * 9.0;
            rubraview_pal_rect_t size_box = { row.x + row.width - size_w - lw->row_height * 0.3, row.y, size_w, row.height };
            if (sn > 0) rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ (const proven_byte_t *)size_text, (size_t)sn }, size_box, font * 0.9, 0xB0F0F0F0u, RUBRAVIEW_TEXT_RIGHT);
        }
        char line[640];
        proven_u8str_view_t name = page_display_name(app, i);
        int m = snprintf(line, sizeof(line), "%4zu   %.*s", i + 1, (int)(name.size < 600 ? name.size : 600), name.ptr);
        rubraview_pal_rect_t text = { text_left + lw->row_height * 0.3, row.y, row.x + row.width - text_left - lw->row_height * 0.9 - size_w, row.height };
        if (m > 0) rubraview_pal_render_draw_text(r, files_fit_text(cut, sizeof(cut), (proven_u8str_view_t){ (const proven_byte_t *)line, (size_t)(m < (int)sizeof(line) ? m : (int)sizeof(line) - 1) }, text.width, font),
                                                  text, font, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    }
    draw_list_drop_line(app, r, lw, true);
    if (shown > rows && rows > 0) {              /* where in a long list the rows are */
        double track = lw->height - head_h;
        double bh = track * (double)rows / (double)shown;
        if (bh < lw->row_height * 0.5) bh = lw->row_height * 0.5;
        double by = lw->y + head_h + (track - bh) * (double)lw->first / (double)(shown - rows);
        rubraview_pal_rect_t bar = { lw->x + lw->width - 5.0, by, 3.0, bh };
        rubraview_pal_render_fill_rect(r, bar, COLOR_BOX_BORDER | 0x60000000u, 1.5);
    }

    /* The preview: the picture fitted above two lines that say what it is. */
    rubraview_rect_t pv = app->files_preview_rect;
    if (pv.width > 0.0) {
        rubraview_pal_rect_t edge = { pv.x, pv.y, 1.0, pv.height };
        rubraview_pal_render_fill_rect(r, edge, COLOR_BOX_BORDER, 0.0);
        double caption_h = lw->row_height * 2.4;
        rubraview_rect_t room = { pv.x, pv.y, pv.width, pv.height - caption_h };
        int32_t page = app->files_preview_page;
        const char *say = NULL;
        switch (app->files_preview_state) {
            case FILES_PREVIEW_SHOWN: {
                int32_t tw = 0, th = 0;
                rubraview_pal_texture_size(app->files_preview, &tw, &th);
                rubraview_rect_t fit = rubraview_filewin_fit(room, (double)app->files_preview_w, (double)app->files_preview_h, 8.0 * dpi);
                if (fit.width > 0.0 && tw > 0 && th > 0) {
                    rubraview_mat3x2_t place = rubraview_mat3x2_multiply(
                        rubraview_mat3x2_scale(fit.width / (double)tw, fit.height / (double)th),
                        rubraview_mat3x2_translate(fit.x, fit.y));
                    rubraview_pal_render_draw_texture(r, app->files_preview, place, RUBRAVIEW_INTERP_LINEAR);
                }
                break;
            }
            case FILES_PREVIEW_FAILED: say = T("no preview: it could not be read as a picture"); break;
            case FILES_PREVIEW_FAR:    say = T("deep in a solid archive: Enter or a click opens it"); break;
            case FILES_PREVIEW_MEDIA:  say = T("a film or a sound: Enter or a click plays it"); break;
            default:                   say = page >= 0 ? NULL : T("point at a file, or use the arrow keys"); break;
        }
        if (say) {
            rubraview_pal_rect_t mid = { room.x + 8.0 * dpi, room.y, room.width - 16.0 * dpi, room.height };
            rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ (const proven_byte_t *)say, strlen(say) }, mid, font, 0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
        }
        if (page >= 0 && (size_t)page < count) {
            rubraview_pal_rect_t line1 = { pv.x + 8.0 * dpi, pv.y + pv.height - caption_h, pv.width - 16.0 * dpi, caption_h * 0.5 };
            rubraview_pal_rect_t line2 = { line1.x, line1.y + caption_h * 0.5 - 2.0, line1.width, caption_h * 0.5 };
            proven_u8str_view_t name = page_display_name(app, (size_t)page);
            rubraview_pal_render_draw_text(r, files_fit_text(cut, sizeof(cut), name, line1.width, font), line1, font, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
            char facts[160], size_text[32];
            int fn = 0;
            uint64_t bytes = rubraview_page_source_entry_size(&app->source, (size_t)page);
            if (bytes > 0) files_size_text(size_text, sizeof(size_text), bytes); else size_text[0] = '\0';
            if (app->files_preview_state == FILES_PREVIEW_SHOWN) {
                fn = bytes > 0 ? snprintf(facts, sizeof(facts), "%d x %d   %s", app->files_preview_w, app->files_preview_h, size_text)
                               : snprintf(facts, sizeof(facts), "%d x %d", app->files_preview_w, app->files_preview_h);
            } else if (bytes > 0) {
                fn = snprintf(facts, sizeof(facts), "%s", size_text);
            }
            if (fn > 0) rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ (const proven_byte_t *)facts, (size_t)fn }, line2, font * 0.9, 0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
        }
    }
    if (app->ctx_in_files) draw_ctxmenu_on(app, r, app->files_px, app->files_py);   /* RV-117: a file's own list */
    draw_chooser(app, r, CHOOSE_IN_FILES, app->files_px, app->files_py);           /* 1.25.0: .ext, the sort */
    if (!rubraview_pal_render_end(r)) files_close(app);   /* its device went: the key opens it again */
}

size_t files_pump(app_state_t *app) {
    size_t handled = 0;
    rubraview_window_event_t event;
    while (app->files_open && rubraview_pal_window_poll_event(app->files_window, &event)) {
        handled++;
        size_t count = list_rows(app), index = 0;   /* 1.25.0: rows; a row's page by list_page */
        switch (event.kind) {
            case RUBRAVIEW_WINDOW_EVENT_CLOSE:
                files_close(app);
                return handled;
            case RUBRAVIEW_WINDOW_EVENT_TEXT:   /* 1.25.0: the filter typed in the tools row */
                if (app->list_filter_in_files) {
                    char next[sizeof(app->list_filter)];
                    size_t n = strlen(app->list_filter);
                    memcpy(next, app->list_filter, n);
                    (void)rubraview_rename_append(next, sizeof(next), &n, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)event.text.utf8, .size = event.text.length });
                    list_filter_set(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)next, .size = n });
                    app->files_dirty = true;
                }
                break;
            case RUBRAVIEW_WINDOW_EVENT_RESIZE:
                rubraview_pal_render_resize(app->files_renderer, event.resize.width, event.resize.height);
                files_layout(app);
                break;
            case RUBRAVIEW_WINDOW_EVENT_PAINT:
            case RUBRAVIEW_WINDOW_EVENT_DPI_CHANGED:
                files_layout(app);
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE: {
                int32_t was = app->files_hover;
                app->files_px = event.mouse.x;
                app->files_py = event.mouse.y;
                app->files_hover = rubraview_listwin_hit(&app->files_list, event.mouse.x, event.mouse.y, count, &index) == RUBRAVIEW_LISTWIN_ROW
                                     ? (int32_t)list_page(app, index) : -1;
                if (app->files_hover != was) app->files_dirty = true;
                if (app->chooser.open && app->chooser.where == CHOOSE_IN_FILES) app->files_dirty = true;
                if (app->files_list.editable || app->ctx_in_files) app->files_dirty = true;   /* its buttons light under the pointer */
                list_motion(app, event.mouse.x, event.mouse.y, true);
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_UP:
                if (app->list_held_in_files && listwin_release(app)) app->files_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_DROP:
                /* RV-117: files from Explorer join the list at the row they are let go over */
                if (event.drop.count > 0 && list_editable(app)) {
                    proven_u8str_view_t paths[16];
                    size_t n = event.drop.count < 16 ? event.drop.count : 16;
                    for (size_t i = 0; i < n; ++i) paths[i] = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)event.drop.paths[i], .size = event.drop.path_lengths[i] };
                    listwin_sync(app);
                    size_t at = event.drop.has_point && !list_viewed(app) ? rubraview_listwin_insert_index(&app->files_list, event.drop.y, page_count(app))
                                                                          : page_count(app);
                    list_insert_paths(app, paths, n, at);
                    app->files_dirty = true;
                }
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_LEAVE:
                if (app->files_hover != -1) { app->files_hover = -1; app->files_dirty = true; }
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN: {
                bool right = event.mouse.button == RUBRAVIEW_MOUSE_RIGHT;
                if (chooser_press(app, CHOOSE_IN_FILES, event.mouse.x, event.mouse.y)) { app->files_dirty = true; break; }   /* 1.25.0 */
                if (app->list_filter_in_files &&
                    !rubraview_rect_contains(rubraview_listwin_tool_rect(&app->files_list, RUBRAVIEW_LISTWIN_FILTER), event.mouse.x, event.mouse.y))
                    list_filter_files_end(app);   /* a press elsewhere: done typing, the filter kept */
                if (app->ctx_open && app->ctx_in_files) {          /* its own list takes the press first */
                    if (popups_press(app, event.mouse.x, event.mouse.y, right) || !right) { app->files_dirty = true; break; }
                }
                if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT || right) {
                    list_press(app, &app->files_list, event.mouse.x, event.mouse.y, right, true);   /* RV-117: as the floating list */
                }
                app->files_dirty = true;
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_WHEEL:
                if (chooser_wheel(app, CHOOSE_IN_FILES, event.mouse.wheel_delta)) break;   /* 1.25.0 */
                rubraview_listwin_scroll(&app->files_list, event.mouse.wheel_delta > 0 ? -3 : 3, count);
                /* the row under the pointer is another one now */
                app->files_hover = rubraview_listwin_hit(&app->files_list, event.mouse.x, event.mouse.y, count, &index) == RUBRAVIEW_LISTWIN_ROW
                                     ? (int32_t)list_page(app, index) : -1;
                app->files_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN: {
                rubraview_key_combo_t combo = event.key.combo;
                long rows = (long)rubraview_listwin_rows_visible(&app->files_list);
                long step = 0;
                if (app->ctx_open && app->ctx_in_files && popups_key(app, combo)) { app->files_dirty = true; break; }
                if (chooser_key(app, CHOOSE_IN_FILES, combo)) break;   /* 1.25.0 */
                if (app->list_filter_in_files) {   /* 1.25.0: the filter's keys */
                    if (key_is(combo, "Escape")) { list_filter_set(app, PROVEN_LIT("")); list_filter_files_end(app); break; }
                    if (key_is(combo, "Enter")) { list_filter_files_end(app); break; }
                    if (key_is(combo, "Backspace")) {
                        size_t n = strlen(app->list_filter);
                        while (n > 0 && ((unsigned char)app->list_filter[n - 1] & 0xC0u) == 0x80u) --n;
                        if (n > 0) --n;
                        list_filter_set(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->list_filter, .size = n });
                        app->files_dirty = true;
                        break;
                    }
                    if (!(combo.modifiers & (RUBRAVIEW_MOD_CTRL | RUBRAVIEW_MOD_ALT))) break;   /* letters come as text */
                }
                if (key_is(combo, "Escape")) { files_close(app); return handled; }
                if (key_is(combo, "Enter")) {
                    files_go_to(app, app->files_selected >= 0 ? app->files_selected : files_target(app));
                    break;
                }
                if (key_is(combo, "Up")) step = -1;
                else if (key_is(combo, "Down")) step = +1;
                else if (key_is(combo, "PageUp")) step = -(rows > 1 ? rows - 1 : 1);
                else if (key_is(combo, "PageDown")) step = rows > 1 ? rows - 1 : 1;
                else if (key_is(combo, "Home")) step = -(long)count;
                else if (key_is(combo, "End")) step = (long)count;
                if (step != 0) {   /* 1.25.0: through the rows shown */
                    int32_t current = current_page_index(app);
                    size_t from = app->files_selected >= 0 ? list_row_of(app, (size_t)app->files_selected) : SIZE_MAX;
                    size_t here = current >= 0 ? list_row_of(app, (size_t)current) : SIZE_MAX;
                    size_t to = rubraview_filewin_step(from, here, step, count);
                    if (to != SIZE_MAX && list_page(app, to) != SIZE_MAX) {
                        app->files_selected = (int32_t)list_page(app, to);
                        app->files_hover = -1;           /* the keys have it now, wherever the pointer was left */
                        app->files_want_now = true;
                        rubraview_listwin_reveal(&app->files_list, to, count);
                        app->files_dirty = true;
                    }
                    break;
                }
                /* Any other key is the viewer's: its own key closes this window, the rest work as there. */
                dispatch_key(app, combo);
                app->files_dirty = true;
                break;
            }
            default:
                break;
        }
    }
    return handled;
}
