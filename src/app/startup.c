/*
 * Startup without a file, the keymap, the slides built and followed, and the pages read ahead
 * (RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* ---- startup ---- */

/* Settings › General › On startup (owner, 2026-09-30), when no file was
   given: nothing (the picker's PC page, as before), the last thing read,
   or the picker in the folder it was in. */
void startup_without_file(app_state_t *app, bool no_file) {
    if (!no_file || app->history.count == 0) return;
    int mode = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("startup")));
    if (mode == 0) return;
    const rubraview_history_entry_t *last = &app->history.entries[0];
    for (size_t i = 1; i < app->history.count; ++i) {
        if (app->history.entries[i].timestamp > last->timestamp) last = &app->history.entries[i];
    }
    rubraview_fs_entry_t entry;
    if (!rubraview_pal_fs_stat(app->arena, last->path, &entry)) return;   /* gone since: start as blank does */
    if (mode == 1) {
        open_path(app, last->path);
    } else {
        proven_u8str_view_t folder = entry.is_directory ? last->path : rubraview_path_dirname(last->path);
        picker_open(app);
        picker_navigate(app, folder);
        app->picker.focus = 0;
    }
}

void load_keymap(app_state_t *app) {
    /* §3.7.5 / D-14: keymap.ini lives where settings.ini does — beside the
       program in portable mode, in AppData otherwise — and replaces the
       built-in bindings wholesale. Before the settings window could write
       one it was read only from the working folder, so a file left there
       still counts when AppData has none. */
    /* Settings › Keys › Use keymap.ini: off, the built-in keys, whatever the file says. */
    bool use_file = rubraview_settings_get(&app->settings, PROVEN_LIT("keys"), PROVEN_LIT("use_keymap_file")) > 0.5;
    proven_u8str_view_t text = use_file ? rubraview_pal_fs_read_file(app->arena, app->keymap_path, KEYMAP_MAX_BYTES) : PROVEN_LIT("");
    if (use_file && text.size == 0 && app->config_mode != RUBRAVIEW_CONFIG_PORTABLE) {
        text = rubraview_pal_fs_read_file(app->arena, PROVEN_LIT("keymap.ini"), KEYMAP_MAX_BYTES);
    }
    bool own = text.size > 0;
    if (text.size == 0) text = cstr(rubraview_default_keymap());
    app->keymap = rubraview_keymap_parse(app->arena, text);
    /* RV-184 (D-219): a file saved before the contexts were split puts each action where it is now */
    if (own) {
        rubraview_keymap_t defaults = rubraview_keymap_parse(app->arena, cstr(rubraview_default_keymap()));
        rubraview_keymap_place_by_action(&app->keymap, &defaults);
        /* RV-211: the function keys given up, where the file still has them as they came */
        rubraview_keymap_t retired = rubraview_keymap_parse(app->arena, cstr(rubraview_default_keymap_retired()));
        rubraview_keymap_retire(&app->keymap, &retired, &defaults);
        rubraview_keymap_add_missing(app->arena, &app->keymap, &defaults);   /* RV-196: actions made after the file was saved */
    }
}

void build_slides(app_state_t *app) {
    if (app->layout.count == 0) return;
    /* the delay and the end chosen stay; a new list is not a new slide show */
    double interval = app->slideshow.interval_seconds > 0.0 ? app->slideshow.interval_seconds : 3.0;
    rubraview_loop_policy_t loop = app->slideshow.item_count > 0 ? app->slideshow.loop : RUBRAVIEW_LOOP_ALL;
    bool paused = app->slideshow.item_count == 0 || app->slideshow.paused;
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, app->layout.count, sizeof(rubraview_slideshow_item_t));
    if (!proven_is_ok(res.err)) return;

    app->slides = (rubraview_slideshow_item_t*)(void*)res.value.ptr;
    for (size_t i = 0; i < app->layout.count; ++i) {
        /* Every slide starts as a still. A page that turns out to hold
           an animation upgrades its own slide when it is opened, since
           only then is the cycle length known; video joins with M5
           (RV-061). */
        app->slides[i] = (rubraview_slideshow_item_t){ .kind = RUBRAVIEW_MEDIA_STILL, .duration_seconds = 0.0 };
    }
    app->slideshow = rubraview_slideshow_create(app->layout.count, interval, loop);
    app->slideshow.current_index = app->spread_index < app->layout.count ? app->spread_index : 0;
    if (paused || !app->slideshow_running) rubraview_slideshow_pause(&app->slideshow);
}

