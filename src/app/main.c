/*
 * Rubraview application entry point (RFC-0001 §11 milestones M2 and M3).
 *
 * M2 brought up the window, the Direct2D canvas and WIC decoding. M3
 * adds the reading UI on top: the dual floating boxes (RV-020), pointer
 * and wheel semantics (RV-037), keymap.ini loaded from disk (RV-038),
 * the OSD (RV-039), the hover titlebar (RV-040), the slide show
 * (RV-015), non-destructive rotation (RV-041) and the filmstrip
 * (RV-042).
 *
 * Everything the UI decides — hit testing, timers, menu navigation,
 * pointer intent — lives in the portable core and is covered by the host
 * test suite; this file is the wiring, and the drawing calls behind the
 * PAL.
 */

#include "app.h"

/* The file-scope variables app.h declares (RV-163). */
bool g_text_on = true;
double g_zoom_step = 1.1;
bool g_whole_zoom = false;          /* Settings > Viewer > Enlarge by whole steps only */
int  g_cli_sort = -1;               /* --sort= for this run; -1: the setting */
int  g_cli_descending = -1;         /* --descending / --ascending; -1: the setting */
bool g_cli_subfolders = false;      /* --recursive, without --batch */
rubraview_transition_pose_t g_pose = { 0.0, 1.0, 1.0 };   /* where draw_spread puts what it draws */
bool g_pose_on = false;
uint32_t g_canvas = 0xFF101010u;    /* Settings > Viewer > Behind and between pages */
uint32_t g_subtitle_argb = 0xFFFFFFFFu;
unsigned g_subtitle_style = 0;      /* stepped when the subtitle's font changes: a kept layout is measured again */
double g_gutter = 8.0;
uint32_t g_accent = 0xFFC0304Au;
double g_tile_base = 64.0;
double g_view_ratio = 0.0;
bool g_view_turned = false;

void open_path(app_state_t *app, proven_u8str_view_t path);
void recent_note(app_state_t *app, proven_u8str_view_t path);
void recent_note_shown(app_state_t *app);
void recent_sync(app_state_t *app, proven_u8str_view_t opened, bool clear);
void recent_forget(app_state_t *app, const proven_u8str_view_t *paths, size_t count);
void recentwin_build(app_state_t *app);
void recentwin_toggle(app_state_t *app);
void favwin_build(app_state_t *app);
void favwin_toggle(app_state_t *app);

void update_precache(app_state_t *app);

/* The animation helpers below sit next to page loading, which is where
   frames are decoded, but they ask which page is on screen — a question
   the layout answers further down. */
int32_t current_page_index(const app_state_t *app);
void tiles_reset(app_state_t *app);

/* The workbench and the two dialogs are defined further down, next to
   the drawing they belong with; the key handler above needs to name
   them. */
bool open_folder(app_state_t *app, proven_u8str_view_t dir);
void folder_relist(app_state_t *app);
void open_paths_as_set(app_state_t *app, const proven_u8str_view_t *paths, size_t count, const char *what);
bool path_is_listed_kind(proven_u8str_view_t path);
void open_set_ordered(app_state_t *app, rubraview_fs_entry_t *entries, size_t count, proven_u8str_view_t said,
                             bool as_given, proven_u8str_view_t start);
