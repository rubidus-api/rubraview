/*
 * RV-270 (D-330, owner 2026-11-11: "동영상에 대해 색상 명도 채도 색온도 밝기곡선 같은 것 조절해서 볼 수 있게 ... 샤픈이나 블러,
 * 해상도 업 같은 필터 ... 이미지의 e와 마찬가지로 이펙트의 e"): a film's picture adjusted as it plays.
 *
 * The values (vgrade.h) are kept in `[video] adjust_*`. With any of them on, a frame goes first into the film's
 * raw texture, and the card's passes draw it adjusted into the page's own, which everything after reads (the
 * blend, a frame copied or saved, the canvas). The upscaler runs before the window is drawn, from the page's
 * texture into one of the size the film takes on screen, which the canvas then draws in its place. The panel
 * is a box of its own over the window (as Classify's is), its sliders the values; the film plays under it.
 */
#include "app.h"
#include "rubraview/vgrade.h"

static proven_u8str_view_t adjust_key(int i, char *buf, size_t cap) {
    const rubraview_vgrade_range_t *r = rubraview_vgrade_range((rubraview_vgrade_value_t)i);
    int n = rubraview_fmt(buf, cap, "adjust_{}", PROVEN_ARG(r ? r->key : ""));
    return (proven_u8str_view_t){ (const proven_byte_t *)buf, n > 0 ? (size_t)n : 0 };
}

static void film_adjust_prepare(app_state_t *app) {
    app->film_adjust_film = rubraview_vgrade_prepare(&app->film_adjust, &app->film_adjust_prep);
}

void film_adjust_load(app_state_t *app) {
    for (int i = 0; i < RUBRAVIEW_VGRADE_COUNT; ++i) {
        char key[48];
        app->film_adjust.v[i] = (float)rubraview_settings_get(&app->settings, PROVEN_LIT("video"), adjust_key(i, key, sizeof(key)));
    }
    rubraview_vgrade_clamp(&app->film_adjust);
    film_adjust_prepare(app);
}

static void film_adjust_store(app_state_t *app) {
    for (int i = 0; i < RUBRAVIEW_VGRADE_COUNT; ++i) {
        char key[48];
        rubraview_settings_set(&app->settings, PROVEN_LIT("video"), adjust_key(i, key, sizeof(key)), (double)app->film_adjust.v[i]);
    }
}

bool film_adjust_on(const app_state_t *app) {
    return !rubraview_vgrade_is_neutral(&app->film_adjust);
}

/* Not with Blend frames (D-272): a blended refresh draws the page's texture under the one before, so the
   upscaled picture would come and go with it; Blend frames is off with HDR output, as media_blend_on says. */
static bool upscale_wanted(const app_state_t *app) {
    bool blending = !app->media_info.hdr_output &&
                    rubraview_settings_get(&app->settings, PROVEN_LIT("video"), PROVEN_LIT("blend_frames")) > 0.5;
    return app->film_adjust_prep.upscale > 0.0f && !app->film_up_refused && !blending;
}

void film_grade_drop(app_state_t *app) {
    if (app->media_raw_texture) rubraview_pal_texture_destroy(app->media_raw_texture);
    app->media_raw_texture = NULL;
    app->media_raw_valid = false;
    if (app->media_up_texture) rubraview_pal_texture_destroy(app->media_up_texture);
    app->media_up_texture = NULL;
    app->media_up_ready = false;
    app->media_up_dirty = true;
}

static void film_refused(app_state_t *app, bool *flag, proven_u8str_view_t what) {
    if (*flag) return;
    *flag = true;
    const char *why = rubraview_pal_render_vgrade_why(app->renderer);
    char line[256];
    int n = why ? rubraview_fmt(line, sizeof(line), T("{}: {}"), PROVEN_ARG(what), PROVEN_ARG(rubraview_tr(why)))
                : rubraview_fmt(line, sizeof(line), "{}", PROVEN_ARG(what));
    osd_say(app, (proven_u8str_view_t){ (const proven_byte_t *)line, n > 0 ? (size_t)n : 0 });
}

/* The page's texture on the card, which the passes draw into (a film the processor decodes has one in memory). */
static bool page_on_card(app_state_t *app, app_page_t *page) {
    if (rubraview_pal_texture_on_card(page->texture)) return true;
    rubraview_texture_t *t = rubraview_pal_texture_create_video(app->renderer, page->width, page->height);
    if (!t) return false;
    if (page->texture) rubraview_pal_texture_destroy(page->texture);
    page->texture = t;
    return true;
}

