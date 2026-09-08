/*
 * Rubraview application entry point (RFC-0001 §11 milestones M2 and M3).
 *
 * M2 brought up the window, the Direct2D canvas and WIC decoding. M3
 * adds the reading UI on top: the dual floating boxes (RV-020), pointer
 * and wheel semantics (RV-037), keymap.ini loaded from disk (RV-038),
 * the OSD (RV-039), the hover titlebar (RV-040), the slide show
 * (RV-015), non-destructive rotation (RV-041) and the filmstrip
 * (RV-042).
 *
 * Everything the UI decides — hit testing, timers, menu navigation,
 * pointer intent — lives in the portable core and is covered by the host
 * test suite; this file is the wiring, and the drawing calls behind the
 * PAL.
 */
#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "rubraview/core.h"
#include "rubraview/path.h"
#include "rubraview/keymap.h"
#include "rubraview/layout.h"
#include "rubraview/viewport.h"
#include "rubraview/compositor.h"
#include "rubraview/transform.h"
#include "rubraview/slideshow.h"
#include "rubraview/ui_input.h"
#include "rubraview/ui_box.h"
#include "rubraview/ui_menu.h"
#include "rubraview/ui_chrome.h"
#include "rubraview/filmstrip.h"
#include "rubraview/pal/pal_window.h"
#include "rubraview/pal/pal_render.h"
#include "rubraview/pal/pal_image.h"
#include "rubraview/pal/pal_fs.h"
#include "rubraview/pal/pal_time.h"

#define APP_ARENA_BYTES (64u * 1024u * 1024u)
#define IMAGE_FILTER "*.jpg;*.jpeg;*.png;*.webp;*.gif;*.bmp;*.tif;*.tiff;*.ico"
#define PIXEL_GRID_MIN_SCALE 4.0 /* §3.5: the grid appears from 400% zoom */
#define ZOOM_STEP 1.1
#define PAN_STEP 60.0
#define GUTTER 8.0
#define KEYMAP_MAX_BYTES (256u * 1024u)
#define FILMSTRIP_THUMB 120.0
#define TOOLBOX_TILES 8
#define MENU_ROOT_TILES 4

/* Metro palette (§3.6.4): flat, high-contrast, no gradients. */
#define COLOR_CANVAS      0xFF101010u
#define COLOR_BOX_FILL    0xE01A1A1Au
#define COLOR_BOX_BORDER  0x30FFFFFFu
#define COLOR_TILE_FILL   0xE0242424u
#define COLOR_TEXT        0xFFF0F0F0u
#define COLOR_BAR_FILL    0xE1141414u
#define COLOR_CLOSE_HOVER 0xFFE81123u

/*
 * The built-in keymap, used when no keymap.ini is present. It is the
 * same format §3.7.5 documents, so a user file overrides it wholesale.
 */
static const char *const DEFAULT_KEYMAP =
    "toggle_fullscreen = F, F11\n"
    "toggle_pixel_grid = G\n"
    "toggle_osd = I\n"
    "toggle_filmstrip = F4\n"
    "toggle_menu = Tab, F1\n"
    "toggle_toolbox = T, F2\n"
    "quit = Escape\n"
    "\n"
    "[navigation]\n"
    "next_page = Right, PageDown, Space, J, D\n"
    "prev_page = Left, PageUp, K, A\n"
    "first_page = Home\n"
    "last_page = End\n"
    "toggle_layout = B\n"
    "toggle_reading_order = M\n"
    "\n"
    "[view]\n"
    "fit_window = 1\n"
    "fit_width = 2\n"
    "fit_height = 3\n"
    "actual_size = 4\n"
    "smart_fit = 5\n"
    "zoom_in = Plus\n"
    "zoom_out = Minus\n"
    "rotate_cw = R\n"
    "flip_horizontal = H\n"
    "flip_vertical = V\n"
    "toggle_nearest = N\n"
    "\n"
    "[slideshow]\n"
    "toggle_slideshow = S, F5\n";

/* U8() only works on string literals; this is its runtime counterpart. */
static u8str_t cstr(const char *s) {
    return (u8str_t){ .ptr = s, .len = strlen(s) };
}

typedef struct app_page {
    rubraview_texture_t *texture;
    int32_t width, height;
    bool loaded;
    bool failed;
} app_page_t;

typedef struct app_state {
    proven_arena_t *arena;
    rubraview_window_t *window;
    rubraview_renderer_t *renderer;
    rubraview_keymap_t keymap;

    rubraview_sibling_index_t siblings;
    app_page_t *pages;

    rubraview_layout_opts_t layout_opts;
    rubraview_layout_result_t layout;
    size_t spread_index;

    rubraview_fit_mode_t fit_mode;
    double zoom;
    double pan_x, pan_y;
    bool pixel_grid;
    bool force_nearest;
    bool needs_relayout;

    /* M3 additions */
    rubraview_orientation_t orientation;   /* RV-041, per view rather than per file */
    rubraview_osd_t osd;                   /* RV-039 */
    rubraview_titlebar_t titlebar;         /* RV-040 */
    rubraview_box_t toolbox;               /* RV-020 */
    rubraview_box_t menubox;
    rubraview_menu_state_t menu;
    rubraview_filmstrip_t filmstrip;       /* RV-042 */
    rubraview_slideshow_t slideshow;       /* RV-015, state machine from RV-032 */
    rubraview_slideshow_item_t *slides;
    bool slideshow_running;
    rubraview_transition_t transition;
    rubraview_cursor_hide_t cursor;
    double last_frame_seconds;
    double pointer_x, pointer_y;
} app_state_t;

