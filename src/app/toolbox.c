/*
 * The detached toolbox (its grip, size, window, docking) and the seek buttons' choices
 * (RFC-0002 Q6; RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* ---- RFC-0002 Q6: the detached toolbox ---- */

/* The grip beside the pin (owner, 2026-09-29): the three-dot corner of a
   window's sizing grip, and while it is dragged, what the size is now. */
void draw_box_grip(app_state_t *app, const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics,
                          uint32_t fill, uint32_t border) {
    rubraview_rect_t gr = rubraview_box_grip_rect(box, metrics);
    if (gr.width <= 0.0) return;
    rubraview_pal_rect_t grip = { gr.x, gr.y, gr.width, gr.height };
    bool hot = box->grip_active || rubraview_rect_contains(gr, app->pointer_x, app->pointer_y);
    rubraview_pal_render_fill_rect(app->renderer, grip, hot ? COLOR_TILE_FILL : fill, 2.0);
    rubraview_pal_render_stroke_rect(app->renderer, grip, border, 1.0, 2.0);
    draw_sign(app->renderer, RUBRAVIEW_ICON_RESIZE, "R", grip, gr.height * 0.42, COLOR_TEXT);   /* 1.25.0: R, resize; RV-181 its icon */
    if (!box->grip_active) return;
    rubraview_tile_metrics_t sized = rubraview_box_metrics(box, metrics);
    char line[64];
    int n = snprintf(line, sizeof(line), T("%d a row, %d%%"),
                     box->kind == RUBRAVIEW_BOX_TOOLBOX ? sized.strip_columns : sized.columns,
                     (int)lround((box->scale > 0.0 ? box->scale : 1.0) * 100.0));
    if (n <= 0) return;
    double w = gr.width * 3.2;
    /* On the side away from the box, so it covers none of it. */
    rubraview_rect_t body = rubraview_box_bounds(box, metrics);
    bool body_above = body.y + body.height * 0.5 < gr.y;
    rubraview_pal_rect_t tip = { gr.x + gr.width * 0.5 - w * 0.5, body_above ? gr.y + gr.height * 1.1 : gr.y - gr.height * 0.6,
                                 w, gr.height * 0.5 };
    if (tip.x < 0.0) tip.x = 0.0;
    rubraview_pal_render_fill_rect(app->renderer, tip, COLOR_BOX_FILL, 3.0);
    rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ (const proven_byte_t *)line, (size_t)n }, tip, gr.height * 0.28, COLOR_TEXT,
                                   RUBRAVIEW_TEXT_CENTER);
}

/* Its size: the anchor bar on top, the profile's grid under it. */
void draw_strip_head(app_state_t *app, rubraview_renderer_t *r, const rubraview_toolbox_layout_t *l,
                            double ox, double oy, int32_t hovered, double opacity);
void draw_strip_button(app_state_t *app, rubraview_renderer_t *r, rubraview_pal_rect_t tile, int32_t i,
                              const rubraview_action_facts_t *facts, bool hovered, uint32_t fill, uint32_t border);

/* Detached, the strip sits under the window's own anchor bar. */
static rubraview_toolbox_layout_t toolbox_window_layout(const app_state_t *app, const rubraview_tile_metrics_t *m) {
    rubraview_tile_metrics_t sized = rubraview_box_metrics(&app->toolbox, m);   /* the size its grip set */
    rubraview_toolbox_shape_t shape = rubraview_box_toolbox_shape(&app->toolbox);
    shape.count = app->toolbox_tile_count;
    shape.timeline = app->media != NULL;
    return rubraview_toolbox_layout_shaped(&sized, &shape);
}

static void toolbox_window_size(const app_state_t *app, const rubraview_tile_metrics_t *m, int32_t *out_w, int32_t *out_h) {
    rubraview_toolbox_layout_t l = toolbox_window_layout(app, m);
    *out_w = (int32_t)ceil(l.width > m->anchor_size * 2.0 ? l.width : m->anchor_size * 2.0);
    *out_h = (int32_t)ceil(m->anchor_size + l.height);
}

