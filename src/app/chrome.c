/*
 * The window's chrome: the title bar and its pull-down, the wheel's strip, the music page's backdrop, extras and
 * words, the strip drawn (RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* ---- 1.25.0: the title bar's ☰ pull-down (owner, 2026-10-20: "타이틀바 고정(Pin), 정렬align, 새 창 열기,
   파일 열기, 최근 파일, 일괄 처리, 인쇄, 재생목록Playlist, 설정Settings, 도움말Help") ---- */
static const struct { const char *words, *action; } TMENU[] = {
    { N_("Pin the title bar"), "toggle_titlebar_pin" },
    { N_("Align the Floating Widget"), "align_widget" },
    { N_("New window"), "new_window" },
    { N_("Open file"), "open_picker" },
    { N_("Recent files..."), "open_recent_list" },
    { N_("Batch"), "open_batch" },
    { N_("Print..."), "print" },
    { N_("Playlist"), "toggle_playlist" },
    { N_("Settings"), "open_settings" },
    { N_("Help"), "toggle_help" },
};
#define TMENU_COUNT ((int32_t)(sizeof(TMENU) / sizeof(TMENU[0])))

/* RV-205 (owner, 2026-10-31: "화면 우클릭 컨텍스트 메뉴의 모든 항목들이 타이틀바 좌측의 메뉴에도 들어가게 ... 2열로 각 항목들
   가로로 길쭉한 버튼으로"): its own rows, then the right click's list for what is on screen, all of it (the
   right click leaves out what the Toolbar has; this does not), less what its own rows already do; in two
   columns of wide buttons, each group on lines of its own (rubraview_gridmenu_*). */
#define TMENU_MAX (TMENU_COUNT + RUBRAVIEW_CONTEXT_MAX_ITEMS)
typedef struct tmenu_row { proven_u8str_view_t words, action; int32_t group; } tmenu_row_t;

static size_t tmenu_rows(const app_state_t *app, tmenu_row_t *rows) {
    size_t n = 0;
    for (int32_t i = 0; i < TMENU_COUNT; ++i)
        rows[n++] = (tmenu_row_t){ rubraview_tr_u8(cstr(TMENU[i].words)), cstr(TMENU[i].action), 0 };
    const rubraview_boxes_doc_t *doc = rubraview_boxes_document();
    const rubraview_toolbox_profile_t *list = rubraview_boxes_context(doc, cstr(toolbox_profile_name(app)));
    for (int32_t i = 0; list && i < list->tile_count && n < TMENU_MAX; ++i) {
        const rubraview_box_tile_t *item = &doc->context_items[list->first_tile + i];
        bool own = false;
        for (int32_t k = 0; k < TMENU_COUNT && !own; ++k) own = proven_u8str_view_eq(item->action, cstr(TMENU[k].action));
        if (own) continue;
        rows[n++] = (tmenu_row_t){ rubraview_tr_ctx_u8("menu", item->caption), item->action, item->group + 1 };
    }
    return n;
}

typedef struct tmenu_layout {
    size_t n;
    tmenu_row_t rows[TMENU_MAX];
    int32_t groups[TMENU_MAX];
    bool enabled[TMENU_MAX];
    rubraview_gridmenu_metrics_t m;
    rubraview_rect_t frame;
} tmenu_layout_t;

/* The grid under the title bar at the left, made smaller to stay in the window. */
static void tmenu_layout(app_state_t *app, tmenu_layout_t *L) {
    double dpi = rubraview_pal_window_dpi_scale(app->window), k = place_size(app, PF_MENU);
    L->n = tmenu_rows(app, L->rows);
    rubraview_action_facts_t facts = action_facts(app);
    for (size_t i = 0; i < L->n; ++i) {
        L->groups[i] = L->rows[i].group;
        L->enabled[i] = rubraview_action_state(L->rows[i].action, &facts).enabled;
    }
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    L->m = (rubraview_gridmenu_metrics_t){ .cell_width = 250.0 * dpi * k, .cell_height = 34.0 * dpi * k, .gap = 8.0 * dpi,
                                           .pad = 6.0 * dpi, .columns = 2 };
    double top = app->titlebar.height;
    L->frame = rubraview_gridmenu_frame(0.0, top, &L->m, L->groups, L->n);
    if (L->frame.width > (double)w && w > 0) {
        L->m.cell_width = fmax(120.0 * dpi, ((double)w - L->m.pad * 2.0 - L->m.gap) / 2.0);
        L->frame = rubraview_gridmenu_frame(0.0, top, &L->m, L->groups, L->n);
    }
    double room = (double)h - top;
    if (L->frame.height > room && room > 0.0) {
        double lines = 0.0, gaps = 0.0;   /* the lines kept, each made lower */
        for (size_t i = 1; i < L->n; ++i) if (L->groups[i] != L->groups[i - 1]) gaps += 1.0;
        lines = (L->frame.height - L->m.pad * 2.0 - L->m.gap * gaps) / L->m.cell_height;
        if (lines > 0.0) L->m.cell_height = fmax(20.0 * dpi, (room - L->m.pad * 2.0 - L->m.gap * gaps) / lines);
        L->frame = rubraview_gridmenu_frame(0.0, top, &L->m, L->groups, L->n);
    }
}

/* A cell chosen, by a click or Enter. Delete asks a second time, as the right click's does. */
static bool tmenu_choose(app_state_t *app, const tmenu_layout_t *L, int32_t i) {
    if (i < 0 || (size_t)i >= L->n || !L->enabled[i]) return false;
    proven_u8str_view_t action = L->rows[i].action;
    bool clears = proven_u8str_view_eq(action, PROVEN_LIT("recent_clear"));
    if ((clears || proven_u8str_view_eq(action, PROVEN_LIT("delete_file")) || proven_u8str_view_eq(action, PROVEN_LIT("picker_recycle"))) &&
        !rubraview_confirm_press(&app->ctx_confirm, i, rubraview_pal_time_now_seconds())) {
        osd_say(app, clears ? TU8("click Clear again to empty the recent list") : TU8("click Delete again to move it to the recycle bin"));
        return true;
    }
    app->tmenu_open = false;
    rubraview_confirm_clear(&app->ctx_confirm);
    handle_action(app, action);
    return true;
}

