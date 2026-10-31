/*
 * Keys and actions: the menu and toolbox kept in step, a tile's caption and facts, speed, layout and panning,
 * an action carried out, the picker's and the boxes' keys, a key dispatched (RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* The box's grid must match however many tiles the current menu level
   shows, which changes as the reader drills in and back out. */
void sync_menubox_tiles(app_state_t *app) {
    int32_t count = rubraview_menu_visible_count(&app->menu);
    if (count > MENU_MAX_TILES) count = MENU_MAX_TILES;
    app->menubox.tile_count = count;
}

static uint32_t boxes_when(const app_state_t *app) {
    /* RV-116: Playback for an animation too; Sound and Subtitles for a film or a song only */
    return app->media ? RUBRAVIEW_BOXES_WHEN_MEDIA | RUBRAVIEW_BOXES_WHEN_PLAYS : anim_plays(app) ? RUBRAVIEW_BOXES_WHEN_PLAYS : 0u;
}

/* RFC-0002 §5: the menu tree, built again from the document — with the
   reading history's newest entries under File › Recent — and the menu
   back at its root. */
static unsigned char g_menu_memory[1 << 16];   /* the tree lives here until it is built again */

void menu_rebuild(app_state_t *app) {
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = g_menu_memory, .size = sizeof(g_menu_memory) });
    enum { RECENT_MAX = 10 };
    rubraview_recent_entry_t recent[RECENT_MAX];
    size_t recent_count = 0;
    /* D-104: the files opened lately, newest first; another viewer's too. */
    recent_sync(app, PROVEN_LIT(""), false);
    size_t newest[RECENT_MAX];
    size_t shown = rubraview_recent_newest(&app->recent, newest, RECENT_MAX);
    for (size_t r = 0; r < shown; ++r) {
        char action[32];
        int n = snprintf(action, sizeof(action), "open_recent:%zu", newest[r]);
        proven_result_mem_mut_t text = proven_arena_alloc(&arena, (size_t)n + 1);
        if (n <= 0 || !proven_is_ok(text.err)) break;
        memcpy(text.value.ptr, action, (size_t)n + 1);
        recent[recent_count++] = (rubraview_recent_entry_t){
            .label = rubraview_path_basename(app->recent.list.entries[newest[r]].path),
            .action = { .ptr = (const proven_byte_t *)(const char*)text.value.ptr, .size = (size_t)n },
        };
    }
    app->menu_when = boxes_when(app);
    app->menu_tree = rubraview_boxes_menu(&arena, rubraview_boxes_document(), app->menu_when,
                                          recent, recent_count, MENU_MAX_TILES - 1);
    app->menu = rubraview_menu_create(&app->menu_tree);
    rubraview_confirm_clear(&app->menu_confirm);
    sync_menubox_tiles(app);
}

/* RFC-0002 §4: which toolbox profile what is on screen calls for — the
   same tests dispatch_key uses to pick a key context. */
const char *toolbox_profile_name(const app_state_t *app) {
    if (text_active(app)) return "text";
    if (app->media) return app->media_info.has_video ? "video" : "music";
    if (app->anim_active) return app->animation.kind == RUBRAVIEW_FRAMES_ANIMATION ? "animation" : "multipage";
    if (app->source.archive_path.size > 0) return "archive";
    return "image";
}

void strip_title_plan(app_state_t *app);

void toolbox_refresh(app_state_t *app) {
    const rubraview_boxes_doc_t *doc = rubraview_boxes_document();
    int32_t count = 0;
    const rubraview_toolbox_profile_t *parts[2] = {
        app->slideshow_running ? rubraview_boxes_profile(doc, PROVEN_LIT("slideshow")) : NULL,
        rubraview_boxes_profile(doc, cstr(toolbox_profile_name(app))),
    };
    /* D-124: the groups, in order: the slide show's own first, then the profile's. */
    int32_t groups = 0, last_part = -1, last_group = -1;
    memset(app->toolbox.group_sizes, 0, sizeof(app->toolbox.group_sizes));
    for (size_t p = 0; p < 2; ++p) {
        if (!parts[p]) continue;
        for (int32_t i = 0; i < parts[p]->tile_count && count < RUBRAVIEW_TOOLBOX_MAX_TILES; ++i) {
            const rubraview_box_tile_t *tile = &doc->tiles[parts[p]->first_tile + i];
            /* The slide show's own Stop tile stands in for the profile's Slides tile. */
            if (p == 1 && parts[0] && proven_u8str_view_eq(tile->action, PROVEN_LIT("toggle_slideshow"))) continue;
            /* D-123: and its Delay tile for the speed tile, while it runs. */
            if (p == 1 && parts[0] && proven_u8str_view_eq(tile->action, PROVEN_LIT("media_speed_cycle"))) continue;
            if (edit_panel_open(app) && edit_denies(tile->action)) continue;   /* RV-137: not while Edit is open */
            if (((int32_t)p != last_part || tile->group != last_group) && groups < RUBRAVIEW_TOOLBOX_MAX_GROUPS) groups++;
            last_part = (int32_t)p;
            last_group = tile->group;
            app->toolbox.group_sizes[groups - 1]++;
            app->toolbox_tiles[count++] = *tile;
        }
    }
    app->toolbox_tile_count = count;
    app->toolbox.tile_count = count;
    app->toolbox.group_count = groups;
    /* The strip's seek bar, for a film or music; for a still picture with
       others around it, where it is among them (owner, 2026-09-28). */
    app->toolbox.timeline = app->media != NULL || anim_plays(app) || page_count(app) > 1 || app->pagebar_dragging;
    strip_title_plan(app);
    /* The menu follows what is on screen too, but only while it is at its
       root: a reader halfway down a submenu is not pulled back. */
    if (app->menu_when != boxes_when(app) && app->menu.depth == 0) menu_rebuild(app);
}

/* "1x", "1.25x", "0.5x": two decimals, trailing zeros dropped. */
int speed_text(char *buffer, size_t size, double speed);

/* The speed of what plays: a film or music, or an animation (D-82). */
void speed_set(app_state_t *app, double speed) {
    if (speed < 0.25) speed = 0.25;
    if (speed > 4.0) speed = 4.0;
    app->media_speed = speed;
    if (app->media) {
        rubraview_media_clock_set_rate(&app->media_clock, speed, rubraview_pal_time_now_seconds());
        rubraview_pal_audio_set_speed(speed);
    } else {
        app->animation.speed = speed;   /* D-82: the same ladder, and kept for the session as a film's is */
    }
    char line[32];
    char text[16];
    speed_text(text, sizeof(text), speed);
    int n = snprintf(line, sizeof(line), T("speed %s"), text);
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
}

int speed_text(char *buffer, size_t size, double speed) {
    int n = snprintf(buffer, size, "%.2f", speed);
    if (n <= 0 || (size_t)n >= size) return 0;
    while (n > 0 && buffer[n - 1] == '0') buffer[--n] = '\0';
    if (n > 0 && buffer[n - 1] == '.') buffer[--n] = '\0';
    if ((size_t)n + 1 < size) { buffer[n++] = 'x'; buffer[n] = '\0'; }
    return n;
}

/* D-123: the slide show's delay, said as it changes. */
void interval_set(app_state_t *app, double seconds) {
    rubraview_slideshow_set_interval(&app->slideshow, seconds);
    char text[16];
    char line[40];
    if (rubraview_slideshow_interval_text(text, sizeof(text), app->slideshow.interval_seconds) <= 0) return;
    int n = snprintf(line, sizeof(line), T("slide delay %s"), text);
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
}

/* A toggle tile says what a tap will do; a tile that can do nothing now is dimmed. */
/* What a tile or a menu item can tell about its action: the facts of the
   page on screen, read the same way for both boxes. */
rubraview_action_facts_t action_facts(const app_state_t *app) {
    return (rubraview_action_facts_t){
        .has_page = page_count(app) > 0,
        .archive_series = app->source.archive_path.size > 0,
        .folder_series = app->source.archive_path.size == 0 && !app->list_is_set && page_count(app) > 0,
        .media = app->media != NULL,
        .video = app->media != NULL && app->media_info.has_video,
        .frames = app->anim_active,
        .other_audio_track = rubraview_tracks_next(&app->tracks, RUBRAVIEW_TRACK_AUDIO, app->tracks.current_audio) >= 0 &&
                             rubraview_tracks_next(&app->tracks, RUBRAVIEW_TRACK_AUDIO, app->tracks.current_audio) != app->tracks.current_audio,
        .other_subtitle = rubraview_tracks_next(&app->tracks, RUBRAVIEW_TRACK_SUBTITLE, app->tracks.current_subtitle) >= 0,
        .subtitle_shown = app->tracks.current_subtitle >= 0,
        .has_subtitles = rubraview_tracks_count(&app->tracks, RUBRAVIEW_TRACK_SUBTITLE) > 0,
        .slideshow = app->slideshow_running,
        /* D-82: an animation's own end, said in the film's words. */
        .repeat_mode = anim_plays(app) ? (anim_end_mode(app) == 0 ? (int32_t)RUBRAVIEW_REPEAT_ONE
                                        : anim_end_mode(app) == 1 ? (int32_t)RUBRAVIEW_REPEAT_STOP
                                                                  : (int32_t)RUBRAVIEW_REPEAT_NEXT)
                                     : (int32_t)repeat_mode(app),
        .aspect = (int32_t)app->aspect,
        .aspect_on_its_side = app->aspect_side,
        .filmstrip = app->filmstrip.visible,
        .osd = app->osd.always_on,
        .toolbox_pinned = app->toolbox.pinned,
        .titlebar_pinned = rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("titlebar_pinned")) > 0.5,
        .wheel_strip = rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("wheel_strip")) > 0.5,
        .toolbox_detached = app->toolbox.state == RUBRAVIEW_BOX_DETACHED,
        .fullscreen = rubraview_pal_window_is_fullscreen(app->window),
        .nearest = app->force_nearest,
        .pixel_grid = app->pixel_grid,
        .spread_detect = app->spread_detect,
        .fit_lock = app->fit_lock,
        .always_on_top = rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("always_on_top")) > 0.5,
        .muted = rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("mute")) > 0.5,
        .layout = app->layout_opts.mode,
        .fit = app->fit_mode,
        .rtl = app->layout_opts.direction == RUBRAVIEW_READING_RTL,
        .playing = app->media ? !app->media_paused : (app->anim_active && !app->animation.paused),
    };
}


/* D-94: what page_picture_size stretches to. Editing works in the file's
   own pixels, so while the edit panel is open the shape is the file's. */
void aspect_refresh(app_state_t *app) {
    double ratio = edit_panel_open(app) ? 0.0
        : rubraview_aspect_ratio(app->aspect, rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("aspect_width")),
                                 rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("aspect_height")), app->aspect_side);
    double w = 0.0, h = 0.0;
    rubraview_orientation_apply_size(app->orientation, 1.0, 2.0, &w, &h);
    bool turned = w > h;
    if (ratio == g_view_ratio && turned == g_view_turned) return;
    g_view_ratio = ratio;
    g_view_turned = turned;
    app->strip_base_w = 0.0;   /* the strip's width is a page's */
    app->needs_relayout = true;
}

static void aspect_set(app_state_t *app, rubraview_aspect_t aspect, bool side) {
    app->aspect = aspect;
    app->aspect_side = aspect == RUBRAVIEW_ASPECT_ORIGINAL ? false : side;
    aspect_refresh(app);
    char line[64];
    double ratio = g_view_ratio;
    int n = ratio > 0.0 ? snprintf(line, sizeof(line), T("shape: %s%s (%.3g:1)"), rubraview_aspect_label(aspect), app->aspect_side ? T(", on its side") : "", ratio)
                        : snprintf(line, sizeof(line), T("shape: the file's own"));
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1 });
}

