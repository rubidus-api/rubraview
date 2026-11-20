/*
 * RV-296 (owner, 2026-11-16: "이미지 보기 모드에서, 현재 보고있는 파일의 미리 지정된 폴더로의 파일 복사/이동을 ... 기본 단축키는
 * g ... 창 내부 카드박스가 뜨면서 미리 지정된 폴더 목록 ... 클릭해서 경로명을 편집하거나 찾아보기로 새로 지정 ... 화살표키와
 * Enter ... cc는 기본 폴더로 복사, mm은 기본 폴더로 이동 ... c1은 1번 폴더로 복사, m1은 1번 폴더로 이동"): the folders card.
 *
 * Over the page: the default folder (Settings › Files' "Copy to", `curation.copy_dir`) and folders 1 to 9
 * (`curation.dir_1` ...), one row each — its path, or "not set", and Browse. A click on a path, or Enter on the row
 * the arrows lit, edits it in place (Enter keeps it, Esc leaves it); Browse picks it with Windows' dialog. With
 * nothing being edited, C or M and then C / M (the default folder) or a digit (that folder) copy or move the file on
 * screen there through the file job (filing_put_to: a name already there asked about, a move taking the page out of
 * the list once it is done). Esc, or a press outside, closes it.
 */
#include "app.h"

enum { FC_ROWS = 10 };

static struct {
    bool open, editing;
    int  sel;                          /* the row lit: 0 the default folder, 1..9 */
    char prefix;                       /* 'C' or 'M' pressed, waiting for its second key; 0 none */
    rubraview_editctl_t ec;
    char buf[1024];
    char composing[64];
    editbox_view_t view;
    rubraview_pal_rect_t card, row[FC_ROWS], browse[FC_ROWS], close;
} F;

static proven_u8str_view_t row_dir(const app_state_t *app, int row) {
    return row == 0 ? app->curation.copy_dir : rubraview_curation_target(&app->curation, row);
}

bool foldercard_open(const app_state_t *app) {
    (void)app;
    return F.open;
}

uint64_t foldercard_signature(const app_state_t *app) {
    if (!F.open) return 0;
    uint64_t h = 1469598103934665603ull;
    uint64_t parts[] = { (uint64_t)F.sel, (uint64_t)F.editing, (uint64_t)(unsigned char)F.prefix, F.ec.te.len, F.ec.te.caret, F.ec.te.anchor,
                         (uint64_t)(int64_t)F.view.scroll, (uint64_t)(int64_t)app->pointer_x, (uint64_t)(int64_t)app->pointer_y };
    for (size_t i = 0; i < sizeof parts / sizeof *parts; ++i) h = (h ^ parts[i]) * 1099511628211ull;
    proven_u8str_view_t t = rubraview_textedit_text(&F.ec.te);
    for (size_t i = 0; i < t.size; ++i) h = (h ^ t.ptr[i]) * 1099511628211ull;
    for (size_t i = 0; F.composing[i]; ++i) h = (h ^ (unsigned char)F.composing[i]) * 1099511628211ull;
    for (int r = 0; r < FC_ROWS; ++r) h = (h ^ row_dir(app, r).size) * 1099511628211ull;
    return h;
}

static void edit_end(app_state_t *app) {
    if (!F.editing) return;
    F.editing = false;
    F.composing[0] = '\0';
    rubraview_editctl_free(&F.ec);
    rubraview_pal_window_text_input(app->window, false);
}

void foldercard_close(app_state_t *app) {
    edit_end(app);
    F.open = false;
    F.prefix = 0;
}

void foldercard_show(app_state_t *app) {
    if (current_page_index(app) < 0) { osd_say(app, TU8("open a file first")); return; }
    if (app->rename_active) rename_end(app);
    F.open = true;
    F.prefix = 0;
    F.editing = false;
    if (F.sel < 0 || F.sel >= FC_ROWS) F.sel = 0;
}

/* A row's folder kept: in the settings and in what the viewer reads them from. */
static void row_set(app_state_t *app, int row, proven_u8str_view_t path) {
    while (path.size > 3 && (path.ptr[path.size - 1] == '\\' || path.ptr[path.size - 1] == '/')) path.size--;   /* "C:\" keeps its own */
    proven_u8str_view_t kept = app_keep(app, path);
    if (path.size > 0 && kept.size == 0) { osd_say(app, TU8("could not keep that folder")); return; }
    char key[16];
    if (row == 0) rubraview_fmt(key, sizeof key, "copy_dir");
    else rubraview_fmt(key, sizeof key, "dir_{}", PROVEN_ARG(row));
    rubraview_settings_set_text(&app->settings, PROVEN_LIT("curation"), proven_u8str_view_from_cstr(key), kept);
    settings_write_if_changed(app, false);
    if (row == 0) app->curation.copy_dir = kept;
    else app->curation.dirs[row - 1] = kept;
}