static void draw_tmenu_in(app_state_t *app);
static void draw_tmenu(app_state_t *app, double win_w) {
    (void)win_w;
    if (!app->tmenu_open) return;
    int before = place_font_begin(app, PF_MENU, NULL);   /* 1.25.0: in the menus' face */
    draw_tmenu_in(app);
    place_font_end(app, before);
}

static void draw_tmenu_in(app_state_t *app) {
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    static tmenu_layout_t L;
    tmenu_layout(app, &L);
    rubraview_pal_rect_t f = { L.frame.x, L.frame.y, L.frame.width, L.frame.height };
    rubraview_pal_render_fill_rect(app->renderer, f, 0xF6202020u, 4.0 * dpi);
    rubraview_pal_render_stroke_rect(app->renderer, f, COLOR_BOX_BORDER, 1.0, 4.0 * dpi);
    bool pinned = rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("titlebar_pinned")) > 0.5;
    rubraview_action_facts_t facts = action_facts(app);
    double now = rubraview_pal_time_now_seconds();
    int32_t hover = rubraview_gridmenu_at(L.frame, &L.m, L.groups, L.n, app->pointer_x, app->pointer_y);
    double size = fmin(15.0 * dpi * place_size(app, PF_MENU), L.m.cell_height * 0.46);
    for (size_t i = 0; i < L.n; ++i) {
        rubraview_rect_t c = rubraview_gridmenu_cell(L.frame, &L.m, L.groups, i);
        rubraview_pal_rect_t r = { c.x, c.y + 1.0 * dpi, c.width, c.height - 2.0 * dpi };
        bool on = pinned && proven_u8str_view_eq(L.rows[i].action, PROVEN_LIT("toggle_titlebar_pin"));
        rubraview_pal_render_fill_rect(app->renderer, r, on ? TITLEBAR_ON : 0x18FFFFFFu, 3.0 * dpi);   /* a button */
        if (L.enabled[i] && ((int32_t)i == hover || (int32_t)i == app->tmenu_focus))
            rubraview_pal_render_fill_rect(app->renderer, r, 0x40FFFFFFu, 3.0 * dpi);
        rubraview_action_state_t st = rubraview_action_state(L.rows[i].action, &facts);
        char scratch[160], line[200];
        proven_u8str_view_t text = rubraview_confirm_armed(&app->ctx_confirm, (int64_t)i, now)
            ? (proven_u8str_view_eq(L.rows[i].action, PROVEN_LIT("recent_clear")) ? TU8("Clear?") : TU8("Delete?"))
            : rubraview_tile_caption(L.rows[i].words, false, st.mark, st.value, scratch, sizeof(scratch));
        int k = snprintf(line, sizeof(line), "%.*s%s", (int)text.size, (const char *)text.ptr, on ? "  \xE2\x9C\x93" : "");
        proven_u8str_view_t shown = k > 0 ? (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)k < sizeof(line) ? (size_t)k : sizeof(line) - 1 } : text;
        rubraview_pal_rect_t words = { r.x + 12.0 * dpi, r.y, r.width - 16.0 * dpi, r.height };
        rubraview_pal_render_draw_text(app->renderer, shown, words, size, L.enabled[i] ? COLOR_TEXT : 0x60F0F0F0u, RUBRAVIEW_TEXT_LEFT);
        if (on) rubraview_pal_render_draw_text(app->renderer, shown, (rubraview_pal_rect_t){ words.x + 0.9, words.y, words.width, words.height },
                                               size, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    }
}

/* A press while it is open: a button does its action; anywhere else closes it (the title bar's ☰ toggles it). */
bool tmenu_press(app_state_t *app, double x, double y) {
    if (!app->tmenu_open) return false;
    if (rubraview_titlebar_hit(&app->titlebar, x, y, (double)tmenu_win_w(app)) == RUBRAVIEW_TITLEBAR_MENU) return false;
    static tmenu_layout_t L;
    tmenu_layout(app, &L);
    int32_t at = rubraview_gridmenu_at(L.frame, &L.m, L.groups, L.n, x, y);
    if (at >= 0) { (void)tmenu_choose(app, &L, at); return true; }
    bool inside = rubraview_rect_contains(L.frame, x, y);
    if (!inside) { app->tmenu_open = false; rubraview_confirm_clear(&app->ctx_confirm); }
    return inside;
}

bool tmenu_key(app_state_t *app, rubraview_key_combo_t combo) {
    if (!app->tmenu_open) return false;
    static tmenu_layout_t L;
    tmenu_layout(app, &L);
    if (app->tmenu_focus >= (int32_t)L.n) app->tmenu_focus = -1;   /* a shorter list since */
    if (key_is(combo, "Escape")) { app->tmenu_open = false; return true; }
    if (key_is(combo, "Right")) { app->tmenu_focus = rubraview_ctxmenu_step(L.enabled, L.n, app->tmenu_focus, +1); return true; }
    if (key_is(combo, "Left")) { app->tmenu_focus = rubraview_ctxmenu_step(L.enabled, L.n, app->tmenu_focus, -1); return true; }
    if (key_is(combo, "Down") || key_is(combo, "Up")) {
        app->tmenu_focus = rubraview_gridmenu_step_vertical(L.frame, &L.m, L.groups, L.enabled, L.n, app->tmenu_focus, key_is(combo, "Down") ? +1 : -1);
        return true;
    }
    if (key_is(combo, "Enter") && app->tmenu_focus >= 0) { (void)tmenu_choose(app, &L, app->tmenu_focus); return true; }
    app->tmenu_open = false;
    return false;
}