static proven_u8str_view_t toolbox_caption_in(const app_state_t *app, const rubraview_box_tile_t *tile_in, bool *out_enabled);

/* The owner (2026-10-18): "툴바 아이콘 위의 텍스트는 언제나 영어로 ... 아이콘 안의 텍스트만
   영어로 해야 넘치지 않아요" — a tile's own words are English whatever the language, so
   they fit it; its tooltip, the menus and every message stay in the language in use. */
/* The fit tile has no key of its own: the key of the fit it shows (owner, 2026-10-18). */
const char *fit_action_of(rubraview_fit_mode_t fit) {
    switch (fit) {
        case RUBRAVIEW_FIT_WIDTH: return "fit_width";
        case RUBRAVIEW_FIT_HEIGHT: return "fit_height";
        case RUBRAVIEW_FIT_STRETCH: return "fit_stretch";
        case RUBRAVIEW_FIT_ACTUAL_SIZE: return "actual_size";
        case RUBRAVIEW_FIT_SMART: return "smart_fit";
        case RUBRAVIEW_FIT_WINDOW: default: return "fit_window";
    }
}

proven_u8str_view_t toolbox_caption(const app_state_t *app, const rubraview_box_tile_t *tile_in, bool *out_enabled) {
    const rubraview_lang_t *pack = rubraview_lang_current();
    rubraview_lang_use(NULL);
    proven_u8str_view_t caption = toolbox_caption_in(app, tile_in, out_enabled);
    rubraview_lang_use(pack);
    /* 1.25.0 (owner, 2026-10-20: "아이콘에는 단축키를 표시하지 말고 툴팁이나 ... 에만"): the keys are in the tooltip only. */
    return caption;
}

static proven_u8str_view_t toolbox_caption_in(const app_state_t *app, const rubraview_box_tile_t *tile_in, bool *out_enabled) {
    rubraview_box_tile_t shown = *tile_in;
    shown.caption = rubraview_tr_ctx_u8("toolbox", tile_in->caption);   /* English here: see toolbox_caption */
    const rubraview_box_tile_t *tile = &shown;
    rubraview_action_facts_t facts = action_facts(app);
    rubraview_action_state_t st = rubraview_action_state(tile->action, &facts);
    *out_enabled = st.enabled;
    if (st.value) {
        /* A setting the tile names, "Order: R>L": drawn before the next call. */
        static char named[48];
        return rubraview_tile_caption(tile->caption, false, st.mark, st.value, named, sizeof(named));
    }
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("zoom_reset")) || proven_u8str_view_eq(tile->action, PROVEN_LIT("zoom_cycle"))) {
        /* D-97: the zoom now, where 100% is the size the fit option gives. D-109: the one
           tile that says it goes its round on a tap (`zoom_cycle`). */
        static char percent[16];   /* drawn before the next call */
        /* Real size is not a point on that scale: the owner's name for it is 0%. */
        if (app->fit_mode == RUBRAVIEW_FIT_ACTUAL_SIZE && fabs(app->zoom - 1.0) < 0.0005) return TU8("Real\nsize");   /* 0 %, on two rows (owner, 2026-10-15) */
        proven_u8str_view_t text = rubraview_scale_caption(percent, sizeof(percent), app->zoom);
        return text.size > 0 ? text : tile->caption;
    }
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("fit_cycle"))) return cstr(rubraview_fit_tile_name(app->fit_mode));
    /* 1.25.0 (owner, 2026-10-20): the encoding the text is read in, short, on two rows: EUC / KR, SHFT / JIS */
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("text_encoding")) && app->text) return cstr(rubraview_textenc_short((rubraview_textenc_t)text_enc_now(app)));
    /* Two pages (owner, 2026-10-17): a wide page alone by its shape, or always two. */
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("toggle_spread_detect"))) return app->spread_detect ? TU8("Wide\nalone") : TU8("Always\ntwo");
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("text_wrap"))) return TU8("Word\nwrap");   /* on two rows (owner, 2026-10-17) */
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("media_ab_edit"))) return TU8("Set\nA-B");   /* on two rows (owner, 2026-10-15) */
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("media_play_pause"))) {
        bool paused = app->media ? app->media_paused : app->animation.paused;
        return paused ? TU8("Play") : TU8("Pause");
    }
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("media_mute"))) {
        return rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("mute")) > 0.5 ? TU8("Unmute") : TU8("Mute");
    }
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("media_speed_cycle"))) {
        static char speed[16];   /* drawn before the next call */
        int n = speed_text(speed, sizeof(speed), app->media_speed > 0.0 ? app->media_speed : 1.0);
        return n > 0 ? (proven_u8str_view_t){ .ptr = (const proven_byte_t *)speed, .size = (size_t)n } : tile->caption;
    }
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("interval_cycle"))) {
        static char delay[16];   /* drawn before the next call */
        int n = rubraview_slideshow_interval_text(delay, sizeof(delay), app->slideshow.interval_seconds);
        return n > 0 ? (proven_u8str_view_t){ .ptr = (const proven_byte_t *)delay, .size = (size_t)n } : tile->caption;
    }
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("media_repeat_cycle"))) {
        /* "To Next" on two lines (owner, 2026-10-11): "Next" alone read as the next-file button. */
        if (anim_plays(app)) return anim_end_mode(app) == 0 ? TU8("Loop") : anim_end_mode(app) == 1 ? TU8("Once") : TU8("To\nNext");
        if (repeat_mode(app) == RUBRAVIEW_REPEAT_NEXT) return TU8("To\nNext");
        return cstr(rubraview_repeat_caption(repeat_mode(app)));
    }
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("media_eq_cycle"))) {
        int preset = eq_preset_now(app);
        return preset <= 0 ? TU8("EQ") : eq_preset_label(preset);
    }
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("media_night_toggle"))) {
        return rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("night_mode")) > 0.5 ? TU8("Night on") : TU8("Night");
    }
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("media_visualizer_cycle"))) {
        const char *const V[] = { T("Viz"), T("Bars"), T("Wave") };
        int v = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("visualizer")));
        return cstr(V[v >= 0 && v < 3 ? v : 0]);
    }
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("media_ab_cycle"))) {
        return app->ab_a < 0.0 ? PROVEN_LIT("A-B") : app->ab_b < 0.0 ? TU8("Set B") : TU8("A-B off");
    }
    return tile->caption;
}

/* The volume as the OSD says it: "volume 70%" or "muted (70%)". */
static void media_say_volume(app_state_t *app) {
    char line[48];
    int volume = (int)rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("volume"));
    bool muted = rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("mute")) > 0.5;
    int n = muted ? snprintf(line, sizeof(line), T("muted (%d%%)"), volume)
                  : snprintf(line, sizeof(line), T("volume %d%%"), volume);
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
}

/* D-16: one step of moving or sizing the window from the keyboard. The
   frame stays on a screen (the PAL pulls it back) and never smaller than
   a usable minimum; a fullscreen window is left alone. */
static bool window_nudge(app_state_t *app, proven_u8str_view_t action) {
    static const struct { const char *name; int dx, dy, dw, dh; } STEPS[] = {
        { "window_move_left", -1, 0, 0, 0 }, { "window_move_right", 1, 0, 0, 0 },
        { "window_move_up", 0, -1, 0, 0 },   { "window_move_down", 0, 1, 0, 0 },
        { "window_narrower", 0, 0, -1, 0 },  { "window_wider", 0, 0, 1, 0 },
        { "window_shorter", 0, 0, 0, -1 },   { "window_taller", 0, 0, 0, 1 },
    };
    for (size_t i = 0; i < sizeof(STEPS) / sizeof(STEPS[0]); ++i) {
        if (!proven_u8str_view_eq(action, proven_u8str_view_from_cstr(STEPS[i].name))) continue;
        if (rubraview_pal_window_is_fullscreen(app->window)) return true;
        int32_t x = 0, y = 0, w = 0, h = 0;
        if (!rubraview_pal_window_get_frame(app->window, &x, &y, &w, &h)) return true;
        int32_t step = (int32_t)(40.0 * rubraview_pal_window_dpi_scale(app->window));
        int32_t min_w = (int32_t)(320.0 * rubraview_pal_window_dpi_scale(app->window));
        int32_t min_h = (int32_t)(240.0 * rubraview_pal_window_dpi_scale(app->window));
        x += STEPS[i].dx * step;
        y += STEPS[i].dy * step;
        w += STEPS[i].dw * step;
        h += STEPS[i].dh * step;
        if (w < min_w) w = min_w;
        if (h < min_h) h = min_h;
        rubraview_pal_window_set_frame(app->window, x, y, w, h);
        return true;
    }
    return false;
}

/* The layout, from the tile, the menu or the key: written to the setting
   (Settings › Viewer › Layout shows the same thing and is saved with the
   rest), which is what applies it. The page on screen stays. */
/* A layout said: its short name first — the one its button and the settings use — then what it is (owner, 2026-10-14). */
const char *layout_said(rubraview_layout_choice_t choice) {
    const char *const SAY[RUBRAVIEW_LAYOUT_CHOICE_COUNT] = {
        T("single: one page at a time"),
        T("dual: two pages side by side, left to right"),
        T("dual-rtl: two pages side by side, right to left"),
        T("book: the cover alone, then two pages, left to right"),
        T("book-rtl: the cover alone, then two pages, right to left"),
        T("webtoon: every page one under the other, at one width"),
        T("comic: one page, a wide scan as its two halves, left first"),
        T("comic-rtl: one page, a wide scan as its two halves, the right one first") };
    return SAY[choice >= 0 && choice < RUBRAVIEW_LAYOUT_CHOICE_COUNT ? choice : 0];
}

void layout_set(app_state_t *app, rubraview_layout_choice_t choice, bool say) {
    int32_t page = current_page_index(app);
    if (page >= 0 && app->spread_index < app->layout.count) {
        int32_t other = app->layout.spreads[app->spread_index].right_index;
        if (other >= 0 && other < page) page = other;
    }
    rubraview_settings_set(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("layout"), (double)(int)choice);
    settings_took_effect(app);
    rebuild_layout(app);
    if (page >= 0) app->spread_index = spread_index_for_page(app, page);
    reset_view(app);
    menu_rebuild(app);
    sync_menubox_tiles(app);
    if (say) {
        osd_say(app, cstr(layout_said(choice)));
    }
}

/* The arrow keys over a picture larger than the window (owner, 2026-10-14): what lies past the
   edge they point to comes in, a seventh of the window a press, never past the picture's edge;
   a picture that fits does not move. The webtoon strip scrolls with Up and Down. */
static void pan_key(app_state_t *app, int dx, int dy) {
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    double step_x = (double)win_w / 7.0, step_y = (double)win_h / 7.0;
    if (dy != 0 && strip_active(app)) {
        strip_scroll_by(app, (double)dy * step_y);
        note_activity(app);
        return;
    }
    rubraview_composition_t comp;
    if (!spread_composition(app, &comp) || comp.count == 0) return;
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;   /* every page of the spread, turned as it is shown */
    for (size_t i = 0; i < comp.count; ++i) {
        const rubraview_draw_command_t *cmd = &comp.commands[i];
        double w = cmd->src_right - cmd->src_left, h = cmd->src_bottom - cmd->src_top;
        const double cx[4] = { 0.0, w, 0.0, w }, cy[4] = { 0.0, 0.0, h, h };
        for (int k = 0; k < 4; ++k) {
            double px = 0.0, py = 0.0;
            rubraview_mat3x2_apply(cmd->transform, cx[k], cy[k], &px, &py);
            if (px < x0) x0 = px;
            if (px > x1) x1 = px;
            if (py < y0) y0 = py;
            if (py > y1) y1 = py;
        }
    }
    double mx = dx ? rubraview_pan_key_delta((double)win_w, x0, x1 - x0, dx, step_x) : 0.0;
    double my = dy ? rubraview_pan_key_delta((double)win_h, y0, y1 - y0, dy, step_y) : 0.0;
    /* Zoomed out (owner, 2026-10-17): the smaller picture moves, inside the window. */
    if (app->zoom < 0.999) {
        if (dx && mx == 0.0) mx = rubraview_pan_key_delta_small((double)win_w, x0, x1 - x0, dx, step_x);
        if (dy && my == 0.0) my = rubraview_pan_key_delta_small((double)win_h, y0, y1 - y0, dy, step_y);
    }
    if (mx == 0.0 && my == 0.0) return;
    app->pan_x += mx;
    app->pan_y += my;
    note_activity(app);
}