bool film_grade_frame(app_state_t *app, app_page_t *page, const rubraview_video_frame_t *frame) {
    app->media_up_dirty = true;
    app->media_raw_valid = false;
    if (!app->media_info.has_video || !app->renderer || !page || !frame) return false;
    if (!app->film_adjust_film || app->film_grade_refused) {
        if (upscale_wanted(app)) page_on_card(app, page);   /* the upscaler reads it there */
        return false;
    }
    int32_t w = page->width, h = page->height;
    bool hdr = rubraview_pal_texture_is_hdr(page->texture);
    if (app->media_raw_texture) {
        int32_t rw = 0, rh = 0;
        rubraview_pal_texture_size(app->media_raw_texture, &rw, &rh);
        if (rw != w || rh != h || rubraview_pal_texture_is_hdr(app->media_raw_texture) != hdr) {
            rubraview_pal_texture_destroy(app->media_raw_texture);
            app->media_raw_texture = NULL;
        }
    }
    if (!app->media_raw_texture) {
        app->media_raw_texture = hdr ? rubraview_pal_texture_create_video_hdr(app->renderer, w, h)
                                     : rubraview_pal_texture_create_video(app->renderer, w, h);
    }
    if (!app->media_raw_texture || !page_on_card(app, page)) {
        film_refused(app, &app->film_grade_refused, TU8("the picture adjustment has no room on the graphics card"));
        return false;
    }
    bool in = frame->gpu_texture ? rubraview_pal_texture_copy_video_frame(app->media_raw_texture, frame->gpu_texture, frame->gpu_subresource)
                                 : rubraview_pal_texture_upload_bgra(app->media_raw_texture, frame->pixels, frame->stride);
    if (!in) return false;
    if (!rubraview_pal_render_vgrade(app->renderer, &app->film_adjust_prep, app->media_raw_texture, page->texture)) {
        film_refused(app, &app->film_grade_refused, TU8("the picture adjustment could not run on this graphics card"));
        return false;
    }
    app->media_raw_valid = true;
    return true;
}

/* A value changed: kept, and the frame on screen drawn again with it (from the raw frame when there is one,
   else the frame asked for again, as a paused film's frame is for Copy frame). */
void film_adjust_changed(app_state_t *app) {
    rubraview_vgrade_clamp(&app->film_adjust);
    film_adjust_prepare(app);
    film_adjust_store(app);
    app->media_up_dirty = true;
    app->media_new_picture = true;
    if (!app->media || !app->media_info.has_video || app->media_page < 0 || (size_t)app->media_page >= page_count(app)) return;
    app_page_t *page = &app->pages[app->media_page];
    if (app->film_adjust_film && app->media_raw_valid && !app->film_grade_refused &&
        rubraview_pal_render_vgrade(app->renderer, &app->film_adjust_prep, app->media_raw_texture, page->texture)) return;
    if (app->media_paused && app->media_has_frame) media_seek_to(app, app->media_position);
}

/* Before the window is drawn: the film's picture at the size it was last drawn, when that is larger. */
void film_upscale_prepare(app_state_t *app) {
    app->media_up_ready = false;
    if (!upscale_wanted(app) || !app->media || !app->media_info.has_video || !app->video_transform_ok ||
        app->media_page < 0 || (size_t)app->media_page >= page_count(app)) return;
    app_page_t *page = &app->pages[app->media_page];
    const rubraview_mat3x2_t m = app->video_transform;
    double k = sqrt(fabs(m.a * m.d - m.b * m.c));
    app->media_up_scale = k;
    int32_t dw = 0, dh = 0;
    if (!page->texture || !rubraview_pal_texture_on_card(page->texture) ||
        !rubraview_vgrade_upscale_size(page->width, page->height, k, &dw, &dh)) return;
    bool hdr = rubraview_pal_texture_is_hdr(page->texture);
    if (app->media_up_texture) {
        int32_t uw = 0, uh = 0;
        rubraview_pal_texture_size(app->media_up_texture, &uw, &uh);
        if (uw != dw || uh != dh || rubraview_pal_texture_is_hdr(app->media_up_texture) != hdr) {
            rubraview_pal_texture_destroy(app->media_up_texture);
            app->media_up_texture = NULL;
        }
    }
    if (!app->media_up_texture) {
        app->media_up_texture = hdr ? rubraview_pal_texture_create_video_hdr(app->renderer, dw, dh)
                                    : rubraview_pal_texture_create_video(app->renderer, dw, dh);
        app->media_up_dirty = true;
        if (!app->media_up_texture) return;   /* no room this time: the canvas enlarges it */
    }
    if (app->media_up_dirty) {
        if (!rubraview_pal_render_vgrade_upscale(app->renderer, app->film_adjust_prep.upscale, page->texture, app->media_up_texture)) {
            film_refused(app, &app->film_up_refused, TU8("the upscaler could not run on this graphics card"));
            return;
        }
        app->media_up_dirty = false;
    }
    app->media_up_ready = true;
}

