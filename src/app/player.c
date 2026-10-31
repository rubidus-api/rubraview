/*
 * The player's own windows and keys: the equaliser and a .cue's tracks, A-B by hand, the mini player
 * (RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* ---- The equaliser window (owner, 2026-10-01) ---- */

void eqwin_toggle(app_state_t *app) {
    app->eqwin.open = !app->eqwin.open;
    app->eqwin.sliding = -1;
    app->eqwin.dragging = false;
    if (!app->eqwin.open) return;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    rubraview_eqwin_place(&app->eqwin, (double)w, (double)h);
}

/* A band moved: the curve on screen becomes Custom, starting from the
   gains the sliders showed, so a named preset is a place to start from. */
void eqwin_set_band(app_state_t *app, size_t band, double db) {
    if (band >= RUBRAVIEW_EQ_BANDS) return;
    if (eq_preset_now(app) != RUBRAVIEW_EQWIN_CUSTOM) {
        float shown[RUBRAVIEW_EQ_BANDS];
        eq_shown_gains(app, shown);
        for (size_t b = 0; b < RUBRAVIEW_EQ_BANDS; ++b) {
            rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), eq_band_key(b), (double)shown[b]);
        }
        rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("eq_preset"), (double)RUBRAVIEW_EQWIN_CUSTOM);
    }
    rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), eq_band_key(band), db);
    audio_chain_update(app);
}

/* A press on the window: true when it took it. A right click puts a band
   back at 0 dB. */
bool eqwin_press(app_state_t *app, double x, double y, bool right) {
    size_t index = 0;
    switch (rubraview_eqwin_hit(&app->eqwin, x, y, &index)) {
        case RUBRAVIEW_EQWIN_CLOSE: app->eqwin.open = false; return true;
        case RUBRAVIEW_EQWIN_TITLE: rubraview_eqwin_drag_begin(&app->eqwin, x, y); return true;
        case RUBRAVIEW_EQWIN_PRESET:
            rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("eq_preset"), (double)index);
            audio_chain_update(app);
            return true;
        case RUBRAVIEW_EQWIN_SLIDER:
            if (right) { eqwin_set_band(app, index, 0.0); return true; }
            app->eqwin.sliding = (int)index;
            eqwin_set_band(app, index, rubraview_eqwin_db_at(&app->eqwin, index, y));
            return true;
        case RUBRAVIEW_EQWIN_BODY: return true;
        default: return false;
    }
}

/* The wheel over a band: half a decibel a notch. */
bool eqwin_wheel(app_state_t *app, double x, double y, double delta) {
    size_t index = 0;
    rubraview_eqwin_part_t part = rubraview_eqwin_hit(&app->eqwin, x, y, &index);
    if (part == RUBRAVIEW_EQWIN_NONE) return false;
    if (part == RUBRAVIEW_EQWIN_SLIDER) {
        float shown[RUBRAVIEW_EQ_BANDS];
        eq_shown_gains(app, shown);
        eqwin_set_band(app, index, rubraview_eqwin_step(shown[index], delta > 0 ? 1 : -1));
    }
    return true;
}

/* With a `.cue` cutting the song into tracks (owner, 2026-09-30): the next
   or previous track in the same file. The previous one goes back to the
   start of this track first when it is more than 3 s in, as players do.
   False when there is no cue, or no track that way (the file changes). */
bool cue_step(app_state_t *app, int direction) {
    if (!app->media || app->media_info.has_video || app->music_cue.count < 2) return false;
    const rubraview_cue_sheet_t *cue = &app->music_cue;
    int32_t now = rubraview_cue_track_at(cue, app->media_position);
    if (now < 0) now = 0;
    int32_t target = now + direction;
    if (direction < 0 && app->media_position - cue->tracks[now].start_seconds > 3.0) target = now;
    if (target < 0 || (size_t)target >= cue->count) return false;
    media_seek_to(app, cue->tracks[target].start_seconds);
    app->media_position = cue->tracks[target].start_seconds;
    char line[160];
    int n = snprintf(line, sizeof(line), "%d / %zu  %.*s", cue->tracks[target].number, cue->count,
                     (int)cue->tracks[target].title.size, cue->tracks[target].title.ptr);
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)(n < (int)sizeof(line) ? n : (int)sizeof(line) - 1) });
    return true;
}