static void edit_begin(app_state_t *app, int row) {
    edit_end(app);
    F.sel = row;
    F.editing = true;
    F.view = (editbox_view_t){0};
    editbox_begin(&F.ec, F.buf, sizeof F.buf, false);
    rubraview_editctl_set(&F.ec, row_dir(app, row));
    rubraview_textedit_select_all(&F.ec.te);
    rubraview_pal_window_text_input(app->window, true);
}

static void edit_keep(app_state_t *app) {
    proven_u8str_view_t t = rubraview_textedit_text(&F.ec.te);
    char typed[1024];
    size_t n = t.size < sizeof typed ? t.size : sizeof typed - 1;
    proven_sys_mem_copy(typed, t.ptr, n);
    int row = F.sel;
    edit_end(app);
    row_set(app, row, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)typed, .size = n });
}

static void folder_chosen(app_state_t *app, void *ctx, bool accepted, const proven_u8str_view_t *paths, size_t count, uint32_t filter) {
    (void)filter;
    if (accepted && count > 0 && F.open) row_set(app, (int)(intptr_t)ctx, paths[0]);   /* the card still open for it */
}

static void browse(app_state_t *app, int row) {
    edit_end(app);
    F.sel = row;
    choose_opts_t o = { .mode = RUBRAVIEW_CHOOSE_FOLDER, .title = row == 0 ? T("The default folder") : T("A folder for its number"),   /* RV-314 */
                        .start_dir = proven_u8str_view_from_cstr(browse_start_dir(app, app->picker_open)) };
    (void)choose_ask(app, &o, folder_chosen, (void *)(intptr_t)row);
}

/* The file on screen to row `row`'s folder; a folder not set yet is edited first. */
static void put(app_state_t *app, int row, bool moving) {
    proven_u8str_view_t dir = row_dir(app, row);
    if (dir.size == 0) {
        osd_say(app, row == 0 ? TU8("the default folder is not set: type it, or Browse") : TU8("that folder is not set: type it, or Browse"));
        edit_begin(app, row);
        return;
    }
    foldercard_close(app);
    filing_put_to(app, dir, moving);
}

/* RV-299: the navigator's Copy and Move: the default folder at once, as G C C / G M M; not set, the card asks for it. */
void foldercard_put_default(app_state_t *app, bool moving) {
    if (current_page_index(app) < 0) return;
    if (row_dir(app, 0).size == 0) { foldercard_show(app); put(app, 0, moving); return; }
    put(app, 0, moving);
}

proven_u8str_view_t foldercard_default_dir(const app_state_t *app) {
    return row_dir(app, 0);
}

bool foldercard_key(app_state_t *app, rubraview_key_combo_t combo) {
    if (!F.open) return false;
    if (F.editing) {
        if (key_is(combo, "Enter")) { edit_keep(app); return true; }
        if (key_is(combo, "Escape")) { edit_end(app); return true; }
        if (key_is(combo, "Up") || key_is(combo, "Down")) {   /* kept, and on to the row above or below */
            int to = F.sel + (key_is(combo, "Up") ? -1 : 1);
            edit_keep(app);
            F.sel = (to + FC_ROWS) % FC_ROWS;
            return true;
        }
        (void)editbox_key(app->window, &F.ec, combo, false);
        return true;   /* the field has the keys: nothing under it moves */
    }
    if (F.prefix) {   /* the second key: C / M the default folder, a digit that folder */
        char p = F.prefix;
        F.prefix = 0;
        if ((p == 'C' && key_is(combo, "C")) || (p == 'M' && key_is(combo, "M"))) { put(app, 0, p == 'M'); return true; }
        for (int d = 1; d <= 9; ++d) {
            char name[2] = { (char)('0' + d), 0 };
            if (key_is(combo, name)) { put(app, d, p == 'M'); return true; }
        }
        if (key_is(combo, "Escape")) return true;   /* the C or M taken back */
    }
    if (key_is(combo, "Escape") || key_is(combo, "G")) { foldercard_close(app); return true; }
    if (key_is(combo, "C")) { F.prefix = 'C'; return true; }
    if (key_is(combo, "M")) { F.prefix = 'M'; return true; }
    if (key_is(combo, "Up")) { F.sel = (F.sel + FC_ROWS - 1) % FC_ROWS; return true; }
    if (key_is(combo, "Down")) { F.sel = (F.sel + 1) % FC_ROWS; return true; }
    if (key_is(combo, "Enter")) { edit_begin(app, F.sel); return true; }
    for (int d = 0; d <= 9; ++d) {   /* a digit alone lights its row (0 the default folder) */
        char name[2] = { (char)('0' + d), 0 };
        if (key_is(combo, name)) { F.sel = d; return true; }
    }
    return true;   /* the card has the keys while it is open */
}

