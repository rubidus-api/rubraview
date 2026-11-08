/* RV-256 (owner, 2026-11-08: "내부 로그 창도 있으면 좋겠어요. 동작의 로그들을 차근차근 저장하고, 필요하면 창으로 보여주고,
   버튼 누르면 텍스트 파일로 저장할 수 있고. 이건 따로 저장하지는 않고 매 창(실행 인스턴스)마다 그때그때 저장만 합니다. 로그
   크기 상한은 기본 1MB로 하되 필요하면 설정에서 조절할 수 있도록 하고, 이건 다른 메모리랑 별개로 힙에서 추가 할당받아서
   따로 관리하게 합시다."; asked, "Ctrl+Shift+L과 ☰ › 로그"): this window's log (rubraview_log, D-308) and the window that
   shows it — the information window's grid, newest at the foot and followed unless scrolled up; Save as text...,
   Copy all, Clear at its foot. Nothing is written unless Save is pressed. */
#include "app.h"

void app_log_start(app_state_t *app) {
    app->log_t0 = rubraview_pal_time_now_seconds();
    double mb = rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("log_limit_mb"));
    if (mb < 1.0) mb = 1.0;
    if (!rubraview_log_init(&app->log, (size_t)mb << 20)) (void)rubraview_log_init(&app->log, RUBRAVIEW_LOG_DEFAULT_BYTES);
    app->log_follow = true;
}

void app_log_limit(app_state_t *app) {
    double mb = rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("log_limit_mb"));
    if (mb < 1.0) mb = 1.0;
    if (app->log.ring && app->log.cap != ((size_t)mb << 20) && rubraview_log_resize(&app->log, (size_t)mb << 20)) app->log_built = (size_t)-1;
}

void app_log(app_state_t *app, const char *kind, proven_u8str_view_t text) {
    if (!app->log.ring) return;
    rubraview_log_add(&app->log, rubraview_pal_time_now_seconds() - app->log_t0, kind, text);
    if (app->log_open) app->log_dirty = true;
}

/* The rows made again from the log when it changed: one view a line into a copy of its text. */
static void log_build(app_state_t *app) {
    size_t mark = app->log.lines + app->log.dropped * 0x10000u + app->log.len;
    if (mark == app->log_built && app->log_text) return;
    app->log_built = mark;
    size_t n = rubraview_log_copy(&app->log, NULL, 0);
    char *text = (char *)rubraview_alloc(n + 1);
    proven_u8str_view_t *rows = (proven_u8str_view_t *)rubraview_alloc_zero(app->log.lines + 1, sizeof *rows);
    if (!text || !rows) { rubraview_free(text); rubraview_free(rows); return; }
    rubraview_log_copy(&app->log, text, n + 1);
    size_t count = 0, start = 0;
    for (size_t i = 0; i < n && count <= app->log.lines; ++i) {
        if (text[i] != '\n') continue;
        rows[count++] = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)text + start, .size = i - start };
        start = i + 1;
    }
    rubraview_free(app->log_text);
    rubraview_free(app->log_rows);
    app->log_text = text;
    app->log_rows = rows;
    app->log_row_count = count;
}

void log_close(app_state_t *app) {
    if (!app->log_open) return;
    rubraview_pal_window_get_frame(app->log_window, &app->log_frame[0], &app->log_frame[1], &app->log_frame[2], &app->log_frame[3]);
    if (app->log_renderer) rubraview_pal_render_destroy(app->log_renderer);
    if (app->log_window) rubraview_pal_window_destroy(app->log_window);
    window_block_give(app, &app->log_block);
    app->log_renderer = NULL;
    app->log_window = NULL;
    app->log_open = false;
    rubraview_free(app->log_text);
    rubraview_free(app->log_rows);
    app->log_text = NULL;
    app->log_rows = NULL;
    app->log_row_count = 0;
}

void log_show(app_state_t *app) {
    if (app->log_open) { log_close(app); return; }   /* Ctrl+Shift+L again puts it away */
    rubraview_window_config_t config = { .title = T("Rubraview - log"), .width = 900, .height = 600, .owner = app->window };
    app->log_window = rubraview_pal_window_create(window_block_take(app, &app->log_block), &config);
    if (!app->log_window) { osd_say(app, TU8("the log window could not be opened")); return; }
    if (app->log_frame[2] > 0 && app->log_frame[3] > 0)
        rubraview_pal_window_set_frame(app->log_window, app->log_frame[0], app->log_frame[1], app->log_frame[2], app->log_frame[3]);
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->log_window, &w, &h);
    app->log_renderer = rubraview_pal_render_create(window_block_arena(app, &app->log_block), rubraview_pal_window_native_handle(app->log_window), w, h);
    ui_font_apply_all(app);
    if (!app->log_renderer) {
        rubraview_pal_window_destroy(app->log_window); window_block_give(app, &app->log_block);
        app->log_window = NULL;
        osd_say(app, TU8("the log window could not be drawn"));
        return;
    }
    double dpi = rubraview_pal_window_dpi_scale(app->log_window);
    app->log_font = 14.0 * dpi;
    if (!rubraview_pal_render_mono_cell(app->log_renderer, app->log_font, &app->log_cell_w, &app->log_cell_h) ||
        app->log_cell_w <= 0.0 || app->log_cell_h <= 0.0) {
        app->log_cell_w = 7.8 * dpi;
        app->log_cell_h = 16.5 * dpi;
    }
    app->log_open = true;
    app->log_follow = true;
    app->log_message[0] = '\0';
    app->log_built = (size_t)-1;
    app->log_dirty = true;
}

