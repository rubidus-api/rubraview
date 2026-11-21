/*
 * RV-310 (an outside review, 2026-11-19) and RV-311: the font picker, taken out of settingswin.c as it was.
 */
#include "app.h"
#include "rubraview/keyboard.h"
#include "settingslook.h"

/* ---- RV-136: the font picker (owner, 2026-10-18) ----
   A window of its own over the settings, in the program's look: the
   machine's families by name, found by typing; right of each name a short
   sample — I, i, l, 1 and O, 0 side by side, and the language in use —
   drawn first in the program's face and then, a few rows a frame, in the
   family's own as they come into sight, so a thousand fonts never stall
   it. Under the list the weight, italic, a large preview, Default, OK and
   Cancel. What it keeps is "Family[, Weight][, Italic]" (fontspec.h). */

#define FONTPICK_BACKGROUND 0xFF181818u
#define FONTPICK_NEW_FACES  6     /* rows drawn in their own face a frame */

static proven_u8str_view_t fontpick_sample(void) {
    static char buf[96];
    /* "Abc" in a pack is that language's own short word (한글, かな漢字, ...). */
    int n = rubraview_fmt(buf, sizeof(buf), "Iil1 aoO0 {}", PROVEN_ARG(T("Abc")));
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)buf, .size = n > 0 ? (size_t)n : 0 };
}

enum { FONTPICK_WEIGHT, FONTPICK_ITALIC, FONTPICK_DEFAULT, FONTPICK_OK, FONTPICK_CANCEL, FONTPICK_PLACES };

static double fontpick_dpi(const app_state_t *app) { return rubraview_pal_window_dpi_scale(app->fontpick_window); }
static double fontpick_row_h(const app_state_t *app) { return 30.0 * fontpick_dpi(app); }
static double fontpick_top(const app_state_t *app) { return 46.0 * fontpick_dpi(app); }

/* Lay out the five controls (`Weight`, `Italic`, `Default`, `OK`, `Cancel`) in wrapping rows inside `cw`. */
static double fontpick_layout_buttons(const app_state_t *app, double cw, double dpi, double y_top, rubraview_pal_rect_t out[FONTPICK_PLACES]) {
    rubraview_renderer_t *r = app->fontpick_renderer;
    double font = 14.0 * dpi, pad = 12.0 * dpi, gap = 8.0 * dpi, bh = 30.0 * dpi;
    double room = cw - 2.0 * pad;
    if (room < 40.0 * dpi) room = 40.0 * dpi;
    char weight[64];
    rubraview_fmt(weight, sizeof(weight), "<  {}  >", PROVEN_ARG(rubraview_font_weight_name(app->fontpick_weight)));
    const char *words[FONTPICK_PLACES] = { weight, app->fontpick_italic ? T("[x] Italic") : T("[ ] Italic"), T("Default"), T("OK"), T("Cancel") };
    double w[FONTPICK_PLACES] = { 0 }, rx[FONTPICK_PLACES] = { 0 };
    uint32_t row[FONTPICK_PLACES] = { 0 };
    for (int i = 0; i < FONTPICK_PLACES; ++i) {
        double tw = 80.0 * dpi;
        if (r) (void)rubraview_pal_render_measure_text(r, cstr(words[i]), font, &tw);
        double min_w = i == FONTPICK_WEIGHT ? 140.0 * dpi : 72.0 * dpi;
        w[i] = tw + 20.0 * dpi;
        if (w[i] < min_w) w[i] = min_w;
        if (w[i] > room) w[i] = room;
    }
    size_t rows = rubraview_flow_inline(w, FONTPICK_PLACES, gap, room, row, rx);
    for (int i = 0; i < FONTPICK_PLACES; ++i)
        out[i] = (rubraview_pal_rect_t){ pad + rx[i], y_top + (double)row[i] * (bh + 6.0 * dpi), w[i], bh };
    return rows ? (double)rows * bh + (double)(rows - 1) * 6.0 * dpi : 0.0;
}

