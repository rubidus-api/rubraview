/*
 * The pop-ups: the right-click list, the slide show's settings, hints, the rename and answer boxes, the list
 * window drawn, a place's font (RV-116; RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* ---- RV-116: the right-click list, and the slide show's settings ---- */

#define CTX_MAX RUBRAVIEW_CONTEXT_MAX_ITEMS

typedef struct ctx_row { proven_u8str_view_t caption, action; int32_t group; } ctx_row_t;

/* 1.21.0 (owner, 2026-10-18: "우클릭 메뉴에서, 툴바 버튼으로 호출 가능한 기능들은 빼 주세요"): is this action
   a tile of the toolbox for what is on screen? Then the right click leaves it out. Open file stays
   (owner, 2026-10-19: "파일 열기도 우클릭 컨텍스트 메뉴에 있어야 해요"). */
static bool ctx_on_toolbox(const app_state_t *app, proven_u8str_view_t action) {
    if (action.size == 0) return false;
    const rubraview_boxes_doc_t *doc = rubraview_boxes_document();
    const rubraview_toolbox_profile_t *p = rubraview_boxes_profile(doc, cstr(toolbox_profile_name(app)));
    if (!p) return false;
    for (int32_t i = 0; i < p->tile_count; ++i)
        if (proven_u8str_view_eq(doc->tiles[p->first_tile + i].action, action)) return true;
    return false;
}

/* The rows on show: the list's, less (on the picture) what the toolbox has a tile for. 1.23.0: the
   menu's sections are gone from it (owner, 2026-10-19: all of them have another way in). */
static size_t ctx_row_list(const app_state_t *app, ctx_row_t *rows) {
    if (!app->ctx_list) return 0;
    size_t n = 0;
    const rubraview_boxes_doc_t *doc = rubraview_boxes_document();
    for (int32_t i = 0; i < app->ctx_list->tile_count && n < CTX_MAX; ++i) {
        const rubraview_box_tile_t *item = &doc->context_items[app->ctx_list->first_tile + i];
        if (app->ctx_on_picture && ctx_on_toolbox(app, item->action)) continue;
        rows[n++] = (ctx_row_t){ item->caption, item->action, item->group };
    }
    return n;
}

static rubraview_window_t *ctx_window(const app_state_t *app) {
    return app->ctx_in_files && app->files_window ? app->files_window : app->window;   /* RV-117: or over the file window's rows */
}

static rubraview_ctxmenu_metrics_t ctx_metrics(const app_state_t *app) {
    double dpi = rubraview_pal_window_dpi_scale(ctx_window(app));
    double k = place_size((app_state_t *)app, PF_MENU);   /* 1.25.0: the menus' size */
    return (rubraview_ctxmenu_metrics_t){ .row_height = 30.0 * dpi * k, .gap = 9.0 * dpi, .pad = 6.0 * dpi, .width = 250.0 * dpi * k };
}

/* The open list's rows: their groups, and whether each can act now. */
static size_t ctx_rows(const app_state_t *app, int32_t *groups, bool *enabled) {
    ctx_row_t rows[CTX_MAX];
    size_t n = ctx_row_list(app, rows);
    rubraview_action_facts_t facts = action_facts(app);
    for (size_t i = 0; i < n; ++i) {
        groups[i] = rows[i].group;
        if (enabled) enabled[i] = rubraview_action_state(rows[i].action, &facts).enabled;
    }
    return n;
}

/* The list's one column, turned to stay in the window. */
typedef struct ctx_layout {
    size_t n;
    int32_t groups[CTX_MAX];
    bool enabled[CTX_MAX];
    rubraview_rect_t a;
    rubraview_ctxmenu_metrics_t m;
} ctx_layout_t;

static void ctx_layout_make(const app_state_t *app, ctx_layout_t *L) {
    L->n = ctx_rows(app, L->groups, L->enabled);
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(ctx_window(app), &w, &h);
    L->m = ctx_metrics(app);
    L->a = rubraview_ctxmenu_frame((double)w, (double)h, app->ctx_x, app->ctx_y, &L->m, L->groups, L->n);
}

static rubraview_rect_t slidepop_frame(const app_state_t *app) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    return rubraview_slidepop_frame((double)w, (double)h, app->slidepop_x, app->slidepop_y,
                                    rubraview_pal_window_dpi_scale(app->window));
}

void ctx_close(app_state_t *app) {
    if (app->ctx_open && app->ctx_in_files) app->files_dirty = true;
    app->ctx_open = false;
    app->ctx_list = NULL;
    app->ctx_focus = -1;
    app->ctx_on_picture = false;
    rubraview_confirm_clear(&app->ctx_confirm);
}

void slidepop_close(app_state_t *app) {
    app->slidepop_open = false;
}

/* A right click on the picture: while the slide show runs, its settings; otherwise
   the list for what is on screen (the toolbox's profiles name them). */
void ctx_open_at(app_state_t *app, double x, double y) {
    ctx_close(app);
    slidepop_close(app);
    app->ctx_in_files = false;
    app->sub_list_open = false;
    if (app->slideshow_running) {
        app->slidepop_open = true;
        app->slidepop_x = x;
        app->slidepop_y = y;
        return;
    }
    const rubraview_toolbox_profile_t *list = rubraview_boxes_context(rubraview_boxes_document(), cstr(toolbox_profile_name(app)));
    if (!list || list->tile_count <= 0) return;
    app->ctx_open = true;
    app->ctx_list = list;
    app->ctx_x = x;
    app->ctx_y = y;
    app->ctx_on_picture = true;
}

/* A row chosen, by a click or Enter. Delete asks a second time, as the menu's does. */
static void ctx_choose(app_state_t *app, int32_t row) {
    int32_t groups[CTX_MAX];
    bool enabled[CTX_MAX];
    size_t n = ctx_rows(app, groups, enabled);
    if (row < 0 || (size_t)row >= n || !enabled[row]) return;
    ctx_row_t rows[CTX_MAX];
    (void)ctx_row_list(app, rows);
    bool clears = proven_u8str_view_eq(rows[row].action, PROVEN_LIT("recent_clear"));
    if ((clears || proven_u8str_view_eq(rows[row].action, PROVEN_LIT("delete_file")) || proven_u8str_view_eq(rows[row].action, PROVEN_LIT("picker_recycle"))) &&
        !rubraview_confirm_press(&app->ctx_confirm, row, rubraview_pal_time_now_seconds())) {
        osd_say(app, clears ? TU8("click Clear again to empty the recent list") : TU8("click Delete again to move it to the recycle bin"));
        return;
    }
    proven_u8str_view_t action = rows[row].action;
    ctx_close(app);
    handle_action(app, action);
}

/* 1.25.0: in its place's face (Settings › General) */
static void draw_ctxmenu_on_in(app_state_t *app, rubraview_renderer_t *r, double px, double py);
void draw_ctxmenu_on(app_state_t *app, rubraview_renderer_t *r, double px, double py) {
    int before = place_font_begin(app, PF_MENU, NULL);
    draw_ctxmenu_on_in(app, r, px, py);
    place_font_end(app, before);
}

static void draw_ctxmenu_on_in(app_state_t *app, rubraview_renderer_t *r, double px, double py) {
    if (!app->ctx_open) return;
    ctx_layout_t L;
    ctx_layout_make(app, &L);
    size_t n = L.n;
    if (n == 0) return;
    const int32_t *groups = L.groups;
    const bool *enabled = L.enabled;
    rubraview_ctxmenu_metrics_t m = L.m;
    double dpi = rubraview_pal_window_dpi_scale(ctx_window(app));
    rubraview_rect_t f = L.a;
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ f.x, f.y, f.width, f.height }, 0xF6202020u, 4.0 * dpi);
    rubraview_pal_render_stroke_rect(r, (rubraview_pal_rect_t){ f.x, f.y, f.width, f.height }, COLOR_BOX_BORDER, 1.0 * dpi, 4.0 * dpi);
    ctx_row_t rows[CTX_MAX];
    (void)ctx_row_list(app, rows);
    int32_t hover = rubraview_ctxmenu_row_at(L.a, &m, groups, n, px, py);
    if (hover < 0) hover = app->ctx_focus;
    rubraview_action_facts_t facts = action_facts(app);
    double now = rubraview_pal_time_now_seconds();
    for (size_t i = 0; i < n; ++i) {
        rubraview_rect_t row = rubraview_ctxmenu_row(L.a, &m, groups, i);
        if (i > 0 && groups[i] != groups[i - 1]) {   /* the line between two groups */
            rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ f.x + 8.0 * dpi, row.y - m.gap * 0.5 - 0.5 * dpi,
                                                                      f.width - 16.0 * dpi, 1.0 * dpi }, 0x40FFFFFFu, 0.0);
        }
        rubraview_pal_rect_t cell = { row.x + 4.0 * dpi, row.y + 1.0 * dpi, row.width - 8.0 * dpi, row.height - 2.0 * dpi };
        if ((int32_t)i == hover && enabled[i]) rubraview_pal_render_fill_rect(r, cell, 0x40FFFFFFu, 3.0 * dpi);
        const ctx_row_t *item = &rows[i];
        rubraview_action_state_t st = rubraview_action_state(item->action, &facts);
        char scratch[160];
        proven_u8str_view_t text;
        if (rubraview_confirm_armed(&app->ctx_confirm, (int64_t)i, now)) {
            text = proven_u8str_view_eq(item->action, PROVEN_LIT("recent_clear")) ? TU8("Clear?") : TU8("Delete?");
        } else {
            text = rubraview_tile_caption(rubraview_tr_ctx_u8("menu", item->caption), false, st.mark, st.value, rubraview_mem_mut(scratch, sizeof(scratch)));
        }
        rubraview_pal_rect_t words = { cell.x + 10.0 * dpi, cell.y, cell.width - 14.0 * dpi, cell.height };
        rubraview_pal_render_draw_text(r, text, words, 14.0 * dpi * place_size(app, PF_MENU), enabled[i] ? COLOR_TEXT : 0x60F0F0F0u, RUBRAVIEW_TEXT_LEFT);
    }
}

void draw_ctxmenu(app_state_t *app) {
    if (!app->ctx_in_files) draw_ctxmenu_on(app, app->renderer, app->pointer_x, app->pointer_y);
}

/* RV-117: a file's own list, from a right click on its row in either list. */
void list_row_menu(app_state_t *app, size_t row, double x, double y, bool in_files) {
    ctx_close(app);
    slidepop_close(app);
    const rubraview_toolbox_profile_t *list = rubraview_boxes_context(rubraview_boxes_document(), PROVEN_LIT("listrow"));
    if (!list || list->tile_count <= 0) return;
    app->ctx_open = true;
    app->ctx_list = list;
    app->ctx_in_files = in_files;
    app->ctx_x = x;
    app->ctx_y = y;
    app->list_ctx_row = row;
    app->files_dirty = true;
}

/* What the field says: the delay, "3.5 s" (RV-267: a press on it asks the number in the input card). */
static proven_u8str_view_t slidepop_delay_text(const app_state_t *app, proven_mem_mut_t buffer_view) {
    char *buffer = (char *)buffer_view.ptr;
    size_t size = (size_t)buffer_view.size;
    int n = rubraview_fmt(buffer, size, "{:.1} s", PROVEN_ARG((double)app->slideshow.interval_seconds));
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)buffer, .size = n > 0 ? (size_t)n : 0 };
}

/* RV-267 (owner, 2026-11-10: "모든 어떤 입력창들이 공통 위젯을 쓰게"): the volume and the slide show's delay typed in
   the input card (D-323), as every other value: moved, sized, OK and Cancel, modeless. */
void number_card_begin(app_state_t *app, int32_t target) {
    char now[32];
    int n = target == NUMBER_VOLUME ? rubraview_fmt(now, sizeof now, "{}", PROVEN_ARG(volume_percent(app)))
                                    : rubraview_fmt(now, sizeof now, "{:.1}", PROVEN_ARG((double)app->slideshow.interval_seconds));
    if (target == NUMBER_VOLUME) volpop_close(app); else slidepop_close(app);
    if (app->rename_active) rename_end(app);
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    rubraview_textedit_set(&app->rename_edit, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)now, .size = n > 0 ? (size_t)n : 0 });
    rubraview_textedit_select_all(&app->rename_edit);
    app->rename_active = true;
    app->rename_is_number = true;
    app->number_target = target;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, true);
}

void number_typed(app_state_t *app) {
    rename_insert(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->rename_composing, .size = app->rename_composing_length });
    app->rename_composing_length = 0;
    proven_u8str_view_t typed = rubraview_textedit_text(&app->rename_edit);
    double v = 0.0;
    if (app->number_target == NUMBER_VOLUME) {
        if (!rubraview_volpop_parse(typed, &v)) { osd_say(app, TU8("the volume is a percent, 0 to 100")); return; }   /* the card stays, to put it right */
        rename_end(app);
        volume_set(app, v);
    } else {
        if (!rubraview_subwin_amount(typed, &v)) { osd_say(app, TU8("the delay is a number of seconds, 0.1 to 300")); return; }
        rename_end(app);
        interval_set(app, v);
    }
}

static int slidepop_transition(const app_state_t *app) {
    int turn = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("transition")));
    return turn >= 0 && turn < 4 ? turn : 1;
}