bool foldercard_text(app_state_t *app, const rubraview_window_event_t *event) {
    if (!F.open || !F.editing) return F.open;
    if (event->kind == RUBRAVIEW_WINDOW_EVENT_TEXT) {
        F.composing[0] = '\0';
        editbox_text(&F.ec, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)event->text.utf8, .size = event->text.length }, true);
    } else {
        size_t n = event->text.length < sizeof F.composing - 1 ? event->text.length : 0;
        proven_sys_mem_copy(F.composing, event->text.utf8, n);
        F.composing[n] = '\0';
    }
    (void)app;
    return true;
}

bool foldercard_press(app_state_t *app, double x, double y, bool shift) {
    if (!F.open) return false;
    if (F.editing && pal_rect_has(F.view.box, x, y)) { (void)editbox_pointer_down(app->renderer, &F.ec, &F.view, x, y, shift); return true; }
    if (pal_rect_has(F.close, x, y)) { foldercard_close(app); return true; }
    for (int r = 0; r < FC_ROWS; ++r) {
        if (pal_rect_has(F.browse[r], x, y)) { browse(app, r); return true; }
        if (pal_rect_has(F.row[r], x, y)) {
            if (F.editing && F.sel != r) edit_keep(app);
            edit_begin(app, r);
            (void)editbox_pointer_down(app->renderer, &F.ec, &F.view, x, y, shift);
            return true;
        }
    }
    if (!pal_rect_has(F.card, x, y)) { if (F.editing) edit_keep(app); foldercard_close(app); }   /* outside: closed, a typed path kept */
    return true;
}

bool foldercard_motion(app_state_t *app, double x) {
    if (!F.open || !F.editing || !F.view.dragging) return false;
    return editbox_pointer_move(app->renderer, &F.ec, &F.view, x);
}

void foldercard_release(app_state_t *app) {
    (void)app;
    F.view.dragging = false;
}

