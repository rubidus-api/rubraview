/*
 * RV-277: the navigator, a box at the pointer in the middle of the window over a picture or a book (ui_navbox.h
 * holds its geometry and its rules): when it shows, how it is drawn, what a press on it does.
 */
#include "app.h"
#include "rubraview/ui_navbox.h"
#include "rubraview/playback.h"

static bool navbox_allowed(app_state_t *app) {
    if (rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("navigator")) < 0.5) return false;
    if (app->navbox_shown && app->pagebar_dragging && !app->pagebar_detached) return true;   /* its bar being dragged */
    return app->pointer_inside && !app->cursor.hidden && page_count(app) > 0 && !app->picker_open &&
           !app->settings_open && !app->panel.open && !app->titlebar.shown && !app->media && !app->text &&
           !app->view_pressed && !app->view_dragging && !app->pagebar_dragging && !app->ctx_open && !app->tmenu_open &&
           !window_taking_input(app) && app->zoom <= 1.001 && !strip_active(app);
}

static bool over_a_window(app_state_t *app, double x, double y) {
    if (app->listwin.open && rubraview_rect_contains(rubraview_listwin_rect(&app->listwin), x, y)) return true;
    if (app->eqwin.open && rubraview_rect_contains(rubraview_eqwin_rect(&app->eqwin), x, y)) return true;
    return false;
}

/* Once a pass (tick_timers): where the pointer is, and since when. */
void navbox_tick(app_state_t *app) {
    double now = rubraview_pal_time_now_seconds();
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    double px = app->pointer_x, py = app->pointer_y;
    bool allowed = navbox_allowed(app);
    bool in_middle = allowed && rubraview_rect_contains(rubraview_navbox_middle((double)w, (double)h), px, py) && !over_a_window(app, px, py);
    bool in_box = app->navbox_shown && rubraview_rect_contains(app->navbox.box, px, py);
    if (in_middle) { if (app->navbox_rest_since <= 0.0) app->navbox_rest_since = now; }
    else app->navbox_rest_since = 0.0;
    if (in_middle || in_box) app->navbox_near_at = now;
    double rested = app->navbox_rest_since > 0.0 ? now - app->navbox_rest_since : 0.0;
    bool keep = rubraview_navbox_keep(app->navbox_shown, allowed, in_middle, in_box, rested, now - app->navbox_near_at);
    if (keep && !app->navbox_shown)
        app->navbox = rubraview_navbox_place(px, py, (double)w, (double)h, rubraview_pal_window_dpi_scale(app->window));
    app->navbox_shown = keep;
}

/* A press: true when the box took it. A press elsewhere puts it away and goes on to what is under it. */
bool navbox_press(app_state_t *app, double x, double y) {
    if (!app->navbox_shown) return false;
    rubraview_navbox_part_t part = rubraview_navbox_hit(&app->navbox, x, y);
    if (part == RUBRAVIEW_NAVBOX_NONE) {
        if (rubraview_rect_contains(app->navbox.box, x, y)) return true;   /* between its buttons: nothing */
        app->navbox_shown = false;
        app->navbox_rest_since = 0.0;
        return false;
    }
    app->navbox_near_at = rubraview_pal_time_now_seconds();
    if (part == RUBRAVIEW_NAVBOX_BAR) {
        rubraview_rect_t bar = rubraview_navbox_part_rect(&app->navbox, RUBRAVIEW_NAVBOX_BAR);
        if (page_count(app) > 1) pagebar_press(app, bar.x, bar.width, x, false);
        return true;
    }
    handle_action(app, cstr(rubraview_navbox_action(part)));
    note_activity(app);
    return true;
}