void draw_slidepop(app_state_t *app) {
    if (!app->slidepop_open) return;
    double s = rubraview_pal_window_dpi_scale(app->window);
    rubraview_rect_t f = slidepop_frame(app);
    rubraview_renderer_t *r = app->renderer;
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ f.x, f.y, f.width, f.height }, 0xF6202020u, 5.0 * s);
    rubraview_pal_render_stroke_rect(r, (rubraview_pal_rect_t){ f.x, f.y, f.width, f.height }, COLOR_TILE_CURRENT, 1.5 * s, 5.0 * s);
    rubraview_rect_t t = rubraview_slidepop_title(f, s);
    rubraview_pal_render_draw_text(r, app->slideshow.paused ? TU8("Slide show - paused") : TU8("Slide show"),
                                   (rubraview_pal_rect_t){ t.x, t.y, t.width, t.height }, 15.0 * s, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    const char *const LABELS[3] = { T("Delay"), T("Change"), T("At the end") };
    for (int l = 0; l < 3; ++l) {
        rubraview_rect_t lr = rubraview_slidepop_label(f, s, (rubraview_slidepop_label_t)l);
        rubraview_pal_render_draw_text(r, cstr(LABELS[l]), (rubraview_pal_rect_t){ lr.x, lr.y, lr.width, lr.height },
                                       13.0 * s, 0xB0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
    }
    int fx = slidepop_transition(app);
    bool full = rubraview_pal_window_is_fullscreen(app->window);
    for (int p = 0; p < RUBRAVIEW_SLIDEPOP_PART_COUNT; ++p) {
        rubraview_rect_t pr = rubraview_slidepop_part(f, s, (rubraview_slidepop_part_t)p);
        rubraview_pal_rect_t box = { pr.x, pr.y, pr.width, pr.height };
        bool current = false;
        char buffer[24];
        proven_u8str_view_t text = PROVEN_LIT("");
        switch ((rubraview_slidepop_part_t)p) {
            case RUBRAVIEW_SLIDEPOP_DELAY_DOWN:  text = PROVEN_LIT("-"); break;
            case RUBRAVIEW_SLIDEPOP_DELAY_UP:    text = PROVEN_LIT("+"); break;
            case RUBRAVIEW_SLIDEPOP_DELAY_FIELD: text = slidepop_delay_text(app, rubraview_mem_mut(buffer, sizeof(buffer))); break;
            case RUBRAVIEW_SLIDEPOP_FX_CUT:      text = TU8("Cut");   current = fx == 0; break;
            case RUBRAVIEW_SLIDEPOP_FX_FADE:     text = TU8("Fade");  current = fx == 1; break;
            case RUBRAVIEW_SLIDEPOP_FX_SLIDE:    text = TU8("Slide"); current = fx == 2; break;
            case RUBRAVIEW_SLIDEPOP_FX_ZOOM:     text = TU8("Zoom");  current = fx == 3; break;
            case RUBRAVIEW_SLIDEPOP_LOOP:        text = TU8("Go round");    current = app->slideshow.loop == RUBRAVIEW_LOOP_ALL; break;
            case RUBRAVIEW_SLIDEPOP_ONCE:        text = TU8("Stop on last"); current = app->slideshow.loop == RUBRAVIEW_LOOP_PLAY_ONCE; break;
            case RUBRAVIEW_SLIDEPOP_PAUSE:       text = app->slideshow.paused ? TU8("Play") : TU8("Pause"); break;
            case RUBRAVIEW_SLIDEPOP_STOP:        text = TU8("Stop"); break;
            case RUBRAVIEW_SLIDEPOP_FULL:        text = full ? TU8("Window") : TU8("Full screen"); break;
            case RUBRAVIEW_SLIDEPOP_CLOSE:       text = TU8("Close"); break;
            default: break;
        }
        bool hover = app->pointer_x >= pr.x && app->pointer_x < pr.x + pr.width && app->pointer_y >= pr.y && app->pointer_y < pr.y + pr.height;
        bool field = p == RUBRAVIEW_SLIDEPOP_DELAY_FIELD;
        rubraview_pal_render_fill_rect(r, box, field ? 0xFF101010u : current ? COLOR_TILE_CURRENT : hover ? 0x50FFFFFFu : 0x28FFFFFFu, 3.0 * s);
        rubraview_pal_render_stroke_rect(r, box, field && current ? COLOR_TILE_CURRENT : COLOR_BOX_BORDER, 1.0 * s, 3.0 * s);
        rubraview_pal_render_draw_text(r, text, box, (p <= RUBRAVIEW_SLIDEPOP_DELAY_UP && !field ? 18.0 : 13.0) * s, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
}

static void slidepop_press(app_state_t *app, double x, double y) {
    double s = rubraview_pal_window_dpi_scale(app->window);
    rubraview_rect_t f = slidepop_frame(app);
    rubraview_slidepop_part_t part = rubraview_slidepop_part_at(f, s, x, y);
    if (part == RUBRAVIEW_SLIDEPOP_NONE) {
        if (!rubraview_rect_contains(f, x, y)) slidepop_close(app);   /* a click away closes it */
        return;
    }
    switch (part) {
        case RUBRAVIEW_SLIDEPOP_DELAY_DOWN: interval_set(app, rubraview_slidepop_step(app->slideshow.interval_seconds, -1)); break;
        case RUBRAVIEW_SLIDEPOP_DELAY_UP:   interval_set(app, rubraview_slidepop_step(app->slideshow.interval_seconds, +1)); break;
        case RUBRAVIEW_SLIDEPOP_DELAY_FIELD: number_card_begin(app, NUMBER_DELAY); break;   /* RV-267: in the input card */
        case RUBRAVIEW_SLIDEPOP_FX_CUT: case RUBRAVIEW_SLIDEPOP_FX_FADE:
        case RUBRAVIEW_SLIDEPOP_FX_SLIDE: case RUBRAVIEW_SLIDEPOP_FX_ZOOM: {
            const char *const SAY[4] = { T("slides change at once"), T("slides fade into the next"),
                                                T("slides slide in"), T("slides zoom and fade") };
            int fx = (int)(part - RUBRAVIEW_SLIDEPOP_FX_CUT);
            rubraview_settings_set(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("transition"), (double)fx);   /* Settings › Viewer › Turning a page */
            settings_took_effect(app);
            osd_say(app, cstr(SAY[fx]));
            break;
        }
        case RUBRAVIEW_SLIDEPOP_LOOP: app->slideshow.loop = RUBRAVIEW_LOOP_ALL; break;
        case RUBRAVIEW_SLIDEPOP_ONCE: app->slideshow.loop = RUBRAVIEW_LOOP_PLAY_ONCE; break;
        case RUBRAVIEW_SLIDEPOP_PAUSE:
            if (app->slideshow.paused) rubraview_slideshow_resume(&app->slideshow);
            else rubraview_slideshow_pause(&app->slideshow);
            break;
        case RUBRAVIEW_SLIDEPOP_STOP:
            slidepop_close(app);
            if (app->slideshow_running) toggle_slideshow(app);
            break;
        case RUBRAVIEW_SLIDEPOP_FULL:  handle_action(app, PROVEN_LIT("toggle_fullscreen")); break;
        case RUBRAVIEW_SLIDEPOP_CLOSE: slidepop_close(app); break;
        default: break;
    }
}

/* A press while either is open: true when it was theirs (a click away closes and is spent). */
bool popups_press(app_state_t *app, double x, double y, bool right) {
    if (app->slidepop_open) {
        if (right) { slidepop_close(app); return false; }   /* a right click elsewhere opens it there again */
        slidepop_press(app, x, y);
        return true;
    }
    if (!app->ctx_open) return false;
    ctx_layout_t L;
    ctx_layout_make(app, &L);
    if (right) { ctx_close(app); return false; }
    if (rubraview_rect_contains(L.a, x, y)) {
        int32_t row = rubraview_ctxmenu_row_at(L.a, &L.m, L.groups, L.n, x, y);
        if (row >= 0) ctx_choose(app, row);
        return true;
    }
    ctx_close(app);
    return true;
}

/* Keys while either is open: the arrows and Enter in the list, typing in the delay, Esc. */
bool popups_key(app_state_t *app, rubraview_key_combo_t combo) {
    if (volpop_key(app, combo)) return true;   /* RV-237 */
    if (app->slidepop_open) {
        if (key_is(combo, "Escape")) { slidepop_close(app); return true; }
        return false;
    }
    if (!app->ctx_open) return false;
    int32_t groups[CTX_MAX];
    bool enabled[CTX_MAX];
    size_t n = ctx_rows(app, groups, enabled);
    if (key_is(combo, "Escape")) { ctx_close(app); return true; }
    if (key_is(combo, "Down")) { app->ctx_focus = rubraview_ctxmenu_step(enabled, n, app->ctx_focus, +1); return true; }
    if (key_is(combo, "Up")) { app->ctx_focus = rubraview_ctxmenu_step(enabled, n, app->ctx_focus, -1); return true; }
    if (key_is(combo, "Enter")) { if (app->ctx_focus >= 0) ctx_choose(app, app->ctx_focus); return true; }
    ctx_close(app);   /* any other key: it closes, and the key does what it does */
    return false;
}

#define HINT_DELAY 0.6

bool hint_shown(const app_state_t *app) {
    return app->hint.size > 0 && rubraview_pal_time_now_seconds() - app->hint_since >= HINT_DELAY;
}

/* RV-116: the hint, below and right of the pointer, turned to stay in the window. */
/* A tile's or a menu item's hint (owner, 2026-10-17): what it does, and its
   keys, "Toolbox (T, Ctrl+T)". Its words are the hint when it has one, else
   the action's description as the key list has it. */
proven_u8str_view_t hint_with_keys(app_state_t *app, proven_u8str_view_t action, const char *said, char *buffer, size_t capacity) {
    if (action.size == 0 || capacity < 8) return PROVEN_LIT("");
    const char *words = said ? said : rubraview_action_words(action);
    if (words && !said) words = rubraview_tr(words);
    size_t at = 0;
    int n = rubraview_fmt(buffer, capacity, "{}", PROVEN_ARG(words ? words : ""));
    at = n > 0 ? ((size_t)n < capacity ? (size_t)n : capacity - 1) : 0;
    size_t keys = 0;
    for (size_t b = 0; b < app->keymap.count && keys < 3; ++b) {
        const rubraview_key_binding_t *binding = &app->keymap.bindings[b];
        if (!proven_u8str_view_eq(binding->action, action)) continue;
        for (size_t c = 0; c < binding->combo_count && keys < 3; ++c) {
            char combo[48];
            proven_u8str_view_t k = rubraview_key_combo_format(combo, sizeof(combo), binding->combos[c]);
            if (k.size == 0) continue;
            n = rubraview_fmt(buffer + at, capacity - at, "{}{}", PROVEN_ARG(keys == 0 ? (at > 0 ? "  (" : "(") : ", "), PROVEN_ARG(rubraview_bytes_view(k.ptr, (int)k.size)));
            if (n > 0) at += (size_t)n < capacity - at ? (size_t)n : capacity - at - 1;
            keys++;
        }
    }
    if (keys > 0 && at + 1 < capacity) { buffer[at++] = ')'; buffer[at] = '\0'; }
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)buffer, .size = at };
}

void draw_hint(app_state_t *app, double win_w, double win_h) {
    if (!hint_shown(app) || app->tmenu_open) return;   /* 1.25.0: never over the ☰ pull-down's rows */
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double font = 13.0 * dpi;
    double w = (double)app->hint.size * font * 0.52 + 20.0 * dpi;
    double tw = 0.0, th = 0.0;
    if (rubraview_pal_render_measure_text(app->renderer, app->hint, font, &tw)) w = tw + 20.0 * dpi;
    double h = 28.0 * dpi;
    /* 1.25.0: a long one in lines, never wider than most of the window (VM: the Floating Widget's ran off both edges) */
    double most = fmin(win_w - 8.0 * dpi, 520.0 * dpi);
    if (w > most) {
        w = most;
        if (rubraview_pal_render_measure_text_wrapped(app->renderer, app->hint, font, w - 20.0 * dpi, &tw, &th)) h = th + 12.0 * dpi;
    }
    double x = app->pointer_x + 14.0 * dpi, y = app->pointer_y + 22.0 * dpi;
    if (x + w > win_w) x = win_w - w - 4.0 * dpi;
    if (y + h > win_h) y = app->pointer_y - h - 8.0 * dpi;
    if (x < 0.0) x = 0.0;
    if (y < 0.0) y = 0.0;
    rubraview_pal_rect_t box = { x, y, w, h };
    rubraview_pal_render_fill_rect(app->renderer, box, 0xF0303030u, 3.0 * dpi);
    rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_BOX_BORDER, 1.0 * dpi, 3.0 * dpi);
    rubraview_pal_rect_t words = { box.x + 10.0 * dpi, box.y, box.width - 20.0 * dpi, box.height };
    rubraview_pal_render_draw_text(app->renderer, app->hint, words, font, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
}

bool sub_tile_anchor(app_state_t *app, double *x, double *y) {
    if (app->toolbox.state == RUBRAVIEW_BOX_DETACHED) return false;
    rubraview_tile_metrics_t metrics = tile_metrics(rubraview_pal_window_dpi_scale(app->window));
    for (int32_t i = 0; i < app->toolbox_tile_count; ++i) {
        if (!proven_u8str_view_eq(app->toolbox_tiles[i].action, PROVEN_LIT("toggle_subtitles"))) continue;
        rubraview_rect_t r = rubraview_box_tile_rect(&app->toolbox, &metrics, i);
        if (r.width <= 0.0) return false;
        *x = r.x + r.width * 0.5;
        *y = r.y - 4.0 * rubraview_pal_window_dpi_scale(app->window);
        return true;
    }
    return false;
}

/* D-33: a press on the subtitle box, its buttons, or away from a selected
   box. True when the press was the box's. */
bool subbox_press(app_state_t *app, double x, double y, bool buttons_only) {
    if (!app->media || app->tracks.current_subtitle < 0) return false;
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    rubraview_rect_t box = subbox_geometry(app, (double)win_w, (double)win_h, NULL);
    rubraview_subbox_part_t part = rubraview_subbox_hit(&app->subbox, box, SUBBOX_BUTTON * dpi, SUBBOX_GAP * dpi,
                                                        subbox_live(app), x, y);
    /* The buttons of a selected box float over whatever is there (the
       toolbox's anchor sits right by the default place), so they are asked
       before the rest of the chrome; the body and the tap away after it. */
    if (buttons_only && (part == RUBRAVIEW_SUBBOX_NONE || part == RUBRAVIEW_SUBBOX_BODY)) return false;
    switch (part) {
        case RUBRAVIEW_SUBBOX_NONE:
            if (!app->subbox.selected) return false;
            app->subbox.selected = false;   /* the tap that lets go does nothing else */
            return true;
        case RUBRAVIEW_SUBBOX_BODY:
            app->subbox.selected = true;
            return true;
        case RUBRAVIEW_SUBBOX_SETTINGS:
            settings_open(app);
            if (app->settings_open) {
                rubraview_settings_view_set_page(&app->settings_view, RUBRAVIEW_TAB_VIDEO);
                app->settings_dirty = true;
            }
            return true;
        case RUBRAVIEW_SUBBOX_MOVE:
        case RUBRAVIEW_SUBBOX_RESIZE:
            (void)rubraview_subbox_begin_drag(&app->subbox, part, box, x, y);
            return true;
        case RUBRAVIEW_SUBBOX_CLOSE:
            if (!subs_may_leave(app)) return true;
            app->subtitle_last = app->tracks.current_subtitle;
            subtitle_select(app, -1);
            app->subbox.selected = false;
            subtitle_say(app);
            return true;
    }
    return false;
}

/* D-33, D-92: M or R held and dragged. Both write Settings > Video's
   place and size of the box, in whole percents of the window, so the
   settings page and the box never disagree. (R wrote the subtitle size
   until 0.0.47; the size is its own setting now.) */
void subbox_drag_motion(app_state_t *app, double x, double y) {
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) return;
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    rubraview_rect_t box = subbox_geometry(app, (double)win_w, (double)win_h, NULL);
    bool resizing = app->subbox.dragging == RUBRAVIEW_SUBBOX_RESIZE;
    double height = box.height;
    rubraview_subbox_drag(&app->subbox, box, x, y, (double)win_w, (double)win_h,
                          160.0 * dpi, 0.05 * (double)win_h, (double)win_h, &height);
    rubraview_rect_t moved = { app->subbox.left * (double)win_w, app->subbox.bottom * (double)win_h - height,
                               app->subbox.width * (double)win_w, height };
    app->subbox.placed = false;   /* the drag's own record is not a place to migrate */
    rubraview_subbox_place_t place = subbox_place(app);
    rubraview_subbox_place_from_rect(&place, moved, (double)win_w, (double)win_h, resizing);
    rubraview_settings_t *st = &app->settings;
    rubraview_settings_set(st, PROVEN_LIT("video"), PROVEN_LIT("subtitle_x"), floor(place.x * 100.0 + 0.5));
    rubraview_settings_set(st, PROVEN_LIT("video"), PROVEN_LIT("subtitle_y"), floor(place.y * 100.0 + 0.5));
    if (resizing) {
        rubraview_settings_set(st, PROVEN_LIT("video"), PROVEN_LIT("subtitle_width"), floor(place.width * 100.0 + 0.5));
        rubraview_settings_set(st, PROVEN_LIT("video"), PROVEN_LIT("subtitle_height"), floor(place.height * 100.0 + 0.5));
    }
    settings_took_effect(app);
}

/* The text box: renaming a file, or giving the picked files one
   extension. Drawn by the chrome and by the picker, which covers it. */
/* The OSD line. The picker covers the chrome, so it draws this itself. */
/* 1.25.0 (owner, 2026-10-20: "툴바의 진행막대 위에서 휠을 돌리면 파일이름/안내메시지 글꼴 크기"): how large the
   toolbar's file name and the messages are, from Settings › Display. */
double name_scale(const app_state_t *app) {
    double k = rubraview_settings_get(&app->settings, PROVEN_LIT("display"), PROVEN_LIT("name_scale")) / 100.0;
    return k < 0.6 ? 0.6 : k > 2.0 ? 2.0 : k;
}

/* What Shape (or a sort by length) waits for, under the button pressed: how many of how many, a bar, Esc. */
static void draw_pack_ask(app_state_t *app, double win_w, double win_h, double dpi);   /* D-267, below */
static void draw_extract_done(app_state_t *app, double win_w, double win_h, double dpi);   /* RV-240, below */
static void draw_playlist_ask(app_state_t *app, double win_w, double win_h, double dpi);   /* RV-255, below */
static void draw_list_del(app_state_t *app, double win_w, double win_h, double dpi);       /* RV-256, below */

void draw_reading(app_state_t *app, double win_w, double win_h, double dpi) {
    size_t done = 0, count = 0;
    bool sizes = true;
    if (!shape_waiting(app, &done, &count, &sizes) || count == 0) return;
    double w = 340.0 * dpi, h = 78.0 * dpi;
    rubraview_pal_rect_t box = { app->shape_x, app->shape_y + 4.0 * dpi, w, h };
    if (app->shape_x <= 0.0 && app->shape_y <= 0.0) box = (rubraview_pal_rect_t){ (win_w - w) * 0.5, win_h * 0.3, w, h };
    if (box.x + w > win_w - 8.0 * dpi) box.x = win_w - 8.0 * dpi - w;
    if (box.x < 8.0 * dpi) box.x = 8.0 * dpi;
    if (box.y + h > win_h - 8.0 * dpi) box.y = win_h - 8.0 * dpi - h;
    rubraview_pal_render_fill_rect(app->renderer, box, 0xF6202020u, 4.0 * dpi);
    rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_BOX_BORDER, 1.0, 4.0 * dpi);
    int pct = (int)(done * 100u / count);
    char line[128];
    int k = rubraview_fmt(line, sizeof line, sizes ? T("Reading the sizes: {} / {} ({}%)") : T("Reading the lengths: {} / {} ({}%)"),
                     PROVEN_ARG((size_t)done), PROVEN_ARG((size_t)count), PROVEN_ARG((int)pct));
    rubraview_pal_rect_t text = { box.x + 12.0 * dpi, box.y + 6.0 * dpi, w - 24.0 * dpi, 26.0 * dpi };
    if (k > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)k },
                                              text, 14.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    rubraview_pal_rect_t track = { text.x, box.y + 38.0 * dpi, text.width, 8.0 * dpi };
    rubraview_pal_render_fill_rect(app->renderer, track, 0x60FFFFFFu, 4.0 * dpi);
    rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ track.x, track.y, track.width * (double)done / (double)count, track.height },
                                   COLOR_TILE_CURRENT, 4.0 * dpi);
    rubraview_pal_render_draw_text(app->renderer, TU8("Esc stops it; the choices open when it is done"),
                                   (rubraview_pal_rect_t){ text.x, box.y + 50.0 * dpi, text.width, 24.0 * dpi },
                                   12.0 * dpi, 0xC0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
}

void draw_notice(app_state_t *app, double win_w, double win_h, double chrome_dpi) {
    draw_reading(app, win_w, win_h, chrome_dpi);
    draw_pack_ask(app, win_w, win_h, chrome_dpi);   /* D-267 */
    draw_extract_done(app, win_w, win_h, chrome_dpi);   /* RV-240 */
    draw_playlist_ask(app, win_w, win_h, chrome_dpi);   /* RV-255 */
    draw_list_del(app, win_w, win_h, chrome_dpi);       /* RV-256 */
    draw_arcadd_card(app, win_w, win_h, chrome_dpi);    /* RV-260 */
    if (app->notice_seconds > 0.0 && app->notice_length > 0) {
        double k = name_scale(app);   /* 1.25.0 */
        int before = place_font_begin(app, PF_NAME, NULL);
        double box_h = 44.0 * chrome_dpi * k;
        rubraview_pal_rect_t box = { win_w * 0.2, win_h * 0.08, win_w * 0.6, box_h };
        rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BAR_FILL, 3.0);
        rubraview_pal_render_draw_text(app->renderer,
                                       (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->notice, .size = app->notice_length },
                                       box, 15.0 * chrome_dpi * k, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        place_font_end(app, before);
    }
}

/* D-260 (owner, 2026-11-02: "압축할 때 옵션 창 ... 압축포맷 압축률 압축코덱 분할압축 등"): the Compress window's rows over
   its name box. The format is the name's ending; a greyed chip is one FultaArc does not write yet. */
static const char *const PACK_LEVEL_WORD[RUBRAVIEW_PACK_LEVEL_STEPS] = { N_("Store"), N_("Fastest"), N_("Fast"), N_("Normal"),
                                                                         N_("Maximum"), N_("Ultra") };
static const char *const PACK_SPLIT_WORD[] = { N_("None"), "10 MB", "100 MB", "700 MB", "4 GB" };
#define PACK_SPLITS ((int32_t)(sizeof PACK_SPLIT_WORD / sizeof *PACK_SPLIT_WORD))

static const char *pack_method_word(rubraview_pack_method_t m) {
    switch (m) {
        case RUBRAVIEW_PACK_METHOD_DEFLATE: return "Deflate";
        case RUBRAVIEW_PACK_METHOD_BZIP2:   return "BZip2";
        case RUBRAVIEW_PACK_METHOD_LZMA:    return "LZMA";
        case RUBRAVIEW_PACK_METHOD_LZMA2:   return "LZMA2";
        case RUBRAVIEW_PACK_METHOD_PPMD:    return "PPMd";
        case RUBRAVIEW_PACK_METHOD_ZSTD:    return "Zstandard";
        default:                            return "";
    }
}

#define PACK_TITLE_H 34.0
#define PACK_LINE_H 40.0     /* one line of chips */
#define PACK_LABEL_W 110.0   /* the rows' names, beside their chips when there is room */
#define PACK_WIDE_MIN 470.0  /* below this inside width a row's name stands on a line of its own over its chips */

