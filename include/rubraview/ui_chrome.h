#ifndef RUBRAVIEW_UI_CHROME_H
#define RUBRAVIEW_UI_CHROME_H

#include "rubraview/core.h"
#include "rubraview/ui_box.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The two pieces of transient window chrome: the On-Screen Display
 * (RFC-0001 §3.1) and the auto-hiding hover titlebar (§3.21.2, §3.21.3).
 * Both are fade/timing state machines driven by an injected delta time,
 * plus hit-test geometry — no window, no clock of their own, so both
 * are testable on the host.
 */

/* ---- On-Screen Display (§3.1) ---- */

/**
 * The OSD fades out after a couple of idle seconds and reappears on any
 * activity. `opacity` is what the renderer multiplies its colours by.
 */
typedef struct rubraview_osd {
    double visible_seconds;  /* how long it stays fully opaque, RFC default 2.0 */
    double fade_seconds;     /* how long the fade itself takes */
    double elapsed;          /* since the last activity */
    bool   always_on;        /* the `I` key pins it open (§3.7.2) */
} rubraview_osd_t;

rubraview_osd_t rubraview_osd_create(double visible_seconds, double fade_seconds);

/** Any navigation, zoom or pointer movement restarts the countdown. */
void rubraview_osd_notify_activity(rubraview_osd_t *osd);

void rubraview_osd_tick(rubraview_osd_t *osd, double delta_seconds);

/** 1.0 while visible, easing to 0.0 across the fade, then staying there. */
double rubraview_osd_opacity(const rubraview_osd_t *osd);

/**
 * §3.1: the status line — file name, resolution, zoom, index and total.
 * Written into `buffer`, returned as a NUL-terminated slice.
 */
u8str_t rubraview_osd_format(char *buffer, size_t buffer_size,
                             u8str_t file_name, int32_t width, int32_t height,
                             double zoom_percent, size_t index, size_t total);

/* ---- Hover titlebar (§3.21.2, §3.21.3) ---- */

typedef enum rubraview_titlebar_button {
    RUBRAVIEW_TITLEBAR_NONE = 0,
    RUBRAVIEW_TITLEBAR_MINIMIZE,
    RUBRAVIEW_TITLEBAR_MAXIMIZE,
    RUBRAVIEW_TITLEBAR_FULLSCREEN,
    RUBRAVIEW_TITLEBAR_CLOSE,
    RUBRAVIEW_TITLEBAR_CAPTION, /* empty space: dragging here moves the window */
} rubraview_titlebar_button_t;

typedef struct rubraview_titlebar {
    double height;          /* §3.21.2: 36 px, scaled by DPI */
    double trigger_zone;    /* top band that reveals it, RFC default 12 px */
    double hide_delay;      /* grace period after the pointer leaves, 0.5 s */
    bool   shown;
    double idle_seconds;
} rubraview_titlebar_t;

rubraview_titlebar_t rubraview_titlebar_create(double dpi_scale);

/**
 * Feed the pointer's vertical position each time it moves. Returns true
 * when visibility changed. Entering the top trigger zone reveals the
 * bar immediately; leaving the bar starts the hide countdown.
 */
bool rubraview_titlebar_pointer_moved(rubraview_titlebar_t *bar, double pointer_y);

/** Advance the hide countdown; returns true when this call hid the bar. */
bool rubraview_titlebar_tick(rubraview_titlebar_t *bar, double delta_seconds);

/** Which control is at this point, or NONE when the bar is hidden or the point is below it. */
rubraview_titlebar_button_t rubraview_titlebar_hit(const rubraview_titlebar_t *bar,
                                                   double x, double y, double window_width);

/** The rectangle of one control, for painting (§3.21.3). */
rubraview_rect_t rubraview_titlebar_button_rect(const rubraview_titlebar_t *bar,
                                                rubraview_titlebar_button_t button,
                                                double window_width);

/* ---- Slide-show presentation timing (§3.2.5) ---- */

/**
 * §3.2.5: hardware-accelerated transitions. The RFC recommends the
 * instant cut below a 0.5 s interval, since a cross-fade cannot finish
 * in time and, over Remote Desktop, continuous alpha blending is exactly
 * what §3.6.4 warns costs bandwidth.
 */
typedef enum rubraview_transition_kind {
    RUBRAVIEW_TRANSITION_CUT = 0,
    RUBRAVIEW_TRANSITION_CROSSFADE,
    RUBRAVIEW_TRANSITION_SLIDE_LEFT,
    RUBRAVIEW_TRANSITION_SLIDE_RIGHT,
    RUBRAVIEW_TRANSITION_ZOOM_FADE,
} rubraview_transition_kind_t;

typedef struct rubraview_transition {
    rubraview_transition_kind_t kind;
    double duration;
    double elapsed;
    bool   active;
} rubraview_transition_t;

rubraview_transition_t rubraview_transition_create(rubraview_transition_kind_t kind, double duration);

/**
 * Pick the transition to actually run for a given slide interval: any
 * interval below `cut_threshold` (0.5 s) forces an instant cut however
 * the user configured it.
 */
rubraview_transition_kind_t rubraview_transition_for_interval(rubraview_transition_kind_t configured,
                                                              double interval_seconds,
                                                              double cut_threshold);

void rubraview_transition_start(rubraview_transition_t *transition);
void rubraview_transition_tick(rubraview_transition_t *transition, double delta_seconds);

/** 0.0 at the start, 1.0 once finished; always 1.0 for an instant cut. */
double rubraview_transition_progress(const rubraview_transition_t *transition);
bool rubraview_transition_active(const rubraview_transition_t *transition);

/* ---- Cursor auto-hide (§3.2.5) ---- */

/** The cursor hides after 1.5 s of stillness in fullscreen presentation. */
typedef struct rubraview_cursor_hide {
    double delay;
    double idle_seconds;
    bool   hidden;
} rubraview_cursor_hide_t;

rubraview_cursor_hide_t rubraview_cursor_hide_create(double delay_seconds);

/** Any pointer movement reveals the cursor; returns true when it changed. */
bool rubraview_cursor_hide_notify_motion(rubraview_cursor_hide_t *cursor);

/** Returns true when this call hid the cursor. */
bool rubraview_cursor_hide_tick(rubraview_cursor_hide_t *cursor, double delta_seconds);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_UI_CHROME_H */