/* RV-137 (owner, 2026-10-18: "에디트 상태에서 동작해야 할 버튼과 아닌 버튼을 구분하고, 작동 안하는건
   ... 안보이게"): what does not work while Edit is open — another view of the page, a show,
   another screen, the file itself changed or moved, a film's or a text's own controls. Hidden
   from the toolbox, and said when a key or the menu asks for it. */
bool edit_denies(proven_u8str_view_t action) {
    static const char *const NO[] = {
        "rotate_cw", "rotate_ccw", "rotate_180", "flip_horizontal", "flip_vertical", "toggle_layout", "toggle_spread_detect",
        "toggle_slideshow", "interval_cycle", "open_picker", "open_folder", "up_to_folder", "open_batch", "print",
        "rename_file", "delete_file", "purge_file", "copy_to_folder", "move_to_folder", "copy_to_other_folder",
        "move_to_other_folder", "convert_archive", "toggle_miniplayer", "toggle_subtitle_window", "toggle_subtitles",
        "next_audio_track", "next_subtitle_track", "toggle_classify",
    };
    for (size_t i = 0; i < sizeof(NO) / sizeof(NO[0]); ++i) if (proven_u8str_view_eq(action, proven_u8str_view_from_cstr(NO[i]))) return true;
    static const char *const PREFIX[] = { "media_", "anim_", "subpage_", "text_", "zoom_strip" };
    for (size_t i = 0; i < sizeof(PREFIX) / sizeof(PREFIX[0]); ++i) {
        size_t n = strlen(PREFIX[i]);
        if (action.size >= n && memcmp(action.ptr, PREFIX[i], n) == 0) return true;
    }
    return false;
}

/* While Edit is open the pages turned are pictures only: a film, a sound, a text is passed over. */
static bool edit_page_is_picture(const app_state_t *app, size_t i) {
    proven_u8str_view_t path = app->source.pages[i].path.size > 0 ? app->source.pages[i].path : app->source.pages[i].name;
    return !is_media_path(path) && !is_text_path(path) && rubraview_glob_match_list(rubraview_path_basename(path), PROVEN_LIT(IMAGE_FILTER));
}

static bool edit_turn(app_state_t *app, int dir, bool to_end) {
    size_t count = page_count(app);
    int32_t at = current_page_index(app);
    if (count == 0) return false;
    long i = to_end ? (dir > 0 ? (long)count - 1 : 0) : (long)at + dir;
    for (; i >= 0 && i < (long)count; i += to_end ? -dir : dir) {
        if (i == at) break;
        if (!edit_page_is_picture(app, (size_t)i)) continue;
        go_to_spread(app, spread_for_chosen_page(app, (int32_t)i));
        update_precache(app);
        return true;
    }
    osd_say(app, TU8("no other picture that way"));
    return true;
}

