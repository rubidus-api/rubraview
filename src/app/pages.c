/*
 * Pages: how many and their names, loading and unloading, a page's bytes, and far pages read off the main
 * thread (RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

size_t page_count(const app_state_t *app) {
    return app->source.page_count;
}

proven_u8str_view_t page_display_name(const app_state_t *app, size_t index) {
    if (index >= app->source.page_count) return PROVEN_LIT("");
    return app->source.pages[index].name;
}

/* ---- page loading ---- */

void unload_all_pages(app_state_t *app) {
    tiles_reset(app);   /* D-40: made for these pages, on this device */
    if (!app->pages) return;
    for (size_t i = 0; i < page_count(app); ++i) {
        if (app->pages[i].texture) {
            rubraview_pal_texture_destroy(app->pages[i].texture);
        }
        page_forget(&app->pages[i]);
    }
}

/* Whether a page is a video, opened by the media PAL rather than WIC. RV-157: an internet radio
   station is one too, played by FFmpeg alone. */
bool is_media_path(proven_u8str_view_t path) {
    return path.size > 0 && (rubraview_stream_is_address(path) ||
                            rubraview_glob_match_list(rubraview_path_basename(path), PROVEN_LIT(MEDIA_FILTER)));
}

/* RV-157: a page that is an internet radio address: nothing on the disk is asked about it. */
bool is_station_path(proven_u8str_view_t path) {
    return rubraview_stream_is_address(path);
}

/* ---- a page's bytes, and far pages off the main thread ---- */

/* The job in flight is called off and waited for — a 1 MB chunk at most —
   so the main thread can read. */
void page_job_stop(app_state_t *app) {
    page_job_t *job = app->page_job;
    if (!job) return;
    rubraview_page_source_cancel(&app->source, true);
    WaitForSingleObject(job->thread, INFINITE);
    CloseHandle(job->thread);
    rubraview_page_source_cancel(&app->source, false);
    free(job->mem);
    free(job);
    app->page_job = NULL;
}

/* What it costs to reach a page, when nothing is reading it: bytes to
   decode first. While the thread reads, the stream's place is moving. */
uint64_t page_read_cost(app_state_t *app, size_t index) {
    if (app->page_job) return UINT64_MAX;
    return rubraview_page_source_read_cost(&app->source, index);
}

/* A page's bytes in memory of their own, freed by the caller once they
   are a texture. They went into the 64 MB working arena, which is never
   given back: after a few dozen CB7 pages, or a compressed CBZ's worth,
   pages failed to open (found 2026-09-29). A folder page is read by path
   and allocates nothing. */
rubraview_page_bytes_t page_read_owned(rubraview_page_source_t *source, SRWLOCK *lock, size_t index, void **out_mem) {
    *out_mem = NULL;
    rubraview_page_bytes_t none = { .data = PROVEN_LIT(""), .from_disk = false, .ok = false };
    AcquireSRWLockExclusive(lock);
    rubraview_page_bytes_t bytes = none;
    if (source->kind == RUBRAVIEW_PAGE_SOURCE_FOLDER) {
        uint8_t nothing[64];
        proven_arena_t unused = proven_arena_create((proven_mem_mut_t){ .ptr = nothing, .size = sizeof(nothing) });
        bytes = rubraview_page_source_read(&unused, source, index, MAX_PAGE_BYTES);
    } else {
        size_t budget = rubraview_page_source_read_budget(source, index);
        void *mem = budget > 0 && budget <= (size_t)MAX_PAGE_BYTES + 4096u ? malloc(budget) : NULL;
        if (mem) {
            proven_arena_t scratch = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)mem, .size = budget });
            bytes = rubraview_page_source_read(&scratch, source, index, MAX_PAGE_BYTES);
            if (bytes.ok) *out_mem = mem; else free(mem);
        }
    }
    ReleaseSRWLockExclusive(lock);
    return bytes;
}

static DWORD WINAPI page_job_run(LPVOID param) {
    page_job_t *job = (page_job_t*)param;
    job->bytes = page_read_owned(job->source, job->lock, job->index, &job->mem);
    InterlockedExchange(&job->done, 1);
    rubraview_pal_window_wake(job->wake);
    return 0;
}

static void page_take_bytes(app_state_t *app, app_page_t *page, size_t index, rubraview_page_bytes_t bytes);
void update_window_title(app_state_t *app);
int32_t station_connecting_page(const app_state_t *app);

/* Each pass: a far page that has arrived becomes its texture. */
void page_job_poll(app_state_t *app) {
    page_job_t *job = app->page_job;
    if (!job || !InterlockedCompareExchange(&job->done, 0, 0)) return;
    WaitForSingleObject(job->thread, INFINITE);
    CloseHandle(job->thread);
    app->page_job = NULL;
    if (job->index < page_count(app)) {
        app_page_t *page = &app->pages[job->index];
        if (!page->loaded) {
            if (job->bytes.ok) page_take_bytes(app, page, job->index, job->bytes);
            else page->failed = true;
            app->needs_relayout = true;
            update_window_title(app);
        }
    }
    free(job->mem);
    free(job);
}

