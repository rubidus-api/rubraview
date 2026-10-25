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
        int argc = 0;
        LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (wargv) {
            /* Every argument: Explorer's menu may name many files (D-86). */
            int usable = 0;
            const char **argv_utf8 = NULL;
            proven_result_mem_mut_t list = rubraview_arena_alloc_array(arena, (size_t)(argc > 0 ? argc : 1), sizeof(char*));
            if (proven_is_ok(list.err)) {
                argv_utf8 = (const char**)(void*)list.value.ptr;
                for (int i = 0; i < argc; ++i) {
                    argv_utf8[i] = "";
                    int need = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, NULL, 0, NULL, NULL);
                    proven_result_mem_mut_t one = need > 0 ? proven_arena_alloc(arena, (size_t)need)
                                                           : (proven_result_mem_mut_t){ .err = PROVEN_ERR_OVERFLOW };
                    if (proven_is_ok(one.err) &&
                        WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, (char*)(void*)one.value.ptr, need, NULL, NULL) > 0) {
                        argv_utf8[i] = (const char*)(void*)one.value.ptr;
                    }
                }
                usable = argc;
            }
            LocalFree(wargv);

            rubraview_cli_result_t cli;
            rubraview_cli_parse(&cli, arena, usable, argv_utf8);

            if (cli.err != RUBRAVIEW_CLI_OK) {
                char message[512];
                size_t pos = 0;
                message[0] = '\0';
                append_text(message, sizeof(message), &pos, "rubraview: ");
                proven_u8str_view_t reason = rubraview_cli_error_text(cli.err);
                for (size_t i = 0; i < reason.size && pos + 1 < sizeof(message); ++i) {
                    message[pos++] = reason.ptr[i];
                }
                message[pos] = '\0';
                if (cli.offending.size > 0) {
                    append_text(message, sizeof(message), &pos, ": ");
                    for (size_t i = 0; i < cli.offending.size && pos + 1 < sizeof(message); ++i) {
                        message[pos++] = cli.offending.ptr[i];
                    }
                    message[pos] = '\0';
                }
                console_line(message);
                rubraview_mem_release(&mem);
                CoUninitialize();
                return 2;
            }

            if (cli.show_version) {
                console_line("rubraview " RUBRAVIEW_VERSION_STRING);
                rubraview_mem_release(&mem);
                CoUninitialize();
                return 0;
            }

            if (cli.probe_media) {
                int code = probe_media_file(arena, cli.input);
                rubraview_mem_release(&mem);
                CoUninitialize();
                return code;
            }

            if (cli.probe_gpu) {
                probe_gpu_resize();   /* D-38 first: it needs no file */
                int code = probe_gpu_file(arena, cli.input);
                rubraview_mem_release(&mem);
                CoUninitialize();
                return code;
            }

            if (cli.diagnostics) {
                int code = run_diagnostics(arena, cli.input);
                rubraview_mem_release(&mem);
                CoUninitialize();
                return code;
            }

            launch_paths_gather(arena, &cli);
            g_launch_verb = cli.shell_verb;
            g_launch_page = cli.start_page;
            /* RV-106: the order and the depth of this run's folders. */
            if (cli.sort_given) g_cli_sort = (int)cli.sort_mode;
            if (cli.descending_given) g_cli_descending = cli.descending ? 1 : 0;
            g_cli_subfolders = cli.recursive && !cli.batch_mode;
            g_launch_whole = cli.shell_whole;

            if (cli.batch_mode) {
                int code = run_batch(arena, &cli);
                rubraview_mem_release(&mem);
                CoUninitialize();
                return code;
            }

            /* §3.19.3, D-86: registering the file types and the menu is a job,
               not a launch. Without --types every extension is meant. */
            if (cli.wipe_shell) {
                /* D-110: this user's entries; with --all-users every user's, and the packages. */
                int removed = shell_wipe(cli.shell_all_users);
                console_line(removed < 0 ? "rubraview: could not remove the entries (every user's need an administrator)"
                                         : "rubraview: every Rubraview entry is out of Explorer's menu");
                rubraview_mem_release(&mem);
                CoUninitialize();
                return removed < 0 ? SHELL_FAILED : SHELL_DONE;
            }
            if (cli.register_shell || cli.unregister_shell) {
                int code = SHELL_FAILED;
                if (cli.register_shell) {
                    proven_u8str_view_t selection = cli.shell_types.size > 0 ? cli.shell_types : rubraview_shell_extensions_all();
                    shell_part_t types = cli.shell_no_types ? SHELL_PART_LEAVE : cli.shell_remove_types ? SHELL_PART_REMOVE : SHELL_PART_APPLY;
                    shell_part_t menu = cli.shell_no_menu ? SHELL_PART_LEAVE : cli.shell_remove_menu ? SHELL_PART_REMOVE : SHELL_PART_APPLY;
                    code = shell_apply(arena, selection, cli.shell_menu_types, types, menu, cli.shell_all_users);
                } else {
                    code = shell_remove(cli.shell_all_users);
                }
                console_line(code == SHELL_FAILED ? "rubraview: could not update the file types and the menu"
                           : code == SHELL_DONE_NO_PARTS ? "rubraview: updated; Windows 11's menu needs rubraview_menu.dll and rubraview_menu.msix beside the program"
                           : code == SHELL_DONE_REFUSED ? "rubraview: updated; Windows did not take the Windows 11 menu's package"
                           : code == SHELL_DONE_STORE ? "rubraview: updated; Windows 11's menu is left to the Microsoft Store's copy"
                           : "rubraview: file types and menu updated");
                if (cli.shell_setup) code = rubraview_shell_setup_exit(code, cli.unregister_shell && !cli.register_shell);
                rubraview_mem_release(&mem);
                CoUninitialize();
                return code;
            }

            /* §3.19.1: hand the file to the window that is already open,
               rather than opening a second one. */
            {
                /* The file the viewer itself reads (settings_read_file): beside the
                   executable in portable mode, under AppData otherwise. It was looked
                   for in the working folder, so an installed viewer never saw it. */
                proven_u8str_view_t exe = rubraview_pal_process_executable(arena);
                proven_u8str_view_t beside = exe.size > 0 ? rubraview_path_dirname(exe) : PROVEN_LIT(".");
                if (beside.size == 0) beside = PROVEN_LIT(".");
                proven_u8str_view_t portable_ini = rubraview_path_join(arena, beside, PROVEN_LIT("settings.ini"));
                proven_u8str_view_t appdata = PROVEN_LIT(".");
                char appdata_utf8[1024];
                DWORD appdata_len = GetEnvironmentVariableA("APPDATA", appdata_utf8, (DWORD)sizeof(appdata_utf8));
                if (appdata_len > 0 && appdata_len < sizeof(appdata_utf8)) appdata = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)appdata_utf8, .size = appdata_len };
                proven_u8str_view_t settings_file = rubraview_config_path(
                    arena, rubraview_config_mode_for(portable_ini.size > 0 && rubraview_pal_fs_exists(portable_ini)),
                    beside, appdata, PROVEN_LIT("settings.ini"));
                proven_u8str_view_t settings = rubraview_pal_fs_read_file(arena, settings_file, 256u * 1024u);
                /* Through the settings loader, which knows where the key
                   lives now ([general]) and where older files put it — and
                   what it is when no file says (D-99: a window each). */
                rubraview_settings_t loaded = rubraview_settings_load(arena, settings);
                bool single_instance = rubraview_settings_get(&loaded, PROVEN_LIT("general"), PROVEN_LIT("single_instance")) > 0.5;
                /* What the menu asks is for the window that is open (the
                   classic menu starts this once a selected file). */
                if (cli.shell_verb != RUBRAVIEW_SHELL_VERB_NONE) single_instance = true;
                /* D-100: but "Open in a new window" is a window of its own, always. */
                if (cli.new_instance || cli.shell_verb == RUBRAVIEW_SHELL_VERB_NEW_WINDOW) single_instance = false;

                bool alone = rubraview_pal_instance_claim();
                if (rubraview_instance_decide(single_instance, !alone) == RUBRAVIEW_INSTANCE_HAND_OVER) {
                    size_t need = rubraview_shellreq_pack(cli.shell_verb, cli.shell_whole, g_launch_paths, g_launch_count, NULL, 0);
                    char *blob = (char*)malloc(need);
                    bool sent = false;
                    if (blob) {
                        (void)rubraview_shellreq_pack(cli.shell_verb, cli.shell_whole, g_launch_paths, g_launch_count, blob, need);
                        /* The other launch may still be making its window
                           (several files opened at once): it is waited for. */
                        sent = rubraview_pal_instance_send_request(blob, need, 8000);
                        free(blob);
                    }
                    if (sent) {
                        rubraview_mem_release(&mem);
                        CoUninitialize();
                        return 0;
                    }
                    /* The mutex said someone was there but no window
                       answered — a crashed instance, most likely. Start
                       normally rather than refusing to run. */
                }
            }
        }
    }

    app_state_t app = {0};
    app.arena = arena;
    app.mem = &mem;
    app.fit_mode = RUBRAVIEW_FIT_WINDOW;
    app.zoom = 1.0;
    app.layout_exempt = -1;
    /* Found with RV-108: Export began at quality 0 — the options were never given their defaults. */
    app.export_options = rubraview_export_defaults();
    app.curve_dragging = -1;   /* no point held; 0 would mean the first one */
    app.orientation = rubraview_orientation_identity();
    app.spread_detect = true; /* §3.3.4 default */
    app.layout_opts = rubraview_layout_opts_default(RUBRAVIEW_PAGE_LAYOUT_SINGLE, RUBRAVIEW_READING_LTR);

    rubraview_window_config_t window_config = {
        /* The version is in the title so a screenshot identifies the
           build it came from — which is most of what a bug report needs. */
        .title = "Rubraview " RUBRAVIEW_VERSION_STRING,
        .width = 1280,
        .height = 800,
        .frameless = true, /* §2 invariant 5: zero-margin canvas */
    };
    app.window = rubraview_pal_window_create(arena, &window_config);
    if (!app.window) {
        rubraview_mem_release(&mem);
        CoUninitialize();
        return 1;
    }

    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app.window, &win_w, &win_h);
    app.renderer = rubraview_pal_render_create(arena, rubraview_pal_window_native_handle(app.window), win_w, win_h);
    ui_font_apply_all(&app);
    if (!app.renderer) {
        report_startup_failure();
        rubraview_pal_window_destroy(app.window);
        rubraview_mem_release(&mem);
        CoUninitialize();
        return 1;
    }

    double dpi = rubraview_pal_window_dpi_scale(app.window);

    /* §3.1: no worker pool. Every decode touches WIC, Direct2D and the
       arena, none of which may be used off this thread; the ring runs
       without one and precache_decode queues pages for the main loop. */
    app.jobs = NULL;
    app.media_page = -1;
    /* §3.22 [video] decoder (D-9): which backend opens a file first. The
       other one still gets its turn when this one cannot. */
    app.media_preferred = preferred_backend(arena);
    history_load(&app);
    load_keymap(&app);   /* after history_load: it settles portable or AppData, and so where keymap.ini is */
    /* history_load read settings.ini from wherever it lives (beside the
       program or in AppData); the decoder choice comes from that, not
       only from a file in the working folder. */
    settings_took_effect(&app);
    app.osd = rubraview_osd_create(2.0, 0.5);            /* §3.1 */
    app.osd_size_page = -1;
    app.titlebar = rubraview_titlebar_create(dpi);       /* §3.21.2 */
    app.titlebar.trigger_zone = fmax(36.0, rubraview_settings_get(&app.settings, PROVEN_LIT("general"), PROVEN_LIT("titlebar_trigger_px"))) * dpi;
    app.titlebar.hide_delay = rubraview_settings_get(&app.settings, PROVEN_LIT("general"), PROVEN_LIT("titlebar_hide_ms")) / 1000.0;
    app.listwin = rubraview_listwin_create(dpi);
    app.list_tip_page = -1;
    app.listwin_shown_page = -1;
    app.names_codepage = -1;
    app.eqwin = rubraview_eqwin_create(dpi);
    app.follow_for = -1;
    app.rename_favorite = -1;
    app.fav_press = -1;
    app.toolbox = rubraview_box_create(RUBRAVIEW_BOX_TOOLBOX, (double)win_w - 220.0 * dpi, (double)win_h - 160.0 * dpi, 8);   /* toolbox_refresh sets the real count */
    app.menubox = rubraview_box_create(RUBRAVIEW_BOX_MENU, 8.0 * dpi, 8.0 * dpi + app.titlebar.height, app.menu_tree.root_count);
    /* One in the top-left corner, one in the bottom-right (owner, 2026-10-11), until they are moved. */
    app.box_share[0][0] = app.box_share[0][1] = 1.0;
    app.box_share_known[0] = true;   /* the menu box's share is taken from its place, under the title bar (owner, 2026-10-15) */
    app.box_put[0][0] = app.toolbox.anchor_x; app.box_put[0][1] = app.toolbox.anchor_y;
    app.box_put[1][0] = app.menubox.anchor_x; app.box_put[1][1] = app.menubox.anchor_y;
    layout_load(&app);   /* §3.6: back where the reader left them */
    menu_rebuild(&app);   /* §3.6.2, RFC-0002 §5: the tree from the boxes' document */
    g_caption_app = &app;
    app.transition = rubraview_transition_create(RUBRAVIEW_TRANSITION_CROSSFADE, 0.25);
    app.settings_last_setting = -1;
    app.settings_tip_help = -1;
    app.cursor = rubraview_cursor_hide_create(1.5);      /* §3.2.5 */
    app.filmstrip = rubraview_filmstrip_create(0, FILMSTRIP_THUMB * dpi, (double)win_w);

    /* §3.18.3: the triage folders, and §3.19.2: accept drops. The folders
       are read from the settings file the viewer actually uses — until
       2026-09-23 this said `settings.ini`, a name with no folder, so the
       numbers only curated when the viewer happened to be started in a
       folder holding one, which is to say almost never. */
    {
        proven_u8str_view_t settings = rubraview_pal_fs_read_file(arena, app.settings_path, 256u * 1024u);
        app.curation = rubraview_curation_parse(arena, settings);
    }
    rubraview_pal_window_accept_drops(app.window, true);
    sync_menubox_tiles(&app);

    /* Let the freshly created window settle its own messages before the
       first file is opened. Decoding runs COM calls, and an apartment
       with an unpumped window is where those calls go to hang. */
    pump_messages();

    /* What this launch was given. A plain launch opens at once; what the
       menu asked is gathered with what the other launches of the same
       selection hand over (D-86). */
    app.launch_page = g_launch_page;
    if (g_launch_count > 0) {
        shell_request_take(&app, g_launch_verb, g_launch_whole || g_launch_verb == RUBRAVIEW_SHELL_VERB_NONE,
                           g_launch_paths, g_launch_count);
        if (rubraview_shellreq_ready(&app.shell_requests, rubraview_pal_time_now_seconds())) shell_requests_flush(&app);
    }
    startup_without_file(&app, g_launch_count == 0);

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
