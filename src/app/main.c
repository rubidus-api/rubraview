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
#include <stdio.h>
#include <string.h>

#include "rubraview/core.h"
#include "rubraview/path.h"
#include "rubraview/keymap.h"
#include "rubraview/layout.h"
#include "rubraview/viewport.h"
#include "rubraview/compositor.h"
#include "rubraview/transform.h"
#include "rubraview/slideshow.h"
#include "rubraview/glob.h"
#include "rubraview/ui_input.h"
#include "rubraview/ui_box.h"
#include "rubraview/ui_menu.h"
#include "rubraview/ui_chrome.h"
#include "rubraview/filmstrip.h"
#include "rubraview/picker.h"
#include "rubraview/default_keymap.h"
#include "rubraview/pagesource.h"
#include "rubraview/precache.h"
#include "rubraview/history.h"
#include "rubraview/comicinfo.h"
#include "proven/job.h"
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
#define ARCHIVE_FILTER "*.cbz;*.zip"
#define MAX_ARCHIVE_BYTES (2048u * 1024u * 1024u)  /* the whole CBZ, held in memory (§3.8.1) */
#define MAX_PAGE_BYTES (512u * 1024u * 1024u)      /* §10.2's per-page zip-bomb guard */
#define PAGE_CACHE_BUDGET (512u * 1024u * 1024u)   /* §7.4's default budget */
#define ESTIMATED_PAGE_BYTES (12u * 1024u * 1024u)
#define PRECACHE_WORKERS 2
#define HISTORY_MAX_ENTRIES 512
#define TOOLBOX_TILES 8
#define MENU_MAX_TILES 12

/* Metro palette (§3.6.4): flat, high-contrast, no gradients. */
#define COLOR_CANVAS      0xFF101010u
#define COLOR_BOX_FILL    0xE01A1A1Au
#define COLOR_BOX_BORDER  0x30FFFFFFu
#define COLOR_TILE_FILL   0xE0242424u
#define COLOR_TEXT        0xFFF0F0F0u
#define COLOR_BAR_FILL    0xE1141414u
#define COLOR_CLOSE_HOVER 0xFFE81123u



/*
 * The Menu Box's category tree (§3.6.2). Children are contiguous, which
 * is what rubraview_menu_tree_t indexes; every leaf names an action the
 * keyboard can already reach, so the tiles and the keymap stay in step.
 */
enum {
    MENU_ROOT_LAYOUT = 0, MENU_ROOT_FIT, MENU_ROOT_VIEW, MENU_ROOT_SHOW,
    MENU_ROOT_COUNT,
};

/* U8() builds a compound literal, which is not a constant initializer at
   file scope; a brace initializer with sizeof for the length is, and it
   still avoids hand-counting any lengths. */
#define MENU_STR(lit) { .ptr = (lit), .len = sizeof(lit) - 1 }

static const rubraview_menu_item_t MENU_ITEMS[] = {
    /* 0 */ { .label = MENU_STR("Layout"), .action = MENU_STR(""), .first_child = 4, .child_count = 3 },
    /* 1 */ { .label = MENU_STR("Fit"),    .action = MENU_STR(""), .first_child = 7, .child_count = 5 },
    /* 2 */ { .label = MENU_STR("View"),   .action = MENU_STR(""), .first_child = 12, .child_count = 4 },
    /* 3 */ { .label = MENU_STR("Show"),   .action = MENU_STR(""), .first_child = 16, .child_count = 3 },

    /* Layout (4..6) */
    { .label = MENU_STR("Single"), .action = MENU_STR("layout_single"), .first_child = -1, .child_count = 0 },
    { .label = MENU_STR("Dual"),   .action = MENU_STR("layout_dual"),   .first_child = -1, .child_count = 0 },
    { .label = MENU_STR("Book"),   .action = MENU_STR("layout_book"),   .first_child = -1, .child_count = 0 },

    /* Fit (7..11) */
    { .label = MENU_STR("Window"), .action = MENU_STR("fit_window"),  .first_child = -1, .child_count = 0 },
    { .label = MENU_STR("Width"),  .action = MENU_STR("fit_width"),   .first_child = -1, .child_count = 0 },
    { .label = MENU_STR("Height"), .action = MENU_STR("fit_height"),  .first_child = -1, .child_count = 0 },
    { .label = MENU_STR("1:1"),    .action = MENU_STR("actual_size"), .first_child = -1, .child_count = 0 },
    { .label = MENU_STR("Smart"),  .action = MENU_STR("smart_fit"),   .first_child = -1, .child_count = 0 },

    /* View (12..15) */
    { .label = MENU_STR("Rotate"), .action = MENU_STR("rotate_cw"),         .first_child = -1, .child_count = 0 },
    { .label = MENU_STR("Flip H"), .action = MENU_STR("flip_horizontal"),   .first_child = -1, .child_count = 0 },
    { .label = MENU_STR("Crisp"),  .action = MENU_STR("toggle_nearest"),    .first_child = -1, .child_count = 0 },
    { .label = MENU_STR("Grid"),   .action = MENU_STR("toggle_pixel_grid"), .first_child = -1, .child_count = 0 },

    /* Show (16..18) */
    { .label = MENU_STR("Slides"), .action = MENU_STR("toggle_slideshow"), .first_child = -1, .child_count = 0 },
    { .label = MENU_STR("Strip"),  .action = MENU_STR("toggle_filmstrip"), .first_child = -1, .child_count = 0 },
    { .label = MENU_STR("Files"),  .action = MENU_STR("open_picker"),      .first_child = -1, .child_count = 0 },
};

