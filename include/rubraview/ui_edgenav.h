#ifndef RUBRAVIEW_UI_EDGENAV_H
#define RUBRAVIEW_UI_EDGENAV_H

#include "rubraview/core.h"
#include "rubraview/ui_box.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The edge buttons (owner, 2026-09-29): at each side of the window, shown while the pointer is near
 * that side. RV-206 (owner, 2026-10-31: "세로로 길게 호버링되어 나열 ... 호버링 뜨기 전에 클릭하면 그냥
 * 이전/다음 버튼"): a tall column of four, shown once the pointer has stayed at the side for
 * RUBRAVIEW_EDGE_COLUMN_DELAY; a press there before it shows is plain previous / next. For pictures
 * the left column is a page back / ten back / the first page / the folder or archive before, the right
 * the same forward; for a film or music, 5 s / 5 % of its length / the file before / the folder or
 * archive before, and forward. Top to bottom in that order. Geometry and the actions only: the
 * viewer paints.
 */

typedef enum rubraview_edge_side {
    RUBRAVIEW_EDGE_NONE = -1,
    RUBRAVIEW_EDGE_LEFT = 0,
    RUBRAVIEW_EDGE_RIGHT = 1,
} rubraview_edge_side_t;

#define RUBRAVIEW_EDGE_SLOTS 4   /* RV-179: the folder or archive beside, under the three */

typedef struct rubraview_edge_hit {
    rubraview_edge_side_t side;
    int32_t slot;          /* 0 top .. 2 bottom; -1 with no side */
} rubraview_edge_hit_t;

#define RUBRAVIEW_EDGE_COLUMN_DELAY 0.4   /* RV-206: seconds at a side before its column shows (the owner's choice) */

typedef struct rubraview_edgenav {
    double button;         /* a button's width, scaled by DPI; the column's height is shared by the four */
    double gap;            /* between the buttons, and from the window's edge */
    double reach;          /* how far in from the edge the pointer reveals a stack */
} rubraview_edgenav_t;

rubraview_edgenav_t rubraview_edgenav_create(double dpi_scale);

/** One button's rectangle in a `view_w` x `view_h` client area. */
rubraview_rect_t rubraview_edgenav_rect(const rubraview_edgenav_t *nav, rubraview_edge_side_t side, int32_t slot,
                                        double view_w, double view_h);

/**
 * Which stack the pointer reveals: within `reach` of that side, and
 * vertically no further from the stack than one button. NONE elsewhere,
 * and in a window too narrow for both stacks and a picture between them.
 */
rubraview_edge_side_t rubraview_edgenav_revealed(const rubraview_edgenav_t *nav, double px, double py,
                                                 double view_w, double view_h);

/** The button under the point, among the revealed stack's. */
rubraview_edge_hit_t rubraview_edgenav_hit(const rubraview_edgenav_t *nav, double px, double py,
                                           double view_w, double view_h);

/** RV-206: what a press at a side does before its column shows: the page or file before or after. */
const char *rubraview_edgenav_plain_action(rubraview_edge_side_t side);

/** The action a button runs: `media` when a film or music is on screen. */
const char *rubraview_edgenav_action(rubraview_edge_side_t side, int32_t slot, bool media);

/** A short caption for the button, shown beside it while hovered. */
const char *rubraview_edgenav_caption(rubraview_edge_side_t side, int32_t slot, bool media);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_UI_EDGENAV_H */
