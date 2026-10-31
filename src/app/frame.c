/*
 * A pass of the viewer's loop: the boxes fitted to the window before the events, then the other windows'
 * queues, the timers and the polls, whether to draw, and the wait (RV-163, D-198: cut from wWinMain, its
 * lines as they were, `app.` read as `app->` and the loop's clock as `fc->`).
 */
#include "app.h"

void frame_fit_boxes(app_state_t *app) {
    /* RFC-0002 §4: the boxes open inside the window, whatever its size now. */
    int32_t view_w = 0, view_h = 0;
    rubraview_pal_window_get_size(app->window, &view_w, &view_h);
    app->toolbox.view_width = app->menubox.view_width = (double)view_w;
    app->toolbox.view_height = app->menubox.view_height = (double)view_h;
    /* With our own title bar the menu is its ☰'s, under the bar; with Windows' frame the M box stays. */
    app->menubox.headless = true;   /* 1.21.0: no M box either; the menu lives in the right click */
    if (app->menubox.headless) {
        app->menubox.anchor_x = 0.0;
        app->menubox.anchor_y = app->titlebar.height;
        app->menubox.pinned = false;   /* no pin: it folds as a menu does */
    }
    boxes_place(app);
    rubraview_tile_metrics_t side_metrics = tile_metrics(rubraview_pal_window_dpi_scale(app->window));
    rubraview_box_keep_side(&app->toolbox, &side_metrics);
    rubraview_box_keep_side(&app->menubox, &side_metrics);
    aspect_refresh(app);   /* the orientation, the edit panel or the custom pair may have changed */
}