void draw_navbox(app_state_t *app) {
    if (!app->navbox_shown) return;
    rubraview_renderer_t *r = app->renderer;
    const rubraview_navbox_t *nav = &app->navbox;
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    rubraview_pal_rect_t box = { nav->box.x, nav->box.y, nav->box.width, nav->box.height };
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ box.x + 2.0 * dpi, box.y + 4.0 * dpi, box.width, box.height }, 0x50000000u, 8.0 * dpi);
    rubraview_pal_render_fill_rect(r, box, COLOR_BOX_FILL, 8.0 * dpi);
    rubraview_pal_render_stroke_rect(r, box, COLOR_BOX_BORDER, 1.0, 8.0 * dpi);
    rubraview_navbox_part_t over = rubraview_navbox_hit(nav, app->pointer_x, app->pointer_y);

    /* the bar: where the page is among them, and its number */
    rubraview_rect_t b = rubraview_navbox_part_rect(nav, RUBRAVIEW_NAVBOX_BAR);
    rubraview_pal_rect_t track = { b.x, b.y + b.height * 0.5 - 3.0 * dpi, b.width, 6.0 * dpi };
    rubraview_pal_render_fill_rect(r, track, 0x40FFFFFFu, 3.0 * dpi);
    int32_t at = current_page_index(app);
    double f = at >= 0 ? rubraview_pagebar_fraction((size_t)at, page_count(app)) : 0.0;
    f = f < 0.0 ? 0.0 : f > 1.0 ? 1.0 : f;
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ track.x, track.y, track.width * f, track.height }, COLOR_TILE_CURRENT, 3.0 * dpi);
    double knob = 12.0 * dpi;
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ track.x + track.width * f - knob * 0.5, b.y + b.height * 0.5 - knob * 0.5, knob, knob },
                                   over == RUBRAVIEW_NAVBOX_BAR ? 0xFFFFFFFFu : 0xFFD8D8D8u, knob * 0.5);
    char place[48];
    int n = rubraview_fmt(place, sizeof place, "{} / {}", PROVEN_ARG((size_t)(at >= 0 ? at + 1 : 0)), PROVEN_ARG(page_count(app)));
    if (n > 0) rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ (const proven_byte_t *)place, (size_t)n },
                                              (rubraview_pal_rect_t){ b.x, b.y - 2.0 * dpi, b.width, b.height * 0.5 }, 10.0 * dpi, 0xC0F0F0F0u, RUBRAVIEW_TEXT_RIGHT);

    /* the X */
    rubraview_rect_t xr = rubraview_navbox_part_rect(nav, RUBRAVIEW_NAVBOX_CLOSE);
    rubraview_pal_rect_t xb = { xr.x, xr.y, xr.width, xr.height };
    rubraview_pal_render_fill_rect(r, xb, over == RUBRAVIEW_NAVBOX_CLOSE ? COLOR_CLOSE_HOVER : COLOR_TILE_FILL, 4.0 * dpi);
    if (!rubraview_pal_render_draw_icon(r, 0xE8BB, xb, xb.height * 0.45, COLOR_TEXT))
        rubraview_pal_render_draw_text(r, PROVEN_LIT("X"), xb, xb.height * 0.6, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);

    /* the four pairs: left the one before, right the one after (whatever the reading direction) */
    static const uint32_t ICON[8] = { 0xE76B, 0xE76C, 0xE72B, 0xE72A, 0xEB9E, 0xEB9D, 0xE892, 0xE893 };
    static const char *const SIGN[8] = { "<", ">", "<-", "->", "|<", ">|", "[<", ">]" };
    for (int32_t i = 0; i < 8; ++i) {
        rubraview_navbox_part_t part = (rubraview_navbox_part_t)(RUBRAVIEW_NAVBOX_PAGE_PREV + i);
        rubraview_rect_t pr = rubraview_navbox_part_rect(nav, part);
        rubraview_pal_rect_t pb = { pr.x, pr.y, pr.width, pr.height };
        rubraview_pal_render_fill_rect(r, pb, over == part ? 0x50FFFFFFu : COLOR_TILE_FILL, 4.0 * dpi);
        rubraview_pal_render_stroke_rect(r, pb, COLOR_BOX_BORDER, 1.0, 4.0 * dpi);
        if (!rubraview_pal_render_draw_icon(r, ICON[i], pb, pb.height * 0.42, COLOR_TEXT))
            rubraview_pal_render_draw_text(r, cstr(SIGN[i]), pb, pb.height * 0.4, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        if (i % 2 == 0) {
            rubraview_rect_t next = rubraview_navbox_part_rect(nav, (rubraview_navbox_part_t)(part + 1));
            rubraview_pal_rect_t words = { pr.x - 6.0 * dpi, pr.y + pr.height + 2.0 * dpi, next.x + next.width - pr.x + 12.0 * dpi, nav->label_h };
            rubraview_pal_render_draw_text(r, cstr(rubraview_tr(rubraview_navbox_pair_caption(i / 2))), words, 11.0 * dpi,
                                           0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
        }
    }
}