/* The spreads were made again (a wide page found, the layout changed, a page
   left out) or the reader turned a page while it ran: the slide show goes on
   from the spread on screen, its time from nothing. Before, its own count and
   place stayed as they were when the file was opened, and it went on from
   there — past spreads, or to the wrong ones. */
void slides_follow_screen(app_state_t *app) {
    if (app->slideshow.item_count != app->layout.count) build_slides(app);
    if (app->slideshow.current_index != app->spread_index && app->spread_index < app->layout.count) {
        app->slideshow.current_index = app->spread_index;
        app->slideshow.elapsed_in_item = 0.0;
    }
}

/* The spread on screen has been drawn — its pages read, or found unreadable,
   or a film or a song: the delay counts from then, so a large picture still
   being read is not passed over before it was seen. */
bool slide_ready(const app_state_t *app) {
    if (app->spread_index >= app->layout.count) return true;
    int32_t pages[2] = { app->layout.spreads[app->spread_index].left_index, app->layout.spreads[app->spread_index].right_index };
    for (int i = 0; i < 2; ++i) {
        if (pages[i] < 0 || (size_t)pages[i] >= page_count(app)) continue;
        const app_page_t *page = &app->pages[pages[i]];
        if (!page->loaded && !page->failed && !is_media_path(app->source.pages[pages[i]].path) &&
            !is_text_path(app->source.pages[pages[i]].path) && !is_text_path(app->source.pages[pages[i]].name)) return false;
    }
    return true;
}

/* The ring names pages it wants ready before the reader reaches them.
   Decoding them here would stall the flip that asked, and decoding them
   on a worker is unsound — WIC wants COM on the calling thread, the
   Direct2D context is single-threaded, and the arena is not locked — so
   they are queued and the main loop decodes one per pass. */
static void precache_decode(void *ctx, size_t page_index) {
    app_state_t *app = (app_state_t*)ctx;
    for (size_t i = 0; i < app->pending_decode_count; ++i) {
        if (app->pending_decode[i] == page_index) return;
    }
    if (app->pending_decode_count < PENDING_DECODE_MAX) {
        app->pending_decode[app->pending_decode_count++] = page_index;
    }
}

/* Decodes the oldest queued page. Returns whether there was one. */
bool drain_one_pending_decode(app_state_t *app) {
    if (app->pending_decode_count == 0) return false;
    size_t index = app->pending_decode[0];
    app->pending_decode_count--;
    memmove(app->pending_decode, app->pending_decode + 1,
            app->pending_decode_count * sizeof(app->pending_decode[0]));
    ensure_page_loaded_how(app, (int32_t)index, false);
    return true;
}

static void release_evicted(app_state_t *app, const uint64_t *evicted, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        size_t index = (size_t)evicted[i];
        if (index >= page_count(app)) continue;
        if ((int32_t)index == app->media_page) continue; /* the playing video, not a cached picture */
        if (!app->pages[index].texture) continue;
        /* §7.4: the budget decided this page has to go. */
        rubraview_pal_texture_destroy(app->pages[index].texture);
        page_forget(&app->pages[index]);
    }
}

/* Keeps the ring in step with the page on screen, and lets the budget
   reclaim whatever fell out of it. */
void update_precache(app_state_t *app) {
    if (page_count(app) == 0) return;

    int32_t current = current_page_index(app);
    if (current < 0) return;

    rubraview_precache_set_interval(&app->precache,
                                    app->slideshow_running ? app->slideshow.interval_seconds : 0.0,
                                    0.5);

    uint64_t evicted[32];
    size_t evicted_count = 0;
    rubraview_precache_update(&app->precache, (size_t)current, precache_decode, app,
                              evicted, sizeof(evicted) / sizeof(evicted[0]), &evicted_count);
    /* §7.4: give back the textures the budget decided it could not keep. */
    release_evicted(app, evicted, evicted_count);
}
