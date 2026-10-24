/*
 * The program's shared state, types and the functions its files call across (RV-163, D-197).
 * Lines 16-1302 of the old main.c as they were; its file-scope variables are declared here and defined
 * in main.c, its small helpers are static inline.
 */
#ifndef RUBRAVIEW_APP_H
#define RUBRAVIEW_APP_H

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
#include <ctype.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "rubraview/number.h"
#include "rubraview/lang.h"
#include "rubraview/subbox.h"
#include "rubraview/aspect.h"
#include "rubraview/detail.h"
#include "rubraview/thumb.h"
#include "rubraview/textedit.h"
#include "rubraview/pal/pal_thumbs.h"
#include "rubraview/pal/pal_tiles.h"
#include "rubraview/pal/pal_gpu_resample.h"
#include "rubraview/resample_mt.h"
#include "rubraview/core.h"
#include "rubraview/path.h"
#include "rubraview/keymap.h"
#include "rubraview/layout.h"
#include "rubraview/viewport.h"
#include "rubraview/compositor.h"
#include "rubraview/tiles.h"
#include "rubraview/transform.h"
#include "rubraview/slideshow.h"
#include "rubraview/subtitle.h"
#include "rubraview/encoding.h"
#include "rubraview/glob.h"
#include "rubraview/shellreq.h"
#include "rubraview/arcedit.h"
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
#include "rubraview/ui_edgenav.h"
#include "rubraview/ui_subwin.h"
#include "rubraview/ui_ctxmenu.h"
#include "rubraview/ui_chooser.h"
#include "rubraview/action_words.h"
#include "rubraview/ui_slidepop.h"
#include "rubraview/matroska.h"
#include "rubraview/mp4.h"
#include "rubraview/ui_listwin.h"
#include "rubraview/classify.h"
#include "rubraview/fontspec.h"
#include "rubraview/memory.h"
#include "rubraview/ui_eqwin.h"
#include "rubraview/repeat.h"
#include "rubraview/lyrics.h"
#include "rubraview/filmstrip.h"
#include "rubraview/picker.h"
#include "rubraview/favorites.h"
#include "rubraview/fileinfo.h"
#include "rubraview/notices.h"
#include <time.h>
#include "rubraview/playlist.h"
#include "rubraview/stream.h"
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
#include "rubraview/keepinfo.h"
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
#include "rubraview/tonemap.h"
#include "rubraview/plugin.h"
#include "rubraview/plugin_filter.h"
#include "rubraview/pal/pal_plugin.h"
#include "rubraview/pal/pal_gmic.h"
#include "rubraview/gmic.h"
#include "rubraview/pal/pal_ffilter.h"
#include "rubraview/ffilter.h"
#include "rubraview/playback.h"
#include "rubraview/textenc.h"
#include "rubraview/textview.h"
#include "rubraview/effects.h"

#define IMAGE_FILTER "*.jpg;*.jpeg;*.png;*.apng;*.webp;*.gif;*.bmp;*.tif;*.tiff;*.ico;*.avif;*.jxl"
/* M5: video and sound files join the folder as pages (sound since slice 2, RV-084). */
#define VIDEO_FILTER "*.mp4;*.m4v;*.mov;*.mkv;*.webm;*.avi;*.wmv;*.asf;*.ts;*.m2ts;*.mts;*.mpg;*.mpeg;*.flv;*.ogv;*.3gp"
#define AUDIO_FILTER "*.mp3;*.m4a;*.aac;*.flac;*.wav;*.wma;*.ogg;*.oga;*.opus"
#define MEDIA_FILTER VIDEO_FILTER ";" AUDIO_FILTER
/* Playlists; RV-157: and the lists internet radio comes in (.pls, and Windows Media's .asx, .wax, .wvx). */
#define LIST_FILTER "*.m3u8;*.m3u;*.rvlist;*.pls;*.asx;*.wax;*.wvx"
/* RFC-0005: text files are pages too (the owner, 2026-10-17: .txt, .log, .nfo, .md). */
#define TEXT_FILTER "*.txt;*.log;*.nfo;*.md"
/* 1.25.0 (owner, 2026-10-20: "텍스트 뷰어를 사용할지 안할지 ... 텍스트 파일을 목록 등에서도 받지 않고 보여주지도 않음"):
   Settings › Text › Show text files; off, the text kinds are left out wherever files are listed or opened. */
extern bool g_text_on;
#define TEXT_KINDS (g_text_on ? ";" TEXT_FILTER : "")
/* `base` with the text kinds after it when they are shown; `buf` holds it. */
static inline proven_u8str_view_t kinds_with_text(char *buf, size_t cap, const char *base) {
    int n = snprintf(buf, cap, "%s%s", base, TEXT_KINDS);
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)buf, .size = n > 0 && (size_t)n < cap ? (size_t)n : 0 };
}
/* How far the picture's clock may run on from the last thing the sound
   thread reported, before it waits for the next report. */
#define MEDIA_AUDIO_EXTRAPOLATION 0.2
#define MEDIA_SEEK_STEP 5.0
#define MEDIA_SEEK_FINE 1.0    /* RV-107: Ctrl+Shift + an arrow */
#define MEDIA_SEEK_LONG 30.0   /* the edge buttons' second step, Shift + an arrow (owner, 2026-09-29) */
#define MEDIA_NOTICE_SECONDS 5.0
/* How far the picture may fall behind the clock before the clock is
   moved to the picture instead: on a machine that cannot decode in
   real time, playing a little slow beats a slideshow of jumps. */
#define MEDIA_RESYNC_SECONDS 0.25
#define PIXEL_GRID_MIN_SCALE 4.0 /* §3.5: the grid appears from 400% zoom */
/* Settings › Viewer › Zoom step (owner, 2026-09-30): 10 % a step unless set. */
extern double g_zoom_step;
/* RV-106 (RFC-0004 §1). */
extern bool g_whole_zoom;
extern int  g_cli_sort;
extern int  g_cli_descending;
extern bool g_cli_subfolders;
extern rubraview_transition_pose_t g_pose;
extern bool g_pose_on;
/* RV-107 (RFC-0004 §2). */
extern uint32_t g_canvas;
extern uint32_t g_subtitle_argb;
extern unsigned g_subtitle_style;
#define ZOOM_STEP g_zoom_step
#define PAN_STEP 60.0
/* Settings › Viewer › Gutter: the gap between two pages, in pixels. */
extern double g_gutter;
#define GUTTER g_gutter
#define KEYMAP_MAX_BYTES (256u * 1024u)
#define FILMSTRIP_THUMB 120.0
#define ARCHIVE_FILTER "*.cbz;*.zip;*.cb7;*.7z;*.cbr;*.rar;*.alz;*.egg"
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
#define RECENT_KEPT 30          /* D-104: files in recent.ini; the menu shows the newest ten */
#define RECENT_SKIP_DIRS 264   /* the list, as many as wanted, and the eight of before (owner, 2026-10-16) */
#define MENU_MAX_TILES 16   /* 4 x 4; the owner chose a larger grid over splitting File (2026-09-21) */

/* Metro palette (§3.6.4): flat, high-contrast, no gradients. */
#define COLOR_CANVAS      g_canvas
#define COLOR_BOX_FILL    0xE01A1A1Au
#define COLOR_BOX_BORDER  0x30FFFFFFu
#define COLOR_TILE_FILL   0xE0242424u
#define COLOR_GROUP_FILL   0xE0303A4Au   /* a toolbox group's ground (owner, 2026-10-15) */
#define COLOR_GROUP_BORDER 0xA08A9BB8u
#define COLOR_TEXT        0xFFF0F0F0u
#define COLOR_BAR_FILL    0xE1141414u
#define COLOR_CLOSE_HOVER 0xFFE81123u
/* The choice in use among several (a layout, a fit): Settings › Display ›
   Accent colour (owner, 2026-09-30), crimson as the icon unless chosen. */
extern uint32_t g_accent;
#define COLOR_TILE_CURRENT g_accent

/* Settings › Display › Touch tile size (owner, 2026-09-30): 48, 64 or 96
   pixels a menu tile; the anchors and the toolbox's buttons grow with it. */
extern double g_tile_base;
static inline rubraview_tile_metrics_t tile_metrics(double dpi_scale) {
    rubraview_tile_metrics_t m = rubraview_tile_metrics_default(dpi_scale);
    double k = g_tile_base / 64.0;
    m.tile_size *= k;
    m.anchor_size *= k;
    m.button_size *= k;
    return m;
}



/* The boxes' tiles and menu tree come from their document (RFC-0002,
   D-15): src/core/default_boxes_doc.c. */

/* PROVEN_LIT() only works on string literals; this is its runtime counterpart. */
static inline proven_u8str_view_t cstr(const char *s) {
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)s, .size = strlen(s) };
}

/* D-99: how long one viewer waits for another to finish with a state file
   before writing anyway — the files are exchanged whole, so the worst is
   one change lost, never a broken file. */
#define CONFIG_LOCK_MS 2000u

typedef struct app_page {
    rubraview_texture_t *texture;
    int32_t width, height;             /* the texture's */
    int32_t full_width, full_height;   /* the picture's, when the texture is reduced */
    bool reduced;                      /* too large for the device: held smaller (the fall-back) */
    bool loaded;
    bool failed;
    bool failure_said;                 /* RV-110: why it is empty was said once */
    bool animated;                     /* D-98: a picture that moves, found when it was read */
    bool skipped;                      /* D-98: left out of the layout now (rebuild_layout) */
    /* 1.25.0 (owner, 2026-10-20: "Shift+B의 펼침면 설정이 자꾸 풀림"): the shape it had when last read, kept when its
       texture goes, so a wide page put out of memory is not laid out as a narrow one and paired */
    int32_t seen_w, seen_h;
} app_page_t;

/* A page's texture let go; its shape remembered. */
static inline void page_forget(app_page_t *page) {
    int32_t w = page->seen_w, h = page->seen_h;
    *page = (app_page_t){0};
    page->seen_w = w;
    page->seen_h = h;
}

/* The size a page is laid out, zoomed and cropped at: the picture's own,
   even when its texture had to be reduced — so actual size, the crop and
   the status line all speak of the real picture. */
static inline void page_picture_size(const app_page_t *page, int32_t *out_w, int32_t *out_h);
static inline int32_t picture_w(const app_page_t *page) { int32_t w = 0, h = 0; page_picture_size(page, &w, &h); return w; }
static inline int32_t picture_h(const app_page_t *page) { int32_t w = 0, h = 0; page_picture_size(page, &w, &h); return h; }

/* The file's own pixels: what editing, saving and the information speak of. */
static inline void page_real_size(const app_page_t *page, int32_t *out_w, int32_t *out_h) {
    bool full = page->reduced && page->full_width > 0 && page->full_height > 0;
    *out_w = full ? page->full_width : page->width;
    *out_h = full ? page->full_height : page->height;
}
static inline int32_t real_w(const app_page_t *page) { int32_t w = 0, h = 0; page_real_size(page, &w, &h); return w; }
static inline int32_t real_h(const app_page_t *page) { int32_t w = 0, h = 0; page_real_size(page, &w, &h); return h; }

/* D-94: the shape chosen to look at pictures in (0: their own), and
   whether they are shown turned a quarter — set by aspect_refresh. The
   layout size is the real one stretched to it; the texture is drawn to
   the layout size by page_texture_prescale, as a reduced one already was. */
extern double g_view_ratio;
extern bool g_view_turned;

static inline void page_picture_size(const app_page_t *page, int32_t *out_w, int32_t *out_h) {
    int32_t w = 0, h = 0;
    page_real_size(page, &w, &h);
    rubraview_aspect_apply(g_view_ratio, g_view_turned, w, h, out_w, out_h);
}

/* Texture pixels to picture pixels: identity unless the page is reduced. */
static inline rubraview_mat3x2_t page_texture_prescale(const app_page_t *page) {
    int32_t w = 0, h = 0;
    page_picture_size(page, &w, &h);
    if (page->width <= 0 || page->height <= 0) return rubraview_mat3x2_scale(1.0, 1.0);
    return rubraview_mat3x2_scale((double)w / (double)page->width, (double)h / (double)page->height);
}

/* A page far inside a streamed 7z block, read on a thread of its own
   (owner, 2026-09-29, plan far-jump-off-main). The thread holds the
   source's lock while it reads; the main thread cancels it before it
   reads anything itself, and never waits long. */
typedef struct page_job {
    rubraview_page_source_t *source;
    SRWLOCK *lock;
    size_t index;
    void *mem;                  /* the page's own memory, handed over */
    rubraview_page_bytes_t bytes;
    HANDLE thread;
    rubraview_window_t *wake;
    volatile LONG done;
} page_job_t;

#define PAGE_JOB_MIN_COST (32u * 1024u * 1024u)   /* bytes to decode first before a page goes to the thread */

/* D-88: an archive written again with one page deleted, renamed, replaced
   or added, on a thread of its own; the main thread then puts the new file
   in the old one's place and opens it. */
typedef struct arc_job {
    rubraview_arcedit_job_t job;
    char archive[1024];      /* the set's first volume: what is read, and what an in-place edit replaces */
    char opened[1024];       /* the path the viewer opened (a later volume, for a set opened by one) */
    char out[1024];
    char name[512];
    char land[1024];         /* the page to be on afterwards, by its name; empty: by its number */
    size_t page;
    uint8_t *data;
    bool convert;
    rubraview_window_t *wake;
    HANDLE thread;
    volatile LONG done;
    int result;
} arc_job_t;

/* What is asked before an archive is written again. */
typedef enum { ARC_ASK_NONE = 0, ARC_ASK_DELETE, ARC_ASK_FORMAT } arc_ask_t;

typedef struct save_job {
    /* D-88: the page of an archive, edited: the copy is made between two
       temporary files and then put into the archive. */
    bool into_archive;
    rubraview_arcedit_op_t arc_op;
    size_t arc_entry;
    char arc_path[1024];
    char arc_name[512];
    char source[1024], out[1024];
    rubraview_edit_session_t edit;
    rubraview_edit_steps_t steps;   /* the owner's edit: the steps in force, applied in order */
    bool over_original;             /* written beside it, then put in its place (the old one to the Recycle Bin) */
    bool apply_edit;
    rubraview_export_options_t options;
    rubraview_export_format_t source_format;
    int route;
    int32_t width, height;
    rubraview_window_t *wake;
    HANDLE thread;
    volatile LONG done;
    int result;
    const char *why;   /* RV-139: SAVE_UNWRITABLE, when the writer said why (an N_ literal) */
} save_job_t;

/* The owner (2026-10-18): Edit's steps onto every picture of the list, each
   written as a copy beside it, named with a prefix or a suffix. */
typedef struct apply_all_job {
    char **paths;
    size_t count;
    rubraview_edit_steps_t steps;
    char prefix[64], suffix[64];
    bool over;   /* RV-137: written over each original (the old one to the Recycle Bin), not beside it */
    rubraview_export_options_t options;
    rubraview_window_t *wake;
    HANDLE thread;
    volatile LONG done, failed, finished, cancel;
} apply_all_job_t;

/* 1.24.0 (owner, 2026-10-19): the list a one-of-many choice opens, in the window where it was pressed. */
enum { CHOOSE_TILE = 1, CHOOSE_PANEL, CHOOSE_SETTING, CHOOSE_SORT, CHOOSE_ORDER, CHOOSE_LISTEXT, CHOOSE_LISTSORT };
enum { CHOOSE_IN_MAIN = 0, CHOOSE_IN_SETTINGS, CHOOSE_IN_EDIT, CHOOSE_IN_FILES };
#define CHOOSE_MAX 48
typedef struct chooser {
    bool    open;
    int     kind, where;
    char    action[40];                    /* a tile's action */
    int32_t panel, row;                    /* a panel's (1 the panel, 2 the selection's) row */
    const rubraview_setting_def_t *def;    /* a setting */
    size_t  count;
    int32_t current, focus;
    char    title[96];
    char    words[CHOOSE_MAX][96];
    double  values[CHOOSE_MAX];            /* what each sets, where a number does */
    rubraview_chooser_layout_t layout;
} chooser_t;

