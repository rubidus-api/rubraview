/*
 * RV-192 (owner, 2026-10-30: "파일 브라우저, 재생목록 등에서 가로로 긴 파일, 세로로 긴 파일, 정사각형
 * 비율 파일 등만 선택하거나 해제"): the open-file screen's Shape and the list windows' Shape — the
 * pictures and films picked or let go by shape. Their sizes are read on threads of their own the
 * first time (rubraview_pal_sizes_*), kept by path and modified time, and the choice opens once
 * the ones it needs are known.
 *
 * RV-210 (owner, 2026-10-31: "이번에 디스크캐시 넣고 뒤에서 미리 읽어요"): read by our own parsers from
 * the files' heads; read ahead, slowly, for a folder shown in the open-file screen and for the list in
 * its window; kept on disk (media-facts.txt beside history.ini), so a folder seen before is known at once.
 */
#include "app.h"
#include "rubraview/stream.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FACTS_FILE_MAX   (8u << 20)   /* the file read back, at most */
#define FACTS_KEEP       20000u       /* the files it keeps, the newest */
#define FACTS_SAVE_EVERY 20.0         /* seconds between writes after readings; the rest at the end */

static bool shape_kind(proven_u8str_view_t path) {
    if (path.size == 0 || rubraview_stream_is_address(path)) return false;
    proven_u8str_view_t name = rubraview_path_basename(path);
    return rubraview_glob_match_list(name, PROVEN_LIT(IMAGE_FILTER)) || rubraview_glob_match_list(name, PROVEN_LIT(VIDEO_FILTER));
}

/* RV-202: a film or a song: what has a length */
bool facts_has_length(proven_u8str_view_t path) {
    if (path.size == 0 || rubraview_stream_is_address(path)) return false;
    proven_u8str_view_t name = rubraview_path_basename(path);
    return rubraview_glob_match_list(name, PROVEN_LIT(VIDEO_FILTER)) || rubraview_glob_match_list(name, PROVEN_LIT(AUDIO_FILTER));
}

/* What a reading for `then` needs: the pictures and films for a shape or a resolution, the films and songs for a
   length, all three read ahead. */
static bool facts_kind_for(int32_t then, int key, proven_u8str_view_t path) {
    if (then == FACTS_THEN_AHEAD) return shape_kind(path) || facts_has_length(path);
    if (then == FACTS_THEN_SHAPE || (then == FACTS_THEN_PICKER_SORT && key == RUBRAVIEW_PICKER_SORT_RESOLUTION)) return shape_kind(path);
    return facts_has_length(path);
}

/* What a target holds: n items, the i-th's path and modified time (0 for a list's page). */
static size_t shape_items(app_state_t *app, int32_t target) {
    if (target == SHAPE_FOR_PICKER) return app->picker_in_book || rubraview_picker_is_places(app->picker_dir) ? 0 : app->picker_listing.count;
    return target == SHAPE_FOR_LIST ? page_count(app) : 0;
}

static proven_u8str_view_t shape_item(app_state_t *app, int32_t target, size_t i, int64_t *mtime) {
    *mtime = 0;
    if (target == SHAPE_FOR_PICKER) {
        const rubraview_fs_entry_t *e = &app->picker_listing.entries[i];
        if (e->is_directory) return PROVEN_LIT("");
        *mtime = e->mtime;
        return e->path;
    }
    return app->source.pages[i].path;
}

static bool shape_target_alive(app_state_t *app) {
    if (app->shape_for == SHAPE_FOR_PICKER) return app->picker_open && app->picker_generation == app->shape_generation;
    if (app->shape_for == SHAPE_FOR_LIST) return app->source.pages == app->shape_pages && list_editable(app);
    return false;
}

/* ---- the file on disk ---- */