static rubraview_rect_t toolbox_window_tile(const app_state_t *app, const rubraview_tile_metrics_t *m, int32_t i) {
    rubraview_toolbox_layout_t l = toolbox_window_layout(app, m);
    rubraview_tile_metrics_t sized = rubraview_box_metrics(&app->toolbox, m);
    rubraview_rect_t b = rubraview_toolbox_button_rect(&l, &sized, i);
    b.y += m->anchor_size;
    return b;
}

void toolbox_detach(app_state_t *app, int32_t screen_x, int32_t screen_y, bool follow_pointer) {
    if (app->toolbox_window) return;
    toolbox_refresh(app);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    rubraview_tile_metrics_t m = tile_metrics(dpi);
    int32_t w = 0, h = 0;
    toolbox_window_size(app, &m, &w, &h);
    rubraview_window_config_t config = {
        .title = T("Rubraview Toolbar"), .width = (int32_t)(w / dpi), .height = (int32_t)(h / dpi),
        .owner = app->window, .tool_window = true,
    };
    app->toolbox_window = rubraview_pal_window_create(window_block_take(app, &app->toolbox_block), &config);
    if (!app->toolbox_window) {
        rubraview_box_dock(&app->toolbox);
        osd_say(app, TU8("the Toolbar could not open a window of its own"));
        return;
    }
    rubraview_pal_window_set_frame(app->toolbox_window, screen_x, screen_y, w, h);
    int32_t cw = 0, ch = 0;
    rubraview_pal_window_get_size(app->toolbox_window, &cw, &ch);
    app->toolbox_renderer = rubraview_pal_render_create(window_block_arena(app, &app->toolbox_block), rubraview_pal_window_native_handle(app->toolbox_window), cw, ch);
    ui_font_apply_all(app);
    app->toolbox_window_opacity = -1.0;
    app->toolbox_window_drawn = 0.0;
    app->toolbox_window_pointer_x = app->toolbox_window_pointer_y = -1.0;
    app->toolbox.state = RUBRAVIEW_BOX_DETACHED;
    if (follow_pointer) rubraview_pal_window_begin_drag(app->toolbox_window);
}

/* Back into the viewer's window, its anchor at a client point, open. */
void toolbox_dock(app_state_t *app, double client_x, double client_y) {
    if (app->toolbox_renderer) rubraview_pal_render_destroy(app->toolbox_renderer);
    if (app->toolbox_window) rubraview_pal_window_destroy(app->toolbox_window);
    window_block_give(app, &app->toolbox_block);
    app->toolbox_renderer = NULL;
    app->toolbox_window = NULL;
    rubraview_box_dock(&app->toolbox);
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double max_x = (double)w - 88.0 * dpi, max_y = (double)h - 48.0 * dpi;
    app->toolbox.anchor_x = client_x < 0.0 ? 0.0 : client_x > max_x ? max_x : client_x;
    app->toolbox.anchor_y = client_y < 0.0 ? 0.0 : client_y > max_y ? max_y : client_y;
    note_activity(app);
}

/* ---- the seek buttons' choices (owner, 2026-10-09) ---- */

bool seek_tile_action(proven_u8str_view_t action, bool *out_forward) {
    if (proven_u8str_view_eq(action, PROVEN_LIT("media_seek_forward"))) { *out_forward = true; return true; }
    if (proven_u8str_view_eq(action, PROVEN_LIT("media_seek_back"))) { *out_forward = false; return true; }
    return false;
}

/* The window the pressed button is in: the choices stay inside it. */
static void seekfan_bounds(const app_state_t *app, double *out_w, double *out_h) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->seekfan_detached && app->toolbox_window ? app->toolbox_window : app->window, &w, &h);
    *out_w = (double)w;
    *out_h = (double)h;
}

void seekfan_pointer(app_state_t *app, double x, double y) {
    if (!app->seekfan.open) return;
    double bw = 0.0, bh = 0.0;
    seekfan_bounds(app, &bw, &bh);
    rubraview_seekfan_pointer(&app->seekfan, x, y, 4.0 * rubraview_pal_window_dpi_scale(app->window), bw, bh);
}

