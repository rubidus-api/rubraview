/*
 * Rubraview application entry point (RFC-0001 §11 milestone M2).
 *
 * This is the first milestone that produces a running executable: it
 * opens a frameless window, indexes the opened file's siblings, decodes
 * pages through WIC into Direct2D bitmaps, and drives pan/zoom/fit/page
 * navigation entirely through the portable M1 core — the layout engine
 * (RV-021), viewport maths (RV-022), compositor (RV-012), keymap
 * (RV-030), and sibling indexing (RV-035). No image processing happens
 * on the CPU in the viewing path; pan and zoom only update a transform.
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

/*
 * The built-in keymap, used when no keymap.ini is present. It is the
 * same format §3.7.5 documents, so a user file overrides it wholesale.
 */
static const char *const DEFAULT_KEYMAP =
    "toggle_fullscreen = F, F11\n"
    "toggle_pixel_grid = G\n"
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
    "pan_left = Left\n"
    "pan_right = Right\n"
    "pan_up = Up\n"
    "pan_down = Down\n"
    "toggle_nearest = N\n";

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
    app_page_t *pages;          /* one slot per sibling; textures load on demand */

    rubraview_layout_opts_t layout_opts;
    rubraview_layout_result_t layout;
    size_t spread_index;

    rubraview_fit_mode_t fit_mode;
    double zoom;
    double pan_x, pan_y;
    bool pixel_grid;
    bool force_nearest;
    bool needs_relayout;
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

/*
 * Rebuilding the layout needs every page's dimensions, but pages load
 * lazily. Pages that have not loaded yet are described with the current
 * page's shape so pagination stays stable; the layout is rebuilt as
 * neighbours arrive.
 */
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
        if (app->pages[i].loaded) {
            infos[i].width = (double)app->pages[i].width;
            infos[i].height = (double)app->pages[i].height;
        } else {
            infos[i].width = fallback_w;
            infos[i].height = fallback_h;
        }
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

/* Finds the spread showing a given page, so page order is preserved when
   the layout mode changes under the reader's feet. */
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

static void go_to_spread(app_state_t *app, size_t index) {
    if (app->layout.count == 0) return;
    if (index >= app->layout.count) index = app->layout.count - 1;
    app->spread_index = index;
    reset_view(app);
}

static bool action_is(u8str_t action, const char *name) {
    size_t n = strlen(name);
    return action.len == n && memcmp(action.ptr, name, n) == 0;
}

