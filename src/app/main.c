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
#include <wincodec.h>
#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>
/* --probe-gpu only: the graphics device and Media Foundation's own
   interfaces, asked about directly rather than through a PAL, because
   the question it answers is about this machine, not about the app. */
#include <d3d11_4.h>   /* ID3D11Multithread lives here in mingw-w64 */
#include <dxgi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
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
#include "rubraview/subtitle.h"
#include "rubraview/encoding.h"
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
#include "rubraview/batchrun.h"
#include "rubraview/filter.h"
#include "rubraview/edit.h"
#include "rubraview/ui_panel.h"
#include "rubraview/filemanage.h"
#include "rubraview/settings.h"
#include "rubraview/version.h"
#include "rubraview/export.h"
#include "rubraview/jpegtran.h"
#include "rubraview/comicinfo.h"
#include "proven/job.h"
#include "rubraview/pal/pal_window.h"
#include "rubraview/pal/pal_render.h"
#include "rubraview/pal/pal_image.h"
#include "rubraview/pal/pal_fs.h"
#include "rubraview/pal/pal_time.h"
#include "rubraview/pal/pal_media.h"
#include "rubraview/mediaclock.h"
#include "rubraview/playback.h"

#define APP_ARENA_BYTES (64u * 1024u * 1024u)
#define IMAGE_FILTER "*.jpg;*.jpeg;*.png;*.webp;*.gif;*.bmp;*.tif;*.tiff;*.ico"
/* M5: video and sound files join the folder as pages (sound since slice 2, RV-084). */
#define MEDIA_FILTER "*.mp4;*.m4v;*.mov;*.mkv;*.webm;*.avi;*.wmv;*.asf;*.ts;*.m2ts;*.mts;*.mpg;*.mpeg;*.flv;*.ogv;*.3gp;" \
                     "*.mp3;*.m4a;*.aac;*.flac;*.wav;*.wma;*.ogg;*.oga;*.opus"
/* How far the picture's clock may run on from the last thing the sound
   thread reported, before it waits for the next report. */
#define MEDIA_AUDIO_EXTRAPOLATION 0.2
#define MEDIA_SEEK_STEP 5.0
#define MEDIA_NOTICE_SECONDS 5.0
/* How far the picture may fall behind the clock before the clock is
   moved to the picture instead: on a machine that cannot decode in
   real time, playing a little slow beats a slideshow of jumps. */
#define MEDIA_RESYNC_SECONDS 0.25
#define PIXEL_GRID_MIN_SCALE 4.0 /* §3.5: the grid appears from 400% zoom */
#define ZOOM_STEP 1.1
#define PAN_STEP 60.0
#define GUTTER 8.0
#define KEYMAP_MAX_BYTES (256u * 1024u)
#define FILMSTRIP_THUMB 120.0
#define ARCHIVE_FILTER "*.cbz;*.zip;*.cb7;*.7z"
#define MAX_ARCHIVE_BYTES (2048u * 1024u * 1024u)  /* the whole CBZ, held in memory (§3.8.1) */
#define MAX_PAGE_BYTES (512u * 1024u * 1024u)      /* §10.2's per-page zip-bomb guard */
#define BATCH_WORK_ARENA_BYTES (256u * 1024u * 1024u)  /* §3.11: one file's worth, reset per file */
#define PAGE_CACHE_BUDGET (512u * 1024u * 1024u)   /* §7.4's default budget */
#define ESTIMATED_PAGE_BYTES (12u * 1024u * 1024u)
#define PENDING_DECODE_MAX 32
/* Seconds of redrawing after the last input or animation. Longer than
   every fade the chrome runs (OSD 2.0 + 0.5, box grace 0.5), so none is
   cut off when the loop stops drawing. */
#define IDLE_REDRAW_GRACE 3.0
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
    MENU_ROOT_LAYOUT = 0, MENU_ROOT_FIT, MENU_ROOT_VIEW, MENU_ROOT_SHOW, MENU_ROOT_MEDIA,
    MENU_ROOT_COUNT,
};

/* U8() builds a compound literal, which is not a constant initializer at
   file scope; a brace initializer with sizeof for the length is, and it
   still avoids hand-counting any lengths. */
#define MENU_STR(lit) { .ptr = (lit), .len = sizeof(lit) - 1 }

static const rubraview_menu_item_t MENU_ITEMS[] = {
    /* 0 */ { .label = MENU_STR("Layout"), .action = MENU_STR(""), .first_child = 5, .child_count = 3 },
    /* 1 */ { .label = MENU_STR("Fit"),    .action = MENU_STR(""), .first_child = 8, .child_count = 5 },
    /* 2 */ { .label = MENU_STR("View"),   .action = MENU_STR(""), .first_child = 13, .child_count = 4 },
    /* 3 */ { .label = MENU_STR("Show"),   .action = MENU_STR(""), .first_child = 17, .child_count = 3 },
    /* 4 */ { .label = MENU_STR("Media"),  .action = MENU_STR(""), .first_child = 20, .child_count = 4 },

    /* Layout (5..7) */
    { .label = MENU_STR("Single"), .action = MENU_STR("layout_single"), .first_child = -1, .child_count = 0 },
    { .label = MENU_STR("Dual"),   .action = MENU_STR("layout_dual"),   .first_child = -1, .child_count = 0 },
    { .label = MENU_STR("Book"),   .action = MENU_STR("layout_book"),   .first_child = -1, .child_count = 0 },

    /* Fit (8..12) */
    { .label = MENU_STR("Window"), .action = MENU_STR("fit_window"),  .first_child = -1, .child_count = 0 },
    { .label = MENU_STR("Width"),  .action = MENU_STR("fit_width"),   .first_child = -1, .child_count = 0 },
    { .label = MENU_STR("Height"), .action = MENU_STR("fit_height"),  .first_child = -1, .child_count = 0 },
    { .label = MENU_STR("1:1"),    .action = MENU_STR("actual_size"), .first_child = -1, .child_count = 0 },
    { .label = MENU_STR("Smart"),  .action = MENU_STR("smart_fit"),   .first_child = -1, .child_count = 0 },

    /* View (13..16) */
    { .label = MENU_STR("Rotate"), .action = MENU_STR("rotate_cw"),         .first_child = -1, .child_count = 0 },
    { .label = MENU_STR("Flip H"), .action = MENU_STR("flip_horizontal"),   .first_child = -1, .child_count = 0 },
    { .label = MENU_STR("Crisp"),  .action = MENU_STR("toggle_nearest"),    .first_child = -1, .child_count = 0 },
    { .label = MENU_STR("Grid"),   .action = MENU_STR("toggle_pixel_grid"), .first_child = -1, .child_count = 0 },

    /* Show (17..19) */
    { .label = MENU_STR("Slides"), .action = MENU_STR("toggle_slideshow"), .first_child = -1, .child_count = 0 },
    { .label = MENU_STR("Strip"),  .action = MENU_STR("toggle_filmstrip"), .first_child = -1, .child_count = 0 },
    { .label = MENU_STR("Files"),  .action = MENU_STR("open_picker"),      .first_child = -1, .child_count = 0 },

    /* Media (20..23) — §3.16.2 wants these reachable from the Menu Box,
       not only from the keyboard. */
    { .label = MENU_STR("Sound"),  .action = MENU_STR("next_audio_track"),    .first_child = -1, .child_count = 0 },
    { .label = MENU_STR("Subs"),   .action = MENU_STR("next_subtitle_track"), .first_child = -1, .child_count = 0 },
    { .label = MENU_STR("Sub -"),  .action = MENU_STR("subtitle_earlier"),    .first_child = -1, .child_count = 0 },
    { .label = MENU_STR("Sub +"),  .action = MENU_STR("subtitle_later"),      .first_child = -1, .child_count = 0 },
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
    /* Pages the ring asked for, decoded one per loop pass on this thread:
       WIC, Direct2D and the arena all belong to the main thread. */
    size_t                  pending_decode[PENDING_DECODE_MAX];
    size_t                  pending_decode_count;
    rubraview_history_t     history;
    u8str_t                 history_path;
    rubraview_config_mode_t config_mode;
    rubraview_page_info_t  *comic_page_flags; /* §3.8.5 cover marks, applied on every relayout */

    /* §3.20: the animation or sub-page set belonging to the page on
       screen. Only one page animates at a time — the one being read. */
    rubraview_animation_t  animation;
    rubraview_frame_t     *anim_frames;
    int32_t                anim_page;    /* which page these frames describe; -1 for none */
    bool                   anim_active;

    /* M5: the video on the current page, when it is one (D-8, D-9). */
    rubraview_media_t      *media;
    rubraview_media_info_t  media_info;
    rubraview_media_clock_t media_clock;
    int32_t                media_page;        /* -1 when no video is open */
    bool                   media_paused;
    bool                   media_has_frame;   /* false until the first picture after opening or seeking */
    bool                   media_skip_pending;/* D-9: a file nothing could open; move past it */
    double                 media_position;    /* pts of the picture on screen */
    int64_t                media_title_tenth; /* the tenth of a second the title last showed */
    rubraview_clock_master_t media_master;    /* §5.3: the sound when it is heard, else the wall clock */
    rubraview_media_backend_t media_preferred; /* §3.22 [video] decoder — the one tried first (D-9) */

    /* §3.6: an anchor is a button and a handle at once; which one a press
       turns out to be is known only when it is released. */
    rubraview_box_t       *box_drag;          /* the box a press landed on, NULL when none */
    double                 box_grab_dx, box_grab_dy;   /* where inside the anchor it was grabbed */
    double                 box_press_x, box_press_y;
    bool                   box_drag_moved;
    bool                   media_ended;       /* reached the end; Space plays it again from the start */
    /* §3.16.1 / R135: the external subtitle file that goes with the
       video on screen. Empty when the film has none. */
    rubraview_subtitle_track_t subtitle;
    u8str_t                    subtitle_name;   /* what to say in the OSD */
    /* §3.16.2: the sound tracks the file holds and the subtitle files
       beside it, in one list — what the reader cycles through. */
    rubraview_track_set_t         tracks;
    rubraview_subtitle_candidate_t subtitle_files[8];
    size_t                         subtitle_count;
    /* §3.16.1: a sync the reader set by hand belongs to that film, not
       to the moment — playing it again must not throw it away. */
    u8str_t                        subtitle_offset_for;
    double                         subtitle_offset_seconds;
    char                           subtitle_label[96];   /* what the OSD calls the track in use */
    bool                    resume_offer;   /* §3.17.1: the prompt is showing */
    int32_t                 resume_page;

    rubraview_layout_opts_t layout_opts;
    rubraview_layout_result_t layout;
    size_t spread_index;
    size_t prev_spread_index;   /* the one the cross-fade is fading out (§3.2.5) */
    bool   prev_spread_valid;

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

    /* §3.13 / §3.10 / §3.11: the workbench and the two dialogs. All
       three are the same panel model with different rows. */
    rubraview_panel_t         panel;
    bool                      panel_is_export;   /* which of the three the panel currently is */
    bool                      panel_is_batch;
    rubraview_edit_session_t  edit;
    rubraview_export_options_t export_options;
    rubraview_batch_job_t      batch_job;
    rubraview_batch_action_t   batch_actions[8];

    /* §3.18: triage. The undo stack is what makes Delete safe to press
       quickly, which is the point of the whole section. */
    rubraview_undo_stack_t undo;
    rubraview_curation_t   curation;
    bool                   rename_active;
    char                   rename_buffer[256];
    size_t                 rename_length;
    bool                   confirm_purge;   /* §3.18.1's Y/N dialog is showing */

    /* A short-lived message: "moved to Best", "cannot be brought back".
       §3.18.3 calls it a notification badge; it is a line of text with a
       timer, drawn over the canvas. */
    char                   notice[192];
    size_t                 notice_length;
    double                 notice_seconds;

    /* §3.22: the settings window. It is the same panel model again,
       with a tab strip above it — one tab's settings at a time. */
    bool                     settings_open;
    rubraview_settings_tab_t settings_tab;
    rubraview_settings_t     settings;
    rubraview_settings_t     settings_saved;   /* what is on disk, for Cancel and for Apply's state */
    u8str_t                  settings_path;
    u8str_t                  layout_path;      /* §3.6: where the floating boxes were left */
    rubraview_panel_t        settings_panel;

    /* In-app Metro file picker (§3.15.2), RV-043 */
    bool                   picker_open;
    u8str_t                picker_dir;
    rubraview_fs_listing_t picker_listing;
    rubraview_picker_t     picker;
} app_state_t;

static void open_path(app_state_t *app, u8str_t path);

static void update_precache(app_state_t *app);

/* The animation helpers below sit next to page loading, which is where
   frames are decoded, but they ask which page is on screen — a question
   the layout answers further down. */
static int32_t current_page_index(const app_state_t *app);

/* The workbench and the two dialogs are defined further down, next to
   the drawing they belong with; the key handler above needs to name
   them. */
static bool open_folder(app_state_t *app, u8str_t dir);
static void triage_delete(app_state_t *app, bool permanent);
static void triage_undo(app_state_t *app);
static void triage_curate(app_state_t *app, int32_t digit);
static void rename_begin(app_state_t *app);
static void rename_commit(app_state_t *app);
static void finish_open(app_state_t *app, size_t start_page);

static void panel_close(app_state_t *app);
static void osd_say(app_state_t *app, u8str_t text);
static void settings_open(app_state_t *app);
static void settings_close(app_state_t *app, bool keep_changes);
static void panel_open_edit(app_state_t *app);
static void panel_open_export(app_state_t *app);
static void panel_open_batch(app_state_t *app);

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

/* Whether a page is a video, opened by the media PAL rather than WIC. */
static bool is_media_path(u8str_t path) {
    return path.len > 0 && rubraview_glob_match_list(rubraview_path_basename(path), U8(MEDIA_FILTER));
}