/* The film's page drawn from the upscaled picture, when it is of the size the film takes now; false: the
   caller draws the page's own texture (and a size that changed is made for the next frame). */
bool film_upscale_draw(app_state_t *app, const app_page_t *page, rubraview_mat3x2_t oriented, double opacity) {
    if (!upscale_wanted(app) || app->media_blend_weight > 0.0) return false;
    double k = sqrt(fabs(oriented.a * oriented.d - oriented.b * oriented.c));
    if (fabs(k - app->media_up_scale) > 1e-3 * (k > 1.0 ? k : 1.0)) { app->media_up_retry = true; return false; }
    if (!app->media_up_ready || !app->media_up_texture || page->width <= 0 || page->height <= 0) return false;
    int32_t dw = 0, dh = 0;
    rubraview_pal_texture_size(app->media_up_texture, &dw, &dh);
    rubraview_mat3x2_t place = rubraview_mat3x2_multiply(
        rubraview_mat3x2_scale((double)page->width / (double)dw, (double)page->height / (double)dh), oriented);
    rubraview_pal_render_draw_texture_opacity(app->renderer, app->media_up_texture, place, RUBRAVIEW_INTERP_LINEAR, opacity);
    return true;
}

/* ---- the panel ---- */

enum { FILM_ROW_VALUE = 0, FILM_ROW_RESET = 100, FILM_ROW_EDIT_FRAME = 101 };

static void film_panel_add(app_state_t *app, rubraview_vgrade_value_t v) {
    const rubraview_vgrade_range_t *r = rubraview_vgrade_range(v);
    const char *label = rubraview_tr(r->label);
    rubraview_panel_add_slider(&app->film_panel, FILM_ROW_VALUE + (int32_t)v,
                               (proven_u8str_view_t){ (const proven_byte_t *)label, proven_cstr_len(label) },
                               (double)app->film_adjust.v[v], (double)r->min, (double)r->max, (double)r->step);
}

static void film_panel_build(app_state_t *app) {
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    bool placed = app->film_panel.placed;
    double px = app->film_panel.place_x, py = app->film_panel.place_y, scroll = app->film_panel.scroll;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    app->film_panel = rubraview_panel_create(TU8("Adjust film"), dpi);
    app->film_panel.bounds.width = 360.0 * dpi;
    app->film_panel.place_x = placed ? px : (double)w - 372.0 * dpi;
    app->film_panel.place_y = placed ? py : 60.0 * dpi;
    app->film_panel.placed = true;
    rubraview_panel_add_heading(&app->film_panel, TU8("Light and colour"));
    for (int v = RUBRAVIEW_VGRADE_EXPOSURE; v <= RUBRAVIEW_VGRADE_GAMMA; ++v) film_panel_add(app, (rubraview_vgrade_value_t)v);
    rubraview_panel_add_heading(&app->film_panel, TU8("Brightness curve"));
    for (int v = RUBRAVIEW_VGRADE_SHADOWS; v <= RUBRAVIEW_VGRADE_HIGHLIGHTS; ++v) film_panel_add(app, (rubraview_vgrade_value_t)v);
    rubraview_panel_add_heading(&app->film_panel, TU8("Filters"));
    for (int v = RUBRAVIEW_VGRADE_BLUR; v <= RUBRAVIEW_VGRADE_UPSCALE; ++v) film_panel_add(app, (rubraview_vgrade_value_t)v);
    rubraview_panel_add_separator(&app->film_panel);
    rubraview_panel_add_button(&app->film_panel, FILM_ROW_RESET, TU8("Reset (the film as it is)"));
    rubraview_panel_add_button(&app->film_panel, FILM_ROW_EDIT_FRAME, TU8("Edit this frame as a picture (paused: E)"));
    /* the names' column as wide as the longest, translated (as Batch's and Edit's), the sliders 200 wide beside */
    double size = app->film_panel.row_height * 0.45, most = 0.0;
    for (size_t i = 0; i < app->film_panel.row_count; ++i) {
        const rubraview_panel_row_t *row = &app->film_panel.rows[i];
        double tw = 0.0;
        if (row->kind == RUBRAVIEW_ROW_SLIDER && rubraview_pal_render_measure_text(app->renderer, row->label, size, &tw) && tw > most) most = tw;
    }
    double want = most + size * 4.2 + app->film_panel.padding * 1.6;
    if (want > app->film_panel.label_width) app->film_panel.label_width = want;
    app->film_panel.bounds.width = app->film_panel.label_width + 200.0 * dpi;
    if (!placed) app->film_panel.place_x = (double)w - app->film_panel.bounds.width - 12.0 * dpi;
    app->film_panel.open = true;
    rubraview_panel_layout(&app->film_panel, (double)w, (double)h);
    app->film_panel.scroll = scroll;
}

