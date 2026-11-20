/* RV-289: the names-already-there window (clashui.h), moved out of filejob.c as it was drawn (RV-258, D-310). */
#include "app.h"
#include "clashui.h"
#include "rubraview/fontspec.h"
#include "rubraview/picker.h"

#define CW_BACK   0xFF1B1E23u
#define CW_TEXT   0xFFE6E6E6u
#define CW_DIM    0xFF9AA2ACu
#define CW_LINE   0xFF30353Cu
#define CW_ON     0xFF3E8BF0u

enum { CW_BTN_APPLY = 0, CW_BTN_OVERWRITE, CW_BTN_BOTH, CW_BTN_SKIP, CW_BTN_SAME, CW_BTN_RECYCLE, CW_BTN_FOR_GOOD, CW_BTNS };
static rubraview_pal_rect_t cw_btn[CW_BTNS];
static rubraview_pal_rect_t cw_box[256][2];
static size_t cw_box_row[256];
static size_t cw_nbox;
static rubraview_pal_rect_t cw_head[2];   /* RV-313: the columns' "all" boxes */

static size_t ticked_in(const clashui_t *ui, int c) {
    size_t n = 0;
    for (size_t i = 0; i < ui->nrows; ++i) n += c == 0 ? ui->rows[i].keep_coming : ui->rows[i].keep_there;
    return n;
}

void clashui_rows_free(clashui_row_t *rows, size_t n) {
    for (size_t i = 0; i < n; ++i) { rubraview_free(rows[i].source); rubraview_free(rows[i].target); }
    rubraview_free(rows);
}

void clashui_set_rows(clashui_t *ui, clashui_row_t *rows, size_t n) {
    for (size_t k = 0; k < n; ++k)   /* the ticks the reader set are kept */
        for (size_t o = 0; o < ui->nrows; ++o)
            if (ui->rows[o].owner == rows[k].owner && ui->rows[o].index == rows[k].index) {
                rows[k].keep_coming = ui->rows[o].keep_coming;
                rows[k].keep_there = ui->rows[o].keep_there;
            }
    clashui_rows_free(ui->rows, ui->nrows);
    ui->rows = rows;
    ui->nrows = n;
    ui->dirty = true;
}

/* ---- RV-289.7: the thumbnails ---- */

static void clash_thumbs_wake(void *ctx) { (void)ctx; }

/* The cache's slot for a file as it is now (a new slot when its size or time changed). */
static size_t thumb_slot(clashui_t *ui, const char *path, uint64_t size, int64_t mtime) {
    for (size_t i = 0; i < ui->ncache; ++i)
        if (ui->cache[i].path && ui->cache[i].size == size && ui->cache[i].mtime == mtime && rubraview_cstr_cmp(ui->cache[i].path, path) == 0) return i;
    if (ui->ncache == ui->cap) {
        size_t cap = ui->cap ? ui->cap * 2 : 64;
        clashui_thumb_t *more = (clashui_thumb_t *)rubraview_realloc((proven_mem_mut_t){ .ptr = (proven_byte_t *)ui->cache, .size = cap * sizeof *more });
        if (!more) return SIZE_MAX;
        ui->cache = more;
        ui->cap = cap;
    }
    ui->cache[ui->ncache] = (clashui_thumb_t){ .path = rubraview_strdup(path), .size = size, .mtime = mtime };
    return ui->ncache++;
}

/* Textures belong to the window's renderer: gone with it; what was asked is asked again next time. */
static void thumbs_drop(clashui_t *ui) {
    if (ui->thumbs) { rubraview_pal_thumbs_stop(ui->thumbs); ui->thumbs = NULL; }
    for (size_t i = 0; i < ui->ncache; ++i) {
        if (ui->cache[i].texture) rubraview_pal_texture_destroy(ui->cache[i].texture);
        ui->cache[i].texture = NULL;
        if (ui->cache[i].state != 3) ui->cache[i].state = 0;
    }
}

