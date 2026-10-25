/*
 * Rendering: the tiles and boxes, the page bar and strip, the picker drawn, subtitles drawn, the logo, a frame
 * and what changed since the last (RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* ---- rendering ---- */

/* The viewer the menu's toggle captions read their state from. */
const app_state_t *g_caption_app;

/* Tile captions for the menu box come from the current menu level, with
   the Back tile at index 0 below the root (§3.6.2). A submenu ends in
   ">", a toggle says whether it is on, the choice in use is marked, and
   what cannot act on the page on screen is dimmed. */
proven_u8str_view_t menu_tile_caption(const rubraview_menu_state_t *menu, int32_t tile, char *scratch, size_t scratch_size,
                                 bool *out_enabled, bool *out_current) {
    if (out_enabled) *out_enabled = true;
    if (out_current) *out_current = false;
    if (rubraview_menu_has_back_tile(menu) && tile == 0) return TU8("< Back");
    const rubraview_menu_item_t *item = rubraview_menu_item_at(menu, tile);
    if (!item) return PROVEN_LIT("");
    bool submenu = item->child_count > 0;
    rubraview_action_state_t st = { .enabled = true, .mark = RUBRAVIEW_MARK_NONE };
    if (!submenu && g_caption_app) {
        rubraview_action_facts_t facts = action_facts(g_caption_app);
        st = rubraview_action_state(item->action, &facts);
    }
    if (out_enabled) *out_enabled = st.enabled;
    if (out_current) *out_current = st.mark == RUBRAVIEW_MARK_CURRENT;
    if (g_caption_app && rubraview_confirm_armed(&g_caption_app->menu_confirm, item - menu->tree->items,
                                                 rubraview_pal_time_now_seconds())) {
        return proven_u8str_view_eq(item->action, PROVEN_LIT("recent_clear")) ? TU8("Clear?") : TU8("Delete?");
    }
    return rubraview_tile_caption(item->label, submenu, st.mark, st.value, scratch, scratch_size);
}

static proven_u8str_view_t box_opacity_key(const rubraview_box_t *box) {
    return box->kind == RUBRAVIEW_BOX_MENU ? PROVEN_LIT("menubox_opacity") : PROVEN_LIT("toolbox_opacity");
}

double box_opacity(const app_state_t *app, const rubraview_box_t *box) {
    /* Spelled out, not through box_opacity_key: check-settings.py finds reads by their literal keys. */
    return box->kind == RUBRAVIEW_BOX_MENU ? rubraview_settings_get(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("menubox_opacity"))
                                           : rubraview_settings_get(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("toolbox_opacity"));
}

/* D-15: Alt + wheel over a box. Returns true when the wheel was spent on it. */
/* 1.25.0 (owner, 2026-10-20; by the anchors' old names): the wheel over F with Alt makes the Floating Widget more
   or less see-through; over T its buttons bigger or smaller. RV-171 (owner, 2026-10-25): the plain wheel over F
   goes through the file list as the wheel over the picture does, the strip below shown as Settings say; the
   buttons a row, which it set before, are R's (the grip). */
static bool box_anchor_wheel(app_state_t *app, double x, double y, double notches, uint32_t modifiers) {
    if (app->toolbox.state == RUBRAVIEW_BOX_DETACHED || notches == 0.0) return false;
    rubraview_tile_metrics_t metrics = tile_metrics(rubraview_pal_window_dpi_scale(app->window));
    rubraview_box_t *b = &app->toolbox;
    char line[64];
    int n = 0;
    if (rubraview_rect_contains(rubraview_box_grip_rect(b, &metrics), x, y)) {   /* RV-171: R, the buttons a row */
        rubraview_tile_metrics_t sized = rubraview_box_metrics(b, &metrics);
        rubraview_box_set_size(b, sized.strip_columns + (notches > 0.0 ? 1 : -1), b->scale > 0.0 ? b->scale : 1.0);
        rubraview_settings_set(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("toolbox_columns"), (double)b->columns);
        n = snprintf(line, sizeof(line), T("%d buttons a row"), rubraview_box_metrics(b, &metrics).strip_columns);
        if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
        note_activity(app);
        return true;
    }
    rubraview_anchor_half_t half = rubraview_box_anchor_half_at(&app->toolbox, &metrics, x, y);
    if (half == RUBRAVIEW_ANCHOR_NONE) return false;
    if (half == RUBRAVIEW_ANCHOR_CLICK && !(modifiers & RUBRAVIEW_MOD_ALT)) {   /* RV-171: F, the file before or after */
        if (page_count(app) < 2) return true;
        apply_intent(app, notches > 0.0 ? RUBRAVIEW_INTENT_PREV_PAGE : RUBRAVIEW_INTENT_NEXT_PAGE);
        strip_peek(app);   /* shown or not as Settings › Viewer says */
        note_activity(app);
        return true;
    }
    if (half == RUBRAVIEW_ANCHOR_CLICK && (modifiers & RUBRAVIEW_MOD_ALT)) {
        double next = rubraview_box_opacity_step(rubraview_settings_get(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("widget_opacity")), notches);
        rubraview_settings_set(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("widget_opacity"), next);
        rubraview_settings_set(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("toolbox_opacity"), next);   /* its toolbar with it */
        n = snprintf(line, sizeof(line), T("Floating Widget %d%%"), (int)next);
    } else {
        double scale = (b->scale > 0.0 ? b->scale : 1.0) + (notches > 0.0 ? 0.1 : -0.1);
        rubraview_box_set_size(b, b->columns, scale);
        rubraview_settings_set(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("toolbox_size"), b->scale * 100.0);
        n = snprintf(line, sizeof(line), T("buttons %d%%"), (int)lround(b->scale * 100.0));
    }
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
    note_activity(app);
    return true;
}

bool box_wheel_opacity(app_state_t *app, double x, double y, double notches, uint32_t modifiers) {
    if (box_anchor_wheel(app, x, y, notches, modifiers)) return true;   /* 1.25.0 */
    if (!(modifiers & RUBRAVIEW_MOD_ALT)) return false;
    rubraview_tile_metrics_t metrics = tile_metrics(rubraview_pal_window_dpi_scale(app->window));
    rubraview_box_t *boxes[2] = { &app->menubox, &app->toolbox };
    for (size_t i = 0; i < 2; ++i) {
        if (boxes[i]->state == RUBRAVIEW_BOX_DETACHED) continue;
        if (!rubraview_rect_contains(rubraview_box_bounds(boxes[i], &metrics), x, y)) continue;
        double next = rubraview_box_opacity_step(box_opacity(app, boxes[i]), notches);
        rubraview_settings_set(&app->settings, PROVEN_LIT("ui"), box_opacity_key(boxes[i]), next);
        char line[48];
        int n = snprintf(line, sizeof(line), "%s %d%%", boxes[i]->kind == RUBRAVIEW_BOX_MENU ? T("menu box") : "toolbox", (int)next);
        if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
        note_activity(app);
        return true;
    }
    return false;
}

/* D-110: the speed tile. A tap goes round the usual speeds as before; held
   and dragged up or down, or with the wheel over it, the speed moves a
   tenth at a time. So the tap is decided when the button is let go. */
static void speed_nudge(app_state_t *app, double base, int steps) {
    double speed = floor((base + (double)steps * 0.1) * 10.0 + 0.5) / 10.0;
    if (speed < 0.3) speed = 0.3;   /* whole tenths: 0.25 is the ladder's own */
    if (fabs(speed - (app->media_speed > 0.0 ? app->media_speed : 1.0)) > 1e-6) speed_set(app, speed);
}

/* 1.24.0: where a list goes in the viewer's window when what opened it is elsewhere (the toolbox's own window). */
double chooser_main_x(app_state_t *app) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    return (double)w * 0.5 - 150.0 * rubraview_pal_window_dpi_scale(app->window);
}

double chooser_main_y(app_state_t *app) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    return (double)h * 0.25;
}

void speed_tile_press(app_state_t *app, double y, bool detached, bool interval) {
    app->speed_pressed = true;
    app->speed_dragged = false;
    app->speed_press_detached = detached;
    app->speed_press_interval = interval;   /* D-123: the slide show's Delay tile, half a second a step */
    app->speed_press_y = y;
    app->speed_press_base = interval ? app->slideshow.interval_seconds : app->media_speed > 0.0 ? app->media_speed : 1.0;
}

static void interval_nudge(app_state_t *app, double base, int steps) {
    double seconds = rubraview_slideshow_interval_nudge(base, steps);
    if (fabs(seconds - app->slideshow.interval_seconds) > 1e-6) interval_set(app, seconds);
}

void speed_tile_move(app_state_t *app, double y, bool detached) {
    if (!app->speed_pressed || app->speed_press_detached != detached) return;
    double step = 12.0 * rubraview_pal_window_dpi_scale(app->window);   /* a tenth every twelve pixels */
    int steps = (int)((app->speed_press_y - y) / step);
    if (steps == 0 && !app->speed_dragged) return;
    app->speed_dragged = true;
    if (app->speed_press_interval) interval_nudge(app, app->speed_press_base, steps);
    else speed_nudge(app, app->speed_press_base, steps);
    app->toolbox_window_drawn = 0.0;
}

void speed_tile_release(app_state_t *app, bool detached) {
    if (!app->speed_pressed || app->speed_press_detached != detached) return;
    app->speed_pressed = false;
    if (!app->speed_dragged) {   /* 1.24.0: a tap lists the speeds (or the delays); a drag still slides */
        proven_u8str_view_t action = app->speed_press_interval ? PROVEN_LIT("interval_cycle") : PROVEN_LIT("media_speed_cycle");
        double x = detached ? chooser_main_x(app) : app->pointer_x, y = detached ? chooser_main_y(app) : app->pointer_y;
        if (!chooser_open_tile(app, action, x, y, y)) handle_action(app, action);
    }
    app->toolbox_window_drawn = 0.0;
}

/* D-109: the wheel over a zoom tile (the percent, Zoom+, Zoom-) is the zoom, a step a notch. */
bool zoom_tile_wheel(app_state_t *app, double x, double y, double notches) {
    if (app->toolbox.state == RUBRAVIEW_BOX_DETACHED || notches == 0.0) return false;
    rubraview_tile_metrics_t metrics = tile_metrics(rubraview_pal_window_dpi_scale(app->window));
    int32_t tile = rubraview_box_tile_at(&app->toolbox, &metrics, x, y);
    if (tile < 0) return false;
    toolbox_refresh(app);
    if (tile >= app->toolbox_tile_count) return false;
    proven_u8str_view_t action = app->toolbox_tiles[tile].action;
    if (proven_u8str_view_eq(action, PROVEN_LIT("media_speed_cycle")) && (app->media || anim_plays(app))) {
        speed_nudge(app, app->media_speed > 0.0 ? app->media_speed : 1.0, notches > 0.0 ? 1 : -1);   /* D-110 */
        return true;
    }
    if (proven_u8str_view_eq(action, PROVEN_LIT("interval_cycle"))) {
        interval_nudge(app, app->slideshow.interval_seconds, notches > 0.0 ? 1 : -1);   /* D-123 */
        return true;
    }
    if (!proven_u8str_view_eq(action, PROVEN_LIT("zoom_cycle")) && !proven_u8str_view_eq(action, PROVEN_LIT("zoom_reset")) &&
        !proven_u8str_view_eq(action, PROVEN_LIT("zoom_in")) && !proven_u8str_view_eq(action, PROVEN_LIT("zoom_out"))) {
        /* 1.25.0 (owner, 2026-10-20): over a tile that chooses one of several, the wheel goes to the one before
           or after (its tile changing with it); a click still lists them */
        return chooser_wheel_tile(app, action, notches);
    }
    zoom_step(app, notches > 0.0);
    return true;
}

/* The strip's bar for a still picture: its place in the folder or archive. */
bool pagebar_shown(const app_state_t *app) {
    return !app->media && !anim_plays(app) && page_count(app) > 1;
}

/* A click on that bar: the page whose share of the bar it landed in. */
static void pagebar_click(app_state_t *app, double bar_x, double bar_width, double x) {
    size_t page = rubraview_pagebar_page(bar_x, bar_width, x, page_count(app));
    if ((int32_t)page == current_page_index(app)) return;
    go_to_spread(app, spread_for_chosen_page(app, (int32_t)page));
    update_precache(app);
}

/* The press also takes hold of the bar: moving the pointer turns the
   pages under it, as the film's seek bar does. */
void pagebar_press(app_state_t *app, double bar_x, double bar_width, double x, bool detached) {
    /* RV-115: held here, or every step of the drag after would count as the second try */
    size_t page = rubraview_pagebar_page(bar_x, bar_width, x, page_count(app));
    if ((int32_t)page != current_page_index(app) && !subs_may_leave(app)) return;
    pagebar_click(app, bar_x, bar_width, x);
    app->pagebar_dragging = true;
    app->pagebar_detached = detached;
    app->pagebar_x = bar_x;
    app->pagebar_width = bar_width;
    app->pagebar_last = rubraview_pal_time_now_seconds();
}

/* A page every tenth of a second at most while dragging — each is a
   decode — and the one under the pointer when it is let go. */
void pagebar_drag(app_state_t *app, double x, bool released) {
    double now = rubraview_pal_time_now_seconds();
    if (!released && now - app->pagebar_last < 0.1) return;
    app->pagebar_last = now;
    /* Pages all the way: a film or a song passed over does not stop the
       drag. Where it ends decides the bar: a film or a song there makes it
       that page's own seek bar again (owner, 2026-09-28). */
    if (page_count(app) > 1) pagebar_click(app, app->pagebar_x, app->pagebar_width, x);
    if (released) app->pagebar_dragging = false;
}

/* The strip's seek bar and the line under it: the file's name or, under
   the pointer, what the button does. Offsets put the layout on screen. */
