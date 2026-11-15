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

enum { CW_BTN_APPLY = 0, CW_BTN_OVERWRITE, CW_BTN_BOTH, CW_BTN_SKIP, CW_BTN_SAME, CW_BTN_RECYCLE, CW_BTNS };
static rubraview_pal_rect_t cw_btn[CW_BTNS];
static rubraview_pal_rect_t cw_box[256][2];
static size_t cw_box_row[256];
static size_t cw_nbox;

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

void clashui_close(clashui_t *ui) {
    if (!ui->open) return;
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
    rubraview_window_config_t config = { .title = T("Rubraview - names already there"), .width = 980, .height = 620 };
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
    double dpi = rubraview_pal_window_dpi_scale(ui->window), pad = 12.0 * dpi, row_h = 54.0 * dpi, box = 20.0 * dpi;
    rubraview_pal_render_begin(r, CW_BACK);
    char head[200];
    int n = rubraview_fmt(head, sizeof head, T("{} names already there — the other files go on being copied or moved meanwhile"), PROVEN_ARG(ui->nrows));
    if (n > 0) rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)head, .size = (size_t)n },
                                              (rubraview_pal_rect_t){ pad, pad * 0.5, (double)w - 2 * pad, 26.0 * dpi }, 15.0 * dpi, CW_TEXT, RUBRAVIEW_TEXT_LEFT);
    double col0 = pad + 10.0 * dpi, col1 = pad + 64.0 * dpi, text_x = pad + 104.0 * dpi, top = 38.0 * dpi;
    rubraview_pal_render_draw_text(r, TU8("coming"), (rubraview_pal_rect_t){ col0 + box * 0.5 - 26.0 * dpi, top, 52.0 * dpi, 18.0 * dpi }, 11.0 * dpi, CW_DIM, RUBRAVIEW_TEXT_CENTER);
    rubraview_pal_render_draw_text(r, TU8("there"), (rubraview_pal_rect_t){ col1 + box * 0.5 - 26.0 * dpi, top, 52.0 * dpi, 18.0 * dpi }, 11.0 * dpi, CW_DIM, RUBRAVIEW_TEXT_CENTER);
    double y = top + 22.0 * dpi, foot = (double)h - 96.0 * dpi;
    int32_t rows = (int32_t)((foot - y) / row_h);
    if (rows < 1) rows = 1;
    if (ui->scroll > (int32_t)ui->nrows - rows) ui->scroll = (int32_t)ui->nrows - rows;
    if (ui->scroll < 0) ui->scroll = 0;
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
        proven_u8str_view_t name = rubraview_path_basename(cstr(row->target));
        rubraview_pal_render_draw_text(r, name, (rubraview_pal_rect_t){ text_x, ry + 4.0 * dpi, (double)w - text_x - pad, 22.0 * dpi }, 14.0 * dpi, CW_TEXT, RUBRAVIEW_TEXT_LEFT);
        char ss[32], ts[32], sw[64], tw[64], line[1400];
        (void)rubraview_picker_size_text(ss, sizeof ss, row->ssize);
        (void)rubraview_picker_size_text(ts, sizeof ts, row->tsize);
        when_text(sw, sizeof sw, row->smtime); when_text(tw, sizeof tw, row->tmtime);
        int m = rubraview_fmt(line, sizeof line, T("coming: {}  {}  {}      there: {}  {}{}"), PROVEN_ARG(ss), PROVEN_ARG(sw),
                              PROVEN_ARG(rubraview_path_dirname(cstr(row->source))), PROVEN_ARG(ts), PROVEN_ARG(tw),
                              PROVEN_ARG(row->kind_differs ? T("   (a file and a folder: skip or keep both)") : ""));
        if (m > 0) rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)m < sizeof line ? (size_t)m : sizeof line - 1 },
                                                  (rubraview_pal_rect_t){ text_x, ry + 26.0 * dpi, (double)w - text_x - pad, 20.0 * dpi }, 12.0 * dpi, CW_DIM, RUBRAVIEW_TEXT_LEFT);
    }
    /* the foot: what the boxes mean, the answers for all, the copy's same file */
    rubraview_pal_render_draw_text(r, TU8("Both ticked: both kept (the one coming as \"name (2)\"). Only the one coming: it replaces the one there (that one to the Recycle Bin). Otherwise: left as it is."),
                                   (rubraview_pal_rect_t){ pad, foot + 4.0 * dpi, (double)w - 2 * pad, 20.0 * dpi }, 12.0 * dpi, CW_DIM, RUBRAVIEW_TEXT_LEFT);
    const char *words[CW_BTNS] = { T("Apply the ticks"), T("Overwrite all"), T("Keep both, all"), T("Skip all"), T("Only the same ones"),
                                   ui->same_recycles_copy ? T("A copy's same file: its source to the Recycle Bin") : T("A copy's same file: its source left") };
    double bx = pad, by = (double)h - 50.0 * dpi, bh = 36.0 * dpi;
    for (int i = 0; i < CW_BTNS; ++i) {
        if (i == CW_BTN_RECYCLE && !ui->recycle_set) break;
        double tw = 0.0;
        (void)rubraview_pal_render_measure_text(r, cstr(words[i]), 13.0 * dpi, &tw);
        rubraview_pal_rect_t b = { bx, by, tw + 22.0 * dpi, bh };
        if (bx + b.width > (double)w - pad && i > 0) { b.x = bx = pad; b.y = by -= bh + 6.0 * dpi; }
        cw_btn[i] = b;
        bool over = pal_rect_has(b, ui->px, ui->py);
        rubraview_pal_render_fill_rect(r, b, i == 0 ? (over ? 0xFF4F9CF7u : CW_ON) : over ? 0x50FFFFFFu : 0xFF2A2F36u, 4.0 * dpi);
        rubraview_pal_render_stroke_rect(r, b, 0xFF4A525Cu, 1.0, 4.0 * dpi);
        rubraview_pal_render_draw_text(r, cstr(words[i]), b, 13.0 * dpi, CW_TEXT, RUBRAVIEW_TEXT_CENTER);
        bx += b.width + 8.0 * dpi;
    }
    if (!rubraview_pal_render_end(r)) ui->dirty = true;
}

static void press(clashui_t *ui, double x, double y) {
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
                if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT) press(ui, event.mouse.x, event.mouse.y);
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