/* What the window's two notes say: what the choice means, then the split set's names or what is greyed. */
static void pack_notes(app_state_t *app, const char **note, const char **note2) {
    bool sevenz = archwork_pack_is_7z(app);
    rubraview_pack_format_t format = sevenz ? RUBRAVIEW_PACK_7Z : RUBRAVIEW_PACK_ZIP;
    rubraview_pack_method_t lit = archwork_pack_method(app, sevenz);
    bool store = app->pack_level == 0;
    /* D-265 (owner, 2026-11-03: "7-Zip과 같게 + 안내"): LZMA's dictionary at Maximum and Ultra as 7-Zip's (FultaArc 3ca96f0) */
    bool lzma = lit == RUBRAVIEW_PACK_METHOD_LZMA || lit == RUBRAVIEW_PACK_METHOD_LZMA2;
    *note = lzma && app->pack_level == 5 ? T("Ultra: a large file may take about 3 GB of memory to pack, as in 7-Zip")
          : lzma && app->pack_level == 4 ? T("Maximum: a large file may take about 1.5 GB of memory to pack, as in 7-Zip")
          : app->pack_pw_len[0] || app->pack_focus == 1 || app->pack_focus == 2 ? (sevenz ? T("Password: AES-256 (7-Zip's own)") : T("Password: AES-256 (WinZip's; Windows' own ZIP cannot open it)"))
          : store ? T("Store: the files go in as they are, the codec unused")
          : !sevenz && rubraview_pack_method_plain(format, lit) ? T("ZIP with Deflate: Windows opens it too")
          : !sevenz ? T("ZIP with this codec: 7-Zip, Bandizip and this viewer open it, Windows' own does not")
          : T("7z: 7-Zip, Bandizip and this viewer open it");
    /* D-263: the split set's names, or what the greyed chips are; RV-248: how a size is typed while it is */
    *note2 = app->pack_focus == 3 ? T("Split: a size such as 700M, 4G or 10,000,000 (K, M, G, T, P count by 1024); empty for none")
           : app->pack_split_len > 0 ? (sevenz ? T("Split: name.7z.001, .002 ... (open the .001)") : T("Split: name.z01, .z02 ... name.zip (open the .zip)"))
           : app->pack_names_hidden && sevenz ? T("Hidden names: the archive is not even listed without its password")
           : !sevenz ? T("Greyed: ZIP packs each file on its own and keeps its names readable") : "";
}

/* RV-248 (owner, 2026-11-07: "압축 팝업창을 공간 효율적으로 그리고 좌우 폭에 상관없이 잘 레이아웃이 변할 수 있도록 ... html에서
   inline 요소들 처리하듯이 가로가 좁으면 다음 줄로 밀리게요"): the window's rows laid out in `panel`'s width — the chips of a row
   flow as inline words (rubraview_flow_inline), a row's name beside them or, narrow, on its own line over them; the
   notes wrap. Its height; drawn, and its chips and fields kept for the presses, only with `draw`. */
static double pack_window_pass(app_state_t *app, rubraview_pal_rect_t panel, double dpi, bool draw) {
    bool sevenz = archwork_pack_is_7z(app);
    rubraview_pack_format_t format = sevenz ? RUBRAVIEW_PACK_7Z : RUBRAVIEW_PACK_ZIP;
    rubraview_pack_method_t methods[8];
    size_t method_n = rubraview_pack_methods(format, methods, 8);
    rubraview_pack_method_t lit = archwork_pack_method(app, sevenz);
    bool store = app->pack_level == 0;
    double pad = 12.0 * dpi, line_h = PACK_LINE_H * dpi, gap = 6.0 * dpi, inner = panel.width - 2.0 * pad;
    bool wide = inner >= PACK_WIDE_MIN * dpi;
    double label_w = wide ? PACK_LABEL_W * dpi : 0.0, room = inner - label_w, name_h = 22.0 * dpi;
    uint64_t split_now = 0;
    bool split_reads = rubraview_pack_split_parse((proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->pack_split, .size = app->pack_split_len }, &split_now);
    if (draw) {
        app->pack_chip_n = 0;
        rubraview_pal_render_draw_text(app->renderer, TU8("Compress"), (rubraview_pal_rect_t){ panel.x + pad, panel.y, inner, PACK_TITLE_H * dpi },
                                       16.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    }
    static const char *const ROW_WORD[PACK_ROWS] = { N_("Format"), N_("Level"), N_("Codec"), N_("Solid"), N_("Split"),
                                                     N_("Password"), N_("Again"), N_("Names") };
    double y = panel.y + PACK_TITLE_H * dpi;
    for (int32_t row = 0; row < PACK_ROWS; ++row) {
        const char *words[8];
        bool on[8] = { 0 }, off[8] = { 0 };
        int32_t n = 0, field = -1;   /* a typed field first in the row: 1 the password, 2 it again, 3 the split size */
        switch (row) {
            case PACK_ROW_FORMAT:
                words[n] = "ZIP"; on[n++] = !sevenz;
                words[n] = "7z";  on[n++] = sevenz;
                break;
            case PACK_ROW_LEVEL:
                for (int32_t k = 0; k < RUBRAVIEW_PACK_LEVEL_STEPS; ++k) { words[n] = rubraview_tr(PACK_LEVEL_WORD[k]); on[n++] = app->pack_level == k; }
                break;
            case PACK_ROW_METHOD:
                for (size_t k = 0; k < method_n && n < 8; ++k) {
                    words[n] = pack_method_word(methods[k]);
                    on[n] = !store && methods[k] == lit;
                    off[n++] = store || !rubraview_pack_method_ready(format, methods[k]);
                }
                break;
            case PACK_ROW_SOLID:
                words[n] = T("Solid");        on[n] = sevenz && !app->pack_solid_off; off[n++] = !sevenz;
                words[n] = T("File by file"); on[n] = !sevenz || app->pack_solid_off;  off[n++] = !sevenz;
                break;
            case PACK_ROW_NAMES:   /* D-261: 7z only, not written by FultaArc yet */
                words[n] = T("Readable"); on[n] = !sevenz || !app->pack_names_hidden; off[n++] = !sevenz;
                words[n] = T("Hidden by the password"); on[n] = sevenz && app->pack_names_hidden;
                off[n++] = !sevenz || !rubraview_pack_names_hidden_supported(format);
                break;
            case PACK_ROW_PASSWORD: field = 1; break;
            case PACK_ROW_AGAIN:    field = 2; break;
            default: {   /* RV-248: the size typed, then the chips that write one in */
                static const uint64_t PRESET_MIB[] = { 0, 10, 100, 700, 4095 };
                field = 3;
                for (int32_t k = 0; k < PACK_SPLITS; ++k) {
                    words[n] = k == 0 ? rubraview_tr(PACK_SPLIT_WORD[0]) : PACK_SPLIT_WORD[k];
                    on[n] = split_reads && split_now == (PRESET_MIB[k] << 20);
                    off[n++] = k > 0 && !rubraview_pack_split_supported(format);
                }
                break;
            }
        }
        /* the items' widths: the field, then the chips */
        double widths[9];
        int32_t items = 0;
        if (field > 0) widths[items++] = fmin((field == 3 ? 150.0 : 320.0) * dpi, room);
        for (int32_t k = 0; k < n; ++k) {
            double tw = 0.0;
            (void)rubraview_pal_render_measure_text(app->renderer, cstr(words[k]), 14.0 * dpi, &tw);
            widths[items++] = fmin(fmax(tw + 24.0 * dpi, 64.0 * dpi), room);
        }
        uint32_t line_of[9];
        double x_of[9];
        size_t lines = rubraview_flow_inline(widths, (size_t)items, gap, room, line_of, x_of);
        double label_y = y;
        if (!wide) y += name_h;   /* the row's name on its own line */
        if (draw) {
            rubraview_pal_render_draw_text(app->renderer, cstr(rubraview_tr(ROW_WORD[row])),
                                           wide ? (rubraview_pal_rect_t){ panel.x + pad, label_y, label_w, line_h - 6.0 * dpi }
                                                : (rubraview_pal_rect_t){ panel.x + pad, label_y, inner, name_h },
                                           wide ? 14.0 * dpi : 13.0 * dpi, 0xC0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
            double x0 = panel.x + pad + label_w;
            for (int32_t i = 0; i < items; ++i) {
                rubraview_pal_rect_t r = { x0 + x_of[i], y + (double)line_of[i] * line_h + 3.0 * dpi, widths[i], line_h - 8.0 * dpi };
                if (field > 0 && i == 0) {   /* D-261: a field; the box itself is drawn there when typed in */
                    app->pack_field[field] = r;
                    if (app->pack_focus == field) continue;
                    rubraview_pal_render_fill_rect(app->renderer, r, COLOR_BOX_FILL, 3.0);
                    rubraview_pal_render_stroke_rect(app->renderer, r, COLOR_BOX_BORDER, 1.0, 3.0);
                    rubraview_pal_rect_t in = { r.x + 10.0 * dpi, r.y, r.width - 20.0 * dpi, r.height };
                    if (field == 3) {
                        if (app->pack_split_len) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->pack_split, .size = app->pack_split_len },
                                                                                in, 14.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
                        else rubraview_pal_render_draw_text(app->renderer, cstr(rubraview_tr(PACK_SPLIT_WORD[0])), in, 13.0 * dpi, 0x80F0F0F0u, RUBRAVIEW_TEXT_LEFT);
                        continue;
                    }
                    char dots[3 * 256];
                    size_t nd = 0;
                    for (size_t c = 0; c < app->pack_pw_len[field - 1]; ++c)
                        if (((unsigned char)app->pack_pw[field - 1][c] & 0xC0) != 0x80) { proven_sys_mem_copy(dots + nd, "\xE2\x97\x8F", 3); nd += 3; }
                    if (nd) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)dots, .size = nd },
                                                           in, 14.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
                    else if (field == 1) rubraview_pal_render_draw_text(app->renderer, TU8("none (Tab or a click to type one)"), in, 13.0 * dpi, 0x80F0F0F0u, RUBRAVIEW_TEXT_LEFT);
                    continue;
                }
                int32_t k = field > 0 ? i - 1 : i;
                bool over = pal_rect_has(r, app->pointer_x, app->pointer_y);
                rubraview_pal_render_fill_rect(app->renderer, r, on[k] ? COLOR_TILE_CURRENT : over && !off[k] ? 0x40FFFFFFu : COLOR_TILE_FILL, 3.0);
                rubraview_pal_render_stroke_rect(app->renderer, r, COLOR_BOX_BORDER, 1.0, 3.0);
                rubraview_pal_render_draw_text(app->renderer, cstr(words[k]), r, 14.0 * dpi, off[k] ? 0x60F0F0F0u : COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
                if (app->pack_chip_n < PACK_CHIPS_MAX) {
                    app->pack_chip[app->pack_chip_n] = r;
                    app->pack_chip_kind[app->pack_chip_n] = row * 16 + k;
                    app->pack_chip_off[app->pack_chip_n++] = off[k];
                }
            }
        }
        y += (double)(lines ? lines : 1) * line_h;
    }
    /* the notes, wrapped in the width there is */
    const char *note, *note2;
    pack_notes(app, &note, &note2);
    const char *notes[2] = { note, note2 };
    for (int i = 0; i < 2; ++i) {
        if (!notes[i][0]) continue;
        double tw = 0.0, th = 0.0;
        if (!rubraview_pal_render_measure_text_wrapped(app->renderer, cstr(notes[i]), 13.0 * dpi, inner, &tw, &th)) th = 18.0 * dpi;
        double h = th + 6.0 * dpi;
        if (draw) rubraview_pal_render_draw_text(app->renderer, cstr(notes[i]), (rubraview_pal_rect_t){ panel.x + pad, y, inner, h },
                                                 13.0 * dpi, i == 0 ? 0xC0F0F0F0u : 0x90F0F0F0u, RUBRAVIEW_TEXT_LEFT);
        y += h;
    }
    return y + 10.0 * dpi - panel.y;
}

/* The window over its name box: as wide as the label under it, its rows laid out for that width. */
static double pack_window_height(app_state_t *app, double width, double dpi) {
    return pack_window_pass(app, (rubraview_pal_rect_t){ 0.0, 0.0, width, 0.0 }, dpi, false);
}

static void draw_pack_window(app_state_t *app, rubraview_pal_rect_t name_label, double dpi) {
    double panel_h = pack_window_height(app, name_label.width, dpi);
    rubraview_pal_rect_t panel = { name_label.x, name_label.y - panel_h - 8.0 * dpi, name_label.width, panel_h };
    if (panel.y < 4.0 * dpi) panel.y = 4.0 * dpi;
    app->pack_panel = panel;
    rubraview_pal_render_fill_rect(app->renderer, panel, 0xF6202020u, 4.0 * dpi);
    rubraview_pal_render_stroke_rect(app->renderer, panel, COLOR_BOX_BORDER, 1.0, 4.0 * dpi);
    (void)pack_window_pass(app, panel, dpi, true);
}

