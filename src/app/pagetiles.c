/*
 * Tiles over a page shown reduced, taken back and handed out, a page enlarged sharply, and the open-file
 * screen's thumbnails stepped (D-40; RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* ---- D-40: tiles over a page shown reduced ---- */

#define TILE_SLOTS (sizeof(((struct tile_set*)0)->slots) / sizeof(((struct tile_set*)0)->slots[0]))
#define TILE_ASK_MAX (sizeof(((struct tile_set*)0)->asked) / sizeof(((struct tile_set*)0)->asked[0]))

/* Every tile goes, and what the thread still makes for them is known as
   late by the new generation. */
static void tile_set_reset(struct tile_set *ts) {
    for (size_t i = 0; i < TILE_SLOTS; ++i) {
        if (ts->slots[i].texture) rubraview_pal_texture_destroy(ts->slots[i].texture);
        ts->slots[i] = (struct tile_slot){0};
    }
    ts->generation++;
    ts->page = -1;
    ts->page_texture = NULL;
    ts->asked_count = 0;
    if (ts->tiles) rubraview_pal_tiles_want(ts->tiles, ts->generation, NULL, 0);
}

void tiles_reset(app_state_t *app) {
    for (size_t i = 0; i < TILE_SETS; ++i) tile_set_reset(&app->tile_sets[i]);
}

static bool tile_key_eq(rubraview_tile_key_t a, rubraview_tile_key_t b) {
    return a.level == b.level && a.tx == b.tx && a.ty == b.ty;
}

static struct tile_slot *tile_find(struct tile_set *ts, rubraview_tile_key_t key) {
    for (size_t i = 0; i < TILE_SLOTS; ++i) {
        if (ts->slots[i].live && tile_key_eq(ts->slots[i].key, key)) return &ts->slots[i];
    }
    return NULL;
}

/* Each pass: turn what the thread has made into textures, the least
   recently drawn tile making room when every slot is taken. */
static void tile_set_take_all(app_state_t *app, struct tile_set *ts) {
    if (!ts->tiles) return;
    rubraview_tile_result_t done;
    while (rubraview_pal_tiles_take(ts->tiles, &done)) {
        if (done.generation == ts->generation && !tile_find(ts, done.key)) {
            struct tile_slot *slot = NULL;
            for (size_t i = 0; i < TILE_SLOTS && !slot; ++i) {
                if (!ts->slots[i].live) slot = &ts->slots[i];
            }
            if (!slot) {
                struct tile_slot *oldest = &ts->slots[0];
                for (size_t k = 1; k < TILE_SLOTS; ++k) {
                    if (ts->slots[k].used < oldest->used) oldest = &ts->slots[k];
                }
                if (oldest->texture) rubraview_pal_texture_destroy(oldest->texture);
                *oldest = (struct tile_slot){0};
                slot = oldest;
            }
            rubraview_texture_t *texture = NULL;
            if (done.bgra) {
                texture = rubraview_pal_texture_create_bgra(app->renderer, done.width, done.height);
                if (texture && !rubraview_pal_texture_upload_bgra(texture, done.bgra, done.width * 4)) {
                    rubraview_pal_texture_destroy(texture);
                    texture = NULL;
                }
            }
            *slot = (struct tile_slot){ .key = done.key, .texture = texture, .used = ++ts->clock,
                                        .live = true, .failed = texture == NULL };
            app->tiles_arrived = true;
        }
        free(done.bgra);
    }
}

void tiles_take_all(app_state_t *app) {
    app->tiles_arrived = false;
    for (size_t i = 0; i < TILE_SETS; ++i) tile_set_take_all(app, &app->tile_sets[i]);
}

/* RV-085: an archive page has no file for the thread to read, so it is
   read here once — the page source (a CB7's solid block, the SDK's index)
   belongs to this thread — into an arena of just its size, not the app's,
   and handed over as a copy. Without it every tile fails once and is not
   asked for again, as for a file that cannot be read. */
