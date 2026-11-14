#include <math.h>
#include "rubraview/ui_input.h"
#include "rubraview/lang.h"

rubraview_pointer_intent_t rubraview_pointer_click(const rubraview_pointer_context_t *ctx, double x, bool is_double_click) {
    (void)x;
    if (!ctx) return RUBRAVIEW_INTENT_NONE;

    if (is_double_click) return RUBRAVIEW_INTENT_CENTER_PICTURE;   /* 1.25.0: the picture back in the middle */

    /* Dragging a zoomed image pans it; the page-flip zones would fight
       with panning, so they only apply when the whole image is visible. */
    if (ctx->zoomed_in) return RUBRAVIEW_INTENT_PAN;

    /* RV-285 (owner, 2026-11-14: "그림과 책에서 가운데와 양쪽 10% 밖을 클릭하면 아무것도 하지 않습니다"): the sides' 10 %
       turn pages and the middle opens the navigator, both the viewer's before this; anywhere else a click does nothing
       (the 30 % zones of 1.25.0 are gone). */
    return RUBRAVIEW_INTENT_NONE;
}

rubraview_pointer_intent_t rubraview_pointer_middle_click(const rubraview_pointer_context_t *ctx) {
    (void)ctx;
    return RUBRAVIEW_INTENT_TOGGLE_ACTUAL_SIZE;
}

rubraview_pointer_intent_t rubraview_pointer_right_click(const rubraview_pointer_context_t *ctx) {
    (void)ctx;
    return RUBRAVIEW_INTENT_CONTEXT_MENU;
}

rubraview_pointer_intent_t rubraview_pointer_wheel(const rubraview_pointer_context_t *ctx, double wheel_delta, uint32_t modifiers) {
    if (!ctx || wheel_delta == 0.0) return RUBRAVIEW_INTENT_NONE;
    bool forward = wheel_delta > 0.0; /* away from the user */

    if (modifiers & RUBRAVIEW_MOD_CTRL) {
        return forward != ctx->wheel_down_enlarges ? RUBRAVIEW_INTENT_ZOOM_IN : RUBRAVIEW_INTENT_ZOOM_OUT;
    }
    if (modifiers & RUBRAVIEW_MOD_SHIFT) {
        return forward ? RUBRAVIEW_INTENT_SKIP_BACKWARD : RUBRAVIEW_INTENT_SKIP_FORWARD;
    }
    if (ctx->scrollable_vertically) {
        return RUBRAVIEW_INTENT_SCROLL_VERTICAL;
    }

    /* On a picture and in a book the wheel turns files and pages, away from you back, towards you on (RV-276, owner
     * 2026-11-13: "사진과 만화만 휠로 다음/이전 파일"; it zoomed from 2026-10-20, Ctrl+wheel still does); a picture
     * zoomed in moves up and down first. A film or a song has its own wheel, the volume. Which way is next is a
     * setting (owner, 2026-11-13: "휠 올리기와 휠 내리기 중 어떤 것으로 할지 설정하는 옵션"). */
    if (ctx->wheel_turns) {
        if (ctx->zoomed_in) return RUBRAVIEW_INTENT_SCROLL_VERTICAL;
        return forward != ctx->wheel_up_next ? RUBRAVIEW_INTENT_PREV_PAGE : RUBRAVIEW_INTENT_NEXT_PAGE;
    }
    return RUBRAVIEW_INTENT_NONE;
}

rubraview_pointer_intent_t rubraview_pointer_side_button(bool is_forward_button) {
    return is_forward_button ? RUBRAVIEW_INTENT_NEXT_PAGE : RUBRAVIEW_INTENT_PREV_PAGE;
}

rubraview_pointer_intent_t rubraview_pointer_swipe(const rubraview_pointer_context_t *ctx, double dx, double threshold) {
    if (!ctx) return RUBRAVIEW_INTENT_NONE;
    if (threshold <= 0.0) threshold = 48.0;

    double distance = dx < 0.0 ? -dx : dx;
    if (distance < threshold) return RUBRAVIEW_INTENT_NONE; /* a tap or a pan, not a swipe */

    /* Swiping left drags the current page away to reveal the next one. */
    bool swipe_left = dx < 0.0;
    bool rtl = (ctx->reading_direction == RUBRAVIEW_READING_RTL);
    bool next = rtl ? !swipe_left : swipe_left;
    return next ? RUBRAVIEW_INTENT_NEXT_PAGE : RUBRAVIEW_INTENT_PREV_PAGE;
}