void handle_action(app_state_t *app, proven_u8str_view_t action) {
    if (text_action(app, action)) return;   /* RFC-0005 */
    if (action.size == 0) return;
    if (edit_panel_open(app) && edit_denies(action)) {
        osd_say(app, TU8("not while Edit is open (E closes it)"));
        return;
    }
    if (edit_panel_open(app)) {   /* pictures only, the rest passed over */
        if (proven_u8str_view_eq(action, PROVEN_LIT("next_page")) && edit_turn(app, +1, false)) return;
        if (proven_u8str_view_eq(action, PROVEN_LIT("prev_page")) && edit_turn(app, -1, false)) return;
        if (proven_u8str_view_eq(action, PROVEN_LIT("first_page")) && edit_turn(app, -1, true)) return;
        if (proven_u8str_view_eq(action, PROVEN_LIT("last_page")) && edit_turn(app, +1, true)) return;
    }
    note_activity(app);
    /* D-88: while the archive on screen is being written again nothing is
       opened, closed or changed; Esc stops the writing, and a quit does too. */
    if (app->arc_job && !proven_u8str_view_eq(action, PROVEN_LIT("quit"))) {
        osd_say(app, TU8("the archive is being written (Esc stops it)"));
        return;
    }
    /* D-89: with the question of what the archive becomes open, doing something else is "no". */
    if (app->arc_ask == ARC_ASK_FORMAT && !proven_u8str_view_eq(action, PROVEN_LIT("quit"))) panel_close(app);

    if (proven_u8str_view_eq(action, PROVEN_LIT("quit"))) {
        /* Esc closes what is open before it closes the program. */
        if (app->arc_job && !atomic_load(&app->arc_job->job.cancel)) {
            atomic_store(&app->arc_job->job.cancel, true);
            osd_say(app, PROVEN_LIT("stopping..."));
            return;
        }
        if (app->settings_open) { settings_close(app); return; }
        if (app->panel.open) { panel_close(app); return; }
        if (app->recentwin.open) { app->recentwin.open = false; return; }   /* the owner's recent window (2026-10-17) */
        if (app->favwin.open) { app->favwin.open = false; return; }         /* 1.24.0 */
        if (app->listwin.open) { app->listwin.open = false; return; }
        if (app->eqwin.open) { app->eqwin.open = false; return; }
        /* RV-116: an open menu box or toolbox (not pinned) folds first; it used to close the program */
        {
            rubraview_box_t *boxes[2] = { &app->menubox, &app->toolbox };
            bool folded = false;
            for (int i = 0; i < 2; ++i) {
                rubraview_box_state_t st = boxes[i]->state;
                if ((st == RUBRAVIEW_BOX_EXPANDED || st == RUBRAVIEW_BOX_LOCKED_OPEN) && !boxes[i]->pinned) {
                    rubraview_box_dismiss(boxes[i]);
                    folded = true;
                }
            }
            if (folded) { rubraview_menu_reset(&app->menu); sync_menubox_tiles(app); return; }
        }
        if (!subs_may_leave(app)) return;
        rubraview_pal_window_request_close(app->window);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("next_page")) && cue_step(app, +1)) {
        /* a `.cue`'s next track, in the same file (owner, 2026-09-30) */
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("prev_page")) && cue_step(app, -1)) {
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("next_page"))) {
        next_spread(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("prev_page"))) {
        prev_spread(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_eq_cycle"))) {
        int preset = (eq_preset_now(app) + 1) % RUBRAVIEW_EQWIN_PRESETS;
        rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("eq_preset"), (double)preset);
        audio_chain_update(app);
        char line[64];
        proven_u8str_view_t name = eq_preset_label(preset);
        int n = snprintf(line, sizeof(line), T("equaliser: %.*s"), (int)name.size, name.ptr);
        if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_eq_window"))) {
        eqwin_toggle(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_night_toggle"))) {
        bool on = rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("night_mode")) < 0.5;
        rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("night_mode"), on ? 1.0 : 0.0);
        audio_chain_update(app);
        osd_say(app, on ? TU8("night mode: loud passages brought down") : TU8("night mode off"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_visualizer_cycle"))) {
        int v = ((int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("visualizer"))) + 1) % 3;
        rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("visualizer"), (double)v);
        const char *const SAY[] = { T("analyser off"), T("analyser: spectrum"), T("analyser: oscilloscope") };
        osd_say(app, cstr(SAY[v]));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("first_page"))) {
        go_to_spread(app, 0);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("last_page"))) {
        go_to_spread(app, app->layout.count > 0 ? app->layout.count - 1 : 0);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("fit_window"))) {
        app->fit_mode = RUBRAVIEW_FIT_WINDOW; reset_view(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("fit_width"))) {
        app->fit_mode = RUBRAVIEW_FIT_WIDTH; reset_view(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("fit_height"))) {
        app->fit_mode = RUBRAVIEW_FIT_HEIGHT; reset_view(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("actual_size"))) {
        if (app->fit_mode != RUBRAVIEW_FIT_ACTUAL_SIZE) app->fit_beside_real = app->fit_mode;
        app->fit_mode = RUBRAVIEW_FIT_ACTUAL_SIZE; reset_view(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("smart_fit"))) {
        app->fit_mode = RUBRAVIEW_FIT_SMART; reset_view(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("fit_stretch"))) {
        app->fit_mode = RUBRAVIEW_FIT_STRETCH; reset_view(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("fit_cycle"))) {
        /* the one fit tile (owner, 2026-10-15): window, width, height, smart, stretch */
        app->fit_mode = rubraview_fit_cycle_next(app->fit_mode); reset_view(app);
        const char *const SAID[] = { [RUBRAVIEW_FIT_WINDOW] = T("fit: the window"), [RUBRAVIEW_FIT_WIDTH] = T("fit: the width"),
                                            [RUBRAVIEW_FIT_HEIGHT] = T("fit: the height"), [RUBRAVIEW_FIT_SMART] = T("fit: smart (never enlarged)"),
                                            [RUBRAVIEW_FIT_STRETCH] = T("fit: stretched to fill") };
        if ((size_t)app->fit_mode < sizeof(SAID) / sizeof(SAID[0]) && SAID[app->fit_mode]) osd_say(app, cstr(SAID[app->fit_mode]));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_fit_lock"))) {
        app->fit_lock = !app->fit_lock;
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("skip_forward"))) {
        go_to_spread(app, app->spread_index + 10);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("skip_backward"))) {
        go_to_spread(app, app->spread_index > 10 ? app->spread_index - 10 : 0);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("up_to_folder"))) {
        /* §3.7.2: ascend to the parent directory, shown in the picker so
           the reader can choose what to open next. */
        proven_u8str_view_t here = app->picker_dir;
        if (here.size == 0) here = app->source_dir;
        proven_u8str_view_t parent = rubraview_picker_parent(here);
        if (parent.size > 0) {
            picker_navigate(app, parent);
            app->picker.focus = 0;
            app->picker_open = app->picker_listing.count > 0;
        }
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("open_folder"))) {
        picker_open(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_spread_detect"))) {
        /* §3.3.4: with detection off, a wide scan pairs like any other
           page instead of standing alone. */
        app->spread_detect = !app->spread_detect;
        app->layout_opts.spread_ar_threshold = app->spread_detect ? 1.15 : 1.0e9;
        app->needs_relayout = true;
        osd_say(app, app->spread_detect ? TU8("two pages: a wide page stands alone, by its shape")
                                        : TU8("two pages: always two, whatever their shape"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("interval_up"))) {
        interval_set(app, app->slideshow.interval_seconds + 0.5);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("interval_down"))) {
        interval_set(app, app->slideshow.interval_seconds - 0.5);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("interval_up_fine"))) {
        interval_set(app, app->slideshow.interval_seconds + 0.1);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("interval_down_fine"))) {
        interval_set(app, app->slideshow.interval_seconds - 0.1);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("interval_cycle"))) {
        interval_set(app, rubraview_slideshow_interval_cycle(app->slideshow.interval_seconds));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("zoom_reset"))) {
        reset_view(app);   /* D-97: 100% is the fitted size; the fit option stays */
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("zoom_cycle"))) {
        zoom_cycle(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("pan_left")) || proven_u8str_view_eq(action, PROVEN_LIT("pan_right")) ||
               proven_u8str_view_eq(action, PROVEN_LIT("pan_up")) || proven_u8str_view_eq(action, PROVEN_LIT("pan_down"))) {
        pan_key(app, proven_u8str_view_eq(action, PROVEN_LIT("pan_left")) ? -1 : proven_u8str_view_eq(action, PROVEN_LIT("pan_right")) ? 1 : 0,
                proven_u8str_view_eq(action, PROVEN_LIT("pan_up")) ? -1 : proven_u8str_view_eq(action, PROVEN_LIT("pan_down")) ? 1 : 0);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("zoom_in"))) {
        zoom_step(app, true);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("zoom_out"))) {
        zoom_step(app, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("rotate_cw")) || proven_u8str_view_eq(action, PROVEN_LIT("rotate_ccw")) ||
               proven_u8str_view_eq(action, PROVEN_LIT("flip_horizontal")) || proven_u8str_view_eq(action, PROVEN_LIT("flip_vertical"))) {
        /* Turning changes which pages pair, so the spreads are made again
           and the page that was on screen is found among them: of two,
           the earlier one, whichever side it was on. */
        int32_t page = current_page_index(app);
        if (page >= 0) {
            int32_t other = app->layout.spreads[app->spread_index].right_index;
            if (other >= 0 && other < page) page = other;
        }
        bool turn = true;
        if (proven_u8str_view_eq(action, PROVEN_LIT("rotate_cw"))) {
            app->orientation = rubraview_orientation_rotate_cw(app->orientation);
        } else if (proven_u8str_view_eq(action, PROVEN_LIT("rotate_ccw"))) {
            app->orientation = rubraview_orientation_rotate_ccw(app->orientation);
        } else if (proven_u8str_view_eq(action, PROVEN_LIT("flip_horizontal"))) {
            app->orientation = rubraview_orientation_flip_h(app->orientation);
            turn = false;
        } else {
            app->orientation = rubraview_orientation_flip_v(app->orientation);
            turn = false;
        }
        rebuild_layout(app);
        if (page >= 0) app->spread_index = spread_index_for_page(app, page);
        if (turn) reset_view(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_pixel_grid"))) {
        app->pixel_grid = !app->pixel_grid;
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_nearest"))) {
        app->force_nearest = !app->force_nearest;
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("center_picture"))) {
        /* 1.25.0 (owner, 2026-10-20): the picture's middle on the window's, its size as it is */
        app->pan_x = 0.0;
        app->pan_y = 0.0;
        note_activity(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("align_widget"))) {
        /* 1.25.0 (owner, 2026-10-20: "플로팅 바를 우측 하단으로, 하단 Information bar가 있을 경우 그 위에"): the Floating
           Widget back in its corner, inside the window, over the information bar when it is shown */
        int32_t ww = 0, wh = 0;
        rubraview_pal_window_get_size(app->window, &ww, &wh);
        double dpi = rubraview_pal_window_dpi_scale(app->window);
        rubraview_tile_metrics_t m = tile_metrics(dpi);
        if (app->toolbox_window) toolbox_dock(app, 0.0, 0.0);
        rubraview_box_snap_home(&app->toolbox, &m, (double)ww, (double)wh);
        if (app->osd.always_on) app->toolbox.anchor_y -= 28.0 * dpi;
        osd_say(app, TU8("the Floating Widget is back in its corner"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_osd"))) {
        app->osd.always_on = !app->osd.always_on;
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_filmstrip"))) {
        app->filmstrip.visible = !app->filmstrip.visible;
        int32_t page = current_page_index(app);
        if (page >= 0) strip_follow(app, (size_t)page);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("layout_single"))) {
        layout_set(app, RUBRAVIEW_LAYOUT_CHOICE_SINGLE, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("layout_dual"))) {
        layout_set(app, RUBRAVIEW_LAYOUT_CHOICE_DUAL, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("layout_dual_rtl"))) {
        layout_set(app, RUBRAVIEW_LAYOUT_CHOICE_DUAL_RTL, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("layout_book"))) {
        layout_set(app, RUBRAVIEW_LAYOUT_CHOICE_BOOK, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("layout_book_rtl"))) {
        layout_set(app, RUBRAVIEW_LAYOUT_CHOICE_BOOK_RTL, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("layout_webtoon"))) {
        layout_set(app, RUBRAVIEW_LAYOUT_CHOICE_WEBTOON, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("layout_comic"))) {
        layout_set(app, RUBRAVIEW_LAYOUT_CHOICE_COMIC, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("layout_comic_rtl"))) {
        layout_set(app, RUBRAVIEW_LAYOUT_CHOICE_COMIC_RTL, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_original"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_ORIGINAL, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_4_3"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_4_3, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_5_4"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_5_4, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_3_2"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_3_2, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_16_10"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_16_10, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_16_9"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_16_9, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_185"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_CINEMA_FLAT, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_21_9"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_21_9, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_239"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_CINEMA_SCOPE, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_1_1"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_1_1, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_paper"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_PAPER, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_letter"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_LETTER, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_custom"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_CUSTOM, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_cycle"))) {
        aspect_set(app, rubraview_aspect_next(app->aspect), app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_flip"))) {
        aspect_set(app, app->aspect, !app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_toolbox"))) {
        /* A toolbox that is its own window is put back, not shown twice. */
        if (app->toolbox.state == RUBRAVIEW_BOX_DETACHED) handle_action(app, PROVEN_LIT("toggle_toolbox_detach"));
        else rubraview_box_click_anchor(&app->toolbox);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("open_favorites"))) {
        favwin_toggle(app);   /* 1.24.0 */
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_favorite"))) {
        proven_u8str_view_t here = current_file_path(app);   /* 1.24.0: the file on screen in or out of the favourites */
        if (here.size == 0) osd_say(app, TU8("open a file first"));
        else { (void)favorite_toggle_path(app, here); if (app->favwin.open) favwin_build(app); }
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("open_recent_list"))) {
        /* The owner (2026-10-17): a list window of their own, with when, how large and where
           (the open-file screen's Recent page is still there, as tiles). */
        recentwin_toggle(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("open_picker"))) {
        app->picker_add_mode = false;   /* 1.22.0 */
        if (app->picker_open) {
            app->picker_open = false;
        } else if (rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("native_dialog")) > 0.5) {
            /* RV-107: Windows' own dialog in place of the picker. */
            const rubraview_file_filter_t FILTERS[] = {
                { T("Pictures, films, music, books"),
                  IMAGE_FILTER ";" MEDIA_FILTER ";" TEXT_FILTER ";" ARCHIVE_FILTER ";*.cue;" LIST_FILTER },
                { T("All files"), "*.*" } };
            rubraview_file_dialog_opts_t opts = {
                .title = T("Open"), .filters = FILTERS, .filter_count = 2, .allow_multi = true,
                .parent_window_handle = rubraview_pal_window_native_handle(app->window),
            };
            rubraview_file_dialog_result_t picked = rubraview_pal_file_dialog_open(app->arena, &opts);
            if (picked.accepted && picked.count == 1) open_path(app, picked.paths[0]);
            else if (picked.accepted && picked.count > 1) open_paths_as_set(app, picked.paths, picked.count, T("files opened as one set"));
        } else {
            picker_open(app);
        }
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("list_open")) || proven_u8str_view_eq(action, PROVEN_LIT("list_copy_path")) ||
               proven_u8str_view_eq(action, PROVEN_LIT("list_reveal")) || proven_u8str_view_eq(action, PROVEN_LIT("list_take_out")) ||
               proven_u8str_view_eq(action, PROVEN_LIT("list_delete_file"))) {
        /* RV-117: a file of a list, right-clicked on its row */
        size_t row = app->list_ctx_row;
        rubraview_window_t *owner = app->ctx_in_files && app->files_window ? app->files_window : app->window;
        if (row >= page_count(app)) {
            osd_say(app, TU8("that file is no longer in the list"));
        } else if (proven_u8str_view_eq(action, PROVEN_LIT("list_open"))) {
            if ((int32_t)row != current_page_index(app)) {
                go_to_spread(app, spread_for_chosen_page(app, (int32_t)row));
                update_precache(app);
            }
        } else if (proven_u8str_view_eq(action, PROVEN_LIT("list_copy_path"))) {
            proven_u8str_view_t whole = rubraview_pal_fs_absolute(app->arena, app->source.pages[row].path);
            osd_say(app, rubraview_pal_clipboard_set_text(owner, whole) ? TU8("its whole path is on the clipboard")
                                                                         : TU8("the path could not be copied"));
        } else if (proven_u8str_view_eq(action, PROVEN_LIT("list_reveal"))) {
            if (!rubraview_pal_shell_reveal(app->source.pages[row].path)) osd_say(app, TU8("Explorer could not show the file"));
        } else if (proven_u8str_view_eq(action, PROVEN_LIT("list_take_out"))) {
            bool *one = (bool*)calloc(page_count(app), sizeof(bool));
            if (one) { one[row] = true; list_remove(app, one); free(one); }
        } else {
            list_delete_row(app, row, owner);
        }
        app->files_dirty = true;
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("reveal_in_explorer"))) {
        /* Owner, 2026-10-09: the file on screen picked out in Explorer; a
           page inside an archive has no file of its own, so the archive. */
        int32_t at = current_page_index(app);
        proven_u8str_view_t path = app->source.archive_path.size > 0 ? app->source.archive_path
                     : (at >= 0 && (size_t)at < page_count(app)) ? app->source.pages[at].path : PROVEN_LIT("");
        if (path.size == 0) osd_say(app, TU8("there is no file to show"));
        else if (!rubraview_pal_shell_reveal(path)) osd_say(app, TU8("Explorer could not show the file"));
    } else if ((proven_u8str_view_eq(action, PROVEN_LIT("delete_file")) || proven_u8str_view_eq(action, PROVEN_LIT("purge_file"))) && on_archive_page(app)) {
        /* D-88: a page of an archive; there is no bin inside one, so either key asks. */
        arc_edit_on_page(app, RUBRAVIEW_ARCEDIT_DELETE, NULL);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("delete_file"))) {
        triage_delete(app, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("purge_file"))) {
        /* §3.18.1: a permanent delete asks first, every time. */
        app->confirm_purge = true;
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("undo"))) {
        if (!arc_undo(app)) triage_undo(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("rename_file"))) {
        rename_begin(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_file_list"))) {
        files_show(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_subtitle_window"))) {
        subs_show(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("extract_subtitles"))) {
        takeout_begin(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_classify"))) {
        classify_toggle(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("copy_to_folder"))) {
        filing_put(app, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("move_to_folder"))) {
        filing_put(app, true);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("copy_to_other_folder"))) {
        target_begin(app, false, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("move_to_other_folder"))) {
        target_begin(app, true, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("open_settings"))) {
        if (app->settings_open) settings_close(app);
        else settings_open(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("open_edit"))) {
        if (app->panel.open && !app->panel_is_export && !app->panel_is_batch && !app->panel_is_print && !app->panel_is_convert) panel_close(app);
        else if (app->media && !(app->media_info.has_video && app->media_page >= 0)) osd_say(app, TU8("Edit is for pictures"));
        else if (app->media && !app->media_paused) osd_say(app, TU8("pause the film first (Space): E then edits the frame on screen"));
        else if (app->media) {
            /* The owner (2026-10-18): a paused film's frame, and only it — saved beside the film as a
               picture, which Edit opens; turning on, Edit passes the film by. */
            app->frame_wanted = FRAME_EDIT;
            media_seek_to(app, app->media_position);
        } else panel_open_edit(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("copy_frame")) || proven_u8str_view_eq(action, PROVEN_LIT("save_frame"))) {
        /* RV-108: of a film or an animation; a still picture has Export. */
        bool film = app->media && app->media_info.has_video && app->media_page >= 0;
        if (!film && !anim_plays(app) && proven_u8str_view_eq(action, PROVEN_LIT("copy_frame"))) {
            picture_copy(app);   /* RV-116: a still picture, whole, as its file holds it */
        } else if (!film && !anim_plays(app)) {
            osd_say(app, TU8("that is for a film or an animation: Ctrl+E exports a picture"));
        } else {
            app->frame_wanted = proven_u8str_view_eq(action, PROVEN_LIT("copy_frame")) ? FRAME_COPY : FRAME_SAVE;
            /* Playing, the next frame is taken; standing still, the frame is asked for again. */
            if (film && app->media_paused) media_seek_to(app, app->media_position);
            else if (!film) show_frame(app, app->animation.current);
        }
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("quick_export")) || proven_u8str_view_eq(action, PROVEN_LIT("save_as"))) {
        if (app->panel.open && app->panel_is_export) panel_close(app);
        else {
            /* From Edit (owner, 2026-10-18): the edits are in what Export writes, and Edit comes back after it. */
            app->export_from_edit = edit_panel_open(app) && app->edit_steps.cursor > 0;
            if (edit_panel_open(app)) { edit_live_close(app); sel_mode_set(app, false); edit_window_close(app); }
            panel_open_export(app);
        }
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("new_window"))) {
        new_window(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("open_batch"))) {
        /* RV-194: a window of its own, on the list now open; again, it closes */
        if (app->panel.open && app->panel_is_batch) panel_close(app);   /* the panel of a viewer that could not open the window */
        else batch_window_toggle(app, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("print"))) {
        /* D-86, D-87: the picture on screen; how is chosen first, then the system's print dialog. */
        proven_u8str_view_t path = current_file_path(app);
        if (app->panel.open && app->panel_is_print) panel_close(app);
        else if (on_archive_page(app)) print_archive_page(app);
        else if (path.size == 0) osd_say(app, TU8("there is no picture to print"));
        else print_paths(app, &path, 1);
    } else if (app->media && (proven_u8str_view_eq(action, PROVEN_LIT("media_play_pause")) || proven_u8str_view_eq(action, PROVEN_LIT("anim_toggle_pause")))) {
        media_toggle_pause(app);   /* anim_toggle_pause: the name before D-16, in keymap.ini files saved earlier */
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("media_stop"))) {
        /* Stop: back to the start and paused, the first frame on screen. */
        if (!app->media_paused) media_toggle_pause(app);
        media_seek_to(app, 0.0);
        osd_say(app, TU8("stopped"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_volume_up")) || proven_u8str_view_eq(action, PROVEN_LIT("media_volume_down"))) {
        double volume = rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("volume"));
        rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("volume"),
                               volume + (proven_u8str_view_eq(action, PROVEN_LIT("media_volume_up")) ? 5.0 : -5.0));
        rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("mute"), 0.0);   /* turning it up or down unmutes */
        settings_took_effect(app);
        media_say_volume(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_mute"))) {
        bool muted = rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("mute")) > 0.5;
        rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("mute"), muted ? 0.0 : 1.0);
        settings_took_effect(app);
        media_say_volume(app);
    } else if (action.size > 12 && memcmp(action.ptr, "open_recent:", 12) == 0) {
        /* File › Recent: the entry's index in the reading history. */
        /* Read inside the view: an action is a slice, not a C string. */
        int64_t index = -1;
        if (rubraview_parse_i64((proven_u8str_view_t){ .ptr = action.ptr + 12, .size = action.size - 12 }, &index) &&
            index >= 0 && (size_t)index < app->recent.list.count) {
            proven_u8str_view_t path = app_keep(app, app->recent.list.entries[index].path);
            if (rubraview_pal_fs_exists(path)) open_path(app, path);
            else osd_say(app, TU8("that file is no longer there"));
        }
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("recent_clear"))) {
        recent_sync(app, PROVEN_LIT(""), true);
        menu_rebuild(app);
        osd_say(app, TU8("the recent list is empty"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_always_on_top"))) {
        bool on = rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("always_on_top")) > 0.5;
        rubraview_settings_set(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("always_on_top"), on ? 0.0 : 1.0);
        settings_took_effect(app);
        osd_say(app, on ? TU8("no longer on top of other windows") : TU8("always on top of other windows"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_wheel_strip"))) {
        bool on = rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("wheel_strip")) > 0.5;
        rubraview_settings_set(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("wheel_strip"), on ? 0.0 : 1.0);
        if (on) app->strip_peek_until = 0.0;   /* one up now goes */
        osd_say(app, on ? TU8("no thumbnails while the wheel turns pages") : TU8("thumbnails while the wheel turns pages"));
    } else if (proven_u8str_view_starts_with(action, PROVEN_LIT("picker_")) && app->picker_open) {   /* 1.22.0: a tile's right click */
        size_t at = app->picker_ctx_index;
        if (proven_u8str_view_eq(action, PROVEN_LIT("picker_open_item"))) { if (at < app->picker_listing.count) picker_activate(app, at); }
        else if (proven_u8str_view_eq(action, PROVEN_LIT("picker_look_inside"))) { if (!picker_enter_book(app, at)) osd_say(app, TU8("that is not a book")); }
        else if (proven_u8str_view_eq(action, PROVEN_LIT("picker_add_to_list"))) picker_add_to_list(app);
        else if (proven_u8str_view_eq(action, PROVEN_LIT("picker_make_playlist"))) picker_make_playlist(app);
        else if (proven_u8str_view_eq(action, PROVEN_LIT("picker_copy_paths"))) picker_copy_paths(app);
        else if (proven_u8str_view_eq(action, PROVEN_LIT("picker_reveal"))) {
            if (at < app->picker_listing.count) (void)rubraview_pal_shell_reveal(app->picker_listing.entries[at].path);
        }
        else if (proven_u8str_view_eq(action, PROVEN_LIT("picker_rename"))) rename_picker_begin(app, at);
        else if (proven_u8str_view_eq(action, PROVEN_LIT("picker_change_ext"))) picker_button(app, PICKER_BTN_EXTENSION);   /* 1.24.0: off the bottom */
        else if (proven_u8str_view_eq(action, PROVEN_LIT("picker_favorite"))) {   /* 1.24.0: a file or a folder in or out of the favourites */
            if (at < app->picker_listing.count) { (void)favorite_toggle_path(app, app->picker_listing.entries[at].path); if (app->favwin.open) favwin_build(app); }
        }
        else if (proven_u8str_view_eq(action, PROVEN_LIT("picker_copy_to"))) picker_button(app, PICKER_BTN_COPY);
        else if (proven_u8str_view_eq(action, PROVEN_LIT("picker_move_to"))) picker_button(app, PICKER_BTN_MOVE);
        else if (proven_u8str_view_eq(action, PROVEN_LIT("picker_recycle"))) {
            /* asked on the list (a second click); here it goes */
            rubraview_confirm_press(&app->picker_confirm, PICKER_BTN_RECYCLE, rubraview_pal_time_now_seconds());
            picker_recycle(app);
        }
        else if (proven_u8str_view_eq(action, PROVEN_LIT("picker_deselect_all"))) {
            for (size_t i = 0; app->picker_selected && i < app->picker_listing.count; ++i) app->picker_selected[i] = false;
            osd_say(app, TU8("nothing picked"));
        }
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_titlebar_pin"))) {
        /* 1.20.0 (owner, 2026-10-18: "툴바에 상단 타이틀바 자체를 고정하는 pin"): the title bar stays */
        bool on = rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("titlebar_pinned")) > 0.5;
        rubraview_settings_set(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("titlebar_pinned"), on ? 0.0 : 1.0);
        osd_say(app, on ? TU8("the title bar comes and goes") : TU8("the title bar stays"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("hide_boxes"))) {
        /* D-110: asked again (a key), they come back as a click brings them. */
        chrome_hide(app, !app->chrome_hidden);
        osd_say(app, app->chrome_hidden ? TU8("hidden: click the picture to bring the title bar and the Floating Widget back")
                                        : TU8("the title bar and the Floating Widget are back"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_toolbox_pin"))) {
        rubraview_box_set_pinned(&app->toolbox, !app->toolbox.pinned);
        osd_say(app, app->toolbox.pinned ? TU8("Toolbar pinned open") : TU8("Toolbar unpinned"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_toolbox_detach"))) {
        if (app->toolbox.state == RUBRAVIEW_BOX_DETACHED) {
            int32_t w = 0, h = 0;
            rubraview_pal_window_get_size(app->window, &w, &h);
            double dpi = rubraview_pal_window_dpi_scale(app->window);
            toolbox_dock(app, (double)w - 220.0 * dpi, (double)h - 160.0 * dpi);
        } else {
            int32_t fx = 0, fy = 0, fw = 0, fh = 0;
            rubraview_pal_window_get_frame(app->window, &fx, &fy, &fw, &fh);
            app->toolbox.state = RUBRAVIEW_BOX_DETACHED;
            toolbox_detach(app, fx + fw - (int32_t)(360.0 * rubraview_pal_window_dpi_scale(app->window)),
                           fy + fh / 2, false);
        }
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("open_keys"))) {
        settings_open(app);
        if (app->settings_open) {
            rubraview_settings_view_set_page(&app->settings_view, RUBRAVIEW_TAB_KEYS);
            rubraview_settings_view_set_table_rows(&app->settings_view, (int32_t)app->keymap.count + 1);
            skip_dirs_table_rows(app);
            app->settings_dirty = true;
        }
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_help"))) {
        help_show(app);
    } else if (anim_plays(app) && proven_u8str_view_eq(action, PROVEN_LIT("media_repeat_cycle"))) {
        /* D-82: an animation's own end — round again, stop there, the next file. */
        int mode = (anim_end_mode(app) + 1) % 3;
        rubraview_settings_set(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("animation_end"), (double)mode);
        osd_say(app, mode == 0 ? TU8("an animation goes round and round")
                   : mode == 1 ? TU8("an animation plays once and stops")
                               : TU8("after an animation, the next file"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_repeat_cycle"))) {
        /* What happens at the end of a film or a song (owner, 2026-09-29). */
        rubraview_repeat_mode_t mode = rubraview_repeat_cycle(repeat_mode(app));
        rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("at_end"), (double)mode);
        app->follow_for = -1;   /* decided again, in the new mode */
        osd_say(app, cstr(rubraview_repeat_sentence(mode)));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_playlist"))) {
        listwin_toggle(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_info"))) {
        info_show(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_miniplayer"))) {
        mini_show(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("boxes_opacity_100")) || proven_u8str_view_eq(action, PROVEN_LIT("boxes_opacity_80")) ||
               proven_u8str_view_eq(action, PROVEN_LIT("boxes_opacity_60")) || proven_u8str_view_eq(action, PROVEN_LIT("boxes_opacity_40"))) {
        double percent = 100.0;
        if (!rubraview_parse_double((proven_u8str_view_t){ .ptr = action.ptr + 14, .size = action.size - 14 }, &percent)) percent = 100.0;
        rubraview_settings_set(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("menubox_opacity"), percent);
        rubraview_settings_set(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("toolbox_opacity"), percent);
        char line[32];
        int n = snprintf(line, sizeof(line), T("Floating Widget %d%%"), (int)percent);
        if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
    } else if (window_nudge(app, action)) {
        /* D-16: Ctrl + arrows size the window, Alt + arrows move it. */
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("anim_step_forward"))) {
        media_step(app, true);
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("anim_step_back"))) {
        media_step(app, false);
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("media_seek_forward"))) {
        media_seek_to(app, app->media_position + MEDIA_SEEK_STEP);
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("media_seek_back"))) {
        media_seek_to(app, app->media_position - MEDIA_SEEK_STEP);
    } else if ((app->media || anim_plays(app)) && (proven_u8str_view_eq(action, PROVEN_LIT("media_seek_forward_percent")) ||
                                                   proven_u8str_view_eq(action, PROVEN_LIT("media_seek_back_percent")))) {
        /* RV-206: the edge column's 5 % of the length (5 s when the length is not known) */
        double step = play_duration(app) > 0.0 ? play_duration(app) * 0.05 : MEDIA_SEEK_STEP;
        bool on = proven_u8str_view_eq(action, PROVEN_LIT("media_seek_forward_percent"));
        play_seek(app, play_position(app) + (on ? step : -step));
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("media_seek_forward_long"))) {
        media_seek_to(app, app->media_position + MEDIA_SEEK_LONG);
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("media_seek_back_long"))) {
        media_seek_to(app, app->media_position - MEDIA_SEEK_LONG);
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("media_seek_forward_fine"))) {
        media_seek_to(app, app->media_position + MEDIA_SEEK_FINE);   /* RV-107 */
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("media_seek_back_fine"))) {
        media_seek_to(app, app->media_position - MEDIA_SEEK_FINE);
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("next_audio_track"))) {
        /* §3.16.2: the next sound track of the same file, without
           stopping the picture. */
        int32_t next = rubraview_tracks_next(&app->tracks, RUBRAVIEW_TRACK_AUDIO,
                                             app->tracks.current_audio);
        if (next < 0 || next == app->tracks.current_audio) osd_say(app, TU8("this file has only one sound track"));
        else audio_track_set(app, next);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("debug_lose_device"))) {
        /* A test's trigger (RV-062): the next frame acts as if the graphics
           device had been lost. Bound by no default key. */
        rubraview_pal_render_force_lost(app->renderer);
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("toggle_subtitles"))) {
        /* D-33: from the menu or a key; the Sub tile's taps come through
           sub_tile_result, which also tells a double tap from a single. */
        subtitles_toggle(app);
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("choose_subtitle_track"))) {
        int32_t w = 0, h = 0;
        rubraview_pal_window_get_size(app->window, &w, &h);
        sub_list_open_at(app, w * 0.5, h * 0.75);
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("next_subtitle_track"))) {
        /* §3.16.2: the subtitle files beside the film, and off. */
        if (rubraview_tracks_count(&app->tracks, RUBRAVIEW_TRACK_SUBTITLE) == 0) {
            osd_say(app, TU8("this video has no subtitles, in it or beside it"));
        } else if (subs_may_leave(app)) {
            int32_t next = rubraview_tracks_next(&app->tracks, RUBRAVIEW_TRACK_SUBTITLE,
                                                 app->tracks.current_subtitle);
            subtitle_select(app, next);
            char label[160];
            proven_u8str_view_t text = rubraview_track_label(label, sizeof(label), &app->tracks,
                                                 app->tracks.current_subtitle);
            char line[192];
            int n = snprintf(line, sizeof(line), T("subtitles %.*s"), (int)text.size, text.ptr);
            if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
        }
    } else if (app->media && (proven_u8str_view_eq(action, PROVEN_LIT("subtitle_earlier")) || proven_u8str_view_eq(action, PROVEN_LIT("subtitle_later")) ||
                              proven_u8str_view_eq(action, PROVEN_LIT("subtitle_earlier_fine")) || proven_u8str_view_eq(action, PROVEN_LIT("subtitle_later_fine")) ||
                              proven_u8str_view_eq(action, PROVEN_LIT("subtitle_sync_reset")))) {
        /* §3.16.1: half a second at a time, and the OSD says where the
           track now sits so the reader can aim. */
        /* A DVD's or a Blu-ray's pictures are subtitles too, and they are
           moved by the same offset (found while measuring T081). */
        if (app->subtitle.count == 0 && app->vobsub.count == 0 && app->pgs.count == 0) {
            osd_say(app, TU8("no subtitles are showing"));
        } else {
            /* Half a second (Z / X), a tenth (Shift), or back to the file's own times (Alt+Z) (owner, 2026-10-16). */
            if (proven_u8str_view_eq(action, PROVEN_LIT("subtitle_sync_reset"))) rubraview_subtitle_sync_reset(&app->subtitle);
            else if (proven_u8str_view_eq(action, PROVEN_LIT("subtitle_earlier_fine"))) rubraview_subtitle_shift(&app->subtitle, -0.1);
            else if (proven_u8str_view_eq(action, PROVEN_LIT("subtitle_later_fine"))) rubraview_subtitle_shift(&app->subtitle, 0.1);
            else rubraview_subtitle_nudge(&app->subtitle, proven_u8str_view_eq(action, PROVEN_LIT("subtitle_later")));
            app->subs_dirty = true;   /* the list's Save lights: the offset can be written in */
            if (app->media_page >= 0 && (size_t)app->media_page < page_count(app)) {
                app->subtitle_offset_for = app->source.pages[app->media_page].path;
                app->subtitle_offset_seconds = app->subtitle.offset_seconds;
            }
            char line[96];
            int n = app->subtitle.offset_seconds == 0.0 ? snprintf(line, sizeof(line), T("subtitles in the file's own time"))
                                                        : snprintf(line, sizeof(line), T("subtitles %+.1f s"), app->subtitle.offset_seconds);
            if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
        }
    } else if (anim_plays(app) && proven_u8str_view_eq(action, PROVEN_LIT("media_stop"))) {
        /* D-82: an animation takes a film's keys and tiles from here on. */
        rubraview_animation_stop(&app->animation);
        show_frame(app, 0);
        update_window_title(app);
        osd_say(app, TU8("stopped"));
    } else if (anim_plays(app) && proven_u8str_view_eq(action, PROVEN_LIT("media_seek_forward"))) {
        play_seek(app, play_position(app) + MEDIA_SEEK_STEP);
    } else if (anim_plays(app) && proven_u8str_view_eq(action, PROVEN_LIT("media_seek_back"))) {
        play_seek(app, play_position(app) - MEDIA_SEEK_STEP);
    } else if (anim_plays(app) && proven_u8str_view_eq(action, PROVEN_LIT("media_seek_forward_long"))) {
        play_seek(app, play_position(app) + MEDIA_SEEK_LONG);
    } else if (anim_plays(app) && proven_u8str_view_eq(action, PROVEN_LIT("media_seek_back_long"))) {
        play_seek(app, play_position(app) - MEDIA_SEEK_LONG);
    } else if (anim_plays(app) && proven_u8str_view_eq(action, PROVEN_LIT("media_seek_forward_fine"))) {
        play_seek(app, play_position(app) + MEDIA_SEEK_FINE);
    } else if (anim_plays(app) && proven_u8str_view_eq(action, PROVEN_LIT("media_seek_back_fine"))) {
        play_seek(app, play_position(app) - MEDIA_SEEK_FINE);
    } else if (!app->media && !anim_plays(app) && app->bgm_media && proven_u8str_view_eq(action, PROVEN_LIT("media_play_pause"))) {
        /* §3.14.6: with a picture on screen and music behind it, the
           play key is the music's — and the listener's own pause
           outranks the arbiter from then on. */
        bool pausing = !app->bgm_paused;
        rubraview_pal_media_set_paused(app->bgm_media, pausing);
        app->bgm_paused = pausing;
        (void)rubraview_bgm_event(&app->bgm,
                                  pausing ? RUBRAVIEW_BGM_READER_PAUSED : RUBRAVIEW_BGM_READER_RESUMED,
                                  bgm_pause_for_sound(app));
        osd_say(app, pausing ? TU8("the music is paused") : TU8("the music plays on"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("anim_toggle_pause")) || proven_u8str_view_eq(action, PROVEN_LIT("media_play_pause"))) {
        bool ended = app->animation.ended;
        if (app->animation.paused) rubraview_animation_resume(&app->animation);
        else rubraview_animation_pause(&app->animation);
        if (ended) show_frame(app, app->animation.current);   /* played again: from the first frame */
        update_window_title(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("subpage_next")) && app->anim_active &&
               app->animation.current + 1 >= app->animation.frame_count) {
        next_spread(app);   /* past a multi-page file's last page: the next file (owner, 2026-10-14) */
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("subpage_prev")) && app->anim_active && app->animation.current == 0) {
        prev_spread(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("anim_step_forward")) || proven_u8str_view_eq(action, PROVEN_LIT("subpage_next"))) {
        rubraview_animation_step(&app->animation, true);
        show_frame(app, app->animation.current);
        update_window_title(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("anim_step_back")) || proven_u8str_view_eq(action, PROVEN_LIT("subpage_prev"))) {
        rubraview_animation_step(&app->animation, false);
        show_frame(app, app->animation.current);
        update_window_title(app);
    } else if ((app->media || anim_plays(app)) &&
               (proven_u8str_view_eq(action, PROVEN_LIT("anim_speed_up")) || proven_u8str_view_eq(action, PROVEN_LIT("anim_speed_down")) ||
                proven_u8str_view_eq(action, PROVEN_LIT("media_speed_up")) || proven_u8str_view_eq(action, PROVEN_LIT("media_speed_down")) ||
                proven_u8str_view_eq(action, PROVEN_LIT("media_speed_reset")) || proven_u8str_view_eq(action, PROVEN_LIT("media_speed_cycle")))) {
        /* D-15: 0.25x to 4x a quarter at a time; the tile cycles the usual ones. */
        double speed = app->media_speed > 0.0 ? app->media_speed : 1.0;
        if (proven_u8str_view_eq(action, PROVEN_LIT("media_speed_reset"))) {
            speed = 1.0;
        } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_speed_cycle"))) {
            static const double CYCLE[] = { 1.0, 1.25, 1.5, 2.0, 0.5, 0.75 };
            size_t next = 0;
            for (size_t i = 0; i < 6; ++i) if (fabs(CYCLE[i] - speed) < 1e-6) next = (i + 1) % 6;
            /* D-110: a speed the drag or the wheel left between the steps goes to 1x first. */
            speed = CYCLE[next];
        } else {
            bool up = proven_u8str_view_eq(action, PROVEN_LIT("anim_speed_up")) || proven_u8str_view_eq(action, PROVEN_LIT("media_speed_up"));
            speed += up ? 0.25 : -0.25;
        }
        speed_set(app, speed);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_ab_edit"))) {
        ab_edit_begin(app);
    } else if ((app->media || anim_plays(app)) &&
               (proven_u8str_view_eq(action, PROVEN_LIT("media_ab_a")) || proven_u8str_view_eq(action, PROVEN_LIT("media_ab_b")) ||
                proven_u8str_view_eq(action, PROVEN_LIT("media_ab_clear")) || proven_u8str_view_eq(action, PROVEN_LIT("media_ab_cycle")))) {
        /* D-15 A-B repeat. The tile walks A, then B, then off. */
        char line[64];
        int n = 0;
        bool set_a = proven_u8str_view_eq(action, PROVEN_LIT("media_ab_a")) || (proven_u8str_view_eq(action, PROVEN_LIT("media_ab_cycle")) && app->ab_a < 0.0);
        bool set_b = proven_u8str_view_eq(action, PROVEN_LIT("media_ab_b")) || (proven_u8str_view_eq(action, PROVEN_LIT("media_ab_cycle")) && app->ab_a >= 0.0 && app->ab_b < 0.0);
        if (set_a || set_b) {
            /* The point just taken wins; the other goes to the end or the start (owner, 2026-10-15). */
            rubraview_ab_take(&app->ab_a, &app->ab_b, play_position(app), set_a, play_duration(app));
            n = app->ab_b < 0.0 ? snprintf(line, sizeof(line), T("repeat from %.1f s"), app->ab_a)
                                : snprintf(line, sizeof(line), T("repeating %.1f - %.1f s"), app->ab_a, app->ab_b);
        } else {
            app->ab_a = app->ab_b = -1.0;
            n = snprintf(line, sizeof(line), T("repeat off"));
        }
        if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("anim_speed_up"))) {
        app->animation.speed = rubraview_animation_step_speed(app->animation.speed, true);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("anim_speed_down"))) {
        app->animation.speed = rubraview_animation_step_speed(app->animation.speed, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("archive_names_auto"))) {
        archive_names_reread(app, RUBRAVIEW_CODEPAGE_AUTO);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("archive_names_utf8"))) {
        archive_names_reread(app, RUBRAVIEW_CODEPAGE_UTF8);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("archive_names_korean"))) {
        archive_names_reread(app, RUBRAVIEW_CODEPAGE_KOREAN);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("archive_names_japanese"))) {
        archive_names_reread(app, RUBRAVIEW_CODEPAGE_JAPANESE);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("archive_names_chinese_simplified"))) {
        archive_names_reread(app, RUBRAVIEW_CODEPAGE_SIMPLIFIED_CHINESE);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("archive_names_chinese_traditional"))) {
        archive_names_reread(app, RUBRAVIEW_CODEPAGE_TRADITIONAL_CHINESE);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("archive_names_western"))) {
        archive_names_reread(app, RUBRAVIEW_CODEPAGE_WESTERN);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("archive_names_cycle"))) {
        /* the code pages in their menu order, round again after Western */
        int now = app->names_codepage >= 0 ? app->names_codepage : (int)app->archive_codepage;
        archive_names_reread(app, (rubraview_codepage_t)((now + 1) % ((int)RUBRAVIEW_CODEPAGE_WESTERN + 1)));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("next_archive"))) {
        /* RV-179: in an archive the next archive, in a folder the next folder (as T's drag and the ends do) */
        bool archive = app->source.archive_path.size > 0;
        if (!(archive ? open_sibling_archive(app, true) : !app->list_is_set && open_sibling_folder(app, true)))
            osd_say(app, archive ? TU8("no next archive beside this one") : TU8("no next folder beside this one"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("prev_archive"))) {
        bool archive = app->source.archive_path.size > 0;
        if (!(archive ? open_sibling_archive(app, false) : !app->list_is_set && open_sibling_folder(app, false)))
            osd_say(app, archive ? TU8("no previous archive beside this one") : TU8("no previous folder beside this one"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("resume_accept"))) {
        /* §3.17.1: the prompt is answered by opening the remembered page. */
        if (app->resume_offer) {
            app->resume_offer = false;
            size_t target = spread_index_for_page(app, app->resume_page);
            go_to_spread(app, target);
            update_precache(app);
        }
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_slideshow"))) {
        toggle_slideshow(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_fullscreen"))) {
        rubraview_pal_window_set_fullscreen(app->window,
                                            !rubraview_pal_window_is_fullscreen(app->window));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_layout"))) {
        /* One page, two pages, a book (the cover alone), the webtoon strip,
           comic (wide scans in halves) — two pages, the book and comic each
           followed by itself read right to left — and round again (owner,
           2026-10-09, 2026-10-11). */
        int now = (int)rubraview_layout_choice_of(app->layout_opts.mode, app->layout_opts.direction);
        layout_set(app, (rubraview_layout_choice_t)((now + 1) % (int)RUBRAVIEW_LAYOUT_CHOICE_COUNT), true);
    }
}

/* While the picker is open it owns the keyboard (§3.7.1's modal
   context): letters jump, arrows move, Enter opens, Esc closes. */
static bool picker_handle_key(app_state_t *app, rubraview_key_combo_t combo) {
    if (!app->picker_open) return false;
    picker_layout(app);
    size_t columns = (size_t)app->picker.columns;

    if ((combo.modifiers & RUBRAVIEW_MOD_CTRL) && key_is(combo, "L")) {
        picker_path_begin(app);
        return true;
    }
    if ((combo.modifiers & RUBRAVIEW_MOD_CTRL) && key_is(combo, "D")) {
        picker_toggle_favorite(app);   /* D-43, as a browser bookmarks a page */
        return true;
    }
    if ((combo.modifiers & RUBRAVIEW_MOD_CTRL) && key_is(combo, "A")) { picker_select_everything(app); return true; }   /* 1.22.0 */
    if ((combo.modifiers & RUBRAVIEW_MOD_CTRL) && key_is(combo, "F")) { picker_filter_begin(app); return true; }        /* 1.22.0 */
    if ((combo.modifiers & RUBRAVIEW_MOD_CTRL) && key_is(combo, "Z")) { triage_undo(app); return true; }   /* 1.23.0: what was done here, back */
    if (combo.modifiers & RUBRAVIEW_MOD_ALT) {   /* 1.24.0: back, forward and up through the folders, as Explorer */
        if (key_is(combo, "Left"))  { picker_history_go(app, false); return true; }
        if (key_is(combo, "Right")) { picker_history_go(app, true); return true; }
        if (key_is(combo, "Up"))    { if (!app->picker_in_book) picker_go_up(app); return true; }
    }

    if (combo.key_name.size == 1) {
        char c = combo.key_name.ptr[0];
        if (app->picker_has_pending && c == '0') {
            app->picker_has_pending = false;
            target_begin(app, app->picker_pending == (int32_t)PICKER_BTN_MOVE, true);
            return true;
        }
        if (app->picker_has_pending && c >= '1' && c <= '9') {
            /* A number with no folder behind it leaves the picker waiting
               rather than letting the next one fall through to §3.18.3's
               own meaning, which would move the page on screen. */
            if (rubraview_curation_target(&app->curation, c - '0').size == 0) {
                osd_say(app, TU8("no folder is bound to that number - Settings > Files, or 0 for another folder"));
                return true;
            }
            app->picker_has_pending = false;
            picker_curate(app, c - '0', app->picker_pending == (int32_t)PICKER_BTN_MOVE);
            return true;
        }
        if (app->picker_has_pending && (c == 'A' || c == 'Z')) {
            app->picker_has_pending = false;   /* any letter gives up waiting */
        }
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            rubraview_picker_type_ahead(&app->picker, c);
            return true;
        }
    }

    size_t count = app->picker_listing.count;
    if (count == 0) { app->picker_open = false; return true; }

    if (proven_u8str_view_eq(combo.key_name, PROVEN_LIT("Right"))) {
        if (app->picker.focus + 1 < count) app->picker.focus++;
        rubraview_picker_reveal_focus(&app->picker);
        return true;
    }
    if (proven_u8str_view_eq(combo.key_name, PROVEN_LIT("Left"))) {
        if (app->picker.focus > 0) app->picker.focus--;
        rubraview_picker_reveal_focus(&app->picker);
        return true;
    }
    if (proven_u8str_view_eq(combo.key_name, PROVEN_LIT("Down"))) {
        app->picker.focus = (app->picker.focus + columns < count)
            ? app->picker.focus + columns : count - 1;
        rubraview_picker_reveal_focus(&app->picker);
        return true;
    }
    if (proven_u8str_view_eq(combo.key_name, PROVEN_LIT("Up"))) {
        app->picker.focus = (app->picker.focus >= columns) ? app->picker.focus - columns : 0;
        rubraview_picker_reveal_focus(&app->picker);
        return true;
    }
    if (proven_u8str_view_eq(combo.key_name, PROVEN_LIT("Enter"))) {
        if ((combo.modifiers & RUBRAVIEW_MOD_SHIFT) && picker_enter_book(app, app->picker.focus)) return true;
        picker_activate(app, app->picker.focus);
        return true;
    }
    if (proven_u8str_view_eq(combo.key_name, PROVEN_LIT("Escape"))) {
        app->picker_open = false;
        return true;
    }
    if (proven_u8str_view_eq(combo.key_name, PROVEN_LIT("Backspace"))) {
        proven_u8str_view_t parent = rubraview_picker_parent(app->picker_dir);
        if (parent.size > 0) {
            picker_navigate(app, parent);
            app->picker.focus = 0;
        }
        return true;
    }
    return false;
}

bool key_is(rubraview_key_combo_t combo, const char *name) {
    size_t n = strlen(name);
    return combo.key_name.size == n && memcmp(combo.key_name.ptr, name, n) == 0;
}

/* Typed, composed or pasted text into the rename box: control bytes and
   anything that is not UTF-8 are left out (rubraview_rename_append's rule),
   then it goes in at the caret in place of the selection. */
void rename_insert(app_state_t *app, proven_u8str_view_t text) {
    if (text.size == 0) return;
    char clean[sizeof(app->rename_buffer)];
    size_t length = 0;
    (void)rubraview_rename_append(clean, sizeof(clean), &length, text);
    (void)rubraview_textedit_insert(&app->rename_edit, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)clean, .size = length });
}