void seekfan_let_go(app_state_t *app) {
    if (!app->seekfan.down) return;
    double seconds = rubraview_seekfan_release(&app->seekfan);
    app->toolbox_window_drawn = 0.0;
    if (seconds != 0.0 && play_duration(app) > 0.0) play_seek(app, play_position(app) + seconds);
}

void draw_seekfan(app_state_t *app, rubraview_renderer_t *r, bool detached) {
    if (!app->seekfan.open || app->seekfan_detached != detached) return;
    double bw = 0.0, bh = 0.0;
    seekfan_bounds(app, &bw, &bh);
    double gap = 4.0 * rubraview_pal_window_dpi_scale(app->window);
    for (int32_t i = 0; i < RUBRAVIEW_SEEKFAN_COUNT; ++i) {
        rubraview_rect_t b = rubraview_seekfan_button(&app->seekfan, i, gap, bw, bh);
        rubraview_pal_rect_t rect = { b.x, b.y, b.width, b.height };
        bool on = i == app->seekfan.hover;
        rubraview_pal_render_fill_rect(r, rect, on ? COLOR_TILE_CURRENT : 0xF0202020u, 3.0);
        rubraview_pal_render_stroke_rect(r, rect, on ? COLOR_TEXT : COLOR_BOX_BORDER, 1.0, 3.0);
        char caption[8];
        int n = snprintf(caption, sizeof(caption), "%s%s", app->seekfan.forward ? "+" : "-", rubraview_seekfan_label(i));
        if (n > 0) rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)caption, .size = (size_t)n }, rect,
                                                  rect.height * 0.34, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
}