void draw_titlebar(app_state_t *app, double win_w) {
    if (!app->titlebar.shown) return;
    rubraview_pal_rect_t bar = { 0.0, 0.0, win_w, app->titlebar.height };
    rubraview_pal_render_fill_rect(app->renderer, bar, rubraview_box_fade(COLOR_BAR_FILL, rubraview_settings_get(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("titlebar_opacity"))), 0.0);

    int32_t page = current_page_index(app);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double bh = app->titlebar.height;
    /* 1.25.0 (owner, 2026-10-20): ☰ at the left end — three lines; lit while its pull-down is open */
    rubraview_rect_t mr = rubraview_titlebar_button_rect(&app->titlebar, RUBRAVIEW_TITLEBAR_MENU, win_w);
    {
        rubraview_pal_rect_t mb = { mr.x, mr.y, mr.width, mr.height };
        if (app->tmenu_open) rubraview_pal_render_fill_rect(app->renderer, mb, TITLEBAR_ON, 0.0);
        else if (rubraview_rect_contains(mr, app->pointer_x, app->pointer_y)) rubraview_pal_render_fill_rect(app->renderer, mb, COLOR_TILE_FILL, 0.0);
        double lw = mr.width * 0.42, lx = mr.x + (mr.width - lw) * 0.5, th = fmax(1.5, bh * 0.06);
        for (int k = -1; k <= 1; ++k)
            rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ lx, mr.y + bh * 0.5 + (double)k * bh * 0.17 - th * 0.5, lw, th }, COLOR_TEXT, th * 0.5);
    }
    /* the name: the folder in the open-file screen, else the file on screen; a press opens the open-file screen */
    proven_u8str_view_t name = app->picker_open ? (app->picker_dir.size > 0 ? app->picker_dir : PROVEN_LIT("Rubraview"))
                 : page >= 0 && (size_t)page < page_count(app) ? page_display_name(app, (size_t)page) : PROVEN_LIT("");
    double tk = 1.0;
    int before_font = place_font_begin(app, PF_TITLE, &tk);   /* 1.25.0: the title bar's name in its own face and size */
    double font = bh * 0.38 * tk, name_w = 0.0;
    if (name.size > 0 && rubraview_pal_render_measure_text(app->renderer, name, font, &name_w)) app->titlebar.name_width = name_w + 20.0 * dpi;
    else app->titlebar.name_width = 0.0;
    rubraview_rect_t nr = rubraview_titlebar_button_rect(&app->titlebar, RUBRAVIEW_TITLEBAR_NAME, win_w);
    if (nr.width > 0.0) {
        rubraview_pal_rect_t nb = { nr.x, nr.y + 4.0 * dpi, nr.width, nr.height - 8.0 * dpi };
        if (rubraview_rect_contains(nr, app->pointer_x, app->pointer_y) && !app->picker_open)
            rubraview_pal_render_fill_rect(app->renderer, nb, COLOR_TILE_FILL, 3.0 * dpi);
        char fit[1024];
        /* measured to fit: whole (the estimate files_fit_text makes would cut it); cut only when the row is short */
        proven_u8str_view_t shown = nr.width + 0.5 >= name_w + 20.0 * dpi ? name : files_fit_text(fit, sizeof(fit), name, nr.width - 20.0 * dpi, font);
        rubraview_pal_render_draw_text(app->renderer, shown,
                                       (rubraview_pal_rect_t){ nr.x + 10.0 * dpi, nr.y, nr.width - 10.0 * dpi, nr.height }, font, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    }
    place_font_end(app, before_font);

    /* The right-hand row: Set, P, H, T, R, F, _, [], X (RV-218: Windows' three at the end); what is on is lit and bold (owner, 2026-10-20) */
    static const rubraview_titlebar_button_t BUTTONS[] = {
        RUBRAVIEW_TITLEBAR_SETTINGS, RUBRAVIEW_TITLEBAR_BARPIN, RUBRAVIEW_TITLEBAR_HIDE, RUBRAVIEW_TITLEBAR_PIN,
        RUBRAVIEW_TITLEBAR_FULLSCREEN, RUBRAVIEW_TITLEBAR_MINIMIZE, RUBRAVIEW_TITLEBAR_MAXIMIZE, RUBRAVIEW_TITLEBAR_RESIZE,
        RUBRAVIEW_TITLEBAR_CLOSE,
    };
    const char *const GLYPHS[] = { T("Set"), "P", "H", "T", "F", "_", "[]", "R", "X" };   /* where the icons are missing */
    const uint32_t ICONS[] = { RUBRAVIEW_ICON_SETTINGS, RUBRAVIEW_ICON_PIN, RUBRAVIEW_ICON_HIDE, RUBRAVIEW_ICON_ON_TOP, RUBRAVIEW_ICON_FULLSCREEN,
                               RUBRAVIEW_ICON_MINIMIZE, rubraview_pal_window_is_maximized(app->window) ? RUBRAVIEW_ICON_RESTORE : RUBRAVIEW_ICON_MAXIMIZE,
                               RUBRAVIEW_ICON_RESIZE, RUBRAVIEW_ICON_CLOSE };   /* RV-181 */
    bool on[9] = { app->settings_open,
                   rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("titlebar_pinned")) > 0.5,
                   app->chrome_hidden,
                   rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("always_on_top")) > 0.5,
                   rubraview_pal_window_is_fullscreen(app->window),
                   false,
                   rubraview_pal_window_is_maximized(app->window),
                   false, false };
    double bk = 1.0;
    int before_buttons = place_font_begin(app, PF_BUTTONS, &bk);
    for (size_t i = 0; i < sizeof(BUTTONS) / sizeof(BUTTONS[0]); ++i) {
        rubraview_rect_t r = rubraview_titlebar_button_rect(&app->titlebar, BUTTONS[i], win_w);
        rubraview_pal_rect_t br = { r.x, r.y, r.width, r.height };
        bool hovered = rubraview_rect_contains(r, app->pointer_x, app->pointer_y);
        if (on[i]) rubraview_pal_render_fill_rect(app->renderer, br, TITLEBAR_ON, 0.0);
        if (hovered) rubraview_pal_render_fill_rect(app->renderer, br, BUTTONS[i] == RUBRAVIEW_TITLEBAR_CLOSE ? COLOR_CLOSE_HOVER : COLOR_TILE_FILL, 0.0);
        if (!rubraview_pal_render_draw_icon(app->renderer, ICONS[i], br, bh * 0.34 * bk, on[i] ? COLOR_TEXT : 0xC8F0F0F0u))
            draw_text_weight(app, cstr(GLYPHS[i]), br, bh * 0.36 * bk, on[i] ? COLOR_TEXT : 0xC8F0F0F0u, on[i]);
    }
    place_font_end(app, before_buttons);
    draw_tmenu(app, win_w);
}

/* The filmstrip (F4, or the wheel's for a while): thumbnails from its own thread, else the
   page if it is in memory, else the page's number; the page on screen framed. */