/* D-267: an older split set's parts in the way, asked about over everything (the open-file screen too). */
static void draw_pack_ask(app_state_t *app, double win_w, double win_h, double dpi) {
    if (!app->pack_ask) return;
    double box_w = 640.0 * dpi, box_h = 170.0 * dpi;
    if (box_w > win_w - 16.0 * dpi) box_w = win_w - 16.0 * dpi;
    rubraview_pal_rect_t box = { (win_w - box_w) * 0.5, (win_h - box_h) * 0.5, box_w, box_h };
    rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BOX_FILL, 3.0);
    rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_CLOSE_HOVER, 2.0, 3.0);
    char line[1200];
    proven_u8str_view_t set = rubraview_path_basename(proven_u8str_view_from_cstr(app->pack_ask_set));
    int n = rubraview_fmt(line, sizeof line, T("Parts of an older split {} are in the way ({} files)."), PROVEN_ARG(rubraview_bytes_view((const char *)set.ptr, (int)set.size)),
                     PROVEN_ARG((size_t)app->pack_ask_parts));
    if (n > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)(n < (int)sizeof line ? n : (int)sizeof line - 1) },
                                              (rubraview_pal_rect_t){ box.x + 12.0 * dpi, box.y + 14.0 * dpi, box.width - 24.0 * dpi, 30.0 * dpi },
                                              16.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    const char *words[2] = { T("Recycle them and compress  (Enter)"), T("Cancel  (Esc)") };
    double gap = 12.0 * dpi, bh = 52.0 * dpi, bw = (box.width - 3.0 * gap) / 2.0, by = box.y + 60.0 * dpi;
    for (int i = 0; i < 2; ++i) {
        rubraview_pal_rect_t b = { box.x + gap + (double)i * (bw + gap), by, bw, bh };
        app->pack_ask_btn[i] = b;
        bool over = pal_rect_has(b, app->pointer_x, app->pointer_y);
        rubraview_pal_render_fill_rect(app->renderer, b, i == 0 ? (over ? COLOR_CLOSE_HOVER : 0xC0802030u) : over ? 0x50FFFFFFu : COLOR_BOX_FILL, 3.0);
        rubraview_pal_render_stroke_rect(app->renderer, b, COLOR_BOX_BORDER, 1.0, 3.0);
        rubraview_pal_render_draw_text(app->renderer, cstr(words[i]), b, 15.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
    rubraview_pal_render_draw_text(app->renderer, TU8("They would be read as part of the new set; the Recycle Bin can give them back."),
                                   (rubraview_pal_rect_t){ box.x, by + bh + 12.0 * dpi, box.width, 26.0 * dpi },
                                   13.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
}

/* RV-240: an extraction done, the card asks what next — the folder here, in Explorer, or nothing (the RV-238 card's look) */
static void draw_extract_done(app_state_t *app, double win_w, double win_h, double dpi) {
    if (!app->extract_done) return;
    double cw = fmin(600.0 * dpi, win_w - 16.0 * dpi), ch = 196.0 * dpi, pad = 20.0 * dpi;
    rubraview_pal_rect_t card = { (win_w - cw) * 0.5, (win_h - ch) * 0.45, cw, ch };
    rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ 0.0, 0.0, win_w, win_h }, 0x90000000u, 0.0);
    rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ card.x + 3.0 * dpi, card.y + 5.0 * dpi, cw, ch }, 0x60000000u, 8.0 * dpi);
    rubraview_pal_render_fill_rect(app->renderer, card, 0xFA242424u, 8.0 * dpi);
    rubraview_pal_render_stroke_rect(app->renderer, card, COLOR_BOX_BORDER, 1.0 * dpi, 8.0 * dpi);
    rubraview_pal_rect_t tr = { card.x + pad, card.y + pad - 4.0 * dpi, cw - 2.0 * pad, 30.0 * dpi };
    proven_u8str_view_t title = TU8("Extracted");
    rubraview_pal_render_draw_text(app->renderer, title, tr, 18.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    rubraview_pal_render_draw_text(app->renderer, title, (rubraview_pal_rect_t){ tr.x + 0.6 * dpi, tr.y, tr.width, tr.height }, 18.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    rubraview_pal_render_draw_text(app->renderer, cstr(app->extract_done_line), (rubraview_pal_rect_t){ tr.x, tr.y + 30.0 * dpi, tr.width, 22.0 * dpi },
                                   14.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    char fit[1200];
    proven_u8str_view_t path = files_fit_text(fit, sizeof fit, cstr(app->extract_done_path), tr.width, 13.0 * dpi);
    rubraview_pal_render_draw_text(app->renderer, path, (rubraview_pal_rect_t){ tr.x, tr.y + 54.0 * dpi, tr.width, 22.0 * dpi },
                                   13.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
    const char *words[3] = { T("Open it here"), T("Show in Explorer"), T("Close") };
    double bh = 38.0 * dpi, gap = 10.0 * dpi, bw[3], total = 0.0;
    for (int i = 0; i < 3; ++i) {
        double tw = 0.0;
        (void)rubraview_pal_render_measure_text(app->renderer, cstr(words[i]), 14.0 * dpi, &tw);
        bw[i] = fmax(104.0 * dpi, tw + 36.0 * dpi);
        total += bw[i] + (i ? gap : 0.0);
    }
    double x = card.x + cw - pad - total, y = card.y + ch - pad - bh;
    for (int i = 0; i < 3; ++i) {
        rubraview_pal_rect_t b = { x, y, bw[i], bh };
        app->extract_done_btn[i] = b;
        bool over = pal_rect_has(b, app->pointer_x, app->pointer_y);
        rubraview_pal_render_fill_rect(app->renderer, b, i == 0 ? COLOR_TILE_CURRENT : over ? 0x50FFFFFFu : COLOR_TILE_FILL, 5.0 * dpi);
        if (i == 0 && over) rubraview_pal_render_fill_rect(app->renderer, b, 0x30FFFFFFu, 5.0 * dpi);
        if (i > 0) rubraview_pal_render_stroke_rect(app->renderer, b, COLOR_BOX_BORDER, 1.0, 5.0 * dpi);
        rubraview_pal_render_draw_text(app->renderer, cstr(words[i]), b, 14.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        x += bw[i] + gap;
    }
}

void extract_done_answer(app_state_t *app, int which) {
    if (!app->extract_done) return;
    app->extract_done = false;
    proven_u8str_view_t path = app_keep(app, cstr(app->extract_done_path));
    if (which == 0) {   /* the folder in the open-file screen */
        if (picker_in_archive(app)) picker_leave_archive(app);
        if (!app->picker_open) handle_action(app, PROVEN_LIT("open_picker"));
        picker_navigate(app, path);
        app->picker.focus = 0;
    } else if (which == 1) {   /* Windows' own Explorer */
        if (!rubraview_pal_shell_open_folder(path, false)) osd_say(app, TU8("Explorer could not show the folder"));
    }
}

bool extract_done_press(app_state_t *app, double x, double y) {
    if (!app->extract_done) return false;
    for (int i = 0; i < 3; ++i) if (pal_rect_has(app->extract_done_btn[i], x, y)) { extract_done_answer(app, i); return true; }
    return true;   /* nothing under it while it asks */
}

/* RV-255 (owner, 2026-11-08: "자체 탐색기에서 재생목록 파일을 열려고 하면, 텍스트 에디터로 열기 / 현재 재생목록에 추가하기 / 이 재생목록으로 교체 이렇게 선택하도록 하자."): a playlist chosen in the open-file screen asks how it is opened — as
   text in Windows' editor, its files added to the list being read, or in place of that list; Esc leaves it. The
   card's look is RV-240's; its buttons wrap to a second line in a narrow window. */
void playlist_ask_open(app_state_t *app, proven_u8str_view_t path) {
    if (path.size == 0 || path.size >= sizeof app->playlist_ask_path) return;
    proven_sys_mem_copy(app->playlist_ask_path, path.ptr, path.size);
    app->playlist_ask_path[path.size] = '\0';
    app->playlist_ask = true;
}

static void draw_playlist_ask(app_state_t *app, double win_w, double win_h, double dpi) {
    if (!app->playlist_ask) return;
    double cw = fmin(640.0 * dpi, win_w - 16.0 * dpi), pad = 20.0 * dpi, bh = 38.0 * dpi, gap = 10.0 * dpi;
    const char *words[4] = { T("Edit it as text"), T("Add to the current list"), T("Open in place of the list"), T("Cancel") };
    double bw[4];
    for (int i = 0; i < 4; ++i) {
        double tw = 0.0;
        (void)rubraview_pal_render_measure_text(app->renderer, cstr(words[i]), 14.0 * dpi, &tw);
        bw[i] = fmin(fmax(96.0 * dpi, tw + 32.0 * dpi), cw - 2.0 * pad);
    }
    uint32_t line_of[4];
    double x_of[4];
    size_t lines = rubraview_flow_inline(bw, 4, gap, cw - 2.0 * pad, line_of, x_of);   /* RV-248's flow */
    double ch = pad + 30.0 * dpi + 26.0 * dpi + 14.0 * dpi + (double)lines * (bh + gap) - gap + pad;
    rubraview_pal_rect_t card = { (win_w - cw) * 0.5, (win_h - ch) * 0.45, cw, ch };
    rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ 0.0, 0.0, win_w, win_h }, 0x90000000u, 0.0);
    rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ card.x + 3.0 * dpi, card.y + 5.0 * dpi, cw, ch }, 0x60000000u, 8.0 * dpi);
    rubraview_pal_render_fill_rect(app->renderer, card, 0xFA242424u, 8.0 * dpi);
    rubraview_pal_render_stroke_rect(app->renderer, card, COLOR_BOX_BORDER, 1.0 * dpi, 8.0 * dpi);
    rubraview_pal_rect_t tr = { card.x + pad, card.y + pad - 4.0 * dpi, cw - 2.0 * pad, 30.0 * dpi };
    proven_u8str_view_t title = TU8("A playlist: how is it opened?");
    rubraview_pal_render_draw_text(app->renderer, title, tr, 18.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    rubraview_pal_render_draw_text(app->renderer, title, (rubraview_pal_rect_t){ tr.x + 0.6 * dpi, tr.y, tr.width, tr.height }, 18.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    char fit[1200];
    proven_u8str_view_t name = files_fit_text(fit, sizeof fit, rubraview_path_basename(cstr(app->playlist_ask_path)), tr.width, 13.0 * dpi);
    rubraview_pal_render_draw_text(app->renderer, name, (rubraview_pal_rect_t){ tr.x, tr.y + 30.0 * dpi, tr.width, 22.0 * dpi },
                                   13.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
    double y0 = tr.y + 30.0 * dpi + 26.0 * dpi + 14.0 * dpi;
    for (int i = 0; i < 4; ++i) {
        rubraview_pal_rect_t b = { card.x + pad + x_of[i], y0 + (double)line_of[i] * (bh + gap), bw[i], bh };
        app->playlist_ask_btn[i] = b;
        bool over = pal_rect_has(b, app->pointer_x, app->pointer_y), main = i == 2;   /* Enter's: in place of the list */
        rubraview_pal_render_fill_rect(app->renderer, b, main ? COLOR_TILE_CURRENT : over ? 0x50FFFFFFu : COLOR_TILE_FILL, 5.0 * dpi);
        if (main && over) rubraview_pal_render_fill_rect(app->renderer, b, 0x30FFFFFFu, 5.0 * dpi);
        if (!main) rubraview_pal_render_stroke_rect(app->renderer, b, COLOR_BOX_BORDER, 1.0, 5.0 * dpi);
        rubraview_pal_render_draw_text(app->renderer, cstr(words[i]), b, 14.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
}

void playlist_ask_answer(app_state_t *app, int which) {
    if (!app->playlist_ask) return;
    app->playlist_ask = false;
    proven_u8str_view_t path = app_keep(app, cstr(app->playlist_ask_path));
    if (which == 0) {
        if (!rubraview_pal_shell_edit_text(path)) osd_say(app, TU8("no text editor could be started"));
    } else if (which == 1) {
        app->picker_open = false;
        playlist_add(app, path);
    } else if (which == 2) {
        app->picker_open = false;
        open_path(app, path);
    }
}

bool playlist_ask_press(app_state_t *app, double x, double y) {
    if (!app->playlist_ask) return false;
    for (int i = 0; i < 4; ++i) if (pal_rect_has(app->playlist_ask_btn[i], x, y)) { playlist_ask_answer(app, i); return true; }
    return true;   /* nothing under it while it asks */
}

/* RV-256 (D-308): RV-117's question about a file of the list, on a card of the window's own (D-267's look); No is
   the default. */
static void draw_list_del(app_state_t *app, double win_w, double win_h, double dpi) {
    if (!app->list_del_ask) return;
    double box_w = fmin(640.0 * dpi, win_w - 16.0 * dpi), box_h = 196.0 * dpi;
    rubraview_pal_rect_t box = { (win_w - box_w) * 0.5, (win_h - box_h) * 0.5, box_w, box_h };
    rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ 0.0, 0.0, win_w, win_h }, 0x90000000u, 0.0);
    rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BOX_FILL, 3.0);
    rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_CLOSE_HOVER, 2.0, 3.0);
    rubraview_pal_render_draw_text(app->renderer, TU8("Move this file to the recycle bin?"),
                                   (rubraview_pal_rect_t){ box.x + 12.0 * dpi, box.y + 12.0 * dpi, box.width - 24.0 * dpi, 28.0 * dpi },
                                   17.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    char fit[1200];
    proven_u8str_view_t name = files_fit_text(fit, sizeof fit, rubraview_path_basename(cstr(app->list_del_path)), box.width - 24.0 * dpi, 14.0 * dpi);
    rubraview_pal_render_draw_text(app->renderer, name, (rubraview_pal_rect_t){ box.x + 12.0 * dpi, box.y + 42.0 * dpi, box.width - 24.0 * dpi, 24.0 * dpi },
                                   14.0 * dpi, 0xD0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
    const char *words[2] = { T("Recycle it  (Y)"), T("Keep it  (Esc)") };
    double gap = 12.0 * dpi, bh = 48.0 * dpi, bw = (box.width - 3.0 * gap) / 2.0, by = box.y + 76.0 * dpi;
    for (int i = 0; i < 2; ++i) {
        rubraview_pal_rect_t b = { box.x + gap + (double)i * (bw + gap), by, bw, bh };
        app->list_del_btn[i] = b;
        bool over = pal_rect_has(b, app->pointer_x, app->pointer_y);
        rubraview_pal_render_fill_rect(app->renderer, b, i == 0 ? (over ? COLOR_CLOSE_HOVER : 0xC0802030u) : over ? 0x50FFFFFFu : COLOR_BOX_FILL, 3.0);
        rubraview_pal_render_stroke_rect(app->renderer, b, COLOR_BOX_BORDER, 1.0, 3.0);
        rubraview_pal_render_draw_text(app->renderer, cstr(words[i]), b, 15.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
    rubraview_pal_render_draw_text(app->renderer, TU8("It leaves the list and its folder; the recycle bin can give it back."),
                                   (rubraview_pal_rect_t){ box.x, by + bh + 12.0 * dpi, box.width, 26.0 * dpi },
                                   13.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
}

bool list_del_press(app_state_t *app, double x, double y) {
    if (!app->list_del_ask) return false;
    for (int i = 0; i < 2; ++i) if (pal_rect_has(app->list_del_btn[i], x, y)) { list_delete_answer(app, i == 0); return true; }
    return true;   /* nothing under it while it asks */
}

bool pack_ask_press(app_state_t *app, double x, double y) {
    if (!app->pack_ask) return false;
    if (pal_rect_has(app->pack_ask_btn[0], x, y)) archwork_pack_ask_answer(app, true);
    else if (pal_rect_has(app->pack_ask_btn[1], x, y)) archwork_pack_ask_answer(app, false);
    return true;   /* nothing under it while it asks */
}

/* RV-265 (R296): the box as a modeless card. Which kind of question it is (its place is kept by kind), -1 for the
   boxes that are fields of their own screens: the open-file screen's path and filter, the Compress window. */
int32_t card_kind_now(const app_state_t *app) {
    if (!app->rename_active || app->rename_is_path || app->rename_is_filter || app->rename_is_pack) return -1;
    if (app->rename_is_find) return CARD_FIND;
    if (app->rename_is_listfilter) return CARD_LIST_FILTER;
    if (app->rename_is_target) return CARD_TARGET;
    if (app->rename_is_extract) return CARD_EXTRACT;
    if (app->rename_is_extension) return CARD_EXTENSIONS;
    if (app->rename_is_password) return CARD_PASSWORD;
    if (app->rename_is_address) return CARD_ADDRESS;
    if (app->rename_is_apply) return CARD_COPIES;
    if (app->rename_is_number) return app->number_target == NUMBER_VOLUME ? CARD_VOLUME : CARD_DELAY;   /* RV-267 */
    if (app->rename_is_value || app->rename_is_gmic || app->rename_is_ffmpeg || app->batch_typing) return CARD_VALUE;
    if (app->rename_is_archive) return CARD_ARCHIVE_PAGE;
    if (app->rename_is_picker) return CARD_PICKER_NAME;
    if (app->rename_favorite >= 0) return CARD_FAVORITE;
    return CARD_RENAME;
}

static const char *const CARD_KEY[CARD_KINDS] = { "rename", "archive_page", "name", "extensions", "target", "extract", "password",
                                                  "find", "list_filter", "address", "copies", "value", "favorite", "size", "progress",
                                                  "volume", "delay", "repeat" };
const char *card_kind_key(int32_t kind) { return kind >= 0 && kind < CARD_KINDS ? CARD_KEY[kind] : ""; }

/* What the window had in hand when the card opened: the page, the open-file screen's folder and listing. A card
   that acts on it (a page of an archive renamed, a name in the open-file screen, the extensions or the folder for
   the files picked) closes when the window moves on, rather than act on what came after. */
static uint64_t card_subject_now(app_state_t *app) {
    uint64_t h = 1469598103934665603ull;
    proven_u8str_view_t parts[2] = { current_file_path(app), app->picker_dir };
    for (int k = 0; k < 2; ++k)
        for (size_t i = 0; i < parts[k].size; ++i) { h ^= parts[k].ptr[i]; h *= 1099511628211ull; }
    uint64_t more[3] = { (uint64_t)(int64_t)current_page_index(app), app->picker_open ? 1u : 0u, (uint64_t)app->picker_listing.count };
    for (int k = 0; k < 3; ++k) { h ^= more[k]; h *= 1099511628211ull; }
    return h;
}

static bool card_bound(int32_t kind) {
    return kind == CARD_ARCHIVE_PAGE || kind == CARD_PICKER_NAME || kind == CARD_EXTENSIONS || kind == CARD_TARGET;
}

/* The keyboard is the box's: no card, or the card has the focus. */
bool rename_has_keys(const app_state_t *app) {
    return app->rename_active && (!app->card.open || app->card.focused);
}

/* The card goes with its box; where it was is kept for the next of its kind. */
void card_close_keep(app_state_t *app) {
    if (app->card.open && app->card_kind >= 0 && app->card_kind < CARD_KINDS && app->card_host_w > 0.0)
        app->card_kept[app->card_kind] = rubraview_card_share_of(&app->card, app->card_host_w, app->card_host_h);
    rubraview_card_close(&app->card);
    app->card_kind = -1;
    app->card_for_len = 0;
}

bool card_motion(app_state_t *app, int32_t host, double x, double y) {
    if (app->ab_card.open && app->ab_card.drag != 0 && host == CARD_HOST_MAIN) {   /* RV-267: the A-B card */
        int32_t w = 0, h = 0;
        rubraview_pal_window_get_size(app->window, &w, &h);
        (void)rubraview_card_motion(&app->ab_card, x, y, (double)w, (double)h);
        return true;
    }
    if (app->prog_card.open && app->prog_card.drag != 0 && host == CARD_HOST_MAIN) {   /* RV-268: the progress card */
        (void)rubraview_card_motion(&app->prog_card, x, y, app->prog_host_w, app->prog_host_h);
        return true;
    }
    if (app->px_card.open && app->px_card.drag != 0 && host == app->px_card_host) {   /* the size box's card */
        (void)rubraview_card_motion(&app->px_card, x, y, app->px_card_host_w, app->px_card_host_h);
        return true;
    }
    if (!app->card.open || app->card.drag == 0 || host != app->card_host) return false;
    (void)rubraview_card_motion(&app->card, x, y, app->card_host_w, app->card_host_h);
    return true;
}

bool card_release(app_state_t *app, int32_t host) {
    if (app->ab_card.open && host == CARD_HOST_MAIN && rubraview_card_release(&app->ab_card)) return true;
    if (app->prog_card.open && host == CARD_HOST_MAIN && rubraview_card_release(&app->prog_card)) return true;
    if (app->px_card.open && host == app->px_card_host && rubraview_card_release(&app->px_card)) return true;
    if (host != app->card_host) return false;
    return rubraview_card_release(&app->card);
}

/* Once a pass: a card for what the window has left closes, saying so; a plain rename's file gone, the same. */
void card_watch(app_state_t *app) {
    if (!app->card.open || !app->rename_active) return;
    int32_t kind = app->card_kind;
    bool gone = false;
    if (card_bound(kind) && card_subject_now(app) != app->card_subject) gone = true;
    if (kind == CARD_RENAME && app->card_for_len > 0) {
        double now = rubraview_pal_time_now_seconds();
        if (now - app->card_checked_at >= 1.0) {
            app->card_checked_at = now;
            gone = !rubraview_pal_fs_exists((proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->card_for, .size = app->card_for_len });
        }
    }
    if (!gone) return;
    if (app->rename_is_extract) archwork_extract_dest_dropped(app);
    rename_end(app);
    osd_say(app, TU8("the box closed: what it was for is no longer there"));
}

/* A card opened in a host: its kind's place if kept, else where it always came. */
static void card_open_in(app_state_t *app, int32_t kind, double win_w, double win_h, double want_w, double want_h, bool live, double dpi) {
    rubraview_card_t *cd = &app->card;
    cd->margin = 8.0 * dpi;
    cd->edge = 6.0 * dpi;
    cd->title_height = 46.0 * dpi;
    const rubraview_card_share_t *kept = &app->card_kept[kind];
    double rise = 0.45;
    if (live) {   /* what it filters stays in sight: near the top */
        double room = win_h - want_h;
        rise = room > 1.0 ? fmin(1.0, (app->rename_is_find ? 96.0 : 60.0) * dpi / room) : 0.0;
    }
    rubraview_card_open(cd, win_w, win_h, want_w, want_h, rise, kept);
    /* Edit to all asks with Edit open: the card goes left of its panel, the first time. */
    if (!rubraview_card_share_valid(kept) && edit_panel_open(app) && !app->edit_window && app->panel.open &&
        cd->rect.x + cd->rect.width > app->panel.bounds.x && app->panel.bounds.x > cd->rect.width)
        cd->rect.x = (app->panel.bounds.x - cd->rect.width) * 0.5;
    app->card_kind = kind;
    app->card_subject = card_subject_now(app);
    app->card_checked_at = rubraview_pal_time_now_seconds();
}

/* RV-238 (owner, 2026-11-07: "파일이름이나 텍스트를 입력받을 때 모양이 조금 성의가 없어요. 잘 다듬어진 팝업창을 띄우거나 해서 좀
   다듬어 주세요"): what the box is for, as its card's title and the line under it. */
static void rename_card_words(const app_state_t *app, char *title, size_t capacity, const char **hint) {
    const char *t = T("Rename");
    *hint = T("The whole name, its extension too  ·  Enter renames  ·  Esc leaves it");
    if (app->card_for_len > 0 && card_kind_now(app) == CARD_RENAME) {   /* RV-265: the card names the file it is for */
        proven_u8str_view_t name = rubraview_path_basename((proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->card_for, .size = app->card_for_len });
        rubraview_fmt(title, capacity, T("Rename {}"), PROVEN_ARG(rubraview_bytes_view(name.ptr, (int)name.size)));
        return;
    }
    if (app->rename_is_find) { t = T("Find"); *hint = T("Enter or Ctrl+G the next  ·  Shift+Enter back  ·  Esc closes"); }
    else if (app->rename_is_listfilter) { t = T("Filter the list"); *hint = T("The list shows the names holding this  ·  Esc shows them all"); }
    else if (app->rename_is_target) {
        t = app->target_moving ? T("Move to which folder?") : T("Copy to which folder?");
        *hint = T("Enter  ·  Ctrl+O browses  ·  a folder not there yet is made");
    } else if (app->rename_is_extract) {   /* RV-240 */
        t = T("Extract into which folder?");
        *hint = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("extract_mode"))) == 1
              ? T("A folder of the archive's name is made in it when it holds more than one thing  ·  Ctrl+O browses")
              : T("A folder of the archive's name is made in it  ·  Ctrl+O browses");
    } else if (app->rename_is_extension) { t = T("Change extensions"); *hint = T("A new one for each  ·  Tab: the next row"); }
    else if (app->rename_is_password) {
        proven_u8str_view_t name = rubraview_path_basename((proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->password_for, .size = app->password_for_len });
        rubraview_fmt(title, capacity, T("Password for {}"), PROVEN_ARG(rubraview_bytes_view(name.ptr, (int)name.size)));
        *hint = T("Enter opens  ·  the eye shows it as typed  ·  Esc leaves it");
        return;
    }
    else if (app->rename_is_address) { t = T("Add a station"); *hint = T("Its address, then a space and its title if you like"); }
    else if (app->rename_is_apply) { t = T("Name of the copies"); *hint = T("* is the picture's own name: *_edit, edited_*"); }
    else if (app->rename_is_number && app->number_target == NUMBER_VOLUME) { t = T("Volume"); *hint = T("A percent, 0 to 100  ·  Enter keeps it  ·  Esc leaves it"); }
    else if (app->rename_is_number) { t = T("Slide show delay"); *hint = T("Seconds, 0.1 to 300  ·  Enter keeps it  ·  Esc leaves it"); }
    else if (app->rename_is_value || app->rename_is_gmic || app->rename_is_ffmpeg || app->batch_typing) {
        t = T("Type it"); *hint = T("Enter keeps it  ·  Esc leaves it");
    }
    else if (app->rename_is_archive) { t = T("Rename the page"); *hint = T("Inside the archive  ·  Enter renames  ·  Esc leaves it"); }
    else if (app->rename_is_picker) { t = T("Name"); *hint = T("Enter keeps it  ·  Esc leaves it"); }
    rubraview_fmt(title, capacity, "{}", PROVEN_ARG(t));
}

/* RV-265 (R296): every input card is drawn by this: its shadow, the card lit at its edge while it has the keys, the
   grip its corner is sized by, the title (moved by), the line saying what the keys do. Returns where the content
   starts. */
double draw_card_frame(app_state_t *app, rubraview_pal_rect_t c, const char *title, const char *hint, bool focused, double dpi) {
    double pad = 20.0 * dpi, title_h = 30.0 * dpi, hint_h = 22.0 * dpi;
    rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ c.x + 3.0 * dpi, c.y + 5.0 * dpi, c.width, c.height }, 0x60000000u, 8.0 * dpi);   /* its shadow */
    rubraview_pal_render_fill_rect(app->renderer, c, 0xFA242424u, 8.0 * dpi);
    rubraview_pal_render_stroke_rect(app->renderer, c, focused ? COLOR_TILE_CURRENT : COLOR_BOX_BORDER, (focused ? 1.5 : 1.0) * dpi, 8.0 * dpi);
    for (int gi = 0; gi < 3; ++gi)   /* the grip a corner is sized by: six dots in a triangle */
        for (int gj = 0; gi + gj < 3; ++gj)
            rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ c.x + c.width - 8.0 * dpi - (double)gi * 4.0 * dpi,
                                           c.y + c.height - 8.0 * dpi - (double)gj * 4.0 * dpi, 2.0 * dpi, 2.0 * dpi }, 0x90F0F0F0u, 0.0);
    if (!focused) hint = T("The window has the keys  ·  click the card to type in it  ·  Esc closes it");
    rubraview_pal_rect_t tr = { c.x + pad, c.y + pad - 4.0 * dpi, c.width - 2.0 * pad, title_h };
    proven_u8str_view_t tv = proven_u8str_view_from_cstr(title);
    rubraview_pal_render_draw_text(app->renderer, tv, tr, 18.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    rubraview_pal_render_draw_text(app->renderer, tv, (rubraview_pal_rect_t){ tr.x + 0.6 * dpi, tr.y, tr.width, tr.height },
                                   18.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);   /* bold, as a title */
    rubraview_pal_render_draw_text(app->renderer, cstr(hint), (rubraview_pal_rect_t){ c.x + pad, tr.y + title_h, c.width - 2.0 * pad, hint_h },
                                   13.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
    return tr.y + title_h + hint_h + 12.0 * dpi;
}

/* A card's buttons along its foot at the right, the first lit (what Enter does). */
void draw_card_buttons(app_state_t *app, rubraview_pal_rect_t c, const char *const *words, int32_t n, rubraview_pal_rect_t *out, double dpi) {
    double bw = 104.0 * dpi, bh = 38.0 * dpi, gap = 10.0 * dpi;
    for (int32_t i = 0; i < n; ++i) {
        double tw = 0.0;
        if (rubraview_pal_render_measure_text(app->renderer, cstr(words[i]), 15.0 * dpi, &tw) && tw + 36.0 * dpi > bw) bw = tw + 36.0 * dpi;
    }
    double bx = c.x + c.width - 20.0 * dpi - (bw + gap) * (double)n + gap, by = c.y + c.height - 20.0 * dpi - bh;
    for (int32_t i = 0; i < n; ++i) {
        rubraview_pal_rect_t b = { bx + (double)i * (bw + gap), by, bw, bh };
        out[i] = b;
        bool over = pal_rect_has(b, app->pointer_x, app->pointer_y);
        double round = 5.0 * dpi;
        rubraview_pal_render_fill_rect(app->renderer, b, i == 0 ? COLOR_TILE_CURRENT : over ? 0x50FFFFFFu : COLOR_TILE_FILL, round);
        if (i == 0 && over) rubraview_pal_render_fill_rect(app->renderer, b, 0x30FFFFFFu, round);
        if (i > 0) rubraview_pal_render_stroke_rect(app->renderer, b, COLOR_BOX_BORDER, 1.0, round);
        rubraview_pal_render_draw_text(app->renderer, cstr(words[i]), b, 14.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
}

void draw_rename_box(app_state_t *app, int32_t host, double win_w, double win_h, double dpi) {
    if (!app->rename_active) return;
    {
        /* RV-238: a card — title, the line saying what the keys do, the field, the buttons — over a dimmed window;
           the open-file screen's typed path and filter stay in its path bar, Compress keeps its own window */
        bool card = !(app->rename_is_path || app->rename_is_filter || app->rename_is_pack);
        bool live = app->rename_is_find || app->rename_is_listfilter;   /* what it filters stays in sight, undimmed */
        rubraview_pal_rect_t card_rect = {0};
        bool focused = true;
        double box_w = (app->rename_is_pack ? 680.0 : 560.0) * dpi, box_h = 72.0 * dpi;   /* 1.25.0: roomier; D-260: the Compress window's rows */
        if (box_w > win_w - 16.0 * dpi) box_w = win_w - 16.0 * dpi;   /* a narrow Edit window (RV-137) */
        rubraview_pal_rect_t box = { (win_w - box_w) * 0.5, win_h * 0.75, box_w, box_h };
        if (app->rename_is_pack) {   /* D-260: the rows, the label, the box and its buttons centred together */
            double above = pack_window_height(app, fmin(box_w + 80.0 * dpi, win_w - 16.0 * dpi), dpi) + 38.0 * dpi, below = box_h + 68.0 * dpi;
            double top = (win_h - above - below) * 0.5;
            box.y = (top > 8.0 * dpi ? top : 8.0 * dpi) + above;
        }
        if (card) {
            double pad = 20.0 * dpi, title_h = 30.0 * dpi, hint_h = 22.0 * dpi, field_h = 46.0 * dpi, btn_h = 38.0 * dpi;
            double row_h = 34.0 * dpi, rows_h = app->rename_is_extension ? row_h * (double)app->ext_n + 8.0 * dpi : 0.0;
            /* RV-265 (R296): the card is where the reader put it and as large, inside the window, never under what its
               content needs; nothing under it is dimmed or held: a press there is the window's, and takes the keys */
            double min_h = pad + title_h + hint_h + 12.0 * dpi + rows_h + field_h + 18.0 * dpi + btn_h + pad;
            int32_t kind = card_kind_now(app);
            if (!app->card.open || app->card_kind != kind) {
                if (app->card.open) card_close_keep(app);
                app->card_host = host;
                app->card_host_w = win_w;
                app->card_host_h = win_h;
                card_open_in(app, kind, win_w, win_h, 600.0 * dpi, min_h, live, dpi);
            }
            app->card_host = host;
            app->card_host_w = win_w;
            app->card_host_h = win_h;
            rubraview_card_fit(&app->card, win_w, win_h, 440.0 * dpi, min_h);
            focused = app->card.focused;
            double cx = app->card.rect.x, cy = app->card.rect.y, cw = app->card.rect.width, ch = app->card.rect.height;
            card_rect = (rubraview_pal_rect_t){ cx, cy, cw, ch };
            char title[1200];
            const char *hint = "";
            rename_card_words(app, title, sizeof(title), &hint);
            double fy = draw_card_frame(app, card_rect, title, hint, focused, dpi);
            if (app->rename_is_extension) {   /* 1.24.0: the rows over the field, the one it edits lit */
                for (int32_t k = 0; k < app->ext_n; ++k) {
                    rubraview_pal_rect_t r = { cx + pad, fy + row_h * (double)k, cw - 2.0 * pad, row_h - 4.0 * dpi };
                    app->ext_row_rect[k] = r;
                    bool on = k == app->ext_at;
                    rubraview_pal_render_fill_rect(app->renderer, r, on ? COLOR_TILE_CURRENT : pal_rect_has(r, app->pointer_x, app->pointer_y) ? 0x40FFFFFFu : COLOR_TILE_FILL, 4.0 * dpi);
                    char line[128];
                    proven_u8str_view_t to = on ? rubraview_textedit_text(&app->rename_edit) : cstr(app->ext_to[k]);
                    int n = rubraview_fmt(line, sizeof(line), "{}   \xE2\x86\x92   {}", PROVEN_ARG(app->ext_from[k]), PROVEN_ARG(rubraview_bytes_view(to.ptr, (int)(to.size < 64 ? to.size : 64))));
                    if (n > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n },
                                                              (rubraview_pal_rect_t){ r.x + 14.0 * dpi, r.y, r.width - 28.0 * dpi, r.height },
                                                              15.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
                }
                fy += rows_h;
            }
            box = (rubraview_pal_rect_t){ cx + pad, fy, cw - 2.0 * pad, field_h };
        }
        if (app->rename_is_pack) {   /* RV-207: the new archive's place */
            /* RV-248: never wider than the window; its words smaller rather than cut off */
            double lw = fmin(box.width + 80.0 * dpi, win_w - 16.0 * dpi);
            rubraview_pal_rect_t label = { (win_w - lw) * 0.5, box.y - 30.0 * dpi, lw, 26.0 * dpi };
            proven_u8str_view_t into = TU8("Compress into:  its ending (.zip or .7z) says which  ·  Tab: the password  ·  Enter compresses");
            double font = 14.0 * dpi, tw = 0.0;
            if (rubraview_pal_render_measure_text(app->renderer, into, font, &tw) && tw > lw - 12.0 * dpi)
                font = fmax(10.0 * dpi, font * (lw - 12.0 * dpi) / tw);
            rubraview_pal_render_fill_rect(app->renderer, label, COLOR_BOX_FILL, 3.0);
            rubraview_pal_render_draw_text(app->renderer, into, label, font, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
            draw_pack_window(app, label, dpi);   /* D-260 */
            app->pack_field[0] = box;
            if (app->pack_focus > 0) {   /* D-261: the name stays where it is; the box types the password in its row */
                rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BOX_FILL, 3.0);
                rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_BOX_BORDER, 1.0, 3.0);
                rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->pack_name, .size = app->pack_name_len },
                                               box, 18.0 * dpi, 0xC0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
                box = app->pack_field[app->pack_focus];
            }
        }
        /* The typed path sits where the path is, as an address bar does. */
        if (app->rename_is_path || app->rename_is_filter) {
            rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ 0.0, picker_top(app), win_w, PICKER_CRUMB_HEIGHT * dpi },
                                           0xFF101010u, 0.0);   /* the chips under it do not show through */
            box = (rubraview_pal_rect_t){ 4.0 * dpi, picker_top(app) + 4.0 * dpi, win_w - 8.0 * dpi, (PICKER_CRUMB_HEIGHT - 8.0) * dpi };
        }
        if (card) {   /* RV-238: a field that looks like one: dark, lit at its edge */
            rubraview_pal_render_fill_rect(app->renderer, box, 0xFF141414u, 5.0 * dpi);
            rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_TILE_CURRENT, 1.5 * dpi, 5.0 * dpi);
        } else {
            rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BOX_FILL, 3.0);
            rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_BOX_BORDER, 1.0, 3.0);
        }
        /* The name with the syllable the IME is still building at the
           caret, the selection shaded under its characters, and a caret
           that blinks (owner, 2026-09-23: the box did not look like it was
           taking text). Widths come from the face the text is drawn in, so
           caret and shading sit under the right characters, Hangul too. */
        const rubraview_textedit_t *te = &app->rename_edit;
        char shown[3 * (sizeof(app->rename_buffer) + sizeof(app->rename_composing))];
        size_t caret = te->caret, len = te->len, comp = app->rename_composing_length;
        proven_sys_mem_copy(shown, app->rename_buffer, caret);
        proven_sys_mem_copy(shown + caret, app->rename_composing, comp);
        proven_sys_mem_copy(shown + caret + comp, app->rename_buffer + caret, len - caret);
        size_t total = len + comp;
        size_t sel_a = 0, sel_b = 0;
        bool has_selection = rubraview_textedit_has_selection(te);
        if (has_selection) rubraview_textedit_selection_range(te, &sel_a, &sel_b);
        if (app->rename_is_password && !app->password_shown) {   /* D-262: the eye shows it as typed */
            /* A dot a character (the IME's unfinished syllable too); the
               caret and the selection are counted in characters. */
            char plain[sizeof(app->rename_buffer) + sizeof(app->rename_composing)];
            proven_sys_mem_copy(plain, shown, total);
            size_t dots = 0, at_caret = 0, at_a = 0, at_b = 0;
            for (size_t i = 0; i <= total; ++i) {
                if (i == caret + comp) at_caret = dots;
                if (i == (sel_a >= caret ? sel_a + comp : sel_a)) at_a = dots;
                if (i == (sel_b >= caret ? sel_b + comp : sel_b)) at_b = dots;
                if (i < total && ((unsigned char)plain[i] & 0xC0) != 0x80) dots++;
            }
            secure_wipe(rubraview_mem_mut(plain, sizeof(plain)));
            for (size_t d = 0; d < dots; ++d) proven_sys_mem_copy(shown + 3 * d, "\xE2\x97\x8F", 3);   /* ● */
            total = 3 * dots;
            caret = 3 * at_caret;
            comp = 0;
            sel_a = 3 * at_a;
            sel_b = 3 * at_b;
        }
        double size = 18.0 * dpi, pad = 14.0 * dpi;
        double full = 0.0, to_caret = 0.0;
        (void)rubraview_pal_render_measure_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)shown, .size = total }, size, &full);
        (void)rubraview_pal_render_measure_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)shown, .size = caret + comp }, size, &to_caret);
        /* Centred while it fits; longer, it scrolls so the caret stays in the box. */
        double inner = box.width - 2.0 * pad;
        double x0 = full <= inner && !app->rename_is_path && !card ? box.x + (box.width - full) * 0.5 : box.x + pad;   /* RV-238: a card's from the left */
        if (full > inner && to_caret > inner) x0 = box.x + pad + inner - to_caret;
        if (has_selection) {
            size_t a = sel_a, b = sel_b;
            /* the composing text sits at the caret, which is one end of the selection */
            size_t da = a >= caret ? a + comp : a, db = b >= caret ? b + comp : b;
            double xa = 0.0, xb = 0.0;
            (void)rubraview_pal_render_measure_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)shown, .size = da }, size, &xa);
            (void)rubraview_pal_render_measure_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)shown, .size = db }, size, &xb);
            rubraview_pal_rect_t shade = { x0 + xa, box.y + box.height * 0.2, xb - xa, box.height * 0.6 };
            rubraview_pal_render_fill_rect(app->renderer, shade, 0xA05B9BD5u, 2.0);
        }
        rubraview_pal_rect_t line = { x0, box.y, full + 4.0 * dpi, box.height };
        if (total > 0) {
            rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)shown, .size = total },
                                           line, size, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
        }
        if (focused && fmod(rubraview_pal_time_now_seconds(), 1.0) < 0.5) {
            rubraview_pal_rect_t bar = { x0 + to_caret, box.y + box.height * 0.22, 2.0 * dpi, box.height * 0.56 };
            rubraview_pal_render_fill_rect(app->renderer, bar, COLOR_TEXT, 0.0);
        }
        /* 1.24.0: its buttons under it — what Enter does, Cancel (Esc), and Browse (Ctrl+O) for a folder */
        const char *words[4];
        int32_t kinds[4], n = 0;
        if (app->rename_is_find) {
            words[n] = T("Next"); kinds[n++] = RENAME_BTN_OK;
            words[n] = T("Back"); kinds[n++] = RENAME_BTN_BACK;
            words[n] = T("Close"); kinds[n++] = RENAME_BTN_CANCEL;
        } else {
            words[n] = app->rename_is_extension ? (app->ext_at + 1 < app->ext_n ? T("Next") : T("Apply"))
                     : app->rename_is_pack ? T("Compress")
                     : app->rename_is_extract ? T("Extract")
                     : app->rename_is_password ? (app->password_for_extract ? T("Extract") : T("Open")) : app->rename_is_filter || app->rename_is_listfilter ? T("Done") : T("OK");
            kinds[n++] = RENAME_BTN_OK;
            if (app->rename_is_extension && app->ext_at + 1 < app->ext_n) { words[n] = T("Apply"); kinds[n++] = RENAME_BTN_OK + 100; }
            if (app->rename_is_target || app->rename_is_extract) { words[n] = T("Browse..."); kinds[n++] = RENAME_BTN_BROWSE; }
            words[n] = T("Cancel"); kinds[n++] = RENAME_BTN_CANCEL;
        }
        double bw = (card ? 104.0 : 120.0) * dpi, bh = (card ? 38.0 : 48.0) * dpi, gap = 10.0 * dpi;   /* 1.25.0: roomier; RV-238: a card's */
        for (int32_t i = 0; i < n; ++i) {   /* RV-182: one width for all, the longest word's with room, never less */
            double tw = 0.0;
            if (rubraview_pal_render_measure_text(app->renderer, cstr(words[i]), 15.0 * dpi, &tw) && tw + 36.0 * dpi > bw) bw = tw + 36.0 * dpi;
        }
        rubraview_pal_rect_t at = app->rename_is_pack ? app->pack_field[0] : box;   /* D-261: under the name, whichever field is typed in */
        double bx = app->rename_is_path || app->rename_is_filter ? at.x + at.width - (bw + gap) * (double)n
                                                                   : at.x + (at.width - (bw + gap) * (double)n + gap) * 0.5;
        double by = at.y + at.height + 10.0 * dpi;
        if (card) {   /* RV-238: along the card's foot, at its right */
            bx = card_rect.x + card_rect.width - 20.0 * dpi - (bw + gap) * (double)n + gap;
            by = card_rect.y + card_rect.height - 20.0 * dpi - bh;
        }
        app->rename_btn_n = n;
        for (int32_t i = 0; i < n; ++i) {
            rubraview_pal_rect_t b = { bx + (double)i * (bw + gap), by, bw, bh };
            app->rename_btn[i] = b;
            app->rename_btn_kind[i] = kinds[i];
            bool over = pal_rect_has(b, app->pointer_x, app->pointer_y);
            double round = card ? 5.0 * dpi : 3.0;
            rubraview_pal_render_fill_rect(app->renderer, b, i == 0 ? COLOR_TILE_CURRENT : over ? 0x50FFFFFFu : card ? COLOR_TILE_FILL : COLOR_BOX_FILL, round);
            if (i == 0 && over && card) rubraview_pal_render_fill_rect(app->renderer, b, 0x30FFFFFFu, round);
            if (!card || i > 0) rubraview_pal_render_stroke_rect(app->renderer, b, COLOR_BOX_BORDER, 1.0, round);
            rubraview_pal_render_draw_text(app->renderer, cstr(words[i]), b, (card ? 14.0 : 15.0) * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
        /* D-262 (owner, 2026-11-02: "눈알 버튼을 달아서 그걸로 보면서 입력할 때는 다른 문자도 입력 가능"): an unpacking
           password's eye, at the box's right end: shown as typed, any letter through the IME; hidden, plain letters */
        if (app->rename_is_password && !app->rename_is_pack && n < 4) {
            rubraview_pal_rect_t eye = { box.x + box.width - box.height, box.y, box.height, box.height };
            bool over = pal_rect_has(eye, app->pointer_x, app->pointer_y);
            if (over || app->password_shown) rubraview_pal_render_fill_rect(app->renderer, eye, app->password_shown ? COLOR_TILE_CURRENT : 0x40FFFFFFu, 3.0);
            draw_sign(app->renderer, app->password_shown ? RUBRAVIEW_ICON_HIDE : RUBRAVIEW_ICON_VIEW, app->password_shown ? "x" : "o", eye, 20.0 * dpi, COLOR_TEXT);
            app->rename_btn[n] = eye;
            app->rename_btn_kind[n] = RENAME_BTN_EYE;
            app->rename_btn_n = n + 1;
        }
    }
}