typedef struct fontpick_geom {
    double d, rh, cw, sy, top, list_y, list_h, prev_h, btns_h, bottom_h, content_h;
    size_t rows;
    rubraview_pal_rect_t btns[FONTPICK_PLACES];
} fontpick_geom_t;

static fontpick_geom_t fontpick_measure_geom(const app_state_t *app) {
    fontpick_geom_t g = { 0 };
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->fontpick_window, &w, &h);
    g.d = fontpick_dpi(app);
    g.rh = fontpick_row_h(app);
    g.cw = dlgpage_width(&app->fontpick_page, (double)w, g.d);
    g.sy = app->fontpick_page.scroll;
    g.top = fontpick_top(app);
    g.prev_h = 56.0 * g.d;
    rubraview_pal_rect_t rel_btns[FONTPICK_PLACES];
    g.btns_h = fontpick_layout_buttons(app, g.cw, g.d, 0.0, rel_btns);
    g.bottom_h = 8.0 * g.d + g.prev_h + 8.0 * g.d + g.btns_h + 10.0 * g.d;
    double min_list = g.rh * 3.0;
    g.list_h = (double)h - g.top - g.bottom_h;
    if (g.list_h < min_list) g.list_h = min_list;
    g.rows = (size_t)(g.list_h / g.rh);
    if (g.rows < 1) g.rows = 1;
    g.list_y = g.top - g.sy;
    double by = g.top + g.list_h - g.sy;
    double btn_y0 = by + 8.0 * g.d + g.prev_h + 8.0 * g.d;
    for (int i = 0; i < FONTPICK_PLACES; ++i) {
        g.btns[i] = rel_btns[i];
        g.btns[i].y += btn_y0;
    }
    g.content_h = g.top + g.list_h + g.bottom_h;
    return g;
}

static size_t fontpick_rows(const app_state_t *app) {
    return fontpick_measure_geom(app).rows;
}

/* Does `name` hold the words typed, letters of any case alike? */
static bool fontpick_finds(proven_u8str_view_t name, proven_u8str_view_t find_view) {
    const char *find = (const char *)find_view.ptr;
    size_t len = (size_t)find_view.size;
    if (len == 0) return true;
    for (size_t i = 0; i + len <= name.size; ++i) {
        if (proven_u8str_view_eq_ascii_ci(proven_u8str_view_slice(name, i, len), proven_u8str_view_from_bytes(find, len))) return true;
    }
    return false;
}

static void fontpick_filter(app_state_t *app) {
    app->fontpick_list_count = 0;
    for (size_t i = 0; i < app->fontpick_all_count; ++i) {
        const rubraview_font_family_t *f = &app->fontpick_all[i];
        if (fontpick_finds(f->shown, rubraview_bytes_view(app->fontpick_find, app->fontpick_find_len)) || fontpick_finds(f->name, rubraview_bytes_view(app->fontpick_find, app->fontpick_find_len)))
            app->fontpick_list[app->fontpick_list_count++] = (uint32_t)i;
    }
    if (app->fontpick_sel >= (int32_t)app->fontpick_list_count) app->fontpick_sel = app->fontpick_list_count ? 0 : -1;
    if (app->fontpick_list_count && app->fontpick_sel < 0) app->fontpick_sel = 0;
    app->fontpick_first = 0;
    if (app->fontpick_find_len > 0) {   /* the first family whose name begins with what was typed is the one Enter keeps */
        app->fontpick_sel = app->fontpick_list_count ? 0 : -1;
        for (size_t i = 0; i < app->fontpick_list_count; ++i) {
            const rubraview_font_family_t *f = &app->fontpick_all[app->fontpick_list[i]];
            proven_u8str_view_t a = { f->shown.ptr, f->shown.size < app->fontpick_find_len ? f->shown.size : app->fontpick_find_len };
            proven_u8str_view_t b = { f->name.ptr, f->name.size < app->fontpick_find_len ? f->name.size : app->fontpick_find_len };
            if ((a.size == app->fontpick_find_len && fontpick_finds(a, rubraview_bytes_view(app->fontpick_find, app->fontpick_find_len))) ||
                (b.size == app->fontpick_find_len && fontpick_finds(b, rubraview_bytes_view(app->fontpick_find, app->fontpick_find_len)))) {
                app->fontpick_sel = (int32_t)i;
                break;
            }
        }
        fontpick_reveal(app);
    }
    app->fontpick_dirty = true;
}