static void draw_wheel_strip(app_state_t *app) {
    double shown = page_count(app) > 0 ? strip_shown(app) : 0.0;
    if (shown > 0.0) {
        /* The thumbnails, the page on screen framed; the wheel's strip fades as it goes (owner, 2026-10-15). */
        double dpi = rubraview_pal_window_dpi_scale(app->window);
        rubraview_pal_rect_t strip = strip_rect(app);
        rubraview_pal_render_fill_rect(app->renderer, strip, strip_fade(rubraview_box_fade(COLOR_BAR_FILL,
                                       rubraview_settings_get(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("strip_opacity"))), shown), 0.0);   /* 1.25.0 */
        double lead = rubraview_filmstrip_lead(&app->filmstrip);
        int32_t current = current_page_index(app);
        rubraview_virtual_range_t visible = rubraview_filmstrip_visible(&app->filmstrip);
        for (size_t i = 0; i < visible.count; ++i) {
            size_t index = visible.first + i;
            if (index >= page_count(app)) break;
            double x = lead + (double)index * app->filmstrip.thumb_extent - app->filmstrip.scroll_offset;
            rubraview_pal_rect_t cell = { x + 4.0 * dpi, strip.y + 6.0 * dpi,
                                          app->filmstrip.thumb_extent - 8.0 * dpi, strip.height - 12.0 * dpi };
            rubraview_texture_t *tex = NULL;
            double tw = 0.0, th = 0.0;
            if (index < app->strip_cell_count && app->strip_cells[index].texture) {
                tex = app->strip_cells[index].texture;
                int32_t w = 0, h = 0;
                rubraview_pal_texture_size(tex, &w, &h);
                tw = (double)w; th = (double)h;
            } else if (app->pages[index].loaded && app->pages[index].texture) {   /* the page read already */
                tex = app->pages[index].texture;
                tw = (double)app->pages[index].width; th = (double)app->pages[index].height;
            }
            if (tex && tw > 0.0 && th > 0.0) {
                double s = fmin(cell.width / tw, cell.height / th);   /* whole, its own shape */
                rubraview_mat3x2_t fit = rubraview_mat3x2_multiply(
                    rubraview_mat3x2_scale(s, s),
                    rubraview_mat3x2_translate(cell.x + (cell.width - tw * s) * 0.5, cell.y + (cell.height - th * s) * 0.5));
                rubraview_pal_render_draw_texture_opacity(app->renderer, tex, fit, RUBRAVIEW_INTERP_LINEAR, shown);
            } else {
                char number[24];
                int n = snprintf(number, sizeof(number), "%zu", index + 1);
                rubraview_pal_render_fill_rect(app->renderer, cell, strip_fade(COLOR_TILE_FILL, shown), 3.0);
                if (n > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)number, .size = (size_t)n }, cell,
                                                          14.0 * dpi, strip_fade(COLOR_TEXT, shown), RUBRAVIEW_TEXT_CENTER);
            }
            bool here = (int32_t)index == current;
            bool over = pal_rect_has(cell, app->pointer_x, app->pointer_y);
            if (here || over) {
                rubraview_pal_render_stroke_rect(app->renderer, cell, strip_fade(here ? COLOR_TILE_CURRENT : COLOR_BOX_BORDER, shown),
                                                 here ? 3.0 * dpi : 1.5 * dpi, 3.0);
            }
        }
    }

}

