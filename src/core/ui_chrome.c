#include "rubraview/ui_chrome.h"
#include <stdio.h>

/* ---- On-Screen Display ---- */

rubraview_osd_t rubraview_osd_create(double visible_seconds, double fade_seconds) {
    if (visible_seconds < 0.0) visible_seconds = 0.0;
    if (fade_seconds < 0.0) fade_seconds = 0.0;
    return (rubraview_osd_t){
        .visible_seconds = visible_seconds,
        .fade_seconds = fade_seconds,
        .elapsed = 0.0,
        .always_on = false,
    };
}

void rubraview_osd_notify_activity(rubraview_osd_t *osd) {
    if (osd) osd->elapsed = 0.0;
}

void rubraview_osd_tick(rubraview_osd_t *osd, double delta_seconds) {
    if (!osd || osd->always_on) return;
    if (delta_seconds > 0.0) osd->elapsed += delta_seconds;
}

double rubraview_osd_opacity(const rubraview_osd_t *osd) {
    if (!osd) return 0.0;
    if (osd->always_on) return 1.0;

    if (osd->elapsed <= osd->visible_seconds) return 1.0;
    if (osd->fade_seconds <= 0.0) return 0.0;

    double into_fade = osd->elapsed - osd->visible_seconds;
    if (into_fade >= osd->fade_seconds) return 0.0;
    return 1.0 - (into_fade / osd->fade_seconds);
}

u8str_t rubraview_osd_format(char *buffer, size_t buffer_size,
                             u8str_t file_name, int32_t width, int32_t height,
                             double zoom_percent, size_t index, size_t total) {
    if (!buffer || buffer_size == 0) return (u8str_t){ .ptr = "", .len = 0 };

    /* file_name is a slice and need not be NUL-terminated, so its length
       is passed explicitly via the precision specifier. */
    int written = snprintf(buffer, buffer_size, "%.*s  |  %d x %d  |  %.0f%%  |  %zu / %zu",
                           (int)file_name.len, file_name.ptr ? file_name.ptr : "",
                           width, height, zoom_percent,
                           total > 0 ? index + 1 : 0, total);
    if (written < 0) {
        buffer[0] = '\0';
        return (u8str_t){ .ptr = buffer, .len = 0 };
    }

    size_t len = (size_t)written;
    if (len >= buffer_size) len = buffer_size - 1; /* snprintf truncated */
    return (u8str_t){ .ptr = buffer, .len = len };
}

/* ---- Hover titlebar ---- */

rubraview_titlebar_t rubraview_titlebar_create(double dpi_scale) {
    if (dpi_scale <= 0.0) dpi_scale = 1.0;
    return (rubraview_titlebar_t){
        .height = 36.0 * dpi_scale,      /* §3.21.2 */
        .trigger_zone = 12.0 * dpi_scale,
        .hide_delay = 0.5,
        .shown = false,
        .idle_seconds = 0.0,
    };
}

bool rubraview_titlebar_pointer_moved(rubraview_titlebar_t *bar, double pointer_y) {
    if (!bar) return false;

    if (pointer_y <= bar->trigger_zone) {
        bar->idle_seconds = 0.0;
        if (!bar->shown) {
            bar->shown = true; /* §3.21.2: reveals as soon as the top edge is approached */
            return true;
        }
        return false;
    }

    /* While the pointer is still over the revealed bar it stays put; the
       countdown only starts once the pointer is clear of it. */
    if (bar->shown && pointer_y <= bar->height) {
        bar->idle_seconds = 0.0;
        return false;
    }

    if (bar->shown) bar->idle_seconds = 0.0; /* leaving now: start the grace period */
    return false;
}

bool rubraview_titlebar_tick(rubraview_titlebar_t *bar, double delta_seconds) {
    if (!bar || !bar->shown) return false;

    bar->idle_seconds += delta_seconds;
    if (bar->idle_seconds < bar->hide_delay) return false;

    bar->shown = false;
    bar->idle_seconds = 0.0;
    return true;
}

/* §3.21.3: the control cluster sits at the right edge, ordered
   minimize, maximize/restore, fullscreen, close. */
static const rubraview_titlebar_button_t BUTTON_ORDER[] = {
    /* First in the order is leftmost on screen. */
    RUBRAVIEW_TITLEBAR_SNAP_BOXES,
    RUBRAVIEW_TITLEBAR_MINIMIZE,
    RUBRAVIEW_TITLEBAR_MAXIMIZE,
    RUBRAVIEW_TITLEBAR_FULLSCREEN,
    RUBRAVIEW_TITLEBAR_CLOSE,
};
#define BUTTON_COUNT ((int32_t)(sizeof(BUTTON_ORDER) / sizeof(BUTTON_ORDER[0])))

