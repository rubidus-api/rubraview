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
#include <math.h>
#include <time.h>

#include "rubraview/number.h"
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
#include "rubraview/ui_actions.h"
#include "rubraview/vobsub.h"
#include "rubraview/pgs.h"
#include "rubraview/tags.h"
#include "rubraview/music.h"
#include "rubraview/boxes_doc.h"
#include "rubraview/ui_chrome.h"
#include "rubraview/filmstrip.h"
#include "rubraview/picker.h"
#include "rubraview/playlist.h"
#include "rubraview/help.h"
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
#include "rubraview/settings_doc.h"
#include "rubraview/ui_settings.h"
#include "rubraview/pal/pal_file_dialog.h"
#include "rubraview/pal/pal_process.h"
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
#include "rubraview/pal/pal_audio.h"
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
#define MENU_MAX_TILES 16   /* 4 x 4; the owner chose a larger grid over splitting File (2026-09-21) */

/* Metro palette (§3.6.4): flat, high-contrast, no gradients. */
#define COLOR_CANVAS      0xFF101010u
#define COLOR_BOX_FILL    0xE01A1A1Au
#define COLOR_BOX_BORDER  0x30FFFFFFu
#define COLOR_TILE_FILL   0xE0242424u
#define COLOR_TEXT        0xFFF0F0F0u
#define COLOR_BAR_FILL    0xE1141414u
#define COLOR_CLOSE_HOVER 0xFFE81123u
#define COLOR_TILE_CURRENT 0xFF5B9BD5u   /* the choice in use among several (a layout, a fit) */



/* The boxes' tiles and menu tree come from their document (RFC-0002,
   D-15): src/core/default_boxes_doc.c. */

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
    /* §3.22.2 / D-14: keymap.ini sits with settings.ini; what it held when
       the settings window opened is what Revert goes back to. */
    rubraview_keymap_t keymap_saved;
    u8str_t            keymap_path;
    int32_t            key_capture;       /* a Keys row waiting for its new key (1..), or 0 */

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
    bool                   media_new_picture; /* a picture went up since the last redraw */
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
    double                 media_speed;       /* D-15: 0.25–4.0, kept across files for the session */
    double                 ab_a, ab_b;        /* D-15 A-B repeat points in file seconds, -1 when unset */
    /* The same two points, typed (owner, 2026-09-24). The box holds one
       field per point; a key stamps the playhead into the one in hand,
       and what is typed goes back to the points when it closes. */
    bool                   ab_edit_open;
    int32_t                ab_edit_field;     /* 0 = A, 1 = B */
    char                   ab_edit_text[2][32];
    size_t                 ab_edit_length[2];
    bool                   timeline_dragging; /* RFC-0002 §4.2: the pointer holds the seek bar */
    /* RFC-0002 Q6: the toolbox as a window of its own once dragged out. */
    rubraview_window_t    *toolbox_window;
    rubraview_renderer_t  *toolbox_renderer;
    double                 toolbox_window_drawn;
    double                 toolbox_window_pointer_x, toolbox_window_pointer_y;   /* for the hovered button's caption */
    double                 toolbox_window_opacity;
    double                 timeline_last_seek;/* wall time of the last seek while dragging */
    /* §3.16.1 / R135: the external subtitle file that goes with the
       video on screen. Empty when the film has none. */
    rubraview_subtitle_track_t subtitle;
    u8str_t                    subtitle_name;   /* what to say in the OSD */
    /* A DVD's picture subtitles (owner, 2026-09-23). The index is held
       whole; one picture at a time is decoded into `vobsub_pixels` and
       uploaded, and the texture is kept while that subtitle is up. */
    rubraview_vobsub_track_t   vobsub;
    u8str_t                    vobsub_sub;          /* the `.sub` file's bytes */
    uint8_t                   *vobsub_pixels;
    rubraview_texture_t       *vobsub_texture;
    int32_t                    vobsub_texture_cue;  /* which one the texture holds, -1 for none */
    int32_t                    vobsub_texture_w, vobsub_texture_h;
    rubraview_vobsub_cue_t     vobsub_cue;
    /* A Blu-ray's picture subtitles (owner, 2026-09-24). The same shape
       as the DVD's above, over a `.sup` file instead of an index pair. */
    rubraview_pgs_track_t      pgs;
    u8str_t                    pgs_bytes;           /* the `.sup` file itself */
    uint8_t                   *pgs_pixels;
    rubraview_texture_t       *pgs_texture;
    int32_t                    pgs_texture_cue;
    int32_t                    pgs_texture_w, pgs_texture_h;
    rubraview_pgs_cue_t        pgs_cue;
    /* RV-076: what a music file says about itself, and the backdrop made
       from its cover. Both belong to the track on screen. */
    rubraview_tags_t           music_tags;
    bool                       music_tags_read;
    rubraview_texture_t       *music_backdrop;
    /* RV-075: the next track, opened while this one still plays, so one
       runs into the next without a gap — and, when the reader asks for
       one, across a crossfade. */
    rubraview_media_t         *next_media;
    rubraview_media_info_t     next_info;
    int32_t                    next_page;
    bool                       next_playing;
    bool                       music_overlapping;  /* both tracks are sounding */
    /* RV-081: music that outlives the page it came from — the background
       music of §3.14.6. It is the same player, moved out of the page's
       hands rather than opened again. */
    rubraview_media_t         *bgm_media;
    rubraview_media_info_t     bgm_info;
    int32_t                    bgm_page;
    double                     bgm_position;
    bool                       bgm_paused;
    rubraview_bgm_t            bgm;
    rubraview_tags_t           bgm_tags;        /* what the background track says about itself */
    /* §3.14.6: the mini player — its own small window, on top, showing
       whichever track is sounding and driving it. */
    rubraview_window_t        *mini_window;
    rubraview_renderer_t      *mini_renderer;
    rubraview_texture_t       *mini_cover;
    int32_t                    mini_cover_w, mini_cover_h;
    bool                       mini_open;
    bool                       mini_dirty;
    int32_t                    mini_frame[4];
    int32_t                    mini_hover;      /* which button the pointer is on, -1 for none */
    double                     music_end_seen;     /* wall time the backend first said "finished", or 0 */
    rubraview_mat3x2_t         video_transform;     /* where the film was last drawn */
    bool                       video_transform_ok;
    int32_t                    video_page_w, video_page_h;
    /* §3.16.2: the sound tracks the file holds and the subtitle files
       beside it, in one list — what the reader cycles through. */
    rubraview_track_set_t         tracks;
    rubraview_subtitle_candidate_t subtitle_files[8];
    /* A DVD index holds one list per language, so one file can be several
       tracks; this says which language each track reads (D-22's backlog). */
    int32_t                    subtitle_vobsub_stream[8];
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
    bool            menubox_was_open;   /* for rubraview_box_just_opened */
    rubraview_menu_state_t menu;
    /* RFC-0002: the menu tree for this moment and the toolbox's tiles for
       what is on screen, both built from the boxes' document. */
    rubraview_menu_tree_t  menu_tree;
    uint32_t               menu_when;          /* RUBRAVIEW_BOXES_WHEN_* the tree was built for */
    rubraview_box_tile_t   toolbox_tiles[RUBRAVIEW_TOOLBOX_MAX_TILES];
    int32_t                toolbox_tile_count;
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
    /* §3.13: a crop being dragged on the picture — where it started, in image pixels. */
    bool                      crop_dragging;
    /* §3.13: the curve widget — which point is being dragged, or -1. */
    int32_t                   curve_dragging;
    int32_t                   crop_drag_x0, crop_drag_y0;
    rubraview_edit_session_t  edit;
    rubraview_export_options_t export_options;
    rubraview_batch_job_t      batch_job;
    rubraview_batch_action_t   batch_actions[8];

    /* §3.18: triage. The undo stack is what makes Delete safe to press
       quickly, which is the point of the whole section. */
    rubraview_undo_stack_t undo;
    rubraview_curation_t   curation;
    bool                   rename_active;
    bool                   rename_is_extension;   /* the box is taking an extension for the picked files */
    char                   rename_buffer[256];
    size_t                 rename_length;
    char                   rename_composing[64];   /* the IME's unfinished syllable, drawn after the text */
    size_t                 rename_composing_length;
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
    rubraview_settings_t     settings;
    rubraview_settings_t     settings_saved;   /* what the file held when the window opened — Revert goes back to it */
    u8str_t                  settings_path;
    u8str_t                  config_beside;   /* the executable's own folder: portable mode's home */
    u8str_t                  layout_path;      /* §3.6: where the floating boxes were left */
    /* §3.22.1 / D-13: a window of its own, laid out by the interpreter
       from the settings document. */
    /* The help (owner, 2026-09-23): a window of its own that the reader
       can leave open while using the viewer, holding the keys in force. */
    rubraview_window_t        *help_window;
    rubraview_renderer_t      *help_renderer;
    bool                       help_open;
    bool                       help_dirty;
    int32_t                    help_scroll;      /* the first line on screen */
    double                     help_font, help_cell_w, help_cell_h;
    rubraview_help_line_t      help_lines[512];
    size_t                     help_count;
    int32_t                    help_key_cols;    /* the widest key list, so nothing overlaps */
    int32_t                    help_frame[4];    /* where it was last put */

    rubraview_window_t        *settings_window;
    rubraview_renderer_t      *settings_renderer;
    rubraview_settings_view_t  settings_view;
    double                     settings_font, settings_cell_w, settings_cell_h;
    bool                       settings_dirty;       /* its picture is out of date */
    double                     settings_info_seconds; /* when `info` lines were last redrawn */
    /* Where the window was left (layout.ini); width 0 until known. Closing
       hides the window rather than destroying it, so it is made once. */
    int32_t                    settings_frame[4];     /* x, y, width, height in screen pixels */
    bool                       settings_mouse_down;
    char                       settings_message[240]; /* the last action's result, under the page */

    /* In-app Metro file picker (§3.15.2), RV-043 */
    bool                   picker_open;
    u8str_t                picker_dir;
    rubraview_fs_listing_t picker_listing;
    size_t                 picker_hidden;   /* files in the folder the viewer cannot open, not listed */
    rubraview_confirm_t    menu_confirm;    /* a destructive menu item asks first */
    rubraview_picker_t     picker;
    bool                  *picker_selected;   /* one per listed item, remade on every folder */
    /* Move and Copy wait for the digit that names a curation folder;
       Recycle waits to be pressed a second time (§3.18.1's rule). */
    int32_t                picker_pending;     /* a picker_button_t, waiting for its digit */
    bool                   picker_has_pending;
    rubraview_confirm_t    picker_confirm;
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
static void help_show(app_state_t *app);
static void mini_show(app_state_t *app);
static bool mini_paused(const app_state_t *app);
static void mini_close(app_state_t *app);
static void draw_rename_box(app_state_t *app, double win_w, double win_h, double dpi);
static void draw_ab_edit_box(app_state_t *app, double win_w, double win_h, double dpi);
static void ab_edit_begin(app_state_t *app);
static void draw_notice(app_state_t *app, double win_w, double win_h, double chrome_dpi);
static void rename_end(app_state_t *app);
static void finish_open(app_state_t *app, size_t start_page);

static void panel_close(app_state_t *app);
static bool curve_widget_press(app_state_t *app, double px, double py, bool remove);
static rubraview_pal_rect_t curve_widget_rect(const app_state_t *app);
static bool edit_panel_open(const app_state_t *app);
static bool crop_point_to_image(app_state_t *app, double px, double py, int32_t *out_x, int32_t *out_y);
static bool point_in_curve_widget(const app_state_t *app, double px, double py);
static void append_number(char *buf, size_t cap, size_t *pos, size_t value);
static void history_remember(app_state_t *app);
static void append_text(char *buf, size_t cap, size_t *pos, const char *text);
static void osd_say(app_state_t *app, u8str_t text);
static void settings_open(app_state_t *app);
static void settings_close(app_state_t *app);
static void layout_save(app_state_t *app);
static void settings_took_effect(app_state_t *app);
static void media_ab_check(app_state_t *app);
static void dispatch_key(app_state_t *app, rubraview_key_combo_t combo);
static void toolbox_detach(app_state_t *app, int32_t screen_x, int32_t screen_y, bool follow_pointer);
static void toolbox_dock(app_state_t *app, double client_x, double client_y);
static void settings_write_if_changed(app_state_t *app, bool say);
static void settings_say(app_state_t *app, const char *text);
static void panel_open_edit(app_state_t *app);
static void panel_open_export(app_state_t *app);
static void panel_open_batch(app_state_t *app);
static void panel_run_batch(app_state_t *app);

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
        proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, ANIM_MAX_FRAMES, sizeof(rubraview_frame_t));
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

    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, page_count(app), sizeof(rubraview_page_info_t));
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
        /* The name the status line shows, not the path: an archive page
           has no path of its own (found by T037). */
        u8str_t name = page_display_name(app, (size_t)page);
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

static void vobsub_clear(app_state_t *app);
static void pgs_clear(app_state_t *app);
static void music_clear(app_state_t *app);
static void music_next_close(app_state_t *app);
static bool bgm_adopt(app_state_t *app);
static void music_transition_tick(app_state_t *app);
static void music_promote_next(app_state_t *app);
static void go_to_spread(app_state_t *app, size_t index);
static void update_precache(app_state_t *app);
static void ab_edit_close(app_state_t *app);

static void media_close(app_state_t *app) {
    /* §3.14.6: a track that is still playing does not die because its
       page is leaving — it becomes the background music. This is here
       rather than in the caller so that no path can forget it. */
    if (bgm_adopt(app)) return;
    vobsub_clear(app);
    pgs_clear(app);
    music_clear(app);
    music_next_close(app);
    /* Nothing is playing now, so there is nothing for the points to be
       in: a box left open would apply them to the next file. */
    if (app->ab_edit_open) ab_edit_close(app);
    app->video_transform_ok = false;
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
/* RV-076: the head of a music file is where its tags and its cover live,
   so a 300 MB record is not read to find out who is singing. */
#define MUSIC_MAX_HEAD_BYTES (4u * 1024u * 1024u)

static void music_clear(app_state_t *app) {
    if (app->music_backdrop) rubraview_pal_texture_destroy(app->music_backdrop);
    app->music_backdrop = NULL;
    app->music_tags = (rubraview_tags_t){0};
    app->music_tags_read = false;
}

/* The cover, blurred: a small copy of it, softened, and left for the
   renderer to stretch — which is the blur a backdrop actually wants and
   costs a few thousand pixels rather than a few million. */
static void music_make_backdrop(app_state_t *app, const rubraview_tags_t *tags) {
    if (!tags->art || tags->art_size == 0) return;
    rubraview_pixbuf_t art = rubraview_pal_image_read_pixels(app->arena, (u8str_t){ .ptr = "", .len = 0 },
                                                             tags->art, tags->art_size, false);
    if (!rubraview_pixbuf_is_valid(&art)) return;

    rubraview_pixbuf_t small = rubraview_pixbuf_resample(app->arena, &art, 64, 64, RUBRAVIEW_FILTER_BILINEAR);
    if (!rubraview_pixbuf_is_valid(&small)) return;
    rubraview_pixbuf_t soft = rubraview_filter_box_blur(app->arena, &small, 6);
    if (!rubraview_pixbuf_is_valid(&soft)) soft = small;
    rubraview_pixbuf_t bgra = soft.format == RUBRAVIEW_PIXFMT_BGRA8
        ? soft : rubraview_pixbuf_convert(app->arena, &soft, RUBRAVIEW_PIXFMT_BGRA8);
    if (!rubraview_pixbuf_is_valid(&bgra)) return;

    app->music_backdrop = rubraview_pal_texture_create_bgra(app->renderer, bgra.width, bgra.height);
    if (!app->music_backdrop) return;
    if (!rubraview_pal_texture_upload_bgra(app->music_backdrop, bgra.pixels, bgra.stride)) {
        rubraview_pal_texture_destroy(app->music_backdrop);
        app->music_backdrop = NULL;
    }
}

/* Read the file's own words once per track. */
static void music_load(app_state_t *app, u8str_t path) {
    music_clear(app);
    if (path.len == 0) return;
    u8str_t head = rubraview_pal_fs_read_file(app->arena, path, MUSIC_MAX_HEAD_BYTES);
    if (head.len == 0) return;      /* a record too large for the head budget: no tags, and it still plays */
    app->music_tags = rubraview_tags_read(app->arena, (rubraview_tags_source_t){
        .head = (const uint8_t*)head.ptr, .head_size = head.len, .file_size = head.len });
    app->music_tags_read = true;
    music_make_backdrop(app, &app->music_tags);
}

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
    /* The shell knows the common formats; when it does not (FLAC and
       Opus often), the cover this file carries is read here instead. */
    if (!texture && app->music_tags_read && app->music_tags.art_size > 0) {
        rubraview_image_load_result_t art = rubraview_pal_image_load_texture_from_memory(
            app->renderer, app->music_tags.art, app->music_tags.art_size, false);
        if (art.texture) {
            texture = art.texture;
            *out_w = art.width;
            *out_h = art.height;
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
        /* RV-062: a film decoded on the card gets a texture on the card,
           and its frames are copied into it there. */
        page->texture = !app->media_info.has_video
            ? audio_page_picture(app, app->source.pages[app->media_page].path, &w, &h)
            : app->media_info.hardware_decode
                ? rubraview_pal_texture_create_video(app->renderer, w, h)
                : rubraview_pal_texture_create_bgra(app->renderer, w, h);
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
/* A DVD's index is text, its pictures are not: a feature film's worth of
   subpictures runs to a few tens of megabytes. */
#define VOBSUB_MAX_IDX_BYTES (4u * 1024u * 1024u)
#define VOBSUB_MAX_SUB_BYTES (192u * 1024u * 1024u)
#define PGS_MAX_SUP_BYTES    (192u * 1024u * 1024u)
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

    proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, listing.count, sizeof(u8str_t));
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

    rubraview_subtitle_candidate_t found[SUBTITLE_MAX_CANDIDATES];
    size_t found_count = subtitle_candidates(app->arena, video_path, found, SUBTITLE_MAX_CANDIDATES, NULL);

    /* One entry per track: a text file is one, a DVD index is one for
       each language it lists, so `C` walks them like any other track. */
    app->subtitle_count = 0;
    for (size_t i = 0; i < found_count && app->subtitle_count < SUBTITLE_MAX_CANDIDATES; ++i) {
        size_t languages = 1;
        u8str_t tags[SUBTITLE_MAX_CANDIDATES];
        if (found[i].format == RUBRAVIEW_SUBTITLE_VOBSUB) {
            u8str_t idx_text = rubraview_pal_fs_read_file(app->arena, found[i].path, VOBSUB_MAX_IDX_BYTES);
            size_t n = rubraview_vobsub_languages(idx_text, tags, SUBTITLE_MAX_CANDIDATES);
            if (n > 0) languages = n;
        } else if (found[i].format == RUBRAVIEW_SUBTITLE_PGS) {
            languages = 1;     /* one `.sup` is one language, and it is not text */
        } else {
            /* A SAMI file usually holds every language at once, a class
               each (owner, 2026-09-23); the others hold one. */
            rubraview_subtitle_track_t peek = subtitle_read(app->arena, found[i], NULL);
            size_t n = rubraview_subtitle_languages(&peek, tags, SUBTITLE_MAX_CANDIDATES);
            if (n > 1) languages = n;
        }
        for (size_t k = 0; k < languages && app->subtitle_count < SUBTITLE_MAX_CANDIDATES; ++k) {
            app->subtitle_files[app->subtitle_count] = found[i];
            app->subtitle_vobsub_stream[app->subtitle_count] = (int32_t)k;
            app->subtitle_count++;
        }
    }

    static const char *const FORMAT_NAME[] = { "?", "srt", "smi", "vtt", "ass", "idx", "sup" };
    for (size_t i = 0; i < app->subtitle_count; ++i) {
        u8str_t language = rubraview_subtitle_language_tag(video_path, app->subtitle_files[i].path);
        u8str_t tags[SUBTITLE_MAX_CANDIDATES];
        size_t which = (size_t)app->subtitle_vobsub_stream[i];
        if (app->subtitle_files[i].format == RUBRAVIEW_SUBTITLE_VOBSUB) {
            /* The index names its own languages; the file's name does not. */
            u8str_t idx_text = rubraview_pal_fs_read_file(app->arena, app->subtitle_files[i].path, VOBSUB_MAX_IDX_BYTES);
            size_t n = rubraview_vobsub_languages(idx_text, tags, SUBTITLE_MAX_CANDIDATES);
            if (which < n) language = tags[which];
        } else if (app->subtitle_files[i].format == RUBRAVIEW_SUBTITLE_PGS) {
            /* Its name is all there is to go on, which `language` already has. */
        } else if (which > 0 || app->subtitle_files[i].format == RUBRAVIEW_SUBTITLE_SMI) {
            rubraview_subtitle_track_t peek = subtitle_read(app->arena, app->subtitle_files[i], NULL);
            size_t n = rubraview_subtitle_languages(&peek, tags, SUBTITLE_MAX_CANDIDATES);
            if (which < n && tags[which].len > 0) language = tags[which];
        }
        rubraview_track_t track = {
            .kind = RUBRAVIEW_TRACK_SUBTITLE,
            .is_external = true,          /* a file beside the video, not a stream in it */
            .stream_index = (int32_t)i,   /* into app->subtitle_files, not the container */
            .language = language,
            .title = rubraview_path_basename(app->subtitle_files[i].path),
            .codec = cstr(FORMAT_NAME[(size_t)app->subtitle_files[i].format < 7
                                      ? (size_t)app->subtitle_files[i].format : 0]),
        };
        rubraview_tracks_add(&app->tracks, track);
    }
}

static bool same_text(u8str_t a, u8str_t b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0);
}

/* Shows one subtitle track, or none when `index` is -1. */
/* The picture subtitles go with the film they belong to. */
static void vobsub_clear(app_state_t *app) {
    if (app->vobsub_texture) rubraview_pal_texture_destroy(app->vobsub_texture);
    app->vobsub_texture = NULL;
    app->vobsub_texture_cue = -1;
    app->vobsub_texture_w = app->vobsub_texture_h = 0;
    app->vobsub = (rubraview_vobsub_track_t){0};
    app->vobsub_sub = (u8str_t){ .ptr = "", .len = 0 };
}

/* `movie.idx` names the index; its pictures are in `movie.sub` beside it. */
static void vobsub_load(app_state_t *app, u8str_t idx_path, size_t language) {
    vobsub_clear(app);
    u8str_t idx_text = rubraview_pal_fs_read_file(app->arena, idx_path, VOBSUB_MAX_IDX_BYTES);
    if (idx_text.len == 0) return;

    /* The whole path, not just the name: the viewer is not always
       started in the film's folder. */
    u8str_t sub_path = rubraview_path_with_ext(app->arena, idx_path, ".sub");
    if (sub_path.len == 0) return;
    app->vobsub_sub = rubraview_pal_fs_read_file(app->arena, sub_path, VOBSUB_MAX_SUB_BYTES);
    if (app->vobsub_sub.len == 0) {
        osd_say(app, U8("the .sub file beside the index is missing"));
        return;
    }
    if (!app->vobsub_pixels) {
        proven_result_mem_mut_t mem = proven_arena_alloc(app->arena, RUBRAVIEW_VOBSUB_MAX_PIXELS);
        if (!proven_is_ok(mem.err)) return;
        app->vobsub_pixels = (uint8_t*)mem.value.ptr;
    }
    app->vobsub = rubraview_vobsub_index(app->arena, idx_text, language);
}

/* `movie.sup` is the whole of it: no index, no second file. */
static void pgs_clear(app_state_t *app) {
    if (app->pgs_texture) rubraview_pal_texture_destroy(app->pgs_texture);
    app->pgs_texture = NULL;
    app->pgs_texture_cue = -1;
    app->pgs_texture_w = app->pgs_texture_h = 0;
    app->pgs = (rubraview_pgs_track_t){0};
    app->pgs_bytes = (u8str_t){ .ptr = "", .len = 0 };
}

static void pgs_load(app_state_t *app, u8str_t sup_path) {
    pgs_clear(app);
    app->pgs_bytes = rubraview_pal_fs_read_file(app->arena, sup_path, PGS_MAX_SUP_BYTES);
    if (app->pgs_bytes.len == 0) {
        osd_say(app, U8("the subtitle file could not be read"));
        return;
    }
    if (!app->pgs_pixels) {
        proven_result_mem_mut_t mem = proven_arena_alloc(app->arena, RUBRAVIEW_PGS_MAX_PIXELS);
        if (!proven_is_ok(mem.err)) return;
        app->pgs_pixels = (uint8_t*)mem.value.ptr;
    }
    app->pgs = rubraview_pgs_index(app->arena, (const uint8_t*)app->pgs_bytes.ptr, app->pgs_bytes.len);
}

/* The sync the reader set by hand for this film comes back — for a text
   track and for a picture one alike, since one offset moves them all. */
static void subtitle_restore_offset(app_state_t *app) {
    if (app->media_page >= 0 && (size_t)app->media_page < page_count(app) &&
        same_text(app->subtitle_offset_for, app->source.pages[app->media_page].path)) {
        app->subtitle.offset_seconds = app->subtitle_offset_seconds;
    }
}