/* The two points as text, side by side, with the one in hand marked and
   a line saying how the keys and the numbers cross over. */

void draw_ab_edit_box(app_state_t *app, double win_w, double win_h, double dpi) {
    if (!app->ab_edit_open) return;
    /* RV-267 (R296): the input card's kind — moved by its title, sized by its edges, OK and Cancel, modeless */
    rubraview_card_t *cd = &app->ab_card;
    double min_h = 122.0 * dpi + 30.0 * dpi + 18.0 * dpi + 38.0 * dpi + 20.0 * dpi;
    if (!cd->open) {
        cd->margin = 8.0 * dpi;
        cd->edge = 6.0 * dpi;
        cd->title_height = 46.0 * dpi;
        rubraview_card_open(cd, win_w, win_h, 640.0 * dpi, min_h, 0.62, &app->card_kept[CARD_REPEAT]);
    }
    rubraview_card_fit(cd, win_w, win_h, 560.0 * dpi, min_h);
    rubraview_pal_rect_t box = ab_box_rect(app);
    bool focused = cd->focused;
    (void)draw_card_frame(app, box, T("Repeat from A to B  (hours, minutes, seconds)"),
                          T("Click a field or Tab  ·  : next field  ·  \\ empties  ·  [ ] take now  ·  Enter keeps  ·  Esc leaves"), focused, dpi);
    const char *const TAKE[2] = { T("A = now  ["), T("B = now  ]") };
    for (int32_t k = 0; k < 2; ++k) {
        rubraview_pal_rect_t b = ab_box_button(app, k);
        bool over = pal_rect_has(b, app->pointer_x, app->pointer_y);
        rubraview_pal_render_fill_rect(app->renderer, b, over ? 0x50FFFFFFu : COLOR_TILE_FILL, 3.0);
        rubraview_pal_render_stroke_rect(app->renderer, b, COLOR_BOX_BORDER, 1.0, 3.0);
        rubraview_pal_render_draw_text(app->renderer, cstr(TAKE[k]), b, 14.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
    /* Each point in three fields, the one in hand lit with a caret (owner, 2026-10-15). */
    bool caret_lit = focused && fmod(rubraview_pal_time_now_seconds(), 1.0) < 0.5;
    static const char *const UNIT[3] = { "h", "m", "s" };
    for (int32_t side = 0; side < 2; ++side) {
        rubraview_pal_rect_t first = ab_part_rect(app, side, 0);
        rubraview_pal_rect_t label = { first.x - AB_LABEL_W * dpi, first.y, AB_LABEL_W * dpi, first.height };
        rubraview_pal_render_draw_text(app->renderer, side == 0 ? PROVEN_LIT("A") : PROVEN_LIT("B"), label, 18.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        for (int32_t part = 0; part < 3; ++part) {
            rubraview_pal_rect_t r = ab_part_rect(app, side, part);
            bool here = app->ab_edit_field == side && app->ab_edit_part == part;
            rubraview_pal_render_fill_rect(app->renderer, r, here ? 0xFF141414u : COLOR_TILE_FILL, 3.0);
            rubraview_pal_render_stroke_rect(app->renderer, r, here ? COLOR_TILE_CURRENT : COLOR_BOX_BORDER, here ? 2.0 : 1.0, 3.0);
            char shown[16];
            proven_u8str_view_t t = ab_part(app, side, part);
            int n = rubraview_fmt(shown, sizeof(shown), "{}{}", PROVEN_ARG(rubraview_bytes_view(t.ptr, (int)t.size)), PROVEN_ARG(here && caret_lit && !app->ab_edit_fresh ? "|" : ""));
            if (n > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)shown, .size = (size_t)n }, r, 17.0 * dpi,
                                                      COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
            rubraview_pal_rect_t unit = { r.x + r.width, r.y, AB_UNIT_W * dpi, r.height };
            rubraview_pal_render_draw_text(app->renderer, cstr(UNIT[part]), unit, 13.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
        }
    }
    const char *words[2] = { T("OK"), T("Cancel") };
    draw_card_buttons(app, box, words, 2, app->ab_btn, dpi);
}

/* RV-187 (owner, 2026-10-29: "칸마다 px/비율 전환"): under each field, its unit, pressed to switch it. */
static void px_unit_buttons(app_state_t *app, double dpi) {
    static const char *const CROP[4] = { "X", "Y", "W", "H" }, *const SIZE[2] = { "W", "H" };
    for (int32_t k = 0; k < px_edit_fields(app); ++k) {
        rubraview_pal_rect_t b = px_unit_rect(app, k);
        bool ratio = (app->px_edit_ratio & (1u << k)) != 0;
        rubraview_pal_render_fill_rect(app->renderer, b, ratio ? 0xFF2E4A6Eu : 0xFF3A3A3Au, 3.0);
        char word[64];
        int n = rubraview_fmt(word, sizeof(word), "{}: {}", PROVEN_ARG(app->px_edit_crop ? CROP[k] : SIZE[k]), PROVEN_ARG(ratio ? T("ratio") : T("pixels")));
        rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)word, .size = n > 0 ? (size_t)n : 0 },
                                       b, 13.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
}

void draw_px_edit_box(app_state_t *app, int32_t host, double win_w, double win_h, double dpi) {
    if (!app->px_edit_open) return;
    /* RV-265 (R296, RV-265.3): a card as the text box's — moved by its title, sized by its edges inside the window,
       OK and Cancel at its foot, modeless */
    rubraview_card_t *cd = &app->px_card;
    double min_h = 84.0 * dpi + 40.0 * dpi + 8.0 * dpi + 28.0 * dpi + 18.0 * dpi + 38.0 * dpi + 20.0 * dpi;
    app->px_card_host = host;
    app->px_card_host_w = win_w;
    app->px_card_host_h = win_h;
    if (!cd->open) {
        cd->margin = 8.0 * dpi;
        cd->edge = 6.0 * dpi;
        cd->title_height = 46.0 * dpi;
        rubraview_card_open(cd, win_w, win_h, 640.0 * dpi, min_h, 0.62, &app->card_kept[CARD_SIZE]);
        /* clear of an open panel (the Adjust panel the crop box comes from), the first time, when there is room */
        if (!rubraview_card_share_valid(&app->card_kept[CARD_SIZE]) && host == CARD_HOST_MAIN && app->panel.open &&
            cd->rect.x + cd->rect.width > app->panel.bounds.x - 8.0)
            cd->rect.x = app->panel.bounds.x - 8.0 - cd->rect.width;
    }
    rubraview_card_fit(cd, win_w, win_h, 480.0 * dpi, min_h);
    rubraview_pal_rect_t box = { cd->rect.x, cd->rect.y, cd->rect.width, cd->rect.height };
    bool focused = cd->focused;
    bool caret_lit = focused && fmod(rubraview_pal_time_now_seconds(), 1.0) < 0.5;
    const char *title = app->px_edit_crop ? T("The area: its corner and its size") : T("The size it is saved at");
    const char *hint = app->px_edit_crop ? T("From the top left  ·  Tab next  ·  / pixels or a ratio (0.25 = a quarter)  ·  Enter cuts  ·  Esc")
                     : app->edit.resize_lock_aspect ? T("The other side follows the shape  ·  Tab swaps  ·  / pixels or a ratio  ·  Enter keeps  ·  Esc")
                                                    : T("Tab swaps  ·  / pixels or a ratio (1.5 = half as big again)  ·  Enter keeps  ·  Esc");
    (void)draw_card_frame(app, box, title, hint, focused, dpi);
    rubraview_pal_rect_t row = px_fields_rect(app);
    rubraview_pal_render_fill_rect(app->renderer, row, 0xFF141414u, 5.0 * dpi);
    rubraview_pal_render_stroke_rect(app->renderer, row, focused ? COLOR_TILE_CURRENT : COLOR_BOX_BORDER, 1.0 * dpi, 5.0 * dpi);
    char line[160];
    int n = 0;
    if (app->px_edit_crop) {
        /* The crop: its left and top corner and its size, the field in hand in brackets. */
        static const char *const NAME[4] = { "X", "Y", "W", "H" };
        int at = 0;
        for (int32_t k = 0; k < 4 && at < (int)sizeof(line); ++k) {
            bool here = app->px_edit_field == k;
            int m = rubraview_fmt(line + at, sizeof(line) - (size_t)at, "{}{} {}{}{}{}", PROVEN_ARG(k == 0 ? "" : k == 2 ? "    " : "  "), PROVEN_ARG(NAME[k]),
                             PROVEN_ARG(here ? "[" : " "), PROVEN_ARG(rubraview_bytes_view(app->px_edit_text[k], (int)app->px_edit_length[k])), PROVEN_ARG(here ? (caret_lit ? "|]" : " ]") : " "),
                             PROVEN_ARG((app->px_edit_ratio & (1u << k)) ? "×" : ""));
            if (m < 0) break;
            at += m;
        }
        n = at > (int)sizeof(line) - 1 ? (int)sizeof(line) - 1 : at;
    } else {
        n = rubraview_fmt(line, sizeof(line), T("Width {}{}{}{}  x  Height {}{}{}{}"),
                         PROVEN_ARG(app->px_edit_field == 0 ? "[" : " "), PROVEN_ARG(rubraview_bytes_view(app->px_edit_text[0], (int)app->px_edit_length[0])),
                         PROVEN_ARG(app->px_edit_field == 0 ? (caret_lit ? "|]" : " ]") : " "), PROVEN_ARG((app->px_edit_ratio & 1u) ? "×" : ""),
                         PROVEN_ARG(app->px_edit_field == 1 ? "[" : " "), PROVEN_ARG(rubraview_bytes_view(app->px_edit_text[1], (int)app->px_edit_length[1])),
                         PROVEN_ARG(app->px_edit_field == 1 ? (caret_lit ? "|]" : " ]") : " "), PROVEN_ARG((app->px_edit_ratio & 2u) ? "×" : ""));
    }
    if (n > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n },
                                              row, (app->px_edit_crop ? 18.0 : 20.0) * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    px_unit_buttons(app, dpi);
    const char *words[2] = { T("OK"), T("Cancel") };
    draw_card_buttons(app, box, words, 2, app->px_btn, dpi);
}

/* §3.18's confirmation, the text boxes and the notices sit above
   everything else: they are answers to something the reader just did. They
   used to be drawn first, under the floating boxes, and the toolbox's
   anchor covered the start of a name being typed (VM, 2026-09-25). */
void draw_chrome_answers(app_state_t *app, double win_w, double win_h) {
    double chrome_dpi = rubraview_pal_window_dpi_scale(app->window);
    /* The equaliser is worked by hand, so it sits over the floating boxes
       (VM, 2026-10-01: the toolbox showed through it). */
    draw_eqwin(app, win_w, win_h);
    if (app->confirm_restart) {   /* RFC-0006 */
        double box_w = 680.0 * chrome_dpi, box_h = 100.0 * chrome_dpi;
        if (box_w > win_w - 16.0) box_w = win_w - 16.0;
        rubraview_pal_rect_t box = { (win_w - box_w) * 0.5, (win_h - box_h) * 0.5, box_w, box_h };
        rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BOX_FILL, 3.0);
        rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_TILE_CURRENT, 2.0, 3.0);
        rubraview_pal_render_draw_text(app->renderer,
                                       TU8("Rubraview has used much of its memory. Restart it where you are?  (Y: restart now, N: later)"),
                                       box, 15.0 * chrome_dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
    if (app->confirm_purge) {
        double box_w = 520.0 * chrome_dpi, box_h = 90.0 * chrome_dpi;
        rubraview_pal_rect_t box = { (win_w - box_w) * 0.5, (win_h - box_h) * 0.5, box_w, box_h };
        rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BOX_FILL, 3.0);
        rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_CLOSE_HOVER, 2.0, 3.0);
        rubraview_pal_render_draw_text(app->renderer,
                                       TU8("Delete this file from the disk for good?  (Y / N)"),
                                       box, 16.0 * chrome_dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
    if (app->arc_ask == ARC_ASK_DELETE) {
        /* D-88; RV-212: the page, the whole archive, or neither — three buttons, 1 / 2 / Esc */
        double box_w = 820.0 * chrome_dpi, box_h = 190.0 * chrome_dpi;
        if (box_w > win_w - 16.0) box_w = win_w - 16.0;
        rubraview_pal_rect_t box = { (win_w - box_w) * 0.5, (win_h - box_h) * 0.5, box_w, box_h };
        rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BOX_FILL, 3.0);
        rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_CLOSE_HOVER, 2.0, 3.0);
        rubraview_pal_rect_t title = { box.x, box.y + 14.0 * chrome_dpi, box_w, 30.0 * chrome_dpi };
        rubraview_pal_render_draw_text(app->renderer, TU8("Delete this page from the archive, or the whole archive?"),
                                       title, 17.0 * chrome_dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        const char *words[3] = { T("1  This page only"),
                                 app->arc_ask_purge ? T("2  The whole archive, for good") : T("2  The whole archive (Recycle Bin)"),
                                 T("Esc  Cancel") };
        int32_t kinds[3] = { ARC_DEL_PAGE, ARC_DEL_WHOLE, ARC_DEL_CANCEL };
        bool live[3] = { app->arc_ask_page_ok, app->arc_ask_whole_ok, true };
        double gap = 12.0 * chrome_dpi, bh = 52.0 * chrome_dpi, bw = (box_w - 4.0 * gap) / 3.0;
        double by = box.y + 56.0 * chrome_dpi;
        app->arc_ask_btn_n = 3;
        for (int32_t i = 0; i < 3; ++i) {
            rubraview_pal_rect_t b = { box.x + gap + (double)i * (bw + gap), by, bw, bh };
            app->arc_ask_btn[i] = b;
            app->arc_ask_btn_kind[i] = live[i] ? kinds[i] : 0;
            bool over = live[i] && pal_rect_has(b, app->pointer_x, app->pointer_y);
            uint32_t fill = !live[i] ? COLOR_BOX_FILL : i == 1 ? (over ? COLOR_CLOSE_HOVER : 0xC0802030u) : over ? 0x50FFFFFFu : i == 0 ? COLOR_TILE_CURRENT : COLOR_BOX_FILL;
            rubraview_pal_render_fill_rect(app->renderer, b, fill, 3.0);
            rubraview_pal_render_stroke_rect(app->renderer, b, COLOR_BOX_BORDER, 1.0, 3.0);
            rubraview_pal_render_draw_text(app->renderer, cstr(words[i]), b, 15.0 * chrome_dpi, live[i] ? COLOR_TEXT : 0x70F0F0F0u, RUBRAVIEW_TEXT_CENTER);
        }
        rubraview_pal_rect_t foot = { box.x, by + bh + 12.0 * chrome_dpi, box_w, 26.0 * chrome_dpi };
        proven_u8str_view_t note = app->arc_ask_why[0] ? cstr(app->arc_ask_why)
                                 : app->arc_ask_purge ? TU8("The page is taken out of the archive; the whole archive is deleted from the disk for good.")
                                                      : TU8("The page is taken out of the archive; the whole archive goes to the Recycle Bin.");
        rubraview_pal_render_draw_text(app->renderer, note, foot, 13.0 * chrome_dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
    }
    /* RV-137: a value typed, or the copies' name, asked for in the Edit window is drawn there */
    if (!(app->edit_window && edit_panel_open(app) && (app->rename_is_value || app->rename_is_apply)) &&
        !(app->batch_typing && app->batch_window)) draw_rename_box(app, CARD_HOST_MAIN, win_w, win_h, chrome_dpi);   /* RV-194: Batch's in its window */
    draw_ab_edit_box(app, win_w, win_h, chrome_dpi);
    if (!(app->edit_window && edit_panel_open(app))) draw_px_edit_box(app, CARD_HOST_MAIN, win_w, win_h, chrome_dpi);   /* RV-137: else in the Edit window */
    draw_progress_card(app, win_w, win_h, chrome_dpi);   /* RV-268 */
    draw_notice(app, win_w, win_h, chrome_dpi);
}

/* The edge buttons (owner, 2026-09-29): the revealed side's three, an
   icon or a short sign each, and the hovered one's caption beside it. */
void draw_edge_nav(app_state_t *app, double win_w, double win_h) {
    rubraview_edge_side_t side = edge_nav_side(app, app->pointer_x, app->pointer_y);
    if (side == RUBRAVIEW_EDGE_NONE || !edge_column_shown(app)) return;   /* RV-206: after the pointer stayed */
    bool media = app->media != NULL;
    /* [media][side][slot]: an icon of Segoe MDL2 Assets, or 0 and the sign. */
    static const uint32_t ICON[2][2][RUBRAVIEW_EDGE_SLOTS] = {   /* RV-179: the fourth, the folder or archive beside */
        { { 0xE76B, 0, 0xEB9E, 0xE892 }, { 0xE76C, 0, 0xEB9D, 0xE893 } },   /* RV-180: pages ◁◁ ▷▷, the folder |◁ ▷| */
        { { 0, 0, 0xEB9E, 0xE892 },      { 0, 0, 0xEB9D, 0xE893 } },
    };
    static const char *const SIGN[2][2][RUBRAVIEW_EDGE_SLOTS] = {
        { { "<", "-10", "<<", "|<" }, { ">", "+10", ">>", ">|" } },
        { { "-5s", "-5%", "<<", "|<" }, { "+5s", "+5%", ">>", ">|" } },   /* RV-206: 5 % */
    };
    rubraview_edgenav_t nav = rubraview_edgenav_create(rubraview_pal_window_dpi_scale(app->window));
    rubraview_edge_hit_t hit = rubraview_edgenav_hit(&nav, app->pointer_x, app->pointer_y, win_w, win_h);
    double opacity = box_opacity(app, &app->toolbox);
    for (int32_t slot = 0; slot < RUBRAVIEW_EDGE_SLOTS; ++slot) {
        rubraview_rect_t r = rubraview_edgenav_rect(&nav, side, slot, win_w, win_h);
        rubraview_pal_rect_t br = { r.x, r.y, r.width, r.height };
        bool hovered = hit.side == side && hit.slot == slot;
        rubraview_pal_render_fill_rect(app->renderer, br, rubraview_box_fade(hovered ? COLOR_TILE_FILL : COLOR_BOX_FILL, opacity), 4.0);
        rubraview_pal_render_stroke_rect(app->renderer, br, COLOR_BOX_BORDER, 1.0, 4.0);
        /* RV-206: a tall button: its sign in the upper part, its words under it */
        uint32_t icon = ICON[media][side][slot];
        rubraview_pal_rect_t sign = { br.x, br.y + br.height * 0.5 - r.width * 0.55, br.width, r.width * 0.6 };
        if (!icon || !rubraview_pal_render_draw_icon(app->renderer, icon, sign, r.width * 0.32, COLOR_TEXT)) {
            rubraview_pal_render_draw_text(app->renderer, cstr(SIGN[media][side][slot]), sign, r.width * 0.24,
                                           COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
        if (hovered) {
            /* RV-206: sized by the button's width, the buttons being tall now; RV-179: the longer caption */
            double ch = r.width * 0.6, cw = r.width * (slot == 3 ? 4.6 : 3.2);
            rubraview_pal_rect_t cap = { side == RUBRAVIEW_EDGE_LEFT ? r.x + r.width + nav.gap : r.x - nav.gap - cw,
                                         r.y + (r.height - ch) * 0.5, cw, ch };
            rubraview_pal_render_fill_rect(app->renderer, cap, rubraview_box_fade(COLOR_BOX_FILL, opacity), 3.0);
            rubraview_pal_render_draw_text(app->renderer, cstr(rubraview_edgenav_caption(side, slot, media)), cap,
                                           ch * 0.45, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
    }
}

/* The playlist window: a title saying what the list is and where the file
   on screen is in it, then a row a file, that one marked. See-through as
   the toolbox is. */
/* A button of the list window: lit under the pointer, or when `on`. */
/* A small button of a list, drawn by `r` with the pointer at (px, py) in its window. */
void list_button(rubraview_renderer_t *rr, double px, double py, rubraview_rect_t r, const char *label, double size,
                        bool on, uint32_t color) {
    if (r.width <= 0.0) return;
    rubraview_pal_rect_t box = { r.x + 1.0, r.y + 1.0, r.width - 2.0, r.height - 2.0 };
    if (on) rubraview_pal_render_fill_rect(rr, box, (COLOR_TILE_CURRENT & 0x00FFFFFFu) | 0xC0000000u, 2.0);
    else if (rubraview_rect_contains(r, px, py)) rubraview_pal_render_fill_rect(rr, box, COLOR_TILE_FILL, 2.0);
    rubraview_pal_render_draw_text(rr, cstr(label), box, size, color, RUBRAVIEW_TEXT_CENTER);
}

/* RV-181: a list window's button that shows an icon (its letter where the font is missing). */
void listwin_sign_button(app_state_t *app, rubraview_renderer_t *rr, rubraview_rect_t r, uint32_t icon, const char *letter, double size, bool on) {
    if (r.width <= 0.0) return;
    rubraview_pal_rect_t box = { r.x + 1.0, r.y + 1.0, r.width - 2.0, r.height - 2.0 };
    if (on) rubraview_pal_render_fill_rect(rr, box, (COLOR_TILE_CURRENT & 0x00FFFFFFu) | 0xC0000000u, 2.0);
    else if (rubraview_rect_contains(r, app->pointer_x, app->pointer_y)) rubraview_pal_render_fill_rect(rr, box, COLOR_TILE_FILL, 2.0);
    draw_sign(rr, icon, letter, box, size, COLOR_TEXT);
}

void listwin_button(app_state_t *app, rubraview_rect_t r, const char *label, double size, bool on, uint32_t color) {
    list_button(app->renderer, app->pointer_x, app->pointer_y, r, label, size, on, color);
}

void list_check_mark(rubraview_renderer_t *rr, rubraview_rect_t c, bool every, bool some) {
    double side = c.width < c.height ? c.width : c.height;
    double inset = side * 0.25;
    rubraview_pal_rect_t mark = { c.x + (c.width - side) * 0.5 + inset, c.y + (c.height - side) * 0.5 + inset,
                                  side - inset * 2.0, side - inset * 2.0 };
    if (every) rubraview_pal_render_fill_rect(rr, mark, 0xFF5B9BD5u, 2.0);
    else if (some) rubraview_pal_render_fill_rect(rr, (rubraview_pal_rect_t){ mark.x + mark.width * 0.2, mark.y + mark.height * 0.4,
                                                                            mark.width * 0.6, mark.height * 0.2 }, 0xFF5B9BD5u, 1.0);
    rubraview_pal_render_stroke_rect(rr, mark, COLOR_TEXT, 1.0, 2.0);
}

/* RV-117: what both lists have when they can be changed — the bar (the box
   for all, Add, Del, Save, Load, Rel/Abs) ... */
void draw_list_bar(app_state_t *app, rubraview_renderer_t *rr, const rubraview_listwin_t *lw, double px, double py) {
    double small = lw->row_height * 0.5 * place_size(app, PF_LIST);   /* 1.25.0 */
    char del[40];
    size_t checked = list_checked_total(app);
    if (checked > 0) rubraview_fmt(del, sizeof(del), T("Del ({})"), PROVEN_ARG((size_t)checked)); else rubraview_fmt(del, sizeof(del), T("Del"));
    list_check_mark(rr, rubraview_listwin_bar_rect(lw, RUBRAVIEW_LISTWIN_ALL),
                    app->list_checked_count > 0 && checked == app->list_checked_count, checked > 0);
    list_button(rr, px, py, rubraview_listwin_bar_rect(lw, RUBRAVIEW_LISTWIN_ADD), T("Add"), small, false, COLOR_TEXT);
    list_button(rr, px, py, rubraview_listwin_bar_rect(lw, RUBRAVIEW_LISTWIN_URL), T("URL"), small, false, COLOR_TEXT);   /* RV-158 */
    list_button(rr, px, py, rubraview_listwin_bar_rect(lw, RUBRAVIEW_LISTWIN_DEL), del, small, false, checked > 0 ? COLOR_TEXT : COLOR_BOX_BORDER);
    list_button(rr, px, py, rubraview_listwin_bar_rect(lw, RUBRAVIEW_LISTWIN_SAVE), T("Save"), small, false, COLOR_TEXT);
    list_button(rr, px, py, rubraview_listwin_bar_rect(lw, RUBRAVIEW_LISTWIN_LOAD), T("Load"), small, false, COLOR_TEXT);
    list_button(rr, px, py, rubraview_listwin_bar_rect(lw, RUBRAVIEW_LISTWIN_PATHS), list_saves_whole_paths(app) ? T("Abs") : T("Rel"),
                small, false, COLOR_TEXT);
    /* the owner (2026-10-18): Edit's steps onto all of them, as copies; lit while Edit has a step */
    list_button(rr, px, py, rubraview_listwin_bar_rect(lw, RUBRAVIEW_LISTWIN_APPLY), T("Edit to all"), small, false,
                edit_panel_open(app) && app->edit_steps.cursor > 0 ? COLOR_TEXT : COLOR_BOX_BORDER);
    double head_h = rubraview_listwin_head_height(lw);
    rubraview_pal_rect_t rule = { lw->x + 2.0, lw->y + head_h - 1.0, lw->width - 4.0, 1.0 };
    rubraview_pal_render_fill_rect(rr, rule, COLOR_BOX_BORDER, 0.0);
}

/* 1.25.0: ... the tools row under it: .ext, the filter (its words, a caret while typed in
   the file window, an X to clear it), the sort and the order ... */
void draw_list_tools(app_state_t *app, rubraview_renderer_t *rr, const rubraview_listwin_t *lw, double px, double py, bool in_files) {
    if (!lw->editable || !lw->tools) return;
    double small = lw->row_height * 0.5 * place_size(app, PF_LIST);
    char ext[32];
    if (app->list_ext_off_n > 0) rubraview_fmt(ext, sizeof(ext), ".ext (-{})", PROVEN_ARG((size_t)app->list_ext_off_n)); else rubraview_fmt(ext, sizeof(ext), ".ext");
    list_button(rr, px, py, rubraview_listwin_tool_rect(lw, RUBRAVIEW_LISTWIN_EXT), ext, small, app->list_ext_off_n > 0, COLOR_TEXT);
    char shape[48];   /* RV-192 */
    rubraview_fmt(shape, sizeof(shape), "{} \xE2\x96\xBE", PROVEN_ARG(T("Shape")));
    list_button(rr, px, py, rubraview_listwin_tool_rect(lw, RUBRAVIEW_LISTWIN_SHAPE), shape, small, false, COLOR_TEXT);
    rubraview_rect_t f = rubraview_listwin_tool_rect(lw, RUBRAVIEW_LISTWIN_FILTER);
    if (f.width > 0.0) {
        rubraview_pal_rect_t box = { f.x + 2.0, f.y + 2.0, f.width - 4.0, f.height - 4.0 };
        bool typing = in_files ? app->list_filter_in_files : (app->rename_active && app->rename_is_listfilter);
        rubraview_pal_render_fill_rect(rr, box, 0xFF161616u, 2.0);
        rubraview_pal_render_stroke_rect(rr, box, typing ? 0xFF5B9BD5u : COLOR_BOX_BORDER, 1.0, 2.0);
        char words[160];
        bool empty = app->list_filter[0] == '\0';
        rubraview_fmt(words, sizeof(words), "{}{}", PROVEN_ARG(empty && !typing ? T("Filter") : app->list_filter), PROVEN_ARG(typing && in_files ? "|" : ""));
        rubraview_pal_rect_t inner = { box.x + 6.0, box.y, box.width - 12.0 - (empty ? 0.0 : box.height), box.height };
        rubraview_pal_render_draw_text(rr, cstr(words), inner, small, empty && !typing ? 0x90F0F0F0u : COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
        if (!empty) list_button(rr, px, py, (rubraview_rect_t){ f.x + f.width - f.height, f.y, f.height, f.height }, "\xC3\x97", small, false, COLOR_TEXT);
    }
    list_button(rr, px, py, rubraview_listwin_tool_rect(lw, RUBRAVIEW_LISTWIN_SORT), list_sort_word(app->list_sort_key), small,
                app->list_sort_key > 0, COLOR_TEXT);
    list_button(rr, px, py, rubraview_listwin_tool_rect(lw, RUBRAVIEW_LISTWIN_ORDER),
                app->list_sort_desc ? "\xE2\x96\xBC" : "\xE2\x96\xB2", small, false, COLOR_TEXT);
}

/* ... the four buttons at a row's left ... */
void draw_list_row_parts(app_state_t *app, rubraview_renderer_t *rr, const rubraview_listwin_t *lw, size_t i, size_t count,
                                double px, double py, size_t page) {
    double small = lw->row_height * 0.5 * place_size(app, PF_LIST);   /* 1.25.0 */
    bool checked = page < app->list_checked_count && app->list_checked[page];
    bool moves = !list_viewed(app);   /* 1.25.0: a filtered list's rows stay */
    list_check_mark(rr, rubraview_listwin_row_part_rect(lw, i, count, RUBRAVIEW_LISTWIN_ROW_CHECK), checked, false);
    list_button(rr, px, py, rubraview_listwin_row_part_rect(lw, i, count, RUBRAVIEW_LISTWIN_ROW_REMOVE), "x", small, false, COLOR_TEXT);
    list_button(rr, px, py, rubraview_listwin_row_part_rect(lw, i, count, RUBRAVIEW_LISTWIN_ROW_UP), "\xE2\x96\xB2", small * 0.8, false,
                moves && i > 0 ? COLOR_TEXT : COLOR_BOX_BORDER);
    list_button(rr, px, py, rubraview_listwin_row_part_rect(lw, i, count, RUBRAVIEW_LISTWIN_ROW_DOWN), "\xE2\x96\xBC", small * 0.8, false,
                moves && i + 1 < count ? COLOR_TEXT : COLOR_BOX_BORDER);
}

/* ... and, while a row is carried, a line where it would go. */
void draw_list_drop_line(app_state_t *app, rubraview_renderer_t *rr, const rubraview_listwin_t *lw, bool in_files) {
    size_t rows = rubraview_listwin_rows_visible(lw);
    if (!app->list_row_dragging || app->list_held_in_files != in_files) return;
    if (app->list_drop_at < lw->first || app->list_drop_at > lw->first + rows) return;
    rubraview_pal_rect_t line = { lw->x + 4.0, lw->y + rubraview_listwin_head_height(lw) + (double)(app->list_drop_at - lw->first) * lw->row_height - 1.0,
                                  lw->width - 8.0, 2.0 };
    rubraview_pal_render_fill_rect(rr, line, 0xFF5B9BD5u, 0.0);
}

/* 1.25.0 (owner, 2026-10-20: "재생목록 위에 커서를 대면 해당 파일의 썸네일이 보이게"): the row pointed at,
   as a picture beside the list — the page itself when it is in memory, else its strip thumbnail. */
static void draw_list_tip(app_state_t *app, const rubraview_listwin_t *lw, size_t page, double win_w, double win_h) {
    app->list_tip_page = page < page_count(app) ? (int32_t)page : -1;
    if (app->list_tip_page < 0) return;
    rubraview_texture_t *tex = NULL;
    if (app->pages && app->pages[page].texture && app->pages[page].loaded) tex = app->pages[page].texture;
    else if (page < app->strip_cell_count && app->strip_cells[page].texture) tex = app->strip_cells[page].texture;
    if (!tex) return;
    int32_t tw = 0, th = 0;
    rubraview_pal_texture_size(tex, &tw, &th);
    if (tw <= 0 || th <= 0) return;
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double side = 220.0 * dpi, k = side / (double)(tw > th ? tw : th);
    double w = (double)tw * k, h = (double)th * k;
    double x = lw->x - w - 10.0 * dpi;
    if (x < 4.0 * dpi) x = lw->x + lw->width + 10.0 * dpi;
    if (x + w > win_w) x = win_w - w - 4.0 * dpi;
    double y = app->pointer_y - h * 0.5;
    if (y + h > win_h - 4.0 * dpi) y = win_h - h - 4.0 * dpi;
    if (y < 4.0 * dpi) y = 4.0 * dpi;
    rubraview_pal_rect_t frame = { x - 3.0 * dpi, y - 3.0 * dpi, w + 6.0 * dpi, h + 6.0 * dpi };
    rubraview_pal_render_fill_rect(app->renderer, frame, 0xF0202020u, 4.0 * dpi);
    rubraview_pal_render_stroke_rect(app->renderer, frame, COLOR_BOX_BORDER, 1.0, 4.0 * dpi);
    rubraview_mat3x2_t place = rubraview_mat3x2_multiply(rubraview_mat3x2_scale(w / (double)tw, h / (double)th), rubraview_mat3x2_translate(x, y));
    rubraview_pal_render_draw_texture(app->renderer, tex, place, RUBRAVIEW_INTERP_LINEAR);
}

/* 1.25.0: in its place's face (Settings › General) */
static void draw_listwin_in(app_state_t *app, double win_w, double win_h);
void draw_listwin(app_state_t *app, double win_w, double win_h) {
    int before = place_font_begin(app, PF_LIST, NULL);
    draw_listwin_in(app, win_w, win_h);
    place_font_end(app, before);
}

static void draw_listwin_in(app_state_t *app, double win_w, double win_h) {
    rubraview_listwin_t *lw = &app->listwin;
    app->list_tip_page = -1;
    if (!lw->open) return;
    listwin_sync(app);
    size_t count = list_rows(app);   /* 1.25.0: the rows shown */
    listwin_place_clear(app, win_w, win_h);
    int32_t current = current_page_index(app);
    if (current != app->listwin_shown_page && current >= 0) {   /* follow the file on screen */
        size_t at = list_row_of(app, (size_t)current);
        if (at != SIZE_MAX) rubraview_listwin_reveal(lw, at, count);
        app->listwin_shown_page = current;
    }
    double opacity = box_opacity(app, &app->toolbox);
    rubraview_pal_rect_t body = { lw->x, lw->y, lw->width, lw->height };
    /* See-through (owner: "반투명 플로팅"): the picture shows under the list. */
    rubraview_pal_render_fill_rect(app->renderer, body, rubraview_box_fade(COLOR_BOX_FILL, opacity * 0.75), 4.0);
    rubraview_pal_render_stroke_rect(app->renderer, body, COLOR_BOX_BORDER, 1.0, 4.0);

    const char *what = app->list_is_set ? T("Playlist") : app->source.archive_path.size > 0 ? T("In this archive") : T("In this folder");
    char title[96];
    int n = current >= 0 ? rubraview_fmt(title, sizeof(title), "{}   {} / {}", PROVEN_ARG(what), PROVEN_ARG((int)(current + 1)), PROVEN_ARG((size_t)(page_count(app))))
                         : rubraview_fmt(title, sizeof(title), "{}   {}", PROVEN_ARG(what), PROVEN_ARG((size_t)(page_count(app))));
    if (n > 0 && list_viewed(app) && (size_t)n < sizeof(title))
        n += rubraview_fmt(title + n, sizeof(title) - (size_t)n, T("   ({} shown)"), PROVEN_ARG((size_t)count));
    if (n >= (int)sizeof(title)) n = (int)sizeof(title) - 1;
    rubraview_pal_rect_t head = { lw->x + lw->row_height * 0.4, lw->y, rubraview_listwin_title_text_right(lw) - lw->x - lw->row_height * 0.4, lw->title_height };
    if (n > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ (const proven_byte_t *)title, (size_t)n }, head, lw->title_height * 0.45,
                                              COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    rubraview_rect_t cr = rubraview_listwin_close_rect(lw);
    rubraview_pal_rect_t close = { cr.x, cr.y, cr.width, cr.height };
    if (rubraview_rect_contains(cr, app->pointer_x, app->pointer_y)) {
        rubraview_pal_render_fill_rect(app->renderer, close, COLOR_CLOSE_HOVER, 4.0);
    }
    draw_sign(app->renderer, RUBRAVIEW_ICON_CLOSE, "X", close, lw->title_height * 0.4, COLOR_TEXT);
    /* R sizes the window when dragged, in a book's list too (RV-198); RV-197 maximizes it. */
    listwin_sign_button(app, app->renderer, rubraview_listwin_resize_rect(lw), RUBRAVIEW_ICON_RESIZE, "R", lw->title_height * 0.4, lw->resizing);
    draw_listwin_maximize(app, app->renderer, lw);

    double small = lw->row_height * 0.5 * place_size(app, PF_LIST);   /* 1.25.0 */
    if (lw->editable) {
        /* D-111: the bar acts on the list. */
        draw_list_bar(app, app->renderer, lw, app->pointer_x, app->pointer_y);
        draw_list_tools(app, app->renderer, lw, app->pointer_x, app->pointer_y, false);
    }

    size_t hovered = SIZE_MAX, index = 0;
    rubraview_listwin_part_t under = rubraview_listwin_hit(lw, app->pointer_x, app->pointer_y, count, &index);
    if (under == RUBRAVIEW_LISTWIN_ROW || (under >= RUBRAVIEW_LISTWIN_ROW_CHECK && under <= RUBRAVIEW_LISTWIN_ROW_DOWN)) hovered = index;
    size_t rows = rubraview_listwin_rows_visible(lw);
    double text_x = rubraview_listwin_row_text_x(lw);
    for (size_t i = lw->first; i < count && i < lw->first + rows; ++i) {
        size_t page = list_page(app, i);
        if (page == SIZE_MAX) break;
        rubraview_rect_t rr = rubraview_listwin_row_rect(lw, i, count);
        rubraview_pal_rect_t row = { rr.x + 2.0, rr.y, rr.width - 4.0, rr.height };
        bool checked = page < app->list_checked_count && app->list_checked[page];
        bool carried = app->list_row_dragging && (page == app->list_press_row ||
                       (checked && app->list_press_row < app->list_checked_count && app->list_checked[app->list_press_row]));
        if ((int32_t)page == current) rubraview_pal_render_fill_rect(app->renderer, row, (COLOR_TILE_CURRENT & 0x00FFFFFFu) | 0x90000000u, 2.0);
        else if (carried) rubraview_pal_render_fill_rect(app->renderer, row, 0x405B9BD5u, 2.0);
        else if (i == hovered) rubraview_pal_render_fill_rect(app->renderer, row, COLOR_TILE_FILL, 2.0);
        if (lw->editable) draw_list_row_parts(app, app->renderer, lw, i, count, app->pointer_x, app->pointer_y, page);
        char line[512], fit[512];
        proven_u8str_view_t name = page_display_name(app, page);
        int m = rubraview_fmt(line, sizeof(line), "{:4}   {}", PROVEN_ARG((size_t)(page + 1)), PROVEN_ARG(rubraview_bytes_view(name.ptr, (int)(name.size < 480 ? name.size : 480))));
        rubraview_pal_rect_t text = { text_x + lw->row_height * 0.3, row.y, lw->x + lw->width - text_x - lw->row_height * 0.6, row.height };
        if (m > 0) rubraview_pal_render_draw_text(app->renderer,
                                                  files_fit_text(fit, sizeof(fit), (proven_u8str_view_t){ (const proven_byte_t *)line, (size_t)(m < (int)sizeof(line) ? m : (int)sizeof(line) - 1) },
                                                                 text.width, small),
                                                  text, small, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    }
    draw_list_drop_line(app, app->renderer, lw, false);   /* a row being carried */
    /* A bar says where in a long list the rows are; RV-170: dragged, pressed or by its buttons it moves them. */
    draw_scrollbar(app, app->renderer, RVSB_LIST, app->pointer_x, app->pointer_y);
    if (hovered != SIZE_MAX && !app->list_row_dragging && !app->chooser.open) draw_list_tip(app, lw, list_page(app, hovered), win_w, win_h);
}

void draw_eqwin(app_state_t *app, double win_w, double win_h) {
    rubraview_eqwin_t *w = &app->eqwin;
    if (!w->open) return;
    rubraview_eqwin_place(w, win_w, win_h);
    rubraview_pal_rect_t body = { w->x, w->y, w->width, w->height };
    rubraview_pal_render_fill_rect(app->renderer, body, (COLOR_BOX_FILL & 0x00FFFFFFu) | 0xF0000000u, 4.0);
    rubraview_pal_render_stroke_rect(app->renderer, body, COLOR_BOX_BORDER, 1.0, 4.0);
    double pad = w->label_height * 0.5;

    int preset = eq_preset_now(app);
    proven_u8str_view_t name = eq_preset_label(preset);
    char title[96];
    int n = rubraview_fmt(title, sizeof(title), T("Equaliser   {}"), PROVEN_ARG(rubraview_bytes_view(name.ptr, (int)name.size)));
    rubraview_pal_rect_t head = { w->x + pad, w->y, w->width - w->title_height - pad, w->title_height };
    if (n > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ (const proven_byte_t *)title, (size_t)n }, head, w->title_height * 0.45,
                                              COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    rubraview_rect_t cr = rubraview_eqwin_close_rect(w);
    rubraview_pal_rect_t close = { cr.x, cr.y, cr.width, cr.height };
    if (rubraview_rect_contains(cr, app->pointer_x, app->pointer_y)) {
        rubraview_pal_render_fill_rect(app->renderer, close, COLOR_CLOSE_HOVER, 4.0);
    }
    draw_sign(app->renderer, RUBRAVIEW_ICON_CLOSE, "X", close, w->title_height * 0.4, COLOR_TEXT);

    for (int p = 0; p < RUBRAVIEW_EQWIN_PRESETS; ++p) {
        rubraview_rect_t r = rubraview_eqwin_preset_rect(w, (size_t)p);
        rubraview_pal_rect_t chip = { r.x, r.y, r.width, r.height };
        bool hover = rubraview_rect_contains(r, app->pointer_x, app->pointer_y);
        uint32_t fill = p == preset ? ((COLOR_TILE_CURRENT & 0x00FFFFFFu) | 0xC0000000u) : hover ? COLOR_TILE_FILL : 0x40202020u;
        rubraview_pal_render_fill_rect(app->renderer, chip, fill, 3.0);
        rubraview_pal_render_draw_text(app->renderer, eq_preset_label(p), chip, w->preset_height * 0.36,
                                       COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }

    float gains[RUBRAVIEW_EQ_BANDS];
    eq_shown_gains(app, gains);
    const double *freqs = rubraview_eq_frequencies();
    for (size_t b = 0; b < RUBRAVIEW_EQ_BANDS; ++b) {
        rubraview_rect_t col = rubraview_eqwin_band_rect(w, b);
        rubraview_rect_t t = rubraview_eqwin_track_rect(w, b);
        double cx = t.x + t.width * 0.5;
        /* the track, the 0 dB line across it, and the knob */
        rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ cx - 2.0, t.y, 4.0, t.height }, 0x80A0A0A0u, 2.0);
        double zero = rubraview_eqwin_y_of(w, b, 0.0);
        rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ cx - t.width * 0.25, zero - 0.5, t.width * 0.5, 1.0 }, 0x60F0F0F0u, 0.0);
        double ky = rubraview_eqwin_y_of(w, b, gains[b]);
        double kw = t.width * 0.5, kh = w->label_height * 0.5;
        bool held = app->eqwin.sliding == (int)b;
        bool hover = rubraview_rect_contains(col, app->pointer_x, app->pointer_y);
        uint32_t knob = held ? COLOR_TILE_CURRENT : hover ? 0xFFFFFFFFu : 0xFFD0D0D0u;
        rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ cx - kw * 0.5, ky - kh * 0.5, kw, kh }, knob, 3.0);
        char v[16];
        int m = rubraview_fmt(v, sizeof(v), "{:+.1}", PROVEN_ARG((double)gains[b]));
        rubraview_pal_rect_t top = { col.x, col.y, col.width, w->label_height };
        if (m > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ (const proven_byte_t *)v, (size_t)m }, top, w->label_height * 0.55,
                                                  COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        char f[16];
        m = freqs[b] >= 1000.0 ? rubraview_fmt(f, sizeof(f), "{}k", PROVEN_ARG(rubraview_g_text((char[32]){0}, (double)(freqs[b] / 1000.0), 6))) : rubraview_fmt(f, sizeof(f), "{}", PROVEN_ARG(rubraview_g_text((char[32]){0}, (double)(freqs[b]), 6)));
        rubraview_pal_rect_t bottom = { col.x, col.y + col.height - w->label_height, col.width, w->label_height };
        if (m > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ (const proven_byte_t *)f, (size_t)m }, bottom, w->label_height * 0.55,
                                                  0xC0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
    }
}

/* Hover titlebar (§3.21.2, §3.21.3). Over the viewer it names the file;
   over the picker, the folder. */
/* 1.25.0 (owner, 2026-10-20: "UI에 쓰이는 여러 글꼴의 종류와 크기 등을 설정"): the face and size of a place's words,
   from Settings › General. begin sets the place's face and gives its size (a factor); end puts back the one before. */
static int g_place = PF_NONE;
static void place_font_apply(app_state_t *app, int which) {
    proven_u8str_view_t f = which == PF_TITLE ? rubraview_settings_get_text(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("font_titlebar"))
              : which == PF_MENU ? rubraview_settings_get_text(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("font_menu"))
              : which == PF_NAME ? rubraview_settings_get_text(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("font_name"))
              : which == PF_LIST ? rubraview_settings_get_text(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("font_list"))
              : which == PF_BUTTONS ? rubraview_settings_get_text(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("font_buttons")) : PROVEN_LIT("");
    rubraview_font_spec_t face = rubraview_font_spec_parse(f);
    rubraview_pal_render_set_text_font(app->renderer, face.family, face.weight, face.italic);
}

double place_size(app_state_t *app, int which) {
    double v = which == PF_TITLE ? rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("size_titlebar"))
             : which == PF_MENU ? rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("size_menu"))
             : which == PF_LIST ? rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("size_list"))
             : which == PF_BUTTONS ? rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("size_buttons")) : 100.0;
    if (which == PF_NAME) return name_scale(app);
    return v >= 60.0 && v <= 200.0 ? v / 100.0 : 1.0;
}

