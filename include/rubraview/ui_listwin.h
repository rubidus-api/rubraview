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
} rubraview_listwin_t;

rubraview_listwin_t rubraview_listwin_create(double dpi_scale);

/**
 * Keep it inside a `view_w` x `view_h` client area; the first time, put it
 * at the right, a little below the top, a third of the view wide (within
 * reason) and three quarters of it tall.
 */
void rubraview_listwin_place(rubraview_listwin_t *lw, double view_w, double view_h);

rubraview_rect_t rubraview_listwin_rect(const rubraview_listwin_t *lw);
rubraview_rect_t rubraview_listwin_close_rect(const rubraview_listwin_t *lw);

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

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_UI_LISTWIN_H */
