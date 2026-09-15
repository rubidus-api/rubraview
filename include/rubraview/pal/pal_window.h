#ifndef RUBRAVIEW_PAL_WINDOW_H
#define RUBRAVIEW_PAL_WINDOW_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "rubraview/core.h"
#include "rubraview/keymap.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Windowing and input PAL (RFC-0001 §8.2, §3.21 frameless shell, §4.2
 * Per-Monitor V2 DPI). The window is an opaque handle and every event is
 * a platform-neutral value — no Win32 type appears in this header
 * (owner decision D-1, 2026-09-08).
 *
 * Key events carry a rubraview_key_combo_t (rubraview/keymap.h): the
 * Win32 backend translates virtual-key codes into the same symbolic
 * names keymap.ini uses ("Right", "PageDown", "BracketLeft", ...), so
 * the application dispatches with rubraview_keymap_find_action and never
 * sees a VK code.
 */

typedef struct rubraview_window rubraview_window_t;

typedef enum rubraview_window_event_kind {
    RUBRAVIEW_WINDOW_EVENT_NONE = 0,
    RUBRAVIEW_WINDOW_EVENT_CLOSE,
    RUBRAVIEW_WINDOW_EVENT_RESIZE,       /* client area changed; the renderer must resize its back buffer */
    RUBRAVIEW_WINDOW_EVENT_DPI_CHANGED,  /* §4.2: window moved to a monitor with a different scale factor */
    RUBRAVIEW_WINDOW_EVENT_PAINT,        /* the OS asked for a redraw */
    RUBRAVIEW_WINDOW_EVENT_KEY_DOWN,
    RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN,
    RUBRAVIEW_WINDOW_EVENT_MOUSE_UP,
    RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE,
    RUBRAVIEW_WINDOW_EVENT_MOUSE_WHEEL,
    RUBRAVIEW_WINDOW_EVENT_GESTURE_ZOOM, /* §3.6.5 GID_ZOOM: two-finger pinch */
    RUBRAVIEW_WINDOW_EVENT_GESTURE_PAN,  /* §3.6.5 GID_PAN: two-finger drag */
    RUBRAVIEW_WINDOW_EVENT_DROP,         /* §3.19.2: files dropped on the window */
    RUBRAVIEW_WINDOW_EVENT_OPEN_REQUEST, /* §3.19.1: another instance handed us a path */
    RUBRAVIEW_WINDOW_EVENT_MOVED,        /* the reader finished moving the window (RFC-0002 Q6: docking) */
} rubraview_window_event_kind_t;

typedef enum rubraview_mouse_button {
    RUBRAVIEW_MOUSE_LEFT = 0,
    RUBRAVIEW_MOUSE_RIGHT,
    RUBRAVIEW_MOUSE_MIDDLE,
    RUBRAVIEW_MOUSE_X1,
    RUBRAVIEW_MOUSE_X2,
} rubraview_mouse_button_t;

typedef struct rubraview_window_event {
    rubraview_window_event_kind_t kind;

    struct {
        int32_t width, height; /* client-area pixels */
    } resize;

    struct {
        double scale;          /* DPI / 96.0, e.g. 1.5 at 150% */
    } dpi;

    struct {
        rubraview_key_combo_t combo; /* symbolic key name + modifier bitmask */
    } key;

    struct {
        double x, y;                 /* client-area coordinates, pixels */
        double wheel_delta;          /* notches; positive is away from the user */
        rubraview_mouse_button_t button;
        uint32_t modifiers;          /* rubraview_key_mod_t bitmask held during the event */
    } mouse;

    struct {
        /* §3.19.2 / §3.19.1: paths arriving from outside. The strings
           point into the window's own buffer and are valid until the
           next poll, which is long enough to open them. */
        const char *paths[16];
        size_t      path_lengths[16];
        size_t      count;
    } drop;

    struct {
        double scale_ratio;   /* pinch: >1 spreading apart, <1 pinching together; 1.0 for a pan */
        double dx, dy;        /* pan: movement since the previous gesture message, in pixels */
        double center_x, center_y; /* the gesture centroid, in client coordinates */
    } gesture;
} rubraview_window_event_t;

typedef struct rubraview_window_config {
    const char *title;
    int32_t width, height;  /* initial client size in logical pixels (scaled by DPI at creation) */
    bool    frameless;      /* §3.21.1: strip the OS caption and borders via WM_NCCALCSIZE */
    /**
     * §3.22 / D-13: a window that belongs to another — the settings
     * window. It stays above its owner, has no taskbar button of its own,
     * and closing it ends nothing but itself. NULL for the main window.
     */
    rubraview_window_t *owner;
    /**
     * RFC-0002 Q6: a detached toolbox — a small popup with no frame, on top
     * of other windows, without a taskbar button, that does not take the
     * keyboard from the viewer when clicked, and can be made see-through.
     */
    bool tool_window;
} rubraview_window_config_t;