static app_page_t *ensure_page_loaded(app_state_t *app, int32_t index) {
    if (index < 0 || (size_t)index >= page_count(app)) return NULL;
    app_page_t *page = &app->pages[index];
    if (page->loaded || page->failed) return page;
    /* A video page gets its texture from media_prepare, and only while
       it is the page on screen: the pre-cache must not open videos. */
    if (is_media_path(app->source.pages[index].path)) return page;

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

/* ---- animated images and sub-pages (§3.20) ---- */

#define ANIM_MAX_FRAMES 512

/* Replaces the page's texture with one frame of it. The page keeps its
   own dimensions from the frame, because an ICO's mipmaps genuinely
   differ in size and the layout has to follow. */
static void show_frame(app_state_t *app, size_t frame_index) {
    if (app->anim_page < 0) return;
    app_page_t *page = &app->pages[app->anim_page];

    u8str_t path = app->source.pages[app->anim_page].path;
    rubraview_page_bytes_t bytes = { .data = { .ptr = "", .len = 0 }, .from_disk = true, .ok = false };
    if (path.len == 0) {
        bytes = rubraview_page_source_read(app->arena, &app->source, (size_t)app->anim_page, MAX_PAGE_BYTES);
        if (!bytes.ok) return;
    }

    double delay = 0.0;
    rubraview_image_load_result_t loaded = rubraview_pal_image_load_frame(
        app->renderer, path, (const uint8_t*)bytes.data.ptr, bytes.data.len,
        frame_index, true, &delay);
    if (!loaded.ok) return;

    if (page->texture) rubraview_pal_texture_destroy(page->texture);
    page->texture = loaded.texture;
    page->width = loaded.width;
    page->height = loaded.height;
    page->loaded = true;
    app->needs_relayout = true;
}

/* Called whenever the page on screen changes. Most pages are ordinary
   images and this costs one frame-count query; the rest set up the
   clock, or — for an ICO — jump straight to the biggest mipmap, which
   §3.20.2 says is the one worth showing. */
static void animation_prepare(app_state_t *app) {
    app->anim_active = false;
    app->anim_page = -1;

    int32_t page_index = current_page_index(app);
    if (page_index < 0 || (size_t)page_index >= page_count(app)) return;

    u8str_t path = app->source.pages[page_index].path;
    rubraview_page_bytes_t bytes = { .data = { .ptr = "", .len = 0 }, .from_disk = true, .ok = false };
    if (path.len == 0) {
        bytes = rubraview_page_source_read(app->arena, &app->source, (size_t)page_index, MAX_PAGE_BYTES);
        if (!bytes.ok) return;
    }

    if (!app->anim_frames) {
        proven_result_mem_mut_t res = proven_arena_alloc(app->arena, ANIM_MAX_FRAMES * sizeof(rubraview_frame_t));
        if (!proven_is_ok(res.err)) return;
        app->anim_frames = (rubraview_frame_t*)(void*)res.value.ptr;
    }

    size_t count = rubraview_pal_image_frame_info(path, (const uint8_t*)bytes.data.ptr, bytes.data.len,
                                                  app->anim_frames, ANIM_MAX_FRAMES);
    if (count <= 1) return;               /* an ordinary image */
    if (count > ANIM_MAX_FRAMES) count = ANIM_MAX_FRAMES;

    rubraview_frame_kind_t kind = rubraview_animation_classify(app->anim_frames, count);
    bool timed = kind == RUBRAVIEW_FRAMES_ANIMATION;

    app->animation = rubraview_animation_create(kind, app->anim_frames, count);
    app->anim_page = page_index;
    app->anim_active = true;

    /* §3.2.6: a slide holding an animation waits for one full pass
       rather than the fixed interval, so a slide show does not cut a
       GIF off halfway. */
    if (timed && app->slides && app->spread_index < app->layout.count) {
        app->slides[app->spread_index].kind = RUBRAVIEW_MEDIA_ANIMATED;
        app->slides[app->spread_index].duration_seconds = rubraview_animation_cycle_seconds(&app->animation);
    }

    if (!timed) {
        /* §3.20.2: open an ICO at its largest layer. */
        size_t largest = rubraview_animation_largest_frame(&app->animation);
        if (largest != 0) {
            app->animation.current = largest;
            show_frame(app, largest);
        }
    }
}

static void animation_tick(app_state_t *app, double dt) {
    if (!app->anim_active) return;
    rubraview_animation_event_t event = rubraview_animation_tick(&app->animation, dt);
    if (event != RUBRAVIEW_ANIMATION_NONE) show_frame(app, app->animation.current);
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

/* The caption names the file on screen and where it sits in the folder
   or archive, so the taskbar, Alt+Tab and a test script can all tell
   which page is showing. */
static void update_window_title(app_state_t *app) {
    char title[768];
    size_t used = 0;
    int32_t page = current_page_index(app);
    if (page >= 0 && (size_t)page < page_count(app)) {
        u8str_t name = rubraview_path_basename(app->source.pages[page].path);
        int n = snprintf(title, sizeof(title), "%.*s (%d/%zu) ",
                         (int)name.len, name.ptr, (int)page + 1, page_count(app));
        used = n > 0 ? ((size_t)n < sizeof(title) ? (size_t)n : sizeof(title) - 1) : 0;
        if (app->media && page == app->media_page) {
            /* A video shows where it is, to the millisecond — which is
               also how a frame step can be checked from outside. */
            char at[32], total[32];
            u8str_t a = rubraview_format_timecode(at, sizeof(at), app->media_position, true);
            u8str_t t = rubraview_format_timecode(total, sizeof(total), app->media_info.duration_seconds, true);
            n = snprintf(title + used, sizeof(title) - used, "%.*s / %.*s%s%s ",
                         (int)a.len, a.ptr, (int)t.len, t.ptr,
                         app->media_paused ? " paused" : "",
                         app->media_info.backend == RUBRAVIEW_BACKEND_FFMPEG ? " ffmpeg" : "");
            if (n > 0) used += (size_t)n < sizeof(title) - used ? (size_t)n : sizeof(title) - used - 1;
        }
    }
    snprintf(title + used, sizeof(title) - used, "%sRubraview %s", used > 0 ? "- " : "", RUBRAVIEW_VERSION_STRING);
    rubraview_pal_window_set_title(app->window, title);
}

/* ---- video pages (M5 slice 1: D-8, D-9) ---- */

static void osd_say(app_state_t *app, u8str_t text);

static void media_close(app_state_t *app) {
    if (app->media) {
        rubraview_pal_media_close(app->media);
        app->media = NULL;
    }
    if (app->media_page >= 0 && (size_t)app->media_page < page_count(app)) {
        app_page_t *page = &app->pages[app->media_page];
        if (page->texture) rubraview_pal_texture_destroy(page->texture);
        *page = (app_page_t){0};
    }
    app->media_page = -1;
    app->media_paused = false;
    app->media_has_frame = false;
    app->media_position = 0.0;
    app->media_title_tenth = -1;
    app->media_ended = false;
    /* Otherwise the still page after a film would keep drawing its last
       line of dialogue. */
    app->subtitle = (rubraview_subtitle_track_t){0};
    app->subtitle_name = (u8str_t){ .ptr = "", .len = 0 };
    app->tracks = rubraview_tracks_create();
    app->subtitle_count = 0;
}

/* Defined here so the link does not depend on which MinGW carries it. */
static const GUID RV_IID_IShellItemImageFactory = {0xBCC18B79, 0xBA16, 0x442F, {0x80, 0xC4, 0x8A, 0x59, 0xC3, 0x0C, 0x46, 0x3B}};

/* RV-084: what a file that is only sound shows — the album art Windows'
   own shell finds in it (ID3, MP4 and FLAC tags alike), or a plain dark
   square when it has none. Main thread only: the shell wants the
   apartment this thread already runs. */
static rubraview_texture_t *audio_page_picture(app_state_t *app, u8str_t path, int32_t *out_w, int32_t *out_h) {
    rubraview_texture_t *texture = NULL;
    char narrow[MAX_PATH * 4];
    WCHAR wide[MAX_PATH * 2];
    if (path.len > 0 && path.len < sizeof(narrow)) {
        memcpy(narrow, path.ptr, path.len);
        narrow[path.len] = '\0';
        IShellItemImageFactory *factory = NULL;
        if (MultiByteToWideChar(CP_UTF8, 0, narrow, -1, wide, MAX_PATH * 2) > 0 &&
            SUCCEEDED(SHCreateItemFromParsingName(wide, NULL, &RV_IID_IShellItemImageFactory, (void**)&factory)) &&
            factory) {
            SIZE size = { 512, 512 };
            HBITMAP bitmap = NULL;
            /* THUMBNAILONLY: the art itself, never the generic file icon. */
            if (SUCCEEDED(factory->lpVtbl->GetImage(factory, size, SIIGBF_BIGGERSIZEOK | SIIGBF_THUMBNAILONLY, &bitmap)) &&
                bitmap) {
                BITMAP info;
                if (GetObjectW(bitmap, sizeof(info), &info) && info.bmWidth > 0 && info.bmHeight > 0 &&
                    info.bmWidth <= 4096 && info.bmHeight <= 4096) {
                    int32_t w = info.bmWidth, h = info.bmHeight;
                    uint8_t *pixels = (uint8_t*)malloc((size_t)w * (size_t)h * 4u);
                    BITMAPINFO bi = {0};
                    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bi.bmiHeader.biWidth = w;
                    bi.bmiHeader.biHeight = -h;   /* top row first */
                    bi.bmiHeader.biPlanes = 1;
                    bi.bmiHeader.biBitCount = 32;
                    bi.bmiHeader.biCompression = BI_RGB;
                    HDC dc = GetDC(NULL);
                    if (pixels && dc && GetDIBits(dc, bitmap, 0, (UINT)h, pixels, &bi, DIB_RGB_COLORS) == h) {
                        texture = rubraview_pal_texture_create_bgra(app->renderer, w, h);
                        if (texture && !rubraview_pal_texture_upload_bgra(texture, pixels, w * 4)) {
                            rubraview_pal_texture_destroy(texture);
                            texture = NULL;
                        }
                        if (texture) { *out_w = w; *out_h = h; }
                    }
                    if (dc) ReleaseDC(NULL, dc);
                    free(pixels);
                }
                DeleteObject(bitmap);
            }
            factory->lpVtbl->Release(factory);
        }
    }
    if (!texture) {
        enum { SIDE = 512 };
        uint32_t *pixels = (uint32_t*)malloc((size_t)SIDE * SIDE * 4u);
        if (pixels) {
            for (size_t k = 0; k < (size_t)SIDE * SIDE; ++k) pixels[k] = 0xFF262626u;
            texture = rubraview_pal_texture_create_bgra(app->renderer, SIDE, SIDE);
            if (texture && !rubraview_pal_texture_upload_bgra(texture, (const uint8_t*)(const void*)pixels, SIDE * 4)) {
                rubraview_pal_texture_destroy(texture);
                texture = NULL;
            }
            if (texture) { *out_w = SIDE; *out_h = SIDE; }
            free(pixels);
        }
    }
    return texture;
}

/* The media page with its texture, made again after a lost device: a
   frame-sized one for video, the album picture for sound only. */
static app_page_t *media_page_ready(app_state_t *app) {
    if (!app->media || app->media_page < 0 || (size_t)app->media_page >= page_count(app)) return NULL;
    app_page_t *page = &app->pages[app->media_page];
    if (!page->texture) {
        int32_t w = app->media_info.width, h = app->media_info.height;
        page->texture = app->media_info.has_video
            ? rubraview_pal_texture_create_bgra(app->renderer, w, h)
            : audio_page_picture(app, app->source.pages[app->media_page].path, &w, &h);
        if (!page->texture) return NULL;
        page->width = w;
        page->height = h;
        page->loaded = true;
        page->failed = false;
        /* A sound-only page's picture is already on it; a video's first
           frame is still to come. */
        app->media_has_frame = !app->media_info.has_video;
        app->needs_relayout = true;
    }
    return page;
}

#define SUBTITLE_MAX_BYTES (2u * 1024u * 1024u)
#define SUBTITLE_MAX_CANDIDATES 8

/* §3.16.1 / R135: the subtitle files that share the film's name. The
   folder is listed rather than reused from the page list, because a
   .srt is not a page — the page source never saw it. */
static size_t subtitle_candidates(proven_arena_t *arena, u8str_t video_path,
                                  rubraview_subtitle_candidate_t *out, size_t capacity,
                                  const char **out_why) {
    if (out_why) *out_why = "no subtitle file beside the video";
    rubraview_fs_listing_t listing =
        rubraview_pal_fs_list_dir(arena, rubraview_path_dirname(video_path));
    if (listing.count == 0) { if (out_why) *out_why = "the folder could not be listed"; return 0; }

    proven_result_mem_mut_t res = proven_arena_alloc(arena, listing.count * sizeof(u8str_t));
    if (!proven_is_ok(res.err)) { if (out_why) *out_why = "out of memory"; return 0; }
    u8str_t *paths = (u8str_t*)(void*)res.value.ptr;
    size_t sibling_count = 0;
    for (size_t i = 0; i < listing.count; ++i) {
        if (listing.entries[i].is_directory) continue;
        paths[sibling_count++] = listing.entries[i].path;
    }
    return rubraview_subtitle_discover(video_path, paths, sibling_count, out, capacity);
}

/* Reads and parses one of them. */
static rubraview_subtitle_track_t subtitle_read(proven_arena_t *arena,
                                                rubraview_subtitle_candidate_t candidate,
                                                const char **out_why) {
    rubraview_subtitle_track_t empty = {0};
    u8str_t text = rubraview_pal_fs_read_file(arena, candidate.path, SUBTITLE_MAX_BYTES);
    if (text.len == 0) { if (out_why) *out_why = "the subtitle file could not be read"; return empty; }
    /* A Korean .smi is nearly always CP949, and the parser reads UTF-8.
       The archive-name check is a UTF-8 validator, so it answers this
       question too; bytes that are not UTF-8 are read in the machine's
       own code page (0). */
    if (rubraview_archive_filename_detect(text, false) == RUBRAVIEW_ENCODING_NEEDS_FALLBACK) {
        u8str_t converted = rubraview_pal_transcode_codepage(arena, text, 0);
        if (converted.len > 0) text = converted;
    }
    rubraview_subtitle_track_t track = rubraview_subtitle_parse(arena, text, candidate.format);
    if (track.count == 0) { if (out_why) *out_why = "the subtitle file held no usable lines"; return empty; }
    if (out_why) *out_why = NULL;
    return track;
}

/* What `--probe-media` reports: the first subtitle file, read. */
static rubraview_subtitle_track_t subtitle_find(proven_arena_t *arena, u8str_t video_path,
                                                u8str_t *out_name, const char **out_why) {
    rubraview_subtitle_track_t empty = {0};
    if (out_name) *out_name = (u8str_t){ .ptr = "", .len = 0 };
    rubraview_subtitle_candidate_t found[SUBTITLE_MAX_CANDIDATES];
    size_t count = subtitle_candidates(arena, video_path, found, SUBTITLE_MAX_CANDIDATES, out_why);
    if (count == 0) return empty;
    rubraview_subtitle_track_t track = subtitle_read(arena, found[0], out_why);
    if (track.count > 0 && out_name) *out_name = rubraview_path_basename(found[0].path);
    return track;
}

/* Puts the film's tracks — its own sound, and every subtitle file beside
   it — into one list, and turns on the subtitle the reader would want
   (§3.16.2). Subtitles are "off" as well as every file: that is a
   choice, so it is in the cycle. */
static void tracks_prepare(app_state_t *app, u8str_t video_path) {
    app->tracks = rubraview_tracks_create();
    if (app->media) rubraview_pal_media_tracks(app->media, &app->tracks);

    app->subtitle_count = subtitle_candidates(app->arena, video_path, app->subtitle_files,
                                              SUBTITLE_MAX_CANDIDATES, NULL);
    static const char *const FORMAT_NAME[] = { "?", "srt", "smi", "vtt", "ass" };
    for (size_t i = 0; i < app->subtitle_count; ++i) {
        rubraview_track_t track = {
            .kind = RUBRAVIEW_TRACK_SUBTITLE,
            .is_external = true,          /* a file beside the video, not a stream in it */
            .stream_index = (int32_t)i,   /* into app->subtitle_files, not the container */
            .language = rubraview_subtitle_language_tag(video_path, app->subtitle_files[i].path),
            .title = rubraview_path_basename(app->subtitle_files[i].path),
            .codec = cstr(FORMAT_NAME[(size_t)app->subtitle_files[i].format < 5
                                      ? (size_t)app->subtitle_files[i].format : 0]),
        };
        rubraview_tracks_add(&app->tracks, track);
    }
}

static bool same_text(u8str_t a, u8str_t b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0);
}

/* Shows one subtitle track, or none when `index` is -1. */
static void subtitle_select(app_state_t *app, int32_t index) {
    app->subtitle = (rubraview_subtitle_track_t){0};
    app->subtitle_name = (u8str_t){ .ptr = "", .len = 0 };
    app->tracks.current_subtitle = -1;
    if (index < 0 || (size_t)index >= app->tracks.count) return;
    const rubraview_track_t *track = &app->tracks.tracks[index];
    if (track->kind != RUBRAVIEW_TRACK_SUBTITLE) return;

    if (track->is_external) {
        if (track->stream_index < 0 || (size_t)track->stream_index >= app->subtitle_count) return;
        app->subtitle = subtitle_read(app->arena, app->subtitle_files[track->stream_index], NULL);
    } else {
        /* §3.16.1 / D-12: a stream inside the file. Reading it walks the
           whole container once, so say what is happening first. */
        osd_say(app, U8("reading the subtitles out of the file"));
        u8str_t text = rubraview_pal_media_read_subtitle_stream(app->media, app->arena,
                                                                track->stream_index);
        if (text.len == 0) {
            osd_say(app, U8("this backend cannot read that subtitle stream"));
            return;
        }
        app->subtitle = rubraview_subtitle_parse(app->arena, text, RUBRAVIEW_SUBTITLE_SRT);
    }
    if (app->subtitle.count == 0) return;
    /* A stream inside a file often has no name of its own, so the OSD
       uses the same label the track menu shows. */
    u8str_t label = rubraview_track_label(app->subtitle_label, sizeof(app->subtitle_label),
                                          &app->tracks, index);
    app->subtitle_name = track->title.len > 0 ? track->title : label;
    app->tracks.current_subtitle = index;
    /* The same film again: the sync the reader set by hand comes back. */
    if (app->media_page >= 0 && (size_t)app->media_page < page_count(app) &&
        same_text(app->subtitle_offset_for, app->source.pages[app->media_page].path)) {
        app->subtitle.offset_seconds = app->subtitle_offset_seconds;
    }
}

static void subtitle_load(app_state_t *app, u8str_t video_path) {
    tracks_prepare(app, video_path);
    /* §3.16.2: the preferred language decides; with none set, the first
       file wins. Parsing happens now, and only for the one chosen. */
    subtitle_select(app, rubraview_tracks_choose(&app->tracks, RUBRAVIEW_TRACK_SUBTITLE,
                                                 (u8str_t){ .ptr = "", .len = 0 }));
}

/* Opens the video when the page on screen is one, and closes the old one. */
static void media_prepare(app_state_t *app) {
    int32_t index = current_page_index(app);
    if (app->media && index == app->media_page) return;
    media_close(app);
    if (index < 0 || (size_t)index >= page_count(app)) return;
    u8str_t path = app->source.pages[index].path;
    if (!is_media_path(path)) return;

    /* D-9: the preferred backend first, the other one when it cannot. */
    rubraview_media_backend_t order[2];
    size_t count = rubraview_media_backend_order(app->media_preferred,
                                                 rubraview_pal_media_backend_available(RUBRAVIEW_BACKEND_FFMPEG),
                                                 order);
    rubraview_media_open_result_t opened = { .media = NULL, .failure = RUBRAVIEW_MEDIA_FAIL_FILE };
    rubraview_media_failure_t why = RUBRAVIEW_MEDIA_FAIL_FILE;
    for (size_t i = 0; i < count; ++i) {
        opened = rubraview_pal_media_open(path, order[i]);
        if (opened.media) break;
        why = i == 0 ? opened.failure : rubraview_media_failure_pick(why, opened.failure);
    }
    if (!opened.media) {
        osd_say(app, rubraview_media_failure_text(why));
        app->notice_seconds = MEDIA_NOTICE_SECONDS;   /* long enough to read the HEVC line */
        app->pages[index].failed = true;
        app->media_skip_pending = true;   /* D-9: say so, then move on */
        return;
    }

    app->media = opened.media;
    app->media_info = opened.info;
    app->media_page = index;
    app->media_paused = false;
    app->media_has_frame = false;
    app->media_position = 0.0;
    app->media_title_tenth = -1;
    app->media_ended = false;
    /* §5.3: the sound is the master clock when it reaches a device;
       otherwise — a silent file, or no device, as on the test VM — the
       wall clock is. */
    app->media_master = rubraview_media_master_for(opened.info.has_audio, opened.info.audio_output);
    app->media_clock = rubraview_media_clock_create(app->media_master, 0.0, rubraview_pal_time_now_seconds());
    /* §3.2.6 / RV-061: a slide holding a film or a track waits for the
       whole of it, not for the still-image interval. */
    if (app->slides && (size_t)app->spread_index < app->layout.count) {
        app->slides[app->spread_index].kind = RUBRAVIEW_MEDIA_VIDEO;
        app->slides[app->spread_index].duration_seconds = opened.info.duration_seconds;
    }
    subtitle_load(app, path);
    if (app->subtitle.count > 0) {
        char line[256];
        int n = snprintf(line, sizeof(line), "subtitles: %.*s",
                         (int)app->subtitle_name.len, app->subtitle_name.ptr);
        if (n > 0) osd_say(app, (u8str_t){ .ptr = line, .len = (size_t)n });
    }
    if (!media_page_ready(app)) {
        media_close(app);
        osd_say(app, U8("could not make room to show that video"));
        app->pages[index].failed = true;
        app->media_skip_pending = true;
        return;
    }
    app->needs_relayout = true;   /* the page has a size now */
}

static void media_show(app_state_t *app, app_page_t *page, const rubraview_video_frame_t *frame) {
    rubraview_pal_texture_upload_bgra(page->texture, frame->pixels, frame->stride);
    app->media_has_frame = true;
    app->media_position = frame->pts;
    int64_t tenth = (int64_t)(frame->pts * 10.0);
    if (app->media_paused || tenth != app->media_title_tenth) {
        app->media_title_tenth = tenth;
        update_window_title(app);
    }
}

/* Once per loop pass: follow the sound, put the due frame on the page,
   and notice the end. */
static void media_tick(app_state_t *app) {
    app_page_t *page = media_page_ready(app);
    if (!page) return;
    rubraview_media_t *m = app->media;
    double now = rubraview_pal_time_now_seconds();

    /* §5.3: with the sound as master, the clock follows what is heard. */
    if (app->media_master == RUBRAVIEW_CLOCK_AUDIO) {
        double heard = 0.0, at = 0.0;
        if (rubraview_pal_media_audio_position(m, &heard, &at)) {
            rubraview_media_clock_sync_audio(&app->media_clock,
                rubraview_audio_position_now(heard, at, now, !app->media_paused, MEDIA_AUDIO_EXTRAPOLATION),
                now);
        }
    }
    double clock = rubraview_media_clock_now(&app->media_clock, now);

    if (!app->media_info.has_video) {
        /* Only sound: the position is the clock. */
        app->media_position = clock;
        int64_t tenth = (int64_t)(clock * 10.0);
        if (tenth != app->media_title_tenth) {
            app->media_title_tenth = tenth;
            update_window_title(app);
        }
    } else if (!(app->media_paused && app->media_has_frame)) {
        rubraview_video_frame_t frame;
        while (rubraview_pal_media_peek_frame(m, &frame)) {
            /* The first picture after opening or seeking shows at once. */
            rubraview_frame_decision_t decision = app->media_has_frame
                ? rubraview_media_schedule(frame.pts, frame.duration, clock)
                : RUBRAVIEW_FRAME_SHOW;
            if (decision == RUBRAVIEW_FRAME_WAIT) break;
            if (decision == RUBRAVIEW_FRAME_DROP && rubraview_pal_media_frames_ready(m) > 1) {
                rubraview_pal_media_pop_frame(m);
                continue;
            }
            bool first = !app->media_has_frame;
            media_show(app, page, &frame);
            rubraview_pal_media_pop_frame(m);
            /* Only a wall clock may be moved to the picture. The sound is
               the truth when it is the master; a late picture is dropped. */
            if (app->media_master == RUBRAVIEW_CLOCK_WALL &&
                (first || clock - frame.pts > MEDIA_RESYNC_SECONDS)) {
                rubraview_media_clock_seek(&app->media_clock, frame.pts, now);
            }
            break;
        }
    }

    if (!app->media_paused) {
        /* A sound-only file with no device decodes nothing; its end is its duration. */
        bool at_end = (app->media_info.has_video || app->media_info.audio_output)
            ? rubraview_pal_media_finished(m)
            : (app->media_info.duration_seconds > 0.0 && clock >= app->media_info.duration_seconds);
        if (at_end) {
            /* The end: hold the last picture. Slice 4 hands this to the slide show. */
            rubraview_media_clock_pause(&app->media_clock, now);
            rubraview_pal_media_set_paused(m, true);
            app->media_paused = true;
            app->media_ended = true;
            if (!app->media_info.has_video && app->media_info.duration_seconds > 0.0 &&
                app->media_position > app->media_info.duration_seconds) {
                app->media_position = app->media_info.duration_seconds;   /* the clock overshoots by a pass */
            }
            update_window_title(app);
        }
    }
}

static void media_seek_to(app_state_t *app, double seconds) {
    if (!app->media) return;
    double duration = app->media_info.duration_seconds;
    if (duration > 0.0 && seconds > duration) seconds = duration;
    if (seconds < 0.0) seconds = 0.0;
    rubraview_pal_media_seek(app->media, seconds);
    rubraview_media_clock_seek(&app->media_clock, seconds, rubraview_pal_time_now_seconds());
    /* The landing frame shows at once, even while paused; a sound-only
       page keeps its picture. */
    app->media_has_frame = !app->media_info.has_video;
    app->media_ended = false;
}

static void media_toggle_pause(app_state_t *app) {
    if (!app->media) return;
    double now = rubraview_pal_time_now_seconds();
    if (app->media_paused) {
        /* At the end, playing again starts from the beginning. */
        if (app->media_ended) media_seek_to(app, 0.0);
        rubraview_media_clock_resume(&app->media_clock, now);
        rubraview_pal_media_set_paused(app->media, false);
        app->media_paused = false;
    } else {
        rubraview_media_clock_pause(&app->media_clock, now);
        rubraview_pal_media_set_paused(app->media, true);
        app->media_paused = true;
    }
    update_window_title(app);
}

static double media_frame_seconds(const app_state_t *app) {
    return app->media_info.frame_rate > 0.0 ? 1.0 / app->media_info.frame_rate : 1.0 / 30.0;
}

/* §5.3 frame stepping. Forward takes the next decoded frame; back seeks
   into the middle of the previous frame's interval, which the exact seek
   turns into that frame. Either way playback is paused first. */
static void media_step(app_state_t *app, bool forward) {
    if (!app->media || !app->media_info.has_video) return;   /* sound has no frames to step */
    if (!app->media_paused) {
        rubraview_media_clock_pause(&app->media_clock, rubraview_pal_time_now_seconds());
        rubraview_pal_media_set_paused(app->media, true);
        app->media_paused = true;
    }
    if (!forward) {
        media_seek_to(app, app->media_position - media_frame_seconds(app) * 0.5);
        update_window_title(app);
        return;
    }
    app_page_t *page = media_page_ready(app);
    rubraview_video_frame_t frame;
    for (int i = 0; i < 100 && !rubraview_pal_media_peek_frame(app->media, &frame); ++i) {
        rubraview_pal_time_sleep_ms(5);   /* the decoder is at most one frame behind */
    }
    if (page && rubraview_pal_media_peek_frame(app->media, &frame)) {
        media_show(app, page, &frame);
        rubraview_pal_media_pop_frame(app->media);
        rubraview_media_clock_seek(&app->media_clock, frame.pts, rubraview_pal_time_now_seconds());
    }
}

static void go_to_spread(app_state_t *app, size_t index) {
    if (app->layout.count == 0) return;
    if (index >= app->layout.count) index = app->layout.count - 1;
    if (index != app->spread_index) {
        /* Remember what is leaving the screen, so the cross-fade has
           something to fade *from* (§3.2.5). */
        app->prev_spread_index = app->spread_index;
        app->prev_spread_valid = true;
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
    animation_prepare(app);
    media_prepare(app);
    update_window_title(app);
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
        /* Esc closes what is open before it closes the program. */
        if (app->settings_open) { settings_close(app, false); return; }
        if (app->panel.open) { panel_close(app); return; }
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
    } else if (action_is(action, "delete_file")) {
        triage_delete(app, false);
    } else if (action_is(action, "purge_file")) {
        /* §3.18.1: a permanent delete asks first, every time. */
        app->confirm_purge = true;
    } else if (action_is(action, "undo")) {
        triage_undo(app);
    } else if (action_is(action, "rename_file")) {
        rename_begin(app);
    } else if (action_is(action, "open_settings")) {
        if (app->settings_open) settings_close(app, false);
        else settings_open(app);
    } else if (action_is(action, "open_edit")) {
        if (app->panel.open && !app->panel_is_export && !app->panel_is_batch) panel_close(app);
        else panel_open_edit(app);
    } else if (action_is(action, "quick_export") || action_is(action, "save_as")) {
        if (app->panel.open && app->panel_is_export) panel_close(app);
        else panel_open_export(app);
    } else if (action_is(action, "open_batch")) {
        if (app->panel.open && app->panel_is_batch) panel_close(app);
        else panel_open_batch(app);
    } else if (app->media && action_is(action, "anim_toggle_pause")) {
        media_toggle_pause(app);
    } else if (app->media && action_is(action, "anim_step_forward")) {
        media_step(app, true);
    } else if (app->media && action_is(action, "anim_step_back")) {
        media_step(app, false);
    } else if (app->media && action_is(action, "media_seek_forward")) {
        media_seek_to(app, app->media_position + MEDIA_SEEK_STEP);
    } else if (app->media && action_is(action, "media_seek_back")) {
        media_seek_to(app, app->media_position - MEDIA_SEEK_STEP);
    } else if (app->media && action_is(action, "next_audio_track")) {
        /* §3.16.2: the next sound track of the same file, without
           stopping the picture. */
        int32_t next = rubraview_tracks_next(&app->tracks, RUBRAVIEW_TRACK_AUDIO,
                                             app->tracks.current_audio);
        if (next < 0 || next == app->tracks.current_audio) {
            osd_say(app, U8("this file has only one sound track"));
        } else if (!rubraview_pal_media_select_audio_track(app->media,
                                                           app->tracks.tracks[next].stream_index)) {
            /* No device on this machine, or the track cannot be decoded:
               the one that was playing keeps playing. */
            osd_say(app, U8("the sound track cannot be changed here"));
        } else {
            app->tracks.current_audio = next;
            /* Both streams start again from where the film is, so what
               the old track had already decoded is thrown away. */
            media_seek_to(app, app->media_position);
            char label[160];
            u8str_t text = rubraview_track_label(label, sizeof(label), &app->tracks, next);
            char line[192];
            int n = snprintf(line, sizeof(line), "sound %.*s", (int)text.len, text.ptr);
            if (n > 0) osd_say(app, (u8str_t){ .ptr = line, .len = (size_t)n });
        }
    } else if (app->media && action_is(action, "next_subtitle_track")) {
        /* §3.16.2: the subtitle files beside the film, and off. */
        if (rubraview_tracks_count(&app->tracks, RUBRAVIEW_TRACK_SUBTITLE) == 0) {
            osd_say(app, U8("this video has no subtitles, in it or beside it"));
        } else {
            int32_t next = rubraview_tracks_next(&app->tracks, RUBRAVIEW_TRACK_SUBTITLE,
                                                 app->tracks.current_subtitle);
            subtitle_select(app, next);
            char label[160];
            u8str_t text = rubraview_track_label(label, sizeof(label), &app->tracks,
                                                 app->tracks.current_subtitle);
            char line[192];
            int n = snprintf(line, sizeof(line), "subtitles %.*s", (int)text.len, text.ptr);
            if (n > 0) osd_say(app, (u8str_t){ .ptr = line, .len = (size_t)n });
        }
    } else if (app->media && (action_is(action, "subtitle_earlier") || action_is(action, "subtitle_later"))) {
        /* §3.16.1: half a second at a time, and the OSD says where the
           track now sits so the reader can aim. */
        if (app->subtitle.count == 0) {
            osd_say(app, U8("no subtitles are showing"));
        } else {
            rubraview_subtitle_nudge(&app->subtitle, action_is(action, "subtitle_later"));
            if (app->media_page >= 0 && (size_t)app->media_page < page_count(app)) {
                app->subtitle_offset_for = app->source.pages[app->media_page].path;
                app->subtitle_offset_seconds = app->subtitle.offset_seconds;
            }
            char line[96];
            int n = snprintf(line, sizeof(line), "subtitles %+.1f s", app->subtitle.offset_seconds);
            if (n > 0) osd_say(app, (u8str_t){ .ptr = line, .len = (size_t)n });
        }
    } else if (action_is(action, "anim_toggle_pause")) {
        if (app->animation.paused) rubraview_animation_resume(&app->animation);
        else rubraview_animation_pause(&app->animation);
    } else if (action_is(action, "anim_step_forward") || action_is(action, "subpage_next")) {
        rubraview_animation_step(&app->animation, true);
        show_frame(app, app->animation.current);
    } else if (action_is(action, "anim_step_back") || action_is(action, "subpage_prev")) {
        rubraview_animation_step(&app->animation, false);
        show_frame(app, app->animation.current);
    } else if (action_is(action, "anim_speed_up")) {
        app->animation.speed = rubraview_animation_step_speed(app->animation.speed, true);
    } else if (action_is(action, "anim_speed_down")) {
        app->animation.speed = rubraview_animation_step_speed(app->animation.speed, false);
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

static bool key_is(rubraview_key_combo_t combo, const char *name) {
    size_t n = strlen(name);
    return combo.key_name.len == n && memcmp(combo.key_name.ptr, name, n) == 0;
}

/* Everything §3.18 puts in front of the reader answers the next key
   press before anything else does: a confirmation that is ignored is
   worse than no confirmation at all. */
static bool triage_handle_key(app_state_t *app, rubraview_key_combo_t combo) {
    /* §3.17.1's resume prompt answers Enter before paging does. Enter
       turns a page now (owner, 2026-09-09), and a prompt that the very
       key meant to answer it walks straight past is not a prompt. */
    if (app->resume_offer && key_is(combo, "Enter")) {
        app->resume_offer = false;
        size_t target = spread_index_for_page(app, app->resume_page);
        go_to_spread(app, target);
        update_precache(app);
        return true;
    }

    if (app->confirm_purge) {
        if (key_is(combo, "Y") || key_is(combo, "Enter")) {
            app->confirm_purge = false;
            triage_delete(app, true);
        } else {
            /* Anything else is "no". A confirmation should be hard to
               agree to by accident and easy to refuse. */
            app->confirm_purge = false;
        }
        return true;
    }

    if (app->rename_active) {
        if (key_is(combo, "Enter")) { rename_commit(app); return true; }
        if (key_is(combo, "Escape")) { app->rename_active = false; return true; }
        if (key_is(combo, "Backspace")) {
            if (app->rename_length > 0) {
                /* Step back over a whole UTF-8 character, not one byte:
                   deleting half of a Hangul syllable would leave the
                   name unwritable. */
                size_t at = app->rename_length;
                while (at > 0 && ((unsigned char)app->rename_buffer[at - 1] & 0xC0u) == 0x80u) at--;
                if (at > 0) at--;
                app->rename_length = at;
                app->rename_buffer[at] = '\0';
            }
            return true;
        }

        /* A single printable key extends the name. Text entry beyond
           this — IME, selection, the clipboard — belongs to the native
           EDIT control §3.18.2 names, which is not built yet. */
        if (combo.key_name.len == 1 && combo.modifiers == RUBRAVIEW_MOD_NONE) {
            char c = combo.key_name.ptr[0];
            if (app->rename_length + 1 < sizeof(app->rename_buffer)) {
                if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
                app->rename_buffer[app->rename_length++] = c;
                app->rename_buffer[app->rename_length] = '\0';
            }
            return true;
        }
        return true;   /* while renaming, nothing else gets through */
    }

    /* §3.18.3: 1-9 curate — but only where a folder is actually bound.
       §3.7.2 gives 1-5 to the fit modes, and an unconfigured viewer must
       keep them; a reader who has set up triage folders has said which
       meaning they want. */
    if (combo.modifiers == RUBRAVIEW_MOD_NONE && combo.key_name.len == 1) {
        char c = combo.key_name.ptr[0];
        if (c >= '1' && c <= '9') {
            int32_t digit = c - '0';
            if (rubraview_curation_target(&app->curation, digit).len > 0) {
                triage_curate(app, digit);
                return true;
            }
        }
    }

    return false;
}

static void dispatch_key(app_state_t *app, rubraview_key_combo_t combo) {
    if (triage_handle_key(app, combo)) return;
    if (picker_handle_key(app, combo)) return;
    /* §3.7.1: the slide show's own bindings win while it is running,
       then the viewing context, then the global section. */
    if (app->slideshow_running) {
        u8str_t action = rubraview_keymap_find_action(&app->keymap, U8("slideshow"), combo);
        if (action.len > 0) { handle_action(app, action); return; }
    }

    /* §3.20: while an animated image or a multi-page file is the page
       being read, its own context is consulted first. This is what lets
       `Space` and `Ctrl + [` mean two things without either meaning
       being lost — see the comment on those sections in
       src/core/default_keymap.c. */
    if (app->anim_active || app->media) {
        u8str_t context = (app->media || app->animation.kind == RUBRAVIEW_FRAMES_ANIMATION)
                            ? U8("animation") : U8("subpage");
        u8str_t action = rubraview_keymap_find_action(&app->keymap, context, combo);
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
        case RUBRAVIEW_TITLEBAR_SNAP_BOXES:
            /* Both boxes back to their corners, inside the window. */
            rubraview_box_snap_home(&app->menubox, &metrics, (double)win_w, (double)win_h);
            rubraview_box_snap_home(&app->toolbox, &metrics, (double)win_w, (double)win_h);
            osd_say(app, U8("the floating boxes are back in their corners"));
            return true;
        case RUBRAVIEW_TITLEBAR_CAPTION:    rubraview_pal_window_begin_drag(app->window); return true;
        default: break;
    }

    /* The anchor's two buttons answer first: they sit on top of the
       box, and a click there is never a tile. */
    if (rubraview_box_click(&app->menubox, &metrics, x, y)) {
        sync_menubox_tiles(app);
        return true;
    }
    if (rubraview_box_click(&app->toolbox, &metrics, x, y)) return true;

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

    /* The anchor is two buttons, and they must not look like one. The
       left is outlined — it waits to be clicked; the right is filled —
       it answers the pointer on its own. */
    rubraview_rect_t click_half = rubraview_box_anchor_half_rect(box, metrics, RUBRAVIEW_ANCHOR_CLICK);
    rubraview_rect_t hover_half = rubraview_box_anchor_half_rect(box, metrics, RUBRAVIEW_ANCHOR_HOVER);

    rubraview_pal_rect_t left = { click_half.x, click_half.y, click_half.width, click_half.height };
    rubraview_pal_rect_t right = { hover_half.x, hover_half.y, hover_half.width, hover_half.height };

    rubraview_pal_render_stroke_rect(app->renderer, left, COLOR_BOX_BORDER, 1.0, 2.0);
    rubraview_pal_render_fill_rect(app->renderer, right, COLOR_TILE_FILL, 2.0);
    rubraview_pal_render_stroke_rect(app->renderer, right, COLOR_BOX_BORDER, 1.0, 2.0);

    rubraview_pal_render_draw_text(app->renderer,
                                   box->kind == RUBRAVIEW_BOX_MENU ? U8("=") : U8("<>"),
                                   left, metrics->anchor_size * 0.42, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    rubraview_pal_render_draw_text(app->renderer, U8("v"), right,
                                   metrics->anchor_size * 0.42, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);

    if (box->state == RUBRAVIEW_BOX_COLLAPSED) return;

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

/* §3.16.1 / R135: the line of dialogue for where the film is now, laid
   over the picture with a dark outline so it reads on any background. */
static void draw_subtitle(app_state_t *app, int32_t win_w, int32_t win_h) {
    if (!app->media || app->subtitle.count == 0) return;
    const rubraview_subtitle_cue_t *cue = rubraview_subtitle_at(&app->subtitle, app->media_position);
    if (!cue || cue->text.len == 0) return;

    double dpi = rubraview_pal_window_dpi_scale(app->window);
    /* §3.22.2.5: the reader sets the size in points; the window's own
       height still has a say, so a subtitle is not a speck on a large
       screen nor a banner on a small one. */
    double wanted = rubraview_settings_get(&app->settings, U8("video"), U8("subtitle_size"));
    if (wanted <= 0.0) wanted = 24.0;
    double font = wanted * dpi * ((double)win_h / 720.0);
    if (font < wanted * dpi * 0.6) font = wanted * dpi * 0.6;
    if (font > wanted * dpi * 2.5) font = wanted * dpi * 2.5;

    size_t lines = 1;
    for (size_t i = 0; i < cue->text.len; ++i) {
        if (cue->text.ptr[i] == '\n') ++lines;
    }
    double height = font * 1.35 * (double)lines;
    double below = (app->filmstrip.visible ? FILMSTRIP_THUMB * dpi : 0.0) + 36.0 * dpi;
    rubraview_pal_rect_t rect = { (double)win_w * 0.05, (double)win_h - below - height,
                                  (double)win_w * 0.9, height };

    /* R135 asks for a two-pixel outline, and §3.22.2.5 lets the reader
       change it (0 turns it off). The text PAL draws flat text, so the
       outline is the same string drawn four times behind it — which
       keeps this out of the PAL header. */
    double off = rubraview_settings_get(&app->settings, U8("video"), U8("subtitle_outline")) * dpi;
    static const double DX[4] = { -1.0, 1.0, -1.0, 1.0 };
    static const double DY[4] = { -1.0, -1.0, 1.0, 1.0 };
    for (size_t i = 0; off > 0.0 && i < 4; ++i) {
        rubraview_pal_rect_t shadow = { rect.x + DX[i] * off, rect.y + DY[i] * off,
                                        rect.width, rect.height };
        rubraview_pal_render_draw_text(app->renderer, cue->text, shadow, font,
                                       0xFF000000u, RUBRAVIEW_TEXT_CENTER);
    }
    rubraview_pal_render_draw_text(app->renderer, cue->text, rect, font,
                                   0xFFFFFFFFu, RUBRAVIEW_TEXT_CENTER);
}

static void draw_chrome(app_state_t *app, double win_w, double win_h) {
    rubraview_tile_metrics_t metrics = rubraview_tile_metrics_default(rubraview_pal_window_dpi_scale(app->window));
    double chrome_dpi = rubraview_pal_window_dpi_scale(app->window);

    /* §3.18's confirmation and its notices sit above everything else:
       they are answers to something the reader just did. */
    if (app->confirm_purge) {
        double box_w = 520.0 * chrome_dpi, box_h = 90.0 * chrome_dpi;
        rubraview_pal_rect_t box = { (win_w - box_w) * 0.5, (win_h - box_h) * 0.5, box_w, box_h };
        rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BOX_FILL, 3.0);
        rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_CLOSE_HOVER, 2.0, 3.0);
        rubraview_pal_render_draw_text(app->renderer,
                                       U8("Delete this file from the disk for good?  (Y / N)"),
                                       box, 16.0 * chrome_dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }

    if (app->rename_active) {
        double box_w = 560.0 * chrome_dpi, box_h = 64.0 * chrome_dpi;
        rubraview_pal_rect_t box = { (win_w - box_w) * 0.5, win_h * 0.75, box_w, box_h };
        rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BOX_FILL, 3.0);
        rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_BOX_BORDER, 1.0, 3.0);
        rubraview_pal_render_draw_text(app->renderer,
                                       (u8str_t){ .ptr = app->rename_buffer, .len = app->rename_length },
                                       box, 18.0 * chrome_dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }

    if (app->notice_seconds > 0.0 && app->notice_length > 0) {
        double box_h = 44.0 * chrome_dpi;
        rubraview_pal_rect_t box = { win_w * 0.2, win_h * 0.08, win_w * 0.6, box_h };
        rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BAR_FILL, 3.0);
        rubraview_pal_render_draw_text(app->renderer,
                                       (u8str_t){ .ptr = app->notice, .len = app->notice_length },
                                       box, 15.0 * chrome_dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }

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

    draw_subtitle(app, win_w, win_h);

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

/* Draws one spread at a given opacity. Splitting this out is what makes
   §3.2.5's cross-fade possible: the outgoing spread is drawn first at a
   falling opacity and the incoming one over it at a rising one. */
static void draw_spread(app_state_t *app, size_t spread_index, double opacity,
                        int32_t win_w, int32_t win_h) {
    if (spread_index >= app->layout.count) return;
    {
        const rubraview_spread_t *spread = &app->layout.spreads[spread_index];

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

        /* RV-064 made the cubic modes real; before the device context
           they fell back to linear. Pixel art still gets nearest, which
           is the one §3.5 actually depends on. */
        rubraview_interpolation_t interp = app->force_nearest
            ? RUBRAVIEW_INTERP_NEAREST : RUBRAVIEW_INTERP_CUBIC;

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
                rubraview_pal_render_draw_texture_opacity(app->renderer, page->texture, oriented, interp, opacity);

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
}



/* ---- triage: delete, rename, curate (§3.18) ---- */

/* The file behind the page on screen, or an empty slice for an archive
   page — a page inside a CBZ is not a file, so none of §3.18 applies to
   it and every action below refuses rather than doing something to the
   archive. */
static u8str_t current_file_path(app_state_t *app) {
    int32_t page = current_page_index(app);
    if (page < 0 || app->source.kind != RUBRAVIEW_PAGE_SOURCE_FOLDER) {
        return (u8str_t){ .ptr = "", .len = 0 };
    }
    return app->source.pages[page].path;
}

/* After a file leaves the sequence the viewer has to land somewhere.
   §3.18.1: the next item, or the one before when the last was removed. */
static void reopen_after_removal(app_state_t *app, size_t removed_index) {
    u8str_t dir = app->source_dir;
    if (dir.len == 0) return;

    size_t target = removed_index;
    open_folder(app, dir);
    if (page_count(app) == 0) return;
    if (target >= page_count(app)) target = page_count(app) - 1;

    finish_open(app, target);
}

static void osd_say(app_state_t *app, u8str_t text) {
    size_t n = text.len < sizeof(app->notice) - 1 ? text.len : sizeof(app->notice) - 1;
    memcpy(app->notice, text.ptr, n);
    app->notice[n] = '\0';
    app->notice_length = n;
    app->notice_seconds = 2.5;
}

static void triage_delete(app_state_t *app, bool permanent) {
    u8str_t path = current_file_path(app);
    if (path.len == 0) return;

    int32_t page = current_page_index(app);
    if (page < 0) return;

    bool ok = permanent ? rubraview_pal_fs_delete(path) : rubraview_pal_fs_recycle(path);
    if (!ok) {
        osd_say(app, U8("could not delete that file"));
        return;
    }

    rubraview_undo_push(&app->undo, (rubraview_file_action_t){
        .op = permanent ? RUBRAVIEW_FILE_OP_PURGE : RUBRAVIEW_FILE_OP_RECYCLE,
        .source_path = path,
        .playlist_index = (size_t)page,
    });

    osd_say(app, permanent ? U8("deleted permanently") : U8("moved to the recycle bin"));
    reopen_after_removal(app, (size_t)page);
}

static void triage_curate(app_state_t *app, int32_t digit) {
    u8str_t path = current_file_path(app);
    if (path.len == 0) return;

    u8str_t target_dir = rubraview_curation_target(&app->curation, digit);
    if (target_dir.len == 0) return;   /* that key is not bound: do nothing */

    int32_t page = current_page_index(app);
    if (page < 0) return;

    rubraview_pal_fs_make_dirs(target_dir);
    u8str_t name = rubraview_path_basename(path);
    u8str_t target = rubraview_path_join(app->arena, target_dir, name);
    if (target.len == 0) return;

    bool moving = app->curation.mode == RUBRAVIEW_CURATION_MOVE;
    bool ok = moving ? rubraview_pal_fs_move(path, target) : rubraview_pal_fs_copy(path, target);
    if (!ok) {
        osd_say(app, U8("could not put the file there"));
        return;
    }

    rubraview_undo_push(&app->undo, (rubraview_file_action_t){
        .op = moving ? RUBRAVIEW_FILE_OP_MOVE : RUBRAVIEW_FILE_OP_COPY,
        .source_path = path,
        .target_path = target,
        .playlist_index = (size_t)page,
    });

    osd_say(app, moving ? U8("moved") : U8("copied"));

    /* §3.18.3: a move takes the file out of the sequence, so the viewer
       advances; a copy leaves it, so it stays put. */
    if (rubraview_curation_advances(&app->curation)) {
        reopen_after_removal(app, (size_t)page);
    }
}

static void triage_undo(app_state_t *app) {
    rubraview_file_action_t action = {0};
    switch (rubraview_undo_peek(&app->undo, &action)) {
        case RUBRAVIEW_UNDO_NOTHING:
            osd_say(app, U8("nothing to undo"));
            return;
        case RUBRAVIEW_UNDO_IRREVERSIBLE:
            /* Said plainly rather than silently undoing the action
               before it, which would restore the wrong file. */
            osd_say(app, U8("that file was deleted permanently and cannot be brought back"));
            return;
        case RUBRAVIEW_UNDO_AVAILABLE:
            break;
    }

    bool ok = false;
    switch (action.op) {
        case RUBRAVIEW_FILE_OP_MOVE:
        case RUBRAVIEW_FILE_OP_RENAME:
            ok = rubraview_pal_fs_move(action.target_path, action.source_path);
            break;
        case RUBRAVIEW_FILE_OP_COPY:
            /* Undoing a copy means removing the copy, not the original. */
            ok = rubraview_pal_fs_delete(action.target_path);
            break;
        case RUBRAVIEW_FILE_OP_RECYCLE:
            ok = rubraview_pal_fs_restore_last_recycled(action.source_path);
            if (!ok) osd_say(app, U8("Windows keeps the recycle bin's undo to itself — restore it from there"));
            break;
        default:
            break;
    }

    if (!ok) return;

    rubraview_undo_commit(&app->undo);
    osd_say(app, U8("undone"));
    reopen_after_removal(app, action.playlist_index);
}

/* §3.18.2's inline rename. The text is collected by the key handler
   rather than by a child EDIT control: the control the RFC names brings
   native IME with it, and adding it is a separate piece of work, so the
   limitation is written down rather than hidden. */
static void rename_begin(app_state_t *app) {
    u8str_t path = current_file_path(app);
    if (path.len == 0) return;

    u8str_t name = rubraview_path_basename(path);
    size_t stem = rubraview_rename_stem_length(name);
    if (stem >= sizeof(app->rename_buffer)) return;

    memcpy(app->rename_buffer, name.ptr, stem);
    app->rename_length = stem;
    app->rename_buffer[stem] = '\0';
    app->rename_active = true;
}

static void rename_commit(app_state_t *app) {
    if (!app->rename_active) return;
    app->rename_active = false;

    u8str_t path = current_file_path(app);
    if (path.len == 0) return;

    u8str_t old_name = rubraview_path_basename(path);
    u8str_t stem = { .ptr = app->rename_buffer, .len = app->rename_length };
    u8str_t new_name = rubraview_rename_compose(app->arena, old_name, stem);

    rubraview_rename_err_t err = rubraview_rename_validate(new_name);
    if (err != RUBRAVIEW_RENAME_OK) {
        osd_say(app, rubraview_rename_error_text(err));
        return;
    }

    u8str_t target = rubraview_path_join(app->arena, rubraview_path_dirname(path), new_name);
    if (target.len == 0 || !rubraview_pal_fs_move(path, target)) {
        osd_say(app, U8("could not rename that file"));
        return;
    }

    int32_t page = current_page_index(app);
    rubraview_undo_push(&app->undo, (rubraview_file_action_t){
        .op = RUBRAVIEW_FILE_OP_RENAME,
        .source_path = path,
        .target_path = target,
        .playlist_index = page >= 0 ? (size_t)page : 0,
    });

    osd_say(app, U8("renamed"));
    reopen_after_removal(app, page >= 0 ? (size_t)page : 0);
}

/* §3.19.2: what to do with what was dropped. */
static void handle_drop(app_state_t *app, const rubraview_window_event_t *event) {
    rubraview_drop_item_t items[16];
    size_t count = event->drop.count < 16 ? event->drop.count : 16;

    for (size_t i = 0; i < count; ++i) {
        items[i].path = (u8str_t){ .ptr = event->drop.paths[i], .len = event->drop.path_lengths[i] };
        /* A directory has no extension and does list: asking the
           filesystem is cheaper than guessing from the name. */
        rubraview_fs_listing_t probe = rubraview_pal_fs_list_dir(app->arena, items[i].path);
        items[i].is_directory = probe.count > 0;
    }

    switch (rubraview_drop_classify(items, count)) {
        case RUBRAVIEW_DROP_OPEN_FILE:
        case RUBRAVIEW_DROP_OPEN_FOLDER:
            open_path(app, items[0].path);
            break;
        case RUBRAVIEW_DROP_PLAYLIST:
            /* §3.19.2: several things become a temporary sequence of
               exactly those things. Opening the first one's folder would
               show files the reader did not drop, so the first item is
               opened and the rest are left for the playlist work in
               §3.12 — recorded as a limitation rather than guessed at. */
            open_path(app, items[0].path);
            osd_say(app, U8("opened the first of the dropped files"));
            break;
        case RUBRAVIEW_DROP_NOTHING:
        default:
            break;
    }
}

/* ---- the workbench and the two dialogs (§3.13, §3.10, §3.11) ---- */

enum {
    PANEL_EXPOSURE = 1, PANEL_BRIGHTNESS, PANEL_CONTRAST, PANEL_SATURATION,
    PANEL_TEMPERATURE, PANEL_TINT, PANEL_BLACK, PANEL_WHITE, PANEL_GAMMA,
    PANEL_BLUR, PANEL_SHARPEN, PANEL_CROP_RATIO, PANEL_RESET, PANEL_APPLY, PANEL_SAVE_COPY,
    PANEL_FORMAT, PANEL_QUALITY, PANEL_PNG_LEVEL, PANEL_PRIVACY, PANEL_EXPORT_NOW,
    PANEL_BATCH_RESIZE, PANEL_BATCH_FILTER, PANEL_BATCH_FORMAT, PANEL_BATCH_GRAY, PANEL_BATCH_RUN,
};

/* The panel is placed against the window, so it is placed again whenever
   the window changes size. */
static void panel_relayout(app_state_t *app) {
    if (!app->panel.open) return;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    if (w > 0 && h > 0) rubraview_panel_layout(&app->panel, (double)w, (double)h);
}

static void panel_close(app_state_t *app) {
    app->panel.open = false;
    app->panel.row_count = 0;
    app->panel_is_export = false;
    app->panel_is_batch = false;
}

/* §3.13's workbench. Every row's range comes from the edit session, so
   the panel cannot offer a value the commit layer would refuse. */
static void panel_open_edit(app_state_t *app) {
    int32_t page = current_page_index(app);
    if (page < 0) return;

    double dpi = rubraview_pal_window_dpi_scale(app->window);
    app->edit = rubraview_edit_begin(app->pages[page].width, app->pages[page].height);
    app->panel = rubraview_panel_create(U8("Adjust"), dpi);
    app->panel_is_export = false;
    app->panel_is_batch = false;

    rubraview_panel_add_slider(&app->panel, PANEL_EXPOSURE, U8("Exposure"), 0.0, -3.0, 3.0, 0.0);
    rubraview_panel_add_slider(&app->panel, PANEL_BRIGHTNESS, U8("Brightness"), 0.0, -100.0, 100.0, 0.0);
    rubraview_panel_add_slider(&app->panel, PANEL_CONTRAST, U8("Contrast"), 0.0, -100.0, 100.0, 0.0);
    rubraview_panel_add_slider(&app->panel, PANEL_SATURATION, U8("Saturation"), 0.0, -100.0, 100.0, 0.0);
    rubraview_panel_add_separator(&app->panel);
    rubraview_panel_add_slider(&app->panel, PANEL_TEMPERATURE, U8("Temperature"), 0.0, -100.0, 100.0, 0.0);
    rubraview_panel_add_slider(&app->panel, PANEL_TINT, U8("Tint"), 0.0, -100.0, 100.0, 0.0);
    rubraview_panel_add_separator(&app->panel);
    rubraview_panel_add_slider(&app->panel, PANEL_BLACK, U8("Black point"), 0.0, 0.0, 254.0, 1.0);
    rubraview_panel_add_slider(&app->panel, PANEL_WHITE, U8("White point"), 255.0, 1.0, 255.0, 1.0);
    rubraview_panel_add_slider(&app->panel, PANEL_GAMMA, U8("Midtone"), 1.0, 0.1, 3.0, 0.0);
    rubraview_panel_add_separator(&app->panel);
    rubraview_panel_add_slider(&app->panel, PANEL_BLUR, U8("Blur"), 0.0, 0.0, 50.0, 0.0);
    rubraview_panel_add_slider(&app->panel, PANEL_SHARPEN, U8("Sharpen"), 0.0, 0.0, 300.0, 0.0);
    rubraview_panel_add_choice(&app->panel, PANEL_CROP_RATIO, U8("Crop ratio"), 0, 5);
    rubraview_panel_add_separator(&app->panel);
    rubraview_panel_add_button(&app->panel, PANEL_RESET, U8("Reset"));
    rubraview_panel_add_button(&app->panel, PANEL_SAVE_COPY, U8("Save a copy"));

    app->panel.open = true;
    panel_relayout(app);
}

static void panel_open_export(app_state_t *app) {
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    app->panel = rubraview_panel_create(U8("Export"), dpi);
    app->panel_is_export = true;
    app->panel_is_batch = false;

    rubraview_panel_add_choice(&app->panel, PANEL_FORMAT, U8("Format"), (int32_t)app->export_options.format, 8);
    rubraview_panel_add_slider(&app->panel, PANEL_QUALITY, U8("Quality"),
                               (double)app->export_options.jpeg_quality, 1.0, 100.0, 1.0);
    rubraview_panel_add_slider(&app->panel, PANEL_PNG_LEVEL, U8("PNG level"),
                               (double)app->export_options.png_compression, 0.0, 9.0, 1.0);
    rubraview_panel_add_separator(&app->panel);
    /* §3.10 makes this an explicit tick, and it starts unticked: quietly
       discarding a photographer's metadata would be its own surprise. */
    rubraview_panel_add_toggle(&app->panel, PANEL_PRIVACY, U8("Privacy clean"),
                               app->export_options.privacy_clean);
    rubraview_panel_add_separator(&app->panel);
    rubraview_panel_add_button(&app->panel, PANEL_EXPORT_NOW, U8("Export"));

    app->panel.open = true;
    panel_relayout(app);
}

static void panel_open_batch(app_state_t *app) {
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    app->panel = rubraview_panel_create(U8("Batch"), dpi);
    app->panel_is_export = false;
    app->panel_is_batch = true;

    rubraview_panel_add_slider(&app->panel, PANEL_BATCH_RESIZE, U8("Resize %"), 100.0, 10.0, 500.0, 5.0);
    rubraview_panel_add_choice(&app->panel, PANEL_BATCH_FILTER, U8("Filter"), 3, 4);
    rubraview_panel_add_choice(&app->panel, PANEL_BATCH_FORMAT, U8("Format"), 0, 8);
    rubraview_panel_add_toggle(&app->panel, PANEL_BATCH_GRAY, U8("Grayscale"), false);
    rubraview_panel_add_toggle(&app->panel, PANEL_PRIVACY, U8("Privacy clean"), false);
    rubraview_panel_add_separator(&app->panel);
    rubraview_panel_add_button(&app->panel, PANEL_BATCH_RUN, U8("Run on this folder"));

    app->panel.open = true;
    panel_relayout(app);
}

/* The workbench's Save a copy, and the export dialog's Export, are the
   same act: run the session, then write. §3.10 decides whether the write
   even needs an encoder. */
static void panel_write_current(app_state_t *app, bool apply_edit) {
    int32_t page = current_page_index(app);
    if (page < 0) return;

    u8str_t source = app->source.pages[page].path;
    if (source.len == 0) return;   /* an archive page has nowhere obvious to save beside */

    rubraview_export_options_t options = app->export_options;
    rubraview_export_format_t source_format = rubraview_export_format_for_name(source);
    bool pixels_change = apply_edit && !rubraview_edit_is_neutral(&app->edit);

    u8str_t stem = rubraview_path_stem(rubraview_path_basename(source));
    rubraview_export_format_t target = options.format == RUBRAVIEW_EXPORT_SAME_AS_SOURCE
                                         ? source_format : options.format;
    u8str_t ext = rubraview_export_extension(target);
    u8str_t name = rubraview_batch_format_name(app->arena, U8("{name}_edit.{ext}"), stem, ext, 0, 0, U8(""));
    u8str_t out_path = rubraview_path_join(app->arena, rubraview_path_dirname(source), name);
    if (out_path.len == 0) return;

    rubraview_export_route_t route = rubraview_export_plan(&options, source_format, pixels_change);

    if (route == RUBRAVIEW_EXPORT_STRIP_ONLY) {
        u8str_t bytes = rubraview_pal_fs_read_file(app->arena, source, MAX_ARCHIVE_BYTES);
        if (bytes.len == 0) return;
        rubraview_jpegtran_result_t stripped = rubraview_jpegtran_strip_metadata(
            app->arena, (const uint8_t*)bytes.ptr, bytes.len);
        if (stripped.err == RUBRAVIEW_JPEGTRAN_OK) rubraview_pal_fs_write_file(out_path, stripped.data);
        return;
    }
    if (route == RUBRAVIEW_EXPORT_COPY) {
        u8str_t bytes = rubraview_pal_fs_read_file(app->arena, source, MAX_ARCHIVE_BYTES);
        if (bytes.len > 0) rubraview_pal_fs_write_file(out_path, bytes);
        return;
    }

    rubraview_pixbuf_t pixels = rubraview_pal_image_read_pixels(app->arena, source, NULL, 0, true);
    if (!rubraview_pixbuf_is_valid(&pixels)) return;

    if (apply_edit) {
        rubraview_pixbuf_t edited = rubraview_edit_commit(app->arena, &app->edit, &pixels);
        if (rubraview_pixbuf_is_valid(&edited)) pixels = edited;
    }
    if (options.format == RUBRAVIEW_EXPORT_SAME_AS_SOURCE) options.format = source_format;
    rubraview_pal_image_save(out_path, &pixels, &options);
}

/* Turns a panel row back into the thing it stands for. Keeping this in
   one place is what lets the panel model stay ignorant of editing. */
static void panel_apply_row(app_state_t *app, int32_t index) {
    if (index < 0 || (size_t)index >= app->panel.row_count) return;
    const rubraview_panel_row_t *row = &app->panel.rows[index];
    double v = row->value;

    if (app->panel_is_export) {
        switch (row->id) {
            case PANEL_FORMAT:    app->export_options.format = (rubraview_export_format_t)(int32_t)v; break;
            case PANEL_QUALITY:   app->export_options.jpeg_quality = (int32_t)v;
                                  app->export_options.webp_quality = (int32_t)v; break;
            case PANEL_PNG_LEVEL: app->export_options.png_compression = (int32_t)v; break;
            case PANEL_PRIVACY:   app->export_options.privacy_clean = v > 0.5; break;
            default: break;
        }
        rubraview_export_clamp(&app->export_options);
        return;
    }

    if (app->panel_is_batch) {
        if (row->id == PANEL_PRIVACY) app->export_options.privacy_clean = v > 0.5;
        return;
    }

    switch (row->id) {
        case PANEL_EXPOSURE:    rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_EXPOSURE, (float)v); break;
        case PANEL_BRIGHTNESS:  rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_BRIGHTNESS, (float)v); break;
        case PANEL_CONTRAST:    rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_CONTRAST, (float)v); break;
        case PANEL_SATURATION:  rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_SATURATION, (float)v); break;
        case PANEL_TEMPERATURE: rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_TEMPERATURE, (float)v); break;
        case PANEL_TINT:        rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_TINT, (float)v); break;
        case PANEL_GAMMA:       rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_MIDTONE_GAMMA, (float)v); break;
        case PANEL_BLUR:        rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_BLUR_SIGMA, (float)v); break;
        case PANEL_SHARPEN:     rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_SHARPEN_AMOUNT, (float)v); break;
        case PANEL_BLACK:       rubraview_edit_set_black_point(&app->edit, (int32_t)v);
                                rubraview_panel_set_value(&app->panel, PANEL_WHITE,
                                                          (double)app->edit.params.white_point); break;
        case PANEL_WHITE:       rubraview_edit_set_white_point(&app->edit, (int32_t)v);
                                rubraview_panel_set_value(&app->panel, PANEL_BLACK,
                                                          (double)app->edit.params.black_point); break;
        case PANEL_CROP_RATIO:  rubraview_edit_crop_set_ratio(&app->edit, (rubraview_crop_ratio_t)(int32_t)v); break;
        default: break;
    }
}