/* ---- A-B by hand (owner, 2026-09-24) ---- */

/* The A-B box and the pixel box sit in the same place: centred, a little below the middle.
   Each has an X at its top right, and the A-B box two buttons that take this moment (owner, 2026-10-15). */
static rubraview_pal_rect_t answer_box_rect(const app_state_t *app, double box_w, double box_h) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    box_w *= dpi; box_h *= dpi;
    if (box_w > (double)w - 16.0) box_w = (double)w - 16.0;
    double x = ((double)w - box_w) * 0.5;
    /* Clear of an open panel (the Adjust panel the crop box comes from), when there is room. */
    if (app->panel.open && x + box_w > app->panel.bounds.x - 8.0) {
        double left = app->panel.bounds.x - 8.0 - box_w;
        x = left >= 8.0 ? left : 8.0;
    }
    return (rubraview_pal_rect_t){ x, (double)h * 0.62, box_w, box_h };
}
rubraview_pal_rect_t answer_box_close(const app_state_t *app, rubraview_pal_rect_t box) {
    double s = 26.0 * rubraview_pal_window_dpi_scale(app->window);
    return (rubraview_pal_rect_t){ box.x + box.width - s - 4.0, box.y + 4.0, s, s };
}
rubraview_pal_rect_t ab_box_rect(const app_state_t *app) { return answer_box_rect(app, 560.0, 158.0); }
rubraview_pal_rect_t px_box_rect(const app_state_t *app) { return answer_box_rect(app, 600.0, 132.0); }
/* RV-187: a field's px / ratio switch, under the fields, a column each */
rubraview_pal_rect_t px_unit_rect(const app_state_t *app, int32_t field) {
    rubraview_pal_rect_t box = px_box_rect(app);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    int32_t n = px_edit_fields(app);
    double col = box.width / (double)n, w = col - 16.0 * dpi;
    return (rubraview_pal_rect_t){ box.x + col * field + 8.0 * dpi, box.y + 92.0 * dpi, w, 28.0 * dpi };
}
/* "A = now" (0) and "B = now" (1), side by side under the fields. */
rubraview_pal_rect_t ab_box_button(const app_state_t *app, int32_t which) {
    rubraview_pal_rect_t box = ab_box_rect(app);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double bw = 150.0 * dpi, bh = 30.0 * dpi, gap = 20.0 * dpi;
    double left = box.x + (box.width - 2.0 * bw - gap) * 0.5;
    return (rubraview_pal_rect_t){ left + (double)which * (bw + gap), box.y + 80.0 * dpi, bw, bh };
}
bool pal_rect_has(rubraview_pal_rect_t r, double x, double y) {
    return x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height;
}
/* A point's field in the A-B box: A's at the left half, B's at the right; hours, minutes,
   seconds, each followed by its unit (owner, 2026-10-15). */
static const double AB_PART_W[3] = { 48.0, 56.0, 90.0 };
rubraview_pal_rect_t ab_part_rect(const app_state_t *app, int32_t side, int32_t part) {
    rubraview_pal_rect_t box = ab_box_rect(app);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double total = AB_LABEL_W + AB_PART_W[0] + AB_PART_W[1] + AB_PART_W[2] + 3.0 * AB_UNIT_W;
    double half = box.width * 0.5;
    double x = box.x + (double)side * half + (half - total * dpi) * 0.5 + AB_LABEL_W * dpi;
    for (int32_t k = 0; k < part; ++k) x += (AB_PART_W[k] + AB_UNIT_W) * dpi;
    return (rubraview_pal_rect_t){ x, box.y + 38.0 * dpi, AB_PART_W[part] * dpi, 30.0 * dpi };
}

/* A point in three fields, hours, minutes and seconds (owner, 2026-10-15). An unset
   point is three empty fields rather than noughts: nought is a time. */
proven_u8str_view_t ab_part(const app_state_t *app, int32_t side, int32_t part) {
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->ab_edit_text[side][part], .size = app->ab_edit_length[side][part] };
}