static void subtitle_select(app_state_t *app, int32_t index) {
    vobsub_clear(app);
    pgs_clear(app);
    app->subtitle = (rubraview_subtitle_track_t){0};
    app->subtitle_name = (u8str_t){ .ptr = "", .len = 0 };
    app->tracks.current_subtitle = -1;
    if (index < 0 || (size_t)index >= app->tracks.count) return;
    const rubraview_track_t *track = &app->tracks.tracks[index];
    if (track->kind != RUBRAVIEW_TRACK_SUBTITLE) return;

    if (track->is_external) {
        if (track->stream_index < 0 || (size_t)track->stream_index >= app->subtitle_count) return;
        if (app->subtitle_files[track->stream_index].format == RUBRAVIEW_SUBTITLE_VOBSUB) {
            vobsub_load(app, app->subtitle_files[track->stream_index].path,
                        (size_t)app->subtitle_vobsub_stream[track->stream_index]);
            if (app->vobsub.count == 0) return;
            u8str_t picture_label = rubraview_track_label(app->subtitle_label, sizeof(app->subtitle_label),
                                                          &app->tracks, index);
            app->subtitle_name = track->title.len > 0 ? track->title : picture_label;
            app->tracks.current_subtitle = index;
            subtitle_restore_offset(app);
            return;
        }
        if (app->subtitle_files[track->stream_index].format == RUBRAVIEW_SUBTITLE_PGS) {
            pgs_load(app, app->subtitle_files[track->stream_index].path);
            if (app->pgs.count == 0) return;
            u8str_t picture_label = rubraview_track_label(app->subtitle_label, sizeof(app->subtitle_label),
                                                          &app->tracks, index);
            app->subtitle_name = track->title.len > 0 ? track->title : picture_label;
            app->tracks.current_subtitle = index;
            subtitle_restore_offset(app);
            return;
        }
        app->subtitle = subtitle_read(app->arena, app->subtitle_files[track->stream_index], NULL);
        /* One file, several languages: show the one this track stands for. */
        u8str_t tags[SUBTITLE_MAX_CANDIDATES];
        size_t n = rubraview_subtitle_languages(&app->subtitle, tags, SUBTITLE_MAX_CANDIDATES);
        size_t which = (size_t)app->subtitle_vobsub_stream[track->stream_index];
        if (n > 1 && which < n) app->subtitle.shown_language = tags[which];
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
    subtitle_restore_offset(app);
}

static void subtitle_load(app_state_t *app, u8str_t video_path) {
    tracks_prepare(app, video_path);
    /* §3.16.2: the preferred language decides; with none set, the first
       file wins. Parsing happens now, and only for the one chosen. */
    subtitle_select(app, rubraview_tracks_choose(&app->tracks, RUBRAVIEW_TRACK_SUBTITLE,
                                                 (u8str_t){ .ptr = "", .len = 0 }));
}

/* ---- RV-081: music that outlives its page ---- */

static bool bgm_pause_for_sound(const app_state_t *app) {
    return rubraview_settings_get(&app->settings, U8("audio"), U8("bgm_pause_on_video")) != 0.0;
}

static void bgm_do(app_state_t *app, rubraview_bgm_action_t action) {
    if (!app->bgm_media) return;
    if (action == RUBRAVIEW_BGM_DO_PAUSE && !app->bgm_paused) {
        rubraview_pal_media_set_paused(app->bgm_media, true);
        app->bgm_paused = true;
    } else if (action == RUBRAVIEW_BGM_DO_RESUME && app->bgm_paused) {
        rubraview_pal_media_set_paused(app->bgm_media, false);
        app->bgm_paused = false;
    }
}

static void bgm_close(app_state_t *app) {
    if (app->bgm_media) rubraview_pal_media_close(app->bgm_media);
    app->bgm_media = NULL;
    app->bgm_info = (rubraview_media_info_t){0};
    app->bgm_page = -1;
    app->bgm_position = 0.0;
    app->bgm_paused = false;
    (void)rubraview_bgm_event(&app->bgm, RUBRAVIEW_BGM_MUSIC_CLOSED, bgm_pause_for_sound(app));
}

/* The page is leaving but the track is not: the player is moved out of
   the page's hands and goes on playing behind whatever is opened next.
   Nothing is opened or decoded again — it is the same player. */
static bool bgm_adopt(app_state_t *app) {
    if (!app->media || app->media_info.has_video || app->media_ended) return false;
    if (!app->media_info.has_audio) return false;
    if (app->media_page < 0 || (size_t)app->media_page >= page_count(app)) return false;
    if (!rubraview_tags_is_music_name(rubraview_path_basename(app->source.pages[app->media_page].path))) return false;

    bgm_close(app);                 /* only one piece of background music at a time */
    music_next_close(app);          /* and no track waiting behind it */

    app->bgm_media = app->media;
    app->bgm_info = app->media_info;
    app->bgm_page = app->media_page;
    app->bgm_position = app->media_position;
    app->bgm_tags = app->music_tags;   /* its words outlive the page too */
    app->bgm_paused = app->media_paused;
    app->media = NULL;
    app->media_page = -1;
    app->media_ended = false;
    app->media_has_frame = false;

    /* The page it came from is gone, so nothing is sounding on screen —
       otherwise the arbiter would see the track's own page as a rival
       and stand the music aside from itself. */
    (void)rubraview_bgm_event(&app->bgm, RUBRAVIEW_BGM_PAGE_QUIET, bgm_pause_for_sound(app));
    bgm_do(app, rubraview_bgm_event(&app->bgm, RUBRAVIEW_BGM_MUSIC_OPENED, bgm_pause_for_sound(app)));
    return true;
}

/* Back to the page the music came from: the same player takes its place
   again, at the moment it has reached. */
static bool bgm_release_to_page(app_state_t *app, int32_t index) {
    if (!app->bgm_media || index < 0 || index != app->bgm_page) return false;
    app->media = app->bgm_media;
    app->media_info = app->bgm_info;
    app->media_page = index;
    app->media_paused = app->bgm_paused;
    app->media_position = app->bgm_position;
    app->media_ended = false;
    app->media_has_frame = false;
    app->media_title_tenth = -1;
    app->media_master = rubraview_media_master_for(app->media_info.has_audio, app->media_info.audio_output);
    app->media_clock = rubraview_media_clock_create(app->media_master, app->bgm_position,
                                                    rubraview_pal_time_now_seconds());
    rubraview_media_clock_set_rate(&app->media_clock, app->media_speed, rubraview_pal_time_now_seconds());
    if (!app->media_paused) rubraview_media_clock_resume(&app->media_clock, rubraview_pal_time_now_seconds());

    app->bgm_media = NULL;
    app->bgm_page = -1;
    app->bgm_paused = false;
    (void)rubraview_bgm_event(&app->bgm, RUBRAVIEW_BGM_MUSIC_CLOSED, bgm_pause_for_sound(app));

    music_load(app, app->source.pages[index].path);
    subtitle_load(app, app->source.pages[index].path);
    (void)media_page_ready(app);
    return true;
}

/* Once a pass: keep the background music's own position, and notice when
   it has played itself out. */
static void bgm_tick(app_state_t *app) {
    if (!app->bgm_media) return;
    if (!app->bgm_paused) {
        double heard = 0.0, at = 0.0;
        if (rubraview_pal_media_audio_position(app->bgm_media, &heard, &at)) app->bgm_position = heard;
        /* The backend says "finished" before the device has been heard
           out (the same early word the crossfade ran into, T084), so the
           track is only let go once it has actually reached its end. */
        if (rubraview_pal_media_finished(app->bgm_media) &&
            (app->bgm_info.duration_seconds <= 0.0 ||
             app->bgm_position >= app->bgm_info.duration_seconds - 0.25)) {
            bgm_close(app);
        }
    }
}

/* Opens the video when the page on screen is one, and closes the old one. */
static void media_prepare(app_state_t *app) {
    int32_t index = current_page_index(app);
    if (app->media && index == app->media_page) return;

    media_close(app);   /* which hands a playing track to the background */

    /* Back on the music's own page: the same player, where it had got to. */
    if (bgm_release_to_page(app, index)) return;

    if (index < 0 || (size_t)index >= page_count(app)) {
        bgm_do(app, rubraview_bgm_event(&app->bgm, RUBRAVIEW_BGM_PAGE_QUIET, bgm_pause_for_sound(app)));
        return;
    }
    u8str_t path = app->source.pages[index].path;
    if (!is_media_path(path)) {
        /* A picture or a comic makes no sound: the music has the floor. */
        bgm_do(app, rubraview_bgm_event(&app->bgm, RUBRAVIEW_BGM_PAGE_QUIET, bgm_pause_for_sound(app)));
        return;
    }

    /* D-9: the preferred backend first, the other one when it cannot. */
    rubraview_media_backend_t order[2];
    size_t count = rubraview_media_backend_order(app->media_preferred,
                                                 rubraview_pal_media_backend_available(RUBRAVIEW_BACKEND_FFMPEG),
                                                 order);
    rubraview_media_open_result_t opened = { .media = NULL, .failure = RUBRAVIEW_MEDIA_FAIL_FILE };
    rubraview_media_failure_t why = RUBRAVIEW_MEDIA_FAIL_FILE;
    /* RV-062: decoding on the graphics card, as [video] hardware_decode
       says — off (the default until it has been measured on a real card),
       on where the card offers decoders, or always (diagnostic). */
    rubraview_media_gpu_t gpu = {
        .mode = (int32_t)lround(rubraview_settings_get(&app->settings, U8("video"), U8("hardware_decode"))),
    };
    if (gpu.mode > 0) gpu.device = rubraview_pal_render_video_device(app->renderer, &gpu.decoder_profiles);
    for (size_t i = 0; i < count; ++i) {
        opened = rubraview_pal_media_open(path, order[i], &gpu);
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
    if (app->media_speed <= 0.0) app->media_speed = 1.0;
    rubraview_media_clock_set_rate(&app->media_clock, app->media_speed, rubraview_pal_time_now_seconds());
    rubraview_pal_audio_set_speed(app->media_speed);
    app->ab_a = app->ab_b = -1.0;   /* a repeat belongs to the file it was set in */
    /* §3.2.6 / RV-061: a slide holding a film or a track waits for the
       whole of it, not for the still-image interval. */
    if (app->slides && (size_t)app->spread_index < app->layout.count) {
        app->slides[app->spread_index].kind = RUBRAVIEW_MEDIA_VIDEO;
        app->slides[app->spread_index].duration_seconds = opened.info.duration_seconds;
    }
    /* A file whose sound cannot be played looks exactly like a file with
       no sound, which is how three measurements were misread on
       2026-09-24. Say which call failed instead. */
    if (opened.info.has_audio && !opened.info.audio_output) {
        char why[128], line[192];
        int n = rubraview_pal_audio_last_failure(why, sizeof(why))
            ? snprintf(line, sizeof(line), "no sound: the device would not open (%s)", why)
            : snprintf(line, sizeof(line), "no sound: the device would not open");
        if (n > 0) osd_say(app, (u8str_t){ .ptr = line, .len = (size_t)n });
    }
    /* §3.14.6: something with sound of its own is on screen now. */
    if (opened.info.has_audio) {
        bgm_do(app, rubraview_bgm_event(&app->bgm, RUBRAVIEW_BGM_PAGE_SOUNDS, bgm_pause_for_sound(app)));
    }
    if (!opened.info.has_video) music_load(app, path);
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
    if (frame->gpu_texture) {
        rubraview_pal_texture_copy_video_frame(page->texture, frame->gpu_texture, frame->gpu_subresource);
    } else {
        rubraview_pal_texture_upload_bgra(page->texture, frame->pixels, frame->stride);
    }
    app->media_has_frame = true;
    app->media_new_picture = true;
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
    media_ab_check(app);
    rubraview_media_t *m = app->media;
    double now = rubraview_pal_time_now_seconds();

    /* §5.3: with the sound as master, the clock follows what is heard. */
    if (app->media_master == RUBRAVIEW_CLOCK_AUDIO) {
        double heard = 0.0, at = 0.0;
        if (rubraview_pal_media_audio_position(m, &heard, &at)) {
            rubraview_media_clock_sync_audio(&app->media_clock,
                rubraview_audio_position_now_at_rate(heard, at, now, !app->media_paused, MEDIA_AUDIO_EXTRAPOLATION,
                                                     app->media_speed > 0.0 ? app->media_speed : 1.0),
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

    /* RV-075: the next track is opened while this one still plays, and
       takes over when it is due. */
    music_transition_tick(app);
    if (app->media != m) return;      /* it took over; the rest is the new track's */

    if (!app->media_paused) {
        /* A sound-only file with no device decodes nothing; its end is its duration. */
        bool at_end = (app->media_info.has_video || app->media_info.audio_output)
            ? rubraview_pal_media_finished(m)
            : (app->media_info.duration_seconds > 0.0 && clock >= app->media_info.duration_seconds);
        if (at_end && app->next_media && app->next_playing) {
            /* One was already playing under it: hand over rather than
               stop — but not in the middle of a crossfade. The backend
               says "finished" when its decoder has run out, which is
               about a second before the sound the device still holds has
               been heard; handing over there closes the output and cuts
               both the tail and the fade (measured, T084). So the
               overlap is allowed to finish, with a wall-clock guard in
               case the position stops moving. */
            if (app->music_end_seen == 0.0) app->music_end_seen = now;
            double waited = now - app->music_end_seen;
            double crossfade = rubraview_settings_get(&app->settings, U8("audio"), U8("crossfade_seconds"));
            if (!app->music_overlapping || waited > crossfade + 1.0) {
                music_promote_next(app);
                return;
            }
        } else if (!at_end) {
            app->music_end_seen = 0.0;
        }
        if (at_end && app->music_overlapping) return;   /* the overlap has the floor */
        if (at_end) {
            /* The end: hold the last picture. Slice 4 hands this to the slide show. */
            rubraview_media_clock_pause(&app->media_clock, now);
            rubraview_pal_media_set_paused(m, true);
            app->media_paused = true;
            app->media_ended = true;
            /* §3.14.6: the film is over; the music that stood aside for
               it may come back. */
            bgm_do(app, rubraview_bgm_event(&app->bgm, RUBRAVIEW_BGM_PAGE_QUIET, bgm_pause_for_sound(app)));
            if (!app->media_info.has_video && app->media_info.duration_seconds > 0.0 &&
                app->media_position > app->media_info.duration_seconds) {
                app->media_position = app->media_info.duration_seconds;   /* the clock overshoots by a pass */
            }
            update_window_title(app);
        }
    }
}

/* ---- RV-075: one track running into the next ---- */

/* The page after this one, when it is a music file. A folder holds
   pictures and films too, and neither is something to slide into: the
   next track is the next *page*, and only when it is a track. */
static int32_t music_next_page(const app_state_t *app) {
    if (app->media_page < 0) return -1;
    size_t next = (size_t)app->media_page + 1;
    if (next >= page_count(app)) return -1;
    u8str_t path = app->source.pages[next].path;
    if (!is_media_path(path) || !rubraview_tags_is_music_name(rubraview_path_basename(path))) return -1;
    return (int32_t)next;
}

static void music_next_close(app_state_t *app) {
    if (app->next_media) rubraview_pal_media_close(app->next_media);
    app->next_media = NULL;
    app->next_info = (rubraview_media_info_t){0};
    app->next_page = -1;
    app->next_playing = false;
    app->music_overlapping = false;
    app->music_end_seen = 0.0;
}

/* The one that was waiting becomes the one that is playing. Nothing is
   opened here — that is the whole point — so the page moves to it with
   the player it already has. */
static void music_promote_next(app_state_t *app) {
    if (!app->next_media) return;
    int32_t page = app->next_page;

    if (app->media) rubraview_pal_media_close(app->media);
    vobsub_clear(app);
    pgs_clear(app);
    music_clear(app);

    app->media = app->next_media;
    app->media_info = app->next_info;
    app->media_page = page;
    app->next_media = NULL;
    app->next_page = -1;
    app->next_playing = false;
    app->music_overlapping = false;
    app->music_end_seen = 0.0;

    rubraview_pal_media_set_gain(app->media, 1.0);
    app->media_paused = false;
    app->media_ended = false;
    app->media_has_frame = false;
    app->media_position = 0.0;
    app->media_title_tenth = -1;
    app->media_master = rubraview_media_master_for(app->media_info.has_audio, app->media_info.audio_output);
    app->media_clock = rubraview_media_clock_create(app->media_master, 0.0, rubraview_pal_time_now_seconds());
    rubraview_media_clock_set_rate(&app->media_clock, app->media_speed, rubraview_pal_time_now_seconds());
    app->ab_a = app->ab_b = -1.0;      /* a repeat belongs to the track it was set in */

    music_load(app, app->source.pages[page].path);
    subtitle_load(app, app->source.pages[page].path);
    (void)media_page_ready(app);

    /* And the page on screen follows. `media_prepare` sees that the
       player is already this page's and leaves it alone. */
    go_to_spread(app, spread_index_for_page(app, page));
    update_precache(app);
}

/* Called once a tick while a track plays: open the next one in time,
   start it when it is due, and hold both at the levels the plan says. */
static void music_transition_tick(app_state_t *app) {
    if (!app->media || app->media_info.has_video || app->media_paused) return;

    rubraview_track_change_t how = {
        .gapless = rubraview_settings_get(&app->settings, U8("audio"), U8("gapless")) != 0.0,
        .crossfade_seconds = rubraview_settings_get(&app->settings, U8("audio"), U8("crossfade_seconds")),
    };
    int32_t next = music_next_page(app);
    /* The track that was waiting is no longer the one that follows —
       the reader moved on by hand — so it is let go. */
    if (app->next_media && app->next_page != next) music_next_close(app);

    rubraview_track_plan_t plan = rubraview_track_plan(how, app->media_position,
                                                       app->media_info.duration_seconds, next >= 0);

    if (plan.open_next && !app->next_media && next >= 0) {
        u8str_t path = app->source.pages[next].path;
        rubraview_media_backend_t order[2];
        size_t count = rubraview_media_backend_order(app->media_preferred,
                                                     rubraview_pal_media_backend_available(RUBRAVIEW_BACKEND_FFMPEG),
                                                     order);
        for (size_t i = 0; i < count; ++i) {
            /* No graphics card for a track: there are no pictures in it. */
            rubraview_media_open_result_t opened = rubraview_pal_media_open(path, order[i], NULL);
            if (opened.media) {
                app->next_media = opened.media;
                app->next_info = opened.info;
                app->next_page = next;
                app->next_playing = false;
                rubraview_pal_media_set_gain(app->next_media, 0.0);
                rubraview_pal_media_set_paused(app->next_media, true);
                break;
            }
        }
        /* One that cannot be opened is not tried again every tick: the
           end of this track will move to it in the ordinary way. */
        if (!app->next_media) app->next_page = next;
    }

    if (plan.start_next && app->next_media && !app->next_playing) {
        rubraview_pal_media_set_paused(app->next_media, false);
        app->next_playing = true;
    }

    rubraview_pal_media_set_gain(app->media, plan.gain_current);
    if (app->next_media) rubraview_pal_media_set_gain(app->next_media, plan.gain_next);
    app->music_overlapping = app->next_playing && !plan.close_current;

    if (plan.close_current && app->next_playing) music_promote_next(app);
}

/* D-15 A-B repeat: past B, back to A. */
static void media_seek_to(app_state_t *app, double seconds);
static void media_ab_check(app_state_t *app) {
    if (!app->media || app->media_paused || app->ab_a < 0.0 || app->ab_b <= app->ab_a) return;
    if (app->media_position >= app->ab_b) {
        media_seek_to(app, app->ab_a);
        /* The picture still shown is from past B until the next frame
           comes; without this every tick would seek again and it would
           never play on. */
        app->media_position = app->ab_a;
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

static u8str_t menu_tile_caption(const rubraview_menu_state_t *menu, int32_t tile, char *scratch, size_t scratch_size,
                                 bool *out_enabled, bool *out_current);

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
#define PICKER_ACTION_HEIGHT 96.0   /* two rows: the buttons, then what is picked */
#define PICKER_BUTTON_ROW 48.0

/* The buttons along the bottom of the picker (owner, 2026-09-23). The
   first three are modes and switches; the rest act on what is picked. */
typedef enum picker_button {
    PICKER_BTN_INDIVIDUAL = 0,
    PICKER_BTN_RANGE,
    PICKER_BTN_SAME_TYPE,
    PICKER_BTN_EXTENSION,
    PICKER_BTN_PLAYLIST,
    PICKER_BTN_RECYCLE,
    PICKER_BTN_MOVE,
    PICKER_BTN_COPY,
    PICKER_BTN_CLEAR,
    PICKER_BTN_COUNT,
} picker_button_t;

static const char *const PICKER_BUTTON_LABEL[PICKER_BTN_COUNT] = {
    "Individual", "Range", "Same type", "Change ext",
    "Playlist", "Recycle", "Move to", "Copy to", "Clear",
};

/* Where a button sits, in client coordinates. */
static rubraview_pal_rect_t picker_button_rect(double win_w, double win_h, double dpi, int32_t index) {
    double row_h = PICKER_BUTTON_ROW * dpi;
    double top = win_h - PICKER_ACTION_HEIGHT * dpi;
    double gap = 6.0 * dpi;
    double width = (win_w - gap * (PICKER_BTN_COUNT + 1)) / (double)PICKER_BTN_COUNT;
    return (rubraview_pal_rect_t){ gap + (double)index * (width + gap), top + gap * 0.5,
                                   width, row_h - gap };
}

/* Lists a directory and orders it the way the viewer orders pages, so
   the picker and the page sequence agree. */
static void picker_navigate(app_state_t *app, u8str_t dir) {
    rubraview_fs_listing_t listing = rubraview_pal_fs_list_dir(app->arena, dir);
    /* Only folders and what the viewer opens (owner, 2026-09-21). */
    size_t hidden = rubraview_picker_keep_openable(&listing, U8(IMAGE_FILTER ";" MEDIA_FILTER ";" ARCHIVE_FILTER));
    if (listing.count == 0 && rubraview_path_dirname(dir).len == 0) {
        if (hidden > 0) osd_say(app, U8("nothing in that folder can be opened here"));
        return;
    }
    if (listing.count == 0 && hidden > 0) {
        /* D-18 kept the reader out of a folder with nothing to open; with
           `..` on screen it is no longer a trap, so it is only said. */
        osd_say(app, U8("nothing here can be opened"));
    }
    app->picker_hidden = hidden;

    /* Directories first, then files, each in natural order — folders are
       what a reader scans for first on a touch screen. */
    rubraview_sort_item_t *items = NULL;
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, listing.count, sizeof(rubraview_sort_item_t));
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
            rubraview_arena_alloc_array(app->arena, listing.count, sizeof(rubraview_fs_entry_t));
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

    /* `..` first, wherever there is a folder above this one (owner,
       2026-09-23). `Ctrl+Backspace` did this already, but not by touch. */
    u8str_t parent = rubraview_path_dirname(dir);
    if (parent.len > 0 && !(parent.len == dir.len && memcmp(parent.ptr, dir.ptr, dir.len) == 0)) {
        proven_result_mem_mut_t up_res =
            rubraview_arena_alloc_array(app->arena, (listing.count + 1), sizeof(rubraview_fs_entry_t));
        if (proven_is_ok(up_res.err)) {
            rubraview_fs_entry_t *with_up = (rubraview_fs_entry_t*)(void*)up_res.value.ptr;
            with_up[0] = (rubraview_fs_entry_t){
                .name = U8(".."),
                .path = parent,
                .is_directory = true,
            };
            memcpy(with_up + 1, listing.entries, listing.count * sizeof(rubraview_fs_entry_t));
            listing.entries = with_up;
            listing.count += 1;
        }
    }

    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double tile = 160.0 * dpi;

    proven_result_mem_mut_t sel_res = rubraview_arena_alloc_array(app->arena, listing.count, sizeof(bool));
    app->picker_selected = proven_is_ok(sel_res.err) ? (bool*)(void*)sel_res.value.ptr : NULL;
    if (app->picker_selected) memset(app->picker_selected, 0, listing.count * sizeof(bool));

    rubraview_picker_mode_t mode = app->picker.mode;   /* the mode outlives the folder */
    app->picker_dir = dir;
    app->picker_listing = listing;
    app->picker = rubraview_picker_create(&app->picker_listing, tile,
                                          (double)win_h - (PICKER_CRUMB_HEIGHT + PICKER_ACTION_HEIGHT) * dpi,
                                          PICKER_COLUMNS);
    app->picker.selected = app->picker_selected;
    rubraview_picker_set_mode(&app->picker, mode);
    /* The focus starts on the first file: `..` and the folders are for
       going somewhere, and "Same type" reads the focused file. */
    for (size_t i = 0; i < listing.count; ++i) {
        if (!listing.entries[i].is_directory) { app->picker.focus = i; break; }
    }
}

/* The extension of the item the focus is on, ".jpg" and such. */
static u8str_t picker_focus_extension(const app_state_t *app) {
    u8str_t none = { .ptr = "", .len = 0 };
    if (app->picker.focus < app->picker_listing.count) {
        const rubraview_fs_entry_t *entry = &app->picker_listing.entries[app->picker.focus];
        if (!entry->is_directory) return rubraview_path_ext(entry->name);
    }
    /* The focus is on a folder: the first picked file answers instead. */
    for (size_t i = 0; i < app->picker_listing.count; ++i) {
        if (!app->picker_selected || !app->picker_selected[i]) continue;
        if (app->picker_listing.entries[i].is_directory) continue;
        return rubraview_path_ext(app->picker_listing.entries[i].name);
    }
    return none;
}

static void rename_extension_begin(app_state_t *app);
static void open_set_from_entries(app_state_t *app, rubraview_fs_entry_t *entries, size_t count, u8str_t said);

/* The picked files, gathered as a listing the page source can take. */
static size_t picker_picked_entries(app_state_t *app, rubraview_fs_entry_t **out) {
    size_t picked = 0;
    for (size_t i = 0; i < app->picker_listing.count; ++i) {
        if (app->picker_selected && app->picker_selected[i] && !app->picker_listing.entries[i].is_directory) picked++;
    }
    if (picked == 0) return 0;
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, picked, sizeof(rubraview_fs_entry_t));
    if (!proven_is_ok(res.err)) return 0;
    rubraview_fs_entry_t *entries = (rubraview_fs_entry_t*)(void*)res.value.ptr;
    size_t n = 0;
    for (size_t i = 0; i < app->picker_listing.count; ++i) {
        if (!app->picker_selected || !app->picker_selected[i]) continue;
        if (app->picker_listing.entries[i].is_directory) continue;
        entries[n++] = app->picker_listing.entries[i];
    }
    *out = entries;
    return n;
}

/* The picked files as a playlist of their own, then opened (owner,
   2026-09-23, who preferred this to opening them loose). The file is
   `PlaylistNNNN.m3u8` in this folder — the first number not taken. */
static void picker_make_playlist(app_state_t *app) {
    rubraview_fs_entry_t *entries = NULL;
    size_t n = picker_picked_entries(app, &entries);
    if (n == 0) { osd_say(app, U8("pick some files first")); return; }

    rubraview_playlist_t list = {0};
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, n, sizeof(rubraview_playlist_entry_t));
    if (!proven_is_ok(res.err)) return;
    list.entries = (rubraview_playlist_entry_t*)(void*)res.value.ptr;
    for (size_t i = 0; i < n; ++i) {
        list.entries[i] = (rubraview_playlist_entry_t){ .path = entries[i].name };   /* beside the file */
    }
    list.count = n;

    u8str_t text = rubraview_playlist_serialize_m3u8(app->arena, &list);
    if (text.len == 0) { osd_say(app, U8("could not write the playlist")); return; }

    char name[32];
    u8str_t target = { .ptr = "", .len = 0 };
    for (int number = 1; number <= 9999; ++number) {
        int written = snprintf(name, sizeof(name), "Playlist%04d.m3u8", number);
        if (written <= 0) return;
        u8str_t candidate = rubraview_path_join(app->arena, app->picker_dir,
                                                (u8str_t){ .ptr = name, .len = (size_t)written });
        rubraview_fs_entry_t taken;
        if (!rubraview_pal_fs_stat(app->arena, candidate, &taken)) { target = candidate; break; }
    }
    if (target.len == 0) { osd_say(app, U8("too many playlists in this folder")); return; }
    if (!rubraview_pal_fs_write_file(target, text)) { osd_say(app, U8("could not write the playlist")); return; }

    char line[96];
    int written = snprintf(line, sizeof(line), "%zu file(s) in %s", n, name);
    open_set_from_entries(app, entries, n, written > 0 ? (u8str_t){ .ptr = line, .len = (size_t)written }
                                                       : U8("playlist opened"));
}

/* The picked files to the recycle bin, once the button has been pressed
   twice (§3.18.1 asks before anything leaves the disk). */
static void picker_recycle(app_state_t *app) {
    rubraview_fs_entry_t *entries = NULL;
    size_t n = picker_picked_entries(app, &entries);
    if (n == 0) { osd_say(app, U8("pick some files first")); return; }

    double now = rubraview_pal_time_now_seconds();
    if (!rubraview_confirm_armed(&app->picker_confirm, PICKER_BTN_RECYCLE, now)) {
        rubraview_confirm_press(&app->picker_confirm, PICKER_BTN_RECYCLE, now);
        char line[96];
        int written = snprintf(line, sizeof(line), "press Recycle again to bin %zu file(s)", n);
        if (written > 0) osd_say(app, (u8str_t){ .ptr = line, .len = (size_t)written });
        return;
    }
    rubraview_confirm_clear(&app->picker_confirm);

    size_t done = 0;
    for (size_t i = 0; i < n; ++i) {
        if (!rubraview_pal_fs_recycle(entries[i].path)) continue;
        rubraview_undo_push(&app->undo, (rubraview_file_action_t){
            .op = RUBRAVIEW_FILE_OP_RECYCLE,
            .source_path = entries[i].path,
            .target_path = entries[i].path,
        });
        done++;
    }
    rubraview_undo_commit(&app->undo);
    char line[96];
    int written = snprintf(line, sizeof(line), "%zu file(s) in the recycle bin", done);
    if (written > 0) osd_say(app, (u8str_t){ .ptr = line, .len = (size_t)written });
    picker_navigate(app, app->picker_dir);
}

/* Move or copy the picked files into the curation folder a digit names. */
static void picker_curate(app_state_t *app, int32_t digit, bool moving) {
    u8str_t target_dir = rubraview_curation_target(&app->curation, digit);
    if (target_dir.len == 0) { osd_say(app, U8("no folder is bound to that number")); return; }
    rubraview_fs_entry_t *entries = NULL;
    size_t n = picker_picked_entries(app, &entries);
    if (n == 0) { osd_say(app, U8("pick some files first")); return; }

    rubraview_pal_fs_make_dirs(target_dir);
    size_t done = 0;
    for (size_t i = 0; i < n; ++i) {
        u8str_t target = rubraview_path_join(app->arena, target_dir, entries[i].name);
        if (target.len == 0) continue;
        bool ok = moving ? rubraview_pal_fs_move(entries[i].path, target)
                         : rubraview_pal_fs_copy(entries[i].path, target);
        if (!ok) continue;
        rubraview_undo_push(&app->undo, (rubraview_file_action_t){
            .op = moving ? RUBRAVIEW_FILE_OP_MOVE : RUBRAVIEW_FILE_OP_COPY,
            .source_path = entries[i].path,
            .target_path = target,
        });
        done++;
    }
    rubraview_undo_commit(&app->undo);
    char line[128];
    int written = snprintf(line, sizeof(line), "%zu file(s) %s %.*s", done, moving ? "moved to" : "copied to",
                           (int)target_dir.len, target_dir.ptr);
    if (written > 0) osd_say(app, (u8str_t){ .ptr = line, .len = (size_t)written });
    if (moving) picker_navigate(app, app->picker_dir);
}

/* One of the buttons along the bottom was pressed. */
static void picker_button(app_state_t *app, picker_button_t button) {
    char line[128];
    switch (button) {
        case PICKER_BTN_INDIVIDUAL:
            rubraview_picker_set_mode(&app->picker, app->picker.mode == RUBRAVIEW_PICK_INDIVIDUAL
                                                    ? RUBRAVIEW_PICK_SINGLE : RUBRAVIEW_PICK_INDIVIDUAL);
            osd_say(app, app->picker.mode == RUBRAVIEW_PICK_INDIVIDUAL
                         ? U8("tap files to pick them one by one") : U8("a tap opens again"));
            break;
        case PICKER_BTN_RANGE:
            rubraview_picker_set_mode(&app->picker, app->picker.mode == RUBRAVIEW_PICK_RANGE
                                                    ? RUBRAVIEW_PICK_SINGLE : RUBRAVIEW_PICK_RANGE);
            osd_say(app, app->picker.mode == RUBRAVIEW_PICK_RANGE
                         ? U8("tap two files: everything between them turns over") : U8("a tap opens again"));
            break;
        case PICKER_BTN_SAME_TYPE: {
            u8str_t ext = picker_focus_extension(app);
            if (ext.len == 0) { osd_say(app, U8("put the focus on a file first")); break; }
            if (app->picker.mode == RUBRAVIEW_PICK_SINGLE) {
                rubraview_picker_set_mode(&app->picker, RUBRAVIEW_PICK_INDIVIDUAL);
            }
            size_t n = rubraview_picker_select_extension(&app->picker, ext, true);
            int written = snprintf(line, sizeof(line), "%zu %.*s file(s) picked", n, (int)ext.len, ext.ptr);
            if (written > 0) osd_say(app, (u8str_t){ .ptr = line, .len = (size_t)written });
            break;
        }
        case PICKER_BTN_EXTENSION:
            rename_extension_begin(app);
            break;
        case PICKER_BTN_PLAYLIST:
            picker_make_playlist(app);
            break;
        case PICKER_BTN_RECYCLE:
            picker_recycle(app);
            break;
        case PICKER_BTN_MOVE:
        case PICKER_BTN_COPY:
            app->picker_pending = (int32_t)button;
            app->picker_has_pending = true;
            osd_say(app, button == PICKER_BTN_MOVE ? U8("press 1-9 for the folder to move them to")
                                                   : U8("press 1-9 for the folder to copy them to"));
            break;
        case PICKER_BTN_CLEAR:
            app->picker_has_pending = false;
            rubraview_confirm_clear(&app->picker_confirm);
            rubraview_picker_clear_selection(&app->picker);
            osd_say(app, U8("nothing picked"));
            break;
        default:
            break;
    }
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

static uint32_t boxes_when(const app_state_t *app) {
    return app->media ? RUBRAVIEW_BOXES_WHEN_MEDIA : 0u;
}

/* RFC-0002 §5: the menu tree, built again from the document — with the
   reading history's newest entries under File › Recent — and the menu
   back at its root. */
static unsigned char g_menu_memory[1 << 16];   /* the tree lives here until it is built again */

static void menu_rebuild(app_state_t *app) {
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = g_menu_memory, .size = sizeof(g_menu_memory) });
    enum { RECENT_MAX = 10 };
    rubraview_recent_entry_t recent[RECENT_MAX];
    size_t recent_count = 0;
    bool taken[HISTORY_MAX_ENTRIES] = {0};
    for (size_t r = 0; r < RECENT_MAX; ++r) {
        size_t best = app->history.count;
        for (size_t i = 0; i < app->history.count && i < HISTORY_MAX_ENTRIES; ++i) {
            if (taken[i]) continue;
            if (best == app->history.count || app->history.entries[i].timestamp > app->history.entries[best].timestamp) best = i;
        }
        if (best == app->history.count) break;
        taken[best] = true;
        char action[32];
        int n = snprintf(action, sizeof(action), "open_recent:%zu", best);
        proven_result_mem_mut_t text = proven_arena_alloc(&arena, (size_t)n + 1);
        if (n <= 0 || !proven_is_ok(text.err)) break;
        memcpy(text.value.ptr, action, (size_t)n + 1);
        recent[recent_count++] = (rubraview_recent_entry_t){
            .label = rubraview_path_basename(app->history.entries[best].path),
            .action = { .ptr = (const char*)text.value.ptr, .len = (size_t)n },
        };
    }
    app->menu_when = boxes_when(app);
    app->menu_tree = rubraview_boxes_menu(&arena, rubraview_boxes_document(), app->menu_when,
                                          recent, recent_count, MENU_MAX_TILES - 1);
    app->menu = rubraview_menu_create(&app->menu_tree);
    rubraview_confirm_clear(&app->menu_confirm);
    sync_menubox_tiles(app);
}

/* RFC-0002 §4: which toolbox profile what is on screen calls for — the
   same tests dispatch_key uses to pick a key context. */
static const char *toolbox_profile_name(const app_state_t *app) {
    if (app->media) return app->media_info.has_video ? "video" : "music";
    if (app->anim_active) return app->animation.kind == RUBRAVIEW_FRAMES_ANIMATION ? "animation" : "multipage";
    if (app->source.archive_path.len > 0) return "archive";
    return "image";
}

static void toolbox_refresh(app_state_t *app) {
    const rubraview_boxes_doc_t *doc = rubraview_boxes_document();
    int32_t count = 0;
    const rubraview_toolbox_profile_t *parts[2] = {
        app->slideshow_running ? rubraview_boxes_profile(doc, U8("slideshow")) : NULL,
        rubraview_boxes_profile(doc, cstr(toolbox_profile_name(app))),
    };
    for (size_t p = 0; p < 2; ++p) {
        if (!parts[p]) continue;
        for (int32_t i = 0; i < parts[p]->tile_count && count < RUBRAVIEW_TOOLBOX_MAX_TILES; ++i) {
            const rubraview_box_tile_t *tile = &doc->tiles[parts[p]->first_tile + i];
            /* The slide show's own Stop tile stands in for the profile's Slides tile. */
            if (p == 1 && parts[0] && rubraview_u8_eq_lit(tile->action, "toggle_slideshow")) continue;
            app->toolbox_tiles[count++] = *tile;
        }
    }
    app->toolbox_tile_count = count;
    app->toolbox.tile_count = count;
    app->toolbox.timeline = app->media != NULL;   /* the strip's seek bar, for a film or music */
    /* The menu follows what is on screen too, but only while it is at its
       root: a reader halfway down a submenu is not pulled back. */
    if (app->menu_when != boxes_when(app) && app->menu.depth == 0) menu_rebuild(app);
}

/* "1x", "1.25x", "0.5x": two decimals, trailing zeros dropped. */
static int speed_text(char *buffer, size_t size, double speed) {
    int n = snprintf(buffer, size, "%.2f", speed);
    if (n <= 0 || (size_t)n >= size) return 0;
    while (n > 0 && buffer[n - 1] == '0') buffer[--n] = '\0';
    if (n > 0 && buffer[n - 1] == '.') buffer[--n] = '\0';
    if ((size_t)n + 1 < size) { buffer[n++] = 'x'; buffer[n] = '\0'; }
    return n;
}

/* A toggle tile says what a tap will do; a tile that can do nothing now is dimmed. */
/* What a tile or a menu item can tell about its action: the facts of the
   page on screen, read the same way for both boxes. */
static rubraview_action_facts_t action_facts(const app_state_t *app) {
    return (rubraview_action_facts_t){
        .has_page = page_count(app) > 0,
        .archive_series = app->source.archive_path.len > 0,
        .media = app->media != NULL,
        .video = app->media != NULL && app->media_info.has_video,
        .frames = app->anim_active,
        .other_audio_track = rubraview_tracks_next(&app->tracks, RUBRAVIEW_TRACK_AUDIO, app->tracks.current_audio) >= 0 &&
                             rubraview_tracks_next(&app->tracks, RUBRAVIEW_TRACK_AUDIO, app->tracks.current_audio) != app->tracks.current_audio,
        .other_subtitle = rubraview_tracks_next(&app->tracks, RUBRAVIEW_TRACK_SUBTITLE, app->tracks.current_subtitle) >= 0,
        .subtitle_shown = app->tracks.current_subtitle >= 0,
        .slideshow = app->slideshow_running,
        .filmstrip = app->filmstrip.visible,
        .osd = app->osd.always_on,
        .toolbox_pinned = app->toolbox.pinned,
        .toolbox_detached = app->toolbox.state == RUBRAVIEW_BOX_DETACHED,
        .fullscreen = rubraview_pal_window_is_fullscreen(app->window),
        .nearest = app->force_nearest,
        .pixel_grid = app->pixel_grid,
        .spread_detect = app->spread_detect,
        .fit_lock = app->fit_lock,
        .always_on_top = rubraview_settings_get(&app->settings, U8("general"), U8("always_on_top")) > 0.5,
        .muted = rubraview_settings_get(&app->settings, U8("audio"), U8("mute")) > 0.5,
        .layout = app->layout_opts.mode,
        .fit = app->fit_mode,
        .rtl = app->layout_opts.direction == RUBRAVIEW_READING_RTL,
        .playing = app->media ? !app->media_paused : (app->anim_active && !app->animation.paused),
    };
}

static u8str_t toolbox_caption(const app_state_t *app, const rubraview_box_tile_t *tile, bool *out_enabled) {
    rubraview_action_facts_t facts = action_facts(app);
    rubraview_action_state_t st = rubraview_action_state(tile->action, &facts);
    *out_enabled = st.enabled;
    if (st.value) {
        /* A setting the tile names, "Order: R>L": drawn before the next call. */
        static char named[48];
        return rubraview_tile_caption(tile->caption, false, st.mark, st.value, named, sizeof(named));
    }
    if (rubraview_u8_eq_lit(tile->action, "media_play_pause")) {
        bool paused = app->media ? app->media_paused : app->animation.paused;
        return paused ? U8("Play") : U8("Pause");
    }
    if (rubraview_u8_eq_lit(tile->action, "media_mute")) {
        return rubraview_settings_get(&app->settings, U8("audio"), U8("mute")) > 0.5 ? U8("Unmute") : U8("Mute");
    }
    if (rubraview_u8_eq_lit(tile->action, "media_speed_cycle")) {
        static char speed[16];   /* drawn before the next call */
        int n = speed_text(speed, sizeof(speed), app->media_speed > 0.0 ? app->media_speed : 1.0);
        return n > 0 ? (u8str_t){ .ptr = speed, .len = (size_t)n } : tile->caption;
    }
    if (rubraview_u8_eq_lit(tile->action, "media_ab_cycle")) {
        return app->ab_a < 0.0 ? U8("A-B") : app->ab_b < 0.0 ? U8("Set B") : U8("A-B off");
    }
    return tile->caption;
}

/* The volume as the OSD says it: "volume 70%" or "muted (70%)". */
static void media_say_volume(app_state_t *app) {
    char line[48];
    int volume = (int)rubraview_settings_get(&app->settings, U8("audio"), U8("volume"));
    bool muted = rubraview_settings_get(&app->settings, U8("audio"), U8("mute")) > 0.5;
    int n = muted ? snprintf(line, sizeof(line), "muted (%d%%)", volume)
                  : snprintf(line, sizeof(line), "volume %d%%", volume);
    if (n > 0) osd_say(app, (u8str_t){ .ptr = line, .len = (size_t)n });
}

/* D-16: one step of moving or sizing the window from the keyboard. The
   frame stays on a screen (the PAL pulls it back) and never smaller than
   a usable minimum; a fullscreen window is left alone. */
static bool window_nudge(app_state_t *app, u8str_t action) {
    static const struct { const char *name; int dx, dy, dw, dh; } STEPS[] = {
        { "window_move_left", -1, 0, 0, 0 }, { "window_move_right", 1, 0, 0, 0 },
        { "window_move_up", 0, -1, 0, 0 },   { "window_move_down", 0, 1, 0, 0 },
        { "window_narrower", 0, 0, -1, 0 },  { "window_wider", 0, 0, 1, 0 },
        { "window_shorter", 0, 0, 0, -1 },   { "window_taller", 0, 0, 0, 1 },
    };
    for (size_t i = 0; i < sizeof(STEPS) / sizeof(STEPS[0]); ++i) {
        if (!rubraview_u8_eq_lit(action, STEPS[i].name)) continue;
        if (rubraview_pal_window_is_fullscreen(app->window)) return true;
        int32_t x = 0, y = 0, w = 0, h = 0;
        if (!rubraview_pal_window_get_frame(app->window, &x, &y, &w, &h)) return true;
        int32_t step = (int32_t)(40.0 * rubraview_pal_window_dpi_scale(app->window));
        int32_t min_w = (int32_t)(320.0 * rubraview_pal_window_dpi_scale(app->window));
        int32_t min_h = (int32_t)(240.0 * rubraview_pal_window_dpi_scale(app->window));
        x += STEPS[i].dx * step;
        y += STEPS[i].dy * step;
        w += STEPS[i].dw * step;
        h += STEPS[i].dh * step;
        if (w < min_w) w = min_w;
        if (h < min_h) h = min_h;
        rubraview_pal_window_set_frame(app->window, x, y, w, h);
        return true;
    }
    return false;
}

static void handle_action(app_state_t *app, u8str_t action) {
    if (action.len == 0) return;
    note_activity(app);

    if (rubraview_u8_eq_lit(action, "quit")) {
        /* Esc closes what is open before it closes the program. */
        if (app->settings_open) { settings_close(app); return; }
        if (app->panel.open) { panel_close(app); return; }
        rubraview_pal_window_request_close(app->window);
    } else if (rubraview_u8_eq_lit(action, "next_page")) {
        next_spread(app);
    } else if (rubraview_u8_eq_lit(action, "prev_page")) {
        prev_spread(app);
    } else if (rubraview_u8_eq_lit(action, "first_page")) {
        go_to_spread(app, 0);
    } else if (rubraview_u8_eq_lit(action, "last_page")) {
        go_to_spread(app, app->layout.count > 0 ? app->layout.count - 1 : 0);
    } else if (rubraview_u8_eq_lit(action, "fit_window")) {
        app->fit_mode = RUBRAVIEW_FIT_WINDOW; reset_view(app);
    } else if (rubraview_u8_eq_lit(action, "fit_width")) {
        app->fit_mode = RUBRAVIEW_FIT_WIDTH; reset_view(app);
    } else if (rubraview_u8_eq_lit(action, "fit_height")) {
        app->fit_mode = RUBRAVIEW_FIT_HEIGHT; reset_view(app);
    } else if (rubraview_u8_eq_lit(action, "actual_size")) {
        app->fit_mode = RUBRAVIEW_FIT_ACTUAL_SIZE; reset_view(app);
    } else if (rubraview_u8_eq_lit(action, "smart_fit")) {
        app->fit_mode = RUBRAVIEW_FIT_SMART; reset_view(app);
    } else if (rubraview_u8_eq_lit(action, "fit_stretch")) {
        app->fit_mode = RUBRAVIEW_FIT_STRETCH; reset_view(app);
    } else if (rubraview_u8_eq_lit(action, "toggle_fit_lock")) {
        app->fit_lock = !app->fit_lock;
    } else if (rubraview_u8_eq_lit(action, "skip_forward")) {
        go_to_spread(app, app->spread_index + 10);
    } else if (rubraview_u8_eq_lit(action, "skip_backward")) {
        go_to_spread(app, app->spread_index > 10 ? app->spread_index - 10 : 0);
    } else if (rubraview_u8_eq_lit(action, "up_to_folder")) {
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
    } else if (rubraview_u8_eq_lit(action, "open_folder")) {
        picker_open(app);
    } else if (rubraview_u8_eq_lit(action, "toggle_spread_detect")) {
        /* §3.3.4: with detection off, a wide scan pairs like any other
           page instead of standing alone. */
        app->spread_detect = !app->spread_detect;
        app->layout_opts.spread_ar_threshold = app->spread_detect ? 1.15 : 1.0e9;
        app->needs_relayout = true;
    } else if (rubraview_u8_eq_lit(action, "interval_up")) {
        rubraview_slideshow_set_interval(&app->slideshow, app->slideshow.interval_seconds + 0.5);
    } else if (rubraview_u8_eq_lit(action, "interval_down")) {
        rubraview_slideshow_set_interval(&app->slideshow, app->slideshow.interval_seconds - 0.5);
    } else if (rubraview_u8_eq_lit(action, "interval_up_fine")) {
        rubraview_slideshow_set_interval(&app->slideshow, app->slideshow.interval_seconds + 0.1);
    } else if (rubraview_u8_eq_lit(action, "interval_down_fine")) {
        rubraview_slideshow_set_interval(&app->slideshow, app->slideshow.interval_seconds - 0.1);
    } else if (rubraview_u8_eq_lit(action, "zoom_in")) {
        app->zoom *= ZOOM_STEP;
    } else if (rubraview_u8_eq_lit(action, "zoom_out")) {
        app->zoom /= ZOOM_STEP;
        if (app->zoom < 0.01) app->zoom = 0.01;
    } else if (rubraview_u8_eq_lit(action, "rotate_cw")) {
        app->orientation = rubraview_orientation_rotate_cw(app->orientation);
        app->needs_relayout = true;
        reset_view(app);
    } else if (rubraview_u8_eq_lit(action, "rotate_ccw")) {
        app->orientation = rubraview_orientation_rotate_ccw(app->orientation);
        app->needs_relayout = true;
        reset_view(app);
    } else if (rubraview_u8_eq_lit(action, "flip_horizontal")) {
        app->orientation = rubraview_orientation_flip_h(app->orientation);
    } else if (rubraview_u8_eq_lit(action, "flip_vertical")) {
        app->orientation = rubraview_orientation_flip_v(app->orientation);
    } else if (rubraview_u8_eq_lit(action, "toggle_pixel_grid")) {
        app->pixel_grid = !app->pixel_grid;
    } else if (rubraview_u8_eq_lit(action, "toggle_nearest")) {
        app->force_nearest = !app->force_nearest;
    } else if (rubraview_u8_eq_lit(action, "toggle_osd")) {
        app->osd.always_on = !app->osd.always_on;
    } else if (rubraview_u8_eq_lit(action, "toggle_filmstrip")) {
        app->filmstrip.visible = !app->filmstrip.visible;
    } else if (rubraview_u8_eq_lit(action, "toggle_menu")) {
        rubraview_box_click_anchor(&app->menubox);
        menu_rebuild(app);
        sync_menubox_tiles(app);
    } else if (rubraview_u8_eq_lit(action, "layout_single")) {
        app->layout_opts.mode = RUBRAVIEW_PAGE_LAYOUT_SINGLE;
        app->needs_relayout = true;
        reset_view(app);
    } else if (rubraview_u8_eq_lit(action, "layout_dual")) {
        app->layout_opts.mode = RUBRAVIEW_PAGE_LAYOUT_DUAL;
        app->needs_relayout = true;
        reset_view(app);
    } else if (rubraview_u8_eq_lit(action, "layout_book")) {
        app->layout_opts.mode = RUBRAVIEW_PAGE_LAYOUT_BOOK;
        app->needs_relayout = true;
        reset_view(app);
    } else if (rubraview_u8_eq_lit(action, "toggle_toolbox")) {
        /* A toolbox that is its own window is put back, not shown twice. */
        if (app->toolbox.state == RUBRAVIEW_BOX_DETACHED) handle_action(app, U8("toggle_toolbox_detach"));
        else rubraview_box_click_anchor(&app->toolbox);
    } else if (rubraview_u8_eq_lit(action, "open_picker")) {
        if (app->picker_open) {
            app->picker_open = false;
        } else {
            picker_open(app);
        }
    } else if (rubraview_u8_eq_lit(action, "delete_file")) {
        triage_delete(app, false);
    } else if (rubraview_u8_eq_lit(action, "purge_file")) {
        /* §3.18.1: a permanent delete asks first, every time. */
        app->confirm_purge = true;
    } else if (rubraview_u8_eq_lit(action, "undo")) {
        triage_undo(app);
    } else if (rubraview_u8_eq_lit(action, "rename_file")) {
        rename_begin(app);
    } else if (rubraview_u8_eq_lit(action, "open_settings")) {
        if (app->settings_open) settings_close(app);
        else settings_open(app);
    } else if (rubraview_u8_eq_lit(action, "open_edit")) {
        if (app->panel.open && !app->panel_is_export && !app->panel_is_batch) panel_close(app);
        else panel_open_edit(app);
    } else if (rubraview_u8_eq_lit(action, "quick_export") || rubraview_u8_eq_lit(action, "save_as")) {
        if (app->panel.open && app->panel_is_export) panel_close(app);
        else panel_open_export(app);
    } else if (rubraview_u8_eq_lit(action, "open_batch")) {
        if (app->panel.open && app->panel_is_batch) panel_close(app);
        else panel_open_batch(app);
    } else if (app->media && (rubraview_u8_eq_lit(action, "media_play_pause") || rubraview_u8_eq_lit(action, "anim_toggle_pause"))) {
        media_toggle_pause(app);   /* anim_toggle_pause: the name before D-16, in keymap.ini files saved earlier */
    } else if (app->media && rubraview_u8_eq_lit(action, "media_stop")) {
        /* Stop: back to the start and paused, the first frame on screen. */
        if (!app->media_paused) media_toggle_pause(app);
        media_seek_to(app, 0.0);
        osd_say(app, U8("stopped"));
    } else if (rubraview_u8_eq_lit(action, "media_volume_up") || rubraview_u8_eq_lit(action, "media_volume_down")) {
        double volume = rubraview_settings_get(&app->settings, U8("audio"), U8("volume"));
        rubraview_settings_set(&app->settings, U8("audio"), U8("volume"),
                               volume + (rubraview_u8_eq_lit(action, "media_volume_up") ? 5.0 : -5.0));
        rubraview_settings_set(&app->settings, U8("audio"), U8("mute"), 0.0);   /* turning it up or down unmutes */
        settings_took_effect(app);
        media_say_volume(app);
    } else if (rubraview_u8_eq_lit(action, "media_mute")) {
        bool muted = rubraview_settings_get(&app->settings, U8("audio"), U8("mute")) > 0.5;
        rubraview_settings_set(&app->settings, U8("audio"), U8("mute"), muted ? 0.0 : 1.0);
        settings_took_effect(app);
        media_say_volume(app);
    } else if (action.len > 12 && memcmp(action.ptr, "open_recent:", 12) == 0) {
        /* File › Recent: the entry's index in the reading history. */
        /* Read inside the view: an action is a slice, not a C string. */
        int64_t index = -1;
        if (rubraview_parse_i64((u8str_t){ .ptr = action.ptr + 12, .len = action.len - 12 }, &index) &&
            index >= 0 && (size_t)index < app->history.count) {
            open_path(app, app->history.entries[index].path);
        }
    } else if (rubraview_u8_eq_lit(action, "toggle_always_on_top")) {
        bool on = rubraview_settings_get(&app->settings, U8("general"), U8("always_on_top")) > 0.5;
        rubraview_settings_set(&app->settings, U8("general"), U8("always_on_top"), on ? 0.0 : 1.0);
        settings_took_effect(app);
        osd_say(app, on ? U8("no longer on top of other windows") : U8("always on top of other windows"));
    } else if (rubraview_u8_eq_lit(action, "toggle_toolbox_pin")) {
        rubraview_box_set_pinned(&app->toolbox, !app->toolbox.pinned);
        osd_say(app, app->toolbox.pinned ? U8("toolbox pinned open") : U8("toolbox unpinned"));
    } else if (rubraview_u8_eq_lit(action, "toggle_toolbox_detach")) {
        if (app->toolbox.state == RUBRAVIEW_BOX_DETACHED) {
            int32_t w = 0, h = 0;
            rubraview_pal_window_get_size(app->window, &w, &h);
            double dpi = rubraview_pal_window_dpi_scale(app->window);
            toolbox_dock(app, (double)w - 220.0 * dpi, (double)h - 160.0 * dpi);
        } else {
            int32_t fx = 0, fy = 0, fw = 0, fh = 0;
            rubraview_pal_window_get_frame(app->window, &fx, &fy, &fw, &fh);
            app->toolbox.state = RUBRAVIEW_BOX_DETACHED;
            toolbox_detach(app, fx + fw - (int32_t)(360.0 * rubraview_pal_window_dpi_scale(app->window)),
                           fy + fh / 2, false);
        }
    } else if (rubraview_u8_eq_lit(action, "open_keys")) {
        settings_open(app);
        if (app->settings_open) {
            rubraview_settings_view_set_page(&app->settings_view, RUBRAVIEW_TAB_KEYS);
            rubraview_settings_view_set_table_rows(&app->settings_view, (int32_t)app->keymap.count + 1);
            app->settings_dirty = true;
        }
    } else if (rubraview_u8_eq_lit(action, "toggle_help")) {
        help_show(app);
    } else if (rubraview_u8_eq_lit(action, "toggle_miniplayer")) {
        mini_show(app);
    } else if (rubraview_u8_eq_lit(action, "about")) {
        char line[160];
        int n = snprintf(line, sizeof(line), "Rubraview %s  -  FFmpeg %s", RUBRAVIEW_VERSION_STRING,
                         rubraview_pal_media_backend_available(RUBRAVIEW_BACKEND_FFMPEG) ? "found beside the program" : "not found");
        if (n > 0) osd_say(app, (u8str_t){ .ptr = line, .len = (size_t)n });
    } else if (rubraview_u8_eq_lit(action, "boxes_opacity_100") || rubraview_u8_eq_lit(action, "boxes_opacity_80") ||
               rubraview_u8_eq_lit(action, "boxes_opacity_60") || rubraview_u8_eq_lit(action, "boxes_opacity_40")) {
        double percent = 100.0;
        if (!rubraview_parse_double((u8str_t){ .ptr = action.ptr + 14, .len = action.len - 14 }, &percent)) percent = 100.0;
        rubraview_settings_set(&app->settings, U8("ui"), U8("menubox_opacity"), percent);
        rubraview_settings_set(&app->settings, U8("ui"), U8("toolbox_opacity"), percent);
        char line[32];
        int n = snprintf(line, sizeof(line), "both boxes %d%%", (int)percent);
        if (n > 0) osd_say(app, (u8str_t){ .ptr = line, .len = (size_t)n });
    } else if (window_nudge(app, action)) {
        /* D-16: Ctrl + arrows size the window, Alt + arrows move it. */
    } else if (app->media && rubraview_u8_eq_lit(action, "anim_step_forward")) {
        media_step(app, true);
    } else if (app->media && rubraview_u8_eq_lit(action, "anim_step_back")) {
        media_step(app, false);
    } else if (app->media && rubraview_u8_eq_lit(action, "media_seek_forward")) {
        media_seek_to(app, app->media_position + MEDIA_SEEK_STEP);
    } else if (app->media && rubraview_u8_eq_lit(action, "media_seek_back")) {
        media_seek_to(app, app->media_position - MEDIA_SEEK_STEP);
    } else if (app->media && rubraview_u8_eq_lit(action, "next_audio_track")) {
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
    } else if (app->media && rubraview_u8_eq_lit(action, "next_subtitle_track")) {
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
    } else if (app->media && (rubraview_u8_eq_lit(action, "subtitle_earlier") || rubraview_u8_eq_lit(action, "subtitle_later"))) {
        /* §3.16.1: half a second at a time, and the OSD says where the
           track now sits so the reader can aim. */
        /* A DVD's or a Blu-ray's pictures are subtitles too, and they are
           moved by the same offset (found while measuring T081). */
        if (app->subtitle.count == 0 && app->vobsub.count == 0 && app->pgs.count == 0) {
            osd_say(app, U8("no subtitles are showing"));
        } else {
            rubraview_subtitle_nudge(&app->subtitle, rubraview_u8_eq_lit(action, "subtitle_later"));
            if (app->media_page >= 0 && (size_t)app->media_page < page_count(app)) {
                app->subtitle_offset_for = app->source.pages[app->media_page].path;
                app->subtitle_offset_seconds = app->subtitle.offset_seconds;
            }
            char line[96];
            int n = snprintf(line, sizeof(line), "subtitles %+.1f s", app->subtitle.offset_seconds);
            if (n > 0) osd_say(app, (u8str_t){ .ptr = line, .len = (size_t)n });
        }
    } else if (!app->media && app->bgm_media && rubraview_u8_eq_lit(action, "media_play_pause")) {
        /* §3.14.6: with a picture on screen and music behind it, the
           play key is the music's — and the listener's own pause
           outranks the arbiter from then on. */
        bool pausing = !app->bgm_paused;
        rubraview_pal_media_set_paused(app->bgm_media, pausing);
        app->bgm_paused = pausing;
        (void)rubraview_bgm_event(&app->bgm,
                                  pausing ? RUBRAVIEW_BGM_READER_PAUSED : RUBRAVIEW_BGM_READER_RESUMED,
                                  bgm_pause_for_sound(app));
        osd_say(app, pausing ? U8("the music is paused") : U8("the music plays on"));
    } else if (rubraview_u8_eq_lit(action, "anim_toggle_pause") || rubraview_u8_eq_lit(action, "media_play_pause")) {
        if (app->animation.paused) rubraview_animation_resume(&app->animation);
        else rubraview_animation_pause(&app->animation);
    } else if (rubraview_u8_eq_lit(action, "anim_step_forward") || rubraview_u8_eq_lit(action, "subpage_next")) {
        rubraview_animation_step(&app->animation, true);
        show_frame(app, app->animation.current);
    } else if (rubraview_u8_eq_lit(action, "anim_step_back") || rubraview_u8_eq_lit(action, "subpage_prev")) {
        rubraview_animation_step(&app->animation, false);
        show_frame(app, app->animation.current);
    } else if (app->media && (rubraview_u8_eq_lit(action, "anim_speed_up") || rubraview_u8_eq_lit(action, "anim_speed_down") ||
                              rubraview_u8_eq_lit(action, "media_speed_up") || rubraview_u8_eq_lit(action, "media_speed_down") ||
                              rubraview_u8_eq_lit(action, "media_speed_reset") || rubraview_u8_eq_lit(action, "media_speed_cycle"))) {
        /* D-15: 0.25x to 4x a quarter at a time; the tile cycles the usual ones. */
        double speed = app->media_speed > 0.0 ? app->media_speed : 1.0;
        if (rubraview_u8_eq_lit(action, "media_speed_reset")) {
            speed = 1.0;
        } else if (rubraview_u8_eq_lit(action, "media_speed_cycle")) {
            static const double CYCLE[] = { 1.0, 1.25, 1.5, 2.0, 0.5, 0.75 };
            size_t next = 0;
            for (size_t i = 0; i < 6; ++i) if (fabs(CYCLE[i] - speed) < 1e-6) next = (i + 1) % 6;
            speed = CYCLE[next];
        } else {
            bool up = rubraview_u8_eq_lit(action, "anim_speed_up") || rubraview_u8_eq_lit(action, "media_speed_up");
            speed += up ? 0.25 : -0.25;
        }
        if (speed < 0.25) speed = 0.25;
        if (speed > 4.0) speed = 4.0;
        app->media_speed = speed;
        rubraview_media_clock_set_rate(&app->media_clock, speed, rubraview_pal_time_now_seconds());
        rubraview_pal_audio_set_speed(speed);
        char line[32];
        char text[16];
        speed_text(text, sizeof(text), speed);
        int n = snprintf(line, sizeof(line), "speed %s", text);
        if (n > 0) osd_say(app, (u8str_t){ .ptr = line, .len = (size_t)n });
    } else if (rubraview_u8_eq_lit(action, "media_ab_edit")) {
        ab_edit_begin(app);
    } else if (app->media && (rubraview_u8_eq_lit(action, "media_ab_a") || rubraview_u8_eq_lit(action, "media_ab_b") ||
                              rubraview_u8_eq_lit(action, "media_ab_clear") || rubraview_u8_eq_lit(action, "media_ab_cycle"))) {
        /* D-15 A-B repeat. The tile walks A, then B, then off. */
        char line[64];
        int n = 0;
        bool set_a = rubraview_u8_eq_lit(action, "media_ab_a") || (rubraview_u8_eq_lit(action, "media_ab_cycle") && app->ab_a < 0.0);
        bool set_b = rubraview_u8_eq_lit(action, "media_ab_b") || (rubraview_u8_eq_lit(action, "media_ab_cycle") && app->ab_a >= 0.0 && app->ab_b < 0.0);
        if (set_a) {
            app->ab_a = app->media_position;
            if (app->ab_b >= 0.0 && app->ab_b <= app->ab_a) app->ab_b = -1.0;
            n = snprintf(line, sizeof(line), "repeat from %.1f s", app->ab_a);
        } else if (set_b && app->ab_a >= 0.0 && app->media_position > app->ab_a) {
            app->ab_b = app->media_position;
            n = snprintf(line, sizeof(line), "repeating %.1f - %.1f s", app->ab_a, app->ab_b);
        } else if (set_b) {
            n = snprintf(line, sizeof(line), "B must come after A");
        } else {
            app->ab_a = app->ab_b = -1.0;
            n = snprintf(line, sizeof(line), "repeat off");
        }
        if (n > 0) osd_say(app, (u8str_t){ .ptr = line, .len = (size_t)n });
    } else if (rubraview_u8_eq_lit(action, "anim_speed_up")) {
        app->animation.speed = rubraview_animation_step_speed(app->animation.speed, true);
    } else if (rubraview_u8_eq_lit(action, "anim_speed_down")) {
        app->animation.speed = rubraview_animation_step_speed(app->animation.speed, false);
    } else if (rubraview_u8_eq_lit(action, "next_archive")) {
        open_sibling_archive(app, true);
    } else if (rubraview_u8_eq_lit(action, "prev_archive")) {
        open_sibling_archive(app, false);
    } else if (rubraview_u8_eq_lit(action, "resume_accept")) {
        /* §3.17.1: the prompt is answered by opening the remembered page. */
        if (app->resume_offer) {
            app->resume_offer = false;
            size_t target = spread_index_for_page(app, app->resume_page);
            go_to_spread(app, target);
            update_precache(app);
        }
    } else if (rubraview_u8_eq_lit(action, "toggle_slideshow")) {
        toggle_slideshow(app);
    } else if (rubraview_u8_eq_lit(action, "toggle_fullscreen")) {
        rubraview_pal_window_set_fullscreen(app->window,
                                            !rubraview_pal_window_is_fullscreen(app->window));
    } else if (rubraview_u8_eq_lit(action, "toggle_layout")) {
        int32_t page = current_page_index(app);
        app->layout_opts.mode = (app->layout_opts.mode == RUBRAVIEW_PAGE_LAYOUT_SINGLE)
            ? RUBRAVIEW_PAGE_LAYOUT_DUAL
            : (app->layout_opts.mode == RUBRAVIEW_PAGE_LAYOUT_DUAL
                ? RUBRAVIEW_PAGE_LAYOUT_BOOK
                : RUBRAVIEW_PAGE_LAYOUT_SINGLE);
        rebuild_layout(app);
        if (page >= 0) app->spread_index = spread_index_for_page(app, page);
        reset_view(app);
    } else if (rubraview_u8_eq_lit(action, "toggle_reading_order")) {
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
        if (app->picker_has_pending && c >= '1' && c <= '9') {
            /* A number with no folder behind it leaves the picker waiting
               rather than letting the next one fall through to §3.18.3's
               own meaning, which would move the page on screen. */
            if (rubraview_curation_target(&app->curation, c - '0').len == 0) {
                osd_say(app, U8("no folder is bound to that number - Settings > Files"));
                return true;
            }
            app->picker_has_pending = false;
            picker_curate(app, c - '0', app->picker_pending == (int32_t)PICKER_BTN_MOVE);
            return true;
        }
        if (app->picker_has_pending && (c == 'A' || c == 'Z')) {
            app->picker_has_pending = false;   /* any letter gives up waiting */
        }
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
        if (key_is(combo, "Escape")) { rename_end(app); return true; }
        if (key_is(combo, "Backspace")) {
            /* A whole character, not one byte: half a Hangul syllable
               would leave the name unwritable. (Inside a syllable the
               IME takes Backspace itself and it never comes here.) */
            rubraview_rename_backspace(app->rename_buffer, &app->rename_length);
            return true;
        }
        /* The characters themselves arrive as text (rename_text), in
           either case and from the IME; the keys that make them are
           swallowed here, as is everything else while renaming. */
        return true;
    }

    /* §3.18.3: 1-9 curate — but only where a folder is actually bound.
       §3.7.2 gives 1-5 to the fit modes, and an unconfigured viewer must
       keep them; a reader who has set up triage folders has said which
       meaning they want. */
    if (combo.modifiers == RUBRAVIEW_MOD_NONE && combo.key_name.len == 1 && !app->picker_open) {
        /* Not while the picker is open: there a number answers its own
           "Move to" or "Copy to", and curating the page behind it would
           move a file the reader is not looking at (2026-09-23). */
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

/* ---- A-B by hand (owner, 2026-09-24) ---- */

/* One point as text, the way the timeline writes it. An unset point is
   an empty field rather than a nought: nought is a time. */
static void ab_edit_fill(app_state_t *app, int32_t field, double seconds) {
    char buffer[32];
    if (seconds < 0.0) {
        app->ab_edit_length[field] = 0;
        app->ab_edit_text[field][0] = '\0';
        return;
    }
    u8str_t text = rubraview_format_timecode(buffer, sizeof(buffer), seconds, true);
    size_t n = text.len < sizeof(app->ab_edit_text[0]) - 1 ? text.len : sizeof(app->ab_edit_text[0]) - 1;
    memcpy(app->ab_edit_text[field], text.ptr, n);
    app->ab_edit_text[field][n] = '\0';
    app->ab_edit_length[field] = n;
}

static void ab_edit_begin(app_state_t *app) {
    if (!app->media) { osd_say(app, U8("nothing is playing")); return; }
    app->ab_edit_open = true;
    app->ab_edit_field = 0;
    ab_edit_fill(app, 0, app->ab_a);
    ab_edit_fill(app, 1, app->ab_b);
    rubraview_pal_window_text_input(app->window, true);
}

static void ab_edit_close(app_state_t *app) {
    app->ab_edit_open = false;
    rubraview_pal_window_text_input(app->window, false);
}

/* What was typed becomes the points — or says why it cannot, and keeps
   the box open so the reader can fix it rather than start again. */
static bool ab_edit_commit(app_state_t *app) {
    double a = -1.0, b = -1.0;
    for (int32_t field = 0; field < 2; ++field) {
        u8str_t text = { .ptr = app->ab_edit_text[field], .len = app->ab_edit_length[field] };
        double *into = field == 0 ? &a : &b;
        if (text.len == 0) continue;            /* left empty: that point is unset */
        if (!rubraview_parse_timecode(text, into)) {
            osd_say(app, field == 0 ? U8("A is not a time — try 1:23.5")
                                    : U8("B is not a time — try 1:23.5"));
            app->ab_edit_field = field;
            return false;
        }
    }
    if (a >= 0.0 && b >= 0.0 && b <= a) {
        osd_say(app, U8("B has to come after A"));
        app->ab_edit_field = 1;
        return false;
    }
    app->ab_a = a;
    app->ab_b = b;
    char line[96];
    int n = a < 0.0 ? snprintf(line, sizeof(line), "repeat off")
          : b < 0.0 ? snprintf(line, sizeof(line), "repeat from %.3f s", a)
                    : snprintf(line, sizeof(line), "repeating %.3f - %.3f s", a, b);
    if (n > 0) osd_say(app, (u8str_t){ .ptr = line, .len = (size_t)n });
    return true;
}

/* The keys the box takes. Everything else is swallowed while it is open:
   a box that is taking a number must not also turn pages. */
static bool ab_edit_handle_key(app_state_t *app, rubraview_key_combo_t combo) {
    if (!app->ab_edit_open) return false;
    if (key_is(combo, "Escape")) { ab_edit_close(app); return true; }
    if (key_is(combo, "Enter")) {
        if (ab_edit_commit(app)) ab_edit_close(app);
        return true;
    }
    if (key_is(combo, "Tab") || key_is(combo, "Up") || key_is(combo, "Down")) {
        app->ab_edit_field = app->ab_edit_field == 0 ? 1 : 0;
        return true;
    }
    if (key_is(combo, "Backspace")) {
        int32_t f = app->ab_edit_field;
        if (app->ab_edit_length[f] > 0) app->ab_edit_text[f][--app->ab_edit_length[f]] = '\0';
        return true;
    }
    /* The crossing-over the owner asked for: the keys that set a point
       put the playhead into the field in hand, as a number, and it can
       then be typed over. */
    if (key_is(combo, "BracketLeft") || key_is(combo, "BracketRight")) {
        int32_t f = key_is(combo, "BracketLeft") ? 0 : 1;
        app->ab_edit_field = f;
        ab_edit_fill(app, f, app->media_position);
        return true;
    }
    if (key_is(combo, "Backslash")) {
        app->ab_edit_length[0] = app->ab_edit_length[1] = 0;
        app->ab_edit_text[0][0] = app->ab_edit_text[1][0] = '\0';
        return true;
    }
    return true;   /* the digits arrive as text; the rest is swallowed */
}

/* The characters themselves: only what a time is made of. */
static void ab_edit_text_typed(app_state_t *app, u8str_t text) {
    int32_t f = app->ab_edit_field;
    for (size_t i = 0; i < text.len; ++i) {
        char c = text.ptr[i];
        bool wanted = (c >= '0' && c <= '9') || c == ':' || c == '.' || c == ',';
        if (!wanted) continue;
        if (app->ab_edit_length[f] + 1 >= sizeof(app->ab_edit_text[0])) return;
        app->ab_edit_text[f][app->ab_edit_length[f]++] = c;
        app->ab_edit_text[f][app->ab_edit_length[f]] = '\0';
    }
}

/* ---- §3.14.6: the mini player ---- */

#define MINI_BACKGROUND 0xFF181818u
#define MINI_WIDTH  320
#define MINI_HEIGHT 80

/* Which player it speaks to: the background music if there is any,
   otherwise the track the page itself is playing. */
static rubraview_media_t *mini_target(const app_state_t *app) {
    if (app->bgm_media) return app->bgm_media;
    if (app->media && !app->media_info.has_video) return app->media;
    return NULL;
}

static const rubraview_tags_t *mini_tags(const app_state_t *app) {
    return app->bgm_media ? &app->bgm_tags : &app->music_tags;
}

static bool mini_paused(const app_state_t *app) {
    return app->bgm_media ? app->bgm_paused : app->media_paused;
}

static double mini_position(const app_state_t *app) {
    return app->bgm_media ? app->bgm_position : app->media_position;
}

static double mini_duration(const app_state_t *app) {
    return app->bgm_media ? app->bgm_info.duration_seconds : app->media_info.duration_seconds;
}

/* The page of the track being heard, or -1 when it has none on screen. */
static int32_t mini_page(const app_state_t *app) {
    return app->bgm_media ? app->bgm_page : app->media_page;
}

static void mini_close(app_state_t *app) {
    if (!app->mini_open) return;
    rubraview_pal_window_get_frame(app->mini_window, &app->mini_frame[0], &app->mini_frame[1],
                                   &app->mini_frame[2], &app->mini_frame[3]);
    if (app->mini_cover) rubraview_pal_texture_destroy(app->mini_cover);
    if (app->mini_renderer) rubraview_pal_render_destroy(app->mini_renderer);
    if (app->mini_window) rubraview_pal_window_destroy(app->mini_window);
    app->mini_cover = NULL;
    app->mini_renderer = NULL;
    app->mini_window = NULL;
    app->mini_open = false;
}

/* The cover belongs to the renderer that draws it, so the small window
   decodes its own copy from the same bytes. */
static void mini_cover_load(app_state_t *app) {
    if (app->mini_cover) rubraview_pal_texture_destroy(app->mini_cover);
    app->mini_cover = NULL;
    app->mini_cover_w = app->mini_cover_h = 0;
    const rubraview_tags_t *tags = mini_tags(app);
    if (!app->mini_renderer || !tags->art || tags->art_size == 0) return;
    rubraview_image_load_result_t art = rubraview_pal_image_load_texture_from_memory(
        app->mini_renderer, tags->art, tags->art_size, false);
    if (art.texture) {
        app->mini_cover = art.texture;
        app->mini_cover_w = art.width;
        app->mini_cover_h = art.height;
    }
}

static void mini_show(app_state_t *app) {
    if (app->mini_open) { mini_close(app); return; }   /* the same key puts it away */
    if (!mini_target(app)) {
        osd_say(app, U8("there is no music to show"));
        return;
    }
    rubraview_window_config_t config = {
        .title = "Rubraview music", .width = MINI_WIDTH, .height = MINI_HEIGHT + 24, .owner = app->window,
    };
    app->mini_window = rubraview_pal_window_create(app->arena, &config);
    if (!app->mini_window) {
        osd_say(app, U8("the mini player could not be opened"));
        return;
    }
    if (app->mini_frame[2] > 0 && app->mini_frame[3] > 0) {
        rubraview_pal_window_set_frame(app->mini_window, app->mini_frame[0], app->mini_frame[1],
                                       app->mini_frame[2], app->mini_frame[3]);
    }
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->mini_window, &w, &h);
    app->mini_renderer = rubraview_pal_render_create(app->arena,
                                                     rubraview_pal_window_native_handle(app->mini_window), w, h);
    if (!app->mini_renderer) {
        rubraview_pal_window_destroy(app->mini_window);
        app->mini_window = NULL;
        osd_say(app, U8("the mini player could not be drawn"));
        return;
    }
    /* Small and on top: it is meant to sit over whatever is being read. */
    rubraview_pal_window_set_topmost(app->mini_window, true);
    app->mini_open = true;
    app->mini_hover = -1;
    mini_cover_load(app);
    app->mini_dirty = true;
}

/* Where each part of the little window is, in its own pixels. */
static rubraview_pal_rect_t mini_cover_rect(double dpi, double win_h) {
    double m = 6.0 * dpi;
    double side = win_h - 2.0 * m;
    if (side < 16.0 * dpi) side = 16.0 * dpi;
    return (rubraview_pal_rect_t){ m, m, side, side };
}

static rubraview_pal_rect_t mini_button_rect(double dpi, double win_w, double win_h, int32_t which) {
    (void)win_h;
    double side = 24.0 * dpi, gap = 4.0 * dpi;
    double right = win_w - 6.0 * dpi;
    double x = right - (3 - which) * side - (2 - which) * gap;
    return (rubraview_pal_rect_t){ x, 6.0 * dpi, side, side };
}

/* The strip runs from beside the cover to the window's edge, under the
   words and the buttons. */
static rubraview_pal_rect_t mini_strip_rect(double dpi, double win_w, double win_h) {
    double left = mini_cover_rect(dpi, win_h).width + 12.0 * dpi;
    double width = win_w - left - 46.0 * dpi;    /* room for the clock at the end */
    if (width < 20.0 * dpi) width = 20.0 * dpi;
    return (rubraview_pal_rect_t){ left, win_h - 14.0 * dpi, width, 5.0 * dpi };
}

/* ---- RFC-0002 Q6: the detached toolbox ---- */

/* Its size: the anchor bar on top, the profile's grid under it. */
static void draw_strip_head(app_state_t *app, rubraview_renderer_t *r, const rubraview_toolbox_layout_t *l,
                            double ox, double oy, int32_t hovered);
static void draw_strip_button(app_state_t *app, rubraview_renderer_t *r, rubraview_pal_rect_t tile, int32_t i,
                              const rubraview_action_facts_t *facts, bool hovered, uint32_t fill, uint32_t border);

/* Detached, the strip sits under the window's own anchor bar. */
static rubraview_toolbox_layout_t toolbox_window_layout(const app_state_t *app, const rubraview_tile_metrics_t *m) {
    return rubraview_toolbox_layout(m, app->toolbox_tile_count, app->media != NULL);
}

static void toolbox_window_size(const app_state_t *app, const rubraview_tile_metrics_t *m, int32_t *out_w, int32_t *out_h) {
    rubraview_toolbox_layout_t l = toolbox_window_layout(app, m);
    *out_w = (int32_t)ceil(l.width > m->anchor_size * 2.0 ? l.width : m->anchor_size * 2.0);
    *out_h = (int32_t)ceil(m->anchor_size + l.height);
}

static rubraview_rect_t toolbox_window_tile(const app_state_t *app, const rubraview_tile_metrics_t *m, int32_t i) {
    rubraview_toolbox_layout_t l = toolbox_window_layout(app, m);
    rubraview_rect_t b = rubraview_toolbox_button_rect(&l, m, i);
    b.y += m->anchor_size;
    return b;
}

static void toolbox_detach(app_state_t *app, int32_t screen_x, int32_t screen_y, bool follow_pointer) {
    if (app->toolbox_window) return;
    toolbox_refresh(app);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    rubraview_tile_metrics_t m = rubraview_tile_metrics_default(dpi);
    int32_t w = 0, h = 0;
    toolbox_window_size(app, &m, &w, &h);
    rubraview_window_config_t config = {
        .title = "Rubraview toolbox", .width = (int32_t)(w / dpi), .height = (int32_t)(h / dpi),
        .owner = app->window, .tool_window = true,
    };
    app->toolbox_window = rubraview_pal_window_create(app->arena, &config);
    if (!app->toolbox_window) {
        rubraview_box_dock(&app->toolbox);
        osd_say(app, U8("the toolbox could not open a window of its own"));
        return;
    }
    rubraview_pal_window_set_frame(app->toolbox_window, screen_x, screen_y, w, h);
    int32_t cw = 0, ch = 0;
    rubraview_pal_window_get_size(app->toolbox_window, &cw, &ch);
    app->toolbox_renderer = rubraview_pal_render_create(app->arena, rubraview_pal_window_native_handle(app->toolbox_window), cw, ch);
    app->toolbox_window_opacity = -1.0;
    app->toolbox_window_drawn = 0.0;
    app->toolbox_window_pointer_x = app->toolbox_window_pointer_y = -1.0;
    app->toolbox.state = RUBRAVIEW_BOX_DETACHED;
    if (follow_pointer) rubraview_pal_window_begin_drag(app->toolbox_window);
}

/* Back into the viewer's window, its anchor at a client point, open. */
static void toolbox_dock(app_state_t *app, double client_x, double client_y) {
    if (app->toolbox_renderer) rubraview_pal_render_destroy(app->toolbox_renderer);
    if (app->toolbox_window) rubraview_pal_window_destroy(app->toolbox_window);
    app->toolbox_renderer = NULL;
    app->toolbox_window = NULL;
    rubraview_box_dock(&app->toolbox);
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double max_x = (double)w - 88.0 * dpi, max_y = (double)h - 48.0 * dpi;
    app->toolbox.anchor_x = client_x < 0.0 ? 0.0 : client_x > max_x ? max_x : client_x;
    app->toolbox.anchor_y = client_y < 0.0 ? 0.0 : client_y > max_y ? max_y : client_y;
    note_activity(app);
}

static void draw_toolbox_window(app_state_t *app) {
    if (!app->toolbox_window || !app->toolbox_renderer) return;
    double now = rubraview_pal_time_now_seconds();
    if (now - app->toolbox_window_drawn < 0.1) return;
    app->toolbox_window_drawn = now;
    toolbox_refresh(app);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    rubraview_tile_metrics_t m = rubraview_tile_metrics_default(dpi);
    int32_t want_w = 0, want_h = 0, cw = 0, ch = 0;
    toolbox_window_size(app, &m, &want_w, &want_h);
    rubraview_pal_window_get_size(app->toolbox_window, &cw, &ch);
    if (cw != want_w || ch != want_h) {
        /* Another profile, another grid: the window follows it. */
        int32_t fx = 0, fy = 0, fw = 0, fh = 0;
        rubraview_pal_window_get_frame(app->toolbox_window, &fx, &fy, &fw, &fh);
        rubraview_pal_window_set_frame(app->toolbox_window, fx, fy, want_w, want_h);
    }
    double opacity = rubraview_settings_get(&app->settings, U8("ui"), U8("toolbox_opacity"));
    if (opacity != app->toolbox_window_opacity) {
        rubraview_pal_window_set_opacity(app->toolbox_window, opacity);
        app->toolbox_window_opacity = opacity;
    }
    rubraview_renderer_t *r = app->toolbox_renderer;
    rubraview_pal_render_begin(r, 0xFF1A1A1Au);
    rubraview_pal_rect_t left = { 0.0, 0.0, m.anchor_size, m.anchor_size };
    rubraview_pal_rect_t right = { m.anchor_size, 0.0, m.anchor_size, m.anchor_size };
    rubraview_pal_render_stroke_rect(r, left, COLOR_BOX_BORDER, 1.0, 2.0);
    rubraview_pal_render_fill_rect(r, right, COLOR_TILE_FILL, 2.0);
    rubraview_pal_render_draw_text(r, U8("T"), left, m.anchor_size * 0.42, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    rubraview_pal_render_draw_text(r, U8("Dock"), right, m.anchor_size * 0.3, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    rubraview_toolbox_layout_t l = toolbox_window_layout(app, &m);
    rubraview_action_facts_t facts = action_facts(app);
    int32_t hovered = -1;
    for (int32_t i = 0; i < app->toolbox_tile_count; ++i) {
        if (rubraview_rect_contains(toolbox_window_tile(app, &m, i), app->toolbox_window_pointer_x, app->toolbox_window_pointer_y)) hovered = i;
    }
    draw_strip_head(app, r, &l, 0.0, m.anchor_size, hovered);
    for (int32_t i = 0; i < app->toolbox_tile_count; ++i) {
        rubraview_rect_t t = toolbox_window_tile(app, &m, i);
        draw_strip_button(app, r, (rubraview_pal_rect_t){ t.x, t.y, t.width, t.height }, i, &facts, i == hovered,
                          COLOR_TILE_FILL, COLOR_BOX_BORDER);
    }
    if (!rubraview_pal_render_end(r)) app->toolbox_window_drawn = 0.0;
}

/* Where the toolbox window is over the viewer's client area, when its middle is. */
static bool toolbox_window_over_viewer(const app_state_t *app, double *out_x, double *out_y) {
    int32_t tx = 0, ty = 0, tw = 0, th = 0, vx = 0, vy = 0, vw = 0, vh = 0;
    if (!rubraview_pal_window_get_frame(app->toolbox_window, &tx, &ty, &tw, &th) ||
        !rubraview_pal_window_get_frame(app->window, &vx, &vy, &vw, &vh)) return false;
    double mx = tx + tw * 0.5, my = ty + th * 0.5;
    if (mx < vx || my < vy || mx >= vx + vw || my >= vy + vh) return false;
    *out_x = (double)(tx - vx);
    *out_y = (double)(ty - vy);
    return true;
}

static void toolbox_window_pump(app_state_t *app) {
    rubraview_window_event_t event;
    while (app->toolbox_window && rubraview_pal_window_poll_event(app->toolbox_window, &event)) {
        double dpi = rubraview_pal_window_dpi_scale(app->window);
        rubraview_tile_metrics_t m = rubraview_tile_metrics_default(dpi);
        switch (event.kind) {
            case RUBRAVIEW_WINDOW_EVENT_CLOSE: {
                int32_t w = 0, h = 0;
                rubraview_pal_window_get_size(app->window, &w, &h);
                toolbox_dock(app, (double)w - 220.0 * dpi, (double)h - 160.0 * dpi);
                return;
            }
            case RUBRAVIEW_WINDOW_EVENT_RESIZE:
                rubraview_pal_render_resize(app->toolbox_renderer, event.resize.width, event.resize.height);
                app->toolbox_window_drawn = 0.0;
                break;
            case RUBRAVIEW_WINDOW_EVENT_PAINT:
                app->toolbox_window_drawn = 0.0;
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOVED:
                /* §3.6.1 would dock it when dropped over the canvas. A viewer
                   filling the screen is under it wherever it goes, so that
                   would dock it at every move: it docks by its Dock half,
                   Ctrl+T or Show › Detach toolbox instead (D-15). */
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE:
                app->toolbox_window_pointer_x = event.mouse.x;
                app->toolbox_window_pointer_y = event.mouse.y;
                app->toolbox_window_drawn = 0.0;
                break;
            case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN:
                dispatch_key(app, event.key.combo);   /* the viewer's keys work from here too */
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN: {
                if (event.mouse.button != RUBRAVIEW_MOUSE_LEFT) break;
                if (event.mouse.y < m.anchor_size) {
                    if (event.mouse.x < m.anchor_size) {
                        rubraview_pal_window_begin_drag(app->toolbox_window);
                    } else if (event.mouse.x < m.anchor_size * 2.0) {
                        int32_t w = 0, h = 0;
                        rubraview_pal_window_get_size(app->window, &w, &h);
                        double cx = (double)w - 220.0 * dpi, cy = (double)h - 160.0 * dpi;
                        (void)toolbox_window_over_viewer(app, &cx, &cy);
                        toolbox_dock(app, cx, cy);
                        return;
                    }
                    break;
                }
                rubraview_toolbox_layout_t l = toolbox_window_layout(app, &m);
                if (app->media && app->media_info.duration_seconds > 0.0 && l.timeline.width > 0.0 &&
                    event.mouse.x >= l.timeline.x && event.mouse.x < l.timeline.x + l.timeline.width &&
                    event.mouse.y >= m.anchor_size + l.timeline.y - l.timeline.height &&
                    event.mouse.y < m.anchor_size + l.timeline.y + l.timeline.height * 2.0) {
                    double seconds = rubraview_seekbar_time(l.timeline.x, l.timeline.width, event.mouse.x, app->media_info.duration_seconds);
                    media_seek_to(app, seconds);
                    app->media_position = seconds;
                    app->toolbox_window_drawn = 0.0;
                    break;
                }
                for (int32_t i = 0; i < app->toolbox_tile_count; ++i) {
                    if (!rubraview_rect_contains(toolbox_window_tile(app, &m, i), event.mouse.x, event.mouse.y)) continue;
                    bool enabled = true;
                    (void)toolbox_caption(app, &app->toolbox_tiles[i], &enabled);
                    if (enabled) handle_action(app, app->toolbox_tiles[i].action);
                    app->toolbox_window_drawn = 0.0;
                    break;
                }
                break;
            }
            default:
                break;
        }
    }
}

static void dispatch_key(app_state_t *app, rubraview_key_combo_t combo) {
    if (ab_edit_handle_key(app, combo)) return;
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
                            ? U8("media") : U8("subpage");
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
        case RUBRAVIEW_TITLEBAR_PIN:        handle_action(app, U8("toggle_always_on_top")); return true;
        case RUBRAVIEW_TITLEBAR_SNAP_BOXES:
            /* Both boxes back to their corners, inside the window. */
            rubraview_box_snap_home(&app->menubox, &metrics, (double)win_w, (double)win_h);
            if (app->toolbox_window) toolbox_dock(app, 0.0, 0.0);
            rubraview_box_snap_home(&app->toolbox, &metrics, (double)win_w, (double)win_h);
            osd_say(app, U8("the floating boxes are back in their corners"));
            return true;
        case RUBRAVIEW_TITLEBAR_CAPTION:    rubraview_pal_window_begin_drag(app->window); return true;
        default: break;
    }

    /* The anchor's two buttons answer first: they sit on top of the
       box, and a click there is never a tile. */
    if (rubraview_box_click(&app->menubox, &metrics, x, y)) {
        menu_rebuild(app);   /* opened or closed: Recent as it is now, from the root */
        return true;
    }
    bool toolbox_here = app->toolbox.state != RUBRAVIEW_BOX_DETACHED;   /* detached, it answers in its own window */
    if (toolbox_here && rubraview_box_click(&app->toolbox, &metrics, x, y)) return true;

    /* The pins at the boxes' top-left (owner, 2026-09-22). */
    if (rubraview_box_pin_click(&app->menubox, &metrics, x, y)) return true;
    if (toolbox_here && rubraview_box_pin_click(&app->toolbox, &metrics, x, y)) return true;
    /* The strip's seek bar: a click puts the film there. */
    if (toolbox_here && app->media && app->media_info.duration_seconds > 0.0) {
        rubraview_rect_t bar = rubraview_box_timeline_rect(&app->toolbox, &metrics);
        if (bar.width > 0.0 && x >= bar.x && x < bar.x + bar.width &&
            y >= bar.y - bar.height && y < bar.y + bar.height * 2.0) {   /* a little taller than drawn: easier to hit */
            double seconds = rubraview_seekbar_time(bar.x, bar.width, x, app->media_info.duration_seconds);
            media_seek_to(app, seconds);
            app->media_position = seconds;
            note_activity(app);
            return true;
        }
    }

    int32_t tile = rubraview_box_tile_at(&app->menubox, &metrics, x, y);
    if (tile >= 0) {
        /* A dimmed item does nothing, as a dimmed toolbox tile does. */
        bool enabled = true;
        char scratch[64];
        (void)menu_tile_caption(&app->menu, tile, scratch, sizeof(scratch), &enabled, NULL);
        if (!enabled) return true;
        /* Delete asks first (owner, 2026-09-21): the first tap arms it and
           its tile says "Delete?"; a second tap on it within five seconds
           acts. Any other tap disarms it. */
        const rubraview_menu_item_t *item = rubraview_menu_item_at(&app->menu, tile);
        if (item && rubraview_u8_eq_lit(item->action, "delete_file")) {
            if (!rubraview_confirm_press(&app->menu_confirm, item - app->menu.tree->items,
                                         rubraview_pal_time_now_seconds())) {
                osd_say(app, U8("tap Delete again to move it to the recycle bin"));
                return true;
            }
        } else {
            rubraview_confirm_clear(&app->menu_confirm);
        }
        u8str_t action;
        rubraview_menu_result_t result = rubraview_menu_activate(&app->menu, tile, &action);
        if (result == RUBRAVIEW_MENU_ACTIVATED) {
            handle_action(app, action);
        } else if (result == RUBRAVIEW_MENU_DESCENDED || result == RUBRAVIEW_MENU_WENT_BACK) {
            sync_menubox_tiles(app);
        }
        return true;
    }

    tile = toolbox_here ? rubraview_box_tile_at(&app->toolbox, &metrics, x, y) : -1;
    if (tile >= 0) {
        /* RFC-0002 §4: the tiles of the profile on screen; a dimmed one does nothing. */
        toolbox_refresh(app);
        if (tile < app->toolbox_tile_count) {
            bool enabled = true;
            (void)toolbox_caption(app, &app->toolbox_tiles[tile], &enabled);
            if (enabled) handle_action(app, app->toolbox_tiles[tile].action);
        }
        return true;
    }

    return false;
}

/* ---- rendering ---- */

/* The viewer the menu's toggle captions read their state from. */
static const app_state_t *g_caption_app;

/* Tile captions for the menu box come from the current menu level, with
   the Back tile at index 0 below the root (§3.6.2). A submenu ends in
   ">", a toggle says whether it is on, the choice in use is marked, and
   what cannot act on the page on screen is dimmed. */
static u8str_t menu_tile_caption(const rubraview_menu_state_t *menu, int32_t tile, char *scratch, size_t scratch_size,
                                 bool *out_enabled, bool *out_current) {
    if (out_enabled) *out_enabled = true;
    if (out_current) *out_current = false;
    if (rubraview_menu_has_back_tile(menu) && tile == 0) return U8("< Back");
    const rubraview_menu_item_t *item = rubraview_menu_item_at(menu, tile);
    if (!item) return (u8str_t){ .ptr = "", .len = 0 };
    bool submenu = item->child_count > 0;
    rubraview_action_state_t st = { .enabled = true, .mark = RUBRAVIEW_MARK_NONE };
    if (!submenu && g_caption_app) {
        rubraview_action_facts_t facts = action_facts(g_caption_app);
        st = rubraview_action_state(item->action, &facts);
    }
    if (out_enabled) *out_enabled = st.enabled;
    if (out_current) *out_current = st.mark == RUBRAVIEW_MARK_CURRENT;
    if (g_caption_app && rubraview_confirm_armed(&g_caption_app->menu_confirm, item - menu->tree->items,
                                                 rubraview_pal_time_now_seconds())) {
        return U8("Delete?");
    }
    return rubraview_tile_caption(item->label, submenu, st.mark, st.value, scratch, scratch_size);
}

static u8str_t box_opacity_key(const rubraview_box_t *box) {
    return box->kind == RUBRAVIEW_BOX_MENU ? U8("menubox_opacity") : U8("toolbox_opacity");
}

static double box_opacity(const app_state_t *app, const rubraview_box_t *box) {
    /* Spelled out, not through box_opacity_key: check-settings.py finds reads by their literal keys. */
    return box->kind == RUBRAVIEW_BOX_MENU ? rubraview_settings_get(&app->settings, U8("ui"), U8("menubox_opacity"))
                                           : rubraview_settings_get(&app->settings, U8("ui"), U8("toolbox_opacity"));
}

/* D-15: Alt + wheel over a box. Returns true when the wheel was spent on it. */
static bool box_wheel_opacity(app_state_t *app, double x, double y, double notches, uint32_t modifiers) {
    if (!(modifiers & RUBRAVIEW_MOD_ALT)) return false;
    rubraview_tile_metrics_t metrics = rubraview_tile_metrics_default(rubraview_pal_window_dpi_scale(app->window));
    rubraview_box_t *boxes[2] = { &app->menubox, &app->toolbox };
    for (size_t i = 0; i < 2; ++i) {
        if (boxes[i]->state == RUBRAVIEW_BOX_DETACHED) continue;
        if (!rubraview_rect_contains(rubraview_box_bounds(boxes[i], &metrics), x, y)) continue;
        double next = rubraview_box_opacity_step(box_opacity(app, boxes[i]), notches);
        rubraview_settings_set(&app->settings, U8("ui"), box_opacity_key(boxes[i]), next);
        char line[48];
        int n = snprintf(line, sizeof(line), "%s %d%%", boxes[i]->kind == RUBRAVIEW_BOX_MENU ? "menu box" : "toolbox", (int)next);
        if (n > 0) osd_say(app, (u8str_t){ .ptr = line, .len = (size_t)n });
        note_activity(app);
        return true;
    }
    return false;
}

/* RFC-0002 §4.2: the timeline strip above the information bar while a
   video or music page is on screen — elapsed, the seek bar, total, and the
   volume and speed. It shows with the OSD, and stays while paused. */
static double timeline_alpha(const app_state_t *app) {
    if (!app->media) return 0.0;
    if (app->media_paused || app->timeline_dragging) return 1.0;
    return rubraview_osd_opacity(&app->osd);
}

static void timeline_geometry(const app_state_t *app, double win_w, double win_h,
                              rubraview_pal_rect_t *out_bar, rubraview_pal_rect_t *out_track) {
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double bar_h = 28.0 * dpi;
    double info_top = win_h - bar_h - (app->filmstrip.visible ? FILMSTRIP_THUMB : 0.0);
    *out_bar = (rubraview_pal_rect_t){ 0.0, info_top - bar_h, win_w, bar_h };
    double left = 110.0 * dpi, right = 250.0 * dpi;
    double width = win_w - left - right;
    if (width < 40.0 * dpi) width = 40.0 * dpi;
    *out_track = (rubraview_pal_rect_t){ left, out_bar->y + bar_h * 0.4, width, bar_h * 0.2 };
}

static bool timeline_hit(const app_state_t *app, double x, double y, rubraview_pal_rect_t *out_track) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    rubraview_pal_rect_t bar, track;
    timeline_geometry(app, (double)w, (double)h, &bar, &track);
    if (out_track) *out_track = track;
    return timeline_alpha(app) > 0.01 && y >= bar.y && y < bar.y + bar.height &&
           x >= track.x - 6.0 && x <= track.x + track.width + 6.0;
}

static void timeline_seek_to_pointer(app_state_t *app, double x) {
    rubraview_pal_rect_t track;
    (void)timeline_hit(app, x, 0.0, &track);
    double seconds = rubraview_seekbar_time(track.x, track.width, x, app->media_info.duration_seconds);
    media_seek_to(app, seconds);
    app->media_position = seconds;   /* the strip follows the pointer before the frame arrives */
    app->timeline_last_seek = rubraview_pal_time_now_seconds();
    note_activity(app);
}

static void draw_timeline(app_state_t *app, double win_w, double win_h) {
    double a = timeline_alpha(app);
    if (a <= 0.01 || app->media_info.duration_seconds <= 0.0) return;
    uint32_t alpha = (uint32_t)(a * 255.0) & 0xFFu;
    rubraview_pal_rect_t bar, track;
    timeline_geometry(app, win_w, win_h, &bar, &track);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    rubraview_pal_render_fill_rect(app->renderer, bar, (alpha / 2u) << 24, 0.0);
    rubraview_pal_render_fill_rect(app->renderer, track, (alpha / 3u) << 24 | 0x00FFFFFFu, 2.0);
    double duration = app->media_info.duration_seconds;
    double done = rubraview_seekbar_fraction(app->media_position, duration);
    rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ track.x, track.y, track.width * done, track.height },
                                   (alpha << 24) | 0x006FA8DCu, 2.0);
    /* A-B repeat: the section, marked on the bar. */
    if (app->ab_a >= 0.0) {
        double ax = track.x + track.width * rubraview_seekbar_fraction(app->ab_a, duration);
        double bx = app->ab_b > app->ab_a ? track.x + track.width * rubraview_seekbar_fraction(app->ab_b, duration) : ax + 2.0 * dpi;
        rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ ax, bar.y + bar.height * 0.2, bx - ax, bar.height * 0.6 },
                                       (alpha / 3u) << 24 | 0x00F0C040u, 0.0);
    }
    uint32_t text = (alpha << 24) | (COLOR_TEXT & 0x00FFFFFFu);
    char elapsed[32], total[32], right[96];
    u8str_t e = rubraview_format_timecode(elapsed, sizeof(elapsed), app->media_position, false);
    u8str_t t = rubraview_format_timecode(total, sizeof(total), duration, false);
    rubraview_pal_render_draw_text(app->renderer, e, (rubraview_pal_rect_t){ 0.0, bar.y, track.x - 8.0 * dpi, bar.height },
                                   bar.height * 0.45, text, RUBRAVIEW_TEXT_RIGHT);
    int volume = (int)rubraview_settings_get(&app->settings, U8("audio"), U8("volume"));
    bool muted = rubraview_settings_get(&app->settings, U8("audio"), U8("mute")) > 0.5;
    double speed = app->media_speed > 0.0 ? app->media_speed : 1.0;
    int n = snprintf(right, sizeof(right), "%.*s   %s %d%%", (int)t.len, t.ptr, muted ? "muted" : "vol", volume);
    if (n > 0 && fabs(speed - 1.0) > 1e-6 && (size_t)n + 12 < sizeof(right)) {
        n += snprintf(right + n, sizeof(right) - (size_t)n, "   ");
        n += speed_text(right + n, sizeof(right) - (size_t)n, speed);
    }
    if (app->ab_a >= 0.0 && n > 0 && (size_t)n + 8 < sizeof(right)) n += snprintf(right + n, sizeof(right) - (size_t)n, app->ab_b > app->ab_a ? "   A-B" : "   A-");
    if (n > 0) {
        rubraview_pal_render_draw_text(app->renderer, (u8str_t){ .ptr = right, .len = (size_t)n },
                                       (rubraview_pal_rect_t){ track.x + track.width + 8.0 * dpi, bar.y, win_w, bar.height },
                                       bar.height * 0.45, text, RUBRAVIEW_TEXT_LEFT);
    }
}