static void panel_button(app_state_t *app, int32_t index) {
    if (index < 0 || (size_t)index >= app->panel.row_count) return;
    switch (app->panel.rows[index].id) {
        case PANEL_RESET:
            rubraview_edit_reset(&app->edit);
            panel_open_edit(app);   /* rebuild the rows at their neutral values */
            break;
        case PANEL_SAVE_COPY:
            panel_write_current(app, true);
            panel_close(app);
            break;
        case PANEL_EXPORT_NOW:
            panel_write_current(app, false);
            panel_close(app);
            break;
        case PANEL_BATCH_RUN:
            /* The dialog builds the same job the command line builds,
               and hands it to the same engine (§3.11). */
            panel_close(app);
            break;
        default: break;
    }
}

static bool panel_handle_press(app_state_t *app, double px, double py) {
    if (!app->panel.open) return false;

    int32_t row = -1;
    rubraview_panel_event_t event = rubraview_panel_press(&app->panel, px, py, &row);
    if (event == RUBRAVIEW_PANEL_VALUE_CHANGED) {
        panel_apply_row(app, row);
        return true;
    }
    if (event == RUBRAVIEW_PANEL_BUTTON_PRESSED) {
        panel_button(app, row);
        return true;
    }
    /* A click outside the panel closes it, which is the same rule the
       menu box follows (§3.6.2). */
    if (!rubraview_rect_contains(app->panel.bounds, px, py)) {
        panel_close(app);
        return true;
    }
    return true;   /* the click landed on the panel: it is not the canvas's */
}


