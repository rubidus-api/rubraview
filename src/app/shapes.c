/*
 * RV-192 (owner, 2026-10-30: "파일 브라우저, 재생목록 등에서 가로로 긴 파일, 세로로 긴 파일, 정사각형
 * 비율 파일 등만 선택하거나 해제"): the open-file screen's Shape and the list windows' Shape — the
 * pictures and films picked or let go by shape. Their sizes are read on a thread of their own the
 * first time (rubraview_pal_sizes_*), kept by path and modified time, and the choice opens once
 * they are all known.
 */
#include "app.h"
#include "rubraview/stream.h"

static bool shape_kind(proven_u8str_view_t path) {
    if (path.size == 0 || rubraview_stream_is_address(path)) return false;
    proven_u8str_view_t name = rubraview_path_basename(path);
    return rubraview_glob_match_list(name, PROVEN_LIT(IMAGE_FILTER)) || rubraview_glob_match_list(name, PROVEN_LIT(VIDEO_FILTER));
}

/* What a target holds: n items, the i-th's path and modified time (0 for a list's page). */
static size_t shape_items(app_state_t *app, int32_t target) {
    if (target == SHAPE_FOR_PICKER) return app->picker_in_book ? 0 : app->picker_listing.count;
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

static void shape_reader_end(app_state_t *app) {
    rubraview_pal_sizes_stop(app->shape_reader);
    app->shape_reader = NULL;
    free(app->shape_reader_mtime);
    app->shape_reader_mtime = NULL;
}

void shape_free(app_state_t *app) {
    shape_reader_end(app);
    rubraview_shape_cache_free(&app->shape_cache);
    app->shape_for = 0;
}

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

void shape_begin(app_state_t *app, int32_t target, int where, double x, double y, double above_y) {
    shape_reader_end(app);
    size_t count = shape_items(app, target);
    if (count == 0) { osd_say(app, target == SHAPE_FOR_PICKER && app->picker_in_book ? TU8("inside a book: tap a page to go to it")
                                                                                     : TU8("no pictures or films here")); return; }
    app->shape_for = target;
    app->shape_generation = app->picker_generation;
    app->shape_pages = app->source.pages;
    app->shape_where = where;
    app->shape_x = x; app->shape_y = y; app->shape_above = above_y;

    /* The ones not read before go to the reader; with none, the choice opens at once. */
    proven_u8str_view_t *paths = (proven_u8str_view_t*)calloc(count, sizeof(*paths));
    int64_t *mtimes = (int64_t*)calloc(count, sizeof(int64_t));
    if (!paths || !mtimes) { free(paths); free(mtimes); return; }
    size_t missing = 0;
    for (size_t i = 0; i < count; ++i) {
        int64_t mtime = 0;
        proven_u8str_view_t path = shape_item(app, target, i, &mtime);
        if (!shape_kind(path) || rubraview_shape_cache_get(&app->shape_cache, path, mtime, NULL)) continue;
        paths[missing] = path; mtimes[missing] = mtime;
        missing++;
    }
    if (missing == 0) {
        free(paths); free(mtimes);
        bool any = false;
        for (size_t i = 0; i < count && !any; ++i) { int64_t m = 0; any = shape_kind(shape_item(app, target, i, &m)); }
        if (!any) { osd_say(app, TU8("no pictures or films here")); return; }
        chooser_open_shape(app, where, x, y, above_y);
        return;
    }
    app->shape_reader = rubraview_pal_sizes_start(paths, missing, thumbs_wake, app->window);
    free(paths);
    if (!app->shape_reader) { free(mtimes); osd_say(app, TU8("the sizes could not be read")); return; }
    app->shape_reader_mtime = mtimes;
    app->shape_said = 0.0;
}

/* Each pass: how far the reading is; when it is done, the sizes kept and the choice opened. */
void shape_step(app_state_t *app) {
    if (!app->shape_reader) return;
    if (!shape_target_alive(app)) { shape_reader_end(app); return; }   /* another folder or list: for nobody */
    size_t done = rubraview_pal_sizes_done(app->shape_reader), count = rubraview_pal_sizes_count(app->shape_reader);
    double now = rubraview_pal_time_now_seconds();
    if (done < count) {
        if (now - app->shape_said > 0.25) {
            app->shape_said = now;
            char line[96];
            int k = snprintf(line, sizeof(line), T("reading the sizes: %zu of %zu"), done, count);
            if (k > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)k });
        }
        return;
    }
    for (size_t n = 0; n < count; ++n) {
        int32_t w = 0, h = 0;
        rubraview_pal_sizes_get(app->shape_reader, n, &w, &h);
        (void)rubraview_shape_cache_put(&app->shape_cache, rubraview_pal_sizes_path(app->shape_reader, n),
                                        app->shape_reader_mtime[n], rubraview_shape_of(w, h));
    }
    shape_reader_end(app);
    chooser_open_shape(app, app->shape_where, app->shape_x, app->shape_y, app->shape_above);
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