static void ab_part_set(app_state_t *app, int32_t side, int32_t part, const char *text) {
    size_t n = strlen(text);
    if (n >= sizeof(app->ab_edit_text[0][0])) n = sizeof(app->ab_edit_text[0][0]) - 1;
    memcpy(app->ab_edit_text[side][part], text, n);
    app->ab_edit_text[side][part][n] = '\0';
    app->ab_edit_length[side][part] = n;
}

static void ab_edit_fill(app_state_t *app, int32_t side, double seconds) {
    if (seconds < 0.0) {
        for (int32_t p = 0; p < 3; ++p) ab_part_set(app, side, p, "");
        return;
    }
    int32_t h = 0, m = 0;
    double sec = 0.0;
    rubraview_timecode_split(seconds, &h, &m, &sec);
    char text[16];
    snprintf(text, sizeof(text), "%d", (int)h);     ab_part_set(app, side, 0, text);
    snprintf(text, sizeof(text), "%02d", (int)m);   ab_part_set(app, side, 1, text);
    snprintf(text, sizeof(text), "%06.3f", sec);    ab_part_set(app, side, 2, text);
}

/* The point a side's fields say: -1 when all three are empty; false when one is not a number. */
static bool ab_edit_value(const app_state_t *app, int32_t side, double *out) {
    return rubraview_timecode_join(ab_part(app, side, 0), ab_part(app, side, 1), ab_part(app, side, 2), out);
}

void ab_edit_goto(app_state_t *app, int32_t side, int32_t part) {
    app->ab_edit_field = side;
    app->ab_edit_part = part;
    app->ab_edit_fresh = true;
}

/* This moment into A or B; the other gives way when they cross (rubraview_ab_take). */
void ab_edit_take(app_state_t *app, int32_t f) {
    double v[2] = { -1.0, -1.0 };
    for (int32_t k = 0; k < 2; ++k) {
        if (!ab_edit_value(app, k, &v[k])) v[k] = -1.0;
    }
    rubraview_ab_take(&v[0], &v[1], play_position(app), f == 0, play_duration(app));
    ab_edit_fill(app, 0, v[0]);
    ab_edit_fill(app, 1, v[1]);
    ab_edit_goto(app, f, 2);
}

void ab_edit_begin(app_state_t *app) {
    if (!app->media && !anim_plays(app)) { osd_say(app, TU8("nothing is playing")); return; }
    app->ab_edit_open = true;
    ab_edit_goto(app, 0, 2);
    ab_edit_fill(app, 0, app->ab_a);
    ab_edit_fill(app, 1, app->ab_b);
    rubraview_pal_window_text_input(app->window, true);
}

void ab_edit_close(app_state_t *app) {
    app->ab_edit_open = false;
    rubraview_pal_window_text_input(app->window, false);
}

/* What was typed becomes the points — or says why it cannot, and keeps
   the box open so the reader can fix it rather than start again. */
static bool ab_edit_commit(app_state_t *app) {
    double v[2] = { -1.0, -1.0 };
    for (int32_t side = 0; side < 2; ++side) {
        if (!ab_edit_value(app, side, &v[side])) {
            osd_say(app, side == 0 ? TU8("A is not a time: hours and minutes are whole, seconds like 23.5")
                                   : TU8("B is not a time: hours and minutes are whole, seconds like 23.5"));
            ab_edit_goto(app, side, 2);
            return false;
        }
    }
    double a = v[0], b = v[1];
    if (a >= 0.0 && b >= 0.0 && b <= a) {
        osd_say(app, TU8("B has to come after A"));
        ab_edit_goto(app, 1, 2);
        return false;
    }
    app->ab_a = a;
    app->ab_b = b;
    char line[96];
    int n = a < 0.0 ? snprintf(line, sizeof(line), T("repeat off"))
          : b < 0.0 ? snprintf(line, sizeof(line), T("repeat from %.3f s"), a)
                    : snprintf(line, sizeof(line), T("repeating %.3f - %.3f s"), a, b);
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
    return true;
}

/* The keys the box takes. Everything else is swallowed while it is open:
   a box that is taking a number must not also turn pages. */
