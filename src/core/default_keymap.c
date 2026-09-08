#include "rubraview/default_keymap.h"

const char *rubraview_default_keymap(void) {
    return

    /* UI & Windows (§3.7.2) */
    "toggle_fullscreen = F, F11, Alt+Enter\n"
    "toggle_menu = Tab, F1\n"
    "toggle_toolbox = T, F2\n"
    "open_picker = O, Ctrl+O\n"
    "open_folder = Ctrl+Shift+O\n"
    "toggle_filmstrip = F4\n"
    "toggle_osd = I\n"
    "toggle_pixel_grid = G\n"
    "quit = Escape\n"
    "\n"
    "[navigation]\n"
    "next_page = Right, PageDown, Space, J, D\n"
    "prev_page = Left, PageUp, Shift+Space, K, A\n"
    "first_page = Home, Ctrl+Home\n"
    "last_page = End, Ctrl+End\n"
    "skip_forward = Shift+Right, Ctrl+PageDown\n"
    "skip_backward = Shift+Left, Ctrl+PageUp\n"
    /* §3.7.2 offers `Alt+Up` as an alternative here, but that chord is
       also panning's primary; "up to folder" keeps its own primary,
       Backspace, and panning keeps the whole Alt+Arrow set. */
    "up_to_folder = Backspace\n"
    "toggle_layout = B\n"
    "toggle_reading_order = M\n"
    "toggle_spread_detect = Shift+B\n"
    "\n"
    "[view]\n"
    "fit_window = 1\n"
    "fit_width = 2\n"
    "fit_height = 3\n"
    "actual_size = 4, 0, Ctrl+0\n"
    "smart_fit = 5\n"
    "fit_stretch = Ctrl+1\n"
    "toggle_fit_lock = L\n"
    "zoom_in = Plus\n"
    "zoom_out = Minus\n"
    /* §3.7.2 lists both `Alt + Arrow keys` and `W A S D` for panning,
       but it also gives A and D to page navigation and S to the slide
       show. Those primary bindings win, so panning keeps the arrows. */
    "pan_left = Alt+Left\n"
    "pan_right = Alt+Right\n"
    "pan_up = Alt+Up\n"
    "pan_down = Alt+Down\n"
    "rotate_cw = R\n"
    "rotate_ccw = Shift+R\n"
    "flip_horizontal = H\n"
    "flip_vertical = V\n"
    "toggle_nearest = N\n"
    "\n"
    "[slideshow]\n"
    "toggle_slideshow = S, F5\n"
    "interval_up = BracketRight\n"
    "interval_down = BracketLeft\n"
    "interval_up_fine = Shift+BracketRight\n"
    "interval_down_fine = Shift+BracketLeft\n";
}
