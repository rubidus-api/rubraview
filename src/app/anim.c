/*
 * Animated images and sub-pages: their frames read, shown and kept, a frame copied or taken, the clock that
 * steps them, and seeking (§3.20; RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* ---- animated images and sub-pages (§3.20) ---- */

/* A TIFF's pages or an ICO's sizes; an animation's frames are as many as the file has. */
#define ANIM_MAX_FRAMES 8192
/* Every frame's picture is kept while all of them together fit in this.
   Of a longer animation (D-84) every so-manieth is kept, and the frames
   between are drawn again from the nearest kept one before them. */
#define ANIM_CACHE_BYTES ((uint64_t)384 << 20)

void next_spread(app_state_t *app);

/* D-82: an animation that plays by itself, and so is handled as a film
   is — a place in seconds, a seek bar, A-B, speed, stop. */
bool anim_plays(const app_state_t *app) {
    return app->anim_active && !app->media && app->animation.kind == RUBRAVIEW_FRAMES_ANIMATION;
}

/* What happens at its end: 0 round again, 1 stop there, 2 the next file. */
int anim_end_mode(const app_state_t *app) {
    int mode = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("animation_end")));
    return mode >= 0 && mode <= 2 ? mode : 0;
}

static void anim_close(app_state_t *app) {
    if (app->anim_cache) {
        for (size_t i = 0; i < app->animation.frame_count; ++i) free(app->anim_cache[i]);
        free(app->anim_cache);
        app->anim_cache = NULL;
    }
    app->anim_kept_bytes = 0;
    free(app->anim_buffers);
    app->anim_buffers = NULL;
    if (app->anim_reader) {
        /* The frame list was the reader's. */
        app->animation.frames = NULL;
        app->animation.frame_count = 0;
        app->anim_canvas.frames = NULL;
        app->anim_canvas.frame_count = 0;
    }
    rubraview_pal_anim_close(app->anim_reader);
    app->anim_reader = NULL;
    app->anim_texture = NULL;   /* the page owns it */
}

static bool anim_read_frame(void *ctx, size_t index, uint8_t *dst, size_t stride) {
    app_state_t *app = (app_state_t*)ctx;
    return rubraview_pal_anim_read(app->anim_reader, index, dst, stride, app->animation.frames[index].height);
}

/* One frame of the open animation as the whole picture, on the page. */
/* RV-108 (RFC-0001 §5.3, §3.20.1): the picture on screen of a film or an
   animation, copied to the clipboard or written as a PNG beside its file.
   It is asked for (`frame_wanted`) and taken where the next frame's
   pixels are at hand: a paused film is asked for its frame again. */
double play_position(const app_state_t *app);

/* RV-141 (5): a buffer's size from a picture's, checked — SIZE_MAX, which malloc refuses,
   when it does not fit (a width and a height come out of a file). */
size_t picture_bytes(size_t w, size_t h, size_t per_pixel, size_t extra) {
    size_t n = 0;
    if (!rubraview_size_product(&n, w, h, per_pixel) || PROVEN_CKD_ADD(&n, n, extra)) return SIZE_MAX;
    return n;
}

size_t bytes_scaled(size_t n, size_t k, size_t extra) {
    size_t out = 0;
    if (n == SIZE_MAX || PROVEN_CKD_MUL(&out, n, k) || PROVEN_CKD_ADD(&out, out, extra)) return SIZE_MAX;
    return out;
}

/* RV-116 (owner, 2026-10-14): Ctrl+C, or Copy picture, on a still picture — the
   picture as its file holds it, at its own size (not turned or adjusted as it is
   shown), read again into memory of its own that is given back at once. */
