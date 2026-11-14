/*
 * RV-277: the navigator, a box at the pointer in the middle of the window (ui_navbox.h holds its geometry and its
 * rules): when it shows, how it is drawn, what a press on it does. RV-285: the pointer over the middle shows a
 * see-through hint there; a click there opens the box, over a picture, a book, a film or music; it closes by its X,
 * Esc, a click outside it (which does nothing else) or the pointer away from it and the middle for 2 s.
 */
#include "app.h"
#include "rubraview/ui_navbox.h"
#include "rubraview/playback.h"

/* `pressing`: a press is being decided (the click that opens it): the button held is not in the way then. */
static bool navbox_allowed(app_state_t *app, bool pressing) {
    if (rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("navigator")) < 0.5) return false;
    if (app->navbox_shown && app->pagebar_dragging && !app->pagebar_detached) return true;   /* its bar being dragged */
    return app->pointer_inside && !app->cursor.hidden && page_count(app) > 0 && !app->picker_open &&
           !app->settings_open && !app->panel.open && !app->titlebar.shown && !app->text &&
           (pressing || (!app->view_pressed && !app->view_dragging)) && !app->pagebar_dragging && !app->ctx_open &&
           !app->tmenu_open && !window_taking_input(app) && app->zoom <= 1.001 && !strip_active(app);
}

static bool over_a_window(app_state_t *app, double x, double y) {
    if (app->listwin.open && rubraview_rect_contains(rubraview_listwin_rect(&app->listwin), x, y)) return true;
    if (app->eqwin.open && rubraview_rect_contains(rubraview_eqwin_rect(&app->eqwin), x, y)) return true;
    return false;
}

static bool in_middle_at(app_state_t *app, double x, double y) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    return rubraview_rect_contains(rubraview_navbox_middle((double)w, (double)h), x, y) && !over_a_window(app, x, y);
}

/* Once a pass (tick_timers): the hint, and whether the box stays. */
void navbox_tick(app_state_t *app) {
    double now = rubraview_pal_time_now_seconds();
    double px = app->pointer_x, py = app->pointer_y;
    bool allowed = navbox_allowed(app, false);
    bool in_middle = allowed && in_middle_at(app, px, py);
    bool in_box = app->navbox_shown && rubraview_rect_contains(app->navbox.box, px, py);
    if (app->navbox_shown && app->navbox.media != (app->media != NULL)) app->navbox_shown = false;   /* a film after a picture, or back: a click opens the right one */
    if (in_middle || in_box) app->navbox_near_at = now;
    app->navbox_shown = rubraview_navbox_keep(app->navbox_shown, allowed, in_middle || in_box, now - app->navbox_near_at);
    app->navbox_hint = rubraview_navbox_hint(allowed, app->navbox_shown, in_middle,
                                             app->view_pressed || app->edge_press.on);
}

/* A click let go at (x, y): in the middle, the box opens there. True when it did. */
bool navbox_click(app_state_t *app, double x, double y) {
    if (app->navbox_shown || !navbox_allowed(app, true) || !in_middle_at(app, x, y)) return false;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    app->navbox = rubraview_navbox_place(x, y, (double)w, (double)h, rubraview_pal_window_dpi_scale(app->window), app->media != NULL);
    app->navbox_shown = true;
    app->navbox_hint = false;
    app->navbox_opened_at = app->navbox_near_at = rubraview_pal_time_now_seconds();
    note_activity(app);
    return true;
}

/* Esc: true when the box was up and is now closed. */
bool navbox_escape(app_state_t *app) {
    if (!app->navbox_shown) return false;
    app->navbox_shown = false;
    return true;
}

