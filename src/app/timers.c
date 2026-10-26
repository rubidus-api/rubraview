/*
 * The per-frame timers (RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* ---- per-frame timers ---- */

void tick_timers(app_state_t *app, double dt) {
    rubraview_osd_tick(&app->osd, dt);
    hdr_screen_tick(app, dt);   /* RV-153 */
    media_stopped_tick(app);    /* RV-176 */
    picker_hold_tick(app);   /* 1.22.0 */
    view_hold_tick(app);     /* 1.25.0 */
    /* Where the pointer is now, not where it last moved — the same reason
       as the boxes below: a pointer resting on the titlebar must keep it. */
    /* With Windows' own title bar (Settings › General › Frameless window off) ours stays away. */
    if (app->frameless && !app->chrome_hidden) rubraview_titlebar_pointer_moved(&app->titlebar, app->pointer_y);
    else app->titlebar.shown = false;
    if (app->frameless && !app->chrome_hidden && app->menubox.headless && app->menubox.state != RUBRAVIEW_BOX_COLLAPSED) {
        app->titlebar.shown = true;   /* the bar stays while its menu is open (owner, 2026-10-15) */
        app->titlebar.idle_seconds = 0.0;
    }
    if (app->frameless && !app->chrome_hidden &&
        (app->picker_open || app->tmenu_open || rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("titlebar_pinned")) > 0.5)) {
        app->titlebar.shown = true;   /* 1.20.0: pinned; 1.24.0: and always over the open-file screen */
        app->titlebar.idle_seconds = 0.0;
    }
    {   /* 1.24.0: the window's title follows the open-file screen's folder, and goes back when it closes */
        uint64_t key = app->picker_open ? 1469598103934665603ull : 0;
        for (size_t i = 0; app->picker_open && i < app->picker_dir.size; ++i) key = (key ^ (unsigned char)app->picker_dir.ptr[i]) * 1099511628211ull;
        if (key != app->title_key) { app->title_key = key; update_window_title(app); }
    }
    rubraview_titlebar_tick(&app->titlebar, dt);
    rubraview_transition_tick(&app->transition, dt);
    animation_tick(app, dt);

    if (app->notice_seconds > 0.0) {
        /* A frame that waited on a long act (a save, a decode) must not
           spend the notice that act just raised before it is drawn. */
        app->notice_seconds -= dt > 0.25 ? 0.25 : dt;
        if (app->notice_seconds < 0.0) app->notice_seconds = 0.0;
    }

    double grace = 0.5; /* §3.6.3 */
    /* Where the pointer *is*, not where it last moved: a pointer resting
       on a box must hold it open. Pointer events stop arriving the moment
       the mouse stops, and the box used to fold up under it half a second
       later. */
    rubraview_tile_metrics_t box_metrics =
        tile_metrics(rubraview_pal_window_dpi_scale(app->window));
    rubraview_box_pointer(&app->toolbox, &box_metrics, app->pointer_x, app->pointer_y);
    rubraview_box_pointer(&app->menubox, &box_metrics, app->pointer_x, app->pointer_y);
    char hint_scratch[320];
    {
        /* RV-116: resting on a box's anchor, pin, grip or seek bar says what they do */
        rubraview_tile_metrics_t base = tile_metrics(rubraview_pal_window_dpi_scale(app->window));
        bool free_pointer = app->pointer_inside && !app->box_drag && !app->box_sizing && !app->ctx_open && !app->slidepop_open;
        proven_u8str_view_t hint = free_pointer ? rubraview_box_hint(&app->menubox, &base, app->pointer_x, app->pointer_y) : PROVEN_LIT("");
        const char *keys_of = NULL;   /* the owner (2026-10-18): every tooltip with its keys; the menu box is gone (1.21.0) */
        if (hint.size == 0 && free_pointer) {
            hint = rubraview_box_hint(&app->toolbox, &base, app->pointer_x, app->pointer_y);
            keys_of = rubraview_rect_contains(rubraview_box_pin_rect(&app->toolbox, &base), app->pointer_x, app->pointer_y) ? "toggle_toolbox_pin" : "toggle_toolbox";
        }
        if (hint.size > 0 && keys_of) hint = hint_with_keys(app, cstr(keys_of), (const char *)hint.ptr, hint_scratch, sizeof(hint_scratch));
        if (hint.size == 0 && free_pointer && app->titlebar.shown) {   /* 1.25.0: every title bar button says what it does */
            int32_t bw = 0, bh = 0;
            rubraview_pal_window_get_size(app->window, &bw, &bh);
            static const struct { rubraview_titlebar_button_t b; const char *action, *words; } TIPS[] = {
                { RUBRAVIEW_TITLEBAR_MENU, "", N_("Menu") },
                { RUBRAVIEW_TITLEBAR_NAME, "open_picker", N_("The open-file screen, here") },
                { RUBRAVIEW_TITLEBAR_SETTINGS, "open_settings", N_("Settings") },
                { RUBRAVIEW_TITLEBAR_BARPIN, "toggle_titlebar_pin", N_("P: keep the title bar shown") },
                { RUBRAVIEW_TITLEBAR_HIDE, "hide_boxes", N_("H: hide the title bar and the Floating Widget") },
                { RUBRAVIEW_TITLEBAR_PIN, "toggle_always_on_top", N_("T: always on top") },
                { RUBRAVIEW_TITLEBAR_FULLSCREEN, "toggle_fullscreen", N_("F: full screen") },
                { RUBRAVIEW_TITLEBAR_MINIMIZE, "", N_("Minimise") },
                { RUBRAVIEW_TITLEBAR_MAXIMIZE, "", N_("Maximise, or back to a window") },
                { RUBRAVIEW_TITLEBAR_RESIZE, "", N_("R: drag to size the window") },
                { RUBRAVIEW_TITLEBAR_CLOSE, "quit", N_("Close Rubraview") },
            };
            rubraview_titlebar_button_t over = rubraview_titlebar_hit(&app->titlebar, app->pointer_x, app->pointer_y, (double)bw);
            for (size_t k = 0; k < sizeof(TIPS) / sizeof(TIPS[0]) && hint.size == 0; ++k) {
                if (TIPS[k].b != over) continue;
                hint = TIPS[k].action[0] ? hint_with_keys(app, cstr(TIPS[k].action), rubraview_tr(TIPS[k].words), hint_scratch, sizeof(hint_scratch))
                                         : cstr(rubraview_tr(TIPS[k].words));
            }
        }
        if (hint.size == 0 && app->pointer_inside && app->toolbox.state != RUBRAVIEW_BOX_COLLAPSED &&
            app->toolbox.state != RUBRAVIEW_BOX_DETACHED && !app->ctx_open && !app->slidepop_open) {
            /* the layout's icon says which it is, as words, when the pointer rests on it (owner, 2026-10-14) */
            int32_t tile = rubraview_box_tile_at(&app->toolbox, &base, app->pointer_x, app->pointer_y);
            if (tile >= 0 && tile < app->toolbox_tile_count && proven_u8str_view_eq(app->toolbox_tiles[tile].action, PROVEN_LIT("toggle_layout"))) {
                hint = hint_with_keys(app, PROVEN_LIT("toggle_layout"), layout_said(rubraview_layout_choice_of(app->layout_opts.mode, app->layout_opts.direction)),
                                      hint_scratch, sizeof(hint_scratch));
            }
            if (tile >= 0 && tile < app->toolbox_tile_count && proven_u8str_view_eq(app->toolbox_tiles[tile].action, PROVEN_LIT("fit_cycle"))) {
                /* the fit it is in, in words (owner, 2026-10-15), and its keys */
                hint = hint_with_keys(app, cstr(fit_action_of(app->fit_mode)), rubraview_fit_tile_hint(app->fit_mode), hint_scratch, sizeof(hint_scratch));
            }
            if (hint.size == 0 && tile >= 0 && tile < app->toolbox_tile_count) {
                const char *said = rubraview_action_hint(app->toolbox_tiles[tile].action);
                hint = hint_with_keys(app, app->toolbox_tiles[tile].action, said, hint_scratch, sizeof(hint_scratch));
            }
        }
        if (hint.size == 0 && app->pointer_inside && app->menubox.state != RUBRAVIEW_BOX_COLLAPSED && !app->ctx_open) {
            /* A menu item whose name does not explain itself says what it does (owner, 2026-10-15: Crisp, Grid). */
            int32_t tile = rubraview_box_tile_at(&app->menubox, &base, app->pointer_x, app->pointer_y);
            const rubraview_menu_item_t *item = tile >= 0 ? rubraview_menu_item_at(&app->menu, tile) : NULL;
            const char *said = item ? rubraview_action_hint(item->action) : NULL;
            if (item) hint = hint_with_keys(app, item->action, said, hint_scratch, sizeof(hint_scratch));
        }
        /* Kept in the app's own buffer, so it is the same text however it was made. */
        if (hint.size >= sizeof(app->hint_text)) hint.size = sizeof(app->hint_text) - 1;
        if (hint.size != app->hint.size || memcmp(hint.ptr, app->hint_text, hint.size) != 0) {
            memmove(app->hint_text, hint.ptr, hint.size);
            app->hint_text[hint.size] = '\0';
            app->hint = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->hint_text, .size = hint.size };
            app->hint_since = rubraview_pal_time_now_seconds();
        }
    }
    rubraview_box_tick(&app->toolbox, dt, grace);
    rubraview_box_tick(&app->menubox, dt, grace);
    /* However it was opened, the menu opens at its root, Recent as it is now. */
    if (rubraview_box_just_opened(&app->menubox, &app->menubox_was_open)) menu_rebuild(app);

    if (app->slideshow_running) {
        /* §3.2.5: the pointer goes away over the slides only — not while something is open to be
           pointed at (the slide show's settings, a list, a box, the settings window). */
        bool pointing = app->slidepop_open || app->ctx_open || app->settings_open || app->panel.open || app->listwin.open ||
                        app->sub_list_open || app->menubox.state != RUBRAVIEW_BOX_COLLAPSED ||
                        (app->toolbox.state != RUBRAVIEW_BOX_COLLAPSED && app->toolbox.state != RUBRAVIEW_BOX_DETACHED);
        if (pointing) {
            if (rubraview_cursor_hide_notify_motion(&app->cursor)) rubraview_pal_window_set_cursor_visible(app->window, true);
        } else if (rubraview_cursor_hide_tick(&app->cursor, dt)) {
            rubraview_pal_window_set_cursor_visible(app->window, false);
        }
        /* What the slide show steps through is what is on screen (owner, 2026-10-14: it skipped files):
           the spreads as they are now, from the one shown, timed from when it is drawn. */
        slides_follow_screen(app);
        rubraview_slideshow_event_t event = slide_ready(app) ? rubraview_slideshow_tick(&app->slideshow, app->slides, dt)
                                                             : RUBRAVIEW_SLIDESHOW_NONE;
        if (event == RUBRAVIEW_SLIDESHOW_ADVANCED) {
            app->subs_leave_unasked = true;
            go_to_spread(app, app->slideshow.current_index);
            app->subs_leave_unasked = false;
        } else if (event == RUBRAVIEW_SLIDESHOW_ENDED) {
            app->slideshow_running = false;
            rubraview_pal_window_set_cursor_visible(app->window, true);
        }
    }
}