void fontpick_reveal(app_state_t *app) {
    size_t rows = fontpick_rows(app);
    if (app->fontpick_sel < 0) return;
    size_t at = (size_t)app->fontpick_sel;
    if (at < app->fontpick_first) app->fontpick_first = at;
    if (at >= app->fontpick_first + rows) app->fontpick_first = at + 1 - rows;
}

void fontpick_close(app_state_t *app) {
    if (!app->fontpick_open) return;
    if (app->fontpick_renderer) rubraview_pal_render_destroy(app->fontpick_renderer);
    if (app->fontpick_window) rubraview_pal_window_destroy(app->fontpick_window);
    window_block_give(app, &app->fontpick_block);
    app->fontpick_renderer = NULL;
    app->fontpick_window = NULL;
    rubraview_free(app->fontpick_mem);
    app->fontpick_mem = NULL;
    app->fontpick_all = NULL;
    app->fontpick_list = NULL;
    app->fontpick_drawn = NULL;
    app->fontpick_open = false;
    app->settings_dirty = true;
}

void fontpick_show(app_state_t *app, proven_u8str_view_t section, proven_u8str_view_t key) {
    if (app->fontpick_open) fontpick_close(app);
    size_t cap = 4u << 20;
    app->fontpick_mem = rubraview_alloc(cap);
    if (!app->fontpick_mem) return;
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)app->fontpick_mem, .size = cap });
    app->fontpick_all_count = rubraview_pal_font_families(&arena, &app->fontpick_all);
    proven_result_mem_mut_t list = proven_arena_alloc(&arena, (app->fontpick_all_count + 1) * sizeof(uint32_t));
    proven_result_mem_mut_t drawn = proven_arena_alloc(&arena, app->fontpick_all_count + 1);
    if (app->fontpick_all_count == 0 || !proven_is_ok(list.err) || !proven_is_ok(drawn.err)) {
        rubraview_free(app->fontpick_mem);
        app->fontpick_mem = NULL;
        settings_say(app, T("no fonts could be listed"));
        return;
    }
    app->fontpick_list = (uint32_t*)(void*)list.value.ptr;
    app->fontpick_drawn = (uint8_t*)drawn.value.ptr;
    proven_sys_mem_set(app->fontpick_drawn, 0, app->fontpick_all_count + 1);
    rubraview_window_config_t config = { .title = T("Rubraview font"), .width = 680, .height = 600, .owner = app->settings_window };
    app->fontpick_window = rubraview_pal_window_create(window_block_take(app, &app->fontpick_block), &config);
    int32_t w = 0, h = 0;
    if (app->fontpick_window) rubraview_pal_window_get_size(app->fontpick_window, &w, &h);
    app->fontpick_renderer = app->fontpick_window
        ? rubraview_pal_render_create(window_block_arena(app, &app->fontpick_block), rubraview_pal_window_native_handle(app->fontpick_window), w, h) : NULL;
    if (!app->fontpick_renderer) {
        if (app->fontpick_window) rubraview_pal_window_destroy(app->fontpick_window);
        window_block_give(app, &app->fontpick_block);
        app->fontpick_window = NULL;
        rubraview_free(app->fontpick_mem);
        app->fontpick_mem = NULL;
        settings_say(app, T("the font picker could not open"));
        return;
    }
    app->fontpick_open = true;
    ui_font_apply_all(app);
    rubraview_pal_window_text_input(app->fontpick_window, true);   /* typing finds a family */
    app->fontpick_section = section;
    app->fontpick_key = key;
    rubraview_font_spec_t now = rubraview_font_spec_parse(rubraview_settings_get_text(&app->settings, section, key));
    app->fontpick_weight = now.weight;
    app->fontpick_italic = now.italic;
    app->fontpick_find_len = 0;
    app->fontpick_hover = -1;
    app->fontpick_sel = -1;
    app->fontpick_page = (dlgpage_t){0};
    fontpick_filter(app);
    for (size_t i = 0; i < app->fontpick_list_count; ++i) {   /* the font in use, chosen and in sight */
        const rubraview_font_family_t *f = &app->fontpick_all[app->fontpick_list[i]];
        if (now.family.size > 0 && (proven_u8str_view_eq(f->name, now.family) || proven_u8str_view_eq(f->shown, now.family))) {
            app->fontpick_sel = (int32_t)i;
            break;
        }
    }
    size_t rows = fontpick_rows(app);
    if (app->fontpick_sel > 0) app->fontpick_first = (size_t)app->fontpick_sel > rows / 2 ? (size_t)app->fontpick_sel - rows / 2 : 0;
    fontpick_reveal(app);
    app->fontpick_dirty = true;
}