/* ---- §3.19 lifecycle and shell integration ---- */

/**
 * §3.19.1: is another copy of this program already running? Taking the
 * named mutex is what answers it, and holding it for the life of the
 * process is what makes the answer true for anyone who asks later.
 */
bool rubraview_pal_instance_claim(void);

/**
 * Hand a path to the running instance and bring its window forward.
 * Returns false when there is nothing to hand it to, in which case the
 * caller should start normally rather than exiting.
 */
bool rubraview_pal_instance_hand_over(u8str_t path);

/** §3.19.2: accept files dropped on this window. */
void rubraview_pal_window_accept_drops(rubraview_window_t *window, bool accept);

/**
 * §3.19.3: register or remove this program's file associations under
 * HKCU. Registering writes one ProgID per extension; unregistering must
 * leave none behind, which is why both derive the names the same way
 * (rubraview_shell_progid).
 */
bool rubraview_pal_shell_register(u8str_t extensions_semicolon_list);
bool rubraview_pal_shell_unregister(u8str_t extensions_semicolon_list);

/** Returns NULL if the window cannot be created (or on a host build with no windowing backend). */
rubraview_window_t *rubraview_pal_window_create(proven_arena_t *arena, const rubraview_window_config_t *config);

void rubraview_pal_window_destroy(rubraview_window_t *window);

/**
 * Drain one pending event. Returns false when the queue is empty for
 * this iteration, which is the application's cue to render a frame.
 */
bool rubraview_pal_window_poll_event(rubraview_window_t *window, rubraview_window_event_t *out_event);

/**
 * Block until the OS has input or a message for this thread, or until
 * timeout_ms passes. Returns at once if events are already queued. An
 * idle viewer calls this instead of redrawing a frame nobody asked for.
 */
void rubraview_pal_window_wait_event(rubraview_window_t *window, uint32_t timeout_ms);

/** Set the window's caption (UTF-8). Frameless windows still show it in
    the taskbar and Alt+Tab. */
void rubraview_pal_window_set_title(rubraview_window_t *window, const char *title_utf8);

void rubraview_pal_window_get_size(const rubraview_window_t *window, int32_t *out_width, int32_t *out_height);

/**
 * §3.22.1: where the window is on the desktop — its outer frame, in
 * screen pixels — so a window can come back where it was left. Returns
 * false when there is no window.
 */
bool rubraview_pal_window_get_frame(const rubraview_window_t *window,
                                    int32_t *out_x, int32_t *out_y, int32_t *out_width, int32_t *out_height);
/** Move and size the frame; a place off every screen is pulled back onto the nearest one. */
void rubraview_pal_window_set_frame(rubraview_window_t *window, int32_t x, int32_t y, int32_t width, int32_t height);

/** Always on top of other programs' windows, or not (owner, 2026-09-15). */
void rubraview_pal_window_set_topmost(rubraview_window_t *window, bool topmost);

/** RFC-0002 Q6: how opaque a tool window is, 30–100 %. Other windows ignore it. */
void rubraview_pal_window_set_opacity(rubraview_window_t *window, double percent);

/** Hide a window without destroying it, or show it again in front. */
void rubraview_pal_window_set_visible(rubraview_window_t *window, bool visible);

/** §4.2: DPI / 96.0 for the monitor the window is currently on. */
double rubraview_pal_window_dpi_scale(const rubraview_window_t *window);

/**
 * The platform's native handle (HWND on Win32), passed opaquely to the
 * render PAL. The application never dereferences it.
 */
void *rubraview_pal_window_native_handle(const rubraview_window_t *window);

/** §3.21.3 / §3.2.5: true fullscreen covering the whole monitor, taskbar included. */
void rubraview_pal_window_set_fullscreen(rubraview_window_t *window, bool enabled);
bool rubraview_pal_window_is_fullscreen(const rubraview_window_t *window);

/** §3.2.5: hide the pointer during fullscreen presentation. Idempotent. */
void rubraview_pal_window_set_cursor_visible(rubraview_window_t *window, bool visible);

/** §3.21.2: begin an OS window drag, as if the caption bar were grabbed. */
void rubraview_pal_window_begin_drag(rubraview_window_t *window);

/** §3.21.3: the minimize and maximize/restore controls. */
void rubraview_pal_window_minimize(rubraview_window_t *window);
void rubraview_pal_window_toggle_maximize(rubraview_window_t *window);

bool rubraview_pal_window_should_close(const rubraview_window_t *window);
void rubraview_pal_window_request_close(rubraview_window_t *window);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_WINDOW_H */