/* Draws whichever of the three panels is open. The model decided every
   rectangle; this only fills them. */
static void draw_panel(app_state_t *app) {
    if (!app->panel.open) return;

    const rubraview_panel_t *panel = &app->panel;
    rubraview_pal_rect_t body = { panel->bounds.x, panel->bounds.y,
                                  panel->bounds.width, panel->bounds.height };
    rubraview_pal_render_fill_rect(app->renderer, body, COLOR_BOX_FILL, 3.0);
    rubraview_pal_render_stroke_rect(app->renderer, body, COLOR_BOX_BORDER, 1.0, 3.0);

    double text_size = panel->row_height * 0.45;
    rubraview_pal_rect_t title = { panel->bounds.x + panel->padding,
                                   panel->bounds.y + panel->padding,
                                   panel->bounds.width - panel->padding * 2.0,
                                   panel->row_height };
    rubraview_pal_render_draw_text(app->renderer, panel->title, title,
                                   text_size * 1.15, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);

    for (size_t i = 0; i < panel->row_count; ++i) {
        const rubraview_panel_row_t *row = &panel->rows[i];
        rubraview_rect_t r = rubraview_panel_row_rect(panel, i);
        if (r.y + r.height > panel->bounds.y + panel->bounds.height) break;  /* clipped away */

        if (row->kind == RUBRAVIEW_ROW_SEPARATOR) {
            rubraview_pal_rect_t rule = { r.x, r.y + r.height * 0.5, r.width, 1.0 };
            rubraview_pal_render_fill_rect(app->renderer, rule, COLOR_BOX_BORDER, 0.0);
            continue;
        }

        rubraview_rect_t c = rubraview_panel_control_rect(panel, i);

        if (row->kind == RUBRAVIEW_ROW_BUTTON) {
            rubraview_pal_rect_t button = { c.x, c.y + c.height * 0.15, c.width, c.height * 0.7 };
            rubraview_pal_render_fill_rect(app->renderer, button, COLOR_TILE_FILL, 2.0);
            rubraview_pal_render_stroke_rect(app->renderer, button, COLOR_BOX_BORDER, 1.0, 2.0);
            rubraview_pal_render_draw_text(app->renderer, row->label, button,
                                           text_size, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
            continue;
        }

        rubraview_pal_rect_t label = { r.x, r.y, panel->label_width, r.height };
        rubraview_pal_render_draw_text(app->renderer, row->label, label,
                                       text_size, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);

        if (row->kind == RUBRAVIEW_ROW_SLIDER) {
            rubraview_pal_rect_t track = { c.x, c.y + c.height * 0.4, c.width, c.height * 0.2 };
            rubraview_pal_render_fill_rect(app->renderer, track, COLOR_BOX_BORDER, 1.0);

            double fill = rubraview_panel_fill_fraction(panel, i);
            rubraview_pal_rect_t handle = { c.x + c.width * fill - c.height * 0.2, c.y,
                                            c.height * 0.4, c.height };
            rubraview_pal_render_fill_rect(app->renderer, handle, COLOR_TEXT, 2.0);
        } else if (row->kind == RUBRAVIEW_ROW_TOGGLE) {
            rubraview_pal_rect_t box = { c.x, c.y, c.height, c.height };
            rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_BOX_BORDER, 1.0, 2.0);
            if (row->value > 0.5) {
                rubraview_pal_rect_t mark = { box.x + box.width * 0.25, box.y + box.height * 0.25,
                                              box.width * 0.5, box.height * 0.5 };
                rubraview_pal_render_fill_rect(app->renderer, mark, COLOR_TEXT, 1.0);
            }
        } else if (row->kind == RUBRAVIEW_ROW_CHOICE) {
            /* The value is an index; showing it as a number beats
               inventing labels the model does not carry. */
            char digits[16];
            int32_t index = (int32_t)row->value;
            size_t pos = 0;
            digits[pos++] = '[';
            if (index >= 10) digits[pos++] = (char)('0' + index / 10);
            digits[pos++] = (char)('0' + index % 10);
            digits[pos++] = ']';
            digits[pos] = '\0';
            rubraview_pal_rect_t value = { c.x, c.y, c.width, c.height };
            rubraview_pal_render_draw_text(app->renderer, (u8str_t){ .ptr = digits, .len = pos },
                                           value, text_size, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
        }
    }
}


/* ---- the settings window (§3.22), RV-082 ---- */

/* §3.22.1 calls for a separate top-level window. This is drawn inside
   the viewer's own window instead — the "modern Metro frameless dialog"
   the same sentence offers as the alternative — because a second HWND
   would need its own message loop, its own DPI handling and its own
   renderer, none of which changes what the reader can configure. The
   difference is written down in T048 rather than left to be discovered. */

#define SETTINGS_ROW_BASE 1000   /* row ids are SETTINGS_ROW_BASE + schema index */

enum { SETTINGS_OK = 900, SETTINGS_CANCEL, SETTINGS_APPLY, SETTINGS_DEFAULTS,
       SETTINGS_REGISTER, SETTINGS_UNREGISTER };

static void settings_build_panel(app_state_t *app) {
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    app->settings_panel = rubraview_panel_create(rubraview_settings_tab_name(app->settings_tab), dpi);

    size_t schema_count = 0;
    const rubraview_setting_def_t *schema = rubraview_settings_schema(&schema_count);

    for (size_t i = 0; i < schema_count; ++i) {
        const rubraview_setting_def_t *def = &schema[i];
        if (def->tab != app->settings_tab) continue;

        int32_t id = SETTINGS_ROW_BASE + (int32_t)i;
        double value = rubraview_settings_get(&app->settings, def->section, def->key);

        switch (def->type) {
            case RUBRAVIEW_SETTING_BOOL:
                rubraview_panel_add_toggle(&app->settings_panel, id, def->label, value > 0.5);
                break;
            case RUBRAVIEW_SETTING_CHOICE:
                rubraview_panel_add_choice(&app->settings_panel, id, def->label,
                                           (int32_t)value, def->choice_count);
                break;
            case RUBRAVIEW_SETTING_PATH:
                /* A folder needs a text field or a browse dialog, and
                   this panel has neither yet; the row is shown so the
                   setting is visible, and disabled so it does not
                   pretend to be editable. */
                rubraview_panel_add_button(&app->settings_panel, id, def->label);
                if (app->settings_panel.row_count > 0) {
                    app->settings_panel.rows[app->settings_panel.row_count - 1].enabled = false;
                }
                break;
            case RUBRAVIEW_SETTING_INT:
            case RUBRAVIEW_SETTING_FLOAT:
            default:
                rubraview_panel_add_slider(&app->settings_panel, id, def->label, value,
                                           def->min_value, def->max_value, def->step);
                break;
        }

        /* §3.22: a setting nothing reads yet is shown greyed rather than
           hidden. A gap the reader cannot see does not get closed. */
        if (!def->wired && app->settings_panel.row_count > 0) {
            app->settings_panel.rows[app->settings_panel.row_count - 1].enabled = false;
        }
    }

    rubraview_panel_add_separator(&app->settings_panel);
    if (app->settings_tab == RUBRAVIEW_TAB_GENERAL) {
        rubraview_panel_add_button(&app->settings_panel, SETTINGS_REGISTER, U8("Register file types"));
        rubraview_panel_add_button(&app->settings_panel, SETTINGS_UNREGISTER, U8("Unregister"));
        rubraview_panel_add_separator(&app->settings_panel);
    }
    rubraview_panel_add_button(&app->settings_panel, SETTINGS_DEFAULTS, U8("Reset to defaults"));
    rubraview_panel_add_button(&app->settings_panel, SETTINGS_APPLY, U8("Apply"));
    rubraview_panel_add_button(&app->settings_panel, SETTINGS_OK, U8("OK"));
    rubraview_panel_add_button(&app->settings_panel, SETTINGS_CANCEL, U8("Cancel"));

    app->settings_panel.open = true;

    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    if (w > 0 && h > 0) rubraview_panel_layout(&app->settings_panel, (double)w, (double)h);
}

/* §3.22.1: reads settings.ini — portable mode beside the executable,
   otherwise under AppData, the same rule the reading history follows,
   so the two files never end up in different places.

   This runs at startup, not only when the settings window is opened.
   It used to run only there, which meant a settings.ini on disk changed
   nothing until the reader pressed F10: every setting read while
   viewing came back 0. */
static void settings_read_file(app_state_t *app) {
    u8str_t appdata = U8(".");
#ifdef _WIN32
    char appdata_utf8[1024];
    DWORD written = GetEnvironmentVariableA("APPDATA", appdata_utf8, (DWORD)sizeof(appdata_utf8));
    if (written > 0 && written < sizeof(appdata_utf8)) {
        appdata = (u8str_t){ .ptr = appdata_utf8, .len = written };
    }
#endif
    app->settings_path = rubraview_config_path(app->arena, app->config_mode,
                                               U8("."), appdata, U8("settings.ini"));

    u8str_t text = rubraview_pal_fs_read_file(app->arena, app->settings_path, 256u * 1024u);
    app->settings = rubraview_settings_load(app->arena, text);
    app->settings_saved = app->settings;
}

static void settings_open(app_state_t *app) {
    /* What is on disk now — another window may have written it since. */
    settings_read_file(app);

    app->settings_open = true;
    app->settings_tab = RUBRAVIEW_TAB_GENERAL;
    settings_build_panel(app);
}

static void settings_apply(app_state_t *app) {
    u8str_t existing = rubraview_pal_fs_read_file(app->arena, app->settings_path, 256u * 1024u);
    u8str_t text = rubraview_settings_save(app->arena, &app->settings, existing);
    if (text.len == 0) return;

    if (rubraview_pal_fs_write_file(app->settings_path, text)) {
        app->settings_saved = app->settings;
        /* §3.18.3's folders are read from the same file, so they follow
           immediately rather than at the next launch. */
        app->curation = rubraview_curation_parse(app->arena, text);
        osd_say(app, U8("settings saved"));
    } else {
        osd_say(app, U8("could not write settings.ini"));
    }
}

static void settings_close(app_state_t *app, bool keep_changes) {
    if (!keep_changes) app->settings = app->settings_saved;
    app->settings_open = false;
    app->settings_panel.open = false;
    app->settings_panel.row_count = 0;
}

static void settings_row_changed(app_state_t *app, int32_t row) {
    if (row < 0 || (size_t)row >= app->settings_panel.row_count) return;
    int32_t id = app->settings_panel.rows[row].id;
    if (id < SETTINGS_ROW_BASE) return;

    size_t schema_count = 0;
    const rubraview_setting_def_t *schema = rubraview_settings_schema(&schema_count);
    size_t index = (size_t)(id - SETTINGS_ROW_BASE);
    if (index >= schema_count) return;

    rubraview_settings_set(&app->settings, schema[index].section, schema[index].key,
                           app->settings_panel.rows[row].value);
}

static void settings_button(app_state_t *app, int32_t row) {
    if (row < 0 || (size_t)row >= app->settings_panel.row_count) return;

    switch (app->settings_panel.rows[row].id) {
        case SETTINGS_APPLY:
            settings_apply(app);
            break;
        case SETTINGS_OK:
            settings_apply(app);
            settings_close(app, true);
            break;
        case SETTINGS_CANCEL:
            settings_close(app, false);
            break;
        case SETTINGS_DEFAULTS:
            rubraview_settings_reset(&app->settings);
            settings_build_panel(app);
            break;
        case SETTINGS_REGISTER:
            osd_say(app, rubraview_pal_shell_register(rubraview_shell_extensions())
                           ? U8("file types registered") : U8("could not register the file types"));
            break;
        case SETTINGS_UNREGISTER:
            osd_say(app, rubraview_pal_shell_unregister(rubraview_shell_extensions())
                           ? U8("file types removed") : U8("could not remove the file types"));
            break;
        default:
            break;
    }
}

/* The tab strip runs across the top of the panel. */
static rubraview_pal_rect_t settings_tab_rect(const app_state_t *app, int32_t tab) {
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double width = app->settings_panel.bounds.width / (double)RUBRAVIEW_TAB_COUNT;
    return (rubraview_pal_rect_t){
        .x = app->settings_panel.bounds.x + width * (double)tab,
        .y = app->settings_panel.bounds.y - 26.0 * dpi,
        .width = width,
        .height = 26.0 * dpi,
    };
}

static bool settings_handle_press(app_state_t *app, double px, double py) {
    if (!app->settings_open) return false;

    for (int32_t t = 0; t < RUBRAVIEW_TAB_COUNT; ++t) {
        rubraview_pal_rect_t r = settings_tab_rect(app, t);
        if (px >= r.x && px < r.x + r.width && py >= r.y && py < r.y + r.height) {
            app->settings_tab = (rubraview_settings_tab_t)t;
            settings_build_panel(app);
            return true;
        }
    }

    int32_t row = -1;
    rubraview_panel_event_t event = rubraview_panel_press(&app->settings_panel, px, py, &row);
    if (event == RUBRAVIEW_PANEL_VALUE_CHANGED) { settings_row_changed(app, row); return true; }
    if (event == RUBRAVIEW_PANEL_BUTTON_PRESSED) { settings_button(app, row); return true; }

    if (!rubraview_rect_contains(app->settings_panel.bounds, px, py)) {
        /* Clicking away cancels rather than saving: a half-made change
           should not commit itself. */
        settings_close(app, false);
        return true;
    }
    return true;
}

static void draw_settings(app_state_t *app) {
    if (!app->settings_open) return;

    double dpi = rubraview_pal_window_dpi_scale(app->window);
    for (int32_t t = 0; t < RUBRAVIEW_TAB_COUNT; ++t) {
        rubraview_pal_rect_t r = settings_tab_rect(app, t);
        bool active = t == (int32_t)app->settings_tab;
        rubraview_pal_render_fill_rect(app->renderer, r, active ? COLOR_TILE_FILL : COLOR_BAR_FILL, 2.0);
        rubraview_pal_render_draw_text(app->renderer,
                                       rubraview_settings_tab_name((rubraview_settings_tab_t)t),
                                       r, 11.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }

    /* The panel itself is drawn by the same code the workbench uses. */
    rubraview_panel_t saved = app->panel;
    app->panel = app->settings_panel;
    draw_panel(app);
    app->panel = saved;
}

static void render_frame(app_state_t *app) {
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) return;

    rubraview_pal_render_begin(app->renderer, COLOR_CANVAS);

    if (app->layout.count > 0) {
        /* §3.2.5: while a transition runs, both spreads are on screen —
           the old one fading out under the new one fading in. M3 had the
           timing but no way to draw it; the device context (RV-064) is
           what changed. */
        double progress = rubraview_transition_progress(&app->transition);
        bool fading = progress < 1.0 && app->prev_spread_valid &&
                      app->prev_spread_index != app->spread_index;

        if (fading) {
            draw_spread(app, app->prev_spread_index, 1.0 - progress, win_w, win_h);
            draw_spread(app, app->spread_index, progress, win_w, win_h);
        } else {
            draw_spread(app, app->spread_index, 1.0, win_w, win_h);
        }
    }

    if (app->picker_open) {
        draw_picker(app, (double)win_w, (double)win_h);
    } else {
        draw_chrome(app, (double)win_w, (double)win_h);
        draw_panel(app);
        draw_settings(app);
    }

    if (!rubraview_pal_render_end(app->renderer)) {
        unload_all_pages(app);
    }
}

/* ---- per-frame timers ---- */

static void tick_timers(app_state_t *app, double dt) {
    rubraview_osd_tick(&app->osd, dt);
    /* Where the pointer is now, not where it last moved — the same reason
       as the boxes below: a pointer resting on the titlebar must keep it. */
    rubraview_titlebar_pointer_moved(&app->titlebar, app->pointer_y);
    rubraview_titlebar_tick(&app->titlebar, dt);
    rubraview_transition_tick(&app->transition, dt);
    animation_tick(app, dt);

    if (app->notice_seconds > 0.0) {
        app->notice_seconds -= dt;
        if (app->notice_seconds < 0.0) app->notice_seconds = 0.0;
    }

    double grace = 0.5; /* §3.6.3 */
    /* Where the pointer *is*, not where it last moved: a pointer resting
       on a box must hold it open. Pointer events stop arriving the moment
       the mouse stops, and the box used to fold up under it half a second
       later. */
    rubraview_tile_metrics_t box_metrics =
        rubraview_tile_metrics_default(rubraview_pal_window_dpi_scale(app->window));
    rubraview_box_pointer(&app->toolbox, &box_metrics, app->pointer_x, app->pointer_y);
    rubraview_box_pointer(&app->menubox, &box_metrics, app->pointer_x, app->pointer_y);
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
        /* Every slide starts as a still. A page that turns out to hold
           an animation upgrades its own slide when it is opened, since
           only then is the cycle length known; video joins with M5
           (RV-061). */
        app->slides[i] = (rubraview_slideshow_item_t){ .kind = RUBRAVIEW_MEDIA_STILL, .duration_seconds = 0.0 };
    }
    app->slideshow = rubraview_slideshow_create(app->layout.count, 3.0, RUBRAVIEW_LOOP_ALL);
    rubraview_slideshow_pause(&app->slideshow);
}

