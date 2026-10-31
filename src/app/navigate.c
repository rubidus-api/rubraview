/*
 * Moving through the pages: the layout rebuilt, the view reset, the zoom in steps, the window's title, the
 * strip (webtoon layout), a spread gone to, the next and the previous, past the end, the slide show switched
 * (RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* ---- layout ---- */

void rebuild_layout(app_state_t *app) {
    if (page_count(app) == 0) return;

    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, page_count(app), sizeof(rubraview_page_info_t));
    if (!proven_is_ok(res.err)) return;
    rubraview_page_info_t *infos = (rubraview_page_info_t*)(void*)res.value.ptr;

    double fallback_w = 800.0, fallback_h = 1200.0;
    for (size_t i = 0; i < page_count(app); ++i) {
        app_page_t *pg = &app->pages[i];
        if (pg->loaded) { pg->seen_w = picture_w(pg); pg->seen_h = picture_h(pg); }
    }
    for (size_t i = 0; i < page_count(app); ++i) {
        /* a page not read yet is taken as the first narrow one read is: a wide one is no model for the rest */
        const app_page_t *pg = &app->pages[i];
        if (pg->seen_w > 0 && pg->seen_h > 0 && (double)pg->seen_w < (double)pg->seen_h * 1.15) {
            fallback_w = (double)pg->seen_w;
            fallback_h = (double)pg->seen_h;
            break;
        }
    }

    for (size_t i = 0; i < page_count(app); ++i) {
        const app_page_t *pg = &app->pages[i];
        bool seen = pg->seen_w > 0 && pg->seen_h > 0;
        double w = pg->loaded ? (double)picture_w(pg) : seen ? (double)pg->seen_w : fallback_w;
        double h = pg->loaded ? (double)picture_h(pg) : seen ? (double)pg->seen_h : fallback_h;
        /* Pagination sees the page as the reader does, so a rotated
           portrait page is treated as the landscape it now presents. */
        rubraview_orientation_apply_size(app->orientation, w, h, &infos[i].width, &infos[i].height);
        /* §3.8.5: a page the archive tagged as a cover never pairs. */
        infos[i].force_standalone = app->comic_page_flags ? app->comic_page_flags[i].force_standalone : false;
        infos[i].skip = false;
    }

    /* D-98 (owner, 2026-10-11): in a layout for looking at pictures — two
       pages, a book, a comic, the strip — a film, a sound and a picture that
       moves are not shown; the picture after them is. The one the reader
       asked for by name is, alone. With nothing but those, all are shown. */
    bool pictures_only = app->layout_opts.mode != RUBRAVIEW_PAGE_LAYOUT_SINGLE;
    size_t stills = 0;
    for (size_t i = 0; i < page_count(app); ++i) {
        bool other = is_media_path(app->source.pages[i].path) || is_text_path(app->source.pages[i].path) ||
                     is_text_path(app->source.pages[i].name) || app->pages[i].animated;
        infos[i].skip = pictures_only && other && (int32_t)i != app->layout_exempt;
        if ((int32_t)i == app->layout_exempt && other) infos[i].force_standalone = true;
        if (!other) stills++;
    }
    for (size_t i = 0; i < page_count(app); ++i) {
        if (stills == 0) infos[i].skip = false;
        app->pages[i].skipped = infos[i].skip;
    }

    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);

    /* The reader's rotation (owner, 2026-10-08): a page it has laid on its
       side is one page shown whole, not a spread to cut in two. Turned
       upside down, each page is turned where it stands (owner, 2026-10-09). */
    rubraview_layout_opts_t opts = rubraview_layout_opts_turned(
        app->layout_opts, rubraview_orientation_swaps_axes(app->orientation));

    app->layout = rubraview_layout_compute(app->arena, infos, page_count(app),
                                           (double)win_w, (double)win_h, opts);
    if (app->spread_index >= app->layout.count && app->layout.count > 0) {
        app->spread_index = app->layout.count - 1;
    }
    app->needs_relayout = false;
}

size_t spread_index_for_page(const app_state_t *app, int32_t page_index) {
    for (size_t i = 0; i < app->layout.count; ++i) {
        if (app->layout.spreads[i].left_index == page_index ||
            app->layout.spreads[i].right_index == page_index) {
            return i;
        }
    }
    /* D-98: a page left out of the layout: the first spread after it, or the last before. */
    size_t before = 0;
    for (size_t i = 0; i < app->layout.count; ++i) {
        if (app->layout.spreads[i].left_index > page_index || app->layout.spreads[i].right_index > page_index) return i;
        before = i;
    }
    return before;
}