static void tiles_give_archive_page(app_state_t *app, struct tile_set *ts, int32_t index, int32_t pw, int32_t ph) {
    uint8_t *copy = NULL;
    size_t copy_size = 0;
    uint64_t size = rubraview_page_source_entry_size(&app->source, (size_t)index);
    size_t budget = rubraview_page_source_read_budget(&app->source, (size_t)index);
    /* Not a page that must be decoded again from far back (a streamed 7z block). */
    bool close_by = page_read_cost(app, (size_t)index) <= PAGE_JOB_MIN_COST;
    void *mem = close_by && size > 0 && size <= MAX_PAGE_BYTES && budget > 0 ? malloc(budget) : NULL;
    if (mem) {
        proven_arena_t scratch = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)mem, .size = budget });
        AcquireSRWLockExclusive(&app->source_lock);
        rubraview_page_bytes_t bytes = rubraview_page_source_read(&scratch, &app->source, (size_t)index, MAX_PAGE_BYTES);
        ReleaseSRWLockExclusive(&app->source_lock);
        if (bytes.ok && bytes.data.size > 0) {
            copy = (uint8_t*)malloc(bytes.data.size);
            if (copy) {
                memcpy(copy, bytes.data.ptr, bytes.data.size);
                copy_size = bytes.data.size;
            }
        }
        free(mem);
    }
    rubraview_pal_tiles_source_bytes(ts->tiles, ts->generation, copy, copy_size, true, pw, ph);
}

/* D-81: is this page, shown larger than its own pixels, to be enlarged
   sharp? Not with the pixel-art toggle or a plainer scaling filter chosen
   (the reader asked for those pixels), nor for a film or an animation,
   whose picture changes under the tiles. */
static bool enlarge_sharp(const app_state_t *app, int32_t index) {
    if (app->force_nearest || app->anim_active || index == app->media_page) return false;
    if (rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("enlarge")) < 0.5) return false;
    return (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("interpolation"))) >= 2;
}

/* After a reduced page is drawn: the tiles on screen, sharper, over it,
   and a request for those not yet made. `place` maps picture pixels to
   the screen (the reader's rotation included); `scale` is its size. */
