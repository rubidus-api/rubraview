#ifndef RUBRAVIEW_UI_SETTINGS_H
#define RUBRAVIEW_UI_SETTINGS_H

#include "rubraview/core.h"
#include "rubraview/settings.h"
#include "rubraview/settings_doc.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The settings window's interpreter (§3.22, D-13): what is on screen,
 * what has focus, and what a key or a click does — worked out from the
 * page document, with no drawing in it.
 *
 * The window is a grid of fixed-width character cells. Every position
 * here is a cell (column, row); the drawing side multiplies by the
 * font's cell size. That is what lets one interpreter lay out every page
 * without a line of per-control code: a label is so many cells, a value
 * so many more, a slider a run of cells that fill.
 *
 *   col 0        list_cols     content_col
 *   ┌───────────┬──────────────────────────────────────────────┐ row 0
 *   │ General   │ Video                                        │
 *   │ Viewer    │                                              │
 *   │ …         │ ── Subtitles ─────────────────────────────── │
 *   │ > Video   │ Subtitle size     [########------------] 24 pt│
 *   │           │ Subtitle outline  [#####---------------]  2 px│
 *   │           │ (preview, 3 rows)                            │
 *   │           │                                              │
 *   │           │  [ Revert ]  [ Defaults ]  [ Close ]         │ rows - 1
 *   └───────────┴──────────────────────────────────────────────┘
 */

typedef enum rubraview_settings_line_kind {
    RUBRAVIEW_LINE_SECTION = 0,
    RUBRAVIEW_LINE_SETTING,
    RUBRAVIEW_LINE_INFO,
    RUBRAVIEW_LINE_PREVIEW,
    RUBRAVIEW_LINE_TABLE,
    RUBRAVIEW_LINE_ACTION,
} rubraview_settings_line_kind_t;

/** One laid-out line of the current page, in content rows (before scrolling). */
typedef struct rubraview_settings_line {
    rubraview_settings_line_kind_t kind;
    int32_t node;      /* index into the document's nodes */
    int32_t row;       /* first content row it occupies */
    int32_t height;    /* rows */
} rubraview_settings_line_t;

typedef enum rubraview_settings_button {
    RUBRAVIEW_BUTTON_NONE = -1,
    RUBRAVIEW_BUTTON_REVERT = 0,    /* back to what the file held when the window opened */
    RUBRAVIEW_BUTTON_DEFAULTS,
    RUBRAVIEW_BUTTON_CLOSE,         /* the file is written as the window closes */
    RUBRAVIEW_BUTTON_COUNT,
} rubraview_settings_button_t;

#define RUBRAVIEW_SETTINGS_VIEW_MAX_LINES 96
#define RUBRAVIEW_TABLE_ROWS 12   /* how tall a `table` line is until the viewer says how many rows it has */
#define RUBRAVIEW_TABLE_ROWS_MAX 1024

typedef struct rubraview_settings_view {
    const rubraview_settings_doc_t *doc;
    int32_t cols, rows;              /* the window, in cells */
    int32_t list_cols;               /* the page list on the left */
    int32_t content_col;             /* where the page starts */
    int32_t label_cols;              /* a setting's label column */
    int32_t page;
    int32_t focus_line;              /* index into `lines`, or -1 when a button has focus */
    int32_t focus_button;            /* a button, or RUBRAVIEW_BUTTON_NONE */
    int32_t scroll;                  /* the first content row shown */
    int32_t dragging_line;           /* a number being dragged, or -1 */
    int32_t table_rows;              /* how tall a `table` line is, in rows */

    rubraview_settings_line_t lines[RUBRAVIEW_SETTINGS_VIEW_MAX_LINES];
    size_t line_count;
    int32_t content_height;          /* rows the page needs */
} rubraview_settings_view_t;

rubraview_settings_view_t rubraview_settings_view_create(const rubraview_settings_doc_t *doc,
                                                         int32_t cols, int32_t rows);

/** The window changed size: lay the page out again, keeping the focus in sight. */
void rubraview_settings_view_resize(rubraview_settings_view_t *view, int32_t cols, int32_t rows);

/**
 * How many rows `table` lines have (a heading and the keymap's bindings,
 * say), so every row can be scrolled to rather than the first dozen.
 * Clamped to 1..RUBRAVIEW_TABLE_ROWS_MAX.
 */
void rubraview_settings_view_set_table_rows(rubraview_settings_view_t *view, int32_t rows);

/** Go to a page; focus its first setting. */
void rubraview_settings_view_set_page(rubraview_settings_view_t *view, int32_t page);

/** How many content rows are visible between the title and the buttons. */
int32_t rubraview_settings_view_visible_rows(const rubraview_settings_view_t *view);

/** A laid-out line's screen row, or -1 when it is scrolled out of sight. */
int32_t rubraview_settings_view_screen_row(const rubraview_settings_view_t *view, size_t line);

/** Where a button is: its first column and width, on the last row. */
void rubraview_settings_view_button_cells(const rubraview_settings_view_t *view,
                                          rubraview_settings_button_t button,
                                          int32_t *out_col, int32_t *out_width);

typedef enum rubraview_settings_key {
    RUBRAVIEW_SKEY_UP = 0, RUBRAVIEW_SKEY_DOWN, RUBRAVIEW_SKEY_LEFT, RUBRAVIEW_SKEY_RIGHT,
    RUBRAVIEW_SKEY_PAGE_UP, RUBRAVIEW_SKEY_PAGE_DOWN,   /* a value by ten steps */
    RUBRAVIEW_SKEY_SPACE, RUBRAVIEW_SKEY_ENTER,
    RUBRAVIEW_SKEY_TAB, RUBRAVIEW_SKEY_SHIFT_TAB,       /* the next / previous page */
    RUBRAVIEW_SKEY_HOME, RUBRAVIEW_SKEY_END,
    RUBRAVIEW_SKEY_ESCAPE,
    RUBRAVIEW_SKEY_DELETE,                              /* empties a path */
} rubraview_settings_key_t;

typedef enum rubraview_settings_event {
    RUBRAVIEW_SEVENT_NONE = 0,
    RUBRAVIEW_SEVENT_MOVED,      /* focus, scroll or page changed: redraw */
    RUBRAVIEW_SEVENT_CHANGED,    /* a value changed: apply it and redraw */
    RUBRAVIEW_SEVENT_EDIT_TEXT,  /* the focused setting is a path: let the reader pick it */
    RUBRAVIEW_SEVENT_CLEAR_TEXT, /* the focused path is to be emptied (Delete) */
    RUBRAVIEW_SEVENT_ACTION,     /* the focused action line was pressed: do what it names */
    RUBRAVIEW_SEVENT_REVERT,
    RUBRAVIEW_SEVENT_DEFAULTS,
    RUBRAVIEW_SEVENT_CLOSE,
} rubraview_settings_event_t;

rubraview_settings_event_t rubraview_settings_view_key(rubraview_settings_view_t *view,
                                                        rubraview_settings_t *settings,
                                                        rubraview_settings_key_t key);

/** The focused line's document node, or NULL when a button has focus. */
const rubraview_settings_node_t *rubraview_settings_view_focused_node(const rubraview_settings_view_t *view);

/** A click, in cells. */
rubraview_settings_event_t rubraview_settings_view_press(rubraview_settings_view_t *view,
                                                          rubraview_settings_t *settings,
                                                          int32_t col, int32_t row);
/** The pointer moved with the button down: a dragged number follows it. */
rubraview_settings_event_t rubraview_settings_view_drag(rubraview_settings_view_t *view,
                                                         rubraview_settings_t *settings,
                                                         int32_t col);
void rubraview_settings_view_release(rubraview_settings_view_t *view);

/** A wheel turn over the page scrolls it. */
rubraview_settings_event_t rubraview_settings_view_scroll(rubraview_settings_view_t *view, int32_t rows);

/**
 * Live data for `info` lines and rows for `table` lines, from the viewer.
 * Both may be NULL; a line with no source shows a dash.
 */
typedef struct rubraview_settings_sources {
    void *user;
    /** Write the value of `source` into `buffer`; return its length. */
    size_t (*info)(void *user, u8str_t source, char *buffer, size_t capacity);
    /** Write row `index` of table `source`; return its length, or 0 past the last row. */
    size_t (*table_row)(void *user, u8str_t source, size_t index, char *buffer, size_t capacity);
} rubraview_settings_sources_t;

/**
 * The text of one laid-out line, exactly `width` cells (UTF-8, so possibly
 * more bytes), padded with spaces. A preview line is blank here — the
 * viewer paints it. `table_index` picks the row of a table line.
 */
u8str_t rubraview_settings_line_text(const rubraview_settings_view_t *view,
                                      const rubraview_settings_t *settings,
                                      const rubraview_settings_sources_t *sources,
                                      size_t line, int32_t table_index,
                                      char *buffer, size_t capacity);

/** A page's entry in the list on the left, `width` cells, the current one marked. */
u8str_t rubraview_settings_page_text(const rubraview_settings_view_t *view, int32_t page,
                                     char *buffer, size_t capacity);

/** A button's label, as drawn: "[ Revert ]". */
u8str_t rubraview_settings_button_text(rubraview_settings_button_t button);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_UI_SETTINGS_H */