/* Text for a box: whole UTF-8 characters, no control characters. */
void textbox_insert(rubraview_textedit_t *te, proven_u8str_view_t text) {
    if (text.size == 0) return;
    char clean[1024];
    size_t length = 0;
    (void)rubraview_rename_append(clean, sizeof(clean), &length, text);
    (void)rubraview_textedit_insert(te, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)clean, .size = length });
}

/* The editing keys of a text box (the rename box, the picker's path, the
   settings window's folders). The characters themselves come as text;
   these move, select, delete and use the clipboard of `window`. */
bool textbox_key(rubraview_window_t *window, rubraview_textedit_t *te, rubraview_key_combo_t combo) {
    bool shift = (combo.modifiers & RUBRAVIEW_MOD_SHIFT) != 0;
    bool ctrl = (combo.modifiers & RUBRAVIEW_MOD_CTRL) != 0;
    if (key_is(combo, "Backspace")) {
        /* A whole character, not one byte: half a Hangul syllable would
           leave the name unwritable. (Inside a syllable the IME takes
           Backspace itself and it never comes here.) */
        rubraview_textedit_backspace(te);
    } else if (key_is(combo, "Delete")) {
        rubraview_textedit_delete(te);
    } else if (key_is(combo, "Left")) {
        rubraview_textedit_move(te, RUBRAVIEW_TEXTEDIT_LEFT, shift);
    } else if (key_is(combo, "Right")) {
        rubraview_textedit_move(te, RUBRAVIEW_TEXTEDIT_RIGHT, shift);
    } else if (key_is(combo, "Home")) {
        rubraview_textedit_move(te, RUBRAVIEW_TEXTEDIT_HOME, shift);
    } else if (key_is(combo, "End")) {
        rubraview_textedit_move(te, RUBRAVIEW_TEXTEDIT_END, shift);
    } else if (ctrl && key_is(combo, "A")) {
        rubraview_textedit_select_all(te);
    } else if (ctrl && (key_is(combo, "C") || key_is(combo, "X"))) {
        proven_u8str_view_t chosen = rubraview_textedit_selection(te);
        if (chosen.size > 0 && rubraview_pal_clipboard_set_text(window, chosen) && key_is(combo, "X")) {
            rubraview_textedit_backspace(te);   /* removes the selection */
        }
    } else if (ctrl && key_is(combo, "V")) {
        char pasted[1024];
        size_t n = rubraview_pal_clipboard_get_text(window, pasted, sizeof(pasted));
        textbox_insert(te, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)pasted, .size = n });
    } else {
        return false;
    }
    return true;
}