static void facts_load(app_state_t *app) {
    if (app->facts_loaded) return;
    app->facts_loaded = true;
    /* beside history.ini, wherever this viewer keeps it (beside itself when portable) */
    proven_u8str_view_t base = rubraview_path_basename(app->history_path);
    size_t dir = app->history_path.size - base.size;
    static const char name[] = "media-facts.txt";
    if (app->history_path.size == 0 || dir + sizeof(name) > sizeof(app->facts_path)) return;
    memcpy(app->facts_path, app->history_path.ptr, dir);
    memcpy(app->facts_path + dir, name, sizeof(name));
    void *memory = malloc(FACTS_FILE_MAX + 4096u);
    if (!memory) return;
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t *)memory, .size = FACTS_FILE_MAX + 4096u });
    proven_u8str_view_t text = rubraview_pal_fs_read_file(&arena, cstr(app->facts_path), FACTS_FILE_MAX);
    (void)rubraview_shape_cache_parse(&app->shape_cache, (const char *)text.ptr, text.size);
    free(memory);
}

static bool facts_kept_out(void *context, proven_u8str_view_t path) {
    return recent_kept_out((const app_state_t *)context, path);   /* D-104: nothing of such a folder is written down */
}

/* What was read since is written: merged with what other viewers wrote, the newest kept. Not when the reader
   keeps no list of what was opened (Settings › Recent files). */
static void facts_save(app_state_t *app) {
    if (!app->facts_dirty || app->facts_path[0] == '\0') return;
    app->facts_dirty = false;
    app->facts_saved_at = rubraview_pal_time_now_seconds();
    if (rubraview_settings_get(&app->settings, PROVEN_LIT("recent"), PROVEN_LIT("keep")) < 0.5) return;
    bool locked = rubraview_pal_config_lock(CONFIG_LOCK_MS);
    void *memory = malloc(FACTS_FILE_MAX + 4096u);
    if (memory) {
        proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t *)memory, .size = FACTS_FILE_MAX + 4096u });
        proven_u8str_view_t text = rubraview_pal_fs_read_file(&arena, cstr(app->facts_path), FACTS_FILE_MAX);
        (void)rubraview_shape_cache_parse(&app->shape_cache, (const char *)text.ptr, text.size);   /* another viewer's */
        free(memory);
    }
    size_t size = 0;
    char *out = rubraview_shape_cache_serialize(&app->shape_cache, FACTS_KEEP, facts_kept_out, app, &size);
    if (out) (void)rubraview_pal_fs_write_file_atomic(cstr(app->facts_path), (proven_u8str_view_t){ .ptr = (const proven_byte_t *)out, .size = size });
    free(out);
    if (locked) rubraview_pal_config_unlock();
}

/* ---- the reader ---- */

static void shape_reader_end(app_state_t *app) {
    if (app->shape_reader && app->shape_ahead && rubraview_pal_sizes_done(app->shape_reader) < rubraview_pal_sizes_count(app->shape_reader)) {
        /* a reading ahead cut short starts again when it can */
        if (app->shape_for == SHAPE_FOR_PICKER) app->facts_ahead_for = 0;
        else app->facts_ahead_pages = NULL;
    }
    rubraview_pal_sizes_stop(app->shape_reader);
    app->shape_reader = NULL;
    app->shape_ahead = false;
    free(app->shape_reader_mtime);
    app->shape_reader_mtime = NULL;
    free(app->shape_taken);
    app->shape_taken = NULL;
}

void shape_free(app_state_t *app) {
    shape_reader_end(app);
    facts_save(app);
    rubraview_shape_cache_free(&app->shape_cache);
    app->shape_for = 0;
}

/* How many of the files of `target` that `then` needs are not known yet. */
static size_t facts_missing(app_state_t *app, int32_t target, int32_t then, int key) {
    size_t count = shape_items(app, target), missing = 0;
    for (size_t i = 0; i < count; ++i) {
        int64_t mtime = 0;
        proven_u8str_view_t path = shape_item(app, target, i, &mtime);
        if (facts_kind_for(then, key, path) && !rubraview_shape_cache_get(&app->shape_cache, path, mtime, NULL)) missing++;
    }
    return missing;
}