void picture_copy(app_state_t *app) {
    int32_t page = current_page_index(app);
    if (page < 0 || (size_t)page >= page_count(app) || !app->pages[page].loaded) {
        osd_say(app, TU8("there is no picture to copy"));
        return;
    }
    int32_t w = real_w(&app->pages[page]), h = real_h(&app->pages[page]);
    if (w <= 0 || h <= 0 || (uint64_t)w * (uint64_t)h > (uint64_t)200000000u) {
        osd_say(app, TU8("this picture is too large to copy"));
        return;
    }
    size_t big = picture_bytes((size_t)w, (size_t)h, 4u * 3u, 16u << 20);
    void *memory = malloc(big);
    if (!memory) { osd_say(app, TU8("there is not memory enough to copy this picture")); return; }
    proven_arena_t scratch = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)memory, .size = big });
    rubraview_pixbuf_t full = {0};
    proven_u8str_view_t path = app->source.pages[page].path;
    if (path.size > 0) {
        full = rubraview_pal_image_read_pixels_within(&scratch, path, NULL, 0, true, 0, 0);
    } else {
        rubraview_page_bytes_t bytes = { .ok = false };
        AcquireSRWLockExclusive(&app->source_lock);
        bytes = rubraview_page_source_read(&scratch, &app->source, (size_t)page, MAX_PAGE_BYTES);
        ReleaseSRWLockExclusive(&app->source_lock);
        if (bytes.ok && bytes.data.size > 0) {
            full = rubraview_pal_image_read_pixels_within(&scratch, PROVEN_LIT(""),
                                                          (const uint8_t*)bytes.data.ptr, bytes.data.size, true, 0, 0);
        }
    }
    rubraview_pixbuf_t bgra = !rubraview_pixbuf_is_valid(&full) || full.format == RUBRAVIEW_PIXFMT_BGRA8
        ? full : rubraview_pixbuf_convert(&scratch, &full, RUBRAVIEW_PIXFMT_BGRA8);
    bool copied = rubraview_pixbuf_is_valid(&bgra) &&
                  rubraview_pal_clipboard_set_image(app->window, bgra.pixels, bgra.width, bgra.height, (size_t)bgra.stride);
    free(memory);
    osd_say(app, copied ? TU8("the picture is on the clipboard") : TU8("the picture could not be copied"));
}

void frame_take(app_state_t *app, const uint8_t *bgra, int32_t width, int32_t height, size_t stride) {
    int wanted = app->frame_wanted;
    app->frame_wanted = FRAME_NOT;
    if (wanted == FRAME_NOT || !bgra || width <= 0 || height <= 0) return;
    if (wanted == FRAME_COPY) {
        osd_say(app, rubraview_pal_clipboard_set_image(app->window, bgra, width, height, stride)
                         ? TU8("the frame is on the clipboard") : TU8("the frame could not be copied"));
        return;
    }
    int32_t page = app->media ? app->media_page : app->anim_page;
    if (page < 0 || (size_t)page >= page_count(app) || app->source.archive_path.size > 0) {
        osd_say(app, TU8("a frame is saved beside its file; this one is in an archive (Ctrl+C copies it)"));
        return;
    }
    proven_u8str_view_t path = app->source.pages[page].path;
    proven_u8str_view_t name = rubraview_path_basename(path);
    size_t stem = name.size;
    for (size_t i = name.size; i > 0; --i) if (name.ptr[i - 1] == '.') { stem = i - 1; break; }
    double at = app->media ? app->media_position : play_position(app);
    char file[512];
    int n = snprintf(file, sizeof(file), "%.*s_%02d-%02d-%03d.png", (int)(stem < 400 ? stem : 400), name.ptr,
                     (int)(at / 60.0), (int)fmod(at, 60.0), (int)(fmod(at, 1.0) * 1000.0));
    if (n <= 0) return;
    proven_u8str_view_t target = rubraview_path_join(app->arena, rubraview_path_dirname(path), (proven_u8str_view_t){ .ptr = (const proven_byte_t *)file, .size = (size_t)n });
    /* The frame as the writer wants it: its own rows, alpha full. */
    uint8_t *copy = (uint8_t*)rubraview_malloc_product((size_t)width, 4u, (size_t)height);
    if (!copy) return;
    for (int32_t y = 0; y < height; ++y) {
        memcpy(copy + (size_t)y * (size_t)width * 4u, bgra + (size_t)y * stride, (size_t)width * 4u);
        for (int32_t x = 0; x < width; ++x) copy[((size_t)y * (size_t)width + (size_t)x) * 4u + 3u] = 255;
    }
    rubraview_pixbuf_t pixels = { .pixels = copy, .width = width, .height = height, .stride = width * 4,
                                  .format = RUBRAVIEW_PIXFMT_BGRA8 };
    rubraview_export_options_t options = rubraview_export_defaults();
    options.format = RUBRAVIEW_EXPORT_PNG;
    bool written = rubraview_pal_image_save(target, &pixels, &options);
    free(copy);
    if (wanted == FRAME_EDIT && written && target.size < sizeof(app->edit_after)) {
        memcpy(app->edit_after, target.ptr, target.size);   /* Edit opens once it is the page on screen */
        app->edit_after[target.size] = '\0';
        reopen_on_path(app, app_keep(app, target), (size_t)page + 1);
    }
    char line[600];
    snprintf(line, sizeof(line), written ? T("frame saved: %s") : T("could not write %s"), file);
    osd_say(app, cstr(line));
}