static const rubraview_menu_tree_t MENU_TREE = {
    .items = MENU_ITEMS,
    .item_count = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]),
    .root_first = 0,
    .root_count = MENU_ROOT_COUNT,
};

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

    /* Pages come from a folder or a CBZ through the same source (§3.8.1). */
    rubraview_page_source_t source;
    u8str_t                 source_dir;   /* the directory the source lives in */
    u8str_t                 archive_bytes;/* the CBZ held in memory, empty for a folder */
    app_page_t *pages;

    /* M4 */
    proven_job_sys_t       *jobs;
    rubraview_lru_cache_t   page_cache;
    rubraview_precache_t    precache;
    rubraview_history_t     history;
    u8str_t                 history_path;
    rubraview_config_mode_t config_mode;
    rubraview_page_info_t  *comic_page_flags; /* §3.8.5 cover marks, applied on every relayout */
    bool                    resume_offer;   /* §3.17.1: the prompt is showing */
    int32_t                 resume_page;

    rubraview_layout_opts_t layout_opts;
    rubraview_layout_result_t layout;
    size_t spread_index;

    rubraview_fit_mode_t fit_mode;
    double zoom;
    double pan_x, pan_y;
    bool pixel_grid;
    bool force_nearest;
    bool needs_relayout;
    bool fit_lock;       /* §3.4: keep the fit mode and zoom across page changes */
    bool spread_detect;  /* §3.3.4 / Shift+B: AR >= threshold treated as a pre-merged spread */

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

    /* In-app Metro file picker (§3.15.2), RV-043 */
    bool                   picker_open;
    u8str_t                picker_dir;
    rubraview_fs_listing_t picker_listing;
    rubraview_picker_t     picker;
} app_state_t;

static void open_path(app_state_t *app, u8str_t path);

static void update_precache(app_state_t *app);

static size_t page_count(const app_state_t *app) {
    return app->source.page_count;
}

static u8str_t page_display_name(const app_state_t *app, size_t index) {
    if (index >= app->source.page_count) return (u8str_t){ .ptr = "", .len = 0 };
    return app->source.pages[index].name;
}

/* ---- page loading ---- */

static void unload_all_pages(app_state_t *app) {
    if (!app->pages) return;
    for (size_t i = 0; i < page_count(app); ++i) {
        if (app->pages[i].texture) {
            rubraview_pal_texture_destroy(app->pages[i].texture);
        }
        app->pages[i] = (app_page_t){0};
    }
}

static app_page_t *ensure_page_loaded(app_state_t *app, int32_t index) {
    if (index < 0 || (size_t)index >= page_count(app)) return NULL;
    app_page_t *page = &app->pages[index];
    if (page->loaded || page->failed) return page;

    rubraview_page_bytes_t bytes = rubraview_page_source_read(app->arena, &app->source,
                                                              (size_t)index, MAX_PAGE_BYTES);
    if (!bytes.ok) {
        page->failed = true;
        return page;
    }

    /* A folder page is opened by path; an archive page is decoded from
       the bytes the source produced, which never touched the disk. */
    rubraview_image_load_result_t loaded = bytes.from_disk
        ? rubraview_pal_image_load_texture(app->renderer, app->source.pages[index].path, true)
        : rubraview_pal_image_load_texture_from_memory(app->renderer,
                                                       (const uint8_t*)bytes.data.ptr, bytes.data.len, true);
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
    if (page_count(app) == 0) return;

    proven_result_mem_mut_t res = proven_arena_alloc(app->arena, page_count(app) * sizeof(rubraview_page_info_t));
    if (!proven_is_ok(res.err)) return;
    rubraview_page_info_t *infos = (rubraview_page_info_t*)(void*)res.value.ptr;

    double fallback_w = 800.0, fallback_h = 1200.0;
    for (size_t i = 0; i < page_count(app); ++i) {
        if (app->pages[i].loaded) {
            fallback_w = (double)app->pages[i].width;
            fallback_h = (double)app->pages[i].height;
            break;
        }
    }

    for (size_t i = 0; i < page_count(app); ++i) {
        double w = app->pages[i].loaded ? (double)app->pages[i].width : fallback_w;
        double h = app->pages[i].loaded ? (double)app->pages[i].height : fallback_h;
        /* Pagination sees the page as the reader does, so a rotated
           portrait page is treated as the landscape it now presents. */
        rubraview_orientation_apply_size(app->orientation, w, h, &infos[i].width, &infos[i].height);
        /* §3.8.5: a page the archive tagged as a cover never pairs. */
        infos[i].force_standalone = app->comic_page_flags ? app->comic_page_flags[i].force_standalone : false;
    }

    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);

    app->layout = rubraview_layout_compute(app->arena, infos, page_count(app),
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
    /* §3.4 Fit Lock: with the lock on, moving between images of
       different resolutions keeps the chosen zoom instead of resetting
       it — the point of the lock. */
    if (!app->fit_lock) reset_view(app);
    note_activity(app);

    int32_t page = current_page_index(app);
    if (page >= 0) rubraview_filmstrip_reveal(&app->filmstrip, (size_t)page);
}

static void open_sibling_archive(app_state_t *app, bool forward);

static void next_spread(app_state_t *app) {
    if (app->spread_index + 1 < app->layout.count) {
        go_to_spread(app, app->spread_index + 1);
        update_precache(app);
        return;
    }
    /* §3.8.1 point 4: past the last page, continue into the next volume. */
    open_sibling_archive(app, true);
}