/* ---- page loading ---- */

static void unload_all_pages(app_state_t *app) {
    if (!app->pages) return;
    for (size_t i = 0; i < app->siblings.count; ++i) {
        if (app->pages[i].texture) {
            rubraview_pal_texture_destroy(app->pages[i].texture);
        }
        app->pages[i] = (app_page_t){0};
    }
}

static app_page_t *ensure_page_loaded(app_state_t *app, int32_t index) {
    if (index < 0 || (size_t)index >= app->siblings.count) return NULL;
    app_page_t *page = &app->pages[index];
    if (page->loaded || page->failed) return page;

    rubraview_image_load_result_t loaded =
        rubraview_pal_image_load_texture(app->renderer, app->siblings.paths[index], true);
    if (!loaded.ok) {
        page->failed = true;
        return page;
    }

    page->texture = loaded.texture;
    page->width = loaded.width;
    page->height = loaded.height;
    page->loaded = true;
    return page;
}

/* ---- layout ---- */

static void rebuild_layout(app_state_t *app) {
    if (app->siblings.count == 0) return;

    proven_result_mem_mut_t res = proven_arena_alloc(app->arena, app->siblings.count * sizeof(rubraview_page_info_t));
    if (!proven_is_ok(res.err)) return;
    rubraview_page_info_t *infos = (rubraview_page_info_t*)(void*)res.value.ptr;

    double fallback_w = 800.0, fallback_h = 1200.0;
    for (size_t i = 0; i < app->siblings.count; ++i) {
        if (app->pages[i].loaded) {
            fallback_w = (double)app->pages[i].width;
            fallback_h = (double)app->pages[i].height;
            break;
        }
    }

    for (size_t i = 0; i < app->siblings.count; ++i) {
        double w = app->pages[i].loaded ? (double)app->pages[i].width : fallback_w;
        double h = app->pages[i].loaded ? (double)app->pages[i].height : fallback_h;
        /* Pagination sees the page as the reader does, so a rotated
           portrait page is treated as the landscape it now presents. */
        rubraview_orientation_apply_size(app->orientation, w, h, &infos[i].width, &infos[i].height);
    }

    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);

    app->layout = rubraview_layout_compute(app->arena, infos, app->siblings.count,
                                           (double)win_w, (double)win_h, app->layout_opts);
    if (app->spread_index >= app->layout.count && app->layout.count > 0) {
        app->spread_index = app->layout.count - 1;
    }
    app->needs_relayout = false;
}

static size_t spread_index_for_page(const app_state_t *app, int32_t page_index) {
    for (size_t i = 0; i < app->layout.count; ++i) {
        if (app->layout.spreads[i].left_index == page_index ||
            app->layout.spreads[i].right_index == page_index) {
            return i;
        }
    }
    return 0;
}

static int32_t current_page_index(const app_state_t *app) {
    if (app->layout.count == 0) return -1;
    return app->layout.spreads[app->spread_index].left_index;
}

/* ---- actions ---- */

static void reset_view(app_state_t *app) {
    app->zoom = 1.0;
    app->pan_x = 0.0;
    app->pan_y = 0.0;
}

static void note_activity(app_state_t *app) {
    rubraview_osd_notify_activity(&app->osd);
}

static void go_to_spread(app_state_t *app, size_t index) {
    if (app->layout.count == 0) return;
    if (index >= app->layout.count) index = app->layout.count - 1;
    if (index != app->spread_index) {
        rubraview_transition_start(&app->transition);
    }
    app->spread_index = index;
    reset_view(app);
    note_activity(app);

    int32_t page = current_page_index(app);
    if (page >= 0) rubraview_filmstrip_reveal(&app->filmstrip, (size_t)page);
}

static void next_spread(app_state_t *app) {
    if (app->spread_index + 1 < app->layout.count) go_to_spread(app, app->spread_index + 1);
}

static void prev_spread(app_state_t *app) {
    if (app->spread_index > 0) go_to_spread(app, app->spread_index - 1);
}

static bool action_is(u8str_t action, const char *name) {
    size_t n = strlen(name);
    return action.len == n && memcmp(action.ptr, name, n) == 0;
}

static void toggle_slideshow(app_state_t *app) {
    app->slideshow_running = !app->slideshow_running;
    if (app->slideshow_running) {
        app->slideshow.current_index = app->spread_index;
        rubraview_slideshow_resume(&app->slideshow);
        rubraview_pal_window_set_fullscreen(app->window, true); /* §3.2.5 */
    } else {
        rubraview_slideshow_pause(&app->slideshow);
        rubraview_pal_window_set_cursor_visible(app->window, true);
    }
}

