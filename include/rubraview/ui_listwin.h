#ifndef RUBRAVIEW_UI_LISTWIN_H
#define RUBRAVIEW_UI_LISTWIN_H

#include "rubraview/core.h"
#include "rubraview/ui_box.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The playlist window (owner, 2026-09-29): what is open now — the
 * playlist, or the folder's or the archive's files when there is none — as
 * a see-through list floating over the picture. A title row that drags it
 * and closes it, then one row a file, the one on screen marked and kept in
 * view; the wheel scrolls it, a click goes to that file. Geometry and state
 * only: the viewer paints.
 */

typedef enum rubraview_listwin_part {
    RUBRAVIEW_LISTWIN_NONE = 0,
    RUBRAVIEW_LISTWIN_TITLE,   /* drags the window */
    RUBRAVIEW_LISTWIN_CLOSE,
    RUBRAVIEW_LISTWIN_ROW,
    RUBRAVIEW_LISTWIN_BODY,    /* inside, below the last row */
    /* D-111, a list that can be changed (`editable`): */
    RUBRAVIEW_LISTWIN_RESIZE,     /* left of Close: dragged, it sizes the window */
    RUBRAVIEW_LISTWIN_ADD,        /* the bar under the title: files picked go into the list (RV-117) */
    RUBRAVIEW_LISTWIN_DEL,        /* the checked rows leave the list */
    RUBRAVIEW_LISTWIN_SAVE,
    RUBRAVIEW_LISTWIN_LOAD,
    RUBRAVIEW_LISTWIN_PATHS,      /* saved with relative or whole paths */
    RUBRAVIEW_LISTWIN_ALL,        /* the bar's box at its left, over the rows' boxes: every row checked, or none */
    RUBRAVIEW_LISTWIN_ROW_CHECK,  /* at the left of a row, in this order */
    RUBRAVIEW_LISTWIN_ROW_REMOVE,
    RUBRAVIEW_LISTWIN_ROW_UP,
    RUBRAVIEW_LISTWIN_ROW_DOWN,
    RUBRAVIEW_LISTWIN_APPLY,      /* the bar: Edit's steps onto every picture of the list, as copies (owner, 2026-10-18) */
    /* 1.25.0 (owner, 2026-10-20): a row of tools under the bar (`tools`) */
    RUBRAVIEW_LISTWIN_EXT,        /* .ext: the extensions in the list, each picked or let go at once */
    RUBRAVIEW_LISTWIN_FILTER,     /* the names shown: those holding what is typed */
    RUBRAVIEW_LISTWIN_SORT,       /* the list in another order: by what */
    RUBRAVIEW_LISTWIN_ORDER,      /* rising or falling */
    RUBRAVIEW_LISTWIN_URL,        /* RV-158, the bar beside Add: a station's address typed joins the list */
    RUBRAVIEW_LISTWIN_SHAPE,      /* RV-192, the tools row after .ext: rows checked or not by shape (wide, tall, square) */
    RUBRAVIEW_LISTWIN_MAXIMIZE,   /* RV-197, left of R (of Close without R): the whole view, or back */
} rubraview_listwin_part_t;

typedef struct rubraview_listwin {
    bool   open;
    bool   placed;             /* false until first placed in a view */
    double x, y, width, height;
    double title_height, row_height;
    size_t first;              /* the first row shown */
    /* a drag of the title under way */
    bool   dragging;
    double grab_dx, grab_dy;
    /* D-111: a bar of buttons under the title and four small buttons at the
       left of every row; without it the list is only looked at and clicked. */
    bool   editable;
    double bar_height;
    bool   resizing;           /* the resize button held */
    double resize_dx, resize_dy;
    double resize_bottom;      /* the bottom edge, which stays while the top follows */
    /* RV-117: the list in a window of its own (F3): the window's frame moves,
       sizes and closes it, so it has no R, no X and no title to drag */
    bool   own_window;
    bool   tools;              /* 1.25.0: the tools row under the bar (an editable list) */
    /* RV-197 (owner, 2026-10-30): maximized, it fills the view; the place it had is kept to go back to */
    bool   maximized;
    double restore_x, restore_y, restore_width, restore_height;
    double view_top;           /* the view starts here (below the viewer's own title bar): set by the viewer */
} rubraview_listwin_t;

/** 1.25.0: one of the tools row's parts (EXT, FILTER, SORT, ORDER); empty without the row. */
rubraview_rect_t rubraview_listwin_tool_rect(const rubraview_listwin_t *lw, rubraview_listwin_part_t part);

rubraview_listwin_t rubraview_listwin_create(double dpi_scale);

/**
 * Keep it inside a `view_w` x `view_h` client area, below `view_top`; maximized, all of it. The first time, put it
 * at the right, a little below the top, a third of the view wide (within
 * reason) and three quarters of it tall.
 */
void rubraview_listwin_place(rubraview_listwin_t *lw, double view_w, double view_h);

rubraview_rect_t rubraview_listwin_rect(const rubraview_listwin_t *lw);
rubraview_rect_t rubraview_listwin_close_rect(const rubraview_listwin_t *lw);

