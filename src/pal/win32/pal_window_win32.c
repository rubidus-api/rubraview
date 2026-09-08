#ifdef _WIN32
#include <windows.h>
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
    bool fullscreen;
    int32_t width, height;
    double dpi_scale;

    /* Placement saved when entering fullscreen, restored on exit. */
    WINDOWPLACEMENT saved_placement;
    LONG saved_style;
    LONG saved_ex_style;

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

        case WM_CLOSE: {
            w->should_close = true;
            rubraview_window_event_t e = { .kind = RUBRAVIEW_WINDOW_EVENT_CLOSE };
            queue_push(w, e);
            return 0;
        }

        case WM_DESTROY:
            w->should_close = true;
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
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
    w->frameless = config->frameless;
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

    DWORD style = config->frameless
        ? (WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX)
        : WS_OVERLAPPEDWINDOW;

    WCHAR wide_title[256];
    const char *title = config->title ? config->title : "Rubraview";
    if (MultiByteToWideChar(CP_UTF8, 0, title, -1, wide_title, 256) <= 0) {
        wcscpy(wide_title, L"Rubraview");
    }

    HWND hwnd = CreateWindowExW(0, RUBRAVIEW_WINDOW_CLASS, wide_title, style,
                                CW_USEDEFAULT, CW_USEDEFAULT, width, height,
                                NULL, NULL, instance, w);
    if (!hwnd) return NULL;

    w->hwnd = hwnd;
    update_dpi_scale(w);

    if (config->frameless) {
        /* §3.21.1: keep the OS drop shadow even without a visible frame. */
        MARGINS margins = { 0, 0, 1, 0 };
        DwmExtendFrameIntoClientArea(hwnd, &margins);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    RECT client;
    GetClientRect(hwnd, &client);
    w->width = client.right - client.left;
    w->height = client.bottom - client.top;

    ShowWindow(hwnd, SW_SHOW);
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

void rubraview_pal_window_get_size(const rubraview_window_t *window, int32_t *out_width, int32_t *out_height) {
    if (!window) return;
    if (out_width) *out_width = window->width;
    if (out_height) *out_height = window->height;
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

bool rubraview_pal_window_should_close(const rubraview_window_t *window) {
    return window ? window->should_close : true;
}

void rubraview_pal_window_request_close(rubraview_window_t *window) {
    if (window) window->should_close = true;
}

#endif /* _WIN32 */