void draw_chrome(app_state_t *app, double win_w, double win_h) {
    rubraview_tile_metrics_t metrics = tile_metrics(rubraview_pal_window_dpi_scale(app->window));

    /* Filmstrip (§3.1): under the boxes, over the picture. */
    draw_wheel_strip(app);

    draw_edge_nav(app, win_w, win_h);

    /* Floating boxes (§3.6); none while they are hidden (D-110). */
    toolbox_refresh(app);
    if (!app->chrome_hidden && app->toolbox.state != RUBRAVIEW_BOX_DETACHED) draw_box(app, &app->toolbox, &metrics, NULL, 0, NULL);
    /* 1.21.0: the menu box is not drawn; its tree is the right click's lower rows */

    if (!app->chrome_hidden && app->menubox.state != RUBRAVIEW_BOX_COLLAPSED && !app->menubox.headless) {
        char crumb[128];
        proven_u8str_view_t text = rubraview_menu_breadcrumb(&app->menu, crumb, sizeof(crumb));
        /* Beside the anchor, on its row: the grid is below or above it now. */
        rubraview_rect_t anchor = rubraview_box_anchor_rect(&app->menubox, &metrics);
        rubraview_rect_t pin = rubraview_box_pin_rect(&app->menubox, &metrics);
        rubraview_rect_t grip = rubraview_box_grip_rect(&app->menubox, &metrics);
        double after = anchor.x + anchor.width;   /* past the pin and the grip, when they are on the right */
        if (pin.x > anchor.x && pin.x + pin.width > after) after = pin.x + pin.width;
        if (grip.width > 0.0 && grip.x > anchor.x && grip.x + grip.width > after) after = grip.x + grip.width;
        rubraview_pal_rect_t label = { after + metrics.gutter, anchor.y,
                                       metrics.tile_size * 6.0, anchor.height };
        rubraview_pal_render_draw_text(app->renderer, text, label, metrics.tile_size * 0.2,
                                       COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    }

    draw_listwin(app, win_w, win_h);
    draw_recentwin(app, win_w, win_h);
    draw_favwin(app, win_w, win_h);   /* 1.24.0 */

    draw_titlebar(app, win_w);

    /* §3.17.1: an unobtrusive prompt offering the remembered page. */
    if (app->resume_offer) {
        double bar_h = 40.0 * rubraview_pal_window_dpi_scale(app->window);
        rubraview_pal_rect_t prompt = { win_w * 0.25, win_h * 0.5 - bar_h * 0.5, win_w * 0.5, bar_h };
        rubraview_pal_render_fill_rect(app->renderer, prompt, COLOR_BOX_FILL, 3.0);
        rubraview_pal_render_stroke_rect(app->renderer, prompt, COLOR_BOX_BORDER, 1.0, 3.0);

        char line[128];
        int written = snprintf(line, sizeof(line), T("Resume page %d / %zu  (Enter)"),
                               app->resume_page + 1, page_count(app));
        if (written > 0) {
            rubraview_pal_render_draw_text(app->renderer,
                                           (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)written },
                                           prompt, bar_h * 0.4, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
    }

    draw_subtitle(app, win_w, win_h);

    /* OSD (§3.1), skipped once it has faded out entirely. */
    /* Only when asked for with Shift+I: it no longer comes up by itself
       at every page (owner, 2026-10-08). */
    if (app->osd.always_on && rubraview_osd_opacity(&app->osd) > 0.01) {
        int32_t page = current_page_index(app);
        if (page >= 0 && (size_t)page < page_count(app)) {
            /* The file's name at the left; its size, resolution and place at the right, the place last (owner, 2026-10-16). */
            const app_page_t *pg = &app->pages[page];
            proven_u8str_view_t name = app->source.pages[page].name;
            if (app->osd_size_page != page) {
                /* Read once a page: from the archive's index, or the file's own entry. */
                app->osd_size_page = page;
                app->osd_size_bytes = 0;
                proven_u8str_view_t path = app->source.pages[page].path;
                rubraview_fs_entry_t entry;
                if (path.size == 0) app->osd_size_bytes = rubraview_page_source_entry_size(&app->source, (size_t)page);
                else if (rubraview_pal_fs_stat(app->arena, path, &entry)) app->osd_size_bytes = entry.size_bytes;
            }
            bool playing = app->media && page == app->media_page;
            bool picture = pg->loaded && !playing;
            char right_buf[192];
            proven_u8str_view_t right = rubraview_osd_right(right_buf, sizeof(right_buf), (size_t)page, page_count(app),
                                                picture ? real_w(pg) : 0, picture ? real_h(pg) : 0,
                                                app->osd_size_bytes, picture && pg->reduced,
                                                playing ? app->media_position : 0.0,
                                                playing ? app->media_info.duration_seconds : 0.0);
            /* RV-123 (RFC-0005 §6): a text file says its encoding and how far through it is. */
            char text_buf[256];
            int through = text_percent(app);
            if (through >= 0 && text_encoding_shown(app)) {
                int n = snprintf(text_buf, sizeof(text_buf), T("%s, %d %% through  %.*s"), text_encoding_shown(app), through, (int)right.size, right.ptr);
                if (n > 0 && (size_t)n < sizeof(text_buf)) right = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)text_buf, .size = (size_t)n };
            }

            /* The alpha byte carries the fade, so the whole overlay
               dims together rather than popping out. */
            uint32_t alpha = (uint32_t)(rubraview_osd_opacity(&app->osd) * 255.0) & 0xFFu;
            double dpi = rubraview_pal_window_dpi_scale(app->window);
            double bar_h = 28.0 * dpi, pad = 10.0 * dpi, font = bar_h * 0.5;
            double bottom = win_h - bar_h - (app->filmstrip.visible ? FILMSTRIP_THUMB : 0.0);
            rubraview_pal_rect_t back = { 0.0, bottom, win_w, bar_h };
            rubraview_pal_render_fill_rect(app->renderer, back, (alpha / 2u) << 24, 0.0);
            uint32_t ink = (alpha << 24) | (COLOR_TEXT & 0x00FFFFFFu);

            double right_w = 0.0;
            if (!rubraview_pal_render_measure_text(app->renderer, right, font, &right_w)) right_w = (double)right.size * font * 0.55;
            rubraview_pal_rect_t right_box = { win_w - pad - right_w - 2.0, bottom, right_w + 2.0, bar_h };
            rubraview_pal_render_draw_text(app->renderer, right, right_box, font, ink, RUBRAVIEW_TEXT_LEFT);

            /* The name in what is left: whole if it fits, else its start, "...", and its last ten characters. */
            double room = right_box.x - 2.0 * pad;
            char name_buf[1024];
            proven_u8str_view_t shown = name;
            double w = 0.0;
            if (room > 0.0 && rubraview_pal_render_measure_text(app->renderer, name, font, &w) && w > room) {
                size_t total = rubraview_utf8_chars(name);
                size_t lo = 0, hi = total > 13 ? total - 13 : 0;   /* head characters that fit */
                while (lo < hi) {
                    size_t mid = (lo + hi + 1) / 2;
                    proven_u8str_view_t trial = rubraview_middle_ellipsis(name_buf, sizeof(name_buf), name, mid, 10);
                    if (rubraview_pal_render_measure_text(app->renderer, trial, font, &w) && w <= room) lo = mid;
                    else hi = mid - 1;
                }
                shown = rubraview_middle_ellipsis(name_buf, sizeof(name_buf), name, lo, 10);
            }
            if (room > 0.0) {
                rubraview_pal_rect_t left_box = { pad, bottom, room, bar_h };
                rubraview_pal_render_draw_text(app->renderer, shown, left_box, font, ink, RUBRAVIEW_TEXT_LEFT);
            }
        }
    }
}

/* RV-076: the cover, blurred, filling the window behind the track — the
   thing that makes a music player look like one rather than like a
   viewer with nothing on screen. Nothing is drawn for a film, or for a
   track whose file carries no cover. */
void draw_music_backdrop(app_state_t *app, int32_t win_w, int32_t win_h) {
    if (!app->media || app->media_info.has_video || !app->music_backdrop) return;
    rubraview_mat3x2_t place = rubraview_mat3x2_scale((double)win_w / 64.0, (double)win_h / 64.0);
    rubraview_pal_render_draw_texture_opacity(app->renderer, app->music_backdrop, place,
                                              RUBRAVIEW_INTERP_LINEAR, 0.45);
}

/* What the file says about the track, along the bottom: the name and who
   made it, then the record it came from, then what the sound itself is.
   A line the file does not answer is left out rather than shown empty. */
/* Whether the analyser has something to draw now: a song playing on its page. */
bool visualizer_live(const app_state_t *app) {
    return app->media && !app->media_info.has_video && !app->media_paused &&
           rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("visualizer")) > 0.5;
}

/* The music page's analyser and lyrics (owner, 2026-09-30). The analyser
   is a strip above the song's words: 64 bands with falling peaks, or the
   wave itself; the lyrics are the line being sung with three before and
   three after, near the top, and a click on one goes there. */