/* What the picker keeps: the family chosen with its weight and italic; empty for the program's face. */
static void fontpick_keep(app_state_t *app, bool program_face) {
    char buf[200];
    proven_u8str_view_t value = PROVEN_LIT("");
    if (!program_face && app->fontpick_sel >= 0 && (size_t)app->fontpick_sel < app->fontpick_list_count) {
        const rubraview_font_family_t *f = &app->fontpick_all[app->fontpick_list[app->fontpick_sel]];
        value = rubraview_font_spec_format(buf, sizeof(buf), f->name, app->fontpick_weight, app->fontpick_italic);
    }
    rubraview_settings_set_text(&app->settings, app->fontpick_section, app->fontpick_key, app_keep(app, value));
    fontpick_close(app);
    settings_took_effect(app);
    settings_say(app, value.size ? T("font chosen") : T("the program's own font"));
}

/* The bottom's places: the weight (its left half lighter, right half heavier), italic, Default, OK, Cancel. */
static void fontpick_link(rubraview_renderer_t *r, rubraview_pal_rect_t box, proven_u8str_view_t text, double size, bool lit) {
    if (box.width <= 0.0 || box.height <= 0.0) return;
    if (lit) rubraview_pal_render_fill_rect(r, box, 0xFF2A2F38u, 3.0);
    rubraview_pal_render_stroke_rect(r, box, COLOR_BOX_BORDER, 1.0, 3.0);
    char cut[96];
    proven_u8str_view_t shown = box.width > 8.0 ? fit_text_measured(r, cut, sizeof(cut), text, box.width - 8.0, size) : PROVEN_LIT("");
    if (shown.size == 0) return;
    rubraview_pal_render_draw_text(r, shown, box, size, SETTINGS_LINK, RUBRAVIEW_TEXT_CENTER);
    double tw = 0.0;
    if (rubraview_pal_render_measure_text(r, shown, size, &tw) && tw + 6.0 <= box.width) {
        rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ box.x + (box.width - tw) * 0.5, box.y + box.height * 0.5 + size * 0.62,
                                                                  tw, size > 16.0 ? size / 16.0 : 1.0 }, SETTINGS_LINK, 0.0);
    }
}