static bool rename_edit_key(app_state_t *app, rubraview_key_combo_t combo) {
    return textbox_key(app->window, &app->rename_edit, combo);
}

/* Everything §3.18 puts in front of the reader answers the next key
   press before anything else does: a confirmation that is ignored is
   worse than no confirmation at all. */
/* RFC-0006: the program started again where it is — the file on screen (a book's page
   comes back from the reading history) — after what quitting asks; then this one closes. */
void restart_now(app_state_t *app) {
    app->confirm_restart = false;
    if (!subs_may_leave(app)) return;   /* subtitle changes not saved: asked, as quitting asks */
    settings_write_if_changed(app, false);
    proven_u8str_view_t here = current_file_path(app);
    if (app->source.archive_path.size > 0) here = app->source.archive_path;
    proven_u8str_view_t exe = rubraview_pal_process_executable(app->arena);
    char line[4096];
    size_t pos = 0;
    line[0] = '\0';
    if (exe.size > 0) {
        append_arg(line, sizeof(line), &pos, exe);
        append_arg_cstr(line, sizeof(line), &pos, "--new-instance");
        /* A book (an archive) comes back on its page; a folder's file is named itself. */
        int32_t page = current_page_index(app);
        if (here.size > 0 && app->source.kind != RUBRAVIEW_PAGE_SOURCE_FOLDER && page > 0) {
            char word[32];
            snprintf(word, sizeof(word), "--page=%d", (int)page + 1);
            append_arg_cstr(line, sizeof(line), &pos, word);
        }
        if (here.size > 0) append_arg(line, sizeof(line), &pos, here);
    }
    if (exe.size == 0 || !rubraview_pal_process_start((proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = pos })) {
        osd_say(app, TU8("Rubraview could not be started again"));
        return;
    }
    rubraview_pal_window_request_close(app->window);
}

