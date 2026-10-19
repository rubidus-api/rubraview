#ifndef RUBRAVIEW_UI_CHOOSER_H
#define RUBRAVIEW_UI_CHOOSER_H

#include "rubraview/core.h"
#include "rubraview/ui_box.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 1.24.0 (owner, 2026-10-19: "여러개 중에 하나를 선택해야 되는 기능이라면 누를
 * 때마다 하나씩 옮겨가는 것이 아니라 추가로 하위요소 팝업창이 떠서 그 안에서
 * 선택"): the list a one-of-many choice opens — a toolbox tile, a panel's
 * choice, a setting, the open-file screen's sorting. A title row, then a row
 * a choice, the one in force marked. Rows are tall for a finger on the
 * picture, a line of text in the settings window; the caller says which by
 * the row height. What is here is where it stands, which row a point means
 * and how far it scrolls; the viewer paints it and does the choice.
 */

typedef struct rubraview_chooser_layout {
    rubraview_rect_t frame;   /* the whole list, its title row first */
    double  row_h;            /* the title row and each choice */
    size_t  rows;             /* the choices */
    size_t  shown;            /* how many fit at once */
    size_t  first;            /* the first one shown (scrolled) */
} rubraview_chooser_layout_t;

/**
 * Where the list goes: its top left at (`at_x`, `at_y`) — under what was
 * pressed — or, when it would run past the window's bottom, its bottom at
 * `above_y` (over what was pressed); pulled inside the window either way.
 * Taller than the window, it shows what fits and scrolls; `current` is
 * brought into view.
 */
rubraview_chooser_layout_t rubraview_chooser_place(double win_w, double win_h, size_t rows, double row_h, double width,
                                                   double at_x, double at_y, double above_y, size_t current);

/** The rectangle of choice `index` (0..rows-1), or an empty one when it is scrolled away. */
rubraview_rect_t rubraview_chooser_row_rect(const rubraview_chooser_layout_t *layout, size_t index);

/** The choice under a point, -1 for the title row, -2 outside the list. */
int32_t rubraview_chooser_row_at(const rubraview_chooser_layout_t *layout, double x, double y);

/** Scrolled by `rows` (down when positive), kept inside what there is. */
void rubraview_chooser_scroll(rubraview_chooser_layout_t *layout, int32_t rows);

/** Up or down from `at` (-1: none yet), wrapping round. */
int32_t rubraview_chooser_step(const rubraview_chooser_layout_t *layout, int32_t at, int32_t direction);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_UI_CHOOSER_H */