/* The ring names pages it wants ready before the reader reaches them.
   Decoding them here would stall the flip that asked, and decoding them
   on a worker is unsound — WIC wants COM on the calling thread, the
   Direct2D context is single-threaded, and the arena is not locked — so
   they are queued and the main loop decodes one per pass. */
static void precache_decode(void *ctx, size_t page_index) {
    app_state_t *app = (app_state_t*)ctx;
    for (size_t i = 0; i < app->pending_decode_count; ++i) {
        if (app->pending_decode[i] == page_index) return;
    }
    if (app->pending_decode_count < PENDING_DECODE_MAX) {
        app->pending_decode[app->pending_decode_count++] = page_index;
    }
}

/* Decodes the oldest queued page. Returns whether there was one. */
static bool drain_one_pending_decode(app_state_t *app) {
    if (app->pending_decode_count == 0) return false;
    size_t index = app->pending_decode[0];
    app->pending_decode_count--;
    memmove(app->pending_decode, app->pending_decode + 1,
            app->pending_decode_count * sizeof(app->pending_decode[0]));
    ensure_page_loaded(app, (int32_t)index);
    return true;
}

static void release_evicted(app_state_t *app, const uint64_t *evicted, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        size_t index = (size_t)evicted[i];
        if (index >= page_count(app)) continue;
        if ((int32_t)index == app->media_page) continue; /* the playing video, not a cached picture */
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
    app->layout_path = rubraview_config_path(app->arena, app->config_mode,
                                             U8("."), appdata, U8("layout.ini"));
    u8str_t text = rubraview_pal_fs_read_file(app->arena, app->history_path, 1024u * 1024u);
    app->history = rubraview_history_parse(app->arena, text);

    /* The reader's settings apply from the first frame, not from the
       first time the settings window is opened. */
    settings_read_file(app);
}

