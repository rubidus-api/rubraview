#ifndef RUBRAVIEW_UI_SLIDEPOP_H
#define RUBRAVIEW_UI_SLIDEPOP_H

#include "rubraview/core.h"
#include "rubraview/ui_box.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The slide show's settings, a right click away while it runs (owner,
 * 2026-10-14, RV-116): the delay — a field to type it in, − and + beside
 * it, half a second a press — how one slide gives way to the next, what
 * the end does, and pause, stop, full screen and close. What is here is
 * where each part stands and which part a point is on; the viewer paints
 * and acts. Sizes are at a scale of 1 and multiplied by `scale`.
 */

typedef enum rubraview_slidepop_part {
    RUBRAVIEW_SLIDEPOP_NONE = -1,
    RUBRAVIEW_SLIDEPOP_DELAY_DOWN = 0,
    RUBRAVIEW_SLIDEPOP_DELAY_FIELD,
    RUBRAVIEW_SLIDEPOP_DELAY_UP,
    RUBRAVIEW_SLIDEPOP_FX_CUT,         /* the four in Settings › Viewer › Turning a page's order */
    RUBRAVIEW_SLIDEPOP_FX_FADE,
    RUBRAVIEW_SLIDEPOP_FX_SLIDE,
    RUBRAVIEW_SLIDEPOP_FX_ZOOM,
    RUBRAVIEW_SLIDEPOP_LOOP,           /* round again after the last */
    RUBRAVIEW_SLIDEPOP_ONCE,           /* stop on the last */
    RUBRAVIEW_SLIDEPOP_PAUSE,
    RUBRAVIEW_SLIDEPOP_STOP,
    RUBRAVIEW_SLIDEPOP_FULL,
    RUBRAVIEW_SLIDEPOP_CLOSE,
    RUBRAVIEW_SLIDEPOP_PART_COUNT,
} rubraview_slidepop_part_t;

/* The labels in front of the three rows: "Delay", "Change", "At the end". */
typedef enum rubraview_slidepop_label {
    RUBRAVIEW_SLIDEPOP_LABEL_DELAY = 0,
    RUBRAVIEW_SLIDEPOP_LABEL_FX,
    RUBRAVIEW_SLIDEPOP_LABEL_END,
} rubraview_slidepop_label_t;

/** The window's place for a click at (x, y), as the right-click list's: there, turned left or up at the edges, inside the window. */
rubraview_rect_t rubraview_slidepop_frame(double window_width, double window_height, double x, double y, double scale);

/** The title's band at the top. */
rubraview_rect_t rubraview_slidepop_title(rubraview_rect_t frame, double scale);
rubraview_rect_t rubraview_slidepop_label(rubraview_rect_t frame, double scale, rubraview_slidepop_label_t label);
rubraview_rect_t rubraview_slidepop_part(rubraview_rect_t frame, double scale, rubraview_slidepop_part_t part);

/** The part under a point, RUBRAVIEW_SLIDEPOP_NONE when none (the window's own ground or outside it). */
rubraview_slidepop_part_t rubraview_slidepop_part_at(rubraview_rect_t frame, double scale, double x, double y);

/** − and +: half a second from the delay, to the next half second, no less than half a second. */
double rubraview_slidepop_step(double seconds, int direction);

#ifdef __cplusplus
}
#endif

#endif
