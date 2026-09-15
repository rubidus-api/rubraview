#ifdef _WIN32
#include <windows.h>
#include <math.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <string.h>
#include "rubraview/pal/pal_window.h"

#define RUBRAVIEW_WINDOW_CLASS L"Rubraview_MainWindow_Class"
#define EVENT_QUEUE_CAPACITY 128
#define FRAMELESS_RESIZE_BORDER 6 /* §3.21.1: invisible resize band, in physical pixels */

/* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 and
   SetProcessDpiAwarenessContext need Windows 10 1703+, so they are bound
   at run time rather than link time: the executable must still start on
   older systems, just without per-monitor V2 scaling. */
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#endif

typedef BOOL (WINAPI *set_process_dpi_awareness_context_fn)(HANDLE);
typedef UINT (WINAPI *get_dpi_for_window_fn)(HWND);

struct rubraview_window {
    HWND hwnd;
    bool frameless;
    bool should_close;
    bool owned;             /* §3.22: a window belonging to another; its end is not the program's */
    bool tool;              /* RFC-0002 Q6: a detached toolbox */
    bool cursor_visible;
    bool fullscreen;
    int32_t width, height;
    double dpi_scale;

    /* Placement saved when entering fullscreen, restored on exit. */
    WINDOWPLACEMENT saved_placement;
    LONG saved_style;
    LONG saved_ex_style;

    /* §3.6.5: WM_GESTURE reports absolute values, so the previous
       sample is kept to turn them into per-message deltas. */
    ULONGLONG last_zoom_distance;
    POINT     last_pan_point;
    bool      gesture_in_progress;

    /* §3.19: dropped and handed-over paths, held until the next poll.
       The event carries pointers into this, so it has to outlive the
       message that filled it. */
    char drop_storage[16][1024];

    rubraview_window_event_t queue[EVENT_QUEUE_CAPACITY];
    size_t queue_head;
    size_t queue_count;

    get_dpi_for_window_fn get_dpi_for_window;
};

static void queue_push(struct rubraview_window *w, rubraview_window_event_t event) {
    if (w->queue_count == EVENT_QUEUE_CAPACITY) {
        /* Drop the oldest event rather than the newest: a stale mouse
           move matters less than the close or resize behind it. */
        w->queue_head = (w->queue_head + 1) % EVENT_QUEUE_CAPACITY;
        w->queue_count--;
    }
    size_t tail = (w->queue_head + w->queue_count) % EVENT_QUEUE_CAPACITY;
    w->queue[tail] = event;
    w->queue_count++;
}

static uint32_t current_modifiers(void) {
    uint32_t mods = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000) mods |= RUBRAVIEW_MOD_SHIFT;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= RUBRAVIEW_MOD_CTRL;
    if (GetKeyState(VK_MENU) & 0x8000) mods |= RUBRAVIEW_MOD_ALT;
    return mods;
}

/* Maps a Win32 virtual-key code to the symbolic name keymap.ini uses
   (§3.7.5), so the application dispatches through rubraview_keymap and
   never sees a VK code. Returns NULL for keys with no symbolic name. */