typedef struct app_state {
    proven_arena_t *arena;    /* RFC-0006: &mem->persist.arena, the region of what lasts */
    rubraview_mem_t *mem;
    /* RFC-0006: a window that opens and closes is made, with its renderer, in a block of the
       windows pool, given back when it closes. */
    struct window_block { void *block; proven_arena_t arena; } mini_block, toolbox_block, info_block, help_block, files_block, fontpick_block, subs_block, edit_block;
    rubraview_window_t *window;
    rubraview_renderer_t *renderer;
    rubraview_keymap_t keymap;
    /* §3.22.2 / D-14: keymap.ini sits with settings.ini; what it held when
       the settings window opened is what Revert goes back to. */
    rubraview_keymap_t keymap_saved;
    proven_u8str_view_t            keymap_path;
    int32_t            key_capture;       /* a Keys row waiting for its new key (1..), or 0 */

    /* Pages come from a folder or a CBZ through the same source (§3.8.1). */
    rubraview_page_source_t source;
    proven_u8str_view_t                 source_dir;   /* the directory the source lives in */
    proven_u8str_view_t                 archive_bytes;/* the archive's bytes (its mapping), empty for a folder */
    rubraview_fs_mapping_t  archive_map;  /* the archive, mapped read-only (owner, 2026-09-28: comics of several GB) */
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
    proven_u8str_view_t                 history_path;
    rubraview_recent_t      recent;         /* D-104: File > Recent, kept in recent.ini */
    proven_u8str_view_t                 recent_path;
    rubraview_config_mode_t config_mode;
    rubraview_page_info_t  *comic_page_flags; /* §3.8.5 cover marks, applied on every relayout */

    /* §3.20: the animation or sub-page set belonging to the page on
       screen. Only one page animates at a time — the one being read. */
    rubraview_animation_t  animation;
    rubraview_frame_t     *anim_frames;
    int32_t                anim_page;    /* which page these frames describe; -1 for none */
    bool                   anim_active;
    /* D-82: an animated GIF or WebP is kept open and laid together frame
       by frame; a short one keeps every picture it has shown. */
    rubraview_anim_reader_t *anim_reader;
    rubraview_frame_canvas_t anim_canvas;
    uint8_t               *anim_buffers;  /* the canvas's three, in one piece */
    uint8_t              **anim_cache;    /* the picture as it stood after a frame, or NULL; `animation.frame_count` of them */
    size_t                 anim_keep_stride;  /* D-84: how far apart kept pictures lie; 1 keeps every frame */
    uint64_t               anim_kept_bytes;
    rubraview_texture_t   *anim_texture;  /* the page's texture while it is one this made */

    /* M5: the video on the current page, when it is one (D-8, D-9). */
    rubraview_media_t      *media;
    rubraview_media_info_t  media_info;
    rubraview_hdr_display_t media_hdr_screen;  /* RV-153: the screen as it was when the film opened */
    const char             *hdr_output_why;    /* the window could not go out as HDR (an N_ literal) */
    bool                    hdr_output_refused;/* ...so films open for SDR until the screen changes */
    double                  hdr_screen_check;  /* seconds since the screen was last looked at */
    int32_t                 media_hdr_handed;  /* RV-150: 1 Windows' decoder found it HDR and FFmpeg took it, to tone map it;
                                                  RV-152: 2 Windows named nothing (VP9, AV1) and FFmpeg found it HDR */
    rubraview_media_clock_t media_clock;
    int32_t                media_page;        /* -1 when no video is open */
    bool                   media_paused;
    bool                   media_has_frame;   /* false until the first picture after opening or seeking */
    bool                   media_new_picture; /* a picture went up since the last redraw */
    bool                   media_skip_pending;/* D-9: a file nothing could open; move past it */
    double                 media_position;    /* pts of the picture on screen; the clock where there is none for now */
    double                 media_shown_pts;   /* the picture on screen: its time and length */
    double                 media_shown_duration;
    bool                   media_sought;      /* the picture awaited is a seek's, not the film's first */
    int64_t                media_title_tenth; /* the tenth of a second the title last showed */
    rubraview_clock_master_t media_master;    /* §5.3: the sound when it is heard, else the wall clock */
    rubraview_media_backend_t media_preferred; /* §3.22 [video] decoder — the one tried first (D-9) */

    /* §3.6: an anchor is a button and a handle at once; which one a press
       turns out to be is known only when it is released. */
    rubraview_box_t       *box_drag;          /* the box a press landed on, NULL when none */
    double                 box_grab_dx, box_grab_dy;   /* where inside the anchor it was grabbed */
    /* Where the two boxes are, as shares of the window ([0] the toolbox, [1] the menu box; x, y),
       and where that last put them: an anchor found elsewhere was moved by hand. */
    double                 box_share[2][2], box_put[2][2];
    bool                   box_share_known[2];
    double                 box_press_x, box_press_y;
    bool                   box_drag_moved;
    bool                   nav_drag;          /* 1.25.0: a press on T: dragged, it turns files (up/down) or folders (sideways) */
    int32_t                nav_done_x, nav_done_y;   /* the steps it has taken */
    rubraview_box_t       *box_sizing;        /* a box whose grip is being dragged (owner, 2026-09-29) */
    rubraview_titlebar_button_t titlebar_held;  /* the caption or Resize pressed, until the pointer moves or lets go */
    double                 titlebar_held_x, titlebar_held_y;
    /* The playlist window (owner, 2026-09-29): what is open, floating over the picture. */
    rubraview_listwin_t    listwin;
    /* The owner (2026-10-17): the recent files in a list window of their own */
    rubraview_listwin_t    recentwin;
    rubraview_listwin_t    favwin;                 /* 1.24.0: the favourites, files and folders */
    uint8_t                fav_kind[RUBRAVIEW_FAVORITES_MAX];   /* 0 gone, 1 folder, 2 file */
    bool                   fav_checked[RUBRAVIEW_FAVORITES_MAX];
    size_t                 fav_checked_n;
    struct recent_row { proven_u8str_view_t path; int64_t opened; uint64_t size; bool there; bool checked; } recent_rows[RECENT_KEPT];
    size_t                 recent_row_count;
    int32_t                listwin_shown_page;   /* the page it last scrolled to, to follow a new one */
    /* D-111: the list changed from its window. */
    bool                  *list_checked;         /* one a page: its row is checked */
    size_t                 list_checked_count;
    bool                   list_row_pressed, list_row_dragging;   /* a row's name held; moved, the row is being carried */
    bool                   list_held_in_files;  /* RV-117: that row is the file window's (F3), not the floating list's */
    size_t                 list_ctx_row;        /* RV-117: the row a right click opened its list for */
    size_t                 list_press_row, list_drop_at;
    double                 list_press_y;
    /* 1.25.0 (owner, 2026-10-20): the rows shown — those whose names hold the filter, of the
       extensions not let go (.ext) — and how the list was last sorted. `list_map[row]` is a page. */
    char                   list_filter[128];
    bool                   list_filter_in_files;   /* the file window's filter being typed (its own keys) */
    char                   list_ext_off[32][16];   /* ".jpg": extensions not shown */
    char                   list_ext_seen[48][16];  /* the .ext list's rows, as last opened */
    size_t                 list_ext_off_n;
    size_t                *list_map, list_map_n, list_map_cap;
    const void            *list_map_pages;
    size_t                 list_map_count;
    uint32_t               list_map_gen, list_view_gen;
    int                    list_sort_key;          /* 0 none, 1 name, 2 extension, 3 size, 4 modified, 5 created */
    bool                   list_sort_desc;
    int32_t                list_tip_page;          /* 1.25.0: the row the pointer rests on, for its thumbnail; -1 */
    rubraview_eqwin_t      eqwin;                /* the equaliser's ten sliders (owner, 2026-10-01) */
    /* The `.cue` sheets in the folder of the song playing, listed once a
       folder: a listing each track grew the working arena, which is never
       given back. */
    char                   cue_scan_dir[1024];
    size_t                 cue_scan_dir_len;
    proven_u8str_view_t               *cue_scan_paths;
    size_t                 cue_scan_count;
    bool                   list_is_set;          /* the pages are a playlist or picked files, not a folder or a book */
    /* D-86: what Explorer's menu asked, gathered until the selection is whole. */
    rubraview_shellreq_collector_t shell_requests;
    proven_u8str_view_t               *batch_list;           /* "Convert..." on chosen files: the batch panel runs on these */
    size_t                 batch_list_count;
    /* What plays after a film or a song ends (owner, 2026-09-29): the page
       decided for `follow_for` in `follow_mode`, and the shuffle's flags. */
    int32_t                follow_page, follow_for;
    int32_t                follow_mode;
    size_t                 follow_count;
    rubraview_shuffle_t    shuffle;
    uint8_t               *shuffle_flags;
    size_t                 shuffle_flags_count;
    bool                   media_ended;       /* reached the end; Space plays it again from the start */
    double                 media_progress_pos, media_progress_at;   /* 1.25.2: the last picture's time plus the sound heard, and when it last changed */
    double                 sound_dry_since;   /* when the sound device last ran empty; 0 while it plays */
    double                 media_speed;       /* D-15: 0.25–4.0, kept across files for the session */
    double                 ab_a, ab_b;        /* D-15 A-B repeat points in file seconds, -1 when unset */
    /* The same two points, typed (owner, 2026-09-24). The box holds one
       field per point; a key stamps the playhead into the one in hand,
       and what is typed goes back to the points when it closes. */
    bool                   ab_edit_open;
    int32_t                ab_edit_field;     /* 0 = A, 1 = B */
    int32_t                ab_edit_part;      /* 0 hours, 1 minutes, 2 seconds (owner, 2026-10-15) */
    bool                   ab_edit_fresh;     /* the field in hand was just come to: the first digit replaces it */
    char                   ab_edit_text[2][3][12];
    size_t                 ab_edit_length[2][3];
    /* The Adjust panel's size in pixels (owner, 2026-10-15): width and height typed,
       the other following the shape. */
    bool                   px_edit_open;
    bool                   px_edit_crop;      /* the crop's corner and size instead (owner, 2026-10-15) */
    int32_t                px_edit_field;     /* 0 = width, 1 = height; for the crop 0..3 = x, y, width, height */
    char                   px_edit_text[4][16];   /* RV-128: a size may carry its unit, "21cm" */
    size_t                 px_edit_length[4];
    /* D-44: the page bar held and dragged (owner, 2026-09-28), in the
       main window or the detached toolbox's, with the bar where it was pressed. */
    bool                   pagebar_dragging, pagebar_detached;
    double                 pagebar_x, pagebar_width, pagebar_last;
    /* RFC-0002 Q6: the toolbox as a window of its own once dragged out. */
    rubraview_window_t    *toolbox_window;
    rubraview_renderer_t  *toolbox_renderer;
    double                 toolbox_window_drawn;
    double                 toolbox_window_pointer_x, toolbox_window_pointer_y;   /* for the hovered button's caption */
    double                 toolbox_window_opacity;
    /* §3.16.1 / R135: the external subtitle file that goes with the
       video on screen. Empty when the film has none. */
    rubraview_subtitle_track_t subtitle;
    proven_u8str_view_t                    subtitle_name;   /* what to say in the OSD */
    /* A DVD's picture subtitles (owner, 2026-09-23). The index is held
       whole; one picture at a time is decoded into `vobsub_pixels` and
       uploaded, and the texture is kept while that subtitle is up. */
    rubraview_vobsub_track_t   vobsub;
    proven_u8str_view_t                    vobsub_sub;          /* the `.sub` file's bytes (its mapping) */
    rubraview_fs_mapping_t     vobsub_map;
    uint8_t                   *vobsub_pixels;
    rubraview_texture_t       *vobsub_texture;
    int32_t                    vobsub_texture_cue;  /* which one the texture holds, -1 for none */
    int32_t                    vobsub_texture_w, vobsub_texture_h;
    rubraview_vobsub_cue_t     vobsub_cue;
    /* A Blu-ray's picture subtitles (owner, 2026-09-24). The same shape
       as the DVD's above, over a `.sup` file instead of an index pair. */
    rubraview_pgs_track_t      pgs;
    proven_u8str_view_t                    pgs_bytes;           /* the `.sup` file itself (its mapping) */
    rubraview_fs_mapping_t     pgs_map;
    uint8_t                   *pgs_pixels;
    rubraview_texture_t       *pgs_texture;
    int32_t                    pgs_texture_cue;
    int32_t                    pgs_texture_w, pgs_texture_h;
    rubraview_pgs_cue_t        pgs_cue;
    /* RV-076: what a music file says about itself, and the backdrop made
       from its cover. Both belong to the track on screen. */
    rubraview_tags_t           music_tags;
    bool                       music_tags_read;
    /* The music features (owner, 2026-09-30): the lines of a `.lrc` beside
       the song, a `.cue` cutting it into tracks, the analyser's bands and
       their falling peaks, and where each lyric line was drawn (a click
       there goes to it). */
    rubraview_lyrics_t         music_lyrics;
    rubraview_cue_sheet_t      music_cue;
    float                      viz_bands[RUBRAVIEW_SPECTRUM_BANDS], viz_peaks[RUBRAVIEW_SPECTRUM_BANDS];
    double                     viz_last;
    int32_t                    lyric_row_index[7];
    rubraview_rect_t           lyric_row_rect[7];
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
    struct station_job        *station_job;   /* RV-157: a station being connected to, off this thread */
    char                      station_title[256];   /* RV-158: what the station playing says is on (ICY); "" none */
    bool                      rename_is_address;    /* RV-158: the box takes an address and its title for the list */
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
    /* The file list window (owner, 2026-10-08): a window of its own beside
       the viewer — the archive's (or the folder's) files on the left, a
       preview on the right; a row clicked or Enter turns the viewer to it. */
    /* The subtitle window (owner, 2026-10-14, RV-115): the film's subtitles
       a row each in a window of its own; a click goes there, the boxes
       choose rows, and the bar above moves the chosen ones in time. */
    rubraview_window_t        *subs_window;
    rubraview_renderer_t      *subs_renderer;
    bool                       subs_open, subs_dirty;
    rubraview_confirm_t        subs_leave;         /* leaving with changes not saved: the second try goes on */
    double                     subs_leave_through; /* until then the rest of a going let through goes on */
    bool                       subs_leave_unasked; /* the going is the film's end or the slide show: held, never armed */
    /* RV-116: a right click on the picture — the list for what is on screen,
       or, while the slide show runs, its settings */
    bool                       ctx_open;
    bool                       ctx_in_files;       /* RV-117: open in the file window (F3), over its rows */
    double                     files_px, files_py; /* the pointer in that window */
    double                     ctx_x, ctx_y;
    const rubraview_toolbox_profile_t *ctx_list;
    int32_t                    ctx_focus;          /* the row the arrow keys are on, -1 */
    /* 1.21.0 (owner, 2026-10-18: "우클릭 컨텍스트 메뉴의 '메뉴' 항목과 그 자체를 다 없애주세요 ... 다른 경로로
       접근 불가능한 기능이 있다면 ... 우클릭 컨텍스트 메뉴 쪽으로"): the menu's tree is the right click's lower
       rows, entered in place: the menu item shown (-1: the list itself), and the way back */
    bool                       ctx_on_picture;   /* opened on the picture: what the toolbox has is left out */
    chooser_t                  chooser;          /* 1.24.0 */
    rubraview_texture_t       *logo_tex;         /* 1.25.0: the icon on an empty window */
    bool                       logo_failed;
    uint32_t                   logo_back;
    bool                       tmenu_open;       /* 1.25.0: the title bar's ☰ pull-down */
    int32_t                    tmenu_focus;
    rubraview_confirm_t        ctx_confirm;        /* Delete asks twice, as in the menu */
    bool                       slidepop_open, slidepop_typing;
    proven_u8str_view_t                    hint;               /* RV-116: what the box part under the pointer does */
    char                       hint_text[320];     /* what `hint` points at: a tile's words and keys */
    double                     hint_since;         /* when the pointer came to rest on it */
    double                     slidepop_x, slidepop_y;
    char                       slidepop_text[16];
    size_t                     slidepop_len;
    int32_t                    subs_frame[4];
    rubraview_listwin_t        subs_list;
    uint32_t                  *subs_rows;          /* the cue of each row (text subtitles) */
    bool                      *subs_chosen;        /* a row's box */
    bool                      *subs_marks;         /* room for moving: a flag a cue */
    size_t                     subs_count, subs_room, subs_marks_room;
    const void                *subs_for;           /* the track the rows were made for */
    size_t                     subs_for_count;
    const char                *subs_for_language;
    uint32_t                subs_for_generation;   /* RFC-0006: the subtitle memory emptied since: another track at the same place */
    size_t                     subs_anchor;        /* the row last clicked */
    int32_t                    subs_hover, subs_now;
    double                     subs_scrolled_at;   /* the reader moved the list: it does not follow for a while */
    char                       subs_amount[16];    /* the seconds a press of Earlier / Later moves */
    size_t                     subs_amount_len;
    bool                       subs_amount_focus;
    /* RV-115 stage 3: the text subtitles of a Matroska film written out
       beside it, by our own reader; the film is walked a slice a frame. */
    struct {
        bool                   running;
        rubraview_fs_mapping_t map;
        rubraview_mkv_t        mkv;
        rubraview_mkv_walk_t   walk;
        rubraview_mp4_t        mp4;            /* an MP4 film's tracks (D-129) */
        char                   film[1024];
        struct { char *data; size_t len, cap; unsigned cues; } out[RUBRAVIEW_MKV_MAX_TRACKS];
        double                 said_at;
        int                    percent;
    } takeout;
    /* RV-115 stage 4 (D-128): the subtitle on screen put into the film.
       Asked first; written to a temporary file beside the film, checked
       against it, and only then the film renamed (kept, in its folder)
       and the new file given its name (the owner, 2026-10-14). */
    struct {
        int                    phase;          /* 0 idle, 1 writing, 2 checking */
        double                 armed_at;       /* the first press; the second within six seconds goes on */
        rubraview_fs_mapping_t map, made_map;
        rubraview_mkv_t        mkv;
        rubraview_mkv_put_t    put;
        rubraview_mkv_check_t  check;
        bool                   is_mp4;         /* D-130: an MP4 film, by `mp4.c` */
        rubraview_mp4_t        mp4;
        rubraview_mp4_put_t    mp4_put;
        rubraview_mp4_check_t  mp4_check;
        rubraview_fs_writer_t  writer;
        char                   film[1024], temp[1100];
        int                    percent;
        double                 said_at;
    } putin;
    int32_t                    subs_edit_row;      /* the row whose text is being typed; -1 */
    bool                       subs_edit_time;     /* RV-115: its times are typed, not its words */
    double                     subs_utf8_armed;    /* RV-115: UTF-8 offered for a save its code page cannot hold */
    rubraview_textedit_t       subs_edit;
    char                       subs_edit_buffer[1024];
    char                       subs_edit_composing[64];   /* the IME's unfinished part */
    size_t                     subs_edit_composing_len;
    int32_t                    subs_click_row;     /* for a double click */
    double                     subs_click_at;
    rubraview_window_t        *files_window;
    /* RV-136: the font picker, a window of its own over the settings */
    rubraview_window_t        *fontpick_window;
    rubraview_renderer_t      *fontpick_renderer;
    bool                       fontpick_open, fontpick_dirty;
    proven_u8str_view_t                    fontpick_section, fontpick_key;   /* the setting it chooses for */
    rubraview_font_family_t   *fontpick_all;
    size_t                     fontpick_all_count;
    uint32_t                  *fontpick_list;                    /* the families the words typed find */
    size_t                     fontpick_list_count, fontpick_first;
    uint8_t                   *fontpick_drawn;                   /* a family's row drawn in its own face yet */
    int32_t                    fontpick_sel, fontpick_hover, fontpick_weight;
    bool                       fontpick_italic;
    char                       fontpick_find[96];
    size_t                     fontpick_find_len;
    double                     fontpick_px, fontpick_py;
    void                      *fontpick_mem;
    /* RV-137: Edit in a window of its own (owner, 2026-10-18) */
    rubraview_window_t        *edit_window;
    rubraview_renderer_t      *edit_renderer;
    int32_t                    edit_frame[4];
    double                     edit_px, edit_py, edit_drawn_at;
    int32_t                    edit_mode;          /* its columns less one; -1 before the first layout */
    size_t                     edit_firsts[3];
    bool                       edit_dirty, edit_text_on;
    uint64_t                   edit_sig;
    rubraview_listwin_t        changes;            /* the change list, rows as the playlist's */
    bool                       change_checked[RUBRAVIEW_EDIT_MAX_STEPS];
    int32_t                    change_press;       /* the change whose name is held, or -1 */
    double                     change_press_y;
    bool                       change_dragging;    /* moved far enough: let go, it goes there */
    bool                       sel_mode;           /* RV-137: a drag on the picture chooses an area (else it moves the picture) */
    int32_t                    effect_group;       /* the kind of effect offered (owner, 2026-10-18) */
    bool                       export_from_edit;   /* Export opened from Edit: the edits are in what it writes, and Edit comes back */
    bool                       edit_kept;          /* Edit opened again with the edits it had */
    char                       edit_after[1024];   /* a film's frame saved to be edited: Edit opens when it is on screen */
    rubraview_renderer_t      *files_renderer;
    bool                       files_open, files_dirty;
    int32_t                    files_frame[4];
    rubraview_listwin_t        files_list;         /* the rows, in that window's own pixels */
    rubraview_rect_t           files_preview_rect;
    const rubraview_page_ref_t *files_pages;       /* the list it was last drawn for: another one resets it */
    size_t                     files_count;
    int32_t                    files_selected;     /* the row the keys moved to; -1: none */
    int32_t                    files_hover;        /* the row under the pointer; -1 */
    int32_t                    files_followed;     /* the file on screen it last scrolled to */
    int32_t                    files_want;         /* the file whose preview is wanted */
    double                     files_want_since;
    bool                       files_want_now;     /* chosen by a key: no waiting for the pointer to rest */
    rubraview_texture_t       *files_preview;      /* belongs to files_renderer */
    int32_t                    files_preview_page, files_preview_w, files_preview_h;
    int32_t                    files_preview_state;
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
    proven_u8str_view_t                        subtitle_offset_for;
    double                         subtitle_offset_seconds;
    char                           subtitle_label[96];   /* what the OSD calls the track in use */
    /* D-33: the subtitle box, the Sub tile's taps, and the list of tracks
       a double tap or a held press opens. */
    rubraview_subbox_t             subbox;
    rubraview_tap_t                sub_tap;
    /* Owner, 2026-10-09: the seek buttons — a tap is 5 s, held they offer
       5 s to 5 min to drag onto. */
    rubraview_seekfan_t            seekfan;
    bool                           seekfan_detached;   /* pressed in the toolbox's own window */
    int32_t                        subtitle_last;        /* the track the Sub tile turns back on, -1 for none */
    bool                           sub_list_open;
    double                         sub_list_anchor_x, sub_list_anchor_y;
    bool                    resume_offer;   /* §3.17.1: the prompt is showing */
    int32_t                 resume_page;
    int32_t                 launch_page;    /* --page=N (1 the first): taken by the first thing opened, 0 after */

    rubraview_layout_opts_t layout_opts;
    rubraview_layout_result_t layout;
    size_t spread_index;
    size_t prev_spread_index;   /* the one the cross-fade is fading out (§3.2.5) */
    bool   prev_spread_valid;

    rubraview_fit_mode_t fit_mode;
    double zoom;
    double pan_x, pan_y;
    bool   view_pressed;       /* D-109: the left button went down on the picture; what it meant is known on release */
    double view_press_x, view_press_y;
    double last_click_at, last_click_x, last_click_y;   /* 1.25.0: a double click centres the picture */
    double view_press_at;
    rubraview_fit_mode_t fit_beside_real;   /* D-109: the fit that real size was entered from, to go back to */
    bool   view_dragging;      /* the left button held on a picture larger than the window: it follows the pointer */
    double view_drag_x, view_drag_y;
    double strip_offset;       /* the strip (webtoon layout): how far into the top page the window's top edge is */
    double strip_base_w;       /* the picture width every page of the strip is drawn at, before the window and zoom */
    size_t strip_base_count;   /* the page count it was taken for */
    bool pixel_grid;
    bool force_nearest;
    bool needs_relayout;
    bool fit_lock;       /* §3.4: keep the fit mode and zoom across page changes */
    bool spread_detect;  /* §3.3.4 / Shift+B: AR >= threshold treated as a pre-merged spread */
    /* Settings read since 2026-09-30 (owner: every setting on the window does something). */
    bool frameless;                        /* General › Frameless window */
    rubraview_codepage_t archive_codepage; /* Files › Archive filenames, for the next archive */
    /* The names of the archive open now, read in a code page chosen for it
       alone (owner, 2026-10-01: Shift-JIS, GBK, Big5 ... once, not the
       global setting); AUTO-and-unset is -1. Reset by the next archive. */
    int32_t                names_codepage;
    size_t cache_budget;                   /* Cache › Memory cap, for the next source opened */

    /* M3 additions */
    rubraview_orientation_t orientation;   /* RV-041, per view rather than per file */
    int    frame_wanted;                   /* RV-108: FRAME_COPY or FRAME_SAVE, until the next frame is shown */
    bool   swipe_going;                    /* RV-107: a pan gesture under way, and how far it has gone */
    double swipe_dx, swipe_dy, swipe_pan_x, swipe_pan_y;
    double added_shown_at;                 /* D-100: when a file added from Explorer was last turned to */
    int32_t layout_exempt;                 /* D-98: the film or animation the reader asked for by name: shown in any layout; -1 none */
    rubraview_aspect_t aspect;             /* D-94: the shape pictures are shown in, for the session */
    bool aspect_side;                      /* that shape on its side */
    rubraview_osd_t osd;                   /* RV-039 */
    int32_t         osd_size_page;         /* the page whose size the bar holds, -1 none (owner, 2026-10-16) */
    uint64_t        osd_size_bytes;
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
    /* The wheel's strip (owner, 2026-10-15): the filmstrip shown for a while after
       the wheel turns a page, sliding to keep the page in the middle; a click on a
       thumbnail goes to it. Its thumbnails come from a thread of their own. */
    double                 strip_peek_until;   /* shown until then, then fades */
    double                 strip_target;       /* the offset it slides to */
    struct picker_thumb   *strip_cells;        /* one a page: state and texture */
    size_t                 strip_cell_count;
    rubraview_thumbs_t    *strip_thumbs;
    uint32_t               strip_generation;   /* one per book or folder opened */
    bool                   strip_arrived;      /* a thumbnail came in this pass: draw */
    rubraview_slideshow_t slideshow;       /* RV-015, state machine from RV-032 */
    rubraview_slideshow_item_t *slides;
    bool slideshow_running;
    rubraview_transition_t transition;
    rubraview_cursor_hide_t cursor;
    double last_frame_seconds;
    double pointer_x, pointer_y;
    bool   pointer_inside;   /* false after the pointer left the window (the edge buttons go with it) */

    /* §3.13 / §3.10 / §3.11: the workbench and the two dialogs. All
       three are the same panel model with different rows. */
    rubraview_panel_t         panel;
    bool                      panel_is_export;   /* which of the three the panel currently is */
    bool                      panel_is_batch;
    /* RV-129: the batch window's choices, kept between openings (and in settings.ini as batch.last) */
    struct batch_ui {
        bool    loaded;
        int32_t size_by;              /* 0 as it is, 1 %, 2 long side, 3 short side, 4 width, 5 height, 6 box, 7 megapixels */
        double  size_value, box_h;
        int32_t filter;
        bool    linear, no_enlarge;
        double  sharpen;
        int32_t rotate, flip;         /* rotate: 0 none, 1 90, 2 180, 3 270, 4 by EXIF; flip: 0 none, 1 h, 2 v */
        char    crop[48];
        bool    autocrop, auto_colour, grayscale, negative;
        int32_t effect;
        double  effect_amount;
        char    canvas[24];
        double  border;
        int32_t frame_colour;         /* 0 white, 1 black, 2 grey */
        char    mark[1024];
        int32_t mark_corner;
        double  mark_opacity;
        int32_t format;
        double  quality;
        bool    privacy;
        rubraview_export_options_t more;   /* the format's own choices and the details kept (RV-140, 1.17.0), as Export has them */
        char    name[256];
        double  start;
        char    replace[256];
        int32_t exists;               /* 0 replace, 1 skip, 2 number */
        bool    keep_dates, recursive, subfolders;
        char    out[1024];
        /* RV-134 */
        char    text[256];
        int32_t text_corner, text_colour;   /* text_colour: 0 white, 1 black, 2 yellow */
        double  text_size, dpi;
    } batch;
    int32_t                   batch_typing;   /* RV-129: the panel row whose text the rename box holds; 0 none */
    bool                      panel_is_print;    /* D-87: a fourth, the choices made when printing */
    /* D-88: editing inside an archive */
    arc_job_t                *arc_job;           /* the archive is being written again, or NULL */
    arc_ask_t                 arc_ask;           /* a question about it is on screen */
    rubraview_arcedit_op_t    arc_ask_op;
    size_t                    arc_ask_entry;
    char                      arc_ask_name[512];
    uint8_t                  *arc_ask_data;
    size_t                    arc_ask_size;
    char                      arc_choice_path[1024];   /* the archive the format was chosen for, this session */
    rubraview_arcedit_target_t arc_choice;
    bool                      rename_is_archive; /* the rename box holds an archive page's name */
    bool                      panel_is_convert;  /* D-89: a fifth panel, what an archive becomes */
    char                      arc_ask_path[1024];      /* the book the question is about */
    char                      arc_undo_path[1024];     /* D-89: the book last edited in place ... */
    char                      arc_undo_backup[1024];   /* ... and the file it was before, kept beside it */
    char                      print_temp[1024];        /* D-89: an archive page laid out as a file to print */
    bool                      print_going;
    bool                      print_with_dialog;   /* 1.19.0: Windows' dialog asked for */
    proven_u8str_view_t                  *print_list;        /* what the print panel's button prints */
    size_t                    print_list_count;
    /* 1.19.0: the printer, paper and way up chosen in the panel, and its preview */
    char                      print_printer_names[32][256];
    size_t                    print_printer_count;
    int32_t                   print_printer;            /* index; -1 none */
    rubraview_printer_paper_t print_papers[160];
    size_t                    print_paper_count;
    int32_t                   print_paper;              /* index; -1 the printer's own */
    rubraview_print_sheet_t   print_sheet;
    bool                      print_sheet_ok;
    int32_t                   print_preview_sheet;      /* which sheet the preview shows, 0 first */
    rubraview_texture_t      *print_thumbs[9];
    int32_t                   print_thumb_w[9], print_thumb_h[9];
    size_t                    print_thumb_count;
    /* §3.13: a crop being dragged on the picture — where it started, in image pixels. */
    bool                      crop_dragging;
    /* §3.13: the curve widget — which point is being dragged, or -1. */
    int32_t                   curve_dragging;
    int32_t                   crop_drag_x0, crop_drag_y0;
    rubraview_edit_session_t  edit;            /* the step being made: its values, and the selection (crop rect) */
    /* The owner's edit (2026-10-17): steps on a selection, undone and redone until saved. */
    rubraview_edit_steps_t    edit_steps;
    rubraview_area_shape_t    sel_shape;       /* the selection: a rectangle or an ellipse in the crop's box */
    int32_t                   sel_feather;
    rubraview_panel_t         sel_panel;       /* the selection's numbers, a box of its own */
    rubraview_panel_t         steps_panel;     /* the undo list */
    apply_all_job_t          *apply_job;       /* the owner (2026-10-18): the steps onto the whole list */
    bool                      rename_is_apply; /* the rename box holds the copies' name: *_edit */
    LONG                      apply_said;      /* how many were said done */
    /* The owner (2026-10-18): a change is a step at once. While one kind of change
       goes on over one area, the last step is that change, set again as it moves. */
    bool                      edit_live;
    int32_t                   edit_live_kind;  /* RUBRAVIEW_STEP_ADJUST or _EFFECT */
    rubraview_edit_area_t     edit_live_area;
    bool                      edit_live_busy;  /* a live step is being set: the selection's own sync does not end it */
    /* RV-133: classifying, the files set aside for the nine folders (made on first use) */
    rubraview_classify_t     *classify;
    rubraview_panel_t         classify_panel;
    int32_t                   classify_shown_page;
    int32_t                   sel_grab;        /* 0 none; 2 moving; 3..10 a handle */
    int32_t                   sel_grab_x, sel_grab_y;
    rubraview_crop_rect_t     sel_grab_rect;
    uint32_t                  edit_rev, edit_shown_rev;
    int32_t                   typed_panel, typed_id;   /* a value typed: 1 the edit panel, 2 the numbers; its row */
    int32_t                   effect_choice;           /* the effect chosen (effects.h), shown on the preview until applied */
    int32_t                   plugin_choice;           /* RFC-0007: 1.. a usable plug-in in the Edit window's list; 0 none */
    double                    plugin_values[8];        /* its parameters as set there */
    /* RFC-0009 C: G'MIC in the Edit window */
    int32_t                   gmic_category;           /* 1.. a category of the filter list; 0 none */
    int32_t                   gmic_filter;             /* the filter within it, from 0 */
    double                    gmic_values[RUBRAVIEW_GMIC_MAX_SHOWN];
    char                      gmic_texts[RUBRAVIEW_GMIC_MAX_SHOWN][256];
    int32_t                   gmic_typing;             /* the box takes a parameter's text (its index), or -1 a whole command */
    bool                      rename_is_gmic;
    char                      gmic_last_typed[RUBRAVIEW_EDIT_COMMAND_MAX];
    /* RFC-0009 D: FFmpeg's filters in the Edit window */
    int32_t                   ffmpeg_group;            /* 1.. a group of the list; 0 none */
    int32_t                   ffmpeg_filter;           /* the filter within it, from 0 */
    double                    ffmpeg_values[RUBRAVIEW_FFILTER_MAX_SHOWN];
    char                      ffmpeg_texts[RUBRAVIEW_FFILTER_MAX_SHOWN][256];
    int32_t                   ffmpeg_typing;           /* the box takes an option's text (its index), or -1 a whole graph */
    bool                      rename_is_ffmpeg;
    char                      ffmpeg_last_typed[RUBRAVIEW_EDIT_COMMAND_MAX];
    double                    effect_amount;
    bool                      edit_compare;            /* the original shown instead, to compare */
    rubraview_export_options_t export_options;
    rubraview_batch_job_t      batch_job;
    rubraview_batch_action_t   batch_actions[8];

    /* §3.18: triage. The undo stack is what makes Delete safe to press
       quickly, which is the point of the whole section. */
    rubraview_undo_stack_t undo;
    char                 (*picker_hist)[1024];   /* 1.24.0: the way back, then the way forward (PICKER_HIST each) */
    int32_t                picker_back_n, picker_fwd_n;
    size_t                 picker_anchor;        /* 1.24.0: where a Shift+click's range starts */
    int32_t                col_drag, col_drag_k; /* 1.25.0: 1 a column's width, 2 a column's place; which column */
    double                 col_drag_x0, col_drag_w0;
    bool                   col_drag_moved;
    uint64_t               title_key;            /* 1.24.0: what the window's title was last made for */
    uint32_t               undo_batch;   /* 1.23.0: the last batch number given to a move, copy or bin of many */
    rubraview_curation_t   curation;
    bool                   rename_active;
    struct save_job       *save_job;   /* Save a copy in progress on its own thread, or NULL */
    struct page_job       *page_job;   /* a far page being read on its own thread, or NULL */
    SRWLOCK                source_lock;/* held around every read of the page source */
    bool                   rename_is_extension;   /* the box is taking an extension for the picked files */
    bool                   rename_is_picker;      /* 1.23.0: the box is naming a file or folder of the open-file screen */
    /* 1.24.0 (owner, 2026-10-19): Change extensions, a row an extension the picked files have; the box edits row ext_at */
    char                   ext_from[16][24];
    char                   ext_to[16][64];
    int32_t                ext_n, ext_at;
    /* 1.24.0: the box's own buttons (OK, Cancel, Browse...), as last drawn, in the window it was drawn in */
    rubraview_pal_rect_t   rename_btn[4];
    int32_t                rename_btn_kind[4];
    int32_t                rename_btn_n;
    rubraview_pal_rect_t   ext_row_rect[16];
    size_t                 rename_picker_at;      /* which, in its listing */
    bool                   rename_is_path;        /* the box is the picker's typed path (owner, 2026-09-28) */
    bool                   rename_is_filter;      /* 1.22.0: the picker's filter on names */
    bool                   rename_is_listfilter;  /* 1.25.0: the playlist window's filter on names */
    /* The box takes the folder to copy or move to (owner, 2026-10-16): the
       picked files, or the file on screen; a folder not there yet is
       offered once and made on the next Enter. */
    bool                   rename_is_target;
    bool                   rename_is_find;         /* RFC-0005: the text page's Find (Ctrl+F) */
    bool                   rename_is_value;        /* the owner's edit: a panel's value typed */
    struct text_page      *text;                   /* RFC-0005: the text page on screen, or NULL */
    bool                   target_moving;
    bool                   target_from_picker;
    char                   target_offered[1024];
    size_t                 target_offered_len;
    int32_t                rename_favorite;       /* the box renames this favourite, or -1 (owner, 2026-09-30) */
    /* An encrypted RAR (owner, 2026-10-01): the box takes its password,
       shown as dots, for the archive named here. The passwords that opened
       something this session are tried first on the next one; they live in
       memory only — never the settings, a log or the clipboard — and are
       wiped when the viewer closes. */
    bool                   rename_is_password;
    char                   password_for[1024];
    size_t                 password_for_len;
    char                   passwords[8][512];
    size_t                 password_lens[8];
    size_t                 password_count;
    const char            *open_password;         /* for the open under way, or NULL */
    size_t                 open_password_len;
    /* A favourite chip pressed: opened on release, or dragged to a new place. */
    int32_t                fav_press;
    double                 fav_press_x, fav_drag_x;
    bool                   fav_dragging;
    char                   rename_buffer[1024];
    /* A folder typed in the settings window (owner, 2026-09-30): the box
       over the focused path row, its setting, and an unfinished IME syllable. */
    bool                   spath_active;
    bool                   spath_number;      /* 1.24.0: the box takes a number for a slider's setting */
    double                 settings_px, settings_py;   /* 1.25.0: the pointer over the settings window */
    rubraview_pal_rect_t   spath_btn[3];      /* 1.24.0: OK, Cancel, Browse... under it, as last drawn */
    int32_t                spath_btn_n;
    rubraview_textedit_t   spath_edit;
    char                   spath_buffer[1024];
    char                   spath_composing[64];
    size_t                 spath_composing_length;
    proven_u8str_view_t                spath_section, spath_key;
    int32_t                spath_list_index;   /* a folder of a list (`recent.skip_dirs`) being typed, or -1 */
    int32_t                spath_list;         /* which of FOLDER_LISTS that list is */
    rubraview_textedit_t   rename_edit;   /* the text in rename_buffer, its caret and selection */
    /* RV-065: the adjust panel's live preview — a reduced copy of the page
       run through the same commit code as Save a copy — and its histogram. */
    void                  *edit_small_mem, *edit_work_mem;
    size_t                 edit_small_bytes, edit_work_bytes;
    rubraview_pixbuf_t     edit_small;
    int32_t                edit_page;          /* the page the copy is of, -1 for none */
    rubraview_texture_t   *edit_preview;
    int32_t                edit_preview_w, edit_preview_h;
    rubraview_edit_params_t edit_shown_params;
    rubraview_edit_curve_t edit_shown_curves[5];
    rubraview_edit_session_t edit_shown_session;   /* blur and sharpen, outside params (owner, 2026-10-15) */
    double                 edit_preview_tried;     /* when the reduced copy was last asked for and not had */
    int32_t                edit_preview_tries;     /* how often, since the panel opened: twenty at most */
    bool                   edit_shown_valid;
    uint32_t               edit_hist[4][256];  /* R, G, B, luminance of the preview */
    bool                   edit_hist_ok;
    char                   rename_composing[64];   /* the IME's unfinished syllable, drawn after the text */
    size_t                 rename_composing_length;
    bool                   confirm_purge;   /* §3.18.1's Y/N dialog is showing */
    bool                   confirm_restart; /* RFC-0006: a restart offered, Y / N */
    bool                   restart_offered; /* once a session */
    double                 memory_checked_at;

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
    rubraview_settings_t     settings_saved;   /* what settings.ini held when it was last read or written by this viewer */
    rubraview_settings_t     settings_opened;  /* D-105: as they were when the settings window opened — Revert goes back to it */
    uint32_t                 share_seen_total; /* D-105: the settings' revision when they were last seen to change */
    double                   share_changed_at;
    bool                     share_failed;     /* the file could not be written: not tried again until something changes */
    proven_u8str_view_t                  settings_path;
    proven_u8str_view_t                  config_beside;   /* the executable's own folder: portable mode's home */
    bool                     packaged;        /* RV-113 (2): run from the Store's MSIX */
    rubraview_other_copies_t other_copies;    /* D-154: the MSI's or the Store's copy beside this one */
    bool                     copies_told;
    int32_t                  settings_last_setting;   /* the def the settings' focus was last on, -1 none */
    char                     package_family[160];
    proven_u8str_view_t                  layout_path;      /* §3.6: where the floating boxes were left */
    /* §3.22.1 / D-13: a window of its own, laid out by the interpreter
       from the settings document. */
    /* The help (owner, 2026-09-23): a window of its own that the reader
       can leave open while using the viewer, holding the keys in force. */
    rubraview_window_t        *help_window;
    rubraview_renderer_t      *help_renderer;
    bool                       help_open;
    /* The file's information (owner, 2026-09-29): a window of its own,
       its text marked with the pointer and copied. */
    bool                       info_open, info_dirty;
    rubraview_window_t        *info_window;
    rubraview_renderer_t      *info_renderer;
    double                     info_font, info_cell_w, info_cell_h;
    int32_t                    info_frame[4];
    int32_t                    info_scroll;
    void                      *info_mem;
    proven_arena_t             info_arena;       /* the rows, made again when the file changes */
    proven_u8str_view_t                   *info_rows;
    bool                      *info_heading;
    size_t                     info_count;
    int64_t                    info_key;         /* which page and film the rows are of */
    rubraview_grid_selection_t info_sel;
    bool                       info_selecting;
    char                       info_message[64];
    /* the folder's summary, kept while the folder stays the same */
    char                       info_folder[1024];
    uint64_t                   info_counts[7], info_bytes[7];   /* pictures, films, songs, archives, folders, other, all */
    bool                       help_dirty;
    int32_t                    help_scroll;      /* the first line on screen */
    double                     help_font, help_cell_w, help_cell_h;
    rubraview_help_line_t      help_lines[2048];   /* the keys, then the licence notices */
    size_t                     help_count;
    int32_t                    help_key_cols;    /* the widest key list, so nothing overlaps */
    /* Its text as rows on the grid it is drawn on, for marking and copying
       (owner, 2026-09-29), as the settings and information windows do. */
    char                     (*help_row_text)[256];
    proven_u8str_view_t                    help_rows[2048];
    rubraview_grid_selection_t help_sel;
    bool                       help_selecting;
    char                       help_message[48];
    int32_t                    help_frame[4];    /* where it was last put */

    rubraview_window_t        *settings_window;
    rubraview_renderer_t      *settings_renderer;
    rubraview_settings_view_t  settings_view;
    double                     settings_font, settings_cell_w, settings_cell_h;
    int32_t                    settings_tip_row;   /* the keys table's row the pointer rests on; -1 (owner, 2026-10-14) */
    double                     settings_tip_since, settings_tip_x, settings_tip_y;
    bool                       settings_tip_shown;
    int32_t                    settings_tip_help;  /* the def whose name the pointer rests on, for its help; -1 */
    bool                       settings_dirty;       /* its picture is out of date */
    double                     settings_info_seconds; /* when `info` lines were last redrawn */
    /* Where the window was left (layout.ini); width 0 until known. Closing
       hides the window rather than destroying it, so it is made once. */
    int32_t                    settings_frame[4];     /* x, y, width, height in screen pixels */
    bool                       settings_mouse_down;
    /* The window's text as last drawn, row by row from the page's first
       column, and the block marked on it (owner, 2026-09-28: copyable). */
    char                       settings_grid_text[96][400];
    uint16_t                   settings_grid_len[96];
    rubraview_grid_selection_t settings_sel;
    bool                       settings_selecting;
    char                       settings_message[240]; /* the last action's result, under the page */

    /* In-app Metro file picker (§3.15.2), RV-043 */
    bool                   picker_open;
    bool                   chrome_hidden;           /* D-110: the title bar and the boxes are away until a click */
    /* D-124: the strip's name on several rows. What was measured last, and what it gave. */
    uint64_t               strip_title_key;
    int32_t                strip_title_lines;
    char                   strip_name[768];                /* the name cut to its three rows, "..." at its end; empty when it fits */
    bool                   strip_place_below;              /* the place or the time on a row of its own, under the name */
    bool                   speed_press_interval;           /* D-123: it is the slide show's Delay tile that is held */
    bool                   speed_pressed, speed_dragged;   /* D-110: the speed tile held; dragged, it is not a tap */
    bool                   speed_press_detached;
    double                 speed_press_y, speed_press_base;
    proven_u8str_view_t                picker_dir;
    rubraview_fs_listing_t picker_places;   /* the places bar: the usual folders and the drives */
    /* The picker's own memory (2026-09-29): every folder shown took a
       listing from the working arena for good. Two arenas take turns: a
       listing is built in the idle one, which becomes the shown one only
       when the change succeeds, so a folder that cannot be shown leaves
       the one on screen whole. */
    proven_arena_t         picker_arenas[2];
    void                  *picker_arena_mem[2];
    int                    picker_side;
    bool                   picker_in_book;  /* the picker is inside the archive being read */
    size_t                *picker_page_of;  /* then: each tile's page, NOT_A_PAGE for a folder */
    size_t                 picker_page_skip;/* tiles before the book's own (`..`) */
    rubraview_favorites_t  favorites;       /* D-43: the reader's own, first on the places bar */
    proven_u8str_view_t                favorites_path;
    rubraview_fs_listing_t picker_listing;
    size_t                 picker_hidden;   /* files in the folder the viewer cannot open, not listed */
    rubraview_confirm_t    menu_confirm;    /* a destructive menu item asks first */
    rubraview_picker_t     picker;
    bool                  *picker_selected;   /* one per listed item, remade on every folder */
    /* 1.22.0: the picker's own work (owner, 2026-10-19) */
    bool                   picker_add_mode;          /* opened from a list's Add: what is picked joins the list */
    char                   picker_filter[128];       /* the names shown hold this; empty: all */
    rubraview_fs_listing_t picker_full;              /* the folder as read, before the filter */
    bool                  *picker_full_selected;
    size_t                *picker_map;               /* a shown row's row in picker_full; NULL without a filter */
    rubraview_fs_entry_t  *picker_filtered;          /* the rows shown under a filter */
    bool                  *picker_filtered_selected;
    size_t                 picker_ctx_index;         /* the tile a right click was on */
    bool                   picker_hold;              /* a left press on a tile, not yet a tap or a long press */
    size_t                 picker_hold_index;
    double                 picker_hold_x, picker_hold_y, picker_hold_at;
    /* D-34: one picture per listed item, made on a thread of its own for
       the tiles on screen and added as each arrives. 0 = not asked for,
       1 = ready, 2 = none to show, 3 = asked for. */
    struct picker_thumb { uint8_t state; rubraview_texture_t *texture; rubraview_picker_media_t media; } *picker_thumbs;
    size_t                 picker_thumb_count;
    rubraview_thumbs_t    *thumbs;              /* the thread, started with the first picker */
    uint32_t               picker_generation;   /* one per listing: late results are known by it */
    bool                   picker_thumbs_arrived;   /* a picture came in this pass: draw */
    /* Move and Copy wait for the digit that names a curation folder;
       Recycle waits to be pressed a second time (§3.18.1's rule). */
    int32_t                picker_pending;     /* a picker_button_t, waiting for its digit */

    /* D-40: sharper tiles over a page shown reduced (D-39), decoded on a
       thread from the file and drawn over the reduced texture. */
    /* Two sets, one for each page of a pair (owner, 2026-10-09: enlarged
       pages get tiles too, and both pages of a spread must match). */
    struct tile_set {
        rubraview_tiles_t     *tiles;          /* the thread, started with the first page that needs it */
        uint32_t               generation;     /* one per page: late tiles are known by it */
        int32_t                page;           /* the page the tiles are for, -1 none */
        rubraview_texture_t   *page_texture;   /* ...and its texture then: reloaded is a new page */
        struct tile_slot {
            rubraview_tile_key_t key;
            rubraview_texture_t *texture;
            uint64_t used;
            bool live, failed;
        } slots[96];
        uint64_t               clock;
        rubraview_tile_key_t   asked[128];     /* the last request, so an unchanged view asks nothing */
        size_t                 asked_count;
    } tile_sets[2];
    bool                   tiles_arrived;      /* a tile came in this pass: draw */
    bool                   picker_has_pending;
    rubraview_confirm_t    picker_confirm;
} app_state_t;