/* A row's side's picture: asked unless it has been. */
static void thumb_ask(clashui_t *ui, const char *path, uint64_t size, int64_t mtime) {
    if (!ui->thumbs || !path) return;
    size_t k = thumb_slot(ui, path, size, mtime);
    if (k == SIZE_MAX || ui->cache[k].state != 0) return;
    ui->cache[k].state = rubraview_pal_thumbs_request(ui->thumbs, 1, k, false, 1.0, cstr(path)) ? 1 : 3;
}

static void row_ask(clashui_t *ui, size_t r) {
    if (r >= ui->nrows) return;
    thumb_ask(ui, ui->rows[r].target, ui->rows[r].tsize, ui->rows[r].tmtime);
    thumb_ask(ui, ui->rows[r].source, ui->rows[r].ssize, ui->rows[r].smtime);
}

void clashui_tick(clashui_t *ui) {
    if (!ui->open) return;
    if (ui->hold.part != RUBRAVIEW_SCROLLBAR_NONE) {   /* RV-289.6: the thumb dragged, or the track or a button held */
        int32_t first = (int32_t)lround(rubraview_scrollbar_held(&ui->sb, &ui->hold, ui->px, ui->py, rubraview_pal_time_now_seconds()));
        if (first != ui->scroll) { ui->scroll = first; ui->dirty = true; }
    }
    if (!ui->thumbs) return;
    rubraview_thumb_result_t done;
    while (rubraview_pal_thumbs_take(ui->thumbs, &done)) {
        if (done.index < ui->ncache) {
            clashui_thumb_t *t = &ui->cache[done.index];
            rubraview_texture_t *texture = NULL;
            if (done.bgra) {
                texture = rubraview_pal_texture_create_bgra(ui->renderer, done.width, done.height);
                if (texture && !rubraview_pal_texture_upload_bgra(texture, done.bgra, done.width * 4)) { rubraview_pal_texture_destroy(texture); texture = NULL; }
            }
            if (t->texture) rubraview_pal_texture_destroy(t->texture);
            t->texture = texture;
            t->state = texture ? 2 : 3;
            ui->dirty = true;
        }
        rubraview_free(done.bgra);
    }
    /* The newest asked is made first: the rows near the screen asked before those on it, so those come first. */
    int32_t first = ui->scroll, shown = ui->rows_shown > 0 ? ui->rows_shown : 1;
    for (int32_t r = first + 2 * shown - 1; r >= first + shown; --r) if (r >= 0) row_ask(ui, (size_t)r);
    for (int32_t r = first - 1; r >= first - shown; --r) if (r >= 0) row_ask(ui, (size_t)r);
    for (int32_t r = first + shown - 1; r >= first; --r) if (r >= 0) row_ask(ui, (size_t)r);
}

void clashui_close(clashui_t *ui) {
    if (!ui->open) return;
    thumbs_drop(ui);
    ui->hold.part = RUBRAVIEW_SCROLLBAR_NONE;
    rubraview_pal_window_get_frame(ui->window, &ui->frame[0], &ui->frame[1], &ui->frame[2], &ui->frame[3]);
    if (ui->renderer) rubraview_pal_render_destroy(ui->renderer);
    if (ui->window) rubraview_pal_window_destroy(ui->window);
    ui->renderer = NULL;
    ui->window = NULL;
    ui->open = false;
}

bool clashui_show(clashui_t *ui, proven_arena_t *arena) {
    if (ui->open) return true;
    if (ui->nrows == 0 || !arena) return false;
    /* RV-289: no owner — a job's window outlives the viewer that started it */
    rubraview_window_config_t config = { .title = T("Rubraview - names already there"), .width = 980, .height = 620,
                                         .no_activate = true };   /* RV-298 (owner, 2026-11-16: "포커스는 여전히 메인 창에 유지"): shown, not given the keys */
    ui->window = rubraview_pal_window_create(arena, &config);
    if (!ui->window) return false;
    if (ui->frame[2] > 0 && ui->frame[3] > 0) rubraview_pal_window_set_frame(ui->window, ui->frame[0], ui->frame[1], ui->frame[2], ui->frame[3]);
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(ui->window, &w, &h);
    ui->renderer = rubraview_pal_render_create(arena, rubraview_pal_window_native_handle(ui->window), w, h);
    if (!ui->renderer) { rubraview_pal_window_destroy(ui->window); ui->window = NULL; return false; }
    rubraview_font_spec_t face = rubraview_font_spec_parse(ui->ui_font);
    rubraview_pal_render_set_ui_font(ui->renderer, face.family, face.weight, face.italic);
    ui->open = true;
    ui->dirty = true;
    ui->scroll = 0;
    ui->thumbs = rubraview_pal_thumbs_start(PROVEN_LIT(IMAGE_FILTER), PROVEN_LIT(VIDEO_FILTER), PROVEN_LIT(ARCHIVE_FILTER), clash_thumbs_wake, NULL);
    if (ui->thumbs) rubraview_pal_thumbs_generation(ui->thumbs, 1);
    return true;
}

