/*
 * The view's rectangles: the spread as the compositor lays it out, the page and the Edit view on screen, a point
 * on the picture, the boxes sized, and a film reopened on a new device (RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* The spread on screen as the compositor lays it out: the question
   draw_spread asks. False when there is no spread or no window. */
bool spread_composition(app_state_t *app, rubraview_composition_t *out) {
    if (app->spread_index >= app->layout.count) return false;

    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) return false;

    const rubraview_spread_t *spread = &app->layout.spreads[app->spread_index];
    app_page_t *left = ensure_page_loaded(app, spread->left_index);
    app_page_t *right = ensure_page_loaded(app, spread->right_index);
    rubraview_page_size_t left_size = {0}, right_size = {0};
    if (left && left->loaded) {
        rubraview_orientation_apply_size(app->orientation, (double)picture_w((left)), (double)picture_h((left)),
                                         &left_size.width, &left_size.height);
    }
    if (right && right->loaded) {
        rubraview_orientation_apply_size(app->orientation, (double)picture_w((right)), (double)picture_h((right)),
                                         &right_size.width, &right_size.height);
    }
    *out = compose_view(
        app, spread, (left && left->loaded) ? &left_size : NULL, (right && right->loaded) ? &right_size : NULL,
        (double)win_w, (double)win_h);
    return true;
}

/* Where the page on screen sits, in client pixels, and how many client
   pixels one of its own pixels takes. The crop overlay needs both to
   turn a drag into image coordinates; it asks the compositor the same
   question draw_spread asks, so the rectangle it draws is the one the
   picture is in. Returns false when nothing is drawn. */
bool page_screen_rect(app_state_t *app, rubraview_pal_rect_t *out_rect, double *out_scale) {
    int32_t page_index = current_page_index(app);
    rubraview_composition_t comp;
    if (page_index < 0 || !spread_composition(app, &comp)) return false;

    for (size_t i = 0; i < comp.count; ++i) {
        const rubraview_draw_command_t *cmd = &comp.commands[i];
        if (cmd->page_index != page_index) continue;
        double x0 = cmd->transform.e, y0 = cmd->transform.f;
        double w = (cmd->src_right - cmd->src_left) * cmd->transform.a;
        double h = (cmd->src_bottom - cmd->src_top) * cmd->transform.d;
        if (w <= 0.0 || h <= 0.0) return false;
        *out_rect = (rubraview_pal_rect_t){ .x = x0, .y = y0, .width = w, .height = h };
        *out_scale = cmd->transform.a;
        return true;
    }
    return false;
}

/* §3.13: the crop rectangle, dragged on the picture itself. The model
   (normalising, the ratio lock, staying inside the image) is
   rubraview_edit_crop_drag's; this turns client pixels into image
   pixels and back, and draws what the model then holds. */
/* The owner's edit: where the picture as the steps left it is on screen —
   the page's own place, or, after a crop or a resize, that shape fitted
   into it. */
bool edit_view_rect(app_state_t *app, rubraview_pal_rect_t *out, double *out_scale, bool *changed) {
    rubraview_pal_rect_t rect;
    double scale = 1.0;
    if (!page_screen_rect(app, &rect, &scale) || scale <= 0.0) return false;
    int32_t cw = 0, ch = 0;
    edit_cur_size(app, &cw, &ch);
    if (changed) *changed = false;
    if (cw > 0 && ch > 0 && (cw != app->edit_steps.width || ch != app->edit_steps.height)) {
        double s = rect.width / cw < rect.height / ch ? rect.width / cw : rect.height / ch;
        rubraview_pal_rect_t fit = { rect.x + (rect.width - cw * s) * 0.5, rect.y + (rect.height - ch * s) * 0.5, cw * s, ch * s };
        if (changed) *changed = true;
        rect = fit;
        scale = s;
    }
    *out = rect;
    *out_scale = scale;
    return true;
}

bool crop_point_to_image(app_state_t *app, double px, double py, int32_t *out_x, int32_t *out_y) {
    rubraview_pal_rect_t rect;
    double scale = 1.0;
    if (!edit_view_rect(app, &rect, &scale, NULL) || scale <= 0.0) return false;
    double ix = (px - rect.x) / scale;
    double iy = (py - rect.y) / scale;
    if (ix < 0.0) ix = 0.0;
    if (iy < 0.0) iy = 0.0;
    if (ix > (double)app->edit.image_width) ix = (double)app->edit.image_width;
    if (iy > (double)app->edit.image_height) iy = (double)app->edit.image_height;
    *out_x = (int32_t)(ix + 0.5);
    *out_y = (int32_t)(iy + 0.5);
    return true;
}