static int32_t log_visible_rows(const app_state_t *app) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->log_window, &w, &h);
    int32_t rows = (int32_t)((double)h / app->log_cell_h) - 3;   /* the foot's buttons under them */
    return rows > 1 ? rows : 1;
}

static void log_scroll_by(app_state_t *app, int32_t lines) {
    int32_t last = (int32_t)app->log_row_count - log_visible_rows(app);
    if (last < 0) last = 0;
    app->log_scroll += lines;
    if (app->log_scroll > last) app->log_scroll = last;
    if (app->log_scroll < 0) app->log_scroll = 0;
    app->log_follow = app->log_scroll >= last;   /* at the foot: new lines are followed */
    app->log_dirty = true;
}

/* RV-264 (D-308 follow-up): the rows' scroll bar, at the window's right edge, as the information window's. */
bool log_sb(app_state_t *app, rubraview_scrollbar_t *sb, double bw, double dpi) {
    if (!app->log_open) return false;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->log_window, &w, &h);
    int32_t rows = log_visible_rows(app);
    double ch = app->log_cell_h;
    *sb = (rubraview_scrollbar_t){ .area = { (double)w - bw - 2.0 * dpi, ch * 0.5, bw, ch * (double)rows },
                                   .button = bw, .total = (double)app->log_row_count, .shown = (double)rows,
                                   .first = (double)app->log_scroll, .step = 1.0, .min_thumb = ch };
    return true;
}

void log_sb_to(app_state_t *app, size_t first) {
    app->log_follow = false;
    log_scroll_by(app, (int32_t)first - app->log_scroll);
}

#define LOG_BACKGROUND 0xFF16181Cu
#define LOG_TEXT       0xFFE0E0E0u
#define LOG_TIME       0xFF7FB2E5u
#define LOG_DIM        0xFF8A9099u

void draw_log_window(app_state_t *app) {
    if (!app->log_open || !app->log_dirty) return;
    app->log_dirty = false;
    log_build(app);
    rubraview_renderer_t *r = app->log_renderer;
    const double cw = app->log_cell_w, ch = app->log_cell_h;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->log_window, &w, &h);
    double dpi = rubraview_pal_window_dpi_scale(app->log_window);
    int32_t rows = log_visible_rows(app);
    if (app->log_follow) {
        int32_t last = (int32_t)app->log_row_count - rows;
        app->log_scroll = last > 0 ? last : 0;
    }
    rubraview_pal_render_begin(r, LOG_BACKGROUND);
    for (int32_t row = 0; row < rows; ++row) {
        size_t index = (size_t)(app->log_scroll + row);
        if (index >= app->log_row_count) break;
        proven_u8str_view_t line = app->log_rows[index];
        double y = ch * (0.5 + (double)row);
        int32_t c0 = 0, c1 = 0;   /* RV-264: what is marked */
        if (rubraview_grid_selection_row(&app->log_sel, (int32_t)index, &c0, &c1)) {
            int32_t last = (int32_t)((double)w / cw);
            if (c1 > last) c1 = last;
            if (c1 >= c0) rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ cw * (0.5 + c0), y, cw * (c1 - c0 + 1), ch }, 0x704A90D9u, 0.0);
        }
        size_t head = line.size < 11 ? line.size : 11;   /* the seconds, in their own colour */
        rubraview_pal_render_draw_text_mono(r, (proven_u8str_view_t){ .ptr = line.ptr, .size = head }, cw * 0.5, y, app->log_font, LOG_TIME);
        if (line.size > head)
            rubraview_pal_render_draw_text_mono(r, (proven_u8str_view_t){ .ptr = line.ptr + head, .size = line.size - head },
                                                cw * (0.5 + (double)head), y, app->log_font, LOG_TEXT);
    }
    /* the foot: three buttons and what was said */
    const char *words[3] = { T("Save as text... (Ctrl+S)"), T("Copy all (Ctrl+C)"), T("Clear") };
    double by = (double)h - ch * 2.2, bh = ch * 1.8, x = cw * 0.5;
    for (int i = 0; i < 3; ++i) {
        double tw = 0.0;
        (void)rubraview_pal_render_measure_text(r, cstr(words[i]), 13.0 * dpi, &tw);
        rubraview_pal_rect_t b = { x, by, tw + 24.0 * dpi, bh };
        app->log_btn[i] = b;
        bool over = pal_rect_has(b, app->log_px, app->log_py);
        rubraview_pal_render_fill_rect(r, b, over ? 0x50FFFFFFu : 0xFF2A2F36u, 4.0 * dpi);
        rubraview_pal_render_stroke_rect(r, b, 0xFF4A525Cu, 1.0, 4.0 * dpi);
        rubraview_pal_render_draw_text(r, cstr(words[i]), b, 13.0 * dpi, LOG_TEXT, RUBRAVIEW_TEXT_CENTER);
        x += b.width + 8.0 * dpi;
    }
    char foot[256];
    int n = rubraview_fmt(foot, sizeof foot, T("{} lines, {} KB of {} KB{}{}"), PROVEN_ARG(app->log.lines), PROVEN_ARG((app->log.len + 1023u) / 1024u),
                          PROVEN_ARG(app->log.cap / 1024u), PROVEN_ARG(app->log_message[0] ? "   |   " : ""), PROVEN_ARG(app->log_message));
    if (n > 0) rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ (const proven_byte_t *)foot, (size_t)n < sizeof foot ? (size_t)n : sizeof foot - 1 },
                                              (rubraview_pal_rect_t){ x + 8.0 * dpi, by, (double)w - x - 16.0 * dpi, bh }, 12.0 * dpi, LOG_DIM, RUBRAVIEW_TEXT_LEFT);
    draw_scrollbar(app, r, RVSB_LOG, app->log_px, app->log_py);   /* RV-264 */
    if (!rubraview_pal_render_end(r)) app->log_dirty = true;
}

