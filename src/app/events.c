/*
 * One event of the viewer's window, handled: the switch on its kind (RV-163, D-198: cut from wWinMain whole, its
 * lines as they were, `app.` read as `app->` and `event.` as `event->`; every `break` still leaves the switch).
 */
#include "app.h"

void frame_event(app_state_t *app, const rubraview_window_event_t *event) {
    switch (event->kind) {
        case RUBRAVIEW_WINDOW_EVENT_CLOSE:
            /* RV-115: the X or Alt+F4 with a subtitle's changes not saved is held once */
            if (subs_may_leave(app)) rubraview_pal_window_request_close(app->window);
            else rubraview_pal_window_keep_open(app->window);
            break;

        case RUBRAVIEW_WINDOW_EVENT_RESIZE:
            rubraview_pal_render_resize(app->renderer, event->resize.width, event->resize.height);
            app->filmstrip.viewport_extent = (double)event->resize.width;
            app->needs_relayout = true;
            panel_relayout(app);
            boxes_place(app);   /* D-96: the same share of the new size (the clamp alone left them where they were) */
            break;

        case RUBRAVIEW_WINDOW_EVENT_DPI_CHANGED:
            app->titlebar = rubraview_titlebar_create(event->dpi.scale);
            app->titlebar.trigger_zone = fmax(36.0, rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("titlebar_trigger_px"))) * event->dpi.scale;
            app->titlebar.hide_delay = rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("titlebar_hide_ms")) / 1000.0;
            app->needs_relayout = true;
            break;

        case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN:
            if (app->sel_mode && key_is(event->key.combo, "Escape")) {   /* stops choosing, not the program */
                sel_mode_set(app, false);
                if (edit_panel_open(app)) panel_build_edit(app);
                break;
            }
            dispatch_key(app, event->key.combo);
            break;

        case RUBRAVIEW_WINDOW_EVENT_TEXT:
        case RUBRAVIEW_WINDOW_EVENT_COMPOSITION:
            rename_text(app, event);
            break;

        case RUBRAVIEW_WINDOW_EVENT_MOUSE_LEAVE:
            app->pointer_inside = false;   /* the edge buttons go with the pointer */
            break;

        case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE: {
            if (scrollbar_motion(app, app->window, event->mouse.x, event->mouse.y)) break;   /* RV-170: a list's bar held */
            if (app->text && app->text->selecting) { text_drag(app, event->mouse.x, event->mouse.y); break; }
            if (!app->seekfan_detached) seekfan_pointer(app, event->mouse.x, event->mouse.y);
            speed_tile_move(app, event->mouse.y, false);
            if (app->view_pressed && !app->view_dragging) {
                /* Owner, 2026-10-14: a click on a film or a song is Play / Pause, so a hand
                   that moves a little while pressing is still a click — a film is dragged
                   only past twelve pixels, and a song, which has nothing to drag, never. */
                double slack = (app->media ? 12.0 : 4.0) * rubraview_pal_window_dpi_scale(app->window);
                if (app->media && !app->media_info.has_video) slack = 1e9;
                if (fabs(event->mouse.x - app->view_press_x) > slack || fabs(event->mouse.y - app->view_press_y) > slack) {
                    app->view_dragging = true;
                }
            }
            if (app->view_dragging) {
                app->pan_x += event->mouse.x - app->view_drag_x;
                app->pan_y += event->mouse.y - app->view_drag_y;
                app->view_drag_x = event->mouse.x;
                app->view_drag_y = event->mouse.y;
                note_activity(app);
            }
            if (app->subbox.dragging != RUBRAVIEW_SUBBOX_NONE) {
                subbox_drag_motion(app, event->mouse.x, event->mouse.y);   /* D-33 */
                app->pointer_x = event->mouse.x;
                app->pointer_y = event->mouse.y;
                break;
            }
            if (app->curve_dragging >= 0 && !app->edit_window) {
                curve_widget_drag(app, event->mouse.x, event->mouse.y);
                app->pointer_x = event->mouse.x;
                app->pointer_y = event->mouse.y;
                break;
            }
            if (app->sel_grab) {
                int32_t ix = 0, iy = 0;
                if (crop_point_to_image(app, event->mouse.x, event->mouse.y, &ix, &iy)) {
                    sel_drag_to(app, ix, iy);
                    edit_sel_sync(app);
                }
                app->pointer_x = event->mouse.x;
                app->pointer_y = event->mouse.y;
                break;
            }
            {
                /* A box taken by its title follows the pointer. */
                rubraview_panel_t *moving = app->edit_window ? (app->classify_panel.moving ? &app->classify_panel : NULL)
                                          : app->panel.moving ? &app->panel : app->sel_panel.moving ? &app->sel_panel
                                          : app->steps_panel.moving ? &app->steps_panel
                                          : app->classify_panel.moving ? &app->classify_panel : NULL;
                if (moving) {
                    int32_t ww = 0, wh = 0;
                    rubraview_pal_window_get_size(app->window, &ww, &wh);
                    rubraview_panel_move_to(moving, event->mouse.x, event->mouse.y, (double)ww, (double)wh);
                    app->pointer_x = event->mouse.x;
                    app->pointer_y = event->mouse.y;
                    break;
                }
            }
            if (app->crop_dragging) {
                int32_t ix = 0, iy = 0;
                app->edit_rev++;
                if (crop_point_to_image(app, event->mouse.x, event->mouse.y, &ix, &iy)) {
                    rubraview_edit_crop_drag(&app->edit, app->crop_drag_x0, app->crop_drag_y0, ix, iy);
                    crop_shown_again(app);
                }
                app->pointer_x = event->mouse.x;
                app->pointer_y = event->mouse.y;
                break;
            }
            if (app->panel.open && app->panel.active_row >= 0 && !(app->edit_window && edit_panel_open(app))) {
                int32_t dragged = -1;
                if (rubraview_panel_drag(&app->panel, event->mouse.x, event->mouse.y, &dragged)
                        == RUBRAVIEW_PANEL_VALUE_CHANGED) {
                    panel_apply_row(app, dragged);
                }
            }
            if (app->pagebar_dragging && !app->pagebar_detached) pagebar_drag(app, event->mouse.x, false);
            if (app->picker_open && picker_details(app)) picker_columns_motion(app, event->mouse.x, event->mouse.y);   /* 1.25.0 */
            if (app->picker_hold) {   /* 1.22.0: moved off: neither a tap nor a long press */
                double dx = event->mouse.x - app->picker_hold_x, dy = event->mouse.y - app->picker_hold_y;
                double slop = 10.0 * rubraview_pal_window_dpi_scale(app->window);
                if (dx * dx + dy * dy > slop * slop) app->picker_hold = false;
            }
            if (app->fav_press >= 0) {
                app->fav_drag_x = event->mouse.x;
                if (fabs(event->mouse.x - app->fav_press_x) >= 6.0 * rubraview_pal_window_dpi_scale(app->window)) app->fav_dragging = true;
            }
            if (app->titlebar_held != RUBRAVIEW_TITLEBAR_NONE) {
                double dx = event->mouse.x - app->titlebar_held_x, dy = event->mouse.y - app->titlebar_held_y;
                double slop = 4.0 * rubraview_pal_window_dpi_scale(app->window);
                if (dx * dx + dy * dy >= slop * slop) {
                    rubraview_titlebar_button_t held = app->titlebar_held;
                    app->titlebar_held = RUBRAVIEW_TITLEBAR_NONE;
                    if (held == RUBRAVIEW_TITLEBAR_RESIZE) rubraview_pal_window_begin_resize(app->window);
                    else rubraview_pal_window_begin_drag(app->window);
                    break;
                }
            }
            if (app->eqwin.dragging || app->eqwin.sliding >= 0) {
                int32_t eq_w = 0, eq_h = 0;
                rubraview_pal_window_get_size(app->window, &eq_w, &eq_h);
                if (app->eqwin.dragging) rubraview_eqwin_drag_to(&app->eqwin, event->mouse.x, event->mouse.y, (double)eq_w, (double)eq_h);
                else eqwin_set_band(app, (size_t)app->eqwin.sliding,
                                    rubraview_eqwin_db_at(&app->eqwin, (size_t)app->eqwin.sliding, event->mouse.y));
            }
            listwin_motion(app, event->mouse.x, event->mouse.y);
            recentwin_motion(app, event->mouse.x, event->mouse.y);
            favwin_motion(app, event->mouse.x, event->mouse.y);
            app->pointer_inside = true;
            app->pointer_x = event->mouse.x;
            app->pointer_y = event->mouse.y;
            if (app->frameless && !app->chrome_hidden) rubraview_titlebar_pointer_moved(&app->titlebar, event->mouse.y);
            if (rubraview_cursor_hide_notify_motion(&app->cursor)) {
                rubraview_pal_window_set_cursor_visible(app->window, true);
            }
            note_activity(app);

            /* §3.6: the box model decides what a pointer over
               it means — only the hover half opens anything. */
            if (app->chrome_hidden) break;   /* D-110: nothing opens under the pointer */
            rubraview_tile_metrics_t metrics = tile_metrics(rubraview_pal_window_dpi_scale(app->window));
            rubraview_box_pointer(&app->toolbox, &metrics, event->mouse.x, event->mouse.y);
            rubraview_box_pointer(&app->menubox, &metrics, event->mouse.x, event->mouse.y);
            box_drag_motion(app, event->mouse.x, event->mouse.y);
            break;
        }

        case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN: {
            if (chooser_press(app, CHOOSE_IN_MAIN, event->mouse.x, event->mouse.y)) break;   /* 1.24.0 */
            if (tmenu_press(app, event->mouse.x, event->mouse.y)) break;   /* 1.25.0: the ☰ pull-down */
            if (!(app->edit_window && edit_panel_open(app) && (app->rename_is_value || app->rename_is_apply)) &&
                rename_box_press(app, event->mouse.x, event->mouse.y)) break;   /* 1.24.0: the box's buttons */
            if (app->picker_open && app->favwin.open && event->mouse.button == RUBRAVIEW_MOUSE_LEFT &&
                favwin_press(app, event->mouse.x, event->mouse.y)) break;   /* 1.24.0: over the open-file screen too */
            if (app->chrome_hidden) {
                /* D-110: the click that brings the bar and the boxes back
                   is only that — it turns no page and pauses nothing. */
                chrome_hide(app, false);
                osd_say(app, TU8("the title bar and the Floating Widget are back"));
                break;
            }
            if (app->picker_open) {
                /* 1.22.0: a tile's right-click list takes the press first */
                if (app->ctx_open && popups_press(app, event->mouse.x, event->mouse.y, event->mouse.button == RUBRAVIEW_MOUSE_RIGHT)) break;
                double dpi = rubraview_pal_window_dpi_scale(app->window);
                double top = picker_top(app);
                double crumb_h = top + PICKER_CRUMB_HEIGHT * dpi;
                double header_h = top + PICKER_HEADER_HEIGHT * dpi;
                picker_layout(app);
                double grid_top = picker_grid_top(app, dpi);

                if (titlebar_click(app, event->mouse.x, event->mouse.y)) break;
                if (event->mouse.y < top) break;   /* the band holds nothing to press */
                if (event->mouse.button == RUBRAVIEW_MOUSE_LEFT && scrollbar_press(app, RVSB_PICKER, event->mouse.x, event->mouse.y)) break;   /* RV-170 */
                if (event->mouse.y < crumb_h && !app->picker_in_book) {   /* 1.22.0: the filter's chip */
                    int32_t fw = 0, fh = 0;
                    rubraview_pal_window_get_size(app->window, &fw, &fh);
                    char fbuf[200];
                    proven_u8str_view_t flabel;
                    rubraview_pal_rect_t chip = picker_filter_chip(app, (double)fw, dpi, &flabel, fbuf, sizeof(fbuf));
                    if (pal_rect_has(chip, event->mouse.x, event->mouse.y)) {
                        if (app->picker_filter[0] && event->mouse.x >= chip.x + chip.width - 28.0 * dpi) picker_filter_set(app, PROVEN_LIT(""));
                        else picker_filter_begin(app);
                        break;
                    }
                }
                if (event->mouse.y < crumb_h) {
                    /* 1.24.0: back, up, forward */
                    int nav = -1;
                    for (int k = 0; k < 3; ++k) if (pal_rect_has(picker_nav_rect(app, dpi, k), event->mouse.x, event->mouse.y)) nav = k;
                    if (nav >= 0) {
                        if (app->rename_active) rename_end(app);
                        if (nav == 1) { if (!app->picker_in_book) picker_go_up(app); }
                        else picker_history_go(app, nav == 2);
                        break;
                    }
                    /* A breadcrumb chip navigates to its prefix; the
                       bar past the last chip takes a typed path. */
                    rubraview_breadcrumbs_t crumbs = rubraview_picker_breadcrumbs(app->picker_dir);
                    double x = PICKER_CRUMB_START * dpi;
                    bool on_chip = false;
                    for (size_t i = 0; i < crumbs.count; ++i) {
                        double w = picker_chip_width(crumbs.items[i].label, dpi);
                        if (event->mouse.x >= x && event->mouse.x < x + w) {
                            if (app->rename_active) rename_end(app);
                            picker_navigate(app, crumbs.items[i].prefix);
                            app->picker.focus = 0;
                            on_chip = true;
                            break;
                        }
                        x += w + 4.0 * dpi;
                    }
                    proven_u8str_view_t star = on_chip ? PROVEN_LIT("") : picker_star_label(app);
                    if (star.size > 0 && event->mouse.x >= x && event->mouse.x < x + picker_chip_width(star, dpi)) {
                        picker_toggle_favorite(app);
                        break;
                    }
                    if (star.size > 0) x += picker_chip_width(star, dpi) + 4.0 * dpi;
                    if (!on_chip && event->mouse.x >= x && !app->rename_active) picker_path_begin(app);
                    break;
                }
                if (event->mouse.y < header_h) {
                    int32_t bw0 = 0, bh0 = 0;
                    rubraview_pal_window_get_size(app->window, &bw0, &bh0);
                    if (pal_rect_has(picker_favwin_chip(app, dpi, (double)bw0), event->mouse.x, event->mouse.y)) { favwin_toggle(app); break; }
                    double x = 8.0 * dpi;
                    picker_bar_item_t item;
                    for (size_t i = 0; picker_bar_item(app, i, &item); ++i) {
                        double w = picker_chip_width(item.label, dpi);
                        if (!item.favorite && i == app->favorites.count && i > 0) x += 10.0 * dpi;   /* as drawn */
                        if (event->mouse.x >= x && event->mouse.x < x + w) {
                            if (app->rename_active) rename_end(app);
                            if (item.favorite && event->mouse.button == RUBRAVIEW_MOUSE_RIGHT) {
                                favorite_rename_begin(app, i);   /* owner, 2026-09-30 */
                            } else if (item.favorite && event->mouse.button == RUBRAVIEW_MOUSE_LEFT) {
                                /* opened on release; dragged, it moves (owner, 2026-09-30) */
                                app->fav_press = (int32_t)i;
                                app->fav_press_x = app->fav_drag_x = event->mouse.x;
                                app->fav_dragging = false;
                            } else {
                                picker_navigate(app, item.path);
                                app->picker.focus = 0;
                            }
                            break;
                        }
                        x += w + 4.0 * dpi;
                    }
                    break;
                }

                int32_t pw = 0, ph = 0;
                rubraview_pal_window_get_size(app->window, &pw, &ph);
                if (event->mouse.y >= (double)ph - PICKER_ACTION_HEIGHT * dpi) {
                    for (int32_t b = 0; b < PICKER_BOTTOM_COUNT; ++b) {
                        if (pal_rect_has(picker_button_rect((double)pw, (double)ph, dpi, b), event->mouse.x, event->mouse.y)) {
                            picker_button(app, PICKER_BOTTOM[b]);
                            break;
                        }
                    }
                    break;
                }
                if (event->mouse.y < header_h + PICKER_TOOL_HEIGHT * dpi) {   /* 1.24.0: the tool row */
                    for (int k = 0; k < PICKER_TOOL_COUNT; ++k) {
                        rubraview_pal_rect_t t = picker_tool_rect(app, dpi, k);
                        if (!pal_rect_has(t, event->mouse.x, event->mouse.y)) continue;
                        switch (k) {
                            case PICKER_TOOL_ALL:        picker_select_everything(app); break;
                            case PICKER_TOOL_INDIVIDUAL: picker_button(app, PICKER_BTN_INDIVIDUAL); break;
                            case PICKER_TOOL_RANGE:      picker_button(app, PICKER_BTN_RANGE); break;
                            case PICKER_TOOL_SAME:       picker_button(app, PICKER_BTN_SAME_TYPE); break;
                            case PICKER_TOOL_MEDIA:      picker_button(app, PICKER_BTN_MEDIA); break;
                            case PICKER_TOOL_SORT:       chooser_open_sort(app, false, t.x, t.y + t.height, t.y); break;
                            case PICKER_TOOL_ORDER:      chooser_open_sort(app, true, t.x, t.y + t.height, t.y); break;
                            case PICKER_TOOL_VIEW:       picker_button(app, PICKER_BTN_VIEW); break;
                            default: break;
                        }
                        break;
                    }
                    break;
                }
                if (event->mouse.y < grid_top) {
                    /* the columns' names: sort by the one pressed, again the other way round (owner, 2026-10-16) */
                    if (event->mouse.y >= grid_top - PICKER_DETAIL_HEAD * dpi) {
                        picker_columns_t cols = picker_detail_columns((double)pw, dpi);
                        /* 1.25.0: on a line between two heads its width is dragged; on a head, it is dragged to
                           another place, or (not moved) sorts by it */
                        int edge = picker_column_edge(&cols, event->mouse.x, dpi);
                        int k = picker_column_at(&cols, event->mouse.x);
                        if (edge > 0) {
                            app->col_drag = 1; app->col_drag_k = edge; app->col_drag_x0 = event->mouse.x; app->col_drag_w0 = g_col_w[edge];
                        } else if (k >= 0) {
                            app->col_drag = 2; app->col_drag_k = k; app->col_drag_x0 = event->mouse.x; app->col_drag_moved = false;
                        }
                    }
                    break;
                }
                size_t columns = (size_t)app->picker.columns;
                double cell_w = (double)pw / (double)columns;
                size_t column = (size_t)(event->mouse.x / cell_w);
                size_t row = (size_t)((event->mouse.y - grid_top + app->picker.scroll_offset) / app->picker.tile_extent);
                size_t index = row * columns + column;
                if (column < columns && index < app->picker_listing.count) {
                    app->picker.focus = index;
                    if (!app->picker_in_book) {   /* 1.22.0 */
                        /* a check box picks, in any mode; folders too */
                        if (event->mouse.button == RUBRAVIEW_MOUSE_LEFT && !picker_is_up(app, index) && app->picker_selected &&
                            pal_rect_has(picker_check_rect(app, index, (double)pw, dpi), event->mouse.x, event->mouse.y)) {
                            app->picker_selected[index] = !app->picker_selected[index];
                            app->picker_anchor = index;
                            break;
                        }
                        if (event->mouse.button == RUBRAVIEW_MOUSE_RIGHT) { picker_context(app, index, event->mouse.x, event->mouse.y); break; }
                        /* 1.24.0 (owner, 2026-10-19): Ctrl+click picks or lets go of one, Shift+click a range from the last one clicked */
                        bool ctrl = (event->mouse.modifiers & RUBRAVIEW_MOD_CTRL) != 0, shift = (event->mouse.modifiers & RUBRAVIEW_MOD_SHIFT) != 0;
                        if (event->mouse.button == RUBRAVIEW_MOUSE_LEFT && (ctrl || shift) && app->picker_selected && !picker_is_up(app, index)) {
                            if (shift) {
                                size_t from = app->picker_anchor < app->picker_listing.count ? app->picker_anchor : index;
                                size_t lo = from < index ? from : index, hi = from < index ? index : from;
                                if (!ctrl) for (size_t i = 0; i < app->picker_listing.count; ++i) app->picker_selected[i] = false;
                                for (size_t i = lo; i <= hi; ++i) app->picker_selected[i] = !picker_is_up(app, i);
                            } else {
                                app->picker_selected[index] = !app->picker_selected[index];
                                app->picker_anchor = index;
                            }
                            break;
                        }
                        if (event->mouse.button == RUBRAVIEW_MOUSE_LEFT) app->picker_anchor = index;
                        if (event->mouse.button == RUBRAVIEW_MOUSE_LEFT) {   /* a tap on release; held still, a right click */
                            app->picker_hold = true;
                            app->picker_hold_index = index;
                            app->picker_hold_x = event->mouse.x;
                            app->picker_hold_y = event->mouse.y;
                            app->picker_hold_at = rubraview_pal_time_now_seconds();
                            break;
                        }
                    }
                    /* Right-click on an archive: inside it, not reading it yet. */
                    if (event->mouse.button == RUBRAVIEW_MOUSE_RIGHT && picker_enter_book(app, index)) break;
                    /* In a picking mode the tap picks; otherwise it opens. */
                    /* In a book a tap always goes to the page, whatever mode was left on outside it. */
                    if (app->picker_in_book || !rubraview_picker_tap(&app->picker, index)) picker_activate(app, index);
                }
                break;
            }

            /* RV-116: the right-click list and the slide show's settings take the next press */
            if ((app->ctx_open || app->slidepop_open) &&
                popups_press(app, event->mouse.x, event->mouse.y, event->mouse.button == RUBRAVIEW_MOUSE_RIGHT)) break;

            /* D-33: the list of subtitle tracks takes the next tap —
               a row chooses it, anywhere else closes the list. */
            if (app->sub_list_open) {
                size_t rows = 0;
                rubraview_rect_t list = sub_list_rect(app, &rows);
                int32_t row = rubraview_choice_list_row_at(list, SUB_LIST_ROW * rubraview_pal_window_dpi_scale(app->window),
                                                           rows, event->mouse.x, event->mouse.y);
                if (row >= 0) sub_list_choose(app, row);
                else if (!rubraview_rect_contains(list, event->mouse.x, event->mouse.y)) app->sub_list_open = false;
                break;
            }

            /* The A-B box and the pixel box answer a click of their own: the X leaves,
               the A-B box's buttons take this moment, a field is chosen (owner, 2026-10-15).
               Elsewhere the click goes on (the seek bar, a crop drag). */
            /* The strip at the bottom: a thumbnail clicked is the page to go to (owner, 2026-10-15).
               The floating boxes are drawn over it and keep their own clicks. */
            if (event->mouse.button == RUBRAVIEW_MOUSE_LEFT && !app->panel.open) {
                rubraview_tile_metrics_t m = tile_metrics(rubraview_pal_window_dpi_scale(app->window));
                rubraview_rect_t tb = rubraview_box_bounds(&app->toolbox, &m);
                bool on_box = app->toolbox.state != RUBRAVIEW_BOX_DETACHED && !app->chrome_hidden &&
                              rubraview_rect_contains(tb, event->mouse.x, event->mouse.y);
                if (!on_box && strip_press(app, event->mouse.x, event->mouse.y)) break;
            }
            if (event->mouse.button == RUBRAVIEW_MOUSE_LEFT && app->ab_edit_open) {
                rubraview_pal_rect_t box = ab_box_rect(app);
                if (pal_rect_has(answer_box_close(app, box), event->mouse.x, event->mouse.y)) { ab_edit_close(app); break; }
                if (pal_rect_has(ab_box_button(app, 0), event->mouse.x, event->mouse.y)) { ab_edit_take(app, 0); break; }
                if (pal_rect_has(ab_box_button(app, 1), event->mouse.x, event->mouse.y)) { ab_edit_take(app, 1); break; }
                if (pal_rect_has(box, event->mouse.x, event->mouse.y)) {
                    /* A field clicked is the one typed into; elsewhere in a half, that point's seconds. */
                    int32_t side = event->mouse.x < box.x + box.width * 0.5 ? 0 : 1, part = 2;
                    for (int32_t k = 0; k < 3; ++k) {
                        if (pal_rect_has(ab_part_rect(app, side, k), event->mouse.x, event->mouse.y)) part = k;
                    }
                    ab_edit_goto(app, side, part);
                    break;
                }
            }
            if (event->mouse.button == RUBRAVIEW_MOUSE_LEFT && app->px_edit_open) {
                rubraview_pal_rect_t box = px_box_rect(app);
                if (pal_rect_has(answer_box_close(app, box), event->mouse.x, event->mouse.y)) { px_edit_close(app); break; }
                if (pal_rect_has(box, event->mouse.x, event->mouse.y)) {
                    int32_t n = px_edit_fields(app);
                    int32_t k = (int32_t)((event->mouse.x - box.x) / box.width * (double)n);
                    app->px_edit_field = k < 0 ? 0 : k >= n ? n - 1 : k;
                    break;
                }
            }

            /* §3.13: with the workbench open, a drag on the picture
               is the crop rectangle. It is asked before the panel,
               whose "a click outside closes it" rule would otherwise
               shut the workbench at the first press on the picture. */
            /* The owner's edit: the boxes beside it, then the selection's handles and inside. */
            if (event->mouse.button == RUBRAVIEW_MOUSE_LEFT && classify_press(app, event->mouse.x, event->mouse.y)) break;
            bool edit_away = app->edit_window && edit_panel_open(app);   /* RV-137: Edit's boxes are in their own window */
            if (event->mouse.button == RUBRAVIEW_MOUSE_LEFT && !edit_away && edit_side_press(app, event->mouse.x, event->mouse.y)) break;
            /* The floating lists over the picture are theirs, not the selection's ("Edit to all"). */
            if (edit_panel_open(app) && app->recentwin.open && event->mouse.button == RUBRAVIEW_MOUSE_LEFT &&
                rubraview_rect_contains(rubraview_listwin_rect(&app->recentwin), event->mouse.x, event->mouse.y) &&
                recentwin_press(app, event->mouse.x, event->mouse.y)) break;
            if (edit_panel_open(app) && app->listwin.open &&
                rubraview_rect_contains(rubraview_listwin_rect(&app->listwin), event->mouse.x, event->mouse.y) &&
                listwin_press(app, event->mouse.x, event->mouse.y, event->mouse.button == RUBRAVIEW_MOUSE_RIGHT)) break;
            /* RV-137 (owner, 2026-10-18): in its own window, Edit chooses an area only after its button. */
            bool choosing = app->sel_mode;   /* only after the button (owner, 2026-10-18: a drag moved boxes too) */
            if (event->mouse.button == RUBRAVIEW_MOUSE_LEFT && edit_panel_open(app) && choosing &&
                (edit_away || !rubraview_rect_contains(app->panel.bounds, event->mouse.x, event->mouse.y))) {
                int32_t hit = sel_hit(app, event->mouse.x, event->mouse.y);
                if (hit && crop_point_to_image(app, event->mouse.x, event->mouse.y, &app->sel_grab_x, &app->sel_grab_y)) {
                    app->sel_grab = hit;
                    app->sel_grab_rect = app->edit.crop;
                    break;
                }
            }
            if (event->mouse.button == RUBRAVIEW_MOUSE_LEFT && edit_panel_open(app) && choosing &&
                (edit_away || !rubraview_rect_contains(app->panel.bounds, event->mouse.x, event->mouse.y)) &&
                !point_in_curve_widget(app, event->mouse.x, event->mouse.y) &&
                crop_point_to_image(app, event->mouse.x, event->mouse.y,
                                    &app->crop_drag_x0, &app->crop_drag_y0)) {
                app->crop_dragging = true;
                rubraview_edit_crop_drag(&app->edit, app->crop_drag_x0, app->crop_drag_y0,
                                         app->crop_drag_x0, app->crop_drag_y0);
                break;
            }

            if (!edit_away && panel_handle_press(app, event->mouse.x, event->mouse.y)) break;
            if (app->eqwin.open && eqwin_press(app, event->mouse.x, event->mouse.y,
                                              event->mouse.button == RUBRAVIEW_MOUSE_RIGHT)) break;
            if (app->recentwin.open && event->mouse.button == RUBRAVIEW_MOUSE_LEFT && recentwin_press(app, event->mouse.x, event->mouse.y)) break;
            if (app->favwin.open && event->mouse.button == RUBRAVIEW_MOUSE_LEFT && favwin_press(app, event->mouse.x, event->mouse.y)) break;
            if (app->listwin.open && listwin_press(app, event->mouse.x, event->mouse.y, event->mouse.button == RUBRAVIEW_MOUSE_RIGHT)) break;
            if (event->mouse.button == RUBRAVIEW_MOUSE_LEFT && app->subbox.selected &&
                subbox_press(app, event->mouse.x, event->mouse.y, true)) break;   /* D-33: S M R X on top */
            /* The anchors answer before the rest of the chrome: a
               press there is a click *or* the start of a drag. */
            if (box_press(app, event->mouse.x, event->mouse.y)) break;
            if (handle_chrome_click(app, event->mouse.x, event->mouse.y)) break;
            if (event->mouse.button == RUBRAVIEW_MOUSE_LEFT && subbox_press(app, event->mouse.x, event->mouse.y, false)) break;

            rubraview_pointer_context_t ctx = pointer_context(app);
            rubraview_pointer_intent_t intent;
            if (event->mouse.button == RUBRAVIEW_MOUSE_MIDDLE) {
                intent = rubraview_pointer_middle_click(&ctx);
            } else if (event->mouse.button == RUBRAVIEW_MOUSE_X1) {
                intent = rubraview_pointer_side_button(false);
            } else if (event->mouse.button == RUBRAVIEW_MOUSE_X2) {
                intent = rubraview_pointer_side_button(true);
            } else if (event->mouse.button == RUBRAVIEW_MOUSE_RIGHT && !app->edit_window &&
                       curve_widget_press(app, event->mouse.x, event->mouse.y, true)) {
                break;   /* a point taken off the curve */
            } else if (event->mouse.button == RUBRAVIEW_MOUSE_RIGHT) {
                intent = rubraview_pointer_right_click(&ctx);
                if (intent == RUBRAVIEW_INTENT_CONTEXT_MENU) {   /* RV-116: the list, at the pointer */
                    ctx_open_at(app, event->mouse.x, event->mouse.y);
                    break;
                }
            } else {
                /* D-109: what a press on the picture means is known when it is
                   let go — moved, it was a drag and the picture followed, at any
                   zoom; not moved, a click. */
                intent = RUBRAVIEW_INTENT_NONE;
                if (text_press(app, event->mouse.x, event->mouse.y, event->mouse.modifiers)) break;   /* RFC-0005: a selection */
                app->view_pressed = true;
                app->view_dragging = false;
                app->view_press_at = rubraview_pal_time_now_seconds();   /* 1.25.0: held still, a right click */
                app->view_press_x = app->view_drag_x = event->mouse.x;
                app->view_press_y = app->view_drag_y = event->mouse.y;
            }
            apply_intent(app, intent);
            break;
        }

        case RUBRAVIEW_WINDOW_EVENT_MOUSE_UP:
            if (scrollbar_release(app, app->window)) break;   /* RV-170 */
            if (app->col_drag && picker_columns_release(app, event->mouse.x)) break;   /* 1.25.0 */
            if (app->picker_hold && event->mouse.button == RUBRAVIEW_MOUSE_LEFT) {   /* 1.22.0: the tap, now it is one */
                app->picker_hold = false;
                size_t index = app->picker_hold_index;
                if (app->picker_open && index < app->picker_listing.count &&
                    !rubraview_picker_tap(&app->picker, index)) picker_activate(app, index);
                break;
            }
            if (event->mouse.button == RUBRAVIEW_MOUSE_LEFT && text_release(app, event->mouse.x, event->mouse.y)) break;
            if (app->view_pressed && !app->view_dragging && event->mouse.button == RUBRAVIEW_MOUSE_LEFT) {
                /* D-109: a click. Over a film or music it pauses and plays; over a
                   picture it is what it was (a page turned at the sides, the boxes
                   shown), and nothing on one larger than the window. */
                if (app->media) {
                    handle_action(app, PROVEN_LIT("media_play_pause"));
                } else {
                    rubraview_pointer_context_t ctx = pointer_context(app);
                    rubraview_pointer_intent_t intent = rubraview_pointer_click(&ctx, app->view_press_x, false);
                    /* 1.25.0: two clicks in one place, quickly, where a click turns no page: the picture back in the middle */
                    double now_c = rubraview_pal_time_now_seconds(), slop = 6.0 * rubraview_pal_window_dpi_scale(app->window);
                    bool twice = now_c - app->last_click_at < 0.45 && fabs(event->mouse.x - app->last_click_x) < slop &&
                                 fabs(event->mouse.y - app->last_click_y) < slop;
                    app->last_click_at = twice ? 0.0 : now_c;
                    app->last_click_x = event->mouse.x;
                    app->last_click_y = event->mouse.y;
                    if (twice && (intent == RUBRAVIEW_INTENT_NONE || intent == RUBRAVIEW_INTENT_PAN))
                        intent = rubraview_pointer_click(&ctx, app->view_press_x, true);
                    if (intent != RUBRAVIEW_INTENT_PAN) apply_intent(app, intent);
                }
            }
            app->view_pressed = false;
            app->view_dragging = false;
            sub_tile_result(app, rubraview_tap_release(&app->sub_tap, rubraview_pal_time_now_seconds()));
            seekfan_let_go(app);
            speed_tile_release(app, false);
            app->titlebar_held = RUBRAVIEW_TITLEBAR_NONE;
            if (app->fav_press >= 0) {
                size_t from = (size_t)app->fav_press;
                if (app->fav_dragging && from < app->favorites.count) {
                    int32_t fw = 0, fh = 0;
                    rubraview_pal_window_get_size(app->window, &fw, &fh);
                    size_t to = favorite_drop_index(app, event->mouse.x, rubraview_pal_window_dpi_scale(app->window), (double)fw);
                    if (to != from && rubraview_favorites_move(&app->favorites, from, to)) {
                        favorites_save(app);
                        osd_say(app, TU8("favourite moved"));
                    }
                } else if (from < app->favorites.count) {
                    rubraview_fs_entry_t st;   /* 1.24.0: a file among them opens in the viewer */
                    proven_u8str_view_t path = app_keep(app, app->favorites.paths[from]);
                    if (rubraview_pal_fs_stat(app->arena, path, &st) && !st.is_directory) { app->picker_open = false; open_path(app, path); }
                    else { picker_navigate(app, path); app->picker.focus = 0; }
                }
                app->fav_press = -1;
                app->fav_dragging = false;
                break;
            }
            if (app->eqwin.dragging || app->eqwin.sliding >= 0) { app->eqwin.dragging = false; app->eqwin.sliding = -1; break; }
            if (recentwin_release(app)) break;
            if (favwin_release(app)) break;
            if (listwin_release(app)) break;
            if (app->subbox.dragging != RUBRAVIEW_SUBBOX_NONE) {
                rubraview_subbox_end_drag(&app->subbox);
                break;
            }
            app->curve_dragging = -1;
            rubraview_panel_release(&app->sel_panel);
            rubraview_panel_release(&app->steps_panel);
            rubraview_panel_release(&app->classify_panel);
            app->panel.moving = false;
            if (app->sel_grab) {
                app->sel_grab = 0;
                if (edit_panel_open(app)) panel_build_edit(app);   /* "Apply to the selection" */
                break;
            }
            if (app->crop_dragging) {
                app->crop_dragging = false;
                app->edit_rev++;
                int32_t ix = 0, iy = 0;
                if (crop_point_to_image(app, event->mouse.x, event->mouse.y, &ix, &iy)) {
                    rubraview_edit_crop_drag(&app->edit, app->crop_drag_x0, app->crop_drag_y0, ix, iy);
                }
                /* A click without a drag clears the rectangle rather
                   than leaving a one-pixel crop behind. */
                if (app->edit.crop.width < 4 || app->edit.crop.height < 4) {
                    app->edit.crop_active = false;
                }
                crop_shown_again(app);
                if (edit_panel_open(app)) panel_build_edit(app);
                edit_sel_sync(app);
                break;
            }
            if (app->pagebar_dragging && !app->pagebar_detached) {
                pagebar_drag(app, event->mouse.x, true);
                break;
            }
            rubraview_panel_release(&app->panel);
            box_release(app, event->mouse.x, event->mouse.y);
            break;

        case RUBRAVIEW_WINDOW_EVENT_MOUSE_WHEEL: {
            if (chooser_wheel(app, CHOOSE_IN_MAIN, event->mouse.wheel_delta)) break;   /* 1.24.0 */
            if (app->picker_open) {
                picker_layout(app);
                rubraview_picker_scroll_by(&app->picker, -event->mouse.wheel_delta * app->picker.tile_extent *
                                                        (picker_details(app) ? 3.0 : 0.5));
                break;
            }
            if (box_wheel_opacity(app, event->mouse.x, event->mouse.y, event->mouse.wheel_delta, event->mouse.modifiers)) break;
            {   /* 1.25.0: over the toolbar's bar or its name, the wheel sizes the name and the messages */
                rubraview_tile_metrics_t wm = tile_metrics(rubraview_pal_window_dpi_scale(app->window));
                rubraview_rect_t bar = rubraview_box_timeline_rect(&app->toolbox, &wm), title = rubraview_box_title_rect(&app->toolbox, &wm);
                /* the band from a little over the bar down to the name's line (VM: the bar alone is a hairline) */
                double reach = 8.0 * rubraview_pal_window_dpi_scale(app->window);
                rubraview_rect_t band = bar;
                if (bar.width > 0.0) {
                    double bottom = title.height > 0.0 ? title.y + title.height : bar.y + bar.height + reach;
                    band = (rubraview_rect_t){ bar.x, bar.y - reach, bar.width, bottom - (bar.y - reach) };
                }
                if (app->toolbox.state != RUBRAVIEW_BOX_DETACHED &&
                    (rubraview_rect_contains(band, event->mouse.x, event->mouse.y) || rubraview_rect_contains(title, event->mouse.x, event->mouse.y))) {
                    double pct = rubraview_settings_get(&app->settings, PROVEN_LIT("display"), PROVEN_LIT("name_scale")) + (event->mouse.wheel_delta > 0 ? 10.0 : -10.0);
                    rubraview_settings_set(&app->settings, PROVEN_LIT("display"), PROVEN_LIT("name_scale"), pct);
                    char line[48];
                    int n = snprintf(line, sizeof(line), T("names and messages %d%%"), (int)lround(name_scale(app) * 100.0));
                    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
                    break;
                }
            }
            {
                /* The owner's edit: a box taller than the window scrolls. */
                rubraview_panel_t *boxes[4] = { &app->classify_panel, &app->steps_panel, &app->sel_panel, &app->panel };
                bool took = false;
                for (int b = 0; b < (app->edit_window ? 1 : 4) && !took; ++b) {   /* RV-137: Edit's scroll in its window */
                    if (!boxes[b]->open || !rubraview_rect_contains(boxes[b]->bounds, event->mouse.x, event->mouse.y)) continue;
                    rubraview_panel_scroll_by(boxes[b], -event->mouse.wheel_delta * boxes[b]->row_height * 3.0);
                    took = true;
                }
                if (took) break;
            }
            if (app->eqwin.open && eqwin_wheel(app, event->mouse.x, event->mouse.y, event->mouse.wheel_delta)) break;
            if (app->favwin.open && rubraview_rect_contains(rubraview_listwin_rect(&app->favwin), event->mouse.x, event->mouse.y)) {
                rubraview_listwin_scroll(&app->favwin, event->mouse.wheel_delta > 0 ? -3 : 3, app->favorites.count);
                break;
            }
            if (app->recentwin.open && rubraview_rect_contains(rubraview_listwin_rect(&app->recentwin), event->mouse.x, event->mouse.y)) {
                rubraview_listwin_scroll(&app->recentwin, event->mouse.wheel_delta > 0 ? -3 : 3, app->recent_row_count);
                break;
            }
            if (app->listwin.open && rubraview_rect_contains(rubraview_listwin_rect(&app->listwin), event->mouse.x, event->mouse.y)) {
                rubraview_listwin_scroll(&app->listwin, event->mouse.wheel_delta > 0 ? -3 : 3, list_rows(app));
                break;
            }
            /* 1.25.0: a tile first — over the text too (VM: the wheel on UTF8 scrolled nothing and chose nothing) */
            if (event->mouse.modifiers == 0 &&
                zoom_tile_wheel(app, event->mouse.x, event->mouse.y, event->mouse.wheel_delta)) break;
            if (text_wheel(app, event->mouse.wheel_delta, event->mouse.modifiers)) break;   /* RFC-0005 */
            if (strip_active(app) && event->mouse.modifiers == 0) {
                /* The wheel moves the strip, the way it moves any long page. */
                strip_scroll_by(app, -event->mouse.wheel_delta * 120.0 * rubraview_pal_window_dpi_scale(app->window));
                note_activity(app);
                break;
            }
            if (event->mouse.modifiers == 0 && !app->media && strip_shown(app) > 0.0 &&
                pal_rect_has(strip_rect(app), event->mouse.x, event->mouse.y)) {
                /* Over the strip the wheel goes through the thumbnails, a page a notch. */
                apply_intent(app, event->mouse.wheel_delta > 0.0 ? RUBRAVIEW_INTENT_PREV_PAGE : RUBRAVIEW_INTENT_NEXT_PAGE);
                strip_peek(app);
                break;
            }
            rubraview_pointer_context_t ctx = pointer_context(app);
            uint32_t wheel_mods = event->mouse.modifiers;
            /* D-109: while a film or music plays the plain wheel is the volume
               (Settings › Video › Wheel is the volume while playing); with that
               off it zooms a film, as Ctrl + wheel does, as it did before. */
            if (app->media && wheel_mods == 0) {
                if (rubraview_settings_get(&app->settings, PROVEN_LIT("video"), PROVEN_LIT("wheel_volume")) > 0.5) {
                    handle_action(app, event->mouse.wheel_delta > 0.0 ? PROVEN_LIT("media_volume_up") : PROVEN_LIT("media_volume_down"));
                    note_activity(app);
                    break;
                }
                if (app->media_info.has_video) wheel_mods = RUBRAVIEW_MOD_CTRL;
            }
            rubraview_pointer_intent_t wheel_intent = rubraview_pointer_wheel(&ctx, event->mouse.wheel_delta, wheel_mods);
            if (wheel_intent == RUBRAVIEW_INTENT_SCROLL_VERTICAL) {
                /* Both ways: it only ever moved the page one way, whichever way the wheel turned. */
                app->pan_y += event->mouse.wheel_delta > 0.0 ? PAN_STEP : -PAN_STEP;
                note_activity(app);
                break;
            }
            apply_intent(app, wheel_intent);
            if (wheel_intent == RUBRAVIEW_INTENT_NEXT_PAGE || wheel_intent == RUBRAVIEW_INTENT_PREV_PAGE) strip_peek(app);
            break;
        }

        case RUBRAVIEW_WINDOW_EVENT_GESTURE_ZOOM:
            /* §3.6.5: pinch-to-zoom about the gesture centroid. */
            app->zoom *= event->gesture.scale_ratio;
            if (app->zoom < 0.01) app->zoom = 0.01;
            note_activity(app);
            break;

        case RUBRAVIEW_WINDOW_EVENT_DROP:
        case RUBRAVIEW_WINDOW_EVENT_OPEN_REQUEST:
            handle_drop(app, event);
            break;

        case RUBRAVIEW_WINDOW_EVENT_GESTURE_PAN:
            if (app->picker_open) {
                rubraview_picker_scroll_by(&app->picker, -event->gesture.dy);
            } else {
                if (!app->swipe_going) {
                    app->swipe_going = true;
                    app->swipe_dx = app->swipe_dy = 0.0;
                    app->swipe_pan_x = app->pan_x;
                    app->swipe_pan_y = app->pan_y;
                }
                app->swipe_dx += event->gesture.dx;
                app->swipe_dy += event->gesture.dy;
                app->pan_x += event->gesture.dx;
                app->pan_y += event->gesture.dy;
            }
            note_activity(app);
            break;

        case RUBRAVIEW_WINDOW_EVENT_GESTURE_END: {
            /* RV-107 (§3.7.4): a finger drawn sideways over a page that has nowhere
               to go — fitted, not the strip — turns it; the page goes back where it was. */
            bool was = app->swipe_going;
            app->swipe_going = false;
            if (!was || app->picker_open || app->zoom > 1.001 || strip_active(app) || page_count(app) < 2) break;
            double across = app->swipe_dx < 0.0 ? -app->swipe_dx : app->swipe_dx;
            double down = app->swipe_dy < 0.0 ? -app->swipe_dy : app->swipe_dy;
            if (across < 2.0 * down) break;
            rubraview_pointer_context_t ctx = { .reading_direction = app->layout_opts.direction };
            rubraview_pointer_intent_t intent =
                rubraview_pointer_swipe(&ctx, app->swipe_dx, 64.0 * rubraview_pal_window_dpi_scale(app->window));
            if (intent != RUBRAVIEW_INTENT_NEXT_PAGE && intent != RUBRAVIEW_INTENT_PREV_PAGE) break;
            app->pan_x = app->swipe_pan_x;
            app->pan_y = app->swipe_pan_y;
            handle_action(app, intent == RUBRAVIEW_INTENT_NEXT_PAGE ? PROVEN_LIT("next_page") : PROVEN_LIT("prev_page"));
            break;
        }

        default:
            break;
    }
}
