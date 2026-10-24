/*
 * The pointer: its context and intents, the edge buttons, the title bar and the chrome clicked
 * (RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* ---- pointer ---- */

rubraview_pointer_context_t pointer_context(const app_state_t *app) {
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    return (rubraview_pointer_context_t){
        .window_width = (double)win_w,
        .reading_direction = app->layout_opts.direction,
        .comic_mode = (app->layout_opts.mode != RUBRAVIEW_PAGE_LAYOUT_SINGLE) || page_count(app) > 1,
        .zoomed_in = app->zoom > 1.001 || strip_active(app),   /* a drag moves the strip */
        .scrollable_vertically = (app->fit_mode == RUBRAVIEW_FIT_WIDTH),
        .wheel_zooms = !app->media && !strip_active(app) && !app->text,
    };
}

void apply_intent(app_state_t *app, rubraview_pointer_intent_t intent) {
    switch (intent) {
        case RUBRAVIEW_INTENT_NEXT_PAGE:      next_spread(app); break;
        case RUBRAVIEW_INTENT_PREV_PAGE:      prev_spread(app); break;
        case RUBRAVIEW_INTENT_SKIP_FORWARD:   go_to_spread(app, app->spread_index + 10); break;
        case RUBRAVIEW_INTENT_SKIP_BACKWARD:
            go_to_spread(app, app->spread_index > 10 ? app->spread_index - 10 : 0);
            break;
        case RUBRAVIEW_INTENT_ZOOM_IN:        zoom_step(app, true); break;
        case RUBRAVIEW_INTENT_ZOOM_OUT:       zoom_step(app, false); break;
        case RUBRAVIEW_INTENT_SCROLL_VERTICAL: app->pan_y -= PAN_STEP; note_activity(app); break;
        case RUBRAVIEW_INTENT_TOGGLE_FULLSCREEN:
            rubraview_pal_window_set_fullscreen(app->window, !rubraview_pal_window_is_fullscreen(app->window));
            break;
        case RUBRAVIEW_INTENT_TOGGLE_ACTUAL_SIZE:
            if (app->fit_mode != RUBRAVIEW_FIT_ACTUAL_SIZE) app->fit_beside_real = app->fit_mode;
            app->fit_mode = (app->fit_mode == RUBRAVIEW_FIT_ACTUAL_SIZE)
                ? RUBRAVIEW_FIT_WINDOW : RUBRAVIEW_FIT_ACTUAL_SIZE;
            reset_view(app);
            break;
        case RUBRAVIEW_INTENT_TOGGLE_OVERLAY:
            break;   /* 1.25.0: a click opens no toolbar */
        case RUBRAVIEW_INTENT_CENTER_PICTURE:
            handle_action(app, PROVEN_LIT("center_picture"));
            break;
        default: break;
    }
}

/* The edge buttons show while the pointer is near a side's middle, on a
   page, with nothing else of the viewer's in the way (owner, 2026-09-29). */
static bool edge_nav_allowed(const app_state_t *app) {
    return app->pointer_inside && !app->cursor.hidden && page_count(app) > 0 && !app->picker_open &&
           !app->settings_open && !app->panel.open && !app->titlebar.shown;
}

static bool over_a_box(app_state_t *app, double x, double y) {
    rubraview_tile_metrics_t m = tile_metrics(rubraview_pal_window_dpi_scale(app->window));
    rubraview_box_t *boxes[] = { &app->menubox, &app->toolbox };
    for (size_t i = 0; i < 2; ++i) {
        if (boxes[i]->state == RUBRAVIEW_BOX_DETACHED) continue;
        if (rubraview_rect_contains(rubraview_box_bounds(boxes[i], &m), x, y)) return true;
    }
    return false;
}

rubraview_edge_side_t edge_nav_side(app_state_t *app, double x, double y) {
    if (!edge_nav_allowed(app) || over_a_box(app, x, y)) return RUBRAVIEW_EDGE_NONE;
    if (app->listwin.open && rubraview_rect_contains(rubraview_listwin_rect(&app->listwin), x, y)) return RUBRAVIEW_EDGE_NONE;
    if (app->eqwin.open && rubraview_rect_contains(rubraview_eqwin_rect(&app->eqwin), x, y)) return RUBRAVIEW_EDGE_NONE;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    rubraview_edgenav_t nav = rubraview_edgenav_create(rubraview_pal_window_dpi_scale(app->window));
    return rubraview_edgenav_revealed(&nav, x, y, (double)w, (double)h);
}