/* The strip's seek bar and the line under it: the file's name or, under
   the pointer, what the button does. Offsets put the layout on screen. */
static void draw_strip_head(app_state_t *app, rubraview_renderer_t *r, const rubraview_toolbox_layout_t *l,
                            double ox, double oy, int32_t hovered) {
    if (l->timeline.width > 0.0) {
        rubraview_pal_rect_t track = { ox + l->timeline.x, oy + l->timeline.y + l->timeline.height * 0.35,
                                       l->timeline.width, l->timeline.height * 0.3 };
        rubraview_pal_render_fill_rect(r, track, 0x60FFFFFFu, track.height * 0.5);
        double duration = app->media_info.duration_seconds;
        double f = duration > 0.0 ? app->media_position / duration : 0.0;
        if (f < 0.0) f = 0.0;
        if (f > 1.0) f = 1.0;
        rubraview_pal_rect_t done = { track.x, track.y, track.width * f, track.height };
        rubraview_pal_render_fill_rect(r, done, COLOR_TILE_CURRENT, track.height * 0.5);
    }
    rubraview_pal_rect_t name = { ox + l->title.x, oy + l->title.y, l->title.width, l->title.height };
    u8str_t label = hovered >= 0 && hovered < app->toolbox_tile_count ? app->toolbox_tiles[hovered].caption
                                                                      : page_display_name(app, (size_t)current_page_index(app));
    rubraview_pal_render_draw_text(r, label, name, l->title.height * 0.72, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
}

/* A strip button's face: its icon, or a short caption where the icon font is missing. */
static void draw_strip_button(app_state_t *app, rubraview_renderer_t *r, rubraview_pal_rect_t tile, int32_t i,
                              const rubraview_action_facts_t *facts, bool hovered, uint32_t fill, uint32_t border) {
    rubraview_pal_render_fill_rect(r, tile, hovered ? 0xE0343434u : fill, 3.0);
    rubraview_pal_render_stroke_rect(r, tile, hovered ? COLOR_TILE_CURRENT : border, 1.0, 3.0);
    bool enabled = true;
    u8str_t caption = toolbox_caption(app, &app->toolbox_tiles[i], &enabled);
    uint32_t ink = enabled ? COLOR_TEXT : 0x70F0F0F0u;
    uint32_t icon = rubraview_action_icon(app->toolbox_tiles[i].action, facts);
    if (icon && rubraview_pal_render_draw_icon(r, icon, tile, tile.height * 0.5, ink)) return;
    rubraview_pal_render_draw_text(r, caption, tile, tile.height * 0.34, ink, RUBRAVIEW_TEXT_CENTER);
}

static void draw_box(app_state_t *app, const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics,
                     const char *const *captions, int32_t caption_count,
                     const rubraview_menu_state_t *menu) {
    rubraview_rect_t bounds = rubraview_box_bounds(box, metrics);
    rubraview_pal_rect_t body = { bounds.x, bounds.y, bounds.width, bounds.height };

    /* D-15: each box its own opacity; the text stays solid. */
    double opacity = box_opacity(app, box);
    const uint32_t box_fill = rubraview_box_fade(COLOR_BOX_FILL, opacity);
    const uint32_t box_border = rubraview_box_fade(COLOR_BOX_BORDER, opacity);
    const uint32_t tile_fill = rubraview_box_fade(COLOR_TILE_FILL, opacity);

    rubraview_pal_render_fill_rect(app->renderer, body, box_fill, 2.0);
    rubraview_pal_render_stroke_rect(app->renderer, body, box_border, 1.0, 2.0);
    if (box->state != RUBRAVIEW_BOX_COLLAPSED) {
        /* Open, the grid sits beside the anchor, which keeps its own ground. */
        rubraview_rect_t a = rubraview_box_anchor_rect(box, metrics);
        rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ a.x, a.y, a.width, a.height }, box_fill, 2.0);
    }

    /* The anchor is two buttons, and they must not look like one. The
       left is outlined — it waits to be clicked; the right is filled —
       it answers the pointer on its own. */
    rubraview_rect_t click_half = rubraview_box_anchor_half_rect(box, metrics, RUBRAVIEW_ANCHOR_CLICK);
    rubraview_rect_t hover_half = rubraview_box_anchor_half_rect(box, metrics, RUBRAVIEW_ANCHOR_HOVER);

    rubraview_pal_rect_t left = { click_half.x, click_half.y, click_half.width, click_half.height };
    rubraview_pal_rect_t right = { hover_half.x, hover_half.y, hover_half.width, hover_half.height };

    rubraview_pal_render_stroke_rect(app->renderer, left, box_border, 1.0, 2.0);
    rubraview_pal_render_fill_rect(app->renderer, right, tile_fill, 2.0);
    rubraview_pal_render_stroke_rect(app->renderer, right, box_border, 1.0, 2.0);

    /* RFC-0002 §6.1 (owner, 2026-09-15): M for the menu box, T for the toolbox. */
    rubraview_pal_render_draw_text(app->renderer,
                                   box->kind == RUBRAVIEW_BOX_MENU ? U8("M") : U8("T"),
                                   left, metrics->anchor_size * 0.42, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    rubraview_pal_render_draw_text(app->renderer, box->pinned ? U8("*") : U8("v"), right,
                                   metrics->anchor_size * 0.42, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);

    if (box->state == RUBRAVIEW_BOX_COLLAPSED) return;

    /* The pin, a third square beside the anchor while the box is open
       (owner, 2026-09-22): filled while the box stays open, an outline
       while it folds when the pointer leaves. */
    rubraview_rect_t pr = rubraview_box_pin_rect(box, metrics);
    rubraview_pal_rect_t pin = { pr.x, pr.y, pr.width, pr.height };
    bool pinned = rubraview_box_pin_shown_on(box);
    rubraview_pal_render_fill_rect(app->renderer, pin, box_fill, 2.0);
    rubraview_pal_render_stroke_rect(app->renderer, pin, box_border, 1.0, 2.0);
    if (!rubraview_pal_render_draw_icon(app->renderer, rubraview_pin_icon(pinned), pin, pr.height * 0.45,
                                        pinned ? COLOR_TILE_CURRENT : COLOR_TEXT)) {
        rubraview_pal_render_draw_text(app->renderer, pinned ? U8("*") : U8("o"), pin, pr.height * 0.8,
                                       pinned ? COLOR_TILE_CURRENT : COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }

    bool strip = box == &app->toolbox;
    rubraview_action_facts_t facts = action_facts(app);
    int32_t hovered = rubraview_box_tile_at(box, metrics, app->pointer_x, app->pointer_y);
    if (strip) {
        rubraview_toolbox_layout_t l = rubraview_toolbox_layout(metrics, box->tile_count, box->timeline);
        draw_strip_head(app, app->renderer, &l, bounds.x, bounds.y, hovered);
    }

    for (int32_t i = 0; i < box->tile_count; ++i) {
        rubraview_rect_t t = rubraview_box_tile_rect(box, metrics, i);
        rubraview_pal_rect_t tile = { t.x, t.y, t.width, t.height };
        if (strip && i < app->toolbox_tile_count) {
            draw_strip_button(app, app->renderer, tile, i, &facts, i == hovered, tile_fill, box_border);
            continue;
        }
        rubraview_pal_render_fill_rect(app->renderer, tile, tile_fill, 0.0);
        rubraview_pal_render_stroke_rect(app->renderer, tile, box_border, 1.0, 0.0);
        u8str_t caption = { .ptr = "", .len = 0 };
        bool enabled = true, current = false;
        char scratch[64];
        if (menu) {
            caption = menu_tile_caption(menu, i, scratch, sizeof(scratch), &enabled, &current);
        } else if (captions && i < caption_count) {
            caption = cstr(captions[i]);
        }
        if (current) {
            rubraview_pal_render_stroke_rect(app->renderer, tile, COLOR_TILE_CURRENT, 2.0, 0.0);
        }
        if (caption.len > 0) {
            rubraview_pal_render_draw_text(app->renderer, caption, tile, metrics->tile_size * 0.22,
                                           enabled ? COLOR_TEXT : 0x70F0F0F0u, RUBRAVIEW_TEXT_CENTER);
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
        bool picked = app->picker_selected && index < app->picker_listing.count && app->picker_selected[index];
        rubraview_pal_render_fill_rect(app->renderer, tile, picked ? COLOR_TILE_CURRENT : COLOR_TILE_FILL, 0.0);
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

    /* Action bar: the buttons, then the selection metrics (§3.15.2 tier three). */
    rubraview_pal_rect_t bar = { 0.0, win_h - action_h, win_w, action_h };
    rubraview_pal_render_fill_rect(app->renderer, bar, COLOR_BAR_FILL, 0.0);

    for (int32_t b = 0; b < PICKER_BTN_COUNT; ++b) {
        rubraview_pal_rect_t rect = picker_button_rect(win_w, win_h, dpi, b);
        bool lit = (b == PICKER_BTN_INDIVIDUAL && app->picker.mode == RUBRAVIEW_PICK_INDIVIDUAL) ||
                   (b == PICKER_BTN_RANGE && app->picker.mode == RUBRAVIEW_PICK_RANGE);
        rubraview_pal_render_fill_rect(app->renderer, rect, lit ? COLOR_TILE_CURRENT : COLOR_TILE_FILL, 3.0);
        rubraview_pal_render_stroke_rect(app->renderer, rect, COLOR_BOX_BORDER, 1.0, 3.0);
        rubraview_pal_render_draw_text(app->renderer, cstr(PICKER_BUTTON_LABEL[b]), rect,
                                       rect.height * 0.34, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
    bar.y += PICKER_BUTTON_ROW * dpi;
    bar.height -= PICKER_BUTTON_ROW * dpi;

    char status[224];
    size_t selected = 0;
    uint64_t bytes = 0;
    rubraview_picker_selection_metrics(&app->picker, &selected, &bytes);
    char hidden[48] = "";
    if (app->picker_hidden > 0) snprintf(hidden, sizeof(hidden), "   |   %zu other files hidden", app->picker_hidden);
    int written = snprintf(status, sizeof(status),
                           "%zu items%s   |   selected %zu (%llu bytes)   |   Enter opens, Esc closes",
                           app->picker_listing.count, hidden, selected, (unsigned long long)bytes);
    if (written > 0) {
        rubraview_pal_render_draw_text(app->renderer,
                                       (u8str_t){ .ptr = status, .len = (size_t)written },
                                       bar, action_h * 0.34, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }

    /* The picker covers the chrome: its text box and its messages are drawn here. */
    draw_rename_box(app, win_w, win_h, dpi);
    draw_notice(app, win_w, win_h, dpi);
}

/* Text with a dark outline around it, for subtitles and their preview.
   The text PAL draws flat text, so the outline is the same string drawn
   behind it, displaced. Four corners were enough at two pixels; at eight
   they came apart into four ghost copies (VM, 2026-09-13), so it is
   eight directions, and a second ring at half the distance once the
   outline is wide enough to show a gap. */
static void draw_outlined_text(rubraview_renderer_t *renderer, u8str_t text, rubraview_pal_rect_t rect,
                               double size, double outline, uint32_t argb) {
    static const double DX[8] = { -1.0, 0.0, 1.0, -1.0, 1.0, -1.0, 0.0, 1.0 };
    static const double DY[8] = { -1.0, -1.0, -1.0, 0.0, 0.0, 1.0, 1.0, 1.0 };
    int rings = outline > 3.0 ? 2 : 1;
    for (int ring = 0; outline > 0.0 && ring < rings; ++ring) {
        double off = ring == 0 ? outline : outline * 0.5;
        for (size_t i = 0; i < 8; ++i) {
            rubraview_pal_rect_t shadow = { rect.x + DX[i] * off, rect.y + DY[i] * off, rect.width, rect.height };
            rubraview_pal_render_draw_text(renderer, text, shadow, size, 0xFF000000u, RUBRAVIEW_TEXT_CENTER);
        }
    }
    rubraview_pal_render_draw_text(renderer, text, rect, size, argb, RUBRAVIEW_TEXT_CENTER);
}

/* §3.16.1 / R135: the line of dialogue for where the film is now, laid
   over the picture with a dark outline so it reads on any background. */
/* A DVD subtitle: the picture for this moment, drawn over the film in
   the place the disc put it (owner, 2026-09-23). */
static void draw_vobsub(app_state_t *app) {
    if (!app->media || app->vobsub.count == 0 || !app->vobsub_pixels || !app->video_transform_ok) return;
    if (app->vobsub.frame_width <= 0 || app->vobsub.frame_height <= 0) return;

    double when = app->media_position - app->subtitle.offset_seconds;   /* Z / X move these too */
    int32_t index = rubraview_vobsub_at(&app->vobsub, when);
    if (index < 0) return;

    if (index != app->vobsub_texture_cue) {
        /* A new one: decode it, and make a texture of its own size. */
        if (!rubraview_vobsub_decode(&app->vobsub, (size_t)index, (const uint8_t*)app->vobsub_sub.ptr,
                                     app->vobsub_sub.len, app->vobsub_pixels, &app->vobsub_cue)) {
            app->vobsub_texture_cue = index;   /* a torn one: do not try it again every frame */
            if (app->vobsub_texture) { rubraview_pal_texture_destroy(app->vobsub_texture); app->vobsub_texture = NULL; }
            return;
        }
        app->vobsub_texture_cue = index;
        if (!app->vobsub_texture || app->vobsub_texture_w != app->vobsub_cue.width ||
            app->vobsub_texture_h != app->vobsub_cue.height) {
            if (app->vobsub_texture) rubraview_pal_texture_destroy(app->vobsub_texture);
            app->vobsub_texture = rubraview_pal_texture_create_bgra(app->renderer, app->vobsub_cue.width,
                                                                    app->vobsub_cue.height);
            app->vobsub_texture_w = app->vobsub_cue.width;
            app->vobsub_texture_h = app->vobsub_cue.height;
        }
        if (!app->vobsub_texture) return;
        size_t pixels = (size_t)app->vobsub_cue.width * (size_t)app->vobsub_cue.height;
        proven_result_mem_mut_t mem = rubraview_arena_alloc_array(app->arena, pixels, sizeof(uint32_t));
        if (!proven_is_ok(mem.err)) return;
        uint32_t *bgra = (uint32_t*)mem.value.ptr;
        rubraview_vobsub_pixels(&app->vobsub_cue, bgra);
        rubraview_pal_texture_upload_bgra(app->vobsub_texture, (const uint8_t*)bgra, app->vobsub_cue.width * 4);
    }
    if (!app->vobsub_texture || app->vobsub_cue.width <= 0) return;
    /* The picture's own frame — 720x480 and such — onto the film's. */
    if (app->media_position >= app->vobsub_cue.end_seconds + app->subtitle.offset_seconds) return;
    double sx = (double)app->video_page_w / (double)app->vobsub.frame_width;
    double sy = (double)app->video_page_h / (double)app->vobsub.frame_height;
    rubraview_mat3x2_t place = rubraview_mat3x2_multiply(
        rubraview_mat3x2_scale(sx, sy),
        rubraview_mat3x2_translate((double)app->vobsub_cue.x * sx, (double)app->vobsub_cue.y * sy));
    rubraview_pal_render_draw_texture(app->renderer, app->vobsub_texture,
                                      rubraview_mat3x2_multiply(place, app->video_transform),
                                      RUBRAVIEW_INTERP_LINEAR);
}

/* A Blu-ray subtitle: the same idea as the DVD's above — decode the one
   this moment wants, keep its texture while it is up, and put it where
   the disc put it on its own frame (owner, 2026-09-24). */
static void draw_pgs(app_state_t *app) {
    if (!app->media || app->pgs.count == 0 || !app->pgs_pixels || !app->video_transform_ok) return;
    if (app->pgs.frame_width <= 0 || app->pgs.frame_height <= 0) return;

    double when = app->media_position - app->subtitle.offset_seconds;   /* Z / X move these too */
    int32_t index = rubraview_pgs_at(&app->pgs, when);
    if (index < 0) return;

    if (index != app->pgs_texture_cue) {
        if (!rubraview_pgs_decode(&app->pgs, (size_t)index, (const uint8_t*)app->pgs_bytes.ptr,
                                  app->pgs_bytes.len, app->pgs_pixels, &app->pgs_cue)) {
            app->pgs_texture_cue = index;      /* a torn one: do not try it again every frame */
            if (app->pgs_texture) { rubraview_pal_texture_destroy(app->pgs_texture); app->pgs_texture = NULL; }
            return;
        }
        app->pgs_texture_cue = index;
        if (!app->pgs_texture || app->pgs_texture_w != app->pgs_cue.width ||
            app->pgs_texture_h != app->pgs_cue.height) {
            if (app->pgs_texture) rubraview_pal_texture_destroy(app->pgs_texture);
            app->pgs_texture = rubraview_pal_texture_create_bgra(app->renderer, app->pgs_cue.width,
                                                                 app->pgs_cue.height);
            app->pgs_texture_w = app->pgs_cue.width;
            app->pgs_texture_h = app->pgs_cue.height;
        }
        if (!app->pgs_texture) return;
        size_t pixels = (size_t)app->pgs_cue.width * (size_t)app->pgs_cue.height;
        proven_result_mem_mut_t mem = rubraview_arena_alloc_array(app->arena, pixels, sizeof(uint32_t));
        if (!proven_is_ok(mem.err)) return;
        uint32_t *bgra = (uint32_t*)mem.value.ptr;
        rubraview_pgs_pixels(&app->pgs_cue, bgra);
        rubraview_pal_texture_upload_bgra(app->pgs_texture, (const uint8_t*)bgra, app->pgs_cue.width * 4);
    }
    if (!app->pgs_texture || app->pgs_cue.width <= 0) return;
    if (app->media_position >= app->pgs_cue.end_seconds + app->subtitle.offset_seconds) return;
    double sx = (double)app->video_page_w / (double)app->pgs.frame_width;
    double sy = (double)app->video_page_h / (double)app->pgs.frame_height;
    rubraview_mat3x2_t place = rubraview_mat3x2_multiply(
        rubraview_mat3x2_scale(sx, sy),
        rubraview_mat3x2_translate((double)app->pgs_cue.x * sx, (double)app->pgs_cue.y * sy));
    rubraview_pal_render_draw_texture(app->renderer, app->pgs_texture,
                                      rubraview_mat3x2_multiply(place, app->video_transform),
                                      RUBRAVIEW_INTERP_LINEAR);
}

static void draw_subtitle(app_state_t *app, int32_t win_w, int32_t win_h) {
    draw_vobsub(app);
    draw_pgs(app);
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
       change it (0 turns it off). */
    double outline = rubraview_settings_get(&app->settings, U8("video"), U8("subtitle_outline")) * dpi;
    draw_outlined_text(app->renderer, cue->text, rect, font, outline, 0xFFFFFFFFu);
}

/* The text box: renaming a file, or giving the picked files one
   extension. Drawn by the chrome and by the picker, which covers it. */
/* The OSD line. The picker covers the chrome, so it draws this itself. */
static void draw_notice(app_state_t *app, double win_w, double win_h, double chrome_dpi) {
    if (app->notice_seconds > 0.0 && app->notice_length > 0) {
        double box_h = 44.0 * chrome_dpi;
        rubraview_pal_rect_t box = { win_w * 0.2, win_h * 0.08, win_w * 0.6, box_h };
        rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BAR_FILL, 3.0);
        rubraview_pal_render_draw_text(app->renderer,
                                       (u8str_t){ .ptr = app->notice, .len = app->notice_length },
                                       box, 15.0 * chrome_dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
}

static void draw_rename_box(app_state_t *app, double win_w, double win_h, double dpi) {
    if (!app->rename_active) return;
    {
        double box_w = 560.0 * dpi, box_h = 64.0 * dpi;
        rubraview_pal_rect_t box = { (win_w - box_w) * 0.5, win_h * 0.75, box_w, box_h };
        rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BOX_FILL, 3.0);
        rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_BOX_BORDER, 1.0, 3.0);
        /* The name, the syllable the IME is still building, and a caret
           that blinks (owner, 2026-09-23: the box did not look like it
           was taking text). The caret's place is held by a space while it
           is dark, so the name does not jump as it blinks. */
        char shown[sizeof(app->rename_buffer) + sizeof(app->rename_composing) + 2];
        size_t at = app->rename_length;
        memcpy(shown, app->rename_buffer, at);
        memcpy(shown + at, app->rename_composing, app->rename_composing_length);
        at += app->rename_composing_length;
        bool caret_lit = fmod(rubraview_pal_time_now_seconds(), 1.0) < 0.5;
        shown[at++] = caret_lit ? '|' : ' ';
        rubraview_pal_render_draw_text(app->renderer, (u8str_t){ .ptr = shown, .len = at },
                                       box, 18.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
}

/* The two points as text, side by side, with the one in hand marked and
   a line saying how the keys and the numbers cross over. */
static void draw_ab_edit_box(app_state_t *app, double win_w, double win_h, double dpi) {
    if (!app->ab_edit_open) return;
    double box_w = 560.0 * dpi, box_h = 96.0 * dpi;
    rubraview_pal_rect_t box = { (win_w - box_w) * 0.5, win_h * 0.62, box_w, box_h };
    rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BOX_FILL, 3.0);
    rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_BOX_BORDER, 1.0, 3.0);

    bool caret_lit = fmod(rubraview_pal_time_now_seconds(), 1.0) < 0.5;
    char line[160];
    int n = snprintf(line, sizeof(line), "A  %s%.*s%s     B  %s%.*s%s",
                     app->ab_edit_field == 0 ? "[" : " ",
                     (int)app->ab_edit_length[0], app->ab_edit_text[0],
                     app->ab_edit_field == 0 ? (caret_lit ? "|]" : " ]") : " ",
                     app->ab_edit_field == 1 ? "[" : " ",
                     (int)app->ab_edit_length[1], app->ab_edit_text[1],
                     app->ab_edit_field == 1 ? (caret_lit ? "|]" : " ]") : " ");
    if (n > 0) {
        rubraview_pal_rect_t top = { box.x, box.y + 10.0 * dpi, box.width, 34.0 * dpi };
        rubraview_pal_render_draw_text(app->renderer, (u8str_t){ .ptr = line, .len = (size_t)n },
                                       top, 20.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
    rubraview_pal_rect_t hint = { box.x, box.y + 50.0 * dpi, box.width, 34.0 * dpi };
    rubraview_pal_render_draw_text(app->renderer,
                                   U8("[ or ] takes this moment  ·  Tab swaps  ·  \\ empties  ·  Enter keeps"),
                                   hint, 13.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
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

    draw_rename_box(app, win_w, win_h, chrome_dpi);
    draw_ab_edit_box(app, win_w, win_h, chrome_dpi);

    draw_notice(app, win_w, win_h, chrome_dpi);

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
    toolbox_refresh(app);
    if (app->toolbox.state != RUBRAVIEW_BOX_DETACHED) draw_box(app, &app->toolbox, &metrics, NULL, 0, NULL);
    draw_box(app, &app->menubox, &metrics, NULL, 0, &app->menu);

    if (app->menubox.state != RUBRAVIEW_BOX_COLLAPSED) {
        char crumb[128];
        u8str_t text = rubraview_menu_breadcrumb(&app->menu, crumb, sizeof(crumb));
        /* Beside the anchor, on its row: the grid is below or above it now. */
        rubraview_rect_t anchor = rubraview_box_anchor_rect(&app->menubox, &metrics);
        rubraview_rect_t pin = rubraview_box_pin_rect(&app->menubox, &metrics);
        double after = pin.x > anchor.x ? pin.x + pin.width : anchor.x + anchor.width;   /* past the pin */
        rubraview_pal_rect_t label = { after + metrics.gutter, anchor.y,
                                       metrics.tile_size * 6.0, anchor.height };
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

        /* The box rescue button was hit-tested but never drawn; now it shows. */
        static const rubraview_titlebar_button_t BUTTONS[] = {
            RUBRAVIEW_TITLEBAR_SNAP_BOXES, RUBRAVIEW_TITLEBAR_PIN,
            RUBRAVIEW_TITLEBAR_MINIMIZE, RUBRAVIEW_TITLEBAR_MAXIMIZE,
            RUBRAVIEW_TITLEBAR_FULLSCREEN, RUBRAVIEW_TITLEBAR_CLOSE,
        };
        static const char *const GLYPHS[] = { "Box", "Pin", "_", "[]", "[ ]", "X" };
        bool on_top = rubraview_settings_get(&app->settings, U8("general"), U8("always_on_top")) > 0.5;
        for (size_t i = 0; i < sizeof(BUTTONS) / sizeof(BUTTONS[0]); ++i) {
            rubraview_rect_t r = rubraview_titlebar_button_rect(&app->titlebar, BUTTONS[i], win_w);
            rubraview_pal_rect_t br = { r.x, r.y, r.width, r.height };
            bool hovered = rubraview_rect_contains(r, app->pointer_x, app->pointer_y);
            if (BUTTONS[i] == RUBRAVIEW_TITLEBAR_PIN && on_top) {
                /* On: lit, so the state is visible at a glance. */
                rubraview_pal_render_fill_rect(app->renderer, br, 0xFF2D4A6Eu, 0.0);
            }
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
    draw_timeline(app, win_w, win_h);

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

/* RV-076: the cover, blurred, filling the window behind the track — the
   thing that makes a music player look like one rather than like a
   viewer with nothing on screen. Nothing is drawn for a film, or for a
   track whose file carries no cover. */
static void draw_music_backdrop(app_state_t *app, int32_t win_w, int32_t win_h) {
    if (!app->media || app->media_info.has_video || !app->music_backdrop) return;
    rubraview_mat3x2_t place = rubraview_mat3x2_scale((double)win_w / 64.0, (double)win_h / 64.0);
    rubraview_pal_render_draw_texture_opacity(app->renderer, app->music_backdrop, place,
                                              RUBRAVIEW_INTERP_LINEAR, 0.45);
}

/* What the file says about the track, along the bottom: the name and who
   made it, then the record it came from, then what the sound itself is.
   A line the file does not answer is left out rather than shown empty. */
static void draw_music_words(app_state_t *app, int32_t win_w, int32_t win_h) {
    if (!app->media || app->media_info.has_video || !app->music_tags_read) return;
    const rubraview_tags_t *t = &app->music_tags;
    if (t->title.len == 0 && t->artist.len == 0 && t->album.len == 0 && t->codec.len == 0) return;

    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double size = 17.0 * dpi;

    /* The block sits **above** the seek bar and the status line, not
       across them: how many lines there are is known first, and the
       whole thing is placed from the bottom up. */
    bool has_name = t->title.len > 0 || t->artist.len > 0;
    bool has_record = t->album.len > 0 || t->year.len > 0 || t->track_number.len > 0;
    double height = (has_name ? size * 1.6 : 0.0) + (has_record ? size * 1.4 : 0.0) + size * 1.4;
    double chrome = 56.0 * dpi + (app->filmstrip.visible ? FILMSTRIP_THUMB : 0.0);
    double bottom = (double)win_h - chrome - height - 8.0 * dpi;
    if (bottom < 0.0) bottom = 0.0;

    char line[320];
    int n = 0;
    /* The name, and who made it. */
    if (has_name) {
        if (t->title.len > 0 && t->artist.len > 0) {
            n = snprintf(line, sizeof(line), "%.*s  —  %.*s",
                         (int)t->title.len, t->title.ptr, (int)t->artist.len, t->artist.ptr);
        } else {
            u8str_t one = t->title.len > 0 ? t->title : t->artist;
            n = snprintf(line, sizeof(line), "%.*s", (int)one.len, one.ptr);
        }
        if (n > 0) {
            rubraview_pal_rect_t rect = { 0.0, bottom, (double)win_w, size * 1.6 };
            rubraview_pal_render_draw_text(app->renderer, (u8str_t){ .ptr = line, .len = (size_t)n },
                                           rect, size, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
            bottom += size * 1.6;
        }
    }
    /* The record, its year, and the track's number on it. */
    if (has_record) {
        n = snprintf(line, sizeof(line), "%.*s%s%.*s%s%s%.*s",
                     (int)t->album.len, t->album.ptr,
                     t->album.len > 0 && t->year.len > 0 ? "  (" : "",
                     (int)t->year.len, t->year.ptr,
                     t->album.len > 0 && t->year.len > 0 ? ")" : "",
                     t->track_number.len > 0 ? "   no. " : "",
                     (int)t->track_number.len, t->track_number.ptr);
        if (n > 0) {
            rubraview_pal_rect_t rect = { 0.0, bottom, (double)win_w, size * 1.4 };
            rubraview_pal_render_draw_text(app->renderer, (u8str_t){ .ptr = line, .len = (size_t)n },
                                           rect, size * 0.8, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
            bottom += size * 1.4;
        }
    }
    /* What the sound is. */
    {
        char what[160];
        int w = 0;
        if (t->codec.len > 0) w += snprintf(what + w, sizeof(what) - (size_t)w, "%.*s", (int)t->codec.len, t->codec.ptr);
        if (t->bitrate_kbps > 0) w += snprintf(what + w, sizeof(what) - (size_t)w, "%s%u kbps", w > 0 ? "  ·  " : "", t->bitrate_kbps);
        if (t->sample_rate > 0) {
            w += snprintf(what + w, sizeof(what) - (size_t)w, "%s%.1f kHz", w > 0 ? "  ·  " : "",
                          (double)t->sample_rate / 1000.0);
        }
        if (t->bits_per_sample > 0) w += snprintf(what + w, sizeof(what) - (size_t)w, "  ·  %u bit", t->bits_per_sample);
        if (t->channels > 0) {
            w += snprintf(what + w, sizeof(what) - (size_t)w, "  ·  %s",
                          t->channels == 1 ? "mono" : t->channels == 2 ? "stereo" : "multi");
        }
        if (w > 0) {
            rubraview_pal_rect_t rect = { 0.0, bottom, (double)win_w, size * 1.4 };
            rubraview_pal_render_draw_text(app->renderer, (u8str_t){ .ptr = what, .len = (size_t)w },
                                           rect, size * 0.75, 0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
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
                if (cmd->page_index == app->media_page) {
                    /* A DVD's picture subtitles are drawn in the film's
                       own frame, so they need the film's place on screen. */
                    app->video_transform = oriented;
                    app->video_transform_ok = true;
                    app->video_page_w = page->width;
                    app->video_page_h = page->height;
                }
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

/* §3.18.2's inline rename. The box is drawn here, not a child EDIT
   control; its text comes from the window's text input (TEXT events,
   with the IME attached only while the box is open, K5), so Hangul,
   capitals and symbols go in as typed. No selection or clipboard yet. */
/* The box closes: the window goes back to keys only (K5). */
static void rename_end(app_state_t *app) {
    app->rename_active = false;
    app->rename_is_extension = false;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, false);
}

/* Text typed or finished by the IME, and the IME's unfinished part. */
static void rename_text(app_state_t *app, const rubraview_window_event_t *event) {
    if (app->ab_edit_open) {
        if (event->kind == RUBRAVIEW_WINDOW_EVENT_TEXT) {
            ab_edit_text_typed(app, (u8str_t){ .ptr = event->text.utf8, .len = event->text.length });
        }
        return;
    }
    if (!app->rename_active) return;
    u8str_t text = { .ptr = event->text.utf8, .len = event->text.length };
    if (event->kind == RUBRAVIEW_WINDOW_EVENT_TEXT) {
        (void)rubraview_rename_append(app->rename_buffer, sizeof(app->rename_buffer), &app->rename_length, text);
    } else {
        size_t n = text.len < sizeof(app->rename_composing) ? text.len : sizeof(app->rename_composing) - 1;
        memcpy(app->rename_composing, text.ptr, n);
        app->rename_composing_length = n;
    }
}

static void rename_begin(app_state_t *app) {
    u8str_t path = current_file_path(app);
    if (path.len == 0) return;

    /* The whole name, extension and all (owner, 2026-09-23): an
       extension typed wrongly could not be corrected before, and the
       owner asked for no question when it changes. */
    u8str_t name = rubraview_path_basename(path);
    if (name.len >= sizeof(app->rename_buffer)) return;

    memcpy(app->rename_buffer, name.ptr, name.len);
    app->rename_length = name.len;
    app->rename_buffer[name.len] = '\0';
    app->rename_active = true;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, true);   /* Hangul, capitals, symbols (K5's hole) */
}

/* The picked files all take one extension (owner, 2026-09-23). The box is
   the rename box; what is typed replaces every one of their extensions. */
static void rename_extension_begin(app_state_t *app) {
    size_t picked = 0;
    uint64_t bytes = 0;
    rubraview_picker_selection_metrics(&app->picker, &picked, &bytes);
    if (picked == 0) { osd_say(app, U8("pick some files first")); return; }

    u8str_t ext = picker_focus_extension(app);
    app->rename_length = 0;
    if (ext.len > 0 && ext.len < sizeof(app->rename_buffer)) {
        memcpy(app->rename_buffer, ext.ptr, ext.len);
        app->rename_length = ext.len;
    }
    app->rename_buffer[app->rename_length] = '\0';
    app->rename_active = true;
    app->rename_is_extension = true;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, true);
    osd_say(app, U8("type the extension for the picked files"));
}

/* Renames every picked file to the typed extension, one undo entry each. */
static void rename_extension_commit(app_state_t *app) {
    u8str_t typed = { .ptr = app->rename_buffer, .len = app->rename_length };
    char dotted[64];
    if (typed.len == 0) { rename_end(app); return; }
    if (typed.ptr[0] != '.') {
        if (typed.len + 1 >= sizeof(dotted)) { rename_end(app); return; }
        dotted[0] = '.';
        memcpy(dotted + 1, typed.ptr, typed.len);
        typed = (u8str_t){ .ptr = dotted, .len = typed.len + 1 };
    }

    size_t done = 0, failed = 0;
    for (size_t i = 0; i < app->picker_listing.count; ++i) {
        if (!app->picker_selected || !app->picker_selected[i]) continue;
        const rubraview_fs_entry_t *entry = &app->picker_listing.entries[i];
        if (entry->is_directory) continue;

        char want[64];
        size_t n = typed.len < sizeof(want) - 1 ? typed.len : sizeof(want) - 1;
        memcpy(want, typed.ptr, n);
        want[n] = '\0';
        u8str_t target = rubraview_path_with_ext(app->arena, entry->path, want);
        bool unchanged = target.len == entry->path.len &&
                         memcmp(target.ptr, entry->path.ptr, target.len) == 0;
        if (target.len == 0 || unchanged ||
            rubraview_rename_validate(rubraview_path_basename(target)) != RUBRAVIEW_RENAME_OK) {
            failed++;
            continue;
        }
        if (!rubraview_pal_fs_move(entry->path, target)) { failed++; continue; }
        rubraview_undo_push(&app->undo, (rubraview_file_action_t){
            .op = RUBRAVIEW_FILE_OP_RENAME,
            .source_path = entry->path,
            .target_path = target,
        });
        done++;
    }
    rubraview_undo_commit(&app->undo);

    char line[128];
    int written = failed > 0
        ? snprintf(line, sizeof(line), "%zu renamed to %.*s, %zu could not be", done, (int)typed.len, typed.ptr, failed)
        : snprintf(line, sizeof(line), "%zu file(s) now end in %.*s", done, (int)typed.len, typed.ptr);
    rename_end(app);
    if (written > 0) osd_say(app, (u8str_t){ .ptr = line, .len = (size_t)written });
    picker_navigate(app, app->picker_dir);   /* the names on screen have changed */
}

static void rename_commit(app_state_t *app) {
    if (!app->rename_active) return;
    if (app->rename_is_extension) { rename_extension_commit(app); return; }
    /* Enter with a syllable still being built: it is part of the name. */
    (void)rubraview_rename_append(app->rename_buffer, sizeof(app->rename_buffer), &app->rename_length,
                                  (u8str_t){ .ptr = app->rename_composing, .len = app->rename_composing_length });
    rename_end(app);

    u8str_t path = current_file_path(app);
    if (path.len == 0) return;

    u8str_t new_name = { .ptr = app->rename_buffer, .len = app->rename_length };

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
/* §3.19.2: the files that were dropped, in natural name order, as a
   source of their own. They may come from different folders; the page
   list holds full paths, so that costs nothing. Directories among them
   are left out — rubraview_drop_classify sends a lone folder down the
   other path, and a folder dropped together with files has no obvious
   place in a sequence of pages. */
static void open_dropped_files(app_state_t *app, const rubraview_drop_item_t *items, size_t count) {
    if (count == 0) return;

    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, count, sizeof(rubraview_fs_entry_t));
    if (!proven_is_ok(res.err)) return;
    rubraview_fs_entry_t *entries = (rubraview_fs_entry_t*)(void*)res.value.ptr;

    size_t n = 0;
    for (size_t i = 0; i < count; ++i) {
        if (items[i].is_directory) continue;
        rubraview_fs_entry_t entry;
        if (!rubraview_pal_fs_stat(app->arena, items[i].path, &entry)) continue;
        if (entry.is_directory) continue;
        entries[n++] = entry;
    }
    if (n == 0) return;

    char line[96];
    size_t pos = 0;
    line[0] = '\0';
    append_number(line, sizeof(line), &pos, n);
    append_text(line, sizeof(line), &pos, " dropped files opened as one set");
    open_set_from_entries(app, entries, n, cstr(line));
}

/* A given set of files becomes the page sequence — a drop, or what the
   picker picked. `said` goes to the OSD when it opens. */
static void open_set_from_entries(app_state_t *app, rubraview_fs_entry_t *entries, size_t count, u8str_t said) {
    if (!entries || count == 0) return;

    history_remember(app);
    rubraview_page_source_close(&app->source);
    app->archive_bytes = (u8str_t){ .ptr = "", .len = 0 };

    rubraview_fs_listing_t listing = { .entries = entries, .count = count };
    app->source = rubraview_page_source_from_listing(app->arena, &listing,
                                                     U8(IMAGE_FILTER ";" MEDIA_FILTER),
                                                     RUBRAVIEW_SORT_NAME_NATURAL, true);
    if (app->source.page_count == 0) {
        osd_say(app, U8("none of those files is a picture or a film"));
        return;
    }
    /* The folder of the first one is where Rename, Delete and a batch
       run work; a set of files has no folder of its own. */
    app->source_dir = rubraview_path_dirname(entries[0].path);
    app->resume_offer = false;
    app->picker_open = false;
    finish_open(app, 0);
    osd_say(app, said);
}

static void handle_drop(app_state_t *app, const rubraview_window_event_t *event) {
    rubraview_drop_item_t items[16];
    size_t count = event->drop.count < 16 ? event->drop.count : 16;
    if (count == 0) return;   /* nothing was dropped: there is no first item to open */

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
            /* §3.19.2: several things dropped together become a sequence
               of exactly those things — not the folder they came from,
               which would show files the reader did not drop. */
            open_dropped_files(app, items, count);
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
            panel_run_batch(app);
            panel_close(app);
            break;
        default: break;
    }
}

static bool panel_handle_press(app_state_t *app, double px, double py) {
    if (!app->panel.open) return false;

    /* The curve box is drawn outside the panel's own rectangle, so the
       rule below ("a click outside closes it") would shut the workbench
       the moment a point was grabbed. It is asked first. */
    if (curve_widget_press(app, px, py, false)) return true;

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


/* ---- the settings window (§3.22), RV-082, D-13 ---- */

/* §3.22.1 asks for a separate top-level window, and since D-13 that is
   what it is: a window owned by the viewer's, drawn by its own renderer,
   pumped by the same loop. What is on it is not written here control by
   control — the settings document says what each page holds, the
   interpreter (ui_settings) lays it out on a grid of fixed-width cells
   and turns keys and clicks into changes, and this code only prints the
   lines it is given and does what an event names.

   A change applies to the viewer at once; the file is written when the
   window closes; Revert returns to what the file held when it opened
   (owner, 2026-09-13, "1a"). */

/* §3.22.1: reads settings.ini — portable mode beside the executable,
   otherwise under AppData, the same rule the reading history follows,
   so the two files never end up in different places.

   This runs at startup, not only when the settings window is opened.
   It used to run only there, which meant a settings.ini on disk changed
   nothing until the reader pressed F10: every setting read while
   viewing came back 0. */
static void settings_read_file(app_state_t *app) {
    /* §3.17.2: a settings.ini *beside the executable* means portable mode,
       and then nothing is written to the host machine. Beside the
       executable, not in the folder the viewer was started in: until
       2026-09-23 this asked for the plain name `settings.ini`, so both
       portable mode and §3.18.3's triage folders followed the working
       directory — which is why the number keys curated nothing. */
    u8str_t exe = rubraview_pal_process_executable(app->arena);
    u8str_t beside = exe.len > 0 ? rubraview_path_dirname(exe) : U8(".");
    if (beside.len == 0) beside = U8(".");
    u8str_t portable_ini = rubraview_path_join(app->arena, beside, U8("settings.ini"));
    app->config_mode = rubraview_config_mode_for(portable_ini.len > 0 &&
                                                 rubraview_pal_fs_exists(portable_ini));
    app->config_beside = beside;

    u8str_t appdata = U8(".");
#ifdef _WIN32
    char appdata_utf8[1024];
    DWORD written = GetEnvironmentVariableA("APPDATA", appdata_utf8, (DWORD)sizeof(appdata_utf8));
    if (written > 0 && written < sizeof(appdata_utf8)) {
        appdata = (u8str_t){ .ptr = appdata_utf8, .len = written };
    }
#endif
    app->settings_path = rubraview_config_path(app->arena, app->config_mode,
                                               beside, appdata, U8("settings.ini"));
    app->keymap_path = rubraview_config_path(app->arena, app->config_mode,
                                             beside, appdata, U8("keymap.ini"));

    u8str_t text = rubraview_pal_fs_read_file(app->arena, app->settings_path, 256u * 1024u);
    app->settings = rubraview_settings_load(app->arena, text);
    app->settings_saved = app->settings;
}


#define SETTINGS_BACKGROUND  0xFF161616u
#define SETTINGS_PANE        0xFF1E1E1Eu
#define SETTINGS_FOCUS       0xFF2D4A6Eu
#define SETTINGS_TEXT        0xFFE8E8E8u
#define SETTINGS_DIM         0xFF8A8A8Au   /* a setting nothing reads yet (not `wired`) */
#define SETTINGS_ACCENT      0xFF6FA8DCu

/* The live values the document's `info` lines name. */
static size_t settings_info(void *user, u8str_t source, char *buffer, size_t capacity) {
    app_state_t *app = (app_state_t*)user;
    int n = 0;
    if (rubraview_u8_eq_lit(source, "config.path")) {
        n = snprintf(buffer, capacity, "%.*s%s", (int)app->settings_path.len, app->settings_path.ptr,
                     app->config_mode == RUBRAVIEW_CONFIG_PORTABLE ? "  (portable)" : "");
    } else if (rubraview_u8_eq_lit(source, "media.ffmpeg")) {
        n = snprintf(buffer, capacity, "%s",
                     rubraview_pal_media_backend_available(RUBRAVIEW_BACKEND_FFMPEG)
                         ? "its DLLs are beside the program" : "not found (or not 8.x) - Media Foundation only");
    } else if (rubraview_u8_eq_lit(source, "gpu.adapter")) {
        n = snprintf(buffer, capacity, "run rubraview --probe-gpu <video> for the details");
    } else if (rubraview_u8_eq_lit(source, "cache.used")) {
        size_t loaded = 0;
        for (size_t i = 0; i < page_count(app); ++i) if (app->pages[i].texture) loaded++;
        n = snprintf(buffer, capacity, "%zu page(s) decoded and kept", loaded);
    }
    if (n <= 0) return 0;
    return (size_t)n < capacity ? (size_t)n : capacity - 1;
}

/* The rows the document's `table` lines name. */
static size_t settings_table_row(void *user, u8str_t source, size_t index, char *buffer, size_t capacity) {
    app_state_t *app = (app_state_t*)user;
    if (!rubraview_u8_eq_lit(source, "keymap")) return 0;
    if (index == 0) {
        int n = snprintf(buffer, capacity, "%-12s %-24s %s", "context", "action", "keys  (Enter adds a key, Delete takes the last off)");
        return n > 0 ? (size_t)n < capacity ? (size_t)n : capacity - 1 : 0;
    }
    if (index - 1 >= app->keymap.count) return 0;
    const rubraview_key_binding_t *b = &app->keymap.bindings[index - 1];
    char keys[160];
    size_t used = 0;
    keys[0] = '\0';
    if (app->key_capture == (int32_t)index) {
        used = (size_t)snprintf(keys, sizeof(keys), "> press a key (Esc: leave it)");
    } else {
        for (size_t c = 0; c < b->combo_count && used + 32 < sizeof(keys); ++c) {
            char one[64];
            u8str_t text = rubraview_key_combo_format(one, sizeof(one), b->combos[c]);
            int n = snprintf(keys + used, sizeof(keys) - used, "%s%.*s", c ? ", " : "", (int)text.len, text.ptr);
            if (n > 0) used += (size_t)n;
        }
        /* §3.22.2: a key another action also claims where the two meet —
           only one of them is ever reached. Named, not fixed (D-14). */
        for (size_t i = 0; i < app->keymap.count && used + 40 < sizeof(keys); ++i) {
            const rubraview_key_binding_t *o = &app->keymap.bindings[i];
            if (i == index - 1 || !rubraview_keymap_contexts_meet(o->context, b->context)) continue;
            bool clash = false;
            for (size_t x = 0; x < b->combo_count && !clash; ++x)
                for (size_t y = 0; y < o->combo_count && !clash; ++y)
                    clash = rubraview_key_combo_equal(b->combos[x], o->combos[y]);
            if (!clash) continue;
            int n = snprintf(keys + used, sizeof(keys) - used, "  ! also %.*s", (int)o->action.len, o->action.ptr);
            if (n > 0) used += (size_t)n;
            break;
        }
        if (used >= sizeof(keys)) used = sizeof(keys) - 1;
        if (b->combo_count == 0 && used == 0) used = (size_t)snprintf(keys, sizeof(keys), "(no key)");
    }
    int n = snprintf(buffer, capacity, "%-12.*s %-24.*s %.*s",
                     b->context.len ? (int)b->context.len : 2, b->context.len ? b->context.ptr : "ui",
                     (int)b->action.len, b->action.ptr, (int)used, keys);
    if (n <= 0) return 0;
    return (size_t)n < capacity ? (size_t)n : capacity - 1;
}

static void settings_measure(app_state_t *app) {
    double dpi = rubraview_pal_window_dpi_scale(app->settings_window);
    app->settings_font = 15.0 * dpi;
    if (!rubraview_pal_render_mono_cell(app->settings_renderer, app->settings_font,
                                        &app->settings_cell_w, &app->settings_cell_h) ||
        app->settings_cell_w <= 0.0 || app->settings_cell_h <= 0.0) {
        app->settings_cell_w = 8.3 * dpi;     /* Consolas at 15 px, if measuring failed */
        app->settings_cell_h = 17.6 * dpi;
    }
}

static void settings_grid(const app_state_t *app, int32_t *out_cols, int32_t *out_rows) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->settings_window, &w, &h);
    *out_cols = (int32_t)((double)w / app->settings_cell_w);
    *out_rows = (int32_t)((double)h / app->settings_cell_h);
    if (*out_cols < 40) *out_cols = 40;
    if (*out_rows < 12) *out_rows = 12;
}

static void settings_open(app_state_t *app) {
    if (app->settings_open) return;
    /* What changed outside the window goes to disk first, then what is on
       disk now is read — another copy of the program may have written it. */
    settings_write_if_changed(app, false);
    settings_read_file(app);
    settings_took_effect(app);
    app->keymap_saved = rubraview_keymap_copy(app->arena, &app->keymap);
    app->key_capture = 0;

    if (app->settings_window) {
        /* Made on the first F10 and hidden on close: shown again as it was. */
        rubraview_pal_window_set_visible(app->settings_window, true);
        settings_measure(app);
        int32_t cols = 0, rows = 0;
        settings_grid(app, &cols, &rows);
        rubraview_settings_view_resize(&app->settings_view, cols, rows);
        app->settings_open = true;
        app->settings_dirty = true;
        app->settings_mouse_down = false;
        app->settings_message[0] = '\0';
        return;
    }

    rubraview_window_config_t config = {
        .title = "Rubraview settings", .width = 980, .height = 680, .owner = app->window,
    };
    app->settings_window = rubraview_pal_window_create(app->arena, &config);
    if (!app->settings_window) {
        osd_say(app, U8("the settings window could not be opened"));
        return;
    }
    if (app->settings_frame[2] > 0 && app->settings_frame[3] > 0) {
        rubraview_pal_window_set_frame(app->settings_window, app->settings_frame[0], app->settings_frame[1],
                                       app->settings_frame[2], app->settings_frame[3]);
    }
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->settings_window, &w, &h);
    app->settings_renderer = rubraview_pal_render_create(app->arena,
                                                         rubraview_pal_window_native_handle(app->settings_window), w, h);
    if (!app->settings_renderer) {
        rubraview_pal_window_destroy(app->settings_window);
        app->settings_window = NULL;
        osd_say(app, U8("the settings window could not be drawn"));
        return;
    }
    settings_measure(app);
    int32_t cols = 0, rows = 0;
    settings_grid(app, &cols, &rows);
    app->settings_view = rubraview_settings_view_create(rubraview_settings_document(), cols, rows);
    rubraview_settings_view_set_table_rows(&app->settings_view, (int32_t)app->keymap.count + 1);  /* heading + bindings */
    app->settings_open = true;
    app->settings_dirty = true;
    app->settings_mouse_down = false;
    app->settings_message[0] = '\0';
}

/* settings.ini is written when something in it changed: as the settings
   window closes (1a), before it opens again, and as the viewer quits —
   the volume and the boxes' opacity change outside the window (D-15). */
static void settings_write_if_changed(app_state_t *app, bool say) {
    if (!rubraview_settings_differs(&app->settings, &app->settings_saved)) return;
    u8str_t existing = rubraview_pal_fs_read_file(app->arena, app->settings_path, 256u * 1024u);
    u8str_t text = rubraview_settings_save(app->arena, &app->settings, existing);
    if (text.len > 0 && rubraview_pal_fs_write_file(app->settings_path, text)) {
        app->settings_saved = app->settings;
        /* §3.18.3's folders are read from the same file, so they follow
           at once rather than at the next launch. */
        app->curation = rubraview_curation_parse(app->arena, text);
        if (say) osd_say(app, U8("settings saved"));
    } else if (say) {
        osd_say(app, U8("could not write settings.ini"));
    }
}

/* 1a: the file is written as the window closes, and only when something
   in it changed. */
static void settings_close(app_state_t *app) {
    if (!app->settings_open) return;
    settings_write_if_changed(app, true);
    app->key_capture = 0;
    if (!rubraview_keymap_equal(&app->keymap, &app->keymap_saved)) {
        u8str_t text = rubraview_keymap_serialize(app->arena, &app->keymap);
        if (text.len > 0 && rubraview_pal_fs_write_file(app->keymap_path, text)) {
            app->keymap_saved = rubraview_keymap_copy(app->arena, &app->keymap);
            osd_say(app, U8("keys saved to keymap.ini"));
        } else {
            osd_say(app, U8("could not write keymap.ini"));
        }
    }
    int32_t *f = app->settings_frame;
    if (!rubraview_pal_window_get_frame(app->settings_window, &f[0], &f[1], &f[2], &f[3])) f[2] = f[3] = 0;
    rubraview_pal_window_set_visible(app->settings_window, false);
    app->settings_open = false;
    layout_save(app);
}

/* What a change means for the viewer right now, beyond what it reads
   every frame (the subtitle size and outline). */
static void settings_took_effect(app_state_t *app) {
    app->media_preferred = rubraview_settings_get(&app->settings, U8("video"), U8("decoder")) > 0.5
        ? RUBRAVIEW_BACKEND_FFMPEG : RUBRAVIEW_BACKEND_MEDIA_FOUNDATION;
    /* Owner, 2026-09-15: always on top follows the setting, whichever way it was changed. */
    rubraview_pal_window_set_topmost(app->window,
                                     rubraview_settings_get(&app->settings, U8("general"), U8("always_on_top")) > 0.5);
    /* D-15: the session volume follows the setting. */
    rubraview_pal_audio_set_volume(rubraview_settings_get(&app->settings, U8("audio"), U8("volume")) / 100.0,
                                   rubraview_settings_get(&app->settings, U8("audio"), U8("mute")) > 0.5);
}

/* §3.22.2: a keymap written out to share or keep, or read back in. What
   is imported replaces the keys shown on the Keys page only — Revert still
   goes back to keymap.ini, and keymap.ini is written as the window closes,
   like a key changed by hand. */
static void keys_export_or_import(app_state_t *app, bool writing) {
    static const rubraview_file_filter_t FILTERS[] = { { "Keymap (*.ini)", "*.ini" }, { "All files", "*.*" } };
    rubraview_file_dialog_opts_t opts = {
        .title = writing ? "Export keys" : "Import keys",
        .filters = FILTERS, .filter_count = 2,
        .parent_window_handle = rubraview_pal_window_native_handle(app->settings_window),
    };
    app->key_capture = 0;
    if (writing) {
        rubraview_file_dialog_result_t picked = rubraview_pal_file_dialog_save(app->arena, &opts);
        if (!picked.accepted || picked.count == 0) return;
        u8str_t text = rubraview_keymap_serialize(app->arena, &app->keymap);
        settings_say(app, text.len > 0 && rubraview_pal_fs_write_file(picked.paths[0], text)
                              ? "keys exported" : "could not write that file");
        return;
    }
    rubraview_file_dialog_result_t picked = rubraview_pal_file_dialog_open(app->arena, &opts);
    if (!picked.accepted || picked.count == 0) return;
    u8str_t text = rubraview_pal_fs_read_file(app->arena, picked.paths[0], KEYMAP_MAX_BYTES);
    rubraview_keymap_t keymap = text.len > 0 ? rubraview_keymap_parse(app->arena, text) : (rubraview_keymap_t){0};
    if (keymap.count == 0) {
        settings_say(app, "that file holds no keys; nothing changed");
        return;
    }
    app->keymap = keymap;
    rubraview_settings_view_set_table_rows(&app->settings_view, (int32_t)app->keymap.count + 1);
    settings_say(app, "keys imported (Revert undoes this)");
}

static void settings_say(app_state_t *app, const char *text) {
    snprintf(app->settings_message, sizeof(app->settings_message), "%s", text);
}

static void settings_event(app_state_t *app, rubraview_settings_event_t event) {
    switch (event) {
        case RUBRAVIEW_SEVENT_NONE:
            return;
        case RUBRAVIEW_SEVENT_MOVED:
            break;
        case RUBRAVIEW_SEVENT_CHANGED:
            settings_took_effect(app);
            app->settings_message[0] = '\0';
            break;
        case RUBRAVIEW_SEVENT_REVERT: {
            uint32_t total = app->settings.revision_total;
            app->settings = app->settings_saved;
            app->settings.revision_total = total + 1;
            app->keymap = rubraview_keymap_copy(app->arena, &app->keymap_saved);
            rubraview_settings_view_set_table_rows(&app->settings_view, (int32_t)app->keymap.count + 1);
            app->key_capture = 0;
            settings_took_effect(app);
            settings_say(app, "back to what settings.ini held when this window opened");
            break;
        }
        case RUBRAVIEW_SEVENT_DEFAULTS:
            rubraview_settings_reset(&app->settings);
            app->keymap = rubraview_keymap_parse(app->arena, cstr(rubraview_default_keymap()));
            rubraview_settings_view_set_table_rows(&app->settings_view, (int32_t)app->keymap.count + 1);
            app->key_capture = 0;
            settings_took_effect(app);
            settings_say(app, "every setting and key is at its default (Revert undoes this)");
            break;
        case RUBRAVIEW_SEVENT_CLOSE:
            settings_close(app);
            return;
        case RUBRAVIEW_SEVENT_TABLE_EDIT:
        case RUBRAVIEW_SEVENT_TABLE_CLEAR: {
            int32_t row = rubraview_settings_view_focused_table_row(&app->settings_view);
            if (row < 1 || (size_t)row > app->keymap.count) break;
            const rubraview_key_binding_t *b = &app->keymap.bindings[row - 1];
            char text[160];
            if (event == RUBRAVIEW_SEVENT_TABLE_EDIT) {
                app->key_capture = row;
                snprintf(text, sizeof(text), "press the key to add to %.*s (Esc: leave it as it is)",
                         (int)b->action.len, b->action.ptr);
            } else if (b->combo_count == 0) {
                snprintf(text, sizeof(text), "%.*s has no key to take off", (int)b->action.len, b->action.ptr);
            } else {
                char one[64];
                u8str_t last = rubraview_key_combo_format(one, sizeof(one), b->combos[b->combo_count - 1]);
                rubraview_keymap_unbind_last(&app->keymap, (size_t)row - 1);
                snprintf(text, sizeof(text), "took %.*s off %.*s (Revert puts it back)",
                         (int)last.len, last.ptr, (int)b->action.len, b->action.ptr);
            }
            settings_say(app, text);
            break;
        }
        case RUBRAVIEW_SEVENT_EDIT_TEXT:
        case RUBRAVIEW_SEVENT_CLEAR_TEXT: {
            const rubraview_settings_node_t *node = rubraview_settings_view_focused_node(&app->settings_view);
            if (!node || node->kind != RUBRAVIEW_NODE_SETTING) break;
            const rubraview_setting_def_t *def = &app->settings_view.doc->defs[node->setting];
            if (event == RUBRAVIEW_SEVENT_CLEAR_TEXT) {
                rubraview_settings_set_text(&app->settings, def->section, def->key, U8(""));
                settings_say(app, "folder cleared");
                break;
            }
            rubraview_file_dialog_opts_t opts = {
                .title = "Choose a folder", .folder_mode = true,
                .parent_window_handle = rubraview_pal_window_native_handle(app->settings_window),
            };
            rubraview_file_dialog_result_t picked = rubraview_pal_file_dialog_pick_folder(app->arena, &opts);
            if (picked.accepted && picked.count > 0) {
                rubraview_settings_set_text(&app->settings, def->section, def->key, picked.paths[0]);
                settings_say(app, "folder chosen (Delete clears it)");
            }
            break;
        }
        case RUBRAVIEW_SEVENT_ACTION: {
            const rubraview_settings_node_t *node = rubraview_settings_view_focused_node(&app->settings_view);
            if (!node) break;
            if (rubraview_u8_eq_lit(node->name, "shell.register")) {
                settings_say(app, rubraview_pal_shell_register(rubraview_shell_extensions())
                                      ? "file types registered" : "could not register the file types");
            } else if (rubraview_u8_eq_lit(node->name, "shell.unregister")) {
                settings_say(app, rubraview_pal_shell_unregister(rubraview_shell_extensions())
                                      ? "file types removed" : "could not remove the file types");
            } else if (rubraview_u8_eq_lit(node->name, "keys.export") || rubraview_u8_eq_lit(node->name, "keys.import")) {
                keys_export_or_import(app, rubraview_u8_eq_lit(node->name, "keys.export"));
            }
            break;
        }
    }
    app->settings_dirty = true;
}

static bool settings_key_of(rubraview_key_combo_t combo, rubraview_settings_key_t *out) {
    static const struct { const char *name; rubraview_settings_key_t key; } KEYS[] = {
        { "Up", RUBRAVIEW_SKEY_UP }, { "Down", RUBRAVIEW_SKEY_DOWN },
        { "Left", RUBRAVIEW_SKEY_LEFT }, { "Right", RUBRAVIEW_SKEY_RIGHT },
        { "PageUp", RUBRAVIEW_SKEY_PAGE_UP }, { "PageDown", RUBRAVIEW_SKEY_PAGE_DOWN },
        { "Space", RUBRAVIEW_SKEY_SPACE }, { "Enter", RUBRAVIEW_SKEY_ENTER },
        { "Home", RUBRAVIEW_SKEY_HOME }, { "End", RUBRAVIEW_SKEY_END },
        { "Escape", RUBRAVIEW_SKEY_ESCAPE }, { "Delete", RUBRAVIEW_SKEY_DELETE },
        { "Backspace", RUBRAVIEW_SKEY_DELETE },
    };
    if (key_is(combo, "Tab")) {
        *out = (combo.modifiers & RUBRAVIEW_MOD_SHIFT) ? RUBRAVIEW_SKEY_SHIFT_TAB : RUBRAVIEW_SKEY_TAB;
        return true;
    }
    for (size_t i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); ++i) {
        if (key_is(combo, KEYS[i].name)) { *out = KEYS[i].key; return true; }
    }
    return false;
}

/* The key pressed for a Keys row (D-14): added, or refused with the name
   of the action that has it — never taken away from it (owner, 2026-09-14). */
static void settings_capture_key(app_state_t *app, rubraview_key_combo_t combo) {
    size_t index = (size_t)app->key_capture - 1;
    app->key_capture = 0;
    if (index >= app->keymap.count) return;
    if (combo.modifiers == 0 && rubraview_u8_eq_lit(combo.key_name, "Escape")) {
        settings_say(app, "no key added");
        return;
    }
    char one[64], text[200];
    u8str_t name = rubraview_key_combo_format(one, sizeof(one), combo);
    const rubraview_key_binding_t *holder = NULL;
    rubraview_bind_result_t result = rubraview_keymap_bind(app->arena, &app->keymap, index, combo, &holder);
    const rubraview_key_binding_t *b = &app->keymap.bindings[index];
    switch (result) {
        case RUBRAVIEW_BIND_ADDED:
            snprintf(text, sizeof(text), "%.*s added to %.*s", (int)name.len, name.ptr, (int)b->action.len, b->action.ptr);
            break;
        case RUBRAVIEW_BIND_ALREADY:
            snprintf(text, sizeof(text), "%.*s already has %.*s", (int)b->action.len, b->action.ptr, (int)name.len, name.ptr);
            break;
        case RUBRAVIEW_BIND_TAKEN:
            snprintf(text, sizeof(text), "not changed: %.*s is %.*s's key%s%.*s%s", (int)name.len, name.ptr,
                     (int)holder->action.len, holder->action.ptr,
                     holder->context.len ? " (" : "", (int)holder->context.len, holder->context.ptr,
                     holder->context.len ? ")" : "");
            break;
        case RUBRAVIEW_BIND_FAILED:
        default:
            snprintf(text, sizeof(text), "could not add %.*s", (int)name.len, name.ptr);
            break;
    }
    settings_say(app, text);
}

/* Everything the settings window's queue holds; returns how many. */
static size_t settings_pump(app_state_t *app) {
    size_t handled = 0;
    rubraview_window_event_t event;
    while (app->settings_open && rubraview_pal_window_poll_event(app->settings_window, &event)) {
        handled++;
        rubraview_settings_view_t *view = &app->settings_view;
        switch (event.kind) {
            case RUBRAVIEW_WINDOW_EVENT_CLOSE:
                settings_close(app);
                return handled;
            case RUBRAVIEW_WINDOW_EVENT_RESIZE: {
                rubraview_pal_render_resize(app->settings_renderer, event.resize.width, event.resize.height);
                int32_t cols = 0, rows = 0;
                settings_grid(app, &cols, &rows);
                rubraview_settings_view_resize(view, cols, rows);
                app->settings_dirty = true;
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_DPI_CHANGED: {
                settings_measure(app);
                int32_t cols = 0, rows = 0;
                settings_grid(app, &cols, &rows);
                rubraview_settings_view_resize(view, cols, rows);
                app->settings_dirty = true;
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_PAINT:
                app->settings_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN: {
                if (app->key_capture > 0) {
                    settings_capture_key(app, event.key.combo);
                    app->settings_dirty = true;
                    break;
                }
                rubraview_settings_key_t key;
                if (settings_key_of(event.key.combo, &key)) {
                    settings_event(app, rubraview_settings_view_key(view, &app->settings, key));
                }
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN:
                if (event.mouse.button != RUBRAVIEW_MOUSE_LEFT) break;
                if (app->key_capture > 0) {
                    app->key_capture = 0;   /* a click elsewhere is a change of mind */
                    settings_say(app, "no key added");
                }
                app->settings_mouse_down = true;
                settings_event(app, rubraview_settings_view_press(view, &app->settings,
                                   (int32_t)(event.mouse.x / app->settings_cell_w),
                                   (int32_t)(event.mouse.y / app->settings_cell_h)));
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE:
                if (app->settings_mouse_down) {
                    settings_event(app, rubraview_settings_view_drag(view, &app->settings,
                                       (int32_t)(event.mouse.x / app->settings_cell_w)));
                }
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_UP:
                app->settings_mouse_down = false;
                rubraview_settings_view_release(view);
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_WHEEL:
                settings_event(app, rubraview_settings_view_scroll(view, event.mouse.wheel_delta > 0 ? -3 : 3));
                break;
            default:
                break;
        }
        if (!app->settings_open) break;
    }
    return handled;
}

/* A `preview subtitle` line: the sample drawn the way the video will
   draw it, at the size and outline on the page right now. */
static void settings_preview(app_state_t *app, u8str_t name, rubraview_pal_rect_t rect) {
    rubraview_renderer_t *r = app->settings_renderer;
    rubraview_pal_render_fill_rect(r, rect, 0xFF303A44u, 0.0);
    if (!rubraview_u8_eq_lit(name, "subtitle")) return;
    double dpi = rubraview_pal_window_dpi_scale(app->settings_window);
    double size = rubraview_settings_get(&app->settings, U8("video"), U8("subtitle_size")) * dpi;
    double off = rubraview_settings_get(&app->settings, U8("video"), U8("subtitle_outline")) * dpi;
    u8str_t sample = U8("Subtitle sample  \xEC\x9E\x90\xEB\xA7\x89 \xEB\xAF\xB8\xEB\xA6\xAC\xEB\xB3\xB4\xEA\xB8\xB0");
    /* A size too tall for the block is drawn as tall as the block
       allows — the video's own subtitle shows the real size. */
    if (size > rect.height * 0.6) size = rect.height * 0.6;
    draw_outlined_text(r, sample, rect, size, off, 0xFFFFFFFFu);
}

/* Text on the settings grid, each narrow run and each wide character at
   its own cell, so Hangul in a folder name does not pull the rest of the
   line off its columns. */
static void settings_text(app_state_t *app, u8str_t text, double x, double y, uint32_t color) {
    rubraview_cell_run_t runs[256];
    size_t n = rubraview_cell_runs(text, runs, sizeof(runs) / sizeof(runs[0]));
    for (size_t i = 0; i < n; ++i) {
        u8str_t piece = { .ptr = text.ptr + runs[i].offset, .len = runs[i].length };
        if (piece.len == 1 && piece.ptr[0] == ' ') continue;
        rubraview_pal_render_draw_text_mono(app->settings_renderer, piece, x + app->settings_cell_w * runs[i].col,
                                            y, app->settings_font, color);
    }
}

/* ---- the help window (owner, 2026-09-23) ---- */

#define HELP_BACKGROUND 0xFF141414u
#define HELP_HEADING    0xFF6FA8DCu
#define HELP_KEYS       0xFFE8C46Fu
#define HELP_TEXT       0xFFE8E8E8u

static void help_measure(app_state_t *app) {
    double dpi = rubraview_pal_window_dpi_scale(app->help_window);
    app->help_font = 15.0 * dpi;
    if (!rubraview_pal_render_mono_cell(app->help_renderer, app->help_font,
                                        &app->help_cell_w, &app->help_cell_h) ||
        app->help_cell_w <= 0.0 || app->help_cell_h <= 0.0) {
        app->help_cell_w = 8.3 * dpi;
        app->help_cell_h = 17.6 * dpi;
    }
}

/* The lines are built from the keymap in force, so a rebound key shows
   its new binding the next time the window is opened or the keys change. */
static void help_rebuild(app_state_t *app) {
    app->help_count = rubraview_help_build(app->arena, &app->keymap, rubraview_boxes_document(),
                                           app->help_lines, sizeof(app->help_lines) / sizeof(app->help_lines[0]));
    /* The second column starts after the longest key list there is, so a
       binding with three keys does not run into what it does. */
    size_t widest = 0;
    for (size_t i = 0; i < app->help_count; ++i) {
        if (app->help_lines[i].keys.len > widest) widest = app->help_lines[i].keys.len;
    }
    app->help_key_cols = (int32_t)widest + 4;
    if (app->help_key_cols < 20) app->help_key_cols = 20;
    if (app->help_key_cols > 48) app->help_key_cols = 48;
    app->help_dirty = true;
}

static void help_close(app_state_t *app) {
    if (!app->help_open) return;
    rubraview_pal_window_get_frame(app->help_window, &app->help_frame[0], &app->help_frame[1],
                                   &app->help_frame[2], &app->help_frame[3]);
    if (app->help_renderer) rubraview_pal_render_destroy(app->help_renderer);
    if (app->help_window) rubraview_pal_window_destroy(app->help_window);
    app->help_renderer = NULL;
    app->help_window = NULL;
    app->help_open = false;
}

static void help_show(app_state_t *app) {
    if (app->help_open) { help_close(app); return; }   /* F1 again puts it away */

    rubraview_window_config_t config = {
        .title = "Rubraview help - keys", .width = 860, .height = 720, .owner = app->window,
    };
    app->help_window = rubraview_pal_window_create(app->arena, &config);
    if (!app->help_window) {
        osd_say(app, U8("the help window could not be opened"));
        return;
    }
    if (app->help_frame[2] > 0 && app->help_frame[3] > 0) {
        rubraview_pal_window_set_frame(app->help_window, app->help_frame[0], app->help_frame[1],
                                       app->help_frame[2], app->help_frame[3]);
    }
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->help_window, &w, &h);
    app->help_renderer = rubraview_pal_render_create(app->arena,
                                                     rubraview_pal_window_native_handle(app->help_window), w, h);
    if (!app->help_renderer) {
        rubraview_pal_window_destroy(app->help_window);
        app->help_window = NULL;
        osd_say(app, U8("the help window could not be drawn"));
        return;
    }
    help_measure(app);
    app->help_open = true;
    app->help_scroll = 0;
    help_rebuild(app);
}

/* How many lines fit, and how far the reader may scroll. */
static int32_t help_rows(const app_state_t *app) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->help_window, &w, &h);
    int32_t rows = (int32_t)((double)h / app->help_cell_h) - 1;
    return rows > 1 ? rows : 1;
}

static void help_scroll_by(app_state_t *app, int32_t lines) {
    int32_t last = (int32_t)app->help_count - help_rows(app);
    if (last < 0) last = 0;
    app->help_scroll += lines;
    if (app->help_scroll > last) app->help_scroll = last;
    if (app->help_scroll < 0) app->help_scroll = 0;
    app->help_dirty = true;
}

static void draw_help_window(app_state_t *app) {
    if (!app->help_open || !app->help_dirty) return;
    app->help_dirty = false;
    rubraview_renderer_t *r = app->help_renderer;
    const double cw = app->help_cell_w, ch = app->help_cell_h, fs = app->help_font;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->help_window, &w, &h);

    rubraview_pal_render_begin(r, HELP_BACKGROUND);

    int32_t rows = help_rows(app);
    double keys_col = cw * 2.0;
    double text_col = cw * (double)app->help_key_cols;
    for (int32_t row = 0; row < rows; ++row) {
        size_t index = (size_t)(app->help_scroll + row);
        if (index >= app->help_count) break;
        const rubraview_help_line_t *line = &app->help_lines[index];
        double y = ch * (double)row;
        switch (line->kind) {
            case RUBRAVIEW_HELP_HEADING:
                rubraview_pal_render_draw_text_mono(r, line->text, keys_col, y, fs, HELP_HEADING);
                rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ keys_col, y + ch * 0.92,
                                                                          (double)w - keys_col * 2.0, 1.0 },
                                               HELP_HEADING, 0.0);
                break;
            case RUBRAVIEW_HELP_ENTRY:
                rubraview_pal_render_draw_text_mono(r, line->keys, keys_col, y, fs, HELP_KEYS);
                rubraview_pal_render_draw_text_mono(r, line->text, text_col, y, fs, HELP_TEXT);
                break;
            case RUBRAVIEW_HELP_NOTE:
                rubraview_pal_render_draw_text_mono(r, line->text, keys_col, y, fs, HELP_TEXT);
                break;
            case RUBRAVIEW_HELP_BLANK:
            default:
                break;
        }
    }

    /* The bottom line says how to move and how to leave. */
    char foot[128];
    int written = snprintf(foot, sizeof(foot), " %zu keys   |   wheel or PageUp/PageDown scrolls   |   F1 or Esc closes",
                           app->help_count);
    if (written > 0) {
        rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ 0.0, (double)h - ch, (double)w, ch },
                                       0xFF1E1E1Eu, 0.0);
        rubraview_pal_render_draw_text_mono(r, (u8str_t){ .ptr = foot, .len = (size_t)written },
                                            0.0, (double)h - ch, fs, HELP_HEADING);
    }
    rubraview_pal_render_end(r);
}

static size_t help_pump(app_state_t *app) {
    size_t handled = 0;
    rubraview_window_event_t event;
    while (app->help_open && rubraview_pal_window_poll_event(app->help_window, &event)) {
        handled++;
        switch (event.kind) {
            case RUBRAVIEW_WINDOW_EVENT_CLOSE:
                help_close(app);
                return handled;
            case RUBRAVIEW_WINDOW_EVENT_RESIZE:
                rubraview_pal_render_resize(app->help_renderer, event.resize.width, event.resize.height);
                help_scroll_by(app, 0);   /* the last line may have moved */
                break;
            case RUBRAVIEW_WINDOW_EVENT_DPI_CHANGED:
                help_measure(app);
                app->help_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_PAINT:
                app->help_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_WHEEL:
                help_scroll_by(app, (int32_t)(-event.mouse.wheel_delta * 3.0));
                break;
            case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN: {
                rubraview_key_combo_t combo = event.key.combo;
                if (key_is(combo, "Escape") || key_is(combo, "F1")) { help_close(app); return handled; }
                else if (key_is(combo, "Down")) help_scroll_by(app, 1);
                else if (key_is(combo, "Up")) help_scroll_by(app, -1);
                else if (key_is(combo, "PageDown") || key_is(combo, "Space")) help_scroll_by(app, help_rows(app) - 1);
                else if (key_is(combo, "PageUp")) help_scroll_by(app, -(help_rows(app) - 1));
                else if (key_is(combo, "Home")) help_scroll_by(app, -(int32_t)app->help_count);
                else if (key_is(combo, "End")) help_scroll_by(app, (int32_t)app->help_count);
                break;
            }
            default:
                break;
        }
    }
    return handled;
}

/* ---- the mini player: what it shows and what its buttons do ---- */

/* Play or pause whichever track is sounding. When it is the background
   music, the listener's own pause is recorded — it outranks the arbiter
   from then on (D-30). */
static void mini_toggle_play(app_state_t *app) {
    if (app->bgm_media) {
        bool pausing = !app->bgm_paused;
        rubraview_pal_media_set_paused(app->bgm_media, pausing);
        app->bgm_paused = pausing;
        (void)rubraview_bgm_event(&app->bgm,
                                  pausing ? RUBRAVIEW_BGM_READER_PAUSED : RUBRAVIEW_BGM_READER_RESUMED,
                                  bgm_pause_for_sound(app));
    } else if (app->media && !app->media_info.has_video) {
        handle_action(app, U8("media_play_pause"));
    }
    app->mini_dirty = true;
}

/* The track before or after the one being heard, in the same folder. */
static int32_t mini_neighbour_page(const app_state_t *app, bool forward) {
    int32_t from = mini_page(app);
    if (from < 0) return -1;
    int32_t step = forward ? 1 : -1;
    for (int32_t i = from + step; i >= 0 && (size_t)i < page_count(app); i += step) {
        u8str_t path = app->source.pages[i].path;
        if (is_media_path(path) && rubraview_tags_is_music_name(rubraview_path_basename(path))) return i;
    }
    return -1;
}

/* Play that track. If the page on screen is the one being heard, the
   viewer simply turns to it; otherwise it becomes the background music
   and the reader stays where they are. */
static void mini_go_to(app_state_t *app, int32_t index) {
    if (index < 0) return;
    if (!app->bgm_media) {
        go_to_spread(app, spread_index_for_page(app, index));
        update_precache(app);
        app->mini_dirty = true;
        return;
    }
    u8str_t path = app->source.pages[index].path;
    rubraview_media_backend_t order[2];
    size_t count = rubraview_media_backend_order(app->media_preferred,
                                                 rubraview_pal_media_backend_available(RUBRAVIEW_BACKEND_FFMPEG),
                                                 order);
    for (size_t i = 0; i < count; ++i) {
        rubraview_media_open_result_t opened = rubraview_pal_media_open(path, order[i], NULL);
        if (!opened.media) continue;
        bool was_paused = app->bgm_paused;
        bgm_close(app);
        app->bgm_media = opened.media;
        app->bgm_info = opened.info;
        app->bgm_page = index;
        app->bgm_position = 0.0;
        app->bgm_paused = was_paused;
        app->bgm_tags = (rubraview_tags_t){0};
        {
            u8str_t head = rubraview_pal_fs_read_file(app->arena, path, MUSIC_MAX_HEAD_BYTES);
            if (head.len > 0) {
                app->bgm_tags = rubraview_tags_read(app->arena, (rubraview_tags_source_t){
                    .head = (const uint8_t*)head.ptr, .head_size = head.len, .file_size = head.len });
            }
        }
        rubraview_pal_media_set_paused(app->bgm_media, was_paused);
        bgm_do(app, rubraview_bgm_event(&app->bgm, RUBRAVIEW_BGM_MUSIC_OPENED, bgm_pause_for_sound(app)));
        mini_cover_load(app);
        app->mini_dirty = true;
        return;
    }
    osd_say(app, U8("that track could not be opened"));
}

static void mini_seek_to(app_state_t *app, double fraction) {
    rubraview_media_t *m = mini_target(app);
    double duration = mini_duration(app);
    if (!m || duration <= 0.0) return;
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    double where = fraction * duration;
    if (app->bgm_media) {
        rubraview_pal_media_seek(m, where);
        app->bgm_position = where;
    } else {
        media_seek_to(app, where);
    }
    app->mini_dirty = true;
}

/* A click: a button, or the strip. */
static void mini_press(app_state_t *app, double x, double y) {
    double dpi = rubraview_pal_window_dpi_scale(app->mini_window);
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->mini_window, &w, &h);
    for (int32_t i = 0; i < 3; ++i) {
        rubraview_pal_rect_t b = mini_button_rect(dpi, w, h, i);
        if (x >= b.x && x < b.x + b.width && y >= b.y && y < b.y + b.height) {
            if (i == 0) mini_go_to(app, mini_neighbour_page(app, false));
            else if (i == 1) mini_toggle_play(app);
            else mini_go_to(app, mini_neighbour_page(app, true));
            return;
        }
    }
    rubraview_pal_rect_t strip = mini_strip_rect(dpi, w, h);
    if (y >= strip.y - 8.0 * dpi && y <= strip.y + strip.height + 8.0 * dpi &&
        x >= strip.x && x <= strip.x + strip.width && strip.width > 0.0) {
        mini_seek_to(app, (x - strip.x) / strip.width);
    }
}

static void draw_mini_window(app_state_t *app) {
    if (!app->mini_open || !app->mini_dirty) return;
    app->mini_dirty = false;
    rubraview_renderer_t *r = app->mini_renderer;
    double dpi = rubraview_pal_window_dpi_scale(app->mini_window);
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->mini_window, &w, &h);
    rubraview_pal_render_begin(r, MINI_BACKGROUND);

    /* Nothing is playing: say so, rather than leave the last track's
       place on the strip looking like something that is still there. */
    if (!mini_target(app)) {
        rubraview_pal_rect_t all = { 0.0, 0.0, (double)w, (double)h };
        rubraview_pal_render_draw_text(r, U8("nothing is playing"), all, 13.0 * dpi,
                                       0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
        if (!rubraview_pal_render_end(r)) mini_close(app);
        return;
    }

    /* The cover, or a plain square where one would be. */
    rubraview_pal_rect_t cover = mini_cover_rect(dpi, h);
    if (app->mini_cover && app->mini_cover_w > 0 && app->mini_cover_h > 0) {
        rubraview_mat3x2_t place = rubraview_mat3x2_multiply(
            rubraview_mat3x2_scale(cover.width / (double)app->mini_cover_w,
                                   cover.height / (double)app->mini_cover_h),
            rubraview_mat3x2_translate(cover.x, cover.y));
        rubraview_pal_render_draw_texture(r, app->mini_cover, place, RUBRAVIEW_INTERP_LINEAR);
    } else {
        rubraview_pal_render_fill_rect(r, cover, COLOR_TILE_FILL, 3.0);
    }

    /* The words: the track, then who made it. */
    const rubraview_tags_t *tags = mini_tags(app);
    double left = cover.x + cover.width + 6.0 * dpi;
    double text_w = (double)w - left - 6.0 * dpi - 3.0 * 28.0 * dpi;
    if (text_w < 40.0 * dpi) text_w = 40.0 * dpi;
    u8str_t title = tags->title;
    if (title.len == 0) {
        int32_t page = mini_page(app);
        if (page >= 0 && (size_t)page < page_count(app)) {
            title = rubraview_path_basename(app->source.pages[page].path);
        }
    }
    rubraview_pal_rect_t line1 = { left, 6.0 * dpi, text_w, 18.0 * dpi };
    rubraview_pal_render_draw_text(r, title, line1, 13.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    if (tags->artist.len > 0) {
        rubraview_pal_rect_t line2 = { left, 25.0 * dpi, text_w, 16.0 * dpi };
        rubraview_pal_render_draw_text(r, tags->artist, line2, 11.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
    }

    /* The three buttons: back a track, play or pause, on a track. */
    static const char *const GLYPH[3] = { "|<", "||", ">|" };
    for (int32_t i = 0; i < 3; ++i) {
        rubraview_pal_rect_t b = mini_button_rect(dpi, w, h, i);
        rubraview_pal_render_fill_rect(r, b, app->mini_hover == i ? COLOR_TILE_CURRENT : COLOR_TILE_FILL, 3.0);
        const char *glyph = i == 1 ? (mini_paused(app) ? " >" : "||") : GLYPH[i];
        rubraview_pal_render_draw_text(r, cstr(glyph), b, 13.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }

    /* Where the track has got to, and how far it goes. */
    rubraview_pal_rect_t strip = mini_strip_rect(dpi, w, h);
    rubraview_pal_render_fill_rect(r, strip, 0x40FFFFFFu, 2.0);
    double duration = mini_duration(app);
    if (duration > 0.0) {
        double fraction = mini_position(app) / duration;
        if (fraction < 0.0) fraction = 0.0;
        if (fraction > 1.0) fraction = 1.0;
        rubraview_pal_rect_t done = { strip.x, strip.y, strip.width * fraction, strip.height };
        rubraview_pal_render_fill_rect(r, done, COLOR_TILE_CURRENT, 2.0);
    }
    char clock[64];
    u8str_t at = rubraview_format_timecode(clock, sizeof(clock), mini_position(app), false);
    rubraview_pal_rect_t time_rect = { strip.x + strip.width + 4.0 * dpi, h - 20.0 * dpi,
                                       (double)w - (strip.x + strip.width) - 8.0 * dpi, 16.0 * dpi };
    rubraview_pal_render_draw_text(r, at, time_rect, 11.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);

    if (!rubraview_pal_render_end(r)) mini_close(app);
}

static size_t mini_pump(app_state_t *app) {
    size_t handled = 0;
    rubraview_window_event_t event;
    while (app->mini_open && rubraview_pal_window_poll_event(app->mini_window, &event)) {
        handled++;
        switch (event.kind) {
            case RUBRAVIEW_WINDOW_EVENT_CLOSE:
                mini_close(app);
                return handled;
            case RUBRAVIEW_WINDOW_EVENT_RESIZE:
                rubraview_pal_render_resize(app->mini_renderer, event.resize.width, event.resize.height);
                app->mini_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_PAINT:
            case RUBRAVIEW_WINDOW_EVENT_DPI_CHANGED:
                app->mini_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE: {
                double dpi = rubraview_pal_window_dpi_scale(app->mini_window);
                int32_t w = 0, h = 0;
                rubraview_pal_window_get_size(app->mini_window, &w, &h);
                int32_t was = app->mini_hover;
                app->mini_hover = -1;
                for (int32_t i = 0; i < 3; ++i) {
                    rubraview_pal_rect_t b = mini_button_rect(dpi, w, h, i);
                    if (event.mouse.x >= b.x && event.mouse.x < b.x + b.width &&
                        event.mouse.y >= b.y && event.mouse.y < b.y + b.height) {
                        app->mini_hover = i;
                        break;
                    }
                }
                if (app->mini_hover != was) app->mini_dirty = true;
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN:
                mini_press(app, event.mouse.x, event.mouse.y);
                break;
            case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN: {
                rubraview_key_combo_t combo = event.key.combo;
                if (key_is(combo, "Escape")) { mini_close(app); return handled; }
                if (key_is(combo, "Space")) mini_toggle_play(app);
                break;
            }
            default:
                break;
        }
    }
    return handled;
}

static void draw_settings_window(app_state_t *app) {
    if (!app->settings_open || !app->settings_dirty) return;
    app->settings_dirty = false;
    rubraview_renderer_t *r = app->settings_renderer;
    const rubraview_settings_view_t *v = &app->settings_view;
    const double cw = app->settings_cell_w, ch = app->settings_cell_h, fs = app->settings_font;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->settings_window, &w, &h);
    rubraview_settings_sources_t sources = { .user = app, .info = settings_info, .table_row = settings_table_row };
    char line[1024];

    rubraview_pal_render_begin(r, SETTINGS_BACKGROUND);

    /* The page list. */
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ 0, 0, cw * v->list_cols, (double)h }, SETTINGS_PANE, 0.0);
    rubraview_pal_render_draw_text_mono(r, U8(" Settings"), 0.0, 0.0, fs, SETTINGS_ACCENT);
    for (int32_t p = 0; p < (int32_t)v->doc->page_count; ++p) {
        double y = ch * (2 + p);
        if (p == v->page) {
            rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ 0, y, cw * v->list_cols, ch }, SETTINGS_FOCUS, 0.0);
        }
        settings_text(app, rubraview_settings_page_text(v, p, line, sizeof(line)), 0.0, y, SETTINGS_TEXT);
    }

    /* The page. */
    double x0 = cw * v->content_col;
    settings_text(app, rubraview_settings_page_title((size_t)v->page), x0, 0.0, SETTINGS_ACCENT);
    for (size_t i = 0; i < v->line_count; ++i) {
        const rubraview_settings_line_t *ln = &v->lines[i];
        const rubraview_settings_node_t *node = &v->doc->nodes[ln->node];
        for (int32_t k = 0; k < ln->height; ++k) {
            int32_t content_row = ln->row + k - v->scroll;
            if (content_row < 0 || content_row >= rubraview_settings_view_visible_rows(v)) continue;
            double y = ch * (2 + content_row);
            if (ln->kind == RUBRAVIEW_LINE_PREVIEW) {
                if (k == 0) {
                    rubraview_pal_rect_t box = { x0, y, cw * (v->cols - v->content_col - 1), ch * ln->height };
                    settings_preview(app, node->name, box);
                }
                continue;
            }
            if ((int32_t)i == v->focus_line && (ln->kind != RUBRAVIEW_LINE_TABLE || k == v->focus_row)) {
                rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ x0 - cw * 0.5, y, cw * (v->cols - v->content_col), ch },
                                               SETTINGS_FOCUS, 0.0);
            }
            u8str_t text = rubraview_settings_line_text(v, &app->settings, &sources, i, k, line, sizeof(line));
            uint32_t color = SETTINGS_TEXT;
            if (ln->kind == RUBRAVIEW_LINE_SECTION) color = SETTINGS_ACCENT;
            /* Not read by the viewer yet: dimmed, except under the focus bar, where dim text is unreadable. */
            if (ln->kind == RUBRAVIEW_LINE_SETTING && !v->doc->defs[node->setting].wired &&
                (int32_t)i != v->focus_line) color = SETTINGS_DIM;
            if (ln->kind == RUBRAVIEW_LINE_TABLE && k == 0) color = SETTINGS_ACCENT;
            settings_text(app, text, x0, y, color);
        }
    }

    /* Under the page: the last action's result, then the buttons. */
    if (app->settings_message[0]) {
        rubraview_pal_render_draw_text_mono(r, cstr(app->settings_message), x0, ch * (v->rows - 2), fs, SETTINGS_DIM);
    }
    for (int32_t b = 0; b < RUBRAVIEW_BUTTON_COUNT; ++b) {
        int32_t col = 0, width = 0;
        rubraview_settings_view_button_cells(v, (rubraview_settings_button_t)b, &col, &width);
        double y = ch * (v->rows - 1);
        if (b == v->focus_button) {
            rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ cw * col, y, cw * width, ch }, SETTINGS_FOCUS, 0.0);
        }
        rubraview_pal_render_draw_text_mono(r, rubraview_settings_button_text((rubraview_settings_button_t)b),
                                            cw * col, y, fs, SETTINGS_TEXT);
    }

    if (!rubraview_pal_render_end(r)) app->settings_dirty = true;
}

