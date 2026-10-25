/*
 * The lists' scroll bars (RV-170, owner 2026-10-25: "클릭을 하거나 스크롤바를 드래그해도 목록에서 이동 가능하게";
 * "위아래 이동 버튼은 아래쪽에 한곳에 몰아서"). Each list says where its bar is and where it stands (`scrollbar_of`)
 * and is moved (`scrollbar_to`); the press, the drag, the held track or button and the look are the same for all.
 */
#include "app.h"

/* The bar's width, and its two buttons' height, at a window's scale. */
static double bar_width(double dpi) { return 12.0 * dpi; }

/* A floating list's bar, at its right edge under its head. */
static rubraview_scrollbar_t list_bar(const rubraview_listwin_t *lw, size_t count, double head_h, double dpi) {
    double w = bar_width(dpi);
    return (rubraview_scrollbar_t){
        .area = { lw->x + lw->width - w - 2.0 * dpi, lw->y + head_h + 2.0 * dpi, w, lw->height - head_h - 4.0 * dpi },
        .button = w, .total = (double)count, .shown = (double)rubraview_listwin_rows_visible(lw),
        .first = (double)lw->first, .step = 1.0, .min_thumb = 16.0 * dpi,
    };
}

static size_t whole(double first) { return first > 0.0 ? (size_t)(first + 0.5) : 0; }

static rubraview_window_t *scrollbar_window(app_state_t *app, int which) {
    switch (which) {
        case RVSB_LIST: case RVSB_RECENT: case RVSB_FAVS: case RVSB_PICKER: return app->window;
        case RVSB_CHOOSER:  return chooser_sb_window(app);
        case RVSB_FILES:    return app->files_window;
        case RVSB_SUBS:     return app->subs_window;
        case RVSB_SETTINGS: return app->settings_window;
        case RVSB_FONTPICK: return app->fontpick_window;
        case RVSB_INFO:     return app->info_window;
        default:          return NULL;
    }
}

bool scrollbar_of(app_state_t *app, int which, rubraview_scrollbar_t *sb) {
    *sb = (rubraview_scrollbar_t){ 0 };
    rubraview_window_t *win = scrollbar_window(app, which);
    if (!win) return false;
    double dpi = rubraview_pal_window_dpi_scale(win);
    switch (which) {
        case RVSB_LIST:
            if (!app->listwin.open) return false;
            *sb = list_bar(&app->listwin, list_rows(app), rubraview_listwin_head_height(&app->listwin), dpi);
            break;
        case RVSB_FILES:
            if (!app->files_open) return false;
            *sb = list_bar(&app->files_list, list_rows(app), rubraview_listwin_head_height(&app->files_list), dpi);
            break;
        case RVSB_RECENT:
            if (!app->recentwin.open) return false;
            *sb = list_bar(&app->recentwin, app->recent_row_count, rubraview_listwin_head_height(&app->recentwin), dpi);
            break;
        case RVSB_FAVS:
            if (!app->favwin.open) return false;
            *sb = list_bar(&app->favwin, app->favorites.count, rubraview_listwin_head_height(&app->favwin), dpi);
            break;
        case RVSB_SUBS:
            if (!app->subs_open) return false;
            *sb = list_bar(&app->subs_list, app->subs_count, app->subs_list.title_height, dpi);
            break;
        case RVSB_PICKER: {
            if (!app->picker_open || !app->picker.listing || app->picker.columns < 1) return false;
            picker_layout(app);
            int32_t w = 0, h = 0;
            rubraview_pal_window_get_size(app->window, &w, &h);
            size_t rows = (app->picker.listing->count + (size_t)app->picker.columns - 1) / (size_t)app->picker.columns;
            double bw = bar_width(dpi), top = picker_grid_top(app, dpi);
            *sb = (rubraview_scrollbar_t){
                .area = { (double)w - bw - 2.0 * dpi, top + 2.0 * dpi, bw, app->picker.viewport_extent - 4.0 * dpi },
                .button = bw, .total = (double)rows * app->picker.tile_extent, .shown = app->picker.viewport_extent,
                .first = app->picker.scroll_offset, .step = app->picker.tile_extent, .min_thumb = 16.0 * dpi,
            };
            break;
        }
        case RVSB_CHOOSER:  if (!chooser_sb(app, sb, bar_width(dpi), dpi)) return false; break;
        case RVSB_SETTINGS: if (!settings_sb(app, sb, bar_width(dpi), dpi)) return false; break;
        case RVSB_FONTPICK: if (!fontpick_sb(app, sb, bar_width(dpi), dpi)) return false; break;
        case RVSB_INFO:     if (!info_sb(app, sb, bar_width(dpi), dpi)) return false; break;
        default: return false;
    }
    return rubraview_scrollbar_needed(sb);
}

static void scrollbar_to(app_state_t *app, int which, double first) {
    switch (which) {
        case RVSB_LIST:   app->listwin.first = whole(first); break;
        case RVSB_RECENT: app->recentwin.first = whole(first); break;
        case RVSB_FAVS:   app->favwin.first = whole(first); break;
        case RVSB_FILES:  app->files_list.first = whole(first); app->files_dirty = true; break;
        case RVSB_SUBS:
            app->subs_list.first = whole(first);
            app->subs_scrolled_at = rubraview_pal_time_now_seconds();   /* the reader moved it: it does not follow */
            app->subs_dirty = true;
            break;
        case RVSB_PICKER:
            app->picker.scroll_offset = first;
            rubraview_picker_scroll_by(&app->picker, 0.0);
            break;
        case RVSB_CHOOSER:  chooser_sb_to(app, whole(first)); break;
        case RVSB_SETTINGS: settings_sb_to(app, whole(first)); break;
        case RVSB_FONTPICK: fontpick_sb_to(app, whole(first)); break;
        case RVSB_INFO:     info_sb_to(app, whole(first)); break;
        default: break;
    }
    if (which == RVSB_LIST || which == RVSB_RECENT || which == RVSB_FAVS || which == RVSB_PICKER) note_activity(app);
}