bool ab_edit_handle_key(app_state_t *app, rubraview_key_combo_t combo) {
    if (!app->ab_edit_open) return false;
    if (key_is(combo, "Escape")) { ab_edit_close(app); return true; }
    if (key_is(combo, "Enter")) {
        if (ab_edit_commit(app)) ab_edit_close(app);
        return true;
    }
    /* Tab goes through the six fields, A's hours to B's seconds; Up and Down change the point. */
    if (key_is(combo, "Tab") || key_is(combo, "Shift+Tab")) {
        int32_t at = app->ab_edit_field * 3 + app->ab_edit_part;
        at = (at + (key_is(combo, "Tab") ? 1 : 5)) % 6;
        ab_edit_goto(app, at / 3, at % 3);
        return true;
    }
    if (key_is(combo, "Up") || key_is(combo, "Down")) {
        ab_edit_goto(app, 1 - app->ab_edit_field, app->ab_edit_part);
        return true;
    }
    if (key_is(combo, "Backspace")) {
        int32_t f = app->ab_edit_field, p = app->ab_edit_part;
        if (app->ab_edit_length[f][p] > 0) app->ab_edit_text[f][p][--app->ab_edit_length[f][p]] = '\0';
        app->ab_edit_fresh = false;
        return true;
    }
    /* Left / Right move the point in hand by Settings › Video › A-B step
       (owner, 2026-09-30); a point still empty starts from where the film is. */
    if (key_is(combo, "Left") || key_is(combo, "Right")) {
        int32_t f = app->ab_edit_field;
        double step = rubraview_settings_get(&app->settings, PROVEN_LIT("video"), PROVEN_LIT("ab_step_seconds"));
        double now = app->media_position;
        double typed = -1.0;
        if (ab_edit_value(app, f, &typed) && typed >= 0.0) now = typed;
        now += key_is(combo, "Left") ? -step : step;
        if (now < 0.0) now = 0.0;
        ab_edit_fill(app, f, now);
        app->ab_edit_fresh = true;
        return true;
    }
    /* The crossing-over the owner asked for: the keys that set a point
       put the playhead into it, as numbers, and they can then be typed over. */
    if (key_is(combo, "BracketLeft") || key_is(combo, "BracketRight")) {
        ab_edit_take(app, key_is(combo, "BracketLeft") ? 0 : 1);
        return true;
    }
    if (key_is(combo, "Backslash")) {
        for (int32_t k = 0; k < 2; ++k) ab_edit_fill(app, k, -1.0);
        return true;
    }
    return true;   /* the digits arrive as text; the rest is swallowed */
}

/* The characters themselves: digits, a point in the seconds, and a colon to go on to the next field. */
void ab_edit_text_typed(app_state_t *app, proven_u8str_view_t text) {
    static const size_t MOST[3] = { 3, 3, 6 };   /* 999 hours; 999 minutes add up; 59.999 */
    for (size_t i = 0; i < text.size; ++i) {
        char c = text.ptr[i];
        int32_t f = app->ab_edit_field, p = app->ab_edit_part;
        if (c == ':') {
            if (p < 2) ab_edit_goto(app, f, p + 1);
            continue;
        }
        bool point = c == '.' || c == ',';
        if (!(c >= '0' && c <= '9') && !(point && p == 2)) continue;
        if (app->ab_edit_fresh) { ab_part_set(app, f, p, ""); app->ab_edit_fresh = false; }
        if (point && memchr(app->ab_edit_text[f][p], '.', app->ab_edit_length[f][p])) continue;
        if (app->ab_edit_length[f][p] >= MOST[p]) continue;
        app->ab_edit_text[f][p][app->ab_edit_length[f][p]++] = point ? '.' : c;
        app->ab_edit_text[f][p][app->ab_edit_length[f][p]] = '\0';
    }
}

/* ---- §3.14.6: the mini player ---- */

#define MINI_BACKGROUND 0xFF181818u
#define MINI_WIDTH  320
#define MINI_HEIGHT 80

/* Which player it speaks to: the background music if there is any,
   otherwise the track the page itself is playing. */
static rubraview_media_t *mini_target(const app_state_t *app) {
    if (app->bgm_media) return app->bgm_media;
    if (app->media && !app->media_info.has_video) return app->media;
    return NULL;
}