/* Where the page on screen sits, in client pixels, and how many client
   pixels one of its own pixels takes. The crop overlay needs both to
   turn a drag into image coordinates; it asks the compositor the same
   question draw_spread asks, so the rectangle it draws is the one the
   picture is in. Returns false when nothing is drawn. */
static bool page_screen_rect(app_state_t *app, rubraview_pal_rect_t *out_rect, double *out_scale) {
    int32_t page_index = current_page_index(app);
    if (page_index < 0 || app->spread_index >= app->layout.count) return false;

    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) return false;

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
        spread, (left && left->loaded) ? &left_size : NULL, (right && right->loaded) ? &right_size : NULL,
        (double)win_w, (double)win_h, app->fit_mode, GUTTER, app->zoom, app->pan_x, app->pan_y);

    for (size_t i = 0; i < comp.count; ++i) {
        const rubraview_draw_command_t *cmd = &comp.commands[i];
        if (cmd->page_index != page_index) continue;
        double x0 = cmd->transform.e, y0 = cmd->transform.f;
        double w = (cmd->src_right - cmd->src_left) * cmd->transform.a;
        double h = (cmd->src_bottom - cmd->src_top) * cmd->transform.d;
        if (w <= 0.0 || h <= 0.0) return false;
        *out_rect = (rubraview_pal_rect_t){ .x = x0, .y = y0, .width = w, .height = h };
        *out_scale = cmd->transform.a;
        return true;
    }
    return false;
}