bool edit_panel_open(const app_state_t *app) {
    return app->panel.open && !app->panel_is_export && !app->panel_is_batch && !app->panel_is_print && !app->panel_is_convert &&
           !app->panel_is_names;   /* RV-194 */
}

/* The boxes' size from settings.ini (owner, 2026-09-29), each frame so the
   settings window's change shows at once; not under a grip being dragged. */
void boxes_apply_size(app_state_t *app) {
    if (!app->menubox.grip_active) {
        rubraview_box_set_size(&app->menubox,
                               (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("menubox_columns"))),
                               rubraview_settings_get(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("menubox_size")) / 100.0);
    }
    if (!app->toolbox.grip_active) {
        rubraview_box_set_size(&app->toolbox,
                               (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("toolbox_columns"))),
                               rubraview_settings_get(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("toolbox_size")) / 100.0);
    }
}

/* RV-062: after a lost graphics device, a film decoded (or tone mapped, RV-150) on the card is
   opened again, where it was, paused or not, on the same tracks: its
   decoder lived on the lost card and would hand over no more frames.
   Software-decoded films and sound are not bound to the device and are
   left alone (reopening music would be a gap you can hear). */
static void media_reopen(app_state_t *app);

void media_reopen_on_new_device(app_state_t *app) {
    if (!app->media || !app->media_info.has_video || !(app->media_info.hardware_decode || app->media_info.hdr_tone_mapped)) return;
    media_reopen(app);
    osd_say(app, TU8("the graphics device was reset: the film was opened again"));
}

/* RV-176 (D-208): FFmpeg's child ended or stopped answering on the film on screen. It is opened again with
   Windows' decoder at the same place where Windows can play it; else, and for a station, it is said. */
void media_stopped_tick(app_state_t *app) {
    if (!app->media || !rubraview_pal_media_stopped(app->media)) return;
    int32_t page = app->media_page;
    if (app->media_info.live || page < 0) {
        media_close(app);
        if (page >= 0 && (size_t)page < page_count(app)) app->pages[page].failed = true;
        osd_say(app, TU8("FFmpeg stopped on this station"));
        return;
    }
    app->media_no_ffmpeg_page = page + 1;
    media_reopen(app);
    osd_say(app, app->media ? TU8("FFmpeg stopped on this file: it was opened with Windows' decoder") : TU8("FFmpeg stopped on this file"));
}

/* RV-153: once a second while an HDR film is tone mapped, whether its screen is still the one it was opened
   for (moved to another screen, Windows' HDR switched, the SDR brightness moved); if not, it is opened again. */
void hdr_screen_tick(app_state_t *app, double dt) {
    if (!app->media || !app->media_info.hdr_tone_mapped) { app->hdr_screen_check = 0.0; return; }
    if (app->hdr_output_refused && app->media_info.hdr_output) {   /* the window would not go out as HDR */
        media_reopen(app);
        return;
    }
    app->hdr_screen_check += dt;
    if (app->hdr_screen_check < 1.0) return;
    app->hdr_screen_check = 0.0;
    int32_t hdr_out = (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("video"), PROVEN_LIT("hdr_output")));
    if (hdr_out != 1) return;
    rubraview_hdr_display_t now;
    rubraview_pal_render_hdr_display(app->renderer, &now);
    const rubraview_hdr_display_t *was = &app->media_hdr_screen;
    bool changed = now.hdr != was->hdr || (now.hdr && (now.peak_nits != was->peak_nits || now.sdr_white_nits != was->sdr_white_nits));
    if (!changed) return;
    app->hdr_output_refused = false;   /* another screen: ask again */
    media_reopen(app);
}

/* A film opened again where it was, paused or not, on the same tracks. */
static void media_reopen(app_state_t *app) {
    double position = app->media_position;
    bool paused = app->media_paused;
    int32_t subtitle = app->tracks.current_subtitle;
    int32_t audio = app->tracks.current_audio;
    media_close(app);
    media_prepare(app);
    if (!app->media) return;
    if (audio >= 0 && audio != app->tracks.current_audio && (size_t)audio < app->tracks.count &&
        app->tracks.tracks[audio].kind == RUBRAVIEW_TRACK_AUDIO &&
        rubraview_pal_media_select_audio_track(app->media, app->tracks.tracks[audio].stream_index)) {
        app->tracks.current_audio = audio;
    }
    if (subtitle != app->tracks.current_subtitle) subtitle_select(app, subtitle);
    media_seek_to(app, position);
    app->media_position = position;
    if (paused != app->media_paused) media_toggle_pause(app);
}