/* A press: true when the box took it. A press outside closes it and does nothing else. */
bool navbox_press(app_state_t *app, double x, double y) {
    if (!app->navbox_shown) return false;
    rubraview_navbox_part_t part = rubraview_navbox_hit(&app->navbox, x, y);
    double now = rubraview_pal_time_now_seconds();
    if (part == RUBRAVIEW_NAVBOX_NONE) {
        if (!rubraview_rect_contains(app->navbox.box, x, y)) app->navbox_shown = false;   /* outside: it closes */
        return true;   /* between its buttons, or the click that closed it: nothing more */
    }
    app->navbox_near_at = now;
    if (now - app->navbox_opened_at < RUBRAVIEW_NAVBOX_GRACE) return true;   /* a double click's second: not a button */
    if (part == RUBRAVIEW_NAVBOX_BAR) {
        rubraview_rect_t bar = rubraview_navbox_part_rect(&app->navbox, RUBRAVIEW_NAVBOX_BAR);
        if (app->navbox.media) {   /* RV-285: a film's or a song's time */
            if (play_duration(app) > 0.0) play_seek(app, rubraview_seekbar_time(bar.x, bar.width, x, play_duration(app)));
        } else if (page_count(app) > 1) {
            pagebar_press(app, bar.x, bar.width, x, false);
        }
        note_activity(app);
        return true;
    }
    if (part == RUBRAVIEW_NAVBOX_CLOSE) {   /* owner, 2026-11-14: "X는 상자만 닫는 것으로" */
        app->navbox_shown = false;
        return true;
    }
    handle_action(app, cstr(rubraview_navbox_action(part, app->navbox.media)));
    note_activity(app);
    return true;
}

/* RV-285: the middle's see-through hint, a place to click. */
static void draw_navbox_hint(app_state_t *app) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    rubraview_rect_t m = rubraview_navbox_middle((double)w, (double)h);
    rubraview_pal_rect_t box = { m.x, m.y, m.width, m.height };
    rubraview_pal_render_fill_rect(app->renderer, box, 0x1CFFFFFFu, 12.0 * dpi);
    rubraview_pal_render_stroke_rect(app->renderer, box, 0x60FFFFFFu, 1.5 * dpi, 12.0 * dpi);
}

