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
    /* §3.13 / §3.10 / §3.11 */
    "open_edit = E\n"
    "quick_export = Ctrl+E\n"
    "save_as = Ctrl+Shift+S\n"
    "open_batch = Ctrl+B\n"
    "toggle_pixel_grid = G\n"
    "quit = Escape\n"
    /* §3.18: triage. Delete is safe — it goes to the recycle bin — and
       Shift+Delete asks before it is not. */
    "delete_file = Delete\n"
    "purge_file = Shift+Delete\n"
    "undo = Ctrl+Z\n"
    "rename_file = F2\n"
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
    /* §3.8.1 point 4. §3.7.2 also offers Ctrl+PageDown/PageUp here, but
       those are skipping's primary and skipping keeps them. */
    "next_archive = Ctrl+BracketRight\n"
    "prev_archive = Ctrl+BracketLeft\n"
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
    "interval_down_fine = Shift+BracketLeft\n"
    "\n"

    /* §3.20.1. This context is active only while an animated image is on
       screen, which is what lets these chords be spent twice: the
       specification gives `Space` to page turning (§3.7.2) and to
       play/pause (§3.20.1), and `Ctrl + [` / `Ctrl + ]` to archive
       stepping and to playback speed. A context-specific binding wins
       over the global one, so while a GIF is playing the keys mean the
       animation, and everywhere else they mean what they always meant.
       Nothing is unreachable: paging a folder of GIFs still works with
       Right/Left, PageDown/PageUp, J/K and D/A. */
    "[animation]\n"
    "anim_toggle_pause = Space\n"
    "anim_step_forward = Period\n"
    "anim_step_back = Comma\n"
    "anim_speed_up = Ctrl+BracketRight\n"
    "anim_speed_down = Ctrl+BracketLeft\n"
    "\n"

    /* §3.20.2. A multi-page TIFF or an ICO does not advance by itself,
       so its sub-pages are stepped rather than played. */
    "[subpage]\n"
    "subpage_next = Period\n"
    "subpage_prev = Comma\n";
}