/* Moved from main.c's body and the media run: both files use them. */
enum { FRAME_NOT = 0, FRAME_COPY, FRAME_SAVE, FRAME_EDIT };   /* FRAME_EDIT: saved beside the film, then edited (RV-137) */
/* RV-076: the head of a music file is where its tags and its cover live,
   so a 300 MB record is not read to find out who is singing. */
#define MUSIC_MAX_HEAD_BYTES (4u * 1024u * 1024u)
#define SUBTITLE_MAX_BYTES (2u * 1024u * 1024u)

/* Defined in media.c. */
extern uint32_t g_subs_generation;


/* Moved from main.c: more than one file uses them (RV-163). */
#define SUB_LIST_MAX 16
#define SUB_LIST_ROW 40.0
#define AB_LABEL_W 28.0
#define AB_UNIT_W 14.0
#define FILES_BACKGROUND 0xFF181818u


/* Moved from main.c: more than one file uses them (RV-163). */
enum {
    PANEL_EXPOSURE = 1, PANEL_BRIGHTNESS, PANEL_CONTRAST, PANEL_SATURATION,
    PANEL_TEMPERATURE, PANEL_TINT, PANEL_BLACK, PANEL_WHITE, PANEL_GAMMA,
    PANEL_BLUR, PANEL_SHARPEN, PANEL_CROP_RATIO, PANEL_RESET, PANEL_APPLY, PANEL_SAVE_COPY, PANEL_SAVE_OVER,
    /* RV-108: the adjust panel's second page, and two more of Export's rows. */
    PANEL_EDIT_PAGE, PANEL_BLUR_KIND, PANEL_SHARPEN_KIND, PANEL_SHARPEN_RADIUS, PANEL_SHARPEN_THRESHOLD,
    PANEL_RESIZE, PANEL_RESIZE_PX, PANEL_CROP_PX, PANEL_TRIM, PANEL_PROGRESSIVE, PANEL_PNG_DEPTH, PANEL_WEBP_LOSSLESS, PANEL_WEBP_EFFORT,
    PANEL_FORMAT, PANEL_QUALITY, PANEL_PNG_LEVEL, PANEL_PRIVACY, PANEL_EXPORT_NOW,
    /* RV-140: Export's rows for what the writers take */
    PANEL_JPEG_SUBSAMPLING, PANEL_JPEG_GREY, PANEL_JPEG_OPTIMISE, PANEL_JPEG_RESTART, PANEL_PNG_FILTER, PANEL_PNG_INTERLACE,
    PANEL_COLOURS, PANEL_DITHER, PANEL_TIFF_COMPRESSION, PANEL_BMP_DEPTH, PANEL_WEBP_NEAR, PANEL_WEBP_ALPHA, PANEL_WEBP_SHARP,
    PANEL_KEEP_DETAILS, PANEL_WEBP_PRESET, PANEL_TARGET_KB, PANEL_TIFF_CMYK,   /* 1.17.0 */
    PANEL_AVIF_CHROMA, PANEL_AVIF_BITS,   /* 1.31.0 */
    PANEL_BATCH_RESIZE, PANEL_BATCH_FILTER, PANEL_BATCH_FORMAT, PANEL_BATCH_GRAY, PANEL_BATCH_RUN,
    /* RV-129 */
    PANEL_BATCH_SIZE_BY, PANEL_BATCH_BOX_H, PANEL_BATCH_LINEAR, PANEL_BATCH_NO_ENLARGE, PANEL_BATCH_SHARPEN,
    PANEL_BATCH_ROTATE, PANEL_BATCH_FLIP, PANEL_BATCH_CROP, PANEL_BATCH_AUTOCROP, PANEL_BATCH_AUTOCOLOUR, PANEL_BATCH_NEGATIVE,
    PANEL_BATCH_EFFECT, PANEL_BATCH_EFFECT_AMOUNT, PANEL_BATCH_CANVAS, PANEL_BATCH_BORDER, PANEL_BATCH_FRAME_COLOUR,
    PANEL_BATCH_MARK, PANEL_BATCH_MARK_CORNER, PANEL_BATCH_MARK_OPACITY, PANEL_BATCH_QUALITY,
    PANEL_BATCH_NAME, PANEL_BATCH_START, PANEL_BATCH_REPLACE, PANEL_BATCH_EXISTS, PANEL_BATCH_KEEP_DATES,
    PANEL_BATCH_RECURSIVE, PANEL_BATCH_SUBFOLDERS, PANEL_BATCH_OUT, PANEL_BATCH_TEST, PANEL_BATCH_SAVE, PANEL_BATCH_LOAD,
    PANEL_BATCH_TEXT, PANEL_BATCH_TEXT_CORNER, PANEL_BATCH_TEXT_SIZE, PANEL_BATCH_TEXT_COLOUR, PANEL_BATCH_DPI,   /* RV-134 */
    PANEL_PRINT_TURN, PANEL_PRINT_SIZE, PANEL_PRINT_SCALE, PANEL_PRINT_PLACE, PANEL_PRINT_MARGIN,
    PANEL_PRINT_PER_SHEET, PANEL_PRINT_NOW,
    PANEL_PRINT_PRINTER, PANEL_PRINT_PAPER, PANEL_PRINT_ORIENT, PANEL_PRINT_COPIES, PANEL_PRINT_SHEET, PANEL_PRINT_DIALOG,   /* 1.19.0 */
    PANEL_CONV_FORMAT, PANEL_CONV_METHOD, PANEL_CONV_LEVEL, PANEL_CONV_NOW,
    /* The owner's edit (2026-10-17) */
    PANEL_APPLY_SEL, PANEL_CROP_SEL, PANEL_RESIZE_APPLY, PANEL_UNDO, PANEL_REDO, PANEL_STEPS, PANEL_SEL_BOX,
    PANEL_SAVE_ORIGINAL, PANEL_SEL_SHAPE, PANEL_SEL_X, PANEL_SEL_Y, PANEL_SEL_W, PANEL_SEL_H, PANEL_SEL_FEATHER,
    PANEL_SEL_ALL, PANEL_SEL_TYPE,
    PANEL_BAL_R, PANEL_BAL_G, PANEL_BAL_B, PANEL_AUTO_COLOUR, PANEL_EFFECT, PANEL_EFFECT_AMOUNT, PANEL_EFFECT_APPLY, PANEL_COMPARE,
    /* RV-128 */
    PANEL_RESIZE_PRESET, PANEL_RESIZE_LOCK, PANEL_RESIZE_FILTER, PANEL_RESIZE_LINEAR, PANEL_RESIZE_SHARPEN, PANEL_RESIZE_DPI,
    /* RV-133 */
    PANEL_CLS_HERE, PANEL_CLS_COPY, PANEL_CLS_MOVE, PANEL_CLS_CLEAR, PANEL_CLS_SETTINGS,
    /* RV-137: the Edit window's rooms and the change list's buttons */
    PANEL_SP_CURVE, PANEL_SP_SEL, PANEL_SP_CHANGES, PANEL_CHANGES_REMOVE, PANEL_ALL_COPIES, PANEL_ALL_ORIGINALS,
    PANEL_SEL_MODE, PANEL_SEL_COPY, PANEL_EFFECT_GROUP,
    PANEL_PLUGIN, PANEL_PLUGIN_P0, PANEL_PLUGIN_P1, PANEL_PLUGIN_P2, PANEL_PLUGIN_P3,   /* RFC-0007 */
    PANEL_PLUGIN_P4, PANEL_PLUGIN_P5, PANEL_PLUGIN_P6, PANEL_PLUGIN_P7, PANEL_PLUGIN_APPLY,
    PANEL_GMIC_CATEGORY, PANEL_GMIC_FILTER, PANEL_GMIC_TYPE, PANEL_GMIC_APPLY,   /* RFC-0009 C */
    PANEL_FFMPEG_GROUP, PANEL_FFMPEG_FILTER, PANEL_FFMPEG_TYPE, PANEL_FFMPEG_APPLY,   /* RFC-0009 D */
    PANEL_GMIC_P0 = 800,       /* + the parameter's row, below PANEL_FFMPEG_P0 */
    PANEL_FFMPEG_P0 = 850,     /* + the option's row, below PANEL_CLS_FOLDER */
    PANEL_CLS_FOLDER = 900,    /* + 1 .. 9: a curation folder's row */
    PANEL_STEP_FIRST = 1000,   /* the undo list's rows: PANEL_STEP_FIRST + how many steps are in force */
};
#define PANEL_GMIC_ROWS 24   /* parameters shown; the rest run at their defaults */
typedef struct gmic_list_job {
    HANDLE thread;
    volatile LONG done;
    rubraview_window_t *wake;
    char exe[520], out[600];
    bool ok;
    char said[512];
} gmic_list_job_t;
#define PANEL_FFMPEG_ROWS 24   /* options shown; the rest keep FFmpeg's defaults */
typedef struct batch_tokens {
    const char *v[64];
    size_t      n;
    char        store[8192];
    size_t      used;
} batch_tokens_t;
#define SETTINGS_ACCENT      0xFFE0B46Au   /* headings: warm, so that blue means "press it" (owner, 2026-10-17) */