static const rubraview_file_filter_t LOG_FILTERS[] = { { "Text (*.txt)", "*.txt" } };

static void log_save(app_state_t *app) {
    rubraview_file_dialog_opts_t opts = { .title = T("Save the log"), .filters = LOG_FILTERS, .filter_count = 1,
                                          .parent_window_handle = rubraview_pal_window_native_handle(app->log_window) };
    rubraview_file_dialog_result_t picked = rubraview_pal_file_dialog_save(app->arena, &opts);
    if (!picked.accepted || picked.count == 0) return;
    proven_u8str_view_t target = picked.paths[0];
    if (!rubraview_glob_match_list(rubraview_path_basename(target), PROVEN_LIT("*.txt;*.log"))) target = rubraview_path_with_ext(app->arena, target, ".txt");
    size_t n = rubraview_log_copy(&app->log, NULL, 0);
    char *text = (char *)rubraview_alloc(n + 1);
    if (!text) return;
    rubraview_log_copy(&app->log, text, n + 1);
    bool ok = rubraview_pal_fs_write_file(target, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)text, .size = n });
    rubraview_free(text);
    rubraview_fmt(app->log_message, sizeof app->log_message, "{}", PROVEN_ARG(ok ? T("saved") : T("could not be saved")));
    app_log(app, "log", ok ? target : TU8("could not be saved"));
    app->log_dirty = true;
}

/* RV-264: Ctrl+C copies what is marked, as the information window does; everything when nothing is. */
static void log_copy_all(app_state_t *app);
static void log_copy(app_state_t *app) {
    if (!app->log_sel.active) { log_copy_all(app); return; }
    log_build(app);
    char *text = (char *)rubraview_alloc(1u << 20);
    if (!text) return;
    size_t n = rubraview_grid_selection_copy(&app->log_sel, app->log_rows, (int32_t)app->log_row_count, text, 1u << 20);
    bool ok = n > 0 && rubraview_pal_clipboard_set_text(app->log_window, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)text, .size = n });
    rubraview_free(text);
    rubraview_fmt(app->log_message, sizeof app->log_message, "{}", PROVEN_ARG(ok ? T("copied") : T("nothing to copy")));
    app->log_dirty = true;
}

static void log_copy_all(app_state_t *app) {
    size_t n = rubraview_log_copy(&app->log, NULL, 0);
    char *text = (char *)rubraview_alloc(n + 1);
    if (!text) return;
    rubraview_log_copy(&app->log, text, n + 1);
    bool ok = n > 0 && rubraview_pal_clipboard_set_text(app->log_window, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)text, .size = n });
    rubraview_free(text);
    rubraview_fmt(app->log_message, sizeof app->log_message, "{}", PROVEN_ARG(ok ? T("copied everything") : T("nothing to copy")));
    app->log_dirty = true;
}