static void anim_show(app_state_t *app, size_t frame_index) {
    app_page_t *page = &app->pages[app->anim_page];
    rubraview_frame_canvas_t *canvas = &app->anim_canvas;
    size_t stride = (size_t)canvas->width * 4;
    size_t whole = stride * (size_t)canvas->height;

    const uint8_t *picture = app->anim_cache ? app->anim_cache[frame_index] : NULL;
    if (!picture) {
        /* From the nearest kept picture before it, unless the canvas
           already stands nearer. */
        size_t start = rubraview_frame_canvas_start(canvas, frame_index);
        size_t near_kept = frame_index;
        bool kept_before = false;
        if (app->anim_cache) {
            while (near_kept > start) {
                --near_kept;
                if (app->anim_cache[near_kept]) { kept_before = true; break; }
            }
        }
        bool canvas_nearer = canvas->shown >= 0 && (size_t)canvas->shown <= frame_index &&
                             (!kept_before || (size_t)canvas->shown >= near_kept);
        if (kept_before && !canvas_nearer) rubraview_frame_canvas_seed(canvas, near_kept, app->anim_cache[near_kept]);
        if (!rubraview_frame_canvas_show(canvas, frame_index, anim_read_frame, app)) return;
        picture = canvas->pixels;
        /* Kept when it is far enough from the last kept one, and there is room. */
        size_t apart = kept_before ? frame_index - near_kept : frame_index - start + app->anim_keep_stride;
        if (app->anim_cache && apart >= app->anim_keep_stride &&
            rubraview_frame_canvas_keepable(canvas, frame_index) &&
            app->anim_kept_bytes + whole <= ANIM_CACHE_BYTES) {
            uint8_t *kept = (uint8_t*)malloc(whole);
            if (kept) {
                memcpy(kept, canvas->pixels, whole);
                app->anim_cache[frame_index] = kept;
                app->anim_kept_bytes += whole;
            }
        }
    }
    {
        /* The texture has no see-through: what shows through is the
           window's own colour, as around any picture. */
        const uint8_t back[3] = { (uint8_t)(COLOR_CANVAS & 0xFF), (uint8_t)((COLOR_CANVAS >> 8) & 0xFF), (uint8_t)((COLOR_CANVAS >> 16) & 0xFF) };
        uint8_t *flat = canvas->scratch;
        for (size_t i = 0; i < whole; i += 4) {
            uint32_t a = picture[i + 3];
            if (a == 255) { memcpy(flat + i, picture + i, 4); continue; }
            uint32_t keep = 255 - a;
            for (int c = 0; c < 3; ++c) {
                uint32_t v = picture[i + c] + (back[c] * keep + 127) / 255;
                flat[i + c] = (uint8_t)(v > 255 ? 255 : v);
            }
            flat[i + 3] = 255;
        }
        picture = flat;
    }

    if (!page->texture || page->texture != app->anim_texture) {
        rubraview_texture_t *texture = rubraview_pal_texture_create_bgra(app->renderer, canvas->width, canvas->height);
        if (!texture) return;
        if (page->texture) rubraview_pal_texture_destroy(page->texture);
        page->texture = texture;
        app->anim_texture = texture;
        if (page->width != canvas->width || page->height != canvas->height) app->needs_relayout = true;
        page->width = page->full_width = canvas->width;
        page->height = page->full_height = canvas->height;
        page->reduced = false;
        page->loaded = true;
    }
    (void)rubraview_pal_texture_upload_bgra(page->texture, picture, (int32_t)stride);
    if (app->frame_wanted) frame_take(app, picture, canvas->width, canvas->height, stride);
}

/* Replaces the page's texture with one frame of it. The page keeps its
   own dimensions from the frame, because an ICO's mipmaps genuinely
   differ in size and the layout has to follow. */