void draw_music_extras(app_state_t *app, int32_t win_w, int32_t win_h) {
    for (size_t i = 0; i < 7; ++i) app->lyric_row_index[i] = -1;
    if (!app->media || app->media_info.has_video) return;
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double chrome = 56.0 * dpi + (app->filmstrip.visible ? FILMSTRIP_THUMB : 0.0);

    int viz = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("visualizer")));
    if (viz > 0) {
        double h = 64.0 * dpi, w = (double)win_w * 0.6;
        double x0 = ((double)win_w - w) * 0.5, y0 = (double)win_h - chrome - 96.0 * dpi - h;
        static float samples[RUBRAVIEW_FFT_SIZE], imag[RUBRAVIEW_FFT_SIZE];
        size_t n = rubraview_pal_audio_tap_read(samples, RUBRAVIEW_FFT_SIZE);
        double now = rubraview_pal_time_now_seconds();
        double dt = app->viz_last > 0.0 ? now - app->viz_last : 0.0;
        app->viz_last = now;
        uint32_t ink = (COLOR_TILE_CURRENT & 0x00FFFFFFu) | 0xC0000000u;
        if (viz == 1) {
            if (n == RUBRAVIEW_FFT_SIZE && !app->media_paused) {
                memset(imag, 0, sizeof(imag));
                rubraview_apply_hann(samples, RUBRAVIEW_FFT_SIZE);
                rubraview_fft(samples, imag);
                double rate = app->media_info.audio_sample_rate > 0 ? (double)app->media_info.audio_sample_rate : 48000.0;
                rubraview_spectrum_bands(samples, imag, rate, -80.0, app->viz_bands, RUBRAVIEW_SPECTRUM_BANDS);
            } else {
                for (size_t b = 0; b < RUBRAVIEW_SPECTRUM_BANDS; ++b) app->viz_bands[b] = -80.0f;
            }
            rubraview_spectrum_peaks(app->viz_bands, app->viz_peaks, RUBRAVIEW_SPECTRUM_BANDS, dt > 0.5 ? 0.5 : dt, 40.0);
            double bw = w / RUBRAVIEW_SPECTRUM_BANDS;
            for (size_t b = 0; b < RUBRAVIEW_SPECTRUM_BANDS; ++b) {
                double level = (app->viz_bands[b] + 80.0) / 80.0;
                double peak = (app->viz_peaks[b] + 80.0) / 80.0;
                if (level < 0.0) level = 0.0;
                if (level > 1.0) level = 1.0;
                if (peak < 0.0) peak = 0.0;
                if (peak > 1.0) peak = 1.0;
                rubraview_pal_rect_t bar = { x0 + b * bw + 1.0, y0 + h * (1.0 - level), bw - 2.0, h * level };
                if (bar.height > 0.5) rubraview_pal_render_fill_rect(app->renderer, bar, ink, 0.0);
                rubraview_pal_rect_t cap = { x0 + b * bw + 1.0, y0 + h * (1.0 - peak) - 2.0 * dpi, bw - 2.0, 2.0 * dpi };
                rubraview_pal_render_fill_rect(app->renderer, cap, COLOR_TEXT, 0.0);
            }
        } else if (n > 0) {
            /* The wave: the newest samples across the strip, a dot a column. */
            size_t columns = (size_t)w / 2u;
            if (columns > n) columns = n;
            for (size_t c = 0; c < columns; ++c) {
                float v = samples[n - columns + c];
                if (v > 1.0f) v = 1.0f;
                if (v < -1.0f) v = -1.0f;
                double y = y0 + h * 0.5 - (double)v * h * 0.5;
                rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ x0 + (double)c * 2.0, y - dpi, 2.0, 2.0 * dpi }, ink, 0.0);
            }
        }
    }

    const rubraview_lyrics_t *lyrics = &app->music_lyrics;
    if (lyrics->count > 0) {
        /* Untimed words (USLT and the like) follow the song through its
           length, none of them marked as the one being sung. */
        int32_t now = lyrics->untimed
            ? rubraview_lyrics_untimed_index(lyrics, app->media_position, app->media_info.duration_seconds)
            : rubraview_lyrics_index_at(lyrics, app->media_position);
        double size = 18.0 * dpi, row_h = size * 1.6;
        double top = (double)win_h * 0.08;
        rubraview_pal_rect_t band = { (double)win_w * 0.1, top - size * 0.3, (double)win_w * 0.8, row_h * 7.0 + size * 0.6 };
        rubraview_pal_render_fill_rect(app->renderer, band, 0x80101010u, 6.0);
        for (int32_t k = -3; k <= 3; ++k) {
            int32_t index = (now < 0 ? 0 : now) + k;
            if (now < 0 && k < 0) continue;
            if (index < 0 || (size_t)index >= lyrics->count) continue;
            proven_u8str_view_t text = lyrics->lines[index].text;
            rubraview_pal_rect_t row = { band.x, top + (double)(k + 3) * row_h, band.width, row_h };
            bool current = index == now && !lyrics->untimed;
            rubraview_pal_render_draw_text(app->renderer, text.size > 0 ? text : PROVEN_LIT("\xE2\x99\xAA"), row,
                                           current ? size * 1.1 : size * 0.85,
                                           current ? COLOR_TEXT : 0x90F0F0F0u, RUBRAVIEW_TEXT_CENTER);
            app->lyric_row_index[k + 3] = index;
            app->lyric_row_rect[k + 3] = (rubraview_rect_t){ row.x, row.y, row.width, row.height };
        }
    }
}

