/*
 * The wheel's strip: its cells, where it is and how much of it shows, following the page, peeking, its step
 * and a click on it (RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* ---- The wheel's strip (owner, 2026-10-15) ---- */

#define STRIP_PEEK_SECONDS 2.5
#define STRIP_FADE_SECONDS 0.4

void strip_cells_free(app_state_t *app) {
    for (size_t i = 0; i < app->strip_cell_count; ++i) {
        if (app->strip_cells[i].texture) rubraview_pal_texture_destroy(app->strip_cells[i].texture);
    }
    free(app->strip_cells);
    app->strip_cells = NULL;
    app->strip_cell_count = 0;
}

/* A new book or folder: new cells, and what is still being made is for nobody. */
void strip_cells_reset(app_state_t *app) {
    strip_cells_free(app);
    app->strip_generation++;
    if (app->strip_thumbs) rubraview_pal_thumbs_generation(app->strip_thumbs, app->strip_generation);
    size_t n = page_count(app);
    app->strip_cells = n ? (struct picker_thumb*)calloc(n, sizeof(*app->strip_cells)) : NULL;
    app->strip_cell_count = app->strip_cells ? n : 0;
}

/* How much of the strip shows: 1 while it is up, fading after the wheel's time, 0 when gone. */
uint32_t strip_fade(uint32_t argb, double k) {   /* alpha times k, 0..1 */
    uint32_t alpha = (uint32_t)lround((double)((argb >> 24) & 0xFFu) * (k < 0.0 ? 0.0 : k > 1.0 ? 1.0 : k));
    return (alpha << 24) | (argb & 0x00FFFFFFu);
}

double strip_shown(const app_state_t *app) {
    if (app->filmstrip.visible) return 1.0;
    double left = app->strip_peek_until - rubraview_pal_time_now_seconds();
    if (left <= -STRIP_FADE_SECONDS) return 0.0;
    return left >= 0.0 ? 1.0 : 1.0 + left / STRIP_FADE_SECONDS;
}

rubraview_pal_rect_t strip_rect(const app_state_t *app) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    double strip_h = FILMSTRIP_THUMB * rubraview_pal_window_dpi_scale(app->window);
    return (rubraview_pal_rect_t){ 0.0, (double)h - strip_h, (double)w, strip_h };
}

/* The page on screen is the one the strip slides to; a strip not showing goes there at once. */
void strip_follow(app_state_t *app, size_t page) {
    app->strip_target = rubraview_filmstrip_centre_offset(&app->filmstrip, page);
    if (strip_shown(app) <= 0.0) app->filmstrip.scroll_offset = app->strip_target;
}

/* The wheel turned a page: the strip comes up, or stays up longer. */
void strip_peek(app_state_t *app) {
    if (page_count(app) < 2) return;
    /* 1.22.0 (owner, 2026-10-19: "마우스 휠로 이미지 파일 사이를 이동할 때 하단에 보이는 썸네일 ... 끄고 킬 수 있게") */
    if (rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("wheel_strip")) <= 0.5) return;
    app->strip_peek_until = rubraview_pal_time_now_seconds() + STRIP_PEEK_SECONDS;
}

/* Each pass: slide, keep it up under the pointer, take in thumbnails, ask for the ones in view. */
void strip_step(app_state_t *app, double dt) {
    app->strip_arrived = false;
    double shown = strip_shown(app);
    if (shown > 0.0 && !app->filmstrip.visible && app->pointer_inside &&
        pal_rect_has(strip_rect(app), app->pointer_x, app->pointer_y)) {
        double now = rubraview_pal_time_now_seconds();
        if (app->strip_peek_until < now + 1.0) app->strip_peek_until = now + 1.0;   /* held while pointed at */
    }
    app->filmstrip.scroll_offset = rubraview_filmstrip_glide(app->filmstrip.scroll_offset, app->strip_target, dt);
    bool tip = app->list_tip_page >= 0 && (size_t)app->list_tip_page < page_count(app);   /* 1.25.0: a list row's thumbnail */
    if (shown <= 0.0 && !tip) return;
    if (app->strip_cell_count != page_count(app)) strip_cells_reset(app);
    if (!app->strip_cells) return;
    if (!app->strip_thumbs) {
        app->strip_thumbs = rubraview_pal_thumbs_start(PROVEN_LIT(IMAGE_FILTER), PROVEN_LIT(VIDEO_FILTER), PROVEN_LIT(ARCHIVE_FILTER),
                                                       thumbs_wake, app->window);
        if (!app->strip_thumbs) return;
        rubraview_pal_thumbs_generation(app->strip_thumbs, app->strip_generation);
    }
    rubraview_thumb_result_t done;
    while (rubraview_pal_thumbs_take(app->strip_thumbs, &done)) {
        if (done.generation == app->strip_generation && done.index < app->strip_cell_count) {
            struct picker_thumb *t = &app->strip_cells[done.index];
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
            app->strip_arrived = true;
        }
        free(done.bgra);
    }
    /* The ones in view, nearest the page last so they are made first; a list row's, pointed at, last of all. */
    rubraview_virtual_range_t visible = shown > 0.0 ? rubraview_filmstrip_visible(&app->filmstrip) : (rubraview_virtual_range_t){0};
    int book_reads = 0;
    for (size_t k = visible.count + (tip ? 1 : 0); k-- > 0;) {
        size_t index = k == visible.count ? (size_t)app->list_tip_page : visible.first + k;
        if (index >= app->strip_cell_count) continue;
        struct picker_thumb *t = &app->strip_cells[index];
        if (t->state != 0) continue;
        proven_u8str_view_t path = app->source.pages[index].path;
        if (path.size > 0) {
            t->state = rubraview_pal_thumbs_request(app->strip_thumbs, app->strip_generation, index, false, 1.0, path) ? 3 : 2;
            continue;
        }
        /* A page inside a book: its bytes, three a pass, never one far inside a streamed block. */
        if (page_read_cost(app, index) > PAGE_JOB_MIN_COST) { t->state = 2; continue; }
        if (book_reads >= 3) continue;
        book_reads++;
        void *mem = NULL;
        rubraview_page_bytes_t pb = page_read_owned(&app->source, &app->source_lock, index, &mem);
        uint8_t *copy = pb.ok && pb.data.size > 0 ? (uint8_t*)malloc(pb.data.size) : NULL;
        if (copy) memcpy(copy, pb.data.ptr, pb.data.size);
        free(mem);
        t->state = copy && rubraview_pal_thumbs_request_bytes(app->strip_thumbs, app->strip_generation, index, 1.0, copy, pb.data.size) ? 3 : 2;
    }
}

/* A click on the strip: the page whose thumbnail it is. True when the strip took it. */
bool strip_press(app_state_t *app, double x, double y) {
    if (strip_shown(app) <= 0.0 || !pal_rect_has(strip_rect(app), x, y)) return false;
    size_t page = 0;
    if (rubraview_filmstrip_index_at(&app->filmstrip, x, &page) && page < page_count(app)) {
        go_to_spread(app, spread_for_chosen_page(app, (int32_t)page));
        if (!app->filmstrip.visible) app->strip_peek_until = rubraview_pal_time_now_seconds() + STRIP_PEEK_SECONDS;
    }
    return true;
}