void draw_navbox(app_state_t *app) {
    if (app->navbox_hint) draw_navbox_hint(app);
    if (!app->navbox_shown) return;
    rubraview_renderer_t *r = app->renderer;
    const rubraview_navbox_t *nav = &app->navbox;
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    rubraview_pal_rect_t box = { nav->box.x, nav->box.y, nav->box.width, nav->box.height };
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ box.x + 2.0 * dpi, box.y + 4.0 * dpi, box.width, box.height }, 0x50000000u, 8.0 * dpi);
    rubraview_pal_render_fill_rect(r, box, COLOR_BOX_FILL, 8.0 * dpi);
    rubraview_pal_render_stroke_rect(r, box, COLOR_BOX_BORDER, 1.0, 8.0 * dpi);
    rubraview_navbox_part_t over = rubraview_navbox_hit(nav, app->pointer_x, app->pointer_y);

    /* the bar: where the page is among them and its number; over a film or music, its time (RV-285) */
    bool media = nav->media;
    rubraview_rect_t b = rubraview_navbox_part_rect(nav, RUBRAVIEW_NAVBOX_BAR);
    rubraview_pal_rect_t track = { b.x, b.y + b.height * 0.5 - 3.0 * dpi, b.width, 6.0 * dpi };
    rubraview_pal_render_fill_rect(r, track, 0x40FFFFFFu, 3.0 * dpi);
    int32_t at = current_page_index(app);
    double length = media ? play_duration(app) : 0.0, now_at = media ? play_position(app) : 0.0;
    double f = media ? (length > 0.0 ? now_at / length : 0.0)
                     : (at >= 0 ? rubraview_pagebar_fraction((size_t)at, page_count(app)) : 0.0);
    f = f < 0.0 ? 0.0 : f > 1.0 ? 1.0 : f;
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ track.x, track.y, track.width * f, track.height }, COLOR_TILE_CURRENT, 3.0 * dpi);
    double knob = 12.0 * dpi;
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ track.x + track.width * f - knob * 0.5, b.y + b.height * 0.5 - knob * 0.5, knob, knob },
                                   over == RUBRAVIEW_NAVBOX_BAR ? 0xFFFFFFFFu : 0xFFD8D8D8u, knob * 0.5);
    char place[64];
    int n = 0;
    if (media) {
        char t1[24], t2[24];
        proven_u8str_view_t a = rubraview_format_timecode(rubraview_mem_mut(t1, sizeof t1), now_at, false);
        proven_u8str_view_t z = rubraview_format_timecode(rubraview_mem_mut(t2, sizeof t2), length, false);
        n = rubraview_fmt(place, sizeof place, "{} / {}", PROVEN_ARG(a), PROVEN_ARG(z));
    } else {
        n = rubraview_fmt(place, sizeof place, "{} / {}", PROVEN_ARG((size_t)(at >= 0 ? at + 1 : 0)), PROVEN_ARG(page_count(app)));
    }
    if (n > 0) rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ (const proven_byte_t *)place, (size_t)n },
                                              (rubraview_pal_rect_t){ b.x, b.y - 2.0 * dpi, b.width, b.height * 0.5 }, 10.0 * dpi, 0xC0F0F0F0u, RUBRAVIEW_TEXT_RIGHT);
    if (media) {   /* play / pause, left of the bar */
        rubraview_rect_t pr = rubraview_navbox_part_rect(nav, RUBRAVIEW_NAVBOX_PLAY);
        rubraview_pal_rect_t pb = { pr.x, pr.y, pr.width, pr.height };
        rubraview_pal_render_fill_rect(r, pb, over == RUBRAVIEW_NAVBOX_PLAY ? 0x50FFFFFFu : COLOR_TILE_FILL, 4.0 * dpi);
        if (!rubraview_pal_render_draw_icon(r, app->media_paused ? 0xE768 : 0xE769, pb, pb.height * 0.5, COLOR_TEXT))
            rubraview_pal_render_draw_text(r, app->media_paused ? PROVEN_LIT(">") : PROVEN_LIT("||"), pb, pb.height * 0.55, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }

    /* the X */
    rubraview_rect_t xr = rubraview_navbox_part_rect(nav, RUBRAVIEW_NAVBOX_CLOSE);
    rubraview_pal_rect_t xb = { xr.x, xr.y, xr.width, xr.height };
    rubraview_pal_render_fill_rect(r, xb, over == RUBRAVIEW_NAVBOX_CLOSE ? COLOR_CLOSE_HOVER : COLOR_TILE_FILL, 4.0 * dpi);
    if (!rubraview_pal_render_draw_icon(r, 0xE8BB, xb, xb.height * 0.45, COLOR_TEXT))
        rubraview_pal_render_draw_text(r, PROVEN_LIT("X"), xb, xb.height * 0.6, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);

    /* the four pairs: left the one before, right the one after (whatever the reading direction) */
    static const uint32_t ICON[2][8] = { { 0xE76B, 0xE76C, 0xE72B, 0xE72A, 0xEB9E, 0xEB9D, 0xE892, 0xE893 },
                                         { 0, 0, 0, 0, 0xEB9E, 0xEB9D, 0xE892, 0xE893 } };   /* RV-285: 5 s and 5 % as signs */
    static const char *const SIGN[2][8] = { { "<", ">", "<-", "->", "|<", ">|", "[<", ">]" },
                                            { "-5s", "+5s", "-5%", "+5%", "<<", ">>", "|<", ">|" } };
    for (int32_t i = 0; i < 8; ++i) {
        rubraview_navbox_part_t part = (rubraview_navbox_part_t)(RUBRAVIEW_NAVBOX_PAGE_PREV + i);
        rubraview_rect_t pr = rubraview_navbox_part_rect(nav, part);
        rubraview_pal_rect_t pb = { pr.x, pr.y, pr.width, pr.height };
        rubraview_pal_render_fill_rect(r, pb, over == part ? 0x50FFFFFFu : COLOR_TILE_FILL, 4.0 * dpi);
        rubraview_pal_render_stroke_rect(r, pb, COLOR_BOX_BORDER, 1.0, 4.0 * dpi);
        if (!ICON[media][i] || !rubraview_pal_render_draw_icon(r, ICON[media][i], pb, pb.height * 0.42, COLOR_TEXT))
            rubraview_pal_render_draw_text(r, cstr(SIGN[media][i]), pb, pb.height * (media ? 0.34 : 0.4), COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        if (i % 2 == 0) {
            rubraview_rect_t next = rubraview_navbox_part_rect(nav, (rubraview_navbox_part_t)(part + 1));
            rubraview_pal_rect_t words = { pr.x - 6.0 * dpi, pr.y + pr.height + 2.0 * dpi, next.x + next.width - pr.x + 12.0 * dpi, nav->label_h };
            rubraview_pal_render_draw_text(r, cstr(rubraview_tr(rubraview_navbox_pair_caption(i / 2, media))), words, 11.0 * dpi,
                                           0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
        }
    }
}