int place_font_begin(app_state_t *app, int which, double *size) {
    int before = g_place;
    g_place = which;
    place_font_apply(app, which);
    if (size) *size = place_size(app, which);
    return before;
}

void place_font_end(app_state_t *app, int before) {
    g_place = before;
    place_font_apply(app, before);
}

/* RV-181: a button's icon, at `size` (a share of its height), or its letter where the icon font is missing. */
void draw_sign(rubraview_renderer_t *r, uint32_t icon, const char *letter, rubraview_pal_rect_t box, double size, uint32_t argb) {
    if (icon && rubraview_pal_render_draw_icon(r, icon, box, size, argb)) return;
    rubraview_pal_render_draw_text(r, cstr(letter), box, size, argb, RUBRAVIEW_TEXT_CENTER);
}

/* 1.25.0: words drawn bold (twice, a hair apart) for what is on, plain for what is off. */
void draw_text_weight(app_state_t *app, proven_u8str_view_t text, rubraview_pal_rect_t r, double size, uint32_t argb, bool bold) {
    rubraview_pal_render_draw_text(app->renderer, text, r, size, argb, RUBRAVIEW_TEXT_CENTER);
    if (bold) rubraview_pal_render_draw_text(app->renderer, text, (rubraview_pal_rect_t){ r.x + size * 0.06, r.y, r.width, r.height },
                                             size, argb, RUBRAVIEW_TEXT_CENTER);
}