static void when_text(char *buf, size_t cap, int64_t t) {
    if (t <= 0) { buf[0] = '\0'; return; }
    info_local_time(buf, cap, t);
}

void clashui_draw(clashui_t *ui) {
    if (!ui->open || !ui->dirty) return;
    ui->dirty = false;
    rubraview_renderer_t *r = ui->renderer;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(ui->window, &w, &h);
    double dpi = rubraview_pal_window_dpi_scale(ui->window), pad = 12.0 * dpi, row_h = 78.0 * dpi, box = 20.0 * dpi;
    double pic = 64.0 * dpi, sbw = 14.0 * dpi;
    rubraview_pal_render_begin(r, CW_BACK);
    char head[200];
    int n = rubraview_fmt(head, sizeof head, T("{} names already there — the other files go on being copied or moved meanwhile"), PROVEN_ARG(ui->nrows));
    if (n > 0) rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)head, .size = (size_t)n },
                                              (rubraview_pal_rect_t){ pad, pad * 0.5, (double)w - 2 * pad, 26.0 * dpi }, 15.0 * dpi, CW_TEXT, RUBRAVIEW_TEXT_LEFT);
    double col0 = pad + 10.0 * dpi, col1 = pad + 64.0 * dpi, pics_x = pad + 100.0 * dpi, text_x = pics_x + 2.0 * pic + 20.0 * dpi, top = 38.0 * dpi;
    rubraview_pal_render_draw_text(r, TU8("coming"), (rubraview_pal_rect_t){ col0 + box * 0.5 - 26.0 * dpi, top, 52.0 * dpi, 18.0 * dpi }, 11.0 * dpi, CW_DIM, RUBRAVIEW_TEXT_CENTER);
    rubraview_pal_render_draw_text(r, TU8("there"), (rubraview_pal_rect_t){ col1 + box * 0.5 - 26.0 * dpi, top, 52.0 * dpi, 18.0 * dpi }, 11.0 * dpi, CW_DIM, RUBRAVIEW_TEXT_CENTER);
    for (int c = 0; c < 2; ++c) {   /* RV-313: a column's box for all of it, under its word, as a table's head */
        rubraview_pal_rect_t b = { c == 0 ? col0 : col1, top + 20.0 * dpi, box, box };
        rubraview_fileop_ticks_t st = rubraview_fileop_ticks_of(ticked_in(ui, c), ui->nrows);
        bool over = pal_rect_has(b, ui->px, ui->py);
        rubraview_pal_render_fill_rect(r, b, st == RUBRAVIEW_FILEOP_TICKS_ALL ? CW_ON : over ? 0xFF3A414Bu : 0xFF2A2F36u, 3.0 * dpi);
        rubraview_pal_render_stroke_rect(r, b, over ? 0xFFB0BCCAu : 0xFF6A7685u, 1.0, 3.0 * dpi);
        if (st == RUBRAVIEW_FILEOP_TICKS_ALL) rubraview_pal_render_draw_text(r, TU8("\xE2\x9C\x93"), b, 14.0 * dpi, 0xFFFFFFFFu, RUBRAVIEW_TEXT_CENTER);
        else if (st == RUBRAVIEW_FILEOP_TICKS_SOME)
            rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ b.x + box * 0.25, b.y + box * 0.5 - 1.5 * dpi, box * 0.5, 3.0 * dpi }, CW_ON, 1.0 * dpi);
        cw_head[c] = b;
    }
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ pad * 0.5, top + 45.0 * dpi, (double)w - pad, 1.0 }, CW_LINE, 0.0);
    double y = top + 46.0 * dpi, foot = (double)h - 138.0 * dpi;   /* RV-313: the foot holds two rows of answers */
    int32_t rows = (int32_t)((foot - y) / row_h);
    if (rows < 1) rows = 1;
    if (ui->scroll > (int32_t)ui->nrows - rows) ui->scroll = (int32_t)ui->nrows - rows;
    if (ui->scroll < 0) ui->scroll = 0;
    ui->rows_shown = rows;
    /* RV-289.6: a bar at the right as soon as the rows are more than the window shows */
    ui->sb = (rubraview_scrollbar_t){ .area = { (double)w - pad * 0.5 - sbw, y, sbw, (double)rows * row_h }, .button = sbw, .total = (double)ui->nrows,
                                      .shown = (double)rows, .first = (double)ui->scroll, .step = 1.0, .min_thumb = 24.0 * dpi };
    bool bar = rubraview_scrollbar_needed(&ui->sb);
    double right = bar ? ui->sb.area.x - 6.0 * dpi : (double)w - pad;
    cw_nbox = 0;
    for (int32_t k = 0; k < rows && (size_t)(ui->scroll + k) < ui->nrows; ++k) {
        clashui_row_t *row = &ui->rows[ui->scroll + k];
        double ry = y + (double)k * row_h;
        rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ pad * 0.5, ry + row_h - 1.0, (double)w - pad, 1.0 }, CW_LINE, 0.0);
        for (int c = 0; c < 2; ++c) {
            rubraview_pal_rect_t b = { c == 0 ? col0 : col1, ry + (row_h - box) * 0.5, box, box };
            bool on = c == 0 ? row->keep_coming : row->keep_there;
            rubraview_pal_render_fill_rect(r, b, on ? CW_ON : 0xFF2A2F36u, 3.0 * dpi);
            rubraview_pal_render_stroke_rect(r, b, 0xFF6A7685u, 1.0, 3.0 * dpi);
            if (on) rubraview_pal_render_draw_text(r, TU8("\xE2\x9C\x93"), b, 14.0 * dpi, 0xFFFFFFFFu, RUBRAVIEW_TEXT_CENTER);
            if (cw_nbox < 256) { cw_box[cw_nbox][c] = b; }
        }
        if (cw_nbox < 256) cw_box_row[cw_nbox++] = (size_t)(ui->scroll + k);
        for (int c = 0; c < 2; ++c) {   /* RV-289.7: the one coming, then the one there */
            const char *path = c == 0 ? row->source : row->target;
            size_t k = SIZE_MAX;
            for (size_t i = 0; path && i < ui->ncache && k == SIZE_MAX; ++i)
                if (ui->cache[i].size == (c == 0 ? row->ssize : row->tsize) && ui->cache[i].mtime == (c == 0 ? row->smtime : row->tmtime) &&
                    rubraview_cstr_cmp(ui->cache[i].path, path) == 0) k = i;
            rubraview_pal_rect_t frame = { pics_x + (double)c * (pic + 8.0 * dpi), ry + (row_h - pic) * 0.5, pic, pic };
            rubraview_pal_render_fill_rect(r, frame, 0xFF22262Cu, 3.0 * dpi);
            const clashui_thumb_t *t = k != SIZE_MAX ? &ui->cache[k] : NULL;
            if (t && t->texture) {
                int32_t tw = 0, th = 0;
                rubraview_pal_texture_size(t->texture, &tw, &th);
                double sc = tw > 0 && th > 0 ? fmin(pic / (double)tw, pic / (double)th) : 1.0;
                double dx = frame.x + (pic - (double)tw * sc) * 0.5, dy = frame.y + (pic - (double)th * sc) * 0.5;
                rubraview_pal_render_draw_texture(r, t->texture, rubraview_mat3x2_multiply(rubraview_mat3x2_scale(sc, sc), rubraview_mat3x2_translate(dx, dy)),
                                                  RUBRAVIEW_INTERP_LINEAR);
            } else {   /* not yet, or none: its kind in words */
                proven_u8str_view_t ext = rubraview_path_ext(cstr(path ? path : ""));
                rubraview_pal_render_draw_text(r, t && t->state == 3 ? ext : PROVEN_LIT("..."), frame, 11.0 * dpi, CW_DIM, RUBRAVIEW_TEXT_CENTER);
            }
        }
        proven_u8str_view_t name = rubraview_path_basename(cstr(row->target));
        rubraview_pal_render_draw_text(r, name, (rubraview_pal_rect_t){ text_x, ry + 12.0 * dpi, right - text_x, 22.0 * dpi }, 14.0 * dpi, CW_TEXT, RUBRAVIEW_TEXT_LEFT);
        char ss[32], ts[32], sw[64], tw[64], line[1400];
        (void)rubraview_picker_size_text(ss, sizeof ss, row->ssize);
        (void)rubraview_picker_size_text(ts, sizeof ts, row->tsize);
        when_text(sw, sizeof sw, row->smtime); when_text(tw, sizeof tw, row->tmtime);
        int m = rubraview_fmt(line, sizeof line, T("coming: {}  {}  {}      there: {}  {}{}"), PROVEN_ARG(ss), PROVEN_ARG(sw),
                              PROVEN_ARG(rubraview_path_dirname(cstr(row->source))), PROVEN_ARG(ts), PROVEN_ARG(tw),
                              PROVEN_ARG(row->kind_differs ? T("   (a file and a folder: skip or keep both)") : ""));
        if (m > 0) rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)m < sizeof line ? (size_t)m : sizeof line - 1 },
                                                  (rubraview_pal_rect_t){ text_x, ry + 38.0 * dpi, right - text_x, 20.0 * dpi }, 12.0 * dpi, CW_DIM, RUBRAVIEW_TEXT_LEFT);
    }
    if (bar) {   /* drawn as the viewer's lists draw theirs (scrollbars.c) */
        rubraview_scrollbar_part_t over = rubraview_scrollbar_hit(&ui->sb, ui->px, ui->py);
        rubraview_rect_t tr = rubraview_scrollbar_track(&ui->sb), th = rubraview_scrollbar_thumb(&ui->sb);
        double radius = sbw * 0.5;
        rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ tr.x, tr.y, tr.width, tr.height }, 0x28FFFFFFu, radius);
        uint32_t thumb = ui->hold.part == RUBRAVIEW_SCROLLBAR_THUMB ? 0xF0B0B0B0u : over != RUBRAVIEW_SCROLLBAR_NONE ? 0xE0909090u : 0xB0808080u;
        rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ th.x + 2.0 * dpi, th.y + 1.0 * dpi, th.width - 4.0 * dpi, th.height - 2.0 * dpi }, thumb, radius - 2.0 * dpi);
        for (int i = 0; i < 2; ++i) {
            rubraview_scrollbar_part_t part = i == 0 ? RUBRAVIEW_SCROLLBAR_UP : RUBRAVIEW_SCROLLBAR_DOWN;
            rubraview_rect_t b = i == 0 ? rubraview_scrollbar_up(&ui->sb) : rubraview_scrollbar_down(&ui->sb);
            rubraview_pal_rect_t bx = { b.x, b.y + 1.0 * dpi, b.width, b.height - 1.0 * dpi };
            rubraview_pal_render_fill_rect(r, bx, ui->hold.part == part ? 0x70FFFFFFu : over == part ? 0x40FFFFFFu : 0x20FFFFFFu, 2.0 * dpi);
            rubraview_pal_render_draw_text(r, i == 0 ? PROVEN_LIT("\xE2\x96\xB2") : PROVEN_LIT("\xE2\x96\xBC"), bx, b.width * 0.62, 0xE0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
        }
    }
    /* the foot: what the boxes mean, the answers for all, the copy's same file */
    rubraview_pal_render_draw_text(r, ui->for_good ? TU8("Both ticked: both kept (the one coming as \"name (2)\"). Only the one coming: it replaces the one there (that one deleted for good). Otherwise: left as it is.")
                                                   : TU8("Both ticked: both kept (the one coming as \"name (2)\"). Only the one coming: it replaces the one there (that one to the Recycle Bin). Otherwise: left as it is."),
                                   (rubraview_pal_rect_t){ pad, foot + 4.0 * dpi, (double)w - 2 * pad, 20.0 * dpi }, 12.0 * dpi, CW_DIM, RUBRAVIEW_TEXT_LEFT);
    const char *words[CW_BTNS] = { T("Apply the ticks"), T("Overwrite all"), T("Keep both, all"), T("Skip all"), T("Only the same ones"),
                                   ui->same_recycles_copy ? T("A copy's same file: its source to the Recycle Bin") : T("A copy's same file: its source left"),
                                   ui->for_good ? T("What is removed: deleted for good") : T("What is removed: to the Recycle Bin") };
    double bx = pad, by = foot + 32.0 * dpi, bh = 36.0 * dpi;
    for (int i = 0; i < CW_BTNS; ++i) {
        if ((i == CW_BTN_RECYCLE && !ui->recycle_set) || (i == CW_BTN_FOR_GOOD && !ui->for_good_set)) { cw_btn[i] = (rubraview_pal_rect_t){ 0 }; continue; }
        double tw = 0.0;
        (void)rubraview_pal_render_measure_text(r, cstr(words[i]), 13.0 * dpi, &tw);
        rubraview_pal_rect_t b = { bx, by, tw + 22.0 * dpi, bh };
        if (bx + b.width > (double)w - pad && bx > pad) { b.x = bx = pad; b.y = by += bh + 6.0 * dpi; }   /* the next row, below */
        cw_btn[i] = b;
        bool over = pal_rect_has(b, ui->px, ui->py);
        uint32_t fill = i == 0 ? (over ? 0xFF4F9CF7u : CW_ON) : over ? 0x50FFFFFFu : 0xFF2A2F36u;
        if (i == CW_BTN_FOR_GOOD && ui->for_good) fill = over ? 0xFFD0504Au : 0xFFB03A35u;   /* RV-313: red while it deletes for good */
        rubraview_pal_render_fill_rect(r, b, fill, 4.0 * dpi);
        rubraview_pal_render_stroke_rect(r, b, 0xFF4A525Cu, 1.0, 4.0 * dpi);
        rubraview_pal_render_draw_text(r, cstr(words[i]), b, 13.0 * dpi, CW_TEXT, RUBRAVIEW_TEXT_CENTER);
        bx += b.width + 8.0 * dpi;
    }
    if (!rubraview_pal_render_end(r)) ui->dirty = true;
}