void draw_page_tiles(app_state_t *app, int which, int32_t index, const app_page_t *page,
                            rubraview_mat3x2_t place, double scale, int32_t win_w, int32_t win_h,
                            double opacity, rubraview_interpolation_t interp) {
    struct tile_set *ts = &app->tile_sets[which == 1 ? 1 : 0];
    if (g_view_ratio > 0.0) return;   /* tiles are cut in the file's pixels: a stretched page keeps its one texture */
    int32_t pw = 0, ph = 0;
    page_picture_size(page, &pw, &ph);
    /* Sharper than the texture in two cases: the page is held reduced and
       shown larger than that (D-40), or it is shown larger than its own
       pixels and enlarging is set to sharp (D-81). */
    bool reduced = page->reduced && rubraview_tiles_wanted(scale, pw, page->width);
    bool enlarged = !reduced && rubraview_tile_level_enlarged(scale) < 0 && enlarge_sharp(app, index);
    if (!reduced && !enlarged) {
        if (ts->page == index && ts->page_texture) tile_set_reset(ts);   /* back at its own size: the tiles are of no use */
        return;
    }
    proven_u8str_view_t path = app->source.pages[index].path;
    if (!ts->tiles) {
        ts->tiles = rubraview_pal_tiles_start(thumbs_wake, app->window);
        if (!ts->tiles) return;
    }
    if (ts->page != index || ts->page_texture != page->texture) {
        /* This set only: the other page of a pair keeps its own tiles
           (resetting both made each undo the other's on every frame). */
        tile_set_reset(ts);
        ts->page = index;
        ts->page_texture = page->texture;
        if (path.size > 0) {
            rubraview_pal_tiles_source(ts->tiles, ts->generation, path, true, pw, ph);
        } else {
            tiles_give_archive_page(app, ts, index, pw, ph);
        }
    }

    rubraview_mat3x2_t inverse;
    if (!rubraview_mat3x2_invert(place, &inverse)) return;
    double xs[4], ys[4];
    rubraview_mat3x2_apply(inverse, 0.0, 0.0, &xs[0], &ys[0]);
    rubraview_mat3x2_apply(inverse, (double)win_w, 0.0, &xs[1], &ys[1]);
    rubraview_mat3x2_apply(inverse, 0.0, (double)win_h, &xs[2], &ys[2]);
    rubraview_mat3x2_apply(inverse, (double)win_w, (double)win_h, &xs[3], &ys[3]);
    double x0 = xs[0], x1 = xs[0], y0 = ys[0], y1 = ys[0];
    for (int i = 1; i < 4; ++i) {
        if (xs[i] < x0) x0 = xs[i];
        if (xs[i] > x1) x1 = xs[i];
        if (ys[i] < y0) y0 = ys[i];
        if (ys[i] > y1) y1 = ys[i];
    }
    int32_t level = enlarged ? rubraview_tile_level_enlarged(scale) : rubraview_tile_level(scale);
    double margin = rubraview_tile_span(level) / 2.0;   /* a little around, for a pan */

    rubraview_tile_key_t keys[128], missing[128];
    size_t n = rubraview_tiles_visible(x0 - margin, y0 - margin, x1 + margin, y1 + margin, level, pw, ph, keys, 128);
    size_t m = 0;
    for (size_t i = 0; i < n; ++i) {
        struct tile_slot *slot = tile_find(ts, keys[i]);
        if (!slot) { missing[m++] = keys[i]; continue; }
        slot->used = ++ts->clock;
        int32_t x, y, w, h, ow, oh;
        if (!slot->texture || !rubraview_tile_geometry(keys[i], pw, ph, &x, &y, &w, &h, &ow, &oh)) continue;
        rubraview_mat3x2_t tile = rubraview_mat3x2_multiply(
            rubraview_mat3x2_multiply(rubraview_mat3x2_scale((double)w / (double)ow, (double)h / (double)oh),
                                      rubraview_mat3x2_translate((double)x, (double)y)),
            place);
        rubraview_pal_render_draw_texture_opacity(app->renderer, slot->texture, tile, interp, opacity);
    }
    if (m != ts->asked_count || memcmp(missing, ts->asked, m * sizeof(missing[0])) != 0) {
        rubraview_pal_tiles_want(ts->tiles, ts->generation, missing, m);
        if (m) memcpy(ts->asked, missing, m * sizeof(missing[0]));
        ts->asked_count = m;
    }
}

/* Each pass: take in what the thread has made, and ask for the tiles now
   on screen that have not been asked for. Nothing here waits. */
/* The owner (2026-10-18): folders that show no thumbnail, typed as the folders kept out of Recent
   are. 1.20.0 ("그 폴더와 그 폴더의 하위 폴더 썸네일만 안 보이게"): only those folders' own tiles,
   and the tiles of the folders in them; the files inside show theirs. */
static bool thumb_kept_out(const app_state_t *app, proven_u8str_view_t path, bool is_directory) {
    proven_u8str_view_t dirs[64];
    size_t n = rubraview_folder_list_split(rubraview_settings_get_text(&app->settings, PROVEN_LIT("thumbs"), PROVEN_LIT("skip_dirs")), dirs, 64);
    if (n > 64) n = 64;
    return n > 0 && path.size > 0 && rubraview_thumb_folder_kept_out(path, is_directory, dirs, n);
}