static const rubraview_tags_t *mini_tags(const app_state_t *app) {
    return app->bgm_media ? &app->bgm_tags : &app->music_tags;
}

bool mini_paused(const app_state_t *app) {
    return app->bgm_media ? app->bgm_paused : app->media_paused;
}

static double mini_position(const app_state_t *app) {
    return app->bgm_media ? app->bgm_position : app->media_position;
}

static double mini_duration(const app_state_t *app) {
    return app->bgm_media ? app->bgm_info.duration_seconds : app->media_info.duration_seconds;
}

/* The page of the track being heard, or -1 when it has none on screen. */
static int32_t mini_page(const app_state_t *app) {
    return app->bgm_media ? app->bgm_page : app->media_page;
}

void mini_close(app_state_t *app) {
    if (!app->mini_open) return;
    rubraview_pal_window_get_frame(app->mini_window, &app->mini_frame[0], &app->mini_frame[1],
                                   &app->mini_frame[2], &app->mini_frame[3]);
    if (app->mini_cover) rubraview_pal_texture_destroy(app->mini_cover);
    if (app->mini_renderer) rubraview_pal_render_destroy(app->mini_renderer);
    if (app->mini_window) rubraview_pal_window_destroy(app->mini_window);
    window_block_give(app, &app->mini_block);
    app->mini_cover = NULL;
    app->mini_renderer = NULL;
    app->mini_window = NULL;
    app->mini_open = false;
}

/* The cover belongs to the renderer that draws it, so the small window
   decodes its own copy from the same bytes. */
static void mini_cover_load(app_state_t *app) {
    if (app->mini_cover) rubraview_pal_texture_destroy(app->mini_cover);
    app->mini_cover = NULL;
    app->mini_cover_w = app->mini_cover_h = 0;
    const rubraview_tags_t *tags = mini_tags(app);
    if (!app->mini_renderer || !tags->art || tags->art_size == 0) return;
    rubraview_image_load_result_t art = rubraview_pal_image_load_texture_from_memory(
        app->mini_renderer, tags->art, tags->art_size, false);
    if (art.texture) {
        app->mini_cover = art.texture;
        app->mini_cover_w = art.width;
        app->mini_cover_h = art.height;
    }
}

void mini_show(app_state_t *app) {
    if (app->mini_open) { mini_close(app); return; }   /* the same key puts it away */
    if (!mini_target(app)) {
        osd_say(app, TU8("there is no music to show"));
        return;
    }
    rubraview_window_config_t config = {
        .title = T("Rubraview music"), .width = MINI_WIDTH, .height = MINI_HEIGHT + 24, .owner = app->window,
    };
    app->mini_window = rubraview_pal_window_create(window_block_take(app, &app->mini_block), &config);
    if (!app->mini_window) {
        osd_say(app, TU8("the mini player could not be opened"));
        return;
    }
    if (app->mini_frame[2] > 0 && app->mini_frame[3] > 0) {
        rubraview_pal_window_set_frame(app->mini_window, app->mini_frame[0], app->mini_frame[1],
                                       app->mini_frame[2], app->mini_frame[3]);
    }
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->mini_window, &w, &h);
    app->mini_renderer = rubraview_pal_render_create(window_block_arena(app, &app->mini_block),
                                                     rubraview_pal_window_native_handle(app->mini_window), w, h);
    ui_font_apply_all(app);
    if (!app->mini_renderer) {
        rubraview_pal_window_destroy(app->mini_window); window_block_give(app, &app->mini_block);
        app->mini_window = NULL;
        osd_say(app, TU8("the mini player could not be drawn"));
        return;
    }
    /* Small and on top: it is meant to sit over whatever is being read. */
    rubraview_pal_window_set_topmost(app->mini_window, true);
    app->mini_open = true;
    app->mini_hover = -1;
    mini_cover_load(app);
    app->mini_dirty = true;
}

/* Where each part of the little window is, in its own pixels. */
static rubraview_pal_rect_t mini_cover_rect(double dpi, double win_h) {
    double m = 6.0 * dpi;
    double side = win_h - 2.0 * m;
    if (side < 16.0 * dpi) side = 16.0 * dpi;
    return (rubraview_pal_rect_t){ m, m, side, side };
}