static void press(clashui_t *ui, double x, double y) {
    for (int c = 0; c < 2; ++c)
        if (ui->nrows > 0 && pal_rect_has(cw_head[c], x, y)) {   /* RV-313: the whole column ticked, or all unticked */
            bool on = rubraview_fileop_ticks_pressed(ticked_in(ui, c), ui->nrows);
            for (size_t i = 0; i < ui->nrows; ++i) { if (c == 0) ui->rows[i].keep_coming = on; else ui->rows[i].keep_there = on; }
            ui->dirty = true;
            return;
        }
    for (size_t k = 0; k < cw_nbox; ++k)
        for (int c = 0; c < 2; ++c)
            if (pal_rect_has(cw_box[k][c], x, y) && cw_box_row[k] < ui->nrows) {
                clashui_row_t *row = &ui->rows[cw_box_row[k]];
                if (c == 0) row->keep_coming = !row->keep_coming; else row->keep_there = !row->keep_there;
                ui->dirty = true;
                return;
            }
    int hit = -1;
    for (int i = 0; i < CW_BTNS; ++i) if (pal_rect_has(cw_btn[i], x, y)) hit = i;
    rubraview_fileop_answer_t all = RUBRAVIEW_FILEOP_ANSWER_SKIP;
    switch (hit) {
        case CW_BTN_APPLY:
            for (size_t i = 0; i < ui->nrows; ++i) {
                rubraview_fileop_answer_t a = rubraview_fileop_answer_of_checks(ui->rows[i].keep_coming, ui->rows[i].keep_there);
                if (ui->rows[i].kind_differs && a == RUBRAVIEW_FILEOP_ANSWER_OVERWRITE) a = RUBRAVIEW_FILEOP_ANSWER_SKIP;   /* never a file over a folder */
                if (ui->answer) ui->answer(ui->ctx, &ui->rows[i], a);
            }
            ui->dirty = true;
            return;
        case CW_BTN_OVERWRITE: all = RUBRAVIEW_FILEOP_ANSWER_OVERWRITE; break;
        case CW_BTN_BOTH:      all = RUBRAVIEW_FILEOP_ANSWER_KEEP_BOTH; break;
        case CW_BTN_SKIP:      all = RUBRAVIEW_FILEOP_ANSWER_SKIP; break;
        case CW_BTN_SAME:      all = RUBRAVIEW_FILEOP_ANSWER_SAME_ONLY; break;
        case CW_BTN_FOR_GOOD:
            ui->for_good = !ui->for_good;
            if (ui->for_good_set) ui->for_good_set(ui->ctx, ui->for_good);
            ui->dirty = true;
            return;
        case CW_BTN_RECYCLE:
            ui->same_recycles_copy = !ui->same_recycles_copy;
            if (ui->recycle_set) ui->recycle_set(ui->ctx, ui->same_recycles_copy);
            ui->dirty = true;
            return;
        default: return;
    }
    if (ui->answer_all) ui->answer_all(ui->ctx, all);
    ui->dirty = true;
}