/* Shared variables, defined in the file that owns them. */
extern rubraview_ffilter_catalog_t g_ffm;
extern rubraview_gmic_catalog_t g_gmic;
extern char g_gmic_exe[520];
extern gmic_list_job_t *g_gmic_job;
extern const char *g_gmic_list_why;
extern char g_gmic_where[520];
extern size_t g_plugin_count;
extern char g_plugin_folder[520];
extern _Thread_local const char *g_plugin_why;
extern rubraview_plugin_t g_plugins[RUBRAVIEW_PLUGIN_MAX];


/* Moved from main.c: more than one file uses them (RV-163). */
enum { PF_NONE = 0, PF_TITLE, PF_MENU, PF_NAME, PF_LIST, PF_BUTTONS };
#define PICKER_CRUMB_HEIGHT 44.0
#define PICKER_PLACES_HEIGHT 40.0   /* the places bar under the path (owner, 2026-09-28) */
#define PICKER_HEADER_HEIGHT (PICKER_CRUMB_HEIGHT + PICKER_PLACES_HEIGHT)
#define PICKER_TOOL_HEIGHT 42.0     /* 1.24.0: all, Individual, Range, Same type, Media, Sort, Order, the view */
#define PICKER_NAV_W 40.0           /* 1.24.0: back, up and forward at the path bar's left, a finger wide */
#define PICKER_DETAIL_ROW 30.0
#define PICKER_DETAIL_HEAD 26.0   /* the line naming the columns */
#define PICKER_HEAD_CHECK_ROOM 30.0   /* 1.23.0: the heads' check box, before Name */
#define PICKER_TILE 160.0
#define PICKER_TILE_WIDE 230.0
typedef struct picker_bar_item { proven_u8str_view_t label, path; bool favorite; } picker_bar_item_t;
#define PICKER_ACTION_HEIGHT 76.0   /* two rows: the buttons, then what is picked */
#define PICKER_BUTTON_ROW 48.0
typedef enum picker_button {
    PICKER_BTN_OPEN = 0,     /* 1.23.0: what is picked, or the one with the focus */
    PICKER_BTN_INDIVIDUAL,
    PICKER_BTN_RANGE,
    PICKER_BTN_SAME_TYPE,
    PICKER_BTN_MEDIA,        /* 1.22.0: the pictures, films, music and text here, picked at once or let go */
    PICKER_BTN_EXTENSION,
    PICKER_BTN_PLAYLIST,
    PICKER_BTN_RECYCLE,
    PICKER_BTN_MOVE,
    PICKER_BTN_COPY,
    PICKER_BTN_CLEAR,
    PICKER_BTN_VIEW,         /* D-110: tiles or details */
    PICKER_BTN_CANCEL,       /* 1.25.0: back to what was on screen before */
    PICKER_BTN_COUNT,
} picker_button_t;

