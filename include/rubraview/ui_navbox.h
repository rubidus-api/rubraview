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
 * 플로팅 박스로"): the navigator, a wide box at the pointer once it rests in the middle of the window (the middle 25 %
 * of its width and of its height), over a picture or a book. On top a page bar and the X; under it four pairs, left
 * the one before and right the one after whatever the reading direction: the page (a spread in two pages), the
 * picture (one file, always), the first and the last, the archive or folder beside. Geometry and the actions only:
 * the viewer paints.
 */

typedef enum rubraview_navbox_part {
    RUBRAVIEW_NAVBOX_NONE = -1,
    RUBRAVIEW_NAVBOX_BAR = 0,
    RUBRAVIEW_NAVBOX_PAGE_PREV, RUBRAVIEW_NAVBOX_PAGE_NEXT,
    RUBRAVIEW_NAVBOX_PICTURE_PREV, RUBRAVIEW_NAVBOX_PICTURE_NEXT,
    RUBRAVIEW_NAVBOX_FIRST, RUBRAVIEW_NAVBOX_LAST,
    RUBRAVIEW_NAVBOX_BOOK_PREV, RUBRAVIEW_NAVBOX_BOOK_NEXT,
    RUBRAVIEW_NAVBOX_CLOSE,
    RUBRAVIEW_NAVBOX_PARTS,
} rubraview_navbox_part_t;

#define RUBRAVIEW_NAVBOX_REST  0.3   /* seconds the pointer rests in the middle before the box shows */
#define RUBRAVIEW_NAVBOX_LINGER 0.8  /* seconds it stays once the pointer has left it and the middle */

typedef struct rubraview_navbox {
    rubraview_rect_t box;
    double pad, button, button_h, bar_h, gap, label_h;
} rubraview_navbox_t;

/** The middle of a `view_w` x `view_h` client area: a quarter of its width and of its height, centred. */
rubraview_rect_t rubraview_navbox_middle(double view_w, double view_h);

/** The box centred on the point (px, py), kept inside the client area. */
rubraview_navbox_t rubraview_navbox_place(double px, double py, double view_w, double view_h, double dpi_scale);

rubraview_rect_t rubraview_navbox_part_rect(const rubraview_navbox_t *nav, rubraview_navbox_part_t part);

/** The part under the point; NONE outside the box or between its parts. */
rubraview_navbox_part_t rubraview_navbox_hit(const rubraview_navbox_t *nav, double px, double py);

/** The action a button runs ("" for the bar); the X is `quit`. */
const char *rubraview_navbox_action(rubraview_navbox_part_t part);

/** Under each pair, its words: 0 the page, 1 the picture, 2 first and last, 3 the archive or folder. */
const char *rubraview_navbox_pair_caption(int32_t pair);

/**
 * Whether the box is up after this pass. `shown`: up before it; `in_middle`, `in_box`: where the pointer is;
 * `rested`: seconds the pointer has been in the middle without leaving; `away`: seconds since it was last in the
 * box or the middle; `allowed`: a picture or a book, nothing held or asked.
 */
bool rubraview_navbox_keep(bool shown, bool allowed, bool in_middle, bool in_box, double rested, double away);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_UI_NAVBOX_H */