static rubraview_edge_hit_t edge_nav_hit(app_state_t *app, double x, double y) {
    rubraview_edge_hit_t none = { RUBRAVIEW_EDGE_NONE, -1 };
    if (edge_nav_side(app, x, y) == RUBRAVIEW_EDGE_NONE) return none;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    rubraview_edgenav_t nav = rubraview_edgenav_create(rubraview_pal_window_dpi_scale(app->window));
    return rubraview_edgenav_hit(&nav, x, y, (double)w, (double)h);
}

/* D-110: the title bar and the floating boxes go away — the toolbox in a
   window of its own too — until the picture is clicked. */
int32_t tmenu_win_w(const app_state_t *app) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    return w;
}

void chrome_hide(app_state_t *app, bool hide) {
    if (app->chrome_hidden == hide) return;
    app->chrome_hidden = hide;
    if (hide) {
        app->titlebar.shown = false;
        app->titlebar_held = RUBRAVIEW_TITLEBAR_NONE;
    }
    if (app->toolbox_window) rubraview_pal_window_set_visible(app->toolbox_window, !hide);
    app->toolbox_window_drawn = 0.0;
}


/* §3.21.3: the titlebar's controls take precedence while it is shown,
   over the viewer and over the picker alike. True when it took the click. */
bool titlebar_click(app_state_t *app, double x, double y) {
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    rubraview_tile_metrics_t metrics = tile_metrics(rubraview_pal_window_dpi_scale(app->window));

    rubraview_titlebar_button_t button = rubraview_titlebar_hit(&app->titlebar, x, y, (double)win_w);
    switch (button) {
        case RUBRAVIEW_TITLEBAR_CLOSE:      if (subs_may_leave(app)) rubraview_pal_window_request_close(app->window); return true;
        case RUBRAVIEW_TITLEBAR_MINIMIZE:   rubraview_pal_window_minimize(app->window); return true;
        case RUBRAVIEW_TITLEBAR_MAXIMIZE:   rubraview_pal_window_toggle_maximize(app->window); return true;
        case RUBRAVIEW_TITLEBAR_FULLSCREEN:
            rubraview_pal_window_set_fullscreen(app->window, !rubraview_pal_window_is_fullscreen(app->window));
            return true;
        case RUBRAVIEW_TITLEBAR_PIN:        handle_action(app, PROVEN_LIT("toggle_always_on_top")); return true;
        case RUBRAVIEW_TITLEBAR_HIDE:       handle_action(app, PROVEN_LIT("hide_boxes")); return true;
        case RUBRAVIEW_TITLEBAR_SETTINGS:   handle_action(app, PROVEN_LIT("open_settings")); return true;   /* 1.24.0 */
        case RUBRAVIEW_TITLEBAR_BARPIN:     handle_action(app, PROVEN_LIT("toggle_titlebar_pin")); return true;   /* 1.25.0 */
        case RUBRAVIEW_TITLEBAR_MENU:       app->tmenu_open = !app->tmenu_open; app->tmenu_focus = -1; return true;   /* 1.25.0 */
        case RUBRAVIEW_TITLEBAR_NAME:       /* 1.25.0: the name opens the open-file screen (inside a book being read) */
            if (!app->picker_open) handle_action(app, PROVEN_LIT("open_picker"));
            return true;
        case RUBRAVIEW_TITLEBAR_OPEN:
        case RUBRAVIEW_TITLEBAR_SNAP_BOXES:
            (void)metrics;
            return true;
        case RUBRAVIEW_TITLEBAR_RESIZE:
        case RUBRAVIEW_TITLEBAR_CAPTION:
            /* A double press on the caption maximises or restores, as a system
               title bar does (owner, 2026-09-29). Moving or sizing waits for
               the pointer to move with the button held: the system's loop
               started on the press itself would swallow the second click. */
            if (button == RUBRAVIEW_TITLEBAR_CAPTION &&
                rubraview_titlebar_caption_press(&app->titlebar, rubraview_pal_time_now_seconds(), x, y, 0.5,
                                                 4.0 * rubraview_pal_window_dpi_scale(app->window))) {
                rubraview_pal_window_toggle_maximize(app->window);
                app->titlebar_held = RUBRAVIEW_TITLEBAR_NONE;
            } else {
                app->titlebar_held = button;
                app->titlebar_held_x = x;
                app->titlebar_held_y = y;
            }
            return true;
        default: break;
    }
    return false;
}