void draw_toolbox_window(app_state_t *app) {
    if (!app->toolbox_window || !app->toolbox_renderer) return;
    double now = rubraview_pal_time_now_seconds();
    if (now - app->toolbox_window_drawn < 0.1) return;
    app->toolbox_window_drawn = now;
    toolbox_refresh(app);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    rubraview_tile_metrics_t m = tile_metrics(dpi);
    int32_t want_w = 0, want_h = 0, cw = 0, ch = 0;
    toolbox_window_size(app, &m, &want_w, &want_h);
    rubraview_pal_window_get_size(app->toolbox_window, &cw, &ch);
    if (cw != want_w || ch != want_h) {
        /* Another profile, another grid: the window follows it. */
        int32_t fx = 0, fy = 0, fw = 0, fh = 0;
        rubraview_pal_window_get_frame(app->toolbox_window, &fx, &fy, &fw, &fh);
        rubraview_pal_window_set_frame(app->toolbox_window, fx, fy, want_w, want_h);
    }
    double opacity = rubraview_settings_get(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("toolbox_opacity"));
    if (opacity != app->toolbox_window_opacity) {
        rubraview_pal_window_set_opacity(app->toolbox_window, opacity);
        app->toolbox_window_opacity = opacity;
    }
    rubraview_renderer_t *r = app->toolbox_renderer;
    rubraview_pal_render_begin(r, 0xFF1A1A1Au);
    rubraview_pal_rect_t left = { 0.0, 0.0, m.anchor_size, m.anchor_size };
    rubraview_pal_rect_t right = { m.anchor_size, 0.0, m.anchor_size, m.anchor_size };
    rubraview_pal_render_stroke_rect(r, left, COLOR_BOX_BORDER, 1.0, 2.0);
    rubraview_pal_render_fill_rect(r, right, COLOR_TILE_FILL, 2.0);
    draw_sign(r, RUBRAVIEW_ICON_MOVE, "T", left, m.anchor_size * 0.42, COLOR_TEXT);   /* RV-181 */
    rubraview_pal_render_draw_text(r, TU8("Dock"), right, m.anchor_size * 0.3, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    rubraview_toolbox_layout_t l = toolbox_window_layout(app, &m);
    rubraview_action_facts_t facts = action_facts(app);
    int32_t hovered = -1;
    for (int32_t i = 0; i < app->toolbox_tile_count; ++i) {
        if (rubraview_rect_contains(toolbox_window_tile(app, &m, i), app->toolbox_window_pointer_x, app->toolbox_window_pointer_y)) hovered = i;
    }
    draw_strip_head(app, r, &l, 0.0, m.anchor_size, hovered, 100.0);
    for (int32_t i = 0; i < app->toolbox_tile_count; ++i) {
        rubraview_rect_t t = toolbox_window_tile(app, &m, i);
        draw_strip_button(app, r, (rubraview_pal_rect_t){ t.x, t.y, t.width, t.height }, i, &facts, i == hovered,
                          COLOR_TILE_FILL, COLOR_BOX_BORDER);
    }
    draw_seekfan(app, r, true);
    if (!rubraview_pal_render_end(r)) app->toolbox_window_drawn = 0.0;
}

/* Where the toolbox window is over the viewer's client area, when its middle is. */
static bool toolbox_window_over_viewer(const app_state_t *app, double *out_x, double *out_y) {
    int32_t tx = 0, ty = 0, tw = 0, th = 0, vx = 0, vy = 0, vw = 0, vh = 0;
    if (!rubraview_pal_window_get_frame(app->toolbox_window, &tx, &ty, &tw, &th) ||
        !rubraview_pal_window_get_frame(app->window, &vx, &vy, &vw, &vh)) return false;
    double mx = tx + tw * 0.5, my = ty + th * 0.5;
    if (mx < vx || my < vy || mx >= vx + vw || my >= vy + vh) return false;
    *out_x = (double)(tx - vx);
    *out_y = (double)(ty - vy);
    return true;
}

void toolbox_window_pump(app_state_t *app) {
    rubraview_window_event_t event;
    while (app->toolbox_window && rubraview_pal_window_poll_event(app->toolbox_window, &event)) {
        double dpi = rubraview_pal_window_dpi_scale(app->window);
        rubraview_tile_metrics_t m = tile_metrics(dpi);
        switch (event.kind) {
            case RUBRAVIEW_WINDOW_EVENT_CLOSE: {
                int32_t w = 0, h = 0;
                rubraview_pal_window_get_size(app->window, &w, &h);
                toolbox_dock(app, (double)w - 220.0 * dpi, (double)h - 160.0 * dpi);
                return;
            }
            case RUBRAVIEW_WINDOW_EVENT_RESIZE:
                rubraview_pal_render_resize(app->toolbox_renderer, event.resize.width, event.resize.height);
                app->toolbox_window_drawn = 0.0;
                break;
            case RUBRAVIEW_WINDOW_EVENT_PAINT:
                app->toolbox_window_drawn = 0.0;
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOVED:
                /* §3.6.1 would dock it when dropped over the canvas. A viewer
                   filling the screen is under it wherever it goes, so that
                   would dock it at every move: it docks by its Dock half,
                   Ctrl+T or Show › Detach toolbox instead (D-15). */
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE:
                app->toolbox_window_pointer_x = event.mouse.x;
                app->toolbox_window_pointer_y = event.mouse.y;
                app->toolbox_window_drawn = 0.0;
                if (app->seekfan_detached) seekfan_pointer(app, event.mouse.x, event.mouse.y);
                speed_tile_move(app, event.mouse.y, true);
                if (app->pagebar_dragging && app->pagebar_detached) pagebar_drag(app, event.mouse.x, false);
                break;
            case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN:
                dispatch_key(app, event.key.combo);   /* the viewer's keys work from here too */
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN: {
                if (event.mouse.button != RUBRAVIEW_MOUSE_LEFT) break;
                if (event.mouse.y < m.anchor_size) {
                    if (event.mouse.x < m.anchor_size) {
                        rubraview_pal_window_begin_drag(app->toolbox_window);
                    } else if (event.mouse.x < m.anchor_size * 2.0) {
                        int32_t w = 0, h = 0;
                        rubraview_pal_window_get_size(app->window, &w, &h);
                        double cx = (double)w - 220.0 * dpi, cy = (double)h - 160.0 * dpi;
                        (void)toolbox_window_over_viewer(app, &cx, &cy);
                        toolbox_dock(app, cx, cy);
                        return;
                    }
                    break;
                }
                rubraview_toolbox_layout_t l = toolbox_window_layout(app, &m);
                if (play_duration(app) > 0.0 && l.timeline.width > 0.0 &&
                    event.mouse.x >= l.timeline.x && event.mouse.x < l.timeline.x + l.timeline.width &&
                    event.mouse.y >= m.anchor_size + l.timeline.y - l.timeline.height &&
                    event.mouse.y < m.anchor_size + l.timeline.y + l.timeline.height * 2.0) {
                    double seconds = rubraview_seekbar_time(l.timeline.x, l.timeline.width, event.mouse.x, play_duration(app));
                    play_seek(app, seconds);
                    app->toolbox_window_drawn = 0.0;
                    break;
                }
                if (pagebar_shown(app) && l.timeline.width > 0.0 &&
                    event.mouse.x >= l.timeline.x && event.mouse.x < l.timeline.x + l.timeline.width &&
                    event.mouse.y >= m.anchor_size + l.timeline.y - l.timeline.height &&
                    event.mouse.y < m.anchor_size + l.timeline.y + l.timeline.height * 2.0) {
                    pagebar_press(app, l.timeline.x, l.timeline.width, event.mouse.x, true);
                    app->toolbox_window_drawn = 0.0;
                    break;
                }
                for (int32_t i = 0; i < app->toolbox_tile_count; ++i) {
                    if (!rubraview_rect_contains(toolbox_window_tile(app, &m, i), event.mouse.x, event.mouse.y)) continue;
                    bool enabled = true;
                    (void)toolbox_caption(app, &app->toolbox_tiles[i], &enabled);
                    if (enabled && proven_u8str_view_eq(app->toolbox_tiles[i].action, PROVEN_LIT("toggle_subtitles"))) {
                        /* D-33: decided on release, a double tap or a hold */
                        sub_tile_result(app, rubraview_tap_press(&app->sub_tap, rubraview_pal_time_now_seconds()));
                    } else if (enabled && seek_tile_action(app->toolbox_tiles[i].action, &(bool){ false })) {
                        /* A tap seeks on release; held, it offers longer steps. */
                        bool forward = false;
                        (void)seek_tile_action(app->toolbox_tiles[i].action, &forward);
                        app->seekfan_detached = true;
                        rubraview_seekfan_press(&app->seekfan, forward, rubraview_pal_time_now_seconds(),
                                                toolbox_window_tile(app, &m, i));
                    } else if (enabled && (proven_u8str_view_eq(app->toolbox_tiles[i].action, PROVEN_LIT("media_speed_cycle")) ||
                                           proven_u8str_view_eq(app->toolbox_tiles[i].action, PROVEN_LIT("interval_cycle")))) {
                        speed_tile_press(app, event.mouse.y, true, proven_u8str_view_eq(app->toolbox_tiles[i].action, PROVEN_LIT("interval_cycle")));
                    } else if (enabled && chooser_open_tile(app, app->toolbox_tiles[i].action, chooser_main_x(app), chooser_main_y(app),
                                                            chooser_main_y(app))) {
                        /* 1.24.0: a one-of-many tile lists its choices, over the picture */
                    } else if (enabled) {
                        handle_action(app, app->toolbox_tiles[i].action);
                    }
                    app->toolbox_window_drawn = 0.0;
                    break;
                }
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_UP:
                if (app->pagebar_dragging && app->pagebar_detached) {
                    pagebar_drag(app, event.mouse.x, true);
                    app->toolbox_window_drawn = 0.0;
                    break;
                }
                sub_tile_result(app, rubraview_tap_release(&app->sub_tap, rubraview_pal_time_now_seconds()));
                seekfan_let_go(app);
                speed_tile_release(app, true);
                break;
            default:
                break;
        }
    }
}