void draw_fontpick_window(app_state_t *app) {
    if (!app->fontpick_open || !app->fontpick_dirty) return;
    app->fontpick_dirty = false;
    rubraview_renderer_t *r = app->fontpick_renderer;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->fontpick_window, &w, &h);
    fontpick_geom_t g = fontpick_measure_geom(app);
    double d = g.d, rh = g.rh, font = 14.0 * d;
    for (int i = 0; i < FONTPICK_PLACES; ++i) app->fontpick_btn[i] = g.btns[i];
    rubraview_pal_render_begin(r, FONTPICK_BACKGROUND);

    /* The words that find a family, typed anywhere in the window. */
    rubraview_pal_rect_t find = { 12.0 * d, 8.0 * d - g.sy, g.cw - 24.0 * d, 30.0 * d };
    if (find.width > 8.0 * d) {
        rubraview_pal_render_fill_rect(r, find, 0xFF101010u, 3.0);
        rubraview_pal_render_stroke_rect(r, find, 0xFF5B9BD5u, 1.0, 3.0);
        char line[256], fit[256];
        int n = app->fontpick_find_len
            ? rubraview_fmt(line, sizeof(line), "{}|   {} / {}", PROVEN_ARG(rubraview_bytes_view(app->fontpick_find, (int)app->fontpick_find_len)), PROVEN_ARG((size_t)app->fontpick_list_count), PROVEN_ARG((size_t)app->fontpick_all_count))
            : rubraview_fmt(line, sizeof(line), T("type to find a font   {}"), PROVEN_ARG((size_t)app->fontpick_all_count));
        rubraview_pal_rect_t inner = { find.x + 8.0 * d, find.y, find.width - 16.0 * d, find.height };
        if (n > 0 && inner.width > 4.0)
            rubraview_pal_render_draw_text(r, fit_text_measured(r, fit, sizeof(fit), (proven_u8str_view_t){ (const proven_byte_t *)line, (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1 }, inner.width, font),
                                           inner, font, app->fontpick_find_len ? COLOR_TEXT : 0xFF909090u, RUBRAVIEW_TEXT_LEFT);
    }

    /* The families: the name, then the sample — in its own face once drawn so. */
    size_t fresh = 0;
    bool waiting = false;
    proven_u8str_view_t sample = fontpick_sample();
    bool need_list_sb = app->fontpick_list_count > g.rows;
    double list_w = g.cw - 8.0 * d - (need_list_sb ? 16.0 * d : 0.0);
    bool show_sample = list_w >= 260.0 * d;
    double name_w = show_sample ? list_w * 0.46 : list_w;
    double bottom_top = g.top + g.list_h - g.sy;
    for (size_t k = 0; k < g.rows && app->fontpick_first + k < app->fontpick_list_count; ++k) {
        size_t at = app->fontpick_first + k;
        uint32_t fi = app->fontpick_list[at];
        const rubraview_font_family_t *f = &app->fontpick_all[fi];
        rubraview_pal_rect_t row = { 4.0 * d, g.list_y + rh * (double)k, list_w, rh };
        if (row.y + rh <= g.list_y || row.y >= bottom_top) continue;
        if ((int32_t)at == app->fontpick_sel) rubraview_pal_render_fill_rect(r, row, 0xFF2B4A6Fu, 2.0);
        else if ((int32_t)at == app->fontpick_hover) rubraview_pal_render_fill_rect(r, row, COLOR_TILE_FILL, 2.0);
        char fit[300];
        if (name_w > 20.0 * d)
            rubraview_pal_render_draw_text(r, fit_text_measured(r, fit, sizeof(fit), f->shown, name_w - 20.0 * d, font),
                                           (rubraview_pal_rect_t){ row.x + 10.0 * d, row.y, name_w - 14.0 * d, rh },
                                           font, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
        if (!show_sample) continue;
        rubraview_pal_rect_t right = { row.x + name_w, row.y, list_w - name_w - 6.0 * d, rh };
        if (!app->fontpick_drawn[fi] && fresh < FONTPICK_NEW_FACES) { app->fontpick_drawn[fi] = 1; fresh++; }
        if (app->fontpick_drawn[fi]) {
            rubraview_pal_render_set_text_font(r, f->name, app->fontpick_weight, app->fontpick_italic);
            proven_u8str_view_t shown_sample = fit_text_measured(r, fit, sizeof(fit), sample, right.width, font * 1.15);
            if (shown_sample.size > 0) rubraview_pal_render_draw_text(r, shown_sample, right, font * 1.15, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
            rubraview_pal_render_set_text_family(r, PROVEN_LIT(""));
        } else {
            proven_u8str_view_t shown_sample = fit_text_measured(r, fit, sizeof(fit), sample, right.width, font * 1.15);
            if (shown_sample.size > 0) rubraview_pal_render_draw_text(r, shown_sample, right, font * 1.15, 0xFF606060u, RUBRAVIEW_TEXT_LEFT);
            waiting = true;
        }
    }
    if (app->fontpick_list_count == 0) {
        rubraview_pal_render_draw_text(r, TU8("no font has those letters"), (rubraview_pal_rect_t){ 0.0, g.list_y, g.cw, rh * 2.0 },
                                       font, 0xFF909090u, RUBRAVIEW_TEXT_CENTER);
    }
    draw_scrollbar(app, r, RVSB_FONTPICK, app->fontpick_px, app->fontpick_py);   /* RV-170: where the rows are, and it moves them */

    /* The preview, the weight and italic, and the buttons (painted opaque below list). */
    double by = bottom_top;
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ 0.0, by, g.cw, g.bottom_h + (double)h }, 0xFF202020u, 0.0);
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ 0.0, by, g.cw, 1.0 }, COLOR_BOX_BORDER, 0.0);
    if (app->fontpick_sel >= 0 && (size_t)app->fontpick_sel < app->fontpick_list_count) {
        const rubraview_font_family_t *f = &app->fontpick_all[app->fontpick_list[app->fontpick_sel]];
        double pbox_w = g.cw - 28.0 * d;
        double pfont = 28.0 * d;
        if (pbox_w < 260.0 * d) pfont = fmax(13.0 * d, pbox_w / 10.0);
        char pfit[128];
        rubraview_pal_render_set_text_font(r, f->name, app->fontpick_weight, app->fontpick_italic);
        proven_u8str_view_t ptext = fit_text_measured(r, pfit, sizeof(pfit), sample, pbox_w, pfont);
        if (ptext.size > 0)
            rubraview_pal_render_draw_text(r, ptext, (rubraview_pal_rect_t){ 14.0 * d, by + 8.0 * d, pbox_w, g.prev_h },
                                           pfont, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
        rubraview_pal_render_set_text_family(r, PROVEN_LIT(""));
    }
    char weight[64];
    rubraview_fmt(weight, sizeof(weight), "<  {}  >", PROVEN_ARG(rubraview_font_weight_name(app->fontpick_weight)));
    const char *words[FONTPICK_PLACES] = { weight, app->fontpick_italic ? T("[x] Italic") : T("[ ] Italic"), T("Default"), T("OK"), T("Cancel") };
    for (int i = 0; i < FONTPICK_PLACES; ++i) {
        rubraview_pal_rect_t b = app->fontpick_btn[i];
        bool lit = app->fontpick_px >= b.x && app->fontpick_px < b.x + b.width && app->fontpick_py >= b.y && app->fontpick_py < b.y + b.height;
        fontpick_link(r, b, cstr(words[i]), font, lit);
    }
    if (dlgpage_end(&app->fontpick_page, r, (double)w, (double)h, g.content_h, app->fontpick_px, app->fontpick_py, d))
        app->fontpick_dirty = true;
    if (!rubraview_pal_render_end(r)) { fontpick_close(app); return; }
    if (waiting) app->fontpick_dirty = true;   /* the next frame draws a few more in their own face */
}