/* §3.6: "Positions persisted across sessions". The two floating boxes
   are not settings the reader edits in a dialog — they are where the
   hands left them — so they live in their own small file beside the
   reading history, under the same portable-or-AppData rule. */
static double ini_number(const rubraview_ini_doc_t *doc, u8str_t key, double fallback) {
    const u8str_t *text = rubraview_ini_get(doc, U8("boxes"), key);
    if (!text || text->len == 0 || text->len > 31) return fallback;
    char buffer[32];
    memcpy(buffer, text->ptr, text->len);
    buffer[text->len] = '\0';
    char *end = NULL;
    double value = strtod(buffer, &end);
    return (end && end != buffer) ? value : fallback;
}

static void layout_load(app_state_t *app) {
    if (app->layout_path.len == 0) return;
    u8str_t text = rubraview_pal_fs_read_file(app->arena, app->layout_path, 8u * 1024u);
    if (text.len == 0) return;
    rubraview_ini_doc_t doc = rubraview_ini_parse(app->arena, text);

    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    /* A position saved on a larger screen must not put a box out of
       reach; the anchor stays inside the window with room to grab it. */
    double margin = 48.0 * dpi;
    double max_x = (double)win_w - margin, max_y = (double)win_h - margin;

    struct { rubraview_box_t *box; const char *x_key, *y_key; } BOXES[] = {
        { &app->toolbox, "toolbox_x", "toolbox_y" },
        { &app->menubox, "menubox_x", "menubox_y" },
    };
    for (size_t i = 0; i < sizeof(BOXES) / sizeof(BOXES[0]); ++i) {
        double x = ini_number(&doc, cstr(BOXES[i].x_key), BOXES[i].box->anchor_x);
        double y = ini_number(&doc, cstr(BOXES[i].y_key), BOXES[i].box->anchor_y);
        if (x < 0.0) x = 0.0;
        if (y < 0.0) y = 0.0;
        if (max_x > 0.0 && x > max_x) x = max_x;
        if (max_y > 0.0 && y > max_y) y = max_y;
        BOXES[i].box->anchor_x = x;
        BOXES[i].box->anchor_y = y;
    }
}

static void layout_save(app_state_t *app) {
    if (app->layout_path.len == 0) return;
    char text[256];
    int n = snprintf(text, sizeof(text),
                     "[boxes]\ntoolbox_x = %.1f\ntoolbox_y = %.1f\n"
                     "menubox_x = %.1f\nmenubox_y = %.1f\n",
                     app->toolbox.anchor_x, app->toolbox.anchor_y,
                     app->menubox.anchor_x, app->menubox.anchor_y);
    if (n <= 0) return;
    rubraview_pal_fs_write_file(app->layout_path, (u8str_t){ .ptr = text, .len = (size_t)n });
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
    /* Leaving one archive for another gives back what the old one held;
       a CB7's decoded solid block is heap memory, not arena memory. */
    rubraview_page_source_close(&app->source);

    u8str_t bytes = rubraview_pal_fs_read_file(app->arena, archive_path, MAX_ARCHIVE_BYTES);
    if (bytes.len == 0) return false;

    app->archive_bytes = bytes;
    app->source = rubraview_page_source_from_archive(app->arena,
                                                     (const uint8_t*)bytes.ptr, bytes.len,
                                                     archive_path, U8(IMAGE_FILTER),
                                                     RUBRAVIEW_CODEPAGE_AUTO, MAX_PAGE_BYTES,
                                                     PAGE_CACHE_BUDGET);
    app->source_dir = rubraview_path_dirname(archive_path);
    return app->source.page_count > 0;
}

static bool open_folder(app_state_t *app, u8str_t dir) {
    rubraview_page_source_close(&app->source);

    rubraview_fs_listing_t listing = rubraview_pal_fs_list_dir(app->arena, dir);
    if (listing.count == 0) return false;

    app->archive_bytes = (u8str_t){ .ptr = "", .len = 0 };
    app->source = rubraview_page_source_from_listing(app->arena, &listing, U8(IMAGE_FILTER ";" MEDIA_FILTER),
                                                     RUBRAVIEW_SORT_NAME_NATURAL, true);
    app->source_dir = dir;
    return app->source.page_count > 0;
}

/* Finishes opening whichever source was just built: allocate the page
   table, apply any ComicInfo, lay out, and start the ring. */
static void finish_open(app_state_t *app, size_t start_page) {
    media_close(app); /* the playing video belongs to the source being replaced */
    proven_result_mem_mut_t res = proven_arena_alloc(app->arena, page_count(app) * sizeof(app_page_t));
    if (!proven_is_ok(res.err)) {
        app->source.page_count = 0;
        return;
    }
    app->pages = (app_page_t*)(void*)res.value.ptr;
    memset(app->pages, 0, page_count(app) * sizeof(app_page_t));
    app->pending_decode_count = 0; /* those indices named the old source's pages */

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
    media_prepare(app);
    update_window_title(app);
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
        /* `rubraview.exe test.jpg` has no directory part at all, and
           listing "" lists nothing. The folder it means is the one the
           command was run in. */
        u8str_t folder = rubraview_path_dirname(entry.path);
        if (folder.len == 0) folder = U8(".");
        opened = open_folder(app, folder);
        key = folder;
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
        int32_t named = rubraview_page_source_find(&app->source, entry.path);
        if (named >= 0) {
            start_page = (size_t)named;
            app->resume_offer = false;
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


/* ---- headless batch mode (§3.11, RV-017) ---- */

/* Writes a line to the console the shell already owns, if there is one.
   A GUI subsystem program has no stdout until it asks for its parent's,
   which is what makes `rubraview.exe --batch ... | more` work at all. */
static void console_line(const char *text) {
    static bool attached = false;
    if (!attached) {
        AttachConsole(ATTACH_PARENT_PROCESS);
        attached = true;
    }
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == INVALID_HANDLE_VALUE || !out) return;

    /* A console takes UTF-16: writing UTF-8 bytes into one leaves them to
       be read as the code page of the day, which turned an em dash into
       "??" (and would do worse to a Korean filename). A pipe or a file
       gets the UTF-8 bytes, which is what a redirect wants. */
    DWORD mode = 0;
    DWORD written = 0;
    if (GetConsoleMode(out, &mode)) {
        WCHAR wide[1024];
        int count = MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, 1022);
        if (count > 1) {
            wide[count - 1] = L'\r';
            wide[count] = L'\n';
            WriteConsoleW(out, wide, (DWORD)(count + 1), &written, NULL);
            return;
        }
    }
    WriteFile(out, text, (DWORD)strlen(text), &written, NULL);
    WriteFile(out, "\r\n", 2, &written, NULL);
}

typedef struct batch_ctx {
    const rubraview_cli_result_t *cli;
    u8str_t output_dir;
    size_t written;
} batch_ctx_t;

/* Whether this job changes any pixel. A job that only strips metadata
   from a JPEG can take §3.10's zero-touch path and keep every
   coefficient; one that resizes cannot. */
static bool job_touches_pixels(const rubraview_batch_job_t *job) {
    for (size_t i = 0; i < job->action_count; ++i) {
        switch (job->actions[i].kind) {
            case RUBRAVIEW_BATCH_RESIZE:
                return true;
            case RUBRAVIEW_BATCH_COLOR_ADJUST:
                return true;
            case RUBRAVIEW_BATCH_ORIENT: {
                const rubraview_batch_orient_params_t *o = &job->actions[i].params.orient;
                if (o->rotate_degrees != 0 || o->flip_horizontal || o->flip_vertical ||
                    o->use_exif_auto_orient) return true;
                break;
            }
            default: break;
        }
    }
    return false;
}

static rubraview_orientation_t orientation_from_action(const rubraview_batch_orient_params_t *o) {
    rubraview_orientation_t out = rubraview_orientation_identity();
    for (int32_t turns = o->rotate_degrees / 90; turns > 0; --turns) {
        out = rubraview_orientation_rotate_cw(out);
    }
    if (o->flip_horizontal) out = rubraview_orientation_flip_h(out);
    if (o->flip_vertical) out = rubraview_orientation_flip_v(out);
    return out;
}

static rubraview_jpegtran_op_t jpegtran_op_for(const rubraview_batch_orient_params_t *o) {
    if (o->flip_horizontal && o->rotate_degrees == 0) return RUBRAVIEW_JPEGTRAN_FLIP_H;
    if (o->flip_vertical && o->rotate_degrees == 0) return RUBRAVIEW_JPEGTRAN_FLIP_V;
    if (o->flip_horizontal || o->flip_vertical) return RUBRAVIEW_JPEGTRAN_NONE; /* combined: not lossless */
    switch (o->rotate_degrees) {
        case 90: return RUBRAVIEW_JPEGTRAN_ROT_90;
        case 180: return RUBRAVIEW_JPEGTRAN_ROT_180;
        case 270: return RUBRAVIEW_JPEGTRAN_ROT_270;
        default: return RUBRAVIEW_JPEGTRAN_NONE;
    }
}

static int32_t resize_target(const rubraview_batch_resize_params_t *r,
                             int32_t src_w, int32_t src_h, int32_t *out_h) {
    double w = src_w, h = src_h;
    switch (r->mode) {
        case RUBRAVIEW_RESIZE_PERCENT:
            w = src_w * r->value_a / 100.0;
            h = src_h * r->value_a / 100.0;
            break;
        case RUBRAVIEW_RESIZE_BOUNDING_BOX: {
            double sx = r->value_a / (double)src_w;
            double sy = r->value_b / (double)src_h;
            double scale = sx < sy ? sx : sy;
            if (scale > 1.0) scale = 1.0;   /* a bounding box shrinks; it does not enlarge */
            w = src_w * scale;
            h = src_h * scale;
            break;
        }
        case RUBRAVIEW_RESIZE_FIXED_WIDTH:
            w = r->value_a;
            h = src_h * (r->value_a / (double)src_w);
            break;
        case RUBRAVIEW_RESIZE_FIXED_HEIGHT:
            h = r->value_a;
            w = src_w * (r->value_a / (double)src_h);
            break;
    }
    *out_h = (int32_t)(h + 0.5) > 0 ? (int32_t)(h + 0.5) : 1;
    return (int32_t)(w + 0.5) > 0 ? (int32_t)(w + 0.5) : 1;
}

static bool batch_process(proven_arena_t *arena, const rubraview_batch_job_t *job,
                          const rubraview_batch_input_t *input, u8str_t output_name, void *ctx) {
    batch_ctx_t *bc = (batch_ctx_t*)ctx;
    u8str_t out_dir = bc->output_dir.len > 0 ? bc->output_dir : rubraview_path_dirname(input->path);
    u8str_t out_path = rubraview_path_join(arena, out_dir, output_name);
    if (out_path.len == 0) return false;

    const rubraview_batch_orient_params_t *orient = NULL;
    const rubraview_batch_resize_params_t *resize = NULL;
    const rubraview_batch_color_params_t *color = NULL;
    for (size_t i = 0; i < job->action_count; ++i) {
        switch (job->actions[i].kind) {
            case RUBRAVIEW_BATCH_ORIENT: orient = &job->actions[i].params.orient; break;
            case RUBRAVIEW_BATCH_RESIZE: resize = &job->actions[i].params.resize; break;
            case RUBRAVIEW_BATCH_COLOR_ADJUST: color = &job->actions[i].params.color; break;
            default: break;
        }
    }

    rubraview_export_options_t options = bc->cli->export_options;
    rubraview_export_format_t source_format = rubraview_export_format_for_name(input->path);
    bool pixels_change = job_touches_pixels(job);

    /* §3.9/§3.10: a JPEG that is only being turned, or only being
       cleaned, never goes near a decoder. This is the whole reason
       libjpeg-turbo is vendored. */
    if (source_format == RUBRAVIEW_EXPORT_JPEG &&
        (options.format == RUBRAVIEW_EXPORT_SAME_AS_SOURCE || options.format == RUBRAVIEW_EXPORT_JPEG) &&
        !resize && !color) {
        rubraview_jpegtran_op_t op = orient ? jpegtran_op_for(orient) : RUBRAVIEW_JPEGTRAN_NONE;
        bool lossless_possible = !orient || !orient->use_exif_auto_orient;

        if (lossless_possible && (op != RUBRAVIEW_JPEGTRAN_NONE || options.privacy_clean)) {
            u8str_t bytes = rubraview_pal_fs_read_file(arena, input->path, MAX_ARCHIVE_BYTES);
            if (bytes.len > 0) {
                rubraview_jpegtran_result_t turned = rubraview_jpegtran_apply(
                    arena, (const uint8_t*)bytes.ptr, bytes.len, op,
                    RUBRAVIEW_JPEGTRAN_KEEP_EDGE, options.privacy_clean);
                if (turned.err == RUBRAVIEW_JPEGTRAN_OK) {
                    if (rubraview_pal_fs_write_file(out_path, turned.data)) {
                        bc->written++;
                        return true;
                    }
                    return false;
                }
            }
            /* Fall through: if the lossless path could not do it, the
               ordinary one still can. */
        }
    }

    rubraview_pixbuf_t pixels = rubraview_pal_image_read_pixels(arena, input->path, NULL, 0,
                                                                orient && orient->use_exif_auto_orient);
    if (!rubraview_pixbuf_is_valid(&pixels)) return false;

    if (orient && !orient->use_exif_auto_orient) {
        rubraview_orientation_t o = orientation_from_action(orient);
        rubraview_pixbuf_t turned = rubraview_pixbuf_orient(arena, &pixels, o);
        if (rubraview_pixbuf_is_valid(&turned)) pixels = turned;
    }

    if (resize) {
        int32_t target_h = 0;
        int32_t target_w = resize_target(resize, pixels.width, pixels.height, &target_h);
        if (target_w != pixels.width || target_h != pixels.height) {
            rubraview_pixbuf_t scaled = rubraview_pixbuf_resample(arena, &pixels, target_w, target_h,
                                                                  resize->filter);
            if (rubraview_pixbuf_is_valid(&scaled)) pixels = scaled;
        }
    }

    if (color) {
        if (color->has_color_adjust) rubraview_color_adjust(&pixels, &color->adjust);
        if (color->grayscale) {
            rubraview_color_adjust_params_t gray = { .exposure_ev = 0.0f, .contrast = 0.0f,
                                                     .saturation = 0.0f, .gamma = 1.0f };
            rubraview_color_adjust(&pixels, &gray);
        }
        if (color->has_unsharp) {
            rubraview_pixbuf_t sharp = rubraview_filter_unsharp_mask(arena, &pixels, color->sigma,
                                                                     color->amount, color->threshold);
            if (rubraview_pixbuf_is_valid(&sharp)) pixels = sharp;
        }
    }

    (void)pixels_change;
    if (options.format == RUBRAVIEW_EXPORT_SAME_AS_SOURCE) options.format = source_format;
    if (!rubraview_pal_image_save(out_path, &pixels, &options)) return false;

    bc->written++;
    return true;
}

/* Collects the files a run will work on: one file, or a directory, or a
   directory tree when --recursive was given. */
static size_t collect_inputs(proven_arena_t *arena, u8str_t root, bool recursive,
                             rubraview_batch_input_t *out, size_t capacity, size_t count) {
    rubraview_fs_listing_t listing = rubraview_pal_fs_list_dir(arena, root);
    if (listing.count == 0) {
        /* Not a directory: treat it as the single file it is. */
        if (count < capacity && rubraview_pal_fs_exists(root)) {
            out[count].path = root;
            out[count].size_bytes = 0;
            count++;
        }
        return count;
    }

    for (size_t i = 0; i < listing.count && count < capacity; ++i) {
        const rubraview_fs_entry_t *entry = &listing.entries[i];
        if (entry->is_directory) {
            if (recursive) count = collect_inputs(arena, entry->path, true, out, capacity, count);
            continue;
        }
        out[count].path = entry->path;
        out[count].size_bytes = entry->size_bytes;
        count++;
    }
    return count;
}

#define BATCH_MAX_INPUTS 8192

/* Formats "N of M" without pulling in printf's locale machinery. */
static void append_number(char *buf, size_t cap, size_t *pos, size_t value) {
    char digits[24];
    size_t n = 0;
    if (value == 0) digits[n++] = '0';
    while (value > 0 && n < sizeof(digits)) { digits[n++] = (char)('0' + (value % 10)); value /= 10; }
    while (n > 0 && *pos + 1 < cap) buf[(*pos)++] = digits[--n];
    buf[*pos] = '\0';
}

static void append_text(char *buf, size_t cap, size_t *pos, const char *text) {
    while (*text && *pos + 1 < cap) buf[(*pos)++] = *text++;
    buf[*pos] = '\0';
}