void picker_thumbs_step(app_state_t *app) {
    app->picker_thumbs_arrived = false;
    if (!app->picker_open || !app->picker_thumbs) return;
    if (!app->thumbs) {
        /* A folder's picture: its first image, else its first film —
           not a sound file, which seldom has one (rbmedia, 2026-09-25). */
        app->thumbs = rubraview_pal_thumbs_start(PROVEN_LIT(IMAGE_FILTER), PROVEN_LIT(VIDEO_FILTER), PROVEN_LIT(ARCHIVE_FILTER),
                                                 thumbs_wake, app->window);
        if (!app->thumbs) return;
        rubraview_pal_thumbs_generation(app->thumbs, app->picker_generation);
    }

    rubraview_thumb_result_t done;
    while (rubraview_pal_thumbs_take(app->thumbs, &done)) {
        if (done.generation == app->picker_generation && done.index < app->picker_thumb_count) {
            struct picker_thumb *t = &app->picker_thumbs[done.index];
            rubraview_texture_t *texture = NULL;
            if (done.bgra) {
                texture = rubraview_pal_texture_create_bgra(app->renderer, done.width, done.height);
                if (texture && !rubraview_pal_texture_upload_bgra(texture, done.bgra, done.width * 4)) {
                    rubraview_pal_texture_destroy(texture);
                    texture = NULL;
                }
            }
            if (t->texture) rubraview_pal_texture_destroy(t->texture);
            t->texture = texture;
            t->state = texture ? 1 : 2;
            /* what the file is, for the details view's columns (owner, 2026-10-16) */
            t->media = (rubraview_picker_media_t){ done.source_width, done.source_height, done.seconds };
            app->picker_thumbs_arrived = true;
        }
        free(done.bgra);
    }

    double tw = 0.0, th = 0.0;
    picker_tile_size(app, &tw, &th);
    if (tw <= 1.0 || th <= 1.0) return;
    /* The newest request is made first, so the last tile on screen is asked
       for first and the first one last: they fill in from the top. */
    rubraview_virtual_range_t visible = rubraview_picker_visible(&app->picker);
    int book_reads = 0;
    for (size_t k = visible.count; k-- > 0;) {
        size_t index = visible.first + k;
        if (index >= app->picker_thumb_count || index >= app->picker_listing.count) continue;
        struct picker_thumb *t = &app->picker_thumbs[index];
        if (t->state != 0 && t->state != 3) continue;
        const rubraview_fs_entry_t *entry = &app->picker_listing.entries[index];
        if (entry->is_directory && proven_u8str_view_eq(entry->name, PROVEN_LIT(".."))) { t->state = 2; continue; }
        /* A drive's first picture is not looked for: an empty reader or a
           lost network drive would hold the one thumbnail thread. */
        if (rubraview_picker_is_places(app->picker_dir)) { t->state = 2; continue; }
        if (!app->picker_in_book && thumb_kept_out(app, entry->path, entry->is_directory)) { t->state = 2; continue; }
        if (app->picker_in_book) {
            /* A page in the book has no file to ask about: its bytes go to
               the thread instead (owner, 2026-09-29) — three a pass, so the
               picker keeps moving, and never a page far inside a streamed
               7z block. */
            size_t page = app->picker_page_of && index >= app->picker_page_skip
                ? app->picker_page_of[index - app->picker_page_skip] : RUBRAVIEW_PICKER_NOT_A_PAGE;
            if (entry->is_directory || page == RUBRAVIEW_PICKER_NOT_A_PAGE || page >= page_count(app) ||
                page_read_cost(app, page) > PAGE_JOB_MIN_COST) { t->state = 2; continue; }
            if (book_reads >= 3) continue;   /* the next pass */
            book_reads++;
            void *mem = NULL;
            rubraview_page_bytes_t pb = page_read_owned(&app->source, &app->source_lock, page, &mem);
            uint8_t *copy = pb.ok && pb.data.size > 0 ? (uint8_t*)malloc(pb.data.size) : NULL;
            if (copy) memcpy(copy, pb.data.ptr, pb.data.size);
            free(mem);
            if (copy && rubraview_pal_thumbs_request_bytes(app->thumbs, app->picker_generation, index, tw / th, copy, pb.data.size)) {
                t->state = 3;
            } else {
                t->state = 2;
            }
            continue;
        }
        /* Asked again while waiting, it moves to the front of the queue. */
        if (rubraview_pal_thumbs_request(app->thumbs, app->picker_generation, index, entry->is_directory,
                                         tw / th, entry->path)) {
            t->state = 3;
        } else if (t->state == 0) {
            t->state = 2;   /* a path the queue cannot hold */
        }
    }
}