void draw_music_words(app_state_t *app, int32_t win_w, int32_t win_h) {
    if (!app->media || app->media_info.has_video || !app->music_tags_read) return;
    rubraview_tags_t cue_tags = app->music_tags;
    if (app->music_cue.count > 1) {
        /* With a `.cue`, the track playing is named, not the whole file. */
        int32_t track = rubraview_cue_track_at(&app->music_cue, app->media_position);
        if (track >= 0) {
            const rubraview_cue_track_t *ct = &app->music_cue.tracks[track];
            if (ct->title.size > 0) cue_tags.title = ct->title;
            if (ct->performer.size > 0) cue_tags.artist = ct->performer;
            else if (app->music_cue.album_performer.size > 0) cue_tags.artist = app->music_cue.album_performer;
            if (app->music_cue.album_title.size > 0) cue_tags.album = app->music_cue.album_title;
            static char number[16];
            int n = snprintf(number, sizeof(number), "%d/%zu", ct->number, app->music_cue.count);
            if (n > 0) cue_tags.track_number = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)number, .size = (size_t)n };
        }
    }
    const rubraview_tags_t *t = &cue_tags;
    if (t->title.size == 0 && t->artist.size == 0 && t->album.size == 0 && t->codec.size == 0) return;

    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double size = 17.0 * dpi;

    /* The block sits **above** the seek bar and the status line, not
       across them: how many lines there are is known first, and the
       whole thing is placed from the bottom up. */
    bool has_name = t->title.size > 0 || t->artist.size > 0;
    bool has_record = t->album.size > 0 || t->year.size > 0 || t->track_number.size > 0;
    double height = (has_name ? size * 1.6 : 0.0) + (has_record ? size * 1.4 : 0.0) + size * 1.4;
    double chrome = 56.0 * dpi + (app->filmstrip.visible ? FILMSTRIP_THUMB : 0.0);
    double bottom = (double)win_h - chrome - height - 8.0 * dpi;
    if (bottom < 0.0) bottom = 0.0;

    char line[320];
    int n = 0;
    /* The name, and who made it. */
    if (has_name) {
        if (t->title.size > 0 && t->artist.size > 0) {
            n = snprintf(line, sizeof(line), "%.*s  —  %.*s",
                         (int)t->title.size, t->title.ptr, (int)t->artist.size, t->artist.ptr);
        } else {
            proven_u8str_view_t one = t->title.size > 0 ? t->title : t->artist;
            n = snprintf(line, sizeof(line), "%.*s", (int)one.size, one.ptr);
        }
        if (n > 0) {
            rubraview_pal_rect_t rect = { 0.0, bottom, (double)win_w, size * 1.6 };
            rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n },
                                           rect, size, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
            bottom += size * 1.6;
        }
    }
    /* The record, its year, and the track's number on it. */
    if (has_record) {
        n = snprintf(line, sizeof(line), "%.*s%s%.*s%s%s%.*s",
                     (int)t->album.size, t->album.ptr,
                     t->album.size > 0 && t->year.size > 0 ? "  (" : "",
                     (int)t->year.size, t->year.ptr,
                     t->album.size > 0 && t->year.size > 0 ? ")" : "",
                     t->track_number.size > 0 ? "   no. " : "",
                     (int)t->track_number.size, t->track_number.ptr);
        if (n > 0) {
            rubraview_pal_rect_t rect = { 0.0, bottom, (double)win_w, size * 1.4 };
            rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n },
                                           rect, size * 0.8, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
            bottom += size * 1.4;
        }
    }
    /* What the sound is. */
    {
        char what[160];
        int w = 0;
        if (t->codec.size > 0) w += snprintf(what + w, sizeof(what) - (size_t)w, "%.*s", (int)t->codec.size, t->codec.ptr);
        if (t->bitrate_kbps > 0) w += snprintf(what + w, sizeof(what) - (size_t)w, T("%s%u kbps"), w > 0 ? "  ·  " : "", t->bitrate_kbps);
        if (t->sample_rate > 0) {
            w += snprintf(what + w, sizeof(what) - (size_t)w, T("%s%.1f kHz"), w > 0 ? "  ·  " : "",
                          (double)t->sample_rate / 1000.0);
        }
        if (t->bits_per_sample > 0) w += snprintf(what + w, sizeof(what) - (size_t)w, T("  ·  %u bit"), t->bits_per_sample);
        if (t->channels > 0) {
            w += snprintf(what + w, sizeof(what) - (size_t)w, "  ·  %s",
                          t->channels == 1 ? "mono" : t->channels == 2 ? "stereo" : "multi");
        }
        if (w > 0) {
            rubraview_pal_rect_t rect = { 0.0, bottom, (double)win_w, size * 1.4 };
            rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)what, .size = (size_t)w },
                                           rect, size * 0.75, 0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
        }
    }
}

/* The strip: the page at the top of the window and those under it, as
   far as the window reaches, all at the strip's width. */
void draw_strip(app_state_t *app, int32_t win_w, int32_t win_h) {
    double width = strip_width(app, (double)win_w);
    /* Wider than the window (zoomed in): the pan moves it sideways, inside its edges. */
    double slack = width > (double)win_w ? (width - (double)win_w) / 2.0 : 0.0;
    if (app->pan_x > slack) app->pan_x = slack;
    if (app->pan_x < -slack) app->pan_x = -slack;
    double x0 = ((double)win_w - width) / 2.0 + app->pan_x;
    double y = content_top(app) - app->strip_offset;   /* RV-195: from under a pinned title bar */
    rubraview_interpolation_t interp = page_interpolation(app);

    for (size_t i = app->spread_index; i < page_count(app) && y < (double)win_h; ++i) {
        double h = strip_page_height(app, i);
        app_page_t *page = &app->pages[i];
        if (page->loaded && page->texture && !page->skipped) {
            double ow = 0.0, oh = 0.0;
            rubraview_orientation_apply_size(app->orientation, (double)picture_w(page), (double)picture_h(page), &ow, &oh);
            double scale = ow > 0.0 ? width / ow : 1.0;
            rubraview_mat3x2_t place = rubraview_mat3x2_multiply(
                rubraview_mat3x2_multiply(
                    rubraview_orientation_matrix(app->orientation, (double)picture_w(page), (double)picture_h(page)),
                    rubraview_mat3x2_scale(scale, scale)),
                rubraview_mat3x2_translate(x0, y));
            rubraview_pal_render_draw_texture_opacity(app->renderer, page->texture,
                                                      rubraview_mat3x2_multiply(page_texture_prescale(page), place),
                                                      interp, 1.0);
            /* A long page held reduced (D-39): the part on screen sharper,
               for the page across the window's middle (the tiles serve one page). */
            if (page->reduced && y <= (double)win_h * 0.5 && y + h > (double)win_h * 0.5) {
                draw_page_tiles(app, 0, (int32_t)i, page, place, scale, win_w, win_h, 1.0, interp);
            }
        }
        y += h;
    }
}

/* Draws one spread at a given opacity. Splitting this out is what makes
   §3.2.5's cross-fade possible: the outgoing spread is drawn first at a
   falling opacity and the incoming one over it at a rising one. */
/* One spread placed in the window, as every caller wants it placed.
   RV-106: with "Enlarge by whole steps only" a picture shown larger than
   it is takes a whole number of screen pixels a pixel (2x, 3x, ...) — the
   next one down from what the fit and the zoom ask — so no row or column
   of a small drawing comes out wider than its neighbour. A film is left
   as it is. */
/* RV-195 (owner, 2026-10-30: "제목 표시줄이 고정일 때는 ... 그 아래 영역에만 보이게"): with our own title bar pinned, what
   is shown has the window below it; the bar is not laid over the picture or the film. */
double content_top(const app_state_t *app) {
    if (!app->frameless || app->chrome_hidden) return 0.0;
    return rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("titlebar_pinned")) > 0.5 ? app->titlebar.height : 0.0;
}

