#ifndef RUBRAVIEW_UI_SCROLLBAR_H
#define RUBRAVIEW_UI_SCROLLBAR_H

#include "rubraview/core.h"
#include "rubraview/ui_box.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A scroll bar that moves a list (RV-170, owner 2026-10-25: "클릭을 하거나 스크롤바를 드래그해도 목록에서 이동
 * 가능하게"; "위아래 이동 버튼은 아래쪽에 한곳에 몰아서"). The bar is a strip at the list's right edge: the track
 * above, and at its foot the up and the down button, one over the other. The thumb is dragged; a press on the track
 * beside it moves a screenful toward the press, a button one step; both go on while held, until the thumb reaches
 * the pointer or the pointer leaves the button. Geometry and arithmetic only: each window paints its own and says
 * what its list is.
 */

typedef enum rubraview_scrollbar_part {
    RUBRAVIEW_SCROLLBAR_NONE = 0,
    RUBRAVIEW_SCROLLBAR_THUMB,
    RUBRAVIEW_SCROLLBAR_BEFORE,   /* the track above the thumb */
    RUBRAVIEW_SCROLLBAR_AFTER,    /* the track below it */
    RUBRAVIEW_SCROLLBAR_UP,       /* the upper of the two buttons at the foot */
    RUBRAVIEW_SCROLLBAR_DOWN,
} rubraview_scrollbar_part_t;

/* A list as its bar sees it, in the list's own unit (rows, lines or pixels). */
typedef struct rubraview_scrollbar {
    rubraview_rect_t area;   /* the whole strip: the track, then the two buttons */
    double button;           /* each button's height */
    double total;            /* how much there is */
    double shown;            /* how much the window shows */
    double first;            /* where the window's top is */
    double step;             /* what a button moves */
    double min_thumb;        /* the thumb is never shorter */
} rubraview_scrollbar_t;

/* What a press holds. */
typedef struct rubraview_scrollbar_hold {
    rubraview_scrollbar_part_t part;   /* NONE: nothing */
    double grab;                       /* THUMB: the pointer below the thumb's top */
    double next;                       /* the track or a button: when it moves again */
} rubraview_scrollbar_hold_t;

#define RUBRAVIEW_SCROLLBAR_DELAY  0.40   /* s before a held press goes on */
#define RUBRAVIEW_SCROLLBAR_REPEAT 0.05   /* s between its moves after that */

/** True when there is more than the window shows (and so a bar). */
bool rubraview_scrollbar_needed(const rubraview_scrollbar_t *sb);
/** The largest `first`: what is left once the window shows the end. */
double rubraview_scrollbar_max_first(const rubraview_scrollbar_t *sb);
/** `first` kept within 0 .. max. */
double rubraview_scrollbar_clamp(const rubraview_scrollbar_t *sb, double first);

rubraview_rect_t rubraview_scrollbar_track(const rubraview_scrollbar_t *sb);
rubraview_rect_t rubraview_scrollbar_thumb(const rubraview_scrollbar_t *sb);
rubraview_rect_t rubraview_scrollbar_up(const rubraview_scrollbar_t *sb);
rubraview_rect_t rubraview_scrollbar_down(const rubraview_scrollbar_t *sb);

/** The part under (x, y); NONE outside the strip or when no bar is needed. */
rubraview_scrollbar_part_t rubraview_scrollbar_hit(const rubraview_scrollbar_t *sb, double x, double y);

/** The `first` that puts the thumb's top at `top`. */
double rubraview_scrollbar_first_for_top(const rubraview_scrollbar_t *sb, double top);

/** A press at (x, y) at time `now`: `hold` takes what was pressed, and the result is the new `first` (the same for
 *  the thumb, a screenful toward the press on the track, a step for a button). NONE in `hold->part`: not ours. */
double rubraview_scrollbar_press(const rubraview_scrollbar_t *sb, rubraview_scrollbar_hold_t *hold,
                                 double x, double y, double now);

/** While held, each pass, the pointer at (x, y): the thumb follows the pointer; the track and the buttons move again
 *  when due, if the pointer is still over what was pressed. The result is the new `first`. */
double rubraview_scrollbar_held(const rubraview_scrollbar_t *sb, rubraview_scrollbar_hold_t *hold,
                                double x, double y, double now);

#ifdef __cplusplus
}
#endif

#endif