void film_panel_toggle(app_state_t *app) {
    if (app->film_panel.open) { app->film_panel.open = false; return; }
    if (!app->media || !app->media_info.has_video || app->media_page < 0) { osd_say(app, TU8("Adjust is for films")); return; }
    film_adjust_load(app);
    film_panel_build(app);
}

void film_panel_relayout(app_state_t *app) {
    if (!app->film_panel.open) return;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    if (w > 0 && h > 0) rubraview_panel_layout(&app->film_panel, (double)w, (double)h);
}

static void film_panel_apply(app_state_t *app, int32_t row) {
    if (row < 0 || (size_t)row >= app->film_panel.row_count) return;
    int32_t id = app->film_panel.rows[row].id;
    if (id < FILM_ROW_VALUE || id >= FILM_ROW_VALUE + RUBRAVIEW_VGRADE_COUNT) return;
    app->film_adjust.v[id - FILM_ROW_VALUE] = (float)app->film_panel.rows[row].value;
    film_adjust_changed(app);
}

bool film_panel_press(app_state_t *app, double px, double py) {
    rubraview_panel_t *box = &app->film_panel;
    if (!box->open || !rubraview_rect_contains(box->bounds, px, py)) return false;
    if (scrollbar_press_panel(app, box, px, py)) return true;
    if (rubraview_panel_title_hit(box, px, py)) { rubraview_panel_move_begin(box, px, py); return true; }
    int32_t row = -1;
    rubraview_panel_event_t ev = rubraview_panel_press(box, px, py, &row);
    if (ev == RUBRAVIEW_PANEL_CLOSE) { box->open = false; return true; }
    if (ev == RUBRAVIEW_PANEL_VALUE_CHANGED) { film_panel_apply(app, row); return true; }
    if (ev == RUBRAVIEW_PANEL_BUTTON_PRESSED && row >= 0) {
        int32_t id = box->rows[row].id;
        if (id == FILM_ROW_RESET) {
            rubraview_vgrade_reset(&app->film_adjust);
            film_adjust_changed(app);
            film_panel_build(app);
            osd_say(app, TU8("the film as it is: nothing adjusted"));
        } else if (id == FILM_ROW_EDIT_FRAME) {
            if (!app->media_paused) {
                handle_action(app, PROVEN_LIT("media_play_pause"));
            }
            handle_action(app, PROVEN_LIT("open_edit"));
        }
    }
    return true;   /* the press was on the box */
}

bool film_panel_drag(app_state_t *app, double px, double py) {
    if (!app->film_panel.open || app->film_panel.active_row < 0) return false;
    int32_t row = -1;
    if (rubraview_panel_drag(&app->film_panel, px, py, &row) == RUBRAVIEW_PANEL_VALUE_CHANGED) film_panel_apply(app, row);
    return true;
}

void film_panel_draw(app_state_t *app) {
    if (!app->film_panel.open) return;
    if (!app->media || !app->media_info.has_video) { app->film_panel.open = false; return; }
    draw_panel_of(app, &app->film_panel, true);
}

/* The Toolbar's caption: says when an adjustment is in force. */
proven_u8str_view_t film_adjust_caption(const app_state_t *app) {
    return film_adjust_on(app) ? TU8("Adjust on") : TU8("Adjust");
}