static void log_button(app_state_t *app, int which) {
    if (which == 0) log_save(app);
    else if (which == 1) log_copy_all(app);
    else if (which == 2) {
        rubraview_log_clear(&app->log);
        app->log_sel.active = false;   /* RV-264 */
        app->log_built = (size_t)-1;
        app->log_scroll = 0;
        app->log_follow = true;
        rubraview_fmt(app->log_message, sizeof app->log_message, "{}", PROVEN_ARG(T("cleared")));
        app->log_dirty = true;
    }
}

size_t log_pump(app_state_t *app) {
    size_t handled = 0;
    rubraview_window_event_t event;
    while (app->log_open && rubraview_pal_window_poll_event(app->log_window, &event)) {
        handled++;
        int32_t col = 0, row = 0;   /* RV-264: the cell under the pointer, for marking */
        if (event.kind == RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN || event.kind == RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE ||
            event.kind == RUBRAVIEW_WINDOW_EVENT_MOUSE_UP) {
            col = (int32_t)((event.mouse.x - app->log_cell_w * 0.5) / app->log_cell_w);
            row = app->log_scroll + (int32_t)(event.mouse.y / app->log_cell_h - 0.5);
            if (col < 0) col = 0;
            if (row < 0) row = 0;
            if (row >= (int32_t)app->log_row_count) row = app->log_row_count > 0 ? (int32_t)app->log_row_count - 1 : 0;
        }
        switch (event.kind) {
            case RUBRAVIEW_WINDOW_EVENT_CLOSE:
                log_close(app);
                return handled;
            case RUBRAVIEW_WINDOW_EVENT_RESIZE:
                rubraview_pal_render_resize(app->log_renderer, event.resize.width, event.resize.height);
                app->log_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_PAINT:
                app->log_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_WHEEL:
                log_scroll_by(app, event.mouse.wheel_delta > 0 ? -3 : 3);
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE:
                app->log_px = event.mouse.x;
                app->log_py = event.mouse.y;
                app->log_dirty = true;   /* the buttons light under the pointer */
                if (scrollbar_motion(app, app->log_window, event.mouse.x, event.mouse.y)) break;   /* RV-264 */
                (void)scrollbar_hover(app, RVSB_LOG, event.mouse.x, event.mouse.y);
                if (app->log_selecting) { app->log_sel.end_row = row; app->log_sel.end_col = col; }
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN: {
                if (event.mouse.button != RUBRAVIEW_MOUSE_LEFT) break;
                bool on_button = false;
                for (int i = 0; i < 3; ++i) if (pal_rect_has(app->log_btn[i], event.mouse.x, event.mouse.y)) { log_button(app, i); on_button = true; break; }
                if (!app->log_open) return handled;
                if (on_button) break;
                if (scrollbar_press(app, RVSB_LOG, event.mouse.x, event.mouse.y)) { app->log_dirty = true; break; }   /* RV-264 */
                if (event.mouse.y < app->log_btn[0].y) {   /* a drag over the rows marks them */
                    app->log_sel = (rubraview_grid_selection_t){ true, row, col, row, col };
                    app->log_selecting = true;
                    app->log_follow = false;
                    app->log_message[0] = '\0';
                    app->log_dirty = true;
                }
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_UP:
                if (scrollbar_release(app, app->log_window)) { app->log_dirty = true; break; }   /* RV-264 */
                if (!app->log_selecting) break;
                app->log_selecting = false;
                if (app->log_sel.anchor_row == app->log_sel.end_row && app->log_sel.anchor_col == app->log_sel.end_col) app->log_sel.active = false;
                app->log_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN: {
                rubraview_key_combo_t k = event.key.combo;
                bool ctrl = (k.modifiers & RUBRAVIEW_MOD_CTRL) != 0, shift = (k.modifiers & RUBRAVIEW_MOD_SHIFT) != 0;
                if (ctrl && key_is(k, "S")) { log_save(app); break; }
                if (ctrl && key_is(k, "C")) { log_copy(app); break; }   /* RV-264: what is marked, else all */
                if (ctrl && key_is(k, "A")) {
                    app->log_sel = (rubraview_grid_selection_t){ true, 0, 0, (int32_t)app->log_row_count, INT32_MAX / 2 };
                    app->log_dirty = true;
                    break;
                }
                if (key_is(k, "Escape") || (ctrl && shift && key_is(k, "L"))) { log_close(app); return handled; }
                if (key_is(k, "Down")) log_scroll_by(app, 1);
                else if (key_is(k, "Up")) log_scroll_by(app, -1);
                else if (key_is(k, "PageDown")) log_scroll_by(app, log_visible_rows(app) - 1);
                else if (key_is(k, "PageUp")) log_scroll_by(app, -(log_visible_rows(app) - 1));
                else if (key_is(k, "Home")) log_scroll_by(app, -1000000);
                else if (key_is(k, "End")) log_scroll_by(app, 1000000);
                break;
            }
            default:
                break;
        }
    }
    return handled;
}
