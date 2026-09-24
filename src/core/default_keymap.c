#include "rubraview/default_keymap.h"

const char *rubraview_default_keymap(void) {
    return

    /* UI & Windows (§3.7.2) */
    /* D-13: every binding sits in a section (INI refuses a key before the
       first one) and every value is a quoted string (TOML needs quotes).
       `[ui]` is the context every other section falls back to. */
    "[ui]\n"
    "toggle_fullscreen = \"F, F11, Alt+Enter\"\n"
    "toggle_menu = \"Tab\"\n"   /* F1 is the help now (owner, 2026-09-23); Tab still opens the menu */
    /* F2 is rename's (§3.18.2), as in Explorer — owner, 2026-09-15 (D-14). */
    "toggle_toolbox = \"T\"\n"
    /* RFC-0002 Q6: pin it open, or give it a window of its own. */
    "toggle_toolbox_pin = \"Shift+T\"\n"
    "toggle_toolbox_detach = \"Ctrl+T\"\n"
    /* Owner, 2026-09-15: always on top, one chord away as well as in the menu and the titlebar. */
    "toggle_always_on_top = \"Ctrl+Shift+T\"\n"
    "open_picker = \"O, Ctrl+O\"\n"
    "open_folder = \"Ctrl+Shift+O\"\n"
    "toggle_filmstrip = \"F4\"\n"
    "toggle_osd = \"I\"\n"
    /* §3.13 / §3.10 / §3.11 */
    "open_edit = \"E\"\n"
    "quick_export = \"Ctrl+E\"\n"
    "save_as = \"Ctrl+Shift+S\"\n"
    "open_batch = \"Ctrl+B\"\n"
    /* §3.22.1 */
    "open_settings = \"F10, Ctrl+Comma\"\n"
    "toggle_help = \"F1\"\n"
    /* §3.14.6 (owner, 2026-09-24): the music in a window of its own. */
    "toggle_miniplayer = \"Shift+P\"\n"
    "toggle_pixel_grid = \"G\"\n"
    "quit = \"Escape\"\n"
    /* §3.18: triage. Delete is safe — it goes to the recycle bin — and
       Shift+Delete asks before it is not. */
    "delete_file = \"Delete\"\n"
    "purge_file = \"Shift+Delete\"\n"
    "undo = \"Ctrl+Z\"\n"
    "rename_file = \"F2\"\n"
    /* D-16 (owner, 2026-09-15): Ctrl + arrows size the window, Alt +
       arrows move it — everywhere, whatever is on screen. */
    "window_narrower = \"Ctrl+Left\"\n"
    "window_wider = \"Ctrl+Right\"\n"
    "window_shorter = \"Ctrl+Up\"\n"
    "window_taller = \"Ctrl+Down\"\n"
    "window_move_left = \"Alt+Left\"\n"
    "window_move_right = \"Alt+Right\"\n"
    "window_move_up = \"Alt+Up\"\n"
    "window_move_down = \"Alt+Down\"\n"
    "\n"
    "[navigation]\n"
    /* Owner, 2026-09-09: page turning is on these keys and nothing else
       — PageDown/PageUp, Space/Enter and Backspace. The arrows, J/K and
       A/D were bound here too; they were removed on request so that what
       moves between files is a short, deliberate list. */
    "next_page = \"PageDown, Space, Enter\"\n"
    "prev_page = \"PageUp, Backspace, Shift+Space\"\n"
    "first_page = \"Home, Ctrl+Home\"\n"
    "last_page = \"End, Ctrl+End\"\n"
    "skip_forward = \"Shift+Right, Ctrl+PageDown\"\n"
    "skip_backward = \"Shift+Left, Ctrl+PageUp\"\n"
    /* §3.7.2 gives "up to folder" Backspace and Alt+Up; Backspace turns a
       page (owner, 2026-09-09), Alt+Up moves the window and Ctrl+Up sizes
       it (D-16). Ctrl+Backspace: back, one step wider. */
    "up_to_folder = \"Ctrl+Backspace\"\n"
    "toggle_layout = \"B\"\n"
    "toggle_reading_order = \"M\"\n"
    "toggle_spread_detect = \"Shift+B\"\n"
    /* §3.8.1 point 4. §3.7.2 also offers Ctrl+PageDown/PageUp here, but
       those are skipping's primary and skipping keeps them. */
    "next_archive = \"Ctrl+BracketRight\"\n"
    "prev_archive = \"Ctrl+BracketLeft\"\n"
    "\n"
    "[view]\n"
    "fit_window = \"1\"\n"
    "fit_width = \"2\"\n"
    "fit_height = \"3\"\n"
    "actual_size = \"4, 0, Ctrl+0\"\n"
    "smart_fit = \"5\"\n"
    "fit_stretch = \"Ctrl+1\"\n"
    "toggle_fit_lock = \"L\"\n"
    "zoom_in = \"Plus\"\n"
    "zoom_out = \"Minus\"\n"
    "rotate_cw = \"R\"\n"
    "rotate_ccw = \"Shift+R\"\n"
    "flip_horizontal = \"H\"\n"
    "flip_vertical = \"V\"\n"
    "toggle_nearest = \"N\"\n"
    "\n"
    "[slideshow]\n"
    "toggle_slideshow = \"S, F5\"\n"
    "interval_up = \"BracketRight\"\n"
    "interval_down = \"BracketLeft\"\n"
    "interval_up_fine = \"Shift+BracketRight\"\n"
    "interval_down_fine = \"Shift+BracketLeft\"\n"
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
    "[media]\n"
    /* D-16: the context was [animation]; a keymap.ini that still says so
       is read as [media]. Space plays and pauses; stop is a tile. */
    "media_play_pause = \"Space\"\n"
    "anim_step_forward = \"Period\"\n"
    "anim_step_back = \"Comma\"\n"
    "anim_speed_up = \"Ctrl+BracketRight\"\n"
    "anim_speed_down = \"Ctrl+BracketLeft\"\n"
    /* D-16 (owner, 2026-09-15): the arrows seek 5 s and change the volume
       5 % while a video or music page is on screen. They turn no pages
       (owner, 2026-09-09), so nothing else is lost. */
    "media_seek_forward = \"Right\"\n"
    "media_seek_back = \"Left\"\n"
    "media_volume_up = \"Up\"\n"
    "media_volume_down = \"Down\"\n"
    "media_mute = \"Shift+M\"\n"
    /* D-15 A-B repeat, the keys RFC-0001 §3.7.2 gives it. [ and ] are the
       slide show's interval keys too; that context is asked only while a
       slide show runs, so here they mean A and B. */
    "media_ab_a = \"BracketLeft\"\n"
    "media_ab_b = \"BracketRight\"\n"
    "media_ab_clear = \"Backslash\"\n"
    /* The same two points, typed (owner, 2026-09-24). */
    "media_ab_edit = \"Shift+Backslash\"\n"
    "media_speed_reset = \"Ctrl+Backslash\"\n"
    /* §3.16.1 / R135: an external subtitle that runs early or late is
       nudged half a second at a time. */
    "subtitle_earlier = \"Z\"\n"
    "subtitle_later = \"X\"\n"
    /* §3.16.2 / R135: the next sound track, and the next subtitle. */
    "next_audio_track = \"A\"\n"
    "next_subtitle_track = \"C\"\n"
    "\n"

    /* §3.20.2. A multi-page TIFF or an ICO does not advance by itself,
       so its sub-pages are stepped rather than played. */
    "[subpage]\n"
    "subpage_next = \"Period\"\n"
    "subpage_prev = \"Comma\"\n";
}