static const char *const PICKER_BUTTON_LABEL[PICKER_BTN_COUNT] = {
    N_("Open"), N_("Individual"), N_("Range"), N_("Same type"), N_("Media"), N_("Change ext"),
    N_("Playlist"), N_("Recycle"), N_("Move to"), N_("Copy to"), N_("Deselect all"), N_("Details"), N_("Cancel"),
};
static const picker_button_t PICKER_BOTTOM[] = { PICKER_BTN_PLAYLIST, PICKER_BTN_RECYCLE, PICKER_BTN_MOVE, PICKER_BTN_COPY,
                                                 PICKER_BTN_CANCEL, PICKER_BTN_OPEN };
#define PICKER_BOTTOM_COUNT ((int32_t)(sizeof(PICKER_BOTTOM) / sizeof(PICKER_BOTTOM[0])))
#define PICKER_CRUMB_START (8.0 + 3.0 * (PICKER_NAV_W + 4.0) + 6.0)
enum { PICKER_TOOL_ALL = 0, PICKER_TOOL_INDIVIDUAL, PICKER_TOOL_RANGE, PICKER_TOOL_SAME, PICKER_TOOL_MEDIA,
       PICKER_TOOL_SORT, PICKER_TOOL_ORDER, PICKER_TOOL_VIEW, PICKER_TOOL_COUNT };