/* Closed by its X or Esc: the questions left are the owner's to settle (RV-289: skipped), then it goes. */
static void closed(clashui_t *ui) {
    if (ui->nrows > 0 && ui->closed) ui->closed(ui->ctx);
    clashui_close(ui);
}

size_t clashui_pump(clashui_t *ui) {
    size_t handled = 0;
    rubraview_window_event_t event;
    while (ui->open && rubraview_pal_window_poll_event(ui->window, &event)) {
        handled++;
        switch (event.kind) {
            case RUBRAVIEW_WINDOW_EVENT_CLOSE: closed(ui); return handled;
            case RUBRAVIEW_WINDOW_EVENT_RESIZE:
                rubraview_pal_render_resize(ui->renderer, event.resize.width, event.resize.height);
                ui->dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_PAINT: ui->dirty = true; break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_WHEEL: ui->scroll += event.mouse.wheel_delta > 0 ? -2 : 2; ui->dirty = true; break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE: ui->px = event.mouse.x; ui->py = event.mouse.y; ui->dirty = true; break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN:
                if (event.mouse.button != RUBRAVIEW_MOUSE_LEFT) break;
                if (rubraview_scrollbar_hit(&ui->sb, event.mouse.x, event.mouse.y) != RUBRAVIEW_SCROLLBAR_NONE) {   /* RV-289.6 */
                    ui->scroll = (int32_t)lround(rubraview_scrollbar_press(&ui->sb, &ui->hold, event.mouse.x, event.mouse.y, rubraview_pal_time_now_seconds()));
                    ui->dirty = true;
                    break;
                }
                press(ui, event.mouse.x, event.mouse.y);
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_UP:
                if (ui->hold.part != RUBRAVIEW_SCROLLBAR_NONE) { ui->hold.part = RUBRAVIEW_SCROLLBAR_NONE; ui->dirty = true; }
                break;
            case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN:
                if (key_is(event.key.combo, "Escape")) { closed(ui); return handled; }
                if (key_is(event.key.combo, "Enter")) press(ui, cw_btn[CW_BTN_APPLY].x + 1.0, cw_btn[CW_BTN_APPLY].y + 1.0);
                if (key_is(event.key.combo, "PageDown")) { ui->scroll += 5; ui->dirty = true; }
                if (key_is(event.key.combo, "PageUp")) { ui->scroll -= 5; ui->dirty = true; }
                break;
            default: break;
        }
    }
    return handled;
}
