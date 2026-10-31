#include "rubraview/default_keymap.h"

const char *rubraview_default_keymap(void) {
    return

    /* RV-184 (D-219): a section a kind of file, asked in the order rubraview_keymap_mode_chain gives — a mode's
       own (text; a film's, playing's, a picture's), then navigation, file and common. One key means one thing
       or a like one in every kind of file: ] and [ the folder or archive, Ctrl+] and Ctrl+[ faster and slower
       (a film's speed, the slide show's wait, a text's scrolling), Plus and Minus bigger and smaller, Shift+N
       the next code page. test_default_keymap (T269) holds every key bound twice to that. */

    /* Windows, bars and the program's own (§3.7.2). D-13: every binding sits in a section (INI refuses a key
       before the first one) and every value is a quoted string (TOML needs quotes). */
    "[common]\n"
    "toggle_fullscreen = \"F, Alt+Enter\"\n"
    "toggle_toolbox = \"T\"\n"
    /* RFC-0002 Q6: pin it open, or give it a window of its own. */
    "toggle_toolbox_pin = \"Shift+T\"\n"
    "toggle_toolbox_detach = \"Ctrl+T\"\n"
    /* Owner, 2026-09-15: always on top, one chord away as well as in the menu and the titlebar. */
    "toggle_always_on_top = \"Ctrl+Shift+T\"\n"
    /* Owner, 2026-09-29: I is the file's information; the status line moves to Shift+I. */
    "toggle_osd = \"Shift+I\"\n"
    /* Owner, 2026-10-16: the viewer once more, in a window of its own. */
    "new_window = \"Ctrl+N\"\n"
    /* §3.22.1. RV-211 (owner, 2026-10-31: "펑션키 F1~F12 쓰는 단축키는 다른 키로 옮겨주세요"): no function keys —
       the help on ? (Shift+/), as many programs have it. */
    "open_settings = \"Ctrl+Comma\"\n"
    "toggle_help = \"Shift+Slash\"\n"
    /* Owner, 2026-09-29: the file's size, pixels, codec, EXIF — in a window whose text can be copied. */
    "toggle_info = \"I, Ctrl+I\"\n"
    /* §3.14.6 (owner, 2026-09-24): the music in a window of its own — a song's, or the one behind the pictures. */
    "toggle_miniplayer = \"Shift+P\"\n"
    "quit = \"Escape\"\n"
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
    /* RV-214 (owner, 2026-10-31: "위아래는 동영상과 이미지 둘 다 볼륨조절로"): Up and Down the volume wherever, a text's own
       lines first; with a picture it is the music behind it. */
    "media_volume_up = \"Up\"\n"
    "media_volume_down = \"Down\"\n"
    "\n"

    /* What is done to files: opening, saving, changing, keeping (RV-184: once in [ui]). */
    "[file]\n"
    "open_picker = \"O, Ctrl+O\"\n"
    "open_folder = \"Ctrl+Shift+O\"\n"
    "open_recent_list = \"Ctrl+Shift+R\"\n"   /* the owner (2026-10-17): the recent files in a list window */
    /* §3.13 / §3.10 / §3.11 */
    "open_edit = \"E\"\n"
    "quick_export = \"Ctrl+E\"\n"
    "save_as = \"Ctrl+Shift+S\"\n"
    "open_batch = \"Ctrl+B\"\n"
    /* §3.18: triage. Delete is safe — it goes to the recycle bin — and
       Shift+Delete asks before it is not. */
    "delete_file = \"Delete\"\n"
    "purge_file = \"Shift+Delete\"\n"
    "undo = \"Ctrl+Z\"\n"
    "rename_file = \"Ctrl+M\"\n"   /* RV-211: was F2 */
    /* Owner, 2026-10-09: an Explorer window with the file on screen picked
       out — the archive itself for a page inside one. */
    "reveal_in_explorer = \"Ctrl+Enter\"\n"
    /* D-86: the picture on screen through the system's print dialog. */
    "print = \"Ctrl+P\"\n"
    /* Owner, 2026-10-08: the file on screen copied to its folder, or moved
       to its own (Settings > Files). RV-211: C and M with Ctrl+Shift (were F6 and F7). */
    "copy_to_folder = \"Ctrl+Shift+C\"\n"
    "move_to_folder = \"Ctrl+Shift+M\"\n"
    /* Owner, 2026-10-16: any folder, typed or browsed, made when it is not there. RV-211: with Alt (were Shift+F6, F7). */
    "copy_to_other_folder = \"Alt+C\"\n"
    "move_to_other_folder = \"Alt+M\"\n"
    "toggle_classify = \"K\"\n"   /* RV-133: set files aside for the nine folders */
    /* RV-108: the frame on screen of a film or an animation, or the picture itself; a text's selection is its own Ctrl+C. */
    "copy_frame = \"Ctrl+C\"\n"
    "save_frame = \"Ctrl+Shift+E\"\n"
    "\n"

    "[navigation]\n"
    /* Owner, 2026-09-09: page turning is on these keys and nothing else
       — PageDown/PageUp, Space/Enter and Backspace. The arrows, J/K and
       A/D were bound here too; they were removed on request so that what
       moves between files is a short, deliberate list. */
    /* . and , as well (owner, 2026-10-14); in a multi-page file they step its pages first ([multipage]),
       and in an animation or a film its frames ([playback]). */
    /* RV-214 (owner, 2026-10-31: "좌우키는 시간 이동이나 다음이전 페이지로 넘기는 기능으로"): Right and Left turn the page
       again; a film's own (seek) are asked first. */
    "next_page = \"PageDown, Space, Enter, Period, Right\"\n"
    "prev_page = \"PageUp, Backspace, Shift+Space, Comma, Left\"\n"
    "first_page = \"Home, Ctrl+Home\"\n"
    "last_page = \"End, Ctrl+End\"\n"
    "skip_forward = \"Ctrl+PageDown\"\n"   /* RV-214: Shift+arrows move the picture now */
    "skip_backward = \"Ctrl+PageUp\"\n"
    /* §3.7.2 gives "up to folder" Backspace and Alt+Up; Backspace turns a
       page (owner, 2026-09-09), Alt+Up moves the window and Ctrl+Up sizes
       it (D-16). Ctrl+Backspace: back, one step wider. */
    "up_to_folder = \"Ctrl+Backspace\"\n"
    /* §3.8.1 point 4; in a folder, the folder beside (RV-179). The plain brackets, the keys used more (owner,
       2026-10-29, RV-183), in a film too (RV-184). */
    "next_archive = \"BracketRight\"\n"
    "prev_archive = \"BracketLeft\"\n"
    /* Owner, 2026-10-08: the list of what the archive (or the folder) holds,
       with a preview, in a window of its own. */
    "toggle_file_list = \"Ctrl+L\"\n"   /* RV-211: was F3 */
    "toggle_filmstrip = \"Shift+F\"\n"  /* RV-211: was F4; F is the whole screen */
    /* Owner, 2026-09-29: the playlist (or this folder's files), floating over the picture. */
    "toggle_playlist = \"P\"\n"
    /* The open archive's names, read in the next code page (owner, 2026-10-01); a text's own is the same key. */
    "archive_names_cycle = \"Shift+N\"\n"
    "\n"

    "[image]\n"
    "fit_window = \"1\"\n"
    "fit_width = \"2\"\n"
    "fit_height = \"3\"\n"
    "actual_size = \"4\"\n"
    "zoom_reset = \"0\"\n"                 /* back to the fitted size, which is 100% (D-97); 4 is real size (owner, 2026-10-15) */
    "smart_fit = \"5\"\n"
    "fit_stretch = \"6\"\n"
    "aspect_cycle = \"Shift+A\"\n"       /* the shape the picture is shown in, round (D-94) */
    "toggle_fit_lock = \"L\"\n"
    "zoom_in = \"Plus\"\n"
    "zoom_out = \"Minus\"\n"
    /* RV-214 (owner, 2026-10-31: "쉬프트 화살표키를 동영상, 이미지 둘 다에서 화면 위에서 이동하는 키로"): Shift and an
       arrow move a picture or a film larger than the window (the arrows alone did, 2026-10-14). */
    "pan_left = \"Shift+Left\"\n"
    "pan_right = \"Shift+Right\"\n"
    "pan_up = \"Shift+Up\"\n"
    "pan_down = \"Shift+Down\"\n"
    "rotate_cw = \"R\"\n"
    "rotate_ccw = \"Shift+R\"\n"
    "flip_horizontal = \"H\"\n"
    "flip_vertical = \"V\"\n"
    "toggle_nearest = \"N\"\n"
    "toggle_pixel_grid = \"G\"\n"
    "toggle_layout = \"B\"\n"
    "toggle_spread_detect = \"Shift+B\"\n"
    "\n"

    /* §3.20.2. A multi-page TIFF or an ICO does not advance by itself,
       so its sub-pages are stepped rather than played. */
    "[multipage]\n"
    "subpage_next = \"Period\"\n"
    "subpage_prev = \"Comma\"\n"
    "\n"

    /* Asked first while one runs; S starts one from anywhere (asked last otherwise; F5 too until RV-211). Ctrl+] is faster —
       a shorter wait — as it is a film's speed and a text's scrolling (RV-184). */
    "[slideshow]\n"
    "toggle_slideshow = \"S\"\n"
    "interval_down = \"Ctrl+BracketRight\"\n"
    "interval_up = \"Ctrl+BracketLeft\"\n"
    "interval_down_fine = \"Ctrl+Shift+BracketRight\"\n"
    "interval_up_fine = \"Ctrl+Shift+BracketLeft\"\n"
    "\n"

    /* §3.20.1: a film, music or an animated picture (D-16: once [animation], then [media]). A mode's own
       context is asked first, which lets `Space` both turn a page and play, and `.` `,` step a page or a frame. */
    "[playback]\n"
    "media_play_pause = \"Space\"\n"
    "anim_step_forward = \"Period\"\n"
    "anim_step_back = \"Comma\"\n"
    "anim_speed_up = \"Ctrl+BracketRight\"\n"   /* RV-184: faster and slower, as everywhere */
    "anim_speed_down = \"Ctrl+BracketLeft\"\n"
    "media_speed_reset = \"Ctrl+Backslash\"\n"
    /* D-16 (owner, 2026-09-15): the arrows seek 5 s and change the volume
       5 % while a video or music page is on screen. They turn no pages
       (owner, 2026-09-09), so nothing else is lost. */
    "media_seek_forward = \"Right\"\n"
    "media_seek_back = \"Left\"\n"
    /* 30 s (owner, 2026-09-29); RV-214: on Shift+PageDown / PageUp, Shift+arrows moving the picture. */
    "media_seek_forward_long = \"Shift+PageDown\"\n"
    "media_seek_forward_fine = \"Ctrl+Shift+Right\"\n"
    "media_seek_back_fine = \"Ctrl+Shift+Left\"\n"
    "media_seek_back_long = \"Shift+PageUp\"\n"
    "media_mute = \"Shift+M\"\n"
    /* D-15 A-B repeat: the brackets with Shift (RV-184), the two ends marked. */
    "media_ab_a = \"Shift+BracketLeft\"\n"
    "media_ab_b = \"Shift+BracketRight\"\n"
    "media_ab_clear = \"Backslash\"\n"
    /* The same two points, typed (owner, 2026-09-24). */
    "media_ab_edit = \"Shift+Backslash\"\n"
    /* Owner, 2026-09-29: at the end of a film or a song — stop, next, this one again, all round, shuffle. */
    "media_repeat_cycle = \"Ctrl+R\"\n"
    "\n"

    "[video]\n"
    /* §3.16.1 / R135: an external subtitle that runs early or late is
       nudged half a second at a time. */
    "subtitle_earlier = \"Z\"\n"
    "subtitle_later = \"X\"\n"
    /* A tenth of a second at a time, and back to the file's own times (owner, 2026-10-16). */
    "subtitle_earlier_fine = \"Shift+Z\"\n"
    "subtitle_later_fine = \"Shift+X\"\n"
    "subtitle_sync_reset = \"Alt+Z\"\n"
    /* §3.16.2 / R135: the next sound track, and the next subtitle. */
    "next_audio_track = \"A\"\n"
    "next_subtitle_track = \"C\"\n"
    /* RV-115: the subtitles as a list in a window of its own. */
    "toggle_subtitle_window = \"Shift+C\"\n"
    "\n"

    /* RFC-0005: a text page's own keys, before the rest (owner, 2026-10-17:
       auto-scroll first, its speed changed while it runs; the size and the
       line spacing easy to change). While auto-scroll runs, Up and Down are
       its speed too. RV-184: faster and slower on Ctrl+] and Ctrl+[, bigger and smaller on Plus and Minus,
       the spacing with Alt (as Alt+wheel), the encoding on Shift+N; E is Edit and Ctrl+L the file list here too. */
    "[text]\n"
    "text_autoscroll = \"S, Space\"\n"   /* S, akin to the slide show (owner, 2026-10-20); Space too (2026-10-18) */
    "text_faster = \"Ctrl+BracketRight, Shift+Up\"\n"
    "text_slower = \"Ctrl+BracketLeft, Shift+Down\"\n"
    "text_line_down = \"Down\"\n"
    "text_line_up = \"Up\"\n"
    "text_screen_down = \"PageDown\"\n"
    "text_screen_up = \"PageUp, Shift+Space\"\n"
    "text_top = \"Home\"\n"
    "text_bottom = \"End\"\n"
    "text_bigger = \"Plus, Ctrl+Plus\"\n"
    "text_smaller = \"Minus, Ctrl+Minus\"\n"
    "text_spacing_more = \"Alt+Plus\"\n"
    "text_spacing_less = \"Alt+Minus\"\n"
    "text_breaks = \"B\"\n"
    "text_indent = \"Shift+B\"\n"
    "text_wrap = \"W\"\n"
    "text_find = \"Ctrl+F\"\n"
    "text_find_next = \"Ctrl+G\"\n"
    "text_find_prev = \"Ctrl+Shift+G\"\n"
    "text_encoding = \"Shift+N\"\n"
    "text_copy = \"Ctrl+C\"\n"
    "text_select_all = \"Ctrl+A\"\n";
}