static rubraview_pal_rect_t mini_button_rect(double dpi, double win_w, double win_h, int32_t which) {
    (void)win_h;
    double side = 24.0 * dpi, gap = 4.0 * dpi;
    double right = win_w - 6.0 * dpi;
    double x = right - (3 - which) * side - (2 - which) * gap;
    return (rubraview_pal_rect_t){ x, 6.0 * dpi, side, side };
}

/* The strip runs from beside the cover to the window's edge, under the
   words and the buttons. */
static rubraview_pal_rect_t mini_strip_rect(double dpi, double win_w, double win_h) {
    double left = mini_cover_rect(dpi, win_h).width + 12.0 * dpi;
    double width = win_w - left - 46.0 * dpi;    /* room for the clock at the end */
    if (width < 20.0 * dpi) width = 20.0 * dpi;
    return (rubraview_pal_rect_t){ left, win_h - 14.0 * dpi, width, 5.0 * dpi };
}

/* ---- the mini player: what it shows and what its buttons do ---- */

/* Play or pause whichever track is sounding. When it is the background
   music, the listener's own pause is recorded — it outranks the arbiter
   from then on (D-30). */
static void mini_toggle_play(app_state_t *app) {
    if (app->bgm_media) {
        bool pausing = !app->bgm_paused;
        rubraview_pal_media_set_paused(app->bgm_media, pausing);
        app->bgm_paused = pausing;
        (void)rubraview_bgm_event(&app->bgm,
                                  pausing ? RUBRAVIEW_BGM_READER_PAUSED : RUBRAVIEW_BGM_READER_RESUMED,
                                  bgm_pause_for_sound(app));
    } else if (app->media && !app->media_info.has_video) {
        handle_action(app, PROVEN_LIT("media_play_pause"));
    }
    app->mini_dirty = true;
}

/* The track before or after the one being heard, in the same folder. */
static int32_t mini_neighbour_page(const app_state_t *app, bool forward) {
    int32_t from = mini_page(app);
    if (from < 0) return -1;
    int32_t step = forward ? 1 : -1;
    for (int32_t i = from + step; i >= 0 && (size_t)i < page_count(app); i += step) {
        proven_u8str_view_t path = app->source.pages[i].path;
        if (is_media_path(path) && rubraview_tags_is_music_name(rubraview_path_basename(path))) return i;
    }
    return -1;
}

/* Play that track. If the page on screen is the one being heard, the
   viewer simply turns to it; otherwise it becomes the background music
   and the reader stays where they are. */
static void mini_go_to(app_state_t *app, int32_t index) {
    if (index < 0) return;
    if (!app->bgm_media) {
        go_to_spread(app, spread_for_chosen_page(app, index));
        update_precache(app);
        app->mini_dirty = true;
        return;
    }
    proven_u8str_view_t path = app->source.pages[index].path;
    rubraview_media_backend_t order[2];
    size_t count = rubraview_media_backend_order(app->media_preferred,
                                                 rubraview_pal_media_backend_available(RUBRAVIEW_BACKEND_FFMPEG),
                                                 order);
    for (size_t i = 0; i < count; ++i) {
        filmcache_for(app, path);
        rubraview_media_open_result_t opened = rubraview_pal_media_open(path, order[i], NULL);
        if (!opened.media) continue;
        bool was_paused = app->bgm_paused;
        bgm_close(app);
        app->bgm_media = opened.media;
        app->bgm_info = opened.info;
        app->bgm_page = index;
        app->bgm_position = 0.0;
        app->bgm_paused = was_paused;
        app->bgm_tags = (rubraview_tags_t){0};
        if (app->mem) rubraview_region_reset(&app->mem->bgm);   /* RFC-0006: the track before's words given back */
        {
            proven_arena_t *ba = app->mem && rubraview_region_ensure(&app->mem->bgm, MUSIC_MAX_HEAD_BYTES + (2u << 20)) ? &app->mem->bgm.arena : app->arena;
            proven_u8str_view_t head = rubraview_pal_fs_read_file(ba, path, MUSIC_MAX_HEAD_BYTES);
            if (head.size > 0) {
                app->bgm_tags = rubraview_tags_read(ba, (rubraview_tags_source_t){
                    .head = (const uint8_t*)head.ptr, .head_size = head.size, .file_size = head.size });
            }
        }
        rubraview_pal_media_set_paused(app->bgm_media, was_paused);
        bgm_do(app, rubraview_bgm_event(&app->bgm, RUBRAVIEW_BGM_MUSIC_OPENED, bgm_pause_for_sound(app)));
        mini_cover_load(app);
        app->mini_dirty = true;
        return;
    }
    osd_say(app, TU8("that track could not be opened"));
}