void draw_foldercard(app_state_t *app, double win_w, double win_h, double dpi) {
    if (!F.open) return;
    if (current_page_index(app) < 0) { foldercard_close(app); return; }
    rubraview_renderer_t *r = app->renderer;
    double pad = 14.0 * dpi, title_h = 26.0 * dpi, hint_h = 20.0 * dpi, row_h = 32.0 * dpi;
    double w = fmin(640.0 * dpi, win_w - 16.0 * dpi), h = pad + title_h + hint_h + 6.0 * dpi + row_h * FC_ROWS + pad;
    double scale = h > win_h - 16.0 * dpi ? (win_h - 16.0 * dpi) / h : 1.0;   /* a short window: rows closer, never off it */
    row_h *= scale;
    h = pad + title_h + hint_h + 6.0 * dpi + row_h * FC_ROWS + pad;
    F.card = (rubraview_pal_rect_t){ (win_w - w) * 0.5, fmax(8.0 * dpi, (win_h - h) * 0.5), w, h };
    rubraview_pal_render_fill_rect(r, F.card, 0xF6202326u, 6.0 * dpi);
    rubraview_pal_render_stroke_rect(r, F.card, COLOR_BOX_BORDER, 1.0, 6.0 * dpi);
    double x = F.card.x + pad, y = F.card.y + pad, inner = w - 2.0 * pad;
    proven_u8str_view_t name = rubraview_path_basename(current_file_path(app));
    char title[400];
    int tn = rubraview_fmt(title, sizeof title, T("Copy or move {} to a folder"), PROVEN_ARG(name));
    if (tn > 0) rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)title, .size = (size_t)tn },
                                               (rubraview_pal_rect_t){ x, y, inner - 30.0 * dpi, title_h }, 15.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    F.close = (rubraview_pal_rect_t){ F.card.x + w - pad - 26.0 * dpi, y, 26.0 * dpi, 26.0 * dpi };
    bool on_close = pal_rect_has(F.close, app->pointer_x, app->pointer_y);
    if (on_close) rubraview_pal_render_fill_rect(r, F.close, 0x40FFFFFFu, 4.0 * dpi);
    rubraview_pal_render_draw_text(r, PROVEN_LIT("\xC3\x97"), F.close, 16.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    y += title_h;
    const char *hint = F.prefix == 'C' ? T("Copy: C again for the default folder, or its number")
                     : F.prefix == 'M' ? T("Move: M again for the default folder, or its number")
                     : F.editing ? T("Enter keeps the folder  ·  Esc leaves it  ·  Up / Down: the next")
                     : T("CC / MM: copy / move to the default folder  ·  C1 / M1: to folder 1  ·  Enter or a click edits  ·  Esc");
    proven_u8str_view_t hv = cstr(hint);
    double hs = 12.0 * dpi, hw = 0.0;
    if (rubraview_pal_render_measure_text(r, hv, hs, &hw) && hw > inner) hs = fmax(9.0 * dpi, hs * inner / hw);
    rubraview_pal_render_draw_text(r, hv, (rubraview_pal_rect_t){ x, y, inner, hint_h }, hs, F.prefix ? 0xFFE8C46Fu : 0xC0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
    y += hint_h + 6.0 * dpi;
    double label_w = 64.0 * dpi, bw = 0.0;
    proven_u8str_view_t bword = TU8("Browse...");
    (void)rubraview_pal_render_measure_text(r, bword, 13.0 * dpi, &bw);
    bw += 20.0 * dpi;
    for (int row = 0; row < FC_ROWS; ++row) {
        rubraview_pal_rect_t line = { x, y, inner, row_h - 3.0 * dpi };
        bool lit = row == F.sel;
        if (lit) rubraview_pal_render_fill_rect(r, line, 0x302F7FE0u, 4.0 * dpi);
        char label[48];
        int ln = row == 0 ? rubraview_fmt(label, sizeof label, "{}", PROVEN_ARG(T("Default"))) : rubraview_fmt(label, sizeof label, "{}", PROVEN_ARG(row));
        if (ln > 0) rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)label, .size = (size_t)ln },
                                                   (rubraview_pal_rect_t){ x + 8.0 * dpi, y, label_w - 8.0 * dpi, line.height }, 13.0 * dpi,
                                                   lit ? COLOR_TEXT : 0xC0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
        F.browse[row] = (rubraview_pal_rect_t){ x + inner - bw, y + 2.0 * dpi, bw, line.height - 4.0 * dpi };
        bool on_b = pal_rect_has(F.browse[row], app->pointer_x, app->pointer_y);
        rubraview_pal_render_fill_rect(r, F.browse[row], on_b ? 0x50FFFFFFu : 0xFF2A2F36u, 3.0 * dpi);
        rubraview_pal_render_draw_text(r, bword, F.browse[row], 13.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        F.row[row] = (rubraview_pal_rect_t){ x + label_w, y + 2.0 * dpi, inner - label_w - bw - 8.0 * dpi, line.height - 4.0 * dpi };
        if (F.editing && lit) {
            F.view.box = F.row[row];
            F.view.size = 13.0 * dpi;
            rubraview_pal_render_fill_rect(r, F.row[row], 0xFF101215u, 3.0 * dpi);
            rubraview_pal_render_stroke_rect(r, F.row[row], 0xFF3E8BF0u, 1.0, 3.0 * dpi);
            editbox_draw(r, &F.ec, &F.view, cstr(F.composing), true, COLOR_TEXT, dpi);
        } else {
            proven_u8str_view_t dir = row_dir(app, row);
            bool over = pal_rect_has(F.row[row], app->pointer_x, app->pointer_y);
            if (over) rubraview_pal_render_stroke_rect(r, F.row[row], 0x60FFFFFFu, 1.0, 3.0 * dpi);
            rubraview_pal_render_draw_text(r, dir.size ? dir : TU8("not set"),
                                           (rubraview_pal_rect_t){ F.row[row].x + 6.0 * dpi, F.row[row].y, F.row[row].width - 12.0 * dpi, F.row[row].height },
                                           13.0 * dpi, dir.size ? COLOR_TEXT : 0x80F0F0F0u, RUBRAVIEW_TEXT_LEFT);
        }
        y += row_h;
    }
}