/* Twice a second: past the session's limit (or a test's), a restart is offered, once. */
void memory_watch(app_state_t *app, double now) {
    if (!app->mem || app->restart_offered || now - app->memory_checked_at < 0.5) return;
    app->memory_checked_at = now;
    double test_mb = rubraview_settings_get(&app->settings, PROVEN_LIT("debug"), PROVEN_LIT("memory_restart_mb"));
    if (test_mb > 0.0) app->mem->heap_restart_at = (size_t)test_mb << 20;   /* a test's limit, on all the program holds */
    if (rubraview_mem_pressure(app->mem) == RUBRAVIEW_MEM_OK) return;
    app->restart_offered = true;
    app->confirm_restart = true;
}

/* The box given up: Esc, or its Cancel. */
static void rename_cancel(app_state_t *app) {
    if (app->rename_is_password) osd_say(app, TU8("not opened: it needs its password"));
    bool filter = app->rename_is_filter, listfilter = app->rename_is_listfilter;
    rename_end(app);
    if (filter) picker_filter_set(app, PROVEN_LIT(""));   /* 1.22.0: Esc lets every name show again */
    if (listfilter) list_filter_set(app, PROVEN_LIT(""));
}

/* 1.24.0 (owner, 2026-10-19: "단축키만 있는게 아니라 버튼으로도 각 기능이나 입력, 취소가 버튼으로"): the box's buttons and,
   for Change extensions, its rows. True when the press was theirs. */