void rebuild_layout(app_state_t *app);
bool is_media_path(proven_u8str_view_t path);

/* The spread for a page the reader chose by name — in a list, on the page
   bar, by opening it. A film or an animation chosen so is shown whatever
   the layout (D-98): it is the one exception, until another page is turned to. */
size_t spread_for_chosen_page(app_state_t *app, int32_t page) {
    if (page >= 0 && (size_t)page < page_count(app) && page != app->layout_exempt &&
        (app->pages[page].skipped ||
         (app->layout_opts.mode != RUBRAVIEW_PAGE_LAYOUT_SINGLE &&
          (is_media_path(app->source.pages[page].path) || is_text_path(app->source.pages[page].path) || app->pages[page].animated ||
           page_moves(app, (size_t)page, NULL))))) {   /* a GIF not read yet: its bytes say */
        app->layout_exempt = page;
        rebuild_layout(app);
    }
    return spread_index_for_page(app, page);
}

int32_t current_page_index(const app_state_t *app) {
    if (app->layout.count == 0) return -1;
    return app->layout.spreads[app->spread_index].left_index;
}

/* ---- actions ---- */

void reset_view(app_state_t *app) {
    app->zoom = 1.0;
    app->pan_x = 0.0;
    app->pan_y = 0.0;
}

void note_activity(app_state_t *app) {
    rubraview_osd_notify_activity(&app->osd);
}

/* ---- D-109: the zoom in steps of percent ----
   100 % is the size the fit option gives (D-97). Real size is beside that
   scale, not on it — "0%" — so a step taken from it first finds where on
   the scale the picture is now. */
bool spread_composition(app_state_t *app, rubraview_composition_t *out);

bool at_real_size(const app_state_t *app) {
    return app->fit_mode == RUBRAVIEW_FIT_ACTUAL_SIZE && fabs(app->zoom - 1.0) < 0.0005;
}

/* Out of the real-size fit, keeping the picture the size it is on screen. */
void zoom_leave_real_fit(app_state_t *app) {
    if (app->fit_mode != RUBRAVIEW_FIT_ACTUAL_SIZE) return;
    rubraview_fit_mode_t fit = app->fit_beside_real != RUBRAVIEW_FIT_ACTUAL_SIZE ? app->fit_beside_real : RUBRAVIEW_FIT_WINDOW;
    rubraview_composition_t now, fitted;
    double zoom_was = app->zoom;
    bool had = spread_composition(app, &now) && now.count > 0;
    app->fit_mode = fit;
    app->zoom = 1.0;
    if (had && spread_composition(app, &fitted) && fitted.count > 0 && fitted.commands[0].scale > 0.0) {
        app->zoom = now.commands[0].scale / fitted.commands[0].scale;
    } else {
        app->zoom = zoom_was;
    }
}

void zoom_say(app_state_t *app) {
    char line[24];
    proven_u8str_view_t text = at_real_size(app) ? TU8("0% (real size)") : rubraview_scale_caption(line, sizeof(line), app->zoom);
    if (text.size > 0) osd_say(app, text);
    note_activity(app);
}

/* One step larger or smaller: Zoom+ and Zoom-, Ctrl + wheel, the wheel on a zoom tile. */
void zoom_step(app_state_t *app, bool larger) {
    zoom_leave_real_fit(app);
    double step = (g_zoom_step - 1.0) * 100.0;
    app->zoom = rubraview_zoom_step_percent(app->zoom * 100.0, step, larger) / 100.0;
    zoom_say(app);
}

/* A tap on the zoom tile: real size, 20 % .. 200 %, real size. */
void zoom_cycle(app_state_t *app) {
    bool real = at_real_size(app);
    zoom_leave_real_fit(app);
    double next = rubraview_zoom_cycle_percent(app->zoom * 100.0, real);
    if (next == 0.0) {
        if (app->fit_mode != RUBRAVIEW_FIT_ACTUAL_SIZE) app->fit_beside_real = app->fit_mode;
        app->fit_mode = RUBRAVIEW_FIT_ACTUAL_SIZE;
        reset_view(app);
    } else {
        app->zoom = next / 100.0;
    }
    zoom_say(app);
}