void source_close(app_state_t *app);
void triage_delete(app_state_t *app, bool permanent);
void triage_undo(app_state_t *app);
void triage_curate(app_state_t *app, int32_t digit);
void classify_mark(app_state_t *app, int32_t digit);
void batch_typed(app_state_t *app);
void info_local_time(char *buf, size_t cap, int64_t unix_seconds);
bool filing_exists(void *ctx, proven_u8str_view_t path);
void classify_toggle(app_state_t *app);
bool classify_press(app_state_t *app, double px, double py);
void filing_put(app_state_t *app, bool moving);
void rename_begin(app_state_t *app);
void rename_commit(app_state_t *app);
void password_ask(app_state_t *app, proven_u8str_view_t archive_path, bool wrong);
void secure_wipe(void *p, size_t n);
void password_commit(app_state_t *app);
void password_remember(app_state_t *app, proven_u8str_view_t password);
void picker_path_begin(app_state_t *app);
proven_u8str_view_t app_keep(app_state_t *app, proven_u8str_view_t text);
rubraview_sort_mode_t folder_sort_mode(const app_state_t *app, bool *out_ascending);
bool settings_copy_key(app_state_t *app, rubraview_key_combo_t combo);
bool settings_row_is_text(const app_state_t *app, int32_t row);
bool pagebar_shown(const app_state_t *app);
void pagebar_press(app_state_t *app, double bar_x, double bar_width, double x, bool detached);
void pagebar_drag(app_state_t *app, double x, bool released);
bool key_is(rubraview_key_combo_t combo, const char *name);
void help_show(app_state_t *app);
void info_show(app_state_t *app);
void mini_show(app_state_t *app);
bool mini_paused(const app_state_t *app);
void mini_close(app_state_t *app);
void files_show(app_state_t *app);
void subs_show(app_state_t *app);
void subs_close(app_state_t *app);
bool subs_may_leave(app_state_t *app);
void append_arg(char *buf, size_t cap, size_t *pos, proven_u8str_view_t text);
void append_arg_cstr(char *buf, size_t cap, size_t *pos, const char *text);
void slides_follow_screen(app_state_t *app);
bool popups_key(app_state_t *app, rubraview_key_combo_t combo);
bool hint_shown(const app_state_t *app);
bool slide_ready(const app_state_t *app);
void takeout_begin(app_state_t *app);
void reopen_after_removal(app_state_t *app, size_t removed_index);
void reopen_on_path(app_state_t *app, proven_u8str_view_t path, size_t fallback);
void files_close(app_state_t *app);
void draw_rename_box(app_state_t *app, double win_w, double win_h, double dpi);
void draw_eqwin(app_state_t *app, double win_w, double win_h);
void draw_ab_edit_box(app_state_t *app, double win_w, double win_h, double dpi);
void ab_edit_begin(app_state_t *app);
void draw_notice(app_state_t *app, double win_w, double win_h, double chrome_dpi);
void rename_end(app_state_t *app);
void finish_open(app_state_t *app, size_t start_page);