void frame_tick(app_state_t *app, frame_clock_t *fc, size_t handled) {
    /* §3.22 / D-13: the settings window's own queue. Polling the main
       window pumped the thread's messages into it. */
    handled += shell_requests_pump(app);
    handled += settings_pump(app);
    handled += help_pump(app);
    handled += info_pump(app);
    handled += mini_pump(app);
    handled += files_pump(app);
    handled += fontpick_pump(app);
    handled += edit_pump(app);
    handled += batch_pump(app);   /* RV-194 */
    handled += subs_pump(app);

    if (app->needs_relayout) {
        int32_t page = current_page_index(app);
        rebuild_layout(app);
        if (page >= 0) app->spread_index = spread_index_for_page(app, page);
        if (page >= 0 && current_page_index(app) != page) {
            /* D-98: the page that was on screen is left out now: the one in its place is prepared as a turn would. */
            animation_prepare(app);
            media_prepare(app);
            update_window_title(app);
        }
    }
    if (strip_active(app)) {
        /* Whatever panned the page up or down — a drag, a touch, a key —
           moved the strip; with nothing to move, this keeps it inside its ends. */
        double dy = app->pan_y;
        app->pan_y = 0.0;
        strip_scroll_by(app, -dy);
    }

    double now = rubraview_pal_time_now_seconds();
    double dt = now - app->last_frame_seconds;
    if (dt < 0.0) dt = 0.0;
    app->last_frame_seconds = now;
    tick_timers(app, dt);
    scrollbar_tick(app);   /* RV-170: a scroll bar's track or button held goes on */
    sub_tile_result(app, rubraview_tap_tick(&app->sub_tap, now));   /* D-33: a tap decided, or a hold */
    if (rubraview_seekfan_tick(&app->seekfan, now)) app->toolbox_window_drawn = 0.0;   /* held: its choices open */
    /* D-34: thumbnails for the picker's tiles, a few a pass; given back when it closes. */
    shape_step(app);   /* RV-192: sizes being read for Shape */
    if (app->picker_open) {
        picker_thumbs_step(app);
    } else if (app->picker_thumbs) {
        picker_thumbs_free(app);
        app->picker_generation++;   /* what is still being made is for nobody */
        if (app->thumbs) rubraview_pal_thumbs_generation(app->thumbs, app->picker_generation);
    }
    strip_step(app, dt);   /* the wheel's strip: slide, fade, thumbnails */
    tiles_take_all(app);   /* D-40 */
    save_job_poll(app, false);
    apply_all_poll(app, false);
    if (app->edit_after[0]) {   /* a film's frame saved for Edit: opened once it is on screen */
        int32_t page = current_page_index(app);
        if (page >= 0 && app->pages[page].loaded && rubraview_path_same(app->source.pages[page].path, cstr(app->edit_after))) {
            app->edit_after[0] = '\0';
            if (!edit_panel_open(app)) panel_open_edit(app);
        }
    }
    arc_job_poll(app, false);
    page_job_poll(app);
    arena_refill(app);
    memory_watch(app, rubraview_pal_time_now_seconds());
    if (app->media_skip_pending) {
        /* D-9: the file nothing could open was reported; move past it. */
        app->media_skip_pending = false;
        next_spread(app);
    }
    station_job_poll(app);
    station_title_poll(app);   /* RV-158 */
    gmic_list_poll(app);   /* RFC-0009 C */
    double media_from = rubraview_pal_time_now_seconds();
    bool had_new = app->media_new_picture;
    media_tick(app);
    if (app->frame_stats && app->media_new_picture && !had_new) {   /* RV-215: a picture taken in this pass */
        app->stats_media += rubraview_pal_time_now_seconds() - media_from;
        app->stats_taken++;
    }
    bgm_tick(app);

    /* Redraw only while something can change on screen. Without a
       GPU, Direct2D rasterises on the CPU, and a loop that redrew
       the same still image kept two cores busy doing it. */
    /* D-82: an animation is paced as a film is — a frame when it has a
       new picture, not on every pass (VM, 2026-10-09: every pass kept
       two cores busy on a GIF and the keys waited behind it). */
    bool media_playing = (app->media && !app->media_paused) || (anim_plays(app) && !app->animation.paused);
    text_tick(app, now);   /* RFC-0005: auto-scroll moves by the time since the last pass */
    /* D-154: two copies with two Windows 11 menus, said once (ever). */
    if (!app->copies_told && (app->packaged || app->other_copies.store) && app->other_copies.msi_menu) {
        app->copies_told = true;
        if (rubraview_settings_get(&app->settings, PROVEN_LIT("shell"), PROVEN_LIT("told_twice")) < 0.5) {
            rubraview_settings_set(&app->settings, PROVEN_LIT("shell"), PROVEN_LIT("told_twice"), 1.0);
            osd_say(app, TU8("Rubraview is installed twice (MSI and Store): Windows 11's menu shows two; Settings › Explorer says what to do"));
        }
    }
    bool others_moving = app->slideshow_running || text_moving(app) ||
                         app->notice_seconds > 0.0 || app->pending_decode_count > 0 ||
                         app->rename_active ||   /* the caret blinks (owner, 2026-09-23) */
                         rubraview_tap_waiting(&app->sub_tap) ||   /* D-33: a tap or a hold being decided */
                         app->seekfan.down ||                      /* a seek button held: its choices open and light up */
                         scrollbar_held(app) ||                    /* RV-170: a scroll bar held goes on moving its list */
                         app->subbox.dragging != RUBRAVIEW_SUBBOX_NONE ||
                         app->picker_thumbs_arrived ||   /* D-34: a picture came in; the thread wakes the loop */
                         app->strip_arrived ||           /* a strip thumbnail came in */
                         (!app->filmstrip.visible && strip_shown(app) > 0.0) ||   /* the wheel's strip is up or fading */
                         app->filmstrip.scroll_offset != app->strip_target ||      /* it slides */
                         app->tiles_arrived;             /* D-40: a sharper tile came in */
    if (handled > 0) fc->last_input_seconds = now;
    if (handled > 0 || media_playing || others_moving) fc->last_busy_seconds = now;
    bool settled = now - fc->last_busy_seconds > IDLE_REDRAW_GRACE;
    rubraview_pal_time_fine(!settled || media_playing);   /* RV-215: 1 ms waits while anything moves, the default when still */
    /* Only the film or the sound moves: draw a new picture when one
       is up and the seek bar four times a second, not on every pass
       (T058 on the VM: every pass kept 1.75 of 2 cores busy on a
       sound-only page, and the keys waited behind it). For the grace
       after an input every pass still draws, as the chrome's fades
       expect. */
    /* The analyser moves some thirty times a second while a song plays on its page. */
    if (visualizer_live(app) && now - app->viz_last >= 1.0 / 30.0) app->media_new_picture = true;
    bool paced = media_playing && !others_moving && now - fc->last_input_seconds > IDLE_REDRAW_GRACE;
    /* Settled: a frame when what it would show has changed, checked
       each pass, and one every ten seconds whatever the summary says. */
    bool draw = paced ? rubraview_media_redraw_due(app->media_new_picture, now - fc->last_render_seconds)
                      : (!settled || now - fc->last_idle_frame_seconds >= 10.0 ||
                         scene_signature(app) != fc->last_scene_signature);
    if (draw) {
        double drawn_at = rubraview_pal_time_now_seconds();
        if (app->frame_stats && fc->last_render_seconds > 0.0 && now - fc->last_render_seconds > app->stats_worst)
            app->stats_worst = now - fc->last_render_seconds;
        render_frame(app);
        if (app->frame_stats) {   /* RV-215 */
            app->stats_frames++;
            app->stats_draw += rubraview_pal_time_now_seconds() - drawn_at;
        }
        fc->last_scene_signature = scene_signature(app);
        fc->last_render_seconds = now;
        app->media_new_picture = false;
        if (settled) fc->last_idle_frame_seconds = now;
    }
    if (app->frame_stats && now - app->stats_since >= 1.0) {   /* RV-215: once a second, into the status bar */
        double span = now - app->stats_since;
        snprintf(app->stats_text, sizeof(app->stats_text), T("%.0f frames/s, slowest %.0f ms, %u dropped, taking a picture %.1f ms, drawing %.1f ms"),
                 app->stats_frames / span, app->stats_worst * 1000.0, (unsigned)app->stats_dropped,
                 app->stats_taken ? app->stats_media * 1000.0 / app->stats_taken : 0.0,
                 app->stats_frames ? app->stats_draw * 1000.0 / app->stats_frames : 0.0);
        app->stats_since = now;
        app->stats_frames = app->stats_dropped = app->stats_taken = 0;
        app->stats_worst = app->stats_draw = app->stats_media = 0.0;
    }
    if (app->settings_open && now - app->settings_info_seconds >= 1.0) {
        /* `info` lines (cache in use, the adapter) are live data: once a second. */
        app->settings_info_seconds = now;
        app->settings_dirty = true;
    }
    draw_settings_window(app);
    draw_help_window(app);
    draw_info_window(app);
    /* The strip moves while the track does, so the little window is
       redrawn each pass it is playing. */
    if (app->mini_open && !mini_paused(app)) app->mini_dirty = true;
    draw_mini_window(app);
    files_preview_step(app, now);
    draw_files_window(app);
    draw_fontpick_window(app);
    draw_edit_window(app);
    draw_batch_window(app);   /* RV-194 */
    takeout_step(app);
    putin_step(app);
    draw_subs_window(app);
    toolbox_window_pump(app);
    draw_toolbox_window(app);

    page_failure_notice(app);   /* RV-110 */
    int config_changed = rubraview_pal_instance_take_config_changed();   /* D-105 */
    if (config_changed) settings_follow(app, config_changed);
    settings_share(app, rubraview_pal_time_now_seconds());

    /* A queued decode takes the loop's idle slice. With nothing to
       get ready, a settled loop sleeps until the OS has something
       for it instead of spinning. */
    if (drain_one_pending_decode(app)) {
        fc->last_busy_seconds = now;
    } else if (settled && settings_pump(app) == 0) {
        rubraview_pal_window_wait_event(app->window, 250);
    } else if (paced && !app->media_info.has_video) {
        /* Sound only: nothing is due before the next redraw, but the
           end and the A-B point are checked at least every 20 ms. */
        double wait = rubraview_media_redraw_wait(now - fc->last_render_seconds) * 1000.0;
        rubraview_pal_window_wait_event(app->window, wait < 1.0 ? 1u : wait > 20.0 ? 20u : (uint32_t)wait);
    } else {
        rubraview_pal_time_sleep_ms(4);
    }
}