static const char *vk_to_key_name(WPARAM vk) {
    /* Distinct string literals, not a shared mutable buffer: an event
       sitting in the queue keeps pointing at a valid name even after
       later key presses arrive. */
    static const char *const LETTERS[26] = {
        "A","B","C","D","E","F","G","H","I","J","K","L","M",
        "N","O","P","Q","R","S","T","U","V","W","X","Y","Z",
    };
    static const char *const DIGITS[10] = {
        "0","1","2","3","4","5","6","7","8","9",
    };

    if (vk >= 'A' && vk <= 'Z') return LETTERS[vk - 'A'];
    if (vk >= '0' && vk <= '9') return DIGITS[vk - '0'];

    switch (vk) {
        case VK_LEFT:      return "Left";
        case VK_RIGHT:     return "Right";
        case VK_UP:        return "Up";
        case VK_DOWN:      return "Down";
        case VK_PRIOR:     return "PageUp";
        case VK_NEXT:      return "PageDown";
        case VK_HOME:      return "Home";
        case VK_END:       return "End";
        case VK_SPACE:     return "Space";
        case VK_RETURN:    return "Enter";
        case VK_ESCAPE:    return "Escape";
        case VK_TAB:       return "Tab";
        case VK_BACK:      return "Backspace";
        case VK_DELETE:    return "Delete";
        case VK_INSERT:    return "Insert";
        case VK_OEM_PERIOD: return "Period";
        case VK_OEM_COMMA:  return "Comma";
        case VK_OEM_4:      return "BracketLeft";
        case VK_OEM_6:      return "BracketRight";
        case VK_OEM_5:      return "Backslash";
        case VK_OEM_PLUS:   return "Plus";
        case VK_OEM_MINUS:  return "Minus";
        case VK_ADD:        return "Plus";
        case VK_SUBTRACT:   return "Minus";
        case VK_F1:  return "F1";
        case VK_F2:  return "F2";
        case VK_F3:  return "F3";
        case VK_F4:  return "F4";
        case VK_F5:  return "F5";
        /* F6–F9 had no name, so a key pressed to rebind (D-14) arrived
           as nothing at all. */
        case VK_F6:  return "F6";
        case VK_F7:  return "F7";
        case VK_F8:  return "F8";
        case VK_F9:  return "F9";
        case VK_OEM_1: return "Semicolon";
        case VK_OEM_2: return "Slash";
        case VK_OEM_3: return "Backquote";
        case VK_OEM_7: return "Quote";
        case VK_NUMPAD0: return "Numpad0";
        case VK_NUMPAD1: return "Numpad1";
        case VK_NUMPAD2: return "Numpad2";
        case VK_NUMPAD3: return "Numpad3";
        case VK_NUMPAD4: return "Numpad4";
        case VK_NUMPAD5: return "Numpad5";
        case VK_NUMPAD6: return "Numpad6";
        case VK_NUMPAD7: return "Numpad7";
        case VK_NUMPAD8: return "Numpad8";
        case VK_NUMPAD9: return "Numpad9";
        case VK_F10: return "F10";
        case VK_F11: return "F11";
        case VK_F12: return "F12";
        default:     return NULL;
    }
}