void panel_close(app_state_t *app);
bool curve_widget_press(app_state_t *app, double px, double py, bool remove);
rubraview_pal_rect_t curve_widget_rect(const app_state_t *app);
bool edit_panel_open(const app_state_t *app);
bool crop_point_to_image(app_state_t *app, double px, double py, int32_t *out_x, int32_t *out_y);
bool point_in_curve_widget(const app_state_t *app, double px, double py);
void append_number(char *buf, size_t cap, size_t *pos, size_t value);
void history_remember(app_state_t *app);
bool open_archive(app_state_t *app, proven_u8str_view_t archive_path);
void archive_names_reread(app_state_t *app, rubraview_codepage_t codepage);
void append_text(char *buf, size_t cap, size_t *pos, const char *text);
void osd_say(app_state_t *app, proven_u8str_view_t text);
void panel_build_edit(app_state_t *app);
void strip_follow(app_state_t *app, size_t page);
bool pal_rect_has(rubraview_pal_rect_t r, double x, double y);
void chrome_hide(app_state_t *app, bool hide);
void open_playlist(app_state_t *app, proven_u8str_view_t path);
bool path_is_playlist(proven_u8str_view_t path);
void speed_tile_press(app_state_t *app, double y, bool detached, bool interval);
void speed_tile_move(app_state_t *app, double y, bool detached);
void speed_tile_release(app_state_t *app, bool detached);
proven_u8str_view_t files_fit_text(char *buf, size_t cap, proven_u8str_view_t text, double width, double size);
void settings_open(app_state_t *app);
void settings_close(app_state_t *app);
void settings_path_begin(app_state_t *app, const rubraview_setting_def_t *def);
void skip_dirs_take_old(app_state_t *app);
bool is_text_path(proven_u8str_view_t path);
void text_prepare(app_state_t *app);
void text_close(app_state_t *app);
bool text_action(app_state_t *app, proven_u8str_view_t action);
bool text_active(const app_state_t *app);
bool text_wheel(app_state_t *app, double notches, uint32_t mods);
bool text_press(app_state_t *app, double x, double y, uint32_t mods);
void text_drag(app_state_t *app, double x, double y);
bool text_release(app_state_t *app, double x, double y);
void text_tick(app_state_t *app, double now);
bool text_pages_mode(const app_state_t *app);
int text_percent(const app_state_t *app);
bool text_moving(const app_state_t *app);
void text_find_step(app_state_t *app, bool forward);
bool recent_kept_out(const app_state_t *app, proven_u8str_view_t path);
const char *text_encoding_shown(const app_state_t *app);
void shell_menu_take_old(app_state_t *app, proven_u8str_view_t text);
void target_begin(app_state_t *app, bool moving, bool from_picker);
void target_commit(app_state_t *app);
void target_browse(app_state_t *app);
void filing_put_to(app_state_t *app, proven_u8str_view_t dir, bool moving);
void skip_dirs_table_rows(app_state_t *app);
void layout_save(app_state_t *app);
void settings_took_effect(app_state_t *app);
void ui_font_apply_all(app_state_t *app);
void fontpick_show(app_state_t *app, proven_u8str_view_t section, proven_u8str_view_t key);
void fontpick_close(app_state_t *app);
void edit_window_layout(app_state_t *app);
void edit_window_show(app_state_t *app);
void edit_window_close(app_state_t *app);
void changes_remove_checked(app_state_t *app);
void edit_steps_changed(app_state_t *app);
void edit_preview_close(app_state_t *app);
bool edit_denies(proven_u8str_view_t action);
void sel_mode_set(app_state_t *app, bool on);
void fontpick_reveal(app_state_t *app);
void media_ab_check(app_state_t *app);
void dispatch_key(app_state_t *app, rubraview_key_combo_t combo);
void audio_track_set(app_state_t *app, int32_t next);
bool chooser_open_tile(app_state_t *app, proven_u8str_view_t action, double x, double y, double above_y);
bool chooser_wheel_tile(app_state_t *app, proven_u8str_view_t action, double notches);
void ctx_open_at(app_state_t *app, double x, double y);
double name_scale(const app_state_t *app);
bool settings_scrollbar(const app_state_t *app, rubraview_pal_rect_t *track, rubraview_pal_rect_t *thumb);
int place_font_begin(app_state_t *app, int which, double *size);
void place_font_end(app_state_t *app, int before);
double place_size(app_state_t *app, int which);
double chooser_main_x(app_state_t *app);
void draw_text_weight(app_state_t *app, proven_u8str_view_t text, rubraview_pal_rect_t r, double size, uint32_t argb, bool bold);
bool tmenu_key(app_state_t *app, rubraview_key_combo_t combo);
bool tmenu_press(app_state_t *app, double x, double y);
double chooser_main_y(app_state_t *app);
int text_enc_now(const app_state_t *app);
void chooser_open_panel(app_state_t *app, int32_t which, int32_t row, double x, double y, double above_y);
void chooser_open_setting(app_state_t *app);
void chooser_open_sort(app_state_t *app, bool order, double x, double y, double above_y);
void chooser_close(app_state_t *app);
bool chooser_press(app_state_t *app, int where, double x, double y);
void chooser_open_listext(app_state_t *app, int where, double x, double y, double above_y);
void chooser_open_listsort(app_state_t *app, int where, double x, double y, double above_y);
void list_sort(app_state_t *app, int key, bool desc);
const char *list_sort_word(int key);
bool chooser_key(app_state_t *app, int where, rubraview_key_combo_t combo);
bool chooser_wheel(app_state_t *app, int where, double notches);
void draw_chooser(app_state_t *app, rubraview_renderer_t *r, int where, double px, double py);
void text_encoding_set(app_state_t *app, rubraview_textenc_t next);
bool text_rebuild(app_state_t *app);
void toolbox_detach(app_state_t *app, int32_t screen_x, int32_t screen_y, bool follow_pointer);
void toolbox_dock(app_state_t *app, double client_x, double client_y);
void settings_write_if_changed(app_state_t *app, bool say);
void load_keymap(app_state_t *app);
void settings_say(app_state_t *app, const char *text);
void settings_number_begin(app_state_t *app);
void panel_open_edit(app_state_t *app);
void edit_save_over(app_state_t *app);
void edit_sel_sync(app_state_t *app);
bool edit_apply_pending(app_state_t *app, bool say);
void edit_live_update(app_state_t *app, int32_t kind);
void edit_live_close(app_state_t *app);
void apply_all_begin(app_state_t *app);
void apply_all_start(app_state_t *app, apply_all_job_t *job);
void apply_all_originals(app_state_t *app);
void apply_all_typed(app_state_t *app);
void apply_all_poll(app_state_t *app, bool quitting);
void edit_undo(app_state_t *app, bool redo);
void edit_effect_apply(app_state_t *app);
void edit_auto_colour(app_state_t *app);
const char *effect_word(int32_t e);
void edit_value_typed(app_state_t *app);
void gmic_typed(app_state_t *app);
const char *gmic_step_name(const rubraview_edit_step_t *step);
void ffmpeg_typed(app_state_t *app);
void address_typed(app_state_t *app);
void address_begin(app_state_t *app);
void panel_open_export(app_state_t *app);
void panel_open_batch(app_state_t *app);
void new_window(app_state_t *app);
void panel_write_archive_page(app_state_t *app, bool over);
void panel_run_batch(app_state_t *app);
void print_paths(app_state_t *app, const proven_u8str_view_t *paths, size_t count);
proven_u8str_view_t current_file_path(app_state_t *app);
bool on_archive_page(const app_state_t *app);
void arc_edit_request(app_state_t *app, rubraview_arcedit_op_t op, size_t entry, const char *name, uint8_t *data, size_t size);
void arc_edit_on_page(app_state_t *app, rubraview_arcedit_op_t op, const char *name);
bool arc_ask_key(app_state_t *app, rubraview_key_combo_t combo);
void arc_job_poll(app_state_t *app, bool quitting);
bool arc_undo(app_state_t *app);
void print_archive_page(app_state_t *app);
void picker_open(app_state_t *app);
void media_toggle_pause(app_state_t *app);

