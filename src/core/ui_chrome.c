#include "rubraview/ui_chrome.h"
#include "rubraview/lang.h"
#include "rubraview/playback.h"
#include <stdio.h>
#include <string.h>

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
    /* No name and no count: the toolbox's line says those (owner,
       2026-09-29), and the status line keeps what only it says. */
    int written;
    if (file_name.len == 0 && total == 0) {
        written = snprintf(buffer, buffer_size, "%d x %d  |  %.0f%%", width, height, zoom_percent);
    } else {
        written = snprintf(buffer, buffer_size, "%.*s  |  %d x %d  |  %.0f%%  |  %zu / %zu",
                           (int)file_name.len, file_name.ptr ? file_name.ptr : "",
                           width, height, zoom_percent,
                           total > 0 ? index + 1 : 0, total);
    }
    if (written < 0) {
        buffer[0] = '\0';
        return (u8str_t){ .ptr = buffer, .len = 0 };
    }

    size_t len = (size_t)written;
    if (len >= buffer_size) len = buffer_size - 1; /* snprintf truncated */
    return (u8str_t){ .ptr = buffer, .len = len };
}

size_t rubraview_utf8_chars(u8str_t text) {
    size_t n = 0;
    for (size_t i = 0; i < text.len; ++i) if (((uint8_t)text.ptr[i] & 0xC0) != 0x80) n++;
    return n;
}

/* The byte offset of code point `chars` in `text` (its length when past the end). */
static size_t utf8_offset(u8str_t text, size_t chars) {
    size_t n = 0;
    for (size_t i = 0; i < text.len; ++i) {
        if (((uint8_t)text.ptr[i] & 0xC0) != 0x80) {
            if (n == chars) return i;
            n++;
        }
    }
    return text.len;
}

u8str_t rubraview_middle_ellipsis(char *buffer, size_t buffer_size, u8str_t text, size_t head_chars, size_t tail_chars) {
    size_t total = rubraview_utf8_chars(text);
    if (total <= head_chars + tail_chars + 3 || !buffer) return text;
    size_t head = utf8_offset(text, head_chars);
    size_t tail = utf8_offset(text, total - tail_chars);
    size_t need = head + 3 + (text.len - tail);
    if (need + 1 > buffer_size) return text;
    memcpy(buffer, text.ptr, head);
    memcpy(buffer + head, "...", 3);
    memcpy(buffer + head + 3, text.ptr + tail, text.len - tail);
    buffer[need] = '\0';
    return (u8str_t){ .ptr = buffer, .len = need };
}

u8str_t rubraview_osd_right(char *buffer, size_t buffer_size, size_t index, size_t total,
                            int32_t width, int32_t height, uint64_t bytes, bool reduced,
                            double position, double duration) {
    if (!buffer || buffer_size == 0) return (u8str_t){ .ptr = "", .len = 0 };
    size_t at = 0;
    #define OSD_PUT(...) do { if (at < buffer_size) { int w_ = snprintf(buffer + at, buffer_size - at, __VA_ARGS__); \
                              if (w_ > 0) { at += (size_t)w_; } \
                              if (at >= buffer_size) { at = buffer_size - 1; } } } while (0)
    if (reduced) OSD_PUT(T("shown reduced  |  "));
    if (bytes > 0) {
        const char *const UNIT[] = { "B", T("KB"), T("MB"), T("GB"), T("TB") };
        double v = (double)bytes;
        int u = 0;
        while (v >= 1024.0 && u < 4) { v /= 1024.0; u++; }
        if (u == 0) OSD_PUT("%.0f %s  |  ", v, UNIT[u]);
        else OSD_PUT("%.*f %s  |  ", v < 10.0 ? 1 : 0, v, UNIT[u]);
    }
    if (duration > 0.0) {
        char where[32], length[32];   /* to the second (owner, 2026-10-16) */
        u8str_t a = rubraview_format_timecode(where, sizeof(where), position > duration ? duration : position, false);
        u8str_t l = rubraview_format_timecode(length, sizeof(length), duration, false);
        OSD_PUT("%.*s / %.*s  |  ", (int)a.len, a.ptr, (int)l.len, l.ptr);
    }
    if (width > 0 && height > 0) OSD_PUT("%d x %d  |  ", (int)width, (int)height);
    OSD_PUT("%zu / %zu", total > 0 ? index + 1 : 0, total);   /* the place, at the very right */
    #undef OSD_PUT
    buffer[at] = '\0';
    return (u8str_t){ .ptr = buffer, .len = at };
}