static void handle_action(app_state_t *app, u8str_t action) {
    if (action.len == 0) return;
    note_activity(app);

    if (action_is(action, "quit")) {
        rubraview_pal_window_request_close(app->window);
    } else if (action_is(action, "next_page")) {
        next_spread(app);
    } else if (action_is(action, "prev_page")) {
        prev_spread(app);
    } else if (action_is(action, "first_page")) {
        go_to_spread(app, 0);
    } else if (action_is(action, "last_page")) {
        go_to_spread(app, app->layout.count > 0 ? app->layout.count - 1 : 0);
    } else if (action_is(action, "fit_window")) {
        app->fit_mode = RUBRAVIEW_FIT_WINDOW; reset_view(app);
    } else if (action_is(action, "fit_width")) {
        app->fit_mode = RUBRAVIEW_FIT_WIDTH; reset_view(app);
    } else if (action_is(action, "fit_height")) {
        app->fit_mode = RUBRAVIEW_FIT_HEIGHT; reset_view(app);
    } else if (action_is(action, "actual_size")) {
        app->fit_mode = RUBRAVIEW_FIT_ACTUAL_SIZE; reset_view(app);
    } else if (action_is(action, "smart_fit")) {
        app->fit_mode = RUBRAVIEW_FIT_SMART; reset_view(app);
    } else if (action_is(action, "zoom_in")) {
        app->zoom *= ZOOM_STEP;
    } else if (action_is(action, "zoom_out")) {
        app->zoom /= ZOOM_STEP;
        if (app->zoom < 0.01) app->zoom = 0.01;
    } else if (action_is(action, "rotate_cw")) {
        app->orientation = rubraview_orientation_rotate_cw(app->orientation);
        app->needs_relayout = true;
        reset_view(app);
    } else if (action_is(action, "rotate_ccw")) {
        app->orientation = rubraview_orientation_rotate_ccw(app->orientation);
        app->needs_relayout = true;
        reset_view(app);
    } else if (action_is(action, "flip_horizontal")) {
        app->orientation = rubraview_orientation_flip_h(app->orientation);
    } else if (action_is(action, "flip_vertical")) {
        app->orientation = rubraview_orientation_flip_v(app->orientation);
    } else if (action_is(action, "toggle_pixel_grid")) {
        app->pixel_grid = !app->pixel_grid;
    } else if (action_is(action, "toggle_nearest")) {
        app->force_nearest = !app->force_nearest;
    } else if (action_is(action, "toggle_osd")) {
        app->osd.always_on = !app->osd.always_on;
    } else if (action_is(action, "toggle_filmstrip")) {
        app->filmstrip.visible = !app->filmstrip.visible;
    } else if (action_is(action, "toggle_menu")) {
        rubraview_box_click_anchor(&app->menubox);
        rubraview_menu_reset(&app->menu);
    } else if (action_is(action, "toggle_toolbox")) {
        rubraview_box_click_anchor(&app->toolbox);
    } else if (action_is(action, "toggle_slideshow")) {
        toggle_slideshow(app);
    } else if (action_is(action, "toggle_fullscreen")) {
        rubraview_pal_window_set_fullscreen(app->window,
                                            !rubraview_pal_window_is_fullscreen(app->window));
    } else if (action_is(action, "toggle_layout")) {
        int32_t page = current_page_index(app);
        app->layout_opts.mode = (app->layout_opts.mode == RUBRAVIEW_PAGE_LAYOUT_SINGLE)
            ? RUBRAVIEW_PAGE_LAYOUT_DUAL
            : (app->layout_opts.mode == RUBRAVIEW_PAGE_LAYOUT_DUAL
                ? RUBRAVIEW_PAGE_LAYOUT_BOOK
                : RUBRAVIEW_PAGE_LAYOUT_SINGLE);
        rebuild_layout(app);
        if (page >= 0) app->spread_index = spread_index_for_page(app, page);
        reset_view(app);
    } else if (action_is(action, "toggle_reading_order")) {
        int32_t page = current_page_index(app);
        app->layout_opts.direction = (app->layout_opts.direction == RUBRAVIEW_READING_LTR)
            ? RUBRAVIEW_READING_RTL : RUBRAVIEW_READING_LTR;
        rebuild_layout(app);
        if (page >= 0) app->spread_index = spread_index_for_page(app, page);
    }
}

static void dispatch_key(app_state_t *app, rubraview_key_combo_t combo) {
    /* §3.7.1: the slide show's own bindings win while it is running,
       then the viewing context, then the global section. */
    if (app->slideshow_running) {
        u8str_t action = rubraview_keymap_find_action(&app->keymap, U8("slideshow"), combo);
        if (action.len > 0) { handle_action(app, action); return; }
    }
    u8str_t action = rubraview_keymap_find_action(&app->keymap, U8("navigation"), combo);
    if (action.len == 0) action = rubraview_keymap_find_action(&app->keymap, U8("view"), combo);
    if (action.len == 0) action = rubraview_keymap_find_action(&app->keymap, U8("slideshow"), combo);
    handle_action(app, action);
}

/* ---- pointer ---- */

static rubraview_pointer_context_t pointer_context(const app_state_t *app) {
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    return (rubraview_pointer_context_t){
        .window_width = (double)win_w,
        .reading_direction = app->layout_opts.direction,
        .comic_mode = (app->layout_opts.mode != RUBRAVIEW_PAGE_LAYOUT_SINGLE) || app->siblings.count > 1,
        .zoomed_in = app->zoom > 1.001,
        .scrollable_vertically = (app->fit_mode == RUBRAVIEW_FIT_WIDTH),
    };
}