/** RV-197: the maximize button, left of R, or of Close when there is no R (empty in a window of its own). */
rubraview_rect_t rubraview_listwin_maximize_rect(const rubraview_listwin_t *lw);

/** Where the title's text ends: the left edge of its buttons. */
double rubraview_listwin_title_text_right(const rubraview_listwin_t *lw);

/**
 * RV-197: maximized, back where it was; else the place kept and the whole `view_w` x `view_h` view taken, below
 * `view_top` (the viewer's own title bar). Dragging a maximized window by its title takes it back to its size under the pointer; sizing it
 * starts from the whole view.
 */
void rubraview_listwin_toggle_maximized(rubraview_listwin_t *lw, double view_w, double view_h);

/** The height above the first row: the title, and the bar of an editable list. */
double rubraview_listwin_head_height(const rubraview_listwin_t *lw);

/** The resize button, left of Close (empty unless editable). */
rubraview_rect_t rubraview_listwin_resize_rect(const rubraview_listwin_t *lw);

/** One of the bar's four buttons (DEL, SAVE, LOAD, PATHS), left to right; empty unless editable. */
rubraview_rect_t rubraview_listwin_bar_rect(const rubraview_listwin_t *lw, rubraview_listwin_part_t part);

/** One of a row's four buttons (ROW_CHECK, ROW_REMOVE, ROW_UP, ROW_DOWN); empty unless editable and in view. */
rubraview_rect_t rubraview_listwin_row_part_rect(const rubraview_listwin_t *lw, size_t index, size_t count,
                                                 rubraview_listwin_part_t part);

/** Where a row's text begins: after its buttons in an editable list. */
double rubraview_listwin_row_text_x(const rubraview_listwin_t *lw);

/**
 * The place in the list a thing let go at `py` goes to: before the row
 * whose upper half it is over, after the one whose lower half — 0 to
 * `count`, within the rows in view.
 */
size_t rubraview_listwin_insert_index(const rubraview_listwin_t *lw, double py, size_t count);

/**
 * Sizing by the resize button, at the top right: the button stays under
 * the pointer — the top and the right edge follow it, the bottom and the
 * left stay (owner, 2026-10-14) — inside the view, never too small.
 */
void rubraview_listwin_resize_begin(rubraview_listwin_t *lw, double px, double py);
void rubraview_listwin_resize_to(rubraview_listwin_t *lw, double px, double py, double view_w, double view_h);

/**
 * The order of a list after the items marked in `moving` are taken out and
 * put, in their own order, before what was item `to` (0 to `count`; `count`
 * is the end). `order[i]` is the old index of the item now at `i`. False
 * when nothing moves.
 */
bool rubraview_list_move(size_t *order, size_t count, const bool *moving, size_t to);

/** How many rows fit under the title. */
size_t rubraview_listwin_rows_visible(const rubraview_listwin_t *lw);

/** The rectangle of the file `index` (it must be in view; empty otherwise). */
rubraview_rect_t rubraview_listwin_row_rect(const rubraview_listwin_t *lw, size_t index, size_t count);

/** What is under the point; `out_index` gets the file of a ROW. */
rubraview_listwin_part_t rubraview_listwin_hit(const rubraview_listwin_t *lw, double px, double py, size_t count,
                                               size_t *out_index);

/** Scroll by `rows` (negative: up), kept within the list. */
void rubraview_listwin_scroll(rubraview_listwin_t *lw, long rows, size_t count);

/** Scroll the least that brings `index` into view. */
void rubraview_listwin_reveal(rubraview_listwin_t *lw, size_t index, size_t count);

/** Dragging by the title: begin at the pointer, move with it, kept inside the view. */
void rubraview_listwin_drag_begin(rubraview_listwin_t *lw, double px, double py);
void rubraview_listwin_drag_to(rubraview_listwin_t *lw, double px, double py, double view_w, double view_h);

/* ---- the file list window (owner, 2026-10-08) ----
   A window of its own beside the viewer: the files of the archive (or the
   folder) in a list on the left, the picture of the one pointed at on the
   right. Its rows are a rubraview_listwin_t laid over the window's left
   part; these are the two sums it needs besides. */

/**
 * The list's place in a `client_w` x `client_h` window at `dpi_scale`: the
 * left part, 42 % of the width but no narrower than ten rows are tall and
 * no wider than twenty; all of a window too narrow for both. Sets `lw` up
 * with a header row; keeps its scroll position. Returns the preview's
 * rectangle (empty when there is no room for one).
 */
rubraview_rect_t rubraview_filewin_layout(rubraview_listwin_t *lw, double client_w, double client_h, double dpi_scale);

/**
 * The row a key moves the selection to: `delta` rows from `selected`, or
 * from `current` (the file on screen) when nothing is selected yet, kept
 * within the list. SIZE_MAX when the list is empty.
 */
size_t rubraview_filewin_step(size_t selected, size_t current, long delta, size_t count);

/**
 * A `w` x `h` picture fitted into `box` with `margin` around it, centred,
 * its shape kept; a picture smaller than the room is not enlarged.
 */
rubraview_rect_t rubraview_filewin_fit(rubraview_rect_t box, double w, double h, double margin);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_UI_LISTWIN_H */
