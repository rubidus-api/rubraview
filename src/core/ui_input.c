#include "rubraview/ui_input.h"

rubraview_click_zone_t rubraview_click_zone_at(double x, double window_width) {
    if (window_width <= 0.0) return RUBRAVIEW_ZONE_CENTER;
    double ratio = x / window_width;
    if (ratio < 0.30) return RUBRAVIEW_ZONE_LEFT;
    if (ratio >= 0.70) return RUBRAVIEW_ZONE_RIGHT;
    return RUBRAVIEW_ZONE_CENTER;
}

/* §3.7.3: the left zone is "previous" when reading left-to-right and
   "next" when reading right-to-left — the zones follow the page order,
   not the screen side. */
static rubraview_pointer_intent_t zone_to_page_intent(rubraview_click_zone_t zone, rubraview_reading_dir_t dir) {
    bool rtl = (dir == RUBRAVIEW_READING_RTL);
    switch (zone) {
        case RUBRAVIEW_ZONE_LEFT:
            return rtl ? RUBRAVIEW_INTENT_NEXT_PAGE : RUBRAVIEW_INTENT_PREV_PAGE;
        case RUBRAVIEW_ZONE_RIGHT:
            return rtl ? RUBRAVIEW_INTENT_PREV_PAGE : RUBRAVIEW_INTENT_NEXT_PAGE;
        case RUBRAVIEW_ZONE_CENTER:
        default:
            return RUBRAVIEW_INTENT_TOGGLE_OVERLAY;
    }
}

rubraview_pointer_intent_t rubraview_pointer_click(const rubraview_pointer_context_t *ctx, double x, bool is_double_click) {
    if (!ctx) return RUBRAVIEW_INTENT_NONE;

    if (is_double_click) return RUBRAVIEW_INTENT_TOGGLE_FULLSCREEN;

    /* Dragging a zoomed image pans it; the page-flip zones would fight
       with panning, so they only apply when the whole image is visible. */
    if (ctx->zoomed_in) return RUBRAVIEW_INTENT_PAN;

    if (!ctx->comic_mode) return RUBRAVIEW_INTENT_TOGGLE_OVERLAY;

    return zone_to_page_intent(rubraview_click_zone_at(x, ctx->window_width), ctx->reading_direction);
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
        return forward ? RUBRAVIEW_INTENT_ZOOM_IN : RUBRAVIEW_INTENT_ZOOM_OUT;
    }
    if (modifiers & RUBRAVIEW_MOD_SHIFT) {
        return forward ? RUBRAVIEW_INTENT_SKIP_BACKWARD : RUBRAVIEW_INTENT_SKIP_FORWARD;
    }
    if (ctx->scrollable_vertically) {
        return RUBRAVIEW_INTENT_SCROLL_VERTICAL;
    }

    /* The wheel does *not* turn pages (owner, 2026-09-09).
     *
     * It used to, and that is a common thing for a viewer to do, but it
     * makes the wheel mean two different things depending on whether the
     * picture happens to be taller than the window — so a small scroll
     * on one image nudges it, and the same gesture on the next image
     * jumps to a different file. Turning pages stays on the keys, where
     * it is deliberate. Ctrl+wheel still zooms, and a scrollable image
     * still scrolls. */
    (void)forward;
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