static void mini_seek_to(app_state_t *app, double fraction) {
    rubraview_media_t *m = mini_target(app);
    double duration = mini_duration(app);
    if (!m || duration <= 0.0) return;
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    double where = fraction * duration;
    if (app->bgm_media) {
        rubraview_pal_media_seek(m, where);
        app->bgm_position = where;
    } else {
        media_seek_to(app, where);
    }
    app->mini_dirty = true;
}

/* A click: a button, or the strip. */
static void mini_press(app_state_t *app, double x, double y) {
    double dpi = rubraview_pal_window_dpi_scale(app->mini_window);
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->mini_window, &w, &h);
    for (int32_t i = 0; i < 3; ++i) {
        rubraview_pal_rect_t b = mini_button_rect(dpi, w, h, i);
        if (x >= b.x && x < b.x + b.width && y >= b.y && y < b.y + b.height) {
            if (i == 0) mini_go_to(app, mini_neighbour_page(app, false));
            else if (i == 1) mini_toggle_play(app);
            else mini_go_to(app, mini_neighbour_page(app, true));
            return;
        }
    }
    rubraview_pal_rect_t strip = mini_strip_rect(dpi, w, h);
    if (y >= strip.y - 8.0 * dpi && y <= strip.y + strip.height + 8.0 * dpi &&
        x >= strip.x && x <= strip.x + strip.width && strip.width > 0.0) {
        mini_seek_to(app, (x - strip.x) / strip.width);
    }
}