bool rename_box_press(app_state_t *app, double x, double y) {
    if (!app->rename_active) return false;
    for (int32_t i = 0; i < app->rename_btn_n; ++i) {
        if (!pal_rect_has(app->rename_btn[i], x, y)) continue;
        switch (app->rename_btn_kind[i]) {
            case RENAME_BTN_OK:     rename_commit(app); break;
            case RENAME_BTN_CANCEL: rename_cancel(app); break;
            case RENAME_BTN_BROWSE: target_browse(app); break;
            case RENAME_BTN_BACK:   text_find_step(app, false); break;
            case RENAME_BTN_OK + 100: rename_extension_apply(app); break;   /* Change extensions: all rows now */
            default: break;
        }
        return true;
    }
    if (app->rename_is_extension) {
        for (int32_t k = 0; k < app->ext_n; ++k) if (pal_rect_has(app->ext_row_rect[k], x, y)) { ext_switch(app, k); return true; }
    }
    return false;
}

static bool triage_handle_key(app_state_t *app, rubraview_key_combo_t combo) {
    /* An open text box takes every key first: a resume offer still on
       screen took its Enter, and the typed path went nowhere (VM,
       2026-09-28). */
    if (app->rename_active) {
        if (app->rename_is_find && (key_is(combo, "F3") || ((combo.modifiers & RUBRAVIEW_MOD_SHIFT) && key_is(combo, "Enter")) ||
                                    ((combo.modifiers & RUBRAVIEW_MOD_CTRL) && key_is(combo, "G")))) {   /* RV-184: Ctrl+G as with the box closed */
            text_find_step(app, !(combo.modifiers & RUBRAVIEW_MOD_SHIFT));   /* RFC-0005: back with Shift */
            return true;
        }
        if (key_is(combo, "Enter")) { rename_commit(app); return true; }
        if (key_is(combo, "Escape")) { rename_cancel(app); return true; }
        if (app->rename_is_extension && key_is(combo, "Tab")) {   /* 1.24.0: the next row, or with Shift the one before */
            int32_t to = app->ext_at + ((combo.modifiers & RUBRAVIEW_MOD_SHIFT) ? -1 : 1);
            ext_switch(app, (to % app->ext_n + app->ext_n) % app->ext_n);
            return true;
        }
        /* A password is never copied out of its box. */
        if (app->rename_is_password && (combo.modifiers & RUBRAVIEW_MOD_CTRL) && (key_is(combo, "C") || key_is(combo, "X"))) return true;
        if (app->rename_is_target && (combo.modifiers & RUBRAVIEW_MOD_CTRL) && key_is(combo, "O")) { target_browse(app); return true; }
        if (rename_edit_key(app, combo)) return true;
        /* The characters themselves arrive as text (rename_text), in
           either case and from the IME; the keys that make them are
           swallowed here, as is everything else while renaming. */
        return true;
    }

    if (arc_ask_key(app, combo)) return true;

    /* §3.17.1's resume prompt answers Enter before paging does. Enter
       turns a page now (owner, 2026-09-09), and a prompt that the very
       key meant to answer it walks straight past is not a prompt. */
    if (app->resume_offer && key_is(combo, "Enter")) {
        app->resume_offer = false;
        size_t target = spread_index_for_page(app, app->resume_page);
        go_to_spread(app, target);
        update_precache(app);
        return true;
    }

    if (app->confirm_restart) {   /* RFC-0006 */
        if (key_is(combo, "Y") || key_is(combo, "Enter")) restart_now(app);
        else {
            app->confirm_restart = false;
            osd_say(app, TU8("later, then: Settings > Cache restarts it when you like"));
        }
        return true;
    }
    if (app->confirm_purge) {
        if (key_is(combo, "Y") || key_is(combo, "Enter")) {
            app->confirm_purge = false;
            triage_delete(app, true);
        } else {
            /* Anything else is "no". A confirmation should be hard to
               agree to by accident and easy to refuse. */
            app->confirm_purge = false;
        }
        return true;
    }

    /* §3.18.3: 1-9 curate — but only where a folder is actually bound.
       §3.7.2 gives 1-5 to the fit modes, and an unconfigured viewer must
       keep them; a reader who has set up triage folders has said which
       meaning they want. */
    if (combo.modifiers == RUBRAVIEW_MOD_NONE && combo.key_name.size == 1 && !app->picker_open) {
        /* Not while the picker is open: there a number answers its own
           "Move to" or "Copy to", and curating the page behind it would
           move a file the reader is not looking at (2026-09-23). */
        char c = combo.key_name.ptr[0];
        if (c >= '1' && c <= '9') {
            int32_t digit = c - '0';
            if (app->classify_panel.open && rubraview_curation_target(&app->curation, digit).size > 0) {
                classify_mark(app, digit);   /* RV-133: set aside, not moved yet */
                return true;
            }
            if (rubraview_curation_target(&app->curation, digit).size > 0) {
                triage_curate(app, digit);
                return true;
            }

        }
    }

    return false;
}

/* A sound track of the film on screen, by its index in the track list. */
void audio_track_set(app_state_t *app, int32_t next) {
    if (!app->media || next < 0 || (size_t)next >= app->tracks.count || next == app->tracks.current_audio) return;
    if (!rubraview_pal_media_select_audio_track(app->media, app->tracks.tracks[next].stream_index)) {
        /* No device on this machine, or the track cannot be decoded:
           the one that was playing keeps playing. */
        osd_say(app, TU8("the sound track cannot be changed here"));
        return;
    }
    app->tracks.current_audio = next;
    /* Both streams start again from where the film is, so what
       the old track had already decoded is thrown away. */
    media_seek_to(app, app->media_position);
    char label[160];
    proven_u8str_view_t text = rubraview_track_label(label, sizeof(label), &app->tracks, next);
    char line[192];
    int n = snprintf(line, sizeof(line), T("sound %.*s"), (int)text.size, text.ptr);
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
}

void dispatch_key(app_state_t *app, rubraview_key_combo_t combo) {
    if (chooser_key(app, CHOOSE_IN_MAIN, combo)) return;   /* 1.24.0: the list's keys while it is open */
    if (tmenu_key(app, combo)) return;                     /* 1.25.0: the title bar's ☰ */
    if (ab_edit_handle_key(app, combo)) return;
    if (px_edit_handle_key(app, combo)) return;
    if (popups_key(app, combo)) return;   /* RV-116 */
    /* The owner's edit: Enter applies the step, Ctrl+Z and Ctrl+Y go through the steps. */
    if (edit_panel_open(app) && !app->rename_active && !app->px_edit_open) {
        bool ctrl = (combo.modifiers & RUBRAVIEW_MOD_CTRL) != 0;
        if (ctrl && key_is(combo, "Z")) { edit_undo(app, (combo.modifiers & RUBRAVIEW_MOD_SHIFT) != 0); return; }
        if (ctrl && key_is(combo, "Y")) { edit_undo(app, true); return; }
        if (!ctrl && key_is(combo, "Enter")) { (void)edit_apply_pending(app, true); return; }
    }
    /* D-33: Escape closes the list of subtitle tracks, and only that. */
    if (app->sub_list_open && proven_u8str_view_eq(combo.key_name, PROVEN_LIT("Escape")) && combo.modifiers == 0) {
        app->sub_list_open = false;
        return;
    }
    if (triage_handle_key(app, combo)) return;
    if (picker_handle_key(app, combo)) return;
    /* §3.7.1, RV-184 (D-219): the contexts of what is on screen, in the order rubraview_keymap_mode_chain gives —
       the slide show's while one runs, the kind of file's own, then navigation, file and common. One table, so
       what a key does here is what the Keymap page and F1 say it does. */
    handle_action(app, rubraview_keymap_resolve(&app->keymap, key_mode(app), app->slideshow_running, combo));
}

/* RV-184: what is on screen, for the keys. */
rubraview_key_mode_t key_mode(const app_state_t *app) {
    if (text_active(app)) return RUBRAVIEW_KEY_MODE_TEXT;
    if (app->media) return app->media_info.has_video ? RUBRAVIEW_KEY_MODE_FILM : RUBRAVIEW_KEY_MODE_MUSIC;
    if (app->anim_active) return app->animation.kind == RUBRAVIEW_FRAMES_ANIMATION ? RUBRAVIEW_KEY_MODE_ANIMATION
                                                                                 : RUBRAVIEW_KEY_MODE_MULTIPAGE;
    return RUBRAVIEW_KEY_MODE_PICTURE;
}
