#ifndef RUBRAVIEW_UI_NAVBOX_H
#define RUBRAVIEW_UI_NAVBOX_H

#include "rubraview/core.h"
#include "rubraview/ui_box.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-277 (owner, 2026-11-13: "창길이 기준 가로세로 가운데 25% 부분 영역에 호버링하면 네비바가 마우스 위치에 호버링되어
 * 플로팅 박스로"): the navigator, a wide box at the pointer in the middle of the window (the middle 25 % of its width and
 * of its height). RV-285 (owner, 2026-11-14: "호버링하면 투명한 플로팅 박스가 떠서 클릭 영역임을 알립니다. 그리고 클릭하면 각
 * 모드에 맞는 네비 바가 뜹니다"): the pointer over the middle shows a see-through hint there, and a click opens the box;
 * it closes by its X, Esc, a click outside it, or the pointer away from it and the middle for RUBRAVIEW_NAVBOX_LINGER.
 * Over a picture or a book: on top a page bar and the X; under it four pairs, left the one before and right the one
 * after whatever the reading direction: the page turn (a spread in two pages), the file turn (one file, always), the
 * first and the last, the archive or folder beside. Over a film or music: on top play / pause, a time bar and the X;
 * the pairs 5 s, 5 % of its length, the file, the folder or archive. Geometry and the actions only: the viewer paints.
 */

typedef enum rubraview_navbox_part {
    RUBRAVIEW_NAVBOX_NONE = -1,
    RUBRAVIEW_NAVBOX_BAR = 0,
    RUBRAVIEW_NAVBOX_PAGE_PREV, RUBRAVIEW_NAVBOX_PAGE_NEXT,
    RUBRAVIEW_NAVBOX_PICTURE_PREV, RUBRAVIEW_NAVBOX_PICTURE_NEXT,
    RUBRAVIEW_NAVBOX_FIRST, RUBRAVIEW_NAVBOX_LAST,
    RUBRAVIEW_NAVBOX_BOOK_PREV, RUBRAVIEW_NAVBOX_BOOK_NEXT,
    RUBRAVIEW_NAVBOX_CLOSE,
    RUBRAVIEW_NAVBOX_PLAY,          /* RV-285: a film or music only, left of its time bar */
    RUBRAVIEW_NAVBOX_PARTS,
} rubraview_navbox_part_t;

#define RUBRAVIEW_NAVBOX_LINGER 2.0  /* RV-285: seconds it stays once the pointer has left it and the middle */
#define RUBRAVIEW_NAVBOX_GRACE  0.35 /* RV-285: seconds after it opens in which a press on it runs nothing (a double click) */

typedef struct rubraview_navbox {
    rubraview_rect_t box;
    double pad, button, button_h, bar_h, gap, label_h;
    bool media;            /* RV-285: a film or music's set */
} rubraview_navbox_t;

/** The middle of a `view_w` x `view_h` client area: a quarter of its width and of its height, centred. */
rubraview_rect_t rubraview_navbox_middle(double view_w, double view_h);

/** The box centred on the point (px, py), kept inside the client area; `media`: a film or music's set. */
rubraview_navbox_t rubraview_navbox_place(double px, double py, double view_w, double view_h, double dpi_scale, bool media);

rubraview_rect_t rubraview_navbox_part_rect(const rubraview_navbox_t *nav, rubraview_navbox_part_t part);

/** The part under the point; NONE outside the box or between its parts. */
rubraview_navbox_part_t rubraview_navbox_hit(const rubraview_navbox_t *nav, double px, double py);

/** The action a button runs ("" for the bar and the X, which the viewer handles: the X closes the box). */
const char *rubraview_navbox_action(rubraview_navbox_part_t part, bool media);

/**
 * Under each pair, its words. A picture: 0 the page turn, 1 the file turn, 2 first and last, 3 the archive or folder;
 * a film or music: 0 5 s, 1 5 %, 2 the file, 3 the folder or archive.
 */
const char *rubraview_navbox_pair_caption(int32_t pair, bool media);

/**
 * RV-285: whether the box opened by a click stays up after this pass. `allowed`: something to navigate, nothing held
 * or asked; `near`: the pointer in the box or the middle; `away`: seconds since it was last near.
 */
bool rubraview_navbox_keep(bool shown, bool allowed, bool near, double away);

/** RV-285: whether the middle's see-through hint shows: allowed, the box not up, the pointer there, no button held. */
bool rubraview_navbox_hint(bool allowed, bool shown, bool in_middle, bool pressed);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_UI_NAVBOX_H */