double rubraview_zoom_step_percent(double percent, double step, bool larger) {
    if (!(step > 0.0)) step = 10.0;
    if (!(percent > 0.0)) percent = step;
    double at = percent / step;
    double next = larger ? (floor(at + 1e-6) + 1.0) * step : (ceil(at - 1e-6) - 1.0) * step;
    if (next < step) next = step;
    if (next > 6400.0) next = 6400.0;
    return next;
}

double rubraview_zoom_cycle_percent(double percent, bool at_real_size) {
    if (at_real_size || !(percent > 0.0)) return 20.0;
    double next = (floor(percent / 20.0 + 1e-6) + 1.0) * 20.0;
    return next > 200.0 + 1e-6 ? 0.0 : next;
}

rubraview_fit_mode_t rubraview_fit_cycle_next(rubraview_fit_mode_t fit) {
    switch (fit) {
        case RUBRAVIEW_FIT_WINDOW: return RUBRAVIEW_FIT_WIDTH;
        case RUBRAVIEW_FIT_WIDTH:  return RUBRAVIEW_FIT_HEIGHT;
        case RUBRAVIEW_FIT_HEIGHT: return RUBRAVIEW_FIT_SMART;
        case RUBRAVIEW_FIT_SMART:  return RUBRAVIEW_FIT_STRETCH;
        default:                   return RUBRAVIEW_FIT_WINDOW;
    }
}

const char *rubraview_fit_tile_name(rubraview_fit_mode_t fit) {
    switch (fit) {
        case RUBRAVIEW_FIT_WIDTH:       return T("Fit W");
        case RUBRAVIEW_FIT_HEIGHT:      return T("Fit H");
        case RUBRAVIEW_FIT_SMART:       return T("Smart");
        case RUBRAVIEW_FIT_STRETCH:     return T("Stretch");
        case RUBRAVIEW_FIT_ACTUAL_SIZE: return T("Real\nsize");
        default:                        return T("Fit");
    }
}

const char *rubraview_fit_tile_hint(rubraview_fit_mode_t fit) {
    /* One string a fit, so a change of fit is a change of hint (owner, 2026-10-15). */
    switch (fit) {
        case RUBRAVIEW_FIT_WIDTH:       return T("Fit: to the window's width. Click: fit to its height");
        case RUBRAVIEW_FIT_HEIGHT:      return T("Fit: to the window's height. Click: smart fit");
        case RUBRAVIEW_FIT_SMART:       return T("Fit: smart (a large picture made smaller, a small one left at 100%). Click: stretch to fill");
        case RUBRAVIEW_FIT_STRETCH:     return T("Fit: stretched to fill the window. Click: fit to the window");
        case RUBRAVIEW_FIT_ACTUAL_SIZE: return T("Fit: none, real size. Click: fit to the window");
        default:                        return T("Fit: the whole picture in the window. Click: fit to its width");
    }
}

double rubraview_pan_key_delta(double view_extent, double start, double extent, int direction, double step) {
    if (!(extent > view_extent) || !(step > 0.0) || direction == 0) return 0.0;
    double room = direction > 0 ? start + extent - view_extent   /* hidden past the far edge */
                                : -start;                         /* hidden before the near one */
    if (!(room > 0.0)) return 0.0;
    double by = room < step ? room : step;
    return direction > 0 ? -by : by;
}

double rubraview_pan_key_delta_small(double view_extent, double start, double extent, int direction, double step) {
    if (!(extent < view_extent) || !(step > 0.0) || direction == 0) return 0.0;
    double room = direction > 0 ? view_extent - (start + extent)   /* free space past it */
                                : start;                           /* free space before it */
    if (!(room > 0.0)) return 0.0;
    double by = room < step ? room : step;
    return direction > 0 ? by : -by;
}