/* The answers come in: each kept as it comes, under the file's own time (a list's page has none of its own). */
static void facts_take(app_state_t *app) {
    size_t count = rubraview_pal_sizes_count(app->shape_reader);
    for (size_t n = 0; n < count; ++n) {
        if (app->shape_taken[n]) continue;
        int32_t w = 0, h = 0;
        double seconds = 0.0;
        int64_t mtime = 0;
        bool confirmed = false;
        if (!rubraview_pal_sizes_get(app->shape_reader, n, &w, &h, &seconds, &mtime, &confirmed)) continue;
        app->shape_taken[n] = 1;
        proven_u8str_view_t path = rubraview_pal_sizes_path(app->shape_reader, n);
        int64_t key = app->shape_reader_mtime[n] ? app->shape_reader_mtime[n] : mtime;
        if (confirmed) { (void)rubraview_shape_cache_confirm(&app->shape_cache, path, mtime); continue; }
        (void)rubraview_shape_cache_put_facts(&app->shape_cache, path, key, w, h, seconds);
        if (key != 0) app->facts_dirty = true;
    }
}

/* A reading of the files of `target` that `then` needs and are not known; a list's page kept on disk is only
   checked against its time. False when none is missing (or it could not start). */
static bool facts_start(app_state_t *app, int32_t target, int32_t then, int key, bool ahead) {
    facts_load(app);
    size_t count = shape_items(app, target);
    proven_u8str_view_t *paths = (proven_u8str_view_t*)calloc(count ? count : 1, sizeof(*paths));
    int64_t *mtimes = (int64_t*)calloc(count ? count : 1, sizeof(int64_t));
    int64_t *expect = (int64_t*)calloc(count ? count : 1, sizeof(int64_t));
    size_t missing = 0;
    for (size_t i = 0; paths && mtimes && expect && i < count; ++i) {
        int64_t mtime = 0;
        proven_u8str_view_t path = shape_item(app, target, i, &mtime);
        if (!facts_kind_for(then, key, path) || rubraview_shape_cache_get(&app->shape_cache, path, mtime, NULL)) continue;
        if (mtime == 0) (void)rubraview_shape_cache_unverified(&app->shape_cache, path, &expect[missing]);
        paths[missing] = path;
        mtimes[missing] = mtime;
        missing++;
    }
    if (missing > 0) app->shape_reader = rubraview_pal_sizes_start(paths, expect, missing, ahead, thumbs_wake, app->window);
    free(paths);
    free(expect);
    if (!app->shape_reader) { free(mtimes); return false; }
    app->shape_taken = (uint8_t *)calloc(missing, 1);
    if (!app->shape_taken) { rubraview_pal_sizes_stop(app->shape_reader); app->shape_reader = NULL; free(mtimes); return false; }
    app->shape_reader_mtime = mtimes;
    app->shape_for = target;
    app->shape_generation = app->picker_generation;
    app->shape_pages = app->source.pages;
    app->shape_ahead = ahead;
    app->shape_said = 0.0;
    return true;
}

/* ---- what is asked ---- */

bool shape_table(app_state_t *app, int32_t target, uint8_t **out_shapes, size_t **out_rows, size_t *out_n, bool **out_selected) {
    *out_shapes = NULL; *out_rows = NULL; *out_n = 0; *out_selected = NULL;
    size_t count = shape_items(app, target);
    bool *selected = target == SHAPE_FOR_PICKER ? app->picker_selected
                   : (app->list_checked_count == count ? app->list_checked : NULL);
    if (count == 0 || !selected) return false;
    uint8_t *shapes = (uint8_t*)calloc(count, 1);
    size_t shown = target == SHAPE_FOR_LIST ? list_rows(app) : count;
    size_t *order = (size_t*)calloc(shown ? shown : 1, sizeof(size_t));
    if (!shapes || !order) { free(shapes); free(order); return false; }
    /* The pictures and films only: a list's rows it shows (as its box for all does), the open-file
       screen's all it lists. A folder, a song or a text is no row here; one whose size could not
       be read stays unknown. */
    size_t rows = 0;
    for (size_t r = 0; r < shown; ++r) {
        size_t i = target == SHAPE_FOR_LIST ? list_page(app, r) : r;
        int64_t mtime = 0;
        proven_u8str_view_t path = i < count ? shape_item(app, target, i, &mtime) : PROVEN_LIT("");
        if (!shape_kind(path)) continue;
        rubraview_shape_t s = RUBRAVIEW_SHAPE_UNKNOWN;
        if (rubraview_shape_cache_get(&app->shape_cache, path, mtime, &s)) shapes[i] = (uint8_t)s;
        order[rows++] = i;
    }
    *out_shapes = shapes; *out_rows = order; *out_n = rows; *out_selected = selected;
    return true;
}