const char *rubraview_default_keymap_retired(void) {
    return
    "[common]\n"
    "toggle_fullscreen = \"F, F11, Alt+Enter\"\n"
    "open_settings = \"F10, Ctrl+Comma\"\n"
    "toggle_help = \"F1\"\n"
    "[file]\n"
    "rename_file = \"F2\"\n"
    "copy_to_folder = \"F6\"\n"
    "move_to_folder = \"F7\"\n"
    "copy_to_other_folder = \"Shift+F6\"\n"
    "move_to_other_folder = \"Shift+F7\"\n"
    "[navigation]\n"
    "toggle_file_list = \"F3\"\n"
    "toggle_filmstrip = \"F4\"\n"
    "[slideshow]\n"
    "toggle_slideshow = \"S, F5\"\n"
    /* RV-214: the arrows' defaults before Shift moved the picture */
    "[image]\n"
    "pan_left = \"Left\"\n"
    "pan_right = \"Right\"\n"
    "pan_up = \"Up\"\n"
    "pan_down = \"Down\"\n"
    "[navigation]\n"
    "next_page = \"PageDown, Space, Enter, Period\"\n"
    "prev_page = \"PageUp, Backspace, Shift+Space, Comma\"\n"
    "skip_forward = \"Shift+Right, Ctrl+PageDown\"\n"
    "skip_backward = \"Shift+Left, Ctrl+PageUp\"\n"
    "[playback]\n"
    "media_seek_forward_long = \"Shift+Right\"\n"
    "media_seek_back_long = \"Shift+Left\"\n"
    "media_volume_up = \"Up\"\n"
    "media_volume_down = \"Down\"\n";
}
