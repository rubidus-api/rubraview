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
proven_u8str_view_t rubraview_osd_format(char *buffer, size_t buffer_size,
                             proven_u8str_view_t file_name, int32_t width, int32_t height,
                             double zoom_percent, size_t index, size_t total);

/**
 * A name too long for its place (owner, 2026-10-16): its first `head_chars`
 * characters, "...", and its last `tail_chars`. Characters are UTF-8 code
 * points, never cut in half. A name that fits in head + tail + 3 is
 * returned whole (pointing at `text`, not copied).
 */
proven_u8str_view_t rubraview_middle_ellipsis(char *buffer, size_t buffer_size, proven_u8str_view_t text, size_t head_chars, size_t tail_chars);

/** How many UTF-8 code points `text` holds. */
size_t rubraview_utf8_chars(proven_u8str_view_t text);

/**
 * The right half of the information bar (owner, 2026-10-16), read towards
 * the window's right edge: "shown reduced" when it is, the size ("2.4 MB"),
 * the resolution, and "3 / 120" at the very right. A width of 0 leaves the
 * resolution out (a song); a size of 0 leaves the size out. A film or a
 * song playing says where it is of how long, "01:14 / 04:56", between the
 * size and the place (owner, 2026-10-16); a `duration` of 0 leaves it out.
 */
proven_u8str_view_t rubraview_osd_right(char *buffer, size_t buffer_size, size_t index, size_t total,
                            int32_t width, int32_t height, uint64_t bytes, bool reduced,
                            double position, double duration);

/* ---- Hover titlebar (§3.21.2, §3.21.3) ---- */

typedef enum rubraview_titlebar_button {
    RUBRAVIEW_TITLEBAR_NONE = 0,
    /* Left of the rescue button (owner, 2026-10-12): the title bar and the
       floating boxes go away until the picture is clicked. */
    RUBRAVIEW_TITLEBAR_HIDE,
    /* Leftmost of the icons (owner, 2026-09-10): puts both floating
       boxes back in their corners. It sits with the window controls
       because that is where a reader looks when something about the
       window is wrong, and a box that has wandered off is exactly
       that. */
    RUBRAVIEW_TITLEBAR_SNAP_BOXES,
    /* Always on top (owner, 2026-09-15): next to the rescue button, where a
       reader reaches for what the window does. */
    RUBRAVIEW_TITLEBAR_PIN,
    /* Between the pin and minimise (owner, 2026-09-29): pressed and
       dragged, it sizes the window from its top-right corner, staying
       under the pointer (owner, 2026-10-15). */
    RUBRAVIEW_TITLEBAR_RESIZE,
    RUBRAVIEW_TITLEBAR_MINIMIZE,
    RUBRAVIEW_TITLEBAR_MAXIMIZE,
    RUBRAVIEW_TITLEBAR_FULLSCREEN,
    RUBRAVIEW_TITLEBAR_CLOSE,
    /* 1.25.0 (owner, 2026-10-20: "파일 열기 아이콘을 없애고 대신 가로줄 3줄짜리 메뉴 아이콘"): ☰ at the left end,
       a pull-down of what the window does. */
    RUBRAVIEW_TITLEBAR_MENU,
    /* Gone in 1.25.0 (the menu has Open file): no rectangle, never hit. */
    RUBRAVIEW_TITLEBAR_OPEN,
    /* 1.25.0: first of the right-hand row, left of Hide: the settings window. */
    RUBRAVIEW_TITLEBAR_SETTINGS,
    /* 1.25.0: P, the title bar kept shown (not hidden on its own). */
    RUBRAVIEW_TITLEBAR_BARPIN,
    /* 1.25.0: the file's or the folder's name after ☰: a press opens the open-file screen. */
    RUBRAVIEW_TITLEBAR_NAME,
    RUBRAVIEW_TITLEBAR_CAPTION, /* empty space: dragging here moves the window */
} rubraview_titlebar_button_t;

typedef struct rubraview_titlebar {
    double height;          /* §3.21.2: 36 px, scaled by DPI */
    double trigger_zone;    /* top band that reveals it, RFC default 12 px */
    double hide_delay;      /* grace period after the pointer leaves, 0.5 s */
    bool   shown;
    double idle_seconds;
    double name_width;      /* 1.25.0: how wide the name after ☰ is drawn (the caller measures it); 0: none */
    /* The last press on the caption, for a double press (owner, 2026-09-29). */
    bool   pressed_before;
    double press_time, press_x, press_y;
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

/**
 * A press on the caption at `now_seconds`: true when it is the second of a
 * double press (within `interval` seconds and `slop` pixels of the first),
 * which toggles maximise and restore as a system title bar does. A double
 * press is consumed, so a third press starts again.
 */
bool rubraview_titlebar_caption_press(rubraview_titlebar_t *bar, double now_seconds, double x, double y,
                                      double interval, double slop);

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
    RUBRAVIEW_TRANSITION_DISSOLVE,   /* the new page laid over the old, only its opacity rising (owner, 2026-10-17) */
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

/**
 * RV-106: where the page leaving and the page arriving are drawn at
 * `progress` (0..1) of a transition — moved sideways by `dx`, grown by
 * `scale` about the window's middle, at `opacity`. `forward` is a turn
 * to a later page: a slide then comes in from the right.
 */
typedef struct rubraview_transition_pose {
    double dx;
    double scale;
    double opacity;
} rubraview_transition_pose_t;

void rubraview_transition_poses(rubraview_transition_kind_t kind, double progress, double width, bool forward,
                                rubraview_transition_pose_t *leaving, rubraview_transition_pose_t *arriving);

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