static void handle_action(app_state_t *app, u8str_t action) {
    if (action.len == 0) return;

    if (action_is(action, "quit")) {
        rubraview_pal_window_request_close(app->window);
    } else if (action_is(action, "next_page")) {
        if (app->spread_index + 1 < app->layout.count) go_to_spread(app, app->spread_index + 1);
    } else if (action_is(action, "prev_page")) {
        if (app->spread_index > 0) go_to_spread(app, app->spread_index - 1);
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
    } else if (action_is(action, "pan_left")) {
        app->pan_x += PAN_STEP;
    } else if (action_is(action, "pan_right")) {
        app->pan_x -= PAN_STEP;
    } else if (action_is(action, "pan_up")) {
        app->pan_y += PAN_STEP;
    } else if (action_is(action, "pan_down")) {
        app->pan_y -= PAN_STEP;
    } else if (action_is(action, "toggle_pixel_grid")) {
        app->pixel_grid = !app->pixel_grid;
    } else if (action_is(action, "toggle_nearest")) {
        app->force_nearest = !app->force_nearest;
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

/* The context a key is dispatched in (§3.7.1). M2 has one viewing
   context; video and modal contexts arrive with M3/M5. */
static void dispatch_key(app_state_t *app, rubraview_key_combo_t combo) {
    u8str_t action = rubraview_keymap_find_action(&app->keymap, U8("navigation"), combo);
    if (action.len == 0) {
        action = rubraview_keymap_find_action(&app->keymap, U8("view"), combo);
    }
    handle_action(app, action);
}

/* ---- rendering ---- */

static void render_frame(app_state_t *app) {
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) return;

    rubraview_pal_render_begin(app->renderer, 0xFF101010u);

    if (app->layout.count > 0) {
        const rubraview_spread_t *spread = &app->layout.spreads[app->spread_index];

        app_page_t *left = ensure_page_loaded(app, spread->left_index);
        app_page_t *right = ensure_page_loaded(app, spread->right_index);

        rubraview_page_size_t left_size = {0}, right_size = {0};
        if (left && left->loaded) {
            left_size.width = (double)left->width;
            left_size.height = (double)left->height;
        }
        if (right && right->loaded) {
            right_size.width = (double)right->width;
            right_size.height = (double)right->height;
        }

        rubraview_composition_t comp = rubraview_compose_spread(
            spread,
            (left && left->loaded) ? &left_size : NULL,
            (right && right->loaded) ? &right_size : NULL,
            (double)win_w, (double)win_h,
            app->fit_mode, 8.0, app->zoom, app->pan_x, app->pan_y);

        rubraview_interpolation_t interp = app->force_nearest
            ? RUBRAVIEW_INTERP_NEAREST : RUBRAVIEW_INTERP_LINEAR;

        for (size_t i = 0; i < comp.count; ++i) {
            const rubraview_draw_command_t *cmd = &comp.commands[i];
            if (cmd->page_index < 0 || (size_t)cmd->page_index >= app->siblings.count) continue;
            app_page_t *page = &app->pages[cmd->page_index];
            if (!page->loaded) continue;

            rubraview_src_rect_t src = {
                .left = cmd->src_left, .top = cmd->src_top,
                .right = cmd->src_right, .bottom = cmd->src_bottom,
            };
            rubraview_pal_render_draw_texture_region(app->renderer, page->texture, src, cmd->transform, interp);

            /* §3.5: the pixel grid only makes sense once pixels are big. */
            if (app->pixel_grid && comp.scale >= PIXEL_GRID_MIN_SCALE) {
                rubraview_pal_render_draw_pixel_grid(app->renderer, cmd->transform,
                                                     page->width, page->height, 0x40FFFFFFu);
            }
        }
    }

    if (!rubraview_pal_render_end(app->renderer)) {
        /* The device was lost and the target rebuilt: every bitmap died
           with it, so drop the textures and let them reload on demand. */
        unload_all_pages(app);
    }
}

/* ---- startup ---- */

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

    /* Load the opened page first so the layout has a real page shape. */
    ensure_page_loaded(app, (int32_t)app->siblings.current);
    rebuild_layout(app);
    app->spread_index = spread_index_for_page(app, (int32_t)app->siblings.current);
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
    app.layout_opts = rubraview_layout_opts_default(RUBRAVIEW_PAGE_LAYOUT_SINGLE, RUBRAVIEW_READING_LTR);
    app.keymap = rubraview_keymap_parse(&arena, cstr(DEFAULT_KEYMAP));

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

    /* A path on the command line opens that file and indexes its siblings. */
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

    while (!rubraview_pal_window_should_close(app.window)) {
        rubraview_window_event_t event;
        while (rubraview_pal_window_poll_event(app.window, &event)) {
            switch (event.kind) {
                case RUBRAVIEW_WINDOW_EVENT_CLOSE:
                    rubraview_pal_window_request_close(app.window);
                    break;
                case RUBRAVIEW_WINDOW_EVENT_RESIZE:
                    rubraview_pal_render_resize(app.renderer, event.resize.width, event.resize.height);
                    /* §3.3.5: the window's aspect ratio can flip the
                       effective layout, so pagination is recomputed. */
                    app.needs_relayout = true;
                    break;
                case RUBRAVIEW_WINDOW_EVENT_DPI_CHANGED:
                    app.needs_relayout = true;
                    break;
                case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN:
                    dispatch_key(&app, event.key.combo);
                    break;
                case RUBRAVIEW_WINDOW_EVENT_MOUSE_WHEEL:
                    if (event.mouse.modifiers & RUBRAVIEW_MOD_CTRL) {
                        app.zoom *= (event.mouse.wheel_delta > 0.0) ? ZOOM_STEP : (1.0 / ZOOM_STEP);
                        if (app.zoom < 0.01) app.zoom = 0.01;
                    } else if (event.mouse.wheel_delta > 0.0) {
                        handle_action(&app, U8("prev_page"));
                    } else {
                        handle_action(&app, U8("next_page"));
                    }
                    break;
                default:
                    break;
            }
        }

        if (app.needs_relayout) {
            int32_t page = current_page_index(&app);
            rebuild_layout(&app);
            if (page >= 0) app.spread_index = spread_index_for_page(&app, page);
        }

        render_frame(&app);
        rubraview_pal_time_sleep_ms(4); /* yield; the real frame pacing arrives with M3 */
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