/* ---- RV-237 (owner, 2026-11-07): the volume's bar ----
   A press on the Toolbar's volume button opens a bar by it: the level along it (pressed or dragged), the percent at
   its right end, which a press turns into a box to type the percent in. The wheel over the button sets the volume
   at once and shows only the percent by the button for a moment. */
int volume_percent(const app_state_t *app) {
    return (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("volume")));
}

void volume_set(app_state_t *app, double percent) {
    if (percent < 0.0) percent = 0.0;
    if (percent > 100.0) percent = 100.0;
    rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("volume"), percent);
    rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("mute"), 0.0);   /* as Up and Down: a level set unmutes */
    settings_took_effect(app);
    note_activity(app);
}

static rubraview_rect_t volpop_frame(app_state_t *app) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    return rubraview_volpop_frame(app->volpop_button, (double)w, (double)h, rubraview_pal_window_dpi_scale(app->window));
}

void volpop_open_at(app_state_t *app, rubraview_rect_t button) {
    ctx_close(app);
    slidepop_close(app);
    app->volpop_open = true;
    app->volpop_drag = false;
    app->volpop_button = button;
}

void volpop_open_main(app_state_t *app) {   /* from a key, the menu or a Toolbar of its own: by the window's foot */
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    double s = rubraview_pal_window_dpi_scale(app->window);
    volpop_open_at(app, (rubraview_rect_t){ (double)w * 0.5 - 18.0 * s, (double)h - 80.0 * s, 36.0 * s, 36.0 * s });
}