/* What stands at the right of the strip's name: the place among the pages, or a film's or an
   animation's time. Nothing while a button is pointed at (the name row says its caption). */
static int strip_place_text(const app_state_t *app, int32_t hovered, char *place, size_t size) {
    int n = (pagebar_shown(app) || app->pagebar_dragging) && hovered < 0
        ? snprintf(place, size, "%d / %zu", current_page_index(app) + 1, page_count(app)) : 0;
    if (app->media && !app->pagebar_dragging && hovered < 0 && app->media_info.duration_seconds > 0.0) {
        /* The film's time, its volume, speed and repeat: once on the bottom
           strip, which is gone (owner, 2026-09-29). */
        int volume = (int)rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("volume"));
        bool muted = rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("mute")) > 0.5;
        n = (int)rubraview_media_status(place, size, app->media_position, app->media_info.duration_seconds,
                                        volume, muted, app->media_speed, app->ab_a, app->ab_b).size;
        /* The sync moved by hand stays in sight while it is not nought (owner, 2026-10-16). */
        char sync[32];
        proven_u8str_view_t moved = rubraview_subtitle_offset_text(sync, sizeof(sync), app->subtitle.offset_seconds);
        if (moved.size > 0 && n > 0 && (size_t)n + 3 + moved.size < size) {
            n += snprintf(place + n, size - (size_t)n, "   %.*s", (int)moved.size, moved.ptr);
        }
    }
    if (anim_plays(app) && !app->pagebar_dragging && hovered < 0) {
        /* D-82: an animation's time, and its speed when that is not 1x. */
        char at[32], total[32], speed[16] = "";
        proven_u8str_view_t a = rubraview_format_timecode(at, sizeof(at), play_position(app), false);
        proven_u8str_view_t t = rubraview_format_timecode(total, sizeof(total), play_duration(app), false);
        if (fabs(app->animation.speed - 1.0) > 1e-6) speed_text(speed, sizeof(speed), app->animation.speed);
        n = snprintf(place, size, "%.*s / %.*s%s%s%s", (int)a.size, a.ptr, (int)t.size, t.ptr,
                     speed[0] ? "  " : "", speed, app->ab_a < 0.0 ? "" : app->ab_b < 0.0 ? "  A-" : "  A-B");
        if (n < 0 || (size_t)n >= size) n = 0;
    }
    return n > 0 && (size_t)n < size ? n : 0;
}

static double strip_place_width(int n, double line_height) {
    return (double)n * line_height * 0.45;
}

/* D-124: how many rows the name takes in the strip, one to three, and whether the place goes
   under it on one more. Measured when the name, the width or the place's length changes. */
void strip_title_plan(app_state_t *app) {
    rubraview_tile_metrics_t base = tile_metrics(rubraview_pal_window_dpi_scale(app->window));
    rubraview_tile_metrics_t sized = rubraview_box_metrics(&app->toolbox, &base);
    rubraview_toolbox_shape_t shape = rubraview_box_toolbox_shape(&app->toolbox);
    shape.count = app->toolbox_tile_count;
    rubraview_toolbox_layout_t l = rubraview_toolbox_layout_shaped(&sized, &shape);   /* its width follows its lines */
    double width = l.title.width, line = sized.title_height, font = line * 0.72;
    proven_u8str_view_t name = page_count(app) > 0 ? page_display_name(app, (size_t)current_page_index(app)) : PROVEN_LIT("");
    char place[96];
    int n = strip_place_text(app, -1, place, sizeof(place));

    uint64_t key = 1469598103934665603ull;
    for (size_t i = 0; i < name.size; ++i) key = (key ^ (uint8_t)name.ptr[i]) * 1099511628211ull;
    key = (key ^ (uint64_t)(width * 16.0)) * 1099511628211ull;
    key = (key ^ (uint64_t)(line * 16.0)) * 1099511628211ull;
    key = (key ^ (uint64_t)n) * 1099511628211ull;
    if (key != app->strip_title_key || app->strip_title_lines < 1) {
        app->strip_title_key = key;
        int32_t lines = 1;
        bool below = false, cut_name = false;
        double one_w = 0.0, w = 0.0, h = 0.0, row = 0.0;
        double place_w = n > 0 ? strip_place_width(n, line) + line * 0.5 : 0.0;
        bool measured = name.size > 0 && width > 0.0 && rubraview_pal_render_measure_text(app->renderer, name, font, &one_w);
        if (measured && one_w + place_w > width) {
            /* The name does not fit beside the place: it gets the whole width, and rows. */
            below = n > 0;
            if (rubraview_pal_render_measure_text_wrapped(app->renderer, PROVEN_LIT("Ag"), font, width, &w, &row) && row > 0.0 &&
                rubraview_pal_render_measure_text_wrapped(app->renderer, name, font, width, &w, &h)) {
                lines = (int32_t)floor(h / row + 0.5);
            }
            if (lines < 1) lines = 1;
            if (lines > RUBRAVIEW_TOOLBOX_MAX_NAME_LINES) {
                /* Longer than its three rows: the longest beginning that fits, and an ellipsis. */
                lines = RUBRAVIEW_TOOLBOX_MAX_NAME_LINES;
                static const char DOTS[] = "\xE2\x80\xA6";
                size_t cap = sizeof(app->strip_name) - sizeof(DOTS);
                size_t low = 0, high = name.size < cap ? name.size : cap;
                while (low < high) {
                    size_t mid = (low + high + 1) / 2, cut = mid;
                    while (cut > 0 && cut < name.size && ((uint8_t)name.ptr[cut] & 0xC0) == 0x80) cut--;   /* not inside a character */
                    memcpy(app->strip_name, name.ptr, cut);
                    memcpy(app->strip_name + cut, DOTS, sizeof(DOTS));
                    bool fits = rubraview_pal_render_measure_text_wrapped(app->renderer, (proven_u8str_view_t){ (const proven_byte_t *)app->strip_name, cut + sizeof(DOTS) - 1 },
                                                                          font, width, &w, &h) && h <= row * ((double)lines + 0.5);
                    if (fits) low = mid;
                    else high = cut > 0 ? cut - 1 : 0;
                }
                while (low > 0 && low < name.size && ((uint8_t)name.ptr[low] & 0xC0) == 0x80) low--;
                memcpy(app->strip_name, name.ptr, low);
                memcpy(app->strip_name + low, DOTS, sizeof(DOTS));
                cut_name = true;
            }
            if (below) lines++;   /* the place's own row never takes one of the name's */
        }
        if (!cut_name) app->strip_name[0] = '\0';
        app->strip_title_lines = lines;
        app->strip_place_below = below;
    }
    app->toolbox.title_lines = app->strip_title_lines;
}

void draw_strip_head(app_state_t *app, rubraview_renderer_t *r, const rubraview_toolbox_layout_t *l,
                            double ox, double oy, int32_t hovered, double opacity) {
    /* Each group in a frame of its own (owner, 2026-10-15): a rounded
       ground of another colour, its edge drawn plain, under the buttons. */
    double round = l->group_pad * 1.5;
    for (int32_t k = 0; k < l->group_count; ++k) {
        rubraview_rect_t g = l->group_rects[k];
        rubraview_pal_rect_t f = { ox + g.x, oy + g.y, g.width, g.height };
        rubraview_pal_render_fill_rect(r, f, rubraview_box_fade(COLOR_GROUP_FILL, opacity), round);
        rubraview_pal_render_stroke_rect(r, f, rubraview_box_fade(COLOR_GROUP_BORDER, opacity), l->group_pad * 0.4, round);
    }
    if (l->timeline.width > 0.0) {
        rubraview_pal_rect_t track = { ox + l->timeline.x, oy + l->timeline.y + l->timeline.height * 0.35,
                                       l->timeline.width, l->timeline.height * 0.3 };
        rubraview_pal_render_fill_rect(r, track, 0x60FFFFFFu, track.height * 0.5);
        double duration = play_duration(app);
        double f = duration > 0.0 ? play_position(app) / duration : 0.0;
        bool pages = pagebar_shown(app) || app->pagebar_dragging;   /* held, it stays a page bar */
        if (pages) f = rubraview_pagebar_fraction((size_t)current_page_index(app), page_count(app));
        if (f < 0.0) f = 0.0;
        if (f > 1.0) f = 1.0;
        rubraview_pal_rect_t done = { track.x, track.y, track.width * f, track.height };
        rubraview_pal_render_fill_rect(r, done, COLOR_TILE_CURRENT, track.height * 0.5);
    }
    rubraview_pal_rect_t name = { ox + l->title.x, oy + l->title.y, l->title.width, l->title.height };
    proven_u8str_view_t label = hovered >= 0 && hovered < app->toolbox_tile_count ? rubraview_tr_ctx_u8("toolbox", app->toolbox_tiles[hovered].caption)
                                                                      : page_display_name(app, (size_t)current_page_index(app));
    char said[160];
    if (hovered >= 0 && hovered < app->toolbox_tile_count) {
        /* the state with the name: "Layout: book-rtl", "Crisp: on" (owner, 2026-10-14) */
        rubraview_action_facts_t facts = action_facts(app);
        rubraview_action_state_t st = rubraview_action_state(app->toolbox_tiles[hovered].action, &facts);
        label = rubraview_tile_caption(label, false, st.mark == RUBRAVIEW_MARK_CURRENT ? RUBRAVIEW_MARK_NONE : st.mark, st.value, said, sizeof(said));
    }
    if (hovered < 0 && app->strip_name[0]) label = cstr(app->strip_name);   /* D-124: cut to its rows */
    char place[96];
    int n = strip_place_text(app, hovered, place, sizeof(place));
    double line = l->title_line_height > 0.0 ? l->title_line_height : l->title.height;
    if (n > 0) {
        /* The place at the right end of the line, the name in what is left; or, where the
           name needs the width, on the last row by itself (D-124). */
        double w = strip_place_width(n, line);
        bool below = app->strip_place_below && l->title_lines > 1;
        rubraview_pal_rect_t right = { name.x + name.width - w, below ? name.y + name.height - line : name.y, w, line };
        rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)place, .size = (size_t)n }, right,
                                       line * 0.72, COLOR_TEXT, RUBRAVIEW_TEXT_RIGHT);
        if (below) name.height -= line;
        else name.width -= w + line * 0.5;
    }
    int before_name = place_font_begin(app, PF_NAME, NULL);   /* 1.25.0: file names in their own face */
    rubraview_pal_render_draw_text(r, label, name, line * 0.72 * name_scale(app), COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    place_font_end(app, before_name);
}

/* A strip button's face: its icon, or a short caption where the icon font is missing. */
void draw_strip_button(app_state_t *app, rubraview_renderer_t *r, rubraview_pal_rect_t tile, int32_t i,
                              const rubraview_action_facts_t *facts, bool hovered, uint32_t fill, uint32_t border) {
    rubraview_pal_render_fill_rect(r, tile, hovered ? 0xE0343434u : fill, 3.0);
    rubraview_pal_render_stroke_rect(r, tile, hovered ? COLOR_TILE_CURRENT : border, 1.0, 3.0);
    bool enabled = true;
    proven_u8str_view_t caption = toolbox_caption(app, &app->toolbox_tiles[i], &enabled);
    uint32_t ink = enabled ? COLOR_TEXT : 0x70F0F0F0u;
    uint32_t icon = rubraview_action_icon(app->toolbox_tiles[i].action, facts);
    if (icon && proven_u8str_view_eq(app->toolbox_tiles[i].action, PROVEN_LIT("fit_cycle"))) {
        /* The fit's sign above and its short name below, both changing with the fit (owner, 2026-10-15). */
        rubraview_pal_rect_t top = { tile.x, tile.y + tile.height * 0.06, tile.width, tile.height * 0.58 };
        rubraview_pal_rect_t bottom = { tile.x, tile.y + tile.height * 0.60, tile.width, tile.height * 0.36 };
        if (rubraview_pal_render_draw_icon(r, icon, top, tile.height * 0.38, ink)) {
            rubraview_pal_render_draw_text(r, caption, bottom, tile.height * 0.27, ink, RUBRAVIEW_TEXT_CENTER);
            return;
        }
    }
    if (proven_u8str_view_eq(app->toolbox_tiles[i].action, PROVEN_LIT("center_picture"))) {
        /* 1.25.0: a square with a cross through its middle */
        double side = tile.height * 0.42, cx = tile.x + tile.width * 0.5, cy = tile.y + tile.height * 0.5, th = fmax(1.2, tile.height * 0.045);
        rubraview_pal_render_stroke_rect(r, (rubraview_pal_rect_t){ cx - side * 0.5, cy - side * 0.5, side, side }, ink, th, 1.0);
        rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ cx - side * 0.36, cy - th * 0.5, side * 0.72, th }, ink, 0.0);
        rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ cx - th * 0.5, cy - side * 0.36, th, side * 0.72 }, ink, 0.0);
        return;
    }
    if (icon && rubraview_pal_render_draw_icon(r, icon, tile, tile.height * 0.5, ink)) return;
    /* A word longer than five letters ("Stretch") is drawn smaller to stay inside; a caption
       of two rows ("Real\nsize") keeps its size (owner, 2026-10-15). */
    size_t longest = 0, run = 0;
    for (size_t k = 0; k < caption.size; ++k) {
        if (caption.ptr[k] == '\n') run = 0;
        else if (((uint8_t)caption.ptr[k] & 0xC0) != 0x80 && ++run > longest) longest = run;
    }
    double size = tile.height * 0.34;
    if (longest > 5) size = fmax(tile.height * 0.22, size * 5.0 / (double)longest);
    size *= place_size(app, PF_BUTTONS);   /* 1.25.0 */
    rubraview_pal_render_draw_text(r, caption, tile, size, ink, RUBRAVIEW_TEXT_CENTER);
}