/* What a reading ends in: the shape's choice opened, or the sort it waited for made. */
static void facts_then(app_state_t *app) {
    int32_t then = app->shape_then;
    app->shape_then = FACTS_THEN_SHAPE;
    if (then == FACTS_THEN_LIST_SORT) list_sort(app, app->shape_sort_key, app->shape_sort_desc);
    else if (then == FACTS_THEN_PICKER_SORT) picker_sort_set(app, app->shape_sort_key, app->shape_sort_desc);
    else chooser_open_shape(app, app->shape_where, app->shape_x, app->shape_y, app->shape_above);
}

static void facts_begin(app_state_t *app, int32_t target, int where, double x, double y, double above_y) {
    size_t count = shape_items(app, target);
    if (count == 0) { osd_say(app, target == SHAPE_FOR_PICKER && app->picker_in_book ? TU8("inside a book: tap a page to go to it")
                                                                                     : TU8("no pictures or films here")); return; }
    if (target == SHAPE_FOR_LIST && !list_editable(app)) { osd_say(app, TU8("a book's pages are not picked by shape")); return; }
    app->shape_where = where;
    app->shape_x = x; app->shape_y = y; app->shape_above = above_y;
    facts_load(app);
    if (facts_missing(app, target, app->shape_then, app->shape_sort_key) == 0) {
        /* all known (read before, or kept on disk): at once, a reading ahead left to go on */
        if (app->shape_then != FACTS_THEN_SHAPE) { facts_then(app); return; }
        bool any = false;
        for (size_t i = 0; i < count && !any; ++i) { int64_t m = 0; any = shape_kind(shape_item(app, target, i, &m)); }
        if (!any) { osd_say(app, TU8("no pictures or films here")); return; }
        chooser_open_shape(app, where, x, y, above_y);
        return;
    }
    if (app->shape_reader && app->shape_ahead && app->shape_for == target && shape_target_alive(app)) {
        /* already being read ahead: that reading is taken over, hurried, and the answer given when what is asked is known */
        app->shape_ahead = false;
        app->shape_said = 0.0;
        rubraview_pal_sizes_hurry(app->shape_reader);
        return;
    }
    int32_t then = app->shape_then;
    shape_reader_end(app);
    app->shape_then = then;
    if (!facts_start(app, target, then, app->shape_sort_key, false)) osd_say(app, TU8("the sizes could not be read"));
}

/* RV-202: the files of `target` a sort by length (or the open-file screen's by resolution) needs read first: true
   when a reading started for them (the sort is made when it ends), false when all are known and it can be made now. */
bool facts_read_for_sort(app_state_t *app, int32_t target, int32_t then, int key, bool desc) {
    if (app->shape_reader && !app->shape_ahead && app->shape_then == then && app->shape_for == target) return true;   /* already reading for it */
    facts_load(app);
    if (facts_missing(app, target, then, key) == 0) return false;
    app->shape_then = then;
    app->shape_sort_key = key;
    app->shape_sort_desc = desc;
    facts_begin(app, target, CHOOSE_IN_MAIN, 0.0, 0.0, 0.0);
    return app->shape_reader != NULL;
}

void shape_begin(app_state_t *app, int32_t target, int where, double x, double y, double above_y) {
    app->shape_then = FACTS_THEN_SHAPE;
    facts_begin(app, target, where, x, y, above_y);
}

/* RV-210: nothing being read: what is shown read ahead — the open-file screen's folder, else the list in its window. */
static void facts_ahead(app_state_t *app) {
    if (app->shape_reader) return;
    if (app->picker_open && app->facts_ahead_for != app->picker_generation + 1u) {
        app->facts_ahead_for = app->picker_generation + 1u;
        if (shape_items(app, SHAPE_FOR_PICKER) > 0 && facts_start(app, SHAPE_FOR_PICKER, FACTS_THEN_AHEAD, 0, true)) app->shape_then = FACTS_THEN_AHEAD;
        return;
    }
    if (!app->picker_open && app->listwin.open && list_editable(app) && app->facts_ahead_pages != (const void *)app->source.pages) {
        app->facts_ahead_pages = app->source.pages;
        if (facts_start(app, SHAPE_FOR_LIST, FACTS_THEN_AHEAD, 0, true)) app->shape_then = FACTS_THEN_AHEAD;
    }
}