/* The caption names the file on screen and where it sits in the folder
   or archive, so the taskbar, Alt+Tab and a test script can all tell
   which page is showing. */
void update_window_title(app_state_t *app) {
    char title[768];
    size_t used = 0;
    if (app->picker_open) {   /* 1.24.0 (owner, 2026-10-19): "Rubraview - <the folder>" */
        proven_u8str_view_t name = rubraview_picker_is_places(app->picker_dir) ? TU8("PC")
                     : rubraview_picker_is_recent(app->picker_dir) ? TU8("Recent") : rubraview_path_basename(app->picker_dir);
        if (name.size == 0) name = app->picker_dir;
        snprintf(title, sizeof(title), "Rubraview - %.*s", (int)(name.size < 600 ? name.size : 600), name.ptr);
        rubraview_pal_window_set_title(app->window, title);
        return;
    }
    int32_t page = current_page_index(app);
    if (page >= 0 && (size_t)page < page_count(app)) {
        /* The name the status line shows, not the path: an archive page
           has no path of its own (found by T037). */
        proven_u8str_view_t name = page_display_name(app, (size_t)page);
        bool song = app->media && page == app->media_page && app->media_info.live && app->station_title[0];   /* RV-158 */
        int n = snprintf(title, sizeof(title), "%.*s%s%s (%d/%zu) ",
                         (int)name.size, name.ptr, song ? " \xE2\x80\x94 " : "", song ? app->station_title : "",
                         (int)page + 1, page_count(app));
        used = n > 0 ? ((size_t)n < sizeof(title) ? (size_t)n : sizeof(title) - 1) : 0;
        if (app->media && page == app->media_page) {
            /* A video shows where it is, to the millisecond — which is
               also how a frame step can be checked from outside. */
            char at[32], total[32];
            proven_u8str_view_t a = rubraview_format_timecode(at, sizeof(at), app->media_position, true);
            proven_u8str_view_t t = app->media_info.live ? TU8("live")   /* RV-157 */
                      : rubraview_format_timecode(total, sizeof(total), app->media_info.duration_seconds, true);
            n = snprintf(title + used, sizeof(title) - used, "%.*s / %.*s%s%s ",
                         (int)a.size, a.ptr, (int)t.size, t.ptr,
                         app->media_paused ? " paused" : "",
                         app->media_info.backend == RUBRAVIEW_BACKEND_FFMPEG ? " ffmpeg" : "");
            if (n > 0) used += (size_t)n < sizeof(title) - used ? (size_t)n : sizeof(title) - used - 1;
        } else if (!app->media && page == station_connecting_page(app)) {
            n = snprintf(title + used, sizeof(title) - used, "%s ", T("connecting..."));   /* RV-157 */
            if (n > 0) used += (size_t)n < sizeof(title) - used ? (size_t)n : sizeof(title) - used - 1;
        } else if (text_encoding_shown(app)) {
            /* RFC-0005 §6: what the text is read as. */
            n = snprintf(title + used, sizeof(title) - used, "[%s] ", text_encoding_shown(app));
            if (n > 0) used += (size_t)n < sizeof(title) - used ? (size_t)n : sizeof(title) - used - 1;
        } else if (anim_plays(app) && page == app->anim_page) {
            /* D-82: an animation says where it is as a film does. */
            char at[32], total[32];
            proven_u8str_view_t a = rubraview_format_timecode(at, sizeof(at), rubraview_animation_position(&app->animation), true);
            proven_u8str_view_t t = rubraview_format_timecode(total, sizeof(total), rubraview_animation_cycle_seconds(&app->animation), true);
            n = snprintf(title + used, sizeof(title) - used, "%.*s / %.*s%s ",
                         (int)a.size, a.ptr, (int)t.size, t.ptr, app->animation.paused ? " paused" : "");
            if (n > 0) used += (size_t)n < sizeof(title) - used ? (size_t)n : sizeof(title) - used - 1;
        }
    }
    size_t said = app->frame_stats ? strnlen(app->stats_text, sizeof(app->stats_text)) : 0;
    if (said > 0 && used + said + 3 < sizeof(title)) {   /* RV-215: [the timings] */
        title[used++] = '[';
        memcpy(title + used, app->stats_text, said);
        used += said;
        title[used++] = ']';
        title[used++] = ' ';
        title[used] = '\0';
    }
    snprintf(title + used, sizeof(title) - used, T("%sRubraview %s"), used > 0 ? "- " : "", RUBRAVIEW_VERSION_STRING);
    rubraview_pal_window_set_title(app->window, title);
}

