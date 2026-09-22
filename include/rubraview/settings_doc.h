#ifndef RUBRAVIEW_SETTINGS_DOC_H
#define RUBRAVIEW_SETTINGS_DOC_H

#include "rubraview/core.h"
#include "rubraview/settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The settings page document (§3.22, D-13).
 *
 * The settings window is not written control by control. It is described
 * in a small text document — one line, one thing — and one interpreter
 * lays every page out from it. The same lines that place a setting on a
 * page also declare its type, its range and its default, so there is one
 * list of settings in the program and not two to keep in step.
 *
 *     # a comment
 *     page    video "Video & subtitles"
 *     section "Subtitles"
 *     int     video.subtitle_size  "Size"  10..72 step 1 unit "pt" = 24 wired
 *     choice  video.decoder "Decoder"  windows | ffmpeg = windows wired
 *     toggle  video.hardware_decode "GPU decoding" = true
 *     float   video.ab_step_seconds "A-B step" 0.1..1.0 step 0.1 unit "s" = 0.5
 *     path    curation.dir_1 "Folder 1" wired
 *     info    "FFmpeg" {media.ffmpeg}
 *     note    "Put FFmpeg's DLLs beside rubraview.exe."
 *     preview subtitle 3
 *     table   keymap
 *     action  shell.register "Register file types"
 *
 * `page` starts a page (an id and a title); `section` puts a heading on
 * it. `toggle`, `choice`, `int`, `float` and `path` are settings: a
 * `section.key` — the place in settings.ini — then the label, then what
 * the kind needs. `wired` says something in the viewer reads the key
 * today (scripts/check-settings.py holds the document to that). `info`
 * is a read-only line filled from a named source while the window is
 * open, `note` a line of fixed help text across the page, `preview` a block the viewer paints for a named sample, `table`
 * rows from a named source in fixed-width columns. `action` is a button
 * that does something named — registering file types — rather than
 * holding a value.
 *
 * The document is internal (owner, 2026-09-13): it is compiled in, and a
 * file beside the executable does not replace it.
 */

typedef enum rubraview_settings_node_kind {
    RUBRAVIEW_NODE_PAGE = 0,
    RUBRAVIEW_NODE_SECTION,
    RUBRAVIEW_NODE_SETTING,
    RUBRAVIEW_NODE_INFO,
    RUBRAVIEW_NODE_PREVIEW,
    RUBRAVIEW_NODE_TABLE,
    RUBRAVIEW_NODE_ACTION,
    RUBRAVIEW_NODE_NOTE,       /* a line of help text, the page's full width */
} rubraview_settings_node_kind_t;

typedef struct rubraview_settings_node {
    rubraview_settings_node_kind_t kind;
    int32_t  page;      /* which page it is on (the page node's own index for PAGE) */
    uint32_t line;      /* where in the document, for messages */
    u8str_t  text;      /* PAGE and SECTION: the title; INFO and ACTION: the label */
    u8str_t  name;      /* PAGE: its id; INFO and TABLE: the source; PREVIEW: the sample; ACTION: what it does */
    int32_t  setting;   /* SETTING: the index into `defs` */
    int32_t  rows;      /* PREVIEW: how many rows tall */
} rubraview_settings_node_t;

#define RUBRAVIEW_SETTINGS_DOC_MAX_NODES 256
#define RUBRAVIEW_SETTINGS_DOC_MAX_PAGES 16

typedef struct rubraview_settings_doc {
    rubraview_setting_def_t   *defs;
    size_t                     def_count;
    rubraview_settings_node_t *nodes;
    size_t                     node_count;
    size_t                     page_count;
    /* NULL when the document parsed; otherwise what is wrong, and where. */
    const char                *error;
    uint32_t                   error_line;
} rubraview_settings_doc_t;

/**
 * Parse a document. Everything it returns lives in `arena` or in `text`,
 * which must outlive the result. The first mistake stops the parse and is
 * reported with its line; nothing is guessed.
 */
rubraview_settings_doc_t rubraview_settings_doc_parse(proven_arena_t *arena, u8str_t text);

/** The document compiled into the viewer. */
u8str_t rubraview_default_settings_document(void);

/**
 * The built-in document, parsed once on first use and kept for the life
 * of the program. Its settings are what `rubraview_settings_schema`
 * returns. Call from the main thread.
 */
const rubraview_settings_doc_t *rubraview_settings_document(void);

/** A page's title, or an empty string. */
u8str_t rubraview_settings_page_title(size_t page);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_SETTINGS_DOC_H */