void show_frame(app_state_t *app, size_t frame_index) {
    if (app->anim_page < 0) return;
    if (app->anim_reader) { anim_show(app, frame_index); return; }
    app_page_t *page = &app->pages[app->anim_page];

    proven_u8str_view_t path = app->source.pages[app->anim_page].path;
    rubraview_page_bytes_t bytes = { .data = PROVEN_LIT(""), .from_disk = true, .ok = false };
    void *mem = NULL;
    if (path.size == 0) {
        if (page_read_cost(app, (size_t)app->anim_page) > PAGE_JOB_MIN_COST) return;
        bytes = page_read_owned(&app->source, &app->source_lock, (size_t)app->anim_page, &mem);
        if (!bytes.ok) return;
    }

    double delay = 0.0;
    rubraview_image_load_result_t loaded = rubraview_pal_image_load_frame(
        app->renderer, path, (const uint8_t*)bytes.data.ptr, bytes.data.size,
        frame_index, true, &delay);
    free(mem);
    if (!loaded.ok) return;

    if (page->texture) rubraview_pal_texture_destroy(page->texture);
    page->texture = loaded.texture;
    page->width = loaded.width;
    page->height = loaded.height;
    page->full_width = loaded.full_width;
    page->full_height = loaded.full_height;
    page->reduced = loaded.reduced;
    page->loaded = true;
    app->needs_relayout = true;
}

/* Called whenever the page on screen changes. Most pages are ordinary
   images and this costs one frame-count query; the rest set up the
   clock, or — for an ICO — jump straight to the biggest mipmap, which
   §3.20.2 says is the one worth showing. */
void animation_prepare(app_state_t *app) {
    anim_close(app);
    app->anim_active = false;
    app->anim_page = -1;

    int32_t page_index = current_page_index(app);
    if (page_index < 0 || (size_t)page_index >= page_count(app)) return;

    if (app->pages[page_index].skipped) return;   /* D-98 */
    proven_u8str_view_t path = app->source.pages[page_index].path;
    /* A film or a track is no picture to look for frames in. Looking read
       the whole file, twice, before it played — minutes over a network
       share (owner, 2026-10-11; since D-82, 0.0.39). */
    if (is_media_path(path) || is_text_path(path) || is_text_path(app->source.pages[page_index].name)) return;
    rubraview_page_bytes_t bytes = { .data = PROVEN_LIT(""), .from_disk = true, .ok = false };
    void *mem = NULL;
    if (path.size == 0) {
        /* A far page in a streamed block is not read twice to look for frames. */
        if (page_read_cost(app, (size_t)page_index) > PAGE_JOB_MIN_COST) return;
        bytes = page_read_owned(&app->source, &app->source_lock, (size_t)page_index, &mem);
        if (!bytes.ok) return;
    }

    /* D-98: in a layout for pictures a picture that moves is not played but
       left out — unless it is the one the reader asked for by name. */
    if (app->layout_opts.mode != RUBRAVIEW_PAGE_LAYOUT_SINGLE && page_index != app->layout_exempt &&
        page_moves(app, (size_t)page_index, &bytes)) {
        bool others = false;
        for (size_t i = 0; i < page_count(app) && !others; ++i) {
            others = !is_media_path(app->source.pages[i].path) && !is_text_path(app->source.pages[i].path) &&
                     !app->pages[i].animated && (int32_t)i != page_index;
        }
        app->pages[page_index].animated = true;
        if (others) {
            free(mem);
            app->needs_relayout = true;
            return;
        }
    }

    /* D-82, D-84: an animated GIF, WebP or PNG first — kept open, laid
       together and played as a film is. */
    size_t timed_count = 0;
    int32_t canvas_w = 0, canvas_h = 0;
    const rubraview_frame_t *timed_frames = NULL;
    app->anim_reader = rubraview_pal_anim_open(path, (const uint8_t*)bytes.data.ptr, bytes.data.size,
                                               &timed_frames, &timed_count, &canvas_w, &canvas_h);
    if (app->anim_reader) {
        free(mem);
        size_t whole = picture_bytes((size_t)canvas_w, (size_t)canvas_h, 4u, 0);
        app->anim_buffers = (uint8_t*)rubraview_malloc_product(whole, 3, 1);
        if (!app->anim_buffers) { anim_close(app); return; }
        rubraview_frame_canvas_init(&app->anim_canvas, timed_frames, timed_count, canvas_w, canvas_h,
                                    app->anim_buffers, app->anim_buffers + whole, app->anim_buffers + whole * 2);
        app->animation = rubraview_animation_create(RUBRAVIEW_FRAMES_ANIMATION, timed_frames, timed_count);
        app->anim_keep_stride = rubraview_frame_keep_stride(whole, timed_count, ANIM_CACHE_BYTES);
        app->anim_kept_bytes = 0;
        app->anim_cache = (uint8_t**)calloc(timed_count, sizeof(uint8_t*));
        app->animation.speed = app->media_speed > 0.0 ? app->media_speed : 1.0;
        app->anim_page = page_index;
        app->anim_active = true;
        app->ab_a = app->ab_b = -1.0;   /* a repeat belongs to the file it was set in */
        if (app->slides && app->spread_index < app->layout.count) {
            app->slides[app->spread_index].kind = RUBRAVIEW_MEDIA_ANIMATED;
            app->slides[app->spread_index].duration_seconds = rubraview_animation_cycle_seconds(&app->animation);
        }
        /* The first frame as the whole picture: the page's own decode is
           the container's first frame alone, which may be a part of it. */
        show_frame(app, 0);
        if (rubraview_pal_anim_cut(app->anim_reader)) {
            char line[128];
            int n = snprintf(line, sizeof(line), T("showing the first %zu pictures — the rest does not fit in memory"),
                             timed_count);
            if (n > 0) {
                osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
                app->notice_seconds = MEDIA_NOTICE_SECONDS;
            }
        }
        return;
    }

    if (!app->anim_frames) {
        /* Kept for the session: one list, for whichever page has sub-pages. */
        app->anim_frames = (rubraview_frame_t*)calloc(ANIM_MAX_FRAMES, sizeof(rubraview_frame_t));
        if (!app->anim_frames) { free(mem); return; }
    }
    size_t count = rubraview_pal_image_frame_info(path, (const uint8_t*)bytes.data.ptr, bytes.data.size,
                                                  app->anim_frames, ANIM_MAX_FRAMES);
    free(mem);
    if (count <= 1) return;               /* an ordinary image */
    if (count > ANIM_MAX_FRAMES) count = ANIM_MAX_FRAMES;

    rubraview_frame_kind_t kind = rubraview_animation_classify(app->anim_frames, count);
    bool timed = kind == RUBRAVIEW_FRAMES_ANIMATION;

    app->animation = rubraview_animation_create(kind, app->anim_frames, count);
    app->anim_page = page_index;
    app->anim_active = true;

    /* §3.2.6: a slide holding an animation waits for one full pass
       rather than the fixed interval, so a slide show does not cut a
       GIF off halfway. */
    if (timed && app->slides && app->spread_index < app->layout.count) {
        app->slides[app->spread_index].kind = RUBRAVIEW_MEDIA_ANIMATED;
        app->slides[app->spread_index].duration_seconds = rubraview_animation_cycle_seconds(&app->animation);
    }

    if (!timed) {
        /* §3.20.2: open an ICO at its largest layer. */
        size_t largest = rubraview_animation_largest_frame(&app->animation);
        if (largest != 0) {
            app->animation.current = largest;
            show_frame(app, largest);
        }
    }
}