void volpop_close(app_state_t *app) {
    app->volpop_open = app->volpop_drag = false;
}

void draw_volpop(app_state_t *app) {
    double s = rubraview_pal_window_dpi_scale(app->window);
    rubraview_renderer_t *r = app->renderer;
    if (rubraview_pal_time_now_seconds() < app->vol_flash_until) {   /* the wheel's percent, by the button */
        char line[16];
        int n = rubraview_fmt(line, sizeof(line), "{}%", PROVEN_ARG(volume_percent(app)));
        rubraview_rect_t b = app->vol_flash_at;
        rubraview_pal_rect_t box = { b.x + b.width * 0.5 - 30.0 * s, b.y - 34.0 * s, 60.0 * s, 28.0 * s };
        if (box.y < 0.0) box.y = b.y + b.height + 6.0 * s;
        if (box.x < 0.0) box.x = 0.0;
        rubraview_pal_render_fill_rect(r, box, 0xF0303030u, 3.0 * s);
        rubraview_pal_render_stroke_rect(r, box, COLOR_BOX_BORDER, 1.0 * s, 3.0 * s);
        rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = n > 0 ? (size_t)n : 0 },
                                       box, 14.0 * s, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
    if (!app->volpop_open) return;
    rubraview_rect_t f = volpop_frame(app);
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ f.x, f.y, f.width, f.height }, 0xF6202020u, 5.0 * s);
    rubraview_pal_render_stroke_rect(r, (rubraview_pal_rect_t){ f.x, f.y, f.width, f.height }, COLOR_TILE_CURRENT, 1.5 * s, 5.0 * s);
    rubraview_rect_t bar = rubraview_volpop_part(f, s, RUBRAVIEW_VOLPOP_BAR);
    double level = (double)volume_percent(app) / 100.0;
    bool muted = rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("mute")) > 0.5;
    double mid = bar.y + bar.height * 0.5, track = 6.0 * s;
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ bar.x, mid - track * 0.5, bar.width, track }, 0x40FFFFFFu, track * 0.5);
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ bar.x, mid - track * 0.5, bar.width * level, track },
                                   muted ? 0xFF808080u : COLOR_TILE_CURRENT, track * 0.5);
    double knob = 14.0 * s;
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ bar.x + bar.width * level - knob * 0.5, mid - knob * 0.5, knob, knob }, COLOR_TEXT, knob * 0.5);
    rubraview_rect_t fr = rubraview_volpop_part(f, s, RUBRAVIEW_VOLPOP_FIELD);
    rubraview_pal_rect_t field = { fr.x, fr.y, fr.width, fr.height };
    char line[16];
    int nl = rubraview_fmt(line, sizeof(line), "{}%", PROVEN_ARG(volume_percent(app)));
    proven_u8str_view_t text = { .ptr = (const proven_byte_t *)line, .size = nl > 0 ? (size_t)nl : 0 };
    bool hover = rubraview_rect_contains(fr, app->pointer_x, app->pointer_y);
    rubraview_pal_render_fill_rect(r, field, hover ? 0x50FFFFFFu : 0x28FFFFFFu, 3.0 * s);
    rubraview_pal_render_stroke_rect(r, field, COLOR_BOX_BORDER, 1.0 * s, 3.0 * s);
    rubraview_pal_render_draw_text(r, text, field, 14.0 * s, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
}

bool volpop_press(app_state_t *app, double x, double y) {
    if (!app->volpop_open) return false;
    double s = rubraview_pal_window_dpi_scale(app->window);
    rubraview_rect_t f = volpop_frame(app);
    rubraview_volpop_part_t part = rubraview_volpop_part_at(f, s, x, y);
    if (part == RUBRAVIEW_VOLPOP_FIELD) { number_card_begin(app, NUMBER_VOLUME); return true; }   /* RV-267: in the input card */
    if (part == RUBRAVIEW_VOLPOP_BAR) {
        app->volpop_drag = true;
        volume_set(app, rubraview_volpop_value_at(rubraview_volpop_part(f, s, RUBRAVIEW_VOLPOP_BAR), x));
        return true;
    }
    if (!rubraview_rect_contains(f, x, y)) volpop_close(app);   /* a press away closes it, and is spent */
    return true;
}

bool volpop_motion(app_state_t *app, double x, double y) {
    (void)y;
    if (!app->volpop_open || !app->volpop_drag) return false;
    double s = rubraview_pal_window_dpi_scale(app->window);
    volume_set(app, rubraview_volpop_value_at(rubraview_volpop_part(volpop_frame(app), s, RUBRAVIEW_VOLPOP_BAR), x));
    return true;
}

bool volpop_release(app_state_t *app) {
    if (!app->volpop_drag) return false;
    app->volpop_drag = false;
    return true;
}

bool volpop_key(app_state_t *app, rubraview_key_combo_t combo) {
    if (!app->volpop_open) return false;
    if (key_is(combo, "Escape") || key_is(combo, "Enter")) { volpop_close(app); return true; }
    return false;
}

/* The volume button under the pointer in the docked Toolbar; its place in `at`. */
static bool volume_tile_at(app_state_t *app, double x, double y, rubraview_rect_t *at) {
    if (app->toolbox.state == RUBRAVIEW_BOX_COLLAPSED || app->toolbox.state == RUBRAVIEW_BOX_DETACHED) return false;
    rubraview_tile_metrics_t m = tile_metrics(rubraview_pal_window_dpi_scale(app->window));
    int32_t tile = rubraview_box_tile_at(&app->toolbox, &m, x, y);
    if (tile < 0 || tile >= app->toolbox_tile_count || !proven_u8str_view_eq(app->toolbox_tiles[tile].action, PROVEN_LIT("media_volume")))
        return false;
    *at = rubraview_box_tile_rect(&app->toolbox, &m, tile);
    return true;
}

bool volume_tile_wheel(app_state_t *app, double x, double y, double notches) {
    rubraview_rect_t at = {0};
    bool on_bar = app->volpop_open && rubraview_rect_contains(volpop_frame(app), x, y);
    if (notches == 0.0 || (!on_bar && !volume_tile_at(app, x, y, &at))) return false;
    volume_set(app, (double)volume_percent(app) + (notches > 0.0 ? 5.0 : -5.0));
    if (!on_bar) {
        app->vol_flash_at = at;
        app->vol_flash_until = rubraview_pal_time_now_seconds() + 1.2;
    }
    return true;
}