#ifdef _WIN32



int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show) {
    (void)instance; (void)previous; (void)command_line; (void)show;

    /* The picker's PC page lists every drive: an empty card reader or DVD
       opened from it must read as an empty folder, not stop the viewer
       behind Windows' "There is no disk in the drive" box. */
    SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS);

    {   /* RFC-0007: a plug-in runs in a child of this program, and nothing else starts in that child */
        int argc = 0;
        LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (wargv && argc == 4 && wcscmp(wargv[1], L"--filter-host") == 0) {
            int code = rubraview_pal_plugin_host_main(wargv[2], wargv[3]);
            LocalFree(wargv);
            return code;
        }
        if (wargv) LocalFree(wargv);
    }
    rubraview_edit_set_plugin_runner(plugin_runner, NULL);

    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
        return 1;
    }

    /* Before any window exists. A single-threaded apartment that is not
       pumping messages can deadlock while activating a COM object, and
       the first image is opened before the message loop has run once —
       so this has to happen while there is no window to deadlock
       against. Without it the program hangs on the first picture, which
       from the outside looks like an image that will not render. */
    rubraview_pal_image_startup();

    /* RFC-0006: the program's memory by use, every part of it on the heap. What lasts
       for the session is `persist`, a region of heap chunks: when the one in use runs
       low the next turn of the loop moves on to a new one, and the older ones stay. */
    static rubraview_mem_t mem;
    if (!rubraview_mem_init(&mem)) {
        CoUninitialize();
        return 1;
    }
    proven_arena_t *const arena = &mem.persist.arena;

    /* §3.11: the command line is read before a window is created,
       because `--batch` must not open one. */
    {
        int code = 0;
        if (run_command_line(arena, &code)) {   /* D-198 Q3-B: it ended the run */
            rubraview_mem_release(&mem);
            CoUninitialize();
            return code;
        }
    }

    app_state_t app = {0};
    if (!app_start(&app, &mem)) {   /* D-198 Q3: the window or its renderer could not be made */
        rubraview_mem_release(&mem);
        CoUninitialize();
        return 1;
    }

    app.last_frame_seconds = rubraview_pal_time_now_seconds();
    frame_clock_t frame_clock = {
        .last_busy_seconds = app.last_frame_seconds,
        .last_idle_frame_seconds = app.last_frame_seconds,
        .last_scene_signature = 0,
        .last_render_seconds = app.last_frame_seconds,
        .last_input_seconds = app.last_frame_seconds,
    };

    while (!rubraview_pal_window_should_close(app.window)) {
        rubraview_window_event_t event;
        size_t handled = 0;
        frame_fit_boxes(&app);
        while (rubraview_pal_window_poll_event(app.window, &event)) {
            handled++;
            frame_event(&app, &event);
        }

        frame_tick(&app, &frame_clock, handled);
    }

    app_end(&app);
    rubraview_mem_release(&mem);
    CoUninitialize();
    return 0;
}

#else

/*
 * The host build has no windowing backend (D-1: Windows only until 1.0).
 * The core and the POSIX filesystem/clock PALs are exercised by the test
 * suite instead; this entry point exists so the file compiles anywhere.
 */
int main(void) {
    return 0;
}

#endif /* _WIN32 */