enum {
    FILES_PREVIEW_NONE = 0,   /* nothing asked for yet */
    FILES_PREVIEW_SHOWN,
    FILES_PREVIEW_FAILED,     /* it could not be read or is not a picture */
    FILES_PREVIEW_FAR,        /* deep in a solid archive: read only when it is opened */
    FILES_PREVIEW_MEDIA,      /* a film or a sound */
};


/* Moved from main.c: more than one file uses them (RV-163). */
enum { SHELL_DONE = 0, SHELL_FAILED = 1, SHELL_DONE_NO_PARTS = 3, SHELL_DONE_REFUSED = 4, SHELL_DONE_STORE = 5 };
typedef enum { SHELL_PART_LEAVE = 0, SHELL_PART_APPLY, SHELL_PART_REMOVE } shell_part_t;

/* Shared variables, defined in the file that owns them. */
extern const char *const LANG_PACKS[];


/* Moved from main.c: more than one file uses them (RV-163). */
enum { DETAILS_NONE = 0, DETAILS_IN_FILE, DETAILS_TXT, DETAILS_FAILED };
#define TEXT_KEEP_BLOCKS 400                   /* laid-out paragraphs kept round the view */
typedef struct text_page {
    int32_t page;                      /* the page it is */
    proven_u8str_view_t key;                       /* its path, for the place kept (malloc'd) */
    char *source;                      /* the file's text, cleaned (§2.3) */
    size_t source_len;
    rubraview_textenc_t enc;
    bool enc_by_hand;
    rubraview_text_view_t view;
    int built_breaks;                  /* what the view was built with */
    bool built_trim;
    bool built_headings;   /* RV-123 */
    rubraview_text_block_t **blocks;   /* per paragraph, NULL until laid out */
    /* 1.25.0 (owner, 2026-10-20: "한줄로 ... 전체가 다 개행이 안되게"): a piece joined to the one before begins with that
       one's last line, so the run flows on; how many bytes it carries (TEXT_CARRY_UNKNOWN until worked out) */
    size_t *carry;
    double *heights;                   /* per paragraph: measured, or guessed until it is */
    double *tops;                      /* where each begins; `total` is where the last ends */
    bool tops_dirty;
    double total;
    size_t laid_out;                   /* blocks alive */
    /* what the blocks were laid out for */
    double lay_width, lay_size, lay_spacing;
    bool lay_wrap;
    uint64_t lay_family;
    /* where the view is: a paragraph and how far into it, so heights
       measured above or below do not move what is being read */
    size_t anchor;
    double anchor_dy;
    double view_h;
    double x_scroll;                   /* no wrap: how far to the right */
    /* auto-scroll (§5): smooth, by pixels; its speed in lines a minute */
    bool autoscroll;
    double press_x, press_y;   /* where the last press on the text was: a click in the middle is auto-scroll */
    double autoscroll_last;
    double autoscroll_carry;
    /* the selection, in the view's text: from `sel_a` to `sel_b` */
    bool selecting;
    size_t sel_a, sel_b;
    /* find */
    char find[256];
    size_t find_len;
    size_t match_at, match_len;
    bool match;
    /* drawing, for the pointer */
    double col_x, col_w, top_pad;
} text_page_t;
#define TEXT_CARRY_UNKNOWN ((size_t)-1)

/* Shared variables, defined in the file that owns them. */
extern size_t         g_launch_count;
extern int32_t        g_launch_page;
extern const proven_u8str_view_t *g_launch_paths;
extern rubraview_shell_verb_t g_launch_verb;
extern bool           g_launch_whole;



/* Moved from main.c: more than one file uses them (RV-163). */
#define TITLEBAR_ON 0xFF2D4A6Eu   /* 1.25.0: the ground of a title bar or widget button that is on */
typedef struct picker_columns {
    double name_x;
    double x[RUBRAVIEW_PICKER_SORT_COUNT];      /* where each begins; Name's end is x[TYPE] */
    double w[RUBRAVIEW_PICKER_SORT_COUNT];      /* how wide, 0 when let go */
} picker_columns_t;
#define SUBBOX_BUTTON 30.0
#define SUBBOX_GAP 4.0

/* Shared variables, defined in the file that owns them. */
extern const app_state_t *g_caption_app;
extern double g_col_w[RUBRAVIEW_PICKER_SORT_COUNT];


/* Moved from main.c: more than one file uses them (RV-163). */
enum { RENAME_BTN_OK = 1, RENAME_BTN_CANCEL, RENAME_BTN_BROWSE, RENAME_BTN_BACK };


/* Shared variables, defined in the file that owns them. */
extern bool g_conv_is_7z;
extern bool g_edit_page_two;


/* Moved from main.c: more than one file uses them (RV-163). */
#define TILE_SETS 2

/* ---- functions called across files ---- */
rubraview_pal_rect_t ab_box_button(const app_state_t *app, int32_t which);
rubraview_pal_rect_t ab_box_rect(const app_state_t *app);
void ab_edit_begin(app_state_t *app);
void ab_edit_close(app_state_t *app);
void ab_edit_goto(app_state_t *app, int32_t side, int32_t part);
bool ab_edit_handle_key(app_state_t *app, rubraview_key_combo_t combo);
void ab_edit_take(app_state_t *app, int32_t f);
void ab_edit_text_typed(app_state_t *app, proven_u8str_view_t text);
proven_u8str_view_t ab_part(const app_state_t *app, int32_t side, int32_t part);
rubraview_pal_rect_t ab_part_rect(const app_state_t *app, int32_t side, int32_t part);
rubraview_action_facts_t action_facts(const app_state_t *app);
void address_begin(app_state_t *app);
void address_typed(app_state_t *app);
proven_u8str_view_t adjust_row_text(const app_state_t *app, const rubraview_panel_row_t *row, char *buffer, size_t size);
int anim_end_mode(const app_state_t *app);
bool anim_plays(const app_state_t *app);
void animation_prepare(app_state_t *app);
void animation_tick(app_state_t *app, double dt);
rubraview_pal_rect_t answer_box_close(const app_state_t *app, rubraview_pal_rect_t box);
proven_u8str_view_t app_keep(app_state_t *app, proven_u8str_view_t text);
void append_arg(char *buf, size_t cap, size_t *pos, proven_u8str_view_t text);
void append_arg_cstr(char *buf, size_t cap, size_t *pos, const char *text);
void append_number(char *buf, size_t cap, size_t *pos, size_t value);
void append_text(char *buf, size_t cap, size_t *pos, const char *text);
void apply_all_begin(app_state_t *app);
void apply_all_originals(app_state_t *app);
void apply_all_poll(app_state_t *app, bool quitting);
void apply_all_start(app_state_t *app, apply_all_job_t *job);
void apply_all_typed(app_state_t *app);
void apply_intent(app_state_t *app, rubraview_pointer_intent_t intent);
void arc_ask_clear(app_state_t *app);
bool arc_ask_key(app_state_t *app, rubraview_key_combo_t combo);
rubraview_arcedit_target_t arc_convert_target(const app_state_t *app);
void arc_edit_on_page(app_state_t *app, rubraview_arcedit_op_t op, const char *name);
void arc_edit_request(app_state_t *app, rubraview_arcedit_op_t op, size_t entry, const char *name, uint8_t *data, size_t size);
void arc_edit_start(app_state_t *app, rubraview_arcedit_target_t target);
void arc_job_poll(app_state_t *app, bool quitting);
bool arc_temp_path(char *dst, size_t cap, const char *tag, proven_u8str_view_t leaf);
bool arc_undo(app_state_t *app);
void arc_undo_drop(app_state_t *app);
void archive_names_reread(app_state_t *app, rubraview_codepage_t codepage);
void arena_refill(app_state_t *app);
void aspect_refresh(app_state_t *app);
bool at_real_size(const app_state_t *app);
void audio_chain_update(app_state_t *app);
void audio_track_set(app_state_t *app, int32_t next);
void batch_save_or_load(app_state_t *app, bool saving);
proven_u8str_view_t batch_text(app_state_t *app);
void batch_tokens(const struct batch_ui *b, batch_tokens_t *tk);
void batch_try_names(app_state_t *app);
void batch_type_begin(app_state_t *app, int32_t id);
void batch_typed(app_state_t *app);
void bgm_close(app_state_t *app);
void bgm_do(app_state_t *app, rubraview_bgm_action_t action);
bool bgm_pause_for_sound(const app_state_t *app);
void bgm_tick(app_state_t *app);
void box_drag_motion(app_state_t *app, double x, double y);
double box_opacity(const app_state_t *app, const rubraview_box_t *box);
bool box_press(app_state_t *app, double x, double y);
void box_release(app_state_t *app, double x, double y);
bool box_wheel_opacity(app_state_t *app, double x, double y, double notches, uint32_t modifiers);
void boxes_apply_size(app_state_t *app);
void boxes_place(app_state_t *app);
void build_slides(app_state_t *app);
size_t bytes_scaled(size_t n, size_t k, size_t extra);
void changes_remove_checked(app_state_t *app);
void chooser_close(app_state_t *app);
bool chooser_key(app_state_t *app, int where, rubraview_key_combo_t combo);
double chooser_main_x(app_state_t *app);
double chooser_main_y(app_state_t *app);
void chooser_open_listext(app_state_t *app, int where, double x, double y, double above_y);
void chooser_open_listsort(app_state_t *app, int where, double x, double y, double above_y);
void chooser_open_panel(app_state_t *app, int32_t which, int32_t row, double x, double y, double above_y);
void chooser_open_setting(app_state_t *app);
void chooser_open_sort(app_state_t *app, bool order, double x, double y, double above_y);
bool chooser_open_tile(app_state_t *app, proven_u8str_view_t action, double x, double y, double above_y);
bool chooser_press(app_state_t *app, int where, double x, double y);
bool chooser_wheel(app_state_t *app, int where, double notches);
bool chooser_wheel_tile(app_state_t *app, proven_u8str_view_t action, double notches);
void chrome_hide(app_state_t *app, bool hide);
void classify_build(app_state_t *app);
void classify_mark(app_state_t *app, int32_t digit);
bool classify_press(app_state_t *app, double px, double py);
void classify_toggle(app_state_t *app);
rubraview_composition_t compose_view(app_state_t *app, const rubraview_spread_t *spread,
                                            const rubraview_page_size_t *left, const rubraview_page_size_t *right,
                                            double win_w, double win_h);
void console_line(const char *text);
proven_u8str_view_t crop_button_label(const app_state_t *app);
bool crop_point_to_image(app_state_t *app, double px, double py, int32_t *out_x, int32_t *out_y);
void crop_shown_again(app_state_t *app);
void ctx_close(app_state_t *app);
void ctx_open_at(app_state_t *app, double x, double y);
proven_u8str_view_t cue_audio_path(app_state_t *app, proven_u8str_view_t cue_path);
bool cue_step(app_state_t *app, int direction);
bool curation_has_numbers(const app_state_t *app);
proven_u8str_view_t current_file_path(app_state_t *app);
int32_t current_page_index(const app_state_t *app);
void curve_widget_drag(app_state_t *app, double px, double py);
bool curve_widget_press(app_state_t *app, double px, double py, bool remove);
rubraview_pal_rect_t curve_widget_rect(const app_state_t *app);
bool details_beside(proven_arena_t *arena, proven_u8str_view_t picture, rubraview_details_src_t *out, proven_u8str_view_t *out_path);
int details_keep(proven_u8str_view_t source, proven_u8str_view_t written, proven_u8str_view_t named, rubraview_export_format_t format,
                        const rubraview_export_options_t *options);
void dispatch_key(app_state_t *app, rubraview_key_combo_t combo);
bool drain_one_pending_decode(app_state_t *app);
void draw_ab_edit_box(app_state_t *app, double win_w, double win_h, double dpi);
void draw_box(app_state_t *app, const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics,
                     const char *const *captions, int32_t caption_count,
                     const rubraview_menu_state_t *menu);
void draw_box_grip(app_state_t *app, const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics,
                          uint32_t fill, uint32_t border);
void draw_chooser(app_state_t *app, rubraview_renderer_t *r, int where, double px, double py);
void draw_chrome(app_state_t *app, double win_w, double win_h);
void draw_chrome_answers(app_state_t *app, double win_w, double win_h);
void draw_crop_overlay(app_state_t *app);
void draw_ctxmenu(app_state_t *app);
void draw_ctxmenu_on(app_state_t *app, rubraview_renderer_t *r, double px, double py);
void draw_curve_widget(app_state_t *app);
void draw_edge_nav(app_state_t *app, double win_w, double win_h);
void draw_edit_preview(app_state_t *app);
void draw_edit_window(app_state_t *app);
void draw_eqwin(app_state_t *app, double win_w, double win_h);
void draw_favwin(app_state_t *app, double win_w, double win_h);
void draw_files_window(app_state_t *app);
void draw_fontpick_window(app_state_t *app);
void draw_help_window(app_state_t *app);
void draw_hint(app_state_t *app, double win_w, double win_h);
void draw_info_window(app_state_t *app);
void draw_list_bar(app_state_t *app, rubraview_renderer_t *rr, const rubraview_listwin_t *lw, double px, double py);
void draw_list_drop_line(app_state_t *app, rubraview_renderer_t *rr, const rubraview_listwin_t *lw, bool in_files);
void draw_list_row_parts(app_state_t *app, rubraview_renderer_t *rr, const rubraview_listwin_t *lw, size_t i, size_t count,
                                double px, double py, size_t page);
void draw_list_tools(app_state_t *app, rubraview_renderer_t *rr, const rubraview_listwin_t *lw, double px, double py, bool in_files);
void draw_listwin(app_state_t *app, double win_w, double win_h);
void draw_mini_window(app_state_t *app);
void draw_music_backdrop(app_state_t *app, int32_t win_w, int32_t win_h);
void draw_music_extras(app_state_t *app, int32_t win_w, int32_t win_h);
void draw_music_words(app_state_t *app, int32_t win_w, int32_t win_h);
void draw_notice(app_state_t *app, double win_w, double win_h, double chrome_dpi);
void draw_outlined_text(rubraview_renderer_t *renderer, proven_u8str_view_t text, rubraview_pal_rect_t rect,
                               double size, double outline, uint32_t argb);
void draw_outlined_text_aligned(rubraview_renderer_t *renderer, proven_u8str_view_t text, rubraview_pal_rect_t rect,
                                       double size, double outline, uint32_t argb, rubraview_text_align_t align);
void draw_page_tiles(app_state_t *app, int which, int32_t index, const app_page_t *page,
                            rubraview_mat3x2_t place, double scale, int32_t win_w, int32_t win_h,
                            double opacity, rubraview_interpolation_t interp);
