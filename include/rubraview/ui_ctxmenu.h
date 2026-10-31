#ifndef RUBRAVIEW_UI_CTXMENU_H
#define RUBRAVIEW_UI_CTXMENU_H

#include "rubraview/core.h"
#include "rubraview/ui_box.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The right-click list (owner, 2026-10-14, RV-116): a column of rows at the
 * pointer, in groups with a little room and a line between two. Its rows
 * are the boxes document's `context` items for what is on screen
 * (`boxes_doc.h`); what is here is only where it stands and which row a
 * point or a key means. The viewer paints it and runs the rows.
 */

typedef struct rubraview_ctxmenu_metrics {
    double row_height;   /* one row */
    double gap;          /* the room between two groups (the line is drawn in it) */
    double pad;          /* above the first row and below the last */
    double width;
} rubraview_ctxmenu_metrics_t;

/**
 * Where the list stands for a click at (x, y): its top left there, as a
 * menu opens; moved to the left of the point when it would pass the
 * window's right edge, above it when it would pass the bottom; and kept
 * inside the window whatever it is (a window smaller than the list shows
 * its top). `groups[i]` is row i's group; rows are in order of group.
 */
rubraview_rect_t rubraview_ctxmenu_frame(double window_width, double window_height, double x, double y,
                                         const rubraview_ctxmenu_metrics_t *metrics, const int32_t *groups, size_t count);

/** Row `index`'s place inside `frame`. */
rubraview_rect_t rubraview_ctxmenu_row(rubraview_rect_t frame, const rubraview_ctxmenu_metrics_t *metrics,
                                       const int32_t *groups, size_t index);

/** The row under a point, or -1: outside the list, or in the room between two groups. */
int32_t rubraview_ctxmenu_row_at(rubraview_rect_t frame, const rubraview_ctxmenu_metrics_t *metrics,
                                 const int32_t *groups, size_t count, double x, double y);

/**
 * The arrow keys: the next row that can act after `from` (-1: none chosen
 * yet) going `direction` (+1 down, -1 up), round from the end to the
 * start; -1 when no row can act.
 */
int32_t rubraview_ctxmenu_step(const bool *enabled, size_t count, int32_t from, int direction);

/*
 * RV-205 (owner, 2026-10-31: "화면 우클릭 컨텍스트 메뉴의 모든 항목들이 타이틀바 좌측의 메뉴에도 ... 2열로 각 항목들
 * 가로로 길쭉한 버튼으로"): the title bar's ☰ as a grid of wide buttons in `columns` columns. Each group
 * starts a line of its own and fills it left to right; a room between two groups. The frame's top
 * left is (x, y); a window too small for it gets the grid's top left.
 */
typedef struct rubraview_gridmenu_metrics {
    double  cell_width, cell_height;
    double  gap;        /* between two groups' lines; also between two columns */
    double  pad;        /* inside the frame, all round */
    int32_t columns;
} rubraview_gridmenu_metrics_t;

rubraview_rect_t rubraview_gridmenu_frame(double x, double y, const rubraview_gridmenu_metrics_t *metrics,
                                          const int32_t *groups, size_t count);
/** Cell `index`'s place inside `frame`. */
rubraview_rect_t rubraview_gridmenu_cell(rubraview_rect_t frame, const rubraview_gridmenu_metrics_t *metrics,
                                         const int32_t *groups, size_t index);
/** The cell under a point, or -1. */
int32_t rubraview_gridmenu_at(rubraview_rect_t frame, const rubraview_gridmenu_metrics_t *metrics,
                              const int32_t *groups, size_t count, double x, double y);
/**
 * Up and Down: the nearest cell that can act in the same column above or below `from` (-1: the first
 * that can act); `from` when there is none that way.
 */
int32_t rubraview_gridmenu_step_vertical(rubraview_rect_t frame, const rubraview_gridmenu_metrics_t *metrics,
                                         const int32_t *groups, const bool *enabled, size_t count, int32_t from, int direction);

#ifdef __cplusplus
}
#endif

#endif