static int32_t fontpick_row_at(const app_state_t *app, double y) {
    fontpick_geom_t g = fontpick_measure_geom(app);
    double bottom_top = g.top + g.list_h - g.sy;
    if (y < g.list_y || y >= bottom_top) return -1;
    size_t k = (size_t)((y - g.list_y) / g.rh);
    if (k >= g.rows || app->fontpick_first + k >= app->fontpick_list_count) return -1;
    return (int32_t)(app->fontpick_first + k);
}

/* RV-170: the list's scroll bar, at the right edge of the font list. */
bool fontpick_sb(app_state_t *app, rubraview_scrollbar_t *sb, double bw, double dpi) {
    if (!app->fontpick_open) return false;
    fontpick_geom_t g = fontpick_measure_geom(app);
    double ah = g.rh * (double)g.rows;
    if (ah <= 2.0 * bw) return false;
    *sb = (rubraview_scrollbar_t){ .area = { g.cw - bw - 2.0 * dpi, g.list_y, bw, ah },
                                   .button = bw, .total = (double)app->fontpick_list_count, .shown = (double)g.rows,
                                   .first = (double)app->fontpick_first, .step = 1.0, .min_thumb = 16.0 * dpi };
    return true;
}

static void fontpick_scroll(app_state_t *app, long rows);
void fontpick_sb_to(app_state_t *app, size_t first) {
    fontpick_scroll(app, (long)first - (long)app->fontpick_first);
}