/* §3.13: the crop rectangle, dragged on the picture itself. The model
   (normalising, the ratio lock, staying inside the image) is
   rubraview_edit_crop_drag's; this turns client pixels into image
   pixels and back, and draws what the model then holds. */
static bool crop_point_to_image(app_state_t *app, double px, double py, int32_t *out_x, int32_t *out_y) {
    rubraview_pal_rect_t rect;
    double scale = 1.0;
    if (!page_screen_rect(app, &rect, &scale) || scale <= 0.0) return false;
    double ix = (px - rect.x) / scale;
    double iy = (py - rect.y) / scale;
    if (ix < 0.0) ix = 0.0;
    if (iy < 0.0) iy = 0.0;
    if (ix > (double)app->edit.image_width) ix = (double)app->edit.image_width;
    if (iy > (double)app->edit.image_height) iy = (double)app->edit.image_height;
    *out_x = (int32_t)(ix + 0.5);
    *out_y = (int32_t)(iy + 0.5);
    return true;
}

static bool edit_panel_open(const app_state_t *app) {
    return app->panel.open && !app->panel_is_export && !app->panel_is_batch;
}

static void draw_crop_overlay(app_state_t *app) {
    if (!edit_panel_open(app) || !app->edit.crop_active) return;
    rubraview_pal_rect_t rect;
    double scale = 1.0;
    if (!page_screen_rect(app, &rect, &scale)) return;

    rubraview_crop_rect_t c = app->edit.crop;
    double x = rect.x + (double)c.x * scale, y = rect.y + (double)c.y * scale;
    double w = (double)c.width * scale, h = (double)c.height * scale;
    if (w <= 0.0 || h <= 0.0) return;

    /* What is being cut away is dimmed, which is the part a reader
       actually looks at when judging a crop. */
    const uint32_t shade = 0x80000000u;
    rubraview_pal_render_fill_rect(app->renderer,
        (rubraview_pal_rect_t){ rect.x, rect.y, rect.width, y - rect.y }, shade, 0.0);
    rubraview_pal_render_fill_rect(app->renderer,
        (rubraview_pal_rect_t){ rect.x, y + h, rect.width, rect.y + rect.height - (y + h) }, shade, 0.0);
    rubraview_pal_render_fill_rect(app->renderer,
        (rubraview_pal_rect_t){ rect.x, y, x - rect.x, h }, shade, 0.0);
    rubraview_pal_render_fill_rect(app->renderer,
        (rubraview_pal_rect_t){ x + w, y, rect.x + rect.width - (x + w), h }, shade, 0.0);

    rubraview_pal_render_stroke_rect(app->renderer, (rubraview_pal_rect_t){ x, y, w, h }, 0xFFFFFFFFu, 1.0, 0.0);

    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double grip = 10.0 * dpi;
    const double corners[4][2] = { { x, y }, { x + w - grip, y }, { x, y + h - grip }, { x + w - grip, y + h - grip } };
    for (size_t i = 0; i < 4; ++i) {
        rubraview_pal_render_fill_rect(app->renderer,
            (rubraview_pal_rect_t){ corners[i][0], corners[i][1], grip, grip }, 0xFFFFFFFFu, 0.0);
    }

    char line[96];
    size_t pos = 0;
    line[0] = '\0';
    append_number(line, sizeof(line), &pos, (size_t)(c.width < 0 ? 0 : c.width));
    append_text(line, sizeof(line), &pos, " x ");
    append_number(line, sizeof(line), &pos, (size_t)(c.height < 0 ? 0 : c.height));
    rubraview_pal_rect_t label = { x, y - 22.0 * dpi, 160.0 * dpi, 20.0 * dpi };
    if (label.y < rect.y) label.y = y + 2.0 * dpi;
    rubraview_pal_render_fill_rect(app->renderer, label, 0xC0000000u, 0.0);
    rubraview_pal_render_draw_text(app->renderer, cstr(line), label, 13.0 * dpi, 0xFFFFFFFFu, RUBRAVIEW_TEXT_LEFT);
}

