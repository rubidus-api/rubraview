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
    app->slidepop_typing = false;
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
            text = rubraview_tile_caption(rubraview_tr_ctx_u8("menu", item->caption), false, st.mark, st.value, scratch, sizeof(scratch));
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

/* What the field says when it is not being typed in: the delay, "3.5". */
static proven_u8str_view_t slidepop_delay_text(const app_state_t *app, char *buffer, size_t size) {
    if (app->slidepop_typing) return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->slidepop_text, .size = app->slidepop_len };
    int n = snprintf(buffer, size, "%.1f s", app->slideshow.interval_seconds);
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)buffer, .size = n > 0 ? (size_t)n : 0 };
}

static void slidepop_type_end(app_state_t *app, bool keep) {
    if (!app->slidepop_typing) return;
    app->slidepop_typing = false;
    double seconds = 0.0;
    if (!keep) return;
    if (rubraview_subwin_amount((proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->slidepop_text, .size = app->slidepop_len }, &seconds)) {
        interval_set(app, seconds);
    } else {
        osd_say(app, TU8("the delay is a number of seconds, 0.1 to 300"));
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
            case RUBRAVIEW_SLIDEPOP_DELAY_FIELD: text = slidepop_delay_text(app, buffer, sizeof(buffer)); current = app->slidepop_typing; break;
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
    if (part != RUBRAVIEW_SLIDEPOP_DELAY_FIELD) slidepop_type_end(app, true);
    if (part == RUBRAVIEW_SLIDEPOP_NONE) {
        if (!rubraview_rect_contains(f, x, y)) slidepop_close(app);   /* a click away closes it */
        return;
    }
    switch (part) {
        case RUBRAVIEW_SLIDEPOP_DELAY_DOWN: interval_set(app, rubraview_slidepop_step(app->slideshow.interval_seconds, -1)); break;
        case RUBRAVIEW_SLIDEPOP_DELAY_UP:   interval_set(app, rubraview_slidepop_step(app->slideshow.interval_seconds, +1)); break;
        case RUBRAVIEW_SLIDEPOP_DELAY_FIELD:
            if (!app->slidepop_typing) {
                int n = snprintf(app->slidepop_text, sizeof(app->slidepop_text), "%.1f", app->slideshow.interval_seconds);
                app->slidepop_len = n > 0 ? (size_t)n : 0;
                app->slidepop_typing = true;
            }
            break;
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
    if (app->slidepop_open) {
        if (app->slidepop_typing) {
            if (key_is(combo, "Escape")) { slidepop_type_end(app, false); return true; }
            if (key_is(combo, "Enter")) { slidepop_type_end(app, true); return true; }
            if (key_is(combo, "Backspace")) { if (app->slidepop_len > 0) app->slidepop_len--; return true; }
            if (combo.key_name.size == 1 || key_is(combo, "Period") || key_is(combo, "Comma")) {
                char c = key_is(combo, "Period") ? '.' : key_is(combo, "Comma") ? ',' : combo.key_name.ptr[0];
                if (((c >= '0' && c <= '9') || c == '.' || c == ',') && app->slidepop_len + 1 < sizeof(app->slidepop_text)) {
                    app->slidepop_text[app->slidepop_len++] = c;
                }
                return true;
            }
            return true;   /* while typing, the slide show's keys wait */
        }
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
    int n = snprintf(buffer, capacity, "%s", words ? words : "");
    at = n > 0 ? ((size_t)n < capacity ? (size_t)n : capacity - 1) : 0;
    size_t keys = 0;
    for (size_t b = 0; b < app->keymap.count && keys < 3; ++b) {
        const rubraview_key_binding_t *binding = &app->keymap.bindings[b];
        if (!proven_u8str_view_eq(binding->action, action)) continue;
        for (size_t c = 0; c < binding->combo_count && keys < 3; ++c) {
            char combo[48];
            proven_u8str_view_t k = rubraview_key_combo_format(combo, sizeof(combo), binding->combos[c]);
            if (k.size == 0) continue;
            n = snprintf(buffer + at, capacity - at, "%s%.*s", keys == 0 ? (at > 0 ? "  (" : "(") : ", ", (int)k.size, k.ptr);
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

void draw_notice(app_state_t *app, double win_w, double win_h, double chrome_dpi) {
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

void draw_rename_box(app_state_t *app, double win_w, double win_h, double dpi) {
    if (!app->rename_active) return;
    {
        double box_w = 560.0 * dpi, box_h = 72.0 * dpi;   /* 1.25.0: roomier */
        if (box_w > win_w - 16.0 * dpi) box_w = win_w - 16.0 * dpi;   /* a narrow Edit window (RV-137) */
        rubraview_pal_rect_t box = { (win_w - box_w) * 0.5, win_h * 0.75, box_w, box_h };
        /* Edit to all asks with Edit open: the box goes left of its panel. */
        if (edit_panel_open(app) && !app->edit_window && app->panel.open && box.x + box.width > app->panel.bounds.x && app->panel.bounds.x > box_w)
            box.x = (app->panel.bounds.x - box_w) * 0.5;
        /* A password is asked in the middle, clear of the toolbox's anchor
           (VM, 2026-10-01: it showed through the box). */
        if (app->rename_is_password || app->rename_is_target || app->rename_is_extension) box.y = (win_h - box_h) * 0.5;
        if (app->rename_is_extension) {   /* 1.24.0: the rows over the box, the one it edits lit */
            double row_h = 34.0 * dpi, top = box.y - 40.0 * dpi - row_h * (double)app->ext_n;
            if (top < 8.0 * dpi) top = 8.0 * dpi;
            rubraview_pal_rect_t panel = { box.x, top - 34.0 * dpi, box.width, 34.0 * dpi + row_h * (double)app->ext_n + 6.0 * dpi };
            rubraview_pal_render_fill_rect(app->renderer, panel, 0xF6202020u, 4.0 * dpi);
            rubraview_pal_render_stroke_rect(app->renderer, panel, COLOR_BOX_BORDER, 1.0, 4.0 * dpi);
            rubraview_pal_render_draw_text(app->renderer, TU8("Change extensions: a new one for each (Tab: the next row)"),
                                           (rubraview_pal_rect_t){ panel.x + 12.0 * dpi, panel.y, panel.width - 24.0 * dpi, 34.0 * dpi },
                                           14.0 * dpi, 0xC0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
            for (int32_t k = 0; k < app->ext_n; ++k) {
                rubraview_pal_rect_t r = { panel.x + 6.0 * dpi, top + row_h * (double)k, panel.width - 12.0 * dpi, row_h - 4.0 * dpi };
                app->ext_row_rect[k] = r;
                bool on = k == app->ext_at;
                rubraview_pal_render_fill_rect(app->renderer, r, on ? COLOR_TILE_CURRENT : pal_rect_has(r, app->pointer_x, app->pointer_y) ? 0x40FFFFFFu : COLOR_TILE_FILL, 3.0);
                char line[128];
                proven_u8str_view_t to = on ? rubraview_textedit_text(&app->rename_edit) : cstr(app->ext_to[k]);
                int n = snprintf(line, sizeof(line), "%s   \xE2\x86\x92   %.*s", app->ext_from[k], (int)(to.size < 64 ? to.size : 64), to.ptr);
                if (n > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n },
                                                          (rubraview_pal_rect_t){ r.x + 14.0 * dpi, r.y, r.width - 28.0 * dpi, r.height },
                                                          16.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
            }
        }
        if (app->rename_is_find) {
            /* RFC-0005: Find, above the text. */
            box.y = 128.0 * dpi;   /* under the messages line */
            rubraview_pal_rect_t label = { box.x - 40.0 * dpi, box.y - 26.0 * dpi, box.width + 80.0 * dpi, 22.0 * dpi };
            rubraview_pal_render_fill_rect(app->renderer, label, COLOR_BOX_FILL, 3.0);
            rubraview_pal_render_draw_text(app->renderer, TU8("Find:  Enter / F3 the next  ·  Shift+Enter back  ·  Esc"),
                                           label, 13.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
        if (app->rename_is_listfilter) {   /* 1.25.0 */
            rubraview_pal_rect_t label = { box.x, box.y - 30.0 * dpi, box.width, 26.0 * dpi };
            rubraview_pal_render_fill_rect(app->renderer, label, COLOR_BOX_FILL, 3.0);
            rubraview_pal_render_draw_text(app->renderer, TU8("The list shows the names holding this  ·  Esc shows them all"),
                                           label, 14.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
        if (app->rename_is_target) {
            rubraview_pal_rect_t label = { box.x - 40.0 * dpi, box.y - 30.0 * dpi, box.width + 80.0 * dpi, 26.0 * dpi };
            rubraview_pal_render_fill_rect(app->renderer, label, COLOR_BOX_FILL, 3.0);
            rubraview_pal_render_draw_text(app->renderer,
                                           app->target_moving ? TU8("Move to which folder?  Enter  /  Ctrl+O browses  /  a new one is made")
                                                              : TU8("Copy to which folder?  Enter  /  Ctrl+O browses  /  a new one is made"),
                                           label, 14.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
        /* The typed path sits where the path is, as an address bar does. */
        if (app->rename_is_path || app->rename_is_filter) {
            rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ 0.0, picker_top(app), win_w, PICKER_CRUMB_HEIGHT * dpi },
                                           0xFF101010u, 0.0);   /* the chips under it do not show through */
            box = (rubraview_pal_rect_t){ 4.0 * dpi, picker_top(app) + 4.0 * dpi, win_w - 8.0 * dpi, (PICKER_CRUMB_HEIGHT - 8.0) * dpi };
        }
        rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BOX_FILL, 3.0);
        rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_BOX_BORDER, 1.0, 3.0);
        /* The name with the syllable the IME is still building at the
           caret, the selection shaded under its characters, and a caret
           that blinks (owner, 2026-09-23: the box did not look like it was
           taking text). Widths come from the face the text is drawn in, so
           caret and shading sit under the right characters, Hangul too. */
        const rubraview_textedit_t *te = &app->rename_edit;
        char shown[3 * (sizeof(app->rename_buffer) + sizeof(app->rename_composing))];
        size_t caret = te->caret, len = te->len, comp = app->rename_composing_length;
        memcpy(shown, app->rename_buffer, caret);
        memcpy(shown + caret, app->rename_composing, comp);
        memcpy(shown + caret + comp, app->rename_buffer + caret, len - caret);
        size_t total = len + comp;
        size_t sel_a = 0, sel_b = 0;
        bool has_selection = rubraview_textedit_has_selection(te);
        if (has_selection) rubraview_textedit_selection_range(te, &sel_a, &sel_b);
        if (app->rename_is_password) {
            /* A dot a character (the IME's unfinished syllable too); the
               caret and the selection are counted in characters. */
            char plain[sizeof(app->rename_buffer) + sizeof(app->rename_composing)];
            memcpy(plain, shown, total);
            size_t dots = 0, at_caret = 0, at_a = 0, at_b = 0;
            for (size_t i = 0; i <= total; ++i) {
                if (i == caret + comp) at_caret = dots;
                if (i == (sel_a >= caret ? sel_a + comp : sel_a)) at_a = dots;
                if (i == (sel_b >= caret ? sel_b + comp : sel_b)) at_b = dots;
                if (i < total && ((unsigned char)plain[i] & 0xC0) != 0x80) dots++;
            }
            secure_wipe(plain, sizeof(plain));
            for (size_t d = 0; d < dots; ++d) memcpy(shown + 3 * d, "\xE2\x97\x8F", 3);   /* ● */
            total = 3 * dots;
            caret = 3 * at_caret;
            comp = 0;
            sel_a = 3 * at_a;
            sel_b = 3 * at_b;
            rubraview_pal_rect_t label = { box.x, box.y - 30.0 * dpi, box.width, 26.0 * dpi };
            char title[1200];
            proven_u8str_view_t name = rubraview_path_basename((proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->password_for, .size = app->password_for_len });
            int tn = snprintf(title, sizeof(title), T("Password for %.*s"), (int)name.size, name.ptr);
            if (tn > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)title, .size = (size_t)(tn < (int)sizeof(title) ? tn : (int)sizeof(title) - 1) },
                                                       label, 14.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
        double size = 18.0 * dpi, pad = 14.0 * dpi;
        double full = 0.0, to_caret = 0.0;
        (void)rubraview_pal_render_measure_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)shown, .size = total }, size, &full);
        (void)rubraview_pal_render_measure_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)shown, .size = caret + comp }, size, &to_caret);
        /* Centred while it fits; longer, it scrolls so the caret stays in the box. */
        double inner = box.width - 2.0 * pad;
        double x0 = full <= inner && !app->rename_is_path ? box.x + (box.width - full) * 0.5 : box.x + pad;
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
        if (fmod(rubraview_pal_time_now_seconds(), 1.0) < 0.5) {
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
                     : app->rename_is_password ? T("Open") : app->rename_is_filter || app->rename_is_listfilter ? T("Done") : T("OK");
            kinds[n++] = RENAME_BTN_OK;
            if (app->rename_is_extension && app->ext_at + 1 < app->ext_n) { words[n] = T("Apply"); kinds[n++] = RENAME_BTN_OK + 100; }
            if (app->rename_is_target) { words[n] = T("Browse..."); kinds[n++] = RENAME_BTN_BROWSE; }
            words[n] = T("Cancel"); kinds[n++] = RENAME_BTN_CANCEL;
        }
        double bw = 120.0 * dpi, bh = 48.0 * dpi, gap = 10.0 * dpi;   /* 1.25.0: roomier */
        for (int32_t i = 0; i < n; ++i) {   /* RV-182: one width for all, the longest word's with room, never less */
            double tw = 0.0;
            if (rubraview_pal_render_measure_text(app->renderer, cstr(words[i]), 15.0 * dpi, &tw) && tw + 36.0 * dpi > bw) bw = tw + 36.0 * dpi;
        }
        double bx = app->rename_is_path || app->rename_is_filter ? box.x + box.width - (bw + gap) * (double)n
                                                                   : box.x + (box.width - (bw + gap) * (double)n + gap) * 0.5;
        double by = box.y + box.height + 10.0 * dpi;
        app->rename_btn_n = n;
        for (int32_t i = 0; i < n; ++i) {
            rubraview_pal_rect_t b = { bx + (double)i * (bw + gap), by, bw, bh };
            app->rename_btn[i] = b;
            app->rename_btn_kind[i] = kinds[i];
            bool over = pal_rect_has(b, app->pointer_x, app->pointer_y);
            rubraview_pal_render_fill_rect(app->renderer, b, i == 0 ? COLOR_TILE_CURRENT : over ? 0x50FFFFFFu : COLOR_BOX_FILL, 3.0);
            rubraview_pal_render_stroke_rect(app->renderer, b, COLOR_BOX_BORDER, 1.0, 3.0);
            rubraview_pal_render_draw_text(app->renderer, cstr(words[i]), b, 15.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
    }
}

/* The two points as text, side by side, with the one in hand marked and
   a line saying how the keys and the numbers cross over. */
static void draw_answer_close(app_state_t *app, rubraview_pal_rect_t box, double dpi) {
    rubraview_pal_rect_t x = answer_box_close(app, box);
    if (pal_rect_has(x, app->pointer_x, app->pointer_y)) rubraview_pal_render_fill_rect(app->renderer, x, COLOR_CLOSE_HOVER, 2.0);
    draw_sign(app->renderer, RUBRAVIEW_ICON_CLOSE, "X", x, 12.0 * dpi, COLOR_TEXT);
}

void draw_ab_edit_box(app_state_t *app, double win_w, double win_h, double dpi) {
    if (!app->ab_edit_open) return;
    (void)win_w; (void)win_h;
    rubraview_pal_rect_t box = ab_box_rect(app);
    rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BOX_FILL, 3.0);
    rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_BOX_BORDER, 1.0, 3.0);
    draw_answer_close(app, box, dpi);
    const char *const TAKE[2] = { T("A = now  ["), T("B = now  ]") };
    for (int32_t k = 0; k < 2; ++k) {
        rubraview_pal_rect_t b = ab_box_button(app, k);
        bool over = pal_rect_has(b, app->pointer_x, app->pointer_y);
        rubraview_pal_render_fill_rect(app->renderer, b, over ? COLOR_TILE_CURRENT : COLOR_TILE_FILL, 3.0);
        rubraview_pal_render_stroke_rect(app->renderer, b, COLOR_BOX_BORDER, 1.0, 3.0);
        rubraview_pal_render_draw_text(app->renderer, cstr(TAKE[k]), b, 14.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }

    rubraview_pal_rect_t title = { box.x + 12.0 * dpi, box.y + 4.0 * dpi, box.width - 60.0 * dpi, 26.0 * dpi };
    rubraview_pal_render_draw_text(app->renderer, TU8("Repeat from A to B  (hours, minutes, seconds)"), title, 14.0 * dpi,
                                   0xB0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
    /* Each point in three fields, the one in hand lit with a caret (owner, 2026-10-15). */
    bool caret_lit = fmod(rubraview_pal_time_now_seconds(), 1.0) < 0.5;
    static const char *const UNIT[3] = { "h", "m", "s" };
    for (int32_t side = 0; side < 2; ++side) {
        rubraview_pal_rect_t first = ab_part_rect(app, side, 0);
        rubraview_pal_rect_t label = { first.x - AB_LABEL_W * dpi, first.y, AB_LABEL_W * dpi, first.height };
        rubraview_pal_render_draw_text(app->renderer, side == 0 ? PROVEN_LIT("A") : PROVEN_LIT("B"), label, 18.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        for (int32_t part = 0; part < 3; ++part) {
            rubraview_pal_rect_t r = ab_part_rect(app, side, part);
            bool here = app->ab_edit_field == side && app->ab_edit_part == part;
            rubraview_pal_render_fill_rect(app->renderer, r, here ? 0xFF3A3A3Au : COLOR_TILE_FILL, 3.0);
            rubraview_pal_render_stroke_rect(app->renderer, r, here ? COLOR_TILE_CURRENT : COLOR_BOX_BORDER, here ? 2.0 : 1.0, 3.0);
            char shown[16];
            proven_u8str_view_t t = ab_part(app, side, part);
            int n = snprintf(shown, sizeof(shown), "%.*s%s", (int)t.size, t.ptr, here && caret_lit && !app->ab_edit_fresh ? "|" : "");
            if (n > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)shown, .size = (size_t)n }, r, 17.0 * dpi,
                                                      COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
            rubraview_pal_rect_t unit = { r.x + r.width, r.y, AB_UNIT_W * dpi, r.height };
            rubraview_pal_render_draw_text(app->renderer, cstr(UNIT[part]), unit, 13.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
        }
    }
    rubraview_pal_rect_t hint = { box.x, box.y + 118.0 * dpi, box.width, 34.0 * dpi };
    rubraview_pal_render_draw_text(app->renderer,
                                   TU8("Click a field or Tab  ·  : next field  ·  \\ empties  ·  Enter keeps  ·  Esc or X leaves"),
                                   hint, 13.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
}

/* RV-187 (owner, 2026-10-29: "칸마다 px/비율 전환"): under each field, its unit, pressed to switch it. */
static void px_unit_buttons(app_state_t *app, double dpi) {
    static const char *const CROP[4] = { "X", "Y", "W", "H" }, *const SIZE[2] = { "W", "H" };
    for (int32_t k = 0; k < px_edit_fields(app); ++k) {
        rubraview_pal_rect_t b = px_unit_rect(app, k);
        bool ratio = (app->px_edit_ratio & (1u << k)) != 0;
        rubraview_pal_render_fill_rect(app->renderer, b, ratio ? 0xFF2E4A6Eu : 0xFF3A3A3Au, 3.0);
        char word[64];
        int n = snprintf(word, sizeof(word), "%s: %s", app->px_edit_crop ? CROP[k] : SIZE[k], ratio ? T("ratio") : T("pixels"));
        rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)word, .size = n > 0 ? (size_t)n : 0 },
                                       b, 13.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
}

void draw_px_edit_box(app_state_t *app, double win_w, double win_h, double dpi) {
    if (!app->px_edit_open) return;
    (void)win_w; (void)win_h;
    rubraview_pal_rect_t box = px_box_rect(app);
    rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BOX_FILL, 3.0);
    rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_BOX_BORDER, 1.0, 3.0);
    draw_answer_close(app, box, dpi);
    bool caret_lit = fmod(rubraview_pal_time_now_seconds(), 1.0) < 0.5;
    char line[160];
    if (app->px_edit_crop) {
        /* The crop: its left and top corner and its size, the field in hand in brackets. */
        static const char *const NAME[4] = { "X", "Y", "W", "H" };
        int at = 0;
        for (int32_t k = 0; k < 4 && at < (int)sizeof(line); ++k) {
            bool here = app->px_edit_field == k;
            int m = snprintf(line + at, sizeof(line) - (size_t)at, "%s%s %s%.*s%s%s", k == 0 ? "" : k == 2 ? "    " : "  ", NAME[k],
                             here ? "[" : " ", (int)app->px_edit_length[k], app->px_edit_text[k], here ? (caret_lit ? "|]" : " ]") : " ",
                             (app->px_edit_ratio & (1u << k)) ? "×" : "");
            if (m < 0) break;
            at += m;
        }
        if (at > (int)sizeof(line) - 1) at = (int)sizeof(line) - 1;
        rubraview_pal_rect_t top = { box.x, box.y + 10.0 * dpi, box.width, 34.0 * dpi };
        rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)at }, top, 18.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        rubraview_pal_rect_t hint = { box.x, box.y + 50.0 * dpi, box.width, 34.0 * dpi };
        rubraview_pal_render_draw_text(app->renderer, TU8("From the top left  ·  Tab next  ·  / pixels or a ratio (0.25 = a quarter)  ·  Enter cuts  ·  Esc"),
                                       hint, 13.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
        px_unit_buttons(app, dpi);
        return;
    }
    int n = snprintf(line, sizeof(line), T("Width %s%.*s%s%s  x  Height %s%.*s%s%s"),
                     app->px_edit_field == 0 ? "[" : " ", (int)app->px_edit_length[0], app->px_edit_text[0],
                     app->px_edit_field == 0 ? (caret_lit ? "|]" : " ]") : " ", (app->px_edit_ratio & 1u) ? "×" : "",
                     app->px_edit_field == 1 ? "[" : " ", (int)app->px_edit_length[1], app->px_edit_text[1],
                     app->px_edit_field == 1 ? (caret_lit ? "|]" : " ]") : " ", (app->px_edit_ratio & 2u) ? "×" : "");
    if (n > 0) {
        rubraview_pal_rect_t top = { box.x, box.y + 10.0 * dpi, box.width, 34.0 * dpi };
        rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n },
                                       top, 20.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
    rubraview_pal_rect_t hint = { box.x, box.y + 50.0 * dpi, box.width, 34.0 * dpi };
    rubraview_pal_render_draw_text(app->renderer,
                                   app->edit.resize_lock_aspect ? TU8("The other side follows the shape  ·  Tab swaps  ·  / pixels or a ratio  ·  Enter keeps  ·  Esc")
                                                                : TU8("Tab swaps  ·  / pixels or a ratio (1.5 = half as big again)  ·  Enter keeps  ·  Esc"),
                                   hint, 13.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
    px_unit_buttons(app, dpi);
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
        /* D-88 */
        double box_w = 760.0 * chrome_dpi, box_h = 110.0 * chrome_dpi;
        if (box_w > win_w - 16.0) box_w = win_w - 16.0;
        rubraview_pal_rect_t box = { (win_w - box_w) * 0.5, (win_h - box_h) * 0.5, box_w, box_h };
        rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BOX_FILL, 3.0);
        rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_CLOSE_HOVER, 2.0, 3.0);
        rubraview_pal_rect_t upper = { box.x, box.y + box_h * 0.12, box_w, box_h * 0.38 };
        rubraview_pal_rect_t lower = { box.x, box.y + box_h * 0.5, box_w, box_h * 0.38 };
        bool deleting = app->arc_ask == ARC_ASK_DELETE;
        rubraview_pal_render_draw_text(app->renderer,
                                       deleting ? TU8("Delete this page from the archive? It cannot be undone.")
                                                : TU8("This archive cannot be changed as it is. It is written again; the old file goes to the Recycle Bin."),
                                       upper, (deleting ? 16.0 : 13.5) * chrome_dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        rubraview_pal_render_draw_text(app->renderer,
                                       deleting ? TU8("Y  delete        N  keep")
                                                : TU8("Z  as ZIP (CBZ)        7  as 7z (CB7)        Esc  leave it"),
                                       lower, 15.0 * chrome_dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
    /* RV-137: a value typed, or the copies' name, asked for in the Edit window is drawn there */
    if (!(app->edit_window && edit_panel_open(app) && (app->rename_is_value || app->rename_is_apply))) draw_rename_box(app, win_w, win_h, chrome_dpi);
    draw_ab_edit_box(app, win_w, win_h, chrome_dpi);
    if (!(app->edit_window && edit_panel_open(app))) draw_px_edit_box(app, win_w, win_h, chrome_dpi);   /* RV-137: else in the Edit window */
    draw_notice(app, win_w, win_h, chrome_dpi);
}

/* The edge buttons (owner, 2026-09-29): the revealed side's three, an
   icon or a short sign each, and the hovered one's caption beside it. */
void draw_edge_nav(app_state_t *app, double win_w, double win_h) {
    rubraview_edge_side_t side = edge_nav_side(app, app->pointer_x, app->pointer_y);
    if (side == RUBRAVIEW_EDGE_NONE) return;
    bool media = app->media != NULL;
    /* [media][side][slot]: an icon of Segoe MDL2 Assets, or 0 and the sign. */
    static const uint32_t ICON[2][2][RUBRAVIEW_EDGE_SLOTS] = {   /* RV-179: the fourth, the folder or archive beside */
        { { 0xE76B, 0, 0xEB9E, 0xE892 }, { 0xE76C, 0, 0xEB9D, 0xE893 } },   /* RV-180: pages ◁◁ ▷▷, the folder |◁ ▷| */
        { { 0, 0, 0xEB9E, 0xE892 },      { 0, 0, 0xEB9D, 0xE893 } },
    };
    static const char *const SIGN[2][2][RUBRAVIEW_EDGE_SLOTS] = {
        { { "<", "-10", "<<", "|<" }, { ">", "+10", ">>", ">|" } },
        { { "-5s", "-30s", "<<", "|<" }, { "+5s", "+30s", ">>", ">|" } },
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
        uint32_t icon = ICON[media][side][slot];
        if (!icon || !rubraview_pal_render_draw_icon(app->renderer, icon, br, r.height * 0.45, COLOR_TEXT)) {
            rubraview_pal_render_draw_text(app->renderer, cstr(SIGN[media][side][slot]), br, r.height * 0.32,
                                           COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
        if (hovered) {
            double cw = r.height * (slot == 3 ? 5.5 : 4.0);   /* RV-179: the longer caption */
            rubraview_pal_rect_t cap = { side == RUBRAVIEW_EDGE_LEFT ? r.x + r.width + nav.gap : r.x - nav.gap - cw,
                                         r.y + r.height * 0.2, cw, r.height * 0.6 };
            rubraview_pal_render_fill_rect(app->renderer, cap, rubraview_box_fade(COLOR_BOX_FILL, opacity), 3.0);
            rubraview_pal_render_draw_text(app->renderer, cstr(rubraview_edgenav_caption(side, slot, media)), cap,
                                           r.height * 0.3, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
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
    if (checked > 0) snprintf(del, sizeof(del), T("Del (%zu)"), checked); else snprintf(del, sizeof(del), T("Del"));
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
    if (app->list_ext_off_n > 0) snprintf(ext, sizeof(ext), ".ext (-%zu)", app->list_ext_off_n); else snprintf(ext, sizeof(ext), ".ext");
    list_button(rr, px, py, rubraview_listwin_tool_rect(lw, RUBRAVIEW_LISTWIN_EXT), ext, small, app->list_ext_off_n > 0, COLOR_TEXT);
    char shape[48];   /* RV-192 */
    snprintf(shape, sizeof(shape), "%s \xE2\x96\xBE", T("Shape"));
    list_button(rr, px, py, rubraview_listwin_tool_rect(lw, RUBRAVIEW_LISTWIN_SHAPE), shape, small, false, COLOR_TEXT);
    rubraview_rect_t f = rubraview_listwin_tool_rect(lw, RUBRAVIEW_LISTWIN_FILTER);
    if (f.width > 0.0) {
        rubraview_pal_rect_t box = { f.x + 2.0, f.y + 2.0, f.width - 4.0, f.height - 4.0 };
        bool typing = in_files ? app->list_filter_in_files : (app->rename_active && app->rename_is_listfilter);
        rubraview_pal_render_fill_rect(rr, box, 0xFF161616u, 2.0);
        rubraview_pal_render_stroke_rect(rr, box, typing ? 0xFF5B9BD5u : COLOR_BOX_BORDER, 1.0, 2.0);
        char words[160];
        bool empty = app->list_filter[0] == '\0';
        snprintf(words, sizeof(words), "%s%s", empty && !typing ? T("Filter") : app->list_filter, typing && in_files ? "|" : "");
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
    int n = current >= 0 ? snprintf(title, sizeof(title), "%s   %d / %zu", what, current + 1, page_count(app))
                         : snprintf(title, sizeof(title), "%s   %zu", what, page_count(app));
    if (n > 0 && list_viewed(app) && (size_t)n < sizeof(title))
        n += snprintf(title + n, sizeof(title) - (size_t)n, T("   (%zu shown)"), count);
    if (n >= (int)sizeof(title)) n = (int)sizeof(title) - 1;
    double buttons = lw->title_height * (lw->editable ? 2.0 : 1.0);
    rubraview_pal_rect_t head = { lw->x + lw->row_height * 0.4, lw->y, lw->width - buttons - lw->row_height * 0.4, lw->title_height };
    if (n > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ (const proven_byte_t *)title, (size_t)n }, head, lw->title_height * 0.45,
                                              COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    rubraview_rect_t cr = rubraview_listwin_close_rect(lw);
    rubraview_pal_rect_t close = { cr.x, cr.y, cr.width, cr.height };
    if (rubraview_rect_contains(cr, app->pointer_x, app->pointer_y)) {
        rubraview_pal_render_fill_rect(app->renderer, close, COLOR_CLOSE_HOVER, 4.0);
    }
    draw_sign(app->renderer, RUBRAVIEW_ICON_CLOSE, "X", close, lw->title_height * 0.4, COLOR_TEXT);

    double small = lw->row_height * 0.5 * place_size(app, PF_LIST);   /* 1.25.0 */
    if (lw->editable) {
        /* D-111: R sizes the window when dragged; the bar acts on the list. */
        listwin_sign_button(app, app->renderer, rubraview_listwin_resize_rect(lw), RUBRAVIEW_ICON_RESIZE, "R", lw->title_height * 0.4, lw->resizing);
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
        int m = snprintf(line, sizeof(line), "%4zu   %.*s", page + 1, (int)(name.size < 480 ? name.size : 480), name.ptr);
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
    int n = snprintf(title, sizeof(title), T("Equaliser   %.*s"), (int)name.size, name.ptr);
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
        int m = snprintf(v, sizeof(v), "%+.1f", (double)gains[b]);
        rubraview_pal_rect_t top = { col.x, col.y, col.width, w->label_height };
        if (m > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ (const proven_byte_t *)v, (size_t)m }, top, w->label_height * 0.55,
                                                  COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        char f[16];
        m = freqs[b] >= 1000.0 ? snprintf(f, sizeof(f), "%gk", freqs[b] / 1000.0) : snprintf(f, sizeof(f), "%g", freqs[b]);
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