static double button_width(const rubraview_titlebar_t *bar) {
    /* 40 x 36 at 100%; keep the ratio as the bar scales with DPI. */
    return bar->height * (40.0 / 36.0);
}

rubraview_rect_t rubraview_titlebar_button_rect(const rubraview_titlebar_t *bar,
                                                rubraview_titlebar_button_t button,
                                                double window_width) {
    if (!bar) return (rubraview_rect_t){0};

    double w = button_width(bar);
    for (int32_t i = 0; i < BUTTON_COUNT; ++i) {
        if (BUTTON_ORDER[i] != button) continue;
        /* Rightmost button is the last in the order. */
        double right_offset = (double)(BUTTON_COUNT - i) * w;
        return (rubraview_rect_t){
            .x = window_width - right_offset,
            .y = 0.0,
            .width = w,
            .height = bar->height,
        };
    }
    return (rubraview_rect_t){0};
}

rubraview_titlebar_button_t rubraview_titlebar_hit(const rubraview_titlebar_t *bar,
                                                   double x, double y, double window_width) {
    if (!bar || !bar->shown) return RUBRAVIEW_TITLEBAR_NONE;
    if (y < 0.0 || y >= bar->height) return RUBRAVIEW_TITLEBAR_NONE;

    for (int32_t i = 0; i < BUTTON_COUNT; ++i) {
        rubraview_rect_t r = rubraview_titlebar_button_rect(bar, BUTTON_ORDER[i], window_width);
        if (rubraview_rect_contains(r, x, y)) return BUTTON_ORDER[i];
    }

    /* §3.21.2: empty titlebar space drags the window. */
    return RUBRAVIEW_TITLEBAR_CAPTION;
}

/* ---- Slide-show presentation timing ---- */

rubraview_transition_t rubraview_transition_create(rubraview_transition_kind_t kind, double duration) {
    if (duration < 0.0) duration = 0.0;
    return (rubraview_transition_t){ .kind = kind, .duration = duration, .elapsed = 0.0, .active = false };
}

rubraview_transition_kind_t rubraview_transition_for_interval(rubraview_transition_kind_t configured,
                                                              double interval_seconds,
                                                              double cut_threshold) {
    /* §3.2.5: below the threshold there is no time for anything but a
       cut, and a half-finished fade looks worse than none. */
    if (interval_seconds < cut_threshold) return RUBRAVIEW_TRANSITION_CUT;
    return configured;
}

void rubraview_transition_start(rubraview_transition_t *transition) {
    if (!transition) return;
    transition->elapsed = 0.0;
    transition->active = (transition->kind != RUBRAVIEW_TRANSITION_CUT) && (transition->duration > 0.0);
}

void rubraview_transition_tick(rubraview_transition_t *transition, double delta_seconds) {
    if (!transition || !transition->active) return;
    transition->elapsed += delta_seconds;
    if (transition->elapsed >= transition->duration) {
        transition->elapsed = transition->duration;
        transition->active = false;
    }
}

double rubraview_transition_progress(const rubraview_transition_t *transition) {
    if (!transition) return 1.0;
    if (transition->kind == RUBRAVIEW_TRANSITION_CUT || transition->duration <= 0.0) return 1.0;
    if (!transition->active && transition->elapsed <= 0.0) return 1.0; /* never started */
    double p = transition->elapsed / transition->duration;
    if (p < 0.0) return 0.0;
    if (p > 1.0) return 1.0;
    return p;
}

bool rubraview_transition_active(const rubraview_transition_t *transition) {
    return transition && transition->active;
}

/* ---- Cursor auto-hide ---- */

rubraview_cursor_hide_t rubraview_cursor_hide_create(double delay_seconds) {
    if (delay_seconds < 0.0) delay_seconds = 0.0;
    return (rubraview_cursor_hide_t){ .delay = delay_seconds, .idle_seconds = 0.0, .hidden = false };
}

bool rubraview_cursor_hide_notify_motion(rubraview_cursor_hide_t *cursor) {
    if (!cursor) return false;
    cursor->idle_seconds = 0.0;
    if (!cursor->hidden) return false;
    cursor->hidden = false;
    return true;
}

bool rubraview_cursor_hide_tick(rubraview_cursor_hide_t *cursor, double delta_seconds) {
    if (!cursor || cursor->hidden) return false;
    cursor->idle_seconds += delta_seconds;
    if (cursor->idle_seconds < cursor->delay) return false;
    cursor->hidden = true;
    return true;
}