/* §3.13's curve widget. The model is already there and tested — points
   sorted, endpoints kept, neighbours not crossed (rubraview_edit_curve_*);
   what was missing was a box to see it in. It sits under the workbench
   panel, 0,0 at the bottom left like every curve editor, and draws the
   line by sampling the same LUT the commit uses, so what is on screen is
   what will be applied. */
static rubraview_pal_rect_t curve_widget_rect(const app_state_t *app) {
    if (!app->panel.open || app->panel_is_export || app->panel_is_batch) {
        return (rubraview_pal_rect_t){0};
    }
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double side = 168.0 * dpi;
    double gap = 8.0 * dpi;
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);

    /* Under the panel when there is room for the whole box; beside it
       otherwise, which is what a short window gives (measured on the VM:
       under a full-height workbench the box ran off the bottom edge and
       lay over the panel's last rows). */
    double x = app->panel.bounds.x;
    double y = app->panel.bounds.y + app->panel.bounds.height + gap;
    if (y + side + gap > (double)win_h) {
        x = app->panel.bounds.x - side - gap;
        y = app->panel.bounds.y;
    }
    if (x < gap) x = gap;
    if (y + side > (double)win_h) y = (double)win_h - side - gap;
    if (y < 0.0) y = 0.0;
    if (x + side > (double)win_w) x = (double)win_w - side - gap;
    if (x < 0.0) x = 0.0;
    return (rubraview_pal_rect_t){ x, y, side, side };
}