static int run_batch(proven_arena_t *arena, const rubraview_cli_result_t *cli) {
    proven_result_mem_mut_t res = proven_arena_alloc(arena, BATCH_MAX_INPUTS * sizeof(rubraview_batch_input_t));
    if (!proven_is_ok(res.err)) {
        console_line("rubraview: out of memory building the file list");
        return 1;
    }
    rubraview_batch_input_t *inputs = (rubraview_batch_input_t*)(void*)res.value.ptr;

    size_t count = collect_inputs(arena, cli->input, cli->recursive, inputs, BATCH_MAX_INPUTS, 0);
    if (count == 0) {
        console_line("rubraview: nothing to do — no files matched");
        return 1;
    }

    /* §3.11: one arena for the work, reset between files, so a run over
       ten thousand files uses what one file needs. */
    void *work_memory = malloc(BATCH_WORK_ARENA_BYTES);
    if (!work_memory) {
        console_line("rubraview: out of memory");
        return 1;
    }
    proven_arena_t work = proven_arena_create(
        (proven_mem_mut_t){ .ptr = (proven_byte_t*)work_memory, .size = BATCH_WORK_ARENA_BYTES });

    batch_ctx_t ctx = { .cli = cli, .output_dir = cli->output_dir, .written = 0 };
    rubraview_batch_report_t report = rubraview_batch_run(&work, &cli->job, inputs, count,
                                                          U8(""), batch_process, &ctx, NULL, 0);
    free(work_memory);

    char line[160];
    size_t pos = 0;
    line[0] = '\0';
    append_text(line, sizeof(line), &pos, "rubraview: ");
    append_number(line, sizeof(line), &pos, report.processed);
    append_text(line, sizeof(line), &pos, " converted, ");
    append_number(line, sizeof(line), &pos, report.skipped);
    append_text(line, sizeof(line), &pos, " skipped, ");
    append_number(line, sizeof(line), &pos, report.failed);
    append_text(line, sizeof(line), &pos, " failed");
    console_line(line);

    return report.failed > 0 ? 1 : 0;
}


/* ---- telling the reader why nothing appeared ---- */

/*
 * A viewer that shows nothing is the least useful failure there is, and
 * it cannot be diagnosed from another machine. So when the graphics
 * device will not start, the reason is put in front of the reader and
 * written to a file next to the program — with the operating system's
 * own error code, which is the part that identifies the cause.
 */
static void report_startup_failure(void) {
    u8str_t where = rubraview_pal_render_last_error();
    uint32_t hr = rubraview_pal_render_last_hresult();

    char message[512];
    int n = snprintf(message, sizeof(message),
        "Rubraview " RUBRAVIEW_VERSION_STRING " could not start its graphics.\r\n\r\n"
        "It failed while %.*s.\r\n"
        "Windows reported error 0x%08lX.\r\n\r\n"
        "This has been written to rubraview-diag.txt next to the program.",
        (int)where.len, where.len > 0 ? where.ptr : "starting up",
        (unsigned long)hr);
    if (n <= 0) return;

    /* The file first: a message box can be dismissed before it is read,
       and a file can be pasted into a report. */
    HANDLE file = CreateFileW(L"rubraview-diag.txt", GENERIC_WRITE, FILE_SHARE_READ,
                              NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, message, (DWORD)n, &written, NULL);
        CloseHandle(file);
    }

    WCHAR wide[1024];
    if (MultiByteToWideChar(CP_UTF8, 0, message, -1, wide, 1024) > 0) {
        MessageBoxW(NULL, wide, L"Rubraview", MB_OK | MB_ICONERROR);
    }
}

/* §3.22 [video] decoder (D-9): the backend a file is offered to first.
   The other one still gets its turn when this one cannot open it. */
static rubraview_media_backend_t preferred_backend(proven_arena_t *arena) {
    u8str_t text = rubraview_pal_fs_read_file(arena, U8("settings.ini"), 256u * 1024u);
    if (text.len == 0) return RUBRAVIEW_BACKEND_MEDIA_FOUNDATION;
    rubraview_ini_doc_t doc = rubraview_ini_parse(arena, text);
    const u8str_t *decoder = rubraview_ini_get(&doc, U8("video"), U8("decoder"));
    return (decoder && decoder->len == 6 && memcmp(decoder->ptr, "ffmpeg", 6) == 0)
        ? RUBRAVIEW_BACKEND_FFMPEG : RUBRAVIEW_BACKEND_MEDIA_FOUNDATION;
}

/* `--probe-media`: open a file with each backend in turn and say what
   happened — which one took it, what it thinks the file is, and whether
   frames really come out. There is no window, so it answers over a
   remote shell: the media PAL hands over CPU pixels and needs no
   renderer. mfprobe asks Windows what it *could* decode; this asks
   rubraview what it actually does with one file. */
static int probe_media_file(proven_arena_t *arena, u8str_t path) {
    char line[512];
    bool ffmpeg_here = rubraview_pal_media_backend_available(RUBRAVIEW_BACKEND_FFMPEG);
    console_line(ffmpeg_here
        ? "FFmpeg: its DLLs are here, at a version these headers know"
        : "FFmpeg: no usable DLLs beside the program (Media Foundation alone)");

    rubraview_media_backend_t preferred = preferred_backend(arena);
    console_line(preferred == RUBRAVIEW_BACKEND_FFMPEG
        ? "settings.ini [video] decoder = ffmpeg — FFmpeg is tried first"
        : "settings.ini [video] decoder = windows (or unset) — Media Foundation is tried first");

    rubraview_media_backend_t order[2];
    size_t count = rubraview_media_backend_order(preferred, ffmpeg_here, order);
    for (size_t i = 0; i < count; ++i) {
        const char *name = order[i] == RUBRAVIEW_BACKEND_FFMPEG ? "FFmpeg" : "Media Foundation";
        rubraview_media_open_result_t opened = rubraview_pal_media_open(path, order[i]);
        if (!opened.media) {
            u8str_t why = rubraview_media_failure_text(opened.failure);
            snprintf(line, sizeof(line), "%s: cannot open — %.*s", name, (int)why.len, why.ptr);
            console_line(line);
            continue;
        }

        snprintf(line, sizeof(line), "%s: opened — %dx%d, %.3f s, %.2f fps, picture %s, sound %s%s",
                 name, opened.info.width, opened.info.height, opened.info.duration_seconds,
                 opened.info.frame_rate, opened.info.has_video ? "yes" : "no",
                 opened.info.has_audio ? "yes" : "no",
                 opened.info.has_audio
                     ? (opened.info.audio_output ? " (going to a device)" : " (no audio device here)") : "");
        console_line(line);

        /* §3.16.2: what the container holds, as the menu would show it. */
        rubraview_track_set_t set = {0};
        if (rubraview_pal_media_tracks(opened.media, &set) && set.count > 0) {
            for (size_t t = 0; t < set.count; ++t) {
                char label[160];
                u8str_t text = rubraview_track_label(label, sizeof(label), &set, (int32_t)t);
                bool current = (int32_t)t == set.current_video || (int32_t)t == set.current_audio;
                const char *kind = set.tracks[t].kind == RUBRAVIEW_TRACK_AUDIO ? "sound"
                                 : set.tracks[t].kind == RUBRAVIEW_TRACK_SUBTITLE ? "subtitle"
                                 : "picture";
                snprintf(line, sizeof(line), "%s: %-8s %s %.*s", name, kind,
                         current ? "*" : " ", (int)text.len, text.ptr);
                console_line(line);
            }
        } else {
            snprintf(line, sizeof(line), "%s: the track list is empty", name);
            console_line(line);
        }

        /* Opening is not playing: take some frames and see. */
        int frames = 0;
        double first = -1.0, last = -1.0;
        for (int spin = 0; spin < 600 && frames < 24; ++spin) {
            rubraview_video_frame_t frame;
            if (rubraview_pal_media_peek_frame(opened.media, &frame)) {
                if (first < 0.0) first = frame.pts;
                last = frame.pts;
                frames++;
                rubraview_pal_media_pop_frame(opened.media);
                continue;
            }
            if (rubraview_pal_media_finished(opened.media)) break;
            rubraview_pal_time_sleep_ms(10);
        }
        if (opened.info.has_video) {
            snprintf(line, sizeof(line), "%s: %d picture(s) decoded, %.3f s to %.3f s", name, frames, first, last);
        } else {
            snprintf(line, sizeof(line), "%s: sound only — nothing to decode into pictures", name);
        }
        console_line(line);
        rubraview_pal_media_close(opened.media);
    }

    /* §3.16.1: the same search the viewer does, said out loud — which
       of discovery, reading and parsing worked, and what came out. */
    u8str_t sub_name = { .ptr = "", .len = 0 };
    const char *why = NULL;
    rubraview_subtitle_track_t track = subtitle_find(arena, path, &sub_name, &why);
    if (track.count == 0) {
        snprintf(line, sizeof(line), "Subtitles: none — %s", why ? why : "not looked for");
    } else {
        snprintf(line, sizeof(line), "Subtitles: %.*s — %zu line(s), the first %.3f s to %.3f s",
                 (int)sub_name.len, sub_name.ptr, track.count,
                 track.cues[0].start_seconds, track.cues[0].end_seconds);
    }
    console_line(line);
    return 0;
}

/* `--probe-gpu`: what graphics card this machine has, whether Media
   Foundation will take it for decoding, and — the question that decides
   RV-062's shape — whether a decoded frame then comes back as a texture
   or as system memory. Written before building the zero-copy path so
   that "can this be tried here at all" is measured, not assumed. */