void animation_tick(app_state_t *app, double dt) {
    if (!app->anim_active) return;
    app->animation.ab_a = app->ab_a;
    app->animation.ab_b = app->ab_b;
    int at_end = anim_end_mode(app);
    /* A slide show turns the page itself, after one pass. */
    app->animation.once = at_end != 0 && !app->slideshow_running;
    rubraview_animation_event_t event = rubraview_animation_tick(&app->animation, dt);
    if (event == RUBRAVIEW_ANIMATION_NONE) return;
    show_frame(app, app->animation.current);
    app->media_new_picture = true;
    /* The title is not rewritten for every frame: Windows answers each
       change with a message, and the loop would never rest (a film's is
       not either). It says the time when paused, stepped or sought. */
    if (event == RUBRAVIEW_ANIMATION_ENDED) update_window_title(app);
    if (event == RUBRAVIEW_ANIMATION_ENDED && at_end == 2 && app->spread_index + 1 < app->layout.count) next_spread(app);
}

/* D-82: where what plays is, how long it is, and going there — a film, a
   song or an animation alike. */
void media_seek_to(app_state_t *app, double seconds);
double play_position(const app_state_t *app) {
    return app->media ? app->media_position : anim_plays(app) ? rubraview_animation_position(&app->animation) : 0.0;
}
double play_duration(const app_state_t *app) {
    return app->media ? app->media_info.duration_seconds
         : anim_plays(app) ? rubraview_animation_cycle_seconds(&app->animation) : 0.0;
}
void play_seek(app_state_t *app, double seconds) {
    if (app->media) {
        media_seek_to(app, seconds);
        app->media_position = seconds;
        return;
    }
    if (!anim_plays(app)) return;
    double duration = rubraview_animation_cycle_seconds(&app->animation);
    if (seconds > duration) seconds = duration;
    rubraview_animation_seek(&app->animation, seconds);
    show_frame(app, app->animation.current);
    update_window_title(app);
}