void draw_mini_window(app_state_t *app) {
    if (!app->mini_open || !app->mini_dirty) return;
    app->mini_dirty = false;
    rubraview_renderer_t *r = app->mini_renderer;
    double dpi = rubraview_pal_window_dpi_scale(app->mini_window);
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->mini_window, &w, &h);
    rubraview_pal_render_begin(r, MINI_BACKGROUND);

    /* Nothing is playing: say so, rather than leave the last track's
       place on the strip looking like something that is still there. */
    if (!mini_target(app)) {
        rubraview_pal_rect_t all = { 0.0, 0.0, (double)w, (double)h };
        rubraview_pal_render_draw_text(r, TU8("nothing is playing"), all, 13.0 * dpi,
                                       0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
        if (!rubraview_pal_render_end(r)) mini_close(app);
        return;
    }

    /* The cover, or a plain square where one would be. */
    rubraview_pal_rect_t cover = mini_cover_rect(dpi, h);
    if (app->mini_cover && app->mini_cover_w > 0 && app->mini_cover_h > 0) {
        rubraview_mat3x2_t place = rubraview_mat3x2_multiply(
            rubraview_mat3x2_scale(cover.width / (double)app->mini_cover_w,
                                   cover.height / (double)app->mini_cover_h),
            rubraview_mat3x2_translate(cover.x, cover.y));
        rubraview_pal_render_draw_texture(r, app->mini_cover, place, RUBRAVIEW_INTERP_LINEAR);
    } else {
        rubraview_pal_render_fill_rect(r, cover, COLOR_TILE_FILL, 3.0);
    }

    /* The words: the track, then who made it. */
    const rubraview_tags_t *tags = mini_tags(app);
    double left = cover.x + cover.width + 6.0 * dpi;
    double text_w = (double)w - left - 6.0 * dpi - 3.0 * 28.0 * dpi;
    if (text_w < 40.0 * dpi) text_w = 40.0 * dpi;
    proven_u8str_view_t title = tags->title;
    if (title.size == 0) {
        int32_t page = mini_page(app);
        if (page >= 0 && (size_t)page < page_count(app)) {
            title = rubraview_path_basename(app->source.pages[page].path);
        }
    }
    rubraview_pal_rect_t line1 = { left, 6.0 * dpi, text_w, 18.0 * dpi };
    rubraview_pal_render_draw_text(r, title, line1, 13.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    if (tags->artist.size > 0) {
        rubraview_pal_rect_t line2 = { left, 25.0 * dpi, text_w, 16.0 * dpi };
        rubraview_pal_render_draw_text(r, tags->artist, line2, 11.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
    }

    /* The three buttons: back a track, play or pause, on a track. */
    static const char *const GLYPH[3] = { "|<", "||", ">|" };
    for (int32_t i = 0; i < 3; ++i) {
        rubraview_pal_rect_t b = mini_button_rect(dpi, w, h, i);
        rubraview_pal_render_fill_rect(r, b, app->mini_hover == i ? COLOR_TILE_CURRENT : COLOR_TILE_FILL, 3.0);
        const char *glyph = i == 1 ? (mini_paused(app) ? " >" : "||") : GLYPH[i];
        rubraview_pal_render_draw_text(r, cstr(glyph), b, 13.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }

    /* Where the track has got to, and how far it goes. */
    rubraview_pal_rect_t strip = mini_strip_rect(dpi, w, h);
    rubraview_pal_render_fill_rect(r, strip, 0x40FFFFFFu, 2.0);
    double duration = mini_duration(app);
    if (duration > 0.0) {
        double fraction = mini_position(app) / duration;
        if (fraction < 0.0) fraction = 0.0;
        if (fraction > 1.0) fraction = 1.0;
        rubraview_pal_rect_t done = { strip.x, strip.y, strip.width * fraction, strip.height };
        rubraview_pal_render_fill_rect(r, done, COLOR_TILE_CURRENT, 2.0);
    }
    char clock[64];
    proven_u8str_view_t at = rubraview_format_timecode(clock, sizeof(clock), mini_position(app), false);
    rubraview_pal_rect_t time_rect = { strip.x + strip.width + 4.0 * dpi, h - 20.0 * dpi,
                                       (double)w - (strip.x + strip.width) - 8.0 * dpi, 16.0 * dpi };
    rubraview_pal_render_draw_text(r, at, time_rect, 11.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);

    if (!rubraview_pal_render_end(r)) mini_close(app);
}

size_t mini_pump(app_state_t *app) {
    size_t handled = 0;
    rubraview_window_event_t event;
    while (app->mini_open && rubraview_pal_window_poll_event(app->mini_window, &event)) {
        handled++;
        switch (event.kind) {
            case RUBRAVIEW_WINDOW_EVENT_CLOSE:
                mini_close(app);
                return handled;
            case RUBRAVIEW_WINDOW_EVENT_RESIZE:
                rubraview_pal_render_resize(app->mini_renderer, event.resize.width, event.resize.height);
                app->mini_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_PAINT:
            case RUBRAVIEW_WINDOW_EVENT_DPI_CHANGED:
                app->mini_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE: {
                double dpi = rubraview_pal_window_dpi_scale(app->mini_window);
                int32_t w = 0, h = 0;
                rubraview_pal_window_get_size(app->mini_window, &w, &h);
                int32_t was = app->mini_hover;
                app->mini_hover = -1;
                for (int32_t i = 0; i < 3; ++i) {
                    rubraview_pal_rect_t b = mini_button_rect(dpi, w, h, i);
                    if (event.mouse.x >= b.x && event.mouse.x < b.x + b.width &&
                        event.mouse.y >= b.y && event.mouse.y < b.y + b.height) {
                        app->mini_hover = i;
                        break;
                    }
                }
                if (app->mini_hover != was) app->mini_dirty = true;
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN:
                mini_press(app, event.mouse.x, event.mouse.y);
                break;
            case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN: {
                rubraview_key_combo_t combo = event.key.combo;
                if (key_is(combo, "Escape")) { mini_close(app); return handled; }
                if (key_is(combo, "Space")) mini_toggle_play(app);
                break;
            }
            default:
                break;
        }
    }
    return handled;
}