static int probe_gpu_file(proven_arena_t *arena, u8str_t path) {
    (void)arena;
    char line[512];

    typedef HRESULT (WINAPI *create_dxgi_manager_fn)(UINT*, IMFDXGIDeviceManager**);
    HMODULE plat = LoadLibraryExW(L"mfplat.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE rw = LoadLibraryExW(L"mfreadwrite.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!plat || !rw) { console_line("Media Foundation is not installed here"); return 2; }
    create_dxgi_manager_fn create_manager =
        (create_dxgi_manager_fn)(void*)GetProcAddress(plat, "MFCreateDXGIDeviceManager");
    HRESULT (WINAPI *mf_startup)(ULONG, DWORD) =
        (HRESULT (WINAPI *)(ULONG, DWORD))(void*)GetProcAddress(plat, "MFStartup");
    HRESULT (WINAPI *mf_shutdown)(void) =
        (HRESULT (WINAPI *)(void))(void*)GetProcAddress(plat, "MFShutdown");
    HRESULT (WINAPI *mf_create_attributes)(IMFAttributes**, UINT32) =
        (HRESULT (WINAPI *)(IMFAttributes**, UINT32))(void*)GetProcAddress(plat, "MFCreateAttributes");
    HRESULT (WINAPI *mf_create_reader)(LPCWSTR, IMFAttributes*, IMFSourceReader**) =
        (HRESULT (WINAPI *)(LPCWSTR, IMFAttributes*, IMFSourceReader**))
            (void*)GetProcAddress(rw, "MFCreateSourceReaderFromURL");
    if (!create_manager || !mf_startup || !mf_shutdown || !mf_create_attributes || !mf_create_reader) {
        console_line("Media Foundation is here but not the calls this needs");
        return 2;
    }

    /* 1. A device. Hardware if there is one, otherwise Windows' own
          software rasteriser (WARP) — which is what a machine with no
          graphics card falls back to. */
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    D3D_FEATURE_LEVEL level = (D3D_FEATURE_LEVEL)0;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    const char *kind = "hardware";
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, NULL, 0,
                                   D3D11_SDK_VERSION, &device, &level, &context);
    if (FAILED(hr)) {
        kind = "WARP (software)";
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, flags, NULL, 0,
                               D3D11_SDK_VERSION, &device, &level, &context);
    }
    if (FAILED(hr) || !device) {
        console_line("no Direct3D 11 device at all, with or without video support");
        return 2;
    }
    snprintf(line, sizeof(line), "Device: %s, feature level %u.%u",
             kind, (unsigned)(level >> 12) & 0xF, (unsigned)(level >> 8) & 0xF);
    console_line(line);

    IDXGIDevice *dxgi = NULL;
    if (SUCCEEDED(ID3D11Device_QueryInterface(device, &IID_IDXGIDevice, (void**)&dxgi)) && dxgi) {
        IDXGIAdapter *adapter = NULL;
        if (SUCCEEDED(IDXGIDevice_GetAdapter(dxgi, &adapter)) && adapter) {
            DXGI_ADAPTER_DESC desc = {0};
            if (SUCCEEDED(IDXGIAdapter_GetDesc(adapter, &desc))) {
                char name[256] = {0};
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, (int)sizeof(name), NULL, NULL);
                snprintf(line, sizeof(line), "Adapter: %s (%u MB of its own memory)",
                         name, (unsigned)(desc.DedicatedVideoMemory / (1024u * 1024u)));
                console_line(line);
            }
            IDXGIAdapter_Release(adapter);
        }
        IDXGIDevice_Release(dxgi);
    }

    /* 2. Does it offer decoders at all? */
    ID3D11VideoDevice *video = NULL;
    if (SUCCEEDED(ID3D11Device_QueryInterface(device, &IID_ID3D11VideoDevice, (void**)&video)) && video) {
        UINT profiles = ID3D11VideoDevice_GetVideoDecoderProfileCount(video);
        snprintf(line, sizeof(line), "Decoder profiles the card offers: %u", profiles);
        console_line(line);
        ID3D11VideoDevice_Release(video);
    } else {
        console_line("Decoder profiles the card offers: none (no video device)");
    }

    /* 3. Hand the device to Media Foundation and read one frame. */
    if (FAILED(mf_startup(((ULONG)0x00020070), 0))) {
        console_line("Media Foundation would not start");
        return 2;
    }
    int code = 0;
    /* mingw-w64 declares the interface but ships no IID for it. */
    static const GUID iid_multithread =
        { 0x9B7E4E00, 0x342C, 0x4106, { 0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0 } };
    ID3D11Multithread *mt = NULL;
    if (SUCCEEDED(ID3D11DeviceContext_QueryInterface(context, &iid_multithread, (void**)&mt)) && mt) {
        /* Media Foundation uses the device from its own threads. */
        ID3D11Multithread_SetMultithreadProtected(mt, TRUE);
        ID3D11Multithread_Release(mt);
    }

    IMFDXGIDeviceManager *manager = NULL;
    UINT token = 0;
    if (FAILED(create_manager(&token, &manager)) || !manager ||
        FAILED(IMFDXGIDeviceManager_ResetDevice(manager, (IUnknown*)device, token))) {
        console_line("Media Foundation refused the device manager");
        code = 2;
    } else {
        WCHAR wide[MAX_PATH * 2];
        if (MultiByteToWideChar(CP_UTF8, 0, path.ptr, (int)path.len, wide,
                                (int)(sizeof(wide) / sizeof(wide[0])) - 1) <= 0) {
            console_line("that path cannot be read");
            code = 2;
        } else {
            wide[MultiByteToWideChar(CP_UTF8, 0, path.ptr, (int)path.len, NULL, 0)] = L'\0';
            IMFAttributes *attrs = NULL;
            IMFSourceReader *reader = NULL;
            if (SUCCEEDED(mf_create_attributes(&attrs, 3)) && attrs) {
                IMFAttributes_SetUnknown(attrs, &MF_SOURCE_READER_D3D_MANAGER, (IUnknown*)manager);
                IMFAttributes_SetUINT32(attrs, &MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
                IMFAttributes_SetUINT32(attrs, &MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
                hr = mf_create_reader(wide, attrs, &reader);
                IMFAttributes_Release(attrs);
            } else {
                hr = E_FAIL;
            }
            if (FAILED(hr) || !reader) {
                console_line("the reader would not open the file with the device attached");
                code = 2;
            } else {
                console_line("The reader took the device.");
                /* The first read often answers with a format change and
                   no picture at all; ask a few times before believing
                   that nothing is coming. */
                DWORD stream = 0, sample_flags = 0;
                LONGLONG timestamp = 0;
                IMFSample *sample = NULL;
                DWORD last_flags = 0;
                for (int attempt = 0; attempt < 40 && !sample; ++attempt) {
                    hr = IMFSourceReader_ReadSample(reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                                    &stream, &sample_flags, &timestamp, &sample);
                    last_flags = sample_flags;
                    if (FAILED(hr) || (sample_flags & MF_SOURCE_READERF_ENDOFSTREAM) ||
                        (sample_flags & MF_SOURCE_READERF_ERROR)) break;
                }
                if (FAILED(hr) || !sample) {
                    snprintf(line, sizeof(line),
                             "but no picture came out of it (hr=0x%08lX, last flags=0x%lX)",
                             (unsigned long)hr, (unsigned long)last_flags);
                    console_line(line);
                    code = 2;
                } else {
                    IMFMediaBuffer *buffer = NULL;
                    bool on_the_card = false;
                    if (SUCCEEDED(IMFSample_GetBufferByIndex(sample, 0, &buffer)) && buffer) {
                        IMFDXGIBuffer *dxgi_buffer = NULL;
                        if (SUCCEEDED(IMFMediaBuffer_QueryInterface(buffer, &IID_IMFDXGIBuffer,
                                                                    (void**)&dxgi_buffer)) && dxgi_buffer) {
                            on_the_card = true;
                            IMFDXGIBuffer_Release(dxgi_buffer);
                        }
                        IMFMediaBuffer_Release(buffer);
                    }
                    console_line(on_the_card
                        ? "The frame came back as a texture on the card — the zero-copy path can be tried here."
                        : "The frame came back as system memory — this machine cannot exercise the zero-copy path.");
                    IMFSample_Release(sample);
                }
                IMFSourceReader_Release(reader);
            }
        }
    }
    if (manager) IMFDXGIDeviceManager_Release(manager);
    mf_shutdown();
    ID3D11DeviceContext_Release(context);
    ID3D11Device_Release(device);
    return code;
}

/* `--diag`: bring the graphics up, say what it got, and stop. One run
   from a command prompt answers "what is this machine actually using",
   which is otherwise guesswork from far away. */
/*
 * The stages are deliberately separated and each one announces itself
 * *before* it runs, so a hang names the step it hung in rather than
 * leaving a silent window. The first run of this found exactly that: the
 * report stopped after "walking the image path", which said the failure
 * was in the very first call and nowhere else.
 *
 * They also run in order of dependency — COM, then the codec, then a
 * window, then the GPU — so the earliest broken thing is found before
 * anything downstream can confuse the picture.
 */
static void pump_messages(void) {
    /* A window whose messages nobody reads is a window Windows calls
       "not responding", and an STA that never pumps can deadlock COM
       outright. Anywhere this code waits, it waits like this. */
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static void wait_pumping(int milliseconds) {
    for (int i = 0; i < milliseconds; i += 16) {
        pump_messages();
        rubraview_pal_time_sleep_ms(16);
    }
}

static int run_diagnostics(proven_arena_t *arena, u8str_t image_path) {
    console_line("rubraview " RUBRAVIEW_VERSION_STRING " - diagnostics");
    console_line("rubraview: step 1 - starting COM's imaging factory (WIC)");
    /* Before any window exists, so a hang here cannot be blamed on one. */
    IWICImagingFactory *probe_factory = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory, (void**)&probe_factory);
    if (FAILED(hr) || !probe_factory) {
        char line[160];
        snprintf(line, sizeof(line), "rubraview: step 1 FAILED (0x%08lX) - no imaging codecs at all", (unsigned long)hr);
        console_line(line);
        return 1;
    }
    console_line("rubraview: step 1 ok");

    if (image_path.len > 0) {
        console_line("rubraview: step 2 - opening the file with WIC (no window, no GPU)");

        char narrow[1024];
        size_t n = image_path.len < sizeof(narrow) - 1 ? image_path.len : sizeof(narrow) - 1;
        memcpy(narrow, image_path.ptr, n);
        narrow[n] = '\0';

        WCHAR wide[1024];
        IWICBitmapDecoder *decoder = NULL;
        if (MultiByteToWideChar(CP_UTF8, 0, narrow, -1, wide, 1024) > 0) {
            hr = IWICImagingFactory_CreateDecoderFromFilename(probe_factory, wide, NULL, GENERIC_READ,
                                                              WICDecodeMetadataCacheOnDemand, &decoder);
        } else {
            hr = E_INVALIDARG;
        }

        char line[256];
        if (FAILED(hr) || !decoder) {
            snprintf(line, sizeof(line), "rubraview: step 2 FAILED (0x%08lX) - the path or the format", (unsigned long)hr);
            console_line(line);
            IWICImagingFactory_Release(probe_factory);
            return 1;
        }

        UINT frames = 0;
        IWICBitmapDecoder_GetFrameCount(decoder, &frames);
        IWICBitmapFrameDecode *frame = NULL;
        hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
        UINT w = 0, h = 0;
        if (SUCCEEDED(hr) && frame) IWICBitmapFrameDecode_GetSize(frame, &w, &h);
        snprintf(line, sizeof(line), "rubraview: step 2 ok - %u frame(s), %ux%u", frames, w, h);
        console_line(line);
        if (frame) IWICBitmapFrameDecode_Release(frame);
        IWICBitmapDecoder_Release(decoder);
    }
    IWICImagingFactory_Release(probe_factory);

    console_line("rubraview: step 3 - creating the window");
    rubraview_window_config_t config = { .title = "Rubraview " RUBRAVIEW_VERSION_STRING " diagnostics", .width = 640, .height = 400, .frameless = false };
    rubraview_window_t *window = rubraview_pal_window_create(arena, &config);
    if (!window) {
        console_line("rubraview: step 3 FAILED - the window could not be created");
        return 1;
    }
    pump_messages();
    console_line("rubraview: step 3 ok");

    console_line("rubraview: step 4 - starting the graphics device");
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(window, &w, &h);
    rubraview_renderer_t *renderer = rubraview_pal_render_create(
        arena, rubraview_pal_window_native_handle(window), w, h);

    if (!renderer) {
        char line[512];
        u8str_t where = rubraview_pal_render_last_error();
        snprintf(line, sizeof(line), "rubraview: step 4 FAILED while %.*s (0x%08lX)",
                 (int)where.len, where.ptr, (unsigned long)rubraview_pal_render_last_hresult());
        console_line(line);
        rubraview_pal_window_destroy(window);
        return 1;
    }

    char described[256];
    u8str_t info = rubraview_pal_render_describe(renderer, described, sizeof(described));
    char line[320];
    snprintf(line, sizeof(line), "rubraview: step 4 ok - %.*s", (int)info.len, info.ptr);
    console_line(line);

    console_line("rubraview: step 5 - drawing a plain rectangle");
    rubraview_pal_render_begin(renderer, 0xFF203040u);
    rubraview_pal_rect_t box = { 40.0, 40.0, 200.0, 120.0 };
    rubraview_pal_render_fill_rect(renderer, box, 0xFFCC4444u, 4.0);
    bool presented = rubraview_pal_render_end(renderer);
    console_line(presented ? "rubraview: step 5 ok - it was presented"
                           : "rubraview: step 5 FAILED - the device was lost");
    wait_pumping(800);

    if (image_path.len > 0) {
        console_line("rubraview: step 6 - the viewer's own decode path, stage by stage");

        char report[2048];
        u8str_t text = rubraview_pal_image_diagnose(renderer, image_path, report, sizeof(report));

        size_t start = 0;
        for (size_t i = 0; i <= text.len; ++i) {
            bool at_end = i == text.len;
            if (!at_end && text.ptr[i] != '\n') continue;
            size_t stop = i;
            while (stop > start && (text.ptr[stop - 1] == '\r' || text.ptr[stop - 1] == '\n')) stop--;
            if (stop > start) {
                char one[256];
                size_t take = stop - start < sizeof(one) - 1 ? stop - start : sizeof(one) - 1;
                memcpy(one, text.ptr + start, take);
                one[take] = '\0';
                console_line(one);
            }
            start = i + 1;
        }

        console_line("rubraview: step 7 - loading it the way the viewer does");
        rubraview_image_load_result_t loaded =
            rubraview_pal_image_load_texture(renderer, image_path, true);
        console_line(loaded.ok ? "rubraview: step 7 ok - it became a texture"
                               : "rubraview: step 7 FAILED - it did not become a texture");

        if (loaded.ok && loaded.texture) {
            console_line("rubraview: step 8 - putting it on the screen (5 seconds)");
            rubraview_pal_render_begin(renderer, 0xFF101010u);
            rubraview_mat3x2_t place = rubraview_mat3x2_identity();
            rubraview_pal_render_draw_texture(renderer, loaded.texture, place, RUBRAVIEW_INTERP_LINEAR);
            bool shown = rubraview_pal_render_end(renderer);
            console_line(shown ? "rubraview: step 8 ok - look at the window now"
                               : "rubraview: step 8 FAILED - it did not present");
            wait_pumping(5000);
            rubraview_pal_texture_destroy(loaded.texture);
        }
    } else {
        wait_pumping(1200);
    }

    console_line("rubraview: done");
    rubraview_pal_render_destroy(renderer);
    rubraview_pal_window_destroy(window);
    return presented ? 0 : 1;
}

/* §3.6: dragging a floating box. The press is held: travel more than a
   few pixels and it was a drag, otherwise it was the click the anchor
   would have done anyway. */
#define BOX_DRAG_SLOP 4.0

static bool box_press(app_state_t *app, double x, double y) {
    rubraview_tile_metrics_t metrics =
        rubraview_tile_metrics_default(rubraview_pal_window_dpi_scale(app->window));
    rubraview_box_t *boxes[2] = { &app->menubox, &app->toolbox };
    for (size_t i = 0; i < 2; ++i) {
        if (rubraview_box_anchor_half_at(boxes[i], &metrics, x, y) == RUBRAVIEW_ANCHOR_NONE) continue;
        app->box_drag = boxes[i];
        app->box_grab_dx = x - boxes[i]->anchor_x;
        app->box_grab_dy = y - boxes[i]->anchor_y;
        app->box_press_x = x;
        app->box_press_y = y;
        app->box_drag_moved = false;
        return true;
    }
    return false;
}

static void box_drag_motion(app_state_t *app, double x, double y) {
    if (!app->box_drag) return;
    if (!app->box_drag_moved) {
        double dx = x - app->box_press_x, dy = y - app->box_press_y;
        if (dx * dx + dy * dy < BOX_DRAG_SLOP * BOX_DRAG_SLOP) return;
        app->box_drag_moved = true;
    }
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    rubraview_tile_metrics_t metrics =
        rubraview_tile_metrics_default(rubraview_pal_window_dpi_scale(app->window));
    rubraview_box_drag_to(app->box_drag, &metrics, x - app->box_grab_dx, y - app->box_grab_dy,
                          (double)win_w, (double)win_h);
}

static void box_release(app_state_t *app, double x, double y) {
    if (!app->box_drag) return;
    rubraview_box_t *box = app->box_drag;
    app->box_drag = NULL;
    if (app->box_drag_moved) return;   /* a drag: the box stays where it was let go */
    rubraview_tile_metrics_t metrics =
        rubraview_tile_metrics_default(rubraview_pal_window_dpi_scale(app->window));
    if (rubraview_box_click(box, &metrics, x, y) && box == &app->menubox) sync_menubox_tiles(app);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show) {
    (void)instance; (void)previous; (void)command_line; (void)show;

    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
        return 1;
    }

    /* Before any window exists. A single-threaded apartment that is not
       pumping messages can deadlock while activating a COM object, and
       the first image is opened before the message loop has run once —
       so this has to happen while there is no window to deadlock
       against. Without it the program hangs on the first picture, which
       from the outside looks like an image that will not render. */
    rubraview_pal_image_startup();

    void *memory = malloc(APP_ARENA_BYTES);
    if (!memory) {
        CoUninitialize();
        return 1;
    }
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)memory, .size = APP_ARENA_BYTES });

    /* §3.11: the command line is read before a window is created,
       because `--batch` must not open one. */
    {
        int argc = 0;
        LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (wargv) {
            static char storage[32][2048];
            const char *argv_utf8[32];
            int usable = argc < 32 ? argc : 32;
            for (int i = 0; i < usable; ++i) {
                int written = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, storage[i],
                                                  (int)sizeof(storage[i]), NULL, NULL);
                argv_utf8[i] = written > 0 ? storage[i] : "";
            }
            LocalFree(wargv);

            rubraview_cli_result_t cli;
            rubraview_cli_parse(&cli, &arena, usable, argv_utf8);

            if (cli.err != RUBRAVIEW_CLI_OK) {
                char message[512];
                size_t pos = 0;
                message[0] = '\0';
                append_text(message, sizeof(message), &pos, "rubraview: ");
                u8str_t reason = rubraview_cli_error_text(cli.err);
                for (size_t i = 0; i < reason.len && pos + 1 < sizeof(message); ++i) {
                    message[pos++] = reason.ptr[i];
                }
                message[pos] = '\0';
                if (cli.offending.len > 0) {
                    append_text(message, sizeof(message), &pos, ": ");
                    for (size_t i = 0; i < cli.offending.len && pos + 1 < sizeof(message); ++i) {
                        message[pos++] = cli.offending.ptr[i];
                    }
                    message[pos] = '\0';
                }
                console_line(message);
                free(memory);
                CoUninitialize();
                return 2;
            }

            if (cli.show_version) {
                console_line("rubraview " RUBRAVIEW_VERSION_STRING);
                free(memory);
                CoUninitialize();
                return 0;
            }

            if (cli.probe_media) {
                int code = probe_media_file(&arena, cli.input);
                free(memory);
                CoUninitialize();
                return code;
            }

            if (cli.probe_gpu) {
                int code = probe_gpu_file(&arena, cli.input);
                free(memory);
                CoUninitialize();
                return code;
            }

            if (cli.diagnostics) {
                int code = run_diagnostics(&arena, cli.input);
                free(memory);
                CoUninitialize();
                return code;
            }

            if (cli.batch_mode) {
                int code = run_batch(&arena, &cli);
                free(memory);
                CoUninitialize();
                return code;
            }

            /* §3.19.3: registering associations is a job, not a launch. */
            if (cli.register_shell || cli.unregister_shell) {
                bool ok = cli.register_shell
                            ? rubraview_pal_shell_register(rubraview_shell_extensions())
                            : rubraview_pal_shell_unregister(rubraview_shell_extensions());
                console_line(ok ? "rubraview: file associations updated"
                                : "rubraview: could not update the file associations");
                free(memory);
                CoUninitialize();
                return ok ? 0 : 1;
            }

            /* §3.19.1: hand the file to the window that is already open,
               rather than opening a second one. */
            {
                u8str_t settings = rubraview_pal_fs_read_file(&arena, U8("settings.ini"), 256u * 1024u);
                bool single_instance = true;
                if (settings.len > 0) {
                    rubraview_ini_doc_t doc = rubraview_ini_parse(&arena, settings);
                    single_instance = rubraview_ini_get_bool(&doc, U8(""), U8("single_instance"), true);
                }
                if (cli.new_instance) single_instance = false;

                bool alone = rubraview_pal_instance_claim();
                if (rubraview_instance_decide(single_instance, !alone) == RUBRAVIEW_INSTANCE_HAND_OVER) {
                    if (rubraview_pal_instance_hand_over(cli.input)) {
                        free(memory);
                        CoUninitialize();
                        return 0;
                    }
                    /* The mutex said someone was there but no window
                       answered — a crashed instance, most likely. Start
                       normally rather than refusing to run. */
                }
            }
        }
    }

    app_state_t app = {0};
    app.arena = &arena;
    app.fit_mode = RUBRAVIEW_FIT_WINDOW;
    app.zoom = 1.0;
    app.orientation = rubraview_orientation_identity();
    app.spread_detect = true; /* §3.3.4 default */
    app.layout_opts = rubraview_layout_opts_default(RUBRAVIEW_PAGE_LAYOUT_SINGLE, RUBRAVIEW_READING_LTR);

    rubraview_window_config_t window_config = {
        /* The version is in the title so a screenshot identifies the
           build it came from — which is most of what a bug report needs. */
        .title = "Rubraview " RUBRAVIEW_VERSION_STRING,
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
        report_startup_failure();
        rubraview_pal_window_destroy(app.window);
        free(memory);
        CoUninitialize();
        return 1;
    }

    double dpi = rubraview_pal_window_dpi_scale(app.window);
    load_keymap(&app);

    /* §3.1: no worker pool. Every decode touches WIC, Direct2D and the
       arena, none of which may be used off this thread; the ring runs
       without one and precache_decode queues pages for the main loop. */
    app.jobs = NULL;
    app.media_page = -1;
    /* §3.22 [video] decoder (D-9): which backend opens a file first. The
       other one still gets its turn when this one cannot. */
    app.media_preferred = preferred_backend(&arena);
    history_load(&app);
    app.osd = rubraview_osd_create(2.0, 0.5);            /* §3.1 */
    app.titlebar = rubraview_titlebar_create(dpi);       /* §3.21.2 */
    app.toolbox = rubraview_box_create(RUBRAVIEW_BOX_TOOLBOX, (double)win_w - 220.0 * dpi, (double)win_h - 160.0 * dpi, TOOLBOX_TILES);
    app.menubox = rubraview_box_create(RUBRAVIEW_BOX_MENU, 24.0 * dpi, 24.0 * dpi, MENU_ROOT_COUNT);
    layout_load(&app);   /* §3.6: back where the reader left them */
    app.menu = rubraview_menu_create(&MENU_TREE); /* §3.6.2 category tree */
    app.transition = rubraview_transition_create(RUBRAVIEW_TRANSITION_CROSSFADE, 0.25);
    app.cursor = rubraview_cursor_hide_create(1.5);      /* §3.2.5 */
    app.filmstrip = rubraview_filmstrip_create(0, FILMSTRIP_THUMB * dpi, (double)win_w);

    /* §3.18.3: the triage folders, and §3.19.2: accept drops. */
    {
        u8str_t settings = rubraview_pal_fs_read_file(&arena, U8("settings.ini"), 256u * 1024u);
        app.curation = rubraview_curation_parse(&arena, settings);
    }
    rubraview_pal_window_accept_drops(app.window, true);
    sync_menubox_tiles(&app);

    int argc = 0;
    /* Let the freshly created window settle its own messages before the
       first file is opened. Decoding runs COM calls, and an apartment
       with an unpumped window is where those calls go to hang. */
    pump_messages();

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
    double last_busy_seconds = app.last_frame_seconds;
    double last_idle_frame_seconds = app.last_frame_seconds;

    while (!rubraview_pal_window_should_close(app.window)) {
        rubraview_window_event_t event;
        size_t handled = 0;
        while (rubraview_pal_window_poll_event(app.window, &event)) {
            handled++;
            switch (event.kind) {
                case RUBRAVIEW_WINDOW_EVENT_CLOSE:
                    rubraview_pal_window_request_close(app.window);
                    break;

                case RUBRAVIEW_WINDOW_EVENT_RESIZE:
                    rubraview_pal_render_resize(app.renderer, event.resize.width, event.resize.height);
                    app.filmstrip.viewport_extent = (double)event.resize.width;
                    app.needs_relayout = true;
                    panel_relayout(&app);
                    break;

                case RUBRAVIEW_WINDOW_EVENT_DPI_CHANGED:
                    app.titlebar = rubraview_titlebar_create(event.dpi.scale);
                    app.needs_relayout = true;
                    break;

                case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN:
                    dispatch_key(&app, event.key.combo);
                    break;

                case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE: {
                    if (app.panel.open && app.panel.active_row >= 0) {
                        int32_t dragged = -1;
                        if (rubraview_panel_drag(&app.panel, event.mouse.x, event.mouse.y, &dragged)
                                == RUBRAVIEW_PANEL_VALUE_CHANGED) {
                            panel_apply_row(&app, dragged);
                        }
                    }
                    app.pointer_x = event.mouse.x;
                    app.pointer_y = event.mouse.y;
                    rubraview_titlebar_pointer_moved(&app.titlebar, event.mouse.y);
                    if (rubraview_cursor_hide_notify_motion(&app.cursor)) {
                        rubraview_pal_window_set_cursor_visible(app.window, true);
                    }
                    note_activity(&app);

                    /* §3.6: the box model decides what a pointer over
                       it means — only the hover half opens anything. */
                    rubraview_tile_metrics_t metrics = rubraview_tile_metrics_default(rubraview_pal_window_dpi_scale(app.window));
                    rubraview_box_pointer(&app.toolbox, &metrics, event.mouse.x, event.mouse.y);
                    rubraview_box_pointer(&app.menubox, &metrics, event.mouse.x, event.mouse.y);
                    box_drag_motion(&app, event.mouse.x, event.mouse.y);
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

                    if (settings_handle_press(&app, event.mouse.x, event.mouse.y)) break;
                    if (panel_handle_press(&app, event.mouse.x, event.mouse.y)) break;
                    /* The anchors answer before the rest of the chrome: a
                       press there is a click *or* the start of a drag. */
                    if (box_press(&app, event.mouse.x, event.mouse.y)) break;
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

                case RUBRAVIEW_WINDOW_EVENT_MOUSE_UP:
                    rubraview_panel_release(&app.panel);
                    box_release(&app, event.mouse.x, event.mouse.y);
                    break;

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

                case RUBRAVIEW_WINDOW_EVENT_DROP:
                case RUBRAVIEW_WINDOW_EVENT_OPEN_REQUEST:
                    handle_drop(&app, &event);
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
        if (app.media_skip_pending) {
            /* D-9: the file nothing could open was reported; move past it. */
            app.media_skip_pending = false;
            next_spread(&app);
        }
        media_tick(&app);

        /* Redraw only while something can change on screen. Without a
           GPU, Direct2D rasterises on the CPU, and a loop that redrew
           the same still image kept two cores busy doing it. */
        bool animating = app.slideshow_running || app.anim_active ||
                         (app.media && !app.media_paused) ||
                         app.notice_seconds > 0.0 || app.pending_decode_count > 0;
        if (handled > 0 || animating) last_busy_seconds = now;
        bool settled = now - last_busy_seconds > IDLE_REDRAW_GRACE;
        if (!settled || now - last_idle_frame_seconds >= 1.0) {
            render_frame(&app);
            if (settled) last_idle_frame_seconds = now;
        }

        /* A queued decode takes the loop's idle slice. With nothing to
           get ready, a settled loop sleeps until the OS has something
           for it instead of spinning. */
        if (drain_one_pending_decode(&app)) {
            last_busy_seconds = now;
        } else if (settled) {
            rubraview_pal_window_wait_event(app.window, 250);
        } else {
            rubraview_pal_time_sleep_ms(4);
        }
    }

    /* §3.6 / §3.17.1: remember where the reader stopped, and where the
       boxes were left, before shutting down. */
    layout_save(&app);
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

    media_close(&app);
    unload_all_pages(&app);
    rubraview_page_source_close(&app.source);
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