/* The owner (2026-10-18): a tile's words all inside it in every language —
   smaller when they would not fit, each word whole on its line. Sizes are
   remembered by the words and the tile's size, so a frame measures nothing. */
/* A Korean word is never split between its syllables (DirectWrite would,
   "최근 파 / 일"): a word joiner (U+2060) between each two of a word. */
static proven_u8str_view_t tile_caption_keep_words(proven_u8str_view_t caption, char *buf, size_t cap) {
    size_t n = 0;
    bool last_hangul = false;
    for (size_t i = 0; i < caption.size;) {
        unsigned char c = (unsigned char)caption.ptr[i];
        size_t len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        if (i + len > caption.size) len = caption.size - i;
        bool hangul = len == 3 && c >= 0xEA && c <= 0xED;
        if (hangul && last_hangul) {
            if (n + 3 >= cap) return caption;
            memcpy(buf + n, "\xE2\x81\xA0", 3);
            n += 3;
        }
        if (n + len >= cap) return caption;
        memcpy(buf + n, caption.ptr + i, len);
        n += len;
        last_hangul = hangul;
        i += len;
    }
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)buf, .size = n };
}

static double tile_caption_size(app_state_t *app, proven_u8str_view_t caption, rubraview_pal_rect_t tile, double size) {
    static struct { uint64_t key; double size; } memo[128];
    uint64_t key = 1469598103934665603ull;
    for (size_t i = 0; i < caption.size; ++i) key = (key ^ (unsigned char)caption.ptr[i]) * 1099511628211ull;
    key ^= (uint64_t)lround(tile.width * 8.0) << 40 ^ (uint64_t)lround(tile.height * 8.0) << 20 ^ (uint64_t)lround(size * 64.0);
    size_t slot = (size_t)(key % 128u);
    if (memo[slot].key == key && memo[slot].size > 0.0) return memo[slot].size;
    double room_w = tile.width - 6.0, room_h = tile.height - 4.0, smallest = size * 0.55;
    for (; size > smallest; size *= 0.92) {
        double w = 0.0, h = 0.0, word = 0.0;
        if (!rubraview_pal_render_measure_text_wrapped(app->renderer, caption, size, room_w, &w, &h)) break;
        bool fits = h <= room_h;
        for (size_t a = 0; fits && a < caption.size;) {   /* no word broken across lines */
            size_t b = a;
            while (b < caption.size && caption.ptr[b] != ' ') ++b;
            bool breaks = false;   /* Chinese and Japanese break between any two characters; Korean, Latin and Persian keep the word */
            for (size_t c = a; c < b; ++c) breaks = breaks || ((unsigned char)caption.ptr[c] >= 0xE3 && (unsigned char)caption.ptr[c] <= 0xE9);
            if (b > a && !breaks && rubraview_pal_render_measure_text(app->renderer, (proven_u8str_view_t){ caption.ptr + a, b - a }, size, &word) && word > room_w) fits = false;
            a = b + 1;
        }
        if (fits) break;
    }
    if (size < smallest) size = smallest;
    memo[slot].key = key;
    memo[slot].size = size;
    return size;
}

static void draw_box_in(app_state_t *app, const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics,
                        const char *const *captions, int32_t caption_count, const rubraview_menu_state_t *menu);
/* 1.25.0: the Floating Widget's words in the buttons' face (Settings › General) */
void draw_box(app_state_t *app, const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics,
                     const char *const *captions, int32_t caption_count,
                     const rubraview_menu_state_t *menu) {
    int before = place_font_begin(app, PF_BUTTONS, NULL);
    draw_box_in(app, box, metrics, captions, caption_count, menu);
    place_font_end(app, before);
}

static void draw_box_in(app_state_t *app, const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics,
                        const char *const *captions, int32_t caption_count, const rubraview_menu_state_t *menu) {
    /* The menu opened from the title bar has no anchor: nothing to draw while it is folded. */
    if (box->headless && box->state == RUBRAVIEW_BOX_COLLAPSED) return;
    rubraview_rect_t bounds = rubraview_box_bounds(box, metrics);
    rubraview_pal_rect_t body = { bounds.x, bounds.y, bounds.width, bounds.height };

    /* D-15: each box its own opacity; the text stays solid. */
    double opacity = box_opacity(app, box);
    const uint32_t box_fill = rubraview_box_fade(COLOR_BOX_FILL, opacity);
    const uint32_t box_border = rubraview_box_fade(COLOR_BOX_BORDER, opacity);
    const uint32_t tile_fill = rubraview_box_fade(COLOR_TILE_FILL, opacity);

    rubraview_pal_render_fill_rect(app->renderer, body, box_fill, 2.0);
    rubraview_pal_render_stroke_rect(app->renderer, body, box_border, 1.0, 2.0);
    if (box->state != RUBRAVIEW_BOX_COLLAPSED && !box->headless) {
        /* Open, the grid sits beside the anchor, which keeps its own ground. */
        rubraview_rect_t a = rubraview_box_anchor_rect(box, metrics);
        rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ a.x, a.y, a.width, a.height }, box_fill, 2.0);
    }

    /* The anchor is two buttons, and they must not look like one. The
       left is outlined — it waits to be clicked; the right is filled —
       it answers the pointer on its own. */
    rubraview_rect_t click_half = rubraview_box_anchor_half_rect(box, metrics, RUBRAVIEW_ANCHOR_CLICK);
    rubraview_rect_t hover_half = rubraview_box_anchor_half_rect(box, metrics, RUBRAVIEW_ANCHOR_HOVER);

    rubraview_pal_rect_t left = { click_half.x, click_half.y, click_half.width, click_half.height };
    rubraview_pal_rect_t right = { hover_half.x, hover_half.y, hover_half.width, hover_half.height };

    if (!box->headless) {
        /* 1.25.0: F and T at the Floating Widget's own opacity (Settings › Display) */
        double wo = rubraview_settings_get(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("widget_opacity"));
        rubraview_pal_render_fill_rect(app->renderer, left, rubraview_box_fade(COLOR_BOX_FILL, wo), 2.0);
        rubraview_pal_render_stroke_rect(app->renderer, left, rubraview_box_fade(COLOR_BOX_BORDER, wo), 1.0, 2.0);
        rubraview_pal_render_fill_rect(app->renderer, right, rubraview_box_fade(COLOR_TILE_FILL, wo), 2.0);
        rubraview_pal_render_stroke_rect(app->renderer, right, rubraview_box_fade(COLOR_BOX_BORDER, wo), 1.0, 2.0);

        /* RFC-0002 §6.1 (owner, 2026-09-15): M for the menu box, T for the toolbox. */
        /* 1.25.0 (owner, 2026-10-20): F (the Floating Widget) and T (its toolbar) */
        rubraview_pal_render_draw_text(app->renderer,
                                       box->kind == RUBRAVIEW_BOX_MENU ? PROVEN_LIT("M") : PROVEN_LIT("F"),
                                       left, metrics->anchor_size * 0.42 * place_size(app, PF_BUTTONS), COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        rubraview_pal_render_draw_text(app->renderer, PROVEN_LIT("T"), right,
                                       metrics->anchor_size * 0.42 * place_size(app, PF_BUTTONS), COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }

    if (box->state == RUBRAVIEW_BOX_COLLAPSED) return;

    /* The pin, a third square beside the anchor while the box is open
       (owner, 2026-09-22): filled while the box stays open, an outline
       while it folds when the pointer leaves. */
    rubraview_rect_t pr = rubraview_box_pin_rect(box, metrics);
    rubraview_pal_rect_t pin = { pr.x, pr.y, pr.width, pr.height };
    bool pinned = rubraview_box_pin_shown_on(box);
    if (!box->headless) {
        /* 1.25.0: P, lit and bold while on */
        rubraview_pal_render_fill_rect(app->renderer, pin, pinned ? TITLEBAR_ON : box_fill, 2.0);
        rubraview_pal_render_stroke_rect(app->renderer, pin, box_border, 1.0, 2.0);
        draw_text_weight(app, PROVEN_LIT("P"), pin, pr.height * 0.42, pinned ? COLOR_TEXT : 0xC8F0F0F0u, pinned);
        draw_box_grip(app, box, metrics, box_fill, box_border);
    }

    bool strip = box == &app->toolbox;
    rubraview_action_facts_t facts = action_facts(app);
    int32_t hovered = rubraview_box_tile_at(box, metrics, app->pointer_x, app->pointer_y);
    if (strip) {
        rubraview_tile_metrics_t sized = rubraview_box_metrics(box, metrics);
        rubraview_toolbox_shape_t shape = rubraview_box_toolbox_shape(box);
        rubraview_toolbox_layout_t l = rubraview_toolbox_layout_shaped(&sized, &shape);
        draw_strip_head(app, app->renderer, &l, bounds.x, bounds.y, hovered, opacity);
    }

    for (int32_t i = 0; i < box->tile_count; ++i) {
        rubraview_rect_t t = rubraview_box_tile_rect(box, metrics, i);
        rubraview_pal_rect_t tile = { t.x, t.y, t.width, t.height };
        if (strip && i < app->toolbox_tile_count) {
            draw_strip_button(app, app->renderer, tile, i, &facts, i == hovered, tile_fill, box_border);
            continue;
        }
        rubraview_pal_render_fill_rect(app->renderer, tile, tile_fill, 0.0);
        rubraview_pal_render_stroke_rect(app->renderer, tile, box_border, 1.0, 0.0);
        proven_u8str_view_t caption = PROVEN_LIT("");
        bool enabled = true, current = false;
        char scratch[64];
        if (menu) {
            caption = menu_tile_caption(menu, i, scratch, sizeof(scratch), &enabled, &current);
        } else if (captions && i < caption_count) {
            caption = cstr(captions[i]);
        }
        if (current) {
            rubraview_pal_render_stroke_rect(app->renderer, tile, COLOR_TILE_CURRENT, 2.0, 0.0);
        }
        char joined[384];
        if (caption.size > 0) caption = tile_caption_keep_words(caption, joined, sizeof(joined));
        if (caption.size > 0) {
            rubraview_pal_render_draw_text(app->renderer, caption, tile, tile_caption_size(app, caption, tile, t.height * 0.22),
                                           enabled ? COLOR_TEXT : 0x70F0F0F0u, RUBRAVIEW_TEXT_CENTER);
        }
    }
}

/* §3.15.2's three tiers: breadcrumb header, virtualised tile grid,
   action bar. Tiles are large enough to hit with a thumb over Remote
   Desktop, which is the whole reason this picker exists. */
void draw_outlined_text_aligned(rubraview_renderer_t *renderer, proven_u8str_view_t text, rubraview_pal_rect_t rect,
                                       double size, double outline, uint32_t argb, rubraview_text_align_t align);
void draw_outlined_text(rubraview_renderer_t *renderer, proven_u8str_view_t text, rubraview_pal_rect_t rect,
                               double size, double outline, uint32_t argb);

/* How a page is scaled on screen: Settings › Viewer › Scaling filter,
   read at last (owner, 2026-09-29: "확대축소 품질개선" — it was shown but
   every page was drawn with plain cubic). Plain cubic samples a small
   neighbourhood and nothing more, so a page shrunk to fit — a 4000-pixel
   scan at a quarter of its size — kept only every fourth pixel's worth of
   detail and aliased; Direct2D's high-quality cubic filters first when it
   shrinks and is the same cubic when it enlarges. Pixel art (the toggle)
   still gets nearest. */
rubraview_interpolation_t page_interpolation(const app_state_t *app) {
    if (app->force_nearest) return RUBRAVIEW_INTERP_NEAREST;
    switch ((int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("interpolation")))) {
        case 0:  return RUBRAVIEW_INTERP_NEAREST;
        case 1:  return RUBRAVIEW_INTERP_LINEAR;
        default: return RUBRAVIEW_INTERP_HIGH_QUALITY_CUBIC;
    }
}

void draw_titlebar(app_state_t *app, double win_w);

/* Where the details view's columns begin (owner, 2026-10-16): Name, Type, Size, Created,
   Modified, Resolution, Length. A narrow window lets Created go first, then Resolution,
   Length and Type, so the name keeps room to be read. A column let go is 0 wide. */

/* 1.25.0 (owner, 2026-10-20: "각 열의 너비를 드래그로 ... 열 사이의 순서도 드래그로"): each column's width (at 100 %)
   and their order after Name, left to right; kept in Settings (files.picker_columns). */
double g_col_w[RUBRAVIEW_PICKER_SORT_COUNT] = { 0.0, 62.0, 84.0, 140.0, 140.0, 110.0, 80.0 };
static int g_col_order[RUBRAVIEW_PICKER_SORT_COUNT - 1] = { 1, 2, 3, 4, 5, 6 };

/* "62,84,140,140,110,80;1,2,3,4,5,6" back into the widths and order; anything else leaves them. */
void picker_columns_read(proven_u8str_view_t text) {
    double w[RUBRAVIEW_PICKER_SORT_COUNT] = { 0.0 };
    int order[RUBRAVIEW_PICKER_SORT_COUNT - 1];
    char buf[160];
    if (text.size == 0 || text.size >= sizeof(buf)) return;
    memcpy(buf, text.ptr, text.size);
    buf[text.size] = '\0';
    char *at = buf;
    for (int k = 1; k < RUBRAVIEW_PICKER_SORT_COUNT; ++k) {
        char *end = NULL;
        w[k] = strtod(at, &end);
        if (end == at || w[k] < 30.0 || w[k] > 800.0) return;
        at = end + (*end == ',' || *end == ';' ? 1 : 0);
    }
    bool seen[RUBRAVIEW_PICKER_SORT_COUNT] = { false };
    for (int j = 0; j < RUBRAVIEW_PICKER_SORT_COUNT - 1; ++j) {
        char *end = NULL;
        long k = strtol(at, &end, 10);
        if (end == at || k < 1 || k >= RUBRAVIEW_PICKER_SORT_COUNT || seen[k]) return;
        seen[k] = true;
        order[j] = (int)k;
        at = end + (*end == ',' ? 1 : 0);
    }
    memcpy(g_col_w, w, sizeof(w));
    memcpy(g_col_order, order, sizeof(order));
}

