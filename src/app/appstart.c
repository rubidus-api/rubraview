/*
 * The viewer's start, after the command line: the state's first values, the window and its renderer, the settings,
 * the keymap and history, the boxes where they were left, and the first file (RV-163, D-198: cut from wWinMain, its
 * lines as they were, `app.` read as `app->` and `&mem` as `mem`; the two failures return false, and wWinMain lets
 * the memory and COM go).
 */
#include "app.h"

bool app_start(app_state_t *app, rubraview_mem_t *mem) {
    proven_arena_t *const arena = &mem->persist.arena;

    app->arena = arena;
    app->mem = mem;
    app->fit_mode = RUBRAVIEW_FIT_WINDOW;
    app->zoom = 1.0;
    app->layout_exempt = -1;
    /* Found with RV-108: Export began at quality 0 — the options were never given their defaults. */
    app->export_options = rubraview_export_defaults();
    app->curve_dragging = -1;   /* no point held; 0 would mean the first one */
    app->orientation = rubraview_orientation_identity();
    app->spread_detect = true; /* §3.3.4 default */
    app->layout_opts = rubraview_layout_opts_default(RUBRAVIEW_PAGE_LAYOUT_SINGLE, RUBRAVIEW_READING_LTR);

    rubraview_window_config_t window_config = {
        /* The version is in the title so a screenshot identifies the
           build it came from — which is most of what a bug report needs. */
        .title = "Rubraview " RUBRAVIEW_VERSION_STRING,
        .width = 1280,
        .height = 800,
        .frameless = true, /* §2 invariant 5: zero-margin canvas */
    };
    app->window = rubraview_pal_window_create(arena, &window_config);
    if (!app->window) {
        return false;
    }

    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    app->renderer = rubraview_pal_render_create(arena, rubraview_pal_window_native_handle(app->window), win_w, win_h);
    ui_font_apply_all(app);
    if (!app->renderer) {
        report_startup_failure();
        rubraview_pal_window_destroy(app->window);
        return false;
    }

    double dpi = rubraview_pal_window_dpi_scale(app->window);

    /* §3.1: no worker pool. Every decode touches WIC, Direct2D and the
       arena, none of which may be used off this thread; the ring runs
       without one and precache_decode queues pages for the main loop. */
    app->jobs = NULL;
    app->media_page = -1;
    /* §3.22 [video] decoder (D-9): which backend opens a file first. The
       other one still gets its turn when this one cannot. */
    app->media_preferred = preferred_backend(arena);
    history_load(app);
    load_keymap(app);   /* after history_load: it settles portable or AppData, and so where keymap.ini is */
    /* history_load read settings.ini from wherever it lives (beside the
       program or in AppData); the decoder choice comes from that, not
       only from a file in the working folder. */
    settings_took_effect(app);
    app->osd = rubraview_osd_create(2.0, 0.5);            /* §3.1 */
    app->osd_size_page = -1;
    app->stats_size_page = -1;
    app->pack_level = 2;   /* RV-207: the Compress box starts at normal */
    app->titlebar = rubraview_titlebar_create(dpi);       /* §3.21.2 */
    app->titlebar.trigger_zone = fmax(36.0, rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("titlebar_trigger_px"))) * dpi;
    app->titlebar.hide_delay = rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("titlebar_hide_ms")) / 1000.0;
    app->listwin = rubraview_listwin_create(dpi);
    app->list_tip_page = -1;
    app->listwin_shown_page = -1;
    app->names_codepage = -1;
    app->eqwin = rubraview_eqwin_create(dpi);
    app->follow_for = -1;
    app->rename_favorite = -1;
    app->fav_press = -1;
    app->toolbox = rubraview_box_create(RUBRAVIEW_BOX_TOOLBOX, (double)win_w - 220.0 * dpi, (double)win_h - 160.0 * dpi, 8);   /* toolbox_refresh sets the real count */
    app->menubox = rubraview_box_create(RUBRAVIEW_BOX_MENU, 8.0 * dpi, 8.0 * dpi + app->titlebar.height, app->menu_tree.root_count);
    /* One in the top-left corner, one in the bottom-right (owner, 2026-10-11), until they are moved. */
    app->box_share[0][0] = app->box_share[0][1] = 1.0;
    app->box_share_known[0] = true;   /* the menu box's share is taken from its place, under the title bar (owner, 2026-10-15) */
    app->box_put[0][0] = app->toolbox.anchor_x; app->box_put[0][1] = app->toolbox.anchor_y;
    app->box_put[1][0] = app->menubox.anchor_x; app->box_put[1][1] = app->menubox.anchor_y;
    layout_load(app);   /* §3.6: back where the reader left them */
    menu_rebuild(app);   /* §3.6.2, RFC-0002 §5: the tree from the boxes' document */
    g_caption_app = app;
    app->transition = rubraview_transition_create(RUBRAVIEW_TRANSITION_CROSSFADE, 0.25);
    app->settings_last_setting = -1;
    app->settings_tip_help = -1;
    app->cursor = rubraview_cursor_hide_create(1.5);      /* §3.2.5 */
    app->filmstrip = rubraview_filmstrip_create(0, FILMSTRIP_THUMB * dpi, (double)win_w);

    /* §3.18.3: the triage folders, and §3.19.2: accept drops. The folders
       are read from the settings file the viewer actually uses — until
       2026-09-23 this said `settings.ini`, a name with no folder, so the
       numbers only curated when the viewer happened to be started in a
       folder holding one, which is to say almost never. */
    {
        proven_u8str_view_t settings = rubraview_pal_fs_read_file(arena, app->settings_path, 256u * 1024u);
        app->curation = rubraview_curation_parse(arena, settings);
    }
    rubraview_pal_window_accept_drops(app->window, true);
    sync_menubox_tiles(app);

    /* Let the freshly created window settle its own messages before the
       first file is opened. Decoding runs COM calls, and an apartment
       with an unpumped window is where those calls go to hang. */
    pump_messages();

    /* What this launch was given. A plain launch opens at once; what the
       menu asked is gathered with what the other launches of the same
       selection hand over (D-86). */
    app->launch_page = g_launch_page;
    if (g_launch_count > 0) {
        shell_request_take(app, g_launch_verb, g_launch_whole || g_launch_verb == RUBRAVIEW_SHELL_VERB_NONE,
                           g_launch_paths, g_launch_count);
        if (rubraview_shellreq_ready(&app->shell_requests, rubraview_pal_time_now_seconds())) shell_requests_flush(app);
    }
    startup_without_file(app, g_launch_count == 0);
    return true;
}