/* ---- the strip (webtoon layout; owner, 2026-10-09) ----
   Every page at one width, one under the other, scrolled as one long
   page. The layout holds one page a spread, so the spread on screen is
   the page at the top of the window. A film, a song or an animation on
   that page is shown the ordinary way. */
bool strip_active(const app_state_t *app) {
    return app->layout_opts.mode == RUBRAVIEW_PAGE_LAYOUT_WEBTOON && page_count(app) > 0 &&
           app->media_page < 0 && !app->anim_active && app->layout.count == page_count(app);
}

/* The strip's width on screen: the width of the page it was entered on,
   no wider than the window, times the zoom. */
double strip_width(app_state_t *app, double win_w) {
    if (app->strip_base_w <= 0.0 || app->strip_base_count != page_count(app)) {
        app_page_t *page = ensure_page_loaded(app, (int32_t)app->spread_index);
        if (page && page->loaded) {
            double w = 0.0, h = 0.0;
            rubraview_orientation_apply_size(app->orientation, (double)picture_w(page), (double)picture_h(page), &w, &h);
            app->strip_base_w = w;
            app->strip_base_count = page_count(app);
        }
    }
    double base = app->strip_base_w > 0.0 ? app->strip_base_w : win_w;
    return (base < win_w ? base : win_w) * app->zoom;
}

double strip_page_height(void *context, size_t index) {
    app_state_t *app = (app_state_t *)context;
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    double width = strip_width(app, (double)win_w);
    if (index < page_count(app) && app->pages[index].skipped) return 0.0;   /* D-98: no place in the strip (the strip makes it a pixel) */
    app_page_t *page = ensure_page_loaded(app, (int32_t)index);
    if (!page || page->failed) return width * 0.1;
    if (!page->loaded) return width * 1.5;      /* on its way: a page's usual shape until it is here */
    double w = 0.0, h = 0.0;
    rubraview_orientation_apply_size(app->orientation, (double)picture_w(page), (double)picture_h(page), &w, &h);
    return w > 0.0 ? width * h / w : width * 1.5;
}

/* Moves the strip; returns what could not be moved (an end was reached). */
double strip_scroll_by(app_state_t *app, double delta) {
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) return delta;   /* minimised: there is no strip to measure */
    rubraview_strip_pos_t pos = { app->spread_index, app->strip_offset };
    double left = rubraview_strip_scroll(&pos, delta, page_count(app), (double)win_h, strip_page_height, app);
    app->strip_offset = pos.offset;
    if (pos.top != app->spread_index) {
        /* Another page is at the top: it is the one the title, the page bar
           and the lists speak of. No fade, no view reset — nothing jumped. */
        app->spread_index = pos.top;
        app->prev_spread_valid = false;
        strip_follow(app, pos.top);
        update_window_title(app);
        update_precache(app);
    }
    return left;
}