static void picker_columns_write(app_state_t *app) {
    char buf[160];
    size_t n = 0;
    for (int k = 1; k < RUBRAVIEW_PICKER_SORT_COUNT; ++k)
        n += (size_t)snprintf(buf + n, sizeof(buf) - n, "%s%.0f", k > 1 ? "," : "", g_col_w[k]);
    for (int j = 0; j < RUBRAVIEW_PICKER_SORT_COUNT - 1; ++j)
        n += (size_t)snprintf(buf + n, sizeof(buf) - n, "%s%d", j == 0 ? ";" : ",", g_col_order[j]);
    rubraview_settings_set_text(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_columns"), app_keep(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)buf, .size = n }));
}

picker_columns_t picker_detail_columns(double win_w, double dpi) {
    static const rubraview_picker_sort_key_t DROP[] = { RUBRAVIEW_PICKER_SORT_CREATED, RUBRAVIEW_PICKER_SORT_RESOLUTION,
                                                        RUBRAVIEW_PICKER_SORT_LENGTH, RUBRAVIEW_PICKER_SORT_TYPE };
    picker_columns_t c = { .name_x = 10.0 * dpi };
    for (int k = 1; k < RUBRAVIEW_PICKER_SORT_COUNT; ++k) c.w[k] = g_col_w[k] * dpi;
    double right = win_w - 8.0 * dpi;
    for (size_t d = 0;; ++d) {
        double used = 0.0;
        for (int k = 1; k < RUBRAVIEW_PICKER_SORT_COUNT; ++k) used += c.w[k];
        if (right - used - c.name_x >= 240.0 * dpi || d >= sizeof(DROP) / sizeof(DROP[0])) break;
        c.w[DROP[d]] = 0.0;
    }
    double x = right;
    for (int j = RUBRAVIEW_PICKER_SORT_COUNT - 2; j >= 0; --j) { int k = g_col_order[j]; x -= c.w[k]; c.x[k] = x; }
    c.x[0] = c.name_x;
    c.w[0] = x - c.name_x;   /* Name, up to the first column after it */
    return c;
}

/* The column under `x` on the heads' line, or -1. */
int picker_column_at(const picker_columns_t *c, double x) {
    for (int k = 0; k < RUBRAVIEW_PICKER_SORT_COUNT; ++k) {
        if (c->w[k] > 0.0 && x >= c->x[k] && x < c->x[k] + c->w[k]) return k;
    }
    return -1;
}

/* 1.25.0: the column whose left edge is within reach of `x` on the heads' line (its width is dragged there), or 0. */
int picker_column_edge(const picker_columns_t *c, double x, double dpi) {
    for (int k = 1; k < RUBRAVIEW_PICKER_SORT_COUNT; ++k)
        if (c->w[k] > 0.0 && fabs(x - c->x[k]) <= 5.0 * dpi) return k;
    return 0;
}

void picker_columns_motion(app_state_t *app, double x, double y) {
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    picker_columns_t cols = picker_detail_columns((double)w, dpi);
    if (app->col_drag == 1) {   /* the left edge moved: wider to the left, as the columns stand from the right */
        double nw = app->col_drag_w0 + (app->col_drag_x0 - x) / dpi;
        g_col_w[app->col_drag_k] = nw < 40.0 ? 40.0 : nw > 600.0 ? 600.0 : nw;
        return;
    }
    if (app->col_drag == 2 && fabs(x - app->col_drag_x0) > 8.0 * dpi) app->col_drag_moved = true;
    double top = picker_grid_top(app, dpi) - PICKER_DETAIL_HEAD * dpi;
    bool on_heads = y >= top && y < top + PICKER_DETAIL_HEAD * dpi;
    rubraview_pal_window_set_cursor_shape(app->window, app->col_drag == 1 || (on_heads && picker_column_edge(&cols, x, dpi) > 0)
                                                       ? RUBRAVIEW_CURSOR_SIZE_WE : RUBRAVIEW_CURSOR_ARROW);
}

/* The press on the heads let go: a width kept, a column put where it was dropped, or a sort. */
bool picker_columns_release(app_state_t *app, double x) {
    int kind = app->col_drag, k = app->col_drag_k;
    app->col_drag = 0;
    rubraview_pal_window_set_cursor_shape(app->window, RUBRAVIEW_CURSOR_ARROW);
    if (kind == 1) { picker_columns_write(app); return true; }
    if (kind != 2) return false;
    if (!app->col_drag_moved) { picker_sort_by(app, k); return true; }
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    picker_columns_t cols = picker_detail_columns((double)w, dpi);
    int to = picker_column_at(&cols, x);
    if (k == 0 || to < 0 || to == k) return true;   /* Name stays first */
    int a = -1, b = to == 0 ? 0 : -1;               /* let go over Name: the first after it */
    for (int j = 0; j < RUBRAVIEW_PICKER_SORT_COUNT - 1; ++j) { if (g_col_order[j] == k) a = j; if (to > 0 && g_col_order[j] == to) b = j; }
    if (a < 0 || b < 0) return true;
    int moved = g_col_order[a];   /* taken out, put in where the other was */
    if (a < b) for (int j = a; j < b; ++j) g_col_order[j] = g_col_order[j + 1];
    else for (int j = a; j > b; --j) g_col_order[j] = g_col_order[j - 1];
    g_col_order[b] = moved;
    picker_columns_write(app);
    return true;
}

/* "2026-10-12 09:30", this machine's time; empty when there is none. */
static size_t picker_date_text(char *buf, size_t cap, int64_t unix_seconds) {
    buf[0] = '\0';
    if (unix_seconds <= 0) return 0;
    time_t t = (time_t)unix_seconds;
    struct tm local;
    if (localtime_s(&local, &t) != 0) return 0;
    return strftime(buf, cap, "%Y-%m-%d %H:%M", &local);
}

static void draw_picker(app_state_t *app, double win_w, double win_h) {
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double crumb_h = PICKER_CRUMB_HEIGHT * dpi;
    double top = picker_top(app);
    picker_layout(app);
    bool details = picker_details(app);
    double header_h = picker_grid_top(app, dpi);
    double action_h = PICKER_ACTION_HEIGHT * dpi;

    rubraview_pal_rect_t backdrop = { 0.0, 0.0, win_w, win_h };
    rubraview_pal_render_fill_rect(app->renderer, backdrop, 0xF0101010u, 0.0);

    /* Virtualised grid: only the rows on screen are drawn. */
    rubraview_virtual_range_t visible = rubraview_picker_visible(&app->picker);
    double cell = app->picker.tile_extent;
    size_t tile_cols = (size_t)picker_tile_columns(app);
    double cell_w = win_w / (double)tile_cols;
    picker_columns_t cols = picker_detail_columns(win_w, dpi);

    for (size_t i = 0; details && i < visible.count; ++i) {
        size_t index = visible.first + i;
        if (index >= app->picker_listing.count) break;
        const rubraview_fs_entry_t *entry = &app->picker_listing.entries[index];
        double y = header_h + (double)index * cell - app->picker.scroll_offset;
        if (y + cell < header_h || y > win_h - action_h) continue;

        rubraview_pal_rect_t row = { 4.0 * dpi, y, win_w - 8.0 * dpi, cell - 1.0 };
        bool picked = app->picker_selected && app->picker_selected[index];
        double check_room = 30.0 * dpi;   /* 1.22.0: the check box at the left */
        double font = 14.0 * dpi;
        if (picked) rubraview_pal_render_fill_rect(app->renderer, row, COLOR_TILE_CURRENT, 0.0);
        else if (index % 2 == 1) rubraview_pal_render_fill_rect(app->renderer, row, 0x18FFFFFFu, 0.0);
        if (index == app->picker.focus) rubraview_pal_render_stroke_rect(app->renderer, row, COLOR_TEXT, 1.0, 0.0);

        /* A small picture where there is one, then the name. */
        rubraview_texture_t *thumb = (app->picker_thumbs && index < app->picker_thumb_count)
                                         ? app->picker_thumbs[index].texture : NULL;
        double side = cell - 4.0 * dpi;
        if (thumb) {
            int32_t tw = 0, th = 0;
            rubraview_pal_texture_size(thumb, &tw, &th);
            if (tw > 0 && th > 0) {
                rubraview_pal_render_draw_texture(app->renderer, thumb, rubraview_mat3x2_multiply(
                    rubraview_mat3x2_scale(side / (double)tw, side / (double)th),
                    rubraview_mat3x2_translate(cols.name_x + check_room, y + 2.0 * dpi)), RUBRAVIEW_INTERP_LINEAR);
            }
        }
        char fit[512], cellbuf[48];
        double name_x = cols.name_x + check_room + side + 8.0 * dpi;
        rubraview_pal_rect_t name = { name_x, y, cols.x[0] + cols.w[0] - name_x - 8.0 * dpi, cell };
        rubraview_pal_render_draw_text(app->renderer, files_fit_text(fit, sizeof(fit), entry->name, name.width, font), name,
                                       font, entry->is_directory ? 0xFFB8D8FFu : COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
        bool up = entry->is_directory && proven_u8str_view_eq(entry->name, PROVEN_LIT(".."));
        /* A folder, a drive or a page of a book has no size or date to give. */
        bool no_file = entry->is_directory || (entry->size_bytes == 0 && entry->mtime == 0);
        rubraview_picker_media_t media = (app->picker_thumbs && index < app->picker_thumb_count)
                                             ? app->picker_thumbs[index].media : (rubraview_picker_media_t){ 0 };
        for (int k = 1; k < RUBRAVIEW_PICKER_SORT_COUNT; ++k) {
            if (cols.w[k] <= 0.0) continue;
            size_t n = 0;
            rubraview_text_align_t align = RUBRAVIEW_TEXT_LEFT;
            uint32_t ink = COLOR_BOX_BORDER;
            switch ((rubraview_picker_sort_key_t)k) {
                case RUBRAVIEW_PICKER_SORT_TYPE: n = up ? 0 : rubraview_picker_type_text(cellbuf, sizeof(cellbuf), entry->name, entry->is_directory); break;
                case RUBRAVIEW_PICKER_SORT_SIZE:
                    n = no_file ? 0 : rubraview_picker_size_text(cellbuf, sizeof(cellbuf), entry->size_bytes);
                    align = RUBRAVIEW_TEXT_RIGHT; ink = COLOR_TEXT;
                    break;
                case RUBRAVIEW_PICKER_SORT_CREATED: n = no_file ? 0 : picker_date_text(cellbuf, sizeof(cellbuf), entry->ctime); break;
                case RUBRAVIEW_PICKER_SORT_MODIFIED: n = no_file ? 0 : picker_date_text(cellbuf, sizeof(cellbuf), entry->mtime); break;
                case RUBRAVIEW_PICKER_SORT_RESOLUTION: {
                    int w = media.width > 0 && media.height > 0 ? snprintf(cellbuf, sizeof(cellbuf), "%d x %d", (int)media.width, (int)media.height) : 0;
                    n = w > 0 ? (size_t)w : 0;
                    break;
                }
                case RUBRAVIEW_PICKER_SORT_LENGTH:
                    if (media.seconds > 0.0) n = rubraview_format_timecode(cellbuf, sizeof(cellbuf), media.seconds, false).size;
                    break;
                default: break;
            }
            double pad = align == RUBRAVIEW_TEXT_RIGHT ? 14.0 * dpi : 4.0 * dpi;
            rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ (const proven_byte_t *)cellbuf, n },
                                           (rubraview_pal_rect_t){ cols.x[k], y, cols.w[k] - pad, cell }, font, ink, align);
        }
    }

    for (size_t i = 0; details && i < visible.count; ++i) {   /* 1.22.0: a check box at each row's left */
        size_t index = visible.first + i;
        if (index >= app->picker_listing.count) break;
        rubraview_pal_rect_t box = picker_check_rect(app, index, win_w, dpi);
        if (box.y + box.height < header_h || box.y > win_h - action_h) continue;
        draw_picker_check(app, index, win_w, dpi);
    }

    for (size_t i = 0; !details && i < visible.count; ++i) {
        size_t index = visible.first + i;
        if (index >= app->picker_listing.count) break;
        const rubraview_fs_entry_t *entry = &app->picker_listing.entries[index];

        size_t row = index / tile_cols;
        size_t column = index % tile_cols;
        double x = (double)column * cell_w;
        double y = header_h + (double)row * cell - app->picker.scroll_offset;
        if (y + cell < header_h || y > win_h - action_h) continue;

        rubraview_pal_rect_t tile = { x + 6.0 * dpi, y + 6.0 * dpi, cell_w - 12.0 * dpi, cell - 12.0 * dpi };
        bool picked = app->picker_selected && index < app->picker_listing.count && app->picker_selected[index];
        rubraview_texture_t *thumb = (app->picker_thumbs && index < app->picker_thumb_count)
                                         ? app->picker_thumbs[index].texture : NULL;
        rubraview_pal_rect_t caption = { tile.x, tile.y + tile.height * 0.62, tile.width, tile.height * 0.38 };
        rubraview_pal_rect_t kind = { tile.x, tile.y + tile.height * 0.2, tile.width, tile.height * 0.3 };

        if (thumb) {
            /* D-34: the picture fills the tile, soft and a little dark; the
               name is light with a dark outline so it reads on any picture. */
            int32_t tw = 0, th = 0;
            rubraview_pal_texture_size(thumb, &tw, &th);
            if (tw > 0 && th > 0) {
                rubraview_mat3x2_t place = rubraview_mat3x2_multiply(
                    rubraview_mat3x2_scale(tile.width / (double)tw, tile.height / (double)th),
                    rubraview_mat3x2_translate(tile.x, tile.y));
                rubraview_pal_render_draw_texture(app->renderer, thumb, place, RUBRAVIEW_INTERP_LINEAR);
            }
            if (picked) rubraview_pal_render_fill_rect(app->renderer, tile, 0x705B9BD5u, 0.0);
            rubraview_pal_render_stroke_rect(app->renderer, tile,
                                             picked ? COLOR_TILE_CURRENT : (index == app->picker.focus ? COLOR_TEXT : COLOR_BOX_BORDER),
                                             picked ? 3.0 * dpi : (index == app->picker.focus ? 2.0 : 1.0), 0.0);
            draw_outlined_text(app->renderer, entry->name, caption, cell * 0.11, 2.0 * dpi, 0xFFFFF4D6u);
            if (entry->is_directory) {
                draw_outlined_text(app->renderer, TU8("[ folder ]"), kind, cell * 0.10, 1.5 * dpi, 0xFFB8D8FFu);
            }
        } else {
            rubraview_pal_render_fill_rect(app->renderer, tile, picked ? COLOR_TILE_CURRENT : COLOR_TILE_FILL, 0.0);
            rubraview_pal_render_stroke_rect(app->renderer, tile,
                                             index == app->picker.focus ? COLOR_TEXT : COLOR_BOX_BORDER,
                                             index == app->picker.focus ? 2.0 : 1.0, 0.0);
            rubraview_pal_render_draw_text(app->renderer, entry->name, caption,
                                           cell * 0.11, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
            rubraview_pal_render_draw_text(app->renderer,
                                           entry->is_directory ? TU8("[ folder ]") : TU8("[ file ]"),
                                           kind, cell * 0.10, COLOR_BOX_BORDER, RUBRAVIEW_TEXT_CENTER);
        }
        if (y + 12.0 * dpi >= header_h) draw_picker_check(app, index, win_w, dpi);   /* 1.22.0 */
    }

    /* Breadcrumb: every segment is its own tappable tile. Drawn after the
       grid, on a bar of its own, so a row scrolled half under it does not
       show through (seen on the VM, 2026-09-25). */
    rubraview_pal_rect_t crumb_bar = { 0.0, 0.0, win_w, header_h };
    rubraview_pal_render_fill_rect(app->renderer, crumb_bar, 0xFF101010u, 0.0);
    {
        /* The columns' names, on the line the rows scroll under; 1.23.0: in the tiles view too, and a check
           box at its left end for all of them (owner, 2026-10-19: "테이블의 헤드 역할을 하는 행"). */
        double hy = header_h - PICKER_DETAIL_HEAD * dpi, hh = PICKER_DETAIL_HEAD * dpi, font = 13.0 * dpi;
        rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ 0.0, hy, win_w, hh }, COLOR_BAR_FILL, 0.0);
        /* A click on a column's name sorts by it; again, the other way round (owner, 2026-10-16). */
        int sorted = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort"))) - 1;
        bool desc = rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort_desc")) > 0.5;
        for (int k = 0; k < RUBRAVIEW_PICKER_SORT_COUNT; ++k) {
            if (cols.w[k] <= 0.0) continue;
            char head[48];
            int n = snprintf(head, sizeof(head), "%s%s", rubraview_picker_sort_name((rubraview_picker_sort_key_t)k),
                             k == sorted ? (desc ? " \xE2\x96\xBC" : " \xE2\x96\xB2") : "");
            bool right = k == RUBRAVIEW_PICKER_SORT_SIZE;
            double pad = right ? 14.0 * dpi : 4.0 * dpi;
            double from = k == 0 ? cols.name_x + PICKER_HEAD_CHECK_ROOM * dpi : cols.x[k];
            rubraview_pal_rect_t at = { from, hy, cols.x[k] + cols.w[k] - from - pad, hh };
            if (app->pointer_y >= hy && app->pointer_y < hy + hh && app->pointer_x >= cols.x[k] && app->pointer_x < cols.x[k] + cols.w[k]) {
                rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ cols.x[k], hy, cols.w[k], hh }, COLOR_TILE_FILL, 0.0);
            }
            if (n > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ (const proven_byte_t *)head, (size_t)n }, at, font,
                                                      k == sorted ? COLOR_TEXT : COLOR_BOX_BORDER, right ? RUBRAVIEW_TEXT_RIGHT : RUBRAVIEW_TEXT_LEFT);
        }
        /* 1.24.0: the tool row over the heads — the all box, the ways of picking, sorting, the view */
        size_t rows = 0, on = 0;
        for (size_t i = 0; i < app->picker_listing.count; ++i) {
            if (picker_is_up(app, i)) continue;
            rows++;
            if (app->picker_selected && app->picker_selected[i]) on++;
        }
        for (int k = 0; k < PICKER_TOOL_COUNT; ++k) {
            rubraview_pal_rect_t t = picker_tool_rect(app, dpi, k);
            if (t.x + t.width > win_w) break;
            bool lit = (k == PICKER_TOOL_INDIVIDUAL && app->picker.mode == RUBRAVIEW_PICK_INDIVIDUAL) ||
                       (k == PICKER_TOOL_RANGE && app->picker.mode == RUBRAVIEW_PICK_RANGE);
            bool over = pal_rect_has(t, app->pointer_x, app->pointer_y);
            rubraview_pal_render_fill_rect(app->renderer, t, lit ? COLOR_TILE_CURRENT : over ? 0x40FFFFFFu : COLOR_TILE_FILL, 3.0);
            rubraview_pal_render_stroke_rect(app->renderer, t, COLOR_BOX_BORDER, 1.0, 3.0);
            char buf[96];
            proven_u8str_view_t label = picker_tool_label(app, k, buf, sizeof(buf));
            rubraview_pal_rect_t words = t;
            if (k == PICKER_TOOL_ALL) {   /* the box: all, some (a dash) or none */
                double side = 18.0 * dpi;
                rubraview_pal_rect_t box = { t.x + 8.0 * dpi, t.y + (t.height - side) * 0.5, side, side };
                bool all = rows > 0 && on == rows;
                rubraview_pal_render_fill_rect(app->renderer, box, on > 0 ? 0xFF2E7BD6u : 0xB0101010u, 3.0 * dpi);
                rubraview_pal_render_stroke_rect(app->renderer, box, on > 0 ? 0xFFFFFFFFu : 0xC0FFFFFFu, 1.5 * dpi, 3.0 * dpi);
                if (on > 0) rubraview_pal_render_draw_text(app->renderer, all ? PROVEN_LIT("\xE2\x9C\x93") : PROVEN_LIT("\xE2\x80\x93"), box,
                                                           box.height * 0.8, 0xFFFFFFFFu, RUBRAVIEW_TEXT_CENTER);
                words.x += 22.0 * dpi;
                words.width -= 22.0 * dpi;
            }
            rubraview_pal_render_draw_text(app->renderer, label, words, 13.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
    }
    if (top > 0.0) {
        /* What this screen is, and nothing to press. */
        rubraview_pal_rect_t band = { 0.0, 0.0, win_w, top };
        rubraview_pal_render_fill_rect(app->renderer, band, 0xFF181818u, 0.0);
        rubraview_pal_rect_t words = { 12.0 * dpi, 0.0, win_w - 24.0 * dpi, top };
        rubraview_pal_render_draw_text(app->renderer,
                                       app->picker_in_book ? TU8("Rubraview  ·  Pages of this book")
                                       : app->picker_add_mode ? TU8("Rubraview  ·  Add to the list: pick, then Add to the list")
                                                           : TU8("Rubraview  ·  Open a file or a folder"),
                                       words, top * 0.36, COLOR_BOX_BORDER, RUBRAVIEW_TEXT_LEFT);
    }
    /* 1.24.0: back, up, forward (owner, 2026-10-19), dim when there is nowhere to go */
    {
        static const char *const NAV[3] = { "\xE2\x97\x80", "\xE2\x96\xB2", "\xE2\x96\xB6" };
        bool can[3] = { app->picker_back_n > 0, rubraview_picker_parent(app->picker_dir).size > 0 && !app->picker_in_book, app->picker_fwd_n > 0 };
        for (int k = 0; k < 3; ++k) {
            rubraview_pal_rect_t b = picker_nav_rect(app, dpi, k);
            bool over = pal_rect_has(b, app->pointer_x, app->pointer_y) && can[k];
            rubraview_pal_render_fill_rect(app->renderer, b, over ? COLOR_TILE_CURRENT : COLOR_TILE_FILL, 2.0);
            rubraview_pal_render_stroke_rect(app->renderer, b, COLOR_BOX_BORDER, 1.0, 2.0);
            rubraview_pal_render_draw_text(app->renderer, cstr(NAV[k]), b, crumb_h * 0.34, can[k] ? COLOR_TEXT : 0x50F0F0F0u, RUBRAVIEW_TEXT_CENTER);
        }
    }
    rubraview_breadcrumbs_t crumbs = rubraview_picker_breadcrumbs(app->picker_dir);
    double crumb_x = PICKER_CRUMB_START * dpi;
    for (size_t i = 0; i < crumbs.count; ++i) {
        double w = picker_chip_width(crumbs.items[i].label, dpi);
        rubraview_pal_rect_t chip = { crumb_x, top + 6.0 * dpi, w, crumb_h - 12.0 * dpi };
        rubraview_pal_render_fill_rect(app->renderer, chip, COLOR_TILE_FILL, 2.0);
        rubraview_pal_render_stroke_rect(app->renderer, chip, COLOR_BOX_BORDER, 1.0, 2.0);
        rubraview_pal_render_draw_text(app->renderer, crumbs.items[i].label, chip,
                                       crumb_h * 0.34, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        crumb_x += w + 4.0 * dpi;
    }
    proven_u8str_view_t star = picker_star_label(app);
    if (star.size > 0) {
        double w = picker_chip_width(star, dpi);
        rubraview_pal_rect_t chip = { crumb_x, top + 6.0 * dpi, w, crumb_h - 12.0 * dpi };
        bool lit = rubraview_favorites_contains(&app->favorites, app->picker_dir);
        rubraview_pal_render_fill_rect(app->renderer, chip, lit ? COLOR_TILE_CURRENT : COLOR_TILE_FILL, 2.0);
        rubraview_pal_render_stroke_rect(app->renderer, chip, COLOR_BOX_BORDER, 1.0, 2.0);
        rubraview_pal_render_draw_text(app->renderer, star, chip, crumb_h * 0.42, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        crumb_x += w + 4.0 * dpi;
    }
    /* 1.22.0: the filter's chip at the bar's right end */
    char filter_buf[200];
    proven_u8str_view_t filter_label;
    rubraview_pal_rect_t filter_chip = picker_filter_chip(app, win_w, dpi, &filter_label, filter_buf, sizeof(filter_buf));
    if (!app->picker_in_book && !app->rename_is_path && !app->rename_is_filter) {
        rubraview_pal_render_fill_rect(app->renderer, filter_chip, app->picker_filter[0] ? COLOR_TILE_CURRENT : COLOR_TILE_FILL, 2.0);
        rubraview_pal_render_stroke_rect(app->renderer, filter_chip, COLOR_BOX_BORDER, 1.0, 2.0);
        rubraview_pal_render_draw_text(app->renderer, filter_label, filter_chip, crumb_h * 0.32, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
    /* The rest of the bar takes a typed path (owner, 2026-09-28), and says so. */
    if (!app->rename_is_path && crumb_x + 200.0 * dpi < filter_chip.x) {
        rubraview_pal_rect_t hint = { crumb_x + 8.0 * dpi, top, win_w - crumb_x - 16.0 * dpi, crumb_h };
        rubraview_pal_render_draw_text(app->renderer, TU8("tap here or Ctrl+L to type a path"), hint,
                                       crumb_h * 0.30, COLOR_BOX_BORDER, RUBRAVIEW_TEXT_LEFT);
    }

    /* The places bar: the usual folders and the drives, one tap away from
       anywhere (owner, 2026-09-28: "즐겨찾기 같은 곳에"). The one on screen is lit. */
    double place_x = 8.0 * dpi;
    picker_bar_item_t item;
    for (size_t i = 0; picker_bar_item(app, i, &item); ++i) {
        double w = picker_chip_width(item.label, dpi);
        /* A little gap between the reader's favourites and the fixed places. */
        if (!item.favorite && i == app->favorites.count && i > 0) place_x += 10.0 * dpi;
        if (place_x + w > picker_favwin_chip(app, dpi, win_w).x - 6.0 * dpi) break;
        rubraview_pal_rect_t chip = { place_x, top + crumb_h + 2.0 * dpi, w, PICKER_PLACES_HEIGHT * dpi - 8.0 * dpi };
        bool here = rubraview_path_same(item.path, app->picker_dir);
        rubraview_pal_render_fill_rect(app->renderer, chip,
                                       here ? COLOR_TILE_CURRENT : item.favorite ? COLOR_TILE_FILL : COLOR_BAR_FILL, 2.0);
        rubraview_pal_render_stroke_rect(app->renderer, chip, item.favorite ? COLOR_TEXT : COLOR_BOX_BORDER, 1.0, 2.0);
        rubraview_pal_render_draw_text(app->renderer, item.label, chip,
                                       crumb_h * 0.30, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        if (app->fav_dragging && (int32_t)i == app->fav_press) {
            rubraview_pal_render_stroke_rect(app->renderer, chip, COLOR_TILE_CURRENT, 2.0, 2.0);
        }
        place_x += w + 4.0 * dpi;
    }
    {   /* 1.24.0: the favourites window, files and folders, all of them */
        rubraview_pal_rect_t fc = picker_favwin_chip(app, dpi, win_w);
        rubraview_pal_render_fill_rect(app->renderer, fc, app->favwin.open ? COLOR_TILE_CURRENT : COLOR_TILE_FILL, 2.0);
        rubraview_pal_render_stroke_rect(app->renderer, fc, COLOR_TEXT, 1.0, 2.0);
        rubraview_pal_render_draw_text(app->renderer, TU8("\xE2\x98\x85 Favourites"), fc, crumb_h * 0.30, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
    /* A favourite being dragged: a bar where it would land. */
    if (app->fav_dragging && app->fav_press >= 0) {
        size_t to = favorite_drop_index(app, app->fav_drag_x, dpi, win_w);
        double cx = 0.0, cw = 0.0;
        if (picker_chip_span(app, to, dpi, win_w, &cx, &cw)) {
            double bx = to > (size_t)app->fav_press ? cx + cw + 2.0 * dpi : cx - 4.0 * dpi;
            rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ bx, top + crumb_h + 2.0 * dpi, 2.0 * dpi,
                                           PICKER_PLACES_HEIGHT * dpi - 8.0 * dpi }, COLOR_TILE_CURRENT, 0.0);
        }
    }

    draw_scrollbar(app, app->renderer, RVSB_PICKER, app->pointer_x, app->pointer_y);   /* RV-170: where the tiles are; it moves them */

    /* Action bar: the buttons, then the selection metrics (§3.15.2 tier three). */
    rubraview_pal_rect_t bar = { 0.0, win_h - action_h, win_w, action_h };
    rubraview_pal_render_fill_rect(app->renderer, bar, COLOR_BAR_FILL, 0.0);

    for (int32_t i = 0; i < PICKER_BOTTOM_COUNT; ++i) {
        picker_button_t b = PICKER_BOTTOM[i];
        rubraview_pal_rect_t rect = picker_button_rect(win_w, win_h, dpi, i);
        bool over = pal_rect_has(rect, app->pointer_x, app->pointer_y);
        rubraview_pal_render_fill_rect(app->renderer, rect, b == PICKER_BTN_OPEN ? COLOR_TILE_CURRENT : over ? 0x40FFFFFFu : COLOR_TILE_FILL, 3.0);
        rubraview_pal_render_stroke_rect(app->renderer, rect, COLOR_BOX_BORDER, 1.0, 3.0);
        const char *label = b == PICKER_BTN_PLAYLIST ? T("Add to the list") : b == PICKER_BTN_OPEN && app->picker_add_mode ? T("OK")
                          : rubraview_tr(PICKER_BUTTON_LABEL[b]);
        rubraview_pal_render_draw_text(app->renderer, cstr(label), rect,
                                       rect.height * 0.34, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
    bar.y += PICKER_BUTTON_ROW * dpi;
    bar.height -= PICKER_BUTTON_ROW * dpi;

    char status[224];
    size_t selected = 0;
    uint64_t bytes = 0;
    rubraview_picker_selection_metrics(&app->picker, &selected, &bytes);
    char hidden[48] = "";
    if (app->picker_hidden > 0) snprintf(hidden, sizeof(hidden), T("   |   %zu other files hidden"), app->picker_hidden);
    int written = snprintf(status, sizeof(status),
                           T("%zu items%s   |   selected %zu (%llu bytes)   |   Enter opens, Ctrl+L types a path, Ctrl+D stars, Esc closes"),
                           app->picker_listing.count, hidden, selected, (unsigned long long)bytes);
    if (written > 0) {
        rubraview_pal_render_draw_text(app->renderer,
                                       (proven_u8str_view_t){ .ptr = (const proven_byte_t *)status, .size = (size_t)written },
                                       bar, 13.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);   /* a status line's size (owner, 2026-10-12: it was 32) */
    }

    /* The picker covers the chrome: its text box and its messages are drawn here. */
    draw_rename_box(app, win_w, win_h, dpi);
    draw_notice(app, win_w, win_h, dpi);
    draw_titlebar(app, win_w);   /* over the band at the top */
}

/* Text with a dark outline around it, for subtitles and their preview.
   The text PAL draws flat text, so the outline is the same string drawn
   behind it, displaced. Four corners were enough at two pixels; at eight
   they came apart into four ghost copies (VM, 2026-09-13), so it is
   eight directions, and a second ring at half the distance once the
   outline is wide enough to show a gap. */
void draw_outlined_text(rubraview_renderer_t *renderer, proven_u8str_view_t text, rubraview_pal_rect_t rect,
                               double size, double outline, uint32_t argb) {
    draw_outlined_text_aligned(renderer, text, rect, size, outline, argb, RUBRAVIEW_TEXT_CENTER);
}

void draw_outlined_text_aligned(rubraview_renderer_t *renderer, proven_u8str_view_t text, rubraview_pal_rect_t rect,
                                       double size, double outline, uint32_t argb, rubraview_text_align_t align) {
    static const double DX[8] = { -1.0, 0.0, 1.0, -1.0, 1.0, -1.0, 0.0, 1.0 };
    static const double DY[8] = { -1.0, -1.0, -1.0, 0.0, 0.0, 1.0, 1.0, 1.0 };
    int rings = outline > 3.0 ? 2 : 1;
    for (int ring = 0; outline > 0.0 && ring < rings; ++ring) {
        double off = ring == 0 ? outline : outline * 0.5;
        for (size_t i = 0; i < 8; ++i) {
            rubraview_pal_rect_t shadow = { rect.x + DX[i] * off, rect.y + DY[i] * off, rect.width, rect.height };
            rubraview_pal_render_draw_text(renderer, text, shadow, size, 0xFF000000u, align);
        }
    }
    rubraview_pal_render_draw_text(renderer, text, rect, size, argb, align);
}

/* §3.16.1 / R135: the line of dialogue for where the film is now, laid
   over the picture with a dark outline so it reads on any background. */
/* A DVD subtitle: the picture for this moment, drawn over the film in
   the place the disc put it (owner, 2026-09-23). */
static void draw_vobsub(app_state_t *app) {
    if (!app->media || app->vobsub.count == 0 || !app->vobsub_pixels || !app->video_transform_ok) return;
    if (app->vobsub.frame_width <= 0 || app->vobsub.frame_height <= 0) return;

    double when = app->media_position - app->subtitle.offset_seconds;   /* Z / X move these too */
    int32_t index = rubraview_vobsub_at(&app->vobsub, when);
    if (index < 0) return;

    if (index != app->vobsub_texture_cue) {
        /* A new one: decode it, and make a texture of its own size. */
        if (!rubraview_vobsub_decode(&app->vobsub, (size_t)index, (const uint8_t*)app->vobsub_sub.ptr,
                                     app->vobsub_sub.size, app->vobsub_pixels, &app->vobsub_cue)) {
            app->vobsub_texture_cue = index;   /* a torn one: do not try it again every frame */
            if (app->vobsub_texture) { rubraview_pal_texture_destroy(app->vobsub_texture); app->vobsub_texture = NULL; }
            return;
        }
        app->vobsub_texture_cue = index;
        if (!app->vobsub_texture || app->vobsub_texture_w != app->vobsub_cue.width ||
            app->vobsub_texture_h != app->vobsub_cue.height) {
            if (app->vobsub_texture) rubraview_pal_texture_destroy(app->vobsub_texture);
            app->vobsub_texture = rubraview_pal_texture_create_bgra(app->renderer, app->vobsub_cue.width,
                                                                    app->vobsub_cue.height);
            app->vobsub_texture_w = app->vobsub_cue.width;
            app->vobsub_texture_h = app->vobsub_cue.height;
        }
        if (!app->vobsub_texture) return;
        size_t pixels = picture_bytes((size_t)app->vobsub_cue.width, (size_t)app->vobsub_cue.height, 1u, 0);
        proven_result_mem_mut_t mem = rubraview_arena_alloc_array(app->arena, pixels, sizeof(uint32_t));
        if (!proven_is_ok(mem.err)) return;
        uint32_t *bgra = (uint32_t*)mem.value.ptr;
        rubraview_vobsub_pixels(&app->vobsub_cue, bgra);
        rubraview_pal_texture_upload_bgra(app->vobsub_texture, (const uint8_t*)bgra, app->vobsub_cue.width * 4);
    }
    if (!app->vobsub_texture || app->vobsub_cue.width <= 0) return;
    /* The picture's own frame — 720x480 and such — onto the film's. */
    if (app->media_position >= app->vobsub_cue.end_seconds + app->subtitle.offset_seconds) return;
    double sx = (double)app->video_page_w / (double)app->vobsub.frame_width;
    double sy = (double)app->video_page_h / (double)app->vobsub.frame_height;
    rubraview_mat3x2_t place = rubraview_mat3x2_multiply(
        rubraview_mat3x2_scale(sx, sy),
        rubraview_mat3x2_translate((double)app->vobsub_cue.x * sx, (double)app->vobsub_cue.y * sy));
    rubraview_pal_render_draw_texture(app->renderer, app->vobsub_texture,
                                      rubraview_mat3x2_multiply(place, app->video_transform),
                                      RUBRAVIEW_INTERP_LINEAR);
}

/* A Blu-ray subtitle: the same idea as the DVD's above — decode the one
   this moment wants, keep its texture while it is up, and put it where
   the disc put it on its own frame (owner, 2026-09-24). */
static void draw_pgs(app_state_t *app) {
    if (!app->media || app->pgs.count == 0 || !app->pgs_pixels || !app->video_transform_ok) return;
    if (app->pgs.frame_width <= 0 || app->pgs.frame_height <= 0) return;

    double when = app->media_position - app->subtitle.offset_seconds;   /* Z / X move these too */
    int32_t index = rubraview_pgs_at(&app->pgs, when);
    if (index < 0) return;

    if (index != app->pgs_texture_cue) {
        if (!rubraview_pgs_decode(&app->pgs, (size_t)index, (const uint8_t*)app->pgs_bytes.ptr,
                                  app->pgs_bytes.size, app->pgs_pixels, &app->pgs_cue)) {
            app->pgs_texture_cue = index;      /* a torn one: do not try it again every frame */
            if (app->pgs_texture) { rubraview_pal_texture_destroy(app->pgs_texture); app->pgs_texture = NULL; }
            return;
        }
        app->pgs_texture_cue = index;
        if (!app->pgs_texture || app->pgs_texture_w != app->pgs_cue.width ||
            app->pgs_texture_h != app->pgs_cue.height) {
            if (app->pgs_texture) rubraview_pal_texture_destroy(app->pgs_texture);
            app->pgs_texture = rubraview_pal_texture_create_bgra(app->renderer, app->pgs_cue.width,
                                                                 app->pgs_cue.height);
            app->pgs_texture_w = app->pgs_cue.width;
            app->pgs_texture_h = app->pgs_cue.height;
        }
        if (!app->pgs_texture) return;
        size_t pixels = picture_bytes((size_t)app->pgs_cue.width, (size_t)app->pgs_cue.height, 1u, 0);
        proven_result_mem_mut_t mem = rubraview_arena_alloc_array(app->arena, pixels, sizeof(uint32_t));
        if (!proven_is_ok(mem.err)) return;
        uint32_t *bgra = (uint32_t*)mem.value.ptr;
        rubraview_pgs_pixels(&app->pgs_cue, bgra);
        rubraview_pal_texture_upload_bgra(app->pgs_texture, (const uint8_t*)bgra, app->pgs_cue.width * 4);
    }
    if (!app->pgs_texture || app->pgs_cue.width <= 0) return;
    if (app->media_position >= app->pgs_cue.end_seconds + app->subtitle.offset_seconds) return;
    double sx = (double)app->video_page_w / (double)app->pgs.frame_width;
    double sy = (double)app->video_page_h / (double)app->pgs.frame_height;
    rubraview_mat3x2_t place = rubraview_mat3x2_multiply(
        rubraview_mat3x2_scale(sx, sy),
        rubraview_mat3x2_translate((double)app->pgs_cue.x * sx, (double)app->pgs_cue.y * sy));
    rubraview_pal_render_draw_texture(app->renderer, app->pgs_texture,
                                      rubraview_mat3x2_multiply(place, app->video_transform),
                                      RUBRAVIEW_INTERP_LINEAR);
}


/* The subtitle size in pixels: §3.22.2.5's points, with the window's own
   height still having a say, so a subtitle is not a speck on a large
   screen nor a banner on a small one. */
static double subtitle_window_scale(double win_h) {
    double scale = win_h / 720.0;
    return scale < 0.6 ? 0.6 : (scale > 2.5 ? 2.5 : scale);
}

static double subtitle_font_px(app_state_t *app, double win_h) {
    double wanted = rubraview_settings_get(&app->settings, PROVEN_LIT("video"), PROVEN_LIT("subtitle_size"));
    if (wanted <= 0.0) wanted = 24.0;
    return wanted * rubraview_pal_window_dpi_scale(app->window) * subtitle_window_scale(win_h);
}

/* The text subtitle on screen now, or NULL. */
static const rubraview_subtitle_cue_t *subtitle_cue_now(app_state_t *app) {
    if (!app->media || app->subtitle.count == 0) return NULL;
    const rubraview_subtitle_cue_t *cue = rubraview_subtitle_at(&app->subtitle, app->media_position);
    return (cue && cue->text.size > 0) ? cue : NULL;
}

/* D-92: where the box is and what it does with a long subtitle, from
   Settings > Video. A box dragged into place before 0.0.47 (layout.ini's
   left edge, bottom edge and width) becomes these settings once. */
rubraview_subbox_place_t subbox_place(app_state_t *app) {
    rubraview_settings_t *st = &app->settings;
    if (app->subbox.placed) {
        app->subbox.placed = false;
        rubraview_settings_set(st, PROVEN_LIT("video"), PROVEN_LIT("subtitle_align"), (double)RUBRAVIEW_SUBBOX_ALIGN_MIDDLE);
        rubraview_settings_set(st, PROVEN_LIT("video"), PROVEN_LIT("subtitle_valign"), (double)RUBRAVIEW_SUBBOX_ALIGN_END);
        rubraview_settings_set(st, PROVEN_LIT("video"), PROVEN_LIT("subtitle_x"), floor((app->subbox.left + app->subbox.width * 0.5) * 100.0 + 0.5));
        rubraview_settings_set(st, PROVEN_LIT("video"), PROVEN_LIT("subtitle_y"), floor(app->subbox.bottom * 100.0 + 0.5));
        rubraview_settings_set(st, PROVEN_LIT("video"), PROVEN_LIT("subtitle_width"), floor(app->subbox.width * 100.0 + 0.5));
        settings_took_effect(app);
    }
    return (rubraview_subbox_place_t){
        .x = rubraview_settings_get(st, PROVEN_LIT("video"), PROVEN_LIT("subtitle_x")) / 100.0,
        .y = rubraview_settings_get(st, PROVEN_LIT("video"), PROVEN_LIT("subtitle_y")) / 100.0,
        .width = rubraview_settings_get(st, PROVEN_LIT("video"), PROVEN_LIT("subtitle_width")) / 100.0,
        .height = rubraview_settings_get(st, PROVEN_LIT("video"), PROVEN_LIT("subtitle_height")) / 100.0,
        .align = (int)lround(rubraview_settings_get(st, PROVEN_LIT("video"), PROVEN_LIT("subtitle_align"))),
        .valign = (int)lround(rubraview_settings_get(st, PROVEN_LIT("video"), PROVEN_LIT("subtitle_valign"))),
        .fit = rubraview_settings_get(st, PROVEN_LIT("video"), PROVEN_LIT("subtitle_fit")) > 0.5 ? RUBRAVIEW_SUBBOX_FIT_SHRINK
                                                                                 : RUBRAVIEW_SUBBOX_FIT_GROW,
    };
}

struct subbox_measure { rubraview_renderer_t *renderer; proven_u8str_view_t text; };

static bool subbox_measure_text(void *context, double font, double wrap_width, double *out_width, double *out_height) {
    struct subbox_measure *m = (struct subbox_measure*)context;
    return rubraview_pal_render_measure_text_wrapped(m->renderer, m->text, font, wrap_width, out_width, out_height);
}

/* RV-107: Settings > Video > Subtitle font. Set while a subtitle is
   measured and drawn, and taken back: the rest of the program keeps its face. */
static void subtitle_face(app_state_t *app, bool on) {
    rubraview_font_spec_t face = rubraview_font_spec_parse(on ? rubraview_settings_get_text(&app->settings, PROVEN_LIT("video"), PROVEN_LIT("subtitle_font")) : PROVEN_LIT(""));
    rubraview_pal_render_set_text_font(app->renderer, face.family, face.weight, face.italic);   /* RV-136: its weight and italic too */
}

/* The box and the text in it, for the subtitle showing now (or the box
   alone). The whole window is its room, the margins above and under a
   film too (D-93, owner 2026-10-11: "자막이 영상 바깥으로 나갈 수 있게") —
   only the filmstrip, when it is up, is kept clear. Measuring text is not
   free and the answer only changes with the subtitle, the window or a
   setting: the last one is kept. */
static rubraview_subbox_layout_t subbox_layout(app_state_t *app, double win_w, double win_h) {
    static struct {
        bool valid;
        const rubraview_subtitle_cue_t *cue;
        const char *text;
        size_t text_len;
        uint32_t generation;   /* RFC-0006: the subtitle's memory emptied since is another text at the same place */
        double win_w, win_h, font, limit;
        unsigned style;
        rubraview_subbox_place_t place;
        rubraview_subbox_layout_t layout;
    } kept;
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double font = subtitle_font_px(app, win_h);
    double limit = win_h - (app->filmstrip.visible ? FILMSTRIP_THUMB : 0.0);
    rubraview_subbox_place_t place = subbox_place(app);
    const rubraview_subtitle_cue_t *cue = subtitle_cue_now(app);
    if (kept.valid && kept.cue == cue && kept.generation == g_subs_generation && kept.text == (cue ? (const char *)cue->text.ptr : NULL) &&
        kept.text_len == (cue ? cue->text.size : 0) && kept.win_w == win_w &&
        kept.win_h == win_h && kept.font == font && kept.limit == limit && kept.style == g_subtitle_style &&
        kept.place.x == place.x && kept.place.y == place.y && kept.place.width == place.width &&
        kept.place.height == place.height && kept.place.align == place.align &&
        kept.place.valign == place.valign && kept.place.fit == place.fit) {
        return kept.layout;
    }
    struct subbox_measure measure = { .renderer = app->renderer, .text = cue ? cue->text : PROVEN_LIT("") };
    subtitle_face(app, true);
    /* No smaller than a size that can still be read: ten points' worth here. */
    double min_font = 10.0 * dpi * subtitle_window_scale(win_h);
    kept.layout = rubraview_subbox_layout(&place, win_w, win_h, limit, font, min_font,
                                          cue ? subbox_measure_text : NULL, &measure);
    subtitle_face(app, false);
    kept.valid = true;
    kept.cue = cue;
    kept.text = cue ? (const char *)cue->text.ptr : NULL;
    kept.text_len = cue ? cue->text.size : 0;
    kept.generation = g_subs_generation;
    kept.win_w = win_w; kept.win_h = win_h; kept.font = font; kept.limit = limit; kept.style = g_subtitle_style;
    kept.place = place;
    return kept.layout;
}

rubraview_rect_t subbox_geometry(app_state_t *app, double win_w, double win_h, double *out_font) {
    rubraview_subbox_layout_t layout = subbox_layout(app, win_w, win_h);
    if (out_font) *out_font = layout.font;
    return layout.box;
}

/* A subtitle box can be touched while it shows text, or once selected. */
bool subbox_live(app_state_t *app) {
    return app->media && app->tracks.current_subtitle >= 0 &&
           (app->subbox.selected || subtitle_cue_now(app) != NULL);
}

void draw_subtitle(app_state_t *app, int32_t win_w, int32_t win_h) {
    /* A DVD's or a Blu-ray's pictures stay where the disc put them. */
    draw_vobsub(app);
    draw_pgs(app);
    const rubraview_subtitle_cue_t *cue = subtitle_cue_now(app);
    if (!app->media || app->tracks.current_subtitle < 0) return;
    if (!cue && !app->subbox.selected) return;   /* an empty box draws nothing */

    double dpi = rubraview_pal_window_dpi_scale(app->window);
    rubraview_subbox_layout_t placed = subbox_layout(app, (double)win_w, (double)win_h);
    double font = placed.font;
    rubraview_rect_t box = placed.box;
    rubraview_pal_rect_t body = { box.x, box.y, box.width, box.height };

    /* The translucent pane: `video.subtitle_background` percent of black. */
    double percent = rubraview_settings_get(&app->settings, PROVEN_LIT("video"), PROVEN_LIT("subtitle_background"));
    if (percent < 0.0) percent = 0.0;
    if (percent > 100.0) percent = 100.0;
    uint32_t alpha = (uint32_t)(percent * 2.55 + 0.5);
    if (alpha > 0) rubraview_pal_render_fill_rect(app->renderer, body, alpha << 24, 6.0 * dpi);

    if (app->subbox.selected) {
        /* Selected: a thick edge, and S M R X above the top-right corner. */
        rubraview_pal_render_stroke_rect(app->renderer, body, COLOR_TILE_CURRENT, 3.0 * dpi, 6.0 * dpi);
        rubraview_rect_t buttons[RUBRAVIEW_SUBBOX_BUTTONS];
        rubraview_subbox_buttons(box, SUBBOX_BUTTON * dpi, SUBBOX_GAP * dpi, buttons);
        static const char *const LETTERS[RUBRAVIEW_SUBBOX_BUTTONS] = { "S", "M", "R", "X" };
        for (int i = 0; i < RUBRAVIEW_SUBBOX_BUTTONS; ++i) {
            rubraview_pal_rect_t b = { buttons[i].x, buttons[i].y, buttons[i].width, buttons[i].height };
            bool dragging = (i == 1 && app->subbox.dragging == RUBRAVIEW_SUBBOX_MOVE) ||
                            (i == 2 && app->subbox.dragging == RUBRAVIEW_SUBBOX_RESIZE);
            rubraview_pal_render_fill_rect(app->renderer, b, dragging ? COLOR_TILE_CURRENT : COLOR_BOX_FILL, 4.0 * dpi);
            rubraview_pal_render_stroke_rect(app->renderer, b, i == 3 ? COLOR_CLOSE_HOVER : COLOR_TILE_CURRENT,
                                             1.5 * dpi, 4.0 * dpi);
            rubraview_pal_render_draw_text(app->renderer, cstr(LETTERS[i]), b, b.height * 0.5,
                                           COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
    }
    if (!cue) return;

    /* D-92: the box was laid out to hold this text; it is drawn where the
       layout put it, aligned as the box is anchored. */
    rubraview_pal_rect_t rect = { placed.text.x, placed.text.y, placed.text.width, placed.text.height };
    int across = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("video"), PROVEN_LIT("subtitle_align")));
    rubraview_text_align_t align = across == RUBRAVIEW_SUBBOX_ALIGN_START ? RUBRAVIEW_TEXT_LEFT
                                 : across == RUBRAVIEW_SUBBOX_ALIGN_END ? RUBRAVIEW_TEXT_RIGHT : RUBRAVIEW_TEXT_CENTER;

    /* R135 asks for a two-pixel outline, and §3.22.2.5 lets the reader
       change it (0 turns it off). */
    double outline = rubraview_settings_get(&app->settings, PROVEN_LIT("video"), PROVEN_LIT("subtitle_outline")) * dpi;
    /* A font shrunk to fit keeps an outline in proportion, not one that fills its letters. */
    double asked = subtitle_font_px(app, (double)win_h);
    if (asked > 0.0 && font < asked) outline *= font / asked;
    subtitle_face(app, true);
    draw_outlined_text_aligned(app->renderer, cue->text, rect, font, outline, g_subtitle_argb, align);
    subtitle_face(app, false);
}

/* The list a double tap or a held press on the Sub tile opens (D-33). */
static void draw_sub_list(app_state_t *app) {
    if (!app->sub_list_open) return;
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    size_t rows = 0;
    rubraview_rect_t list = sub_list_rect(app, &rows);
    double row_h = SUB_LIST_ROW * dpi;
    rubraview_pal_rect_t frame = { list.x, list.y, list.width, list.height };
    rubraview_pal_render_fill_rect(app->renderer, frame, 0xFA1A1A1Au, 4.0 * dpi);
    rubraview_pal_render_stroke_rect(app->renderer, frame, COLOR_TILE_CURRENT, 1.5 * dpi, 4.0 * dpi);
    rubraview_pal_rect_t title = { list.x, list.y, list.width, row_h };
    rubraview_pal_render_draw_text(app->renderer, TU8("Subtitles"), title, row_h * 0.4, 0xB0F0F0F0u,
                                   RUBRAVIEW_TEXT_CENTER);

    int32_t tracks[SUB_LIST_MAX];
    size_t n = subtitle_track_indices(app, tracks, SUB_LIST_MAX);
    int32_t hover = rubraview_choice_list_row_at(list, row_h, rows, app->pointer_x, app->pointer_y);
    for (size_t r = 0; r < rows; ++r) {
        rubraview_pal_rect_t cell = { list.x + 4.0 * dpi, list.y + row_h * (double)(r + 1) + 2.0 * dpi,
                                      list.width - 8.0 * dpi, row_h - 4.0 * dpi };
        bool current = r == 0 ? app->tracks.current_subtitle < 0
                              : (r - 1 < n && tracks[r - 1] == app->tracks.current_subtitle);
        if ((int32_t)r == hover) rubraview_pal_render_fill_rect(app->renderer, cell, 0x40FFFFFFu, 3.0 * dpi);
        if (current) rubraview_pal_render_stroke_rect(app->renderer, cell, COLOR_TILE_CURRENT, 2.0 * dpi, 3.0 * dpi);
        char label[160];
        proven_u8str_view_t text = r == 0 ? TU8("Off")
                              : (r - 1 < n ? rubraview_track_label(label, sizeof(label), &app->tracks, tracks[r - 1]) : PROVEN_LIT(""));
        rubraview_pal_render_draw_text(app->renderer, text, cell, row_h * 0.4, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
}

/* 1.25.0 (owner, 2026-10-20: "아무 옵션 없이 그냥 rubraview가 시작하면 rubraview를 알리는 이미지를 빈 배경 중앙에
   가로 폭의 30% 정도 크기의 정사각형으로"): the program's icon, its largest, in the middle of an empty window. */
static void draw_logo(app_state_t *app, int32_t win_w, int32_t win_h) {
    enum { LOGO = 256 };
    if (app->logo_tex && app->logo_back != COLOR_CANVAS) {   /* the ground changed in Settings: laid again */
        rubraview_pal_texture_destroy(app->logo_tex);
        app->logo_tex = NULL;
        app->logo_failed = false;
    }
    if (!app->logo_tex && !app->logo_failed) {
        app->logo_back = COLOR_CANVAS;
        static uint8_t px[LOGO * LOGO * 4];
        app->logo_failed = true;
        if (rubraview_pal_app_icon_bgra(px, LOGO)) {
            uint32_t back = COLOR_CANVAS;   /* laid on the window's own ground: no alpha left to blend */
            for (size_t i = 0; i < (size_t)LOGO * LOGO; ++i) {
                uint8_t *p = px + i * 4;
                unsigned a = p[3], rest = 255u - a;
                p[0] = (uint8_t)((p[0] * a + (back & 0xFFu) * rest + 127u) / 255u);
                p[1] = (uint8_t)((p[1] * a + ((back >> 8) & 0xFFu) * rest + 127u) / 255u);
                p[2] = (uint8_t)((p[2] * a + ((back >> 16) & 0xFFu) * rest + 127u) / 255u);
                p[3] = 255;
            }
            app->logo_tex = rubraview_pal_texture_create_bgra(app->renderer, LOGO, LOGO);
            if (app->logo_tex && rubraview_pal_texture_upload_bgra(app->logo_tex, px, LOGO * 4)) app->logo_failed = false;
            else if (app->logo_tex) { rubraview_pal_texture_destroy(app->logo_tex); app->logo_tex = NULL; }
        }
    }
    if (!app->logo_tex) return;
    double side = (double)win_w * 0.30;
    if (side > (double)win_h * 0.6) side = (double)win_h * 0.6;
    double k = side / (double)LOGO;
    rubraview_pal_render_draw_texture(app->renderer, app->logo_tex,
        rubraview_mat3x2_multiply(rubraview_mat3x2_scale(k, k),
                                  rubraview_mat3x2_translate(((double)win_w - side) * 0.5, ((double)win_h - side) * 0.5)),
        RUBRAVIEW_INTERP_HIGH_QUALITY_CUBIC);
}

void render_frame(app_state_t *app) {
    boxes_apply_size(app);
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) return;

    /* RV-153: the window goes out as HDR while a film made for an HDR screen is the page shown */
    if (app->media && app->media_info.hdr_output) {
        bool want = current_page_index(app) == app->media_page;
        const char *why = NULL;
        if (!rubraview_pal_render_set_hdr(app->renderer, want, app->media_hdr_screen.sdr_white_nits, &why) && want) {
            app->hdr_output_why = why;
            app->hdr_output_refused = true;   /* reopened for SDR by hdr_screen_tick */
        }
    } else {
        rubraview_pal_render_set_hdr(app->renderer, false, 0.0, NULL);
    }
    rubraview_pal_render_begin(app->renderer, COLOR_CANVAS);

    draw_music_backdrop(app, win_w, win_h);

    if (text_active(app)) {
        draw_text_page(app, win_w, win_h);   /* RFC-0005 */
    } else if (page_count(app) == 0 && !app->media && !app->picker_open) {
        draw_logo(app, win_w, win_h);   /* 1.25.0 */
    } else if (app->layout.count > 0) {
        /* §3.2.5: while a transition runs, both spreads are on screen —
           the old one fading out under the new one fading in. M3 had the
           timing but no way to draw it; the device context (RV-064) is
           what changed. */
        double progress = rubraview_transition_progress(&app->transition);
        bool fading = progress < 1.0 && app->prev_spread_valid &&
                      app->prev_spread_index != app->spread_index;

        /* A far page on its way: the page before it stays on screen, with
           how far the reading has got (owner, 2026-09-29). */
        bool waiting = app->page_job && app->prev_spread_valid && app->prev_spread_index < app->layout.count &&
                       (app->page_job->index == (size_t)app->layout.spreads[app->spread_index].left_index ||
                        app->page_job->index == (size_t)app->layout.spreads[app->spread_index].right_index);
        if (waiting) {
            draw_spread(app, app->prev_spread_index, 1.0, win_w, win_h);
            draw_spread(app, app->spread_index, 1.0, win_w, win_h);   /* asks for its pages; nothing to draw yet */
            uint64_t done = 0, total = 0;
            rubraview_page_source_progress(&app->source, &done, &total);
            char line[96];
            int n = snprintf(line, sizeof(line), T("reading page %zu... %d %%"), app->page_job->index + 1,
                             total > 0 ? (int)(done * 100u / total) : 0);
            if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
        } else if (fading) {
            rubraview_transition_pose_t leaving, arriving;
            rubraview_transition_poses(app->transition.kind, progress, (double)win_w,
                                       app->spread_index > app->prev_spread_index, &leaving, &arriving);
            g_pose_on = true;
            g_pose = leaving;
            if (leaving.opacity > 0.0) draw_spread(app, app->prev_spread_index, leaving.opacity, win_w, win_h);
            g_pose = arriving;
            draw_spread(app, app->spread_index, arriving.opacity, win_w, win_h);
            g_pose_on = false;
        } else if (strip_active(app)) {
            draw_strip(app, win_w, win_h);
        } else {
            draw_spread(app, app->spread_index, 1.0, win_w, win_h);
        }
    }

    draw_music_words(app, win_w, win_h);
    draw_music_extras(app, win_w, win_h);

    list_filter_follow(app);         /* 1.25.0: the list's rows follow its filter as it is typed */
    if (app->picker_open) {
        picker_filter_follow(app);   /* 1.22.0: the rows follow the filter as it is typed */
        draw_picker(app, (double)win_w, (double)win_h);
        draw_favwin(app, (double)win_w, (double)win_h);   /* 1.24.0: opened from its chip */
        draw_ctxmenu(app);           /* 1.22.0: a tile's right click */
    } else {
        edit_preview_update(app);
        draw_edit_preview(app);
        draw_crop_overlay(app);
        draw_chrome(app, (double)win_w, (double)win_h);
        draw_sub_list(app);   /* D-33: on top of every box */
        draw_ctxmenu(app);    /* RV-116 */
        draw_slidepop(app);
        draw_hint(app, (double)win_w, (double)win_h);
        draw_seekfan(app, app->renderer, false);
        draw_chrome_answers(app, (double)win_w, (double)win_h);
        draw_print_preview(app);   /* 1.19.0: under the panel, over the rest */
        draw_panel(app);
        if (!app->edit_window) draw_curve_widget(app);
        /* 1.24.0: a value typed for a panel's slider sits over the panel and the print preview */
        if (app->panel.open && app->rename_active && app->rename_is_value && !app->edit_window) {
            double cd = rubraview_pal_window_dpi_scale(app->window);
            draw_rename_box(app, (double)win_w, (double)win_h, cd);
        }
    }
    draw_chooser(app, app->renderer, CHOOSE_IN_MAIN, app->pointer_x, app->pointer_y);   /* 1.24.0: over everything */

    if (!rubraview_pal_render_end(app->renderer)) {
        /* Every texture made on the lost device is dead, not only the
           pages': drawing one would fail the next frame too. Each of these
           is made again on demand. */
        unload_all_pages(app);
        if (app->logo_tex) { rubraview_pal_texture_destroy(app->logo_tex); app->logo_tex = NULL; app->logo_failed = false; }
        if (app->picker_thumbs) {
            picker_thumbs_free(app);
            app->picker_generation++;   /* the picker asks for its tiles again */
            if (app->thumbs) rubraview_pal_thumbs_generation(app->thumbs, app->picker_generation);
            if (app->picker_open) {
                app->picker_thumbs = (struct picker_thumb *)calloc(app->picker_listing.count ? app->picker_listing.count : 1,
                                                                   sizeof(*app->picker_thumbs));
                app->picker_thumb_count = app->picker_thumbs ? app->picker_listing.count : 0;
            }
        }
        if (app->edit_preview) {
            rubraview_pal_texture_destroy(app->edit_preview);
            app->edit_preview = NULL;
            app->edit_shown_valid = false;   /* the next pass uploads it again */
        }
        if (app->vobsub_texture) { rubraview_pal_texture_destroy(app->vobsub_texture); app->vobsub_texture = NULL; app->vobsub_texture_cue = -1; }
        if (app->pgs_texture) { rubraview_pal_texture_destroy(app->pgs_texture); app->pgs_texture = NULL; app->pgs_texture_cue = -1; }
        if (app->music_backdrop) {
            rubraview_pal_texture_destroy(app->music_backdrop);
            app->music_backdrop = NULL;
            if (app->music_tags_read) music_make_backdrop(app, &app->music_tags);
        }
        media_reopen_on_new_device(app);
    }
}

/* ---- redrawing only what changed ---- */

static void sig_mix(uint64_t *h, const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < len; ++i) { *h ^= p[i]; *h *= 0x100000001B3ull; }   /* FNV-1a */
}
#define SIG(h, v) do { typeof(v) sig_value_ = (v); sig_mix((h), &sig_value_, sizeof(sig_value_)); } while (0)

static void sig_page(uint64_t *h, const app_state_t *app, int32_t index) {
    SIG(h, index);
    if (index < 0 || (size_t)index >= page_count(app) || !app->pages) return;
    const app_page_t *page = &app->pages[index];
    SIG(h, page->loaded); SIG(h, page->failed); SIG(h, page->reduced);
    SIG(h, page->width); SIG(h, page->height); SIG(h, (uintptr_t)page->texture);
}

/* What the settled screen shows, summed up: once the loop has settled, a
   frame is drawn when this changes, not once a second regardless. On a
   machine without a graphics card each frame of a large picture costs a
   third of a core (measured 2026-09-29, 10 s of CPU in 30 s on the VM). */
uint64_t scene_signature(const app_state_t *app) {
    uint64_t h = 0xCBF29CE484222325ull;
    int32_t w = 0, hh = 0;
    rubraview_pal_window_get_size(app->window, &w, &hh);
    SIG(&h, w); SIG(&h, hh);
    SIG(&h, app->spread_index); SIG(&h, page_count(app));
    if (app->spread_index < app->layout.count) {
        sig_page(&h, app, app->layout.spreads[app->spread_index].left_index);
        sig_page(&h, app, app->layout.spreads[app->spread_index].right_index);
    }
    SIG(&h, app->zoom); SIG(&h, app->pan_x); SIG(&h, app->pan_y); SIG(&h, (int)app->fit_mode);
    SIG(&h, app->titlebar.shown); SIG(&h, (int)app->toolbox.state); SIG(&h, (int)app->menubox.state);
    SIG(&h, app->toolbox.tile_count); SIG(&h, app->menubox.tile_count);
    SIG(&h, (int)(rubraview_osd_opacity(&app->osd) * 20.0)); SIG(&h, app->osd.always_on);
    SIG(&h, app->notice_seconds > 0.0);
    SIG(&h, (uintptr_t)app->media); SIG(&h, app->media_paused); SIG(&h, app->media_ended);
    SIG(&h, (int64_t)app->media_position);   /* the time the toolbox shows, to the second */
    SIG(&h, app->listwin.open); SIG(&h, app->listwin.first);
    if (app->listwin.open) {   /* D-111: its buttons light under the pointer, rows are checked and carried */
        SIG(&h, (int64_t)app->listwin.x); SIG(&h, (int64_t)app->listwin.y);
        SIG(&h, (int64_t)app->listwin.width); SIG(&h, (int64_t)app->listwin.height);
        SIG(&h, (int64_t)app->pointer_x); SIG(&h, (int64_t)app->pointer_y);
        SIG(&h, app->list_row_dragging); SIG(&h, app->list_drop_at);
        for (size_t i = 0; i < app->list_checked_count; ++i) if (app->list_checked[i]) SIG(&h, i);
        SIG(&h, list_saves_whole_paths(app));
    }
    if (app->eqwin.open) {   /* the sliders move, and the pointer lights what it is over */
        SIG(&h, (int64_t)app->eqwin.x); SIG(&h, (int64_t)app->eqwin.y); SIG(&h, app->eqwin.sliding);
        SIG(&h, eq_preset_now(app));
        for (size_t b = 0; b < RUBRAVIEW_EQ_BANDS; ++b) SIG(&h, (int64_t)lround(eq_band_db(app, b) * 2.0));
        SIG(&h, (int64_t)app->pointer_x); SIG(&h, (int64_t)app->pointer_y);
    }
    SIG(&h, hint_shown(app)); SIG(&h, (uintptr_t)app->hint.ptr);
    SIG(&h, app->tmenu_open); SIG(&h, app->tmenu_focus);   /* 1.25.0 */
    SIG(&h, app->chooser.open); SIG(&h, app->chooser.focus); SIG(&h, app->chooser.layout.first);   /* 1.24.0 */
    SIG(&h, app->ctx_open); SIG(&h, app->ctx_focus); SIG(&h, app->slidepop_open); SIG(&h, app->slidepop_typing);
    if (app->ctx_open || app->slidepop_open) {   /* RV-116: a row or a button lights under the pointer */
        SIG(&h, (int64_t)app->pointer_x); SIG(&h, (int64_t)app->pointer_y);
        SIG(&h, app->slidepop_len); SIG(&h, (int64_t)lround(app->slideshow.interval_seconds * 10.0));
        SIG(&h, app->slideshow.paused); SIG(&h, (int)app->slideshow.loop);
        SIG(&h, (int64_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("transition"))));
        SIG(&h, app->ctx_confirm.until);
    }
    SIG(&h, app->pointer_inside); SIG(&h, app->cursor.hidden);
    SIG(&h, app->picker_open); SIG(&h, app->settings_open); SIG(&h, app->panel.open);
    SIG(&h, app->chrome_hidden); SIG(&h, (int64_t)app->picker.columns);
    SIG(&h, app->page_job != NULL); SIG(&h, app->resume_offer); SIG(&h, app->confirm_purge); SIG(&h, app->confirm_restart);
    SIG(&h, app->arc_job != NULL); SIG(&h, app->arc_ask);
    if (app->arc_job) SIG(&h, atomic_load(&app->arc_job->job.done));
    SIG(&h, rubraview_pal_window_is_fullscreen(app->window));
    return h;
}
#undef SIG