static void prev_spread(app_state_t *app) {
    if (app->spread_index > 0) {
        go_to_spread(app, app->spread_index - 1);
        update_precache(app);
        return;
    }
    open_sibling_archive(app, false);
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

/* ---- Metro file picker (§3.15.2) ---- */

#define PICKER_COLUMNS 4
#define PICKER_CRUMB_HEIGHT 44.0
#define PICKER_ACTION_HEIGHT 48.0

/* Lists a directory and orders it the way the viewer orders pages, so
   the picker and the page sequence agree. */
static void picker_navigate(app_state_t *app, u8str_t dir) {
    rubraview_fs_listing_t listing = rubraview_pal_fs_list_dir(app->arena, dir);
    if (listing.count == 0) return;

    /* Directories first, then files, each in natural order — folders are
       what a reader scans for first on a touch screen. */
    rubraview_sort_item_t *items = NULL;
    proven_result_mem_mut_t res = proven_arena_alloc(app->arena, listing.count * sizeof(rubraview_sort_item_t));
    if (proven_is_ok(res.err)) {
        items = (rubraview_sort_item_t*)(void*)res.value.ptr;
        for (size_t i = 0; i < listing.count; ++i) {
            items[i] = (rubraview_sort_item_t){
                .name = listing.entries[i].name,
                .mtime = listing.entries[i].is_directory ? 0 : 1, /* sort key: folders first */
                .ctime = listing.entries[i].ctime,
                .size_bytes = listing.entries[i].size_bytes,
                .tag = (uint64_t)i,
            };
        }
        rubraview_sort_items(items, listing.count, RUBRAVIEW_SORT_NAME_NATURAL, true, NULL);

        proven_result_mem_mut_t ordered_res =
            proven_arena_alloc(app->arena, listing.count * sizeof(rubraview_fs_entry_t));
        if (proven_is_ok(ordered_res.err)) {
            rubraview_fs_entry_t *ordered = (rubraview_fs_entry_t*)(void*)ordered_res.value.ptr;
            size_t out = 0;
            for (int folder_pass = 1; folder_pass >= 0; --folder_pass) {
                for (size_t i = 0; i < listing.count; ++i) {
                    const rubraview_fs_entry_t *e = &listing.entries[items[i].tag];
                    if ((int)e->is_directory != folder_pass) continue;
                    ordered[out++] = *e;
                }
            }
            listing.entries = ordered;
        }
    }

    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double tile = 160.0 * dpi;

    app->picker_dir = dir;
    app->picker_listing = listing;
    app->picker = rubraview_picker_create(&app->picker_listing, tile,
                                          (double)win_h - (PICKER_CRUMB_HEIGHT + PICKER_ACTION_HEIGHT) * dpi,
                                          PICKER_COLUMNS);
}

static void picker_open(app_state_t *app) {
    u8str_t dir = app->picker_dir;
    if (dir.len == 0) {
        if (app->source_dir.len > 0) {
            dir = app->source_dir;
        } else {
            dir = U8(".");
        }
    }
    picker_navigate(app, dir);
    app->picker_open = app->picker_listing.count > 0;
}

/* Activating a tile enters a folder or opens a file. */
static void picker_activate(app_state_t *app, size_t index) {
    if (index >= app->picker_listing.count) return;
    const rubraview_fs_entry_t *entry = &app->picker_listing.entries[index];

    if (entry->is_directory) {
        picker_navigate(app, entry->path);
        app->picker.focus = 0;
        return;
    }

    app->picker_open = false;
    open_path(app, entry->path);
}

/* The box's grid must match however many tiles the current menu level
   shows, which changes as the reader drills in and back out. */
static void sync_menubox_tiles(app_state_t *app) {
    int32_t count = rubraview_menu_visible_count(&app->menu);
    if (count > MENU_MAX_TILES) count = MENU_MAX_TILES;
    app->menubox.tile_count = count;
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
    } else if (action_is(action, "fit_stretch")) {
        app->fit_mode = RUBRAVIEW_FIT_STRETCH; reset_view(app);
    } else if (action_is(action, "toggle_fit_lock")) {
        app->fit_lock = !app->fit_lock;
    } else if (action_is(action, "skip_forward")) {
        go_to_spread(app, app->spread_index + 10);
    } else if (action_is(action, "skip_backward")) {
        go_to_spread(app, app->spread_index > 10 ? app->spread_index - 10 : 0);
    } else if (action_is(action, "up_to_folder")) {
        /* §3.7.2: ascend to the parent directory, shown in the picker so
           the reader can choose what to open next. */
        u8str_t here = app->picker_dir;
        if (here.len == 0) here = app->source_dir;
        u8str_t parent = rubraview_path_dirname(here);
        if (parent.len > 0) {
            picker_navigate(app, parent);
            app->picker.focus = 0;
            app->picker_open = app->picker_listing.count > 0;
        }
    } else if (action_is(action, "open_folder")) {
        picker_open(app);
    } else if (action_is(action, "toggle_spread_detect")) {
        /* §3.3.4: with detection off, a wide scan pairs like any other
           page instead of standing alone. */
        app->spread_detect = !app->spread_detect;
        app->layout_opts.spread_ar_threshold = app->spread_detect ? 1.15 : 1.0e9;
        app->needs_relayout = true;
    } else if (action_is(action, "interval_up")) {
        rubraview_slideshow_set_interval(&app->slideshow, app->slideshow.interval_seconds + 0.5);
    } else if (action_is(action, "interval_down")) {
        rubraview_slideshow_set_interval(&app->slideshow, app->slideshow.interval_seconds - 0.5);
    } else if (action_is(action, "interval_up_fine")) {
        rubraview_slideshow_set_interval(&app->slideshow, app->slideshow.interval_seconds + 0.1);
    } else if (action_is(action, "interval_down_fine")) {
        rubraview_slideshow_set_interval(&app->slideshow, app->slideshow.interval_seconds - 0.1);
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
        sync_menubox_tiles(app);
    } else if (action_is(action, "layout_single")) {
        app->layout_opts.mode = RUBRAVIEW_PAGE_LAYOUT_SINGLE;
        app->needs_relayout = true;
        reset_view(app);
    } else if (action_is(action, "layout_dual")) {
        app->layout_opts.mode = RUBRAVIEW_PAGE_LAYOUT_DUAL;
        app->needs_relayout = true;
        reset_view(app);
    } else if (action_is(action, "layout_book")) {
        app->layout_opts.mode = RUBRAVIEW_PAGE_LAYOUT_BOOK;
        app->needs_relayout = true;
        reset_view(app);
    } else if (action_is(action, "toggle_toolbox")) {
        rubraview_box_click_anchor(&app->toolbox);
    } else if (action_is(action, "open_picker")) {
        if (app->picker_open) {
            app->picker_open = false;
        } else {
            picker_open(app);
        }
    } else if (action_is(action, "next_archive")) {
        open_sibling_archive(app, true);
    } else if (action_is(action, "prev_archive")) {
        open_sibling_archive(app, false);
    } else if (action_is(action, "resume_accept")) {
        /* §3.17.1: the prompt is answered by opening the remembered page. */
        if (app->resume_offer) {
            app->resume_offer = false;
            size_t target = spread_index_for_page(app, app->resume_page);
            go_to_spread(app, target);
            update_precache(app);
        }
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

/* While the picker is open it owns the keyboard (§3.7.1's modal
   context): letters jump, arrows move, Enter opens, Esc closes. */
static bool picker_handle_key(app_state_t *app, rubraview_key_combo_t combo) {
    if (!app->picker_open) return false;

    if (combo.key_name.len == 1) {
        char c = combo.key_name.ptr[0];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            rubraview_picker_type_ahead(&app->picker, c);
            return true;
        }
    }

    size_t count = app->picker_listing.count;
    if (count == 0) { app->picker_open = false; return true; }

    if (combo.key_name.len == 5 && memcmp(combo.key_name.ptr, "Right", 5) == 0) {
        if (app->picker.focus + 1 < count) app->picker.focus++;
        rubraview_picker_reveal_focus(&app->picker);
        return true;
    }
    if (combo.key_name.len == 4 && memcmp(combo.key_name.ptr, "Left", 4) == 0) {
        if (app->picker.focus > 0) app->picker.focus--;
        rubraview_picker_reveal_focus(&app->picker);
        return true;
    }
    if (combo.key_name.len == 4 && memcmp(combo.key_name.ptr, "Down", 4) == 0) {
        app->picker.focus = (app->picker.focus + PICKER_COLUMNS < count)
            ? app->picker.focus + PICKER_COLUMNS : count - 1;
        rubraview_picker_reveal_focus(&app->picker);
        return true;
    }
    if (combo.key_name.len == 2 && memcmp(combo.key_name.ptr, "Up", 2) == 0) {
        app->picker.focus = (app->picker.focus >= PICKER_COLUMNS) ? app->picker.focus - PICKER_COLUMNS : 0;
        rubraview_picker_reveal_focus(&app->picker);
        return true;
    }
    if (combo.key_name.len == 5 && memcmp(combo.key_name.ptr, "Enter", 5) == 0) {
        picker_activate(app, app->picker.focus);
        return true;
    }
    if (combo.key_name.len == 6 && memcmp(combo.key_name.ptr, "Escape", 6) == 0) {
        app->picker_open = false;
        return true;
    }
    if (combo.key_name.len == 9 && memcmp(combo.key_name.ptr, "Backspace", 9) == 0) {
        u8str_t parent = rubraview_path_dirname(app->picker_dir);
        if (parent.len > 0) {
            picker_navigate(app, parent);
            app->picker.focus = 0;
        }
        return true;
    }
    return false;
}

static void dispatch_key(app_state_t *app, rubraview_key_combo_t combo) {
    if (picker_handle_key(app, combo)) return;
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
        .comic_mode = (app->layout_opts.mode != RUBRAVIEW_PAGE_LAYOUT_SINGLE) || page_count(app) > 1,
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
        if (result == RUBRAVIEW_MENU_ACTIVATED) {
            handle_action(app, action);
        } else if (result == RUBRAVIEW_MENU_DESCENDED || result == RUBRAVIEW_MENU_WENT_BACK) {
            sync_menubox_tiles(app);
        }
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

/* Tile captions for the menu box come from the current menu level, with
   the Back tile at index 0 below the root (§3.6.2). */
static u8str_t menu_tile_caption(const rubraview_menu_state_t *menu, int32_t tile, char *scratch, size_t scratch_size) {
    (void)scratch; (void)scratch_size;
    if (rubraview_menu_has_back_tile(menu) && tile == 0) return U8("< Back");
    const rubraview_menu_item_t *item = rubraview_menu_item_at(menu, tile);
    return item ? item->label : (u8str_t){ .ptr = "", .len = 0 };
}

static void draw_box(app_state_t *app, const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics,
                     const char *const *captions, int32_t caption_count,
                     const rubraview_menu_state_t *menu) {
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
        u8str_t caption = { .ptr = "", .len = 0 };
        if (menu) {
            char scratch[64];
            caption = menu_tile_caption(menu, i, scratch, sizeof(scratch));
        } else if (captions && i < caption_count) {
            caption = cstr(captions[i]);
        }
        if (caption.len > 0) {
            rubraview_pal_render_draw_text(app->renderer, caption, tile,
                                           metrics->tile_size * 0.22, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
    }
}

/* §3.15.2's three tiers: breadcrumb header, virtualised tile grid,
   action bar. Tiles are large enough to hit with a thumb over Remote
   Desktop, which is the whole reason this picker exists. */
static void draw_picker(app_state_t *app, double win_w, double win_h) {
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double crumb_h = PICKER_CRUMB_HEIGHT * dpi;
    double action_h = PICKER_ACTION_HEIGHT * dpi;

    rubraview_pal_rect_t backdrop = { 0.0, 0.0, win_w, win_h };
    rubraview_pal_render_fill_rect(app->renderer, backdrop, 0xF0101010u, 0.0);

    /* Breadcrumb: every segment is its own tappable tile. */
    rubraview_breadcrumbs_t crumbs = rubraview_picker_breadcrumbs(app->picker_dir);
    double crumb_x = 8.0 * dpi;
    for (size_t i = 0; i < crumbs.count; ++i) {
        double w = (double)(crumbs.items[i].label.len + 3) * 9.0 * dpi;
        rubraview_pal_rect_t chip = { crumb_x, 6.0 * dpi, w, crumb_h - 12.0 * dpi };
        rubraview_pal_render_fill_rect(app->renderer, chip, COLOR_TILE_FILL, 2.0);
        rubraview_pal_render_stroke_rect(app->renderer, chip, COLOR_BOX_BORDER, 1.0, 2.0);
        rubraview_pal_render_draw_text(app->renderer, crumbs.items[i].label, chip,
                                       crumb_h * 0.34, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        crumb_x += w + 6.0 * dpi;
    }

    /* Virtualised grid: only the rows on screen are drawn. */
    rubraview_virtual_range_t visible = rubraview_picker_visible(&app->picker);
    double cell = app->picker.tile_extent;
    double cell_w = win_w / (double)PICKER_COLUMNS;

    for (size_t i = 0; i < visible.count; ++i) {
        size_t index = visible.first + i;
        if (index >= app->picker_listing.count) break;
        const rubraview_fs_entry_t *entry = &app->picker_listing.entries[index];

        size_t row = index / PICKER_COLUMNS;
        size_t column = index % PICKER_COLUMNS;
        double x = (double)column * cell_w;
        double y = crumb_h + (double)row * cell - app->picker.scroll_offset;
        if (y + cell < crumb_h || y > win_h - action_h) continue;

        rubraview_pal_rect_t tile = { x + 6.0 * dpi, y + 6.0 * dpi, cell_w - 12.0 * dpi, cell - 12.0 * dpi };
        rubraview_pal_render_fill_rect(app->renderer, tile, COLOR_TILE_FILL, 0.0);
        rubraview_pal_render_stroke_rect(app->renderer, tile,
                                         index == app->picker.focus ? COLOR_TEXT : COLOR_BOX_BORDER,
                                         index == app->picker.focus ? 2.0 : 1.0, 0.0);

        rubraview_pal_rect_t caption = { tile.x, tile.y + tile.height * 0.62, tile.width, tile.height * 0.38 };
        rubraview_pal_render_draw_text(app->renderer, entry->name, caption,
                                       cell * 0.11, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);

        rubraview_pal_rect_t kind = { tile.x, tile.y + tile.height * 0.2, tile.width, tile.height * 0.3 };
        rubraview_pal_render_draw_text(app->renderer,
                                       entry->is_directory ? U8("[ folder ]") : U8("[ file ]"),
                                       kind, cell * 0.10, COLOR_BOX_BORDER, RUBRAVIEW_TEXT_CENTER);
    }

    /* Action bar with the selection metrics (§3.15.2 tier three). */
    rubraview_pal_rect_t bar = { 0.0, win_h - action_h, win_w, action_h };
    rubraview_pal_render_fill_rect(app->renderer, bar, COLOR_BAR_FILL, 0.0);

    char status[160];
    size_t selected = 0;
    uint64_t bytes = 0;
    rubraview_picker_selection_metrics(&app->picker, &selected, &bytes);
    int written = snprintf(status, sizeof(status),
                           "%zu items   |   selected %zu (%llu bytes)   |   Enter opens, Esc closes",
                           app->picker_listing.count, selected, (unsigned long long)bytes);
    if (written > 0) {
        rubraview_pal_render_draw_text(app->renderer,
                                       (u8str_t){ .ptr = status, .len = (size_t)written },
                                       bar, action_h * 0.34, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
}

static void draw_chrome(app_state_t *app, double win_w, double win_h) {
    rubraview_tile_metrics_t metrics = rubraview_tile_metrics_default(rubraview_pal_window_dpi_scale(app->window));

    /* Filmstrip (§3.1): tiles come from pages already decoded; dedicated
       low-resolution thumbnail decoding arrives with the asynchronous
       pre-cache worker in M4 (RV-044), where async decode belongs. */
    if (app->filmstrip.visible && page_count(app) > 0) {
        double strip_h = FILMSTRIP_THUMB * rubraview_pal_window_dpi_scale(app->window);
        rubraview_pal_rect_t strip = { 0.0, win_h - strip_h, win_w, strip_h };
        rubraview_pal_render_fill_rect(app->renderer, strip, COLOR_BAR_FILL, 0.0);

        rubraview_virtual_range_t visible = rubraview_filmstrip_visible(&app->filmstrip);
        for (size_t i = 0; i < visible.count; ++i) {
            size_t index = visible.first + i;
            double x = (double)index * app->filmstrip.thumb_extent - app->filmstrip.scroll_offset;
            rubraview_pal_rect_t cell = { x, strip.y + 4.0, app->filmstrip.thumb_extent - 8.0, strip_h - 8.0 };
            rubraview_pal_render_stroke_rect(app->renderer, cell, COLOR_BOX_BORDER, 1.0, 0.0);

            if (index < page_count(app) && app->pages[index].loaded) {
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
    draw_box(app, &app->toolbox, &metrics, TOOLBOX_CAPTIONS, TOOLBOX_TILES, NULL);
    draw_box(app, &app->menubox, &metrics, NULL, 0, &app->menu);

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
        if (page >= 0 && (size_t)page < page_count(app)) {
            u8str_t name = page_display_name(app, (size_t)page);
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

    /* §3.17.1: an unobtrusive prompt offering the remembered page. */
    if (app->resume_offer) {
        double bar_h = 40.0 * rubraview_pal_window_dpi_scale(app->window);
        rubraview_pal_rect_t prompt = { win_w * 0.25, win_h * 0.5 - bar_h * 0.5, win_w * 0.5, bar_h };
        rubraview_pal_render_fill_rect(app->renderer, prompt, COLOR_BOX_FILL, 3.0);
        rubraview_pal_render_stroke_rect(app->renderer, prompt, COLOR_BOX_BORDER, 1.0, 3.0);

        char line[128];
        int written = snprintf(line, sizeof(line), "Resume page %d / %zu  (Enter)",
                               app->resume_page + 1, page_count(app));
        if (written > 0) {
            rubraview_pal_render_draw_text(app->renderer,
                                           (u8str_t){ .ptr = line, .len = (size_t)written },
                                           prompt, bar_h * 0.4, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
    }

    /* OSD (§3.1), skipped once it has faded out entirely. */
    if (rubraview_osd_opacity(&app->osd) > 0.01) {
        int32_t page = current_page_index(app);
        if (page >= 0 && (size_t)page < page_count(app) && app->pages[page].loaded) {
            char line[192];
            u8str_t name = page_display_name(app, (size_t)page);
            u8str_t text = rubraview_osd_format(line, sizeof(line), name,
                                                app->pages[page].width, app->pages[page].height,
                                                app->zoom * 100.0, (size_t)page, page_count(app));

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
            if (cmd->page_index < 0 || (size_t)cmd->page_index >= page_count(app)) continue;
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

    if (app->picker_open) {
        draw_picker(app, (double)win_w, (double)win_h);
    } else {
        draw_chrome(app, (double)win_w, (double)win_h);
    }

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
    if (text.len == 0) text = cstr(rubraview_default_keymap());
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

/* Decoding for the pre-cache worker: the ring calls this off the main
   thread for pages it wants ready before the reader reaches them. */
static void precache_decode(void *ctx, size_t page_index) {
    app_state_t *app = (app_state_t*)ctx;
    ensure_page_loaded(app, (int32_t)page_index);
}

static void release_evicted(app_state_t *app, const uint64_t *evicted, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        size_t index = (size_t)evicted[i];
        if (index >= page_count(app)) continue;
        if (!app->pages[index].texture) continue;
        /* §7.4: the budget decided this page has to go. */
        rubraview_pal_texture_destroy(app->pages[index].texture);
        app->pages[index] = (app_page_t){0};
    }
}

/* Keeps the ring in step with the page on screen, and lets the budget
   reclaim whatever fell out of it. */
static void update_precache(app_state_t *app) {
    if (page_count(app) == 0) return;

    int32_t current = current_page_index(app);
    if (current < 0) return;

    rubraview_precache_set_interval(&app->precache,
                                    app->slideshow_running ? app->slideshow.interval_seconds : 0.0,
                                    0.5);

    uint64_t evicted[32];
    size_t evicted_count = 0;
    rubraview_precache_update(&app->precache, (size_t)current, precache_decode, app,
                              evicted, sizeof(evicted) / sizeof(evicted[0]), &evicted_count);
    /* §7.4: give back the textures the budget decided it could not keep. */
    release_evicted(app, evicted, evicted_count);
}

/* ---- reading history (§3.17) ---- */

static void history_load(app_state_t *app) {
    /* §3.17.2: a settings.ini beside the executable means portable mode,
       and then nothing at all is written to the host machine. */
    bool portable = rubraview_pal_fs_exists(U8("settings.ini"));
    app->config_mode = rubraview_config_mode_for(portable);

    u8str_t appdata = U8(".");
#ifdef _WIN32
    char appdata_utf8[1024];
    DWORD written = GetEnvironmentVariableA("APPDATA", appdata_utf8, (DWORD)sizeof(appdata_utf8));
    if (written > 0 && written < sizeof(appdata_utf8)) {
        appdata = (u8str_t){ .ptr = appdata_utf8, .len = written };
    }
#endif

    app->history_path = rubraview_config_path(app->arena, app->config_mode,
                                              U8("."), appdata, U8("history.ini"));
    u8str_t text = rubraview_pal_fs_read_file(app->arena, app->history_path, 1024u * 1024u);
    app->history = rubraview_history_parse(app->arena, text);
}

static void history_remember(app_state_t *app) {
    if (app->source_dir.len == 0 || page_count(app) == 0) return;

    int32_t current = current_page_index(app);
    if (current < 0) return;

    /* An archive is remembered by its own path; a folder by the folder. */
    u8str_t key = app->source.archive_path.len > 0 ? app->source.archive_path : app->source_dir;
    rubraview_history_record(app->arena, &app->history, key, current, (int32_t)page_count(app),
                             (int64_t)rubraview_pal_time_now_seconds());
    rubraview_history_prune(&app->history, HISTORY_MAX_ENTRIES);
}

/* ---- opening ---- */

static bool open_archive(app_state_t *app, u8str_t archive_path) {
    u8str_t bytes = rubraview_pal_fs_read_file(app->arena, archive_path, MAX_ARCHIVE_BYTES);
    if (bytes.len == 0) return false;

    app->archive_bytes = bytes;
    app->source = rubraview_page_source_from_archive(app->arena,
                                                     (const uint8_t*)bytes.ptr, bytes.len,
                                                     archive_path, U8(IMAGE_FILTER),
                                                     RUBRAVIEW_CODEPAGE_AUTO, MAX_PAGE_BYTES);
    app->source_dir = rubraview_path_dirname(archive_path);
    return app->source.page_count > 0;
}

static bool open_folder(app_state_t *app, u8str_t dir) {
    rubraview_fs_listing_t listing = rubraview_pal_fs_list_dir(app->arena, dir);
    if (listing.count == 0) return false;

    app->archive_bytes = (u8str_t){ .ptr = "", .len = 0 };
    app->source = rubraview_page_source_from_listing(app->arena, &listing, U8(IMAGE_FILTER),
                                                     RUBRAVIEW_SORT_NAME_NATURAL, true);
    app->source_dir = dir;
    return app->source.page_count > 0;
}

/* Finishes opening whichever source was just built: allocate the page
   table, apply any ComicInfo, lay out, and start the ring. */
static void finish_open(app_state_t *app, size_t start_page) {
    proven_result_mem_mut_t res = proven_arena_alloc(app->arena, page_count(app) * sizeof(app_page_t));
    if (!proven_is_ok(res.err)) {
        app->source.page_count = 0;
        return;
    }
    app->pages = (app_page_t*)(void*)res.value.ptr;
    memset(app->pages, 0, page_count(app) * sizeof(app_page_t));

    app->page_cache = rubraview_lru_create(app->arena, page_count(app) + 8, PAGE_CACHE_BUDGET);
    app->precache = rubraview_precache_create(app->jobs, &app->page_cache,
                                              page_count(app), ESTIMATED_PAGE_BYTES);

    if (start_page >= page_count(app)) start_page = 0;
    ensure_page_loaded(app, (int32_t)start_page);
    rebuild_layout(app);

    /* §3.8.5: let the archive's own manifest set the reading direction
       and mark its covers before the first spread is chosen. */
    if (app->source.has_comicinfo) {
        rubraview_comicinfo_t info = rubraview_comicinfo_parse(app->arena, app->source.comicinfo_xml);
        proven_result_mem_mut_t infos_res =
            proven_arena_alloc(app->arena, page_count(app) * sizeof(rubraview_page_info_t));
        if (proven_is_ok(infos_res.err)) {
            rubraview_page_info_t *infos = (rubraview_page_info_t*)(void*)infos_res.value.ptr;
            memset(infos, 0, page_count(app) * sizeof(rubraview_page_info_t));
            rubraview_comicinfo_apply(&info, &app->layout_opts, infos, page_count(app));
            app->comic_page_flags = infos;
            rebuild_layout(app);
        }
    }

    app->spread_index = spread_index_for_page(app, (int32_t)start_page);

    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    double scale = rubraview_pal_window_dpi_scale(app->window);
    app->filmstrip = rubraview_filmstrip_create(page_count(app), FILMSTRIP_THUMB * scale, (double)win_w);
    rubraview_filmstrip_reveal(&app->filmstrip, start_page);
    build_slides(app);
    update_precache(app);
}

static void open_path(app_state_t *app, u8str_t path) {
    rubraview_fs_entry_t entry;
    if (!rubraview_pal_fs_stat(app->arena, path, &entry)) return;

    /* Remember where the reader was in whatever was open before. */
    history_remember(app);

    bool opened = false;
    u8str_t key = entry.path;

    if (entry.is_directory) {
        opened = open_folder(app, entry.path);
        key = entry.path;
    } else if (rubraview_glob_match_list(rubraview_path_basename(entry.path), U8(ARCHIVE_FILTER))) {
        opened = open_archive(app, entry.path);
    } else {
        opened = open_folder(app, rubraview_path_dirname(entry.path));
        key = rubraview_path_dirname(entry.path);
    }
    if (!opened) return;

    /* §3.17.1: reopening something read before starts where it stopped,
       rather than dropping the reader back on page one. */
    size_t start_page = 0;
    const rubraview_history_entry_t *seen = rubraview_history_find(&app->history, key);
    if (seen && rubraview_history_should_offer_resume(seen)) {
        app->resume_offer = true;
        app->resume_page = seen->page;
        if ((size_t)seen->page < app->source.page_count) start_page = (size_t)seen->page;
    } else {
        app->resume_offer = false;
    }

    /* A file that was opened directly wins over the remembered spot. */
    if (!entry.is_directory && app->source.kind == RUBRAVIEW_PAGE_SOURCE_FOLDER) {
        for (size_t i = 0; i < app->source.page_count; ++i) {
            if (app->source.pages[i].path.len == entry.path.len &&
                memcmp(app->source.pages[i].path.ptr, entry.path.ptr, entry.path.len) == 0) {
                start_page = i;
                app->resume_offer = false;
                break;
            }
        }
    }

    finish_open(app, start_page);
}

/* §3.8.1 point 4: reaching the end of a volume continues into the next
   archive in the same directory. */
static void open_sibling_archive(app_state_t *app, bool forward) {
    if (app->source.archive_path.len == 0 || app->source_dir.len == 0) return;

    rubraview_fs_listing_t listing = rubraview_pal_fs_list_dir(app->arena, app->source_dir);
    u8str_t next = rubraview_page_source_sibling_archive(app->arena, &listing,
                                                          app->source.archive_path, forward);
    if (next.len == 0) return;
    open_path(app, next);
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
    app.spread_detect = true; /* §3.3.4 default */
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

    /* §3.1: pages decode on worker threads so a flip does not wait for
       the disk. A pool that fails to start is not fatal — the ring then
       decodes inline, which is slower but correct. */
    proven_allocator_t job_alloc = proven_arena_as_allocator(&arena);
    if (proven_job_system_init(job_alloc, PRECACHE_WORKERS, 64, &app.jobs) != PROVEN_OK) {
        app.jobs = NULL;
    }
    history_load(&app);
    app.osd = rubraview_osd_create(2.0, 0.5);            /* §3.1 */
    app.titlebar = rubraview_titlebar_create(dpi);       /* §3.21.2 */
    app.toolbox = rubraview_box_create(RUBRAVIEW_BOX_TOOLBOX, (double)win_w - 220.0 * dpi, (double)win_h - 160.0 * dpi, TOOLBOX_TILES);
    app.menubox = rubraview_box_create(RUBRAVIEW_BOX_MENU, 24.0 * dpi, 24.0 * dpi, MENU_ROOT_COUNT);
    app.menu = rubraview_menu_create(&MENU_TREE); /* §3.6.2 category tree */
    app.transition = rubraview_transition_create(RUBRAVIEW_TRANSITION_CROSSFADE, 0.25);
    app.cursor = rubraview_cursor_hide_create(1.5);      /* §3.2.5 */
    app.filmstrip = rubraview_filmstrip_create(0, FILMSTRIP_THUMB * dpi, (double)win_w);
    sync_menubox_tiles(&app);

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
                    if (app.picker_open) {
                        double dpi = rubraview_pal_window_dpi_scale(app.window);
                        double crumb_h = PICKER_CRUMB_HEIGHT * dpi;

                        if (event.mouse.y < crumb_h) {
                            /* A breadcrumb chip navigates to its prefix. */
                            rubraview_breadcrumbs_t crumbs = rubraview_picker_breadcrumbs(app.picker_dir);
                            double x = 8.0 * dpi;
                            for (size_t i = 0; i < crumbs.count; ++i) {
                                double w = (double)(crumbs.items[i].label.len + 3) * 9.0 * dpi;
                                if (event.mouse.x >= x && event.mouse.x < x + w) {
                                    picker_navigate(&app, crumbs.items[i].prefix);
                                    app.picker.focus = 0;
                                    break;
                                }
                                x += w + 6.0 * dpi;
                            }
                            break;
                        }

                        int32_t pw = 0, ph = 0;
                        rubraview_pal_window_get_size(app.window, &pw, &ph);
                        double cell_w = (double)pw / (double)PICKER_COLUMNS;
                        size_t column = (size_t)(event.mouse.x / cell_w);
                        size_t row = (size_t)((event.mouse.y - crumb_h + app.picker.scroll_offset) / app.picker.tile_extent);
                        size_t index = row * PICKER_COLUMNS + column;
                        if (column < PICKER_COLUMNS && index < app.picker_listing.count) {
                            app.picker.focus = index;
                            picker_activate(&app, index);
                        }
                        break;
                    }

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
                    if (app.picker_open) {
                        rubraview_picker_scroll_by(&app.picker, -event.mouse.wheel_delta * app.picker.tile_extent * 0.5);
                        break;
                    }
                    rubraview_pointer_context_t ctx = pointer_context(&app);
                    apply_intent(&app, rubraview_pointer_wheel(&ctx, event.mouse.wheel_delta, event.mouse.modifiers));
                    break;
                }

                case RUBRAVIEW_WINDOW_EVENT_GESTURE_ZOOM:
                    /* §3.6.5: pinch-to-zoom about the gesture centroid. */
                    app.zoom *= event.gesture.scale_ratio;
                    if (app.zoom < 0.01) app.zoom = 0.01;
                    note_activity(&app);
                    break;

                case RUBRAVIEW_WINDOW_EVENT_GESTURE_PAN:
                    if (app.picker_open) {
                        rubraview_picker_scroll_by(&app.picker, -event.gesture.dy);
                    } else {
                        app.pan_x += event.gesture.dx;
                        app.pan_y += event.gesture.dy;
                    }
                    note_activity(&app);
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

        double now = rubraview_pal_time_now_seconds();
        double dt = now - app.last_frame_seconds;
        if (dt < 0.0) dt = 0.0;
        app.last_frame_seconds = now;
        tick_timers(&app, dt);

        render_frame(&app);
        rubraview_pal_time_sleep_ms(4);
    }

    /* §3.17.1: remember where the reader stopped before shutting down. */
    history_remember(&app);
    if (app.history_path.len > 0 && app.history.count > 0) {
        u8str_t text = rubraview_history_serialize(&arena, &app.history);
        rubraview_pal_fs_write_file(app.history_path, text);
    }

    if (app.jobs) {
        proven_job_system_close(app.jobs);
        proven_job_system_destroy(app.jobs);
        app.jobs = NULL;
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