static rubraview_composition_t compose_below(rubraview_composition_t comp, double top) {
    for (size_t i = 0; i < comp.count; ++i) comp.commands[i].transform.f += top;
    return comp;
}

rubraview_composition_t compose_view(app_state_t *app, const rubraview_spread_t *spread,
                                            const rubraview_page_size_t *left, const rubraview_page_size_t *right,
                                            double win_w, double win_h) {
    double top = content_top(app);
    if (top >= win_h) top = 0.0;
    win_h -= top;
    rubraview_composition_t comp = rubraview_compose_spread(spread, left, right, win_w, win_h,
                                                            app->fit_mode, GUTTER, app->zoom, app->pan_x, app->pan_y);
    if (!g_whole_zoom || comp.count == 0 || app->media) return compose_below(comp, top);
    double scale = comp.commands[0].scale;
    double whole = floor(scale + 1e-6);
    if (scale <= 1.0 || whole < 1.0 || scale - whole < 1e-6) return compose_below(comp, top);
    return compose_below(rubraview_compose_spread(spread, left, right, win_w, win_h,
                                                  app->fit_mode, GUTTER, app->zoom * whole / scale, app->pan_x, app->pan_y), top);
}

void draw_spread(app_state_t *app, size_t spread_index, double opacity,
                        int32_t win_w, int32_t win_h) {
    if (spread_index >= app->layout.count) return;
    {
        const rubraview_spread_t *spread = &app->layout.spreads[spread_index];

        app_page_t *left = ensure_page_loaded(app, spread->left_index);
        app_page_t *right = ensure_page_loaded(app, spread->right_index);

        rubraview_page_size_t left_size = {0}, right_size = {0};
        if (left && left->loaded) {
            rubraview_orientation_apply_size(app->orientation, (double)picture_w((left)), (double)picture_h((left)),
                                             &left_size.width, &left_size.height);
        }
        if (right && right->loaded) {
            rubraview_orientation_apply_size(app->orientation, (double)picture_w((right)), (double)picture_h((right)),
                                             &right_size.width, &right_size.height);
        }

        rubraview_composition_t comp = compose_view(
            app, spread,
            (left && left->loaded) ? &left_size : NULL,
            (right && right->loaded) ? &right_size : NULL,
            (double)win_w, (double)win_h);
        /* RV-106: a slide or a zoom moves the whole spread about the window's middle. */
        rubraview_mat3x2_t pose = rubraview_mat3x2_identity();
        if (g_pose_on) {
            pose = rubraview_mat3x2_multiply(
                rubraview_mat3x2_multiply(rubraview_mat3x2_translate(-win_w * 0.5, -win_h * 0.5),
                                          rubraview_mat3x2_scale(g_pose.scale, g_pose.scale)),
                rubraview_mat3x2_translate(win_w * 0.5 + g_pose.dx, win_h * 0.5));
            for (size_t i = 0; i < comp.count; ++i) {
                comp.commands[i].transform = rubraview_mat3x2_multiply(comp.commands[i].transform, pose);
            }
        }

        rubraview_interpolation_t interp = page_interpolation(app);

        for (size_t i = 0; i < comp.count; ++i) {
            const rubraview_draw_command_t *cmd = &comp.commands[i];
            if (cmd->page_index < 0 || (size_t)cmd->page_index >= page_count(app)) continue;
            app_page_t *page = &app->pages[cmd->page_index];
            if (!page->loaded) continue;

            bool whole_page = (cmd->src_left <= 0.0) &&
                              (cmd->src_right >= left_size.width - 0.5 || comp.count == 2);

            if (whole_page) {
                /* Source pixels -> oriented space -> screen. */
                rubraview_mat3x2_t oriented = rubraview_mat3x2_multiply(
                    rubraview_mat3x2_multiply(page_texture_prescale(page),
                        rubraview_orientation_matrix(app->orientation, (double)picture_w(page), (double)picture_h(page))),
                    cmd->transform);
                if (cmd->page_index == app->media_page) {
                    /* A DVD's picture subtitles are drawn in the film's
                       own frame, so they need the film's place on screen. */
                    app->video_transform = oriented;
                    app->video_transform_ok = true;
                    app->video_page_w = page->width;
                    app->video_page_h = page->height;
                }
                rubraview_pal_render_draw_texture_opacity(app->renderer, page->texture, oriented, interp, opacity);
                if (opacity >= 1.0 && !g_pose_on) {
                    /* D-40, D-81: zoomed past the texture, the sharper tiles on top. */
                    draw_page_tiles(app, (int)i, cmd->page_index, page,
                                    rubraview_mat3x2_multiply(
                                        rubraview_orientation_matrix(app->orientation, (double)picture_w(page),
                                                                     (double)picture_h(page)),
                                        cmd->transform),
                                    cmd->scale, win_w, win_h, opacity, interp);
                }

                if (app->pixel_grid && cmd->scale >= PIXEL_GRID_MIN_SCALE) {
                    rubraview_pal_render_draw_pixel_grid(app->renderer, oriented,
                                                         page->width, page->height, 0x40FFFFFFu);
                }
            } else {
                /* A split half of a wide spread (§3.3.7). The command names
                   the half in the picture as the reader sees it; the part
                   of the stored picture that is, and the turn that puts it
                   upright, come from the orientation. */
                /* The command speaks in picture pixels; a reduced texture
                   has fewer, so the rectangle shrinks and the transform
                   grows by the same ratio. */
                rubraview_mat3x2_t pre = page_texture_prescale(page);
                double sl = 0.0, st = 0.0, sr = 0.0, sb = 0.0;
                rubraview_mat3x2_t turn = rubraview_orientation_region(
                    app->orientation, (double)picture_w(page), (double)picture_h(page),
                    cmd->src_left, cmd->src_top, cmd->src_right, cmd->src_bottom, &sl, &st, &sr, &sb);
                rubraview_src_rect_t src = {
                    .left = sl / pre.a, .top = st / pre.d, .right = sr / pre.a, .bottom = sb / pre.d,
                };
                rubraview_pal_render_draw_texture_region(app->renderer, page->texture, src,
                                                         rubraview_mat3x2_multiply(
                                                             rubraview_mat3x2_multiply(pre, turn), cmd->transform),
                                                         interp);
            }
        }
    }
}