/* ---- Hover titlebar ---- */

rubraview_titlebar_t rubraview_titlebar_create(double dpi_scale) {
    if (dpi_scale <= 0.0) dpi_scale = 1.0;
    return (rubraview_titlebar_t){
        .height = 36.0 * dpi_scale,      /* §3.21.2 */
        .trigger_zone = 36.0 * dpi_scale,   /* the bar's own height: where it will be, it comes (owner, 2026-10-14) */
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

    /* Clear of the bar: the countdown runs. It is not restarted here —
       `idle_seconds` counts from when the pointer last sat on the bar, so
       moving the mouse about elsewhere no longer keeps the bar up (and a
       pointer resting on the bar no longer loses it, because the caller
       re-states where the pointer is every pass). */
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
    RUBRAVIEW_TITLEBAR_HIDE,
    RUBRAVIEW_TITLEBAR_SNAP_BOXES,
    RUBRAVIEW_TITLEBAR_PIN,
    RUBRAVIEW_TITLEBAR_RESIZE,
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
    if (button == RUBRAVIEW_TITLEBAR_MENU) return (rubraview_rect_t){ .x = 0.0, .y = 0.0, .width = w, .height = bar->height };
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
    if (rubraview_rect_contains(rubraview_titlebar_button_rect(bar, RUBRAVIEW_TITLEBAR_MENU, window_width), x, y)) {
        return RUBRAVIEW_TITLEBAR_MENU;
    }

    for (int32_t i = 0; i < BUTTON_COUNT; ++i) {
        rubraview_rect_t r = rubraview_titlebar_button_rect(bar, BUTTON_ORDER[i], window_width);
        if (rubraview_rect_contains(r, x, y)) return BUTTON_ORDER[i];
    }

    /* §3.21.2: empty titlebar space drags the window. */
    return RUBRAVIEW_TITLEBAR_CAPTION;
}

bool rubraview_titlebar_caption_press(rubraview_titlebar_t *bar, double now_seconds, double x, double y,
                                      double interval, double slop) {
    if (!bar) return false;
    bool twice = bar->pressed_before && now_seconds - bar->press_time >= 0.0 &&
                 now_seconds - bar->press_time <= interval &&
                 x - bar->press_x <= slop && bar->press_x - x <= slop &&
                 y - bar->press_y <= slop && bar->press_y - y <= slop;
    bar->pressed_before = !twice;
    bar->press_time = now_seconds;
    bar->press_x = x;
    bar->press_y = y;
    return twice;
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

void rubraview_transition_poses(rubraview_transition_kind_t kind, double progress, double width, bool forward,
                                rubraview_transition_pose_t *leaving, rubraview_transition_pose_t *arriving) {
    double p = progress < 0.0 ? 0.0 : progress > 1.0 ? 1.0 : progress;
    rubraview_transition_pose_t out = { .dx = 0.0, .scale = 1.0, .opacity = 0.0 };
    rubraview_transition_pose_t in = { .dx = 0.0, .scale = 1.0, .opacity = 1.0 };
    switch (kind) {
        case RUBRAVIEW_TRANSITION_CROSSFADE:
            out.opacity = 1.0 - p;
            in.opacity = p;
            break;
        case RUBRAVIEW_TRANSITION_SLIDE_LEFT:
        case RUBRAVIEW_TRANSITION_SLIDE_RIGHT: {
            /* Eased, so it starts and lands softly. */
            double e = p * p * (3.0 - 2.0 * p);
            double way = (forward == (kind == RUBRAVIEW_TRANSITION_SLIDE_LEFT)) ? 1.0 : -1.0;
            out.dx = -way * e * width;
            in.dx = way * (1.0 - e) * width;
            out.opacity = p < 1.0 ? 1.0 : 0.0;
            break;
        }
        case RUBRAVIEW_TRANSITION_ZOOM_FADE:
            out.scale = 1.0 + 0.08 * p;
            out.opacity = 1.0 - p;
            in.scale = 0.92 + 0.08 * p;
            in.opacity = p;
            break;
        case RUBRAVIEW_TRANSITION_CUT:
        default:
            break;
    }
    if (leaving) *leaving = out;
    if (arriving) *arriving = in;
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
