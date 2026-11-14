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
 * archive before, and forward. Top to bottom in that order. RV-285 (owner, 2026-11-14: "창의 양쪽 끝 가로 10% ...
 * 그냥 누르면 최소 이동(5초 이동 혹은 한페이지 넘김)이 있고 호버링 후에 누르거나 손가락으로 누른 채 드래그하면 버튼을 골라
 * 선택할 수 있습니다"): each side is 10 % of the width; a plain press is the smallest move (a page turn, 5 s for a film or
 * music); a press with the column up, or a press dragged along it (a finger), runs the button under it when let go.
 * Geometry and the actions only: the viewer paints.
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
    double reach;          /* RV-285: the least a side is; it is 10 % of the window's width when that is more */
} rubraview_edgenav_t;

#define RUBRAVIEW_EDGE_SIDE_SHARE 0.10   /* RV-285: a side, of the window's width */

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

/** RV-285: how far in from the edge a side reaches in a window `view_w` wide. */
double rubraview_edgenav_reach(const rubraview_edgenav_t *nav, double view_w);

/** RV-206, RV-285: what a plain press at a side does: a page turn back or on; 5 s for a film or music. */
const char *rubraview_edgenav_plain_action(rubraview_edge_side_t side, bool media);

/*
 * RV-285: a press at a side, decided when it is let go. Pressed with the column up (the pointer rested there), or
 * dragged along it past `slop` (a finger), it runs the button under the point of release, nothing off the column;
 * pressed and let go before the column showed, it is the plain move.
 */
typedef struct rubraview_edge_press {
    bool on;                      /* a press at a side is held (zeroed: none) */
    rubraview_edge_side_t side;
    bool column;                  /* the column was up when pressed */
    bool dragged;
    double x0, y0;
} rubraview_edge_press_t;

#define RUBRAVIEW_EDGE_REPEAT 1.0   /* RV-285: seconds after a plain press in which the same side's press is plain again */

/**
 * RV-285 (owner, 2026-11-15: "추천대로"): a press at `side` at `now` repeats the plain press made there at `last_at`
 * (`last_side`), within RUBRAVIEW_EDGE_REPEAT: it is the smallest move again, though the column has come up meanwhile.
 */
bool rubraview_edge_press_repeats(rubraview_edge_side_t last_side, double last_at, rubraview_edge_side_t side, double now);

void rubraview_edge_press_begin(rubraview_edge_press_t *press, rubraview_edge_side_t side, bool column, double x, double y);
void rubraview_edge_press_move(rubraview_edge_press_t *press, double x, double y, double slop);

/** While pressed: whether the column shows (up at the press, or dragged). */
bool rubraview_edge_press_shows(const rubraview_edge_press_t *press);

/** The button of the pressed side under the point; -1 for none. */
int32_t rubraview_edge_press_slot(const rubraview_edge_press_t *press, const rubraview_edgenav_t *nav, double x, double y,
                                  double view_w, double view_h);

/**
 * Let go at (x, y): the action to run ("" for none), and the press ended. NULL when there was no press at a side.
 */
const char *rubraview_edge_press_release(rubraview_edge_press_t *press, const rubraview_edgenav_t *nav, double x, double y,
                                         double view_w, double view_h, bool media);

/** The action a button runs: `media` when a film or music is on screen. */
const char *rubraview_edgenav_action(rubraview_edge_side_t side, int32_t slot, bool media);

/** A short caption for the button, shown beside it while hovered. */
const char *rubraview_edgenav_caption(rubraview_edge_side_t side, int32_t slot, bool media);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_UI_EDGENAV_H */