static void fontpick_scroll(app_state_t *app, long rows) {
    size_t shown = fontpick_rows(app), count = app->fontpick_list_count;
    long first = (long)app->fontpick_first + rows;
    long last = count > shown ? (long)(count - shown) : 0;
    app->fontpick_first = (size_t)(first < 0 ? 0 : first > last ? last : first);
    app->fontpick_dirty = true;
}

static void fontpick_step_weight(app_state_t *app, int dir) {
    int32_t at = 3;
    for (int32_t i = 0; i < RUBRAVIEW_FONT_WEIGHTS; ++i) if (rubraview_font_weight_at(i) == app->fontpick_weight) at = i;
    app->fontpick_weight = rubraview_font_weight_at(at + dir);
    proven_sys_mem_set(app->fontpick_drawn, 0, app->fontpick_all_count);   /* the samples again, in the new weight */
    app->fontpick_dirty = true;
}

size_t fontpick_pump(app_state_t *app) {
    size_t handled = 0;
    rubraview_window_event_t event;
    while (app->fontpick_open && rubraview_pal_window_poll_event(app->fontpick_window, &event)) {
        handled++;
        switch (event.kind) {
            case RUBRAVIEW_WINDOW_EVENT_CLOSE:
                fontpick_close(app);
                return handled;
            case RUBRAVIEW_WINDOW_EVENT_RESIZE:
                rubraview_pal_render_resize(app->fontpick_renderer, event.resize.width, event.resize.height);
                fontpick_reveal(app);
                app->fontpick_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_PAINT:
            case RUBRAVIEW_WINDOW_EVENT_DPI_CHANGED:
                app->fontpick_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_UP:
                if (dlgpage_release(&app->fontpick_page)) app->fontpick_dirty = true;
                if (scrollbar_release(app, app->fontpick_window)) app->fontpick_dirty = true;   /* RV-170 */
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE: {
                int32_t was = app->fontpick_hover;
                app->fontpick_px = event.mouse.x;
                app->fontpick_py = event.mouse.y;
                if (dlgpage_held(&app->fontpick_page, event.mouse.x, event.mouse.y)) { app->fontpick_dirty = true; break; }
                if (scrollbar_motion(app, app->fontpick_window, event.mouse.x, event.mouse.y)) { app->fontpick_dirty = true; break; }   /* RV-170 */
                app->fontpick_hover = fontpick_row_at(app, event.mouse.y);
                if (app->fontpick_hover != was || event.mouse.y > (double)fontpick_top(app)) app->fontpick_dirty = true;
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_WHEEL: {
                fontpick_geom_t g = fontpick_measure_geom(app);
                bool in_list = event.mouse.y >= g.list_y && event.mouse.y < g.top + g.list_h - g.sy;
                size_t prev_first = app->fontpick_first;
                if (!app->fontpick_page.bar || in_list) fontpick_scroll(app, event.mouse.wheel_delta > 0 ? -3 : 3);
                if (app->fontpick_page.bar && (!in_list || app->fontpick_first == prev_first)) {
                    (void)dlgpage_wheel(&app->fontpick_page, event.mouse.wheel_delta, g.d);
                    app->fontpick_dirty = true;
                }
                app->fontpick_hover = fontpick_row_at(app, event.mouse.y);
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN: {
                if (event.mouse.button != RUBRAVIEW_MOUSE_LEFT) break;
                if (dlgpage_press(&app->fontpick_page, event.mouse.x, event.mouse.y)) { app->fontpick_dirty = true; break; }
                if (scrollbar_press(app, RVSB_FONTPICK, event.mouse.x, event.mouse.y)) { app->fontpick_dirty = true; break; }   /* RV-170 */
                fontpick_geom_t g = fontpick_measure_geom(app);
                for (int i = 0; i < FONTPICK_PLACES; ++i) {
                    rubraview_pal_rect_t b = g.btns[i];
                    if (!(event.mouse.x >= b.x && event.mouse.x < b.x + b.width && event.mouse.y >= b.y && event.mouse.y < b.y + b.height)) continue;
                    if (i == FONTPICK_WEIGHT) fontpick_step_weight(app, event.mouse.x < b.x + b.width * 0.5 ? -1 : +1);
                    else if (i == FONTPICK_ITALIC) { app->fontpick_italic = !app->fontpick_italic; proven_sys_mem_set(app->fontpick_drawn, 0, app->fontpick_all_count); }
                    else if (i == FONTPICK_DEFAULT) { fontpick_keep(app, true); return handled; }
                    else if (i == FONTPICK_OK) { fontpick_keep(app, false); return handled; }
                    else { fontpick_close(app); return handled; }
                    app->fontpick_dirty = true;
                    break;
                }
                int32_t row = fontpick_row_at(app, event.mouse.y);
                if (row >= 0) {
                    static double last_click = -1.0;   /* a second click on the chosen row soon after keeps it */
                    double now = rubraview_pal_time_now_seconds();
                    if (row == app->fontpick_sel && last_click >= 0.0 && now - last_click < 0.45) { last_click = -1.0; fontpick_keep(app, false); return handled; }
                    last_click = now;
                    app->fontpick_sel = row;
                    app->fontpick_dirty = true;
                    break;
                }
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_TEXT: {
                /* Typing finds a family; a control character is a key's, not a letter. */
                for (size_t i = 0; i < event.text.length && app->fontpick_find_len + 1 < sizeof(app->fontpick_find); ++i) {
                    unsigned char c = (unsigned char)event.text.utf8[i];
                    if (c < 0x20 || c == 0x7F) continue;
                    app->fontpick_find[app->fontpick_find_len++] = (char)c;
                }
                fontpick_filter(app);
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN: {
                rubraview_key_combo_t combo = event.key.combo;
                bool ctrl = (combo.modifiers & RUBRAVIEW_MOD_CTRL) != 0;
                long rows = (long)fontpick_rows(app), step = 0;
                if (key_is(combo, "Escape")) { fontpick_close(app); return handled; }
                if (key_is(combo, "Enter")) { fontpick_keep(app, false); return handled; }
                if (key_is(combo, "Backspace")) {
                    while (app->fontpick_find_len > 0 && ((unsigned char)app->fontpick_find[app->fontpick_find_len - 1] & 0xC0) == 0x80) app->fontpick_find_len--;
                    if (app->fontpick_find_len > 0) app->fontpick_find_len--;
                    fontpick_filter(app);
                    break;
                }
                if (ctrl && key_is(combo, "B")) { fontpick_step_weight(app, app->fontpick_weight >= 600 ? -3 : +3); break; }
                if (ctrl && key_is(combo, "I")) { app->fontpick_italic = !app->fontpick_italic; proven_sys_mem_set(app->fontpick_drawn, 0, app->fontpick_all_count); app->fontpick_dirty = true; break; }
                if (key_is(combo, "Left")) { fontpick_step_weight(app, -1); break; }
                if (key_is(combo, "Right")) { fontpick_step_weight(app, +1); break; }
                if (key_is(combo, "Up")) step = -1;
                else if (key_is(combo, "Down")) step = 1;
                else if (key_is(combo, "PageUp")) step = -(rows > 1 ? rows - 1 : 1);
                else if (key_is(combo, "PageDown")) step = rows > 1 ? rows - 1 : 1;
                else if (key_is(combo, "Home")) step = -(long)app->fontpick_list_count;
                else if (key_is(combo, "End")) step = (long)app->fontpick_list_count;
                if (step != 0 && app->fontpick_list_count > 0) {
                    long to = (long)(app->fontpick_sel < 0 ? 0 : app->fontpick_sel) + step;
                    if (to < 0) to = 0;
                    if (to >= (long)app->fontpick_list_count) to = (long)app->fontpick_list_count - 1;
                    app->fontpick_sel = (int32_t)to;
                    fontpick_reveal(app);
                    app->fontpick_dirty = true;
                }
                break;
            }
            default:
                break;
        }
    }
    return handled;
}