/* Each pass: the answers come in; what was asked is answered once the files it needs are known. */
void shape_step(app_state_t *app) {
    if (!app->shape_reader) {
        facts_ahead(app);
        if (app->facts_dirty && rubraview_pal_time_now_seconds() - app->facts_saved_at > FACTS_SAVE_EVERY) facts_save(app);
        return;
    }
    if (!shape_target_alive(app)) { int32_t then = app->shape_then; shape_reader_end(app); if (then != FACTS_THEN_AHEAD) app->shape_then = FACTS_THEN_SHAPE; return; }   /* another folder or list: for nobody */
    facts_take(app);
    size_t done = rubraview_pal_sizes_done(app->shape_reader), count = rubraview_pal_sizes_count(app->shape_reader);
    if (!app->shape_ahead) {
        if (facts_missing(app, app->shape_for, app->shape_then, app->shape_sort_key) == 0) {
            /* what was asked is known: answered; the rest of the reading goes on, ahead */
            bool more = done < count;
            facts_then(app);
            if (app->shape_reader) { app->shape_ahead = more; app->shape_then = FACTS_THEN_AHEAD; if (!more) shape_reader_end(app); }
            return;
        }
        double now = rubraview_pal_time_now_seconds();
        if (now - app->shape_said > 0.25) {
            app->shape_said = now;
            char line[96];
            bool sizes = app->shape_then == FACTS_THEN_SHAPE || (app->shape_then == FACTS_THEN_PICKER_SORT && app->shape_sort_key == RUBRAVIEW_PICKER_SORT_RESOLUTION);
            int k = snprintf(line, sizeof(line), sizes ? T("reading the sizes: %zu of %zu") : T("reading the lengths: %zu of %zu"), done, count);
            if (k > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)k });
        }
        return;
    }
    if (done < count) return;
    /* a reading ahead done: the open-file screen sorted again when its order is one of them */
    int32_t target = app->shape_for;
    shape_reader_end(app);
    app->shape_then = FACTS_THEN_SHAPE;
    if (rubraview_pal_time_now_seconds() - app->facts_saved_at > FACTS_SAVE_EVERY) facts_save(app);
    int32_t kept = (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort"))) - 1;
    if (target == SHAPE_FOR_PICKER && app->picker_open && !app->picker_in_book && (kept == RUBRAVIEW_PICKER_SORT_LENGTH || kept == RUBRAVIEW_PICKER_SORT_RESOLUTION))
        picker_resort(app);
}

static const char *shape_word(rubraview_shape_t s) {
    switch (s) {
        case RUBRAVIEW_SHAPE_WIDE:   return T("Wide");
        case RUBRAVIEW_SHAPE_TALL:   return T("Tall");
        case RUBRAVIEW_SHAPE_SQUARE: return T("Square");
        default:                     return T("Size not known");   /* could not be read */
    }
}

const char *shape_name(int32_t shape) { return shape_word((rubraview_shape_t)shape); }

void shape_pick(app_state_t *app, int32_t which) {
    uint8_t *shapes = NULL;
    size_t *rows = NULL, n = 0;
    bool *selected = NULL;
    if (!shape_table(app, app->shape_for, &shapes, &rows, &n, &selected)) return;
    if (app->shape_for == SHAPE_FOR_PICKER && app->picker.mode == RUBRAVIEW_PICK_SINGLE)
        rubraview_picker_set_mode(&app->picker, RUBRAVIEW_PICK_INDIVIDUAL);   /* as Same type does */
    bool picked = false;
    size_t k = rubraview_shape_toggle(selected, shapes, rows, n, (rubraview_shape_t)which, &picked);
    free(shapes);
    free(rows);
    char line[96];
    int w = k == 0 ? snprintf(line, sizeof(line), T("no %s files here"), shape_word((rubraview_shape_t)which))
                   : snprintf(line, sizeof(line), picked ? T("%zu %s picked") : T("%zu %s let go"), k, shape_word((rubraview_shape_t)which));
    if (w > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)w });
    if (app->shape_for == SHAPE_FOR_LIST) app->files_dirty = true;
}
