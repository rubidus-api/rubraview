#ifndef RUBRAVIEW_UI_INPUT_H
#define RUBRAVIEW_UI_INPUT_H

#include "rubraview/core.h"
#include "rubraview/keymap.h"
#include "rubraview/layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Mouse, wheel and touch semantics (RFC-0001 §3.7.3, §3.7.4, §3.6.5)
 * expressed as pure classification: given a click position and the
 * current viewing state, which intent did the reader express? The Win32
 * backend supplies the raw coordinates; this decides what they mean, so
 * the rules are testable without a window.
 */

/** §3.7.3 point 1: the canvas splits into three click zones. */
typedef enum rubraview_click_zone {
    RUBRAVIEW_ZONE_LEFT = 0,  /* leftmost 30% */
    RUBRAVIEW_ZONE_CENTER,    /* middle 40% */
    RUBRAVIEW_ZONE_RIGHT,     /* rightmost 30% */
} rubraview_click_zone_t;

rubraview_click_zone_t rubraview_click_zone_at(double x, double window_width);

/** The intent a click, wheel notch or side button resolves to. */
typedef enum rubraview_pointer_intent {
    RUBRAVIEW_INTENT_NONE = 0,
    RUBRAVIEW_INTENT_NEXT_PAGE,
    RUBRAVIEW_INTENT_PREV_PAGE,
    RUBRAVIEW_INTENT_TOGGLE_OVERLAY,   /* centre click / single tap: show the anchors and OSD */
    RUBRAVIEW_INTENT_ZOOM_IN,
    RUBRAVIEW_INTENT_ZOOM_OUT,
    RUBRAVIEW_INTENT_PAN,              /* drag while zoomed in */
    RUBRAVIEW_INTENT_TOGGLE_FULLSCREEN,/* double click */
    RUBRAVIEW_INTENT_TOGGLE_ACTUAL_SIZE,/* middle click: 1:1 <-> fit window */
    RUBRAVIEW_INTENT_CONTEXT_MENU,     /* right click / long press */
    RUBRAVIEW_INTENT_SCROLL_VERTICAL,  /* wheel while the image overflows vertically */
    RUBRAVIEW_INTENT_SKIP_FORWARD,     /* Shift + wheel: 10-page skip */
    RUBRAVIEW_INTENT_SKIP_BACKWARD,
} rubraview_pointer_intent_t;

/**
 * The state a pointer decision depends on. `zoomed_in` means the image
 * is larger than the viewport, so dragging pans instead of flipping
 * pages; `scrollable_vertically` means the wheel scrolls (Fit to Width
 * on a webtoon) rather than changing page.
 */
typedef struct rubraview_pointer_context {
    double window_width;
    rubraview_reading_dir_t reading_direction;
    bool comic_mode;             /* §3.7.3: the click zones only flip pages in comic viewing */
    bool zoomed_in;
    bool scrollable_vertically;
} rubraview_pointer_context_t;

rubraview_pointer_intent_t rubraview_pointer_click(const rubraview_pointer_context_t *ctx, double x, bool is_double_click);
rubraview_pointer_intent_t rubraview_pointer_middle_click(const rubraview_pointer_context_t *ctx);
rubraview_pointer_intent_t rubraview_pointer_right_click(const rubraview_pointer_context_t *ctx);

/**
 * §3.7.3 point 5. `wheel_delta` is in notches, positive away from the
 * user. Ctrl gives cursor-centred zoom, Shift a 10-page skip, and a
 * plain notch either scrolls or flips depending on the context.
 */
rubraview_pointer_intent_t rubraview_pointer_wheel(const rubraview_pointer_context_t *ctx, double wheel_delta, uint32_t modifiers);

/** §3.7.3 point 6: the mouse's side buttons are previous/next page. */
rubraview_pointer_intent_t rubraview_pointer_side_button(bool is_forward_button);

/**
 * §3.7.4: a horizontal swipe flips a page once it passes a distance
 * threshold; anything shorter is a tap or a pan. `dx` is in pixels.
 */
rubraview_pointer_intent_t rubraview_pointer_swipe(const rubraview_pointer_context_t *ctx, double dx, double threshold);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_UI_INPUT_H */