static void apply_intent(app_state_t *app, rubraview_pointer_intent_t intent) {
    switch (intent) {
        case RUBRAVIEW_INTENT_NEXT_PAGE:      next_spread(app); break;
        case RUBRAVIEW_INTENT_PREV_PAGE:      prev_spread(app); break;
        case RUBRAVIEW_INTENT_SKIP_FORWARD:   go_to_spread(app, app->spread_index + 10); break;
        case RUBRAVIEW_INTENT_SKIP_BACKWARD:
            go_to_spread(app, app->spread_index > 10 ? app->spread_index - 10 : 0);
            break;
        case RUBRAVIEW_INTENT_ZOOM_IN:        app->zoom *= ZOOM_STEP; note_activity(app); break;
        case RUBRAVIEW_INTENT_ZOOM_OUT:
            app->zoom /= ZOOM_STEP;
            if (app->zoom < 0.01) app->zoom = 0.01;
            note_activity(app);
            break;
        case RUBRAVIEW_INTENT_SCROLL_VERTICAL: app->pan_y -= PAN_STEP; note_activity(app); break;
        case RUBRAVIEW_INTENT_TOGGLE_FULLSCREEN:
            rubraview_pal_window_set_fullscreen(app->window, !rubraview_pal_window_is_fullscreen(app->window));
            break;
        case RUBRAVIEW_INTENT_TOGGLE_ACTUAL_SIZE:
            app->fit_mode = (app->fit_mode == RUBRAVIEW_FIT_ACTUAL_SIZE)
                ? RUBRAVIEW_FIT_WINDOW : RUBRAVIEW_FIT_ACTUAL_SIZE;
            reset_view(app);
            break;
        case RUBRAVIEW_INTENT_TOGGLE_OVERLAY:
            rubraview_box_click_anchor(&app->toolbox);
            note_activity(app);
            break;
        default: break;
    }
}

/* Returns true when the chrome consumed the click. */
static bool handle_chrome_click(app_state_t *app, double x, double y) {
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    rubraview_tile_metrics_t metrics = rubraview_tile_metrics_default(rubraview_pal_window_dpi_scale(app->window));

    /* §3.21.3: the titlebar's controls take precedence while it is shown. */
    rubraview_titlebar_button_t button = rubraview_titlebar_hit(&app->titlebar, x, y, (double)win_w);
    switch (button) {
        case RUBRAVIEW_TITLEBAR_CLOSE:      rubraview_pal_window_request_close(app->window); return true;
        case RUBRAVIEW_TITLEBAR_MINIMIZE:   rubraview_pal_window_minimize(app->window); return true;
        case RUBRAVIEW_TITLEBAR_MAXIMIZE:   rubraview_pal_window_toggle_maximize(app->window); return true;
        case RUBRAVIEW_TITLEBAR_FULLSCREEN:
            rubraview_pal_window_set_fullscreen(app->window, !rubraview_pal_window_is_fullscreen(app->window));
            return true;
        case RUBRAVIEW_TITLEBAR_CAPTION:    rubraview_pal_window_begin_drag(app->window); return true;
        default: break;
    }

    int32_t tile = rubraview_box_tile_at(&app->menubox, &metrics, x, y);
    if (tile >= 0) {
        u8str_t action;
        rubraview_menu_result_t result = rubraview_menu_activate(&app->menu, tile, &action);
        if (result == RUBRAVIEW_MENU_ACTIVATED) handle_action(app, action);
        return true;
    }

    tile = rubraview_box_tile_at(&app->toolbox, &metrics, x, y);
    if (tile >= 0) {
        /* Toolbox tiles are fixed playback and viewport controls (§3.6.1). */
        static const char *const TOOLBOX_ACTIONS[TOOLBOX_TILES] = {
            "prev_page", "next_page", "zoom_out", "zoom_in",
            "actual_size", "rotate_cw", "toggle_slideshow", "toggle_fullscreen",
        };
        if (tile < TOOLBOX_TILES) handle_action(app, cstr(TOOLBOX_ACTIONS[tile]));
        return true;
    }

    return false;
}

/* ---- rendering ---- */