/* Returns true when the chrome consumed the click. */
bool handle_chrome_click(app_state_t *app, double x, double y) {
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    rubraview_tile_metrics_t metrics = tile_metrics(rubraview_pal_window_dpi_scale(app->window));

    if (titlebar_click(app, x, y)) return true;

    /* The title bar's menu folds on a click anywhere else, and that click does nothing more. */
    if (app->menubox.headless && app->menubox.state != RUBRAVIEW_BOX_COLLAPSED &&
        !rubraview_rect_contains(rubraview_box_bounds(&app->menubox, &metrics), x, y) &&
        !(app->titlebar.shown && y < app->titlebar.height)) {
        rubraview_box_dismiss(&app->menubox);
        return true;
    }

    /* The anchor's two buttons answer first: they sit on top of the
       box, and a click there is never a tile. */
    if (rubraview_box_click(&app->menubox, &metrics, x, y)) {
        menu_rebuild(app);   /* opened or closed: Recent as it is now, from the root */
        return true;
    }
    bool toolbox_here = app->toolbox.state != RUBRAVIEW_BOX_DETACHED;   /* detached, it answers in its own window */
    if (toolbox_here && rubraview_box_anchor_half_at(&app->toolbox, &metrics, x, y) == RUBRAVIEW_ANCHOR_CLICK) {
        rubraview_box_click_open(&app->toolbox);   /* 1.25.0 */
        return true;
    }
    if (toolbox_here && rubraview_box_click(&app->toolbox, &metrics, x, y)) return true;

    /* The pins at the boxes' top-left (owner, 2026-09-22). */
    if (rubraview_box_pin_click(&app->menubox, &metrics, x, y)) return true;
    if (toolbox_here && rubraview_box_pin_click(&app->toolbox, &metrics, x, y)) return true;
    /* The strip's seek bar: a click puts the film there. */
    if (toolbox_here && play_duration(app) > 0.0) {
        rubraview_rect_t bar = rubraview_box_timeline_rect(&app->toolbox, &metrics);
        if (bar.width > 0.0 && x >= bar.x && x < bar.x + bar.width &&
            y >= bar.y - bar.height && y < bar.y + bar.height * 2.0) {   /* a little taller than drawn: easier to hit */
            double seconds = rubraview_seekbar_time(bar.x, bar.width, x, play_duration(app));
            play_seek(app, seconds);
            note_activity(app);
            return true;
        }
    }
    /* The same bar over a still picture: a click goes to that page. */
    if (toolbox_here && pagebar_shown(app)) {
        rubraview_rect_t bar = rubraview_box_timeline_rect(&app->toolbox, &metrics);
        if (bar.width > 0.0 && x >= bar.x && x < bar.x + bar.width &&
            y >= bar.y - bar.height && y < bar.y + bar.height * 2.0) {
            pagebar_press(app, bar.x, bar.width, x, false);
            note_activity(app);
            return true;
        }
    }

    int32_t tile = rubraview_box_tile_at(&app->menubox, &metrics, x, y);
    if (tile >= 0) {
        /* A dimmed item does nothing, as a dimmed toolbox tile does. */
        bool enabled = true;
        char scratch[64];
        (void)menu_tile_caption(&app->menu, tile, scratch, sizeof(scratch), &enabled, NULL);
        if (!enabled) return true;
        /* Delete asks first (owner, 2026-09-21): the first tap arms it and
           its tile says "Delete?"; a second tap on it within five seconds
           acts. Any other tap disarms it. */
        const rubraview_menu_item_t *item = rubraview_menu_item_at(&app->menu, tile);
        bool clears = item && proven_u8str_view_eq(item->action, PROVEN_LIT("recent_clear"));   /* D-104: asks first too */
        if (item && (clears || proven_u8str_view_eq(item->action, PROVEN_LIT("delete_file")))) {
            if (!rubraview_confirm_press(&app->menu_confirm, item - app->menu.tree->items,
                                         rubraview_pal_time_now_seconds())) {
                osd_say(app, clears ? TU8("tap Clear again to empty the recent list")
                                    : TU8("tap Delete again to move it to the recycle bin"));
                return true;
            }
        } else {
            rubraview_confirm_clear(&app->menu_confirm);
        }
        proven_u8str_view_t action;
        rubraview_menu_result_t result = rubraview_menu_activate(&app->menu, tile, &action);
        if (result == RUBRAVIEW_MENU_ACTIVATED) {
            if (app->menubox.headless) rubraview_box_dismiss(&app->menubox);   /* a choice made: the bar's menu folds */
            handle_action(app, action);
        } else if (result == RUBRAVIEW_MENU_DESCENDED || result == RUBRAVIEW_MENU_WENT_BACK) {
            sync_menubox_tiles(app);
        }
        return true;
    }

    tile = toolbox_here ? rubraview_box_tile_at(&app->toolbox, &metrics, x, y) : -1;
    if (tile >= 0) {
        /* RFC-0002 §4: the tiles of the profile on screen; a dimmed one does nothing. */
        toolbox_refresh(app);
        if (tile < app->toolbox_tile_count) {
            bool enabled = true;
            (void)toolbox_caption(app, &app->toolbox_tiles[tile], &enabled);
            if (enabled && proven_u8str_view_eq(app->toolbox_tiles[tile].action, PROVEN_LIT("toggle_subtitles"))) {
                /* D-33: a tap toggles, a double tap or a hold chooses —
                   so this tile is decided on release, not on the press. */
                sub_tile_result(app, rubraview_tap_press(&app->sub_tap, rubraview_pal_time_now_seconds()));
            } else if (enabled && seek_tile_action(app->toolbox_tiles[tile].action, &(bool){ false })) {
                /* Owner, 2026-10-09: a tap seeks 5 s on release; held, the
                   button offers longer steps to drag onto. */
                bool forward = false;
                (void)seek_tile_action(app->toolbox_tiles[tile].action, &forward);
                app->seekfan_detached = false;
                rubraview_seekfan_press(&app->seekfan, forward, rubraview_pal_time_now_seconds(),
                                        rubraview_box_tile_rect(&app->toolbox, &metrics, tile));
            } else if (enabled && (proven_u8str_view_eq(app->toolbox_tiles[tile].action, PROVEN_LIT("media_speed_cycle")) ||
                                   proven_u8str_view_eq(app->toolbox_tiles[tile].action, PROVEN_LIT("interval_cycle")))) {
                /* D-110: a tap or a drag, known on release. D-123: the Delay tile the same way. */
                speed_tile_press(app, y, false, proven_u8str_view_eq(app->toolbox_tiles[tile].action, PROVEN_LIT("interval_cycle")));
            } else if (enabled) {
                /* 1.24.0: a one-of-many tile lists its choices, over it (under it when there is no room above) */
                rubraview_rect_t at = rubraview_box_tile_rect(&app->toolbox, &metrics, tile);
                if (!chooser_open_tile(app, app->toolbox_tiles[tile].action, at.x, at.y + at.height, at.y))
                    handle_action(app, app->toolbox_tiles[tile].action);
            }
        }
        return true;
    }

    /* A lyric line: the song goes to it (owner, 2026-09-30). */
    if (app->media && !app->media_info.has_video && app->music_lyrics.count > 0) {
        for (size_t i = 0; i < 7; ++i) {
            if (app->lyric_row_index[i] < 0 || !rubraview_rect_contains(app->lyric_row_rect[i], x, y)) continue;
            double t = rubraview_lyrics_time_of(&app->music_lyrics, app->lyric_row_index[i]);   /* -1 for untimed words */
            if (t < 0.0) break;   /* words without times: the click is the song's, Play / Pause */
            media_seek_to(app, t);
            app->media_position = t;
            return true;
        }
    }

    /* The edge buttons (owner, 2026-09-29), under everything above. */
    rubraview_edge_hit_t edge = edge_nav_hit(app, x, y);
    if (edge.side != RUBRAVIEW_EDGE_NONE) {
        handle_action(app, cstr(rubraview_edgenav_action(edge.side, edge.slot, app->media != NULL)));
        return true;
    }

    return false;
}
