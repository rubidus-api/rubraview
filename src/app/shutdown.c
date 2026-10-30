/*
 * The program's end: the windows closed, places and history written, the threads stopped and the pages,
 * the film and the window let go (RV-163, D-198: cut from wWinMain, its lines as they were, `app.` read
 * as `app->`).
 */
#include "app.h"

void app_end(app_state_t *app) {
    proven_arena_t *const arena = app->arena;

    /* §3.22: closing the viewer with the settings window open still
       writes what was changed in it. Every other window of the viewer's
       goes as its own X takes it, before the viewer's renderer does
       (owner, 2026-10-24): the font picker before the settings it belongs
       to, Edit's window with its panel. */
    fontpick_close(app);
    settings_close(app);
    files_close(app);
    subs_close(app);
    if (app->panel.open || app->edit_window) panel_close(app);
    info_close(app);
    help_close(app);
    mini_close(app);
    takeout_end(app);
    putin_end(app, false);
    settings_write_if_changed(app, false);   /* a volume or opacity changed with the window shut */
    if (app->toolbox_window) {
        rubraview_pal_render_destroy(app->toolbox_renderer);
        rubraview_pal_window_destroy(app->toolbox_window);
    }
    if (app->settings_window) {
        rubraview_pal_render_destroy(app->settings_renderer);
        rubraview_pal_window_destroy(app->settings_window);
    }

    /* §3.6 / §3.17.1: remember where the reader stopped, and where the
       boxes were left, before shutting down. */
    layout_save(app);
    text_close(app);   /* RFC-0005: its place is kept */
    history_remember(app);
    if (app->history_path.size > 0 && app->history.count > 0) {
        /* D-99: other viewers wrote their places since this one read the file:
           theirs and this one's are put together, each book at its newer place. */
        bool locked = rubraview_pal_config_lock(CONFIG_LOCK_MS);
        rubraview_history_t all = rubraview_history_parse(arena, rubraview_pal_fs_read_file(arena, app->history_path, 1024u * 1024u));
        rubraview_history_merge(arena, &all, &app->history);
        proven_u8str_view_t skip_dirs[RECENT_SKIP_DIRS];
        (void)rubraview_history_drop_within(&all, skip_dirs, recent_skip_dirs(app, skip_dirs));   /* D-104 */
        rubraview_history_prune(&all, HISTORY_MAX_ENTRIES);
        rubraview_pal_fs_write_file_atomic(app->history_path, rubraview_history_serialize(arena, &all));
        if (locked) rubraview_pal_config_unlock();
    }

    picker_thumbs_free(app);
    shape_free(app);   /* RV-192: its reader stopped before the window goes */
    rubraview_pal_thumbs_stop(app->thumbs);   /* D-34: before the renderer and the window go */
    rubraview_pal_thumbs_stop(app->strip_thumbs);
    strip_cells_free(app);
    tiles_reset(app);                       /* D-40: its textures, then its thread */
    save_job_poll(app, true);   /* a copy being written is finished, not cut off */
    apply_all_poll(app, true);  /* the copy in hand is finished, the rest left */
    arc_job_poll(app, true);    /* D-88: an archive being written again is stopped; the old one stands */
    arc_undo_drop(app);         /* D-89: the kept file of the last edit goes with the program */
    print_temp_drop(app);
    for (size_t i = 0; i < TILE_SETS; ++i) rubraview_pal_tiles_stop(app->tile_sets[i].tiles);
    rubraview_pal_gpu_resample_stop();       /* D-38 */
    app->thumbs = NULL;
    if (app->jobs) {
        proven_job_system_close(app->jobs);
        proven_job_system_destroy(app->jobs);
        app->jobs = NULL;
    }

    media_close(app);
    unload_all_pages(app);
    source_close(app);
    secure_wipe(app->passwords, sizeof(app->passwords));   /* owner, 2026-10-01: memory only, and not after */
    rubraview_pal_render_destroy(app->renderer);
    rubraview_pal_window_destroy(app->window);
}