static void draw_curve_widget(app_state_t *app) {
    rubraview_pal_rect_t box = curve_widget_rect(app);
    if (box.width <= 0.0) return;

    double dpi = rubraview_pal_window_dpi_scale(app->window);
    rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BOX_FILL, 3.0);
    rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_BOX_BORDER, 1.0, 3.0);

    /* quarters, so a point's height can be judged without a ruler */
    for (int i = 1; i < 4; ++i) {
        double t = (double)i / 4.0;
        rubraview_pal_render_fill_rect(app->renderer,
            (rubraview_pal_rect_t){ box.x + box.width * t, box.y, 1.0, box.height }, 0x30FFFFFFu, 0.0);
        rubraview_pal_render_fill_rect(app->renderer,
            (rubraview_pal_rect_t){ box.x, box.y + box.height * t, box.width, 1.0 }, 0x30FFFFFFu, 0.0);
    }

    const rubraview_edit_curve_t *curve = &app->edit.curves[app->edit.active_channel];
    uint8_t lut[256];
    rubraview_curve_build_lut(lut, curve->points, curve->point_count);

    double dot = 2.0 * dpi;
    for (int i = 0; i < 256; ++i) {
        double px = box.x + box.width * ((double)i / 255.0);
        double py = box.y + box.height * (1.0 - (double)lut[i] / 255.0);
        rubraview_pal_render_fill_rect(app->renderer,
            (rubraview_pal_rect_t){ px - dot * 0.5, py - dot * 0.5, dot, dot }, 0xFFFFFFFFu, 0.0);
    }

    double grip = 7.0 * dpi;
    for (size_t i = 0; i < curve->point_count; ++i) {
        double px = box.x + box.width * ((double)curve->points[i].x / 255.0);
        double py = box.y + box.height * (1.0 - (double)curve->points[i].y / 255.0);
        rubraview_pal_render_fill_rect(app->renderer,
            (rubraview_pal_rect_t){ px - grip * 0.5, py - grip * 0.5, grip, grip }, 0xFFFFD000u, 0.0);
    }

    static const char *const CHANNEL_NAMES[5] = { "RGB", "Red", "Green", "Blue", "Luma" };
    rubraview_pal_rect_t label = { box.x + 6.0 * dpi, box.y + 4.0 * dpi, box.width - 12.0 * dpi, 18.0 * dpi };
    rubraview_pal_render_draw_text(app->renderer, cstr(CHANNEL_NAMES[app->edit.active_channel]),
                                   label, 12.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
}

/* A press inside the widget: the nearest point, or a new one. The right
   button takes an interior point off, which is how every curve editor
   does it and what rubraview_edit_curve_remove is for. */
/* Inside the curve box? The crop drag asks before it claims a press. */
static bool point_in_curve_widget(const app_state_t *app, double px, double py) {
    rubraview_pal_rect_t box = curve_widget_rect(app);
    return box.width > 0.0 && px >= box.x && px <= box.x + box.width &&
           py >= box.y && py <= box.y + box.height;
}

static bool curve_widget_press(app_state_t *app, double px, double py, bool remove) {
    rubraview_pal_rect_t box = curve_widget_rect(app);
    if (box.width <= 0.0) return false;
    if (px < box.x || px > box.x + box.width || py < box.y || py > box.y + box.height) return false;

    float x = (float)((px - box.x) / box.width * 255.0);
    float y = (float)((1.0 - (py - box.y) / box.height) * 255.0);
    int32_t index = rubraview_edit_curve_grab(&app->edit, x, y, 10.0f);
    if (index < 0) return true;   /* inside the box, but the curve is full */
    if (remove) {
        rubraview_edit_curve_remove(&app->edit, index);
        app->curve_dragging = -1;
        return true;
    }
    rubraview_edit_curve_move(&app->edit, index, x, y);
    app->curve_dragging = index;
    return true;
}

static void curve_widget_drag(app_state_t *app, double px, double py) {
    rubraview_pal_rect_t box = curve_widget_rect(app);
    if (box.width <= 0.0 || app->curve_dragging < 0) return;
    float x = (float)((px - box.x) / box.width * 255.0);
    float y = (float)((1.0 - (py - box.y) / box.height) * 255.0);
    rubraview_edit_curve_move(&app->edit, app->curve_dragging, x, y);
}

static void render_frame(app_state_t *app) {
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) return;

    rubraview_pal_render_begin(app->renderer, COLOR_CANVAS);

    draw_music_backdrop(app, win_w, win_h);

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

    draw_music_words(app, win_w, win_h);

    if (app->picker_open) {
        draw_picker(app, (double)win_w, (double)win_h);
    } else {
        draw_crop_overlay(app);
        draw_chrome(app, (double)win_w, (double)win_h);
        draw_panel(app);
        draw_curve_widget(app);
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
    /* However it was opened, the menu opens at its root, Recent as it is now. */
    if (rubraview_box_just_opened(&app->menubox, &app->menubox_was_open)) menu_rebuild(app);

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
    /* §3.7.5 / D-14: keymap.ini lives where settings.ini does — beside the
       program in portable mode, in AppData otherwise — and replaces the
       built-in bindings wholesale. Before the settings window could write
       one it was read only from the working folder, so a file left there
       still counts when AppData has none. */
    u8str_t text = rubraview_pal_fs_read_file(app->arena, app->keymap_path, KEYMAP_MAX_BYTES);
    if (text.len == 0 && app->config_mode != RUBRAVIEW_CONFIG_PORTABLE) {
        text = rubraview_pal_fs_read_file(app->arena, U8("keymap.ini"), KEYMAP_MAX_BYTES);
    }
    if (text.len == 0) text = cstr(rubraview_default_keymap());
    app->keymap = rubraview_keymap_parse(app->arena, text);
}

static void build_slides(app_state_t *app) {
    if (app->layout.count == 0) return;
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, app->layout.count, sizeof(rubraview_slideshow_item_t));
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
    /* The settings are read first: they decide portable mode, and the
       reader's settings apply from the first frame rather than from the
       first time the settings window is opened. */
    settings_read_file(app);

    u8str_t appdata = U8(".");
#ifdef _WIN32
    char appdata_utf8[1024];
    DWORD written = GetEnvironmentVariableA("APPDATA", appdata_utf8, (DWORD)sizeof(appdata_utf8));
    if (written > 0 && written < sizeof(appdata_utf8)) {
        appdata = (u8str_t){ .ptr = appdata_utf8, .len = written };
    }
#endif
    app->history_path = rubraview_config_path(app->arena, app->config_mode,
                                              app->config_beside, appdata, U8("history.ini"));
    app->layout_path = rubraview_config_path(app->arena, app->config_mode,
                                             app->config_beside, appdata, U8("layout.ini"));
    u8str_t text = rubraview_pal_fs_read_file(app->arena, app->history_path, 1024u * 1024u);
    app->history = rubraview_history_parse(app->arena, text);
}

/* §3.6: "Positions persisted across sessions". The two floating boxes
   are not settings the reader edits in a dialog — they are where the
   hands left them — so they live in their own small file beside the
   reading history, under the same portable-or-AppData rule. */
static double ini_number(const rubraview_ini_doc_t *doc, u8str_t section, u8str_t key, double fallback) {
    const u8str_t *text = rubraview_ini_get(doc, section, key);
    double value = 0.0;
    return (text && rubraview_parse_double_prefix(*text, &value, NULL)) ? value : fallback;
}

/* A box saved on a larger screen, or left near the edge of a window that
   then shrank, must not be out of reach: each anchor stays inside the
   window with room to grab it. A toolbox that is its own window is not
   in this window at all. */
static void boxes_keep_in_reach(app_state_t *app) {
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    /* the room an anchor takes, as toolbox_dock leaves it */
    double max_x = (double)win_w - 88.0 * dpi, max_y = (double)win_h - 48.0 * dpi;
    rubraview_box_t *boxes[] = { &app->toolbox, &app->menubox };
    for (size_t i = 0; i < 2; ++i) {
        rubraview_box_t *box = boxes[i];
        if (box->state == RUBRAVIEW_BOX_DETACHED) continue;
        if (box->anchor_x < 0.0) box->anchor_x = 0.0;
        if (box->anchor_y < 0.0) box->anchor_y = 0.0;
        if (max_x > 0.0 && box->anchor_x > max_x) box->anchor_x = max_x;
        if (max_y > 0.0 && box->anchor_y > max_y) box->anchor_y = max_y;
    }
}

static void layout_load(app_state_t *app) {
    if (app->layout_path.len == 0) return;
    u8str_t text = rubraview_pal_fs_read_file(app->arena, app->layout_path, 8u * 1024u);
    if (text.len == 0) return;
    rubraview_ini_doc_t doc = rubraview_ini_parse(app->arena, text);

    struct { rubraview_box_t *box; const char *x_key, *y_key; } BOXES[] = {
        { &app->toolbox, "toolbox_x", "toolbox_y" },
        { &app->menubox, "menubox_x", "menubox_y" },
    };
    for (size_t i = 0; i < sizeof(BOXES) / sizeof(BOXES[0]); ++i) {
        BOXES[i].box->anchor_x = ini_number(&doc, U8("boxes"), cstr(BOXES[i].x_key), BOXES[i].box->anchor_x);
        BOXES[i].box->anchor_y = ini_number(&doc, U8("boxes"), cstr(BOXES[i].y_key), BOXES[i].box->anchor_y);
    }
    boxes_keep_in_reach(app);
    /* Where the boxes were left, not a setting: layout.ini, read like the positions. */
    if (ini_number(&doc, U8("boxes"), U8("toolbox_pinned"), 0.0) > 0.5) rubraview_box_set_pinned(&app->toolbox, true);

    /* The settings window's frame; the PAL pulls it onto a screen when it is placed. */
    static const char *const FRAME_KEYS[4] = { "x", "y", "width", "height" };
    int32_t frame[4];
    for (size_t i = 0; i < 4; ++i) {
        double v = ini_number(&doc, U8("settings_window"), cstr(FRAME_KEYS[i]), 0.0);
        frame[i] = (v > -100000.0 && v < 100000.0) ? (int32_t)v : 0;
    }
    if (frame[2] >= 200 && frame[3] >= 150) memcpy(app->settings_frame, frame, sizeof(frame));
}