void draw_panel(app_state_t *app);
void draw_panel_of(app_state_t *app, const rubraview_panel_t *panel, bool numbers);
void draw_picker_check(app_state_t *app, size_t index, double win_w, double dpi);
void draw_print_preview(app_state_t *app);
void draw_px_edit_box(app_state_t *app, double win_w, double win_h, double dpi);
void draw_recentwin(app_state_t *app, double win_w, double win_h);
void draw_rename_box(app_state_t *app, double win_w, double win_h, double dpi);
void draw_seekfan(app_state_t *app, rubraview_renderer_t *r, bool detached);
void draw_settings_window(app_state_t *app);
void draw_slidepop(app_state_t *app);
void draw_spread(app_state_t *app, size_t spread_index, double opacity,
                        int32_t win_w, int32_t win_h);
void draw_strip(app_state_t *app, int32_t win_w, int32_t win_h);
void draw_strip_button(app_state_t *app, rubraview_renderer_t *r, rubraview_pal_rect_t tile, int32_t i,
                              const rubraview_action_facts_t *facts, bool hovered, uint32_t fill, uint32_t border);
void draw_strip_head(app_state_t *app, rubraview_renderer_t *r, const rubraview_toolbox_layout_t *l,
                            double ox, double oy, int32_t hovered, double opacity);
void draw_subs_window(app_state_t *app);
void draw_subtitle(app_state_t *app, int32_t win_w, int32_t win_h);
void draw_text_page(app_state_t *app, int32_t win_w, int32_t win_h);
void draw_text_weight(app_state_t *app, proven_u8str_view_t text, rubraview_pal_rect_t r, double size, uint32_t argb, bool bold);
void draw_titlebar(app_state_t *app, double win_w);
void draw_toolbox_window(app_state_t *app);
rubraview_edge_side_t edge_nav_side(app_state_t *app, double x, double y);
bool edit_apply_pending(app_state_t *app, bool say);
void edit_auto_colour(app_state_t *app);
void edit_copy_area(app_state_t *app);
void edit_crop_to_selection(app_state_t *app);
void edit_cur_size(const app_state_t *app, int32_t *w, int32_t *h);
bool edit_denies(proven_u8str_view_t action);
void edit_effect_apply(app_state_t *app);
void edit_live_close(app_state_t *app);
void edit_live_update(app_state_t *app, int32_t kind);
bool edit_panel_open(const app_state_t *app);
rubraview_edit_step_t edit_pending(const app_state_t *app);
void edit_plugin_apply(app_state_t *app);
void edit_preview_close(app_state_t *app);
void edit_preview_open(app_state_t *app);
void edit_preview_update(app_state_t *app);
size_t edit_pump(app_state_t *app);
void edit_resize_apply(app_state_t *app);
void edit_save_over(app_state_t *app);
void edit_sel_apply_row(app_state_t *app, int32_t index);
void edit_sel_sync(app_state_t *app);
void edit_sel_toggle(app_state_t *app);
bool edit_side_press(app_state_t *app, double px, double py);
void edit_steps_changed(app_state_t *app);
void edit_steps_toggle(app_state_t *app);
void edit_undo(app_state_t *app, bool redo);
void edit_value_typed(app_state_t *app);
void edit_values_fresh(app_state_t *app);
bool edit_view_rect(app_state_t *app, rubraview_pal_rect_t *out, double *out_scale, bool *changed);
rubraview_edit_session_t edit_whole(const app_state_t *app);
void edit_window_close(app_state_t *app);
void edit_window_layout(app_state_t *app);
void edit_window_show(app_state_t *app);
const char *effect_word(int32_t e);
app_page_t *ensure_page_loaded(app_state_t *app, int32_t index);
app_page_t *ensure_page_loaded_how(app_state_t *app, int32_t index, bool on_screen);
double eq_band_db(const app_state_t *app, size_t band);
proven_u8str_view_t eq_band_key(size_t band);
proven_u8str_view_t eq_preset_label(int preset);
int eq_preset_now(const app_state_t *app);
void eq_shown_gains(const app_state_t *app, float *out);
bool eqwin_press(app_state_t *app, double x, double y, bool right);
void eqwin_set_band(app_state_t *app, size_t band, double db);
void eqwin_toggle(app_state_t *app);
bool eqwin_wheel(app_state_t *app, double x, double y, double delta);
bool export_more_apply(rubraview_export_options_t *o, int32_t id, double v);
void export_more_rows(rubraview_panel_t *pn, const rubraview_export_options_t *o, uint32_t rows);
void ext_switch(app_state_t *app, int32_t row);
size_t favorite_drop_index(const app_state_t *app, double x, double dpi, double win_w);
void favorite_rename_begin(app_state_t *app, size_t index);
void favorite_rename_commit(app_state_t *app);
bool favorite_toggle_path(app_state_t *app, proven_u8str_view_t path);
void favorites_save(app_state_t *app);
void favwin_build(app_state_t *app);
void favwin_motion(app_state_t *app, double x, double y);
bool favwin_press(app_state_t *app, double x, double y);
bool favwin_release(app_state_t *app);
void favwin_toggle(app_state_t *app);
bool ffmpeg_button(app_state_t *app, int32_t id);
bool ffmpeg_list_ready(void);
bool ffmpeg_row_changed(app_state_t *app, int32_t id, double v);
void ffmpeg_typed(app_state_t *app);
void files_close(app_state_t *app);
proven_u8str_view_t files_fit_text(char *buf, size_t cap, proven_u8str_view_t text, double width, double size);
void files_go_to(app_state_t *app, int32_t index);
void files_layout(app_state_t *app);
void files_preview_drop(app_state_t *app);
void files_preview_step(app_state_t *app, double now);
size_t files_pump(app_state_t *app);
void files_show(app_state_t *app);
int files_size_text(char *buf, size_t cap, uint64_t bytes);
int32_t files_target(const app_state_t *app);
bool filing_exists(void *ctx, proven_u8str_view_t path);
void filing_put(app_state_t *app, bool moving);
void filing_put_to(app_state_t *app, proven_u8str_view_t dir, bool moving);
void finish_open(app_state_t *app, size_t start_page);
const char *fit_action_of(rubraview_fit_mode_t fit);
void folder_relist(app_state_t *app);
rubraview_sort_mode_t folder_sort_mode(const app_state_t *app, bool *out_ascending);
void fontpick_close(app_state_t *app);
size_t fontpick_pump(app_state_t *app);
void fontpick_reveal(app_state_t *app);
void fontpick_show(app_state_t *app, proven_u8str_view_t section, proven_u8str_view_t key);
void frame_take(app_state_t *app, const uint8_t *bgra, int32_t width, int32_t height, size_t stride);
bool gmic_button(app_state_t *app, int32_t id);
void gmic_from_settings(app_state_t *app);
void gmic_list_make(app_state_t *app);
void gmic_list_poll(app_state_t *app);
bool gmic_row_changed(app_state_t *app, int32_t id, double v);
const char *gmic_step_name(const rubraview_edit_step_t *step);
void gmic_typed(app_state_t *app);
void go_to_spread(app_state_t *app, size_t index);
void handle_action(app_state_t *app, proven_u8str_view_t action);
bool handle_chrome_click(app_state_t *app, double x, double y);
void handle_drop(app_state_t *app, const rubraview_window_event_t *event);
void hdr_screen_tick(app_state_t *app, double dt);
size_t help_pump(app_state_t *app);
void help_show(app_state_t *app);
bool hint_shown(const app_state_t *app);
proven_u8str_view_t hint_with_keys(app_state_t *app, proven_u8str_view_t action, const char *said, char *buffer, size_t capacity);
void history_load(app_state_t *app);
void history_remember(app_state_t *app);
void info_local_time(char *buf, size_t cap, int64_t unix_seconds);
size_t info_pump(app_state_t *app);
void info_show(app_state_t *app);
void interval_set(app_state_t *app, double seconds);
bool is_media_path(proven_u8str_view_t path);
bool is_station_path(proven_u8str_view_t path);
bool is_text_path(proven_u8str_view_t path);
bool key_is(rubraview_key_combo_t combo, const char *name);
void launch_paths_gather(proven_arena_t *arena, const rubraview_cli_result_t *cli);
void layout_load(app_state_t *app);
const char *layout_said(rubraview_layout_choice_t choice);
void layout_save(app_state_t *app);
void layout_set(app_state_t *app, rubraview_layout_choice_t choice, bool say);
void list_apply(app_state_t *app, const size_t *order, size_t n, const rubraview_fs_entry_t *added, const char *said);
void list_button(rubraview_renderer_t *rr, double px, double py, rubraview_rect_t r, const char *label, double size,
                        bool on, uint32_t color);
void list_check_mark(rubraview_renderer_t *rr, rubraview_rect_t c, bool every, bool some);
size_t list_checked_total(const app_state_t *app);
void list_delete_row(app_state_t *app, size_t row, rubraview_window_t *owner);
bool list_editable(const app_state_t *app);
bool list_ext_is_off(const app_state_t *app, proven_u8str_view_t ext);
void list_filter_files_end(app_state_t *app);
void list_filter_follow(app_state_t *app);
void list_filter_set(app_state_t *app, proven_u8str_view_t text);
void list_insert_paths(app_state_t *app, const proven_u8str_view_t *paths, size_t count, size_t to);
void list_motion(app_state_t *app, double x, double y, bool in_files);
size_t list_page(app_state_t *app, size_t row);
bool list_press(app_state_t *app, rubraview_listwin_t *lw, double x, double y, bool right, bool in_files);
void list_remove(app_state_t *app, const bool *gone);
void list_row_menu(app_state_t *app, size_t row, double x, double y, bool in_files);
size_t list_row_of(app_state_t *app, size_t page);
size_t list_rows(app_state_t *app);
bool list_saves_whole_paths(const app_state_t *app);
void list_sort(app_state_t *app, int key, bool desc);
const char *list_sort_word(int key);
char *list_text_decode(proven_u8str_view_t raw, size_t *len);
void list_view_changed(app_state_t *app);
bool list_viewed(const app_state_t *app);
void listwin_button(app_state_t *app, rubraview_rect_t r, const char *label, double size, bool on, uint32_t color);
void listwin_motion(app_state_t *app, double x, double y);
void listwin_place_clear(app_state_t *app, double win_w, double win_h);
bool listwin_press(app_state_t *app, double x, double y, bool right);
bool listwin_release(app_state_t *app);
void listwin_sync(app_state_t *app);
void listwin_toggle(app_state_t *app);
void load_keymap(app_state_t *app);
void media_ab_check(app_state_t *app);
void media_close(app_state_t *app);
void media_prepare(app_state_t *app);
void media_reopen_on_new_device(app_state_t *app);
void media_seek_to(app_state_t *app, double seconds);
void media_step(app_state_t *app, bool forward);
void media_tick(app_state_t *app);
void media_toggle_pause(app_state_t *app);
void memory_watch(app_state_t *app, double now);
void menu_rebuild(app_state_t *app);
proven_u8str_view_t menu_tile_caption(const rubraview_menu_state_t *menu, int32_t tile, char *scratch, size_t scratch_size,
                                 bool *out_enabled, bool *out_current);
void mini_close(app_state_t *app);
bool mini_paused(const app_state_t *app);
size_t mini_pump(app_state_t *app);
void mini_show(app_state_t *app);
void music_make_backdrop(app_state_t *app, const rubraview_tags_t *tags);
bool name_holds(proven_u8str_view_t name, proven_u8str_view_t part);
double name_scale(const app_state_t *app);
void new_window(app_state_t *app);
void next_spread(app_state_t *app);
void note_activity(app_state_t *app);
bool on_archive_page(const app_state_t *app);
bool open_archive(app_state_t *app, proven_u8str_view_t archive_path);
bool open_folder(app_state_t *app, proven_u8str_view_t dir);
void open_path(app_state_t *app, proven_u8str_view_t path);
void open_paths_as_set(app_state_t *app, const proven_u8str_view_t *paths, size_t count, const char *what);
void open_playlist(app_state_t *app, proven_u8str_view_t path);
void open_set_from_entries(app_state_t *app, rubraview_fs_entry_t *entries, size_t count, proven_u8str_view_t said);
void open_set_ordered(app_state_t *app, rubraview_fs_entry_t *entries, size_t count, proven_u8str_view_t said,
                             bool as_given, proven_u8str_view_t start);