void go_to_spread(app_state_t *app, size_t index) {
    if (app->layout.count == 0) return;
    app->strip_offset = 0.0;
    if (index >= app->layout.count) index = app->layout.count - 1;
    if (app->layout_opts.mode == RUBRAVIEW_PAGE_LAYOUT_WEBTOON && app->layout.count == page_count(app)) {
        /* D-98: the strip has a place for every page; one it leaves out is stepped over. */
        while (index + 1 < app->layout.count && app->pages[index].skipped) index++;
        while (index > 0 && app->pages[index].skipped) index--;
    }
    if (index != app->spread_index && !subs_may_leave(app)) return;
    if (index != app->spread_index) {
        /* Remember what is leaving the screen, so the cross-fade has
           something to fade *from* (§3.2.5). */
        app->prev_spread_index = app->spread_index;
        app->prev_spread_valid = true;
        /* Settings > Viewer > Turning a page; a slide show quicker than half a second cuts (§3.2.5). */
        static const rubraview_transition_kind_t KINDS[] = { RUBRAVIEW_TRANSITION_CUT, RUBRAVIEW_TRANSITION_CROSSFADE,
                                                             RUBRAVIEW_TRANSITION_SLIDE_LEFT, RUBRAVIEW_TRANSITION_ZOOM_FADE,
                                                             RUBRAVIEW_TRANSITION_DISSOLVE };
        int turn = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("transition")));
        app->transition.kind = rubraview_transition_for_interval(
            KINDS[turn >= 0 && turn < 5 ? turn : 1],
            app->slideshow_running ? app->slideshow.interval_seconds : 1.0, 0.5);
        rubraview_transition_start(&app->transition);
    }
    app->spread_index = index;
    /* §3.4 Fit Lock: with the lock on, moving between images of
       different resolutions keeps the chosen zoom instead of resetting
       it — the point of the lock. */
    if (!app->fit_lock) reset_view(app);
    note_activity(app);

    int32_t page = current_page_index(app);
    if (app->layout_exempt >= 0 && page != app->layout_exempt) {
        app->layout_exempt = -1;      /* D-98: turned away from: it is left out like the others */
        app->needs_relayout = true;
    }
    if (page >= 0) strip_follow(app, (size_t)page);
    animation_prepare(app);
    media_prepare(app);
    update_window_title(app);
}

bool open_sibling_archive(app_state_t *app, bool forward);
bool open_sibling_folder(app_state_t *app, bool forward);

/* Next past the last page, or Back before the first (owner, 2026-10-14):
   Settings › Files › At the first and last — say so (as it comes), open the
   next or the previous folder or archive (from its last page going back),
   or go round within what is open. */
static void past_the_end(app_state_t *app, bool forward) {
    int mode = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("at_ends")));
    if (mode == 2 && app->layout.count > 1) {
        go_to_spread(app, forward ? 0 : app->layout.count - 1);
        update_precache(app);
        osd_say(app, forward ? TU8("the last: round again to the first") : TU8("the first: round again to the last"));
        return;
    }
    if (mode == 1) {
        bool opened = app->source.archive_path.size > 0 ? open_sibling_archive(app, forward)
                    : !app->list_is_set ? open_sibling_folder(app, forward) : false;
        if (opened) return;
        osd_say(app, forward ? TU8("the last, and nothing after it to open")
                             : TU8("the first, and nothing before it to open"));
        return;
    }
    osd_say(app, forward ? TU8("this is the last") : TU8("this is the first"));
}

void next_spread(app_state_t *app) {
    if (strip_active(app)) {
        /* A window's worth, less a little to keep one's place. */
        int32_t win_w = 0, win_h = 0;
        rubraview_pal_window_get_size(app->window, &win_w, &win_h);
        double step = (double)win_h * 0.9;
        if (strip_scroll_by(app, step) < step - 1.0) return;
        past_the_end(app, true);
        return;
    }
    if (app->spread_index + 1 < app->layout.count) {
        go_to_spread(app, app->spread_index + 1);
        update_precache(app);
        return;
    }
    past_the_end(app, true);   /* §3.8.1 point 4 when Settings › Files › At the first and last says so */
}

void prev_spread(app_state_t *app) {
    if (strip_active(app)) {
        int32_t win_w = 0, win_h = 0;
        rubraview_pal_window_get_size(app->window, &win_w, &win_h);
        double step = (double)win_h * 0.9;
        if (strip_scroll_by(app, -step) > -(step - 1.0)) return;
        past_the_end(app, false);
        return;
    }
    if (app->spread_index > 0) {
        go_to_spread(app, app->spread_index - 1);
        update_precache(app);
        return;
    }
    past_the_end(app, false);
}

proven_u8str_view_t menu_tile_caption(const rubraview_menu_state_t *menu, int32_t tile, char *scratch, size_t scratch_size,
                                 bool *out_enabled, bool *out_current);

void toggle_slideshow(app_state_t *app) {
    app->slideshow_running = !app->slideshow_running;
    if (app->slideshow_running) {
        app->slideshow.current_index = app->spread_index;
        rubraview_slideshow_resume(&app->slideshow);
        /* in the window as it is, full screen or not (owner, 2026-10-14, RV-116; §3.2.5 went full screen) */
    } else {
        rubraview_slideshow_pause(&app->slideshow);
        rubraview_pal_window_set_cursor_visible(app->window, true);
    }
}
