#ifndef RUBRAVIEW_SUBBOX_H
#define RUBRAVIEW_SUBBOX_H

#include "rubraview/ui_box.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The subtitle box (D-33, owner 2026-09-25): text subtitles are drawn in a
 * translucent box the reader can move and resize. A tap selects it; a
 * selected box shows a thick outline and four buttons at its top-right —
 * S (settings), M (move), R (resize), X (subtitles off). Moving and
 * resizing are drags that start on M or R.
 *
 * The box is kept as fractions of the window so a resize or fullscreen
 * keeps it where it was: its left edge, its bottom edge and its width. Its
 * height is not stored — it is two lines at the subtitle size, so the size
 * setting stays the one source of truth and an R drag writes it.
 */

typedef enum rubraview_subbox_part {
    RUBRAVIEW_SUBBOX_NONE = 0,
    RUBRAVIEW_SUBBOX_BODY,
    RUBRAVIEW_SUBBOX_SETTINGS,   /* S */
    RUBRAVIEW_SUBBOX_MOVE,       /* M */
    RUBRAVIEW_SUBBOX_RESIZE,     /* R */
    RUBRAVIEW_SUBBOX_CLOSE,      /* X */
} rubraview_subbox_part_t;

#define RUBRAVIEW_SUBBOX_BUTTONS 4

typedef struct rubraview_subbox {
    bool   placed;       /* false: the default place (bottom, 90% wide) */
    double left, bottom, width;   /* fractions of the window */
    bool   selected;

    rubraview_subbox_part_t dragging;   /* MOVE, RESIZE or NONE */
    double grab_x, grab_y;              /* the pointer's offset from the dragged corner */
} rubraview_subbox_t;

/* Where the box is, in pixels. `height` is the height its text needs;
   `default_rect` is where an unplaced box goes (its height is ignored). */
rubraview_rect_t rubraview_subbox_rect(const rubraview_subbox_t *box, double win_w, double win_h,
                                       double height, rubraview_rect_t default_rect);

/* The four buttons, S M R X from left to right, squares of `size` above
   the box's top-right corner — inside it when there is no room above. */
void rubraview_subbox_buttons(rubraview_rect_t rect, double size, double gap,
                              rubraview_rect_t out[RUBRAVIEW_SUBBOX_BUTTONS]);

/* What is under the pointer. The buttons count only while the box is
   selected; the body counts only while `body_live` (a subtitle is on
   screen, or the box is selected) — an empty box takes no taps. */
rubraview_subbox_part_t rubraview_subbox_hit(const rubraview_subbox_t *box, rubraview_rect_t rect,
                                             double button_size, double gap, bool body_live,
                                             double x, double y);

/* A drag starts on M or R; anything else returns false. */
bool rubraview_subbox_begin_drag(rubraview_subbox_t *box, rubraview_subbox_part_t part,
                                 rubraview_rect_t rect, double x, double y);

/*
 * The pointer moved during a drag. Moving keeps the box inside the
 * window; resizing holds the bottom-left corner and follows the top-right
 * one, with the width kept between `min_w` and the window and the height
 * between `min_h` and `max_h`. The height the drag asks for is returned
 * through `out_height` (the caller turns it into a subtitle size).
 */
void rubraview_subbox_drag(rubraview_subbox_t *box, rubraview_rect_t rect, double x, double y,
                           double win_w, double win_h, double min_w, double min_h, double max_h,
                           double *out_height);

void rubraview_subbox_end_drag(rubraview_subbox_t *box);

/*
 * The Sub tile: a tap turns the subtitles on or off, a double tap or a
 * held press opens the list of tracks. The tap is decided only when the
 * double-tap window has passed, so a double tap never flickers the
 * subtitles off and on first.
 */
typedef enum rubraview_tap_result {
    RUBRAVIEW_TAP_NOTHING = 0,
    RUBRAVIEW_TAP_SINGLE,
    RUBRAVIEW_TAP_CHOOSE,   /* double tap, or held */
} rubraview_tap_result_t;

#define RUBRAVIEW_TAP_DOUBLE_SECONDS 0.35
#define RUBRAVIEW_TAP_HOLD_SECONDS   0.5

typedef struct rubraview_tap {
    bool   down;
    bool   decided;          /* this press already produced CHOOSE */
    double pressed_at;
    bool   pending;          /* a released tap waiting out the double-tap window */
    double released_at;
} rubraview_tap_t;

rubraview_tap_result_t rubraview_tap_press(rubraview_tap_t *tap, double now);
rubraview_tap_result_t rubraview_tap_release(rubraview_tap_t *tap, double now);
rubraview_tap_result_t rubraview_tap_tick(rubraview_tap_t *tap, double now);
/* Something is still to be decided: the caller must keep checking. */
bool rubraview_tap_waiting(const rubraview_tap_t *tap);

/*
 * The list a double tap or a held press on the Sub tile opens: a title
 * row, then one row a choice ("Off", then each subtitle track). It is
 * centred on `anchor_x` with its bottom at `anchor_y` (above the tile that
 * opened it), and pulled inside the window.
 */
rubraview_rect_t rubraview_choice_list_rect(double win_w, double win_h, size_t rows, double row_h,
                                            double width, double anchor_x, double anchor_y);

/* Which choice is under the pointer: 0.. for the rows, -1 for the title
   row or outside the list. */
int32_t rubraview_choice_list_row_at(rubraview_rect_t list, double row_h, size_t rows, double x, double y);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_SUBBOX_H */
