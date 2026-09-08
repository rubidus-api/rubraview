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
} rubraview_window_event_t;

typedef struct rubraview_window_config {
    const char *title;
    int32_t width, height;  /* initial client size in logical pixels (scaled by DPI at creation) */
    bool    frameless;      /* §3.21.1: strip the OS caption and borders via WM_NCCALCSIZE */
} rubraview_window_config_t;

/** Returns NULL if the window cannot be created (or on a host build with no windowing backend). */
rubraview_window_t *rubraview_pal_window_create(proven_arena_t *arena, const rubraview_window_config_t *config);

void rubraview_pal_window_destroy(rubraview_window_t *window);

/**
 * Drain one pending event. Returns false when the queue is empty for
 * this iteration, which is the application's cue to render a frame.
 */
bool rubraview_pal_window_poll_event(rubraview_window_t *window, rubraview_window_event_t *out_event);

void rubraview_pal_window_get_size(const rubraview_window_t *window, int32_t *out_width, int32_t *out_height);

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

bool rubraview_pal_window_should_close(const rubraview_window_t *window);
void rubraview_pal_window_request_close(rubraview_window_t *window);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_WINDOW_H */