static void draw_box(app_state_t *app, const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics,
                     const char *const *captions, int32_t caption_count) {
    rubraview_rect_t bounds = rubraview_box_bounds(box, metrics);
    rubraview_pal_rect_t body = { bounds.x, bounds.y, bounds.width, bounds.height };

    rubraview_pal_render_fill_rect(app->renderer, body, COLOR_BOX_FILL, 2.0);
    rubraview_pal_render_stroke_rect(app->renderer, body, COLOR_BOX_BORDER, 1.0, 2.0);

    if (box->state == RUBRAVIEW_BOX_COLLAPSED) {
        rubraview_pal_render_draw_text(app->renderer,
                                       box->kind == RUBRAVIEW_BOX_MENU ? U8("=") : U8("<>"),
                                       body, metrics->anchor_size * 0.45, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        return;
    }

    for (int32_t i = 0; i < box->tile_count; ++i) {
        rubraview_rect_t t = rubraview_box_tile_rect(box, metrics, i);
        rubraview_pal_rect_t tile = { t.x, t.y, t.width, t.height };
        rubraview_pal_render_fill_rect(app->renderer, tile, COLOR_TILE_FILL, 0.0);
        rubraview_pal_render_stroke_rect(app->renderer, tile, COLOR_BOX_BORDER, 1.0, 0.0);
        if (captions && i < caption_count) {
            rubraview_pal_render_draw_text(app->renderer, cstr(captions[i]), tile,
                                           metrics->tile_size * 0.22, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
    }
}

static void draw_chrome(app_state_t *app, double win_w, double win_h) {
    rubraview_tile_metrics_t metrics = rubraview_tile_metrics_default(rubraview_pal_window_dpi_scale(app->window));

    /* Filmstrip (§3.1): tiles come from pages already decoded; dedicated
       low-resolution thumbnail decoding arrives with the asynchronous
       pre-cache worker in M4 (RV-044), where async decode belongs. */
    if (app->filmstrip.visible && app->siblings.count > 0) {
        double strip_h = FILMSTRIP_THUMB * rubraview_pal_window_dpi_scale(app->window);
        rubraview_pal_rect_t strip = { 0.0, win_h - strip_h, win_w, strip_h };
        rubraview_pal_render_fill_rect(app->renderer, strip, COLOR_BAR_FILL, 0.0);

        rubraview_virtual_range_t visible = rubraview_filmstrip_visible(&app->filmstrip);
        for (size_t i = 0; i < visible.count; ++i) {
            size_t index = visible.first + i;
            double x = (double)index * app->filmstrip.thumb_extent - app->filmstrip.scroll_offset;
            rubraview_pal_rect_t cell = { x, strip.y + 4.0, app->filmstrip.thumb_extent - 8.0, strip_h - 8.0 };
            rubraview_pal_render_stroke_rect(app->renderer, cell, COLOR_BOX_BORDER, 1.0, 0.0);

            if (index < app->siblings.count && app->pages[index].loaded) {
                rubraview_mat3x2_t fit = rubraview_mat3x2_multiply(
                    rubraview_mat3x2_scale(cell.width / (double)app->pages[index].width,
                                           cell.height / (double)app->pages[index].height),
                    rubraview_mat3x2_translate(cell.x, cell.y));
                rubraview_pal_render_draw_texture(app->renderer, app->pages[index].texture,
                                                  fit, RUBRAVIEW_INTERP_LINEAR);
            }
        }
    }

    /* Floating boxes (§3.6). */
    static const char *const TOOLBOX_CAPTIONS[TOOLBOX_TILES] = {
        "Prev", "Next", "Zoom-", "Zoom+", "1:1", "Rotate", "Slides", "Full",
    };
    static const char *const MENU_CAPTIONS[MENU_ROOT_TILES] = {
        "Layout", "Fit", "View", "Show",
    };
    draw_box(app, &app->toolbox, &metrics, TOOLBOX_CAPTIONS, TOOLBOX_TILES);
    draw_box(app, &app->menubox, &metrics, MENU_CAPTIONS, MENU_ROOT_TILES);

    if (app->menubox.state != RUBRAVIEW_BOX_COLLAPSED) {
        char crumb[128];
        u8str_t text = rubraview_menu_breadcrumb(&app->menu, crumb, sizeof(crumb));
        rubraview_rect_t bounds = rubraview_box_bounds(&app->menubox, &metrics);
        rubraview_pal_rect_t label = { bounds.x, bounds.y - metrics.tile_size * 0.4, bounds.width, metrics.tile_size * 0.4 };
        rubraview_pal_render_draw_text(app->renderer, text, label, metrics.tile_size * 0.2,
                                       COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    }

    /* Hover titlebar (§3.21.2, §3.21.3). */
    if (app->titlebar.shown) {
        rubraview_pal_rect_t bar = { 0.0, 0.0, win_w, app->titlebar.height };
        rubraview_pal_render_fill_rect(app->renderer, bar, COLOR_BAR_FILL, 0.0);

        int32_t page = current_page_index(app);
        if (page >= 0 && (size_t)page < app->siblings.count) {
            u8str_t name = rubraview_path_basename(app->siblings.paths[page]);
            rubraview_pal_rect_t title = { 12.0, 0.0, win_w * 0.6, app->titlebar.height };
            rubraview_pal_render_draw_text(app->renderer, name, title,
                                           app->titlebar.height * 0.38, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
        }

        static const rubraview_titlebar_button_t BUTTONS[] = {
            RUBRAVIEW_TITLEBAR_MINIMIZE, RUBRAVIEW_TITLEBAR_MAXIMIZE,
            RUBRAVIEW_TITLEBAR_FULLSCREEN, RUBRAVIEW_TITLEBAR_CLOSE,
        };
        static const char *const GLYPHS[] = { "_", "[]", "[ ]", "X" };
        for (size_t i = 0; i < sizeof(BUTTONS) / sizeof(BUTTONS[0]); ++i) {
            rubraview_rect_t r = rubraview_titlebar_button_rect(&app->titlebar, BUTTONS[i], win_w);
            rubraview_pal_rect_t br = { r.x, r.y, r.width, r.height };
            bool hovered = rubraview_rect_contains(r, app->pointer_x, app->pointer_y);
            if (hovered) {
                rubraview_pal_render_fill_rect(app->renderer, br,
                                               BUTTONS[i] == RUBRAVIEW_TITLEBAR_CLOSE ? COLOR_CLOSE_HOVER : COLOR_TILE_FILL,
                                               0.0);
            }
            rubraview_pal_render_draw_text(app->renderer, cstr(GLYPHS[i]), br,
                                           app->titlebar.height * 0.35, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
    }

    /* OSD (§3.1), skipped once it has faded out entirely. */
    if (rubraview_osd_opacity(&app->osd) > 0.01) {
        int32_t page = current_page_index(app);
        if (page >= 0 && (size_t)page < app->siblings.count && app->pages[page].loaded) {
            char line[192];
            u8str_t name = rubraview_path_basename(app->siblings.paths[page]);
            u8str_t text = rubraview_osd_format(line, sizeof(line), name,
                                                app->pages[page].width, app->pages[page].height,
                                                app->zoom * 100.0, (size_t)page, app->siblings.count);

            /* The alpha byte carries the fade, so the whole overlay
               dims together rather than popping out. */
            uint32_t alpha = (uint32_t)(rubraview_osd_opacity(&app->osd) * 255.0) & 0xFFu;
            double bar_h = 28.0 * rubraview_pal_window_dpi_scale(app->window);
            double bottom = win_h - bar_h - (app->filmstrip.visible ? FILMSTRIP_THUMB : 0.0);
            rubraview_pal_rect_t back = { 0.0, bottom, win_w, bar_h };
            rubraview_pal_render_fill_rect(app->renderer, back, (alpha / 2u) << 24, 0.0);
            rubraview_pal_render_draw_text(app->renderer, text, back, bar_h * 0.5,
                                           (alpha << 24) | (COLOR_TEXT & 0x00FFFFFFu),
                                           RUBRAVIEW_TEXT_CENTER);
        }
    }
}

static void render_frame(app_state_t *app) {
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) return;

    rubraview_pal_render_begin(app->renderer, COLOR_CANVAS);

    if (app->layout.count > 0) {
        const rubraview_spread_t *spread = &app->layout.spreads[app->spread_index];

        app_page_t *left = ensure_page_loaded(app, spread->left_index);
        app_page_t *right = ensure_page_loaded(app, spread->right_index);

        rubraview_page_size_t left_size = {0}, right_size = {0};
        if (left && left->loaded) {
            rubraview_orientation_apply_size(app->orientation, (double)left->width, (double)left->height,
                                             &left_size.width, &left_size.height);
        }
        if (right && right->loaded) {
            rubraview_orientation_apply_size(app->orientation, (double)right->width, (double)right->height,
                                             &right_size.width, &right_size.height);
        }

        rubraview_composition_t comp = rubraview_compose_spread(
            spread,
            (left && left->loaded) ? &left_size : NULL,
            (right && right->loaded) ? &right_size : NULL,
            (double)win_w, (double)win_h,
            app->fit_mode, GUTTER, app->zoom, app->pan_x, app->pan_y);

        rubraview_interpolation_t interp = app->force_nearest
            ? RUBRAVIEW_INTERP_NEAREST : RUBRAVIEW_INTERP_LINEAR;

        for (size_t i = 0; i < comp.count; ++i) {
            const rubraview_draw_command_t *cmd = &comp.commands[i];
            if (cmd->page_index < 0 || (size_t)cmd->page_index >= app->siblings.count) continue;
            app_page_t *page = &app->pages[cmd->page_index];
            if (!page->loaded) continue;

            bool whole_page = (cmd->src_left <= 0.0) &&
                              (cmd->src_right >= left_size.width - 0.5 || comp.count == 2);

            if (whole_page) {
                /* Source pixels -> oriented space -> screen. */
                rubraview_mat3x2_t oriented = rubraview_mat3x2_multiply(
                    rubraview_orientation_matrix(app->orientation, (double)page->width, (double)page->height),
                    cmd->transform);
                rubraview_pal_render_draw_texture(app->renderer, page->texture, oriented, interp);

                if (app->pixel_grid && comp.scale >= PIXEL_GRID_MIN_SCALE) {
                    rubraview_pal_render_draw_pixel_grid(app->renderer, oriented,
                                                         page->width, page->height, 0x40FFFFFFu);
                }
            } else {
                /* A split half of a wide spread (§3.3.7). The source
                   sub-rectangle is expressed in oriented coordinates, so
                   the user's own rotation is not applied to this case —
                   splitting and manual rotation together is left to the
                   editing workbench in M6. */
                rubraview_src_rect_t src = {
                    .left = cmd->src_left, .top = cmd->src_top,
                    .right = cmd->src_right, .bottom = cmd->src_bottom,
                };
                rubraview_pal_render_draw_texture_region(app->renderer, page->texture, src, cmd->transform, interp);
            }
        }
    }

    draw_chrome(app, (double)win_w, (double)win_h);

    if (!rubraview_pal_render_end(app->renderer)) {
        unload_all_pages(app);
    }
}

/* ---- per-frame timers ---- */

static void tick_timers(app_state_t *app, double dt) {
    rubraview_osd_tick(&app->osd, dt);
    rubraview_titlebar_tick(&app->titlebar, dt);
    rubraview_transition_tick(&app->transition, dt);

    double grace = 0.5; /* §3.6.3 */
    rubraview_box_tick(&app->toolbox, dt, grace);
    rubraview_box_tick(&app->menubox, dt, grace);

    if (app->slideshow_running) {
        if (rubraview_cursor_hide_tick(&app->cursor, dt)) {
            rubraview_pal_window_set_cursor_visible(app->window, false); /* §3.2.5 */
        }
        rubraview_slideshow_event_t event = rubraview_slideshow_tick(&app->slideshow, app->slides, dt);
        if (event == RUBRAVIEW_SLIDESHOW_ADVANCED) {
            go_to_spread(app, app->slideshow.current_index);
        } else if (event == RUBRAVIEW_SLIDESHOW_ENDED) {
            app->slideshow_running = false;
            rubraview_pal_window_set_cursor_visible(app->window, true);
        }
    }
}

/* ---- startup ---- */

static void load_keymap(app_state_t *app) {
    /* §3.7.5: keymap.ini beside the executable overrides the built-in
       bindings wholesale; the defaults apply when it is absent. */
    u8str_t text = rubraview_pal_fs_read_file(app->arena, U8("keymap.ini"), KEYMAP_MAX_BYTES);
    if (text.len == 0) text = cstr(DEFAULT_KEYMAP);
    app->keymap = rubraview_keymap_parse(app->arena, text);
}

static void build_slides(app_state_t *app) {
    if (app->layout.count == 0) return;
    proven_result_mem_mut_t res = proven_arena_alloc(app->arena, app->layout.count * sizeof(rubraview_slideshow_item_t));
    if (!proven_is_ok(res.err)) return;

    app->slides = (rubraview_slideshow_item_t*)(void*)res.value.ptr;
    for (size_t i = 0; i < app->layout.count; ++i) {
        /* Still images for now; animated and video items join the
           sequence with M4 (RV-049) and M5 (RV-061). */
        app->slides[i] = (rubraview_slideshow_item_t){ .kind = RUBRAVIEW_MEDIA_STILL, .duration_seconds = 0.0 };
    }
    app->slideshow = rubraview_slideshow_create(app->layout.count, 3.0, RUBRAVIEW_LOOP_ALL);
    rubraview_slideshow_pause(&app->slideshow);
}

static void open_path(app_state_t *app, u8str_t path) {
    rubraview_fs_entry_t entry;
    if (!rubraview_pal_fs_stat(app->arena, path, &entry)) return;

    u8str_t dir = entry.is_directory ? entry.path : rubraview_path_dirname(entry.path);
    rubraview_fs_listing_t listing = rubraview_pal_fs_list_dir(app->arena, dir);
    if (listing.count == 0) return;

    app->siblings = rubraview_fs_index_siblings(app->arena, &listing,
                                                entry.is_directory ? (u8str_t){ .ptr = "", .len = 0 } : entry.path,
                                                U8(IMAGE_FILTER),
                                                RUBRAVIEW_SORT_NAME_NATURAL, true);
    if (app->siblings.count == 0) return;

    proven_result_mem_mut_t res = proven_arena_alloc(app->arena, app->siblings.count * sizeof(app_page_t));
    if (!proven_is_ok(res.err)) {
        app->siblings.count = 0;
        return;
    }
    app->pages = (app_page_t*)(void*)res.value.ptr;
    memset(app->pages, 0, app->siblings.count * sizeof(app_page_t));

    ensure_page_loaded(app, (int32_t)app->siblings.current);
    rebuild_layout(app);
    app->spread_index = spread_index_for_page(app, (int32_t)app->siblings.current);

    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    double scale = rubraview_pal_window_dpi_scale(app->window);
    app->filmstrip = rubraview_filmstrip_create(app->siblings.count,
                                                FILMSTRIP_THUMB * scale, (double)win_w);
    rubraview_filmstrip_reveal(&app->filmstrip, app->siblings.current);
    build_slides(app);
}

#ifdef _WIN32

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show) {
    (void)instance; (void)previous; (void)command_line; (void)show;

    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
        return 1;
    }

    void *memory = malloc(APP_ARENA_BYTES);
    if (!memory) {
        CoUninitialize();
        return 1;
    }
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)memory, .size = APP_ARENA_BYTES });

    app_state_t app = {0};
    app.arena = &arena;
    app.fit_mode = RUBRAVIEW_FIT_WINDOW;
    app.zoom = 1.0;
    app.orientation = rubraview_orientation_identity();
    app.layout_opts = rubraview_layout_opts_default(RUBRAVIEW_PAGE_LAYOUT_SINGLE, RUBRAVIEW_READING_LTR);

    rubraview_window_config_t window_config = {
        .title = "Rubraview",
        .width = 1280,
        .height = 800,
        .frameless = true, /* §2 invariant 5: zero-margin canvas */
    };
    app.window = rubraview_pal_window_create(&arena, &window_config);
    if (!app.window) {
        free(memory);
        CoUninitialize();
        return 1;
    }

    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app.window, &win_w, &win_h);
    app.renderer = rubraview_pal_render_create(&arena, rubraview_pal_window_native_handle(app.window), win_w, win_h);
    if (!app.renderer) {
        rubraview_pal_window_destroy(app.window);
        free(memory);
        CoUninitialize();
        return 1;
    }

    double dpi = rubraview_pal_window_dpi_scale(app.window);
    load_keymap(&app);
    app.osd = rubraview_osd_create(2.0, 0.5);            /* §3.1 */
    app.titlebar = rubraview_titlebar_create(dpi);       /* §3.21.2 */
    app.toolbox = rubraview_box_create(RUBRAVIEW_BOX_TOOLBOX, (double)win_w - 220.0 * dpi, (double)win_h - 160.0 * dpi, TOOLBOX_TILES);
    app.menubox = rubraview_box_create(RUBRAVIEW_BOX_MENU, 24.0 * dpi, 24.0 * dpi, MENU_ROOT_TILES);
    app.menu = rubraview_menu_create(NULL); /* the tile tree is declared by the menu box's captions for now */
    app.transition = rubraview_transition_create(RUBRAVIEW_TRANSITION_CROSSFADE, 0.25);
    app.cursor = rubraview_cursor_hide_create(1.5);      /* §3.2.5 */
    app.filmstrip = rubraview_filmstrip_create(0, FILMSTRIP_THUMB * dpi, (double)win_w);

    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (argc > 1) {
            char utf8[4096];
            int written = WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, utf8, (int)sizeof(utf8), NULL, NULL);
            if (written > 1) {
                open_path(&app, (u8str_t){ .ptr = utf8, .len = (size_t)(written - 1) });
            }
        }
        LocalFree(argv);
    }

    app.last_frame_seconds = rubraview_pal_time_now_seconds();

    while (!rubraview_pal_window_should_close(app.window)) {
        rubraview_window_event_t event;
        while (rubraview_pal_window_poll_event(app.window, &event)) {
            switch (event.kind) {
                case RUBRAVIEW_WINDOW_EVENT_CLOSE:
                    rubraview_pal_window_request_close(app.window);
                    break;

                case RUBRAVIEW_WINDOW_EVENT_RESIZE:
                    rubraview_pal_render_resize(app.renderer, event.resize.width, event.resize.height);
                    app.filmstrip.viewport_extent = (double)event.resize.width;
                    app.needs_relayout = true;
                    break;

                case RUBRAVIEW_WINDOW_EVENT_DPI_CHANGED:
                    app.titlebar = rubraview_titlebar_create(event.dpi.scale);
                    app.needs_relayout = true;
                    break;

                case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN:
                    dispatch_key(&app, event.key.combo);
                    break;

                case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE: {
                    app.pointer_x = event.mouse.x;
                    app.pointer_y = event.mouse.y;
                    rubraview_titlebar_pointer_moved(&app.titlebar, event.mouse.y);
                    if (rubraview_cursor_hide_notify_motion(&app.cursor)) {
                        rubraview_pal_window_set_cursor_visible(app.window, true);
                    }
                    note_activity(&app);

                    rubraview_tile_metrics_t metrics = rubraview_tile_metrics_default(rubraview_pal_window_dpi_scale(app.window));
                    rubraview_rect_t tb = rubraview_box_bounds(&app.toolbox, &metrics);
                    rubraview_rect_t mb = rubraview_box_bounds(&app.menubox, &metrics);
                    if (rubraview_rect_contains(tb, event.mouse.x, event.mouse.y)) {
                        rubraview_box_hover_enter(&app.toolbox);
                    } else {
                        rubraview_box_hover_leave(&app.toolbox);
                    }
                    if (rubraview_rect_contains(mb, event.mouse.x, event.mouse.y)) {
                        rubraview_box_hover_enter(&app.menubox);
                    } else {
                        rubraview_box_hover_leave(&app.menubox);
                    }
                    break;
                }

                case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN: {
                    if (handle_chrome_click(&app, event.mouse.x, event.mouse.y)) break;

                    rubraview_pointer_context_t ctx = pointer_context(&app);
                    rubraview_pointer_intent_t intent;
                    if (event.mouse.button == RUBRAVIEW_MOUSE_MIDDLE) {
                        intent = rubraview_pointer_middle_click(&ctx);
                    } else if (event.mouse.button == RUBRAVIEW_MOUSE_X1) {
                        intent = rubraview_pointer_side_button(false);
                    } else if (event.mouse.button == RUBRAVIEW_MOUSE_X2) {
                        intent = rubraview_pointer_side_button(true);
                    } else if (event.mouse.button == RUBRAVIEW_MOUSE_RIGHT) {
                        intent = rubraview_pointer_right_click(&ctx);
                    } else {
                        intent = rubraview_pointer_click(&ctx, event.mouse.x, false);
                    }
                    apply_intent(&app, intent);
                    break;
                }

                case RUBRAVIEW_WINDOW_EVENT_MOUSE_WHEEL: {
                    rubraview_pointer_context_t ctx = pointer_context(&app);
                    apply_intent(&app, rubraview_pointer_wheel(&ctx, event.mouse.wheel_delta, event.mouse.modifiers));
                    break;
                }

                default:
                    break;
            }
        }

        if (app.needs_relayout) {
            int32_t page = current_page_index(&app);
            rebuild_layout(&app);
            if (page >= 0) app.spread_index = spread_index_for_page(&app, page);
        }

        double now = rubraview_pal_time_now_seconds();
        double dt = now - app.last_frame_seconds;
        if (dt < 0.0) dt = 0.0;
        app.last_frame_seconds = now;
        tick_timers(&app, dt);

        render_frame(&app);
        rubraview_pal_time_sleep_ms(4);
    }

    unload_all_pages(&app);
    rubraview_pal_render_destroy(app.renderer);
    rubraview_pal_window_destroy(app.window);
    free(memory);
    CoUninitialize();
    return 0;
}

#else

/*
 * The host build has no windowing backend (D-1: Windows only until 1.0).
 * The core and the POSIX filesystem/clock PALs are exercised by the test
 * suite instead; this entry point exists so the file compiles anywhere.
 */
int main(void) {
    return 0;
}

#endif /* _WIN32 */