bool open_sibling_archive(app_state_t *app, bool forward);
bool open_sibling_folder(app_state_t *app, bool forward);
void osd_say(app_state_t *app, proven_u8str_view_t text);
size_t page_count(const app_state_t *app);
proven_u8str_view_t page_display_name(const app_state_t *app, size_t index);
void page_failure_notice(app_state_t *app);
rubraview_interpolation_t page_interpolation(const app_state_t *app);
void page_job_poll(app_state_t *app);
void page_job_stop(app_state_t *app);
bool page_moves(app_state_t *app, size_t index, const rubraview_page_bytes_t *bytes);
uint64_t page_read_cost(app_state_t *app, size_t index);
rubraview_page_bytes_t page_read_owned(rubraview_page_source_t *source, SRWLOCK *lock, size_t index, void **out_mem);
bool page_screen_rect(app_state_t *app, rubraview_pal_rect_t *out_rect, double *out_scale);
void pagebar_drag(app_state_t *app, double x, bool released);
void pagebar_press(app_state_t *app, double bar_x, double bar_width, double x, bool detached);
bool pagebar_shown(const app_state_t *app);
bool pal_rect_has(rubraview_pal_rect_t r, double x, double y);
void panel_apply_row(app_state_t *app, int32_t index);
void panel_build_edit(app_state_t *app);
void panel_close(app_state_t *app);
bool panel_handle_press(app_state_t *app, double px, double py);
void panel_open_batch(app_state_t *app);
void panel_open_edit(app_state_t *app);
void panel_open_export(app_state_t *app);
void panel_open_print(app_state_t *app);
void panel_relayout(app_state_t *app);
void panel_run_batch(app_state_t *app);
void panel_write_archive_page(app_state_t *app, bool over);
void panel_write_current(app_state_t *app, bool apply_edit);
void password_ask(app_state_t *app, proven_u8str_view_t archive_path, bool wrong);
void password_commit(app_state_t *app);
void password_remember(app_state_t *app, proven_u8str_view_t password);
bool path_is_listed_kind(proven_u8str_view_t path);
bool path_is_playlist(proven_u8str_view_t path);
void picker_activate(app_state_t *app, size_t index);
void picker_add_to_list(app_state_t *app);
bool picker_bar_item(const app_state_t *app, size_t index, picker_bar_item_t *out);
void picker_button(app_state_t *app, picker_button_t button);
rubraview_pal_rect_t picker_button_rect(double win_w, double win_h, double dpi, int32_t index);
rubraview_pal_rect_t picker_check_rect(const app_state_t *app, size_t index, double win_w, double dpi);
bool picker_chip_span(const app_state_t *app, size_t index, double dpi, double win_w, double *out_x, double *out_w);
double picker_chip_width(proven_u8str_view_t label, double dpi);
int picker_column_at(const picker_columns_t *c, double x);
int picker_column_edge(const picker_columns_t *c, double x, double dpi);
int32_t picker_columns(const app_state_t *app);
void picker_columns_motion(app_state_t *app, double x, double y);
void picker_columns_read(proven_u8str_view_t text);
bool picker_columns_release(app_state_t *app, double x);
void picker_context(app_state_t *app, size_t index, double x, double y);
void picker_copy_paths(app_state_t *app);
void picker_curate(app_state_t *app, int32_t digit, bool moving);
void picker_curate_to(app_state_t *app, proven_u8str_view_t target_dir, bool moving);
picker_columns_t picker_detail_columns(double win_w, double dpi);
bool picker_details(const app_state_t *app);
bool picker_enter_book(app_state_t *app, size_t index);
rubraview_pal_rect_t picker_favwin_chip(const app_state_t *app, double dpi, double win_w);
void picker_filter_begin(app_state_t *app);
rubraview_pal_rect_t picker_filter_chip(const app_state_t *app, double win_w, double dpi, proven_u8str_view_t *out_label, char *buf, size_t cap);
void picker_filter_follow(app_state_t *app);
void picker_filter_forget(app_state_t *app);
void picker_filter_set(app_state_t *app, proven_u8str_view_t text);
proven_u8str_view_t picker_focus_extension(const app_state_t *app);
void picker_go_up(app_state_t *app);
double picker_grid_top(const app_state_t *app, double dpi);
void picker_history_go(app_state_t *app, bool forward);
void picker_hold_tick(app_state_t *app);
bool picker_is_up(const app_state_t *app, size_t index);
void picker_layout(app_state_t *app);
void picker_make_playlist(app_state_t *app);
rubraview_pal_rect_t picker_nav_rect(const app_state_t *app, double dpi, int which);
void picker_navigate(app_state_t *app, proven_u8str_view_t dir);
void picker_open(app_state_t *app);
void picker_path_begin(app_state_t *app);
size_t picker_picked_entries(app_state_t *app, rubraview_fs_entry_t **out);
void picker_recycle(app_state_t *app);
void picker_select_everything(app_state_t *app);
void picker_sort_by(app_state_t *app, int key);
void picker_sort_set(app_state_t *app, int key, bool desc);
proven_u8str_view_t picker_star_label(const app_state_t *app);
void picker_thumbs_free(app_state_t *app);
void picker_thumbs_step(app_state_t *app);
int32_t picker_tile_columns(const app_state_t *app);
void picker_tile_size(app_state_t *app, double *out_w, double *out_h);
void picker_toggle_favorite(app_state_t *app);
proven_u8str_view_t picker_tool_label(const app_state_t *app, int which, char *buf, size_t cap);
rubraview_pal_rect_t picker_tool_rect(const app_state_t *app, double dpi, int which);
double picker_top(const app_state_t *app);
size_t picture_bytes(size_t w, size_t h, size_t per_pixel, size_t extra);
void picture_copy(app_state_t *app);
int place_font_begin(app_state_t *app, int which, double *size);
void place_font_end(app_state_t *app, int before);
double place_size(app_state_t *app, int which);
double play_duration(const app_state_t *app);
double play_position(const app_state_t *app);
void play_seek(app_state_t *app, double seconds);
rubraview_pixbuf_t plugin_runner(void *user, proven_arena_t *arena, const rubraview_edit_step_t *step, const rubraview_pixbuf_t *src);
void plugins_from_settings(app_state_t *app);
void plugins_look(proven_u8str_view_t folder);
size_t plugins_usable(const rubraview_plugin_t **out, size_t capacity);
bool point_in_curve_widget(const app_state_t *app, double px, double py);
rubraview_pointer_context_t pointer_context(const app_state_t *app);
bool popups_key(app_state_t *app, rubraview_key_combo_t combo);
bool popups_press(app_state_t *app, double x, double y, bool right);
rubraview_media_backend_t preferred_backend(proven_arena_t *arena);
void prev_spread(app_state_t *app);
void print_archive_page(app_state_t *app);
void print_now(app_state_t *app);
void print_papers_load(app_state_t *app);
void print_paths(app_state_t *app, const proven_u8str_view_t *paths, size_t count);
proven_u8str_view_t print_printer_name(const app_state_t *app);
proven_u8str_view_t print_row_text(const rubraview_panel_row_t *row, char *buffer, size_t size);
void print_sheet_load(app_state_t *app);
void print_temp_drop(app_state_t *app);
void print_thumbs_load(app_state_t *app);
int probe_gpu_file(proven_arena_t *arena, proven_u8str_view_t path);
void probe_gpu_resize(void);
int probe_media_file(proven_arena_t *arena, proven_u8str_view_t path);
void pump_messages(void);
void putin_end(app_state_t *app, bool swapped);
void putin_step(app_state_t *app);
rubraview_pal_rect_t px_box_rect(const app_state_t *app);
proven_u8str_view_t px_button_label(const app_state_t *app);
void px_edit_begin(app_state_t *app);
void px_edit_begin_crop(app_state_t *app);
void px_edit_close(app_state_t *app);
int32_t px_edit_fields(const app_state_t *app);
void px_edit_fill_crop(app_state_t *app);
bool px_edit_handle_key(app_state_t *app, rubraview_key_combo_t combo);
void px_edit_text_typed(app_state_t *app, proven_u8str_view_t text);
void rebuild_layout(app_state_t *app);
void recent_forget(app_state_t *app, const proven_u8str_view_t *paths, size_t count);
bool recent_kept_out(const app_state_t *app, proven_u8str_view_t path);
void recent_note(app_state_t *app, proven_u8str_view_t path);
void recent_note_shown(app_state_t *app);
size_t recent_skip_dirs(const app_state_t *app, proven_u8str_view_t *out);
void recent_sync(app_state_t *app, proven_u8str_view_t opened, bool clear);
void recentwin_build(app_state_t *app);
void recentwin_motion(app_state_t *app, double x, double y);
bool recentwin_press(app_state_t *app, double x, double y);
bool recentwin_release(app_state_t *app);
void recentwin_toggle(app_state_t *app);
void rename_begin(app_state_t *app);
bool rename_box_press(app_state_t *app, double x, double y);
void rename_commit(app_state_t *app);
void rename_end(app_state_t *app);
void rename_extension_apply(app_state_t *app);
void rename_extension_begin(app_state_t *app);
void rename_insert(app_state_t *app, proven_u8str_view_t text);
void rename_picker_begin(app_state_t *app, size_t at);
void rename_picker_commit(app_state_t *app);
void rename_text(app_state_t *app, const rubraview_window_event_t *event);
void render_frame(app_state_t *app);
void reopen_after_removal(app_state_t *app, size_t removed_index);
void reopen_on_path(app_state_t *app, proven_u8str_view_t path, size_t fallback);
rubraview_repeat_mode_t repeat_mode(const app_state_t *app);
void report_startup_failure(void);
void reset_view(app_state_t *app);
void restart_now(app_state_t *app);
int run_batch(proven_arena_t *arena, const rubraview_cli_result_t *cli);
int run_diagnostics(proven_arena_t *arena, proven_u8str_view_t image_path);
void save_job_poll(app_state_t *app, bool wait);
uint64_t scene_signature(const app_state_t *app);
proven_arena_t *scratch_begin(app_state_t *app, rubraview_scratch_t *s, size_t bytes);
void scratch_end(app_state_t *app, rubraview_scratch_t *s);
void secure_wipe(void *p, size_t n);
bool seek_tile_action(proven_u8str_view_t action, bool *out_forward);
void seekfan_let_go(app_state_t *app);
void seekfan_pointer(app_state_t *app, double x, double y);
void sel_drag_to(app_state_t *app, int32_t ix, int32_t iy);
int32_t sel_hit(app_state_t *app, double px, double py);
void sel_mode_set(app_state_t *app, bool on);
void settings_close(app_state_t *app);
bool settings_copy_key(app_state_t *app, rubraview_key_combo_t combo);
void settings_follow(app_state_t *app, int what);
void settings_number_begin(app_state_t *app);
void settings_open(app_state_t *app);
void settings_path_begin(app_state_t *app, const rubraview_setting_def_t *def);
size_t settings_pump(app_state_t *app);
void settings_read_file(app_state_t *app);
bool settings_row_is_text(const app_state_t *app, int32_t row);
void settings_say(app_state_t *app, const char *text);
bool settings_scrollbar(const app_state_t *app, rubraview_pal_rect_t *track, rubraview_pal_rect_t *thumb);
void settings_share(app_state_t *app, double now);
void settings_took_effect(app_state_t *app);
void settings_write_if_changed(app_state_t *app, bool say);
int shell_apply(proven_arena_t *arena, proven_u8str_view_t selection, proven_u8str_view_t menu_selection, shell_part_t types, shell_part_t menu,
                       bool all_users);
void shell_menu_take_old(app_state_t *app, proven_u8str_view_t text);
int shell_remove(bool all_users);
void shell_request_take(app_state_t *app, rubraview_shell_verb_t verb, bool whole, const proven_u8str_view_t *paths, size_t count);
void shell_requests_flush(app_state_t *app);
size_t shell_requests_pump(app_state_t *app);
int shell_wipe(bool all_users);
void show_frame(app_state_t *app, size_t frame_index);
void skip_dirs_table_rows(app_state_t *app);
void skip_dirs_take_old(app_state_t *app);
bool slide_ready(const app_state_t *app);
void slidepop_close(app_state_t *app);
void slides_follow_screen(app_state_t *app);
void source_close(app_state_t *app);
void speed_set(app_state_t *app, double speed);
int speed_text(char *buffer, size_t size, double speed);
void speed_tile_move(app_state_t *app, double y, bool detached);
void speed_tile_press(app_state_t *app, double y, bool detached, bool interval);
void speed_tile_release(app_state_t *app, bool detached);
bool spread_composition(app_state_t *app, rubraview_composition_t *out);
size_t spread_for_chosen_page(app_state_t *app, int32_t page);
size_t spread_index_for_page(const app_state_t *app, int32_t page_index);
void startup_without_file(app_state_t *app, bool no_file);
void station_alts_note(const rubraview_playlist_entry_t *e);
int32_t station_connecting_page(const app_state_t *app);
void station_job_poll(app_state_t *app);
void station_title_poll(app_state_t *app);
bool strip_active(const app_state_t *app);
void strip_cells_free(app_state_t *app);
void strip_cells_reset(app_state_t *app);
uint32_t strip_fade(uint32_t argb, double k);
void strip_follow(app_state_t *app, size_t page);
double strip_page_height(void *context, size_t index);
void strip_peek(app_state_t *app);
bool strip_press(app_state_t *app, double x, double y);
rubraview_pal_rect_t strip_rect(const app_state_t *app);
double strip_scroll_by(app_state_t *app, double delta);
double strip_shown(const app_state_t *app);
void strip_step(app_state_t *app, double dt);
void strip_title_plan(app_state_t *app);
double strip_width(app_state_t *app, double win_w);
void sub_list_choose(app_state_t *app, int32_t row);
void sub_list_open_at(app_state_t *app, double x, double y);
rubraview_rect_t sub_list_rect(const app_state_t *app, size_t *out_rows);
bool sub_tile_anchor(app_state_t *app, double *x, double *y);
void sub_tile_result(app_state_t *app, rubraview_tap_result_t r);
void subbox_drag_motion(app_state_t *app, double x, double y);
rubraview_rect_t subbox_geometry(app_state_t *app, double win_w, double win_h, double *out_font);
bool subbox_live(app_state_t *app);
rubraview_subbox_place_t subbox_place(app_state_t *app);
bool subbox_press(app_state_t *app, double x, double y, bool buttons_only);
proven_arena_t *subs_arena(app_state_t *app, size_t need);
void subs_close(app_state_t *app);
size_t subs_edit_room(const app_state_t *app);
bool subs_may_leave(app_state_t *app);
size_t subs_pump(app_state_t *app);
void subs_show(app_state_t *app);
rubraview_subtitle_track_t subtitle_find(proven_arena_t *arena, proven_u8str_view_t video_path,
                                                proven_u8str_view_t *out_name, const char **out_why);
void subtitle_say(app_state_t *app);
void subtitle_select(app_state_t *app, int32_t index);
size_t subtitle_track_indices(const app_state_t *app, int32_t *out, size_t capacity);
void subtitles_toggle(app_state_t *app);
void sync_menubox_tiles(app_state_t *app);
void takeout_begin(app_state_t *app);
void takeout_end(app_state_t *app);
void takeout_step(app_state_t *app);
void target_begin(app_state_t *app, bool moving, bool from_picker);
void target_browse(app_state_t *app);
void target_commit(app_state_t *app);
bool text_action(app_state_t *app, proven_u8str_view_t action);
bool text_active(const app_state_t *app);
size_t text_block_start(const text_page_t *t, size_t i);
void text_close(app_state_t *app);
uint32_t text_colour(app_state_t *app, const char *key, uint32_t fallback);
double text_dpi(app_state_t *app);
void text_drag(app_state_t *app, double x, double y);
int text_enc_now(const app_state_t *app);
void text_encoding_set(app_state_t *app, rubraview_textenc_t next);
const char *text_encoding_shown(const app_state_t *app);
void text_find_step(app_state_t *app, bool forward);
void text_free_blocks(text_page_t *t);
uint32_t text_ink(app_state_t *app, uint32_t *paper);
double text_line_h(app_state_t *app);
bool text_moving(const app_state_t *app);
bool text_next_joined(const text_page_t *t, size_t i);
bool text_pages_mode(const app_state_t *app);
size_t text_para_at_y(text_page_t *t, double y);
int text_percent(const app_state_t *app);
void text_prepare(app_state_t *app);
bool text_press(app_state_t *app, double x, double y, uint32_t mods);
double text_px(app_state_t *app);
bool text_rebuild(app_state_t *app);
bool text_release(app_state_t *app, double x, double y);
void text_reset_heights(app_state_t *app, text_page_t *t);
void text_scroll_by(app_state_t *app, double dy);
double text_scroll_max(text_page_t *t);
void text_scroll_to(text_page_t *t, double y);
double text_scroll_y(text_page_t *t);
double text_spacing(app_state_t *app);
void text_tick(app_state_t *app, double now);
void text_tops(text_page_t *t);
bool text_wheel(app_state_t *app, double notches, uint32_t mods);
bool text_with_bytes(app_state_t *app, int32_t index, rubraview_textenc_t want, bool by_hand);
bool text_wraps(app_state_t *app);
void textbox_insert(rubraview_textedit_t *te, proven_u8str_view_t text);
bool textbox_key(rubraview_window_t *window, rubraview_textedit_t *te, rubraview_key_combo_t combo);
void thumbs_wake(void *context);
void tick_timers(app_state_t *app, double dt);
void tiles_reset(app_state_t *app);
void tiles_take_all(app_state_t *app);
bool titlebar_click(app_state_t *app, double x, double y);
bool tmenu_key(app_state_t *app, rubraview_key_combo_t combo);
bool tmenu_press(app_state_t *app, double x, double y);
int32_t tmenu_win_w(const app_state_t *app);
void toggle_slideshow(app_state_t *app);
proven_u8str_view_t toolbox_caption(const app_state_t *app, const rubraview_box_tile_t *tile_in, bool *out_enabled);
void toolbox_detach(app_state_t *app, int32_t screen_x, int32_t screen_y, bool follow_pointer);
void toolbox_dock(app_state_t *app, double client_x, double client_y);
const char *toolbox_profile_name(const app_state_t *app);
void toolbox_refresh(app_state_t *app);
void toolbox_window_pump(app_state_t *app);
void tracks_prepare(app_state_t *app, proven_u8str_view_t video_path);
void triage_curate(app_state_t *app, int32_t digit);
void triage_delete(app_state_t *app, bool permanent);
void triage_undo(app_state_t *app);
void ui_font_apply_all(app_state_t *app);
void unload_all_pages(app_state_t *app);
void update_precache(app_state_t *app);
void update_window_title(app_state_t *app);
void view_hold_tick(app_state_t *app);
bool visualizer_live(const app_state_t *app);
proven_arena_t *window_block_arena(app_state_t *app, struct window_block *wb);
void window_block_give(app_state_t *app, struct window_block *wb);
proven_arena_t *window_block_take(app_state_t *app, struct window_block *wb);
void zoom_cycle(app_state_t *app);
void zoom_leave_real_fit(app_state_t *app);
void zoom_say(app_state_t *app);
void zoom_step(app_state_t *app, bool larger);
bool zoom_tile_wheel(app_state_t *app, double x, double y, double notches);

#endif