bool scrollbar_press(app_state_t *app, int which, double x, double y) {
    rubraview_scrollbar_t sb;
    if (!scrollbar_of(app, which, &sb)) return false;
    rubraview_scrollbar_hold_t hold;
    double first = rubraview_scrollbar_press(&sb, &hold, x, y, rubraview_pal_time_now_seconds());
    if (hold.part == RUBRAVIEW_SCROLLBAR_NONE) return false;
    app->sb_which = which;
    app->sb_hold = hold;
    app->sb_x = x;
    app->sb_y = y;
    scrollbar_to(app, which, first);
    return true;
}

/* Each pass while held: the thumb follows, the track and the buttons go on. */
static void scrollbar_step(app_state_t *app) {
    rubraview_scrollbar_t sb;
    if (!scrollbar_of(app, app->sb_which, &sb)) { app->sb_which = RVSB_NONE; return; }
    double first = rubraview_scrollbar_held(&sb, &app->sb_hold, app->sb_x, app->sb_y, rubraview_pal_time_now_seconds());
    if (first != sb.first) scrollbar_to(app, app->sb_which, first);
}

bool scrollbar_motion(app_state_t *app, rubraview_window_t *win, double x, double y) {
    if (app->sb_which == RVSB_NONE || scrollbar_window(app, app->sb_which) != win) return false;
    app->sb_x = x;
    app->sb_y = y;
    if (app->sb_hold.part == RUBRAVIEW_SCROLLBAR_THUMB) scrollbar_step(app);
    return true;
}

bool scrollbar_release(app_state_t *app, rubraview_window_t *win) {
    if (app->sb_which == RVSB_NONE || scrollbar_window(app, app->sb_which) != win) return false;
    int which = app->sb_which;
    app->sb_which = RVSB_NONE;
    rubraview_scrollbar_t sb;
    if (scrollbar_of(app, which, &sb)) scrollbar_to(app, which, sb.first);   /* drawn again, no longer lit */
    return true;
}

void scrollbar_tick(app_state_t *app) {
    if (app->sb_which != RVSB_NONE && app->sb_hold.part != RUBRAVIEW_SCROLLBAR_THUMB) scrollbar_step(app);
}

bool scrollbar_held(const app_state_t *app) { return app->sb_which != RVSB_NONE; }

/* The pointer moved over a window: true when the part of `which`'s bar under it is another one (it is lit). */
bool scrollbar_hover(app_state_t *app, int which, double x, double y) {
    static rubraview_scrollbar_part_t was[RVSB_INFO + 1];
    if (which <= RVSB_NONE || which > RVSB_INFO) return false;
    rubraview_scrollbar_t sb;
    rubraview_scrollbar_part_t now = scrollbar_of(app, which, &sb) ? rubraview_scrollbar_hit(&sb, x, y) : RUBRAVIEW_SCROLLBAR_NONE;
    bool changed = now != was[which];
    was[which] = now;
    return changed;
}

void draw_scrollbar(app_state_t *app, rubraview_renderer_t *r, int which, double px, double py) {
    rubraview_scrollbar_t sb;
    if (!r || !scrollbar_of(app, which, &sb)) return;
    double dpi = rubraview_pal_window_dpi_scale(scrollbar_window(app, which));
    rubraview_scrollbar_part_t held = app->sb_which == which ? app->sb_hold.part : RUBRAVIEW_SCROLLBAR_NONE;
    rubraview_scrollbar_part_t over = rubraview_scrollbar_hit(&sb, px, py);
    double radius = sb.area.width * 0.5;
    rubraview_rect_t t = rubraview_scrollbar_track(&sb), th = rubraview_scrollbar_thumb(&sb);
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ t.x, t.y, t.width, t.height }, 0x28FFFFFFu, radius);
    uint32_t thumb = held == RUBRAVIEW_SCROLLBAR_THUMB ? 0xF0B0B0B0u : over != RUBRAVIEW_SCROLLBAR_NONE ? 0xE0909090u : 0xB0808080u;
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ th.x + 2.0 * dpi, th.y + 1.0 * dpi, th.width - 4.0 * dpi, th.height - 2.0 * dpi },
                                   thumb, radius - 2.0 * dpi);
    for (int i = 0; i < 2; ++i) {   /* the two buttons together at the foot, up over down */
        rubraview_scrollbar_part_t part = i == 0 ? RUBRAVIEW_SCROLLBAR_UP : RUBRAVIEW_SCROLLBAR_DOWN;
        rubraview_rect_t b = i == 0 ? rubraview_scrollbar_up(&sb) : rubraview_scrollbar_down(&sb);
        rubraview_pal_rect_t box = { b.x, b.y + 1.0 * dpi, b.width, b.height - 1.0 * dpi };
        uint32_t fill = held == part ? 0x70FFFFFFu : over == part ? 0x40FFFFFFu : 0x20FFFFFFu;
        rubraview_pal_render_fill_rect(r, box, fill, 2.0 * dpi);
        rubraview_pal_render_draw_text(r, i == 0 ? PROVEN_LIT("\xE2\x96\xB2") : PROVEN_LIT("\xE2\x96\xBC"), box, b.width * 0.62,
                                       0xE0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
    }
}