static void layout_save(app_state_t *app) {
    if (app->layout_path.len == 0) return;
    /* Through the configuration writer, so the file is in the INI and
       TOML subset (D-13). */
    rubraview_ini_doc_t doc = {0};
    rubraview_ini_set_float(app->arena, &doc, U8("boxes"), U8("toolbox_x"), app->toolbox.anchor_x);
    rubraview_ini_set_float(app->arena, &doc, U8("boxes"), U8("toolbox_y"), app->toolbox.anchor_y);
    rubraview_ini_set_float(app->arena, &doc, U8("boxes"), U8("menubox_x"), app->menubox.anchor_x);
    rubraview_ini_set_float(app->arena, &doc, U8("boxes"), U8("menubox_y"), app->menubox.anchor_y);
    rubraview_ini_set_int(app->arena, &doc, U8("boxes"), U8("toolbox_pinned"), app->toolbox.pinned ? 1 : 0);
    if (app->settings_frame[2] > 0 && app->settings_frame[3] > 0) {
        rubraview_ini_set_int(app->arena, &doc, U8("settings_window"), U8("x"), app->settings_frame[0]);
        rubraview_ini_set_int(app->arena, &doc, U8("settings_window"), U8("y"), app->settings_frame[1]);
        rubraview_ini_set_int(app->arena, &doc, U8("settings_window"), U8("width"), app->settings_frame[2]);
        rubraview_ini_set_int(app->arena, &doc, U8("settings_window"), U8("height"), app->settings_frame[3]);
    }
    u8str_t text = rubraview_ini_serialize(app->arena, &doc);
    if (text.len > 0) rubraview_pal_fs_write_file(app->layout_path, text);
}

static void history_remember(app_state_t *app) {
    if (app->source_dir.len == 0 || page_count(app) == 0) return;

    int32_t current = current_page_index(app);
    if (current < 0) return;

    /* An archive is remembered by its own path; a folder by the folder. */
    u8str_t key = app->source.archive_path.len > 0 ? app->source.archive_path : app->source_dir;
    rubraview_history_record(app->arena, &app->history, key, current, (int32_t)page_count(app),
                             (int64_t)time(NULL));   /* a date: pal_time is a monotonic clock, which restarts with the machine */
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
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, page_count(app), sizeof(app_page_t));
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
            rubraview_arena_alloc_array(app->arena, page_count(app), sizeof(rubraview_page_info_t));
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
    /* The path is spelt as the caller gave it — backslashes from the
       shell, '/' from a folder listing — so every lookup by it compares
       with rubraview_path_same, never byte for byte. */
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
/* Where a batch run's report goes. Three cases, and they are not the
   same handle:

   - started from a command prompt: the parent's console, and a redirect
     (`> log.txt`) must still reach the file, so the standard handle wins
     when there is one;
   - started by the dialog with a console of its own (CREATE_NEW_CONSOLE):
     this is a GUI-subsystem program, so its standard handles are empty
     and the console has to be opened by name — CONOUT$;
   - no console anywhere: nothing is written, and nothing waits.

   This was found on the VM: the run in its own window printed nothing and
   the window closed the moment it finished. */
static HANDLE console_handle(bool input) {
    HANDLE std = GetStdHandle(input ? STD_INPUT_HANDLE : STD_OUTPUT_HANDLE);
    if (std && std != INVALID_HANDLE_VALUE) return std;

    static bool attached = false;
    if (!attached) {
        attached = true;
        if (!AttachConsole(ATTACH_PARENT_PROCESS) && GetConsoleWindow() == NULL) AllocConsole();
    }
    if (GetConsoleWindow() == NULL) return NULL;
    HANDLE named = CreateFileW(input ? L"CONIN$" : L"CONOUT$",
                               GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);
    return named == INVALID_HANDLE_VALUE ? NULL : named;
}

static void console_line(const char *text) {
    HANDLE out = console_handle(false);
    if (!out) return;

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
    /* The name carries the source's extension. When the job converts, the
       file must be named after what is actually written, or a PNG lands
       in a .jpg and nothing can open it. */
    if (bc->cli->export_options.format != RUBRAVIEW_EXPORT_SAME_AS_SOURCE) {
        u8str_t want = rubraview_export_extension(bc->cli->export_options.format);
        u8str_t stem = rubraview_path_stem(output_name);
        if (want.len > 0 && stem.len > 0) {
            size_t n = stem.len + 1 + want.len;
            proven_result_mem_mut_t res = proven_arena_alloc(arena, n + 1);
            if (proven_is_ok(res.err)) {
                char *p = (char*)res.value.ptr;
                memcpy(p, stem.ptr, stem.len);
                p[stem.len] = '.';
                memcpy(p + stem.len + 1, want.ptr, want.len);
                p[n] = '\0';
                output_name = (u8str_t){ .ptr = p, .len = n };
            }
        }
    }
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

/* RV-068: the batch dialog's Run. The dialog does not convert anything
   itself: it writes the command line `--batch` already understands and
   starts a second copy of the program with it, in a console window of
   its own (owner, 2026-09-20). A run then keeps going when the viewer is
   closed, a viewer that hangs does not take it down, and the window it
   reports into can be read afterwards — `--pause` holds it open.

   Two things are decided here rather than asked: the run is over the
   folder being read, and it writes into a `rubraview-out` folder beside
   those pictures, so pressing Run can never overwrite an original. */
static double panel_value_of(const rubraview_panel_t *panel, int32_t id, double fallback) {
    for (size_t i = 0; i < panel->row_count; ++i) {
        if (panel->rows[i].id == id) return panel->rows[i].value;
    }
    return fallback;
}

/* Appends `text`, in quotes when it holds a space, so a folder with one
   in its name survives the command line. */
static void append_arg(char *buf, size_t cap, size_t *pos, u8str_t text) {
    bool quote = false;
    for (size_t i = 0; i < text.len; ++i) if (text.ptr[i] == ' ') { quote = true; break; }
    /* No separator before the first token: a command line that starts
       with a space has no program name in it, and CreateProcess refuses
       it — which is exactly how this was found. */
    if (*pos > 0 && *pos + 1 < cap) buf[(*pos)++] = ' ';
    if (quote && *pos + 1 < cap) buf[(*pos)++] = '"';
    for (size_t i = 0; i < text.len && *pos + 1 < cap; ++i) buf[(*pos)++] = text.ptr[i];
    if (quote && *pos + 1 < cap) buf[(*pos)++] = '"';
    buf[*pos] = '\0';
}

static void append_arg_cstr(char *buf, size_t cap, size_t *pos, const char *text) {
    append_arg(buf, cap, pos, cstr(text));
}

static void panel_run_batch(app_state_t *app) {
    if (app->source_dir.len == 0 || app->archive_bytes.len > 0) {
        osd_say(app, U8("batch runs on a folder of pictures, not on an archive"));
        return;
    }

    u8str_t exe = rubraview_pal_process_executable(app->arena);
    if (exe.len == 0) {
        osd_say(app, U8("could not find this program to start a batch run"));
        return;
    }

    double percent = panel_value_of(&app->panel, PANEL_BATCH_RESIZE, 100.0);
    int32_t filter = (int32_t)panel_value_of(&app->panel, PANEL_BATCH_FILTER, 3.0);
    int32_t format = (int32_t)panel_value_of(&app->panel, PANEL_BATCH_FORMAT, 0.0);
    bool grayscale = panel_value_of(&app->panel, PANEL_BATCH_GRAY, 0.0) > 0.5;
    bool privacy = panel_value_of(&app->panel, PANEL_PRIVACY, 0.0) > 0.5;
    bool resizing = percent < 99.99 || percent > 100.01;

    if (!resizing && !grayscale && !privacy && format <= 0) {
        osd_say(app, U8("nothing to do: choose a size, a format, grayscale or privacy clean"));
        return;
    }

    u8str_t out_dir = rubraview_path_join(app->arena, app->source_dir, U8("rubraview-out"));
    if (out_dir.len == 0 || !rubraview_pal_fs_make_dirs(out_dir)) {
        osd_say(app, U8("could not make the rubraview-out folder"));
        return;
    }

    static const char *const FILTER_NAMES[4] = { "nearest", "bilinear", "bicubic", "lanczos3" };
    char line[2048];
    size_t pos = 0;
    line[0] = '\0';
    append_arg(line, sizeof(line), &pos, exe);
    append_arg_cstr(line, sizeof(line), &pos, "--batch");
    append_arg_cstr(line, sizeof(line), &pos, "--pause");
    if (resizing) {
        char resize_arg[32];
        size_t rp = 0;
        resize_arg[0] = '\0';
        append_text(resize_arg, sizeof(resize_arg), &rp, "--resize=");
        append_number(resize_arg, sizeof(resize_arg), &rp, (size_t)(percent + 0.5));
        append_text(resize_arg, sizeof(resize_arg), &rp, "%");
        append_arg_cstr(line, sizeof(line), &pos, resize_arg);
        if (filter >= 0 && filter <= 3) {
            char filter_arg[32];
            size_t fp = 0;
            filter_arg[0] = '\0';
            append_text(filter_arg, sizeof(filter_arg), &fp, "--filter=");
            append_text(filter_arg, sizeof(filter_arg), &fp, FILTER_NAMES[filter]);
            append_arg_cstr(line, sizeof(line), &pos, filter_arg);
        }
    }
    if (grayscale) append_arg_cstr(line, sizeof(line), &pos, "--grayscale");
    if (privacy) append_arg_cstr(line, sizeof(line), &pos, "--privacy-clean");
    if (format > 0 && format <= RUBRAVIEW_EXPORT_ICO) {
        u8str_t ext = rubraview_export_extension((rubraview_export_format_t)format);
        char format_arg[32];
        size_t gp = 0;
        format_arg[0] = '\0';
        append_text(format_arg, sizeof(format_arg), &gp, "--format=");
        for (size_t i = 0; i < ext.len && gp + 1 < sizeof(format_arg); ++i) format_arg[gp++] = ext.ptr[i];
        format_arg[gp] = '\0';
        append_arg_cstr(line, sizeof(line), &pos, format_arg);
    }
    {
        char out_arg[1024];
        size_t op = 0;
        out_arg[0] = '\0';
        append_text(out_arg, sizeof(out_arg), &op, "--out=");
        for (size_t i = 0; i < out_dir.len && op + 1 < sizeof(out_arg); ++i) out_arg[op++] = out_dir.ptr[i];
        out_arg[op] = '\0';
        append_arg_cstr(line, sizeof(line), &pos, out_arg);
    }
    append_arg(line, sizeof(line), &pos, app->source_dir);

    if (rubraview_pal_process_start_console((u8str_t){ .ptr = line, .len = pos })) {
        osd_say(app, U8("batch started in a window of its own"));
    } else {
        osd_say(app, U8("could not start the batch run"));
    }
}

static int run_batch(proven_arena_t *arena, const rubraview_cli_result_t *cli) {
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, BATCH_MAX_INPUTS, sizeof(rubraview_batch_input_t));
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

    /* --pause: a run started from the dialog has a console of its own,
       and that window closes with the process. Wait for a key so the
       counts above can be read. */
    if (cli->pause_at_end) {
        console_line("press a key to close this window");
        HANDLE in = console_handle(true);
        if (in) {
            FlushConsoleInputBuffer(in);
            for (;;) {
                INPUT_RECORD record;
                DWORD read = 0;
                if (!ReadConsoleInputW(in, &record, 1, &read) || read == 0) break;
                if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown) break;
            }
        }
    }

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
        rubraview_media_open_result_t opened = rubraview_pal_media_open(path, order[i], NULL);
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
    UINT profile_count = 0;
    if (SUCCEEDED(ID3D11Device_QueryInterface(device, &IID_ID3D11VideoDevice, (void**)&video)) && video) {
        UINT profiles = ID3D11VideoDevice_GetVideoDecoderProfileCount(video);
        profile_count = profiles;
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

    /* 4. The viewer's own path (RV-062): the media PAL handed this device
          the way [video] hardware_decode = on and = always hand it over.
          Whether the film decodes on the card, whether pictures come, and
          whether they arrive as textures (zero copy) or as pixels. */
    static const char *const MODE_NAMES[] = { "off", "on", "always" };
    for (int32_t mode = 0; mode <= 2; ++mode) {
        rubraview_media_gpu_t gpu = { .device = device, .decoder_profiles = profile_count, .mode = mode };
        rubraview_media_open_result_t opened = rubraview_pal_media_open(path, RUBRAVIEW_BACKEND_MEDIA_FOUNDATION, &gpu);
        if (!opened.media) {
            /* Why, so "cannot open" and "the card broke it" read differently. */
            u8str_t why = rubraview_media_failure_text(opened.failure);
            snprintf(line, sizeof(line), "hardware_decode = %s: the film did not open - %.*s",
                     MODE_NAMES[mode], (int)why.len, why.ptr);
            console_line(line);
            code = 2;
            continue;
        }
        /* As fast as it decodes, for four seconds: pictures per second and
           the CPU the whole program spent on them, so off and on compare. */
        FILETIME created, exited, kernel0, user0, kernel1, user1;
        GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel0, &user0);
        int pictures = 0, on_card = 0;
        double start = rubraview_pal_time_now_seconds(), elapsed = 0.0;
        while ((elapsed = rubraview_pal_time_now_seconds() - start) < 4.0) {
            rubraview_video_frame_t frame;
            if (rubraview_pal_media_peek_frame(opened.media, &frame)) {
                pictures++;
                if (frame.gpu_texture) on_card++;
                rubraview_pal_media_pop_frame(opened.media);
            } else if (rubraview_pal_media_finished(opened.media)) {
                break;
            } else {
                rubraview_pal_time_sleep_ms(1);
            }
        }
        GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel1, &user1);
        #define RV_FT(ft) ((double)(((uint64_t)(ft).dwHighDateTime << 32) | (ft).dwLowDateTime) / 1e7)
        double cpu = RV_FT(kernel1) - RV_FT(kernel0) + RV_FT(user1) - RV_FT(user0);
        #undef RV_FT
        uint32_t cc = opened.info.video_fourcc;
        char codec[5] = { (char)(cc & 0xFF), (char)((cc >> 8) & 0xFF), (char)((cc >> 16) & 0xFF), (char)(cc >> 24), 0 };
        for (int k = 0; k < 4; ++k) if (codec[k] < 32 || codec[k] > 126) codec[k] = '?';
        snprintf(line, sizeof(line),
                 "hardware_decode = %s: %s %dx%d, decoded %s; %d pictures in %.1f s (%.0f a second), "
                 "%d of them on the card; CPU %.2f s (%.0f%% of one core)",
                 MODE_NAMES[mode], codec, opened.info.width, opened.info.height,
                 opened.info.hardware_decode ? "on the card" : "in software",
                 pictures, elapsed, elapsed > 0.0 ? pictures / elapsed : 0.0, on_card,
                 cpu, elapsed > 0.0 ? 100.0 * cpu / elapsed : 0.0);
        console_line(line);
        if (pictures == 0 && opened.info.has_video) code = 2;
        rubraview_pal_media_close(opened.media);
    }

    ID3D11DeviceContext_Release(context);
    ID3D11Device_Release(device);

    /* 5. The viewer's own renderer (0.0.7 on the owner's card: the probe
          played the film, the viewer showed a black window). A hidden
          window and the real renderer; the decoder's device from it, the
          film opened with it, frames copied into a film texture and drawn
          once, and what was drawn read back. */
    HWND hidden = CreateWindowExW(0, L"STATIC", L"rubraview probe", WS_POPUP, 0, 0, 320, 240, NULL, NULL,
                                  GetModuleHandleW(NULL), NULL);
    rubraview_renderer_t *renderer = hidden ? rubraview_pal_render_create(arena, hidden, 320, 240) : NULL;
    if (!renderer) {
        console_line("viewer path: the renderer would not start here");
        if (hidden) DestroyWindow(hidden);
        return 2;
    }
    uint32_t viewer_profiles = 0;
    void *decode_device = rubraview_pal_render_video_device(renderer, &viewer_profiles);
    snprintf(line, sizeof(line), "viewer path: the decoder's own device %s, %u decoder profiles",
             decode_device ? "made" : "NOT made", (unsigned)viewer_profiles);
    if (!decode_device) {
        console_line(line);
        snprintf(line, sizeof(line), "viewer path: the last Direct3D answer was 0x%08X",
                 (unsigned)rubraview_pal_render_last_hresult());
    }
    console_line(line);
  for (int32_t vmode = 1; vmode <= 2; ++vmode) {
    rubraview_media_gpu_t vgpu = { .device = decode_device, .decoder_profiles = viewer_profiles, .mode = vmode };
    double opened_in = rubraview_pal_time_now_seconds();
    rubraview_media_open_result_t vopen = rubraview_pal_media_open(path, RUBRAVIEW_BACKEND_MEDIA_FOUNDATION, &vgpu);
    opened_in = rubraview_pal_time_now_seconds() - opened_in;
    if (!vopen.media) {
        u8str_t why = rubraview_media_failure_text(vopen.failure);
        snprintf(line, sizeof(line), "viewer path (%s): the film did not open (%.1f s) - %.*s", MODE_NAMES[vmode], opened_in, (int)why.len, why.ptr);
        console_line(line);
        code = 2;
    } else {
        rubraview_texture_t *page = vopen.info.hardware_decode
            ? rubraview_pal_texture_create_video(renderer, vopen.info.width, vopen.info.height) : NULL;
        int pictures = 0, copied = 0, drawn = 0;
        double brightness = -1.0;
        double start = rubraview_pal_time_now_seconds();
        while (rubraview_pal_time_now_seconds() - start < 3.0 && pictures < 60) {
            rubraview_video_frame_t frame;
            if (!rubraview_pal_media_peek_frame(vopen.media, &frame)) { rubraview_pal_time_sleep_ms(2); continue; }
            pictures++;
            if (page && frame.gpu_texture &&
                rubraview_pal_texture_copy_video_frame(page, frame.gpu_texture, frame.gpu_subresource)) {
                copied++;
                rubraview_pal_render_begin(renderer, 0xFF000000u);
                rubraview_pal_render_draw_texture(renderer, page,
                    (rubraview_mat3x2_t){ .a = 320.0 / vopen.info.width, .d = 240.0 / vopen.info.height },
                    RUBRAVIEW_INTERP_LINEAR);
                if (rubraview_pal_render_end(renderer)) drawn++;
                if (pictures > 20) rubraview_pal_texture_video_brightness(page, &brightness);
            }
            rubraview_pal_media_pop_frame(vopen.media);
        }
        snprintf(line, sizeof(line),
                 "viewer path (%s): opened in %.1f s, decoded %s; %d pictures, %d copied to the screen texture, "
                 "%d drawn; brightness of the picture %.0f of 255",
                 MODE_NAMES[vmode], opened_in, vopen.info.hardware_decode ? "on the card" : "in software", pictures, copied, drawn,
                 brightness);
        console_line(line);
        if (vopen.info.hardware_decode && (copied == 0 || drawn == 0)) code = 2;
        if (page) rubraview_pal_texture_destroy(page);
        rubraview_pal_media_close(vopen.media);
    }
  }
    rubraview_pal_render_destroy(renderer);
    DestroyWindow(hidden);
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
        if (boxes[i]->state == RUBRAVIEW_BOX_DETACHED) continue;
        /* Only the left half is a handle (owner, 2026-09-22): the right
           one only hovers. */
        if (rubraview_box_anchor_half_at(boxes[i], &metrics, x, y) != RUBRAVIEW_ANCHOR_CLICK) continue;
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
    /* §3.6.1 / RFC-0002 Q6: the toolbox dragged past the edge becomes a
       window of its own, and the drag carries on moving that window. */
    if (app->box_drag == &app->toolbox &&
        rubraview_box_update_detach(&app->toolbox, &metrics, (double)win_w, (double)win_h, 24.0 * rubraview_pal_window_dpi_scale(app->window))) {
        int32_t fx = 0, fy = 0, fw = 0, fh = 0;
        rubraview_pal_window_get_frame(app->window, &fx, &fy, &fw, &fh);
        app->box_drag = NULL;
        toolbox_detach(app, fx + (int32_t)app->toolbox.anchor_x, fy + (int32_t)app->toolbox.anchor_y, true);
    }
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
                    /* Through the settings loader, which knows where the key
                       lives now ([general]) and where older files put it. */
                    rubraview_settings_t loaded = rubraview_settings_load(&arena, settings);
                    single_instance = rubraview_settings_get(&loaded, U8("general"), U8("single_instance")) > 0.5;
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
    app.curve_dragging = -1;   /* no point held; 0 would mean the first one */
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

    /* §3.1: no worker pool. Every decode touches WIC, Direct2D and the
       arena, none of which may be used off this thread; the ring runs
       without one and precache_decode queues pages for the main loop. */
    app.jobs = NULL;
    app.media_page = -1;
    /* §3.22 [video] decoder (D-9): which backend opens a file first. The
       other one still gets its turn when this one cannot. */
    app.media_preferred = preferred_backend(&arena);
    history_load(&app);
    load_keymap(&app);   /* after history_load: it settles portable or AppData, and so where keymap.ini is */
    /* history_load read settings.ini from wherever it lives (beside the
       program or in AppData); the decoder choice comes from that, not
       only from a file in the working folder. */
    settings_took_effect(&app);
    app.osd = rubraview_osd_create(2.0, 0.5);            /* §3.1 */
    app.titlebar = rubraview_titlebar_create(dpi);       /* §3.21.2 */
    app.toolbox = rubraview_box_create(RUBRAVIEW_BOX_TOOLBOX, (double)win_w - 220.0 * dpi, (double)win_h - 160.0 * dpi, 8);   /* toolbox_refresh sets the real count */
    app.menubox = rubraview_box_create(RUBRAVIEW_BOX_MENU, 24.0 * dpi, 24.0 * dpi, app.menu_tree.root_count);
    layout_load(&app);   /* §3.6: back where the reader left them */
    menu_rebuild(&app);   /* §3.6.2, RFC-0002 §5: the tree from the boxes' document */
    g_caption_app = &app;
    app.transition = rubraview_transition_create(RUBRAVIEW_TRANSITION_CROSSFADE, 0.25);
    app.cursor = rubraview_cursor_hide_create(1.5);      /* §3.2.5 */
    app.filmstrip = rubraview_filmstrip_create(0, FILMSTRIP_THUMB * dpi, (double)win_w);

    /* §3.18.3: the triage folders, and §3.19.2: accept drops. The folders
       are read from the settings file the viewer actually uses — until
       2026-09-23 this said `settings.ini`, a name with no folder, so the
       numbers only curated when the viewer happened to be started in a
       folder holding one, which is to say almost never. */
    {
        u8str_t settings = rubraview_pal_fs_read_file(&arena, app.settings_path, 256u * 1024u);
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
    double last_render_seconds = app.last_frame_seconds;
    double last_input_seconds = app.last_frame_seconds;

    while (!rubraview_pal_window_should_close(app.window)) {
        rubraview_window_event_t event;
        size_t handled = 0;
        {
            /* RFC-0002 §4: the boxes open inside the window, whatever its size now. */
            int32_t view_w = 0, view_h = 0;
            rubraview_pal_window_get_size(app.window, &view_w, &view_h);
            app.toolbox.view_width = app.menubox.view_width = (double)view_w;
            app.toolbox.view_height = app.menubox.view_height = (double)view_h;
        }
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
                    boxes_keep_in_reach(&app);
                    break;

                case RUBRAVIEW_WINDOW_EVENT_DPI_CHANGED:
                    app.titlebar = rubraview_titlebar_create(event.dpi.scale);
                    app.needs_relayout = true;
                    break;

                case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN:
                    dispatch_key(&app, event.key.combo);
                    break;

                case RUBRAVIEW_WINDOW_EVENT_TEXT:
                case RUBRAVIEW_WINDOW_EVENT_COMPOSITION:
                    rename_text(&app, &event);
                    break;

                case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE: {
                    if (app.curve_dragging >= 0) {
                        curve_widget_drag(&app, event.mouse.x, event.mouse.y);
                        app.pointer_x = event.mouse.x;
                        app.pointer_y = event.mouse.y;
                        break;
                    }
                    if (app.crop_dragging) {
                        int32_t ix = 0, iy = 0;
                        if (crop_point_to_image(&app, event.mouse.x, event.mouse.y, &ix, &iy)) {
                            rubraview_edit_crop_drag(&app.edit, app.crop_drag_x0, app.crop_drag_y0, ix, iy);
                        }
                        app.pointer_x = event.mouse.x;
                        app.pointer_y = event.mouse.y;
                        break;
                    }
                    if (app.panel.open && app.panel.active_row >= 0) {
                        int32_t dragged = -1;
                        if (rubraview_panel_drag(&app.panel, event.mouse.x, event.mouse.y, &dragged)
                                == RUBRAVIEW_PANEL_VALUE_CHANGED) {
                            panel_apply_row(&app, dragged);
                        }
                    }
                    if (app.timeline_dragging && rubraview_pal_time_now_seconds() - app.timeline_last_seek > 0.1) {
                        timeline_seek_to_pointer(&app, event.mouse.x);   /* a seek every tenth of a second at most */
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
                        if (event.mouse.y >= (double)ph - PICKER_ACTION_HEIGHT * dpi) {
                            for (int32_t b = 0; b < PICKER_BTN_COUNT; ++b) {
                                rubraview_pal_rect_t rect = picker_button_rect((double)pw, (double)ph, dpi, b);
                                if (event.mouse.x >= rect.x && event.mouse.x < rect.x + rect.width &&
                                    event.mouse.y >= rect.y && event.mouse.y < rect.y + rect.height) {
                                    picker_button(&app, (picker_button_t)b);
                                    break;
                                }
                            }
                            break;
                        }
                        double cell_w = (double)pw / (double)PICKER_COLUMNS;
                        size_t column = (size_t)(event.mouse.x / cell_w);
                        size_t row = (size_t)((event.mouse.y - crumb_h + app.picker.scroll_offset) / app.picker.tile_extent);
                        size_t index = row * PICKER_COLUMNS + column;
                        if (column < PICKER_COLUMNS && index < app.picker_listing.count) {
                            app.picker.focus = index;
                            /* In a picking mode the tap picks; otherwise it opens. */
                            if (!rubraview_picker_tap(&app.picker, index)) picker_activate(&app, index);
                        }
                        break;
                    }

                    /* §3.13: with the workbench open, a drag on the picture
                       is the crop rectangle. It is asked before the panel,
                       whose "a click outside closes it" rule would otherwise
                       shut the workbench at the first press on the picture. */
                    if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT && edit_panel_open(&app) &&
                        !rubraview_rect_contains(app.panel.bounds, event.mouse.x, event.mouse.y) &&
                        !point_in_curve_widget(&app, event.mouse.x, event.mouse.y) &&
                        crop_point_to_image(&app, event.mouse.x, event.mouse.y,
                                            &app.crop_drag_x0, &app.crop_drag_y0)) {
                        app.crop_dragging = true;
                        rubraview_edit_crop_drag(&app.edit, app.crop_drag_x0, app.crop_drag_y0,
                                                 app.crop_drag_x0, app.crop_drag_y0);
                        break;
                    }

                    if (panel_handle_press(&app, event.mouse.x, event.mouse.y)) break;
                    /* RFC-0002 §4.2: the seek bar, before the canvas turns a page. */
                    if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT && timeline_hit(&app, event.mouse.x, event.mouse.y, NULL)) {
                        app.timeline_dragging = true;
                        timeline_seek_to_pointer(&app, event.mouse.x);
                        break;
                    }
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
                    } else if (event.mouse.button == RUBRAVIEW_MOUSE_RIGHT &&
                               curve_widget_press(&app, event.mouse.x, event.mouse.y, true)) {
                        break;   /* a point taken off the curve */
                    } else if (event.mouse.button == RUBRAVIEW_MOUSE_RIGHT) {
                        intent = rubraview_pointer_right_click(&ctx);
                    } else {
                        intent = rubraview_pointer_click(&ctx, event.mouse.x, false);
                    }
                    apply_intent(&app, intent);
                    break;
                }

                case RUBRAVIEW_WINDOW_EVENT_MOUSE_UP:
                    app.curve_dragging = -1;
                    if (app.crop_dragging) {
                        app.crop_dragging = false;
                        int32_t ix = 0, iy = 0;
                        if (crop_point_to_image(&app, event.mouse.x, event.mouse.y, &ix, &iy)) {
                            rubraview_edit_crop_drag(&app.edit, app.crop_drag_x0, app.crop_drag_y0, ix, iy);
                        }
                        /* A click without a drag clears the rectangle rather
                           than leaving a one-pixel crop behind. */
                        if (app.edit.crop.width < 4 || app.edit.crop.height < 4) {
                            app.edit.crop_active = false;
                        }
                        break;
                    }
                    if (app.timeline_dragging) {
                        app.timeline_dragging = false;
                        timeline_seek_to_pointer(&app, event.mouse.x);
                        break;
                    }
                    rubraview_panel_release(&app.panel);
                    box_release(&app, event.mouse.x, event.mouse.y);
                    break;

                case RUBRAVIEW_WINDOW_EVENT_MOUSE_WHEEL: {
                    if (app.picker_open) {
                        rubraview_picker_scroll_by(&app.picker, -event.mouse.wheel_delta * app.picker.tile_extent * 0.5);
                        break;
                    }
                    if (box_wheel_opacity(&app, event.mouse.x, event.mouse.y, event.mouse.wheel_delta, event.mouse.modifiers)) break;
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

        /* §3.22 / D-13: the settings window's own queue. Polling the main
           window pumped the thread's messages into it. */
        handled += settings_pump(&app);
        handled += help_pump(&app);
        handled += mini_pump(&app);

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
        bgm_tick(&app);

        /* Redraw only while something can change on screen. Without a
           GPU, Direct2D rasterises on the CPU, and a loop that redrew
           the same still image kept two cores busy doing it. */
        bool media_playing = app.media && !app.media_paused;
        bool others_moving = app.slideshow_running || app.anim_active ||
                             app.notice_seconds > 0.0 || app.pending_decode_count > 0 ||
                             app.rename_active;   /* the caret blinks (owner, 2026-09-23) */
        if (handled > 0) last_input_seconds = now;
        if (handled > 0 || media_playing || others_moving) last_busy_seconds = now;
        bool settled = now - last_busy_seconds > IDLE_REDRAW_GRACE;
        /* Only the film or the sound moves: draw a new picture when one
           is up and the seek bar four times a second, not on every pass
           (T058 on the VM: every pass kept 1.75 of 2 cores busy on a
           sound-only page, and the keys waited behind it). For the grace
           after an input every pass still draws, as the chrome's fades
           expect. */
        bool paced = media_playing && !others_moving && now - last_input_seconds > IDLE_REDRAW_GRACE;
        bool draw = paced ? rubraview_media_redraw_due(app.media_new_picture, now - last_render_seconds)
                          : (!settled || now - last_idle_frame_seconds >= 1.0);
        if (draw) {
            render_frame(&app);
            last_render_seconds = now;
            app.media_new_picture = false;
            if (settled) last_idle_frame_seconds = now;
        }
        if (app.settings_open && now - app.settings_info_seconds >= 1.0) {
            /* `info` lines (cache in use, the adapter) are live data: once a second. */
            app.settings_info_seconds = now;
            app.settings_dirty = true;
        }
        draw_settings_window(&app);
        draw_help_window(&app);
        /* The strip moves while the track does, so the little window is
           redrawn each pass it is playing. */
        if (app.mini_open && !mini_paused(&app)) app.mini_dirty = true;
        draw_mini_window(&app);
        toolbox_window_pump(&app);
        draw_toolbox_window(&app);

        /* A queued decode takes the loop's idle slice. With nothing to
           get ready, a settled loop sleeps until the OS has something
           for it instead of spinning. */
        if (drain_one_pending_decode(&app)) {
            last_busy_seconds = now;
        } else if (settled && settings_pump(&app) == 0) {
            rubraview_pal_window_wait_event(app.window, 250);
        } else if (paced && !app.media_info.has_video) {
            /* Sound only: nothing is due before the next redraw, but the
               end and the A-B point are checked at least every 20 ms. */
            double wait = rubraview_media_redraw_wait(now - last_render_seconds) * 1000.0;
            rubraview_pal_window_wait_event(app.window, wait < 1.0 ? 1u : wait > 20.0 ? 20u : (uint32_t)wait);
        } else {
            rubraview_pal_time_sleep_ms(4);
        }
    }

    /* §3.22: closing the viewer with the settings window open still
       writes what was changed in it. */
    settings_close(&app);
    settings_write_if_changed(&app, false);   /* a volume or opacity changed with the window shut */
    if (app.toolbox_window) {
        rubraview_pal_render_destroy(app.toolbox_renderer);
        rubraview_pal_window_destroy(app.toolbox_window);
    }
    if (app.settings_window) {
        rubraview_pal_render_destroy(app.settings_renderer);
        rubraview_pal_window_destroy(app.settings_window);
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