static void page_job_start(app_state_t *app, size_t index) {
    page_job_stop(app);
    page_job_t *job = (page_job_t*)calloc(1, sizeof(*job));
    if (!job) return;
    job->source = &app->source;
    job->lock = &app->source_lock;
    job->index = index;
    job->wake = app->window;
    job->thread = CreateThread(NULL, 0, page_job_run, job, 0, NULL);
    if (!job->thread) { free(job); return; }
    app->page_job = job;
}

app_page_t *ensure_page_loaded_how(app_state_t *app, int32_t index, bool on_screen);

app_page_t *ensure_page_loaded(app_state_t *app, int32_t index) {
    return ensure_page_loaded_how(app, index, true);
}

/* `on_screen`: the page is wanted now. A far page then goes to the thread;
   the look-ahead never starts a far read, nor waits for one. */
app_page_t *ensure_page_loaded_how(app_state_t *app, int32_t index, bool on_screen) {
    if (index < 0 || (size_t)index >= page_count(app)) return NULL;
    app_page_t *page = &app->pages[index];
    if (page->loaded || page->failed) return page;
    /* A video page gets its texture from media_prepare, and only while
       it is the page on screen: the pre-cache must not open videos. */
    if (is_media_path(app->source.pages[index].path)) return page;
    if (is_text_path(app->source.pages[index].path) || is_text_path(app->source.pages[index].name)) return page;   /* RFC-0005 */

    if (app->page_job) {
        if (app->page_job->index == (size_t)index) return page;   /* on its way */
        if (!on_screen) return page;                              /* the look-ahead waits its turn */
        page_job_stop(app);                                       /* the reader has moved on */
    }
    if (page_read_cost(app, (size_t)index) > PAGE_JOB_MIN_COST) {
        if (on_screen) page_job_start(app, (size_t)index);
        return page;
    }

    void *mem = NULL;
    rubraview_page_bytes_t bytes = page_read_owned(&app->source, &app->source_lock, (size_t)index, &mem);
    if (!bytes.ok) {
        page->failed = true;
        return page;
    }
    page_take_bytes(app, page, (size_t)index, bytes);
    free(mem);
    if (rubraview_pal_fs_mapping_lost(&app->archive_map)) {
        page->failed = !page->loaded;
        osd_say(app, TU8("the archive is no longer there (its drive went away)"));
    }
    return page;
}

/* D-98: is this page a picture that moves? Looked for only in the kinds
   that can, in the file's bytes (mapped, for a file of a folder); nothing
   is decoded. `bytes` may be NULL when the caller has none in hand. */
bool page_moves(app_state_t *app, size_t index, const rubraview_page_bytes_t *bytes) {
    proven_u8str_view_t path = app->source.pages[index].path;
    proven_u8str_view_t name = path.size > 0 ? path : app->source.pages[index].name;
    if (!rubraview_path_has_ext(name, ".gif") && !rubraview_path_has_ext(name, ".webp") &&
        !rubraview_path_has_ext(name, ".png") && !rubraview_path_has_ext(name, ".apng") &&
        !rubraview_path_has_ext(name, ".avif") && !rubraview_path_has_ext(name, ".jxl")) return false;
    if (path.size > 0) {
        rubraview_fs_mapping_t map = {0};
        bool moves = rubraview_pal_fs_map(path, &map) && map.size <= (uint64_t)SIZE_MAX &&
                     rubraview_image_is_animated(map.data, (size_t)map.size);
        rubraview_pal_fs_unmap(&map);
        return moves;
    }
    return bytes && bytes->ok && rubraview_image_is_animated((const uint8_t*)bytes->data.ptr, bytes->data.size);
}

static void page_take_bytes(app_state_t *app, app_page_t *page, size_t index, rubraview_page_bytes_t bytes) {

    /* A folder page is opened by path; an archive page is decoded from
       the bytes the source produced, which never touched the disk. */
    rubraview_image_load_result_t loaded = bytes.from_disk
        ? rubraview_pal_image_load_texture(app->renderer, app->source.pages[index].path, true)
        : rubraview_pal_image_load_texture_from_memory(app->renderer,
                                                       (const uint8_t*)bytes.data.ptr, bytes.data.size, true);
    if (!loaded.ok) {
        page->failed = true;
        return;
    }

    page->texture = loaded.texture;
    page->width = loaded.width;
    page->height = loaded.height;
    page->full_width = loaded.full_width;
    page->full_height = loaded.full_height;
    page->reduced = loaded.reduced;
    page->loaded = true;
    /* 1.25.0 (VM, 2026-10-20: a wide page read ahead stayed paired with the one before): a page whose shape
       is new to the layout — read for the first time, or not as it was taken — lays the pages out again */
    if (app->layout_opts.mode != RUBRAVIEW_PAGE_LAYOUT_SINGLE &&
        (page->seen_w != picture_w(page) || page->seen_h != picture_h(page))) app->needs_relayout = true;

    /* D-98: a picture that moves is no page of a book: found, the layouts for pictures leave it out. */
    if (page_moves(app, index, &bytes) && !page->animated) {
        page->animated = true;
        if (app->layout_opts.mode != RUBRAVIEW_PAGE_LAYOUT_SINGLE) app->needs_relayout = true;
    }
}