static void update_dpi_scale(struct rubraview_window *w) {
    if (w->get_dpi_for_window) {
        UINT dpi = w->get_dpi_for_window(w->hwnd);
        if (dpi > 0) {
            w->dpi_scale = (double)dpi / 96.0;
            return;
        }
    }
    w->dpi_scale = 1.0;
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    struct rubraview_window *w = (struct rubraview_window*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    if (msg == WM_NCCREATE) {
        CREATESTRUCTW *cs = (CREATESTRUCTW*)lparam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    if (!w) return DefWindowProcW(hwnd, msg, wparam, lparam);

    switch (msg) {
        case WM_DROPFILES: {
            /* §3.19.2. The paths are copied out here because the drop
               handle must be freed before returning. */
            HDROP drop = (HDROP)wparam;
            UINT dropped = DragQueryFileW(drop, 0xFFFFFFFFu, NULL, 0);
            if (dropped > 16) dropped = 16;

            rubraview_window_event_t event = { .kind = RUBRAVIEW_WINDOW_EVENT_DROP };
            for (UINT i = 0; i < dropped; ++i) {
                WCHAR wide[MAX_PATH * 2];
                if (DragQueryFileW(drop, i, wide, (UINT)(sizeof(wide) / sizeof(wide[0]))) == 0) continue;

                int written = WideCharToMultiByte(CP_UTF8, 0, wide, -1, w->drop_storage[event.drop.count],
                                                  (int)sizeof(w->drop_storage[0]), NULL, NULL);
                if (written <= 1) continue;

                event.drop.paths[event.drop.count] = w->drop_storage[event.drop.count];
                event.drop.path_lengths[event.drop.count] = (size_t)(written - 1);
                event.drop.count++;
            }
            DragFinish(drop);

            if (event.drop.count > 0) queue_push(w, event);
            return 0;
        }

        case WM_COPYDATA: {
            /* §3.19.1: the second instance sent us its file and exited. */
            COPYDATASTRUCT *data = (COPYDATASTRUCT*)lparam;
            if (!data || !data->lpData || data->cbData == 0) return TRUE;

            size_t length = data->cbData;
            if (length >= sizeof(w->drop_storage[0])) length = sizeof(w->drop_storage[0]) - 1;
            memcpy(w->drop_storage[0], data->lpData, length);
            w->drop_storage[0][length] = '\0';

            rubraview_window_event_t event = { .kind = RUBRAVIEW_WINDOW_EVENT_OPEN_REQUEST };
            event.drop.paths[0] = w->drop_storage[0];
            event.drop.path_lengths[0] = strlen(w->drop_storage[0]);
            event.drop.count = 1;
            queue_push(w, event);

            /* Bring this window forward: the reader asked for the file
               by launching the program again, and it must not be
               answered by a window they cannot see. */
            if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            return TRUE;
        }

        case WM_NCCALCSIZE:
            /* §3.21.1: returning 0 with wParam TRUE gives the client area
               the entire window rectangle — no caption, no borders, so
               100% of the pixels belong to the Direct2D canvas. */
            if (w->frameless && wparam == TRUE) {
                return 0;
            }
            break;

        case WM_NCHITTEST:
            if (w->frameless && !w->fullscreen) {
                /* §3.21.1: with no visible border, the resize bands must
                   still be hit-testable. */
                POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
                RECT rc;
                GetWindowRect(hwnd, &rc);

                bool left = pt.x < rc.left + FRAMELESS_RESIZE_BORDER;
                bool right = pt.x >= rc.right - FRAMELESS_RESIZE_BORDER;
                bool top = pt.y < rc.top + FRAMELESS_RESIZE_BORDER;
                bool bottom = pt.y >= rc.bottom - FRAMELESS_RESIZE_BORDER;

                if (top && left) return HTTOPLEFT;
                if (top && right) return HTTOPRIGHT;
                if (bottom && left) return HTBOTTOMLEFT;
                if (bottom && right) return HTBOTTOMRIGHT;
                if (left) return HTLEFT;
                if (right) return HTRIGHT;
                if (top) return HTTOP;
                if (bottom) return HTBOTTOM;
                return HTCLIENT;
            }
            break;

        case WM_GETMINMAXINFO: {
            /* §3.21.1: a maximized borderless window must stop at the
               work area so it does not cover the taskbar. Fullscreen
               (which intentionally covers it) bypasses this. */
            if (w->frameless && !w->fullscreen) {
                HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi = { .cbSize = sizeof(mi) };
                if (GetMonitorInfoW(monitor, &mi)) {
                    MINMAXINFO *mmi = (MINMAXINFO*)lparam;
                    mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
                    mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
                    mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
                    mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
                    return 0;
                }
            }
            break;
        }

        case WM_SIZE: {
            w->width = (int32_t)LOWORD(lparam);
            w->height = (int32_t)HIWORD(lparam);
            rubraview_window_event_t e = { .kind = RUBRAVIEW_WINDOW_EVENT_RESIZE };
            e.resize.width = w->width;
            e.resize.height = w->height;
            queue_push(w, e);
            return 0;
        }

        case WM_DPICHANGED: {
            /* §4.2: adopt the suggested rectangle for the new monitor. */
            w->dpi_scale = (double)LOWORD(wparam) / 96.0;
            RECT *suggested = (RECT*)lparam;
            SetWindowPos(hwnd, NULL,
                         suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            rubraview_window_event_t e = { .kind = RUBRAVIEW_WINDOW_EVENT_DPI_CHANGED };
            e.dpi.scale = w->dpi_scale;
            queue_push(w, e);
            return 0;
        }

        case WM_PAINT: {
            /* Painting itself is the renderer's job; acknowledge the
               region so Windows stops re-posting WM_PAINT. */
            ValidateRect(hwnd, NULL);
            rubraview_window_event_t e = { .kind = RUBRAVIEW_WINDOW_EVENT_PAINT };
            queue_push(w, e);
            return 0;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            const char *name = vk_to_key_name(wparam);
            if (name) {
                rubraview_window_event_t e = { .kind = RUBRAVIEW_WINDOW_EVENT_KEY_DOWN };
                e.key.combo.modifiers = current_modifiers();
                e.key.combo.key_name = (u8str_t){ .ptr = name, .len = strlen(name) };
                queue_push(w, e);
            }
            if (msg == WM_SYSKEYDOWN) break; /* let Alt+F4 and friends reach DefWindowProc */
            return 0;
        }

        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN: case WM_LBUTTONUP:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: {
            rubraview_window_event_t e = {0};
            switch (msg) {
                case WM_MOUSEMOVE: e.kind = RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE; break;
                case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
                    e.kind = RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN; break;
                default: e.kind = RUBRAVIEW_WINDOW_EVENT_MOUSE_UP; break;
            }
            switch (msg) {
                case WM_RBUTTONDOWN: case WM_RBUTTONUP: e.mouse.button = RUBRAVIEW_MOUSE_RIGHT; break;
                case WM_MBUTTONDOWN: case WM_MBUTTONUP: e.mouse.button = RUBRAVIEW_MOUSE_MIDDLE; break;
                default: e.mouse.button = RUBRAVIEW_MOUSE_LEFT; break;
            }
            e.mouse.x = (double)GET_X_LPARAM(lparam);
            e.mouse.y = (double)GET_Y_LPARAM(lparam);
            e.mouse.modifiers = current_modifiers();
            queue_push(w, e);
            return 0;
        }

        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP: {
            rubraview_window_event_t e = {
                .kind = (msg == WM_XBUTTONDOWN) ? RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN : RUBRAVIEW_WINDOW_EVENT_MOUSE_UP,
            };
            e.mouse.button = (GET_XBUTTON_WPARAM(wparam) == XBUTTON1) ? RUBRAVIEW_MOUSE_X1 : RUBRAVIEW_MOUSE_X2;
            e.mouse.x = (double)GET_X_LPARAM(lparam);
            e.mouse.y = (double)GET_Y_LPARAM(lparam);
            e.mouse.modifiers = current_modifiers();
            queue_push(w, e);
            return TRUE;
        }

        case WM_MOUSEWHEEL: {
            rubraview_window_event_t e = { .kind = RUBRAVIEW_WINDOW_EVENT_MOUSE_WHEEL };
            e.mouse.wheel_delta = (double)GET_WHEEL_DELTA_WPARAM(wparam) / (double)WHEEL_DELTA;
            POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            ScreenToClient(hwnd, &pt); /* wheel coordinates arrive in screen space */
            e.mouse.x = (double)pt.x;
            e.mouse.y = (double)pt.y;
            e.mouse.modifiers = current_modifiers();
            queue_push(w, e);
            return 0;
        }

        case WM_GESTURE: {
            GESTUREINFO gi = { .cbSize = sizeof(GESTUREINFO) };
            if (!GetGestureInfo((HGESTUREINFO)lparam, &gi)) break;

            POINT centre = { gi.ptsLocation.x, gi.ptsLocation.y };
            ScreenToClient(hwnd, &centre); /* gesture points arrive in screen space */

            switch (gi.dwID) {
                case GID_BEGIN:
                    w->gesture_in_progress = true;
                    w->last_zoom_distance = 0;
                    w->last_pan_point = centre;
                    break;

                case GID_END:
                    w->gesture_in_progress = false;
                    w->last_zoom_distance = 0;
                    break;

                case GID_ZOOM: {
                    /* ullArguments carries the distance between the two
                       fingers; the ratio against the previous sample is
                       the zoom factor for this step. */
                    if (w->last_zoom_distance != 0 && gi.ullArguments != 0) {
                        rubraview_window_event_t e = { .kind = RUBRAVIEW_WINDOW_EVENT_GESTURE_ZOOM };
                        e.gesture.scale_ratio = (double)gi.ullArguments / (double)w->last_zoom_distance;
                        e.gesture.center_x = (double)centre.x;
                        e.gesture.center_y = (double)centre.y;
                        queue_push(w, e);
                    }
                    w->last_zoom_distance = gi.ullArguments;
                    break;
                }

                case GID_PAN: {
                    rubraview_window_event_t e = { .kind = RUBRAVIEW_WINDOW_EVENT_GESTURE_PAN };
                    e.gesture.scale_ratio = 1.0;
                    e.gesture.dx = (double)(centre.x - w->last_pan_point.x);
                    e.gesture.dy = (double)(centre.y - w->last_pan_point.y);
                    e.gesture.center_x = (double)centre.x;
                    e.gesture.center_y = (double)centre.y;
                    if (e.gesture.dx != 0.0 || e.gesture.dy != 0.0) queue_push(w, e);
                    w->last_pan_point = centre;
                    break;
                }

                default:
                    break;
            }

            CloseGestureInfoHandle((HGESTUREINFO)lparam);
            return 0;
        }

        case WM_EXITSIZEMOVE: {
            rubraview_window_event_t e = { .kind = RUBRAVIEW_WINDOW_EVENT_MOVED };
            queue_push(w, e);
            break;
        }

        case WM_MOUSEACTIVATE:
            /* A tool window is clicked without taking the keyboard away from the viewer. */
            if (w->tool) return MA_NOACTIVATE;
            break;

        case WM_SYSCOMMAND:
            /* RFC-0002 §6.2 / RFC-0003 §5.5: Alt pressed and released on its
               own would put the window in menu mode and eat the next key.
               lparam 0 is exactly that case; Alt+Space (the system menu)
               arrives with the space and still opens. */
            if ((wparam & 0xFFF0) == SC_KEYMENU && lparam == 0) return 0;
            return DefWindowProcW(hwnd, msg, wparam, lparam);

        case WM_CLOSE: {
            w->should_close = true;
            rubraview_window_event_t e = { .kind = RUBRAVIEW_WINDOW_EVENT_CLOSE };
            queue_push(w, e);
            return 0;
        }

        case WM_DESTROY:
            w->should_close = true;
            /* Only the main window's end is the program's end. The
               settings window closing must not take the viewer with it. */
            if (!w->owned) PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* Place a frame, pulled inside the work area of the monitor nearest to
   it: no larger than that area, no part of it past an edge or under the
   taskbar. */
static void keep_on_screen(HWND hwnd, int x, int y, int ww, int wh) {
    RECT want = { x, y, x + ww, y + wh };
    MONITORINFO mi = { .cbSize = sizeof(mi) };
    if (!GetMonitorInfoW(MonitorFromRect(&want, MONITOR_DEFAULTTONEAREST), &mi)) return;
    RECT wa = mi.rcWork;
    int aw = wa.right - wa.left, ah = wa.bottom - wa.top;
    if (ww > aw) ww = aw;
    if (wh > ah) wh = ah;
    if (x + ww > wa.right) x = wa.right - ww;
    if (y + wh > wa.bottom) y = wa.bottom - wh;
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;
    SetWindowPos(hwnd, NULL, x, y, ww, wh, SWP_NOZORDER | SWP_NOACTIVATE);
}

rubraview_window_t *rubraview_pal_window_create(proven_arena_t *arena, const rubraview_window_config_t *config) {
    if (!arena || !config) return NULL;

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    set_process_dpi_awareness_context_fn set_dpi_ctx = NULL;
    get_dpi_for_window_fn get_dpi = NULL;
    if (user32) {
        set_dpi_ctx = (set_process_dpi_awareness_context_fn)(void*)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        get_dpi = (get_dpi_for_window_fn)(void*)GetProcAddress(user32, "GetDpiForWindow");
    }
    if (set_dpi_ctx) {
        set_dpi_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); /* §4.2 */
    }

    proven_result_mem_mut_t res = proven_arena_alloc(arena, sizeof(struct rubraview_window));
    if (!proven_is_ok(res.err)) return NULL;
    struct rubraview_window *w = (struct rubraview_window*)(void*)res.value.ptr;
    memset(w, 0, sizeof(*w));
    w->frameless = config->frameless && !config->owner;
    w->owned = config->owner != NULL;
    w->tool = config->tool_window;
    w->cursor_visible = true;
    w->dpi_scale = 1.0;
    w->get_dpi_for_window = get_dpi;
    w->saved_placement.length = sizeof(WINDOWPLACEMENT);

    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSEXW wc = {
        .cbSize = sizeof(WNDCLASSEXW),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = window_proc,
        .hInstance = instance,
        .hCursor = LoadCursorW(NULL, IDC_ARROW),
        .lpszClassName = RUBRAVIEW_WINDOW_CLASS,
    };
    RegisterClassExW(&wc); /* a duplicate registration is harmless here */

    int width = config->width > 0 ? config->width : 1280;
    int height = config->height > 0 ? config->height : 800;

    DWORD style = w->tool ? WS_POPUP
        : w->frameless ? (WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX)
        : WS_OVERLAPPEDWINDOW;
    DWORD ex_style = w->tool ? (WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_NOACTIVATE) : 0;
    /* An owned window: a parent handle on a top-level window makes it
       owned (not a child) — above its owner, no taskbar button. */
    HWND owner_hwnd = config->owner ? config->owner->hwnd : NULL;

    WCHAR wide_title[256];
    const char *title = config->title ? config->title : "Rubraview";
    if (MultiByteToWideChar(CP_UTF8, 0, title, -1, wide_title, 256) <= 0) {
        wcscpy(wide_title, L"Rubraview");
    }

    HWND hwnd = CreateWindowExW(ex_style, RUBRAVIEW_WINDOW_CLASS, wide_title, style,
                                CW_USEDEFAULT, CW_USEDEFAULT, width, height,
                                owner_hwnd, NULL, instance, w);
    if (!hwnd) return NULL;

    w->hwnd = hwnd;
    update_dpi_scale(w);

    if (w->frameless) {
        /* §3.21.1: keep the OS drop shadow even without a visible frame. */
        MARGINS margins = { 0, 0, 1, 0 };
        DwmExtendFrameIntoClientArea(hwnd, &margins);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    /* The requested size can exceed a small screen, and CW_USEDEFAULT
       then leaves the window over the taskbar and past the edge. Keep
       the first placement inside the monitor's work area. */
    {
        RECT r;
        if (GetWindowRect(hwnd, &r)) {
            keep_on_screen(hwnd, r.left, r.top, r.right - r.left, r.bottom - r.top);
        }
    }

    /* §3.6.5: ask for pinch and two-finger pan. Configuring gestures can
       fail on a machine with no touch digitiser, which is not an error —
       mouse and keyboard remain fully operable. */
    GESTURECONFIG gesture_config[] = {
        { .dwID = GID_ZOOM, .dwWant = GC_ZOOM, .dwBlock = 0 },
        { .dwID = GID_PAN,  .dwWant = GC_PAN,  .dwBlock = 0 },
    };
    SetGestureConfig(hwnd, 0,
                     (UINT)(sizeof(gesture_config) / sizeof(gesture_config[0])),
                     gesture_config, sizeof(GESTURECONFIG));

    RECT client;
    GetClientRect(hwnd, &client);
    w->width = client.right - client.left;
    w->height = client.bottom - client.top;

    if (w->tool) SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    ShowWindow(hwnd, w->tool ? SW_SHOWNOACTIVATE : SW_SHOW);
    UpdateWindow(hwnd);
    return w;
}

void rubraview_pal_window_destroy(rubraview_window_t *window) {
    if (!window || !window->hwnd) return;
    DestroyWindow(window->hwnd);
    window->hwnd = NULL;
    /* The struct itself belongs to the caller's arena. */
}

bool rubraview_pal_window_poll_event(rubraview_window_t *window, rubraview_window_event_t *out_event) {
    if (!window || !out_event) return false;

    /* Pump the OS queue first so WndProc can fill our own queue. */
    MSG msg;
    while (window->queue_count == 0 && PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (msg.message == WM_QUIT) {
            window->should_close = true;
            break;
        }
    }

    if (window->queue_count == 0) return false;

    *out_event = window->queue[window->queue_head];
    window->queue_head = (window->queue_head + 1) % EVENT_QUEUE_CAPACITY;
    window->queue_count--;
    return true;
}

void rubraview_pal_window_set_title(rubraview_window_t *window, const char *title_utf8) {
    if (!window || !window->hwnd || !title_utf8) return;
    WCHAR wide[512];
    if (MultiByteToWideChar(CP_UTF8, 0, title_utf8, -1, wide, 512) <= 0) return;
    SetWindowTextW(window->hwnd, wide);
}

void rubraview_pal_window_wait_event(rubraview_window_t *window, uint32_t timeout_ms) {
    if (!window || window->queue_count > 0 || window->should_close) return;
    /* MWMO_INPUTAVAILABLE also wakes for input that an earlier Peek
       already saw but left in the queue. */
    MsgWaitForMultipleObjectsEx(0, NULL, timeout_ms, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
}

void rubraview_pal_window_get_size(const rubraview_window_t *window, int32_t *out_width, int32_t *out_height) {
    if (!window) return;
    if (out_width) *out_width = window->width;
    if (out_height) *out_height = window->height;
}

bool rubraview_pal_window_get_frame(const rubraview_window_t *window,
                                    int32_t *out_x, int32_t *out_y, int32_t *out_width, int32_t *out_height) {
    RECT r;
    if (!window || !window->hwnd || !GetWindowRect(window->hwnd, &r)) return false;
    if (out_x) *out_x = r.left;
    if (out_y) *out_y = r.top;
    if (out_width) *out_width = r.right - r.left;
    if (out_height) *out_height = r.bottom - r.top;
    return true;
}

void rubraview_pal_window_set_frame(rubraview_window_t *window, int32_t x, int32_t y, int32_t width, int32_t height) {
    if (!window || !window->hwnd || width <= 0 || height <= 0) return;
    keep_on_screen(window->hwnd, x, y, width, height);
}

void rubraview_pal_window_set_opacity(rubraview_window_t *window, double percent) {
    if (!window || !window->hwnd || !window->tool) return;
    if (percent < 30.0) percent = 30.0;
    if (percent > 100.0) percent = 100.0;
    SetLayeredWindowAttributes(window->hwnd, 0, (BYTE)lround(percent * 2.55), LWA_ALPHA);
}

void rubraview_pal_window_set_visible(rubraview_window_t *window, bool visible) {
    if (!window || !window->hwnd) return;
    if (visible) {
        window->should_close = false;
        ShowWindow(window->hwnd, SW_SHOW);
        SetForegroundWindow(window->hwnd);
    } else {
        ShowWindow(window->hwnd, SW_HIDE);
    }
}

double rubraview_pal_window_dpi_scale(const rubraview_window_t *window) {
    return window ? window->dpi_scale : 1.0;
}

void *rubraview_pal_window_native_handle(const rubraview_window_t *window) {
    return window ? (void*)window->hwnd : NULL;
}

void rubraview_pal_window_set_fullscreen(rubraview_window_t *window, bool enabled) {
    if (!window || !window->hwnd || window->fullscreen == enabled) return;

    if (enabled) {
        window->saved_style = GetWindowLongW(window->hwnd, GWL_STYLE);
        window->saved_ex_style = GetWindowLongW(window->hwnd, GWL_EXSTYLE);
        GetWindowPlacement(window->hwnd, &window->saved_placement);

        HMONITOR monitor = MonitorFromWindow(window->hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { .cbSize = sizeof(mi) };
        if (!GetMonitorInfoW(monitor, &mi)) return;

        window->fullscreen = true;
        SetWindowLongW(window->hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        /* §3.21.1: true fullscreen covers rcMonitor, taskbar included. */
        SetWindowPos(window->hwnd, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
        window->fullscreen = false;
        SetWindowLongW(window->hwnd, GWL_STYLE, window->saved_style);
        SetWindowLongW(window->hwnd, GWL_EXSTYLE, window->saved_ex_style);
        SetWindowPlacement(window->hwnd, &window->saved_placement);
        SetWindowPos(window->hwnd, NULL, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    }
}

bool rubraview_pal_window_is_fullscreen(const rubraview_window_t *window) {
    return window ? window->fullscreen : false;
}

void rubraview_pal_window_set_cursor_visible(rubraview_window_t *window, bool visible) {
    if (!window || window->cursor_visible == visible) return;
    window->cursor_visible = visible;
    /* ShowCursor keeps an internal counter, so it is only stepped on an
       actual change — matching the flag we track here. */
    ShowCursor(visible ? TRUE : FALSE);
}

void rubraview_pal_window_begin_drag(rubraview_window_t *window) {
    if (!window || !window->hwnd) return;
    /* Hand the drag to the OS so Aero Snap keeps working (§3.21.2). */
    ReleaseCapture();
    SendMessageW(window->hwnd, WM_SYSCOMMAND, SC_MOVE | 0x0002, 0);
}

void rubraview_pal_window_minimize(rubraview_window_t *window) {
    if (!window || !window->hwnd) return;
    ShowWindow(window->hwnd, SW_MINIMIZE);
}

void rubraview_pal_window_toggle_maximize(rubraview_window_t *window) {
    if (!window || !window->hwnd) return;
    ShowWindow(window->hwnd, IsZoomed(window->hwnd) ? SW_RESTORE : SW_MAXIMIZE);
}

bool rubraview_pal_window_should_close(const rubraview_window_t *window) {
    return window ? window->should_close : true;
}

void rubraview_pal_window_request_close(rubraview_window_t *window) {
    if (window) window->should_close = true;
}


/* ---- §3.19 lifecycle and shell integration ---- */

/* Held for the life of the process: the mutex existing is the signal,
   so releasing it early would make a second instance think it is the
   first. */
static HANDLE g_instance_mutex = NULL;

bool rubraview_pal_instance_claim(void) {
    if (g_instance_mutex) return true;   /* already claimed by this process */

    g_instance_mutex = CreateMutexW(NULL, FALSE, L"Local\\Rubraview_SingleInstance_Mutex");
    if (!g_instance_mutex) return true;  /* cannot tell: behave as if alone */

    /* The mutex is kept even when it already existed — releasing it
       would hand the name to nobody and confuse the next launch. */
    return GetLastError() != ERROR_ALREADY_EXISTS;
}

bool rubraview_pal_instance_hand_over(u8str_t path) {
    HWND target = FindWindowW(RUBRAVIEW_WINDOW_CLASS, NULL);
    if (!target) return false;

    if (IsIconic(target)) ShowWindow(target, SW_RESTORE);
    SetForegroundWindow(target);

    /* An empty path is a legitimate hand-over: "come to the front". */
    if (path.len == 0) return true;

    char buffer[1024];
    size_t length = path.len < sizeof(buffer) - 1 ? path.len : sizeof(buffer) - 1;
    memcpy(buffer, path.ptr, length);
    buffer[length] = '\0';

    COPYDATASTRUCT data = {
        .dwData = 1,   /* RUBRAVIEW_IPC_CMD_OPEN */
        .cbData = (DWORD)(length + 1),
        .lpData = buffer,
    };
    SendMessageW(target, WM_COPYDATA, 0, (LPARAM)&data);
    return true;
}

void rubraview_pal_window_accept_drops(rubraview_window_t *window, bool accept) {
    if (!window || !window->hwnd) return;
    DragAcceptFiles(window->hwnd, accept ? TRUE : FALSE);
}

/* ---- §3.19.3 file associations ---- */

/* Writes one string value, creating the key. */
static bool write_key(HKEY root, const WCHAR *subkey, const WCHAR *value_name, const WCHAR *value) {
    HKEY key = NULL;
    if (RegCreateKeyExW(root, subkey, 0, NULL, 0, KEY_WRITE, NULL, &key, NULL) != ERROR_SUCCESS) {
        return false;
    }
    LONG result = RegSetValueExW(key, value_name, 0, REG_SZ, (const BYTE*)value,
                                 (DWORD)((wcslen(value) + 1) * sizeof(WCHAR)));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

/* Splits the caller's ';'-separated list, calling `each` per extension.
   Both register and unregister walk it the same way, which is what keeps
   them from disagreeing about what was written. */
static bool for_each_extension(u8str_t list, bool (*each)(const WCHAR *ext, const WCHAR *progid)) {
    bool all_ok = true;
    size_t start = 0;

    for (size_t i = 0; i <= list.len; ++i) {
        if (i != list.len && list.ptr[i] != ';') continue;
        size_t length = i - start;
        if (length == 0 || length > 16) { start = i + 1; continue; }

        char narrow[32];
        memcpy(narrow, list.ptr + start, length);
        narrow[length] = '\0';

        WCHAR ext[64];
        WCHAR progid[128];
        ext[0] = L'.';
        int written = MultiByteToWideChar(CP_UTF8, 0, narrow, -1, ext + 1, 62);
        if (written > 0) {
            wcscpy(progid, L"Rubraview.");
            wcscat(progid, ext + 1);
            if (!each(ext, progid)) all_ok = false;
        }
        start = i + 1;
    }
    return all_ok;
}

static bool register_one(const WCHAR *ext, const WCHAR *progid) {
    WCHAR exe[MAX_PATH * 2];
    if (GetModuleFileNameW(NULL, exe, (DWORD)(sizeof(exe) / sizeof(exe[0]))) == 0) return false;

    WCHAR command[MAX_PATH * 3];
    wcscpy(command, L"\"");
    wcscat(command, exe);
    wcscat(command, L"\" \"%1\"");

    WCHAR subkey[256];

    wcscpy(subkey, L"Software\\Classes\\");
    wcscat(subkey, progid);
    wcscat(subkey, L"\\shell\\open\\command");
    if (!write_key(HKEY_CURRENT_USER, subkey, NULL, command)) return false;

    wcscpy(subkey, L"Software\\Classes\\");
    wcscat(subkey, progid);
    wcscat(subkey, L"\\DefaultIcon");
    WCHAR icon[MAX_PATH * 2 + 8];
    wcscpy(icon, exe);
    wcscat(icon, L",0");
    write_key(HKEY_CURRENT_USER, subkey, NULL, icon);

    /* The extension points at the ProgID. Writing this under HKCU rather
       than HKCR means no administrator rights are needed and nothing
       another user relies on is touched. */
    wcscpy(subkey, L"Software\\Classes\\");
    wcscat(subkey, ext);
    return write_key(HKEY_CURRENT_USER, subkey, NULL, progid);
}

/* Deletes a key and everything under it. */
static void delete_tree(HKEY root, const WCHAR *subkey) {
    RegDeleteTreeW(root, subkey);
}

static bool unregister_one(const WCHAR *ext, const WCHAR *progid) {
    WCHAR subkey[256];

    wcscpy(subkey, L"Software\\Classes\\");
    wcscat(subkey, progid);
    delete_tree(HKEY_CURRENT_USER, subkey);

    /* The extension key is only removed when it still points at us: a
       reader who has since chosen another program must not have their
       choice deleted. */
    wcscpy(subkey, L"Software\\Classes\\");
    wcscat(subkey, ext);

    HKEY key = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        WCHAR value[128];
        DWORD size = sizeof(value);
        DWORD type = 0;
        bool ours = RegQueryValueExW(key, NULL, NULL, &type, (BYTE*)value, &size) == ERROR_SUCCESS &&
                    type == REG_SZ && wcscmp(value, progid) == 0;
        RegCloseKey(key);
        if (ours) delete_tree(HKEY_CURRENT_USER, subkey);
    }
    return true;
}

bool rubraview_pal_shell_register(u8str_t extensions_semicolon_list) {
    bool ok = for_each_extension(extensions_semicolon_list, register_one);
    /* Explorer caches associations; without this the change does not
       show until the next sign-in. */
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return ok;
}

bool rubraview_pal_shell_unregister(u8str_t extensions_semicolon_list) {
    bool ok = for_each_extension(extensions_semicolon_list, unregister_one);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return ok;
}

#endif /* _WIN32 */
