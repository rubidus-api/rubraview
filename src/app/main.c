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
static void tiles_reset(app_state_t *app);

/* The workbench and the two dialogs are defined further down, next to
   the drawing they belong with; the key handler above needs to name
   them. */
bool open_folder(app_state_t *app, proven_u8str_view_t dir);
void folder_relist(app_state_t *app);
static void open_paths_as_set(app_state_t *app, const proven_u8str_view_t *paths, size_t count, const char *what);
bool path_is_listed_kind(proven_u8str_view_t path);
void open_set_ordered(app_state_t *app, rubraview_fs_entry_t *entries, size_t count, proven_u8str_view_t said,
                             bool as_given, proven_u8str_view_t start);
void source_close(app_state_t *app);
static void triage_delete(app_state_t *app, bool permanent);
static void triage_undo(app_state_t *app);
static void triage_curate(app_state_t *app, int32_t digit);
static void classify_mark(app_state_t *app, int32_t digit);
void batch_typed(app_state_t *app);
void info_local_time(char *buf, size_t cap, int64_t unix_seconds);
bool filing_exists(void *ctx, proven_u8str_view_t path);
static void classify_toggle(app_state_t *app);
static bool classify_press(app_state_t *app, double px, double py);
static void filing_put(app_state_t *app, bool moving);
static void rename_begin(app_state_t *app);
static void rename_commit(app_state_t *app);
void password_ask(app_state_t *app, proven_u8str_view_t archive_path, bool wrong);
void secure_wipe(void *p, size_t n);
void password_commit(app_state_t *app);
void password_remember(app_state_t *app, proven_u8str_view_t password);
static void picker_path_begin(app_state_t *app);
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
static void slides_follow_screen(app_state_t *app);
static bool popups_key(app_state_t *app, rubraview_key_combo_t combo);
bool hint_shown(const app_state_t *app);
static bool slide_ready(const app_state_t *app);
void takeout_begin(app_state_t *app);
void reopen_after_removal(app_state_t *app, size_t removed_index);
void reopen_on_path(app_state_t *app, proven_u8str_view_t path, size_t fallback);
void files_close(app_state_t *app);
void draw_rename_box(app_state_t *app, double win_w, double win_h, double dpi);
static void draw_eqwin(app_state_t *app, double win_w, double win_h);
static void draw_ab_edit_box(app_state_t *app, double win_w, double win_h, double dpi);
void ab_edit_begin(app_state_t *app);
void draw_notice(app_state_t *app, double win_w, double win_h, double chrome_dpi);
void rename_end(app_state_t *app);
void finish_open(app_state_t *app, size_t start_page);

void panel_close(app_state_t *app);
bool curve_widget_press(app_state_t *app, double px, double py, bool remove);
rubraview_pal_rect_t curve_widget_rect(const app_state_t *app);
bool edit_panel_open(const app_state_t *app);
static bool crop_point_to_image(app_state_t *app, double px, double py, int32_t *out_x, int32_t *out_y);
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
static void chrome_hide(app_state_t *app, bool hide);
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
static void target_commit(app_state_t *app);
static void target_browse(app_state_t *app);
static void filing_put_to(app_state_t *app, proven_u8str_view_t dir, bool moving);
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
static bool edit_denies(proven_u8str_view_t action);
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
static bool tmenu_key(app_state_t *app, rubraview_key_combo_t combo);
static bool tmenu_press(app_state_t *app, double x, double y);
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
static void toolbox_detach(app_state_t *app, int32_t screen_x, int32_t screen_y, bool follow_pointer);
static void toolbox_dock(app_state_t *app, double client_x, double client_y);
void settings_write_if_changed(app_state_t *app, bool say);
void load_keymap(app_state_t *app);
void settings_say(app_state_t *app, const char *text);
void settings_number_begin(app_state_t *app);
void panel_open_edit(app_state_t *app);
static void edit_save_over(app_state_t *app);
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
static void edit_value_typed(app_state_t *app);
void gmic_typed(app_state_t *app);
const char *gmic_step_name(const rubraview_edit_step_t *step);
void ffmpeg_typed(app_state_t *app);
static void address_typed(app_state_t *app);
void address_begin(app_state_t *app);
void panel_open_export(app_state_t *app);
void panel_open_batch(app_state_t *app);
void new_window(app_state_t *app);
static void panel_write_archive_page(app_state_t *app, bool over);
void panel_run_batch(app_state_t *app);
static void print_paths(app_state_t *app, const proven_u8str_view_t *paths, size_t count);
proven_u8str_view_t current_file_path(app_state_t *app);
bool on_archive_page(const app_state_t *app);
static void arc_edit_request(app_state_t *app, rubraview_arcedit_op_t op, size_t entry, const char *name, uint8_t *data, size_t size);
static void arc_edit_on_page(app_state_t *app, rubraview_arcedit_op_t op, const char *name);
static bool arc_ask_key(app_state_t *app, rubraview_key_combo_t combo);
static void arc_job_poll(app_state_t *app, bool quitting);
static bool arc_undo(app_state_t *app);
static void print_archive_page(app_state_t *app);
void picker_open(app_state_t *app);
void media_toggle_pause(app_state_t *app);

size_t page_count(const app_state_t *app) {
    return app->source.page_count;
}

proven_u8str_view_t page_display_name(const app_state_t *app, size_t index) {
    if (index >= app->source.page_count) return PROVEN_LIT("");
    return app->source.pages[index].name;
}

/* ---- page loading ---- */

void unload_all_pages(app_state_t *app) {
    tiles_reset(app);   /* D-40: made for these pages, on this device */
    if (!app->pages) return;
    for (size_t i = 0; i < page_count(app); ++i) {
        if (app->pages[i].texture) {
            rubraview_pal_texture_destroy(app->pages[i].texture);
        }
        page_forget(&app->pages[i]);
    }
}

/* Whether a page is a video, opened by the media PAL rather than WIC. RV-157: an internet radio
   station is one too, played by FFmpeg alone. */
bool is_media_path(proven_u8str_view_t path) {
    return path.size > 0 && (rubraview_stream_is_address(path) ||
                            rubraview_glob_match_list(rubraview_path_basename(path), PROVEN_LIT(MEDIA_FILTER)));
}

/* RV-157: a page that is an internet radio address: nothing on the disk is asked about it. */
bool is_station_path(proven_u8str_view_t path) {
    return rubraview_stream_is_address(path);
}

/* ---- a page's bytes, and far pages off the main thread ---- */

/* The job in flight is called off and waited for — a 1 MB chunk at most —
   so the main thread can read. */
void page_job_stop(app_state_t *app) {
    page_job_t *job = app->page_job;
    if (!job) return;
    rubraview_page_source_cancel(&app->source, true);
    WaitForSingleObject(job->thread, INFINITE);
    CloseHandle(job->thread);
    rubraview_page_source_cancel(&app->source, false);
    free(job->mem);
    free(job);
    app->page_job = NULL;
}

/* What it costs to reach a page, when nothing is reading it: bytes to
   decode first. While the thread reads, the stream's place is moving. */
uint64_t page_read_cost(app_state_t *app, size_t index) {
    if (app->page_job) return UINT64_MAX;
    return rubraview_page_source_read_cost(&app->source, index);
}

/* A page's bytes in memory of their own, freed by the caller once they
   are a texture. They went into the 64 MB working arena, which is never
   given back: after a few dozen CB7 pages, or a compressed CBZ's worth,
   pages failed to open (found 2026-09-29). A folder page is read by path
   and allocates nothing. */
rubraview_page_bytes_t page_read_owned(rubraview_page_source_t *source, SRWLOCK *lock, size_t index, void **out_mem) {
    *out_mem = NULL;
    rubraview_page_bytes_t none = { .data = PROVEN_LIT(""), .from_disk = false, .ok = false };
    AcquireSRWLockExclusive(lock);
    rubraview_page_bytes_t bytes = none;
    if (source->kind == RUBRAVIEW_PAGE_SOURCE_FOLDER) {
        uint8_t nothing[64];
        proven_arena_t unused = proven_arena_create((proven_mem_mut_t){ .ptr = nothing, .size = sizeof(nothing) });
        bytes = rubraview_page_source_read(&unused, source, index, MAX_PAGE_BYTES);
    } else {
        size_t budget = rubraview_page_source_read_budget(source, index);
        void *mem = budget > 0 && budget <= (size_t)MAX_PAGE_BYTES + 4096u ? malloc(budget) : NULL;
        if (mem) {
            proven_arena_t scratch = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)mem, .size = budget });
            bytes = rubraview_page_source_read(&scratch, source, index, MAX_PAGE_BYTES);
            if (bytes.ok) *out_mem = mem; else free(mem);
        }
    }
    ReleaseSRWLockExclusive(lock);
    return bytes;
}

static DWORD WINAPI page_job_run(LPVOID param) {
    page_job_t *job = (page_job_t*)param;
    job->bytes = page_read_owned(job->source, job->lock, job->index, &job->mem);
    InterlockedExchange(&job->done, 1);
    rubraview_pal_window_wake(job->wake);
    return 0;
}

static void page_take_bytes(app_state_t *app, app_page_t *page, size_t index, rubraview_page_bytes_t bytes);
void update_window_title(app_state_t *app);
int32_t station_connecting_page(const app_state_t *app);

/* Each pass: a far page that has arrived becomes its texture. */
static void page_job_poll(app_state_t *app) {
    page_job_t *job = app->page_job;
    if (!job || !InterlockedCompareExchange(&job->done, 0, 0)) return;
    WaitForSingleObject(job->thread, INFINITE);
    CloseHandle(job->thread);
    app->page_job = NULL;
    if (job->index < page_count(app)) {
        app_page_t *page = &app->pages[job->index];
        if (!page->loaded) {
            if (job->bytes.ok) page_take_bytes(app, page, job->index, job->bytes);
            else page->failed = true;
            app->needs_relayout = true;
            update_window_title(app);
        }
    }
    free(job->mem);
    free(job);
}

static void page_job_start(app_state_t *app, size_t index) {
    page_job_stop(app);
    page_job_t *job = (page_job_t*)calloc(1, sizeof(*job));
    if (!job) return;
    job->source = &app->source;
    job->lock = &app->source_lock;
    job->index = index;
    job->wake = app->window;
    job->thread = CreateThread(NULL, 0, page_job_run, job, 0, NULL);
    if (!job->thread) { free(job); return; }
    app->page_job = job;
}

static app_page_t *ensure_page_loaded_how(app_state_t *app, int32_t index, bool on_screen);

app_page_t *ensure_page_loaded(app_state_t *app, int32_t index) {
    return ensure_page_loaded_how(app, index, true);
}

/* `on_screen`: the page is wanted now. A far page then goes to the thread;
   the look-ahead never starts a far read, nor waits for one. */
static app_page_t *ensure_page_loaded_how(app_state_t *app, int32_t index, bool on_screen) {
    if (index < 0 || (size_t)index >= page_count(app)) return NULL;
    app_page_t *page = &app->pages[index];
    if (page->loaded || page->failed) return page;
    /* A video page gets its texture from media_prepare, and only while
       it is the page on screen: the pre-cache must not open videos. */
    if (is_media_path(app->source.pages[index].path)) return page;
    if (is_text_path(app->source.pages[index].path) || is_text_path(app->source.pages[index].name)) return page;   /* RFC-0005 */

    if (app->page_job) {
        if (app->page_job->index == (size_t)index) return page;   /* on its way */
        if (!on_screen) return page;                              /* the look-ahead waits its turn */
        page_job_stop(app);                                       /* the reader has moved on */
    }
    if (page_read_cost(app, (size_t)index) > PAGE_JOB_MIN_COST) {
        if (on_screen) page_job_start(app, (size_t)index);
        return page;
    }

    void *mem = NULL;
    rubraview_page_bytes_t bytes = page_read_owned(&app->source, &app->source_lock, (size_t)index, &mem);
    if (!bytes.ok) {
        page->failed = true;
        return page;
    }
    page_take_bytes(app, page, (size_t)index, bytes);
    free(mem);
    if (rubraview_pal_fs_mapping_lost(&app->archive_map)) {
        page->failed = !page->loaded;
        osd_say(app, TU8("the archive is no longer there (its drive went away)"));
    }
    return page;
}

/* D-98: is this page a picture that moves? Looked for only in the kinds
   that can, in the file's bytes (mapped, for a file of a folder); nothing
   is decoded. `bytes` may be NULL when the caller has none in hand. */
static bool page_moves(app_state_t *app, size_t index, const rubraview_page_bytes_t *bytes) {
    proven_u8str_view_t path = app->source.pages[index].path;
    proven_u8str_view_t name = path.size > 0 ? path : app->source.pages[index].name;
    if (!rubraview_path_has_ext(name, ".gif") && !rubraview_path_has_ext(name, ".webp") &&
        !rubraview_path_has_ext(name, ".png") && !rubraview_path_has_ext(name, ".apng") &&
        !rubraview_path_has_ext(name, ".avif") && !rubraview_path_has_ext(name, ".jxl")) return false;
    if (path.size > 0) {
        rubraview_fs_mapping_t map = {0};
        bool moves = rubraview_pal_fs_map(path, &map) && map.size <= (uint64_t)SIZE_MAX &&
                     rubraview_image_is_animated(map.data, (size_t)map.size);
        rubraview_pal_fs_unmap(&map);
        return moves;
    }
    return bytes && bytes->ok && rubraview_image_is_animated((const uint8_t*)bytes->data.ptr, bytes->data.size);
}

static void page_take_bytes(app_state_t *app, app_page_t *page, size_t index, rubraview_page_bytes_t bytes) {

    /* A folder page is opened by path; an archive page is decoded from
       the bytes the source produced, which never touched the disk. */
    rubraview_image_load_result_t loaded = bytes.from_disk
        ? rubraview_pal_image_load_texture(app->renderer, app->source.pages[index].path, true)
        : rubraview_pal_image_load_texture_from_memory(app->renderer,
                                                       (const uint8_t*)bytes.data.ptr, bytes.data.size, true);
    if (!loaded.ok) {
        page->failed = true;
        return;
    }

    page->texture = loaded.texture;
    page->width = loaded.width;
    page->height = loaded.height;
    page->full_width = loaded.full_width;
    page->full_height = loaded.full_height;
    page->reduced = loaded.reduced;
    page->loaded = true;
    /* 1.25.0 (VM, 2026-10-20: a wide page read ahead stayed paired with the one before): a page whose shape
       is new to the layout — read for the first time, or not as it was taken — lays the pages out again */
    if (app->layout_opts.mode != RUBRAVIEW_PAGE_LAYOUT_SINGLE &&
        (page->seen_w != picture_w(page) || page->seen_h != picture_h(page))) app->needs_relayout = true;

    /* D-98: a picture that moves is no page of a book: found, the layouts for pictures leave it out. */
    if (page_moves(app, index, &bytes) && !page->animated) {
        page->animated = true;
        if (app->layout_opts.mode != RUBRAVIEW_PAGE_LAYOUT_SINGLE) app->needs_relayout = true;
    }
}

/* ---- animated images and sub-pages (§3.20) ---- */

/* A TIFF's pages or an ICO's sizes; an animation's frames are as many as the file has. */
#define ANIM_MAX_FRAMES 8192
/* Every frame's picture is kept while all of them together fit in this.
   Of a longer animation (D-84) every so-manieth is kept, and the frames
   between are drawn again from the nearest kept one before them. */
#define ANIM_CACHE_BYTES ((uint64_t)384 << 20)

static void next_spread(app_state_t *app);

/* D-82: an animation that plays by itself, and so is handled as a film
   is — a place in seconds, a seek bar, A-B, speed, stop. */
bool anim_plays(const app_state_t *app) {
    return app->anim_active && !app->media && app->animation.kind == RUBRAVIEW_FRAMES_ANIMATION;
}

/* What happens at its end: 0 round again, 1 stop there, 2 the next file. */
int anim_end_mode(const app_state_t *app) {
    int mode = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("animation_end")));
    return mode >= 0 && mode <= 2 ? mode : 0;
}

static void anim_close(app_state_t *app) {
    if (app->anim_cache) {
        for (size_t i = 0; i < app->animation.frame_count; ++i) free(app->anim_cache[i]);
        free(app->anim_cache);
        app->anim_cache = NULL;
    }
    app->anim_kept_bytes = 0;
    free(app->anim_buffers);
    app->anim_buffers = NULL;
    if (app->anim_reader) {
        /* The frame list was the reader's. */
        app->animation.frames = NULL;
        app->animation.frame_count = 0;
        app->anim_canvas.frames = NULL;
        app->anim_canvas.frame_count = 0;
    }
    rubraview_pal_anim_close(app->anim_reader);
    app->anim_reader = NULL;
    app->anim_texture = NULL;   /* the page owns it */
}

static bool anim_read_frame(void *ctx, size_t index, uint8_t *dst, size_t stride) {
    app_state_t *app = (app_state_t*)ctx;
    return rubraview_pal_anim_read(app->anim_reader, index, dst, stride, app->animation.frames[index].height);
}

/* One frame of the open animation as the whole picture, on the page. */
/* RV-108 (RFC-0001 §5.3, §3.20.1): the picture on screen of a film or an
   animation, copied to the clipboard or written as a PNG beside its file.
   It is asked for (`frame_wanted`) and taken where the next frame's
   pixels are at hand: a paused film is asked for its frame again. */
double play_position(const app_state_t *app);

/* RV-141 (5): a buffer's size from a picture's, checked — SIZE_MAX, which malloc refuses,
   when it does not fit (a width and a height come out of a file). */
size_t picture_bytes(size_t w, size_t h, size_t per_pixel, size_t extra) {
    size_t n = 0;
    if (!rubraview_size_product(&n, w, h, per_pixel) || PROVEN_CKD_ADD(&n, n, extra)) return SIZE_MAX;
    return n;
}

size_t bytes_scaled(size_t n, size_t k, size_t extra) {
    size_t out = 0;
    if (n == SIZE_MAX || PROVEN_CKD_MUL(&out, n, k) || PROVEN_CKD_ADD(&out, out, extra)) return SIZE_MAX;
    return out;
}

/* RV-116 (owner, 2026-10-14): Ctrl+C, or Copy picture, on a still picture — the
   picture as its file holds it, at its own size (not turned or adjusted as it is
   shown), read again into memory of its own that is given back at once. */
static void picture_copy(app_state_t *app) {
    int32_t page = current_page_index(app);
    if (page < 0 || (size_t)page >= page_count(app) || !app->pages[page].loaded) {
        osd_say(app, TU8("there is no picture to copy"));
        return;
    }
    int32_t w = real_w(&app->pages[page]), h = real_h(&app->pages[page]);
    if (w <= 0 || h <= 0 || (uint64_t)w * (uint64_t)h > (uint64_t)200000000u) {
        osd_say(app, TU8("this picture is too large to copy"));
        return;
    }
    size_t big = picture_bytes((size_t)w, (size_t)h, 4u * 3u, 16u << 20);
    void *memory = malloc(big);
    if (!memory) { osd_say(app, TU8("there is not memory enough to copy this picture")); return; }
    proven_arena_t scratch = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)memory, .size = big });
    rubraview_pixbuf_t full = {0};
    proven_u8str_view_t path = app->source.pages[page].path;
    if (path.size > 0) {
        full = rubraview_pal_image_read_pixels_within(&scratch, path, NULL, 0, true, 0, 0);
    } else {
        rubraview_page_bytes_t bytes = { .ok = false };
        AcquireSRWLockExclusive(&app->source_lock);
        bytes = rubraview_page_source_read(&scratch, &app->source, (size_t)page, MAX_PAGE_BYTES);
        ReleaseSRWLockExclusive(&app->source_lock);
        if (bytes.ok && bytes.data.size > 0) {
            full = rubraview_pal_image_read_pixels_within(&scratch, PROVEN_LIT(""),
                                                          (const uint8_t*)bytes.data.ptr, bytes.data.size, true, 0, 0);
        }
    }
    rubraview_pixbuf_t bgra = !rubraview_pixbuf_is_valid(&full) || full.format == RUBRAVIEW_PIXFMT_BGRA8
        ? full : rubraview_pixbuf_convert(&scratch, &full, RUBRAVIEW_PIXFMT_BGRA8);
    bool copied = rubraview_pixbuf_is_valid(&bgra) &&
                  rubraview_pal_clipboard_set_image(app->window, bgra.pixels, bgra.width, bgra.height, (size_t)bgra.stride);
    free(memory);
    osd_say(app, copied ? TU8("the picture is on the clipboard") : TU8("the picture could not be copied"));
}

void frame_take(app_state_t *app, const uint8_t *bgra, int32_t width, int32_t height, size_t stride) {
    int wanted = app->frame_wanted;
    app->frame_wanted = FRAME_NOT;
    if (wanted == FRAME_NOT || !bgra || width <= 0 || height <= 0) return;
    if (wanted == FRAME_COPY) {
        osd_say(app, rubraview_pal_clipboard_set_image(app->window, bgra, width, height, stride)
                         ? TU8("the frame is on the clipboard") : TU8("the frame could not be copied"));
        return;
    }
    int32_t page = app->media ? app->media_page : app->anim_page;
    if (page < 0 || (size_t)page >= page_count(app) || app->source.archive_path.size > 0) {
        osd_say(app, TU8("a frame is saved beside its file; this one is in an archive (Ctrl+C copies it)"));
        return;
    }
    proven_u8str_view_t path = app->source.pages[page].path;
    proven_u8str_view_t name = rubraview_path_basename(path);
    size_t stem = name.size;
    for (size_t i = name.size; i > 0; --i) if (name.ptr[i - 1] == '.') { stem = i - 1; break; }
    double at = app->media ? app->media_position : play_position(app);
    char file[512];
    int n = snprintf(file, sizeof(file), "%.*s_%02d-%02d-%03d.png", (int)(stem < 400 ? stem : 400), name.ptr,
                     (int)(at / 60.0), (int)fmod(at, 60.0), (int)(fmod(at, 1.0) * 1000.0));
    if (n <= 0) return;
    proven_u8str_view_t target = rubraview_path_join(app->arena, rubraview_path_dirname(path), (proven_u8str_view_t){ .ptr = (const proven_byte_t *)file, .size = (size_t)n });
    /* The frame as the writer wants it: its own rows, alpha full. */
    uint8_t *copy = (uint8_t*)rubraview_malloc_product((size_t)width, 4u, (size_t)height);
    if (!copy) return;
    for (int32_t y = 0; y < height; ++y) {
        memcpy(copy + (size_t)y * (size_t)width * 4u, bgra + (size_t)y * stride, (size_t)width * 4u);
        for (int32_t x = 0; x < width; ++x) copy[((size_t)y * (size_t)width + (size_t)x) * 4u + 3u] = 255;
    }
    rubraview_pixbuf_t pixels = { .pixels = copy, .width = width, .height = height, .stride = width * 4,
                                  .format = RUBRAVIEW_PIXFMT_BGRA8 };
    rubraview_export_options_t options = rubraview_export_defaults();
    options.format = RUBRAVIEW_EXPORT_PNG;
    bool written = rubraview_pal_image_save(target, &pixels, &options);
    free(copy);
    if (wanted == FRAME_EDIT && written && target.size < sizeof(app->edit_after)) {
        memcpy(app->edit_after, target.ptr, target.size);   /* Edit opens once it is the page on screen */
        app->edit_after[target.size] = '\0';
        reopen_on_path(app, app_keep(app, target), (size_t)page + 1);
    }
    char line[600];
    snprintf(line, sizeof(line), written ? T("frame saved: %s") : T("could not write %s"), file);
    osd_say(app, cstr(line));
}

static void anim_show(app_state_t *app, size_t frame_index) {
    app_page_t *page = &app->pages[app->anim_page];
    rubraview_frame_canvas_t *canvas = &app->anim_canvas;
    size_t stride = (size_t)canvas->width * 4;
    size_t whole = stride * (size_t)canvas->height;

    const uint8_t *picture = app->anim_cache ? app->anim_cache[frame_index] : NULL;
    if (!picture) {
        /* From the nearest kept picture before it, unless the canvas
           already stands nearer. */
        size_t start = rubraview_frame_canvas_start(canvas, frame_index);
        size_t near_kept = frame_index;
        bool kept_before = false;
        if (app->anim_cache) {
            while (near_kept > start) {
                --near_kept;
                if (app->anim_cache[near_kept]) { kept_before = true; break; }
            }
        }
        bool canvas_nearer = canvas->shown >= 0 && (size_t)canvas->shown <= frame_index &&
                             (!kept_before || (size_t)canvas->shown >= near_kept);
        if (kept_before && !canvas_nearer) rubraview_frame_canvas_seed(canvas, near_kept, app->anim_cache[near_kept]);
        if (!rubraview_frame_canvas_show(canvas, frame_index, anim_read_frame, app)) return;
        picture = canvas->pixels;
        /* Kept when it is far enough from the last kept one, and there is room. */
        size_t apart = kept_before ? frame_index - near_kept : frame_index - start + app->anim_keep_stride;
        if (app->anim_cache && apart >= app->anim_keep_stride &&
            rubraview_frame_canvas_keepable(canvas, frame_index) &&
            app->anim_kept_bytes + whole <= ANIM_CACHE_BYTES) {
            uint8_t *kept = (uint8_t*)malloc(whole);
            if (kept) {
                memcpy(kept, canvas->pixels, whole);
                app->anim_cache[frame_index] = kept;
                app->anim_kept_bytes += whole;
            }
        }
    }
    {
        /* The texture has no see-through: what shows through is the
           window's own colour, as around any picture. */
        const uint8_t back[3] = { (uint8_t)(COLOR_CANVAS & 0xFF), (uint8_t)((COLOR_CANVAS >> 8) & 0xFF), (uint8_t)((COLOR_CANVAS >> 16) & 0xFF) };
        uint8_t *flat = canvas->scratch;
        for (size_t i = 0; i < whole; i += 4) {
            uint32_t a = picture[i + 3];
            if (a == 255) { memcpy(flat + i, picture + i, 4); continue; }
            uint32_t keep = 255 - a;
            for (int c = 0; c < 3; ++c) {
                uint32_t v = picture[i + c] + (back[c] * keep + 127) / 255;
                flat[i + c] = (uint8_t)(v > 255 ? 255 : v);
            }
            flat[i + 3] = 255;
        }
        picture = flat;
    }

    if (!page->texture || page->texture != app->anim_texture) {
        rubraview_texture_t *texture = rubraview_pal_texture_create_bgra(app->renderer, canvas->width, canvas->height);
        if (!texture) return;
        if (page->texture) rubraview_pal_texture_destroy(page->texture);
        page->texture = texture;
        app->anim_texture = texture;
        if (page->width != canvas->width || page->height != canvas->height) app->needs_relayout = true;
        page->width = page->full_width = canvas->width;
        page->height = page->full_height = canvas->height;
        page->reduced = false;
        page->loaded = true;
    }
    (void)rubraview_pal_texture_upload_bgra(page->texture, picture, (int32_t)stride);
    if (app->frame_wanted) frame_take(app, picture, canvas->width, canvas->height, stride);
}

/* Replaces the page's texture with one frame of it. The page keeps its
   own dimensions from the frame, because an ICO's mipmaps genuinely
   differ in size and the layout has to follow. */
static void show_frame(app_state_t *app, size_t frame_index) {
    if (app->anim_page < 0) return;
    if (app->anim_reader) { anim_show(app, frame_index); return; }
    app_page_t *page = &app->pages[app->anim_page];

    proven_u8str_view_t path = app->source.pages[app->anim_page].path;
    rubraview_page_bytes_t bytes = { .data = PROVEN_LIT(""), .from_disk = true, .ok = false };
    void *mem = NULL;
    if (path.size == 0) {
        if (page_read_cost(app, (size_t)app->anim_page) > PAGE_JOB_MIN_COST) return;
        bytes = page_read_owned(&app->source, &app->source_lock, (size_t)app->anim_page, &mem);
        if (!bytes.ok) return;
    }

    double delay = 0.0;
    rubraview_image_load_result_t loaded = rubraview_pal_image_load_frame(
        app->renderer, path, (const uint8_t*)bytes.data.ptr, bytes.data.size,
        frame_index, true, &delay);
    free(mem);
    if (!loaded.ok) return;

    if (page->texture) rubraview_pal_texture_destroy(page->texture);
    page->texture = loaded.texture;
    page->width = loaded.width;
    page->height = loaded.height;
    page->full_width = loaded.full_width;
    page->full_height = loaded.full_height;
    page->reduced = loaded.reduced;
    page->loaded = true;
    app->needs_relayout = true;
}

/* Called whenever the page on screen changes. Most pages are ordinary
   images and this costs one frame-count query; the rest set up the
   clock, or — for an ICO — jump straight to the biggest mipmap, which
   §3.20.2 says is the one worth showing. */
void animation_prepare(app_state_t *app) {
    anim_close(app);
    app->anim_active = false;
    app->anim_page = -1;

    int32_t page_index = current_page_index(app);
    if (page_index < 0 || (size_t)page_index >= page_count(app)) return;

    if (app->pages[page_index].skipped) return;   /* D-98 */
    proven_u8str_view_t path = app->source.pages[page_index].path;
    /* A film or a track is no picture to look for frames in. Looking read
       the whole file, twice, before it played — minutes over a network
       share (owner, 2026-10-11; since D-82, 0.0.39). */
    if (is_media_path(path) || is_text_path(path) || is_text_path(app->source.pages[page_index].name)) return;
    rubraview_page_bytes_t bytes = { .data = PROVEN_LIT(""), .from_disk = true, .ok = false };
    void *mem = NULL;
    if (path.size == 0) {
        /* A far page in a streamed block is not read twice to look for frames. */
        if (page_read_cost(app, (size_t)page_index) > PAGE_JOB_MIN_COST) return;
        bytes = page_read_owned(&app->source, &app->source_lock, (size_t)page_index, &mem);
        if (!bytes.ok) return;
    }

    /* D-98: in a layout for pictures a picture that moves is not played but
       left out — unless it is the one the reader asked for by name. */
    if (app->layout_opts.mode != RUBRAVIEW_PAGE_LAYOUT_SINGLE && page_index != app->layout_exempt &&
        page_moves(app, (size_t)page_index, &bytes)) {
        bool others = false;
        for (size_t i = 0; i < page_count(app) && !others; ++i) {
            others = !is_media_path(app->source.pages[i].path) && !is_text_path(app->source.pages[i].path) &&
                     !app->pages[i].animated && (int32_t)i != page_index;
        }
        app->pages[page_index].animated = true;
        if (others) {
            free(mem);
            app->needs_relayout = true;
            return;
        }
    }

    /* D-82, D-84: an animated GIF, WebP or PNG first — kept open, laid
       together and played as a film is. */
    size_t timed_count = 0;
    int32_t canvas_w = 0, canvas_h = 0;
    const rubraview_frame_t *timed_frames = NULL;
    app->anim_reader = rubraview_pal_anim_open(path, (const uint8_t*)bytes.data.ptr, bytes.data.size,
                                               &timed_frames, &timed_count, &canvas_w, &canvas_h);
    if (app->anim_reader) {
        free(mem);
        size_t whole = picture_bytes((size_t)canvas_w, (size_t)canvas_h, 4u, 0);
        app->anim_buffers = (uint8_t*)rubraview_malloc_product(whole, 3, 1);
        if (!app->anim_buffers) { anim_close(app); return; }
        rubraview_frame_canvas_init(&app->anim_canvas, timed_frames, timed_count, canvas_w, canvas_h,
                                    app->anim_buffers, app->anim_buffers + whole, app->anim_buffers + whole * 2);
        app->animation = rubraview_animation_create(RUBRAVIEW_FRAMES_ANIMATION, timed_frames, timed_count);
        app->anim_keep_stride = rubraview_frame_keep_stride(whole, timed_count, ANIM_CACHE_BYTES);
        app->anim_kept_bytes = 0;
        app->anim_cache = (uint8_t**)calloc(timed_count, sizeof(uint8_t*));
        app->animation.speed = app->media_speed > 0.0 ? app->media_speed : 1.0;
        app->anim_page = page_index;
        app->anim_active = true;
        app->ab_a = app->ab_b = -1.0;   /* a repeat belongs to the file it was set in */
        if (app->slides && app->spread_index < app->layout.count) {
            app->slides[app->spread_index].kind = RUBRAVIEW_MEDIA_ANIMATED;
            app->slides[app->spread_index].duration_seconds = rubraview_animation_cycle_seconds(&app->animation);
        }
        /* The first frame as the whole picture: the page's own decode is
           the container's first frame alone, which may be a part of it. */
        show_frame(app, 0);
        if (rubraview_pal_anim_cut(app->anim_reader)) {
            char line[128];
            int n = snprintf(line, sizeof(line), T("showing the first %zu pictures — the rest does not fit in memory"),
                             timed_count);
            if (n > 0) {
                osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
                app->notice_seconds = MEDIA_NOTICE_SECONDS;
            }
        }
        return;
    }

    if (!app->anim_frames) {
        /* Kept for the session: one list, for whichever page has sub-pages. */
        app->anim_frames = (rubraview_frame_t*)calloc(ANIM_MAX_FRAMES, sizeof(rubraview_frame_t));
        if (!app->anim_frames) { free(mem); return; }
    }
    size_t count = rubraview_pal_image_frame_info(path, (const uint8_t*)bytes.data.ptr, bytes.data.size,
                                                  app->anim_frames, ANIM_MAX_FRAMES);
    free(mem);
    if (count <= 1) return;               /* an ordinary image */
    if (count > ANIM_MAX_FRAMES) count = ANIM_MAX_FRAMES;

    rubraview_frame_kind_t kind = rubraview_animation_classify(app->anim_frames, count);
    bool timed = kind == RUBRAVIEW_FRAMES_ANIMATION;

    app->animation = rubraview_animation_create(kind, app->anim_frames, count);
    app->anim_page = page_index;
    app->anim_active = true;

    /* §3.2.6: a slide holding an animation waits for one full pass
       rather than the fixed interval, so a slide show does not cut a
       GIF off halfway. */
    if (timed && app->slides && app->spread_index < app->layout.count) {
        app->slides[app->spread_index].kind = RUBRAVIEW_MEDIA_ANIMATED;
        app->slides[app->spread_index].duration_seconds = rubraview_animation_cycle_seconds(&app->animation);
    }

    if (!timed) {
        /* §3.20.2: open an ICO at its largest layer. */
        size_t largest = rubraview_animation_largest_frame(&app->animation);
        if (largest != 0) {
            app->animation.current = largest;
            show_frame(app, largest);
        }
    }
}

static void animation_tick(app_state_t *app, double dt) {
    if (!app->anim_active) return;
    app->animation.ab_a = app->ab_a;
    app->animation.ab_b = app->ab_b;
    int at_end = anim_end_mode(app);
    /* A slide show turns the page itself, after one pass. */
    app->animation.once = at_end != 0 && !app->slideshow_running;
    rubraview_animation_event_t event = rubraview_animation_tick(&app->animation, dt);
    if (event == RUBRAVIEW_ANIMATION_NONE) return;
    show_frame(app, app->animation.current);
    app->media_new_picture = true;
    /* The title is not rewritten for every frame: Windows answers each
       change with a message, and the loop would never rest (a film's is
       not either). It says the time when paused, stepped or sought. */
    if (event == RUBRAVIEW_ANIMATION_ENDED) update_window_title(app);
    if (event == RUBRAVIEW_ANIMATION_ENDED && at_end == 2 && app->spread_index + 1 < app->layout.count) next_spread(app);
}

/* D-82: where what plays is, how long it is, and going there — a film, a
   song or an animation alike. */
void media_seek_to(app_state_t *app, double seconds);
double play_position(const app_state_t *app) {
    return app->media ? app->media_position : anim_plays(app) ? rubraview_animation_position(&app->animation) : 0.0;
}
double play_duration(const app_state_t *app) {
    return app->media ? app->media_info.duration_seconds
         : anim_plays(app) ? rubraview_animation_cycle_seconds(&app->animation) : 0.0;
}
void play_seek(app_state_t *app, double seconds) {
    if (app->media) {
        media_seek_to(app, seconds);
        app->media_position = seconds;
        return;
    }
    if (!anim_plays(app)) return;
    double duration = rubraview_animation_cycle_seconds(&app->animation);
    if (seconds > duration) seconds = duration;
    rubraview_animation_seek(&app->animation, seconds);
    show_frame(app, app->animation.current);
    update_window_title(app);
}

/* ---- layout ---- */

void rebuild_layout(app_state_t *app) {
    if (page_count(app) == 0) return;

    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, page_count(app), sizeof(rubraview_page_info_t));
    if (!proven_is_ok(res.err)) return;
    rubraview_page_info_t *infos = (rubraview_page_info_t*)(void*)res.value.ptr;

    double fallback_w = 800.0, fallback_h = 1200.0;
    for (size_t i = 0; i < page_count(app); ++i) {
        app_page_t *pg = &app->pages[i];
        if (pg->loaded) { pg->seen_w = picture_w(pg); pg->seen_h = picture_h(pg); }
    }
    for (size_t i = 0; i < page_count(app); ++i) {
        /* a page not read yet is taken as the first narrow one read is: a wide one is no model for the rest */
        const app_page_t *pg = &app->pages[i];
        if (pg->seen_w > 0 && pg->seen_h > 0 && (double)pg->seen_w < (double)pg->seen_h * 1.15) {
            fallback_w = (double)pg->seen_w;
            fallback_h = (double)pg->seen_h;
            break;
        }
    }

    for (size_t i = 0; i < page_count(app); ++i) {
        const app_page_t *pg = &app->pages[i];
        bool seen = pg->seen_w > 0 && pg->seen_h > 0;
        double w = pg->loaded ? (double)picture_w(pg) : seen ? (double)pg->seen_w : fallback_w;
        double h = pg->loaded ? (double)picture_h(pg) : seen ? (double)pg->seen_h : fallback_h;
        /* Pagination sees the page as the reader does, so a rotated
           portrait page is treated as the landscape it now presents. */
        rubraview_orientation_apply_size(app->orientation, w, h, &infos[i].width, &infos[i].height);
        /* §3.8.5: a page the archive tagged as a cover never pairs. */
        infos[i].force_standalone = app->comic_page_flags ? app->comic_page_flags[i].force_standalone : false;
        infos[i].skip = false;
    }

    /* D-98 (owner, 2026-10-11): in a layout for looking at pictures — two
       pages, a book, a comic, the strip — a film, a sound and a picture that
       moves are not shown; the picture after them is. The one the reader
       asked for by name is, alone. With nothing but those, all are shown. */
    bool pictures_only = app->layout_opts.mode != RUBRAVIEW_PAGE_LAYOUT_SINGLE;
    size_t stills = 0;
    for (size_t i = 0; i < page_count(app); ++i) {
        bool other = is_media_path(app->source.pages[i].path) || is_text_path(app->source.pages[i].path) ||
                     is_text_path(app->source.pages[i].name) || app->pages[i].animated;
        infos[i].skip = pictures_only && other && (int32_t)i != app->layout_exempt;
        if ((int32_t)i == app->layout_exempt && other) infos[i].force_standalone = true;
        if (!other) stills++;
    }
    for (size_t i = 0; i < page_count(app); ++i) {
        if (stills == 0) infos[i].skip = false;
        app->pages[i].skipped = infos[i].skip;
    }

    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);

    /* The reader's rotation (owner, 2026-10-08): a page it has laid on its
       side is one page shown whole, not a spread to cut in two. Turned
       upside down, each page is turned where it stands (owner, 2026-10-09). */
    rubraview_layout_opts_t opts = rubraview_layout_opts_turned(
        app->layout_opts, rubraview_orientation_swaps_axes(app->orientation));

    app->layout = rubraview_layout_compute(app->arena, infos, page_count(app),
                                           (double)win_w, (double)win_h, opts);
    if (app->spread_index >= app->layout.count && app->layout.count > 0) {
        app->spread_index = app->layout.count - 1;
    }
    app->needs_relayout = false;
}

size_t spread_index_for_page(const app_state_t *app, int32_t page_index) {
    for (size_t i = 0; i < app->layout.count; ++i) {
        if (app->layout.spreads[i].left_index == page_index ||
            app->layout.spreads[i].right_index == page_index) {
            return i;
        }
    }
    /* D-98: a page left out of the layout: the first spread after it, or the last before. */
    size_t before = 0;
    for (size_t i = 0; i < app->layout.count; ++i) {
        if (app->layout.spreads[i].left_index > page_index || app->layout.spreads[i].right_index > page_index) return i;
        before = i;
    }
    return before;
}

void rebuild_layout(app_state_t *app);
bool is_media_path(proven_u8str_view_t path);

/* The spread for a page the reader chose by name — in a list, on the page
   bar, by opening it. A film or an animation chosen so is shown whatever
   the layout (D-98): it is the one exception, until another page is turned to. */
size_t spread_for_chosen_page(app_state_t *app, int32_t page) {
    if (page >= 0 && (size_t)page < page_count(app) && page != app->layout_exempt &&
        (app->pages[page].skipped ||
         (app->layout_opts.mode != RUBRAVIEW_PAGE_LAYOUT_SINGLE &&
          (is_media_path(app->source.pages[page].path) || is_text_path(app->source.pages[page].path) || app->pages[page].animated ||
           page_moves(app, (size_t)page, NULL))))) {   /* a GIF not read yet: its bytes say */
        app->layout_exempt = page;
        rebuild_layout(app);
    }
    return spread_index_for_page(app, page);
}

int32_t current_page_index(const app_state_t *app) {
    if (app->layout.count == 0) return -1;
    return app->layout.spreads[app->spread_index].left_index;
}

/* ---- actions ---- */

void reset_view(app_state_t *app) {
    app->zoom = 1.0;
    app->pan_x = 0.0;
    app->pan_y = 0.0;
}

void note_activity(app_state_t *app) {
    rubraview_osd_notify_activity(&app->osd);
}

/* ---- D-109: the zoom in steps of percent ----
   100 % is the size the fit option gives (D-97). Real size is beside that
   scale, not on it — "0%" — so a step taken from it first finds where on
   the scale the picture is now. */
static bool spread_composition(app_state_t *app, rubraview_composition_t *out);

bool at_real_size(const app_state_t *app) {
    return app->fit_mode == RUBRAVIEW_FIT_ACTUAL_SIZE && fabs(app->zoom - 1.0) < 0.0005;
}

/* Out of the real-size fit, keeping the picture the size it is on screen. */
void zoom_leave_real_fit(app_state_t *app) {
    if (app->fit_mode != RUBRAVIEW_FIT_ACTUAL_SIZE) return;
    rubraview_fit_mode_t fit = app->fit_beside_real != RUBRAVIEW_FIT_ACTUAL_SIZE ? app->fit_beside_real : RUBRAVIEW_FIT_WINDOW;
    rubraview_composition_t now, fitted;
    double zoom_was = app->zoom;
    bool had = spread_composition(app, &now) && now.count > 0;
    app->fit_mode = fit;
    app->zoom = 1.0;
    if (had && spread_composition(app, &fitted) && fitted.count > 0 && fitted.commands[0].scale > 0.0) {
        app->zoom = now.commands[0].scale / fitted.commands[0].scale;
    } else {
        app->zoom = zoom_was;
    }
}

void zoom_say(app_state_t *app) {
    char line[24];
    proven_u8str_view_t text = at_real_size(app) ? TU8("0% (real size)") : rubraview_scale_caption(line, sizeof(line), app->zoom);
    if (text.size > 0) osd_say(app, text);
    note_activity(app);
}

/* One step larger or smaller: Zoom+ and Zoom-, Ctrl + wheel, the wheel on a zoom tile. */
void zoom_step(app_state_t *app, bool larger) {
    zoom_leave_real_fit(app);
    double step = (g_zoom_step - 1.0) * 100.0;
    app->zoom = rubraview_zoom_step_percent(app->zoom * 100.0, step, larger) / 100.0;
    zoom_say(app);
}

/* A tap on the zoom tile: real size, 20 % .. 200 %, real size. */
static void zoom_cycle(app_state_t *app) {
    bool real = at_real_size(app);
    zoom_leave_real_fit(app);
    double next = rubraview_zoom_cycle_percent(app->zoom * 100.0, real);
    if (next == 0.0) {
        if (app->fit_mode != RUBRAVIEW_FIT_ACTUAL_SIZE) app->fit_beside_real = app->fit_mode;
        app->fit_mode = RUBRAVIEW_FIT_ACTUAL_SIZE;
        reset_view(app);
    } else {
        app->zoom = next / 100.0;
    }
    zoom_say(app);
}

/* The caption names the file on screen and where it sits in the folder
   or archive, so the taskbar, Alt+Tab and a test script can all tell
   which page is showing. */
void update_window_title(app_state_t *app) {
    char title[768];
    size_t used = 0;
    if (app->picker_open) {   /* 1.24.0 (owner, 2026-10-19): "Rubraview - <the folder>" */
        proven_u8str_view_t name = rubraview_picker_is_places(app->picker_dir) ? TU8("PC")
                     : rubraview_picker_is_recent(app->picker_dir) ? TU8("Recent") : rubraview_path_basename(app->picker_dir);
        if (name.size == 0) name = app->picker_dir;
        snprintf(title, sizeof(title), "Rubraview - %.*s", (int)(name.size < 600 ? name.size : 600), name.ptr);
        rubraview_pal_window_set_title(app->window, title);
        return;
    }
    int32_t page = current_page_index(app);
    if (page >= 0 && (size_t)page < page_count(app)) {
        /* The name the status line shows, not the path: an archive page
           has no path of its own (found by T037). */
        proven_u8str_view_t name = page_display_name(app, (size_t)page);
        bool song = app->media && page == app->media_page && app->media_info.live && app->station_title[0];   /* RV-158 */
        int n = snprintf(title, sizeof(title), "%.*s%s%s (%d/%zu) ",
                         (int)name.size, name.ptr, song ? " \xE2\x80\x94 " : "", song ? app->station_title : "",
                         (int)page + 1, page_count(app));
        used = n > 0 ? ((size_t)n < sizeof(title) ? (size_t)n : sizeof(title) - 1) : 0;
        if (app->media && page == app->media_page) {
            /* A video shows where it is, to the millisecond — which is
               also how a frame step can be checked from outside. */
            char at[32], total[32];
            proven_u8str_view_t a = rubraview_format_timecode(at, sizeof(at), app->media_position, true);
            proven_u8str_view_t t = app->media_info.live ? TU8("live")   /* RV-157 */
                      : rubraview_format_timecode(total, sizeof(total), app->media_info.duration_seconds, true);
            n = snprintf(title + used, sizeof(title) - used, "%.*s / %.*s%s%s ",
                         (int)a.size, a.ptr, (int)t.size, t.ptr,
                         app->media_paused ? " paused" : "",
                         app->media_info.backend == RUBRAVIEW_BACKEND_FFMPEG ? " ffmpeg" : "");
            if (n > 0) used += (size_t)n < sizeof(title) - used ? (size_t)n : sizeof(title) - used - 1;
        } else if (!app->media && page == station_connecting_page(app)) {
            n = snprintf(title + used, sizeof(title) - used, "%s ", T("connecting..."));   /* RV-157 */
            if (n > 0) used += (size_t)n < sizeof(title) - used ? (size_t)n : sizeof(title) - used - 1;
        } else if (text_encoding_shown(app)) {
            /* RFC-0005 §6: what the text is read as. */
            n = snprintf(title + used, sizeof(title) - used, "[%s] ", text_encoding_shown(app));
            if (n > 0) used += (size_t)n < sizeof(title) - used ? (size_t)n : sizeof(title) - used - 1;
        } else if (anim_plays(app) && page == app->anim_page) {
            /* D-82: an animation says where it is as a film does. */
            char at[32], total[32];
            proven_u8str_view_t a = rubraview_format_timecode(at, sizeof(at), rubraview_animation_position(&app->animation), true);
            proven_u8str_view_t t = rubraview_format_timecode(total, sizeof(total), rubraview_animation_cycle_seconds(&app->animation), true);
            n = snprintf(title + used, sizeof(title) - used, "%.*s / %.*s%s ",
                         (int)a.size, a.ptr, (int)t.size, t.ptr, app->animation.paused ? " paused" : "");
            if (n > 0) used += (size_t)n < sizeof(title) - used ? (size_t)n : sizeof(title) - used - 1;
        }
    }
    snprintf(title + used, sizeof(title) - used, T("%sRubraview %s"), used > 0 ? "- " : "", RUBRAVIEW_VERSION_STRING);
    rubraview_pal_window_set_title(app->window, title);
}

/* ---- the strip (webtoon layout; owner, 2026-10-09) ----
   Every page at one width, one under the other, scrolled as one long
   page. The layout holds one page a spread, so the spread on screen is
   the page at the top of the window. A film, a song or an animation on
   that page is shown the ordinary way. */
bool strip_active(const app_state_t *app) {
    return app->layout_opts.mode == RUBRAVIEW_PAGE_LAYOUT_WEBTOON && page_count(app) > 0 &&
           app->media_page < 0 && !app->anim_active && app->layout.count == page_count(app);
}

/* The strip's width on screen: the width of the page it was entered on,
   no wider than the window, times the zoom. */
static double strip_width(app_state_t *app, double win_w) {
    if (app->strip_base_w <= 0.0 || app->strip_base_count != page_count(app)) {
        app_page_t *page = ensure_page_loaded(app, (int32_t)app->spread_index);
        if (page && page->loaded) {
            double w = 0.0, h = 0.0;
            rubraview_orientation_apply_size(app->orientation, (double)picture_w(page), (double)picture_h(page), &w, &h);
            app->strip_base_w = w;
            app->strip_base_count = page_count(app);
        }
    }
    double base = app->strip_base_w > 0.0 ? app->strip_base_w : win_w;
    return (base < win_w ? base : win_w) * app->zoom;
}

static double strip_page_height(void *context, size_t index) {
    app_state_t *app = (app_state_t *)context;
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    double width = strip_width(app, (double)win_w);
    if (index < page_count(app) && app->pages[index].skipped) return 0.0;   /* D-98: no place in the strip (the strip makes it a pixel) */
    app_page_t *page = ensure_page_loaded(app, (int32_t)index);
    if (!page || page->failed) return width * 0.1;
    if (!page->loaded) return width * 1.5;      /* on its way: a page's usual shape until it is here */
    double w = 0.0, h = 0.0;
    rubraview_orientation_apply_size(app->orientation, (double)picture_w(page), (double)picture_h(page), &w, &h);
    return w > 0.0 ? width * h / w : width * 1.5;
}

/* Moves the strip; returns what could not be moved (an end was reached). */
static double strip_scroll_by(app_state_t *app, double delta) {
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) return delta;   /* minimised: there is no strip to measure */
    rubraview_strip_pos_t pos = { app->spread_index, app->strip_offset };
    double left = rubraview_strip_scroll(&pos, delta, page_count(app), (double)win_h, strip_page_height, app);
    app->strip_offset = pos.offset;
    if (pos.top != app->spread_index) {
        /* Another page is at the top: it is the one the title, the page bar
           and the lists speak of. No fade, no view reset — nothing jumped. */
        app->spread_index = pos.top;
        app->prev_spread_valid = false;
        strip_follow(app, pos.top);
        update_window_title(app);
        update_precache(app);
    }
    return left;
}

void go_to_spread(app_state_t *app, size_t index) {
    if (app->layout.count == 0) return;
    app->strip_offset = 0.0;
    if (index >= app->layout.count) index = app->layout.count - 1;
    if (app->layout_opts.mode == RUBRAVIEW_PAGE_LAYOUT_WEBTOON && app->layout.count == page_count(app)) {
        /* D-98: the strip has a place for every page; one it leaves out is stepped over. */
        while (index + 1 < app->layout.count && app->pages[index].skipped) index++;
        while (index > 0 && app->pages[index].skipped) index--;
    }
    if (index != app->spread_index && !subs_may_leave(app)) return;
    if (index != app->spread_index) {
        /* Remember what is leaving the screen, so the cross-fade has
           something to fade *from* (§3.2.5). */
        app->prev_spread_index = app->spread_index;
        app->prev_spread_valid = true;
        /* Settings > Viewer > Turning a page; a slide show quicker than half a second cuts (§3.2.5). */
        static const rubraview_transition_kind_t KINDS[] = { RUBRAVIEW_TRANSITION_CUT, RUBRAVIEW_TRANSITION_CROSSFADE,
                                                             RUBRAVIEW_TRANSITION_SLIDE_LEFT, RUBRAVIEW_TRANSITION_ZOOM_FADE,
                                                             RUBRAVIEW_TRANSITION_DISSOLVE };
        int turn = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("transition")));
        app->transition.kind = rubraview_transition_for_interval(
            KINDS[turn >= 0 && turn < 5 ? turn : 1],
            app->slideshow_running ? app->slideshow.interval_seconds : 1.0, 0.5);
        rubraview_transition_start(&app->transition);
    }
    app->spread_index = index;
    /* §3.4 Fit Lock: with the lock on, moving between images of
       different resolutions keeps the chosen zoom instead of resetting
       it — the point of the lock. */
    if (!app->fit_lock) reset_view(app);
    note_activity(app);

    int32_t page = current_page_index(app);
    if (app->layout_exempt >= 0 && page != app->layout_exempt) {
        app->layout_exempt = -1;      /* D-98: turned away from: it is left out like the others */
        app->needs_relayout = true;
    }
    if (page >= 0) strip_follow(app, (size_t)page);
    animation_prepare(app);
    media_prepare(app);
    update_window_title(app);
}

bool open_sibling_archive(app_state_t *app, bool forward);
bool open_sibling_folder(app_state_t *app, bool forward);

/* Next past the last page, or Back before the first (owner, 2026-10-14):
   Settings › Files › At the first and last — say so (as it comes), open the
   next or the previous folder or archive (from its last page going back),
   or go round within what is open. */
static void past_the_end(app_state_t *app, bool forward) {
    int mode = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("at_ends")));
    if (mode == 2 && app->layout.count > 1) {
        go_to_spread(app, forward ? 0 : app->layout.count - 1);
        update_precache(app);
        osd_say(app, forward ? TU8("the last: round again to the first") : TU8("the first: round again to the last"));
        return;
    }
    if (mode == 1) {
        bool opened = app->source.archive_path.size > 0 ? open_sibling_archive(app, forward)
                    : !app->list_is_set ? open_sibling_folder(app, forward) : false;
        if (opened) return;
        osd_say(app, forward ? TU8("the last, and nothing after it to open")
                             : TU8("the first, and nothing before it to open"));
        return;
    }
    osd_say(app, forward ? TU8("this is the last") : TU8("this is the first"));
}

static void next_spread(app_state_t *app) {
    if (strip_active(app)) {
        /* A window's worth, less a little to keep one's place. */
        int32_t win_w = 0, win_h = 0;
        rubraview_pal_window_get_size(app->window, &win_w, &win_h);
        double step = (double)win_h * 0.9;
        if (strip_scroll_by(app, step) < step - 1.0) return;
        past_the_end(app, true);
        return;
    }
    if (app->spread_index + 1 < app->layout.count) {
        go_to_spread(app, app->spread_index + 1);
        update_precache(app);
        return;
    }
    past_the_end(app, true);   /* §3.8.1 point 4 when Settings › Files › At the first and last says so */
}

static void prev_spread(app_state_t *app) {
    if (strip_active(app)) {
        int32_t win_w = 0, win_h = 0;
        rubraview_pal_window_get_size(app->window, &win_w, &win_h);
        double step = (double)win_h * 0.9;
        if (strip_scroll_by(app, -step) > -(step - 1.0)) return;
        past_the_end(app, false);
        return;
    }
    if (app->spread_index > 0) {
        go_to_spread(app, app->spread_index - 1);
        update_precache(app);
        return;
    }
    past_the_end(app, false);
}

proven_u8str_view_t menu_tile_caption(const rubraview_menu_state_t *menu, int32_t tile, char *scratch, size_t scratch_size,
                                 bool *out_enabled, bool *out_current);

static void toggle_slideshow(app_state_t *app) {
    app->slideshow_running = !app->slideshow_running;
    if (app->slideshow_running) {
        app->slideshow.current_index = app->spread_index;
        rubraview_slideshow_resume(&app->slideshow);
        /* in the window as it is, full screen or not (owner, 2026-10-14, RV-116; §3.2.5 went full screen) */
    } else {
        rubraview_slideshow_pause(&app->slideshow);
        rubraview_pal_window_set_cursor_visible(app->window, true);
    }
}

/* ---- D-40: tiles over a page shown reduced ---- */

#define TILE_SLOTS (sizeof(((struct tile_set*)0)->slots) / sizeof(((struct tile_set*)0)->slots[0]))
#define TILE_ASK_MAX (sizeof(((struct tile_set*)0)->asked) / sizeof(((struct tile_set*)0)->asked[0]))
#define TILE_SETS 2

/* Every tile goes, and what the thread still makes for them is known as
   late by the new generation. */
static void tile_set_reset(struct tile_set *ts) {
    for (size_t i = 0; i < TILE_SLOTS; ++i) {
        if (ts->slots[i].texture) rubraview_pal_texture_destroy(ts->slots[i].texture);
        ts->slots[i] = (struct tile_slot){0};
    }
    ts->generation++;
    ts->page = -1;
    ts->page_texture = NULL;
    ts->asked_count = 0;
    if (ts->tiles) rubraview_pal_tiles_want(ts->tiles, ts->generation, NULL, 0);
}

static void tiles_reset(app_state_t *app) {
    for (size_t i = 0; i < TILE_SETS; ++i) tile_set_reset(&app->tile_sets[i]);
}

static bool tile_key_eq(rubraview_tile_key_t a, rubraview_tile_key_t b) {
    return a.level == b.level && a.tx == b.tx && a.ty == b.ty;
}

static struct tile_slot *tile_find(struct tile_set *ts, rubraview_tile_key_t key) {
    for (size_t i = 0; i < TILE_SLOTS; ++i) {
        if (ts->slots[i].live && tile_key_eq(ts->slots[i].key, key)) return &ts->slots[i];
    }
    return NULL;
}

/* Each pass: turn what the thread has made into textures, the least
   recently drawn tile making room when every slot is taken. */
static void tile_set_take_all(app_state_t *app, struct tile_set *ts) {
    if (!ts->tiles) return;
    rubraview_tile_result_t done;
    while (rubraview_pal_tiles_take(ts->tiles, &done)) {
        if (done.generation == ts->generation && !tile_find(ts, done.key)) {
            struct tile_slot *slot = NULL;
            for (size_t i = 0; i < TILE_SLOTS && !slot; ++i) {
                if (!ts->slots[i].live) slot = &ts->slots[i];
            }
            if (!slot) {
                struct tile_slot *oldest = &ts->slots[0];
                for (size_t k = 1; k < TILE_SLOTS; ++k) {
                    if (ts->slots[k].used < oldest->used) oldest = &ts->slots[k];
                }
                if (oldest->texture) rubraview_pal_texture_destroy(oldest->texture);
                *oldest = (struct tile_slot){0};
                slot = oldest;
            }
            rubraview_texture_t *texture = NULL;
            if (done.bgra) {
                texture = rubraview_pal_texture_create_bgra(app->renderer, done.width, done.height);
                if (texture && !rubraview_pal_texture_upload_bgra(texture, done.bgra, done.width * 4)) {
                    rubraview_pal_texture_destroy(texture);
                    texture = NULL;
                }
            }
            *slot = (struct tile_slot){ .key = done.key, .texture = texture, .used = ++ts->clock,
                                        .live = true, .failed = texture == NULL };
            app->tiles_arrived = true;
        }
        free(done.bgra);
    }
}

static void tiles_take_all(app_state_t *app) {
    app->tiles_arrived = false;
    for (size_t i = 0; i < TILE_SETS; ++i) tile_set_take_all(app, &app->tile_sets[i]);
}

/* RV-085: an archive page has no file for the thread to read, so it is
   read here once — the page source (a CB7's solid block, the SDK's index)
   belongs to this thread — into an arena of just its size, not the app's,
   and handed over as a copy. Without it every tile fails once and is not
   asked for again, as for a file that cannot be read. */
static void tiles_give_archive_page(app_state_t *app, struct tile_set *ts, int32_t index, int32_t pw, int32_t ph) {
    uint8_t *copy = NULL;
    size_t copy_size = 0;
    uint64_t size = rubraview_page_source_entry_size(&app->source, (size_t)index);
    size_t budget = rubraview_page_source_read_budget(&app->source, (size_t)index);
    /* Not a page that must be decoded again from far back (a streamed 7z block). */
    bool close_by = page_read_cost(app, (size_t)index) <= PAGE_JOB_MIN_COST;
    void *mem = close_by && size > 0 && size <= MAX_PAGE_BYTES && budget > 0 ? malloc(budget) : NULL;
    if (mem) {
        proven_arena_t scratch = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)mem, .size = budget });
        AcquireSRWLockExclusive(&app->source_lock);
        rubraview_page_bytes_t bytes = rubraview_page_source_read(&scratch, &app->source, (size_t)index, MAX_PAGE_BYTES);
        ReleaseSRWLockExclusive(&app->source_lock);
        if (bytes.ok && bytes.data.size > 0) {
            copy = (uint8_t*)malloc(bytes.data.size);
            if (copy) {
                memcpy(copy, bytes.data.ptr, bytes.data.size);
                copy_size = bytes.data.size;
            }
        }
        free(mem);
    }
    rubraview_pal_tiles_source_bytes(ts->tiles, ts->generation, copy, copy_size, true, pw, ph);
}

/* D-81: is this page, shown larger than its own pixels, to be enlarged
   sharp? Not with the pixel-art toggle or a plainer scaling filter chosen
   (the reader asked for those pixels), nor for a film or an animation,
   whose picture changes under the tiles. */
static bool enlarge_sharp(const app_state_t *app, int32_t index) {
    if (app->force_nearest || app->anim_active || index == app->media_page) return false;
    if (rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("enlarge")) < 0.5) return false;
    return (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("interpolation"))) >= 2;
}

/* After a reduced page is drawn: the tiles on screen, sharper, over it,
   and a request for those not yet made. `place` maps picture pixels to
   the screen (the reader's rotation included); `scale` is its size. */
static void draw_page_tiles(app_state_t *app, int which, int32_t index, const app_page_t *page,
                            rubraview_mat3x2_t place, double scale, int32_t win_w, int32_t win_h,
                            double opacity, rubraview_interpolation_t interp) {
    struct tile_set *ts = &app->tile_sets[which == 1 ? 1 : 0];
    if (g_view_ratio > 0.0) return;   /* tiles are cut in the file's pixels: a stretched page keeps its one texture */
    int32_t pw = 0, ph = 0;
    page_picture_size(page, &pw, &ph);
    /* Sharper than the texture in two cases: the page is held reduced and
       shown larger than that (D-40), or it is shown larger than its own
       pixels and enlarging is set to sharp (D-81). */
    bool reduced = page->reduced && rubraview_tiles_wanted(scale, pw, page->width);
    bool enlarged = !reduced && rubraview_tile_level_enlarged(scale) < 0 && enlarge_sharp(app, index);
    if (!reduced && !enlarged) {
        if (ts->page == index && ts->page_texture) tile_set_reset(ts);   /* back at its own size: the tiles are of no use */
        return;
    }
    proven_u8str_view_t path = app->source.pages[index].path;
    if (!ts->tiles) {
        ts->tiles = rubraview_pal_tiles_start(thumbs_wake, app->window);
        if (!ts->tiles) return;
    }
    if (ts->page != index || ts->page_texture != page->texture) {
        /* This set only: the other page of a pair keeps its own tiles
           (resetting both made each undo the other's on every frame). */
        tile_set_reset(ts);
        ts->page = index;
        ts->page_texture = page->texture;
        if (path.size > 0) {
            rubraview_pal_tiles_source(ts->tiles, ts->generation, path, true, pw, ph);
        } else {
            tiles_give_archive_page(app, ts, index, pw, ph);
        }
    }

    rubraview_mat3x2_t inverse;
    if (!rubraview_mat3x2_invert(place, &inverse)) return;
    double xs[4], ys[4];
    rubraview_mat3x2_apply(inverse, 0.0, 0.0, &xs[0], &ys[0]);
    rubraview_mat3x2_apply(inverse, (double)win_w, 0.0, &xs[1], &ys[1]);
    rubraview_mat3x2_apply(inverse, 0.0, (double)win_h, &xs[2], &ys[2]);
    rubraview_mat3x2_apply(inverse, (double)win_w, (double)win_h, &xs[3], &ys[3]);
    double x0 = xs[0], x1 = xs[0], y0 = ys[0], y1 = ys[0];
    for (int i = 1; i < 4; ++i) {
        if (xs[i] < x0) x0 = xs[i];
        if (xs[i] > x1) x1 = xs[i];
        if (ys[i] < y0) y0 = ys[i];
        if (ys[i] > y1) y1 = ys[i];
    }
    int32_t level = enlarged ? rubraview_tile_level_enlarged(scale) : rubraview_tile_level(scale);
    double margin = rubraview_tile_span(level) / 2.0;   /* a little around, for a pan */

    rubraview_tile_key_t keys[128], missing[128];
    size_t n = rubraview_tiles_visible(x0 - margin, y0 - margin, x1 + margin, y1 + margin, level, pw, ph, keys, 128);
    size_t m = 0;
    for (size_t i = 0; i < n; ++i) {
        struct tile_slot *slot = tile_find(ts, keys[i]);
        if (!slot) { missing[m++] = keys[i]; continue; }
        slot->used = ++ts->clock;
        int32_t x, y, w, h, ow, oh;
        if (!slot->texture || !rubraview_tile_geometry(keys[i], pw, ph, &x, &y, &w, &h, &ow, &oh)) continue;
        rubraview_mat3x2_t tile = rubraview_mat3x2_multiply(
            rubraview_mat3x2_multiply(rubraview_mat3x2_scale((double)w / (double)ow, (double)h / (double)oh),
                                      rubraview_mat3x2_translate((double)x, (double)y)),
            place);
        rubraview_pal_render_draw_texture_opacity(app->renderer, slot->texture, tile, interp, opacity);
    }
    if (m != ts->asked_count || memcmp(missing, ts->asked, m * sizeof(missing[0])) != 0) {
        rubraview_pal_tiles_want(ts->tiles, ts->generation, missing, m);
        if (m) memcpy(ts->asked, missing, m * sizeof(missing[0]));
        ts->asked_count = m;
    }
}

/* Each pass: take in what the thread has made, and ask for the tiles now
   on screen that have not been asked for. Nothing here waits. */
/* The owner (2026-10-18): folders that show no thumbnail, typed as the folders kept out of Recent
   are. 1.20.0 ("그 폴더와 그 폴더의 하위 폴더 썸네일만 안 보이게"): only those folders' own tiles,
   and the tiles of the folders in them; the files inside show theirs. */
static bool thumb_kept_out(const app_state_t *app, proven_u8str_view_t path, bool is_directory) {
    proven_u8str_view_t dirs[64];
    size_t n = rubraview_folder_list_split(rubraview_settings_get_text(&app->settings, PROVEN_LIT("thumbs"), PROVEN_LIT("skip_dirs")), dirs, 64);
    if (n > 64) n = 64;
    return n > 0 && path.size > 0 && rubraview_thumb_folder_kept_out(path, is_directory, dirs, n);
}

static void picker_thumbs_step(app_state_t *app) {
    app->picker_thumbs_arrived = false;
    if (!app->picker_open || !app->picker_thumbs) return;
    if (!app->thumbs) {
        /* A folder's picture: its first image, else its first film —
           not a sound file, which seldom has one (rbmedia, 2026-09-25). */
        app->thumbs = rubraview_pal_thumbs_start(PROVEN_LIT(IMAGE_FILTER), PROVEN_LIT(VIDEO_FILTER), PROVEN_LIT(ARCHIVE_FILTER),
                                                 thumbs_wake, app->window);
        if (!app->thumbs) return;
        rubraview_pal_thumbs_generation(app->thumbs, app->picker_generation);
    }

    rubraview_thumb_result_t done;
    while (rubraview_pal_thumbs_take(app->thumbs, &done)) {
        if (done.generation == app->picker_generation && done.index < app->picker_thumb_count) {
            struct picker_thumb *t = &app->picker_thumbs[done.index];
            rubraview_texture_t *texture = NULL;
            if (done.bgra) {
                texture = rubraview_pal_texture_create_bgra(app->renderer, done.width, done.height);
                if (texture && !rubraview_pal_texture_upload_bgra(texture, done.bgra, done.width * 4)) {
                    rubraview_pal_texture_destroy(texture);
                    texture = NULL;
                }
            }
            if (t->texture) rubraview_pal_texture_destroy(t->texture);
            t->texture = texture;
            t->state = texture ? 1 : 2;
            /* what the file is, for the details view's columns (owner, 2026-10-16) */
            t->media = (rubraview_picker_media_t){ done.source_width, done.source_height, done.seconds };
            app->picker_thumbs_arrived = true;
        }
        free(done.bgra);
    }

    double tw = 0.0, th = 0.0;
    picker_tile_size(app, &tw, &th);
    if (tw <= 1.0 || th <= 1.0) return;
    /* The newest request is made first, so the last tile on screen is asked
       for first and the first one last: they fill in from the top. */
    rubraview_virtual_range_t visible = rubraview_picker_visible(&app->picker);
    int book_reads = 0;
    for (size_t k = visible.count; k-- > 0;) {
        size_t index = visible.first + k;
        if (index >= app->picker_thumb_count || index >= app->picker_listing.count) continue;
        struct picker_thumb *t = &app->picker_thumbs[index];
        if (t->state != 0 && t->state != 3) continue;
        const rubraview_fs_entry_t *entry = &app->picker_listing.entries[index];
        if (entry->is_directory && proven_u8str_view_eq(entry->name, PROVEN_LIT(".."))) { t->state = 2; continue; }
        /* A drive's first picture is not looked for: an empty reader or a
           lost network drive would hold the one thumbnail thread. */
        if (rubraview_picker_is_places(app->picker_dir)) { t->state = 2; continue; }
        if (!app->picker_in_book && thumb_kept_out(app, entry->path, entry->is_directory)) { t->state = 2; continue; }
        if (app->picker_in_book) {
            /* A page in the book has no file to ask about: its bytes go to
               the thread instead (owner, 2026-09-29) — three a pass, so the
               picker keeps moving, and never a page far inside a streamed
               7z block. */
            size_t page = app->picker_page_of && index >= app->picker_page_skip
                ? app->picker_page_of[index - app->picker_page_skip] : RUBRAVIEW_PICKER_NOT_A_PAGE;
            if (entry->is_directory || page == RUBRAVIEW_PICKER_NOT_A_PAGE || page >= page_count(app) ||
                page_read_cost(app, page) > PAGE_JOB_MIN_COST) { t->state = 2; continue; }
            if (book_reads >= 3) continue;   /* the next pass */
            book_reads++;
            void *mem = NULL;
            rubraview_page_bytes_t pb = page_read_owned(&app->source, &app->source_lock, page, &mem);
            uint8_t *copy = pb.ok && pb.data.size > 0 ? (uint8_t*)malloc(pb.data.size) : NULL;
            if (copy) memcpy(copy, pb.data.ptr, pb.data.size);
            free(mem);
            if (copy && rubraview_pal_thumbs_request_bytes(app->thumbs, app->picker_generation, index, tw / th, copy, pb.data.size)) {
                t->state = 3;
            } else {
                t->state = 2;
            }
            continue;
        }
        /* Asked again while waiting, it moves to the front of the queue. */
        if (rubraview_pal_thumbs_request(app->thumbs, app->picker_generation, index, entry->is_directory,
                                         tw / th, entry->path)) {
            t->state = 3;
        } else if (t->state == 0) {
            t->state = 2;   /* a path the queue cannot hold */
        }
    }
}

/* ---- The wheel's strip (owner, 2026-10-15) ---- */

#define STRIP_PEEK_SECONDS 2.5
#define STRIP_FADE_SECONDS 0.4

static void strip_cells_free(app_state_t *app) {
    for (size_t i = 0; i < app->strip_cell_count; ++i) {
        if (app->strip_cells[i].texture) rubraview_pal_texture_destroy(app->strip_cells[i].texture);
    }
    free(app->strip_cells);
    app->strip_cells = NULL;
    app->strip_cell_count = 0;
}

/* A new book or folder: new cells, and what is still being made is for nobody. */
void strip_cells_reset(app_state_t *app) {
    strip_cells_free(app);
    app->strip_generation++;
    if (app->strip_thumbs) rubraview_pal_thumbs_generation(app->strip_thumbs, app->strip_generation);
    size_t n = page_count(app);
    app->strip_cells = n ? (struct picker_thumb*)calloc(n, sizeof(*app->strip_cells)) : NULL;
    app->strip_cell_count = app->strip_cells ? n : 0;
}

/* How much of the strip shows: 1 while it is up, fading after the wheel's time, 0 when gone. */
static uint32_t strip_fade(uint32_t argb, double k) {   /* alpha times k, 0..1 */
    uint32_t alpha = (uint32_t)lround((double)((argb >> 24) & 0xFFu) * (k < 0.0 ? 0.0 : k > 1.0 ? 1.0 : k));
    return (alpha << 24) | (argb & 0x00FFFFFFu);
}

static double strip_shown(const app_state_t *app) {
    if (app->filmstrip.visible) return 1.0;
    double left = app->strip_peek_until - rubraview_pal_time_now_seconds();
    if (left <= -STRIP_FADE_SECONDS) return 0.0;
    return left >= 0.0 ? 1.0 : 1.0 + left / STRIP_FADE_SECONDS;
}

static rubraview_pal_rect_t strip_rect(const app_state_t *app) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    double strip_h = FILMSTRIP_THUMB * rubraview_pal_window_dpi_scale(app->window);
    return (rubraview_pal_rect_t){ 0.0, (double)h - strip_h, (double)w, strip_h };
}

/* The page on screen is the one the strip slides to; a strip not showing goes there at once. */
void strip_follow(app_state_t *app, size_t page) {
    app->strip_target = rubraview_filmstrip_centre_offset(&app->filmstrip, page);
    if (strip_shown(app) <= 0.0) app->filmstrip.scroll_offset = app->strip_target;
}

/* The wheel turned a page: the strip comes up, or stays up longer. */
static void strip_peek(app_state_t *app) {
    if (page_count(app) < 2) return;
    /* 1.22.0 (owner, 2026-10-19: "마우스 휠로 이미지 파일 사이를 이동할 때 하단에 보이는 썸네일 ... 끄고 킬 수 있게") */
    if (rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("wheel_strip")) <= 0.5) return;
    app->strip_peek_until = rubraview_pal_time_now_seconds() + STRIP_PEEK_SECONDS;
}

/* Each pass: slide, keep it up under the pointer, take in thumbnails, ask for the ones in view. */
static void strip_step(app_state_t *app, double dt) {
    app->strip_arrived = false;
    double shown = strip_shown(app);
    if (shown > 0.0 && !app->filmstrip.visible && app->pointer_inside &&
        pal_rect_has(strip_rect(app), app->pointer_x, app->pointer_y)) {
        double now = rubraview_pal_time_now_seconds();
        if (app->strip_peek_until < now + 1.0) app->strip_peek_until = now + 1.0;   /* held while pointed at */
    }
    app->filmstrip.scroll_offset = rubraview_filmstrip_glide(app->filmstrip.scroll_offset, app->strip_target, dt);
    bool tip = app->list_tip_page >= 0 && (size_t)app->list_tip_page < page_count(app);   /* 1.25.0: a list row's thumbnail */
    if (shown <= 0.0 && !tip) return;
    if (app->strip_cell_count != page_count(app)) strip_cells_reset(app);
    if (!app->strip_cells) return;
    if (!app->strip_thumbs) {
        app->strip_thumbs = rubraview_pal_thumbs_start(PROVEN_LIT(IMAGE_FILTER), PROVEN_LIT(VIDEO_FILTER), PROVEN_LIT(ARCHIVE_FILTER),
                                                       thumbs_wake, app->window);
        if (!app->strip_thumbs) return;
        rubraview_pal_thumbs_generation(app->strip_thumbs, app->strip_generation);
    }
    rubraview_thumb_result_t done;
    while (rubraview_pal_thumbs_take(app->strip_thumbs, &done)) {
        if (done.generation == app->strip_generation && done.index < app->strip_cell_count) {
            struct picker_thumb *t = &app->strip_cells[done.index];
            rubraview_texture_t *texture = NULL;
            if (done.bgra) {
                texture = rubraview_pal_texture_create_bgra(app->renderer, done.width, done.height);
                if (texture && !rubraview_pal_texture_upload_bgra(texture, done.bgra, done.width * 4)) {
                    rubraview_pal_texture_destroy(texture);
                    texture = NULL;
                }
            }
            if (t->texture) rubraview_pal_texture_destroy(t->texture);
            t->texture = texture;
            t->state = texture ? 1 : 2;
            app->strip_arrived = true;
        }
        free(done.bgra);
    }
    /* The ones in view, nearest the page last so they are made first; a list row's, pointed at, last of all. */
    rubraview_virtual_range_t visible = shown > 0.0 ? rubraview_filmstrip_visible(&app->filmstrip) : (rubraview_virtual_range_t){0};
    int book_reads = 0;
    for (size_t k = visible.count + (tip ? 1 : 0); k-- > 0;) {
        size_t index = k == visible.count ? (size_t)app->list_tip_page : visible.first + k;
        if (index >= app->strip_cell_count) continue;
        struct picker_thumb *t = &app->strip_cells[index];
        if (t->state != 0) continue;
        proven_u8str_view_t path = app->source.pages[index].path;
        if (path.size > 0) {
            t->state = rubraview_pal_thumbs_request(app->strip_thumbs, app->strip_generation, index, false, 1.0, path) ? 3 : 2;
            continue;
        }
        /* A page inside a book: its bytes, three a pass, never one far inside a streamed block. */
        if (page_read_cost(app, index) > PAGE_JOB_MIN_COST) { t->state = 2; continue; }
        if (book_reads >= 3) continue;
        book_reads++;
        void *mem = NULL;
        rubraview_page_bytes_t pb = page_read_owned(&app->source, &app->source_lock, index, &mem);
        uint8_t *copy = pb.ok && pb.data.size > 0 ? (uint8_t*)malloc(pb.data.size) : NULL;
        if (copy) memcpy(copy, pb.data.ptr, pb.data.size);
        free(mem);
        t->state = copy && rubraview_pal_thumbs_request_bytes(app->strip_thumbs, app->strip_generation, index, 1.0, copy, pb.data.size) ? 3 : 2;
    }
}

/* A click on the strip: the page whose thumbnail it is. True when the strip took it. */
static bool strip_press(app_state_t *app, double x, double y) {
    if (strip_shown(app) <= 0.0 || !pal_rect_has(strip_rect(app), x, y)) return false;
    size_t page = 0;
    if (rubraview_filmstrip_index_at(&app->filmstrip, x, &page) && page < page_count(app)) {
        go_to_spread(app, spread_for_chosen_page(app, (int32_t)page));
        if (!app->filmstrip.visible) app->strip_peek_until = rubraview_pal_time_now_seconds() + STRIP_PEEK_SECONDS;
    }
    return true;
}

#define PICKER_ARENA_BYTES (32u * 1024u * 1024u)
#define APP_ARENA_LOW (16u * 1024u * 1024u)

/* The working arena is never given back, and when it filled, everything
   that took from it failed without a word (2026-09-29). What fills it
   fastest now has memory of its own (pages, the picker, subtitle
   pictures); as a last guard, a working arena running low goes on in a
   new block. The full one is kept: what is in it is still in use. */
static void arena_refill(app_state_t *app) {
    /* RFC-0006: the persist region moves on to a new heap chunk; app->arena is that region's arena. */
    if (app->mem && !rubraview_region_ensure(&app->mem->persist, APP_ARENA_LOW)) app->mem->full = true;
}

/* The idle picker arena, emptied: the next listing is built in it. */
static proven_arena_t *picker_next_arena(app_state_t *app) {
    int side = app->picker_side ^ 1;
    if (!app->picker_arena_mem[side]) {
        app->picker_arena_mem[side] = malloc(PICKER_ARENA_BYTES);
        if (!app->picker_arena_mem[side]) return app->arena;   /* no room: the old way */
        app->picker_arenas[side] = proven_arena_create((proven_mem_mut_t){
            .ptr = (proven_byte_t*)app->picker_arena_mem[side], .size = PICKER_ARENA_BYTES });
    }
    proven_arena_reset(&app->picker_arenas[side]);
    return &app->picker_arenas[side];
}

/* A string kept past the listing it came from (an undo entry, a
   favourite, a path being opened), copied into the working arena. */
proven_u8str_view_t app_keep(app_state_t *app, proven_u8str_view_t text) {
    if (text.size == 0) return text;
    /* RFC-0006 (RV-141): room is made in the session's region; when there is none, nothing
       is kept and that is said — the text handed in is never given back as if it were kept,
       since a caller's stack buffer would then be held after it is gone. */
    if (app->mem && !rubraview_region_ensure(&app->mem->persist, text.size + 1)) { app->mem->full = true; return PROVEN_LIT(""); }
    proven_result_mem_mut_t res = proven_arena_alloc(app->arena, text.size + 1);
    if (!proven_is_ok(res.err)) { if (app->mem) app->mem->full = true; return PROVEN_LIT(""); }
    memcpy(res.value.ptr, text.ptr, text.size);
    res.value.ptr[text.size] = '\0';
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)res.value.ptr, .size = text.size };
}

/* The recent files as a listing (owner, 2026-10-16): newest first, each read from the
   disk so the details have its size and dates; one gone since is left out. */
static rubraview_fs_listing_t recent_listing(app_state_t *app, proven_arena_t *pa) {
    rubraview_fs_listing_t out = {0};
    recent_sync(app, PROVEN_LIT(""), false);   /* another viewer's too (D-104) */
    size_t newest[RECENT_KEPT];
    size_t shown = rubraview_recent_newest(&app->recent, newest, RECENT_KEPT);
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(pa, shown ? shown : 1, sizeof(rubraview_fs_entry_t));
    if (!proven_is_ok(res.err)) return out;
    out.entries = (rubraview_fs_entry_t*)(void*)res.value.ptr;
    for (size_t r = 0; r < shown; ++r) {
        rubraview_fs_entry_t e;
        if (!rubraview_pal_fs_stat(pa, app->recent.list.entries[newest[r]].path, &e)) continue;
        out.entries[out.count++] = e;
    }
    return out;
}

/* The details view's order (owner, 2026-10-16): the column chosen, kept in
   Settings › Files; every array that follows the listing is put in the same order. */
static void picker_apply_sort(app_state_t *app) {
    int32_t choice = (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort")));
    bool desc = rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort_desc")) > 0.5;
    size_t n = app->picker_listing.count;
    if (choice <= 0 || n < 2 || rubraview_picker_is_places(app->picker_dir)) return;   /* as the folder has it */
    size_t *order = (size_t*)malloc(n * sizeof(size_t));
    rubraview_picker_media_t *media = (rubraview_picker_media_t*)calloc(n, sizeof(rubraview_picker_media_t));
    rubraview_fs_entry_t *entries = (rubraview_fs_entry_t*)malloc(n * sizeof(rubraview_fs_entry_t));
    struct picker_thumb *thumbs = (struct picker_thumb*)malloc(n * sizeof(struct picker_thumb));
    bool *picked = (bool*)malloc(n * sizeof(bool));
    if (order && media && entries && thumbs && picked) {
        bool have_thumbs = app->picker_thumbs && app->picker_thumb_count == n;
        for (size_t i = 0; i < n && have_thumbs; ++i) media[i] = app->picker_thumbs[i].media;
        rubraview_picker_sort_order(&app->picker_listing, media, (rubraview_picker_sort_key_t)(choice - 1), desc, order);
        size_t focus = app->picker.focus;
        for (size_t i = 0; i < n; ++i) {
            entries[i] = app->picker_listing.entries[order[i]];
            if (have_thumbs) thumbs[i] = app->picker_thumbs[order[i]];
            picked[i] = app->picker_selected ? app->picker_selected[order[i]] : false;
            if (order[i] == focus) app->picker.focus = i;
        }
        memcpy((void*)app->picker_listing.entries, entries, n * sizeof(rubraview_fs_entry_t));
        if (have_thumbs) memcpy(app->picker_thumbs, thumbs, n * sizeof(struct picker_thumb));
        if (app->picker_selected) memcpy(app->picker_selected, picked, n * sizeof(bool));
        if (app->picker_in_book && app->picker_page_of) {
            /* a page's place in the book goes with its entry; `..` stays first */
            size_t skip = app->picker_page_skip;
            size_t *pages = (size_t*)malloc(n * sizeof(size_t));
            if (pages) {
                for (size_t i = skip; i < n; ++i) pages[i] = order[i] >= skip ? app->picker_page_of[order[i] - skip] : RUBRAVIEW_PICKER_NOT_A_PAGE;
                for (size_t i = skip; i < n; ++i) app->picker_page_of[i - skip] = pages[i];
                free(pages);
            }
        }
        app->picker.range_anchor = RUBRAVIEW_PICKER_NO_ANCHOR;
    }
    free(order); free(media); free(entries); free(thumbs); free(picked);
}

/* A column's name pressed: by it, rising; pressed again, falling. Kept in Settings › Files. */
bool picker_is_up(const app_state_t *app, size_t index);
void picker_filter_set(app_state_t *app, proven_u8str_view_t text);
void picker_filter_forget(app_state_t *app);
void ctx_close(app_state_t *app);
void picker_activate(app_state_t *app, size_t index);
void slidepop_close(app_state_t *app);
void listwin_sync(app_state_t *app);
void list_insert_paths(app_state_t *app, const proven_u8str_view_t *paths, size_t count, size_t to);
size_t picker_picked_entries(app_state_t *app, rubraview_fs_entry_t **out);

/* 1.24.0: by a column, rising or falling, chosen outright. */
void picker_sort_set(app_state_t *app, int key, bool desc) {
    rubraview_settings_set(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort"), (double)(key + 1));
    rubraview_settings_set(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort_desc"), desc ? 1.0 : 0.0);
    settings_write_if_changed(app, false);
    char kept[sizeof(app->picker_filter)];
    memcpy(kept, app->picker_filter, sizeof(kept));
    if (kept[0]) picker_filter_set(app, PROVEN_LIT(""));   /* 1.22.0: the whole folder sorted, then filtered again */
    picker_apply_sort(app);
    if (kept[0]) picker_filter_set(app, cstr(kept));
    else app->picker_full = app->picker_listing;
    char line[64];
    int n = snprintf(line, sizeof(line), T("by %s, %s"), rubraview_picker_sort_name((rubraview_picker_sort_key_t)key),
                     desc ? "falling" : "rising");
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
}

void picker_sort_by(app_state_t *app, int key) {
    int now = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort"))) - 1;
    bool desc = rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort_desc")) > 0.5;
    picker_sort_set(app, key, now == key ? !desc : false);
}

/* 1.24.0 (owner, 2026-10-19): the folders the open-file screen went through, for back and forward. */
#define PICKER_HIST 24
#define PICKER_HIST_PATH 1024

static void picker_hist_push(app_state_t *app, bool forward, proven_u8str_view_t dir) {
    if (dir.size == 0 || dir.size >= PICKER_HIST_PATH) return;
    if (!app->picker_hist) app->picker_hist = calloc(2 * PICKER_HIST, PICKER_HIST_PATH);
    if (!app->picker_hist) return;
    char (*stack)[PICKER_HIST_PATH] = app->picker_hist + (forward ? PICKER_HIST : 0);
    int32_t *n = forward ? &app->picker_fwd_n : &app->picker_back_n;
    if (*n == PICKER_HIST) { memmove(stack[0], stack[1], (PICKER_HIST - 1) * PICKER_HIST_PATH); --*n; }   /* the oldest goes */
    memcpy(stack[*n], dir.ptr, dir.size);
    stack[*n][dir.size] = '\0';
    ++*n;
}

static void picker_navigate_to(app_state_t *app, proven_u8str_view_t dir, bool from_history);

void picker_navigate(app_state_t *app, proven_u8str_view_t dir) {
    picker_navigate_to(app, dir, false);
}

/* Back (forward false) or forward through the folders gone through; Alt+Left, Alt+Right. */
static void picker_history_go(app_state_t *app, bool forward) {
    int32_t *from = forward ? &app->picker_fwd_n : &app->picker_back_n;
    if (*from <= 0 || !app->picker_hist) { osd_say(app, forward ? TU8("nothing to go forward to") : TU8("nothing to go back to")); return; }
    char target[PICKER_HIST_PATH];
    snprintf(target, sizeof(target), "%s", app->picker_hist[(forward ? PICKER_HIST : 0) + --*from]);
    picker_hist_push(app, !forward, app->picker_dir);
    picker_navigate_to(app, cstr(target), true);
    app->picker.focus = 0;
}

static void picker_go_up(app_state_t *app) {
    proven_u8str_view_t parent = rubraview_picker_parent(app->picker_dir);
    if (parent.size == 0) return;
    picker_navigate(app, parent);
    app->picker.focus = 0;
}

static void picker_navigate_to(app_state_t *app, proven_u8str_view_t dir, bool from_history) {
    /* Where it was, kept before the listing's memory turns over; another folder goes on the way back. */
    char was[PICKER_HIST_PATH];
    size_t was_len = app->picker_dir.size < sizeof(was) ? app->picker_dir.size : 0;
    if (was_len) memcpy(was, app->picker_dir.ptr, was_len);
    if (!from_history && app->picker_open && was_len && !rubraview_path_same((proven_u8str_view_t){ .ptr = (const proven_byte_t *)was, .size = was_len }, dir)) {
        picker_hist_push(app, false, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)was, .size = was_len });
        app->picker_fwd_n = 0;
    }
    proven_arena_t *pa = picker_next_arena(app);
    /* The PC page lists drives and the usual folders, in the PAL's order
       (owner, 2026-09-28: the whole machine, not only the folder the
       viewer was started in). */
    bool places = rubraview_picker_is_places(dir);
    bool recent = rubraview_picker_is_recent(dir);
    /* Inside the book being read (owner, 2026-09-28): its own folders and
       pages, a level at a time, in the order they are read. */
    proven_u8str_view_t inner = PROVEN_LIT("");
    bool in_book = !places && !recent && app->source.kind != RUBRAVIEW_PAGE_SOURCE_FOLDER &&
                   rubraview_picker_inside_archive(dir, app->source.archive_path, &inner);
    rubraview_archive_level_t level = {0};
    if (in_book) {
        level = rubraview_picker_archive_level(pa, app->source.archive_path,
                                               app->source.pages, app->source.page_count, inner);
    }
    /* "." has no parent by name, which kept the reader below the folder the
       viewer was run from (owner, 2026-09-28): every folder is made whole. */
    if (!places && !in_book && !recent) dir = rubraview_pal_fs_absolute(pa, dir);
    rubraview_fs_listing_t listing = in_book ? level.listing
                                   : places ? rubraview_pal_fs_list_places(pa)
                                   : recent ? recent_listing(app, pa)
                                            : rubraview_pal_fs_list_dir(pa, dir);
    /* The reader's favourites first on the PC page, every one of them (owner,
       2026-10-17: the places bar shows as many as fit; the list shows all). */
    if (places && app->favorites.count > 0) {
        proven_result_mem_mut_t res = rubraview_arena_alloc_array(pa, app->favorites.count + listing.count, sizeof(rubraview_fs_entry_t));
        if (proven_is_ok(res.err)) {
            rubraview_fs_entry_t *all = (rubraview_fs_entry_t *)(void *)res.value.ptr;
            for (size_t i = 0; i < app->favorites.count; ++i) {
                rubraview_fs_entry_t st;   /* 1.24.0: a favourite may be a file */
                bool folder = !rubraview_pal_fs_stat(pa, app->favorites.paths[i], &st) || st.is_directory;
                all[i] = (rubraview_fs_entry_t){ .name = rubraview_favorites_title(&app->favorites, i),
                                                 .path = app->favorites.paths[i], .is_directory = folder };
            }
            if (listing.count) memcpy(all + app->favorites.count, listing.entries, listing.count * sizeof(rubraview_fs_entry_t));
            listing.entries = all;
            listing.count += app->favorites.count;
        }
    }
    /* Only folders and what the viewer opens (owner, 2026-09-21). */
    char kinds_buf[1024];
    size_t hidden = in_book ? 0 : rubraview_picker_keep_openable(&listing, kinds_with_text(kinds_buf, sizeof(kinds_buf), IMAGE_FILTER ";" MEDIA_FILTER ";" ARCHIVE_FILTER ";*.cue;" LIST_FILTER));
    /* A RAR set is one book (owner, 2026-10-01): its first volume stands for it. */
    if (!in_book) {
        size_t kept = 0;
        for (size_t i = 0; i < listing.count; ++i) {
            if (!listing.entries[i].is_directory && rubraview_page_source_is_later_volume(listing.entries[i].name)) { hidden++; continue; }
            listing.entries[kept++] = listing.entries[i];
        }
        listing.count = kept;
    }
    if (listing.count == 0 && rubraview_picker_parent(dir).size == 0) {
        if (hidden > 0) osd_say(app, TU8("nothing in that folder can be opened here"));
        return;
    }
    if (listing.count == 0 && hidden > 0) {
        /* D-18 kept the reader out of a folder with nothing to open; with
           `..` on screen it is no longer a trap, so it is only said. */
        osd_say(app, TU8("nothing here can be opened"));
    }
    app->picker_hidden = hidden;

    /* Directories first, then files, each in the Sort by order — folders are
       what a reader scans for first on a touch screen. */
    rubraview_sort_item_t *items = NULL;
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(pa, listing.count, sizeof(rubraview_sort_item_t));
    if (!places && !in_book && !recent && proven_is_ok(res.err)) {   /* the recent files stay newest first */
        items = (rubraview_sort_item_t*)(void*)res.value.ptr;
        for (size_t i = 0; i < listing.count; ++i) {
            items[i] = (rubraview_sort_item_t){
                .name = listing.entries[i].name,
                .mtime = listing.entries[i].mtime,
                .ctime = listing.entries[i].ctime,
                .size_bytes = listing.entries[i].size_bytes,
                .tag = (uint64_t)i,
            };
        }
        /* Settings > Files > Sort by, as the pages are turned (owner,
           2026-09-28); the two passes below put the folders first. */
        bool ascending = true;
        rubraview_sort_mode_t mode = folder_sort_mode(app, &ascending);
        rubraview_sort_items(items, listing.count, mode, ascending, NULL);

        proven_result_mem_mut_t ordered_res =
            rubraview_arena_alloc_array(pa, listing.count, sizeof(rubraview_fs_entry_t));
        if (proven_is_ok(ordered_res.err)) {
            rubraview_fs_entry_t *ordered = (rubraview_fs_entry_t*)(void*)ordered_res.value.ptr;
            size_t out = 0;
            for (int folder_pass = 1; folder_pass >= 0; --folder_pass) {
                for (size_t i = 0; i < listing.count; ++i) {
                    const rubraview_fs_entry_t *e = &listing.entries[items[i].tag];
                    if ((int)e->is_directory != folder_pass) continue;
                    ordered[out++] = *e;
                }
            }
            listing.entries = ordered;
        }
    }

    /* `..` first, wherever there is a folder above this one (owner,
       2026-09-23). `Ctrl+Backspace` did this already, but not by touch.
       Above a drive is the PC page, so every folder has one. */
    proven_u8str_view_t parent = rubraview_picker_parent(dir);
    app->picker_in_book = in_book;
    app->picker_page_of = level.page_of;
    app->picker_page_skip = 0;
    if (parent.size > 0) {
        proven_result_mem_mut_t up_res =
            rubraview_arena_alloc_array(pa, (listing.count + 1), sizeof(rubraview_fs_entry_t));
        if (proven_is_ok(up_res.err)) {
            rubraview_fs_entry_t *with_up = (rubraview_fs_entry_t*)(void*)up_res.value.ptr;
            with_up[0] = (rubraview_fs_entry_t){
                .name = PROVEN_LIT(".."),
                .path = parent,
                .is_directory = true,
            };
            memcpy(with_up + 1, listing.entries, listing.count * sizeof(rubraview_fs_entry_t));
            listing.entries = with_up;
            listing.count += 1;
            app->picker_page_skip = 1;
        }
    }

    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double tile = PICKER_TILE * dpi;

    proven_result_mem_mut_t sel_res = rubraview_arena_alloc_array(pa, listing.count, sizeof(bool));
    app->picker_selected = proven_is_ok(sel_res.err) ? (bool*)(void*)sel_res.value.ptr : NULL;
    if (app->picker_selected) memset(app->picker_selected, 0, listing.count * sizeof(bool));

    rubraview_picker_mode_t mode = app->picker.mode;   /* the mode outlives the folder */
    picker_thumbs_free(app);
    app->picker_generation++;
    if (app->thumbs) rubraview_pal_thumbs_generation(app->thumbs, app->picker_generation);
    app->picker_thumbs = (struct picker_thumb *)calloc(listing.count ? listing.count : 1, sizeof(*app->picker_thumbs));
    app->picker_thumb_count = app->picker_thumbs ? listing.count : 0;
    app->picker_dir = dir;
    if (pa != app->arena) app->picker_side ^= 1;   /* the new listing is the one shown */
    app->picker_listing = listing;
    app->picker = rubraview_picker_create(&app->picker_listing, tile,
                                          (double)win_h - picker_top(app) - (PICKER_HEADER_HEIGHT + PICKER_ACTION_HEIGHT) * dpi,
                                          picker_columns(app));
    picker_layout(app);
    app->picker.selected = app->picker_selected;
    rubraview_picker_set_mode(&app->picker, mode);
    /* The focus starts on the first file: `..` and the folders are for
       going somewhere, and "Same type" reads the focused file. */
    for (size_t i = 0; i < listing.count; ++i) {
        if (!listing.entries[i].is_directory) { app->picker.focus = i; break; }
    }
    picker_apply_sort(app);   /* the column chosen in the details view, kept (owner, 2026-10-16) */
    picker_filter_forget(app);   /* 1.22.0: the folder is what a filter is taken from */
}

/* The extension of the item the focus is on, ".jpg" and such. */
proven_u8str_view_t picker_focus_extension(const app_state_t *app) {
    proven_u8str_view_t none = PROVEN_LIT("");
    if (app->picker.focus < app->picker_listing.count) {
        const rubraview_fs_entry_t *entry = &app->picker_listing.entries[app->picker.focus];
        if (!entry->is_directory) return rubraview_path_ext(entry->name);
    }
    /* The focus is on a folder: the first picked file answers instead. */
    for (size_t i = 0; i < app->picker_listing.count; ++i) {
        if (!app->picker_selected || !app->picker_selected[i]) continue;
        if (app->picker_listing.entries[i].is_directory) continue;
        return rubraview_path_ext(app->picker_listing.entries[i].name);
    }
    return none;
}

void rename_extension_begin(app_state_t *app);
static void rename_picker_begin(app_state_t *app, size_t at);
static void ext_switch(app_state_t *app, int32_t row);
static void rename_extension_apply(app_state_t *app);
void text_find_step(app_state_t *app, bool forward);
static void rename_picker_commit(app_state_t *app);
void open_set_from_entries(app_state_t *app, rubraview_fs_entry_t *entries, size_t count, proven_u8str_view_t said);

/* The picked files, gathered as a listing the page source can take. */

size_t picker_picked_entries(app_state_t *app, rubraview_fs_entry_t **out) {
    size_t picked = 0;
    for (size_t i = 0; i < app->picker_listing.count; ++i) {
        if (app->picker_selected && app->picker_selected[i] && !picker_is_up(app, i)) picked++;   /* 1.22.0: folders too */
    }
    if (picked == 0) return 0;
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, picked, sizeof(rubraview_fs_entry_t));
    if (!proven_is_ok(res.err)) return 0;
    rubraview_fs_entry_t *entries = (rubraview_fs_entry_t*)(void*)res.value.ptr;
    size_t n = 0;
    for (size_t i = 0; i < app->picker_listing.count; ++i) {
        if (!app->picker_selected || !app->picker_selected[i]) continue;
        if (picker_is_up(app, i)) continue;
        entries[n++] = app->picker_listing.entries[i];
    }
    *out = entries;
    return n;
}

/* The picked files as a playlist of their own, then opened (owner,
   2026-09-23, who preferred this to opening them loose). The file is
   `PlaylistNNNN.m3u8` in this folder — the first number not taken. */
static void picker_make_playlist(app_state_t *app) {
    rubraview_fs_entry_t *entries = NULL;
    size_t n = picker_picked_entries(app, &entries);
    if (n == 0) { osd_say(app, TU8("pick some files first")); return; }

    rubraview_playlist_t list = {0};
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, n, sizeof(rubraview_playlist_entry_t));
    if (!proven_is_ok(res.err)) return;
    list.entries = (rubraview_playlist_entry_t*)(void*)res.value.ptr;
    size_t files = 0;
    for (size_t i = 0; i < n; ++i) {
        if (entries[i].is_directory) continue;   /* 1.22.0: a folder picked is not a song */
        list.entries[files] = (rubraview_playlist_entry_t){ .path = entries[i].name };   /* beside the file */
        entries[files++] = entries[i];
    }
    if (files == 0) { osd_say(app, TU8("pick some files first")); return; }
    n = files;
    list.count = n;

    proven_u8str_view_t text = rubraview_playlist_serialize_m3u8(app->arena, &list);
    if (text.size == 0) { osd_say(app, TU8("could not write the playlist")); return; }

    char name[32];
    proven_u8str_view_t target = PROVEN_LIT("");
    for (int number = 1; number <= 9999; ++number) {
        int written = snprintf(name, sizeof(name), "Playlist%04d.m3u8", number);
        if (written <= 0) return;
        proven_u8str_view_t candidate = rubraview_path_join(app->arena, app->picker_dir,
                                                (proven_u8str_view_t){ .ptr = (const proven_byte_t *)name, .size = (size_t)written });
        rubraview_fs_entry_t taken;
        if (!rubraview_pal_fs_stat(app->arena, candidate, &taken)) { target = candidate; break; }
    }
    if (target.size == 0) { osd_say(app, TU8("too many playlists in this folder")); return; }
    if (!rubraview_pal_fs_write_file(target, text)) { osd_say(app, TU8("could not write the playlist")); return; }

    char line[96];
    int written = snprintf(line, sizeof(line), T("%zu file(s) in %s"), n, name);
    open_set_from_entries(app, entries, n, written > 0 ? (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)written }
                                                       : TU8("playlist opened"));
}

/* The picked files to the recycle bin, once the button has been pressed
   twice (§3.18.1 asks before anything leaves the disk). */
void picker_recycle(app_state_t *app) {
    rubraview_fs_entry_t *entries = NULL;
    size_t n = picker_picked_entries(app, &entries);
    if (n == 0) { osd_say(app, TU8("pick some files first")); return; }

    double now = rubraview_pal_time_now_seconds();
    if (!rubraview_confirm_armed(&app->picker_confirm, PICKER_BTN_RECYCLE, now)) {
        rubraview_confirm_press(&app->picker_confirm, PICKER_BTN_RECYCLE, now);
        char line[96];
        int written = snprintf(line, sizeof(line), T("press Recycle again to bin %zu file(s)"), n);
        if (written > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)written });
        return;
    }
    rubraview_confirm_clear(&app->picker_confirm);

    size_t done = 0;
    uint32_t batch = ++app->undo_batch;
    for (size_t i = 0; i < n; ++i) {
        if (!rubraview_pal_fs_recycle(entries[i].path)) continue;
        rubraview_undo_push(&app->undo, (rubraview_file_action_t){
            .op = RUBRAVIEW_FILE_OP_RECYCLE,
            .source_path = app_keep(app, entries[i].path),
            .target_path = app_keep(app, entries[i].path),
            .batch = batch,
        });
        done++;
    }
    char line[96];
    int written = snprintf(line, sizeof(line), T("%zu file(s) in the recycle bin"), done);
    if (written > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)written });
    picker_navigate(app, app->picker_dir);
}

/* Move or copy the picked files into a folder. */
static void picker_curate_to(app_state_t *app, proven_u8str_view_t target_dir, bool moving) {
    rubraview_fs_entry_t *entries = NULL;
    size_t n = picker_picked_entries(app, &entries);
    if (n == 0) { osd_say(app, TU8("pick some files first")); return; }

    rubraview_pal_fs_make_dirs(target_dir);
    size_t done = 0;
    uint32_t batch = ++app->undo_batch;   /* 1.23.0: one undo for all of them */
    for (size_t i = 0; i < n; ++i) {
        proven_u8str_view_t target = rubraview_path_join(app->arena, target_dir, entries[i].name);
        if (target.size == 0) continue;
        bool ok = moving ? rubraview_pal_fs_move(entries[i].path, target)
                         : entries[i].is_directory ? rubraview_pal_fs_copy_tree(entries[i].path, target)   /* 1.22.0 */
                                                   : rubraview_pal_fs_copy(entries[i].path, target);
        if (!ok) continue;
        rubraview_undo_push(&app->undo, (rubraview_file_action_t){
            .op = moving ? RUBRAVIEW_FILE_OP_MOVE : RUBRAVIEW_FILE_OP_COPY,
            .source_path = app_keep(app, entries[i].path),
            .target_path = target,
            .batch = batch,
        });
        done++;
    }
    char line[128];
    int written = snprintf(line, sizeof(line), T("%zu file(s) %s %.*s"), done, moving ? T("moved to") : T("copied to"),
                           (int)target_dir.size, target_dir.ptr);
    if (written > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)written });
    if (moving) picker_navigate(app, app->picker_dir);
}

/* Move or copy the picked files into the curation folder a digit names. */
static void picker_curate(app_state_t *app, int32_t digit, bool moving) {
    proven_u8str_view_t target_dir = rubraview_curation_target(&app->curation, digit);
    if (target_dir.size == 0) { osd_say(app, TU8("no folder is bound to that number")); return; }
    picker_curate_to(app, target_dir, moving);
}

bool curation_has_numbers(const app_state_t *app) {
    for (int32_t d = 1; d <= 9; ++d) if (rubraview_curation_target(&app->curation, d).size > 0) return true;
    return false;
}

/* The box's grid must match however many tiles the current menu level
   shows, which changes as the reader drills in and back out. */
static void sync_menubox_tiles(app_state_t *app) {
    int32_t count = rubraview_menu_visible_count(&app->menu);
    if (count > MENU_MAX_TILES) count = MENU_MAX_TILES;
    app->menubox.tile_count = count;
}

static uint32_t boxes_when(const app_state_t *app) {
    /* RV-116: Playback for an animation too; Sound and Subtitles for a film or a song only */
    return app->media ? RUBRAVIEW_BOXES_WHEN_MEDIA | RUBRAVIEW_BOXES_WHEN_PLAYS : anim_plays(app) ? RUBRAVIEW_BOXES_WHEN_PLAYS : 0u;
}

/* RFC-0002 §5: the menu tree, built again from the document — with the
   reading history's newest entries under File › Recent — and the menu
   back at its root. */
static unsigned char g_menu_memory[1 << 16];   /* the tree lives here until it is built again */

void menu_rebuild(app_state_t *app) {
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = g_menu_memory, .size = sizeof(g_menu_memory) });
    enum { RECENT_MAX = 10 };
    rubraview_recent_entry_t recent[RECENT_MAX];
    size_t recent_count = 0;
    /* D-104: the files opened lately, newest first; another viewer's too. */
    recent_sync(app, PROVEN_LIT(""), false);
    size_t newest[RECENT_MAX];
    size_t shown = rubraview_recent_newest(&app->recent, newest, RECENT_MAX);
    for (size_t r = 0; r < shown; ++r) {
        char action[32];
        int n = snprintf(action, sizeof(action), "open_recent:%zu", newest[r]);
        proven_result_mem_mut_t text = proven_arena_alloc(&arena, (size_t)n + 1);
        if (n <= 0 || !proven_is_ok(text.err)) break;
        memcpy(text.value.ptr, action, (size_t)n + 1);
        recent[recent_count++] = (rubraview_recent_entry_t){
            .label = rubraview_path_basename(app->recent.list.entries[newest[r]].path),
            .action = { .ptr = (const proven_byte_t *)(const char*)text.value.ptr, .size = (size_t)n },
        };
    }
    app->menu_when = boxes_when(app);
    app->menu_tree = rubraview_boxes_menu(&arena, rubraview_boxes_document(), app->menu_when,
                                          recent, recent_count, MENU_MAX_TILES - 1);
    app->menu = rubraview_menu_create(&app->menu_tree);
    rubraview_confirm_clear(&app->menu_confirm);
    sync_menubox_tiles(app);
}

/* RFC-0002 §4: which toolbox profile what is on screen calls for — the
   same tests dispatch_key uses to pick a key context. */
static const char *toolbox_profile_name(const app_state_t *app) {
    if (text_active(app)) return "text";
    if (app->media) return app->media_info.has_video ? "video" : "music";
    if (app->anim_active) return app->animation.kind == RUBRAVIEW_FRAMES_ANIMATION ? "animation" : "multipage";
    if (app->source.archive_path.size > 0) return "archive";
    return "image";
}

void strip_title_plan(app_state_t *app);

void toolbox_refresh(app_state_t *app) {
    const rubraview_boxes_doc_t *doc = rubraview_boxes_document();
    int32_t count = 0;
    const rubraview_toolbox_profile_t *parts[2] = {
        app->slideshow_running ? rubraview_boxes_profile(doc, PROVEN_LIT("slideshow")) : NULL,
        rubraview_boxes_profile(doc, cstr(toolbox_profile_name(app))),
    };
    /* D-124: the groups, in order: the slide show's own first, then the profile's. */
    int32_t groups = 0, last_part = -1, last_group = -1;
    memset(app->toolbox.group_sizes, 0, sizeof(app->toolbox.group_sizes));
    for (size_t p = 0; p < 2; ++p) {
        if (!parts[p]) continue;
        for (int32_t i = 0; i < parts[p]->tile_count && count < RUBRAVIEW_TOOLBOX_MAX_TILES; ++i) {
            const rubraview_box_tile_t *tile = &doc->tiles[parts[p]->first_tile + i];
            /* The slide show's own Stop tile stands in for the profile's Slides tile. */
            if (p == 1 && parts[0] && proven_u8str_view_eq(tile->action, PROVEN_LIT("toggle_slideshow"))) continue;
            /* D-123: and its Delay tile for the speed tile, while it runs. */
            if (p == 1 && parts[0] && proven_u8str_view_eq(tile->action, PROVEN_LIT("media_speed_cycle"))) continue;
            if (edit_panel_open(app) && edit_denies(tile->action)) continue;   /* RV-137: not while Edit is open */
            if (((int32_t)p != last_part || tile->group != last_group) && groups < RUBRAVIEW_TOOLBOX_MAX_GROUPS) groups++;
            last_part = (int32_t)p;
            last_group = tile->group;
            app->toolbox.group_sizes[groups - 1]++;
            app->toolbox_tiles[count++] = *tile;
        }
    }
    app->toolbox_tile_count = count;
    app->toolbox.tile_count = count;
    app->toolbox.group_count = groups;
    /* The strip's seek bar, for a film or music; for a still picture with
       others around it, where it is among them (owner, 2026-09-28). */
    app->toolbox.timeline = app->media != NULL || anim_plays(app) || page_count(app) > 1 || app->pagebar_dragging;
    strip_title_plan(app);
    /* The menu follows what is on screen too, but only while it is at its
       root: a reader halfway down a submenu is not pulled back. */
    if (app->menu_when != boxes_when(app) && app->menu.depth == 0) menu_rebuild(app);
}

/* "1x", "1.25x", "0.5x": two decimals, trailing zeros dropped. */
int speed_text(char *buffer, size_t size, double speed);

/* The speed of what plays: a film or music, or an animation (D-82). */
void speed_set(app_state_t *app, double speed) {
    if (speed < 0.25) speed = 0.25;
    if (speed > 4.0) speed = 4.0;
    app->media_speed = speed;
    if (app->media) {
        rubraview_media_clock_set_rate(&app->media_clock, speed, rubraview_pal_time_now_seconds());
        rubraview_pal_audio_set_speed(speed);
    } else {
        app->animation.speed = speed;   /* D-82: the same ladder, and kept for the session as a film's is */
    }
    char line[32];
    char text[16];
    speed_text(text, sizeof(text), speed);
    int n = snprintf(line, sizeof(line), T("speed %s"), text);
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
}

int speed_text(char *buffer, size_t size, double speed) {
    int n = snprintf(buffer, size, "%.2f", speed);
    if (n <= 0 || (size_t)n >= size) return 0;
    while (n > 0 && buffer[n - 1] == '0') buffer[--n] = '\0';
    if (n > 0 && buffer[n - 1] == '.') buffer[--n] = '\0';
    if ((size_t)n + 1 < size) { buffer[n++] = 'x'; buffer[n] = '\0'; }
    return n;
}

/* D-123: the slide show's delay, said as it changes. */
void interval_set(app_state_t *app, double seconds) {
    rubraview_slideshow_set_interval(&app->slideshow, seconds);
    char text[16];
    char line[40];
    if (rubraview_slideshow_interval_text(text, sizeof(text), app->slideshow.interval_seconds) <= 0) return;
    int n = snprintf(line, sizeof(line), T("slide delay %s"), text);
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
}

/* A toggle tile says what a tap will do; a tile that can do nothing now is dimmed. */
/* What a tile or a menu item can tell about its action: the facts of the
   page on screen, read the same way for both boxes. */
rubraview_action_facts_t action_facts(const app_state_t *app) {
    return (rubraview_action_facts_t){
        .has_page = page_count(app) > 0,
        .archive_series = app->source.archive_path.size > 0,
        .media = app->media != NULL,
        .video = app->media != NULL && app->media_info.has_video,
        .frames = app->anim_active,
        .other_audio_track = rubraview_tracks_next(&app->tracks, RUBRAVIEW_TRACK_AUDIO, app->tracks.current_audio) >= 0 &&
                             rubraview_tracks_next(&app->tracks, RUBRAVIEW_TRACK_AUDIO, app->tracks.current_audio) != app->tracks.current_audio,
        .other_subtitle = rubraview_tracks_next(&app->tracks, RUBRAVIEW_TRACK_SUBTITLE, app->tracks.current_subtitle) >= 0,
        .subtitle_shown = app->tracks.current_subtitle >= 0,
        .has_subtitles = rubraview_tracks_count(&app->tracks, RUBRAVIEW_TRACK_SUBTITLE) > 0,
        .slideshow = app->slideshow_running,
        /* D-82: an animation's own end, said in the film's words. */
        .repeat_mode = anim_plays(app) ? (anim_end_mode(app) == 0 ? (int32_t)RUBRAVIEW_REPEAT_ONE
                                        : anim_end_mode(app) == 1 ? (int32_t)RUBRAVIEW_REPEAT_STOP
                                                                  : (int32_t)RUBRAVIEW_REPEAT_NEXT)
                                     : (int32_t)repeat_mode(app),
        .aspect = (int32_t)app->aspect,
        .aspect_on_its_side = app->aspect_side,
        .filmstrip = app->filmstrip.visible,
        .osd = app->osd.always_on,
        .toolbox_pinned = app->toolbox.pinned,
        .titlebar_pinned = rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("titlebar_pinned")) > 0.5,
        .wheel_strip = rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("wheel_strip")) > 0.5,
        .toolbox_detached = app->toolbox.state == RUBRAVIEW_BOX_DETACHED,
        .fullscreen = rubraview_pal_window_is_fullscreen(app->window),
        .nearest = app->force_nearest,
        .pixel_grid = app->pixel_grid,
        .spread_detect = app->spread_detect,
        .fit_lock = app->fit_lock,
        .always_on_top = rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("always_on_top")) > 0.5,
        .muted = rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("mute")) > 0.5,
        .layout = app->layout_opts.mode,
        .fit = app->fit_mode,
        .rtl = app->layout_opts.direction == RUBRAVIEW_READING_RTL,
        .playing = app->media ? !app->media_paused : (app->anim_active && !app->animation.paused),
    };
}


/* D-94: what page_picture_size stretches to. Editing works in the file's
   own pixels, so while the edit panel is open the shape is the file's. */
static void aspect_refresh(app_state_t *app) {
    double ratio = edit_panel_open(app) ? 0.0
        : rubraview_aspect_ratio(app->aspect, rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("aspect_width")),
                                 rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("aspect_height")), app->aspect_side);
    double w = 0.0, h = 0.0;
    rubraview_orientation_apply_size(app->orientation, 1.0, 2.0, &w, &h);
    bool turned = w > h;
    if (ratio == g_view_ratio && turned == g_view_turned) return;
    g_view_ratio = ratio;
    g_view_turned = turned;
    app->strip_base_w = 0.0;   /* the strip's width is a page's */
    app->needs_relayout = true;
}

static void aspect_set(app_state_t *app, rubraview_aspect_t aspect, bool side) {
    app->aspect = aspect;
    app->aspect_side = aspect == RUBRAVIEW_ASPECT_ORIGINAL ? false : side;
    aspect_refresh(app);
    char line[64];
    double ratio = g_view_ratio;
    int n = ratio > 0.0 ? snprintf(line, sizeof(line), T("shape: %s%s (%.3g:1)"), rubraview_aspect_label(aspect), app->aspect_side ? T(", on its side") : "", ratio)
                        : snprintf(line, sizeof(line), T("shape: the file's own"));
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1 });
}

static proven_u8str_view_t toolbox_caption_in(const app_state_t *app, const rubraview_box_tile_t *tile_in, bool *out_enabled);

/* The owner (2026-10-18): "툴바 아이콘 위의 텍스트는 언제나 영어로 ... 아이콘 안의 텍스트만
   영어로 해야 넘치지 않아요" — a tile's own words are English whatever the language, so
   they fit it; its tooltip, the menus and every message stay in the language in use. */
/* The fit tile has no key of its own: the key of the fit it shows (owner, 2026-10-18). */
static const char *fit_action_of(rubraview_fit_mode_t fit) {
    switch (fit) {
        case RUBRAVIEW_FIT_WIDTH: return "fit_width";
        case RUBRAVIEW_FIT_HEIGHT: return "fit_height";
        case RUBRAVIEW_FIT_STRETCH: return "fit_stretch";
        case RUBRAVIEW_FIT_ACTUAL_SIZE: return "actual_size";
        case RUBRAVIEW_FIT_SMART: return "smart_fit";
        case RUBRAVIEW_FIT_WINDOW: default: return "fit_window";
    }
}

proven_u8str_view_t toolbox_caption(const app_state_t *app, const rubraview_box_tile_t *tile_in, bool *out_enabled) {
    const rubraview_lang_t *pack = rubraview_lang_current();
    rubraview_lang_use(NULL);
    proven_u8str_view_t caption = toolbox_caption_in(app, tile_in, out_enabled);
    rubraview_lang_use(pack);
    /* 1.25.0 (owner, 2026-10-20: "아이콘에는 단축키를 표시하지 말고 툴팁이나 ... 에만"): the keys are in the tooltip only. */
    return caption;
}

static proven_u8str_view_t toolbox_caption_in(const app_state_t *app, const rubraview_box_tile_t *tile_in, bool *out_enabled) {
    rubraview_box_tile_t shown = *tile_in;
    shown.caption = rubraview_tr_ctx_u8("toolbox", tile_in->caption);   /* English here: see toolbox_caption */
    const rubraview_box_tile_t *tile = &shown;
    rubraview_action_facts_t facts = action_facts(app);
    rubraview_action_state_t st = rubraview_action_state(tile->action, &facts);
    *out_enabled = st.enabled;
    if (st.value) {
        /* A setting the tile names, "Order: R>L": drawn before the next call. */
        static char named[48];
        return rubraview_tile_caption(tile->caption, false, st.mark, st.value, named, sizeof(named));
    }
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("zoom_reset")) || proven_u8str_view_eq(tile->action, PROVEN_LIT("zoom_cycle"))) {
        /* D-97: the zoom now, where 100% is the size the fit option gives. D-109: the one
           tile that says it goes its round on a tap (`zoom_cycle`). */
        static char percent[16];   /* drawn before the next call */
        /* Real size is not a point on that scale: the owner's name for it is 0%. */
        if (app->fit_mode == RUBRAVIEW_FIT_ACTUAL_SIZE && fabs(app->zoom - 1.0) < 0.0005) return TU8("Real\nsize");   /* 0 %, on two rows (owner, 2026-10-15) */
        proven_u8str_view_t text = rubraview_scale_caption(percent, sizeof(percent), app->zoom);
        return text.size > 0 ? text : tile->caption;
    }
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("fit_cycle"))) return cstr(rubraview_fit_tile_name(app->fit_mode));
    /* 1.25.0 (owner, 2026-10-20): the encoding the text is read in, short, on two rows: EUC / KR, SHFT / JIS */
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("text_encoding")) && app->text) return cstr(rubraview_textenc_short((rubraview_textenc_t)text_enc_now(app)));
    /* Two pages (owner, 2026-10-17): a wide page alone by its shape, or always two. */
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("toggle_spread_detect"))) return app->spread_detect ? TU8("Wide\nalone") : TU8("Always\ntwo");
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("text_wrap"))) return TU8("Word\nwrap");   /* on two rows (owner, 2026-10-17) */
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("media_ab_edit"))) return TU8("Set\nA-B");   /* on two rows (owner, 2026-10-15) */
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("media_play_pause"))) {
        bool paused = app->media ? app->media_paused : app->animation.paused;
        return paused ? TU8("Play") : TU8("Pause");
    }
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("media_mute"))) {
        return rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("mute")) > 0.5 ? TU8("Unmute") : TU8("Mute");
    }
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("media_speed_cycle"))) {
        static char speed[16];   /* drawn before the next call */
        int n = speed_text(speed, sizeof(speed), app->media_speed > 0.0 ? app->media_speed : 1.0);
        return n > 0 ? (proven_u8str_view_t){ .ptr = (const proven_byte_t *)speed, .size = (size_t)n } : tile->caption;
    }
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("interval_cycle"))) {
        static char delay[16];   /* drawn before the next call */
        int n = rubraview_slideshow_interval_text(delay, sizeof(delay), app->slideshow.interval_seconds);
        return n > 0 ? (proven_u8str_view_t){ .ptr = (const proven_byte_t *)delay, .size = (size_t)n } : tile->caption;
    }
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("media_repeat_cycle"))) {
        /* "To Next" on two lines (owner, 2026-10-11): "Next" alone read as the next-file button. */
        if (anim_plays(app)) return anim_end_mode(app) == 0 ? TU8("Loop") : anim_end_mode(app) == 1 ? TU8("Once") : TU8("To\nNext");
        if (repeat_mode(app) == RUBRAVIEW_REPEAT_NEXT) return TU8("To\nNext");
        return cstr(rubraview_repeat_caption(repeat_mode(app)));
    }
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("media_eq_cycle"))) {
        int preset = eq_preset_now(app);
        return preset <= 0 ? TU8("EQ") : eq_preset_label(preset);
    }
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("media_night_toggle"))) {
        return rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("night_mode")) > 0.5 ? TU8("Night on") : TU8("Night");
    }
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("media_visualizer_cycle"))) {
        const char *const V[] = { T("Viz"), T("Bars"), T("Wave") };
        int v = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("visualizer")));
        return cstr(V[v >= 0 && v < 3 ? v : 0]);
    }
    if (proven_u8str_view_eq(tile->action, PROVEN_LIT("media_ab_cycle"))) {
        return app->ab_a < 0.0 ? PROVEN_LIT("A-B") : app->ab_b < 0.0 ? TU8("Set B") : TU8("A-B off");
    }
    return tile->caption;
}

/* The volume as the OSD says it: "volume 70%" or "muted (70%)". */
static void media_say_volume(app_state_t *app) {
    char line[48];
    int volume = (int)rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("volume"));
    bool muted = rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("mute")) > 0.5;
    int n = muted ? snprintf(line, sizeof(line), T("muted (%d%%)"), volume)
                  : snprintf(line, sizeof(line), T("volume %d%%"), volume);
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
}

/* D-16: one step of moving or sizing the window from the keyboard. The
   frame stays on a screen (the PAL pulls it back) and never smaller than
   a usable minimum; a fullscreen window is left alone. */
static bool window_nudge(app_state_t *app, proven_u8str_view_t action) {
    static const struct { const char *name; int dx, dy, dw, dh; } STEPS[] = {
        { "window_move_left", -1, 0, 0, 0 }, { "window_move_right", 1, 0, 0, 0 },
        { "window_move_up", 0, -1, 0, 0 },   { "window_move_down", 0, 1, 0, 0 },
        { "window_narrower", 0, 0, -1, 0 },  { "window_wider", 0, 0, 1, 0 },
        { "window_shorter", 0, 0, 0, -1 },   { "window_taller", 0, 0, 0, 1 },
    };
    for (size_t i = 0; i < sizeof(STEPS) / sizeof(STEPS[0]); ++i) {
        if (!proven_u8str_view_eq(action, proven_u8str_view_from_cstr(STEPS[i].name))) continue;
        if (rubraview_pal_window_is_fullscreen(app->window)) return true;
        int32_t x = 0, y = 0, w = 0, h = 0;
        if (!rubraview_pal_window_get_frame(app->window, &x, &y, &w, &h)) return true;
        int32_t step = (int32_t)(40.0 * rubraview_pal_window_dpi_scale(app->window));
        int32_t min_w = (int32_t)(320.0 * rubraview_pal_window_dpi_scale(app->window));
        int32_t min_h = (int32_t)(240.0 * rubraview_pal_window_dpi_scale(app->window));
        x += STEPS[i].dx * step;
        y += STEPS[i].dy * step;
        w += STEPS[i].dw * step;
        h += STEPS[i].dh * step;
        if (w < min_w) w = min_w;
        if (h < min_h) h = min_h;
        rubraview_pal_window_set_frame(app->window, x, y, w, h);
        return true;
    }
    return false;
}

/* The layout, from the tile, the menu or the key: written to the setting
   (Settings › Viewer › Layout shows the same thing and is saved with the
   rest), which is what applies it. The page on screen stays. */
/* A layout said: its short name first — the one its button and the settings use — then what it is (owner, 2026-10-14). */
const char *layout_said(rubraview_layout_choice_t choice) {
    const char *const SAY[RUBRAVIEW_LAYOUT_CHOICE_COUNT] = {
        T("single: one page at a time"),
        T("dual: two pages side by side, left to right"),
        T("dual-rtl: two pages side by side, right to left"),
        T("book: the cover alone, then two pages, left to right"),
        T("book-rtl: the cover alone, then two pages, right to left"),
        T("webtoon: every page one under the other, at one width"),
        T("comic: one page, a wide scan as its two halves, left first"),
        T("comic-rtl: one page, a wide scan as its two halves, the right one first") };
    return SAY[choice >= 0 && choice < RUBRAVIEW_LAYOUT_CHOICE_COUNT ? choice : 0];
}

void layout_set(app_state_t *app, rubraview_layout_choice_t choice, bool say) {
    int32_t page = current_page_index(app);
    if (page >= 0 && app->spread_index < app->layout.count) {
        int32_t other = app->layout.spreads[app->spread_index].right_index;
        if (other >= 0 && other < page) page = other;
    }
    rubraview_settings_set(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("layout"), (double)(int)choice);
    settings_took_effect(app);
    rebuild_layout(app);
    if (page >= 0) app->spread_index = spread_index_for_page(app, page);
    reset_view(app);
    menu_rebuild(app);
    sync_menubox_tiles(app);
    if (say) {
        osd_say(app, cstr(layout_said(choice)));
    }
}

/* The arrow keys over a picture larger than the window (owner, 2026-10-14): what lies past the
   edge they point to comes in, a seventh of the window a press, never past the picture's edge;
   a picture that fits does not move. The webtoon strip scrolls with Up and Down. */
static void pan_key(app_state_t *app, int dx, int dy) {
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    double step_x = (double)win_w / 7.0, step_y = (double)win_h / 7.0;
    if (dy != 0 && strip_active(app)) {
        strip_scroll_by(app, (double)dy * step_y);
        note_activity(app);
        return;
    }
    rubraview_composition_t comp;
    if (!spread_composition(app, &comp) || comp.count == 0) return;
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;   /* every page of the spread, turned as it is shown */
    for (size_t i = 0; i < comp.count; ++i) {
        const rubraview_draw_command_t *cmd = &comp.commands[i];
        double w = cmd->src_right - cmd->src_left, h = cmd->src_bottom - cmd->src_top;
        const double cx[4] = { 0.0, w, 0.0, w }, cy[4] = { 0.0, 0.0, h, h };
        for (int k = 0; k < 4; ++k) {
            double px = 0.0, py = 0.0;
            rubraview_mat3x2_apply(cmd->transform, cx[k], cy[k], &px, &py);
            if (px < x0) x0 = px;
            if (px > x1) x1 = px;
            if (py < y0) y0 = py;
            if (py > y1) y1 = py;
        }
    }
    double mx = dx ? rubraview_pan_key_delta((double)win_w, x0, x1 - x0, dx, step_x) : 0.0;
    double my = dy ? rubraview_pan_key_delta((double)win_h, y0, y1 - y0, dy, step_y) : 0.0;
    /* Zoomed out (owner, 2026-10-17): the smaller picture moves, inside the window. */
    if (app->zoom < 0.999) {
        if (dx && mx == 0.0) mx = rubraview_pan_key_delta_small((double)win_w, x0, x1 - x0, dx, step_x);
        if (dy && my == 0.0) my = rubraview_pan_key_delta_small((double)win_h, y0, y1 - y0, dy, step_y);
    }
    if (mx == 0.0 && my == 0.0) return;
    app->pan_x += mx;
    app->pan_y += my;
    note_activity(app);
}

/* RV-137 (owner, 2026-10-18: "에디트 상태에서 동작해야 할 버튼과 아닌 버튼을 구분하고, 작동 안하는건
   ... 안보이게"): what does not work while Edit is open — another view of the page, a show,
   another screen, the file itself changed or moved, a film's or a text's own controls. Hidden
   from the toolbox, and said when a key or the menu asks for it. */
static bool edit_denies(proven_u8str_view_t action) {
    static const char *const NO[] = {
        "rotate_cw", "rotate_ccw", "rotate_180", "flip_horizontal", "flip_vertical", "toggle_layout", "toggle_spread_detect",
        "toggle_slideshow", "interval_cycle", "open_picker", "open_folder", "up_to_folder", "open_batch", "print",
        "rename_file", "delete_file", "purge_file", "copy_to_folder", "move_to_folder", "copy_to_other_folder",
        "move_to_other_folder", "convert_archive", "toggle_miniplayer", "toggle_subtitle_window", "toggle_subtitles",
        "next_audio_track", "next_subtitle_track", "toggle_classify",
    };
    for (size_t i = 0; i < sizeof(NO) / sizeof(NO[0]); ++i) if (proven_u8str_view_eq(action, proven_u8str_view_from_cstr(NO[i]))) return true;
    static const char *const PREFIX[] = { "media_", "anim_", "subpage_", "text_", "zoom_strip" };
    for (size_t i = 0; i < sizeof(PREFIX) / sizeof(PREFIX[0]); ++i) {
        size_t n = strlen(PREFIX[i]);
        if (action.size >= n && memcmp(action.ptr, PREFIX[i], n) == 0) return true;
    }
    return false;
}

/* While Edit is open the pages turned are pictures only: a film, a sound, a text is passed over. */
static bool edit_page_is_picture(const app_state_t *app, size_t i) {
    proven_u8str_view_t path = app->source.pages[i].path.size > 0 ? app->source.pages[i].path : app->source.pages[i].name;
    return !is_media_path(path) && !is_text_path(path) && rubraview_glob_match_list(rubraview_path_basename(path), PROVEN_LIT(IMAGE_FILTER));
}

static bool edit_turn(app_state_t *app, int dir, bool to_end) {
    size_t count = page_count(app);
    int32_t at = current_page_index(app);
    if (count == 0) return false;
    long i = to_end ? (dir > 0 ? (long)count - 1 : 0) : (long)at + dir;
    for (; i >= 0 && i < (long)count; i += to_end ? -dir : dir) {
        if (i == at) break;
        if (!edit_page_is_picture(app, (size_t)i)) continue;
        go_to_spread(app, spread_for_chosen_page(app, (int32_t)i));
        update_precache(app);
        return true;
    }
    osd_say(app, TU8("no other picture that way"));
    return true;
}

void handle_action(app_state_t *app, proven_u8str_view_t action) {
    if (text_action(app, action)) return;   /* RFC-0005 */
    if (action.size == 0) return;
    if (edit_panel_open(app) && edit_denies(action)) {
        osd_say(app, TU8("not while Edit is open (E closes it)"));
        return;
    }
    if (edit_panel_open(app)) {   /* pictures only, the rest passed over */
        if (proven_u8str_view_eq(action, PROVEN_LIT("next_page")) && edit_turn(app, +1, false)) return;
        if (proven_u8str_view_eq(action, PROVEN_LIT("prev_page")) && edit_turn(app, -1, false)) return;
        if (proven_u8str_view_eq(action, PROVEN_LIT("first_page")) && edit_turn(app, -1, true)) return;
        if (proven_u8str_view_eq(action, PROVEN_LIT("last_page")) && edit_turn(app, +1, true)) return;
    }
    note_activity(app);
    /* D-88: while the archive on screen is being written again nothing is
       opened, closed or changed; Esc stops the writing, and a quit does too. */
    if (app->arc_job && !proven_u8str_view_eq(action, PROVEN_LIT("quit"))) {
        osd_say(app, TU8("the archive is being written (Esc stops it)"));
        return;
    }
    /* D-89: with the question of what the archive becomes open, doing something else is "no". */
    if (app->arc_ask == ARC_ASK_FORMAT && !proven_u8str_view_eq(action, PROVEN_LIT("quit"))) panel_close(app);

    if (proven_u8str_view_eq(action, PROVEN_LIT("quit"))) {
        /* Esc closes what is open before it closes the program. */
        if (app->arc_job && !atomic_load(&app->arc_job->job.cancel)) {
            atomic_store(&app->arc_job->job.cancel, true);
            osd_say(app, PROVEN_LIT("stopping..."));
            return;
        }
        if (app->settings_open) { settings_close(app); return; }
        if (app->panel.open) { panel_close(app); return; }
        if (app->recentwin.open) { app->recentwin.open = false; return; }   /* the owner's recent window (2026-10-17) */
        if (app->favwin.open) { app->favwin.open = false; return; }         /* 1.24.0 */
        if (app->listwin.open) { app->listwin.open = false; return; }
        if (app->eqwin.open) { app->eqwin.open = false; return; }
        /* RV-116: an open menu box or toolbox (not pinned) folds first; it used to close the program */
        {
            rubraview_box_t *boxes[2] = { &app->menubox, &app->toolbox };
            bool folded = false;
            for (int i = 0; i < 2; ++i) {
                rubraview_box_state_t st = boxes[i]->state;
                if ((st == RUBRAVIEW_BOX_EXPANDED || st == RUBRAVIEW_BOX_LOCKED_OPEN) && !boxes[i]->pinned) {
                    rubraview_box_dismiss(boxes[i]);
                    folded = true;
                }
            }
            if (folded) { rubraview_menu_reset(&app->menu); sync_menubox_tiles(app); return; }
        }
        if (!subs_may_leave(app)) return;
        rubraview_pal_window_request_close(app->window);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("next_page")) && cue_step(app, +1)) {
        /* a `.cue`'s next track, in the same file (owner, 2026-09-30) */
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("prev_page")) && cue_step(app, -1)) {
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("next_page"))) {
        next_spread(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("prev_page"))) {
        prev_spread(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_eq_cycle"))) {
        int preset = (eq_preset_now(app) + 1) % RUBRAVIEW_EQWIN_PRESETS;
        rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("eq_preset"), (double)preset);
        audio_chain_update(app);
        char line[64];
        proven_u8str_view_t name = eq_preset_label(preset);
        int n = snprintf(line, sizeof(line), T("equaliser: %.*s"), (int)name.size, name.ptr);
        if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_eq_window"))) {
        eqwin_toggle(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_night_toggle"))) {
        bool on = rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("night_mode")) < 0.5;
        rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("night_mode"), on ? 1.0 : 0.0);
        audio_chain_update(app);
        osd_say(app, on ? TU8("night mode: loud passages brought down") : TU8("night mode off"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_visualizer_cycle"))) {
        int v = ((int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("visualizer"))) + 1) % 3;
        rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("visualizer"), (double)v);
        const char *const SAY[] = { T("analyser off"), T("analyser: spectrum"), T("analyser: oscilloscope") };
        osd_say(app, cstr(SAY[v]));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("first_page"))) {
        go_to_spread(app, 0);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("last_page"))) {
        go_to_spread(app, app->layout.count > 0 ? app->layout.count - 1 : 0);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("fit_window"))) {
        app->fit_mode = RUBRAVIEW_FIT_WINDOW; reset_view(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("fit_width"))) {
        app->fit_mode = RUBRAVIEW_FIT_WIDTH; reset_view(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("fit_height"))) {
        app->fit_mode = RUBRAVIEW_FIT_HEIGHT; reset_view(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("actual_size"))) {
        if (app->fit_mode != RUBRAVIEW_FIT_ACTUAL_SIZE) app->fit_beside_real = app->fit_mode;
        app->fit_mode = RUBRAVIEW_FIT_ACTUAL_SIZE; reset_view(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("smart_fit"))) {
        app->fit_mode = RUBRAVIEW_FIT_SMART; reset_view(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("fit_stretch"))) {
        app->fit_mode = RUBRAVIEW_FIT_STRETCH; reset_view(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("fit_cycle"))) {
        /* the one fit tile (owner, 2026-10-15): window, width, height, smart, stretch */
        app->fit_mode = rubraview_fit_cycle_next(app->fit_mode); reset_view(app);
        const char *const SAID[] = { [RUBRAVIEW_FIT_WINDOW] = T("fit: the window"), [RUBRAVIEW_FIT_WIDTH] = T("fit: the width"),
                                            [RUBRAVIEW_FIT_HEIGHT] = T("fit: the height"), [RUBRAVIEW_FIT_SMART] = T("fit: smart (never enlarged)"),
                                            [RUBRAVIEW_FIT_STRETCH] = T("fit: stretched to fill") };
        if ((size_t)app->fit_mode < sizeof(SAID) / sizeof(SAID[0]) && SAID[app->fit_mode]) osd_say(app, cstr(SAID[app->fit_mode]));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_fit_lock"))) {
        app->fit_lock = !app->fit_lock;
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("skip_forward"))) {
        go_to_spread(app, app->spread_index + 10);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("skip_backward"))) {
        go_to_spread(app, app->spread_index > 10 ? app->spread_index - 10 : 0);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("up_to_folder"))) {
        /* §3.7.2: ascend to the parent directory, shown in the picker so
           the reader can choose what to open next. */
        proven_u8str_view_t here = app->picker_dir;
        if (here.size == 0) here = app->source_dir;
        proven_u8str_view_t parent = rubraview_picker_parent(here);
        if (parent.size > 0) {
            picker_navigate(app, parent);
            app->picker.focus = 0;
            app->picker_open = app->picker_listing.count > 0;
        }
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("open_folder"))) {
        picker_open(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_spread_detect"))) {
        /* §3.3.4: with detection off, a wide scan pairs like any other
           page instead of standing alone. */
        app->spread_detect = !app->spread_detect;
        app->layout_opts.spread_ar_threshold = app->spread_detect ? 1.15 : 1.0e9;
        app->needs_relayout = true;
        osd_say(app, app->spread_detect ? TU8("two pages: a wide page stands alone, by its shape")
                                        : TU8("two pages: always two, whatever their shape"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("interval_up"))) {
        interval_set(app, app->slideshow.interval_seconds + 0.5);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("interval_down"))) {
        interval_set(app, app->slideshow.interval_seconds - 0.5);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("interval_up_fine"))) {
        interval_set(app, app->slideshow.interval_seconds + 0.1);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("interval_down_fine"))) {
        interval_set(app, app->slideshow.interval_seconds - 0.1);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("interval_cycle"))) {
        interval_set(app, rubraview_slideshow_interval_cycle(app->slideshow.interval_seconds));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("zoom_reset"))) {
        reset_view(app);   /* D-97: 100% is the fitted size; the fit option stays */
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("zoom_cycle"))) {
        zoom_cycle(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("pan_left")) || proven_u8str_view_eq(action, PROVEN_LIT("pan_right")) ||
               proven_u8str_view_eq(action, PROVEN_LIT("pan_up")) || proven_u8str_view_eq(action, PROVEN_LIT("pan_down"))) {
        pan_key(app, proven_u8str_view_eq(action, PROVEN_LIT("pan_left")) ? -1 : proven_u8str_view_eq(action, PROVEN_LIT("pan_right")) ? 1 : 0,
                proven_u8str_view_eq(action, PROVEN_LIT("pan_up")) ? -1 : proven_u8str_view_eq(action, PROVEN_LIT("pan_down")) ? 1 : 0);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("zoom_in"))) {
        zoom_step(app, true);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("zoom_out"))) {
        zoom_step(app, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("rotate_cw")) || proven_u8str_view_eq(action, PROVEN_LIT("rotate_ccw")) ||
               proven_u8str_view_eq(action, PROVEN_LIT("flip_horizontal")) || proven_u8str_view_eq(action, PROVEN_LIT("flip_vertical"))) {
        /* Turning changes which pages pair, so the spreads are made again
           and the page that was on screen is found among them: of two,
           the earlier one, whichever side it was on. */
        int32_t page = current_page_index(app);
        if (page >= 0) {
            int32_t other = app->layout.spreads[app->spread_index].right_index;
            if (other >= 0 && other < page) page = other;
        }
        bool turn = true;
        if (proven_u8str_view_eq(action, PROVEN_LIT("rotate_cw"))) {
            app->orientation = rubraview_orientation_rotate_cw(app->orientation);
        } else if (proven_u8str_view_eq(action, PROVEN_LIT("rotate_ccw"))) {
            app->orientation = rubraview_orientation_rotate_ccw(app->orientation);
        } else if (proven_u8str_view_eq(action, PROVEN_LIT("flip_horizontal"))) {
            app->orientation = rubraview_orientation_flip_h(app->orientation);
            turn = false;
        } else {
            app->orientation = rubraview_orientation_flip_v(app->orientation);
            turn = false;
        }
        rebuild_layout(app);
        if (page >= 0) app->spread_index = spread_index_for_page(app, page);
        if (turn) reset_view(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_pixel_grid"))) {
        app->pixel_grid = !app->pixel_grid;
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_nearest"))) {
        app->force_nearest = !app->force_nearest;
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("center_picture"))) {
        /* 1.25.0 (owner, 2026-10-20): the picture's middle on the window's, its size as it is */
        app->pan_x = 0.0;
        app->pan_y = 0.0;
        note_activity(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("align_widget"))) {
        /* 1.25.0 (owner, 2026-10-20: "플로팅 바를 우측 하단으로, 하단 Information bar가 있을 경우 그 위에"): the Floating
           Widget back in its corner, inside the window, over the information bar when it is shown */
        int32_t ww = 0, wh = 0;
        rubraview_pal_window_get_size(app->window, &ww, &wh);
        double dpi = rubraview_pal_window_dpi_scale(app->window);
        rubraview_tile_metrics_t m = tile_metrics(dpi);
        if (app->toolbox_window) toolbox_dock(app, 0.0, 0.0);
        rubraview_box_snap_home(&app->toolbox, &m, (double)ww, (double)wh);
        if (app->osd.always_on) app->toolbox.anchor_y -= 28.0 * dpi;
        osd_say(app, TU8("the Floating Widget is back in its corner"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_osd"))) {
        app->osd.always_on = !app->osd.always_on;
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_filmstrip"))) {
        app->filmstrip.visible = !app->filmstrip.visible;
        int32_t page = current_page_index(app);
        if (page >= 0) strip_follow(app, (size_t)page);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("layout_single"))) {
        layout_set(app, RUBRAVIEW_LAYOUT_CHOICE_SINGLE, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("layout_dual"))) {
        layout_set(app, RUBRAVIEW_LAYOUT_CHOICE_DUAL, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("layout_dual_rtl"))) {
        layout_set(app, RUBRAVIEW_LAYOUT_CHOICE_DUAL_RTL, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("layout_book"))) {
        layout_set(app, RUBRAVIEW_LAYOUT_CHOICE_BOOK, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("layout_book_rtl"))) {
        layout_set(app, RUBRAVIEW_LAYOUT_CHOICE_BOOK_RTL, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("layout_webtoon"))) {
        layout_set(app, RUBRAVIEW_LAYOUT_CHOICE_WEBTOON, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("layout_comic"))) {
        layout_set(app, RUBRAVIEW_LAYOUT_CHOICE_COMIC, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("layout_comic_rtl"))) {
        layout_set(app, RUBRAVIEW_LAYOUT_CHOICE_COMIC_RTL, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_original"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_ORIGINAL, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_4_3"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_4_3, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_5_4"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_5_4, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_3_2"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_3_2, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_16_10"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_16_10, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_16_9"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_16_9, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_185"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_CINEMA_FLAT, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_21_9"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_21_9, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_239"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_CINEMA_SCOPE, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_1_1"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_1_1, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_paper"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_PAPER, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_letter"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_LETTER, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_custom"))) {
        aspect_set(app, RUBRAVIEW_ASPECT_CUSTOM, app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_cycle"))) {
        aspect_set(app, rubraview_aspect_next(app->aspect), app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("aspect_flip"))) {
        aspect_set(app, app->aspect, !app->aspect_side);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_toolbox"))) {
        /* A toolbox that is its own window is put back, not shown twice. */
        if (app->toolbox.state == RUBRAVIEW_BOX_DETACHED) handle_action(app, PROVEN_LIT("toggle_toolbox_detach"));
        else rubraview_box_click_anchor(&app->toolbox);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("open_favorites"))) {
        favwin_toggle(app);   /* 1.24.0 */
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_favorite"))) {
        proven_u8str_view_t here = current_file_path(app);   /* 1.24.0: the file on screen in or out of the favourites */
        if (here.size == 0) osd_say(app, TU8("open a file first"));
        else { (void)favorite_toggle_path(app, here); if (app->favwin.open) favwin_build(app); }
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("open_recent_list"))) {
        /* The owner (2026-10-17): a list window of their own, with when, how large and where
           (the open-file screen's Recent page is still there, as tiles). */
        recentwin_toggle(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("open_picker"))) {
        app->picker_add_mode = false;   /* 1.22.0 */
        if (app->picker_open) {
            app->picker_open = false;
        } else if (rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("native_dialog")) > 0.5) {
            /* RV-107: Windows' own dialog in place of the picker. */
            const rubraview_file_filter_t FILTERS[] = {
                { T("Pictures, films, music, books"),
                  IMAGE_FILTER ";" MEDIA_FILTER ";" TEXT_FILTER ";" ARCHIVE_FILTER ";*.cue;" LIST_FILTER },
                { T("All files"), "*.*" } };
            rubraview_file_dialog_opts_t opts = {
                .title = T("Open"), .filters = FILTERS, .filter_count = 2, .allow_multi = true,
                .parent_window_handle = rubraview_pal_window_native_handle(app->window),
            };
            rubraview_file_dialog_result_t picked = rubraview_pal_file_dialog_open(app->arena, &opts);
            if (picked.accepted && picked.count == 1) open_path(app, picked.paths[0]);
            else if (picked.accepted && picked.count > 1) open_paths_as_set(app, picked.paths, picked.count, T("files opened as one set"));
        } else {
            picker_open(app);
        }
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("list_open")) || proven_u8str_view_eq(action, PROVEN_LIT("list_copy_path")) ||
               proven_u8str_view_eq(action, PROVEN_LIT("list_reveal")) || proven_u8str_view_eq(action, PROVEN_LIT("list_take_out")) ||
               proven_u8str_view_eq(action, PROVEN_LIT("list_delete_file"))) {
        /* RV-117: a file of a list, right-clicked on its row */
        size_t row = app->list_ctx_row;
        rubraview_window_t *owner = app->ctx_in_files && app->files_window ? app->files_window : app->window;
        if (row >= page_count(app)) {
            osd_say(app, TU8("that file is no longer in the list"));
        } else if (proven_u8str_view_eq(action, PROVEN_LIT("list_open"))) {
            if ((int32_t)row != current_page_index(app)) {
                go_to_spread(app, spread_for_chosen_page(app, (int32_t)row));
                update_precache(app);
            }
        } else if (proven_u8str_view_eq(action, PROVEN_LIT("list_copy_path"))) {
            proven_u8str_view_t whole = rubraview_pal_fs_absolute(app->arena, app->source.pages[row].path);
            osd_say(app, rubraview_pal_clipboard_set_text(owner, whole) ? TU8("its whole path is on the clipboard")
                                                                         : TU8("the path could not be copied"));
        } else if (proven_u8str_view_eq(action, PROVEN_LIT("list_reveal"))) {
            if (!rubraview_pal_shell_reveal(app->source.pages[row].path)) osd_say(app, TU8("Explorer could not show the file"));
        } else if (proven_u8str_view_eq(action, PROVEN_LIT("list_take_out"))) {
            bool *one = (bool*)calloc(page_count(app), sizeof(bool));
            if (one) { one[row] = true; list_remove(app, one); free(one); }
        } else {
            list_delete_row(app, row, owner);
        }
        app->files_dirty = true;
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("reveal_in_explorer"))) {
        /* Owner, 2026-10-09: the file on screen picked out in Explorer; a
           page inside an archive has no file of its own, so the archive. */
        int32_t at = current_page_index(app);
        proven_u8str_view_t path = app->source.archive_path.size > 0 ? app->source.archive_path
                     : (at >= 0 && (size_t)at < page_count(app)) ? app->source.pages[at].path : PROVEN_LIT("");
        if (path.size == 0) osd_say(app, TU8("there is no file to show"));
        else if (!rubraview_pal_shell_reveal(path)) osd_say(app, TU8("Explorer could not show the file"));
    } else if ((proven_u8str_view_eq(action, PROVEN_LIT("delete_file")) || proven_u8str_view_eq(action, PROVEN_LIT("purge_file"))) && on_archive_page(app)) {
        /* D-88: a page of an archive; there is no bin inside one, so either key asks. */
        arc_edit_on_page(app, RUBRAVIEW_ARCEDIT_DELETE, NULL);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("delete_file"))) {
        triage_delete(app, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("purge_file"))) {
        /* §3.18.1: a permanent delete asks first, every time. */
        app->confirm_purge = true;
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("undo"))) {
        if (!arc_undo(app)) triage_undo(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("rename_file"))) {
        rename_begin(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_file_list"))) {
        files_show(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_subtitle_window"))) {
        subs_show(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("extract_subtitles"))) {
        takeout_begin(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_classify"))) {
        classify_toggle(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("copy_to_folder"))) {
        filing_put(app, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("move_to_folder"))) {
        filing_put(app, true);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("copy_to_other_folder"))) {
        target_begin(app, false, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("move_to_other_folder"))) {
        target_begin(app, true, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("open_settings"))) {
        if (app->settings_open) settings_close(app);
        else settings_open(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("open_edit"))) {
        if (app->panel.open && !app->panel_is_export && !app->panel_is_batch && !app->panel_is_print && !app->panel_is_convert) panel_close(app);
        else if (app->media && !(app->media_info.has_video && app->media_page >= 0)) osd_say(app, TU8("Edit is for pictures"));
        else if (app->media && !app->media_paused) osd_say(app, TU8("pause the film first (Space): E then edits the frame on screen"));
        else if (app->media) {
            /* The owner (2026-10-18): a paused film's frame, and only it — saved beside the film as a
               picture, which Edit opens; turning on, Edit passes the film by. */
            app->frame_wanted = FRAME_EDIT;
            media_seek_to(app, app->media_position);
        } else panel_open_edit(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("copy_frame")) || proven_u8str_view_eq(action, PROVEN_LIT("save_frame"))) {
        /* RV-108: of a film or an animation; a still picture has Export. */
        bool film = app->media && app->media_info.has_video && app->media_page >= 0;
        if (!film && !anim_plays(app) && proven_u8str_view_eq(action, PROVEN_LIT("copy_frame"))) {
            picture_copy(app);   /* RV-116: a still picture, whole, as its file holds it */
        } else if (!film && !anim_plays(app)) {
            osd_say(app, TU8("that is for a film or an animation: Ctrl+E exports a picture"));
        } else {
            app->frame_wanted = proven_u8str_view_eq(action, PROVEN_LIT("copy_frame")) ? FRAME_COPY : FRAME_SAVE;
            /* Playing, the next frame is taken; standing still, the frame is asked for again. */
            if (film && app->media_paused) media_seek_to(app, app->media_position);
            else if (!film) show_frame(app, app->animation.current);
        }
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("quick_export")) || proven_u8str_view_eq(action, PROVEN_LIT("save_as"))) {
        if (app->panel.open && app->panel_is_export) panel_close(app);
        else {
            /* From Edit (owner, 2026-10-18): the edits are in what Export writes, and Edit comes back after it. */
            app->export_from_edit = edit_panel_open(app) && app->edit_steps.cursor > 0;
            if (edit_panel_open(app)) { edit_live_close(app); sel_mode_set(app, false); edit_window_close(app); }
            panel_open_export(app);
        }
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("new_window"))) {
        new_window(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("open_batch"))) {
        app->batch_list_count = 0;   /* opened by hand: on the folder being read */
        if (app->panel.open && app->panel_is_batch) panel_close(app);
        else panel_open_batch(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("print"))) {
        /* D-86, D-87: the picture on screen; how is chosen first, then the system's print dialog. */
        proven_u8str_view_t path = current_file_path(app);
        if (app->panel.open && app->panel_is_print) panel_close(app);
        else if (on_archive_page(app)) print_archive_page(app);
        else if (path.size == 0) osd_say(app, TU8("there is no picture to print"));
        else print_paths(app, &path, 1);
    } else if (app->media && (proven_u8str_view_eq(action, PROVEN_LIT("media_play_pause")) || proven_u8str_view_eq(action, PROVEN_LIT("anim_toggle_pause")))) {
        media_toggle_pause(app);   /* anim_toggle_pause: the name before D-16, in keymap.ini files saved earlier */
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("media_stop"))) {
        /* Stop: back to the start and paused, the first frame on screen. */
        if (!app->media_paused) media_toggle_pause(app);
        media_seek_to(app, 0.0);
        osd_say(app, TU8("stopped"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_volume_up")) || proven_u8str_view_eq(action, PROVEN_LIT("media_volume_down"))) {
        double volume = rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("volume"));
        rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("volume"),
                               volume + (proven_u8str_view_eq(action, PROVEN_LIT("media_volume_up")) ? 5.0 : -5.0));
        rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("mute"), 0.0);   /* turning it up or down unmutes */
        settings_took_effect(app);
        media_say_volume(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_mute"))) {
        bool muted = rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("mute")) > 0.5;
        rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("mute"), muted ? 0.0 : 1.0);
        settings_took_effect(app);
        media_say_volume(app);
    } else if (action.size > 12 && memcmp(action.ptr, "open_recent:", 12) == 0) {
        /* File › Recent: the entry's index in the reading history. */
        /* Read inside the view: an action is a slice, not a C string. */
        int64_t index = -1;
        if (rubraview_parse_i64((proven_u8str_view_t){ .ptr = action.ptr + 12, .size = action.size - 12 }, &index) &&
            index >= 0 && (size_t)index < app->recent.list.count) {
            proven_u8str_view_t path = app_keep(app, app->recent.list.entries[index].path);
            if (rubraview_pal_fs_exists(path)) open_path(app, path);
            else osd_say(app, TU8("that file is no longer there"));
        }
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("recent_clear"))) {
        recent_sync(app, PROVEN_LIT(""), true);
        menu_rebuild(app);
        osd_say(app, TU8("the recent list is empty"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_always_on_top"))) {
        bool on = rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("always_on_top")) > 0.5;
        rubraview_settings_set(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("always_on_top"), on ? 0.0 : 1.0);
        settings_took_effect(app);
        osd_say(app, on ? TU8("no longer on top of other windows") : TU8("always on top of other windows"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_wheel_strip"))) {
        bool on = rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("wheel_strip")) > 0.5;
        rubraview_settings_set(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("wheel_strip"), on ? 0.0 : 1.0);
        if (on) app->strip_peek_until = 0.0;   /* one up now goes */
        osd_say(app, on ? TU8("no thumbnails while the wheel turns pages") : TU8("thumbnails while the wheel turns pages"));
    } else if (proven_u8str_view_starts_with(action, PROVEN_LIT("picker_")) && app->picker_open) {   /* 1.22.0: a tile's right click */
        size_t at = app->picker_ctx_index;
        if (proven_u8str_view_eq(action, PROVEN_LIT("picker_open_item"))) { if (at < app->picker_listing.count) picker_activate(app, at); }
        else if (proven_u8str_view_eq(action, PROVEN_LIT("picker_look_inside"))) { if (!picker_enter_book(app, at)) osd_say(app, TU8("that is not a book")); }
        else if (proven_u8str_view_eq(action, PROVEN_LIT("picker_add_to_list"))) picker_add_to_list(app);
        else if (proven_u8str_view_eq(action, PROVEN_LIT("picker_make_playlist"))) picker_make_playlist(app);
        else if (proven_u8str_view_eq(action, PROVEN_LIT("picker_copy_paths"))) picker_copy_paths(app);
        else if (proven_u8str_view_eq(action, PROVEN_LIT("picker_reveal"))) {
            if (at < app->picker_listing.count) (void)rubraview_pal_shell_reveal(app->picker_listing.entries[at].path);
        }
        else if (proven_u8str_view_eq(action, PROVEN_LIT("picker_rename"))) rename_picker_begin(app, at);
        else if (proven_u8str_view_eq(action, PROVEN_LIT("picker_change_ext"))) picker_button(app, PICKER_BTN_EXTENSION);   /* 1.24.0: off the bottom */
        else if (proven_u8str_view_eq(action, PROVEN_LIT("picker_favorite"))) {   /* 1.24.0: a file or a folder in or out of the favourites */
            if (at < app->picker_listing.count) { (void)favorite_toggle_path(app, app->picker_listing.entries[at].path); if (app->favwin.open) favwin_build(app); }
        }
        else if (proven_u8str_view_eq(action, PROVEN_LIT("picker_copy_to"))) picker_button(app, PICKER_BTN_COPY);
        else if (proven_u8str_view_eq(action, PROVEN_LIT("picker_move_to"))) picker_button(app, PICKER_BTN_MOVE);
        else if (proven_u8str_view_eq(action, PROVEN_LIT("picker_recycle"))) {
            /* asked on the list (a second click); here it goes */
            rubraview_confirm_press(&app->picker_confirm, PICKER_BTN_RECYCLE, rubraview_pal_time_now_seconds());
            picker_recycle(app);
        }
        else if (proven_u8str_view_eq(action, PROVEN_LIT("picker_deselect_all"))) {
            for (size_t i = 0; app->picker_selected && i < app->picker_listing.count; ++i) app->picker_selected[i] = false;
            osd_say(app, TU8("nothing picked"));
        }
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_titlebar_pin"))) {
        /* 1.20.0 (owner, 2026-10-18: "툴바에 상단 타이틀바 자체를 고정하는 pin"): the title bar stays */
        bool on = rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("titlebar_pinned")) > 0.5;
        rubraview_settings_set(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("titlebar_pinned"), on ? 0.0 : 1.0);
        osd_say(app, on ? TU8("the title bar comes and goes") : TU8("the title bar stays"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("hide_boxes"))) {
        /* D-110: asked again (a key), they come back as a click brings them. */
        chrome_hide(app, !app->chrome_hidden);
        osd_say(app, app->chrome_hidden ? TU8("hidden: click the picture to bring the title bar and the Floating Widget back")
                                        : TU8("the title bar and the Floating Widget are back"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_toolbox_pin"))) {
        rubraview_box_set_pinned(&app->toolbox, !app->toolbox.pinned);
        osd_say(app, app->toolbox.pinned ? TU8("Toolbar pinned open") : TU8("Toolbar unpinned"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_toolbox_detach"))) {
        if (app->toolbox.state == RUBRAVIEW_BOX_DETACHED) {
            int32_t w = 0, h = 0;
            rubraview_pal_window_get_size(app->window, &w, &h);
            double dpi = rubraview_pal_window_dpi_scale(app->window);
            toolbox_dock(app, (double)w - 220.0 * dpi, (double)h - 160.0 * dpi);
        } else {
            int32_t fx = 0, fy = 0, fw = 0, fh = 0;
            rubraview_pal_window_get_frame(app->window, &fx, &fy, &fw, &fh);
            app->toolbox.state = RUBRAVIEW_BOX_DETACHED;
            toolbox_detach(app, fx + fw - (int32_t)(360.0 * rubraview_pal_window_dpi_scale(app->window)),
                           fy + fh / 2, false);
        }
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("open_keys"))) {
        settings_open(app);
        if (app->settings_open) {
            rubraview_settings_view_set_page(&app->settings_view, RUBRAVIEW_TAB_KEYS);
            rubraview_settings_view_set_table_rows(&app->settings_view, (int32_t)app->keymap.count + 1);
            skip_dirs_table_rows(app);
            app->settings_dirty = true;
        }
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_help"))) {
        help_show(app);
    } else if (anim_plays(app) && proven_u8str_view_eq(action, PROVEN_LIT("media_repeat_cycle"))) {
        /* D-82: an animation's own end — round again, stop there, the next file. */
        int mode = (anim_end_mode(app) + 1) % 3;
        rubraview_settings_set(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("animation_end"), (double)mode);
        osd_say(app, mode == 0 ? TU8("an animation goes round and round")
                   : mode == 1 ? TU8("an animation plays once and stops")
                               : TU8("after an animation, the next file"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_repeat_cycle"))) {
        /* What happens at the end of a film or a song (owner, 2026-09-29). */
        rubraview_repeat_mode_t mode = rubraview_repeat_cycle(repeat_mode(app));
        rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("at_end"), (double)mode);
        app->follow_for = -1;   /* decided again, in the new mode */
        osd_say(app, cstr(rubraview_repeat_sentence(mode)));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_playlist"))) {
        listwin_toggle(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_info"))) {
        info_show(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_miniplayer"))) {
        mini_show(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("boxes_opacity_100")) || proven_u8str_view_eq(action, PROVEN_LIT("boxes_opacity_80")) ||
               proven_u8str_view_eq(action, PROVEN_LIT("boxes_opacity_60")) || proven_u8str_view_eq(action, PROVEN_LIT("boxes_opacity_40"))) {
        double percent = 100.0;
        if (!rubraview_parse_double((proven_u8str_view_t){ .ptr = action.ptr + 14, .size = action.size - 14 }, &percent)) percent = 100.0;
        rubraview_settings_set(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("menubox_opacity"), percent);
        rubraview_settings_set(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("toolbox_opacity"), percent);
        char line[32];
        int n = snprintf(line, sizeof(line), T("Floating Widget %d%%"), (int)percent);
        if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
    } else if (window_nudge(app, action)) {
        /* D-16: Ctrl + arrows size the window, Alt + arrows move it. */
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("anim_step_forward"))) {
        media_step(app, true);
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("anim_step_back"))) {
        media_step(app, false);
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("media_seek_forward"))) {
        media_seek_to(app, app->media_position + MEDIA_SEEK_STEP);
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("media_seek_back"))) {
        media_seek_to(app, app->media_position - MEDIA_SEEK_STEP);
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("media_seek_forward_long"))) {
        media_seek_to(app, app->media_position + MEDIA_SEEK_LONG);
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("media_seek_back_long"))) {
        media_seek_to(app, app->media_position - MEDIA_SEEK_LONG);
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("media_seek_forward_fine"))) {
        media_seek_to(app, app->media_position + MEDIA_SEEK_FINE);   /* RV-107 */
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("media_seek_back_fine"))) {
        media_seek_to(app, app->media_position - MEDIA_SEEK_FINE);
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("next_audio_track"))) {
        /* §3.16.2: the next sound track of the same file, without
           stopping the picture. */
        int32_t next = rubraview_tracks_next(&app->tracks, RUBRAVIEW_TRACK_AUDIO,
                                             app->tracks.current_audio);
        if (next < 0 || next == app->tracks.current_audio) osd_say(app, TU8("this file has only one sound track"));
        else audio_track_set(app, next);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("debug_lose_device"))) {
        /* A test's trigger (RV-062): the next frame acts as if the graphics
           device had been lost. Bound by no default key. */
        rubraview_pal_render_force_lost(app->renderer);
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("toggle_subtitles"))) {
        /* D-33: from the menu or a key; the Sub tile's taps come through
           sub_tile_result, which also tells a double tap from a single. */
        subtitles_toggle(app);
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("choose_subtitle_track"))) {
        int32_t w = 0, h = 0;
        rubraview_pal_window_get_size(app->window, &w, &h);
        sub_list_open_at(app, w * 0.5, h * 0.75);
    } else if (app->media && proven_u8str_view_eq(action, PROVEN_LIT("next_subtitle_track"))) {
        /* §3.16.2: the subtitle files beside the film, and off. */
        if (rubraview_tracks_count(&app->tracks, RUBRAVIEW_TRACK_SUBTITLE) == 0) {
            osd_say(app, TU8("this video has no subtitles, in it or beside it"));
        } else if (subs_may_leave(app)) {
            int32_t next = rubraview_tracks_next(&app->tracks, RUBRAVIEW_TRACK_SUBTITLE,
                                                 app->tracks.current_subtitle);
            subtitle_select(app, next);
            char label[160];
            proven_u8str_view_t text = rubraview_track_label(label, sizeof(label), &app->tracks,
                                                 app->tracks.current_subtitle);
            char line[192];
            int n = snprintf(line, sizeof(line), T("subtitles %.*s"), (int)text.size, text.ptr);
            if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
        }
    } else if (app->media && (proven_u8str_view_eq(action, PROVEN_LIT("subtitle_earlier")) || proven_u8str_view_eq(action, PROVEN_LIT("subtitle_later")) ||
                              proven_u8str_view_eq(action, PROVEN_LIT("subtitle_earlier_fine")) || proven_u8str_view_eq(action, PROVEN_LIT("subtitle_later_fine")) ||
                              proven_u8str_view_eq(action, PROVEN_LIT("subtitle_sync_reset")))) {
        /* §3.16.1: half a second at a time, and the OSD says where the
           track now sits so the reader can aim. */
        /* A DVD's or a Blu-ray's pictures are subtitles too, and they are
           moved by the same offset (found while measuring T081). */
        if (app->subtitle.count == 0 && app->vobsub.count == 0 && app->pgs.count == 0) {
            osd_say(app, TU8("no subtitles are showing"));
        } else {
            /* Half a second (Z / X), a tenth (Shift), or back to the file's own times (Alt+Z) (owner, 2026-10-16). */
            if (proven_u8str_view_eq(action, PROVEN_LIT("subtitle_sync_reset"))) rubraview_subtitle_sync_reset(&app->subtitle);
            else if (proven_u8str_view_eq(action, PROVEN_LIT("subtitle_earlier_fine"))) rubraview_subtitle_shift(&app->subtitle, -0.1);
            else if (proven_u8str_view_eq(action, PROVEN_LIT("subtitle_later_fine"))) rubraview_subtitle_shift(&app->subtitle, 0.1);
            else rubraview_subtitle_nudge(&app->subtitle, proven_u8str_view_eq(action, PROVEN_LIT("subtitle_later")));
            app->subs_dirty = true;   /* the list's Save lights: the offset can be written in */
            if (app->media_page >= 0 && (size_t)app->media_page < page_count(app)) {
                app->subtitle_offset_for = app->source.pages[app->media_page].path;
                app->subtitle_offset_seconds = app->subtitle.offset_seconds;
            }
            char line[96];
            int n = app->subtitle.offset_seconds == 0.0 ? snprintf(line, sizeof(line), T("subtitles in the file's own time"))
                                                        : snprintf(line, sizeof(line), T("subtitles %+.1f s"), app->subtitle.offset_seconds);
            if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
        }
    } else if (anim_plays(app) && proven_u8str_view_eq(action, PROVEN_LIT("media_stop"))) {
        /* D-82: an animation takes a film's keys and tiles from here on. */
        rubraview_animation_stop(&app->animation);
        show_frame(app, 0);
        update_window_title(app);
        osd_say(app, TU8("stopped"));
    } else if (anim_plays(app) && proven_u8str_view_eq(action, PROVEN_LIT("media_seek_forward"))) {
        play_seek(app, play_position(app) + MEDIA_SEEK_STEP);
    } else if (anim_plays(app) && proven_u8str_view_eq(action, PROVEN_LIT("media_seek_back"))) {
        play_seek(app, play_position(app) - MEDIA_SEEK_STEP);
    } else if (anim_plays(app) && proven_u8str_view_eq(action, PROVEN_LIT("media_seek_forward_long"))) {
        play_seek(app, play_position(app) + MEDIA_SEEK_LONG);
    } else if (anim_plays(app) && proven_u8str_view_eq(action, PROVEN_LIT("media_seek_back_long"))) {
        play_seek(app, play_position(app) - MEDIA_SEEK_LONG);
    } else if (anim_plays(app) && proven_u8str_view_eq(action, PROVEN_LIT("media_seek_forward_fine"))) {
        play_seek(app, play_position(app) + MEDIA_SEEK_FINE);
    } else if (anim_plays(app) && proven_u8str_view_eq(action, PROVEN_LIT("media_seek_back_fine"))) {
        play_seek(app, play_position(app) - MEDIA_SEEK_FINE);
    } else if (!app->media && !anim_plays(app) && app->bgm_media && proven_u8str_view_eq(action, PROVEN_LIT("media_play_pause"))) {
        /* §3.14.6: with a picture on screen and music behind it, the
           play key is the music's — and the listener's own pause
           outranks the arbiter from then on. */
        bool pausing = !app->bgm_paused;
        rubraview_pal_media_set_paused(app->bgm_media, pausing);
        app->bgm_paused = pausing;
        (void)rubraview_bgm_event(&app->bgm,
                                  pausing ? RUBRAVIEW_BGM_READER_PAUSED : RUBRAVIEW_BGM_READER_RESUMED,
                                  bgm_pause_for_sound(app));
        osd_say(app, pausing ? TU8("the music is paused") : TU8("the music plays on"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("anim_toggle_pause")) || proven_u8str_view_eq(action, PROVEN_LIT("media_play_pause"))) {
        bool ended = app->animation.ended;
        if (app->animation.paused) rubraview_animation_resume(&app->animation);
        else rubraview_animation_pause(&app->animation);
        if (ended) show_frame(app, app->animation.current);   /* played again: from the first frame */
        update_window_title(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("subpage_next")) && app->anim_active &&
               app->animation.current + 1 >= app->animation.frame_count) {
        next_spread(app);   /* past a multi-page file's last page: the next file (owner, 2026-10-14) */
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("subpage_prev")) && app->anim_active && app->animation.current == 0) {
        prev_spread(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("anim_step_forward")) || proven_u8str_view_eq(action, PROVEN_LIT("subpage_next"))) {
        rubraview_animation_step(&app->animation, true);
        show_frame(app, app->animation.current);
        update_window_title(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("anim_step_back")) || proven_u8str_view_eq(action, PROVEN_LIT("subpage_prev"))) {
        rubraview_animation_step(&app->animation, false);
        show_frame(app, app->animation.current);
        update_window_title(app);
    } else if ((app->media || anim_plays(app)) &&
               (proven_u8str_view_eq(action, PROVEN_LIT("anim_speed_up")) || proven_u8str_view_eq(action, PROVEN_LIT("anim_speed_down")) ||
                proven_u8str_view_eq(action, PROVEN_LIT("media_speed_up")) || proven_u8str_view_eq(action, PROVEN_LIT("media_speed_down")) ||
                proven_u8str_view_eq(action, PROVEN_LIT("media_speed_reset")) || proven_u8str_view_eq(action, PROVEN_LIT("media_speed_cycle")))) {
        /* D-15: 0.25x to 4x a quarter at a time; the tile cycles the usual ones. */
        double speed = app->media_speed > 0.0 ? app->media_speed : 1.0;
        if (proven_u8str_view_eq(action, PROVEN_LIT("media_speed_reset"))) {
            speed = 1.0;
        } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_speed_cycle"))) {
            static const double CYCLE[] = { 1.0, 1.25, 1.5, 2.0, 0.5, 0.75 };
            size_t next = 0;
            for (size_t i = 0; i < 6; ++i) if (fabs(CYCLE[i] - speed) < 1e-6) next = (i + 1) % 6;
            /* D-110: a speed the drag or the wheel left between the steps goes to 1x first. */
            speed = CYCLE[next];
        } else {
            bool up = proven_u8str_view_eq(action, PROVEN_LIT("anim_speed_up")) || proven_u8str_view_eq(action, PROVEN_LIT("media_speed_up"));
            speed += up ? 0.25 : -0.25;
        }
        speed_set(app, speed);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_ab_edit"))) {
        ab_edit_begin(app);
    } else if ((app->media || anim_plays(app)) &&
               (proven_u8str_view_eq(action, PROVEN_LIT("media_ab_a")) || proven_u8str_view_eq(action, PROVEN_LIT("media_ab_b")) ||
                proven_u8str_view_eq(action, PROVEN_LIT("media_ab_clear")) || proven_u8str_view_eq(action, PROVEN_LIT("media_ab_cycle")))) {
        /* D-15 A-B repeat. The tile walks A, then B, then off. */
        char line[64];
        int n = 0;
        bool set_a = proven_u8str_view_eq(action, PROVEN_LIT("media_ab_a")) || (proven_u8str_view_eq(action, PROVEN_LIT("media_ab_cycle")) && app->ab_a < 0.0);
        bool set_b = proven_u8str_view_eq(action, PROVEN_LIT("media_ab_b")) || (proven_u8str_view_eq(action, PROVEN_LIT("media_ab_cycle")) && app->ab_a >= 0.0 && app->ab_b < 0.0);
        if (set_a || set_b) {
            /* The point just taken wins; the other goes to the end or the start (owner, 2026-10-15). */
            rubraview_ab_take(&app->ab_a, &app->ab_b, play_position(app), set_a, play_duration(app));
            n = app->ab_b < 0.0 ? snprintf(line, sizeof(line), T("repeat from %.1f s"), app->ab_a)
                                : snprintf(line, sizeof(line), T("repeating %.1f - %.1f s"), app->ab_a, app->ab_b);
        } else {
            app->ab_a = app->ab_b = -1.0;
            n = snprintf(line, sizeof(line), T("repeat off"));
        }
        if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("anim_speed_up"))) {
        app->animation.speed = rubraview_animation_step_speed(app->animation.speed, true);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("anim_speed_down"))) {
        app->animation.speed = rubraview_animation_step_speed(app->animation.speed, false);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("archive_names_auto"))) {
        archive_names_reread(app, RUBRAVIEW_CODEPAGE_AUTO);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("archive_names_utf8"))) {
        archive_names_reread(app, RUBRAVIEW_CODEPAGE_UTF8);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("archive_names_korean"))) {
        archive_names_reread(app, RUBRAVIEW_CODEPAGE_KOREAN);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("archive_names_japanese"))) {
        archive_names_reread(app, RUBRAVIEW_CODEPAGE_JAPANESE);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("archive_names_chinese_simplified"))) {
        archive_names_reread(app, RUBRAVIEW_CODEPAGE_SIMPLIFIED_CHINESE);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("archive_names_chinese_traditional"))) {
        archive_names_reread(app, RUBRAVIEW_CODEPAGE_TRADITIONAL_CHINESE);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("archive_names_western"))) {
        archive_names_reread(app, RUBRAVIEW_CODEPAGE_WESTERN);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("archive_names_cycle"))) {
        /* the code pages in their menu order, round again after Western */
        int now = app->names_codepage >= 0 ? app->names_codepage : (int)app->archive_codepage;
        archive_names_reread(app, (rubraview_codepage_t)((now + 1) % ((int)RUBRAVIEW_CODEPAGE_WESTERN + 1)));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("next_archive"))) {
        if (!open_sibling_archive(app, true)) osd_say(app, TU8("no next archive beside this one"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("prev_archive"))) {
        if (!open_sibling_archive(app, false)) osd_say(app, TU8("no previous archive beside this one"));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("resume_accept"))) {
        /* §3.17.1: the prompt is answered by opening the remembered page. */
        if (app->resume_offer) {
            app->resume_offer = false;
            size_t target = spread_index_for_page(app, app->resume_page);
            go_to_spread(app, target);
            update_precache(app);
        }
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_slideshow"))) {
        toggle_slideshow(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_fullscreen"))) {
        rubraview_pal_window_set_fullscreen(app->window,
                                            !rubraview_pal_window_is_fullscreen(app->window));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_layout"))) {
        /* One page, two pages, a book (the cover alone), the webtoon strip,
           comic (wide scans in halves) — two pages, the book and comic each
           followed by itself read right to left — and round again (owner,
           2026-10-09, 2026-10-11). */
        int now = (int)rubraview_layout_choice_of(app->layout_opts.mode, app->layout_opts.direction);
        layout_set(app, (rubraview_layout_choice_t)((now + 1) % (int)RUBRAVIEW_LAYOUT_CHOICE_COUNT), true);
    }
}

/* While the picker is open it owns the keyboard (§3.7.1's modal
   context): letters jump, arrows move, Enter opens, Esc closes. */
static bool picker_handle_key(app_state_t *app, rubraview_key_combo_t combo) {
    if (!app->picker_open) return false;
    picker_layout(app);
    size_t columns = (size_t)app->picker.columns;

    if ((combo.modifiers & RUBRAVIEW_MOD_CTRL) && key_is(combo, "L")) {
        picker_path_begin(app);
        return true;
    }
    if ((combo.modifiers & RUBRAVIEW_MOD_CTRL) && key_is(combo, "D")) {
        picker_toggle_favorite(app);   /* D-43, as a browser bookmarks a page */
        return true;
    }
    if ((combo.modifiers & RUBRAVIEW_MOD_CTRL) && key_is(combo, "A")) { picker_select_everything(app); return true; }   /* 1.22.0 */
    if ((combo.modifiers & RUBRAVIEW_MOD_CTRL) && key_is(combo, "F")) { picker_filter_begin(app); return true; }        /* 1.22.0 */
    if ((combo.modifiers & RUBRAVIEW_MOD_CTRL) && key_is(combo, "Z")) { triage_undo(app); return true; }   /* 1.23.0: what was done here, back */
    if (combo.modifiers & RUBRAVIEW_MOD_ALT) {   /* 1.24.0: back, forward and up through the folders, as Explorer */
        if (key_is(combo, "Left"))  { picker_history_go(app, false); return true; }
        if (key_is(combo, "Right")) { picker_history_go(app, true); return true; }
        if (key_is(combo, "Up"))    { if (!app->picker_in_book) picker_go_up(app); return true; }
    }

    if (combo.key_name.size == 1) {
        char c = combo.key_name.ptr[0];
        if (app->picker_has_pending && c == '0') {
            app->picker_has_pending = false;
            target_begin(app, app->picker_pending == (int32_t)PICKER_BTN_MOVE, true);
            return true;
        }
        if (app->picker_has_pending && c >= '1' && c <= '9') {
            /* A number with no folder behind it leaves the picker waiting
               rather than letting the next one fall through to §3.18.3's
               own meaning, which would move the page on screen. */
            if (rubraview_curation_target(&app->curation, c - '0').size == 0) {
                osd_say(app, TU8("no folder is bound to that number - Settings > Files, or 0 for another folder"));
                return true;
            }
            app->picker_has_pending = false;
            picker_curate(app, c - '0', app->picker_pending == (int32_t)PICKER_BTN_MOVE);
            return true;
        }
        if (app->picker_has_pending && (c == 'A' || c == 'Z')) {
            app->picker_has_pending = false;   /* any letter gives up waiting */
        }
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            rubraview_picker_type_ahead(&app->picker, c);
            return true;
        }
    }

    size_t count = app->picker_listing.count;
    if (count == 0) { app->picker_open = false; return true; }

    if (proven_u8str_view_eq(combo.key_name, PROVEN_LIT("Right"))) {
        if (app->picker.focus + 1 < count) app->picker.focus++;
        rubraview_picker_reveal_focus(&app->picker);
        return true;
    }
    if (proven_u8str_view_eq(combo.key_name, PROVEN_LIT("Left"))) {
        if (app->picker.focus > 0) app->picker.focus--;
        rubraview_picker_reveal_focus(&app->picker);
        return true;
    }
    if (proven_u8str_view_eq(combo.key_name, PROVEN_LIT("Down"))) {
        app->picker.focus = (app->picker.focus + columns < count)
            ? app->picker.focus + columns : count - 1;
        rubraview_picker_reveal_focus(&app->picker);
        return true;
    }
    if (proven_u8str_view_eq(combo.key_name, PROVEN_LIT("Up"))) {
        app->picker.focus = (app->picker.focus >= columns) ? app->picker.focus - columns : 0;
        rubraview_picker_reveal_focus(&app->picker);
        return true;
    }
    if (proven_u8str_view_eq(combo.key_name, PROVEN_LIT("Enter"))) {
        if ((combo.modifiers & RUBRAVIEW_MOD_SHIFT) && picker_enter_book(app, app->picker.focus)) return true;
        picker_activate(app, app->picker.focus);
        return true;
    }
    if (proven_u8str_view_eq(combo.key_name, PROVEN_LIT("Escape"))) {
        app->picker_open = false;
        return true;
    }
    if (proven_u8str_view_eq(combo.key_name, PROVEN_LIT("Backspace"))) {
        proven_u8str_view_t parent = rubraview_picker_parent(app->picker_dir);
        if (parent.size > 0) {
            picker_navigate(app, parent);
            app->picker.focus = 0;
        }
        return true;
    }
    return false;
}

bool key_is(rubraview_key_combo_t combo, const char *name) {
    size_t n = strlen(name);
    return combo.key_name.size == n && memcmp(combo.key_name.ptr, name, n) == 0;
}

/* Typed, composed or pasted text into the rename box: control bytes and
   anything that is not UTF-8 are left out (rubraview_rename_append's rule),
   then it goes in at the caret in place of the selection. */
void rename_insert(app_state_t *app, proven_u8str_view_t text) {
    if (text.size == 0) return;
    char clean[sizeof(app->rename_buffer)];
    size_t length = 0;
    (void)rubraview_rename_append(clean, sizeof(clean), &length, text);
    (void)rubraview_textedit_insert(&app->rename_edit, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)clean, .size = length });
}

/* Text for a box: whole UTF-8 characters, no control characters. */
void textbox_insert(rubraview_textedit_t *te, proven_u8str_view_t text) {
    if (text.size == 0) return;
    char clean[1024];
    size_t length = 0;
    (void)rubraview_rename_append(clean, sizeof(clean), &length, text);
    (void)rubraview_textedit_insert(te, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)clean, .size = length });
}

/* The editing keys of a text box (the rename box, the picker's path, the
   settings window's folders). The characters themselves come as text;
   these move, select, delete and use the clipboard of `window`. */
bool textbox_key(rubraview_window_t *window, rubraview_textedit_t *te, rubraview_key_combo_t combo) {
    bool shift = (combo.modifiers & RUBRAVIEW_MOD_SHIFT) != 0;
    bool ctrl = (combo.modifiers & RUBRAVIEW_MOD_CTRL) != 0;
    if (key_is(combo, "Backspace")) {
        /* A whole character, not one byte: half a Hangul syllable would
           leave the name unwritable. (Inside a syllable the IME takes
           Backspace itself and it never comes here.) */
        rubraview_textedit_backspace(te);
    } else if (key_is(combo, "Delete")) {
        rubraview_textedit_delete(te);
    } else if (key_is(combo, "Left")) {
        rubraview_textedit_move(te, RUBRAVIEW_TEXTEDIT_LEFT, shift);
    } else if (key_is(combo, "Right")) {
        rubraview_textedit_move(te, RUBRAVIEW_TEXTEDIT_RIGHT, shift);
    } else if (key_is(combo, "Home")) {
        rubraview_textedit_move(te, RUBRAVIEW_TEXTEDIT_HOME, shift);
    } else if (key_is(combo, "End")) {
        rubraview_textedit_move(te, RUBRAVIEW_TEXTEDIT_END, shift);
    } else if (ctrl && key_is(combo, "A")) {
        rubraview_textedit_select_all(te);
    } else if (ctrl && (key_is(combo, "C") || key_is(combo, "X"))) {
        proven_u8str_view_t chosen = rubraview_textedit_selection(te);
        if (chosen.size > 0 && rubraview_pal_clipboard_set_text(window, chosen) && key_is(combo, "X")) {
            rubraview_textedit_backspace(te);   /* removes the selection */
        }
    } else if (ctrl && key_is(combo, "V")) {
        char pasted[1024];
        size_t n = rubraview_pal_clipboard_get_text(window, pasted, sizeof(pasted));
        textbox_insert(te, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)pasted, .size = n });
    } else {
        return false;
    }
    return true;
}

static bool rename_edit_key(app_state_t *app, rubraview_key_combo_t combo) {
    return textbox_key(app->window, &app->rename_edit, combo);
}

/* Everything §3.18 puts in front of the reader answers the next key
   press before anything else does: a confirmation that is ignored is
   worse than no confirmation at all. */
/* RFC-0006: the program started again where it is — the file on screen (a book's page
   comes back from the reading history) — after what quitting asks; then this one closes. */
void restart_now(app_state_t *app) {
    app->confirm_restart = false;
    if (!subs_may_leave(app)) return;   /* subtitle changes not saved: asked, as quitting asks */
    settings_write_if_changed(app, false);
    proven_u8str_view_t here = current_file_path(app);
    if (app->source.archive_path.size > 0) here = app->source.archive_path;
    proven_u8str_view_t exe = rubraview_pal_process_executable(app->arena);
    char line[4096];
    size_t pos = 0;
    line[0] = '\0';
    if (exe.size > 0) {
        append_arg(line, sizeof(line), &pos, exe);
        append_arg_cstr(line, sizeof(line), &pos, "--new-instance");
        /* A book (an archive) comes back on its page; a folder's file is named itself. */
        int32_t page = current_page_index(app);
        if (here.size > 0 && app->source.kind != RUBRAVIEW_PAGE_SOURCE_FOLDER && page > 0) {
            char word[32];
            snprintf(word, sizeof(word), "--page=%d", (int)page + 1);
            append_arg_cstr(line, sizeof(line), &pos, word);
        }
        if (here.size > 0) append_arg(line, sizeof(line), &pos, here);
    }
    if (exe.size == 0 || !rubraview_pal_process_start((proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = pos })) {
        osd_say(app, TU8("Rubraview could not be started again"));
        return;
    }
    rubraview_pal_window_request_close(app->window);
}

/* Twice a second: past the session's limit (or a test's), a restart is offered, once. */
static void memory_watch(app_state_t *app, double now) {
    if (!app->mem || app->restart_offered || now - app->memory_checked_at < 0.5) return;
    app->memory_checked_at = now;
    double test_mb = rubraview_settings_get(&app->settings, PROVEN_LIT("debug"), PROVEN_LIT("memory_restart_mb"));
    if (test_mb > 0.0) app->mem->heap_restart_at = (size_t)test_mb << 20;   /* a test's limit, on all the program holds */
    if (rubraview_mem_pressure(app->mem) == RUBRAVIEW_MEM_OK) return;
    app->restart_offered = true;
    app->confirm_restart = true;
}

/* The box given up: Esc, or its Cancel. */
static void rename_cancel(app_state_t *app) {
    if (app->rename_is_password) osd_say(app, TU8("not opened: it needs its password"));
    bool filter = app->rename_is_filter, listfilter = app->rename_is_listfilter;
    rename_end(app);
    if (filter) picker_filter_set(app, PROVEN_LIT(""));   /* 1.22.0: Esc lets every name show again */
    if (listfilter) list_filter_set(app, PROVEN_LIT(""));
}

/* 1.24.0 (owner, 2026-10-19: "단축키만 있는게 아니라 버튼으로도 각 기능이나 입력, 취소가 버튼으로"): the box's buttons and,
   for Change extensions, its rows. True when the press was theirs. */
enum { RENAME_BTN_OK = 1, RENAME_BTN_CANCEL, RENAME_BTN_BROWSE, RENAME_BTN_BACK };
bool rename_box_press(app_state_t *app, double x, double y) {
    if (!app->rename_active) return false;
    for (int32_t i = 0; i < app->rename_btn_n; ++i) {
        if (!pal_rect_has(app->rename_btn[i], x, y)) continue;
        switch (app->rename_btn_kind[i]) {
            case RENAME_BTN_OK:     rename_commit(app); break;
            case RENAME_BTN_CANCEL: rename_cancel(app); break;
            case RENAME_BTN_BROWSE: target_browse(app); break;
            case RENAME_BTN_BACK:   text_find_step(app, false); break;
            case RENAME_BTN_OK + 100: rename_extension_apply(app); break;   /* Change extensions: all rows now */
            default: break;
        }
        return true;
    }
    if (app->rename_is_extension) {
        for (int32_t k = 0; k < app->ext_n; ++k) if (pal_rect_has(app->ext_row_rect[k], x, y)) { ext_switch(app, k); return true; }
    }
    return false;
}

static bool triage_handle_key(app_state_t *app, rubraview_key_combo_t combo) {
    /* An open text box takes every key first: a resume offer still on
       screen took its Enter, and the typed path went nowhere (VM,
       2026-09-28). */
    if (app->rename_active) {
        if (app->rename_is_find && (key_is(combo, "F3") || ((combo.modifiers & RUBRAVIEW_MOD_SHIFT) && key_is(combo, "Enter")))) {
            text_find_step(app, !(combo.modifiers & RUBRAVIEW_MOD_SHIFT));   /* RFC-0005: back with Shift */
            return true;
        }
        if (key_is(combo, "Enter")) { rename_commit(app); return true; }
        if (key_is(combo, "Escape")) { rename_cancel(app); return true; }
        if (app->rename_is_extension && key_is(combo, "Tab")) {   /* 1.24.0: the next row, or with Shift the one before */
            int32_t to = app->ext_at + ((combo.modifiers & RUBRAVIEW_MOD_SHIFT) ? -1 : 1);
            ext_switch(app, (to % app->ext_n + app->ext_n) % app->ext_n);
            return true;
        }
        /* A password is never copied out of its box. */
        if (app->rename_is_password && (combo.modifiers & RUBRAVIEW_MOD_CTRL) && (key_is(combo, "C") || key_is(combo, "X"))) return true;
        if (app->rename_is_target && (combo.modifiers & RUBRAVIEW_MOD_CTRL) && key_is(combo, "O")) { target_browse(app); return true; }
        if (rename_edit_key(app, combo)) return true;
        /* The characters themselves arrive as text (rename_text), in
           either case and from the IME; the keys that make them are
           swallowed here, as is everything else while renaming. */
        return true;
    }

    if (arc_ask_key(app, combo)) return true;

    /* §3.17.1's resume prompt answers Enter before paging does. Enter
       turns a page now (owner, 2026-09-09), and a prompt that the very
       key meant to answer it walks straight past is not a prompt. */
    if (app->resume_offer && key_is(combo, "Enter")) {
        app->resume_offer = false;
        size_t target = spread_index_for_page(app, app->resume_page);
        go_to_spread(app, target);
        update_precache(app);
        return true;
    }

    if (app->confirm_restart) {   /* RFC-0006 */
        if (key_is(combo, "Y") || key_is(combo, "Enter")) restart_now(app);
        else {
            app->confirm_restart = false;
            osd_say(app, TU8("later, then: Settings > Cache restarts it when you like"));
        }
        return true;
    }
    if (app->confirm_purge) {
        if (key_is(combo, "Y") || key_is(combo, "Enter")) {
            app->confirm_purge = false;
            triage_delete(app, true);
        } else {
            /* Anything else is "no". A confirmation should be hard to
               agree to by accident and easy to refuse. */
            app->confirm_purge = false;
        }
        return true;
    }

    /* §3.18.3: 1-9 curate — but only where a folder is actually bound.
       §3.7.2 gives 1-5 to the fit modes, and an unconfigured viewer must
       keep them; a reader who has set up triage folders has said which
       meaning they want. */
    if (combo.modifiers == RUBRAVIEW_MOD_NONE && combo.key_name.size == 1 && !app->picker_open) {
        /* Not while the picker is open: there a number answers its own
           "Move to" or "Copy to", and curating the page behind it would
           move a file the reader is not looking at (2026-09-23). */
        char c = combo.key_name.ptr[0];
        if (c >= '1' && c <= '9') {
            int32_t digit = c - '0';
            if (app->classify_panel.open && rubraview_curation_target(&app->curation, digit).size > 0) {
                classify_mark(app, digit);   /* RV-133: set aside, not moved yet */
                return true;
            }
            if (rubraview_curation_target(&app->curation, digit).size > 0) {
                triage_curate(app, digit);
                return true;
            }

        }
    }

    return false;
}

/* ---- RFC-0002 Q6: the detached toolbox ---- */

/* The grip beside the pin (owner, 2026-09-29): the three-dot corner of a
   window's sizing grip, and while it is dragged, what the size is now. */
void draw_box_grip(app_state_t *app, const rubraview_box_t *box, const rubraview_tile_metrics_t *metrics,
                          uint32_t fill, uint32_t border) {
    rubraview_rect_t gr = rubraview_box_grip_rect(box, metrics);
    if (gr.width <= 0.0) return;
    rubraview_pal_rect_t grip = { gr.x, gr.y, gr.width, gr.height };
    bool hot = box->grip_active || rubraview_rect_contains(gr, app->pointer_x, app->pointer_y);
    rubraview_pal_render_fill_rect(app->renderer, grip, hot ? COLOR_TILE_FILL : fill, 2.0);
    rubraview_pal_render_stroke_rect(app->renderer, grip, border, 1.0, 2.0);
    rubraview_pal_render_draw_text(app->renderer, PROVEN_LIT("R"), grip, gr.height * 0.42, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);   /* 1.25.0: R, resize */
    if (!box->grip_active) return;
    rubraview_tile_metrics_t sized = rubraview_box_metrics(box, metrics);
    char line[64];
    int n = snprintf(line, sizeof(line), T("%d a row, %d%%"),
                     box->kind == RUBRAVIEW_BOX_TOOLBOX ? sized.strip_columns : sized.columns,
                     (int)lround((box->scale > 0.0 ? box->scale : 1.0) * 100.0));
    if (n <= 0) return;
    double w = gr.width * 3.2;
    /* On the side away from the box, so it covers none of it. */
    rubraview_rect_t body = rubraview_box_bounds(box, metrics);
    bool body_above = body.y + body.height * 0.5 < gr.y;
    rubraview_pal_rect_t tip = { gr.x + gr.width * 0.5 - w * 0.5, body_above ? gr.y + gr.height * 1.1 : gr.y - gr.height * 0.6,
                                 w, gr.height * 0.5 };
    if (tip.x < 0.0) tip.x = 0.0;
    rubraview_pal_render_fill_rect(app->renderer, tip, COLOR_BOX_FILL, 3.0);
    rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ (const proven_byte_t *)line, (size_t)n }, tip, gr.height * 0.28, COLOR_TEXT,
                                   RUBRAVIEW_TEXT_CENTER);
}

/* Its size: the anchor bar on top, the profile's grid under it. */
void draw_strip_head(app_state_t *app, rubraview_renderer_t *r, const rubraview_toolbox_layout_t *l,
                            double ox, double oy, int32_t hovered, double opacity);
void draw_strip_button(app_state_t *app, rubraview_renderer_t *r, rubraview_pal_rect_t tile, int32_t i,
                              const rubraview_action_facts_t *facts, bool hovered, uint32_t fill, uint32_t border);

/* Detached, the strip sits under the window's own anchor bar. */
static rubraview_toolbox_layout_t toolbox_window_layout(const app_state_t *app, const rubraview_tile_metrics_t *m) {
    rubraview_tile_metrics_t sized = rubraview_box_metrics(&app->toolbox, m);   /* the size its grip set */
    rubraview_toolbox_shape_t shape = rubraview_box_toolbox_shape(&app->toolbox);
    shape.count = app->toolbox_tile_count;
    shape.timeline = app->media != NULL;
    return rubraview_toolbox_layout_shaped(&sized, &shape);
}

static void toolbox_window_size(const app_state_t *app, const rubraview_tile_metrics_t *m, int32_t *out_w, int32_t *out_h) {
    rubraview_toolbox_layout_t l = toolbox_window_layout(app, m);
    *out_w = (int32_t)ceil(l.width > m->anchor_size * 2.0 ? l.width : m->anchor_size * 2.0);
    *out_h = (int32_t)ceil(m->anchor_size + l.height);
}

static rubraview_rect_t toolbox_window_tile(const app_state_t *app, const rubraview_tile_metrics_t *m, int32_t i) {
    rubraview_toolbox_layout_t l = toolbox_window_layout(app, m);
    rubraview_tile_metrics_t sized = rubraview_box_metrics(&app->toolbox, m);
    rubraview_rect_t b = rubraview_toolbox_button_rect(&l, &sized, i);
    b.y += m->anchor_size;
    return b;
}

static void toolbox_detach(app_state_t *app, int32_t screen_x, int32_t screen_y, bool follow_pointer) {
    if (app->toolbox_window) return;
    toolbox_refresh(app);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    rubraview_tile_metrics_t m = tile_metrics(dpi);
    int32_t w = 0, h = 0;
    toolbox_window_size(app, &m, &w, &h);
    rubraview_window_config_t config = {
        .title = T("Rubraview Toolbar"), .width = (int32_t)(w / dpi), .height = (int32_t)(h / dpi),
        .owner = app->window, .tool_window = true,
    };
    app->toolbox_window = rubraview_pal_window_create(window_block_take(app, &app->toolbox_block), &config);
    if (!app->toolbox_window) {
        rubraview_box_dock(&app->toolbox);
        osd_say(app, TU8("the Toolbar could not open a window of its own"));
        return;
    }
    rubraview_pal_window_set_frame(app->toolbox_window, screen_x, screen_y, w, h);
    int32_t cw = 0, ch = 0;
    rubraview_pal_window_get_size(app->toolbox_window, &cw, &ch);
    app->toolbox_renderer = rubraview_pal_render_create(window_block_arena(app, &app->toolbox_block), rubraview_pal_window_native_handle(app->toolbox_window), cw, ch);
    ui_font_apply_all(app);
    app->toolbox_window_opacity = -1.0;
    app->toolbox_window_drawn = 0.0;
    app->toolbox_window_pointer_x = app->toolbox_window_pointer_y = -1.0;
    app->toolbox.state = RUBRAVIEW_BOX_DETACHED;
    if (follow_pointer) rubraview_pal_window_begin_drag(app->toolbox_window);
}

/* Back into the viewer's window, its anchor at a client point, open. */
static void toolbox_dock(app_state_t *app, double client_x, double client_y) {
    if (app->toolbox_renderer) rubraview_pal_render_destroy(app->toolbox_renderer);
    if (app->toolbox_window) rubraview_pal_window_destroy(app->toolbox_window);
    window_block_give(app, &app->toolbox_block);
    app->toolbox_renderer = NULL;
    app->toolbox_window = NULL;
    rubraview_box_dock(&app->toolbox);
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double max_x = (double)w - 88.0 * dpi, max_y = (double)h - 48.0 * dpi;
    app->toolbox.anchor_x = client_x < 0.0 ? 0.0 : client_x > max_x ? max_x : client_x;
    app->toolbox.anchor_y = client_y < 0.0 ? 0.0 : client_y > max_y ? max_y : client_y;
    note_activity(app);
}

/* ---- the seek buttons' choices (owner, 2026-10-09) ---- */

static bool seek_tile_action(proven_u8str_view_t action, bool *out_forward) {
    if (proven_u8str_view_eq(action, PROVEN_LIT("media_seek_forward"))) { *out_forward = true; return true; }
    if (proven_u8str_view_eq(action, PROVEN_LIT("media_seek_back"))) { *out_forward = false; return true; }
    return false;
}

/* The window the pressed button is in: the choices stay inside it. */
static void seekfan_bounds(const app_state_t *app, double *out_w, double *out_h) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->seekfan_detached && app->toolbox_window ? app->toolbox_window : app->window, &w, &h);
    *out_w = (double)w;
    *out_h = (double)h;
}

static void seekfan_pointer(app_state_t *app, double x, double y) {
    if (!app->seekfan.open) return;
    double bw = 0.0, bh = 0.0;
    seekfan_bounds(app, &bw, &bh);
    rubraview_seekfan_pointer(&app->seekfan, x, y, 4.0 * rubraview_pal_window_dpi_scale(app->window), bw, bh);
}

static void seekfan_let_go(app_state_t *app) {
    if (!app->seekfan.down) return;
    double seconds = rubraview_seekfan_release(&app->seekfan);
    app->toolbox_window_drawn = 0.0;
    if (seconds != 0.0 && play_duration(app) > 0.0) play_seek(app, play_position(app) + seconds);
}

void draw_seekfan(app_state_t *app, rubraview_renderer_t *r, bool detached) {
    if (!app->seekfan.open || app->seekfan_detached != detached) return;
    double bw = 0.0, bh = 0.0;
    seekfan_bounds(app, &bw, &bh);
    double gap = 4.0 * rubraview_pal_window_dpi_scale(app->window);
    for (int32_t i = 0; i < RUBRAVIEW_SEEKFAN_COUNT; ++i) {
        rubraview_rect_t b = rubraview_seekfan_button(&app->seekfan, i, gap, bw, bh);
        rubraview_pal_rect_t rect = { b.x, b.y, b.width, b.height };
        bool on = i == app->seekfan.hover;
        rubraview_pal_render_fill_rect(r, rect, on ? COLOR_TILE_CURRENT : 0xF0202020u, 3.0);
        rubraview_pal_render_stroke_rect(r, rect, on ? COLOR_TEXT : COLOR_BOX_BORDER, 1.0, 3.0);
        char caption[8];
        int n = snprintf(caption, sizeof(caption), "%s%s", app->seekfan.forward ? "+" : "-", rubraview_seekfan_label(i));
        if (n > 0) rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)caption, .size = (size_t)n }, rect,
                                                  rect.height * 0.34, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
}

static void draw_toolbox_window(app_state_t *app) {
    if (!app->toolbox_window || !app->toolbox_renderer) return;
    double now = rubraview_pal_time_now_seconds();
    if (now - app->toolbox_window_drawn < 0.1) return;
    app->toolbox_window_drawn = now;
    toolbox_refresh(app);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    rubraview_tile_metrics_t m = tile_metrics(dpi);
    int32_t want_w = 0, want_h = 0, cw = 0, ch = 0;
    toolbox_window_size(app, &m, &want_w, &want_h);
    rubraview_pal_window_get_size(app->toolbox_window, &cw, &ch);
    if (cw != want_w || ch != want_h) {
        /* Another profile, another grid: the window follows it. */
        int32_t fx = 0, fy = 0, fw = 0, fh = 0;
        rubraview_pal_window_get_frame(app->toolbox_window, &fx, &fy, &fw, &fh);
        rubraview_pal_window_set_frame(app->toolbox_window, fx, fy, want_w, want_h);
    }
    double opacity = rubraview_settings_get(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("toolbox_opacity"));
    if (opacity != app->toolbox_window_opacity) {
        rubraview_pal_window_set_opacity(app->toolbox_window, opacity);
        app->toolbox_window_opacity = opacity;
    }
    rubraview_renderer_t *r = app->toolbox_renderer;
    rubraview_pal_render_begin(r, 0xFF1A1A1Au);
    rubraview_pal_rect_t left = { 0.0, 0.0, m.anchor_size, m.anchor_size };
    rubraview_pal_rect_t right = { m.anchor_size, 0.0, m.anchor_size, m.anchor_size };
    rubraview_pal_render_stroke_rect(r, left, COLOR_BOX_BORDER, 1.0, 2.0);
    rubraview_pal_render_fill_rect(r, right, COLOR_TILE_FILL, 2.0);
    rubraview_pal_render_draw_text(r, PROVEN_LIT("T"), left, m.anchor_size * 0.42, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    rubraview_pal_render_draw_text(r, TU8("Dock"), right, m.anchor_size * 0.3, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    rubraview_toolbox_layout_t l = toolbox_window_layout(app, &m);
    rubraview_action_facts_t facts = action_facts(app);
    int32_t hovered = -1;
    for (int32_t i = 0; i < app->toolbox_tile_count; ++i) {
        if (rubraview_rect_contains(toolbox_window_tile(app, &m, i), app->toolbox_window_pointer_x, app->toolbox_window_pointer_y)) hovered = i;
    }
    draw_strip_head(app, r, &l, 0.0, m.anchor_size, hovered, 100.0);
    for (int32_t i = 0; i < app->toolbox_tile_count; ++i) {
        rubraview_rect_t t = toolbox_window_tile(app, &m, i);
        draw_strip_button(app, r, (rubraview_pal_rect_t){ t.x, t.y, t.width, t.height }, i, &facts, i == hovered,
                          COLOR_TILE_FILL, COLOR_BOX_BORDER);
    }
    draw_seekfan(app, r, true);
    if (!rubraview_pal_render_end(r)) app->toolbox_window_drawn = 0.0;
}

/* Where the toolbox window is over the viewer's client area, when its middle is. */
static bool toolbox_window_over_viewer(const app_state_t *app, double *out_x, double *out_y) {
    int32_t tx = 0, ty = 0, tw = 0, th = 0, vx = 0, vy = 0, vw = 0, vh = 0;
    if (!rubraview_pal_window_get_frame(app->toolbox_window, &tx, &ty, &tw, &th) ||
        !rubraview_pal_window_get_frame(app->window, &vx, &vy, &vw, &vh)) return false;
    double mx = tx + tw * 0.5, my = ty + th * 0.5;
    if (mx < vx || my < vy || mx >= vx + vw || my >= vy + vh) return false;
    *out_x = (double)(tx - vx);
    *out_y = (double)(ty - vy);
    return true;
}

static void toolbox_window_pump(app_state_t *app) {
    rubraview_window_event_t event;
    while (app->toolbox_window && rubraview_pal_window_poll_event(app->toolbox_window, &event)) {
        double dpi = rubraview_pal_window_dpi_scale(app->window);
        rubraview_tile_metrics_t m = tile_metrics(dpi);
        switch (event.kind) {
            case RUBRAVIEW_WINDOW_EVENT_CLOSE: {
                int32_t w = 0, h = 0;
                rubraview_pal_window_get_size(app->window, &w, &h);
                toolbox_dock(app, (double)w - 220.0 * dpi, (double)h - 160.0 * dpi);
                return;
            }
            case RUBRAVIEW_WINDOW_EVENT_RESIZE:
                rubraview_pal_render_resize(app->toolbox_renderer, event.resize.width, event.resize.height);
                app->toolbox_window_drawn = 0.0;
                break;
            case RUBRAVIEW_WINDOW_EVENT_PAINT:
                app->toolbox_window_drawn = 0.0;
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOVED:
                /* §3.6.1 would dock it when dropped over the canvas. A viewer
                   filling the screen is under it wherever it goes, so that
                   would dock it at every move: it docks by its Dock half,
                   Ctrl+T or Show › Detach toolbox instead (D-15). */
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE:
                app->toolbox_window_pointer_x = event.mouse.x;
                app->toolbox_window_pointer_y = event.mouse.y;
                app->toolbox_window_drawn = 0.0;
                if (app->seekfan_detached) seekfan_pointer(app, event.mouse.x, event.mouse.y);
                speed_tile_move(app, event.mouse.y, true);
                if (app->pagebar_dragging && app->pagebar_detached) pagebar_drag(app, event.mouse.x, false);
                break;
            case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN:
                dispatch_key(app, event.key.combo);   /* the viewer's keys work from here too */
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN: {
                if (event.mouse.button != RUBRAVIEW_MOUSE_LEFT) break;
                if (event.mouse.y < m.anchor_size) {
                    if (event.mouse.x < m.anchor_size) {
                        rubraview_pal_window_begin_drag(app->toolbox_window);
                    } else if (event.mouse.x < m.anchor_size * 2.0) {
                        int32_t w = 0, h = 0;
                        rubraview_pal_window_get_size(app->window, &w, &h);
                        double cx = (double)w - 220.0 * dpi, cy = (double)h - 160.0 * dpi;
                        (void)toolbox_window_over_viewer(app, &cx, &cy);
                        toolbox_dock(app, cx, cy);
                        return;
                    }
                    break;
                }
                rubraview_toolbox_layout_t l = toolbox_window_layout(app, &m);
                if (play_duration(app) > 0.0 && l.timeline.width > 0.0 &&
                    event.mouse.x >= l.timeline.x && event.mouse.x < l.timeline.x + l.timeline.width &&
                    event.mouse.y >= m.anchor_size + l.timeline.y - l.timeline.height &&
                    event.mouse.y < m.anchor_size + l.timeline.y + l.timeline.height * 2.0) {
                    double seconds = rubraview_seekbar_time(l.timeline.x, l.timeline.width, event.mouse.x, play_duration(app));
                    play_seek(app, seconds);
                    app->toolbox_window_drawn = 0.0;
                    break;
                }
                if (pagebar_shown(app) && l.timeline.width > 0.0 &&
                    event.mouse.x >= l.timeline.x && event.mouse.x < l.timeline.x + l.timeline.width &&
                    event.mouse.y >= m.anchor_size + l.timeline.y - l.timeline.height &&
                    event.mouse.y < m.anchor_size + l.timeline.y + l.timeline.height * 2.0) {
                    pagebar_press(app, l.timeline.x, l.timeline.width, event.mouse.x, true);
                    app->toolbox_window_drawn = 0.0;
                    break;
                }
                for (int32_t i = 0; i < app->toolbox_tile_count; ++i) {
                    if (!rubraview_rect_contains(toolbox_window_tile(app, &m, i), event.mouse.x, event.mouse.y)) continue;
                    bool enabled = true;
                    (void)toolbox_caption(app, &app->toolbox_tiles[i], &enabled);
                    if (enabled && proven_u8str_view_eq(app->toolbox_tiles[i].action, PROVEN_LIT("toggle_subtitles"))) {
                        /* D-33: decided on release, a double tap or a hold */
                        sub_tile_result(app, rubraview_tap_press(&app->sub_tap, rubraview_pal_time_now_seconds()));
                    } else if (enabled && seek_tile_action(app->toolbox_tiles[i].action, &(bool){ false })) {
                        /* A tap seeks on release; held, it offers longer steps. */
                        bool forward = false;
                        (void)seek_tile_action(app->toolbox_tiles[i].action, &forward);
                        app->seekfan_detached = true;
                        rubraview_seekfan_press(&app->seekfan, forward, rubraview_pal_time_now_seconds(),
                                                toolbox_window_tile(app, &m, i));
                    } else if (enabled && (proven_u8str_view_eq(app->toolbox_tiles[i].action, PROVEN_LIT("media_speed_cycle")) ||
                                           proven_u8str_view_eq(app->toolbox_tiles[i].action, PROVEN_LIT("interval_cycle")))) {
                        speed_tile_press(app, event.mouse.y, true, proven_u8str_view_eq(app->toolbox_tiles[i].action, PROVEN_LIT("interval_cycle")));
                    } else if (enabled && chooser_open_tile(app, app->toolbox_tiles[i].action, chooser_main_x(app), chooser_main_y(app),
                                                            chooser_main_y(app))) {
                        /* 1.24.0: a one-of-many tile lists its choices, over the picture */
                    } else if (enabled) {
                        handle_action(app, app->toolbox_tiles[i].action);
                    }
                    app->toolbox_window_drawn = 0.0;
                    break;
                }
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_UP:
                if (app->pagebar_dragging && app->pagebar_detached) {
                    pagebar_drag(app, event.mouse.x, true);
                    app->toolbox_window_drawn = 0.0;
                    break;
                }
                sub_tile_result(app, rubraview_tap_release(&app->sub_tap, rubraview_pal_time_now_seconds()));
                seekfan_let_go(app);
                speed_tile_release(app, true);
                break;
            default:
                break;
        }
    }
}

/* A sound track of the film on screen, by its index in the track list. */
void audio_track_set(app_state_t *app, int32_t next) {
    if (!app->media || next < 0 || (size_t)next >= app->tracks.count || next == app->tracks.current_audio) return;
    if (!rubraview_pal_media_select_audio_track(app->media, app->tracks.tracks[next].stream_index)) {
        /* No device on this machine, or the track cannot be decoded:
           the one that was playing keeps playing. */
        osd_say(app, TU8("the sound track cannot be changed here"));
        return;
    }
    app->tracks.current_audio = next;
    /* Both streams start again from where the film is, so what
       the old track had already decoded is thrown away. */
    media_seek_to(app, app->media_position);
    char label[160];
    proven_u8str_view_t text = rubraview_track_label(label, sizeof(label), &app->tracks, next);
    char line[192];
    int n = snprintf(line, sizeof(line), T("sound %.*s"), (int)text.size, text.ptr);
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
}

void dispatch_key(app_state_t *app, rubraview_key_combo_t combo) {
    if (chooser_key(app, CHOOSE_IN_MAIN, combo)) return;   /* 1.24.0: the list's keys while it is open */
    if (tmenu_key(app, combo)) return;                     /* 1.25.0: the title bar's ☰ */
    if (ab_edit_handle_key(app, combo)) return;
    if (px_edit_handle_key(app, combo)) return;
    if (popups_key(app, combo)) return;   /* RV-116 */
    /* The owner's edit: Enter applies the step, Ctrl+Z and Ctrl+Y go through the steps. */
    if (edit_panel_open(app) && !app->rename_active && !app->px_edit_open) {
        bool ctrl = (combo.modifiers & RUBRAVIEW_MOD_CTRL) != 0;
        if (ctrl && key_is(combo, "Z")) { edit_undo(app, (combo.modifiers & RUBRAVIEW_MOD_SHIFT) != 0); return; }
        if (ctrl && key_is(combo, "Y")) { edit_undo(app, true); return; }
        if (!ctrl && key_is(combo, "Enter")) { (void)edit_apply_pending(app, true); return; }
    }
    /* D-33: Escape closes the list of subtitle tracks, and only that. */
    if (app->sub_list_open && proven_u8str_view_eq(combo.key_name, PROVEN_LIT("Escape")) && combo.modifiers == 0) {
        app->sub_list_open = false;
        return;
    }
    if (triage_handle_key(app, combo)) return;
    if (picker_handle_key(app, combo)) return;
    /* §3.7.1: the slide show's own bindings win while it is running,
       then the viewing context, then the global section. */
    if (app->slideshow_running) {
        proven_u8str_view_t action = rubraview_keymap_find_action(&app->keymap, PROVEN_LIT("slideshow"), combo);
        if (action.size > 0) { handle_action(app, action); return; }
    }

    /* §3.20: while an animated image or a multi-page file is the page
       being read, its own context is consulted first. This is what lets
       `Space` and `Ctrl + [` mean two things without either meaning
       being lost — see the comment on those sections in
       src/core/default_keymap.c. */
    /* RFC-0005: a text page's own keys first. */
    if (text_active(app)) {
        proven_u8str_view_t action = rubraview_keymap_find_action(&app->keymap, PROVEN_LIT("text"), combo);
        if (action.size > 0) { handle_action(app, action); return; }
    }

    if (app->anim_active || app->media) {
        proven_u8str_view_t context = (app->media || app->animation.kind == RUBRAVIEW_FRAMES_ANIMATION)
                            ? PROVEN_LIT("media") : PROVEN_LIT("subpage");
        proven_u8str_view_t action = rubraview_keymap_find_action(&app->keymap, context, combo);
        if (action.size > 0) { handle_action(app, action); return; }
    }

    proven_u8str_view_t action = rubraview_keymap_find_action(&app->keymap, PROVEN_LIT("navigation"), combo);
    if (action.size == 0) action = rubraview_keymap_find_action(&app->keymap, PROVEN_LIT("view"), combo);
    if (action.size == 0) action = rubraview_keymap_find_action(&app->keymap, PROVEN_LIT("slideshow"), combo);
    handle_action(app, action);
}

/* ---- pointer ---- */

static rubraview_pointer_context_t pointer_context(const app_state_t *app) {
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    return (rubraview_pointer_context_t){
        .window_width = (double)win_w,
        .reading_direction = app->layout_opts.direction,
        .comic_mode = (app->layout_opts.mode != RUBRAVIEW_PAGE_LAYOUT_SINGLE) || page_count(app) > 1,
        .zoomed_in = app->zoom > 1.001 || strip_active(app),   /* a drag moves the strip */
        .scrollable_vertically = (app->fit_mode == RUBRAVIEW_FIT_WIDTH),
        .wheel_zooms = !app->media && !strip_active(app) && !app->text,
    };
}

void apply_intent(app_state_t *app, rubraview_pointer_intent_t intent) {
    switch (intent) {
        case RUBRAVIEW_INTENT_NEXT_PAGE:      next_spread(app); break;
        case RUBRAVIEW_INTENT_PREV_PAGE:      prev_spread(app); break;
        case RUBRAVIEW_INTENT_SKIP_FORWARD:   go_to_spread(app, app->spread_index + 10); break;
        case RUBRAVIEW_INTENT_SKIP_BACKWARD:
            go_to_spread(app, app->spread_index > 10 ? app->spread_index - 10 : 0);
            break;
        case RUBRAVIEW_INTENT_ZOOM_IN:        zoom_step(app, true); break;
        case RUBRAVIEW_INTENT_ZOOM_OUT:       zoom_step(app, false); break;
        case RUBRAVIEW_INTENT_SCROLL_VERTICAL: app->pan_y -= PAN_STEP; note_activity(app); break;
        case RUBRAVIEW_INTENT_TOGGLE_FULLSCREEN:
            rubraview_pal_window_set_fullscreen(app->window, !rubraview_pal_window_is_fullscreen(app->window));
            break;
        case RUBRAVIEW_INTENT_TOGGLE_ACTUAL_SIZE:
            if (app->fit_mode != RUBRAVIEW_FIT_ACTUAL_SIZE) app->fit_beside_real = app->fit_mode;
            app->fit_mode = (app->fit_mode == RUBRAVIEW_FIT_ACTUAL_SIZE)
                ? RUBRAVIEW_FIT_WINDOW : RUBRAVIEW_FIT_ACTUAL_SIZE;
            reset_view(app);
            break;
        case RUBRAVIEW_INTENT_TOGGLE_OVERLAY:
            break;   /* 1.25.0: a click opens no toolbar */
        case RUBRAVIEW_INTENT_CENTER_PICTURE:
            handle_action(app, PROVEN_LIT("center_picture"));
            break;
        default: break;
    }
}

/* The edge buttons show while the pointer is near a side's middle, on a
   page, with nothing else of the viewer's in the way (owner, 2026-09-29). */
static bool edge_nav_allowed(const app_state_t *app) {
    return app->pointer_inside && !app->cursor.hidden && page_count(app) > 0 && !app->picker_open &&
           !app->settings_open && !app->panel.open && !app->titlebar.shown;
}

static bool over_a_box(app_state_t *app, double x, double y) {
    rubraview_tile_metrics_t m = tile_metrics(rubraview_pal_window_dpi_scale(app->window));
    rubraview_box_t *boxes[] = { &app->menubox, &app->toolbox };
    for (size_t i = 0; i < 2; ++i) {
        if (boxes[i]->state == RUBRAVIEW_BOX_DETACHED) continue;
        if (rubraview_rect_contains(rubraview_box_bounds(boxes[i], &m), x, y)) return true;
    }
    return false;
}

static rubraview_edge_side_t edge_nav_side(app_state_t *app, double x, double y) {
    if (!edge_nav_allowed(app) || over_a_box(app, x, y)) return RUBRAVIEW_EDGE_NONE;
    if (app->listwin.open && rubraview_rect_contains(rubraview_listwin_rect(&app->listwin), x, y)) return RUBRAVIEW_EDGE_NONE;
    if (app->eqwin.open && rubraview_rect_contains(rubraview_eqwin_rect(&app->eqwin), x, y)) return RUBRAVIEW_EDGE_NONE;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    rubraview_edgenav_t nav = rubraview_edgenav_create(rubraview_pal_window_dpi_scale(app->window));
    return rubraview_edgenav_revealed(&nav, x, y, (double)w, (double)h);
}

static rubraview_edge_hit_t edge_nav_hit(app_state_t *app, double x, double y) {
    rubraview_edge_hit_t none = { RUBRAVIEW_EDGE_NONE, -1 };
    if (edge_nav_side(app, x, y) == RUBRAVIEW_EDGE_NONE) return none;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    rubraview_edgenav_t nav = rubraview_edgenav_create(rubraview_pal_window_dpi_scale(app->window));
    return rubraview_edgenav_hit(&nav, x, y, (double)w, (double)h);
}

/* D-110: the title bar and the floating boxes go away — the toolbox in a
   window of its own too — until the picture is clicked. */
static int32_t tmenu_win_w(const app_state_t *app) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    return w;
}

static void chrome_hide(app_state_t *app, bool hide) {
    if (app->chrome_hidden == hide) return;
    app->chrome_hidden = hide;
    if (hide) {
        app->titlebar.shown = false;
        app->titlebar_held = RUBRAVIEW_TITLEBAR_NONE;
    }
    if (app->toolbox_window) rubraview_pal_window_set_visible(app->toolbox_window, !hide);
    app->toolbox_window_drawn = 0.0;
}


/* §3.21.3: the titlebar's controls take precedence while it is shown,
   over the viewer and over the picker alike. True when it took the click. */
static bool titlebar_click(app_state_t *app, double x, double y) {
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    rubraview_tile_metrics_t metrics = tile_metrics(rubraview_pal_window_dpi_scale(app->window));

    rubraview_titlebar_button_t button = rubraview_titlebar_hit(&app->titlebar, x, y, (double)win_w);
    switch (button) {
        case RUBRAVIEW_TITLEBAR_CLOSE:      if (subs_may_leave(app)) rubraview_pal_window_request_close(app->window); return true;
        case RUBRAVIEW_TITLEBAR_MINIMIZE:   rubraview_pal_window_minimize(app->window); return true;
        case RUBRAVIEW_TITLEBAR_MAXIMIZE:   rubraview_pal_window_toggle_maximize(app->window); return true;
        case RUBRAVIEW_TITLEBAR_FULLSCREEN:
            rubraview_pal_window_set_fullscreen(app->window, !rubraview_pal_window_is_fullscreen(app->window));
            return true;
        case RUBRAVIEW_TITLEBAR_PIN:        handle_action(app, PROVEN_LIT("toggle_always_on_top")); return true;
        case RUBRAVIEW_TITLEBAR_HIDE:       handle_action(app, PROVEN_LIT("hide_boxes")); return true;
        case RUBRAVIEW_TITLEBAR_SETTINGS:   handle_action(app, PROVEN_LIT("open_settings")); return true;   /* 1.24.0 */
        case RUBRAVIEW_TITLEBAR_BARPIN:     handle_action(app, PROVEN_LIT("toggle_titlebar_pin")); return true;   /* 1.25.0 */
        case RUBRAVIEW_TITLEBAR_MENU:       app->tmenu_open = !app->tmenu_open; app->tmenu_focus = -1; return true;   /* 1.25.0 */
        case RUBRAVIEW_TITLEBAR_NAME:       /* 1.25.0: the name opens the open-file screen (inside a book being read) */
            if (!app->picker_open) handle_action(app, PROVEN_LIT("open_picker"));
            return true;
        case RUBRAVIEW_TITLEBAR_OPEN:
        case RUBRAVIEW_TITLEBAR_SNAP_BOXES:
            (void)metrics;
            return true;
        case RUBRAVIEW_TITLEBAR_RESIZE:
        case RUBRAVIEW_TITLEBAR_CAPTION:
            /* A double press on the caption maximises or restores, as a system
               title bar does (owner, 2026-09-29). Moving or sizing waits for
               the pointer to move with the button held: the system's loop
               started on the press itself would swallow the second click. */
            if (button == RUBRAVIEW_TITLEBAR_CAPTION &&
                rubraview_titlebar_caption_press(&app->titlebar, rubraview_pal_time_now_seconds(), x, y, 0.5,
                                                 4.0 * rubraview_pal_window_dpi_scale(app->window))) {
                rubraview_pal_window_toggle_maximize(app->window);
                app->titlebar_held = RUBRAVIEW_TITLEBAR_NONE;
            } else {
                app->titlebar_held = button;
                app->titlebar_held_x = x;
                app->titlebar_held_y = y;
            }
            return true;
        default: break;
    }
    return false;
}

/* Returns true when the chrome consumed the click. */
static bool handle_chrome_click(app_state_t *app, double x, double y) {
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    rubraview_tile_metrics_t metrics = tile_metrics(rubraview_pal_window_dpi_scale(app->window));

    if (titlebar_click(app, x, y)) return true;

    /* The title bar's menu folds on a click anywhere else, and that click does nothing more. */
    if (app->menubox.headless && app->menubox.state != RUBRAVIEW_BOX_COLLAPSED &&
        !rubraview_rect_contains(rubraview_box_bounds(&app->menubox, &metrics), x, y) &&
        !(app->titlebar.shown && y < app->titlebar.height)) {
        rubraview_box_dismiss(&app->menubox);
        return true;
    }

    /* The anchor's two buttons answer first: they sit on top of the
       box, and a click there is never a tile. */
    if (rubraview_box_click(&app->menubox, &metrics, x, y)) {
        menu_rebuild(app);   /* opened or closed: Recent as it is now, from the root */
        return true;
    }
    bool toolbox_here = app->toolbox.state != RUBRAVIEW_BOX_DETACHED;   /* detached, it answers in its own window */
    if (toolbox_here && rubraview_box_anchor_half_at(&app->toolbox, &metrics, x, y) == RUBRAVIEW_ANCHOR_CLICK) {
        rubraview_box_click_open(&app->toolbox);   /* 1.25.0 */
        return true;
    }
    if (toolbox_here && rubraview_box_click(&app->toolbox, &metrics, x, y)) return true;

    /* The pins at the boxes' top-left (owner, 2026-09-22). */
    if (rubraview_box_pin_click(&app->menubox, &metrics, x, y)) return true;
    if (toolbox_here && rubraview_box_pin_click(&app->toolbox, &metrics, x, y)) return true;
    /* The strip's seek bar: a click puts the film there. */
    if (toolbox_here && play_duration(app) > 0.0) {
        rubraview_rect_t bar = rubraview_box_timeline_rect(&app->toolbox, &metrics);
        if (bar.width > 0.0 && x >= bar.x && x < bar.x + bar.width &&
            y >= bar.y - bar.height && y < bar.y + bar.height * 2.0) {   /* a little taller than drawn: easier to hit */
            double seconds = rubraview_seekbar_time(bar.x, bar.width, x, play_duration(app));
            play_seek(app, seconds);
            note_activity(app);
            return true;
        }
    }
    /* The same bar over a still picture: a click goes to that page. */
    if (toolbox_here && pagebar_shown(app)) {
        rubraview_rect_t bar = rubraview_box_timeline_rect(&app->toolbox, &metrics);
        if (bar.width > 0.0 && x >= bar.x && x < bar.x + bar.width &&
            y >= bar.y - bar.height && y < bar.y + bar.height * 2.0) {
            pagebar_press(app, bar.x, bar.width, x, false);
            note_activity(app);
            return true;
        }
    }

    int32_t tile = rubraview_box_tile_at(&app->menubox, &metrics, x, y);
    if (tile >= 0) {
        /* A dimmed item does nothing, as a dimmed toolbox tile does. */
        bool enabled = true;
        char scratch[64];
        (void)menu_tile_caption(&app->menu, tile, scratch, sizeof(scratch), &enabled, NULL);
        if (!enabled) return true;
        /* Delete asks first (owner, 2026-09-21): the first tap arms it and
           its tile says "Delete?"; a second tap on it within five seconds
           acts. Any other tap disarms it. */
        const rubraview_menu_item_t *item = rubraview_menu_item_at(&app->menu, tile);
        bool clears = item && proven_u8str_view_eq(item->action, PROVEN_LIT("recent_clear"));   /* D-104: asks first too */
        if (item && (clears || proven_u8str_view_eq(item->action, PROVEN_LIT("delete_file")))) {
            if (!rubraview_confirm_press(&app->menu_confirm, item - app->menu.tree->items,
                                         rubraview_pal_time_now_seconds())) {
                osd_say(app, clears ? TU8("tap Clear again to empty the recent list")
                                    : TU8("tap Delete again to move it to the recycle bin"));
                return true;
            }
        } else {
            rubraview_confirm_clear(&app->menu_confirm);
        }
        proven_u8str_view_t action;
        rubraview_menu_result_t result = rubraview_menu_activate(&app->menu, tile, &action);
        if (result == RUBRAVIEW_MENU_ACTIVATED) {
            if (app->menubox.headless) rubraview_box_dismiss(&app->menubox);   /* a choice made: the bar's menu folds */
            handle_action(app, action);
        } else if (result == RUBRAVIEW_MENU_DESCENDED || result == RUBRAVIEW_MENU_WENT_BACK) {
            sync_menubox_tiles(app);
        }
        return true;
    }

    tile = toolbox_here ? rubraview_box_tile_at(&app->toolbox, &metrics, x, y) : -1;
    if (tile >= 0) {
        /* RFC-0002 §4: the tiles of the profile on screen; a dimmed one does nothing. */
        toolbox_refresh(app);
        if (tile < app->toolbox_tile_count) {
            bool enabled = true;
            (void)toolbox_caption(app, &app->toolbox_tiles[tile], &enabled);
            if (enabled && proven_u8str_view_eq(app->toolbox_tiles[tile].action, PROVEN_LIT("toggle_subtitles"))) {
                /* D-33: a tap toggles, a double tap or a hold chooses —
                   so this tile is decided on release, not on the press. */
                sub_tile_result(app, rubraview_tap_press(&app->sub_tap, rubraview_pal_time_now_seconds()));
            } else if (enabled && seek_tile_action(app->toolbox_tiles[tile].action, &(bool){ false })) {
                /* Owner, 2026-10-09: a tap seeks 5 s on release; held, the
                   button offers longer steps to drag onto. */
                bool forward = false;
                (void)seek_tile_action(app->toolbox_tiles[tile].action, &forward);
                app->seekfan_detached = false;
                rubraview_seekfan_press(&app->seekfan, forward, rubraview_pal_time_now_seconds(),
                                        rubraview_box_tile_rect(&app->toolbox, &metrics, tile));
            } else if (enabled && (proven_u8str_view_eq(app->toolbox_tiles[tile].action, PROVEN_LIT("media_speed_cycle")) ||
                                   proven_u8str_view_eq(app->toolbox_tiles[tile].action, PROVEN_LIT("interval_cycle")))) {
                /* D-110: a tap or a drag, known on release. D-123: the Delay tile the same way. */
                speed_tile_press(app, y, false, proven_u8str_view_eq(app->toolbox_tiles[tile].action, PROVEN_LIT("interval_cycle")));
            } else if (enabled) {
                /* 1.24.0: a one-of-many tile lists its choices, over it (under it when there is no room above) */
                rubraview_rect_t at = rubraview_box_tile_rect(&app->toolbox, &metrics, tile);
                if (!chooser_open_tile(app, app->toolbox_tiles[tile].action, at.x, at.y + at.height, at.y))
                    handle_action(app, app->toolbox_tiles[tile].action);
            }
        }
        return true;
    }

    /* A lyric line: the song goes to it (owner, 2026-09-30). */
    if (app->media && !app->media_info.has_video && app->music_lyrics.count > 0) {
        for (size_t i = 0; i < 7; ++i) {
            if (app->lyric_row_index[i] < 0 || !rubraview_rect_contains(app->lyric_row_rect[i], x, y)) continue;
            double t = rubraview_lyrics_time_of(&app->music_lyrics, app->lyric_row_index[i]);   /* -1 for untimed words */
            if (t < 0.0) break;   /* words without times: the click is the song's, Play / Pause */
            media_seek_to(app, t);
            app->media_position = t;
            return true;
        }
    }

    /* The edge buttons (owner, 2026-09-29), under everything above. */
    rubraview_edge_hit_t edge = edge_nav_hit(app, x, y);
    if (edge.side != RUBRAVIEW_EDGE_NONE) {
        handle_action(app, cstr(rubraview_edgenav_action(edge.side, edge.slot, app->media != NULL)));
        return true;
    }

    return false;
}

/* ---- RV-116: the right-click list, and the slide show's settings ---- */

#define CTX_MAX RUBRAVIEW_CONTEXT_MAX_ITEMS

typedef struct ctx_row { proven_u8str_view_t caption, action; int32_t group; } ctx_row_t;

/* 1.21.0 (owner, 2026-10-18: "우클릭 메뉴에서, 툴바 버튼으로 호출 가능한 기능들은 빼 주세요"): is this action
   a tile of the toolbox for what is on screen? Then the right click leaves it out. Open file stays
   (owner, 2026-10-19: "파일 열기도 우클릭 컨텍스트 메뉴에 있어야 해요"). */
static bool ctx_on_toolbox(const app_state_t *app, proven_u8str_view_t action) {
    if (action.size == 0) return false;
    const rubraview_boxes_doc_t *doc = rubraview_boxes_document();
    const rubraview_toolbox_profile_t *p = rubraview_boxes_profile(doc, cstr(toolbox_profile_name(app)));
    if (!p) return false;
    for (int32_t i = 0; i < p->tile_count; ++i)
        if (proven_u8str_view_eq(doc->tiles[p->first_tile + i].action, action)) return true;
    return false;
}

/* The rows on show: the list's, less (on the picture) what the toolbox has a tile for. 1.23.0: the
   menu's sections are gone from it (owner, 2026-10-19: all of them have another way in). */
static size_t ctx_row_list(const app_state_t *app, ctx_row_t *rows) {
    if (!app->ctx_list) return 0;
    size_t n = 0;
    const rubraview_boxes_doc_t *doc = rubraview_boxes_document();
    for (int32_t i = 0; i < app->ctx_list->tile_count && n < CTX_MAX; ++i) {
        const rubraview_box_tile_t *item = &doc->context_items[app->ctx_list->first_tile + i];
        if (app->ctx_on_picture && ctx_on_toolbox(app, item->action)) continue;
        rows[n++] = (ctx_row_t){ item->caption, item->action, item->group };
    }
    return n;
}

static rubraview_window_t *ctx_window(const app_state_t *app) {
    return app->ctx_in_files && app->files_window ? app->files_window : app->window;   /* RV-117: or over the file window's rows */
}

static rubraview_ctxmenu_metrics_t ctx_metrics(const app_state_t *app) {
    double dpi = rubraview_pal_window_dpi_scale(ctx_window(app));
    double k = place_size((app_state_t *)app, PF_MENU);   /* 1.25.0: the menus' size */
    return (rubraview_ctxmenu_metrics_t){ .row_height = 30.0 * dpi * k, .gap = 9.0 * dpi, .pad = 6.0 * dpi, .width = 250.0 * dpi * k };
}

/* The open list's rows: their groups, and whether each can act now. */
static size_t ctx_rows(const app_state_t *app, int32_t *groups, bool *enabled) {
    ctx_row_t rows[CTX_MAX];
    size_t n = ctx_row_list(app, rows);
    rubraview_action_facts_t facts = action_facts(app);
    for (size_t i = 0; i < n; ++i) {
        groups[i] = rows[i].group;
        if (enabled) enabled[i] = rubraview_action_state(rows[i].action, &facts).enabled;
    }
    return n;
}

/* The list's one column, turned to stay in the window. */
typedef struct ctx_layout {
    size_t n;
    int32_t groups[CTX_MAX];
    bool enabled[CTX_MAX];
    rubraview_rect_t a;
    rubraview_ctxmenu_metrics_t m;
} ctx_layout_t;

static void ctx_layout_make(const app_state_t *app, ctx_layout_t *L) {
    L->n = ctx_rows(app, L->groups, L->enabled);
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(ctx_window(app), &w, &h);
    L->m = ctx_metrics(app);
    L->a = rubraview_ctxmenu_frame((double)w, (double)h, app->ctx_x, app->ctx_y, &L->m, L->groups, L->n);
}

static rubraview_rect_t slidepop_frame(const app_state_t *app) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    return rubraview_slidepop_frame((double)w, (double)h, app->slidepop_x, app->slidepop_y,
                                    rubraview_pal_window_dpi_scale(app->window));
}

void ctx_close(app_state_t *app) {
    if (app->ctx_open && app->ctx_in_files) app->files_dirty = true;
    app->ctx_open = false;
    app->ctx_list = NULL;
    app->ctx_focus = -1;
    app->ctx_on_picture = false;
    rubraview_confirm_clear(&app->ctx_confirm);
}

void slidepop_close(app_state_t *app) {
    app->slidepop_open = false;
    app->slidepop_typing = false;
}

/* A right click on the picture: while the slide show runs, its settings; otherwise
   the list for what is on screen (the toolbox's profiles name them). */
void ctx_open_at(app_state_t *app, double x, double y) {
    ctx_close(app);
    slidepop_close(app);
    app->ctx_in_files = false;
    app->sub_list_open = false;
    if (app->slideshow_running) {
        app->slidepop_open = true;
        app->slidepop_x = x;
        app->slidepop_y = y;
        return;
    }
    const rubraview_toolbox_profile_t *list = rubraview_boxes_context(rubraview_boxes_document(), cstr(toolbox_profile_name(app)));
    if (!list || list->tile_count <= 0) return;
    app->ctx_open = true;
    app->ctx_list = list;
    app->ctx_x = x;
    app->ctx_y = y;
    app->ctx_on_picture = true;
}

/* A row chosen, by a click or Enter. Delete asks a second time, as the menu's does. */
static void ctx_choose(app_state_t *app, int32_t row) {
    int32_t groups[CTX_MAX];
    bool enabled[CTX_MAX];
    size_t n = ctx_rows(app, groups, enabled);
    if (row < 0 || (size_t)row >= n || !enabled[row]) return;
    ctx_row_t rows[CTX_MAX];
    (void)ctx_row_list(app, rows);
    bool clears = proven_u8str_view_eq(rows[row].action, PROVEN_LIT("recent_clear"));
    if ((clears || proven_u8str_view_eq(rows[row].action, PROVEN_LIT("delete_file")) || proven_u8str_view_eq(rows[row].action, PROVEN_LIT("picker_recycle"))) &&
        !rubraview_confirm_press(&app->ctx_confirm, row, rubraview_pal_time_now_seconds())) {
        osd_say(app, clears ? TU8("click Clear again to empty the recent list") : TU8("click Delete again to move it to the recycle bin"));
        return;
    }
    proven_u8str_view_t action = rows[row].action;
    ctx_close(app);
    handle_action(app, action);
}

/* 1.25.0: in its place's face (Settings › General) */
static void draw_ctxmenu_on_in(app_state_t *app, rubraview_renderer_t *r, double px, double py);
static void draw_ctxmenu_on(app_state_t *app, rubraview_renderer_t *r, double px, double py) {
    int before = place_font_begin(app, PF_MENU, NULL);
    draw_ctxmenu_on_in(app, r, px, py);
    place_font_end(app, before);
}

static void draw_ctxmenu_on_in(app_state_t *app, rubraview_renderer_t *r, double px, double py) {
    if (!app->ctx_open) return;
    ctx_layout_t L;
    ctx_layout_make(app, &L);
    size_t n = L.n;
    if (n == 0) return;
    const int32_t *groups = L.groups;
    const bool *enabled = L.enabled;
    rubraview_ctxmenu_metrics_t m = L.m;
    double dpi = rubraview_pal_window_dpi_scale(ctx_window(app));
    rubraview_rect_t f = L.a;
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ f.x, f.y, f.width, f.height }, 0xF6202020u, 4.0 * dpi);
    rubraview_pal_render_stroke_rect(r, (rubraview_pal_rect_t){ f.x, f.y, f.width, f.height }, COLOR_BOX_BORDER, 1.0 * dpi, 4.0 * dpi);
    ctx_row_t rows[CTX_MAX];
    (void)ctx_row_list(app, rows);
    int32_t hover = rubraview_ctxmenu_row_at(L.a, &m, groups, n, px, py);
    if (hover < 0) hover = app->ctx_focus;
    rubraview_action_facts_t facts = action_facts(app);
    double now = rubraview_pal_time_now_seconds();
    for (size_t i = 0; i < n; ++i) {
        rubraview_rect_t row = rubraview_ctxmenu_row(L.a, &m, groups, i);
        if (i > 0 && groups[i] != groups[i - 1]) {   /* the line between two groups */
            rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ f.x + 8.0 * dpi, row.y - m.gap * 0.5 - 0.5 * dpi,
                                                                      f.width - 16.0 * dpi, 1.0 * dpi }, 0x40FFFFFFu, 0.0);
        }
        rubraview_pal_rect_t cell = { row.x + 4.0 * dpi, row.y + 1.0 * dpi, row.width - 8.0 * dpi, row.height - 2.0 * dpi };
        if ((int32_t)i == hover && enabled[i]) rubraview_pal_render_fill_rect(r, cell, 0x40FFFFFFu, 3.0 * dpi);
        const ctx_row_t *item = &rows[i];
        rubraview_action_state_t st = rubraview_action_state(item->action, &facts);
        char scratch[160];
        proven_u8str_view_t text;
        if (rubraview_confirm_armed(&app->ctx_confirm, (int64_t)i, now)) {
            text = proven_u8str_view_eq(item->action, PROVEN_LIT("recent_clear")) ? TU8("Clear?") : TU8("Delete?");
        } else {
            text = rubraview_tile_caption(rubraview_tr_ctx_u8("menu", item->caption), false, st.mark, st.value, scratch, sizeof(scratch));
        }
        rubraview_pal_rect_t words = { cell.x + 10.0 * dpi, cell.y, cell.width - 14.0 * dpi, cell.height };
        rubraview_pal_render_draw_text(r, text, words, 14.0 * dpi * place_size(app, PF_MENU), enabled[i] ? COLOR_TEXT : 0x60F0F0F0u, RUBRAVIEW_TEXT_LEFT);
    }
}

void draw_ctxmenu(app_state_t *app) {
    if (!app->ctx_in_files) draw_ctxmenu_on(app, app->renderer, app->pointer_x, app->pointer_y);
}

/* RV-117: a file's own list, from a right click on its row in either list. */
void list_row_menu(app_state_t *app, size_t row, double x, double y, bool in_files) {
    ctx_close(app);
    slidepop_close(app);
    const rubraview_toolbox_profile_t *list = rubraview_boxes_context(rubraview_boxes_document(), PROVEN_LIT("listrow"));
    if (!list || list->tile_count <= 0) return;
    app->ctx_open = true;
    app->ctx_list = list;
    app->ctx_in_files = in_files;
    app->ctx_x = x;
    app->ctx_y = y;
    app->list_ctx_row = row;
    app->files_dirty = true;
}

/* What the field says when it is not being typed in: the delay, "3.5". */
static proven_u8str_view_t slidepop_delay_text(const app_state_t *app, char *buffer, size_t size) {
    if (app->slidepop_typing) return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->slidepop_text, .size = app->slidepop_len };
    int n = snprintf(buffer, size, "%.1f s", app->slideshow.interval_seconds);
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)buffer, .size = n > 0 ? (size_t)n : 0 };
}

static void slidepop_type_end(app_state_t *app, bool keep) {
    if (!app->slidepop_typing) return;
    app->slidepop_typing = false;
    double seconds = 0.0;
    if (!keep) return;
    if (rubraview_subwin_amount((proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->slidepop_text, .size = app->slidepop_len }, &seconds)) {
        interval_set(app, seconds);
    } else {
        osd_say(app, TU8("the delay is a number of seconds, 0.1 to 300"));
    }
}

static int slidepop_transition(const app_state_t *app) {
    int turn = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("transition")));
    return turn >= 0 && turn < 4 ? turn : 1;
}

void draw_slidepop(app_state_t *app) {
    if (!app->slidepop_open) return;
    double s = rubraview_pal_window_dpi_scale(app->window);
    rubraview_rect_t f = slidepop_frame(app);
    rubraview_renderer_t *r = app->renderer;
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ f.x, f.y, f.width, f.height }, 0xF6202020u, 5.0 * s);
    rubraview_pal_render_stroke_rect(r, (rubraview_pal_rect_t){ f.x, f.y, f.width, f.height }, COLOR_TILE_CURRENT, 1.5 * s, 5.0 * s);
    rubraview_rect_t t = rubraview_slidepop_title(f, s);
    rubraview_pal_render_draw_text(r, app->slideshow.paused ? TU8("Slide show - paused") : TU8("Slide show"),
                                   (rubraview_pal_rect_t){ t.x, t.y, t.width, t.height }, 15.0 * s, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    const char *const LABELS[3] = { T("Delay"), T("Change"), T("At the end") };
    for (int l = 0; l < 3; ++l) {
        rubraview_rect_t lr = rubraview_slidepop_label(f, s, (rubraview_slidepop_label_t)l);
        rubraview_pal_render_draw_text(r, cstr(LABELS[l]), (rubraview_pal_rect_t){ lr.x, lr.y, lr.width, lr.height },
                                       13.0 * s, 0xB0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
    }
    int fx = slidepop_transition(app);
    bool full = rubraview_pal_window_is_fullscreen(app->window);
    for (int p = 0; p < RUBRAVIEW_SLIDEPOP_PART_COUNT; ++p) {
        rubraview_rect_t pr = rubraview_slidepop_part(f, s, (rubraview_slidepop_part_t)p);
        rubraview_pal_rect_t box = { pr.x, pr.y, pr.width, pr.height };
        bool current = false;
        char buffer[24];
        proven_u8str_view_t text = PROVEN_LIT("");
        switch ((rubraview_slidepop_part_t)p) {
            case RUBRAVIEW_SLIDEPOP_DELAY_DOWN:  text = PROVEN_LIT("-"); break;
            case RUBRAVIEW_SLIDEPOP_DELAY_UP:    text = PROVEN_LIT("+"); break;
            case RUBRAVIEW_SLIDEPOP_DELAY_FIELD: text = slidepop_delay_text(app, buffer, sizeof(buffer)); current = app->slidepop_typing; break;
            case RUBRAVIEW_SLIDEPOP_FX_CUT:      text = TU8("Cut");   current = fx == 0; break;
            case RUBRAVIEW_SLIDEPOP_FX_FADE:     text = TU8("Fade");  current = fx == 1; break;
            case RUBRAVIEW_SLIDEPOP_FX_SLIDE:    text = TU8("Slide"); current = fx == 2; break;
            case RUBRAVIEW_SLIDEPOP_FX_ZOOM:     text = TU8("Zoom");  current = fx == 3; break;
            case RUBRAVIEW_SLIDEPOP_LOOP:        text = TU8("Go round");    current = app->slideshow.loop == RUBRAVIEW_LOOP_ALL; break;
            case RUBRAVIEW_SLIDEPOP_ONCE:        text = TU8("Stop on last"); current = app->slideshow.loop == RUBRAVIEW_LOOP_PLAY_ONCE; break;
            case RUBRAVIEW_SLIDEPOP_PAUSE:       text = app->slideshow.paused ? TU8("Play") : TU8("Pause"); break;
            case RUBRAVIEW_SLIDEPOP_STOP:        text = TU8("Stop"); break;
            case RUBRAVIEW_SLIDEPOP_FULL:        text = full ? TU8("Window") : TU8("Full screen"); break;
            case RUBRAVIEW_SLIDEPOP_CLOSE:       text = TU8("Close"); break;
            default: break;
        }
        bool hover = app->pointer_x >= pr.x && app->pointer_x < pr.x + pr.width && app->pointer_y >= pr.y && app->pointer_y < pr.y + pr.height;
        bool field = p == RUBRAVIEW_SLIDEPOP_DELAY_FIELD;
        rubraview_pal_render_fill_rect(r, box, field ? 0xFF101010u : current ? COLOR_TILE_CURRENT : hover ? 0x50FFFFFFu : 0x28FFFFFFu, 3.0 * s);
        rubraview_pal_render_stroke_rect(r, box, field && current ? COLOR_TILE_CURRENT : COLOR_BOX_BORDER, 1.0 * s, 3.0 * s);
        rubraview_pal_render_draw_text(r, text, box, (p <= RUBRAVIEW_SLIDEPOP_DELAY_UP && !field ? 18.0 : 13.0) * s, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
}

static void slidepop_press(app_state_t *app, double x, double y) {
    double s = rubraview_pal_window_dpi_scale(app->window);
    rubraview_rect_t f = slidepop_frame(app);
    rubraview_slidepop_part_t part = rubraview_slidepop_part_at(f, s, x, y);
    if (part != RUBRAVIEW_SLIDEPOP_DELAY_FIELD) slidepop_type_end(app, true);
    if (part == RUBRAVIEW_SLIDEPOP_NONE) {
        if (!rubraview_rect_contains(f, x, y)) slidepop_close(app);   /* a click away closes it */
        return;
    }
    switch (part) {
        case RUBRAVIEW_SLIDEPOP_DELAY_DOWN: interval_set(app, rubraview_slidepop_step(app->slideshow.interval_seconds, -1)); break;
        case RUBRAVIEW_SLIDEPOP_DELAY_UP:   interval_set(app, rubraview_slidepop_step(app->slideshow.interval_seconds, +1)); break;
        case RUBRAVIEW_SLIDEPOP_DELAY_FIELD:
            if (!app->slidepop_typing) {
                int n = snprintf(app->slidepop_text, sizeof(app->slidepop_text), "%.1f", app->slideshow.interval_seconds);
                app->slidepop_len = n > 0 ? (size_t)n : 0;
                app->slidepop_typing = true;
            }
            break;
        case RUBRAVIEW_SLIDEPOP_FX_CUT: case RUBRAVIEW_SLIDEPOP_FX_FADE:
        case RUBRAVIEW_SLIDEPOP_FX_SLIDE: case RUBRAVIEW_SLIDEPOP_FX_ZOOM: {
            const char *const SAY[4] = { T("slides change at once"), T("slides fade into the next"),
                                                T("slides slide in"), T("slides zoom and fade") };
            int fx = (int)(part - RUBRAVIEW_SLIDEPOP_FX_CUT);
            rubraview_settings_set(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("transition"), (double)fx);   /* Settings › Viewer › Turning a page */
            settings_took_effect(app);
            osd_say(app, cstr(SAY[fx]));
            break;
        }
        case RUBRAVIEW_SLIDEPOP_LOOP: app->slideshow.loop = RUBRAVIEW_LOOP_ALL; break;
        case RUBRAVIEW_SLIDEPOP_ONCE: app->slideshow.loop = RUBRAVIEW_LOOP_PLAY_ONCE; break;
        case RUBRAVIEW_SLIDEPOP_PAUSE:
            if (app->slideshow.paused) rubraview_slideshow_resume(&app->slideshow);
            else rubraview_slideshow_pause(&app->slideshow);
            break;
        case RUBRAVIEW_SLIDEPOP_STOP:
            slidepop_close(app);
            if (app->slideshow_running) toggle_slideshow(app);
            break;
        case RUBRAVIEW_SLIDEPOP_FULL:  handle_action(app, PROVEN_LIT("toggle_fullscreen")); break;
        case RUBRAVIEW_SLIDEPOP_CLOSE: slidepop_close(app); break;
        default: break;
    }
}

/* A press while either is open: true when it was theirs (a click away closes and is spent). */
static bool popups_press(app_state_t *app, double x, double y, bool right) {
    if (app->slidepop_open) {
        if (right) { slidepop_close(app); return false; }   /* a right click elsewhere opens it there again */
        slidepop_press(app, x, y);
        return true;
    }
    if (!app->ctx_open) return false;
    ctx_layout_t L;
    ctx_layout_make(app, &L);
    if (right) { ctx_close(app); return false; }
    if (rubraview_rect_contains(L.a, x, y)) {
        int32_t row = rubraview_ctxmenu_row_at(L.a, &L.m, L.groups, L.n, x, y);
        if (row >= 0) ctx_choose(app, row);
        return true;
    }
    ctx_close(app);
    return true;
}

/* Keys while either is open: the arrows and Enter in the list, typing in the delay, Esc. */
static bool popups_key(app_state_t *app, rubraview_key_combo_t combo) {
    if (app->slidepop_open) {
        if (app->slidepop_typing) {
            if (key_is(combo, "Escape")) { slidepop_type_end(app, false); return true; }
            if (key_is(combo, "Enter")) { slidepop_type_end(app, true); return true; }
            if (key_is(combo, "Backspace")) { if (app->slidepop_len > 0) app->slidepop_len--; return true; }
            if (combo.key_name.size == 1 || key_is(combo, "Period") || key_is(combo, "Comma")) {
                char c = key_is(combo, "Period") ? '.' : key_is(combo, "Comma") ? ',' : combo.key_name.ptr[0];
                if (((c >= '0' && c <= '9') || c == '.' || c == ',') && app->slidepop_len + 1 < sizeof(app->slidepop_text)) {
                    app->slidepop_text[app->slidepop_len++] = c;
                }
                return true;
            }
            return true;   /* while typing, the slide show's keys wait */
        }
        if (key_is(combo, "Escape")) { slidepop_close(app); return true; }
        return false;
    }
    if (!app->ctx_open) return false;
    int32_t groups[CTX_MAX];
    bool enabled[CTX_MAX];
    size_t n = ctx_rows(app, groups, enabled);
    if (key_is(combo, "Escape")) { ctx_close(app); return true; }
    if (key_is(combo, "Down")) { app->ctx_focus = rubraview_ctxmenu_step(enabled, n, app->ctx_focus, +1); return true; }
    if (key_is(combo, "Up")) { app->ctx_focus = rubraview_ctxmenu_step(enabled, n, app->ctx_focus, -1); return true; }
    if (key_is(combo, "Enter")) { if (app->ctx_focus >= 0) ctx_choose(app, app->ctx_focus); return true; }
    ctx_close(app);   /* any other key: it closes, and the key does what it does */
    return false;
}

#define HINT_DELAY 0.6

bool hint_shown(const app_state_t *app) {
    return app->hint.size > 0 && rubraview_pal_time_now_seconds() - app->hint_since >= HINT_DELAY;
}

/* RV-116: the hint, below and right of the pointer, turned to stay in the window. */
/* A tile's or a menu item's hint (owner, 2026-10-17): what it does, and its
   keys, "Toolbox (T, Ctrl+T)". Its words are the hint when it has one, else
   the action's description as the key list has it. */
static proven_u8str_view_t hint_with_keys(app_state_t *app, proven_u8str_view_t action, const char *said, char *buffer, size_t capacity) {
    if (action.size == 0 || capacity < 8) return PROVEN_LIT("");
    const char *words = said ? said : rubraview_action_words(action);
    if (words && !said) words = rubraview_tr(words);
    size_t at = 0;
    int n = snprintf(buffer, capacity, "%s", words ? words : "");
    at = n > 0 ? ((size_t)n < capacity ? (size_t)n : capacity - 1) : 0;
    size_t keys = 0;
    for (size_t b = 0; b < app->keymap.count && keys < 3; ++b) {
        const rubraview_key_binding_t *binding = &app->keymap.bindings[b];
        if (!proven_u8str_view_eq(binding->action, action)) continue;
        for (size_t c = 0; c < binding->combo_count && keys < 3; ++c) {
            char combo[48];
            proven_u8str_view_t k = rubraview_key_combo_format(combo, sizeof(combo), binding->combos[c]);
            if (k.size == 0) continue;
            n = snprintf(buffer + at, capacity - at, "%s%.*s", keys == 0 ? (at > 0 ? "  (" : "(") : ", ", (int)k.size, k.ptr);
            if (n > 0) at += (size_t)n < capacity - at ? (size_t)n : capacity - at - 1;
            keys++;
        }
    }
    if (keys > 0 && at + 1 < capacity) { buffer[at++] = ')'; buffer[at] = '\0'; }
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)buffer, .size = at };
}

void draw_hint(app_state_t *app, double win_w, double win_h) {
    if (!hint_shown(app) || app->tmenu_open) return;   /* 1.25.0: never over the ☰ pull-down's rows */
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double font = 13.0 * dpi;
    double w = (double)app->hint.size * font * 0.52 + 20.0 * dpi;
    double tw = 0.0, th = 0.0;
    if (rubraview_pal_render_measure_text(app->renderer, app->hint, font, &tw)) w = tw + 20.0 * dpi;
    double h = 28.0 * dpi;
    /* 1.25.0: a long one in lines, never wider than most of the window (VM: the Floating Widget's ran off both edges) */
    double most = fmin(win_w - 8.0 * dpi, 520.0 * dpi);
    if (w > most) {
        w = most;
        if (rubraview_pal_render_measure_text_wrapped(app->renderer, app->hint, font, w - 20.0 * dpi, &tw, &th)) h = th + 12.0 * dpi;
    }
    double x = app->pointer_x + 14.0 * dpi, y = app->pointer_y + 22.0 * dpi;
    if (x + w > win_w) x = win_w - w - 4.0 * dpi;
    if (y + h > win_h) y = app->pointer_y - h - 8.0 * dpi;
    if (x < 0.0) x = 0.0;
    if (y < 0.0) y = 0.0;
    rubraview_pal_rect_t box = { x, y, w, h };
    rubraview_pal_render_fill_rect(app->renderer, box, 0xF0303030u, 3.0 * dpi);
    rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_BOX_BORDER, 1.0 * dpi, 3.0 * dpi);
    rubraview_pal_rect_t words = { box.x + 10.0 * dpi, box.y, box.width - 20.0 * dpi, box.height };
    rubraview_pal_render_draw_text(app->renderer, app->hint, words, font, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
}

bool sub_tile_anchor(app_state_t *app, double *x, double *y) {
    if (app->toolbox.state == RUBRAVIEW_BOX_DETACHED) return false;
    rubraview_tile_metrics_t metrics = tile_metrics(rubraview_pal_window_dpi_scale(app->window));
    for (int32_t i = 0; i < app->toolbox_tile_count; ++i) {
        if (!proven_u8str_view_eq(app->toolbox_tiles[i].action, PROVEN_LIT("toggle_subtitles"))) continue;
        rubraview_rect_t r = rubraview_box_tile_rect(&app->toolbox, &metrics, i);
        if (r.width <= 0.0) return false;
        *x = r.x + r.width * 0.5;
        *y = r.y - 4.0 * rubraview_pal_window_dpi_scale(app->window);
        return true;
    }
    return false;
}

/* D-33: a press on the subtitle box, its buttons, or away from a selected
   box. True when the press was the box's. */
static bool subbox_press(app_state_t *app, double x, double y, bool buttons_only) {
    if (!app->media || app->tracks.current_subtitle < 0) return false;
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    rubraview_rect_t box = subbox_geometry(app, (double)win_w, (double)win_h, NULL);
    rubraview_subbox_part_t part = rubraview_subbox_hit(&app->subbox, box, SUBBOX_BUTTON * dpi, SUBBOX_GAP * dpi,
                                                        subbox_live(app), x, y);
    /* The buttons of a selected box float over whatever is there (the
       toolbox's anchor sits right by the default place), so they are asked
       before the rest of the chrome; the body and the tap away after it. */
    if (buttons_only && (part == RUBRAVIEW_SUBBOX_NONE || part == RUBRAVIEW_SUBBOX_BODY)) return false;
    switch (part) {
        case RUBRAVIEW_SUBBOX_NONE:
            if (!app->subbox.selected) return false;
            app->subbox.selected = false;   /* the tap that lets go does nothing else */
            return true;
        case RUBRAVIEW_SUBBOX_BODY:
            app->subbox.selected = true;
            return true;
        case RUBRAVIEW_SUBBOX_SETTINGS:
            settings_open(app);
            if (app->settings_open) {
                rubraview_settings_view_set_page(&app->settings_view, RUBRAVIEW_TAB_VIDEO);
                app->settings_dirty = true;
            }
            return true;
        case RUBRAVIEW_SUBBOX_MOVE:
        case RUBRAVIEW_SUBBOX_RESIZE:
            (void)rubraview_subbox_begin_drag(&app->subbox, part, box, x, y);
            return true;
        case RUBRAVIEW_SUBBOX_CLOSE:
            if (!subs_may_leave(app)) return true;
            app->subtitle_last = app->tracks.current_subtitle;
            subtitle_select(app, -1);
            app->subbox.selected = false;
            subtitle_say(app);
            return true;
    }
    return false;
}

/* D-33, D-92: M or R held and dragged. Both write Settings > Video's
   place and size of the box, in whole percents of the window, so the
   settings page and the box never disagree. (R wrote the subtitle size
   until 0.0.47; the size is its own setting now.) */
static void subbox_drag_motion(app_state_t *app, double x, double y) {
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) return;
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    rubraview_rect_t box = subbox_geometry(app, (double)win_w, (double)win_h, NULL);
    bool resizing = app->subbox.dragging == RUBRAVIEW_SUBBOX_RESIZE;
    double height = box.height;
    rubraview_subbox_drag(&app->subbox, box, x, y, (double)win_w, (double)win_h,
                          160.0 * dpi, 0.05 * (double)win_h, (double)win_h, &height);
    rubraview_rect_t moved = { app->subbox.left * (double)win_w, app->subbox.bottom * (double)win_h - height,
                               app->subbox.width * (double)win_w, height };
    app->subbox.placed = false;   /* the drag's own record is not a place to migrate */
    rubraview_subbox_place_t place = subbox_place(app);
    rubraview_subbox_place_from_rect(&place, moved, (double)win_w, (double)win_h, resizing);
    rubraview_settings_t *st = &app->settings;
    rubraview_settings_set(st, PROVEN_LIT("video"), PROVEN_LIT("subtitle_x"), floor(place.x * 100.0 + 0.5));
    rubraview_settings_set(st, PROVEN_LIT("video"), PROVEN_LIT("subtitle_y"), floor(place.y * 100.0 + 0.5));
    if (resizing) {
        rubraview_settings_set(st, PROVEN_LIT("video"), PROVEN_LIT("subtitle_width"), floor(place.width * 100.0 + 0.5));
        rubraview_settings_set(st, PROVEN_LIT("video"), PROVEN_LIT("subtitle_height"), floor(place.height * 100.0 + 0.5));
    }
    settings_took_effect(app);
}

/* The text box: renaming a file, or giving the picked files one
   extension. Drawn by the chrome and by the picker, which covers it. */
/* The OSD line. The picker covers the chrome, so it draws this itself. */
/* 1.25.0 (owner, 2026-10-20: "툴바의 진행막대 위에서 휠을 돌리면 파일이름/안내메시지 글꼴 크기"): how large the
   toolbar's file name and the messages are, from Settings › Display. */
double name_scale(const app_state_t *app) {
    double k = rubraview_settings_get(&app->settings, PROVEN_LIT("display"), PROVEN_LIT("name_scale")) / 100.0;
    return k < 0.6 ? 0.6 : k > 2.0 ? 2.0 : k;
}

void draw_notice(app_state_t *app, double win_w, double win_h, double chrome_dpi) {
    if (app->notice_seconds > 0.0 && app->notice_length > 0) {
        double k = name_scale(app);   /* 1.25.0 */
        int before = place_font_begin(app, PF_NAME, NULL);
        double box_h = 44.0 * chrome_dpi * k;
        rubraview_pal_rect_t box = { win_w * 0.2, win_h * 0.08, win_w * 0.6, box_h };
        rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BAR_FILL, 3.0);
        rubraview_pal_render_draw_text(app->renderer,
                                       (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->notice, .size = app->notice_length },
                                       box, 15.0 * chrome_dpi * k, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        place_font_end(app, before);
    }
}

void draw_rename_box(app_state_t *app, double win_w, double win_h, double dpi) {
    if (!app->rename_active) return;
    {
        double box_w = 560.0 * dpi, box_h = 72.0 * dpi;   /* 1.25.0: roomier */
        if (box_w > win_w - 16.0 * dpi) box_w = win_w - 16.0 * dpi;   /* a narrow Edit window (RV-137) */
        rubraview_pal_rect_t box = { (win_w - box_w) * 0.5, win_h * 0.75, box_w, box_h };
        /* Edit to all asks with Edit open: the box goes left of its panel. */
        if (edit_panel_open(app) && !app->edit_window && app->panel.open && box.x + box.width > app->panel.bounds.x && app->panel.bounds.x > box_w)
            box.x = (app->panel.bounds.x - box_w) * 0.5;
        /* A password is asked in the middle, clear of the toolbox's anchor
           (VM, 2026-10-01: it showed through the box). */
        if (app->rename_is_password || app->rename_is_target || app->rename_is_extension) box.y = (win_h - box_h) * 0.5;
        if (app->rename_is_extension) {   /* 1.24.0: the rows over the box, the one it edits lit */
            double row_h = 34.0 * dpi, top = box.y - 40.0 * dpi - row_h * (double)app->ext_n;
            if (top < 8.0 * dpi) top = 8.0 * dpi;
            rubraview_pal_rect_t panel = { box.x, top - 34.0 * dpi, box.width, 34.0 * dpi + row_h * (double)app->ext_n + 6.0 * dpi };
            rubraview_pal_render_fill_rect(app->renderer, panel, 0xF6202020u, 4.0 * dpi);
            rubraview_pal_render_stroke_rect(app->renderer, panel, COLOR_BOX_BORDER, 1.0, 4.0 * dpi);
            rubraview_pal_render_draw_text(app->renderer, TU8("Change extensions: a new one for each (Tab: the next row)"),
                                           (rubraview_pal_rect_t){ panel.x + 12.0 * dpi, panel.y, panel.width - 24.0 * dpi, 34.0 * dpi },
                                           14.0 * dpi, 0xC0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
            for (int32_t k = 0; k < app->ext_n; ++k) {
                rubraview_pal_rect_t r = { panel.x + 6.0 * dpi, top + row_h * (double)k, panel.width - 12.0 * dpi, row_h - 4.0 * dpi };
                app->ext_row_rect[k] = r;
                bool on = k == app->ext_at;
                rubraview_pal_render_fill_rect(app->renderer, r, on ? COLOR_TILE_CURRENT : pal_rect_has(r, app->pointer_x, app->pointer_y) ? 0x40FFFFFFu : COLOR_TILE_FILL, 3.0);
                char line[128];
                proven_u8str_view_t to = on ? rubraview_textedit_text(&app->rename_edit) : cstr(app->ext_to[k]);
                int n = snprintf(line, sizeof(line), "%s   \xE2\x86\x92   %.*s", app->ext_from[k], (int)(to.size < 64 ? to.size : 64), to.ptr);
                if (n > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n },
                                                          (rubraview_pal_rect_t){ r.x + 14.0 * dpi, r.y, r.width - 28.0 * dpi, r.height },
                                                          16.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
            }
        }
        if (app->rename_is_find) {
            /* RFC-0005: Find, above the text. */
            box.y = 128.0 * dpi;   /* under the messages line */
            rubraview_pal_rect_t label = { box.x - 40.0 * dpi, box.y - 26.0 * dpi, box.width + 80.0 * dpi, 22.0 * dpi };
            rubraview_pal_render_fill_rect(app->renderer, label, COLOR_BOX_FILL, 3.0);
            rubraview_pal_render_draw_text(app->renderer, TU8("Find:  Enter / F3 the next  ·  Shift+Enter back  ·  Esc"),
                                           label, 13.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
        if (app->rename_is_listfilter) {   /* 1.25.0 */
            rubraview_pal_rect_t label = { box.x, box.y - 30.0 * dpi, box.width, 26.0 * dpi };
            rubraview_pal_render_fill_rect(app->renderer, label, COLOR_BOX_FILL, 3.0);
            rubraview_pal_render_draw_text(app->renderer, TU8("The list shows the names holding this  ·  Esc shows them all"),
                                           label, 14.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
        if (app->rename_is_target) {
            rubraview_pal_rect_t label = { box.x - 40.0 * dpi, box.y - 30.0 * dpi, box.width + 80.0 * dpi, 26.0 * dpi };
            rubraview_pal_render_fill_rect(app->renderer, label, COLOR_BOX_FILL, 3.0);
            rubraview_pal_render_draw_text(app->renderer,
                                           app->target_moving ? TU8("Move to which folder?  Enter  /  Ctrl+O browses  /  a new one is made")
                                                              : TU8("Copy to which folder?  Enter  /  Ctrl+O browses  /  a new one is made"),
                                           label, 14.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
        /* The typed path sits where the path is, as an address bar does. */
        if (app->rename_is_path || app->rename_is_filter) {
            rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ 0.0, picker_top(app), win_w, PICKER_CRUMB_HEIGHT * dpi },
                                           0xFF101010u, 0.0);   /* the chips under it do not show through */
            box = (rubraview_pal_rect_t){ 4.0 * dpi, picker_top(app) + 4.0 * dpi, win_w - 8.0 * dpi, (PICKER_CRUMB_HEIGHT - 8.0) * dpi };
        }
        rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BOX_FILL, 3.0);
        rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_BOX_BORDER, 1.0, 3.0);
        /* The name with the syllable the IME is still building at the
           caret, the selection shaded under its characters, and a caret
           that blinks (owner, 2026-09-23: the box did not look like it was
           taking text). Widths come from the face the text is drawn in, so
           caret and shading sit under the right characters, Hangul too. */
        const rubraview_textedit_t *te = &app->rename_edit;
        char shown[3 * (sizeof(app->rename_buffer) + sizeof(app->rename_composing))];
        size_t caret = te->caret, len = te->len, comp = app->rename_composing_length;
        memcpy(shown, app->rename_buffer, caret);
        memcpy(shown + caret, app->rename_composing, comp);
        memcpy(shown + caret + comp, app->rename_buffer + caret, len - caret);
        size_t total = len + comp;
        size_t sel_a = 0, sel_b = 0;
        bool has_selection = rubraview_textedit_has_selection(te);
        if (has_selection) rubraview_textedit_selection_range(te, &sel_a, &sel_b);
        if (app->rename_is_password) {
            /* A dot a character (the IME's unfinished syllable too); the
               caret and the selection are counted in characters. */
            char plain[sizeof(app->rename_buffer) + sizeof(app->rename_composing)];
            memcpy(plain, shown, total);
            size_t dots = 0, at_caret = 0, at_a = 0, at_b = 0;
            for (size_t i = 0; i <= total; ++i) {
                if (i == caret + comp) at_caret = dots;
                if (i == (sel_a >= caret ? sel_a + comp : sel_a)) at_a = dots;
                if (i == (sel_b >= caret ? sel_b + comp : sel_b)) at_b = dots;
                if (i < total && ((unsigned char)plain[i] & 0xC0) != 0x80) dots++;
            }
            secure_wipe(plain, sizeof(plain));
            for (size_t d = 0; d < dots; ++d) memcpy(shown + 3 * d, "\xE2\x97\x8F", 3);   /* ● */
            total = 3 * dots;
            caret = 3 * at_caret;
            comp = 0;
            sel_a = 3 * at_a;
            sel_b = 3 * at_b;
            rubraview_pal_rect_t label = { box.x, box.y - 30.0 * dpi, box.width, 26.0 * dpi };
            char title[1200];
            proven_u8str_view_t name = rubraview_path_basename((proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->password_for, .size = app->password_for_len });
            int tn = snprintf(title, sizeof(title), T("Password for %.*s"), (int)name.size, name.ptr);
            if (tn > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)title, .size = (size_t)(tn < (int)sizeof(title) ? tn : (int)sizeof(title) - 1) },
                                                       label, 14.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
        double size = 18.0 * dpi, pad = 14.0 * dpi;
        double full = 0.0, to_caret = 0.0;
        (void)rubraview_pal_render_measure_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)shown, .size = total }, size, &full);
        (void)rubraview_pal_render_measure_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)shown, .size = caret + comp }, size, &to_caret);
        /* Centred while it fits; longer, it scrolls so the caret stays in the box. */
        double inner = box.width - 2.0 * pad;
        double x0 = full <= inner && !app->rename_is_path ? box.x + (box.width - full) * 0.5 : box.x + pad;
        if (full > inner && to_caret > inner) x0 = box.x + pad + inner - to_caret;
        if (has_selection) {
            size_t a = sel_a, b = sel_b;
            /* the composing text sits at the caret, which is one end of the selection */
            size_t da = a >= caret ? a + comp : a, db = b >= caret ? b + comp : b;
            double xa = 0.0, xb = 0.0;
            (void)rubraview_pal_render_measure_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)shown, .size = da }, size, &xa);
            (void)rubraview_pal_render_measure_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)shown, .size = db }, size, &xb);
            rubraview_pal_rect_t shade = { x0 + xa, box.y + box.height * 0.2, xb - xa, box.height * 0.6 };
            rubraview_pal_render_fill_rect(app->renderer, shade, 0xA05B9BD5u, 2.0);
        }
        rubraview_pal_rect_t line = { x0, box.y, full + 4.0 * dpi, box.height };
        if (total > 0) {
            rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)shown, .size = total },
                                           line, size, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
        }
        if (fmod(rubraview_pal_time_now_seconds(), 1.0) < 0.5) {
            rubraview_pal_rect_t bar = { x0 + to_caret, box.y + box.height * 0.22, 2.0 * dpi, box.height * 0.56 };
            rubraview_pal_render_fill_rect(app->renderer, bar, COLOR_TEXT, 0.0);
        }
        /* 1.24.0: its buttons under it — what Enter does, Cancel (Esc), and Browse (Ctrl+O) for a folder */
        const char *words[4];
        int32_t kinds[4], n = 0;
        if (app->rename_is_find) {
            words[n] = T("Next"); kinds[n++] = RENAME_BTN_OK;
            words[n] = T("Back"); kinds[n++] = RENAME_BTN_BACK;
            words[n] = T("Close"); kinds[n++] = RENAME_BTN_CANCEL;
        } else {
            words[n] = app->rename_is_extension ? (app->ext_at + 1 < app->ext_n ? T("Next") : T("Apply"))
                     : app->rename_is_password ? T("Open") : app->rename_is_filter || app->rename_is_listfilter ? T("Done") : T("OK");
            kinds[n++] = RENAME_BTN_OK;
            if (app->rename_is_extension && app->ext_at + 1 < app->ext_n) { words[n] = T("Apply"); kinds[n++] = RENAME_BTN_OK + 100; }
            if (app->rename_is_target) { words[n] = T("Browse..."); kinds[n++] = RENAME_BTN_BROWSE; }
            words[n] = T("Cancel"); kinds[n++] = RENAME_BTN_CANCEL;
        }
        double bw = 120.0 * dpi, bh = 48.0 * dpi, gap = 10.0 * dpi;   /* 1.25.0: roomier */
        double bx = app->rename_is_path || app->rename_is_filter ? box.x + box.width - (bw + gap) * (double)n
                                                                   : box.x + (box.width - (bw + gap) * (double)n + gap) * 0.5;
        double by = box.y + box.height + 10.0 * dpi;
        app->rename_btn_n = n;
        for (int32_t i = 0; i < n; ++i) {
            rubraview_pal_rect_t b = { bx + (double)i * (bw + gap), by, bw, bh };
            app->rename_btn[i] = b;
            app->rename_btn_kind[i] = kinds[i];
            bool over = pal_rect_has(b, app->pointer_x, app->pointer_y);
            rubraview_pal_render_fill_rect(app->renderer, b, i == 0 ? COLOR_TILE_CURRENT : over ? 0x50FFFFFFu : COLOR_BOX_FILL, 3.0);
            rubraview_pal_render_stroke_rect(app->renderer, b, COLOR_BOX_BORDER, 1.0, 3.0);
            rubraview_pal_render_draw_text(app->renderer, cstr(words[i]), b, 15.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
    }
}

/* The two points as text, side by side, with the one in hand marked and
   a line saying how the keys and the numbers cross over. */
static void draw_answer_close(app_state_t *app, rubraview_pal_rect_t box, double dpi) {
    rubraview_pal_rect_t x = answer_box_close(app, box);
    if (pal_rect_has(x, app->pointer_x, app->pointer_y)) rubraview_pal_render_fill_rect(app->renderer, x, COLOR_CLOSE_HOVER, 2.0);
    rubraview_pal_render_draw_text(app->renderer, PROVEN_LIT("X"), x, 14.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
}

static void draw_ab_edit_box(app_state_t *app, double win_w, double win_h, double dpi) {
    if (!app->ab_edit_open) return;
    (void)win_w; (void)win_h;
    rubraview_pal_rect_t box = ab_box_rect(app);
    rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BOX_FILL, 3.0);
    rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_BOX_BORDER, 1.0, 3.0);
    draw_answer_close(app, box, dpi);
    const char *const TAKE[2] = { T("A = now  ["), T("B = now  ]") };
    for (int32_t k = 0; k < 2; ++k) {
        rubraview_pal_rect_t b = ab_box_button(app, k);
        bool over = pal_rect_has(b, app->pointer_x, app->pointer_y);
        rubraview_pal_render_fill_rect(app->renderer, b, over ? COLOR_TILE_CURRENT : COLOR_TILE_FILL, 3.0);
        rubraview_pal_render_stroke_rect(app->renderer, b, COLOR_BOX_BORDER, 1.0, 3.0);
        rubraview_pal_render_draw_text(app->renderer, cstr(TAKE[k]), b, 14.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }

    rubraview_pal_rect_t title = { box.x + 12.0 * dpi, box.y + 4.0 * dpi, box.width - 60.0 * dpi, 26.0 * dpi };
    rubraview_pal_render_draw_text(app->renderer, TU8("Repeat from A to B  (hours, minutes, seconds)"), title, 14.0 * dpi,
                                   0xB0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
    /* Each point in three fields, the one in hand lit with a caret (owner, 2026-10-15). */
    bool caret_lit = fmod(rubraview_pal_time_now_seconds(), 1.0) < 0.5;
    static const char *const UNIT[3] = { "h", "m", "s" };
    for (int32_t side = 0; side < 2; ++side) {
        rubraview_pal_rect_t first = ab_part_rect(app, side, 0);
        rubraview_pal_rect_t label = { first.x - AB_LABEL_W * dpi, first.y, AB_LABEL_W * dpi, first.height };
        rubraview_pal_render_draw_text(app->renderer, side == 0 ? PROVEN_LIT("A") : PROVEN_LIT("B"), label, 18.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        for (int32_t part = 0; part < 3; ++part) {
            rubraview_pal_rect_t r = ab_part_rect(app, side, part);
            bool here = app->ab_edit_field == side && app->ab_edit_part == part;
            rubraview_pal_render_fill_rect(app->renderer, r, here ? 0xFF3A3A3Au : COLOR_TILE_FILL, 3.0);
            rubraview_pal_render_stroke_rect(app->renderer, r, here ? COLOR_TILE_CURRENT : COLOR_BOX_BORDER, here ? 2.0 : 1.0, 3.0);
            char shown[16];
            proven_u8str_view_t t = ab_part(app, side, part);
            int n = snprintf(shown, sizeof(shown), "%.*s%s", (int)t.size, t.ptr, here && caret_lit && !app->ab_edit_fresh ? "|" : "");
            if (n > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)shown, .size = (size_t)n }, r, 17.0 * dpi,
                                                      COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
            rubraview_pal_rect_t unit = { r.x + r.width, r.y, AB_UNIT_W * dpi, r.height };
            rubraview_pal_render_draw_text(app->renderer, cstr(UNIT[part]), unit, 13.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
        }
    }
    rubraview_pal_rect_t hint = { box.x, box.y + 118.0 * dpi, box.width, 34.0 * dpi };
    rubraview_pal_render_draw_text(app->renderer,
                                   TU8("Click a field or Tab  ·  : next field  ·  \\ empties  ·  Enter keeps  ·  Esc or X leaves"),
                                   hint, 13.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
}

void draw_px_edit_box(app_state_t *app, double win_w, double win_h, double dpi) {
    if (!app->px_edit_open) return;
    (void)win_w; (void)win_h;
    rubraview_pal_rect_t box = px_box_rect(app);
    rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BOX_FILL, 3.0);
    rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_BOX_BORDER, 1.0, 3.0);
    draw_answer_close(app, box, dpi);
    bool caret_lit = fmod(rubraview_pal_time_now_seconds(), 1.0) < 0.5;
    char line[160];
    if (app->px_edit_crop) {
        /* The crop: its left and top corner and its size, the field in hand in brackets. */
        static const char *const NAME[4] = { "X", "Y", "W", "H" };
        int at = 0;
        for (int32_t k = 0; k < 4 && at < (int)sizeof(line); ++k) {
            bool here = app->px_edit_field == k;
            int m = snprintf(line + at, sizeof(line) - (size_t)at, "%s%s %s%.*s%s", k == 0 ? "" : k == 2 ? "    " : "  ", NAME[k],
                             here ? "[" : " ", (int)app->px_edit_length[k], app->px_edit_text[k], here ? (caret_lit ? "|]" : " ]") : " ");
            if (m < 0) break;
            at += m;
        }
        if (at > (int)sizeof(line) - 1) at = (int)sizeof(line) - 1;
        rubraview_pal_rect_t top = { box.x, box.y + 10.0 * dpi, box.width, 34.0 * dpi };
        rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)at }, top, 18.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        rubraview_pal_rect_t hint = { box.x, box.y + 50.0 * dpi, box.width, 34.0 * dpi };
        rubraview_pal_render_draw_text(app->renderer, TU8("Pixels from the top left  ·  Tab next  ·  Enter cuts  ·  Esc  ·  or drag on the picture"),
                                       hint, 13.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
        return;
    }
    int n = snprintf(line, sizeof(line), T("Width %s%.*s%s  x  Height %s%.*s%s  px"),
                     app->px_edit_field == 0 ? "[" : " ", (int)app->px_edit_length[0], app->px_edit_text[0],
                     app->px_edit_field == 0 ? (caret_lit ? "|]" : " ]") : " ",
                     app->px_edit_field == 1 ? "[" : " ", (int)app->px_edit_length[1], app->px_edit_text[1],
                     app->px_edit_field == 1 ? (caret_lit ? "|]" : " ]") : " ");
    if (n > 0) {
        rubraview_pal_rect_t top = { box.x, box.y + 10.0 * dpi, box.width, 34.0 * dpi };
        rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n },
                                       top, 20.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
    rubraview_pal_rect_t hint = { box.x, box.y + 50.0 * dpi, box.width, 34.0 * dpi };
    rubraview_pal_render_draw_text(app->renderer,
                                   app->edit.resize_lock_aspect ? TU8("The other side follows the shape  ·  Tab swaps  ·  Enter keeps  ·  Esc")
                                                                : TU8("Tab swaps  ·  Enter keeps  ·  Esc leaves"),
                                   hint, 13.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
}

/* §3.18's confirmation, the text boxes and the notices sit above
   everything else: they are answers to something the reader just did. They
   used to be drawn first, under the floating boxes, and the toolbox's
   anchor covered the start of a name being typed (VM, 2026-09-25). */
void draw_chrome_answers(app_state_t *app, double win_w, double win_h) {
    double chrome_dpi = rubraview_pal_window_dpi_scale(app->window);
    /* The equaliser is worked by hand, so it sits over the floating boxes
       (VM, 2026-10-01: the toolbox showed through it). */
    draw_eqwin(app, win_w, win_h);
    if (app->confirm_restart) {   /* RFC-0006 */
        double box_w = 680.0 * chrome_dpi, box_h = 100.0 * chrome_dpi;
        if (box_w > win_w - 16.0) box_w = win_w - 16.0;
        rubraview_pal_rect_t box = { (win_w - box_w) * 0.5, (win_h - box_h) * 0.5, box_w, box_h };
        rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BOX_FILL, 3.0);
        rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_TILE_CURRENT, 2.0, 3.0);
        rubraview_pal_render_draw_text(app->renderer,
                                       TU8("Rubraview has used much of its memory. Restart it where you are?  (Y: restart now, N: later)"),
                                       box, 15.0 * chrome_dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
    if (app->confirm_purge) {
        double box_w = 520.0 * chrome_dpi, box_h = 90.0 * chrome_dpi;
        rubraview_pal_rect_t box = { (win_w - box_w) * 0.5, (win_h - box_h) * 0.5, box_w, box_h };
        rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BOX_FILL, 3.0);
        rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_CLOSE_HOVER, 2.0, 3.0);
        rubraview_pal_render_draw_text(app->renderer,
                                       TU8("Delete this file from the disk for good?  (Y / N)"),
                                       box, 16.0 * chrome_dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
    if (app->arc_ask == ARC_ASK_DELETE) {
        /* D-88 */
        double box_w = 760.0 * chrome_dpi, box_h = 110.0 * chrome_dpi;
        if (box_w > win_w - 16.0) box_w = win_w - 16.0;
        rubraview_pal_rect_t box = { (win_w - box_w) * 0.5, (win_h - box_h) * 0.5, box_w, box_h };
        rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BOX_FILL, 3.0);
        rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_CLOSE_HOVER, 2.0, 3.0);
        rubraview_pal_rect_t upper = { box.x, box.y + box_h * 0.12, box_w, box_h * 0.38 };
        rubraview_pal_rect_t lower = { box.x, box.y + box_h * 0.5, box_w, box_h * 0.38 };
        bool deleting = app->arc_ask == ARC_ASK_DELETE;
        rubraview_pal_render_draw_text(app->renderer,
                                       deleting ? TU8("Delete this page from the archive? It cannot be undone.")
                                                : TU8("This archive cannot be changed as it is. It is written again; the old file goes to the Recycle Bin."),
                                       upper, (deleting ? 16.0 : 13.5) * chrome_dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        rubraview_pal_render_draw_text(app->renderer,
                                       deleting ? TU8("Y  delete        N  keep")
                                                : TU8("Z  as ZIP (CBZ)        7  as 7z (CB7)        Esc  leave it"),
                                       lower, 15.0 * chrome_dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
    /* RV-137: a value typed, or the copies' name, asked for in the Edit window is drawn there */
    if (!(app->edit_window && edit_panel_open(app) && (app->rename_is_value || app->rename_is_apply))) draw_rename_box(app, win_w, win_h, chrome_dpi);
    draw_ab_edit_box(app, win_w, win_h, chrome_dpi);
    if (!(app->edit_window && edit_panel_open(app))) draw_px_edit_box(app, win_w, win_h, chrome_dpi);   /* RV-137: else in the Edit window */
    draw_notice(app, win_w, win_h, chrome_dpi);
}

/* The edge buttons (owner, 2026-09-29): the revealed side's three, an
   icon or a short sign each, and the hovered one's caption beside it. */
static void draw_edge_nav(app_state_t *app, double win_w, double win_h) {
    rubraview_edge_side_t side = edge_nav_side(app, app->pointer_x, app->pointer_y);
    if (side == RUBRAVIEW_EDGE_NONE) return;
    bool media = app->media != NULL;
    /* [media][side][slot]: an icon of Segoe MDL2 Assets, or 0 and the sign. */
    static const uint32_t ICON[2][2][RUBRAVIEW_EDGE_SLOTS] = {
        { { 0xE76B, 0, 0xE892 }, { 0xE76C, 0, 0xE893 } },
        { { 0, 0, 0xE892 },      { 0, 0, 0xE893 } },
    };
    static const char *const SIGN[2][2][RUBRAVIEW_EDGE_SLOTS] = {
        { { "<", "-10", "|<" }, { ">", "+10", ">|" } },
        { { "-5s", "-30s", "|<" }, { "+5s", "+30s", ">|" } },
    };
    rubraview_edgenav_t nav = rubraview_edgenav_create(rubraview_pal_window_dpi_scale(app->window));
    rubraview_edge_hit_t hit = rubraview_edgenav_hit(&nav, app->pointer_x, app->pointer_y, win_w, win_h);
    double opacity = box_opacity(app, &app->toolbox);
    for (int32_t slot = 0; slot < RUBRAVIEW_EDGE_SLOTS; ++slot) {
        rubraview_rect_t r = rubraview_edgenav_rect(&nav, side, slot, win_w, win_h);
        rubraview_pal_rect_t br = { r.x, r.y, r.width, r.height };
        bool hovered = hit.side == side && hit.slot == slot;
        rubraview_pal_render_fill_rect(app->renderer, br, rubraview_box_fade(hovered ? COLOR_TILE_FILL : COLOR_BOX_FILL, opacity), 4.0);
        rubraview_pal_render_stroke_rect(app->renderer, br, COLOR_BOX_BORDER, 1.0, 4.0);
        uint32_t icon = ICON[media][side][slot];
        if (!icon || !rubraview_pal_render_draw_icon(app->renderer, icon, br, r.height * 0.45, COLOR_TEXT)) {
            rubraview_pal_render_draw_text(app->renderer, cstr(SIGN[media][side][slot]), br, r.height * 0.32,
                                           COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
        if (hovered) {
            double cw = r.height * 4.0;
            rubraview_pal_rect_t cap = { side == RUBRAVIEW_EDGE_LEFT ? r.x + r.width + nav.gap : r.x - nav.gap - cw,
                                         r.y + r.height * 0.2, cw, r.height * 0.6 };
            rubraview_pal_render_fill_rect(app->renderer, cap, rubraview_box_fade(COLOR_BOX_FILL, opacity), 3.0);
            rubraview_pal_render_draw_text(app->renderer, cstr(rubraview_edgenav_caption(side, slot, media)), cap,
                                           r.height * 0.3, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
    }
}

/* The playlist window: a title saying what the list is and where the file
   on screen is in it, then a row a file, that one marked. See-through as
   the toolbox is. */
/* A button of the list window: lit under the pointer, or when `on`. */
/* A small button of a list, drawn by `r` with the pointer at (px, py) in its window. */
void list_button(rubraview_renderer_t *rr, double px, double py, rubraview_rect_t r, const char *label, double size,
                        bool on, uint32_t color) {
    if (r.width <= 0.0) return;
    rubraview_pal_rect_t box = { r.x + 1.0, r.y + 1.0, r.width - 2.0, r.height - 2.0 };
    if (on) rubraview_pal_render_fill_rect(rr, box, (COLOR_TILE_CURRENT & 0x00FFFFFFu) | 0xC0000000u, 2.0);
    else if (rubraview_rect_contains(r, px, py)) rubraview_pal_render_fill_rect(rr, box, COLOR_TILE_FILL, 2.0);
    rubraview_pal_render_draw_text(rr, cstr(label), box, size, color, RUBRAVIEW_TEXT_CENTER);
}

void listwin_button(app_state_t *app, rubraview_rect_t r, const char *label, double size, bool on, uint32_t color) {
    list_button(app->renderer, app->pointer_x, app->pointer_y, r, label, size, on, color);
}

void list_check_mark(rubraview_renderer_t *rr, rubraview_rect_t c, bool every, bool some) {
    double side = c.width < c.height ? c.width : c.height;
    double inset = side * 0.25;
    rubraview_pal_rect_t mark = { c.x + (c.width - side) * 0.5 + inset, c.y + (c.height - side) * 0.5 + inset,
                                  side - inset * 2.0, side - inset * 2.0 };
    if (every) rubraview_pal_render_fill_rect(rr, mark, 0xFF5B9BD5u, 2.0);
    else if (some) rubraview_pal_render_fill_rect(rr, (rubraview_pal_rect_t){ mark.x + mark.width * 0.2, mark.y + mark.height * 0.4,
                                                                            mark.width * 0.6, mark.height * 0.2 }, 0xFF5B9BD5u, 1.0);
    rubraview_pal_render_stroke_rect(rr, mark, COLOR_TEXT, 1.0, 2.0);
}

/* RV-117: what both lists have when they can be changed — the bar (the box
   for all, Add, Del, Save, Load, Rel/Abs) ... */
static void draw_list_bar(app_state_t *app, rubraview_renderer_t *rr, const rubraview_listwin_t *lw, double px, double py) {
    double small = lw->row_height * 0.5 * place_size(app, PF_LIST);   /* 1.25.0 */
    char del[40];
    size_t checked = list_checked_total(app);
    if (checked > 0) snprintf(del, sizeof(del), T("Del (%zu)"), checked); else snprintf(del, sizeof(del), T("Del"));
    list_check_mark(rr, rubraview_listwin_bar_rect(lw, RUBRAVIEW_LISTWIN_ALL),
                    app->list_checked_count > 0 && checked == app->list_checked_count, checked > 0);
    list_button(rr, px, py, rubraview_listwin_bar_rect(lw, RUBRAVIEW_LISTWIN_ADD), T("Add"), small, false, COLOR_TEXT);
    list_button(rr, px, py, rubraview_listwin_bar_rect(lw, RUBRAVIEW_LISTWIN_URL), T("URL"), small, false, COLOR_TEXT);   /* RV-158 */
    list_button(rr, px, py, rubraview_listwin_bar_rect(lw, RUBRAVIEW_LISTWIN_DEL), del, small, false, checked > 0 ? COLOR_TEXT : COLOR_BOX_BORDER);
    list_button(rr, px, py, rubraview_listwin_bar_rect(lw, RUBRAVIEW_LISTWIN_SAVE), T("Save"), small, false, COLOR_TEXT);
    list_button(rr, px, py, rubraview_listwin_bar_rect(lw, RUBRAVIEW_LISTWIN_LOAD), T("Load"), small, false, COLOR_TEXT);
    list_button(rr, px, py, rubraview_listwin_bar_rect(lw, RUBRAVIEW_LISTWIN_PATHS), list_saves_whole_paths(app) ? T("Abs") : T("Rel"),
                small, false, COLOR_TEXT);
    /* the owner (2026-10-18): Edit's steps onto all of them, as copies; lit while Edit has a step */
    list_button(rr, px, py, rubraview_listwin_bar_rect(lw, RUBRAVIEW_LISTWIN_APPLY), T("Edit to all"), small, false,
                edit_panel_open(app) && app->edit_steps.cursor > 0 ? COLOR_TEXT : COLOR_BOX_BORDER);
    double head_h = rubraview_listwin_head_height(lw);
    rubraview_pal_rect_t rule = { lw->x + 2.0, lw->y + head_h - 1.0, lw->width - 4.0, 1.0 };
    rubraview_pal_render_fill_rect(rr, rule, COLOR_BOX_BORDER, 0.0);
}

/* 1.25.0: ... the tools row under it: .ext, the filter (its words, a caret while typed in
   the file window, an X to clear it), the sort and the order ... */
static void draw_list_tools(app_state_t *app, rubraview_renderer_t *rr, const rubraview_listwin_t *lw, double px, double py, bool in_files) {
    if (!lw->editable || !lw->tools) return;
    double small = lw->row_height * 0.5 * place_size(app, PF_LIST);
    char ext[32];
    if (app->list_ext_off_n > 0) snprintf(ext, sizeof(ext), ".ext (-%zu)", app->list_ext_off_n); else snprintf(ext, sizeof(ext), ".ext");
    list_button(rr, px, py, rubraview_listwin_tool_rect(lw, RUBRAVIEW_LISTWIN_EXT), ext, small, app->list_ext_off_n > 0, COLOR_TEXT);
    rubraview_rect_t f = rubraview_listwin_tool_rect(lw, RUBRAVIEW_LISTWIN_FILTER);
    if (f.width > 0.0) {
        rubraview_pal_rect_t box = { f.x + 2.0, f.y + 2.0, f.width - 4.0, f.height - 4.0 };
        bool typing = in_files ? app->list_filter_in_files : (app->rename_active && app->rename_is_listfilter);
        rubraview_pal_render_fill_rect(rr, box, 0xFF161616u, 2.0);
        rubraview_pal_render_stroke_rect(rr, box, typing ? 0xFF5B9BD5u : COLOR_BOX_BORDER, 1.0, 2.0);
        char words[160];
        bool empty = app->list_filter[0] == '\0';
        snprintf(words, sizeof(words), "%s%s", empty && !typing ? T("Filter") : app->list_filter, typing && in_files ? "|" : "");
        rubraview_pal_rect_t inner = { box.x + 6.0, box.y, box.width - 12.0 - (empty ? 0.0 : box.height), box.height };
        rubraview_pal_render_draw_text(rr, cstr(words), inner, small, empty && !typing ? 0x90F0F0F0u : COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
        if (!empty) list_button(rr, px, py, (rubraview_rect_t){ f.x + f.width - f.height, f.y, f.height, f.height }, "\xC3\x97", small, false, COLOR_TEXT);
    }
    list_button(rr, px, py, rubraview_listwin_tool_rect(lw, RUBRAVIEW_LISTWIN_SORT), list_sort_word(app->list_sort_key), small,
                app->list_sort_key > 0, COLOR_TEXT);
    list_button(rr, px, py, rubraview_listwin_tool_rect(lw, RUBRAVIEW_LISTWIN_ORDER),
                app->list_sort_desc ? "\xE2\x96\xBC" : "\xE2\x96\xB2", small, false, COLOR_TEXT);
}

/* ... the four buttons at a row's left ... */
static void draw_list_row_parts(app_state_t *app, rubraview_renderer_t *rr, const rubraview_listwin_t *lw, size_t i, size_t count,
                                double px, double py, size_t page) {
    double small = lw->row_height * 0.5 * place_size(app, PF_LIST);   /* 1.25.0 */
    bool checked = page < app->list_checked_count && app->list_checked[page];
    bool moves = !list_viewed(app);   /* 1.25.0: a filtered list's rows stay */
    list_check_mark(rr, rubraview_listwin_row_part_rect(lw, i, count, RUBRAVIEW_LISTWIN_ROW_CHECK), checked, false);
    list_button(rr, px, py, rubraview_listwin_row_part_rect(lw, i, count, RUBRAVIEW_LISTWIN_ROW_REMOVE), "x", small, false, COLOR_TEXT);
    list_button(rr, px, py, rubraview_listwin_row_part_rect(lw, i, count, RUBRAVIEW_LISTWIN_ROW_UP), "\xE2\x96\xB2", small * 0.8, false,
                moves && i > 0 ? COLOR_TEXT : COLOR_BOX_BORDER);
    list_button(rr, px, py, rubraview_listwin_row_part_rect(lw, i, count, RUBRAVIEW_LISTWIN_ROW_DOWN), "\xE2\x96\xBC", small * 0.8, false,
                moves && i + 1 < count ? COLOR_TEXT : COLOR_BOX_BORDER);
}

/* ... and, while a row is carried, a line where it would go. */
static void draw_list_drop_line(app_state_t *app, rubraview_renderer_t *rr, const rubraview_listwin_t *lw, bool in_files) {
    size_t rows = rubraview_listwin_rows_visible(lw);
    if (!app->list_row_dragging || app->list_held_in_files != in_files) return;
    if (app->list_drop_at < lw->first || app->list_drop_at > lw->first + rows) return;
    rubraview_pal_rect_t line = { lw->x + 4.0, lw->y + rubraview_listwin_head_height(lw) + (double)(app->list_drop_at - lw->first) * lw->row_height - 1.0,
                                  lw->width - 8.0, 2.0 };
    rubraview_pal_render_fill_rect(rr, line, 0xFF5B9BD5u, 0.0);
}

/* 1.25.0 (owner, 2026-10-20: "재생목록 위에 커서를 대면 해당 파일의 썸네일이 보이게"): the row pointed at,
   as a picture beside the list — the page itself when it is in memory, else its strip thumbnail. */
static void draw_list_tip(app_state_t *app, const rubraview_listwin_t *lw, size_t page, double win_w, double win_h) {
    app->list_tip_page = page < page_count(app) ? (int32_t)page : -1;
    if (app->list_tip_page < 0) return;
    rubraview_texture_t *tex = NULL;
    if (app->pages && app->pages[page].texture && app->pages[page].loaded) tex = app->pages[page].texture;
    else if (page < app->strip_cell_count && app->strip_cells[page].texture) tex = app->strip_cells[page].texture;
    if (!tex) return;
    int32_t tw = 0, th = 0;
    rubraview_pal_texture_size(tex, &tw, &th);
    if (tw <= 0 || th <= 0) return;
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double side = 220.0 * dpi, k = side / (double)(tw > th ? tw : th);
    double w = (double)tw * k, h = (double)th * k;
    double x = lw->x - w - 10.0 * dpi;
    if (x < 4.0 * dpi) x = lw->x + lw->width + 10.0 * dpi;
    if (x + w > win_w) x = win_w - w - 4.0 * dpi;
    double y = app->pointer_y - h * 0.5;
    if (y + h > win_h - 4.0 * dpi) y = win_h - h - 4.0 * dpi;
    if (y < 4.0 * dpi) y = 4.0 * dpi;
    rubraview_pal_rect_t frame = { x - 3.0 * dpi, y - 3.0 * dpi, w + 6.0 * dpi, h + 6.0 * dpi };
    rubraview_pal_render_fill_rect(app->renderer, frame, 0xF0202020u, 4.0 * dpi);
    rubraview_pal_render_stroke_rect(app->renderer, frame, COLOR_BOX_BORDER, 1.0, 4.0 * dpi);
    rubraview_mat3x2_t place = rubraview_mat3x2_multiply(rubraview_mat3x2_scale(w / (double)tw, h / (double)th), rubraview_mat3x2_translate(x, y));
    rubraview_pal_render_draw_texture(app->renderer, tex, place, RUBRAVIEW_INTERP_LINEAR);
}

/* 1.25.0: in its place's face (Settings › General) */
static void draw_listwin_in(app_state_t *app, double win_w, double win_h);
static void draw_listwin(app_state_t *app, double win_w, double win_h) {
    int before = place_font_begin(app, PF_LIST, NULL);
    draw_listwin_in(app, win_w, win_h);
    place_font_end(app, before);
}

static void draw_listwin_in(app_state_t *app, double win_w, double win_h) {
    rubraview_listwin_t *lw = &app->listwin;
    app->list_tip_page = -1;
    if (!lw->open) return;
    listwin_sync(app);
    size_t count = list_rows(app);   /* 1.25.0: the rows shown */
    listwin_place_clear(app, win_w, win_h);
    int32_t current = current_page_index(app);
    if (current != app->listwin_shown_page && current >= 0) {   /* follow the file on screen */
        size_t at = list_row_of(app, (size_t)current);
        if (at != SIZE_MAX) rubraview_listwin_reveal(lw, at, count);
        app->listwin_shown_page = current;
    }
    double opacity = box_opacity(app, &app->toolbox);
    rubraview_pal_rect_t body = { lw->x, lw->y, lw->width, lw->height };
    /* See-through (owner: "반투명 플로팅"): the picture shows under the list. */
    rubraview_pal_render_fill_rect(app->renderer, body, rubraview_box_fade(COLOR_BOX_FILL, opacity * 0.75), 4.0);
    rubraview_pal_render_stroke_rect(app->renderer, body, COLOR_BOX_BORDER, 1.0, 4.0);

    const char *what = app->list_is_set ? T("Playlist") : app->source.archive_path.size > 0 ? T("In this archive") : T("In this folder");
    char title[96];
    int n = current >= 0 ? snprintf(title, sizeof(title), "%s   %d / %zu", what, current + 1, page_count(app))
                         : snprintf(title, sizeof(title), "%s   %zu", what, page_count(app));
    if (n > 0 && list_viewed(app) && (size_t)n < sizeof(title))
        n += snprintf(title + n, sizeof(title) - (size_t)n, T("   (%zu shown)"), count);
    if (n >= (int)sizeof(title)) n = (int)sizeof(title) - 1;
    double buttons = lw->title_height * (lw->editable ? 2.0 : 1.0);
    rubraview_pal_rect_t head = { lw->x + lw->row_height * 0.4, lw->y, lw->width - buttons - lw->row_height * 0.4, lw->title_height };
    if (n > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ (const proven_byte_t *)title, (size_t)n }, head, lw->title_height * 0.45,
                                              COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    rubraview_rect_t cr = rubraview_listwin_close_rect(lw);
    rubraview_pal_rect_t close = { cr.x, cr.y, cr.width, cr.height };
    if (rubraview_rect_contains(cr, app->pointer_x, app->pointer_y)) {
        rubraview_pal_render_fill_rect(app->renderer, close, COLOR_CLOSE_HOVER, 4.0);
    }
    rubraview_pal_render_draw_text(app->renderer, PROVEN_LIT("X"), close, lw->title_height * 0.45, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);

    double head_h = rubraview_listwin_head_height(lw);
    double small = lw->row_height * 0.5 * place_size(app, PF_LIST);   /* 1.25.0 */
    if (lw->editable) {
        /* D-111: R sizes the window when dragged; the bar acts on the list. */
        listwin_button(app, rubraview_listwin_resize_rect(lw), "R", lw->title_height * 0.45, lw->resizing, COLOR_TEXT);
        draw_list_bar(app, app->renderer, lw, app->pointer_x, app->pointer_y);
        draw_list_tools(app, app->renderer, lw, app->pointer_x, app->pointer_y, false);
    }

    size_t hovered = SIZE_MAX, index = 0;
    rubraview_listwin_part_t under = rubraview_listwin_hit(lw, app->pointer_x, app->pointer_y, count, &index);
    if (under == RUBRAVIEW_LISTWIN_ROW || (under >= RUBRAVIEW_LISTWIN_ROW_CHECK && under <= RUBRAVIEW_LISTWIN_ROW_DOWN)) hovered = index;
    size_t rows = rubraview_listwin_rows_visible(lw);
    double text_x = rubraview_listwin_row_text_x(lw);
    for (size_t i = lw->first; i < count && i < lw->first + rows; ++i) {
        size_t page = list_page(app, i);
        if (page == SIZE_MAX) break;
        rubraview_rect_t rr = rubraview_listwin_row_rect(lw, i, count);
        rubraview_pal_rect_t row = { rr.x + 2.0, rr.y, rr.width - 4.0, rr.height };
        bool checked = page < app->list_checked_count && app->list_checked[page];
        bool carried = app->list_row_dragging && (page == app->list_press_row ||
                       (checked && app->list_press_row < app->list_checked_count && app->list_checked[app->list_press_row]));
        if ((int32_t)page == current) rubraview_pal_render_fill_rect(app->renderer, row, (COLOR_TILE_CURRENT & 0x00FFFFFFu) | 0x90000000u, 2.0);
        else if (carried) rubraview_pal_render_fill_rect(app->renderer, row, 0x405B9BD5u, 2.0);
        else if (i == hovered) rubraview_pal_render_fill_rect(app->renderer, row, COLOR_TILE_FILL, 2.0);
        if (lw->editable) draw_list_row_parts(app, app->renderer, lw, i, count, app->pointer_x, app->pointer_y, page);
        char line[512], fit[512];
        proven_u8str_view_t name = page_display_name(app, page);
        int m = snprintf(line, sizeof(line), "%4zu   %.*s", page + 1, (int)(name.size < 480 ? name.size : 480), name.ptr);
        rubraview_pal_rect_t text = { text_x + lw->row_height * 0.3, row.y, lw->x + lw->width - text_x - lw->row_height * 0.6, row.height };
        if (m > 0) rubraview_pal_render_draw_text(app->renderer,
                                                  files_fit_text(fit, sizeof(fit), (proven_u8str_view_t){ (const proven_byte_t *)line, (size_t)(m < (int)sizeof(line) ? m : (int)sizeof(line) - 1) },
                                                                 text.width, small),
                                                  text, small, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    }
    draw_list_drop_line(app, app->renderer, lw, false);   /* a row being carried */
    /* A thin bar says where in a long list the rows are. */
    if (count > rows && rows > 0) {
        double track = lw->height - head_h;
        double h = track * (double)rows / (double)count;
        if (h < lw->row_height * 0.5) h = lw->row_height * 0.5;
        double y = lw->y + head_h + (track - h) * (double)lw->first / (double)(count - rows);
        rubraview_pal_rect_t bar = { lw->x + lw->width - 4.0, y, 3.0, h };
        rubraview_pal_render_fill_rect(app->renderer, bar, COLOR_BOX_BORDER | 0x60000000u, 1.5);
    }
    if (hovered != SIZE_MAX && !app->list_row_dragging && !app->chooser.open) draw_list_tip(app, lw, list_page(app, hovered), win_w, win_h);
}

static void draw_eqwin(app_state_t *app, double win_w, double win_h) {
    rubraview_eqwin_t *w = &app->eqwin;
    if (!w->open) return;
    rubraview_eqwin_place(w, win_w, win_h);
    rubraview_pal_rect_t body = { w->x, w->y, w->width, w->height };
    rubraview_pal_render_fill_rect(app->renderer, body, (COLOR_BOX_FILL & 0x00FFFFFFu) | 0xF0000000u, 4.0);
    rubraview_pal_render_stroke_rect(app->renderer, body, COLOR_BOX_BORDER, 1.0, 4.0);
    double pad = w->label_height * 0.5;

    int preset = eq_preset_now(app);
    proven_u8str_view_t name = eq_preset_label(preset);
    char title[96];
    int n = snprintf(title, sizeof(title), T("Equaliser   %.*s"), (int)name.size, name.ptr);
    rubraview_pal_rect_t head = { w->x + pad, w->y, w->width - w->title_height - pad, w->title_height };
    if (n > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ (const proven_byte_t *)title, (size_t)n }, head, w->title_height * 0.45,
                                              COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    rubraview_rect_t cr = rubraview_eqwin_close_rect(w);
    rubraview_pal_rect_t close = { cr.x, cr.y, cr.width, cr.height };
    if (rubraview_rect_contains(cr, app->pointer_x, app->pointer_y)) {
        rubraview_pal_render_fill_rect(app->renderer, close, COLOR_CLOSE_HOVER, 4.0);
    }
    rubraview_pal_render_draw_text(app->renderer, PROVEN_LIT("X"), close, w->title_height * 0.45, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);

    for (int p = 0; p < RUBRAVIEW_EQWIN_PRESETS; ++p) {
        rubraview_rect_t r = rubraview_eqwin_preset_rect(w, (size_t)p);
        rubraview_pal_rect_t chip = { r.x, r.y, r.width, r.height };
        bool hover = rubraview_rect_contains(r, app->pointer_x, app->pointer_y);
        uint32_t fill = p == preset ? ((COLOR_TILE_CURRENT & 0x00FFFFFFu) | 0xC0000000u) : hover ? COLOR_TILE_FILL : 0x40202020u;
        rubraview_pal_render_fill_rect(app->renderer, chip, fill, 3.0);
        rubraview_pal_render_draw_text(app->renderer, eq_preset_label(p), chip, w->preset_height * 0.36,
                                       COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }

    float gains[RUBRAVIEW_EQ_BANDS];
    eq_shown_gains(app, gains);
    const double *freqs = rubraview_eq_frequencies();
    for (size_t b = 0; b < RUBRAVIEW_EQ_BANDS; ++b) {
        rubraview_rect_t col = rubraview_eqwin_band_rect(w, b);
        rubraview_rect_t t = rubraview_eqwin_track_rect(w, b);
        double cx = t.x + t.width * 0.5;
        /* the track, the 0 dB line across it, and the knob */
        rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ cx - 2.0, t.y, 4.0, t.height }, 0x80A0A0A0u, 2.0);
        double zero = rubraview_eqwin_y_of(w, b, 0.0);
        rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ cx - t.width * 0.25, zero - 0.5, t.width * 0.5, 1.0 }, 0x60F0F0F0u, 0.0);
        double ky = rubraview_eqwin_y_of(w, b, gains[b]);
        double kw = t.width * 0.5, kh = w->label_height * 0.5;
        bool held = app->eqwin.sliding == (int)b;
        bool hover = rubraview_rect_contains(col, app->pointer_x, app->pointer_y);
        uint32_t knob = held ? COLOR_TILE_CURRENT : hover ? 0xFFFFFFFFu : 0xFFD0D0D0u;
        rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ cx - kw * 0.5, ky - kh * 0.5, kw, kh }, knob, 3.0);
        char v[16];
        int m = snprintf(v, sizeof(v), "%+.1f", (double)gains[b]);
        rubraview_pal_rect_t top = { col.x, col.y, col.width, w->label_height };
        if (m > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ (const proven_byte_t *)v, (size_t)m }, top, w->label_height * 0.55,
                                                  COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        char f[16];
        m = freqs[b] >= 1000.0 ? snprintf(f, sizeof(f), "%gk", freqs[b] / 1000.0) : snprintf(f, sizeof(f), "%g", freqs[b]);
        rubraview_pal_rect_t bottom = { col.x, col.y + col.height - w->label_height, col.width, w->label_height };
        if (m > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ (const proven_byte_t *)f, (size_t)m }, bottom, w->label_height * 0.55,
                                                  0xC0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
    }
}

/* Hover titlebar (§3.21.2, §3.21.3). Over the viewer it names the file;
   over the picker, the folder. */
/* 1.25.0 (owner, 2026-10-20: "UI에 쓰이는 여러 글꼴의 종류와 크기 등을 설정"): the face and size of a place's words,
   from Settings › General. begin sets the place's face and gives its size (a factor); end puts back the one before. */
static int g_place = PF_NONE;
static void place_font_apply(app_state_t *app, int which) {
    proven_u8str_view_t f = which == PF_TITLE ? rubraview_settings_get_text(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("font_titlebar"))
              : which == PF_MENU ? rubraview_settings_get_text(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("font_menu"))
              : which == PF_NAME ? rubraview_settings_get_text(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("font_name"))
              : which == PF_LIST ? rubraview_settings_get_text(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("font_list"))
              : which == PF_BUTTONS ? rubraview_settings_get_text(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("font_buttons")) : PROVEN_LIT("");
    rubraview_font_spec_t face = rubraview_font_spec_parse(f);
    rubraview_pal_render_set_text_font(app->renderer, face.family, face.weight, face.italic);
}

double place_size(app_state_t *app, int which) {
    double v = which == PF_TITLE ? rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("size_titlebar"))
             : which == PF_MENU ? rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("size_menu"))
             : which == PF_LIST ? rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("size_list"))
             : which == PF_BUTTONS ? rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("size_buttons")) : 100.0;
    if (which == PF_NAME) return name_scale(app);
    return v >= 60.0 && v <= 200.0 ? v / 100.0 : 1.0;
}

int place_font_begin(app_state_t *app, int which, double *size) {
    int before = g_place;
    g_place = which;
    place_font_apply(app, which);
    if (size) *size = place_size(app, which);
    return before;
}

void place_font_end(app_state_t *app, int before) {
    g_place = before;
    place_font_apply(app, before);
}

/* 1.25.0: words drawn bold (twice, a hair apart) for what is on, plain for what is off. */
void draw_text_weight(app_state_t *app, proven_u8str_view_t text, rubraview_pal_rect_t r, double size, uint32_t argb, bool bold) {
    rubraview_pal_render_draw_text(app->renderer, text, r, size, argb, RUBRAVIEW_TEXT_CENTER);
    if (bold) rubraview_pal_render_draw_text(app->renderer, text, (rubraview_pal_rect_t){ r.x + size * 0.06, r.y, r.width, r.height },
                                             size, argb, RUBRAVIEW_TEXT_CENTER);
}

/* ---- 1.25.0: the title bar's ☰ pull-down (owner, 2026-10-20: "타이틀바 고정(Pin), 정렬align, 새 창 열기,
   파일 열기, 최근 파일, 일괄 처리, 인쇄, 재생목록Playlist, 설정Settings, 도움말Help") ---- */
static const struct { const char *words, *action; } TMENU[] = {
    { N_("Pin the title bar"), "toggle_titlebar_pin" },
    { N_("Align the Floating Widget"), "align_widget" },
    { N_("New window"), "new_window" },
    { N_("Open file"), "open_picker" },
    { N_("Recent files..."), "open_recent_list" },
    { N_("Batch"), "open_batch" },
    { N_("Print..."), "print" },
    { N_("Playlist"), "toggle_playlist" },
    { N_("Settings"), "open_settings" },
    { N_("Help"), "toggle_help" },
};
#define TMENU_COUNT ((int32_t)(sizeof(TMENU) / sizeof(TMENU[0])))

static rubraview_pal_rect_t tmenu_row(const app_state_t *app, int32_t i) {
    double dpi = rubraview_pal_window_dpi_scale(app->window), rh = 38.0 * dpi;
    return (rubraview_pal_rect_t){ 4.0 * dpi, app->titlebar.height + 4.0 * dpi + (double)i * rh, 280.0 * dpi, rh };
}

static rubraview_pal_rect_t tmenu_frame(const app_state_t *app) {
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    rubraview_pal_rect_t first = tmenu_row(app, 0);
    return (rubraview_pal_rect_t){ 0.0, app->titlebar.height, first.width + 8.0 * dpi, first.height * (double)TMENU_COUNT + 8.0 * dpi };
}

static void draw_tmenu_in(app_state_t *app);
static void draw_tmenu(app_state_t *app, double win_w) {
    (void)win_w;
    if (!app->tmenu_open) return;
    int before = place_font_begin(app, PF_MENU, NULL);   /* 1.25.0: in the menus' face */
    draw_tmenu_in(app);
    place_font_end(app, before);
}

static void draw_tmenu_in(app_state_t *app) {
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    rubraview_pal_rect_t f = tmenu_frame(app);
    rubraview_pal_render_fill_rect(app->renderer, f, 0xF6202020u, 4.0 * dpi);
    rubraview_pal_render_stroke_rect(app->renderer, f, COLOR_BOX_BORDER, 1.0, 4.0 * dpi);
    bool pinned = rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("titlebar_pinned")) > 0.5;
    for (int32_t i = 0; i < TMENU_COUNT; ++i) {
        rubraview_pal_rect_t r = tmenu_row(app, i);
        bool on = i == 0 && pinned;
        if (on) rubraview_pal_render_fill_rect(app->renderer, r, TITLEBAR_ON, 3.0 * dpi);
        if (pal_rect_has(r, app->pointer_x, app->pointer_y) || i == app->tmenu_focus) rubraview_pal_render_fill_rect(app->renderer, r, 0x40FFFFFFu, 3.0 * dpi);
        char line[96];
        snprintf(line, sizeof(line), "%s%s", rubraview_tr(TMENU[i].words), on ? "  \xE2\x9C\x93" : "");
        rubraview_pal_rect_t words = { r.x + 14.0 * dpi, r.y, r.width - 20.0 * dpi, r.height };
        rubraview_pal_render_draw_text(app->renderer, cstr(line), words, 15.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
        if (on) rubraview_pal_render_draw_text(app->renderer, cstr(line), (rubraview_pal_rect_t){ words.x + 0.9, words.y, words.width, words.height },
                                               15.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    }
}

/* A press while it is open: a row does its action; anywhere else closes it (the title bar's ☰ toggles it). */
static bool tmenu_press(app_state_t *app, double x, double y) {
    if (!app->tmenu_open) return false;
    if (rubraview_titlebar_hit(&app->titlebar, x, y, (double)tmenu_win_w(app)) == RUBRAVIEW_TITLEBAR_MENU) return false;
    for (int32_t i = 0; i < TMENU_COUNT; ++i) {
        if (!pal_rect_has(tmenu_row(app, i), x, y)) continue;
        app->tmenu_open = false;
        handle_action(app, cstr(TMENU[i].action));
        return true;
    }
    app->tmenu_open = false;
    return pal_rect_has(tmenu_frame(app), x, y);
}

static bool tmenu_key(app_state_t *app, rubraview_key_combo_t combo) {
    if (!app->tmenu_open) return false;
    if (key_is(combo, "Escape")) { app->tmenu_open = false; return true; }
    if (key_is(combo, "Down")) { app->tmenu_focus = (app->tmenu_focus + 1) % TMENU_COUNT; return true; }
    if (key_is(combo, "Up")) { app->tmenu_focus = (app->tmenu_focus + TMENU_COUNT - 1) % TMENU_COUNT; return true; }
    if (key_is(combo, "Enter") && app->tmenu_focus >= 0) { app->tmenu_open = false; handle_action(app, cstr(TMENU[app->tmenu_focus].action)); return true; }
    app->tmenu_open = false;
    return false;
}

void draw_titlebar(app_state_t *app, double win_w) {
    if (!app->titlebar.shown) return;
    rubraview_pal_rect_t bar = { 0.0, 0.0, win_w, app->titlebar.height };
    rubraview_pal_render_fill_rect(app->renderer, bar, rubraview_box_fade(COLOR_BAR_FILL, rubraview_settings_get(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("titlebar_opacity"))), 0.0);

    int32_t page = current_page_index(app);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double bh = app->titlebar.height;
    /* 1.25.0 (owner, 2026-10-20): ☰ at the left end — three lines; lit while its pull-down is open */
    rubraview_rect_t mr = rubraview_titlebar_button_rect(&app->titlebar, RUBRAVIEW_TITLEBAR_MENU, win_w);
    {
        rubraview_pal_rect_t mb = { mr.x, mr.y, mr.width, mr.height };
        if (app->tmenu_open) rubraview_pal_render_fill_rect(app->renderer, mb, TITLEBAR_ON, 0.0);
        else if (rubraview_rect_contains(mr, app->pointer_x, app->pointer_y)) rubraview_pal_render_fill_rect(app->renderer, mb, COLOR_TILE_FILL, 0.0);
        double lw = mr.width * 0.42, lx = mr.x + (mr.width - lw) * 0.5, th = fmax(1.5, bh * 0.06);
        for (int k = -1; k <= 1; ++k)
            rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ lx, mr.y + bh * 0.5 + (double)k * bh * 0.17 - th * 0.5, lw, th }, COLOR_TEXT, th * 0.5);
    }
    /* the name: the folder in the open-file screen, else the file on screen; a press opens the open-file screen */
    proven_u8str_view_t name = app->picker_open ? (app->picker_dir.size > 0 ? app->picker_dir : PROVEN_LIT("Rubraview"))
                 : page >= 0 && (size_t)page < page_count(app) ? page_display_name(app, (size_t)page) : PROVEN_LIT("");
    double tk = 1.0;
    int before_font = place_font_begin(app, PF_TITLE, &tk);   /* 1.25.0: the title bar's name in its own face and size */
    double font = bh * 0.38 * tk, name_w = 0.0;
    if (name.size > 0 && rubraview_pal_render_measure_text(app->renderer, name, font, &name_w)) app->titlebar.name_width = name_w + 20.0 * dpi;
    else app->titlebar.name_width = 0.0;
    rubraview_rect_t nr = rubraview_titlebar_button_rect(&app->titlebar, RUBRAVIEW_TITLEBAR_NAME, win_w);
    if (nr.width > 0.0) {
        rubraview_pal_rect_t nb = { nr.x, nr.y + 4.0 * dpi, nr.width, nr.height - 8.0 * dpi };
        if (rubraview_rect_contains(nr, app->pointer_x, app->pointer_y) && !app->picker_open)
            rubraview_pal_render_fill_rect(app->renderer, nb, COLOR_TILE_FILL, 3.0 * dpi);
        char fit[1024];
        /* measured to fit: whole (the estimate files_fit_text makes would cut it); cut only when the row is short */
        proven_u8str_view_t shown = nr.width + 0.5 >= name_w + 20.0 * dpi ? name : files_fit_text(fit, sizeof(fit), name, nr.width - 20.0 * dpi, font);
        rubraview_pal_render_draw_text(app->renderer, shown,
                                       (rubraview_pal_rect_t){ nr.x + 10.0 * dpi, nr.y, nr.width - 10.0 * dpi, nr.height }, font, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    }
    place_font_end(app, before_font);

    /* The right-hand row: Set, P, H, T, F, _, [], R, X; what is on is lit and bold (owner, 2026-10-20) */
    static const rubraview_titlebar_button_t BUTTONS[] = {
        RUBRAVIEW_TITLEBAR_SETTINGS, RUBRAVIEW_TITLEBAR_BARPIN, RUBRAVIEW_TITLEBAR_HIDE, RUBRAVIEW_TITLEBAR_PIN,
        RUBRAVIEW_TITLEBAR_FULLSCREEN, RUBRAVIEW_TITLEBAR_MINIMIZE, RUBRAVIEW_TITLEBAR_MAXIMIZE, RUBRAVIEW_TITLEBAR_RESIZE,
        RUBRAVIEW_TITLEBAR_CLOSE,
    };
    const char *const GLYPHS[] = { T("Set"), "P", "H", "T", "F", "_", "[]", "R", "X" };
    bool on[9] = { app->settings_open,
                   rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("titlebar_pinned")) > 0.5,
                   app->chrome_hidden,
                   rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("always_on_top")) > 0.5,
                   rubraview_pal_window_is_fullscreen(app->window),
                   false,
                   rubraview_pal_window_is_maximized(app->window),
                   false, false };
    double bk = 1.0;
    int before_buttons = place_font_begin(app, PF_BUTTONS, &bk);
    for (size_t i = 0; i < sizeof(BUTTONS) / sizeof(BUTTONS[0]); ++i) {
        rubraview_rect_t r = rubraview_titlebar_button_rect(&app->titlebar, BUTTONS[i], win_w);
        rubraview_pal_rect_t br = { r.x, r.y, r.width, r.height };
        bool hovered = rubraview_rect_contains(r, app->pointer_x, app->pointer_y);
        if (on[i]) rubraview_pal_render_fill_rect(app->renderer, br, TITLEBAR_ON, 0.0);
        if (hovered) rubraview_pal_render_fill_rect(app->renderer, br, BUTTONS[i] == RUBRAVIEW_TITLEBAR_CLOSE ? COLOR_CLOSE_HOVER : COLOR_TILE_FILL, 0.0);
        draw_text_weight(app, cstr(GLYPHS[i]), br, bh * 0.36 * bk, on[i] ? COLOR_TEXT : 0xC8F0F0F0u, on[i]);
    }
    place_font_end(app, before_buttons);
    draw_tmenu(app, win_w);
}

/* The filmstrip (F4, or the wheel's for a while): thumbnails from its own thread, else the
   page if it is in memory, else the page's number; the page on screen framed. */
static void draw_wheel_strip(app_state_t *app) {
    double shown = page_count(app) > 0 ? strip_shown(app) : 0.0;
    if (shown > 0.0) {
        /* The thumbnails, the page on screen framed; the wheel's strip fades as it goes (owner, 2026-10-15). */
        double dpi = rubraview_pal_window_dpi_scale(app->window);
        rubraview_pal_rect_t strip = strip_rect(app);
        rubraview_pal_render_fill_rect(app->renderer, strip, strip_fade(rubraview_box_fade(COLOR_BAR_FILL,
                                       rubraview_settings_get(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("strip_opacity"))), shown), 0.0);   /* 1.25.0 */
        double lead = rubraview_filmstrip_lead(&app->filmstrip);
        int32_t current = current_page_index(app);
        rubraview_virtual_range_t visible = rubraview_filmstrip_visible(&app->filmstrip);
        for (size_t i = 0; i < visible.count; ++i) {
            size_t index = visible.first + i;
            if (index >= page_count(app)) break;
            double x = lead + (double)index * app->filmstrip.thumb_extent - app->filmstrip.scroll_offset;
            rubraview_pal_rect_t cell = { x + 4.0 * dpi, strip.y + 6.0 * dpi,
                                          app->filmstrip.thumb_extent - 8.0 * dpi, strip.height - 12.0 * dpi };
            rubraview_texture_t *tex = NULL;
            double tw = 0.0, th = 0.0;
            if (index < app->strip_cell_count && app->strip_cells[index].texture) {
                tex = app->strip_cells[index].texture;
                int32_t w = 0, h = 0;
                rubraview_pal_texture_size(tex, &w, &h);
                tw = (double)w; th = (double)h;
            } else if (app->pages[index].loaded && app->pages[index].texture) {   /* the page read already */
                tex = app->pages[index].texture;
                tw = (double)app->pages[index].width; th = (double)app->pages[index].height;
            }
            if (tex && tw > 0.0 && th > 0.0) {
                double s = fmin(cell.width / tw, cell.height / th);   /* whole, its own shape */
                rubraview_mat3x2_t fit = rubraview_mat3x2_multiply(
                    rubraview_mat3x2_scale(s, s),
                    rubraview_mat3x2_translate(cell.x + (cell.width - tw * s) * 0.5, cell.y + (cell.height - th * s) * 0.5));
                rubraview_pal_render_draw_texture_opacity(app->renderer, tex, fit, RUBRAVIEW_INTERP_LINEAR, shown);
            } else {
                char number[24];
                int n = snprintf(number, sizeof(number), "%zu", index + 1);
                rubraview_pal_render_fill_rect(app->renderer, cell, strip_fade(COLOR_TILE_FILL, shown), 3.0);
                if (n > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)number, .size = (size_t)n }, cell,
                                                          14.0 * dpi, strip_fade(COLOR_TEXT, shown), RUBRAVIEW_TEXT_CENTER);
            }
            bool here = (int32_t)index == current;
            bool over = pal_rect_has(cell, app->pointer_x, app->pointer_y);
            if (here || over) {
                rubraview_pal_render_stroke_rect(app->renderer, cell, strip_fade(here ? COLOR_TILE_CURRENT : COLOR_BOX_BORDER, shown),
                                                 here ? 3.0 * dpi : 1.5 * dpi, 3.0);
            }
        }
    }

}

void draw_chrome(app_state_t *app, double win_w, double win_h) {
    rubraview_tile_metrics_t metrics = tile_metrics(rubraview_pal_window_dpi_scale(app->window));

    /* Filmstrip (§3.1): under the boxes, over the picture. */
    draw_wheel_strip(app);

    draw_edge_nav(app, win_w, win_h);

    /* Floating boxes (§3.6); none while they are hidden (D-110). */
    toolbox_refresh(app);
    if (!app->chrome_hidden && app->toolbox.state != RUBRAVIEW_BOX_DETACHED) draw_box(app, &app->toolbox, &metrics, NULL, 0, NULL);
    /* 1.21.0: the menu box is not drawn; its tree is the right click's lower rows */

    if (!app->chrome_hidden && app->menubox.state != RUBRAVIEW_BOX_COLLAPSED && !app->menubox.headless) {
        char crumb[128];
        proven_u8str_view_t text = rubraview_menu_breadcrumb(&app->menu, crumb, sizeof(crumb));
        /* Beside the anchor, on its row: the grid is below or above it now. */
        rubraview_rect_t anchor = rubraview_box_anchor_rect(&app->menubox, &metrics);
        rubraview_rect_t pin = rubraview_box_pin_rect(&app->menubox, &metrics);
        rubraview_rect_t grip = rubraview_box_grip_rect(&app->menubox, &metrics);
        double after = anchor.x + anchor.width;   /* past the pin and the grip, when they are on the right */
        if (pin.x > anchor.x && pin.x + pin.width > after) after = pin.x + pin.width;
        if (grip.width > 0.0 && grip.x > anchor.x && grip.x + grip.width > after) after = grip.x + grip.width;
        rubraview_pal_rect_t label = { after + metrics.gutter, anchor.y,
                                       metrics.tile_size * 6.0, anchor.height };
        rubraview_pal_render_draw_text(app->renderer, text, label, metrics.tile_size * 0.2,
                                       COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    }

    draw_listwin(app, win_w, win_h);
    draw_recentwin(app, win_w, win_h);
    draw_favwin(app, win_w, win_h);   /* 1.24.0 */

    draw_titlebar(app, win_w);

    /* §3.17.1: an unobtrusive prompt offering the remembered page. */
    if (app->resume_offer) {
        double bar_h = 40.0 * rubraview_pal_window_dpi_scale(app->window);
        rubraview_pal_rect_t prompt = { win_w * 0.25, win_h * 0.5 - bar_h * 0.5, win_w * 0.5, bar_h };
        rubraview_pal_render_fill_rect(app->renderer, prompt, COLOR_BOX_FILL, 3.0);
        rubraview_pal_render_stroke_rect(app->renderer, prompt, COLOR_BOX_BORDER, 1.0, 3.0);

        char line[128];
        int written = snprintf(line, sizeof(line), T("Resume page %d / %zu  (Enter)"),
                               app->resume_page + 1, page_count(app));
        if (written > 0) {
            rubraview_pal_render_draw_text(app->renderer,
                                           (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)written },
                                           prompt, bar_h * 0.4, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
    }

    draw_subtitle(app, win_w, win_h);

    /* OSD (§3.1), skipped once it has faded out entirely. */
    /* Only when asked for with Shift+I: it no longer comes up by itself
       at every page (owner, 2026-10-08). */
    if (app->osd.always_on && rubraview_osd_opacity(&app->osd) > 0.01) {
        int32_t page = current_page_index(app);
        if (page >= 0 && (size_t)page < page_count(app)) {
            /* The file's name at the left; its size, resolution and place at the right, the place last (owner, 2026-10-16). */
            const app_page_t *pg = &app->pages[page];
            proven_u8str_view_t name = app->source.pages[page].name;
            if (app->osd_size_page != page) {
                /* Read once a page: from the archive's index, or the file's own entry. */
                app->osd_size_page = page;
                app->osd_size_bytes = 0;
                proven_u8str_view_t path = app->source.pages[page].path;
                rubraview_fs_entry_t entry;
                if (path.size == 0) app->osd_size_bytes = rubraview_page_source_entry_size(&app->source, (size_t)page);
                else if (rubraview_pal_fs_stat(app->arena, path, &entry)) app->osd_size_bytes = entry.size_bytes;
            }
            bool playing = app->media && page == app->media_page;
            bool picture = pg->loaded && !playing;
            char right_buf[192];
            proven_u8str_view_t right = rubraview_osd_right(right_buf, sizeof(right_buf), (size_t)page, page_count(app),
                                                picture ? real_w(pg) : 0, picture ? real_h(pg) : 0,
                                                app->osd_size_bytes, picture && pg->reduced,
                                                playing ? app->media_position : 0.0,
                                                playing ? app->media_info.duration_seconds : 0.0);
            /* RV-123 (RFC-0005 §6): a text file says its encoding and how far through it is. */
            char text_buf[256];
            int through = text_percent(app);
            if (through >= 0 && text_encoding_shown(app)) {
                int n = snprintf(text_buf, sizeof(text_buf), T("%s, %d %% through  %.*s"), text_encoding_shown(app), through, (int)right.size, right.ptr);
                if (n > 0 && (size_t)n < sizeof(text_buf)) right = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)text_buf, .size = (size_t)n };
            }

            /* The alpha byte carries the fade, so the whole overlay
               dims together rather than popping out. */
            uint32_t alpha = (uint32_t)(rubraview_osd_opacity(&app->osd) * 255.0) & 0xFFu;
            double dpi = rubraview_pal_window_dpi_scale(app->window);
            double bar_h = 28.0 * dpi, pad = 10.0 * dpi, font = bar_h * 0.5;
            double bottom = win_h - bar_h - (app->filmstrip.visible ? FILMSTRIP_THUMB : 0.0);
            rubraview_pal_rect_t back = { 0.0, bottom, win_w, bar_h };
            rubraview_pal_render_fill_rect(app->renderer, back, (alpha / 2u) << 24, 0.0);
            uint32_t ink = (alpha << 24) | (COLOR_TEXT & 0x00FFFFFFu);

            double right_w = 0.0;
            if (!rubraview_pal_render_measure_text(app->renderer, right, font, &right_w)) right_w = (double)right.size * font * 0.55;
            rubraview_pal_rect_t right_box = { win_w - pad - right_w - 2.0, bottom, right_w + 2.0, bar_h };
            rubraview_pal_render_draw_text(app->renderer, right, right_box, font, ink, RUBRAVIEW_TEXT_LEFT);

            /* The name in what is left: whole if it fits, else its start, "...", and its last ten characters. */
            double room = right_box.x - 2.0 * pad;
            char name_buf[1024];
            proven_u8str_view_t shown = name;
            double w = 0.0;
            if (room > 0.0 && rubraview_pal_render_measure_text(app->renderer, name, font, &w) && w > room) {
                size_t total = rubraview_utf8_chars(name);
                size_t lo = 0, hi = total > 13 ? total - 13 : 0;   /* head characters that fit */
                while (lo < hi) {
                    size_t mid = (lo + hi + 1) / 2;
                    proven_u8str_view_t trial = rubraview_middle_ellipsis(name_buf, sizeof(name_buf), name, mid, 10);
                    if (rubraview_pal_render_measure_text(app->renderer, trial, font, &w) && w <= room) lo = mid;
                    else hi = mid - 1;
                }
                shown = rubraview_middle_ellipsis(name_buf, sizeof(name_buf), name, lo, 10);
            }
            if (room > 0.0) {
                rubraview_pal_rect_t left_box = { pad, bottom, room, bar_h };
                rubraview_pal_render_draw_text(app->renderer, shown, left_box, font, ink, RUBRAVIEW_TEXT_LEFT);
            }
        }
    }
}

/* RV-076: the cover, blurred, filling the window behind the track — the
   thing that makes a music player look like one rather than like a
   viewer with nothing on screen. Nothing is drawn for a film, or for a
   track whose file carries no cover. */
void draw_music_backdrop(app_state_t *app, int32_t win_w, int32_t win_h) {
    if (!app->media || app->media_info.has_video || !app->music_backdrop) return;
    rubraview_mat3x2_t place = rubraview_mat3x2_scale((double)win_w / 64.0, (double)win_h / 64.0);
    rubraview_pal_render_draw_texture_opacity(app->renderer, app->music_backdrop, place,
                                              RUBRAVIEW_INTERP_LINEAR, 0.45);
}

/* What the file says about the track, along the bottom: the name and who
   made it, then the record it came from, then what the sound itself is.
   A line the file does not answer is left out rather than shown empty. */
/* Whether the analyser has something to draw now: a song playing on its page. */
static bool visualizer_live(const app_state_t *app) {
    return app->media && !app->media_info.has_video && !app->media_paused &&
           rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("visualizer")) > 0.5;
}

/* The music page's analyser and lyrics (owner, 2026-09-30). The analyser
   is a strip above the song's words: 64 bands with falling peaks, or the
   wave itself; the lyrics are the line being sung with three before and
   three after, near the top, and a click on one goes there. */
void draw_music_extras(app_state_t *app, int32_t win_w, int32_t win_h) {
    for (size_t i = 0; i < 7; ++i) app->lyric_row_index[i] = -1;
    if (!app->media || app->media_info.has_video) return;
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double chrome = 56.0 * dpi + (app->filmstrip.visible ? FILMSTRIP_THUMB : 0.0);

    int viz = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("visualizer")));
    if (viz > 0) {
        double h = 64.0 * dpi, w = (double)win_w * 0.6;
        double x0 = ((double)win_w - w) * 0.5, y0 = (double)win_h - chrome - 96.0 * dpi - h;
        static float samples[RUBRAVIEW_FFT_SIZE], imag[RUBRAVIEW_FFT_SIZE];
        size_t n = rubraview_pal_audio_tap_read(samples, RUBRAVIEW_FFT_SIZE);
        double now = rubraview_pal_time_now_seconds();
        double dt = app->viz_last > 0.0 ? now - app->viz_last : 0.0;
        app->viz_last = now;
        uint32_t ink = (COLOR_TILE_CURRENT & 0x00FFFFFFu) | 0xC0000000u;
        if (viz == 1) {
            if (n == RUBRAVIEW_FFT_SIZE && !app->media_paused) {
                memset(imag, 0, sizeof(imag));
                rubraview_apply_hann(samples, RUBRAVIEW_FFT_SIZE);
                rubraview_fft(samples, imag);
                double rate = app->media_info.audio_sample_rate > 0 ? (double)app->media_info.audio_sample_rate : 48000.0;
                rubraview_spectrum_bands(samples, imag, rate, -80.0, app->viz_bands, RUBRAVIEW_SPECTRUM_BANDS);
            } else {
                for (size_t b = 0; b < RUBRAVIEW_SPECTRUM_BANDS; ++b) app->viz_bands[b] = -80.0f;
            }
            rubraview_spectrum_peaks(app->viz_bands, app->viz_peaks, RUBRAVIEW_SPECTRUM_BANDS, dt > 0.5 ? 0.5 : dt, 40.0);
            double bw = w / RUBRAVIEW_SPECTRUM_BANDS;
            for (size_t b = 0; b < RUBRAVIEW_SPECTRUM_BANDS; ++b) {
                double level = (app->viz_bands[b] + 80.0) / 80.0;
                double peak = (app->viz_peaks[b] + 80.0) / 80.0;
                if (level < 0.0) level = 0.0;
                if (level > 1.0) level = 1.0;
                if (peak < 0.0) peak = 0.0;
                if (peak > 1.0) peak = 1.0;
                rubraview_pal_rect_t bar = { x0 + b * bw + 1.0, y0 + h * (1.0 - level), bw - 2.0, h * level };
                if (bar.height > 0.5) rubraview_pal_render_fill_rect(app->renderer, bar, ink, 0.0);
                rubraview_pal_rect_t cap = { x0 + b * bw + 1.0, y0 + h * (1.0 - peak) - 2.0 * dpi, bw - 2.0, 2.0 * dpi };
                rubraview_pal_render_fill_rect(app->renderer, cap, COLOR_TEXT, 0.0);
            }
        } else if (n > 0) {
            /* The wave: the newest samples across the strip, a dot a column. */
            size_t columns = (size_t)w / 2u;
            if (columns > n) columns = n;
            for (size_t c = 0; c < columns; ++c) {
                float v = samples[n - columns + c];
                if (v > 1.0f) v = 1.0f;
                if (v < -1.0f) v = -1.0f;
                double y = y0 + h * 0.5 - (double)v * h * 0.5;
                rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ x0 + (double)c * 2.0, y - dpi, 2.0, 2.0 * dpi }, ink, 0.0);
            }
        }
    }

    const rubraview_lyrics_t *lyrics = &app->music_lyrics;
    if (lyrics->count > 0) {
        /* Untimed words (USLT and the like) follow the song through its
           length, none of them marked as the one being sung. */
        int32_t now = lyrics->untimed
            ? rubraview_lyrics_untimed_index(lyrics, app->media_position, app->media_info.duration_seconds)
            : rubraview_lyrics_index_at(lyrics, app->media_position);
        double size = 18.0 * dpi, row_h = size * 1.6;
        double top = (double)win_h * 0.08;
        rubraview_pal_rect_t band = { (double)win_w * 0.1, top - size * 0.3, (double)win_w * 0.8, row_h * 7.0 + size * 0.6 };
        rubraview_pal_render_fill_rect(app->renderer, band, 0x80101010u, 6.0);
        for (int32_t k = -3; k <= 3; ++k) {
            int32_t index = (now < 0 ? 0 : now) + k;
            if (now < 0 && k < 0) continue;
            if (index < 0 || (size_t)index >= lyrics->count) continue;
            proven_u8str_view_t text = lyrics->lines[index].text;
            rubraview_pal_rect_t row = { band.x, top + (double)(k + 3) * row_h, band.width, row_h };
            bool current = index == now && !lyrics->untimed;
            rubraview_pal_render_draw_text(app->renderer, text.size > 0 ? text : PROVEN_LIT("\xE2\x99\xAA"), row,
                                           current ? size * 1.1 : size * 0.85,
                                           current ? COLOR_TEXT : 0x90F0F0F0u, RUBRAVIEW_TEXT_CENTER);
            app->lyric_row_index[k + 3] = index;
            app->lyric_row_rect[k + 3] = (rubraview_rect_t){ row.x, row.y, row.width, row.height };
        }
    }
}

void draw_music_words(app_state_t *app, int32_t win_w, int32_t win_h) {
    if (!app->media || app->media_info.has_video || !app->music_tags_read) return;
    rubraview_tags_t cue_tags = app->music_tags;
    if (app->music_cue.count > 1) {
        /* With a `.cue`, the track playing is named, not the whole file. */
        int32_t track = rubraview_cue_track_at(&app->music_cue, app->media_position);
        if (track >= 0) {
            const rubraview_cue_track_t *ct = &app->music_cue.tracks[track];
            if (ct->title.size > 0) cue_tags.title = ct->title;
            if (ct->performer.size > 0) cue_tags.artist = ct->performer;
            else if (app->music_cue.album_performer.size > 0) cue_tags.artist = app->music_cue.album_performer;
            if (app->music_cue.album_title.size > 0) cue_tags.album = app->music_cue.album_title;
            static char number[16];
            int n = snprintf(number, sizeof(number), "%d/%zu", ct->number, app->music_cue.count);
            if (n > 0) cue_tags.track_number = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)number, .size = (size_t)n };
        }
    }
    const rubraview_tags_t *t = &cue_tags;
    if (t->title.size == 0 && t->artist.size == 0 && t->album.size == 0 && t->codec.size == 0) return;

    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double size = 17.0 * dpi;

    /* The block sits **above** the seek bar and the status line, not
       across them: how many lines there are is known first, and the
       whole thing is placed from the bottom up. */
    bool has_name = t->title.size > 0 || t->artist.size > 0;
    bool has_record = t->album.size > 0 || t->year.size > 0 || t->track_number.size > 0;
    double height = (has_name ? size * 1.6 : 0.0) + (has_record ? size * 1.4 : 0.0) + size * 1.4;
    double chrome = 56.0 * dpi + (app->filmstrip.visible ? FILMSTRIP_THUMB : 0.0);
    double bottom = (double)win_h - chrome - height - 8.0 * dpi;
    if (bottom < 0.0) bottom = 0.0;

    char line[320];
    int n = 0;
    /* The name, and who made it. */
    if (has_name) {
        if (t->title.size > 0 && t->artist.size > 0) {
            n = snprintf(line, sizeof(line), "%.*s  —  %.*s",
                         (int)t->title.size, t->title.ptr, (int)t->artist.size, t->artist.ptr);
        } else {
            proven_u8str_view_t one = t->title.size > 0 ? t->title : t->artist;
            n = snprintf(line, sizeof(line), "%.*s", (int)one.size, one.ptr);
        }
        if (n > 0) {
            rubraview_pal_rect_t rect = { 0.0, bottom, (double)win_w, size * 1.6 };
            rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n },
                                           rect, size, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
            bottom += size * 1.6;
        }
    }
    /* The record, its year, and the track's number on it. */
    if (has_record) {
        n = snprintf(line, sizeof(line), "%.*s%s%.*s%s%s%.*s",
                     (int)t->album.size, t->album.ptr,
                     t->album.size > 0 && t->year.size > 0 ? "  (" : "",
                     (int)t->year.size, t->year.ptr,
                     t->album.size > 0 && t->year.size > 0 ? ")" : "",
                     t->track_number.size > 0 ? "   no. " : "",
                     (int)t->track_number.size, t->track_number.ptr);
        if (n > 0) {
            rubraview_pal_rect_t rect = { 0.0, bottom, (double)win_w, size * 1.4 };
            rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n },
                                           rect, size * 0.8, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
            bottom += size * 1.4;
        }
    }
    /* What the sound is. */
    {
        char what[160];
        int w = 0;
        if (t->codec.size > 0) w += snprintf(what + w, sizeof(what) - (size_t)w, "%.*s", (int)t->codec.size, t->codec.ptr);
        if (t->bitrate_kbps > 0) w += snprintf(what + w, sizeof(what) - (size_t)w, T("%s%u kbps"), w > 0 ? "  ·  " : "", t->bitrate_kbps);
        if (t->sample_rate > 0) {
            w += snprintf(what + w, sizeof(what) - (size_t)w, T("%s%.1f kHz"), w > 0 ? "  ·  " : "",
                          (double)t->sample_rate / 1000.0);
        }
        if (t->bits_per_sample > 0) w += snprintf(what + w, sizeof(what) - (size_t)w, T("  ·  %u bit"), t->bits_per_sample);
        if (t->channels > 0) {
            w += snprintf(what + w, sizeof(what) - (size_t)w, "  ·  %s",
                          t->channels == 1 ? "mono" : t->channels == 2 ? "stereo" : "multi");
        }
        if (w > 0) {
            rubraview_pal_rect_t rect = { 0.0, bottom, (double)win_w, size * 1.4 };
            rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)what, .size = (size_t)w },
                                           rect, size * 0.75, 0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
        }
    }
}

/* The strip: the page at the top of the window and those under it, as
   far as the window reaches, all at the strip's width. */
void draw_strip(app_state_t *app, int32_t win_w, int32_t win_h) {
    double width = strip_width(app, (double)win_w);
    /* Wider than the window (zoomed in): the pan moves it sideways, inside its edges. */
    double slack = width > (double)win_w ? (width - (double)win_w) / 2.0 : 0.0;
    if (app->pan_x > slack) app->pan_x = slack;
    if (app->pan_x < -slack) app->pan_x = -slack;
    double x0 = ((double)win_w - width) / 2.0 + app->pan_x;
    double y = -app->strip_offset;
    rubraview_interpolation_t interp = page_interpolation(app);

    for (size_t i = app->spread_index; i < page_count(app) && y < (double)win_h; ++i) {
        double h = strip_page_height(app, i);
        app_page_t *page = &app->pages[i];
        if (page->loaded && page->texture && !page->skipped) {
            double ow = 0.0, oh = 0.0;
            rubraview_orientation_apply_size(app->orientation, (double)picture_w(page), (double)picture_h(page), &ow, &oh);
            double scale = ow > 0.0 ? width / ow : 1.0;
            rubraview_mat3x2_t place = rubraview_mat3x2_multiply(
                rubraview_mat3x2_multiply(
                    rubraview_orientation_matrix(app->orientation, (double)picture_w(page), (double)picture_h(page)),
                    rubraview_mat3x2_scale(scale, scale)),
                rubraview_mat3x2_translate(x0, y));
            rubraview_pal_render_draw_texture_opacity(app->renderer, page->texture,
                                                      rubraview_mat3x2_multiply(page_texture_prescale(page), place),
                                                      interp, 1.0);
            /* A long page held reduced (D-39): the part on screen sharper,
               for the page across the window's middle (the tiles serve one page). */
            if (page->reduced && y <= (double)win_h * 0.5 && y + h > (double)win_h * 0.5) {
                draw_page_tiles(app, 0, (int32_t)i, page, place, scale, win_w, win_h, 1.0, interp);
            }
        }
        y += h;
    }
}

/* Draws one spread at a given opacity. Splitting this out is what makes
   §3.2.5's cross-fade possible: the outgoing spread is drawn first at a
   falling opacity and the incoming one over it at a rising one. */
/* One spread placed in the window, as every caller wants it placed.
   RV-106: with "Enlarge by whole steps only" a picture shown larger than
   it is takes a whole number of screen pixels a pixel (2x, 3x, ...) — the
   next one down from what the fit and the zoom ask — so no row or column
   of a small drawing comes out wider than its neighbour. A film is left
   as it is. */
static rubraview_composition_t compose_view(app_state_t *app, const rubraview_spread_t *spread,
                                            const rubraview_page_size_t *left, const rubraview_page_size_t *right,
                                            double win_w, double win_h) {
    rubraview_composition_t comp = rubraview_compose_spread(spread, left, right, win_w, win_h,
                                                            app->fit_mode, GUTTER, app->zoom, app->pan_x, app->pan_y);
    if (!g_whole_zoom || comp.count == 0 || app->media) return comp;
    double scale = comp.commands[0].scale;
    double whole = floor(scale + 1e-6);
    if (scale <= 1.0 || whole < 1.0 || scale - whole < 1e-6) return comp;
    return rubraview_compose_spread(spread, left, right, win_w, win_h,
                                    app->fit_mode, GUTTER, app->zoom * whole / scale, app->pan_x, app->pan_y);
}

void draw_spread(app_state_t *app, size_t spread_index, double opacity,
                        int32_t win_w, int32_t win_h) {
    if (spread_index >= app->layout.count) return;
    {
        const rubraview_spread_t *spread = &app->layout.spreads[spread_index];

        app_page_t *left = ensure_page_loaded(app, spread->left_index);
        app_page_t *right = ensure_page_loaded(app, spread->right_index);

        rubraview_page_size_t left_size = {0}, right_size = {0};
        if (left && left->loaded) {
            rubraview_orientation_apply_size(app->orientation, (double)picture_w((left)), (double)picture_h((left)),
                                             &left_size.width, &left_size.height);
        }
        if (right && right->loaded) {
            rubraview_orientation_apply_size(app->orientation, (double)picture_w((right)), (double)picture_h((right)),
                                             &right_size.width, &right_size.height);
        }

        rubraview_composition_t comp = compose_view(
            app, spread,
            (left && left->loaded) ? &left_size : NULL,
            (right && right->loaded) ? &right_size : NULL,
            (double)win_w, (double)win_h);
        /* RV-106: a slide or a zoom moves the whole spread about the window's middle. */
        rubraview_mat3x2_t pose = rubraview_mat3x2_identity();
        if (g_pose_on) {
            pose = rubraview_mat3x2_multiply(
                rubraview_mat3x2_multiply(rubraview_mat3x2_translate(-win_w * 0.5, -win_h * 0.5),
                                          rubraview_mat3x2_scale(g_pose.scale, g_pose.scale)),
                rubraview_mat3x2_translate(win_w * 0.5 + g_pose.dx, win_h * 0.5));
            for (size_t i = 0; i < comp.count; ++i) {
                comp.commands[i].transform = rubraview_mat3x2_multiply(comp.commands[i].transform, pose);
            }
        }

        rubraview_interpolation_t interp = page_interpolation(app);

        for (size_t i = 0; i < comp.count; ++i) {
            const rubraview_draw_command_t *cmd = &comp.commands[i];
            if (cmd->page_index < 0 || (size_t)cmd->page_index >= page_count(app)) continue;
            app_page_t *page = &app->pages[cmd->page_index];
            if (!page->loaded) continue;

            bool whole_page = (cmd->src_left <= 0.0) &&
                              (cmd->src_right >= left_size.width - 0.5 || comp.count == 2);

            if (whole_page) {
                /* Source pixels -> oriented space -> screen. */
                rubraview_mat3x2_t oriented = rubraview_mat3x2_multiply(
                    rubraview_mat3x2_multiply(page_texture_prescale(page),
                        rubraview_orientation_matrix(app->orientation, (double)picture_w(page), (double)picture_h(page))),
                    cmd->transform);
                if (cmd->page_index == app->media_page) {
                    /* A DVD's picture subtitles are drawn in the film's
                       own frame, so they need the film's place on screen. */
                    app->video_transform = oriented;
                    app->video_transform_ok = true;
                    app->video_page_w = page->width;
                    app->video_page_h = page->height;
                }
                rubraview_pal_render_draw_texture_opacity(app->renderer, page->texture, oriented, interp, opacity);
                if (opacity >= 1.0 && !g_pose_on) {
                    /* D-40, D-81: zoomed past the texture, the sharper tiles on top. */
                    draw_page_tiles(app, (int)i, cmd->page_index, page,
                                    rubraview_mat3x2_multiply(
                                        rubraview_orientation_matrix(app->orientation, (double)picture_w(page),
                                                                     (double)picture_h(page)),
                                        cmd->transform),
                                    cmd->scale, win_w, win_h, opacity, interp);
                }

                if (app->pixel_grid && cmd->scale >= PIXEL_GRID_MIN_SCALE) {
                    rubraview_pal_render_draw_pixel_grid(app->renderer, oriented,
                                                         page->width, page->height, 0x40FFFFFFu);
                }
            } else {
                /* A split half of a wide spread (§3.3.7). The command names
                   the half in the picture as the reader sees it; the part
                   of the stored picture that is, and the turn that puts it
                   upright, come from the orientation. */
                /* The command speaks in picture pixels; a reduced texture
                   has fewer, so the rectangle shrinks and the transform
                   grows by the same ratio. */
                rubraview_mat3x2_t pre = page_texture_prescale(page);
                double sl = 0.0, st = 0.0, sr = 0.0, sb = 0.0;
                rubraview_mat3x2_t turn = rubraview_orientation_region(
                    app->orientation, (double)picture_w(page), (double)picture_h(page),
                    cmd->src_left, cmd->src_top, cmd->src_right, cmd->src_bottom, &sl, &st, &sr, &sb);
                rubraview_src_rect_t src = {
                    .left = sl / pre.a, .top = st / pre.d, .right = sr / pre.a, .bottom = sb / pre.d,
                };
                rubraview_pal_render_draw_texture_region(app->renderer, page->texture, src,
                                                         rubraview_mat3x2_multiply(
                                                             rubraview_mat3x2_multiply(pre, turn), cmd->transform),
                                                         interp);
            }
        }
    }
}



/* ---- triage: delete, rename, curate (§3.18) ---- */

/* The file behind the page on screen, or an empty slice for an archive
   page — a page inside a CBZ is not a file, so none of §3.18 applies to
   it and every action below refuses rather than doing something to the
   archive. */
proven_u8str_view_t current_file_path(app_state_t *app) {
    int32_t page = current_page_index(app);
    if (page < 0 || app->source.kind != RUBRAVIEW_PAGE_SOURCE_FOLDER) {
        return PROVEN_LIT("");
    }
    return app->source.pages[page].path;
}

/* After a file leaves the sequence the viewer has to land somewhere.
   §3.18.1: the next item, or the one before when the last was removed. */
void reopen_after_removal(app_state_t *app, size_t removed_index) {
    proven_u8str_view_t dir = app->source_dir;
    if (dir.size == 0) return;

    size_t target = removed_index;
    open_folder(app, dir);
    if (page_count(app) == 0) return;
    if (target >= page_count(app)) target = page_count(app) - 1;

    finish_open(app, target);
}

/* D-128: the folder again, on a file by its path — a file was added beside it,
   so its place in the order (by date, say) may not be what it was. */
void reopen_on_path(app_state_t *app, proven_u8str_view_t path, size_t fallback) {
    proven_u8str_view_t dir = app->source_dir;
    if (dir.size == 0) return;
    char kept[1024];
    if (path.size >= sizeof(kept)) { reopen_after_removal(app, fallback); return; }
    memcpy(kept, path.ptr, path.size);                 /* the list it may point into is about to be made again */
    proven_u8str_view_t want = { .ptr = (const proven_byte_t *)kept, .size = path.size };
    open_folder(app, dir);
    if (page_count(app) == 0) return;
    size_t target = fallback < page_count(app) ? fallback : page_count(app) - 1;
    for (size_t i = 0; i < page_count(app); ++i) {
        if (proven_u8str_view_eq(app->source.pages[i].path, want)) { target = i; break; }
    }
    finish_open(app, target);
}

void osd_say(app_state_t *app, proven_u8str_view_t text) {
    size_t n = text.size < sizeof(app->notice) - 1 ? text.size : sizeof(app->notice) - 1;
    memcpy(app->notice, text.ptr, n);
    app->notice[n] = '\0';
    app->notice_length = n;
    app->notice_seconds = 2.5;
}

static void triage_delete(app_state_t *app, bool permanent) {
    proven_u8str_view_t path = current_file_path(app);
    if (path.size == 0) return;

    int32_t page = current_page_index(app);
    if (page < 0) return;

    bool ok = permanent ? rubraview_pal_fs_delete(path) : rubraview_pal_fs_recycle(path);
    if (!ok) {
        osd_say(app, TU8("could not delete that file"));
        return;
    }

    rubraview_undo_push(&app->undo, (rubraview_file_action_t){
        .op = permanent ? RUBRAVIEW_FILE_OP_PURGE : RUBRAVIEW_FILE_OP_RECYCLE,
        .source_path = path,
        .playlist_index = (size_t)page,
    });

    osd_say(app, permanent ? TU8("deleted permanently") : TU8("moved to the recycle bin"));
    reopen_after_removal(app, (size_t)page);
}

static void triage_curate(app_state_t *app, int32_t digit) {
    proven_u8str_view_t path = current_file_path(app);
    if (path.size == 0) return;

    proven_u8str_view_t target_dir = rubraview_curation_target(&app->curation, digit);
    if (target_dir.size == 0) return;   /* that key is not bound: do nothing */

    int32_t page = current_page_index(app);
    if (page < 0) return;

    rubraview_pal_fs_make_dirs(target_dir);
    proven_u8str_view_t name = rubraview_path_basename(path);
    proven_u8str_view_t target = rubraview_path_join(app->arena, target_dir, name);
    if (target.size == 0) return;

    bool moving = app->curation.mode == RUBRAVIEW_CURATION_MOVE;
    bool ok = moving ? rubraview_pal_fs_move(path, target) : rubraview_pal_fs_copy(path, target);
    if (!ok) {
        osd_say(app, TU8("could not put the file there"));
        return;
    }

    rubraview_undo_push(&app->undo, (rubraview_file_action_t){
        .op = moving ? RUBRAVIEW_FILE_OP_MOVE : RUBRAVIEW_FILE_OP_COPY,
        .source_path = path,
        .target_path = target,
        .playlist_index = (size_t)page,
    });

    osd_say(app, moving ? TU8("moved") : TU8("copied"));

    /* §3.18.3: a move takes the file out of the sequence, so the viewer
       advances; a copy leaves it, so it stays put. */
    if (rubraview_curation_advances(&app->curation)) {
        reopen_after_removal(app, (size_t)page);
    }
}

/* ---- the file on screen, to its folder (owner, 2026-10-08) ----
   `copy_to_folder` and `move_to_folder`, each with a folder of its own from
   Settings > Files. A name already there is never replaced: the file goes
   in as `name-1.ext`, `name-2.ext`, ... A page inside an archive is not a
   file, so it can be copied out (its bytes written as a file) but not
   moved. */
bool filing_exists(void *ctx, proven_u8str_view_t path) {
    (void)ctx;
    return rubraview_pal_fs_exists(path);
}

static void filing_put(app_state_t *app, bool moving) {
    if (current_page_index(app) < 0) return;
    proven_u8str_view_t dir = moving ? app->curation.move_dir : app->curation.copy_dir;
    /* None set yet: the box asks for one, as Shift+F6 / Shift+F7 do. */
    if (dir.size == 0) { target_begin(app, moving, false); return; }
    filing_put_to(app, dir, moving);
}

static void filing_put_to(app_state_t *app, proven_u8str_view_t dir, bool moving) {
    int32_t page = current_page_index(app);
    if (page < 0) return;
    bool in_archive = app->source.kind != RUBRAVIEW_PAGE_SOURCE_FOLDER;
    if (in_archive && moving) {
        osd_say(app, TU8("a page inside an archive can be copied (F6), not moved"));
        return;
    }
    proven_u8str_view_t path = in_archive ? PROVEN_LIT("") : app->source.pages[page].path;
    proven_u8str_view_t name = rubraview_path_basename(in_archive ? app->source.pages[page].name : path);
    if (name.size == 0) return;

    if (!rubraview_pal_fs_make_dirs(dir) && !rubraview_pal_fs_exists(dir)) {
        osd_say(app, TU8("that folder could not be made"));
        return;
    }
    proven_u8str_view_t target = rubraview_filing_target(app->arena, dir, name, filing_exists, NULL);
    if (target.size == 0) {
        osd_say(app, TU8("no free name left in that folder"));
        return;
    }
    /* The same file onto itself would be a copy named -1 beside it: allowed;
       a move into the folder it is already in is nothing to do. */
    if (moving && rubraview_path_same(rubraview_path_dirname(path), dir)) {
        osd_say(app, TU8("it is already in that folder"));
        return;
    }

    bool ok = false;
    if (in_archive) {
        void *mem = NULL;
        rubraview_page_bytes_t bytes = page_read_owned(&app->source, &app->source_lock, (size_t)page, &mem);
        ok = bytes.ok && rubraview_pal_fs_write_file(target, bytes.data);
        free(mem);
    } else {
        ok = moving ? rubraview_pal_fs_move(path, target) : rubraview_pal_fs_copy(path, target);
    }
    if (!ok) {
        osd_say(app, TU8("could not put the file there"));
        return;
    }

    rubraview_undo_push(&app->undo, (rubraview_file_action_t){
        .op = moving ? RUBRAVIEW_FILE_OP_MOVE : RUBRAVIEW_FILE_OP_COPY,
        .source_path = path,
        .target_path = target,
        .playlist_index = (size_t)page,
    });

    /* Say the name it has there: it is not always the name it had. */
    char said[sizeof(app->notice)];
    proven_u8str_view_t put = rubraview_path_basename(target);
    int n = snprintf(said, sizeof(said), "%s: %.*s", moving ? T("moved") : T("copied"), (int)(put.size < 160 ? put.size : 160), put.ptr);
    osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)said, .size = n > 0 ? (size_t)n : 0 });

    if (moving) reopen_after_removal(app, (size_t)page);
}

static void triage_undo(app_state_t *app) {
    rubraview_file_action_t action = {0};
    switch (rubraview_undo_peek(&app->undo, &action)) {
        case RUBRAVIEW_UNDO_NOTHING:
            osd_say(app, TU8("nothing to undo"));
            return;
        case RUBRAVIEW_UNDO_IRREVERSIBLE:
            /* Said plainly rather than silently undoing the action
               before it, which would restore the wrong file. */
            osd_say(app, TU8("that file was deleted permanently and cannot be brought back"));
            return;
        case RUBRAVIEW_UNDO_AVAILABLE:
            break;
    }

    /* 1.23.0: the files of one move or copy of many go back together, newest first. */
    size_t undone = 0;
    for (;;) {
        bool ok = false;
        switch (action.op) {
            case RUBRAVIEW_FILE_OP_MOVE:
            case RUBRAVIEW_FILE_OP_RENAME:
                ok = rubraview_pal_fs_move(action.target_path, action.source_path);
                break;
            case RUBRAVIEW_FILE_OP_COPY:
                /* Undoing a copy means removing the copy, not the original; a folder's copy whole (1.23.0). */
                ok = rubraview_pal_fs_delete(action.target_path) || rubraview_pal_fs_delete_tree(action.target_path);
                break;
            case RUBRAVIEW_FILE_OP_RECYCLE:
                ok = rubraview_pal_fs_restore_last_recycled(action.source_path);
                if (!ok) osd_say(app, TU8("Windows keeps the recycle bin's undo to itself — restore it from there"));
                break;
            default:
                break;
        }
        if (!ok) {
            if (undone > 0) break;
            if (action.op == RUBRAVIEW_FILE_OP_COPY) osd_say(app, TU8("could not take the copy back"));
            return;
        }
        rubraview_undo_commit(&app->undo);
        undone++;
        rubraview_file_action_t next = {0};
        if (!rubraview_undo_same_batch(&app->undo, action.batch) ||
            rubraview_undo_peek(&app->undo, &next) != RUBRAVIEW_UNDO_AVAILABLE) break;
        action = next;
    }

    if (undone > 1) {
        char line[96];
        int n = snprintf(line, sizeof(line), T("%zu undone"), undone);
        osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = n > 0 ? (size_t)n : 0 });
    } else {
        osd_say(app, TU8("undone"));
    }
    if (app->picker_open) picker_navigate(app, app->picker_dir);   /* what it lists has changed */
    else reopen_after_removal(app, action.playlist_index);
}

/* §3.18.2's inline rename. The box is drawn here, not a child EDIT
   control; its text comes from the window's text input (TEXT events,
   with the IME attached only while the box is open, K5), so Hangul,
   capitals and symbols go in as typed. The caret moves and selects with
   the arrows, Home and End (Shift to select), Ctrl+A selects all, and
   Ctrl+C / Ctrl+X / Ctrl+V use the clipboard. */
/* The box closes: the window goes back to keys only (K5). */
void rename_end(app_state_t *app) {
    app->rename_active = false;
    app->rename_btn_n = 0;
    app->rename_is_archive = false;
    app->rename_is_extension = false;
    app->rename_is_picker = false;
    app->rename_is_path = false;
    app->rename_is_filter = false;
    app->rename_is_listfilter = false;
    app->rename_is_target = false;
    app->rename_is_find = false;
    app->rename_is_value = false;
    app->rename_is_gmic = false;
    app->rename_is_ffmpeg = false;
    app->rename_is_address = false;
    app->batch_typing = 0;
    app->rename_is_apply = false;
    app->target_offered_len = 0;
    if (app->rename_is_password) {                       /* nothing of it stays behind */
        secure_wipe(app->rename_buffer, sizeof(app->rename_buffer));
        secure_wipe(app->rename_composing, sizeof(app->rename_composing));
        app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
        app->rename_is_password = false;
    }
    app->rename_favorite = -1;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, false);
}

/* Text typed or finished by the IME, and the IME's unfinished part. */
void rename_text(app_state_t *app, const rubraview_window_event_t *event) {
    if (app->ab_edit_open) {
        if (event->kind == RUBRAVIEW_WINDOW_EVENT_TEXT) {
            ab_edit_text_typed(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)event->text.utf8, .size = event->text.length });
        }
        return;
    }
    if (app->px_edit_open) {
        if (event->kind == RUBRAVIEW_WINDOW_EVENT_TEXT) {
            px_edit_text_typed(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)event->text.utf8, .size = event->text.length });
        }
        return;
    }
    if (!app->rename_active) return;
    proven_u8str_view_t text = { .ptr = (const proven_byte_t *)event->text.utf8, .size = event->text.length };
    if (event->kind == RUBRAVIEW_WINDOW_EVENT_TEXT) {
        rename_insert(app, text);
    } else {
        size_t n = text.size < sizeof(app->rename_composing) ? text.size : sizeof(app->rename_composing) - 1;
        memcpy(app->rename_composing, text.ptr, n);
        app->rename_composing_length = n;
    }
}

/* The picker's path box (owner, 2026-09-28: "경로를 직접 입력하는 기능도
   당연히 필요해요"): the rename box over the path bar, holding the folder
   on screen, all of it selected so typing replaces it. */
static void picker_path_begin(app_state_t *app) {
    if (!app->picker_open || app->rename_active) return;
    proven_u8str_view_t here = rubraview_picker_is_places(app->picker_dir) ? PROVEN_LIT("") : app->picker_dir;
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    rubraview_textedit_set(&app->rename_edit, here.size < sizeof(app->rename_buffer) ? here : PROVEN_LIT(""));
    rubraview_textedit_select_all(&app->rename_edit);
    app->rename_active = true;
    app->rename_is_path = true;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, true);
}

/* Enter in the path box: a folder is shown, a file is opened, and a path
   that is not there is said, with the box left open to put it right. */
static void picker_path_commit(app_state_t *app) {
    rename_insert(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->rename_composing, .size = app->rename_composing_length });
    app->rename_composing_length = 0;
    proven_u8str_view_t path = rubraview_picker_typed_path(app->arena, rubraview_textedit_text(&app->rename_edit), app->picker_dir);
    if (path.size == 0) { rename_end(app); return; }
    rubraview_fs_entry_t found;
    if (!rubraview_pal_fs_stat(app->arena, path, &found)) {
        osd_say(app, TU8("no such folder or file"));
        return;
    }
    rename_end(app);
    if (found.is_directory) {
        picker_navigate(app, path);
        app->picker.focus = 0;
    } else {
        app->picker_open = false;
        open_path(app, path);
    }
}

/* The folder box for a copy or a move (owner, 2026-10-16): the last
   folder typed in it, or the one set for F6 / F7, all selected. */
void target_begin(app_state_t *app, bool moving, bool from_picker) {
    if (from_picker) {
        size_t picked = 0;
        uint64_t bytes = 0;
        rubraview_picker_selection_metrics(&app->picker, &picked, &bytes);
        if (picked == 0) { osd_say(app, TU8("pick some files first")); return; }
    } else {
        if (current_page_index(app) < 0) return;
        if (moving && app->source.kind != RUBRAVIEW_PAGE_SOURCE_FOLDER) {
            osd_say(app, TU8("a page inside an archive can be copied (F6), not moved"));
            return;
        }
    }
    if (app->rename_active) rename_end(app);
    proven_u8str_view_t last = rubraview_settings_get_text(&app->settings, PROVEN_LIT("curation"), PROVEN_LIT("other_dir"));
    if (last.size == 0) last = moving ? app->curation.move_dir : app->curation.copy_dir;
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    rubraview_textedit_set(&app->rename_edit, last.size < sizeof(app->rename_buffer) ? last : PROVEN_LIT(""));
    rubraview_textedit_select_all(&app->rename_edit);
    app->rename_active = true;
    app->rename_is_target = true;
    app->target_moving = moving;
    app->target_from_picker = from_picker;
    app->target_offered_len = 0;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, true);
    osd_say(app, moving ? TU8("type the folder to move to; Ctrl+O browses, a new one is made")
                        : TU8("type the folder to copy to; Ctrl+O browses, a new one is made"));
}

static void target_browse(app_state_t *app) {
    rubraview_file_dialog_opts_t opts = {
        .title = app->target_moving ? T("Move to which folder") : T("Copy to which folder"), .folder_mode = true,
        .parent_window_handle = rubraview_pal_window_native_handle(app->window),
    };
    rubraview_file_dialog_result_t picked = rubraview_pal_file_dialog_pick_folder(app->arena, &opts);
    if (picked.accepted && picked.count > 0 && picked.paths[0].size < sizeof(app->rename_buffer)) {
        rubraview_textedit_set(&app->rename_edit, picked.paths[0]);   /* in the box, for Enter */
        app->target_offered_len = 0;
    }
}

/* Enter in the folder box: a folder there takes the files; one not there
   is offered, and made when Enter comes again for it. */
static void target_commit(app_state_t *app) {
    rename_insert(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->rename_composing, .size = app->rename_composing_length });
    app->rename_composing_length = 0;
    proven_u8str_view_t typed = rubraview_target_folder_trim(rubraview_textedit_text(&app->rename_edit));
    proven_u8str_view_t base = app->target_from_picker ? app->picker_dir : rubraview_path_dirname(current_file_path(app));
    proven_u8str_view_t path = typed.size ? rubraview_picker_typed_path(app->arena, typed, base) : typed;
    rubraview_fs_entry_t found;
    bool exists = path.size > 0 && rubraview_pal_fs_stat(app->arena, path, &found);
    proven_u8str_view_t offered = { .ptr = (const proven_byte_t *)app->target_offered, .size = app->target_offered_len };
    char line[1200];
    switch (rubraview_target_folder_step(path, exists, exists && found.is_directory, offered)) {
        case RUBRAVIEW_TARGET_NOTHING:
            rename_end(app);
            return;
        case RUBRAVIEW_TARGET_NOT_FOLDER:
            osd_say(app, TU8("that is a file, not a folder - put it right, or Esc"));
            return;
        case RUBRAVIEW_TARGET_OFFER: {
            size_t n = path.size < sizeof(app->target_offered) ? path.size : 0;
            memcpy(app->target_offered, path.ptr, n);
            app->target_offered_len = n;
            int w = snprintf(line, sizeof(line), T("%.*s is not there yet: Enter again makes it, Esc leaves it"),
                             (int)path.size, path.ptr);
            if (w > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)w < sizeof(line) ? (size_t)w : sizeof(line) - 1 });
            return;
        }
        case RUBRAVIEW_TARGET_MAKE:
            if (!rubraview_pal_fs_make_dirs(path) && !rubraview_pal_fs_exists(path)) {
                osd_say(app, TU8("that folder could not be made"));
                return;
            }
            break;
        case RUBRAVIEW_TARGET_USE:
            break;
    }
    /* Kept, to be offered first next time. */
    proven_u8str_view_t kept = app_keep(app, path);
    rubraview_settings_set_text(&app->settings, PROVEN_LIT("curation"), PROVEN_LIT("other_dir"), kept);
    settings_write_if_changed(app, false);
    bool moving = app->target_moving, from_picker = app->target_from_picker;
    rename_end(app);
    if (from_picker) picker_curate_to(app, kept, moving);
    else filing_put_to(app, kept, moving);
}

static void rename_begin(app_state_t *app) {
    proven_u8str_view_t path = current_file_path(app);
    /* D-88: a page of an archive is renamed where it is, in its folder there. */
    bool in_archive = on_archive_page(app);
    if (in_archive) {
        if (app->arc_job) return;
        rubraview_arcedit_route_t route = rubraview_arcedit_route(&app->source);
        if (route == RUBRAVIEW_ARCEDIT_ENCRYPTED) { osd_say(app, cstr(rubraview_arcedit_result_text(RUBRAVIEW_ARCEDIT_REFUSED_ENCRYPTED))); return; }
        path = page_display_name(app, (size_t)current_page_index(app));
    }
    if (path.size == 0) return;

    /* The whole name, extension and all (owner, 2026-09-23): an
       extension typed wrongly could not be corrected before, and the
       owner asked for no question when it changes. */
    proven_u8str_view_t name = rubraview_path_basename(path);
    if (name.size >= sizeof(app->rename_buffer)) return;

    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    rubraview_textedit_set(&app->rename_edit, name);
    app->rename_active = true;
    app->rename_is_archive = in_archive;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, true);   /* Hangul, capitals, symbols (K5's hole) */
}

/* 1.24.0 (owner, 2026-10-19: "선택된 파일들의 확장자들에 대해 각각 어떤 것으로 바꿀지를 선택"): Change extensions
   lists each extension the picked files have, its new one beside it; the box edits one row at a time
   (a click on a row, Tab or Enter goes to another), and Apply (Enter on the last) renames them all.
   A row left as it was leaves its files alone. */
static bool ext_same(const char *a, proven_u8str_view_t b) {
    return proven_u8str_view_eq_ascii_ci(proven_u8str_view_from_cstr(a), b);
}

static void ext_store(app_state_t *app) {
    if (app->ext_at < 0 || app->ext_at >= app->ext_n) return;
    proven_u8str_view_t typed = rubraview_textedit_text(&app->rename_edit);
    snprintf(app->ext_to[app->ext_at], sizeof(app->ext_to[0]), "%.*s", (int)typed.size, typed.ptr);
}

static void ext_switch(app_state_t *app, int32_t row) {
    if (row < 0 || row >= app->ext_n) return;
    ext_store(app);
    app->ext_at = row;
    rubraview_textedit_set(&app->rename_edit, cstr(app->ext_to[row]));
    rubraview_textedit_select_all(&app->rename_edit);
    app->rename_composing_length = 0;
}

void rename_extension_begin(app_state_t *app) {
    app->ext_n = 0;
    for (size_t i = 0; i < app->picker_listing.count && app->ext_n < 16; ++i) {
        if (!app->picker_selected || !app->picker_selected[i] || app->picker_listing.entries[i].is_directory) continue;
        proven_u8str_view_t ext = rubraview_path_ext(app->picker_listing.entries[i].name);
        if (ext.size == 0 || ext.size >= sizeof(app->ext_from[0])) continue;
        bool seen = false;
        for (int32_t k = 0; k < app->ext_n && !seen; ++k) seen = ext_same(app->ext_from[k], ext);
        if (seen) continue;
        snprintf(app->ext_from[app->ext_n], sizeof(app->ext_from[0]), "%.*s", (int)ext.size, ext.ptr);
        snprintf(app->ext_to[app->ext_n], sizeof(app->ext_to[0]), "%.*s", (int)ext.size, ext.ptr);
        app->ext_n++;
    }
    if (app->ext_n == 0) { osd_say(app, TU8("pick some files with an extension first")); return; }
    app->ext_at = 0;
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    rubraview_textedit_set(&app->rename_edit, cstr(app->ext_to[0]));
    rubraview_textedit_select_all(&app->rename_edit);
    app->rename_active = true;
    app->rename_is_extension = true;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, true);
}

/* Every picked file whose extension has a new one, renamed to it; one undo for all. */
static void rename_extension_apply(app_state_t *app) {
    ext_store(app);
    size_t done = 0, failed = 0;
    uint32_t batch = ++app->undo_batch;
    for (size_t i = 0; i < app->picker_listing.count; ++i) {
        if (!app->picker_selected || !app->picker_selected[i]) continue;
        const rubraview_fs_entry_t *entry = &app->picker_listing.entries[i];
        if (entry->is_directory) continue;
        proven_u8str_view_t ext = rubraview_path_ext(entry->name);
        int32_t row = -1;
        for (int32_t k = 0; k < app->ext_n && row < 0; ++k) if (ext.size > 0 && ext_same(app->ext_from[k], ext)) row = k;
        if (row < 0) continue;
        char want[66];
        const char *to = app->ext_to[row];
        while (*to == ' ') ++to;
        if (!*to || strcmp(to, app->ext_from[row]) == 0 || (to[0] != '.' && strcmp(to, app->ext_from[row] + 1) == 0)) continue;   /* left as it was */
        snprintf(want, sizeof(want), "%s%s", to[0] == '.' ? "" : ".", to);
        proven_u8str_view_t target = rubraview_path_with_ext(app->arena, entry->path, want);
        if (target.size == 0 || rubraview_rename_validate(rubraview_path_basename(target)) != RUBRAVIEW_RENAME_OK ||
            rubraview_pal_fs_exists(target) || !rubraview_pal_fs_move(entry->path, target)) { failed++; continue; }
        rubraview_undo_push(&app->undo, (rubraview_file_action_t){
            .op = RUBRAVIEW_FILE_OP_RENAME, .source_path = app_keep(app, entry->path), .target_path = target, .batch = batch,
        });
        done++;
    }
    rename_end(app);
    char line[128];
    int written = failed > 0 ? snprintf(line, sizeof(line), T("%zu renamed, %zu could not be"), done, failed)
                             : snprintf(line, sizeof(line), T("%zu file(s) renamed"), done);
    if (written > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)written });
    picker_navigate(app, app->picker_dir);   /* the names on screen have changed */
}

/* Enter: the next row, and Apply on the last. */
static void rename_extension_commit(app_state_t *app) {
    rename_insert(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->rename_composing, .size = app->rename_composing_length });
    app->rename_composing_length = 0;
    if (app->ext_at + 1 < app->ext_n) ext_switch(app, app->ext_at + 1);
    else rename_extension_apply(app);
}

/* 1.23.0 (owner, 2026-10-19): a file or a folder of the open-file screen renamed from its right click.
   The box is the rename box, holding its whole name. */
static void rename_picker_begin(app_state_t *app, size_t at) {
    if (at >= app->picker_listing.count || app->rename_active) return;
    if (rubraview_picker_is_places(app->picker_dir) || rubraview_picker_is_recent(app->picker_dir) || app->picker_in_book) {
        osd_say(app, TU8("this list's names cannot be changed here"));
        return;
    }
    proven_u8str_view_t name = app->picker_listing.entries[at].name;
    if (name.size == 0 || name.size >= sizeof(app->rename_buffer)) return;
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    rubraview_textedit_set(&app->rename_edit, name);
    app->rename_active = true;
    app->rename_is_picker = true;
    app->rename_picker_at = at;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, true);
}

static void rename_picker_commit(app_state_t *app) {
    rename_insert(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->rename_composing, .size = app->rename_composing_length });
    size_t at = app->rename_picker_at;
    rename_end(app);
    if (at >= app->picker_listing.count) return;
    const rubraview_fs_entry_t *entry = &app->picker_listing.entries[at];
    proven_u8str_view_t typed = rubraview_textedit_text(&app->rename_edit);
    if (typed.size == entry->name.size && memcmp(typed.ptr, entry->name.ptr, typed.size) == 0) return;   /* nothing changed */
    rubraview_rename_err_t err = rubraview_rename_validate(typed);
    if (err != RUBRAVIEW_RENAME_OK) { osd_say(app, rubraview_rename_error_text(err)); return; }
    proven_u8str_view_t source = app_keep(app, entry->path);
    proven_u8str_view_t target = rubraview_path_join(app->arena, rubraview_path_dirname(source), typed);
    if (target.size == 0 || rubraview_pal_fs_exists(target) || !rubraview_pal_fs_move(source, target)) {
        osd_say(app, TU8("could not rename that file"));
        return;
    }
    rubraview_undo_push(&app->undo, (rubraview_file_action_t){
        .op = RUBRAVIEW_FILE_OP_RENAME, .source_path = source, .target_path = target,
    });
    osd_say(app, TU8("renamed"));
    picker_navigate(app, app->picker_dir);   /* the name on screen has changed */
}

static void rename_commit(app_state_t *app) {
    if (!app->rename_active) return;
    if (app->rename_is_find) { text_find_step(app, true); return; }   /* the box stays for the next */
    if (app->rename_is_value) { edit_value_typed(app); return; }
    if (app->rename_is_gmic) { gmic_typed(app); return; }   /* RFC-0009 C */
    if (app->rename_is_ffmpeg) { ffmpeg_typed(app); return; }   /* RFC-0009 D */
    if (app->rename_is_address) { address_typed(app); return; }   /* RV-158 */
    if (app->batch_typing) { batch_typed(app); return; }
    if (app->rename_is_apply) { apply_all_typed(app); return; }
    if (app->rename_is_extension) { rename_extension_commit(app); return; }
    if (app->rename_is_picker) { rename_picker_commit(app); return; }
    if (app->rename_is_path) { picker_path_commit(app); return; }
    if (app->rename_is_filter) { picker_filter_follow(app); rename_end(app); return; }   /* 1.22.0: kept */
    if (app->rename_is_listfilter) { list_filter_follow(app); rename_end(app); return; }   /* 1.25.0: kept */
    if (app->rename_is_target) { target_commit(app); return; }
    if (app->rename_is_password) { password_commit(app); return; }
    if (app->rename_favorite >= 0) { favorite_rename_commit(app); return; }
    /* Enter with a syllable still being built: it is part of the name. */
    rename_insert(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->rename_composing, .size = app->rename_composing_length });
    bool in_archive = app->rename_is_archive;
    rename_end(app);

    if (in_archive) {
        /* D-88: the same rules for the name; the archive is written again with it. */
        proven_u8str_view_t typed = rubraview_textedit_text(&app->rename_edit);
        rubraview_rename_err_t bad = rubraview_rename_validate(typed);
        char name[512];
        if (bad != RUBRAVIEW_RENAME_OK) { osd_say(app, rubraview_rename_error_text(bad)); return; }
        if (!on_archive_page(app) || typed.size >= sizeof(name)) return;
        proven_u8str_view_t old = rubraview_path_basename(page_display_name(app, (size_t)current_page_index(app)));
        if (old.size == typed.size && memcmp(old.ptr, typed.ptr, old.size) == 0) return;   /* nothing changed */
        memcpy(name, typed.ptr, typed.size);
        name[typed.size] = '\0';
        arc_edit_on_page(app, RUBRAVIEW_ARCEDIT_RENAME, name);
        return;
    }

    proven_u8str_view_t path = current_file_path(app);
    if (path.size == 0) return;

    proven_u8str_view_t new_name = rubraview_textedit_text(&app->rename_edit);

    rubraview_rename_err_t err = rubraview_rename_validate(new_name);
    if (err != RUBRAVIEW_RENAME_OK) {
        osd_say(app, rubraview_rename_error_text(err));
        return;
    }

    proven_u8str_view_t target = rubraview_path_join(app->arena, rubraview_path_dirname(path), new_name);
    if (target.size == 0 || !rubraview_pal_fs_move(path, target)) {
        osd_say(app, TU8("could not rename that file"));
        return;
    }

    int32_t page = current_page_index(app);
    rubraview_undo_push(&app->undo, (rubraview_file_action_t){
        .op = RUBRAVIEW_FILE_OP_RENAME,
        .source_path = path,
        .target_path = target,
        .playlist_index = page >= 0 ? (size_t)page : 0,
    });

    osd_say(app, TU8("renamed"));
    reopen_after_removal(app, page >= 0 ? (size_t)page : 0);
}

/* §3.19.2: what to do with what was dropped. */
/* §3.19.2: the files that were dropped, in natural name order, as a
   source of their own. They may come from different folders; the page
   list holds full paths, so that costs nothing. Directories among them
   are left out — rubraview_drop_classify sends a lone folder down the
   other path, and a folder dropped together with files has no obvious
   place in a sequence of pages. */
static void open_dropped_files(app_state_t *app, const rubraview_drop_item_t *items, size_t count) {
    if (count == 0) return;

    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, count, sizeof(rubraview_fs_entry_t));
    if (!proven_is_ok(res.err)) return;
    rubraview_fs_entry_t *entries = (rubraview_fs_entry_t*)(void*)res.value.ptr;

    size_t n = 0;
    for (size_t i = 0; i < count; ++i) {
        if (items[i].is_directory) continue;
        rubraview_fs_entry_t entry;
        if (!rubraview_pal_fs_stat(app->arena, items[i].path, &entry)) continue;
        if (entry.is_directory) continue;
        entries[n++] = entry;
    }
    if (n == 0) return;

    char line[96];
    size_t pos = 0;
    line[0] = '\0';
    append_number(line, sizeof(line), &pos, n);
    append_text(line, sizeof(line), &pos, " dropped files opened as one set");
    open_set_from_entries(app, entries, n, cstr(line));
    recent_note_shown(app);
}

/* A given set of files becomes the page sequence — a drop, or what the
   picker picked. `said` goes to the OSD when it opens. */
/* Settings > Files > Sort by and Ascending: the order a folder's pages
   are turned in (owner, 2026-09-28). An archive keeps its natural order. */
rubraview_sort_mode_t folder_sort_mode(const app_state_t *app, bool *out_ascending) {
    *out_ascending = g_cli_descending >= 0 ? g_cli_descending == 0
                   : rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("sort_ascending")) > 0.5;
    if (g_cli_sort >= 0) return (rubraview_sort_mode_t)g_cli_sort;
    return rubraview_sort_mode_for_setting(
        (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("sort_mode"))));
}

/* `as_given` keeps the entries in the order they come in (a list a file
   was added to the end of); `start` is the file to open on, the first
   when it is empty or not among them. */
void open_set_ordered(app_state_t *app, rubraview_fs_entry_t *entries, size_t count, proven_u8str_view_t said,
                             bool as_given, proven_u8str_view_t start) {
    if (!entries || count == 0) return;

    history_remember(app);
    source_close(app);

    rubraview_fs_listing_t listing = { .entries = entries, .count = count };
    bool ascending = true;
    rubraview_sort_mode_t mode = folder_sort_mode(app, &ascending);
    if (as_given) mode = RUBRAVIEW_SORT_AS_GIVEN;
    app->list_is_set = true;   /* a playlist or picked files: the playlist window says so */
    char kinds_buf[1024];   /* 1.25.0: text files too while the text viewer is on (a sorted folder kept them out) */
    app->source = rubraview_page_source_from_listing(app->arena, &listing,
                                                     kinds_with_text(kinds_buf, sizeof(kinds_buf), IMAGE_FILTER ";" MEDIA_FILTER), mode, ascending);
    if (app->source.page_count == 0) {
        osd_say(app, TU8("none of those files is a picture or a film"));
        return;
    }
    /* The folder of the first one is where Rename, Delete and a batch
       run work; a set of files has no folder of its own. */
    app->source_dir = rubraview_path_dirname(entries[0].path);
    for (size_t i = 0; i < count; ++i) {   /* RV-157: a station has no folder; the first file's */
        if (!is_station_path(entries[i].path)) { app->source_dir = rubraview_path_dirname(entries[i].path); break; }
    }
    app->resume_offer = false;
    app->picker_open = false;
    int32_t first = start.size > 0 ? rubraview_page_source_find(&app->source, start) : -1;
    finish_open(app, first > 0 ? (size_t)first : 0);
    osd_say(app, said);
}

void open_set_from_entries(app_state_t *app, rubraview_fs_entry_t *entries, size_t count, proven_u8str_view_t said) {
    open_set_ordered(app, entries, count, said, false, PROVEN_LIT(""));
}

/* ---- D-86: what Explorer's menu asks ---- */

/* The files named, as one set of pages. */
static void open_paths_as_set(app_state_t *app, const proven_u8str_view_t *paths, size_t count, const char *what) {
    if (!subs_may_leave(app)) return;
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, count, sizeof(rubraview_fs_entry_t));
    if (!proven_is_ok(res.err)) return;
    rubraview_fs_entry_t *entries = (rubraview_fs_entry_t*)(void*)res.value.ptr;
    size_t n = 0;
    for (size_t i = 0; i < count; ++i) {
        rubraview_fs_entry_t entry;
        if (!rubraview_pal_fs_stat(app->arena, paths[i], &entry) || entry.is_directory) continue;
        entries[n++] = entry;
    }
    if (n == 0) { osd_say(app, TU8("none of those files could be read")); return; }
    char line[96];
    snprintf(line, sizeof(line), "%zu %s", n, what);
    open_set_from_entries(app, entries, n, cstr(line));
    recent_note_shown(app);
}

bool path_is_listed_kind(proven_u8str_view_t path) {
    char kinds_buf[1024];
    if (is_station_path(path)) return true;   /* RV-157 */
    return rubraview_glob_match_list(rubraview_path_basename(path), kinds_with_text(kinds_buf, sizeof(kinds_buf), IMAGE_FILTER ";" MEDIA_FILTER));
}

/* "Add to the list": the pages being read and the files named, as one
   set. A folder being read becomes a set of its files; a book has no
   files to add to, so the files are opened as a set of their own.

   D-100: the files go to the end of the list (Settings > General > "A
   file added to the list goes": end, or sorted with the rest). `show`
   turns to the first of them — what a file opened from Explorer asks —
   and without it the reader stays on the page being read. */
static void list_add_paths(app_state_t *app, const proven_u8str_view_t *paths, size_t count, bool show) {
    bool files_open = page_count(app) > 0 && app->source.archive_path.size == 0;
    if (!files_open) {
        open_paths_as_set(app, paths, count, count == 1 ? T("file opened as a list") : T("files opened as a list"));
        return;
    }
    bool at_end = lround(rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("added_order"))) == 0;

    size_t have = page_count(app);
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, have + count, sizeof(rubraview_fs_entry_t));
    if (!proven_is_ok(res.err)) return;
    rubraview_fs_entry_t *entries = (rubraview_fs_entry_t*)(void*)res.value.ptr;
    size_t n = 0, added = 0;
    for (size_t i = 0; i < have; ++i) {
        if (at_end || is_station_path(app->source.pages[i].path)) {   /* RV-158: a station cannot be asked about */
            /* Kept as they stand: nothing of a file but its name is needed, so none is asked about. */
            entries[n++] = (rubraview_fs_entry_t){ .name = app->source.pages[i].name, .path = app->source.pages[i].path };
        } else if (rubraview_pal_fs_stat(app->arena, app->source.pages[i].path, &entries[n]) && !entries[n].is_directory) {
            n++;
        }
    }
    size_t kept = n;
    proven_u8str_view_t first_named = PROVEN_LIT("");   /* the first of them that is, or now will be, in the list */
    for (size_t i = 0; i < count; ++i) {
        rubraview_fs_entry_t entry;
        if (!rubraview_pal_fs_stat(app->arena, paths[i], &entry) || entry.is_directory) continue;
        if (!path_is_listed_kind(entry.path)) continue;
        bool there = false;
        for (size_t k = 0; k < n && !there; ++k) {
            there = rubraview_path_same(entries[k].path, entry.path);
            if (there && first_named.size == 0) first_named = entries[k].path;
        }
        if (there) continue;
        entry.path = app_keep(app, entry.path);
        entry.name = rubraview_path_basename(entry.path);
        if (first_named.size == 0) first_named = entry.path;
        entries[n++] = entry;
        added++;
    }

    /* Several files opened at once in Explorer arrive one launch after
       another: the first is turned to, the rest only join the list. */
    double now = rubraview_pal_time_now_seconds();
    if (show && now - app->added_shown_at < RUBRAVIEW_SHELLREQ_GATHER_SECONDS) show = false;
    if (show) app->added_shown_at = now;

    if (added == 0) {
        int32_t page = show && first_named.size > 0 ? rubraview_page_source_find(&app->source, first_named) : -1;
        if (page >= 0) {
            if (page != current_page_index(app)) {
                go_to_spread(app, spread_for_chosen_page(app, page));
                update_precache(app);
            }
        } else {
            osd_say(app, TU8("those files are in the list already"));
        }
        return;
    }

    /* The page being read, and where a film or a song on it had got to. */
    int32_t at = current_page_index(app);
    proven_u8str_view_t reading = at >= 0 && (size_t)at < have ? app->source.pages[at].path : PROVEN_LIT("");
    bool playing = !show && app->media != NULL && app->media_page == at;
    double position = playing ? play_position(app) : 0.0;
    bool paused = app->media_paused;

    char line[96];
    snprintf(line, sizeof(line), T("%zu added to the list (%zu in it)"), added, kept + added);
    open_set_ordered(app, entries, n, cstr(line), at_end, show ? first_named : reading);
    if (show) recent_note_shown(app);

    if (playing && app->media && position > 1.0) {
        play_seek(app, position);
        if (paused && !app->media_paused) media_toggle_pause(app);
    }
}

/* RV-158: a station typed into the list window's box joins the list at its end, by its title, and plays. */
static void list_add_station(app_state_t *app, proven_u8str_view_t address, proven_u8str_view_t title) {
    proven_u8str_view_t path = app_keep(app, address);
    proven_u8str_view_t name = title.size > 0 ? app_keep(app, title) : rubraview_stream_display_name(path);
    bool files_open = page_count(app) > 0 && app->source.archive_path.size == 0;
    size_t have = files_open ? page_count(app) : 0;
    for (size_t i = 0; i < have; ++i) {
        if (!proven_u8str_view_eq(app->source.pages[i].path, path)) continue;
        osd_say(app, TU8("that station is in the list already"));
        go_to_spread(app, spread_for_chosen_page(app, (int32_t)i));
        return;
    }
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, have + 1, sizeof(rubraview_fs_entry_t));
    if (!proven_is_ok(res.err)) return;
    rubraview_fs_entry_t *entries = (rubraview_fs_entry_t*)(void*)res.value.ptr;
    for (size_t i = 0; i < have; ++i) entries[i] = (rubraview_fs_entry_t){ .name = app->source.pages[i].name, .path = app->source.pages[i].path };
    entries[have] = (rubraview_fs_entry_t){ .name = name, .path = path };
    char line[96];
    snprintf(line, sizeof(line), T("%zu added to the list (%zu in it)"), (size_t)1, have + 1);
    open_set_ordered(app, entries, have + 1, cstr(line), true, path);
}

void address_begin(app_state_t *app) {
    if (app->rename_active) rename_end(app);
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    rubraview_textedit_set(&app->rename_edit, PROVEN_LIT("http://"));
    app->rename_active = true;
    app->rename_is_address = true;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, true);
    osd_say(app, TU8("a station's address, then a space and its title if you like (Enter adds it, Esc leaves it)"));
}

static void address_typed(app_state_t *app) {
    proven_u8str_view_t text = rubraview_textedit_text(&app->rename_edit);
    proven_u8str_view_t address = {0}, title = {0};
    bool ok = rubraview_stream_split_typed(text, &address, &title);
    char a[RUBRAVIEW_STREAM_URL_MAX], t[512];
    snprintf(a, sizeof(a), "%.*s", ok ? (int)address.size : 0, ok ? (const char *)address.ptr : "");
    snprintf(t, sizeof(t), "%.*s", ok ? (int)title.size : 0, ok ? (const char *)title.ptr : "");
    rename_end(app);
    if (!ok) { osd_say(app, TU8("not a station's address: http, https or mms, then ://")); return; }
    list_add_station(app, cstr(a), cstr(t));
}

static void panel_open_print(app_state_t *app);
static void print_devices_load(app_state_t *app);
static void print_thumbs_load(app_state_t *app);
static void print_thumbs_drop(app_state_t *app);

/* D-87: these pictures are to be printed: how is asked first, in the panel. */
static void print_paths(app_state_t *app, const proven_u8str_view_t *paths, size_t count) {
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, count ? count : 1, sizeof(proven_u8str_view_t));
    if (!proven_is_ok(res.err) || count == 0) return;
    app->print_list = (proven_u8str_view_t*)(void*)res.value.ptr;
    for (size_t i = 0; i < count; ++i) app->print_list[i] = paths[i];
    app->print_list_count = count;
    if (app->panel.open) panel_close(app);
    app->print_preview_sheet = 0;
    print_devices_load(app);   /* 1.19.0 */
    print_thumbs_load(app);
    panel_open_print(app);
}


/* D-100: a file opened from Explorer — a double click, the menu's Open,
   a launch with files named. With files already being read it joins the
   end of their list and is turned to (owner, 2026-10-11: "현재 재생목록
   맨끝에 이미지가 추가되면서 새 파일로 열리게"); the first file a window is
   given brings its folder ("맨 처음 열릴때만 같은 폴더 파일들을"). Both are
   settings: General > "A file given to the open window" and "The first
   file brings its folder". A book, a folder or a playlist is not a file
   of a list: it is opened in place of what was open. */
static void explorer_open(app_state_t *app, const proven_u8str_view_t *paths, size_t count) {
    enum { GIVEN_ADD = 0, GIVEN_FOLDER, GIVEN_ONLY };
    int given = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("explorer_open")));
    bool files_open = page_count(app) > 0 && app->source.archive_path.size == 0;
    bool listed = true;
    for (size_t i = 0; i < count && listed; ++i) listed = path_is_listed_kind(paths[i]);

    if (!listed) {
        if (count == 1) open_path(app, paths[0]);
        else open_paths_as_set(app, paths, count, T("files opened as one set"));
        return;
    }
    if (given == GIVEN_ADD && files_open) {
        list_add_paths(app, paths, count,
                       rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("added_shows")) > 0.5);
        return;
    }
    if (count > 1) { open_paths_as_set(app, paths, count, T("files opened as one set")); return; }

    bool with_folder = given == GIVEN_FOLDER ||
                       (given == GIVEN_ADD && rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("first_with_folder")) > 0.5);
    if (with_folder) open_path(app, paths[0]);
    else open_paths_as_set(app, paths, count, T("file opened on its own"));
}

static void shell_request_act(app_state_t *app, rubraview_shell_verb_t verb, const proven_u8str_view_t *paths, size_t count) {
    if (count == 0) return;
    switch (verb) {
        case RUBRAVIEW_SHELL_VERB_NONE:
        case RUBRAVIEW_SHELL_VERB_OPEN:
        case RUBRAVIEW_SHELL_VERB_NEW_WINDOW:   /* its window is this one, just made */
            explorer_open(app, paths, count);
            break;
        case RUBRAVIEW_SHELL_VERB_OPEN_ONLY:
            if (count == 1 && !path_is_listed_kind(paths[0])) open_path(app, paths[0]);   /* a book, a folder */
            else open_paths_as_set(app, paths, count, count == 1 ? T("file opened on its own") : T("files opened as one set"));
            break;
        case RUBRAVIEW_SHELL_VERB_ADD:
            list_add_paths(app, paths, count, false);
            break;
        case RUBRAVIEW_SHELL_VERB_BROWSE:
            open_path(app, paths[0]);
            if (app->source.archive_path.size > 0 && rubraview_path_same(app->source.archive_path, paths[0])) {
                app->picker_dir = PROVEN_LIT("");
                picker_open(app);
            }
            break;
        case RUBRAVIEW_SHELL_VERB_CONVERT: {
            proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, count, sizeof(proven_u8str_view_t));
            if (!proven_is_ok(res.err)) break;
            app->batch_list = (proven_u8str_view_t*)(void*)res.value.ptr;
            for (size_t i = 0; i < count; ++i) app->batch_list[i] = paths[i];
            app->batch_list_count = count;
            if (app->panel.open) panel_close(app);
            panel_open_batch(app);
            break;
        }
        case RUBRAVIEW_SHELL_VERB_PRINT:
            print_paths(app, paths, count);
            break;
        default:
            break;
    }
}

/* What was gathered, acted on: the paths copied where they outlive it. */
static void shell_requests_flush(app_state_t *app) {
    rubraview_shellreq_collector_t *c = &app->shell_requests;
    if (!c->pending) return;
    rubraview_shell_verb_t verb = c->verb;
    size_t count = 0;
    proven_u8str_view_t *paths = NULL;
    proven_result_mem_mut_t list = rubraview_arena_alloc_array(app->arena, c->count ? c->count : 1, sizeof(proven_u8str_view_t));
    proven_result_mem_mut_t text = proven_arena_alloc(app->arena, c->text_len + 1);
    if (proven_is_ok(list.err) && proven_is_ok(text.err)) {
        paths = (proven_u8str_view_t*)(void*)list.value.ptr;
        char *copy = (char*)(void*)text.value.ptr;
        memcpy(copy, c->text, c->text_len);
        for (const char *p = rubraview_shellreq_next(c, NULL); p && count < c->count; p = rubraview_shellreq_next(c, p)) {
            paths[count++] = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)copy + (p - c->text), .size = strlen(p) };
        }
    }
    rubraview_shellreq_clear(c);
    if (paths) shell_request_act(app, verb, paths, count);
}

/* One request into the gathering; one with another verb waiting goes first. */
static void shell_request_take(app_state_t *app, rubraview_shell_verb_t verb, bool whole, const proven_u8str_view_t *paths, size_t count) {
    double now = rubraview_pal_time_now_seconds();
    if (!rubraview_shellreq_collect(&app->shell_requests, verb, whole, paths, count, now)) {
        shell_requests_flush(app);
        (void)rubraview_shellreq_collect(&app->shell_requests, verb, whole, paths, count, now);
    }
}

/* Once a pass of the main loop: what other launches handed over. */
static size_t shell_requests_pump(app_state_t *app) {
    size_t handled = 0;
    char *blob = NULL;
    size_t len = 0;
    while (rubraview_pal_instance_take_request(&blob, &len)) {
        rubraview_shell_verb_t verb = RUBRAVIEW_SHELL_VERB_NONE;
        bool whole = false;
        size_t count = 0;
        if (rubraview_shellreq_unpack(blob, len, &verb, &whole, NULL, 0, &count) && count > 0) {
            proven_u8str_view_t *paths = (proven_u8str_view_t*)rubraview_malloc_product(count, sizeof(proven_u8str_view_t), 1);
            if (paths && rubraview_shellreq_unpack(blob, len, &verb, &whole, paths, count, &count)) {
                /* A plain launch is one thing and is not waited on. */
                shell_request_take(app, verb, whole || verb == RUBRAVIEW_SHELL_VERB_NONE, paths, count);
            }
            free(paths);
        }
        free(blob);
        handled++;
    }
    if (rubraview_shellreq_ready(&app->shell_requests, rubraview_pal_time_now_seconds())) {
        shell_requests_flush(app);
        handled++;
    }
    return handled;
}

static void handle_drop(app_state_t *app, const rubraview_window_event_t *event) {
    rubraview_drop_item_t items[16];
    size_t count = event->drop.count < 16 ? event->drop.count : 16;
    if (count == 0) return;   /* nothing was dropped: there is no first item to open */

    /* D-111: let go over the list window, they join the list at that row. */
    if (event->kind == RUBRAVIEW_WINDOW_EVENT_DROP && event->drop.has_point && app->listwin.open && list_editable(app) &&
        !app->picker_open && rubraview_rect_contains(rubraview_listwin_rect(&app->listwin), event->drop.x, event->drop.y)) {
        proven_u8str_view_t paths[16];
        for (size_t i = 0; i < count; ++i) paths[i] = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)event->drop.paths[i], .size = event->drop.path_lengths[i] };
        listwin_sync(app);
        list_insert_paths(app, paths, count, list_viewed(app) ? page_count(app) : rubraview_listwin_insert_index(&app->listwin, event->drop.y, page_count(app)));
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        items[i].path = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)event->drop.paths[i], .size = event->drop.path_lengths[i] };
        /* A directory has no extension and does list: asking the
           filesystem is cheaper than guessing from the name. */
        rubraview_fs_listing_t probe = rubraview_pal_fs_list_dir(app->arena, items[i].path);
        items[i].is_directory = probe.count > 0;
    }

    switch (rubraview_drop_classify(items, count)) {
        case RUBRAVIEW_DROP_OPEN_FILE:
        case RUBRAVIEW_DROP_OPEN_FOLDER:
            open_path(app, items[0].path);
            break;
        case RUBRAVIEW_DROP_PLAYLIST:
            /* §3.19.2: several things dropped together become a sequence
               of exactly those things — not the folder they came from,
               which would show files the reader did not drop. */
            open_dropped_files(app, items, count);
            break;
        case RUBRAVIEW_DROP_NOTHING:
        default:
            break;
    }
}

/* ---- the workbench and the two dialogs (§3.13, §3.10, §3.11) ---- */


/* ---- RV-133: classifying (owner, 2026-10-17) ----
   A window of the nine curation folders. A click on one, or its digit,
   sets the picture on screen aside for it and goes on to the next; Copy
   all or Move all then puts every one in its folder at once, a name never
   replaced (name-1.ext), each move undone with Ctrl+Z (the last 64). */

static void classify_build(app_state_t *app) {
    static char words[13][192];
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    bool placed = app->classify_panel.placed;
    double px = app->classify_panel.place_x, py = app->classify_panel.place_y;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    app->classify_panel = rubraview_panel_create(TU8("Classify"), dpi);
    app->classify_panel.bounds.width = 360.0 * dpi;
    app->classify_panel.place_x = placed ? px : (double)w - 372.0 * dpi;
    app->classify_panel.place_y = placed ? py : 90.0 * dpi;
    app->classify_panel.placed = true;
    const rubraview_classify_t *c = app->classify;
    proven_u8str_view_t here = current_file_path(app);
    int32_t mark = c ? rubraview_classify_folder_of(c, here) : 0;
    int n;
    size_t k = 0;
    bool any = false;
    for (int32_t d = 1; d <= 9; ++d) {
        proven_u8str_view_t dir = rubraview_curation_target(&app->curation, d);
        if (dir.size == 0) continue;
        any = true;
        proven_u8str_view_t name = rubraview_path_basename(dir);
        n = snprintf(words[k], sizeof(words[k]), "%s%d  %.*s  (%zu)", d == mark ? "> " : "  ", (int)d,
                     (int)(name.size < 120 ? name.size : 120), name.ptr, c ? rubraview_classify_count(c, d) : (size_t)0);
        rubraview_panel_add_button(&app->classify_panel, PANEL_CLS_FOLDER + d, (proven_u8str_view_t){ (const proven_byte_t *)words[k], n > 0 ? (size_t)n : 0 });
        ++k;
    }
    if (!any) rubraview_panel_add_button(&app->classify_panel, PANEL_CLS_SETTINGS, TU8("No folders yet: set them in Settings > Files..."));
    rubraview_panel_add_separator(&app->classify_panel);
    if (mark) {
        n = snprintf(words[10], sizeof(words[10]), T("This one goes to %d: press to take it off"), (int)mark);
        rubraview_panel_add_button(&app->classify_panel, PANEL_CLS_HERE, (proven_u8str_view_t){ (const proven_byte_t *)words[10], n > 0 ? (size_t)n : 0 });
    } else {
        rubraview_panel_add_button(&app->classify_panel, PANEL_CLS_HERE, TU8("This one is not set aside (a folder or its digit)"));
    }
    size_t all = c ? rubraview_classify_count(c, 0) : 0;
    n = snprintf(words[11], sizeof(words[11]), T("Copy all set aside (%zu)"), all);
    rubraview_panel_add_button(&app->classify_panel, PANEL_CLS_COPY, (proven_u8str_view_t){ (const proven_byte_t *)words[11], n > 0 ? (size_t)n : 0 });
    n = snprintf(words[12], sizeof(words[12]), T("Move all set aside (%zu)"), all);
    rubraview_panel_add_button(&app->classify_panel, PANEL_CLS_MOVE, (proven_u8str_view_t){ (const proven_byte_t *)words[12], n > 0 ? (size_t)n : 0 });
    rubraview_panel_add_button(&app->classify_panel, PANEL_CLS_CLEAR, TU8("Forget what is set aside"));
    if (any) rubraview_panel_add_button(&app->classify_panel, PANEL_CLS_SETTINGS, TU8("The folders: Settings > Files..."));
    app->classify_panel.open = true;
    rubraview_panel_layout(&app->classify_panel, (double)w, (double)h);
    app->classify_shown_page = current_page_index(app);
}

static void classify_toggle(app_state_t *app) {
    if (app->classify_panel.open) { app->classify_panel.open = false; return; }
    if (!app->classify) {
        app->classify = (rubraview_classify_t*)calloc(1, sizeof(*app->classify));
        if (!app->classify) { osd_say(app, TU8("out of memory")); return; }
    }
    classify_build(app);
}

static void classify_mark(app_state_t *app, int32_t digit) {
    if (!app->classify) return;
    if (app->source.kind != RUBRAVIEW_PAGE_SOURCE_FOLDER) { osd_say(app, TU8("a page inside an archive is not a file of its own: it cannot be set aside")); return; }
    proven_u8str_view_t path = current_file_path(app);
    if (path.size == 0) return;
    int32_t was = rubraview_classify_folder_of(app->classify, path);
    if (!rubraview_classify_set(app->classify, was ? path : app_keep(app, path), digit)) {
        osd_say(app, TU8("the list is full: copy or move what is set aside first"));
        return;
    }
    char said[96];
    int n = snprintf(said, sizeof(said), T("set aside for %d (%zu in all)"), (int)digit, rubraview_classify_count(app->classify, 0));
    osd_say(app, (proven_u8str_view_t){ (const proven_byte_t *)said, n > 0 ? (size_t)n : 0 });
    if ((size_t)current_page_index(app) + 1 < page_count(app)) handle_action(app, PROVEN_LIT("next_page"));
    classify_build(app);
}

static void classify_run(app_state_t *app, bool moving) {
    rubraview_classify_t *c = app->classify;
    if (!c || c->count == 0) { osd_say(app, TU8("nothing is set aside")); return; }
    size_t done = 0, failed = 0;
    for (size_t i = 0; i < c->count;) {
        proven_u8str_view_t path = c->paths[i];
        proven_u8str_view_t dir = rubraview_curation_target(&app->curation, c->folders[i]);
        bool ok = false;
        if (dir.size > 0 && (rubraview_pal_fs_make_dirs(dir) || rubraview_pal_fs_exists(dir))) {
            if (moving && rubraview_path_same(rubraview_path_dirname(path), dir)) ok = true;   /* already there */
            else {
                proven_u8str_view_t target = rubraview_filing_target(app->arena, dir, rubraview_path_basename(path), filing_exists, NULL);
                ok = target.size > 0 && (moving ? rubraview_pal_fs_move(path, target) : rubraview_pal_fs_copy(path, target));
                if (ok) rubraview_undo_push(&app->undo, (rubraview_file_action_t){
                    .op = moving ? RUBRAVIEW_FILE_OP_MOVE : RUBRAVIEW_FILE_OP_COPY,
                    .source_path = path, .target_path = target, .playlist_index = 0 });
            }
        }
        if (ok) { rubraview_classify_remove_at(c, i); ++done; }
        else { ++failed; ++i; }   /* kept, still set aside */
    }
    char said[160];
    int n = failed ? snprintf(said, sizeof(said), moving ? T("moved %zu; %zu could not be put there and are still set aside") : T("copied %zu; %zu could not be put there and are still set aside"), done, failed)
                   : snprintf(said, sizeof(said), moving ? T("moved %zu") : T("copied %zu"), done);
    osd_say(app, (proven_u8str_view_t){ (const proven_byte_t *)said, n > 0 ? (size_t)n : 0 });
    if (moving && done > 0) {
        int32_t page = current_page_index(app);
        reopen_after_removal(app, page > 0 ? (size_t)page : 0);
    }
    classify_build(app);
}

static bool classify_press(app_state_t *app, double px, double py) {
    rubraview_panel_t *box = &app->classify_panel;
    if (!box->open || !rubraview_rect_contains(box->bounds, px, py)) return false;
    if (rubraview_panel_title_hit(box, px, py)) { rubraview_panel_move_begin(box, px, py); return true; }
    int32_t row = -1;
    rubraview_panel_event_t ev = rubraview_panel_press(box, px, py, &row);
    if (ev == RUBRAVIEW_PANEL_CLOSE) { box->open = false; return true; }
    if (ev != RUBRAVIEW_PANEL_BUTTON_PRESSED || row < 0) return true;
    int32_t id = box->rows[row].id;
    if (id > PANEL_CLS_FOLDER && id <= PANEL_CLS_FOLDER + 9) { classify_mark(app, id - PANEL_CLS_FOLDER); return true; }
    switch (id) {
        case PANEL_CLS_HERE:
            if (app->classify && rubraview_classify_set(app->classify, current_file_path(app), 0)) classify_build(app);
            break;
        case PANEL_CLS_COPY:  classify_run(app, false); break;
        case PANEL_CLS_MOVE:  classify_run(app, true); break;
        case PANEL_CLS_CLEAR:
            if (app->classify) rubraview_classify_clear(app->classify);
            osd_say(app, TU8("nothing is set aside now"));
            classify_build(app);
            break;
        case PANEL_CLS_SETTINGS: settings_open(app); break;
        default: break;
    }
    return true;
}


/* The panel is placed against the window, so it is placed again whenever
   the window changes size. */
void panel_relayout(app_state_t *app) {
    if (!app->panel.open) return;
    if (app->edit_window && edit_panel_open(app)) { edit_window_layout(app); return; }   /* RV-137: laid out in its own window */
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    if (w > 0 && h > 0) rubraview_panel_layout(&app->panel, (double)w, (double)h);
}

void edit_preview_close(app_state_t *app);
void edit_preview_open(app_state_t *app);
void media_reopen_on_new_device(app_state_t *app);

static void arc_ask_clear(app_state_t *app);
static void print_temp_drop(app_state_t *app);

void px_edit_close(app_state_t *app);

/* RV-137 (owner, 2026-10-18: "선택 영역 곧바로 잘라서 클립보드에 복사"): the picture at
   its own size with the changes in force, cut to the chosen area (an ellipse's
   corners left clear), put on the clipboard; no area, the whole picture. */
static void edit_copy_area(app_state_t *app) {
    int32_t page = current_page_index(app);
    if (page < 0 || (size_t)page >= page_count(app) || !app->pages[page].loaded) { osd_say(app, TU8("there is no picture to copy")); return; }
    int32_t w = real_w(&app->pages[page]), h = real_h(&app->pages[page]);
    if (w <= 0 || h <= 0 || (uint64_t)w * (uint64_t)h > (uint64_t)100000000u) { osd_say(app, TU8("this picture is too large to copy")); return; }
    rubraview_edit_steps_t steps = app->edit_steps;
    steps.count = steps.cursor;   /* the changes in force, the one in hand among them */
    /* RV-141 (4): the picture read into memory of its own, then each step from one lane into
       the other, the one before given back as the next is made — not a picture's size a step. */
    size_t pixels = 0, first = 0, biggest = 0;
    if (!rubraview_size_product(&pixels, (size_t)w, (size_t)h, 4u)) { osd_say(app, TU8("this picture is too large to copy")); return; }
    biggest = pixels;
    for (size_t k = 1; k <= steps.count; ++k) {
        int32_t sw = 0, sh = 0;
        size_t b = 0;
        rubraview_edit_steps_size(&steps, k, &sw, &sh);
        if (sw > 0 && sh > 0 && rubraview_size_product(&b, (size_t)sw, (size_t)sh, 4u) && b > biggest) biggest = b;
    }
    size_t lane = 0, big = 0;
    if (!rubraview_size_product(&first, pixels, 3u, 1u) || !rubraview_size_product(&lane, biggest, 6u, 1u) ||
        PROVEN_CKD_ADD(&first, first, (size_t)(16u << 20)) || PROVEN_CKD_ADD(&lane, lane, (size_t)(16u << 20)) ||
        PROVEN_CKD_ADD(&big, first, lane) || PROVEN_CKD_ADD(&big, big, lane)) { osd_say(app, TU8("this picture is too large to copy")); return; }
    void *memory = malloc(big);
    if (!memory) { osd_say(app, TU8("there is not memory enough to copy this picture")); return; }
    proven_arena_t scratch = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)memory, .size = first });
    proven_arena_t lanes[2] = {
        proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)memory + first, .size = lane }),
        proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)memory + first + lane, .size = lane }),
    };
    rubraview_pixbuf_t full = {0};
    proven_u8str_view_t path = app->source.pages[page].path;
    if (path.size > 0) {
        full = rubraview_pal_image_read_pixels_within(&scratch, path, NULL, 0, true, 0, 0);
    } else {
        rubraview_page_bytes_t bytes = { .ok = false };
        AcquireSRWLockExclusive(&app->source_lock);
        bytes = rubraview_page_source_read(&scratch, &app->source, (size_t)page, MAX_PAGE_BYTES);
        ReleaseSRWLockExclusive(&app->source_lock);
        if (bytes.ok && bytes.data.size > 0)
            full = rubraview_pal_image_read_pixels_within(&scratch, PROVEN_LIT(""), (const uint8_t*)bytes.data.ptr, bytes.data.size, true, 0, 0);
    }
    rubraview_pixbuf_t done = full;
    for (size_t k = 0; k < steps.count && rubraview_pixbuf_is_valid(&done); ++k) {
        proven_arena_t *into = &lanes[k % 2u];
        proven_arena_reset(into);
        rubraview_pixbuf_t next = rubraview_edit_step_apply(into, &steps.steps[k], &done, 1.0);
        if (rubraview_pixbuf_is_valid(&next)) done = next; else done = (rubraview_pixbuf_t){0};
    }
    proven_arena_t *out_arena = &lanes[steps.count % 2u];   /* the lane not holding the result */
    proven_arena_reset(out_arena);
    if (steps.count == 0) out_arena = &lanes[0];
    rubraview_pixbuf_t bgra = !rubraview_pixbuf_is_valid(&done) || done.format == RUBRAVIEW_PIXFMT_BGRA8
        ? done : rubraview_pixbuf_convert(out_arena, &done, RUBRAVIEW_PIXFMT_BGRA8);
    const rubraview_crop_rect_t *c = &app->edit.crop;
    if (rubraview_pixbuf_is_valid(&bgra) && app->edit.crop_active && c->width >= 1 && c->height >= 1 &&
        c->x + c->width <= bgra.width && c->y + c->height <= bgra.height) {
        rubraview_pixbuf_t cut = rubraview_pixbuf_crop(bgra.pixels == done.pixels ? out_arena : &scratch, &bgra, c->x, c->y, c->width, c->height);
        if (rubraview_pixbuf_is_valid(&cut) && app->sel_shape == RUBRAVIEW_AREA_ELLIPSE) {
            double rx = cut.width / 2.0, ry = cut.height / 2.0;
            for (int32_t y = 0; y < cut.height; ++y) {
                uint8_t *row = cut.pixels + (size_t)y * (size_t)cut.stride;
                for (int32_t x = 0; x < cut.width; ++x) {
                    double dx = (x + 0.5 - rx) / rx, dy = (y + 0.5 - ry) / ry;
                    if (dx * dx + dy * dy > 1.0) row[x * 4 + 3] = 0;   /* outside the ellipse: clear */
                }
            }
        }
        bgra = cut;
    }
    bool copied = rubraview_pixbuf_is_valid(&bgra) &&
                  rubraview_pal_clipboard_set_image(app->window, bgra.pixels, bgra.width, bgra.height, (size_t)bgra.stride);
    free(memory);
    osd_say(app, !copied ? TU8("the picture could not be copied")
                 : app->edit.crop_active ? TU8("the area is on the clipboard") : TU8("the picture is on the clipboard"));
}

void sel_mode_set(app_state_t *app, bool on) {
    app->sel_mode = on;
    rubraview_pal_window_set_cursor_shape(app->window, on ? RUBRAVIEW_CURSOR_CROSS : RUBRAVIEW_CURSOR_ARROW);
}

void panel_close(app_state_t *app) {
    bool back_to_edit = app->export_from_edit && app->panel_is_export;
    app->export_from_edit = false;
    sel_mode_set(app, false);
    edit_window_close(app);
    edit_preview_close(app);
    app->sel_panel.open = false;
    app->steps_panel.open = false;
    app->sel_grab = 0;
    if (app->px_edit_open) px_edit_close(app);
    app->panel.open = false;
    app->panel.row_count = 0;
    app->panel_is_export = false;
    app->panel_is_batch = false;
    /* D-89: closed without its button, the question is answered "no". */
    if (app->panel_is_convert && app->arc_ask == ARC_ASK_FORMAT) {
        arc_ask_clear(app);
        osd_say(app, TU8("the archive is as it was"));
    }
    app->panel_is_convert = false;
    if (app->panel_is_print && !app->print_going) print_temp_drop(app);
    if (app->panel_is_print) print_thumbs_drop(app);   /* 1.19.0 */
    app->panel_is_print = false;
    if (back_to_edit) {   /* Export done or left: Edit again, with its edits */
        app->edit_kept = true;
        panel_open_edit(app);
    }
    toolbox_refresh(app);    /* Edit closed: the toolbox whole again */
}

/* §3.13's workbench. Every row's range comes from the edit session, so
   the panel cannot offer a value the commit layer would refuse. */
/* RV-108: the panel has two pages — the tones, and sharpness, cut and
   size — so that it is never taller than a small window. The rows are
   made from the session as it stands: turning the page changes nothing. */
static bool g_edit_page_two = false;

/* The size Save makes, in pixels, on the button that types it (owner, 2026-10-15). */
proven_u8str_view_t px_button_label(const app_state_t *app) {
    static char label[64];
    const rubraview_edit_session_t *e = &app->edit;
    int32_t w = e->resize_width, h = e->resize_height;
    if (w <= 0 || h <= 0) { w = e->image_width; h = e->image_height; }   /* the whole picture is resized (the owner's edit) */
    /* RV-128: and what it prints at, at the DPI below */
    double dpi = e->resize_dpi > 0.0 ? e->resize_dpi : 300.0;
    int n = snprintf(label, sizeof(label), T("Size: %d x %d px (%.1f x %.1f cm)..."), (int)w, (int)h, w / dpi * 2.54, h / dpi * 2.54);
    return n > 0 ? (proven_u8str_view_t){ .ptr = (const proven_byte_t *)label, .size = (size_t)n } : TU8("Size in pixels...");
}

/* The cut on the button that types it (owner, 2026-10-15). */
proven_u8str_view_t crop_button_label(const app_state_t *app) {
    static char label[64];
    const rubraview_edit_session_t *e = &app->edit;
    if (!e->crop_active) return TU8("Crop: type it, or drag on the picture...");
    int n = snprintf(label, sizeof(label), T("Crop: %d, %d  %d x %d px..."), (int)e->crop.x, (int)e->crop.y, (int)e->crop.width, (int)e->crop.height);
    return n > 0 ? (proven_u8str_view_t){ .ptr = (const proven_byte_t *)label, .size = (size_t)n } : TU8("Crop...");
}

/* The cut changed (a drag, Trim, the ratio): the buttons and the box say it again. */
static void crop_shown_again(app_state_t *app) {
    if (app->panel.open && !app->panel_is_export && !app->panel_is_batch && !app->panel_is_print && !app->panel_is_convert) {
        int32_t k = rubraview_panel_find(&app->panel, PANEL_CROP_PX);
        if (k >= 0) app->panel.rows[k].label = crop_button_label(app);
        k = rubraview_panel_find(&app->panel, PANEL_RESIZE_PX);
        if (k >= 0) app->panel.rows[k].label = px_button_label(app);
    }
    if (app->px_edit_open && app->px_edit_crop) px_edit_fill_crop(app);
}

/* D-87: how the pictures are printed, chosen when printing. The choices
   are settings (`print.*`), so the next print starts from the last. */
static rubraview_print_options_t print_options(const app_state_t *app) {
    rubraview_print_options_t o = {
        .turn = (rubraview_print_turn_t)(int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("turn"))),
        .size = (rubraview_print_size_t)(int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("size"))),
        .scale_percent = (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("scale"))),
        .place = (rubraview_print_place_t)(int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("place"))),
        .margin_mm = (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("margin_mm"))),
        .per_sheet = rubraview_print_per_sheet_of((int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("per_sheet")))),
    };
    rubraview_print_options_clamp(&o);
    return o;
}

/* ---- 1.19.0 (owner, 2026-10-18: "인쇄할 때 용지 선택 가능하게 하고 프리뷰로 어떻게 인쇄되는지 예상 그림
   보여주세요. 프린터 선택도 가능해야 해요."): the printer, its paper and the way up in the panel, a preview of
   the sheet beside it ---- */

static const app_state_t *g_print_app;   /* whose printers and papers the row words name */

static proven_u8str_view_t print_printer_name(const app_state_t *app) {
    if (app->print_printer < 0 || (size_t)app->print_printer >= app->print_printer_count) return PROVEN_LIT("");
    return cstr(app->print_printer_names[app->print_printer]);
}

static int32_t print_orientation(const app_state_t *app) {
    return (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("orientation")));
}

static void print_thumbs_drop(app_state_t *app) {
    for (size_t i = 0; i < 9; ++i) {
        if (app->print_thumbs[i]) rubraview_pal_texture_destroy(app->print_thumbs[i]);
        app->print_thumbs[i] = NULL;
    }
    app->print_thumb_count = 0;
}

/* The pictures of the sheet the preview shows, small, white under what is see-through. */
static void print_thumbs_load(app_state_t *app) {
    print_thumbs_drop(app);
    rubraview_print_options_t o = print_options(app);
    size_t sheets = rubraview_print_sheets(app->print_list_count, o.per_sheet);
    if (app->print_preview_sheet < 0 || (size_t)app->print_preview_sheet >= sheets) app->print_preview_sheet = 0;
    size_t first = (size_t)app->print_preview_sheet * (size_t)o.per_sheet;
    rubraview_scratch_t sc;
    proven_arena_t *sa = scratch_begin(app, &sc, 16u << 20);
    for (size_t k = 0; k < (size_t)o.per_sheet && first + k < app->print_list_count && k < 9; ++k) {
        proven_arena_reset(sa);
        rubraview_pixbuf_t px = rubraview_pal_image_read_pixels_within(sa, app->print_list[first + k], NULL, 0, true, 720, 720);
        if (!px.pixels || px.width <= 0 || px.height <= 0 || px.stride != px.width * 4) continue;
        bool rgba = px.format == RUBRAVIEW_PIXFMT_RGBA8;
        uint8_t *p = px.pixels;
        for (size_t i = 0; i < (size_t)px.width * (size_t)px.height; ++i, p += 4) {
            unsigned a = p[3], rest = 255u - a;
            uint8_t c0 = (uint8_t)((p[0] * a + 255u * rest + 127u) / 255u), c1 = (uint8_t)((p[1] * a + 255u * rest + 127u) / 255u),
                    c2 = (uint8_t)((p[2] * a + 255u * rest + 127u) / 255u);
            p[0] = rgba ? c2 : c0; p[1] = c1; p[2] = rgba ? c0 : c2; p[3] = 255;
        }
        rubraview_texture_t *t = rubraview_pal_texture_create_bgra(app->renderer, px.width, px.height);
        if (t && rubraview_pal_texture_upload_bgra(t, px.pixels, px.stride)) {
            app->print_thumbs[k] = t;
            app->print_thumb_w[k] = px.width;
            app->print_thumb_h[k] = px.height;
        } else if (t) {
            rubraview_pal_texture_destroy(t);
        }
    }
    app->print_thumb_count = 9;
    scratch_end(app, &sc);
}

static void print_sheet_load(app_state_t *app) {
    int32_t paper = app->print_paper >= 0 && (size_t)app->print_paper < app->print_paper_count ? app->print_papers[app->print_paper].id : 0;
    app->print_sheet_ok = app->print_printer >= 0 &&
                          rubraview_pal_printer_sheet(print_printer_name(app), paper, print_orientation(app), &app->print_sheet);
}

static void print_papers_load(app_state_t *app) {
    app->print_paper_count = 0;
    app->print_paper = -1;
    if (app->print_printer < 0) return;
    int32_t own = 0;
    app->print_paper_count = rubraview_pal_printer_papers(print_printer_name(app), app->print_papers, 160, &own);
    int32_t want = (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("paper")));
    for (size_t i = 0; i < app->print_paper_count; ++i) if (app->print_papers[i].id == want) app->print_paper = (int32_t)i;
    for (size_t i = 0; i < app->print_paper_count && app->print_paper < 0; ++i) if (app->print_papers[i].id == own) app->print_paper = (int32_t)i;
}

static void print_devices_load(app_state_t *app) {
    g_print_app = app;
    rubraview_scratch_t sc;
    proven_arena_t *sa = scratch_begin(app, &sc, 1u << 20);
    proven_u8str_view_t names[32];
    int32_t def = -1;
    size_t n = rubraview_pal_printers(sa, names, 32, &def);
    app->print_printer_count = 0;
    for (size_t i = 0; i < n; ++i) {
        if (names[i].size >= sizeof(app->print_printer_names[0])) continue;
        memcpy(app->print_printer_names[app->print_printer_count], names[i].ptr, names[i].size);
        app->print_printer_names[app->print_printer_count][names[i].size] = '\0';
        if ((int32_t)i == def) def = (int32_t)app->print_printer_count;
        app->print_printer_count++;
    }
    scratch_end(app, &sc);
    proven_u8str_view_t want = rubraview_settings_get_text(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("printer"));
    app->print_printer = def >= 0 && (size_t)def < app->print_printer_count ? def : (app->print_printer_count > 0 ? 0 : -1);
    for (size_t i = 0; i < app->print_printer_count; ++i)
        if (want.size > 0 && proven_u8str_view_eq(want, cstr(app->print_printer_names[i]))) app->print_printer = (int32_t)i;
    print_papers_load(app);
    print_sheet_load(app);
}

/* The sheet as it will come out, to the left of the panel: the paper, the part the printer reaches,
   the cells, and each picture where and how it goes — the same sums the printing does. */
void draw_print_preview(app_state_t *app) {
    if (!app->panel.open || !app->panel_is_print) return;
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    double dpi = rubraview_pal_window_dpi_scale(app->window), pad = 24.0 * dpi;
    double right = app->panel.bounds.x > (double)win_w * 0.3 ? app->panel.bounds.x : (double)win_w;
    rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ 0, 0, (double)win_w, (double)win_h }, 0xE0202020u, 0.0);
    if (!app->print_sheet_ok) {
        draw_outlined_text_aligned(app->renderer, app->print_printer < 0 ? TU8("no printer: Windows' own dialog prints")
                                                                        : TU8("the printer does not say its sheet"),
                                   (rubraview_pal_rect_t){ pad, pad, right - 2.0 * pad, 40.0 * dpi }, 16.0 * dpi, 2.0, 0xFFFFFFFFu, RUBRAVIEW_TEXT_LEFT);
        return;
    }
    const rubraview_print_sheet_t *sh = &app->print_sheet;
    double scale = 0, px = 0, py = 0;
    if (!rubraview_print_preview_fit(sh, pad, pad + 28.0 * dpi, right - 2.0 * pad, (double)win_h - 2.0 * pad - 28.0 * dpi, &scale, &px, &py)) return;
    rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ px + 4.0 * dpi, py + 4.0 * dpi, sh->paper_w * scale, sh->paper_h * scale }, 0x80000000u, 0.0);
    rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ px, py, sh->paper_w * scale, sh->paper_h * scale }, 0xFFFFFFFFu, 0.0);
    double ox = px + sh->print_x * scale, oy = py + sh->print_y * scale;
    rubraview_pal_render_stroke_rect(app->renderer, (rubraview_pal_rect_t){ ox, oy, sh->print_w * scale, sh->print_h * scale }, 0xFFB0B0B0u, 1.0, 0.0);
    rubraview_print_options_t o = print_options(app);
    for (int32_t k = 0; k < o.per_sheet && k < 9; ++k) {
        int32_t cx = 0, cy = 0, cw = 0, ch = 0;
        rubraview_print_cell(sh->print_w, sh->print_h, sh->dpi_x, sh->dpi_y, &o, k, &cx, &cy, &cw, &ch);
        if (o.per_sheet > 1)
            rubraview_pal_render_stroke_rect(app->renderer, (rubraview_pal_rect_t){ ox + cx * scale, oy + cy * scale, cw * scale, ch * scale }, 0xFFD8D8D8u, 1.0, 0.0);
        if ((size_t)k >= app->print_thumb_count || !app->print_thumbs[k]) continue;
        /* the picture's full size, so the sums are the printing's; the small texture stands for it */
        size_t index = (size_t)app->print_preview_sheet * (size_t)o.per_sheet + (size_t)k;
        int32_t w = 0, h = 0;
        if (index >= app->print_list_count || !rubraview_pal_image_size(app->print_list[index], &w, &h) || w <= 0 || h <= 0) {
            w = app->print_thumb_w[k]; h = app->print_thumb_h[k];
        }
        rubraview_print_placement_t pl;
        if (!rubraview_print_place(cx, cy, cw, ch, sh->dpi_x, sh->dpi_y, w, h, &o, &pl)) continue;
        /* 1.23.1: the part printed, drawn from its own corner (the renderer's region) onto where it goes;
           what is past the cell is cut away as the printing cuts it, so nothing leaves the paper */
        double r[4], mm[6];
        if (!rubraview_print_preview_region(&pl, w, h, app->print_thumb_w[k], app->print_thumb_h[k],
                                            ox + pl.dst_x * scale, oy + pl.dst_y * scale, pl.dst_w * scale, pl.dst_h * scale, r, mm)) continue;
        rubraview_src_rect_t src = { .left = r[0], .top = r[1], .right = r[2], .bottom = r[3] };
        rubraview_mat3x2_t m = { mm[0], mm[1], mm[2], mm[3], mm[4], mm[5] };
        rubraview_pal_render_draw_texture_region(app->renderer, app->print_thumbs[k], src, m, RUBRAVIEW_INTERP_LINEAR);
    }
    char line[200];
    size_t sheets = rubraview_print_sheets(app->print_list_count, o.per_sheet);
    double mm_w = sh->paper_w * 25.4 / sh->dpi_x, mm_h = sh->paper_h * 25.4 / sh->dpi_y;
    int n = snprintf(line, sizeof(line), T("sheet %d of %zu  -  %.0f x %.0f mm"), (int)app->print_preview_sheet + 1, sheets, mm_w, mm_h);
    if (n > 0) draw_outlined_text_aligned(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n },
                                          (rubraview_pal_rect_t){ pad, pad - 4.0 * dpi, right - 2.0 * pad, 28.0 * dpi }, 15.0 * dpi, 2.0, 0xFFFFFFFFu, RUBRAVIEW_TEXT_LEFT);
}

static void panel_open_print(app_state_t *app) {
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    app->panel = rubraview_panel_create(TU8("Print"), dpi);
    app->panel_is_export = false;
    app->panel_is_batch = false;
    app->panel_is_print = true;

    rubraview_print_options_t o = print_options(app);
    if (app->print_printer_count > 0) {   /* 1.19.0 */
        rubraview_panel_add_choice(&app->panel, PANEL_PRINT_PRINTER, TU8("Printer"), app->print_printer, (int32_t)app->print_printer_count);
        if (app->print_paper_count > 0)
            rubraview_panel_add_choice(&app->panel, PANEL_PRINT_PAPER, TU8("Paper"), app->print_paper < 0 ? 0 : app->print_paper, (int32_t)app->print_paper_count);
        rubraview_panel_add_choice(&app->panel, PANEL_PRINT_ORIENT, TU8("The sheet"), print_orientation(app), 3);
        rubraview_panel_add_slider(&app->panel, PANEL_PRINT_COPIES, TU8("Copies"),
                                   rubraview_settings_get(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("copies")), 1.0, 99.0, 1.0);
        rubraview_panel_add_separator(&app->panel);
    }
    rubraview_panel_add_choice(&app->panel, PANEL_PRINT_TURN, TU8("Turn"), (int32_t)o.turn, RUBRAVIEW_PRINT_TURN_COUNT);
    rubraview_panel_add_choice(&app->panel, PANEL_PRINT_SIZE, TU8("Size"), (int32_t)o.size, RUBRAVIEW_PRINT_SIZE_COUNT);
    rubraview_panel_add_slider(&app->panel, PANEL_PRINT_SCALE, TU8("Scale %"), (double)o.scale_percent,
                               (double)RUBRAVIEW_PRINT_SCALE_MIN, (double)RUBRAVIEW_PRINT_SCALE_MAX, 5.0);
    rubraview_panel_add_choice(&app->panel, PANEL_PRINT_PLACE, TU8("Place"), (int32_t)o.place, RUBRAVIEW_PRINT_PLACE_COUNT);
    rubraview_panel_add_slider(&app->panel, PANEL_PRINT_MARGIN, TU8("Margin mm"), (double)o.margin_mm,
                               0.0, (double)RUBRAVIEW_PRINT_MARGIN_MAX_MM, 1.0);
    rubraview_panel_add_choice(&app->panel, PANEL_PRINT_PER_SHEET, TU8("A sheet"),
                               rubraview_print_per_sheet_index(o.per_sheet), 5);
    size_t sheets = rubraview_print_sheets(app->print_list_count, o.per_sheet);
    if (sheets > 1) rubraview_panel_add_choice(&app->panel, PANEL_PRINT_SHEET, TU8("Preview of"), app->print_preview_sheet, (int32_t)(sheets < 999 ? sheets : 999));
    rubraview_panel_add_separator(&app->panel);
    static char label[64];
    if (app->print_printer_count == 0) {
        if (app->print_list_count == 1) snprintf(label, sizeof(label), T("Print..."));
        else snprintf(label, sizeof(label), T("Print the %zu chosen files..."), app->print_list_count);
    } else if (app->print_list_count == 1) {
        snprintf(label, sizeof(label), T("Print"));
    } else {
        snprintf(label, sizeof(label), T("Print the %zu chosen files"), app->print_list_count);
    }
    rubraview_panel_add_button(&app->panel, PANEL_PRINT_NOW, cstr(label));
    if (app->print_printer_count > 0) rubraview_panel_add_button(&app->panel, PANEL_PRINT_DIALOG, TU8("With Windows' print dialog..."));

    app->panel.open = true;
    panel_relayout(app);
}

static bool g_conv_is_7z;   /* D-89: which format the convert panel's method row is named for */

/* What a row of the print panel shows beside its name; empty for the others. */
proven_u8str_view_t print_row_text(const rubraview_panel_row_t *row, char *buffer, size_t size) {
    int n = 0;
    int32_t v = (int32_t)lround(row->value);
    switch (row->id) {
        case PANEL_PRINT_TURN:      n = snprintf(buffer, size, "%s", rubraview_print_turn_name((rubraview_print_turn_t)v)); break;
        case PANEL_PRINT_SIZE:      n = snprintf(buffer, size, "%s", rubraview_print_size_name((rubraview_print_size_t)v)); break;
        case PANEL_PRINT_PLACE:     n = snprintf(buffer, size, "%s", rubraview_print_place_name((rubraview_print_place_t)v)); break;
        case PANEL_PRINT_PER_SHEET: n = snprintf(buffer, size, "%d", (int)rubraview_print_per_sheet_of(v)); break;
        case PANEL_PRINT_PRINTER:   n = g_print_app && v >= 0 && (size_t)v < g_print_app->print_printer_count
                                          ? snprintf(buffer, size, "%s", g_print_app->print_printer_names[v]) : 0; break;
        case PANEL_PRINT_PAPER:
            if (g_print_app && v >= 0 && (size_t)v < g_print_app->print_paper_count) {
                const rubraview_printer_paper_t *p = &g_print_app->print_papers[v];
                n = snprintf(buffer, size, "%s (%d x %d mm)", p->name, (int)lround(p->w_tenth_mm / 10.0), (int)lround(p->h_tenth_mm / 10.0));
            }
            break;
        case PANEL_PRINT_ORIENT:    n = snprintf(buffer, size, "%s", v == 1 ? T("upright (portrait)") : v == 2 ? T("on its side (landscape)") : T("as the printer has it")); break;
        case PANEL_PRINT_COPIES:    n = snprintf(buffer, size, "%d", (int)v); break;
        case PANEL_PRINT_SHEET:     n = snprintf(buffer, size, T("sheet %d"), (int)v + 1); break;
        case PANEL_PRINT_SCALE:
        case PANEL_PRINT_MARGIN:
        case PANEL_CONV_LEVEL:      n = snprintf(buffer, size, "%d", (int)v); break;
        case PANEL_CONV_FORMAT:     n = snprintf(buffer, size, "%s", v == 1 ? T("7z (CB7)") : T("ZIP (CBZ)")); break;
        /* The words follow the format on the row above: each has one way of packing. */
        case PANEL_CONV_METHOD:     n = snprintf(buffer, size, "%s", v == 1 ? (g_conv_is_7z ? T("Copy (stored)") : T("Store"))
                                                                              : (g_conv_is_7z ? T("LZMA") : T("Deflate"))); break;
        default: break;
    }
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)buffer, .size = n > 0 ? (size_t)n : 0 };
}

static void print_now(app_state_t *app) {
    rubraview_print_options_t o = print_options(app);
    size_t count = app->print_list_count, printed = 0;
    if (count == 0) return;
    bool printed_ok = false;
    if (app->print_printer >= 0 && !app->print_with_dialog) {   /* 1.19.0: there, as chosen, no dialog */
        int32_t paper = app->print_paper >= 0 && (size_t)app->print_paper < app->print_paper_count ? app->print_papers[app->print_paper].id : 0;
        int32_t copies = (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("copies")));
        printed_ok = rubraview_pal_print_pictures_on(print_printer_name(app), paper, print_orientation(app), copies, app->print_list, count, &o, &printed);
    } else {
        printed_ok = rubraview_pal_print_pictures(rubraview_pal_window_native_handle(app->window), app->print_list, count, &o, &printed);
    }
    app->print_with_dialog = false;
    if (!printed_ok) {
        osd_say(app, TU8("not printed"));
        return;
    }
    char line[96];
    if (printed == count) snprintf(line, sizeof(line), T("%zu sent to the printer"), printed);
    else snprintf(line, sizeof(line), T("%zu sent to the printer, %zu could not be read"), printed, count - printed);
    osd_say(app, cstr(line));
}

static bool arc_temp_path(char *dst, size_t cap, const char *tag, proven_u8str_view_t leaf);

static void print_temp_drop(app_state_t *app) {
    if (!app->print_temp[0]) return;
    (void)rubraview_pal_fs_delete(cstr(app->print_temp));
    app->print_temp[0] = '\0';
}

/* D-89: a page of an archive has no file to hand the printer's reader, so
   it is laid in the temporary folder for as long as the print takes. */
static void print_archive_page(app_state_t *app) {
    int32_t page = current_page_index(app);
    proven_u8str_view_t shown = rubraview_path_basename(page_display_name(app, (size_t)page));
    proven_u8str_view_t stem = rubraview_path_stem(shown);
    proven_u8str_view_t ext = { .ptr = shown.ptr + stem.size, .size = shown.size - stem.size };
    print_temp_drop(app);
    bool ok = arc_temp_path(app->print_temp, sizeof(app->print_temp), "print", ext);
    if (ok) {
        page_job_stop(app);   /* the archive is the main thread's to read */
        size_t budget = rubraview_page_source_read_budget(&app->source, (size_t)page);
        void *mem = budget > 0 ? malloc(budget) : NULL;
        ok = false;
        if (mem) {
            proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)mem, .size = budget });
            rubraview_page_bytes_t bytes = rubraview_page_source_read(&arena, &app->source, (size_t)page, MAX_PAGE_BYTES);
            ok = bytes.ok && bytes.data.size > 0 && rubraview_pal_fs_write_file(cstr(app->print_temp), bytes.data);
            free(mem);
        }
    }
    if (!ok) {
        app->print_temp[0] = '\0';
        osd_say(app, TU8("not printed: the page could not be read"));
        return;
    }
    proven_u8str_view_t path = cstr(app->print_temp);
    char kept[sizeof(app->print_temp)];
    memcpy(kept, app->print_temp, sizeof(kept));
    print_paths(app, &path, 1);                       /* closes a panel that is open, which drops the file's name */
    memcpy(app->print_temp, kept, sizeof(kept));
}

/* ---- D-88 (R149): a page inside an archive deleted, renamed, saved ---- */

/* D-89: how what is packed again is packed — the settings the convert
   panel shows. 0 stores; 1..9 packs. */
static int arc_level(const app_state_t *app) {
    if (lround(rubraview_settings_get(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("convert_method"))) == 1) return 0;
    int level = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("convert_level")));
    return level < 1 ? 1 : level > 9 ? 9 : level;
}

static rubraview_arcedit_target_t arc_convert_target(const app_state_t *app) {
    return lround(rubraview_settings_get(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("convert_format"))) == 1
        ? RUBRAVIEW_ARCEDIT_7Z : RUBRAVIEW_ARCEDIT_ZIP;
}

/* D-89: the question of what the archive becomes, as a panel: the format,
   stored or packed, how hard. Its button writes; closing it leaves the book. */
static void panel_open_convert(app_state_t *app) {
    if (app->panel.open) panel_close(app);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    app->panel = rubraview_panel_create(TU8("Write the archive again"), dpi);
    app->panel_is_export = false;
    app->panel_is_batch = false;
    app->panel_is_print = false;
    app->panel_is_convert = true;
    rubraview_panel_add_choice(&app->panel, PANEL_CONV_FORMAT, TU8("Becomes"),
                               arc_convert_target(app) == RUBRAVIEW_ARCEDIT_7Z ? 1 : 0, 2);
    rubraview_panel_add_choice(&app->panel, PANEL_CONV_METHOD, TU8("Method"), arc_level(app) == 0 ? 1 : 0, 2);
    rubraview_panel_add_slider(&app->panel, PANEL_CONV_LEVEL, TU8("Level"),
                               rubraview_settings_get(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("convert_level")), 1.0, 9.0, 1.0);
    rubraview_panel_add_separator(&app->panel);
    rubraview_panel_add_button(&app->panel, PANEL_CONV_NOW, TU8("Write (Enter)"));
    app->panel.open = true;
    panel_relayout(app);
    app->arc_ask = ARC_ASK_FORMAT;
    osd_say(app, TU8("this archive cannot be changed as it is: it is written again, the old file goes to the Recycle Bin"));
}

/* D-89: the file an archive was before its last edit in place is kept
   beside it until the next edit or the end of the program. */
static void arc_undo_drop(app_state_t *app) {
    if (app->arc_undo_backup[0]) (void)rubraview_pal_fs_delete(cstr(app->arc_undo_backup));
    app->arc_undo_backup[0] = '\0';
    app->arc_undo_path[0] = '\0';
}

bool on_archive_page(const app_state_t *app) {
    return app->source.kind != RUBRAVIEW_PAGE_SOURCE_FOLDER && app->source.archive_path.size > 0 && !app->list_is_set &&
           app->source.arc != NULL && current_page_index(app) >= 0;
}

static void arc_ask_clear(app_state_t *app) {
    free(app->arc_ask_data);
    app->arc_ask_data = NULL;
    app->arc_ask_size = 0;
    app->arc_ask = ARC_ASK_NONE;
}

static DWORD WINAPI arc_job_run(LPVOID param) {
    arc_job_t *job = (arc_job_t*)param;
    job->result = (int)rubraview_arcedit_run(&job->job);
    InterlockedExchange(&job->done, 1);
    rubraview_pal_window_wake(job->wake);
    return 0;
}

static bool arc_file_exists(proven_u8str_view_t path) {
    uint8_t mem[4096];
    proven_arena_t probe = proven_arena_create((proven_mem_mut_t){ .ptr = mem, .size = sizeof(mem) });
    rubraview_fs_entry_t entry;
    return rubraview_pal_fs_stat(&probe, path, &entry);
}

/* The archive is written again, the question (if there was one) answered. */
static void arc_edit_start(app_state_t *app, rubraview_arcedit_target_t target) {
    rubraview_arcedit_op_t op = app->arc_ask_op;
    uint8_t *data = app->arc_ask_data;
    size_t size = app->arc_ask_size;
    app->arc_ask_data = NULL;          /* the job's from here */
    arc_ask_clear(app);

    arc_job_t *job = NULL;
    const char *why = T("not changed: could not start");
    if (!on_archive_page(app) || !rubraview_path_same(cstr(app->arc_ask_path), app->source.archive_path)) goto fail;
    rubraview_arcedit_route_t route = rubraview_arcedit_route(&app->source);
    if (route != RUBRAVIEW_ARCEDIT_IN_PLACE && route != RUBRAVIEW_ARCEDIT_CONVERT) goto fail;
    if (route == RUBRAVIEW_ARCEDIT_IN_PLACE) target = RUBRAVIEW_ARCEDIT_SAME;
    else if (target == RUBRAVIEW_ARCEDIT_SAME) goto fail;

    proven_u8str_view_t first = rubraview_page_source_first_volume(&app->source);
    proven_u8str_view_t opened = app->source.archive_path;
    if (first.size == 0) first = opened;
    job = (arc_job_t*)calloc(1, sizeof(*job));
    if (!job) goto fail;
    if (first.size >= sizeof(job->archive) || opened.size >= sizeof(job->opened) || first.size + 16 >= sizeof(job->out)) goto fail;
    memcpy(job->archive, first.ptr, first.size);
    memcpy(job->opened, opened.ptr, opened.size);
    memcpy(job->out, first.ptr, first.size);
    memcpy(job->out + first.size, ".rvnew", 7);
    memcpy(job->name, app->arc_ask_name, sizeof(job->name));

    /* A 7z is packed from a copy of every file laid beside it first. */
    if (target == RUBRAVIEW_ARCEDIT_7Z) {
        uint64_t unpacked = 0, free_bytes = 0;
        for (size_t i = 0; ; ++i) {
            const fulta_arc_entry_t *en = fulta_arc_entry(app->source.arc, i);
            if (!en) break;
            unpacked += en->size;
        }
        if (rubraview_pal_fs_free_bytes(rubraview_path_dirname(first), &free_bytes) && free_bytes < unpacked * 2u + (64u << 20)) {
            static char line[128];
            snprintf(line, sizeof(line), T("not changed: a 7z needs about %llu MB free beside the archive"),
                     (unsigned long long)((unpacked * 2u + (64u << 20)) >> 20));
            why = line;
            goto fail;
        }
    }

    int32_t page = current_page_index(app);
    job->page = (size_t)page;
    job->convert = route == RUBRAVIEW_ARCEDIT_CONVERT;
    job->data = data;
    data = NULL;
    job->wake = app->window;
    /* Where the reader is afterwards: on the page itself under its new
       name, or (a page deleted) on the one that takes its number. */
    proven_u8str_view_t shown = page_display_name(app, (size_t)page);
    if (op == RUBRAVIEW_ARCEDIT_REPLACE && job->name[0] == '\0') {
        if (shown.size < sizeof(job->land)) memcpy(job->land, shown.ptr, shown.size);
    } else if (op != RUBRAVIEW_ARCEDIT_DELETE) {
        rubraview_arcedit_sibling_name(shown, cstr(job->name), job->land, sizeof(job->land));
    }
    rubraview_codepage_t names = app->names_codepage >= 0 ? (rubraview_codepage_t)app->names_codepage : app->archive_codepage;
    job->job = (rubraview_arcedit_job_t){
        .archive_path = job->archive, .codepage = names, .op = op,
        .entry_index = app->arc_ask_entry, .name = job->name[0] ? job->name : NULL,
        .data = job->data, .size = size, .target = target, .out_path = job->out,
        .level = arc_level(app),
    };
    arc_undo_drop(app);   /* one step back, and it is this edit's from here */
    /* The choice holds for this book for the session. */
    if (job->convert && opened.size < sizeof(app->arc_choice_path)) {
        memcpy(app->arc_choice_path, opened.ptr, opened.size);
        app->arc_choice_path[opened.size] = '\0';
        app->arc_choice = target;
    }
    job->thread = CreateThread(NULL, 0, arc_job_run, job, 0, NULL);
    if (!job->thread) goto fail;
    app->arc_job = job;
    if (app->panel.open) panel_close(app);
    osd_say(app, TU8("writing the archive..."));
    return;

fail:
    if (job) free(job->data);
    free(job);
    free(data);
    osd_say(app, cstr(why));
}

/* An edit of entry `entry` is asked for. `data` (malloc) becomes this
   function's, whatever happens. */
static void arc_edit_request(app_state_t *app, rubraview_arcedit_op_t op, size_t entry, const char *name, uint8_t *data, size_t size) {
    const char *why = NULL;
    if (app->arc_job) why = T("the archive is still being written");
    else if (!on_archive_page(app)) why = T("not changed: the page is not in an archive");
    rubraview_arcedit_route_t route = why ? RUBRAVIEW_ARCEDIT_NOT_AN_ARCHIVE : rubraview_arcedit_route(&app->source);
    if (!why && route == RUBRAVIEW_ARCEDIT_ENCRYPTED) why = rubraview_arcedit_result_text(RUBRAVIEW_ARCEDIT_REFUSED_ENCRYPTED);
    if (!why && route == RUBRAVIEW_ARCEDIT_NOT_AN_ARCHIVE) why = T("not changed: the page is not in an archive");
    if (!why && op == RUBRAVIEW_ARCEDIT_DELETE && page_count(app) <= 1) why = T("the last page: delete the archive's file instead");
    if (!why && name && strlen(name) >= sizeof(app->arc_ask_name)) why = T("not changed: that name is too long");
    if (why) {
        free(data);
        osd_say(app, cstr(why));
        return;
    }
    arc_ask_clear(app);
    app->arc_ask_op = op;
    app->arc_ask_entry = entry;
    memset(app->arc_ask_name, 0, sizeof(app->arc_ask_name));
    if (name) memcpy(app->arc_ask_name, name, strlen(name));
    app->arc_ask_data = data;
    app->arc_ask_size = size;
    memset(app->arc_ask_path, 0, sizeof(app->arc_ask_path));
    if (app->source.archive_path.size < sizeof(app->arc_ask_path)) memcpy(app->arc_ask_path, app->source.archive_path.ptr, app->source.archive_path.size);

    bool chosen = app->arc_choice != RUBRAVIEW_ARCEDIT_SAME &&
                  rubraview_path_same(cstr(app->arc_choice_path), app->source.archive_path);
    if (op == RUBRAVIEW_ARCEDIT_DELETE) app->arc_ask = ARC_ASK_DELETE;
    else if (route == RUBRAVIEW_ARCEDIT_CONVERT && !chosen) panel_open_convert(app);
    else arc_edit_start(app, route == RUBRAVIEW_ARCEDIT_CONVERT ? app->arc_choice : RUBRAVIEW_ARCEDIT_SAME);
}

/* The same, of the page on screen. */
static void arc_edit_on_page(app_state_t *app, rubraview_arcedit_op_t op, const char *name) {
    if (!on_archive_page(app)) return;
    arc_edit_request(app, op, app->source.pages[current_page_index(app)].entry_index, name, NULL, 0);
}

static bool arc_ask_key(app_state_t *app, rubraview_key_combo_t combo) {
    if (app->arc_ask == ARC_ASK_NONE) return false;
    if (app->arc_ask == ARC_ASK_DELETE) {
        if (key_is(combo, "Y") || key_is(combo, "Enter")) {
            rubraview_arcedit_route_t route = on_archive_page(app) ? rubraview_arcedit_route(&app->source) : RUBRAVIEW_ARCEDIT_NOT_AN_ARCHIVE;
            bool chosen = app->arc_choice != RUBRAVIEW_ARCEDIT_SAME &&
                          rubraview_path_same(cstr(app->arc_choice_path), app->source.archive_path);
            if (route == RUBRAVIEW_ARCEDIT_CONVERT && !chosen) panel_open_convert(app);
            else arc_edit_start(app, route == RUBRAVIEW_ARCEDIT_CONVERT ? app->arc_choice : RUBRAVIEW_ARCEDIT_SAME);
        } else {
            arc_ask_clear(app);   /* anything else is "no" */
        }
        return true;
    }
    /* D-89: the convert panel is open. Enter is its button; Z and 7 pick the
       format; any other key goes its usual way, and what it does closes the panel. */
    if (combo.modifiers != RUBRAVIEW_MOD_NONE) return false;
    if (key_is(combo, "Enter")) {
        rubraview_arcedit_target_t target = arc_convert_target(app);
        app->panel_is_convert = false;
        panel_close(app);
        arc_edit_start(app, target);
        return true;
    }
    if (key_is(combo, "Z") || key_is(combo, "7")) {
        double v = key_is(combo, "7") ? 1.0 : 0.0;
        rubraview_settings_set(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("convert_format"), v);
        rubraview_panel_set_value(&app->panel, PANEL_CONV_FORMAT, v);
        return true;
    }
    return false;
}

/* A name beside `wanted` that no file has: `book-1.cbz`, `book-2.cbz`, ... */
static proven_u8str_view_t arc_free_name(app_state_t *app, proven_u8str_view_t wanted) {
    if (!arc_file_exists(wanted)) return wanted;
    proven_u8str_view_t dir = rubraview_path_dirname(wanted), base = rubraview_path_basename(wanted);
    proven_u8str_view_t stem = rubraview_path_stem(base);
    proven_u8str_view_t ext = { .ptr = base.ptr + stem.size, .size = base.size - stem.size };
    for (int n = 1; n < 1000; ++n) {
        char name[1024];
        int len = snprintf(name, sizeof(name), "%.*s-%d%.*s", (int)stem.size, stem.ptr, n, (int)ext.size, ext.ptr);
        if (len <= 0 || (size_t)len >= sizeof(name)) break;
        proven_u8str_view_t path = rubraview_path_join(app->arena, dir, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)name, .size = (size_t)len });
        if (path.size > 0 && !arc_file_exists(path)) return path;
    }
    return PROVEN_LIT("");
}

/* The book again, on the page named or numbered. */
static void arc_reopen(app_state_t *app, proven_u8str_view_t path, const char *land, size_t page) {
    if (!open_archive(app, path)) return;
    size_t start = page < page_count(app) ? page : page_count(app) - 1;
    if (land && land[0]) {
        for (size_t i = 0; i < page_count(app); ++i) {
            proven_u8str_view_t name = page_display_name(app, i);
            if (name.size == strlen(land) && memcmp(name.ptr, land, name.size) == 0) { start = i; break; }
        }
    }
    finish_open(app, start);
}

/* The new archive is written and checked: it takes the old one's place.
   The viewer lets go of the old file first — Windows replaces nothing
   that is mapped — and opens the book again afterwards. */
static void arc_job_finish(app_state_t *app, arc_job_t *job) {
    proven_u8str_view_t out = cstr(job->out), first = cstr(job->archive);
    proven_u8str_view_t opened = app_keep(app, cstr(job->opened));
    proven_u8str_view_t final = opened;
    const char *said = NULL;
    bool done = false;

    history_remember(app);
    unload_all_pages(app);
    source_close(app);

    bool can_undo = false;
    if (!job->convert) {
        /* D-89: the old file steps aside as `name.rvold` so that Ctrl+Z can bring it back. */
        char backup[sizeof(app->arc_undo_backup)];
        bool kept = first.size + 7 < sizeof(backup);
        if (kept) {
            memcpy(backup, first.ptr, first.size);
            memcpy(backup + first.size, ".rvold", 7);
            kept = rubraview_pal_fs_replace(first, cstr(backup));
        }
        done = rubraview_pal_fs_replace(out, first);
        if (!done && kept) (void)rubraview_pal_fs_replace(cstr(backup), first);   /* back where it was */
        if (done && kept) {
            memcpy(app->arc_undo_backup, backup, sizeof(backup));
            memset(app->arc_undo_path, 0, sizeof(app->arc_undo_path));
            memcpy(app->arc_undo_path, first.ptr, first.size);
            can_undo = true;
        }
        said = done ? NULL : T("not changed: the archive could not be replaced (is it open elsewhere?)");
    } else {
        char name[1024];
        proven_u8str_view_t base = rubraview_path_basename(first);
        size_t n = rubraview_arcedit_converted_name(base, job->job.target, name, sizeof(name));
        proven_u8str_view_t wanted = n > 0 ? rubraview_path_join(app->arena, rubraview_path_dirname(first), (proven_u8str_view_t){ .ptr = (const proven_byte_t *)name, .size = n })
                               : PROVEN_LIT("");
        bool takes_old_name = false;
        for (size_t i = 0; i < job->job.volume_count; ++i) {
            if (wanted.size > 0 && rubraview_path_same(wanted, cstr(job->job.volumes[i]))) takes_old_name = true;
        }
        if (wanted.size == 0) {
            said = T("not changed: the new archive's name is too long");
        } else if (takes_old_name) {
            /* A solid 7z becoming a 7z: the old one leaves for the bin, then the new one has its name. */
            bool binned = true;
            for (size_t i = 0; i < job->job.volume_count; ++i) {
                if (!rubraview_pal_fs_recycle(cstr(job->job.volumes[i]))) binned = false;
            }
            if (binned && rubraview_pal_fs_replace(out, wanted)) { done = true; final = wanted; }
            else {
                /* The old file would not go: the new one is kept beside it under a free name. */
                proven_u8str_view_t other = arc_free_name(app, wanted);
                if (other.size > 0 && rubraview_pal_fs_move(out, other)) {
                    done = true;
                    final = other;
                    said = T("the old archive could not be put in the Recycle Bin: the new one is beside it");
                } else {
                    said = T("not changed: the archive could not be replaced (is it open elsewhere?)");
                }
            }
        } else {
            /* Another name: never over a file that is already there. */
            proven_u8str_view_t target = arc_free_name(app, wanted);
            if (target.size > 0 && rubraview_pal_fs_move(out, target)) {
                done = true;
                final = target;
                for (size_t i = 0; i < job->job.volume_count; ++i) {
                    if (!rubraview_pal_fs_recycle(cstr(job->job.volumes[i]))) said = T("written; the old archive could not be put in the Recycle Bin");
                }
            } else {
                said = T("not changed: the new archive could not be given its name");
            }
        }
    }
    if (!done) (void)rubraview_pal_fs_delete(out);
    final = app_keep(app, final);
    /* The format chosen holds for the book under its new name too. */
    if (done && job->convert && final.size < sizeof(app->arc_choice_path)) {
        memcpy(app->arc_choice_path, final.ptr, final.size);
        app->arc_choice_path[final.size] = '\0';
    }

    rubraview_arcedit_op_t op = job->job.op;
    arc_reopen(app, done ? final : opened, done ? job->land : NULL, job->page);
    if (said) { osd_say(app, cstr(said)); return; }
    char line[256];
    proven_u8str_view_t base = rubraview_path_basename(final);
    const char *what = op == RUBRAVIEW_ARCEDIT_DELETE ? T("page deleted") : op == RUBRAVIEW_ARCEDIT_RENAME ? T("page renamed")
                     : op == RUBRAVIEW_ARCEDIT_REPLACE ? T("page saved over") : T("copy saved in the archive");
    if (job->convert) snprintf(line, sizeof(line), T("%s; the book is now %.*s"), what, (int)(base.size < 160 ? base.size : 160), base.ptr);
    else snprintf(line, sizeof(line), "%s%s", what, can_undo ? T("  (Ctrl+Z takes it back)") : "");
    osd_say(app, cstr(line));
}

/* D-89: the last edit in place taken back — the book as it was before it,
   while the reader is still in that book. */
static bool arc_undo(app_state_t *app) {
    if (!app->arc_undo_backup[0] || app->arc_job) return false;
    if (!on_archive_page(app)) return false;
    proven_u8str_view_t first = rubraview_page_source_first_volume(&app->source);
    if (!rubraview_path_same(first.size ? first : app->source.archive_path, cstr(app->arc_undo_path))) return false;
    proven_u8str_view_t opened = app_keep(app, app->source.archive_path);
    size_t page = (size_t)current_page_index(app);
    history_remember(app);
    unload_all_pages(app);
    source_close(app);
    bool ok = rubraview_pal_fs_replace(cstr(app->arc_undo_backup), cstr(app->arc_undo_path));
    if (ok) { app->arc_undo_backup[0] = '\0'; app->arc_undo_path[0] = '\0'; }
    arc_reopen(app, opened, NULL, page);
    osd_say(app, ok ? TU8("the archive is as it was before the last change") : TU8("could not take the change back"));
    return true;
}

/* Each pass: how far the writing has got; a finished one is taken up.
   A quit stops it and waits — the old archive stands. */
static void arc_job_poll(app_state_t *app, bool quitting) {
    arc_job_t *job = app->arc_job;
    if (!job) {
        if (quitting) arc_ask_clear(app);
        return;
    }
    if (quitting) atomic_store(&job->job.cancel, true);
    if (!quitting && !InterlockedCompareExchange(&job->done, 0, 0)) {
        uint64_t done = atomic_load(&job->job.done), total = atomic_load(&job->job.total);
        char line[96];
        if (atomic_load(&job->job.cancel)) snprintf(line, sizeof(line), T("stopping..."));
        else if (total > 0) snprintf(line, sizeof(line), T("writing the archive... %d %%  (Esc stops it)"), (int)(done * 100u / total));
        else snprintf(line, sizeof(line), T("writing the archive...  (Esc stops it)"));
        osd_say(app, cstr(line));
        return;
    }
    WaitForSingleObject(job->thread, INFINITE);
    CloseHandle(job->thread);
    app->arc_job = NULL;               /* opening is allowed again */
    if (!quitting) {
        if (job->result == RUBRAVIEW_ARCEDIT_OK) arc_job_finish(app, job);
        else osd_say(app, cstr(rubraview_arcedit_result_text((rubraview_arcedit_result_t)job->result)));
    } else if (job->result == RUBRAVIEW_ARCEDIT_OK) {
        (void)rubraview_pal_fs_delete(cstr(job->out));   /* written, but the reader left: the old one stands */
    }
    rubraview_arcedit_job_free(&job->job);
    free(job->data);
    free(job);
}

/* A file for a moment's use in the system's temporary folder. */
static bool arc_temp_path(char *dst, size_t cap, const char *tag, proven_u8str_view_t leaf) {
    WCHAR wide[MAX_PATH + 1];
    char dir[MAX_PATH * 3 + 1];
    DWORD n = GetTempPathW(MAX_PATH + 1, wide);
    if (n == 0 || n > MAX_PATH) return false;
    if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, dir, (int)sizeof(dir), NULL, NULL) <= 1) return false;
    int len = snprintf(dst, cap, "%srubraview-%s-%lu-%lu-%.*s", dir, tag, (unsigned long)GetCurrentProcessId(),
                       (unsigned long)GetTickCount(), (int)leaf.size, leaf.ptr);
    return len > 0 && (size_t)len < cap;
}

/* Save a copy's work, off the main thread: everything it needs copied in,
   its own memory, its own WIC factory. `result` indexes save_job_poll's lines. */
enum { SAVE_OK = 0, SAVE_UNREADABLE, SAVE_EDIT_FAILED, SAVE_UNWRITABLE, SAVE_NO_MEMORY };

static proven_u8str_view_t save_read_whole(proven_u8str_view_t path, void **out_mem) {
    *out_mem = NULL;
    rubraview_fs_entry_t entry;
    uint8_t probe_mem[4096];
    proven_arena_t probe = proven_arena_create((proven_mem_mut_t){ .ptr = probe_mem, .size = sizeof(probe_mem) });
    if (!rubraview_pal_fs_stat(&probe, path, &entry) || entry.size_bytes == 0 || entry.size_bytes > MAX_ARCHIVE_BYTES) {
        return PROVEN_LIT("");
    }
    size_t size = (size_t)entry.size_bytes + 4096;
    *out_mem = malloc(size);
    if (!*out_mem) return PROVEN_LIT("");
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)*out_mem, .size = size });
    return rubraview_pal_fs_read_file(&arena, path, MAX_ARCHIVE_BYTES);
}

static bool details_path_taken(proven_u8str_view_t path, void *ctx) { (void)ctx; return rubraview_pal_fs_exists(path); }

/* 1.18.0 (owner, 2026-10-18: "그림파일 읽을 때, 옆에 동일한 이름이 있는 텍스트 파일이 있으면 읽어보고
   루브라뷰가 만든 파일이면 ... 파일 내부 정보인 것처럼"): the details a Rubraview .txt beside `picture`
   holds for it — photo.txt, photo-1.txt ... photo-20.txt, the first that names this picture. Any
   thread; `arena` keeps what is read. `out_path`, when given, is the .txt's path. */
#define DETAILS_TXT_MAX_BYTES (1u << 20)
bool details_beside(proven_arena_t *arena, proven_u8str_view_t picture, rubraview_details_src_t *out, proven_u8str_view_t *out_path) {
    *out = (rubraview_details_src_t){0};
    for (unsigned k = 0; k <= 20; ++k) {
        proven_u8str_view_t path = rubraview_details_txt_candidate(arena, picture, k);
        if (path.size == 0) return false;
        if (!rubraview_pal_fs_exists(path)) continue;
        proven_u8str_view_t text = rubraview_pal_fs_read_file(arena, path, DETAILS_TXT_MAX_BYTES);
        if (text.size > 0 && rubraview_details_from_txt(arena, text, picture, out)) {
            if (out_path) *out_path = path;
            return true;
        }
    }
    return false;
}

/* 1.17.0 (owner, 2026-10-18): what `source` said (EXIF, XMP, IPTC, comments) put into `written`, a
   picture just written as `format` with its pixels only, where that format has a place; and, asked
   for, what it cannot hold in a .txt beside `named` (the name the file will have: an original
   written over is written beside it first). Any thread: its own memory. `named` empty: no .txt
   (a page going into an archive). Returns DETAILS_* for the caller to say. */

int details_keep(proven_u8str_view_t source, proven_u8str_view_t written, proven_u8str_view_t named, rubraview_export_format_t format,
                        const rubraview_export_options_t *options) {
    if (!options || options->privacy_clean || options->keep_details <= RUBRAVIEW_KEEP_DETAILS_DROP) return DETAILS_NONE;
    void *src_mem = NULL, *out_mem = NULL;
    proven_u8str_view_t src = save_read_whole(source, &src_mem);
    int said = DETAILS_NONE;
    if (src.size > 0) {
        proven_u8str_view_t out = save_read_whole(written, &out_mem);
        /* what is read, the file made again with the blocks, the words and the .txt */
        size_t room = 0;
        if (out.size > 0 && !PROVEN_CKD_ADD(&room, out.size, src.size) && !PROVEN_CKD_MUL(&room, room, (size_t)3) &&
            !PROVEN_CKD_ADD(&room, room, (size_t)(8u << 20))) {
            void *work_mem = malloc(room);
            if (work_mem) {
                proven_arena_t work = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)work_mem, .size = room });
                rubraview_details_src_t details, beside;
                bool own = rubraview_details_read(&work, (const uint8_t*)src.ptr, src.size, &details);
                /* 1.18.0: what a .txt beside the source kept for it goes on with it */
                if (details_beside(&work, source, &beside, NULL)) { rubraview_details_merge(&work, &details, &beside); own = true; }
                if (own) {
                    rubraview_details_put_t put = rubraview_details_put(&work, format, out, &details);
                    if (put.file.ptr != out.ptr) said = rubraview_pal_fs_write_file(written, put.file) ? DETAILS_IN_FILE : DETAILS_FAILED;
                    if (!put.all_kept && said != DETAILS_FAILED && options->keep_details >= RUBRAVIEW_KEEP_DETAILS_OR_TXT && named.size > 0) {
                        /* the one this picture has already, written again; else a free name */
                        rubraview_details_src_t had;
                        proven_u8str_view_t txt_path = PROVEN_LIT("");
                        if (!details_beside(&work, named, &had, &txt_path)) txt_path = rubraview_details_txt_path(&work, named, details_path_taken, NULL);
                        proven_u8str_view_t txt = rubraview_details_txt(&work, &details, rubraview_path_basename(source), rubraview_path_basename(named));
                        if (txt_path.size > 0 && txt.size > 0) {
                            /* a mark that says UTF-8, for Notepad and the like */
                            proven_result_mem_mut_t res = proven_arena_alloc(&work, txt.size + 3);
                            if (proven_is_ok(res.err)) {
                                memcpy(res.value.ptr, "\xEF\xBB\xBF", 3);
                                memcpy(res.value.ptr + 3, txt.ptr, txt.size);
                                said = rubraview_pal_fs_write_file(txt_path, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)res.value.ptr, .size = txt.size + 3 })
                                           ? DETAILS_TXT : DETAILS_FAILED;
                            }
                        }
                    }
                }
                free(work_mem);
            }
        }
    }
    free(out_mem);
    free(src_mem);
    return said;
}

/* The name the written file ends with: the original's, when it is written over; none for an archive's page. */
static proven_u8str_view_t save_job_named(const save_job_t *job) {
    if (job->into_archive) return PROVEN_LIT("");
    return cstr(job->over_original ? job->source : job->out);
}

static int save_job_work(save_job_t *job) {
    proven_u8str_view_t source = { .ptr = (const proven_byte_t *)job->source, .size = strlen(job->source) };
    proven_u8str_view_t out_path = { .ptr = (const proven_byte_t *)job->out, .size = strlen(job->out) };

    if (job->route == RUBRAVIEW_EXPORT_STRIP_ONLY || job->route == RUBRAVIEW_EXPORT_COPY || job->route == RUBRAVIEW_EXPORT_JPEG_LOSSLESS) {
        void *mem = NULL;
        proven_u8str_view_t bytes = save_read_whole(source, &mem);
        int result = SAVE_UNREADABLE;
        if (bytes.size > 0 && job->route == RUBRAVIEW_EXPORT_COPY) {
            result = rubraview_pal_fs_write_file(out_path, bytes) ? SAVE_OK : SAVE_UNWRITABLE;
        } else if (bytes.size > 0) {
            /* the strip's output is at most the input: room for it beside */
            size_t room = bytes.size * 2u + (1u << 20);
            void *work_mem = malloc(room);
            result = SAVE_NO_MEMORY;
            if (work_mem) {
                proven_arena_t work = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)work_mem, .size = room });
                /* 1.17.0: or its scans, tables and restarts written again, its markers kept unless privacy clean */
                rubraview_jpegtran_result_t stripped = job->route == RUBRAVIEW_EXPORT_JPEG_LOSSLESS
                    ? rubraview_jpegtran_rewrite(&work, (const uint8_t*)bytes.ptr, bytes.size, job->options.jpeg_progressive,
                                                 job->options.jpeg_optimise, job->options.jpeg_restart_rows, !job->options.privacy_clean)
                    : rubraview_jpegtran_strip_metadata(&work, (const uint8_t*)bytes.ptr, bytes.size);
                result = stripped.err != RUBRAVIEW_JPEGTRAN_OK ? SAVE_UNREADABLE
                       : rubraview_pal_fs_write_file(out_path, stripped.data) ? SAVE_OK : SAVE_UNWRITABLE;
                free(work_mem);
            }
        }
        free(mem);
        return result;
    }

    /* RV-109: an animation written as WebP keeps moving — every frame at
       its time. Not when it was edited: an edit is made on one picture. */
    {
        rubraview_export_format_t to = job->options.format == RUBRAVIEW_EXPORT_SAME_AS_SOURCE ? job->source_format
                                                                                              : job->options.format;
        if ((to == RUBRAVIEW_EXPORT_WEBP || to == RUBRAVIEW_EXPORT_AVIF || to == RUBRAVIEW_EXPORT_JXL) && !job->apply_edit) {
            int moved = to == RUBRAVIEW_EXPORT_WEBP ? rubraview_pal_image_save_animation_webp(out_path, source, NULL, 0, &job->options)
                      : rubraview_pal_image_save_animation_ffmpeg(out_path, source, NULL, 0, &job->options, to == RUBRAVIEW_EXPORT_AVIF);   /* 1.31.0 */
            if (moved > 0) (void)details_keep(source, out_path, save_job_named(job), to, &job->options);
            if (moved < 0) job->why = rubraview_pal_image_save_why();
            if (moved != 0) return moved > 0 ? SAVE_OK : SAVE_UNWRITABLE;
        }
    }

    /* The pixels get memory of their own, sized to the picture: the app
       arena is a fixed 64 MB, a 3840x2400 page is 37 MB decoded, and the
       commit's first copy used to fail there — which wrote the page
       unedited, as if the edit had been saved (T093). */
    size_t src_bytes = picture_bytes((size_t)job->width, (size_t)job->height, 4u, 0);
    size_t out_bytes = src_bytes;
    if (job->apply_edit && job->edit.resize_width > 0 && job->edit.resize_height > 0) {
        size_t r = picture_bytes((size_t)job->edit.resize_width, (size_t)job->edit.resize_height, 4u, 0);
        if (r > out_bytes) out_bytes = r;
    }
    /* decode and its conversion, then the commit's clone, blur and sharpen
       (each a copy plus a row buffer's worth), then the resize */
    size_t need = bytes_scaled(src_bytes, 8u, 0), twice_out = bytes_scaled(out_bytes, 2u, 16u << 20);
    if (need == SIZE_MAX || twice_out == SIZE_MAX || PROVEN_CKD_ADD(&need, need, twice_out)) need = SIZE_MAX;   /* malloc refuses it */
    void *work_mem = malloc(need);
    if (!work_mem) return SAVE_NO_MEMORY;
    proven_arena_t work = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)work_mem, .size = need });

    int result = SAVE_OK;
    rubraview_pixbuf_t pixels = rubraview_pal_image_read_pixels(&work, source, NULL, 0, true);
    if (!rubraview_pixbuf_is_valid(&pixels)) result = SAVE_UNREADABLE;
    void *steps_mem = NULL;
    if (result == SAVE_OK && job->apply_edit && job->steps.cursor > 0) {
        /* The owner's edit: each step from one buffer into the other, the
           one before it given back as the next is made. */
        size_t biggest = src_bytes;
        for (size_t i = 1; i <= job->steps.cursor; ++i) {
            int32_t sw = 0, sh = 0;
            rubraview_edit_steps_size(&job->steps, i, &sw, &sh);
            size_t b = picture_bytes((size_t)sw, (size_t)sh, 4u, 0);
            if (b > biggest) biggest = b;
        }
        size_t half = bytes_scaled(biggest, 6u, 16u << 20), both = bytes_scaled(half, 2u, 0);
        steps_mem = both == SIZE_MAX ? NULL : malloc(both);
        if (!steps_mem) {
            result = SAVE_NO_MEMORY;
        } else {
            proven_arena_t lanes[2] = {
                proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)steps_mem, .size = half }),
                proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)steps_mem + half, .size = half }),
            };
            for (size_t i = 0; i < job->steps.cursor && result == SAVE_OK; ++i) {
                proven_arena_t *lane = &lanes[i % 2u];
                proven_arena_reset(lane);
                g_plugin_why = NULL;
                rubraview_pixbuf_t next = rubraview_edit_step_apply(lane, &job->steps.steps[i], &pixels, 1.0);
                if (rubraview_pixbuf_is_valid(&next)) pixels = next; else { result = SAVE_EDIT_FAILED; job->why = g_plugin_why; }
            }
        }
    }
    if (result == SAVE_OK && job->apply_edit && !rubraview_edit_is_neutral(&job->edit)) {
        rubraview_pixbuf_t edited = rubraview_edit_commit(&work, &job->edit, &pixels);
        if (rubraview_pixbuf_is_valid(&edited)) pixels = edited; else result = SAVE_EDIT_FAILED;
    }
    if (result == SAVE_OK) {
        rubraview_export_options_t options = job->options;
        if (options.format == RUBRAVIEW_EXPORT_SAME_AS_SOURCE) options.format = job->source_format;
        if (!rubraview_pal_image_save(out_path, &pixels, &options)) { result = SAVE_UNWRITABLE; job->why = rubraview_pal_image_save_why(); }
        else (void)details_keep(source, out_path, save_job_named(job), options.format, &options);
    }
    free(steps_mem);
    free(work_mem);
    return result;
}

static DWORD WINAPI save_job_run(LPVOID param) {
    save_job_t *job = (save_job_t*)param;
    bool com = SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
    job->result = com ? save_job_work(job) : SAVE_UNREADABLE;
    rubraview_pal_image_thread_end();
    if (com) CoUninitialize();
    InterlockedExchange(&job->done, 1);
    rubraview_pal_window_wake(job->wake);   /* the main loop hears it at once */
    return 0;
}

/* The workbench's Save a copy, and the export dialog's Export, are the
   same act: run the session, then write. §3.10 decides whether the write
   even needs an encoder. */
/* D-88: the page of an archive with the workbench's edit, written into the
   archive — over the page, or beside it as `name_edit.ext`. The page is
   laid in a temporary file, the copy made from it into another exactly as
   a loose file's is, and that file's bytes then go into the archive. */
static void panel_write_archive_page(app_state_t *app, bool over) {
    if (!on_archive_page(app)) return;
    int32_t page = current_page_index(app);
    if (app->arc_job) { osd_say(app, TU8("the archive is still being written")); return; }
    if (app->save_job) { osd_say(app, TU8("still saving the last copy")); return; }
    if (rubraview_arcedit_route(&app->source) == RUBRAVIEW_ARCEDIT_ENCRYPTED) {
        osd_say(app, cstr(rubraview_arcedit_result_text(RUBRAVIEW_ARCEDIT_REFUSED_ENCRYPTED)));
        return;
    }
    int32_t w = real_w(&app->pages[page]), h = real_h(&app->pages[page]);
    if (w <= 0 || h <= 0) { w = app->edit.image_width; h = app->edit.image_height; }
    if (w <= 0 || h <= 0) { osd_say(app, TU8("not saved: the page is not read yet")); return; }

    proven_u8str_view_t shown = rubraview_path_basename(page_display_name(app, (size_t)page));
    rubraview_export_options_t options = app->export_options;
    if (edit_panel_open(app) && app->edit.resize_dpi > 0.0) options.dpi = app->edit.resize_dpi;   /* RV-134: the DPI chosen in Edit */
    rubraview_export_format_t source_format = rubraview_export_format_for_name(shown);
    rubraview_export_format_t target = options.format == RUBRAVIEW_EXPORT_SAME_AS_SOURCE ? source_format : options.format;
    proven_u8str_view_t stem = rubraview_path_stem(shown), ext = rubraview_export_extension(target);
    proven_u8str_view_t source_ext = { .ptr = shown.ptr + stem.size, .size = shown.size - stem.size };   /* with its dot */
    /* Saved over in its own format the page keeps its name; in another, it takes that one's extension. */
    proven_u8str_view_t name = over ? (target == source_format ? PROVEN_LIT("")
                                                   : rubraview_batch_format_name(app->arena, PROVEN_LIT("{name}.{ext}"), stem, ext, 0, 0, PROVEN_LIT("")))
                        : rubraview_batch_format_name(app->arena, PROVEN_LIT("{name}_edit.{ext}"), stem, ext, 0, 0, PROVEN_LIT(""));

    save_job_t *job = (save_job_t*)calloc(1, sizeof(*job));
    if (!job) { osd_say(app, TU8("not saved: not enough memory")); return; }
    proven_u8str_view_t out_leaf = rubraview_batch_format_name(app->arena, PROVEN_LIT("out.{ext}"), stem, ext, 0, 0, PROVEN_LIT(""));
    bool ok = name.size < sizeof(job->arc_name) && app->source.archive_path.size < sizeof(job->arc_path) &&
              arc_temp_path(job->source, sizeof(job->source), "page", source_ext) &&
              arc_temp_path(job->out, sizeof(job->out), "edit", out_leaf);
    /* The page's bytes, read here: the archive is the main thread's to read. */
    if (ok) {
        page_job_stop(app);
        size_t budget = rubraview_page_source_read_budget(&app->source, (size_t)page);
        void *mem = budget > 0 ? malloc(budget) : NULL;
        ok = false;
        if (mem) {
            proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)mem, .size = budget });
            rubraview_page_bytes_t bytes = rubraview_page_source_read(&arena, &app->source, (size_t)page, MAX_PAGE_BYTES);
            ok = bytes.ok && bytes.data.size > 0 && rubraview_pal_fs_write_file(cstr(job->source), bytes.data);
            free(mem);
        }
    }
    if (!ok) { free(job); osd_say(app, TU8("not saved: the page could not be read")); return; }

    job->into_archive = true;
    job->arc_op = over ? RUBRAVIEW_ARCEDIT_REPLACE : RUBRAVIEW_ARCEDIT_ADD;
    job->arc_entry = app->source.pages[page].entry_index;
    memcpy(job->arc_path, app->source.archive_path.ptr, app->source.archive_path.size);
    if (name.size) memcpy(job->arc_name, name.ptr, name.size);
    job->edit = app->edit;
    job->edit.crop_active = false;   /* the selection is not a cut: the steps say what is cut (the owner's edit) */
    job->edit.resize_width = job->edit.resize_height = 0;
    job->steps = app->edit_steps;
    job->apply_edit = true;
    job->options = options;
    job->source_format = source_format;
    job->route = rubraview_export_plan(&options, source_format, true);
    job->width = w;
    job->height = h;
    job->wake = app->window;
    job->thread = CreateThread(NULL, 0, save_job_run, job, 0, NULL);
    if (!job->thread) {
        (void)rubraview_pal_fs_delete(cstr(job->source));
        free(job);
        osd_say(app, TU8("not saved: could not start"));
        return;
    }
    app->save_job = job;
    osd_say(app, TU8("making the edited page..."));
}

static void panel_write_current(app_state_t *app, bool apply_edit) {
    int32_t page = current_page_index(app);
    if (page < 0) return;

    proven_u8str_view_t source = app->source.pages[page].path;
    if (source.size == 0) {
        /* D-88: an archive page's copy goes into the archive, beside the page. */
        if (apply_edit && on_archive_page(app)) panel_write_archive_page(app, false);
        return;
    }

    rubraview_export_options_t options = app->export_options;
    if (apply_edit && app->edit.resize_dpi > 0.0) options.dpi = app->edit.resize_dpi;   /* RV-134: the DPI chosen in Edit */
    rubraview_export_format_t source_format = rubraview_export_format_for_name(source);
    rubraview_edit_step_t pending_now = edit_pending(app);
    bool pixels_change = apply_edit && (rubraview_edit_step_does_something(&pending_now) || app->edit_steps.cursor > 0);

    proven_u8str_view_t stem = rubraview_path_stem(rubraview_path_basename(source));
    rubraview_export_format_t target = options.format == RUBRAVIEW_EXPORT_SAME_AS_SOURCE
                                         ? source_format : options.format;
    proven_u8str_view_t ext = rubraview_export_extension(target);
    proven_u8str_view_t name = rubraview_batch_format_name(app->arena, PROVEN_LIT("{name}_edit.{ext}"), stem, ext, 0, 0, PROVEN_LIT(""));
    proven_u8str_view_t out_path = rubraview_path_join(app->arena, rubraview_path_dirname(source), name);
    if (out_path.size == 0) return;

    rubraview_export_route_t route = rubraview_export_plan(&options, source_format, pixels_change);

    int32_t w = real_w(&app->pages[page]), h = real_h(&app->pages[page]);
    if (w <= 0 || h <= 0) { w = app->edit.image_width; h = app->edit.image_height; }
    if (route != RUBRAVIEW_EXPORT_STRIP_ONLY && route != RUBRAVIEW_EXPORT_COPY && route != RUBRAVIEW_EXPORT_JPEG_LOSSLESS && (w <= 0 || h <= 0)) {
        osd_say(app, TU8("not saved: the page is not read yet"));
        return;
    }
    if (app->save_job) { osd_say(app, TU8("still saving the last copy")); return; }
    if (source.size >= sizeof(app->save_job->source) || out_path.size >= sizeof(app->save_job->out)) return;

    /* The copy is made on a thread of its own (owner, 2026-09-28): a
       large picture took the window with it for as long as it took. */
    save_job_t *job = (save_job_t*)calloc(1, sizeof(*job));
    if (!job) { osd_say(app, TU8("not saved: not enough memory")); return; }
    memcpy(job->source, source.ptr, source.size);
    memcpy(job->out, out_path.ptr, out_path.size);
    job->edit = app->edit;
    job->edit.crop_active = false;   /* the selection is not a cut: the steps say what is cut (the owner's edit) */
    job->edit.resize_width = job->edit.resize_height = 0;
    if (apply_edit) job->steps = app->edit_steps;
    job->apply_edit = apply_edit;
    job->options = options;
    job->source_format = source_format;
    job->route = route;
    job->width = w;
    job->height = h;
    job->wake = app->window;
    job->thread = CreateThread(NULL, 0, save_job_run, job, 0, NULL);
    if (!job->thread) { free(job); osd_say(app, TU8("not saved: could not start")); return; }
    app->save_job = job;
    osd_say(app, TU8("saving a copy..."));
}

/* Each pass: a finished copy says how it went and lets go of its thread. */
static void save_job_poll(app_state_t *app, bool wait) {
    save_job_t *job = app->save_job;
    if (!job) return;
    if (!wait && !InterlockedCompareExchange(&job->done, 0, 0)) return;
    WaitForSingleObject(job->thread, INFINITE);   /* a quit waits: a copy is never left half written */
    CloseHandle(job->thread);
    const char *const SAID[] = {
        T("saved a copy beside the page"), T("not saved: the picture could not be read"),
        T("not saved: the edit could not be applied"), T("not saved: the file could not be written"),
        T("not saved: not enough memory for this picture"),
    };
    if (job->into_archive) {
        /* D-88: the copy was made between two temporary files; its bytes go into the archive now. */
        void *mem = NULL;
        proven_u8str_view_t bytes = job->result == SAVE_OK ? save_read_whole(cstr(job->out), &mem) : PROVEN_LIT("");
        (void)rubraview_pal_fs_delete(cstr(job->source));
        (void)rubraview_pal_fs_delete(cstr(job->out));
        app->save_job = NULL;
        if (wait) {
            /* the reader is leaving: nothing is changed */
        } else if (job->result != SAVE_OK || bytes.size == 0) {
            osd_say(app, cstr(SAID[job->result != SAVE_OK ? job->result : SAVE_UNWRITABLE]));
        } else if (!on_archive_page(app) || !rubraview_path_same(cstr(job->arc_path), app->source.archive_path)) {
            osd_say(app, TU8("not saved: another book is open now"));
        } else {
            uint8_t *data = (uint8_t*)malloc(bytes.size);
            if (data) {
                memcpy(data, bytes.ptr, bytes.size);
                arc_edit_request(app, job->arc_op, job->arc_entry, job->arc_name[0] ? job->arc_name : NULL, data, bytes.size);
            } else {
                osd_say(app, cstr(SAID[SAVE_NO_MEMORY]));
            }
        }
        free(mem);
        free(job);
        return;
    }
    if (job->over_original && job->result == SAVE_OK) {
        /* The owner's edit: the written file takes the original's place; the original goes to the Recycle Bin. */
        bool moved = rubraview_pal_fs_recycle(cstr(job->source)) && rubraview_pal_fs_move(cstr(job->out), cstr(job->source));
        if (!moved) (void)rubraview_pal_fs_delete(cstr(job->out));
        if (!wait) {
            osd_say(app, moved ? TU8("saved over the original (the old file is in the Recycle Bin)")
                               : TU8("not saved: the original could not be replaced"));
            if (moved) {
                /* The page is read again as it is now, and the edit starts over on it. */
                int32_t page = current_page_index(app);
                if (page >= 0 && (size_t)page < page_count(app) && rubraview_path_same(app->source.pages[page].path, cstr(job->source))) {
                    if (edit_panel_open(app)) panel_close(app);
                    unload_all_pages(app);   /* read again from the file as it is now */
                    app->needs_relayout = true;
                }
            }
        }
        free(job);
        app->save_job = NULL;
        return;
    }
    if (job->over_original) (void)rubraview_pal_fs_delete(cstr(job->out));
    if (!wait && (job->result == SAVE_UNWRITABLE || job->result == SAVE_EDIT_FAILED) && job->why) {   /* RV-139; RFC-0007 */
        char line[256];
        snprintf(line, sizeof(line), T("not saved: %s"), rubraview_tr(job->why));
        osd_say(app, cstr(line));
    } else if (!wait && job->result >= 0 && job->result < (int)(sizeof(SAID) / sizeof(SAID[0]))) osd_say(app, cstr(SAID[job->result]));
    free(job);
    app->save_job = NULL;
}

/* The owner's edit: Save writes over the original, asked first by a second
   press within a few seconds; the copy is made beside it under another name
   in the original's own format, then takes its place. */
static void edit_save_over(app_state_t *app) {
    static double asked_at = -100.0;
    double now = rubraview_pal_time_now_seconds();
    int32_t page = current_page_index(app);
    if (page < 0) return;
    proven_u8str_view_t source = app->source.pages[page].path;
    if (source.size == 0) return;
    rubraview_edit_step_t pending = edit_pending(app);
    if (app->edit_steps.cursor == 0 && !rubraview_edit_step_does_something(&pending)) { osd_say(app, TU8("nothing to save: no step was made")); return; }
    if (now - asked_at > 4.0) {
        asked_at = now;
        osd_say(app, TU8("press Save again to write over the original (the old file goes to the Recycle Bin)"));
        return;
    }
    asked_at = -100.0;
    if (app->save_job) { osd_say(app, TU8("still saving the last copy")); return; }
    rubraview_export_options_t options = app->export_options;
    options.format = RUBRAVIEW_EXPORT_SAME_AS_SOURCE;
    rubraview_export_format_t source_format = rubraview_export_format_for_name(source);
    proven_u8str_view_t stem = rubraview_path_stem(rubraview_path_basename(source));
    proven_u8str_view_t ext = rubraview_export_extension(source_format);
    proven_u8str_view_t name = rubraview_batch_format_name(app->arena, PROVEN_LIT("{name}.rvsaving.{ext}"), stem, ext, 0, 0, PROVEN_LIT(""));
    proven_u8str_view_t out_path = rubraview_path_join(app->arena, rubraview_path_dirname(source), name);
    int32_t w = real_w(&app->pages[page]), h = real_h(&app->pages[page]);
    if (out_path.size == 0 || w <= 0 || h <= 0) { osd_say(app, TU8("not saved: the page is not read yet")); return; }
    save_job_t *job = (save_job_t*)calloc(1, sizeof(*job));
    if (!job || source.size >= sizeof(job->source) || out_path.size >= sizeof(job->out)) { free(job); osd_say(app, TU8("not saved: not enough memory")); return; }
    memcpy(job->source, source.ptr, source.size);
    memcpy(job->out, out_path.ptr, out_path.size);
    job->edit = app->edit;
    job->edit.crop_active = false;   /* the selection is not a cut: the steps say what is cut (the owner's edit) */
    job->edit.resize_width = job->edit.resize_height = 0;
    job->steps = app->edit_steps;
    job->apply_edit = true;
    job->over_original = true;
    job->options = options;
    job->source_format = source_format;
    job->route = rubraview_export_plan(&options, source_format, true);
    job->width = w;
    job->height = h;
    job->wake = app->window;
    job->thread = CreateThread(NULL, 0, save_job_run, job, 0, NULL);
    if (!job->thread) { free(job); osd_say(app, TU8("not saved: could not start")); return; }
    app->save_job = job;
    osd_say(app, TU8("saving over the original..."));
}

/* Turns a panel row back into the thing it stands for. Keeping this in
   one place is what lets the panel model stay ignorant of editing. */
void panel_apply_row(app_state_t *app, int32_t index) {
    if (index < 0 || (size_t)index >= app->panel.row_count) return;
    const rubraview_panel_row_t *row = &app->panel.rows[index];
    double v = row->value;

    if (app->panel_is_export) {
        switch (row->id) {
            case PANEL_FORMAT:    app->export_options.format = (rubraview_export_format_t)(int32_t)v; break;
            case PANEL_QUALITY:   app->export_options.jpeg_quality = (int32_t)v;
                                  app->export_options.webp_quality = (int32_t)v; break;
            case PANEL_PNG_LEVEL: app->export_options.png_compression = (int32_t)v; break;
            case PANEL_PRIVACY:   app->export_options.privacy_clean = v > 0.5; break;
            default: break;
        }
        bool rows_change = export_more_apply(&app->export_options, row->id, v);
        rubraview_export_clamp(&app->export_options);
        /* Another format, WebP's lossless, a palette or not: the rows that have use change with it. */
        if (rows_change || row->id == PANEL_FORMAT || row->id == PANEL_PRIVACY) {
            panel_open_export(app);
        }
        return;
    }

    if (app->panel_is_convert) {
        switch (row->id) {
            case PANEL_CONV_FORMAT: rubraview_settings_set(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("convert_format"), v); break;
            case PANEL_CONV_METHOD: rubraview_settings_set(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("convert_method"), v); break;
            case PANEL_CONV_LEVEL:  rubraview_settings_set(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("convert_level"), v); break;
            default: break;
        }
        return;
    }

    if (app->panel_is_print) {
        switch (row->id) {
            case PANEL_PRINT_TURN:      rubraview_settings_set(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("turn"), v); break;
            case PANEL_PRINT_SIZE:      rubraview_settings_set(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("size"), v); break;
            case PANEL_PRINT_SCALE:     rubraview_settings_set(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("scale"), v); break;
            case PANEL_PRINT_PLACE:     rubraview_settings_set(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("place"), v); break;
            case PANEL_PRINT_MARGIN:    rubraview_settings_set(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("margin_mm"), v); break;
            case PANEL_PRINT_PER_SHEET: rubraview_settings_set(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("per_sheet"), v);
                                        app->print_preview_sheet = 0; print_thumbs_load(app); panel_open_print(app); break;
            /* 1.19.0 */
            case PANEL_PRINT_PRINTER:
                app->print_printer = (int32_t)v;
                rubraview_settings_set_text(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("printer"), app_keep(app, print_printer_name(app)));
                print_papers_load(app);
                print_sheet_load(app);
                panel_open_print(app);
                break;
            case PANEL_PRINT_PAPER:
                app->print_paper = (int32_t)v;
                if (app->print_paper >= 0 && (size_t)app->print_paper < app->print_paper_count)
                    rubraview_settings_set(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("paper"), (double)app->print_papers[app->print_paper].id);
                print_sheet_load(app);
                break;
            case PANEL_PRINT_ORIENT: rubraview_settings_set(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("orientation"), v); print_sheet_load(app); break;
            case PANEL_PRINT_COPIES: rubraview_settings_set(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("copies"), v); break;
            case PANEL_PRINT_SHEET:  app->print_preview_sheet = (int32_t)v; print_thumbs_load(app); break;
            default: break;
        }
        return;
    }

    if (app->panel_is_batch) {   /* RV-129 */
        struct batch_ui *b = &app->batch;
        bool again = false;
        switch (row->id) {
            case PANEL_BATCH_SIZE_BY: {
                b->size_by = (int32_t)v; again = true;
                static const double START[8] = { 100, 50, 1920, 1080, 1920, 1080, 1920, 2 };
                b->size_value = START[b->size_by & 7];
                break;
            }
            case PANEL_BATCH_RESIZE:        b->size_value = v; break;
            case PANEL_BATCH_BOX_H:         b->box_h = v; break;
            case PANEL_BATCH_FILTER:        b->filter = (int32_t)v; break;
            case PANEL_BATCH_LINEAR:        b->linear = v > 0.5; break;
            case PANEL_BATCH_NO_ENLARGE:    b->no_enlarge = v > 0.5; break;
            case PANEL_BATCH_SHARPEN:       b->sharpen = v; break;
            case PANEL_BATCH_ROTATE:        b->rotate = (int32_t)v; break;
            case PANEL_BATCH_FLIP:          b->flip = (int32_t)v; break;
            case PANEL_BATCH_AUTOCROP:      b->autocrop = v > 0.5; break;
            case PANEL_BATCH_AUTOCOLOUR:    b->auto_colour = v > 0.5; break;
            case PANEL_BATCH_GRAY:          b->grayscale = v > 0.5; break;
            case PANEL_BATCH_NEGATIVE:      b->negative = v > 0.5; break;
            case PANEL_BATCH_EFFECT: {
                b->effect = (int32_t)v; again = true;
                double start = 0;
                rubraview_effect_range((rubraview_effect_t)b->effect, NULL, NULL, &start);
                b->effect_amount = start;
                break;
            }
            case PANEL_BATCH_EFFECT_AMOUNT: b->effect_amount = v; break;
            case PANEL_BATCH_BORDER:        b->border = v; break;
            case PANEL_BATCH_FRAME_COLOUR:  b->frame_colour = (int32_t)v; break;
            case PANEL_BATCH_MARK_CORNER:   b->mark_corner = (int32_t)v; break;
            case PANEL_BATCH_MARK_OPACITY:  b->mark_opacity = v; break;
            case PANEL_BATCH_TEXT_CORNER:   b->text_corner = (int32_t)v; break;
            case PANEL_BATCH_TEXT_SIZE:     b->text_size = v; break;
            case PANEL_BATCH_TEXT_COLOUR:   b->text_colour = (int32_t)v; break;
            case PANEL_BATCH_DPI:           b->dpi = v; break;
            case PANEL_BATCH_FORMAT:        b->format = (int32_t)v; again = true; break;
            case PANEL_BATCH_QUALITY:       b->quality = v; break;
            case PANEL_PRIVACY:             b->privacy = v > 0.5; app->export_options.privacy_clean = b->privacy; again = true; break;
            default: if (export_more_apply(&b->more, row->id, v)) again = true; break;
            case PANEL_BATCH_START:         b->start = v; break;
            case PANEL_BATCH_EXISTS:        b->exists = (int32_t)v; break;
            case PANEL_BATCH_KEEP_DATES:    b->keep_dates = v > 0.5; break;
            case PANEL_BATCH_RECURSIVE:     b->recursive = v > 0.5; again = true; break;
            case PANEL_BATCH_SUBFOLDERS:    b->subfolders = v > 0.5; break;
        }
        if (again) panel_open_batch(app);
        return;
    }

    if (gmic_row_changed(app, row->id, v)) return;   /* RFC-0009 C: its parameters' rows */
    if (ffmpeg_row_changed(app, row->id, v)) return;   /* RFC-0009 D: its options' rows */
    switch (row->id) {
        case PANEL_EXPOSURE:    rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_EXPOSURE, (float)v); break;
        case PANEL_BRIGHTNESS:  rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_BRIGHTNESS, (float)v); break;
        case PANEL_CONTRAST:    rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_CONTRAST, (float)v); break;
        case PANEL_SATURATION:  rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_SATURATION, (float)v); break;
        case PANEL_TEMPERATURE: rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_TEMPERATURE, (float)v); break;
        case PANEL_TINT:        rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_TINT, (float)v); break;
        case PANEL_BAL_R:       app->edit.params.balance_r = (float)v; break;
        case PANEL_BAL_G:       app->edit.params.balance_g = (float)v; break;
        case PANEL_BAL_B:       app->edit.params.balance_b = (float)v; break;
        case PANEL_EFFECT_GROUP:   /* another kind: none of it chosen yet */
            app->effect_group = (int32_t)v;
            app->effect_choice = RUBRAVIEW_EFFECT_NONE;
            panel_build_edit(app);
            break;
        case PANEL_EFFECT: {
            rubraview_effect_t members[RUBRAVIEW_EFFECT_COUNT];
            size_t n = rubraview_effect_group_members((rubraview_effect_group_t)app->effect_group, members, RUBRAVIEW_EFFECT_COUNT);
            size_t pick = (size_t)lround(v);
            app->effect_choice = pick >= 1 && pick <= n ? (int32_t)members[pick - 1] : RUBRAVIEW_EFFECT_NONE;
            double start = 0.0;
            rubraview_effect_range((rubraview_effect_t)app->effect_choice, NULL, NULL, &start);
            app->effect_amount = start;
            panel_build_edit(app);   /* the amount's range is the effect's */
            break;
        }
        case PANEL_EFFECT_AMOUNT: app->effect_amount = v; break;
        case PANEL_PLUGIN: {   /* RFC-0007: another plug-in, its parameters at their defaults */
            const rubraview_plugin_t *list[RUBRAVIEW_PLUGIN_MAX];
            size_t n = plugins_usable(list, RUBRAVIEW_PLUGIN_MAX);
            app->plugin_choice = (int32_t)lround(v);
            if (app->plugin_choice < 0 || (size_t)app->plugin_choice > n) app->plugin_choice = 0;
            if (app->plugin_choice > 0) rubraview_plugin_params_clamp(list[app->plugin_choice - 1], app->plugin_values, 0);
            panel_build_edit(app);
            break;
        }
        case PANEL_PLUGIN_P0: case PANEL_PLUGIN_P1: case PANEL_PLUGIN_P2: case PANEL_PLUGIN_P3:
        case PANEL_PLUGIN_P4: case PANEL_PLUGIN_P5: case PANEL_PLUGIN_P6: case PANEL_PLUGIN_P7:
            app->plugin_values[row->id - PANEL_PLUGIN_P0] = v;
            break;
        case PANEL_COMPARE:     app->edit_compare = v > 0.5; break;
        case PANEL_GAMMA:       rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_MIDTONE_GAMMA, (float)v); break;
        case PANEL_BLUR:        rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_BLUR_SIGMA, (float)v); break;
        case PANEL_SHARPEN:     rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_SHARPEN_AMOUNT, (float)v); break;
        case PANEL_BLACK:       rubraview_edit_set_black_point(&app->edit, (int32_t)v);
                                rubraview_panel_set_value(&app->panel, PANEL_WHITE,
                                                          (double)app->edit.params.white_point); break;
        case PANEL_WHITE:       rubraview_edit_set_white_point(&app->edit, (int32_t)v);
                                rubraview_panel_set_value(&app->panel, PANEL_BLACK,
                                                          (double)app->edit.params.black_point); break;
        case PANEL_CROP_RATIO:  rubraview_edit_crop_set_ratio(&app->edit, (rubraview_crop_ratio_t)(int32_t)v); crop_shown_again(app); break;
        /* RV-108 */
        case PANEL_BLUR_KIND:    app->edit.blur_box = v > 0.5; break;
        case PANEL_SHARPEN_KIND: app->edit.sharpen_laplacian = v > 0.5; break;
        case PANEL_SHARPEN_RADIUS: rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_SHARPEN_RADIUS, (float)v); break;
        case PANEL_SHARPEN_THRESHOLD: app->edit.sharpen_threshold = (uint8_t)(v < 0.0 ? 0.0 : v > 255.0 ? 255.0 : v); break;
        case PANEL_RESIZE_PRESET: {
            int32_t w = 0, h = 0, sw = 0, sh = 0;
            (void)rubraview_pal_window_monitor_size(app->window, &sw, &sh);
            if (rubraview_resize_preset_size((rubraview_resize_preset_t)(int32_t)v, app->edit.image_width, app->edit.image_height,
                                             sw, sh, app->edit.resize_lock_aspect, &w, &h)) {
                app->edit.resize_width = w;
                app->edit.resize_height = h;
                rubraview_panel_set_value(&app->panel, PANEL_RESIZE, 100.0 * (double)w / (double)app->edit.image_width);
            }
            int32_t k = rubraview_panel_find(&app->panel, PANEL_RESIZE_PX);
            if (k >= 0) app->panel.rows[k].label = px_button_label(app);
            break;
        }
        case PANEL_RESIZE_LOCK:
            app->edit.resize_lock_aspect = v > 0.5;
            if (app->edit.resize_lock_aspect && app->edit.resize_width > 0) {   /* the height follows the width again */
                int32_t w = 0, h = 0;
                rubraview_edit_session_t whole = edit_whole(app);
                if (rubraview_edit_resize_pair(&whole, app->edit.resize_width, true, &w, &h)) { app->edit.resize_width = w; app->edit.resize_height = h; }
                int32_t k = rubraview_panel_find(&app->panel, PANEL_RESIZE_PX);
                if (k >= 0) app->panel.rows[k].label = px_button_label(app);
            }
            break;
        case PANEL_RESIZE_FILTER:  app->edit.resize_filter = (rubraview_resample_filter_t)(int32_t)v; break;
        case PANEL_RESIZE_LINEAR:  app->edit.resize_linear = v > 0.5; break;
        case PANEL_RESIZE_SHARPEN: app->edit.resize_sharpen = (float)v; break;
        case PANEL_RESIZE_DPI: {
            app->edit.resize_dpi = v;
            int32_t k = rubraview_panel_find(&app->panel, PANEL_RESIZE_PX);
            if (k >= 0) app->panel.rows[k].label = px_button_label(app);
            break;
        }
        case PANEL_RESIZE: {
            /* A hundred is the size as it is; the size is of the whole picture (RV-128: whatever is selected). */
            int32_t w = 0, h = 0;
            rubraview_edit_session_t whole = edit_whole(app);
            rubraview_panel_set_value(&app->panel, PANEL_RESIZE_PRESET, 0.0);
            if (lround(v) == 100 || !rubraview_edit_resize_percent(&whole, v, &w, &h)) {
                app->edit.resize_width = app->edit.resize_height = 0;
            } else {
                rubraview_edit_set_resize(&app->edit, w, h, true);
            }
            int32_t k = rubraview_panel_find(&app->panel, PANEL_RESIZE_PX);
            if (k >= 0) app->panel.rows[k].label = px_button_label(app);
            break;
        }
        default: break;
    }
    /* The preview is of the tones, the blur and the sharpening: any of them changing draws it again. */
    app->edit_shown_valid = false;
    app->edit_rev++;
    /* The owner (2026-10-18): the change is on the picture at once, a step of the undo list. */
    if (edit_panel_open(app)) {
        switch (row->id) {
            case PANEL_EXPOSURE: case PANEL_BRIGHTNESS: case PANEL_CONTRAST: case PANEL_SATURATION: case PANEL_TEMPERATURE:
            case PANEL_TINT: case PANEL_BAL_R: case PANEL_BAL_G: case PANEL_BAL_B: case PANEL_GAMMA: case PANEL_BLUR:
            case PANEL_SHARPEN: case PANEL_BLACK: case PANEL_WHITE: case PANEL_BLUR_KIND: case PANEL_SHARPEN_KIND:
            case PANEL_SHARPEN_RADIUS: case PANEL_SHARPEN_THRESHOLD:
                edit_live_update(app, RUBRAVIEW_STEP_ADJUST);
                break;
            case PANEL_EFFECT: case PANEL_EFFECT_AMOUNT: case PANEL_EFFECT_GROUP:
                edit_live_update(app, RUBRAVIEW_STEP_EFFECT);
                break;
            default: break;
        }
    }
}

static void panel_button(app_state_t *app, int32_t index) {
    if (index < 0 || (size_t)index >= app->panel.row_count) return;
    if (app->panel.rows[index].id >= PANEL_GMIC_P0 && app->panel.rows[index].id < PANEL_GMIC_P0 + PANEL_GMIC_ROWS &&
        gmic_button(app, app->panel.rows[index].id)) return;   /* RFC-0009 C: a text, a colour or a point typed */
    if (app->panel.rows[index].id >= PANEL_FFMPEG_P0 && app->panel.rows[index].id < PANEL_FFMPEG_P0 + PANEL_FFMPEG_ROWS &&
        ffmpeg_button(app, app->panel.rows[index].id)) return;   /* RFC-0009 D: an option typed */
    switch (app->panel.rows[index].id) {
        case PANEL_RESET:
            /* Everything since the panel opened: the steps, the values, the selection. */
            app->edit_live = false;
            rubraview_edit_steps_reset(&app->edit_steps);
            rubraview_edit_reset(&app->edit);
            edit_values_fresh(app);
            app->edit.crop_active = false;
            edit_steps_changed(app);
            osd_say(app, TU8("back to the picture as it was"));
            break;
        case PANEL_APPLY_SEL:    (void)edit_apply_pending(app, true); break;
        case PANEL_EFFECT_APPLY: edit_effect_apply(app); break;
        case PANEL_PLUGIN_APPLY: edit_plugin_apply(app); break;   /* RFC-0007 */
        case PANEL_GMIC_APPLY: case PANEL_GMIC_TYPE: (void)gmic_button(app, app->panel.rows[index].id); break;   /* RFC-0009 C */
        case PANEL_FFMPEG_APPLY: case PANEL_FFMPEG_TYPE: (void)ffmpeg_button(app, app->panel.rows[index].id); break;   /* RFC-0009 D */
        case PANEL_AUTO_COLOUR:  edit_auto_colour(app); break;
        case PANEL_CROP_SEL:     edit_crop_to_selection(app); break;
        case PANEL_RESIZE_APPLY: edit_resize_apply(app); break;
        case PANEL_UNDO:         edit_undo(app, false); break;
        case PANEL_REDO:         edit_undo(app, true); break;
        case PANEL_STEPS:        edit_steps_toggle(app); break;
        case PANEL_CHANGES_REMOVE: changes_remove_checked(app); break;
        case PANEL_SEL_MODE:
            sel_mode_set(app, !app->sel_mode);
            osd_say(app, app->sel_mode ? TU8("drag on the picture to choose an area (Esc: stop choosing)") : TU8("a drag moves the picture again"));
            panel_build_edit(app);
            break;
        case PANEL_SEL_COPY:     edit_copy_area(app); break;
        case PANEL_ALL_COPIES:   apply_all_begin(app); break;
        case PANEL_ALL_ORIGINALS: apply_all_originals(app); break;
        case PANEL_SEL_BOX:      edit_sel_toggle(app); break;
        case PANEL_SAVE_ORIGINAL:
            (void)edit_apply_pending(app, false);
            if (on_archive_page(app)) panel_write_archive_page(app, true);
            else edit_save_over(app);
            break;
        case PANEL_EDIT_PAGE:
            g_edit_page_two = !g_edit_page_two;
            panel_build_edit(app);
            break;
        case PANEL_RESIZE_PX:
            px_edit_begin(app);
            break;
        case PANEL_CROP_PX:
            px_edit_begin_crop(app);
            break;
        case PANEL_TRIM: {
            /* RV-108 (§3.13): the plain margin of a scan — white, or black when there is no white one —
               found on the preview's small copy and marked as the cut, for the reader to see before saving. */
            const rubraview_pixbuf_t *small = &app->edit_small;
            int32_t x = 0, y = 0, w = 0, h = 0;
            bool found = rubraview_pixbuf_is_valid(small) &&
                         rubraview_filter_autotrim_rect(small, 245, true, &x, &y, &w, &h) &&
                         (w < small->width || h < small->height);
            if (!found) {
                found = rubraview_pixbuf_is_valid(small) &&
                        rubraview_filter_autotrim_rect(small, 15, false, &x, &y, &w, &h) &&
                        (w < small->width || h < small->height);
            }
            if (!found || w < 2 || h < 2) { osd_say(app, TU8("no plain margin was found")); break; }
            double sx = (double)app->edit.image_width / (double)small->width;
            double sy = (double)app->edit.image_height / (double)small->height;
            rubraview_edit_crop_set_ratio(&app->edit, RUBRAVIEW_CROP_FREE);
            rubraview_edit_crop_drag(&app->edit, (int32_t)floor(x * sx), (int32_t)floor(y * sy),
                                     (int32_t)ceil((x + w) * sx), (int32_t)ceil((y + h) * sy));
            app->edit_rev++;
            panel_build_edit(app);
            edit_sel_sync(app);
            osd_say(app, TU8("the inside of the margins is selected: Crop to the selection cuts them"));
            break;
        }
        case PANEL_SAVE_COPY:
            (void)edit_apply_pending(app, false);
            panel_write_current(app, true);
            panel_close(app);
            break;
        case PANEL_SAVE_OVER:
            panel_write_archive_page(app, true);
            panel_close(app);
            break;
        case PANEL_EXPORT_NOW:
            panel_write_current(app, app->export_from_edit);   /* opened from Edit: the edits written in */
            panel_close(app);
            break;
        case PANEL_BATCH_CROP: case PANEL_BATCH_CANVAS: case PANEL_BATCH_NAME: case PANEL_BATCH_REPLACE: case PANEL_BATCH_OUT: case PANEL_BATCH_TEXT:
            batch_type_begin(app, app->panel.rows[index].id);
            break;
        case PANEL_BATCH_TEST: batch_try_names(app); break;
        case PANEL_BATCH_SAVE: batch_save_or_load(app, true); break;
        case PANEL_BATCH_LOAD: batch_save_or_load(app, false); break;
        case PANEL_BATCH_MARK:
            if (app->batch.mark[0]) { app->batch.mark[0] = '\0'; panel_open_batch(app); break; }   /* pressed again: none */
            {
                static const rubraview_file_filter_t FILTERS[] = { { "Pictures (*.png;*.webp;*.gif;*.jpg)", "*.png;*.webp;*.gif;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff" } };
                rubraview_file_dialog_opts_t opts = { .title = T("A picture to lay over each one"), .filters = FILTERS, .filter_count = 1,
                                                      .parent_window_handle = rubraview_pal_window_native_handle(app->window) };
                rubraview_file_dialog_result_t picked = rubraview_pal_file_dialog_open(app->arena, &opts);
                if (picked.accepted && picked.count > 0)
                    snprintf(app->batch.mark, sizeof(app->batch.mark), "%.*s", (int)picked.paths[0].size, picked.paths[0].ptr);
                panel_open_batch(app);
            }
            break;
        case PANEL_BATCH_RUN:
            /* The dialog builds the same job the command line builds,
               and hands it to the same engine (§3.11). */
            panel_run_batch(app);
            panel_close(app);
            break;
        case PANEL_PRINT_DIALOG:
            app->print_with_dialog = true;
            /* fall through */
        case PANEL_PRINT_NOW:
            /* The panel goes first: the system's dialog holds the window while it is up. */
            app->print_going = true;
            panel_close(app);
            print_now(app);
            app->print_going = false;
            print_temp_drop(app);
            break;
        case PANEL_CONV_NOW: {
            rubraview_arcedit_target_t target = arc_convert_target(app);
            app->panel_is_convert = false;   /* answered: closing the panel is not a "no" */
            panel_close(app);
            arc_edit_start(app, target);
            break;
        }
        default: break;
    }
}

/* The owner's edit: a slider's name pressed asks for its value, typed. */
static bool edit_type_value_at(app_state_t *app, rubraview_panel_t *panel, int32_t which, double px, double py) {
    int32_t index = rubraview_panel_row_at(panel, px, py);
    if (index < 0 || panel->rows[index].kind != RUBRAVIEW_ROW_SLIDER) return false;
    rubraview_rect_t c = rubraview_panel_control_rect(panel, (size_t)index);
    if (px >= c.x) return false;
    if (app->rename_active) rename_end(app);
    char now[32];
    const rubraview_panel_row_t *row = &panel->rows[index];
    int n = snprintf(now, sizeof(now), row->step >= 1.0 ? "%.0f" : "%.2f", row->value);
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    rubraview_textedit_set(&app->rename_edit, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)now, .size = n > 0 ? (size_t)n : 0 });
    rubraview_textedit_select_all(&app->rename_edit);
    app->rename_active = true;
    app->rename_is_value = true;
    app->rename_composing_length = 0;
    app->typed_panel = which;
    app->typed_id = row->id;
    rubraview_pal_window_text_input(app->window, true);
    osd_say(app, TU8("type the value, then Enter (Esc leaves it)"));
    return true;
}

static void edit_value_typed(app_state_t *app) {
    proven_u8str_view_t text = rubraview_textedit_text(&app->rename_edit);
    char buf[48];
    size_t n = text.size < sizeof(buf) - 1 ? text.size : sizeof(buf) - 1;
    memcpy(buf, text.ptr, n);
    buf[n] = '\0';
    for (size_t i = 0; i < n; ++i) if (buf[i] == ',') buf[i] = '.';
    char *end = NULL;
    double v = strtod(buf, &end);
    int32_t which = app->typed_panel, id = app->typed_id;
    rename_end(app);
    if (!end || end == buf) { osd_say(app, TU8("that is not a number")); return; }
    rubraview_panel_t *panel = which == 2 ? &app->sel_panel : &app->panel;
    rubraview_panel_set_value(panel, id, v);
    int32_t index = rubraview_panel_find(panel, id);
    if (which == 2) edit_sel_apply_row(app, index); else panel_apply_row(app, index);
}

/* The two boxes beside the edit: the selection's numbers and the undo list. */
bool edit_side_press(app_state_t *app, double px, double py) {
    if (!edit_panel_open(app)) return false;
    rubraview_panel_t *boxes[2] = { &app->steps_panel, &app->sel_panel };
    for (int b = 0; b < 2; ++b) {
        rubraview_panel_t *box = boxes[b];
        if (!box->open || !rubraview_rect_contains(box->bounds, px, py)) continue;
        if (rubraview_panel_title_hit(box, px, py)) { rubraview_panel_move_begin(box, px, py); return true; }
        if (box == &app->sel_panel && edit_type_value_at(app, box, 2, px, py)) return true;
        int32_t row = -1;
        rubraview_panel_event_t ev = rubraview_panel_press(box, px, py, &row);
        if (ev == RUBRAVIEW_PANEL_CLOSE) { box->open = false; return true; }
        if (row < 0) return true;
        if (ev == RUBRAVIEW_PANEL_CHOOSE) {   /* 1.24.0 */
            rubraview_rect_t c = rubraview_panel_control_rect(box, (size_t)row);
            chooser_open_panel(app, box == &app->sel_panel ? 2 : 1, row, c.x, c.y + c.height, c.y);
            return true;
        }
        int32_t id = box->rows[row].id;
        if (box == &app->steps_panel && id >= PANEL_STEP_FIRST) {
            edit_live_close(app);
            if (rubraview_edit_steps_go(&app->edit_steps, (size_t)(id - PANEL_STEP_FIRST))) edit_steps_changed(app);
        } else if (ev == RUBRAVIEW_PANEL_VALUE_CHANGED) {
            edit_sel_apply_row(app, row);
        } else if (ev == RUBRAVIEW_PANEL_BUTTON_PRESSED && id == PANEL_SEL_ALL) {
            app->edit.crop_active = false;
            app->edit_rev++;
            app->edit_shown_valid = false;
            panel_build_edit(app);
            edit_sel_sync(app);
        } else if (ev == RUBRAVIEW_PANEL_BUTTON_PRESSED && id == PANEL_SEL_TYPE) {
            px_edit_begin_crop(app);
        }
        return true;
    }
    return false;
}

bool panel_handle_press(app_state_t *app, double px, double py) {
    if (!app->panel.open) return false;

    /* The curve box is drawn outside the panel's own rectangle, so the
       rule below ("a click outside closes it") would shut the workbench
       the moment a point was grabbed. It is asked first. */
    if (curve_widget_press(app, px, py, false)) return true;

    /* The owner's edit: taken by its title, it moves. */
    if (rubraview_panel_title_hit(&app->panel, px, py)) { rubraview_panel_move_begin(&app->panel, px, py); return true; }
    /* A value's name (left of its slider) is pressed: the value is typed. */
    /* 1.24.0 (owner, 2026-10-19): every panel's sliders, print and export too */
    if (edit_type_value_at(app, &app->panel, 1, px, py)) return true;
    int32_t row = -1;
    rubraview_panel_event_t event = rubraview_panel_press(&app->panel, px, py, &row);
    if (event == RUBRAVIEW_PANEL_CLOSE) {   /* its X (owner, 2026-10-15) */
        panel_close(app);
        return true;
    }
    if (event == RUBRAVIEW_PANEL_VALUE_CHANGED) {
        panel_apply_row(app, row);
        return true;
    }
    if (event == RUBRAVIEW_PANEL_CHOOSE) {   /* 1.24.0: its words to pick from, under it */
        rubraview_rect_t c = rubraview_panel_control_rect(&app->panel, (size_t)row);
        chooser_open_panel(app, 1, row, c.x, c.y + c.height, c.y);
        return true;
    }
    if (event == RUBRAVIEW_PANEL_BUTTON_PRESSED) {
        panel_button(app, row);
        return true;
    }
    /* A click outside the panel closes it, which is the same rule the
       menu box follows (§3.6.2) — not the edit's, whose selection is
       drawn outside it. */
    if (edit_panel_open(app) && !rubraview_rect_contains(app->panel.bounds, px, py)) return false;
    if (!rubraview_rect_contains(app->panel.bounds, px, py)) {
        panel_close(app);
        return true;
    }
    return true;   /* the click landed on the panel: it is not the canvas's */
}


/* Draws whichever of the three panels is open. The model decided every
   rectangle; this only fills them. */
void draw_panel_of(app_state_t *app, const rubraview_panel_t *panel, bool numbers);
void draw_panel(app_state_t *app) {
    bool windowed = app->edit_window && edit_panel_open(app);   /* RV-137: Edit draws in its own window */
    if (app->panel.open && !windowed) draw_panel_of(app, &app->panel, edit_panel_open(app));
    /* The owner's edit: the selection's numbers and the undo list, each a box of its own. */
    if (edit_panel_open(app) && app->sel_panel.open && !windowed) draw_panel_of(app, &app->sel_panel, true);
    if (edit_panel_open(app) && app->steps_panel.open && !windowed) draw_panel_of(app, &app->steps_panel, false);
    /* RV-133: the classify window says what the page on screen is set aside for */
    if (app->classify_panel.open) {
        if (app->classify_shown_page != current_page_index(app)) classify_build(app);
        draw_panel_of(app, &app->classify_panel, false);
    }
}

void draw_panel_of(app_state_t *app, const rubraview_panel_t *panel, bool numbers) {
    rubraview_pal_rect_t body = { panel->bounds.x, panel->bounds.y,
                                  panel->bounds.width, panel->bounds.height };
    double text_size = panel->row_height * 0.45;
    if (panel->bare) goto rows;   /* RV-137: the window is its box; no title or X of its own */
    rubraview_pal_render_fill_rect(app->renderer, body, COLOR_BOX_FILL, 3.0);
    rubraview_pal_render_stroke_rect(app->renderer, body, COLOR_BOX_BORDER, 1.0, 3.0);

    rubraview_pal_rect_t title = { panel->bounds.x + panel->padding,
                                   panel->bounds.y + panel->padding,
                                   panel->bounds.width - panel->padding * 2.0,
                                   panel->row_height };
    rubraview_pal_render_draw_text(app->renderer, panel->title, title,
                                   text_size * 1.15, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    {
        /* The X that closes it with the mouse (owner, 2026-10-15). */
        rubraview_rect_t xr = rubraview_panel_close_rect(panel);
        rubraview_pal_rect_t xb = { xr.x, xr.y, xr.width, xr.height };
        if (rubraview_rect_contains(xr, app->pointer_x, app->pointer_y)) rubraview_pal_render_fill_rect(app->renderer, xb, COLOR_CLOSE_HOVER, 2.0);
        rubraview_pal_render_draw_text(app->renderer, PROVEN_LIT("X"), xb, text_size, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }

rows:
    g_conv_is_7z = lround(rubraview_settings_get(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("convert_format"))) == 1;
    for (size_t i = 0; i < panel->row_count; ++i) {
        const rubraview_panel_row_t *row = &panel->rows[i];
        rubraview_rect_t r = rubraview_panel_row_rect(panel, i);
        if (r.width <= 0.0) continue;  /* scrolled out of the box */
        if (row->kind == RUBRAVIEW_ROW_SPACER) continue;   /* room the caller draws in */

        if (row->kind == RUBRAVIEW_ROW_SEPARATOR) {
            rubraview_pal_rect_t rule = { r.x, r.y + r.height * 0.5, r.width, 1.0 };
            rubraview_pal_render_fill_rect(app->renderer, rule, COLOR_BOX_BORDER, 0.0);
            continue;
        }

        rubraview_rect_t c = rubraview_panel_control_rect(panel, i);

        if (row->kind == RUBRAVIEW_ROW_BUTTON) {
            rubraview_pal_rect_t button = { c.x, c.y + c.height * 0.15, c.width, c.height * 0.7 };
            rubraview_pal_render_fill_rect(app->renderer, button, COLOR_TILE_FILL, 2.0);
            rubraview_pal_render_stroke_rect(app->renderer, button, COLOR_BOX_BORDER, 1.0, 2.0);
            rubraview_pal_render_draw_text(app->renderer, row->label, button,
                                           text_size, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
            continue;
        }

        rubraview_pal_rect_t label = { r.x, r.y, panel->label_width, r.height };
        rubraview_pal_render_draw_text(app->renderer, row->label, label,
                                       text_size, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);

        if (row->kind == RUBRAVIEW_ROW_SLIDER) {
            rubraview_pal_rect_t track = { c.x, c.y + c.height * 0.4, c.width, c.height * 0.2 };
            rubraview_pal_render_fill_rect(app->renderer, track, COLOR_BOX_BORDER, 1.0);

            double fill = rubraview_panel_fill_fraction(panel, i);
            rubraview_pal_rect_t handle = { c.x + c.width * fill - c.height * 0.2, c.y,
                                            c.height * 0.4, c.height };
            rubraview_pal_render_fill_rect(app->renderer, handle, COLOR_TEXT, 2.0);
            if (numbers) {
                /* The owner's edit: every value as a number, typed by pressing its name. */
                char number[24];
                int k = snprintf(number, sizeof(number), row->step >= 1.0 ? "%.0f" : "%.2f", row->value);
                rubraview_pal_rect_t at = { r.x, r.y, panel->label_width - panel->padding * 0.5, r.height };
                if (k > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)number, .size = (size_t)k }, at,
                                                          text_size * 0.9, 0xFFB8D0F0u, RUBRAVIEW_TEXT_RIGHT);
            } else if (!app->panel_is_print && !app->panel_is_convert &&
                (row->id == PANEL_RESIZE || row->id == PANEL_SHARPEN_RADIUS || row->id == PANEL_SHARPEN_THRESHOLD)) {
                char number[16];
                rubraview_pal_rect_t at = { r.x, r.y, panel->label_width - panel->padding, r.height };
                rubraview_pal_render_draw_text(app->renderer, adjust_row_text(app, row, number, sizeof(number)),
                                               at, text_size, COLOR_TEXT, RUBRAVIEW_TEXT_RIGHT);
            }
            bool shown = numbers || (!app->panel_is_print && !app->panel_is_convert &&
                         (row->id == PANEL_RESIZE || row->id == PANEL_SHARPEN_RADIUS || row->id == PANEL_SHARPEN_THRESHOLD));
            if (app->panel_is_print || app->panel_is_convert) {
                /* D-87: a scale or a margin is chosen by its number. */
                char number[16];
                rubraview_pal_rect_t at = { r.x, r.y, panel->label_width - panel->padding, r.height };
                rubraview_pal_render_draw_text(app->renderer, print_row_text(row, number, sizeof(number)),
                                               at, text_size, COLOR_TEXT, RUBRAVIEW_TEXT_RIGHT);
                shown = true;
            }
            if (!shown) {   /* 1.24.0: every slider says its number */
                char number[24];
                int k = snprintf(number, sizeof(number), row->step >= 1.0 ? "%.0f" : "%.2f", row->value);
                rubraview_pal_rect_t at = { r.x, r.y, panel->label_width - panel->padding, r.height };
                if (k > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)number, .size = (size_t)k }, at,
                                                          text_size, COLOR_TEXT, RUBRAVIEW_TEXT_RIGHT);
            }
            {   /* 1.24.0: and in a box, which a press opens for typing (as its name does) */
                double fw = text_size * 4.2;
                rubraview_pal_rect_t field = { r.x + panel->label_width - panel->padding * 0.4 - fw, r.y + r.height * 0.14, fw, r.height * 0.72 };
                rubraview_pal_render_stroke_rect(app->renderer, field, pal_rect_has(field, app->pointer_x, app->pointer_y) ? COLOR_TILE_CURRENT : COLOR_BOX_BORDER,
                                                 1.0, 2.0);
            }
        } else if (row->kind == RUBRAVIEW_ROW_TOGGLE) {
            rubraview_pal_rect_t box = { c.x, c.y, c.height, c.height };
            rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_BOX_BORDER, 1.0, 2.0);
            if (row->value > 0.5) {
                rubraview_pal_rect_t mark = { box.x + box.width * 0.25, box.y + box.height * 0.25,
                                              box.width * 0.5, box.height * 0.5 };
                rubraview_pal_render_fill_rect(app->renderer, mark, COLOR_TEXT, 1.0);
            }
        } else if (row->kind == RUBRAVIEW_ROW_CHOICE && (app->panel_is_print || app->panel_is_convert)) {
            /* D-87: the print panel's choices have names. */
            char words[48];
            rubraview_pal_rect_t value = { c.x, c.y, c.width, c.height };
            rubraview_pal_render_draw_text(app->renderer, print_row_text(row, words, sizeof(words)),
                                           value, text_size, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
        } else if (row->kind == RUBRAVIEW_ROW_CHOICE) {
            /* RV-108: a choice says its word. */
            char words[48];
            rubraview_pal_rect_t value = { c.x, c.y, c.width, c.height };
            rubraview_pal_render_draw_text(app->renderer, adjust_row_text(app, row, words, sizeof(words)),
                                           value, text_size, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
        }
    }
}


static void draw_files_window(app_state_t *app) {
    if (!app->files_open) return;
    size_t count = page_count(app);
    int32_t current = current_page_index(app);
    /* Another archive, folder or playlist: its list starts afresh. */
    if (app->files_pages != app->source.pages || app->files_count != count) {
        app->files_pages = app->source.pages;
        app->files_count = count;
        app->files_list.first = 0;
        app->files_selected = app->files_hover = -1;
        app->files_followed = -1;
        app->files_want = -1;
        files_preview_drop(app);
        app->files_dirty = true;
    }
    if (current != app->files_followed && current >= 0) {     /* follow the file on screen */
        size_t at = list_row_of(app, (size_t)current);
        if (at != SIZE_MAX) rubraview_listwin_reveal(&app->files_list, at, list_rows(app));
        app->files_followed = current;
        app->files_dirty = true;
    }
    if (!app->files_dirty) return;
    app->files_dirty = false;

    rubraview_renderer_t *r = app->files_renderer;
    listwin_sync(app);
    size_t shown = list_rows(app);   /* 1.25.0: the rows the filter and .ext leave */
    app->files_list.editable = list_editable(app);   /* RV-117: changed as the floating list is, or only looked at */
    const rubraview_listwin_t *lw = &app->files_list;
    double dpi = rubraview_pal_window_dpi_scale(app->files_window);
    double font = lw->row_height * 0.5;
    rubraview_pal_render_begin(r, FILES_BACKGROUND);

    /* The header: what the list is, and where the file on screen is in it. */
    bool in_archive = app->source.archive_path.size > 0 && !app->list_is_set;
    char head[640], cut[640];
    proven_u8str_view_t what = app->list_is_set ? TU8("Playlist")
                 : in_archive ? rubraview_path_basename(app->source.archive_path)
                 : rubraview_path_basename(app->source_dir);
    int hn = current >= 0 ? snprintf(head, sizeof(head), "%d / %zu   %.*s", current + 1, count, (int)(what.size < 560 ? what.size : 560), what.ptr)
                          : snprintf(head, sizeof(head), "%zu   %.*s", count, (int)(what.size < 560 ? what.size : 560), what.ptr);
    rubraview_pal_rect_t head_box = { lw->x, lw->y, lw->width, lw->title_height };
    rubraview_pal_render_fill_rect(r, head_box, COLOR_TILE_FILL, 0.0);
    rubraview_pal_rect_t head_text = { lw->x + lw->row_height * 0.4, lw->y, lw->width - lw->row_height * 0.8, lw->title_height };
    if (hn > 0) rubraview_pal_render_draw_text(r, files_fit_text(cut, sizeof(cut), (proven_u8str_view_t){ (const proven_byte_t *)head, (size_t)hn }, head_text.width, font),
                                               head_text, font, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    if (lw->editable) {
        draw_list_bar(app, r, lw, app->files_px, app->files_py);
        draw_list_tools(app, r, lw, app->files_px, app->files_py, true);
    }
    double head_h = rubraview_listwin_head_height(lw);

    size_t rows = rubraview_listwin_rows_visible(lw);
    for (size_t at = lw->first; at < shown && at < lw->first + rows; ++at) {
        size_t i = list_page(app, at);
        if (i == SIZE_MAX) break;
        rubraview_rect_t rr = rubraview_listwin_row_rect(lw, at, shown);
        rubraview_pal_rect_t row = { rr.x + 2.0, rr.y, rr.width - 8.0, rr.height };
        bool checked = i < app->list_checked_count && app->list_checked[i];
        bool carried = app->list_row_dragging && app->list_held_in_files && (i == app->list_press_row ||
                       (checked && app->list_press_row < app->list_checked_count && app->list_checked[app->list_press_row]));
        if ((int32_t)i == current) rubraview_pal_render_fill_rect(r, row, (COLOR_TILE_CURRENT & 0x00FFFFFFu) | 0x90000000u, 2.0);
        else if (carried) rubraview_pal_render_fill_rect(r, row, 0x405B9BD5u, 2.0);
        else if ((int32_t)i == app->files_hover) rubraview_pal_render_fill_rect(r, row, COLOR_TILE_FILL, 2.0);
        if ((int32_t)i == app->files_selected) rubraview_pal_render_stroke_rect(r, row, COLOR_TEXT, 1.0, 2.0);
        if (lw->editable) draw_list_row_parts(app, r, lw, at, shown, app->files_px, app->files_py, i);
        double text_left = lw->editable ? rubraview_listwin_row_text_x(lw) : row.x;

        /* its size at the right (an archive knows it; a folder's files are not asked), its name in what is left */
        char size_text[32];
        uint64_t bytes = rubraview_page_source_entry_size(&app->source, i);
        double size_w = 0.0;
        if (bytes > 0) {
            int sn = files_size_text(size_text, sizeof(size_text), bytes);
            size_w = font * 0.56 * 9.0;
            rubraview_pal_rect_t size_box = { row.x + row.width - size_w - lw->row_height * 0.3, row.y, size_w, row.height };
            if (sn > 0) rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ (const proven_byte_t *)size_text, (size_t)sn }, size_box, font * 0.9, 0xB0F0F0F0u, RUBRAVIEW_TEXT_RIGHT);
        }
        char line[640];
        proven_u8str_view_t name = page_display_name(app, i);
        int m = snprintf(line, sizeof(line), "%4zu   %.*s", i + 1, (int)(name.size < 600 ? name.size : 600), name.ptr);
        rubraview_pal_rect_t text = { text_left + lw->row_height * 0.3, row.y, row.x + row.width - text_left - lw->row_height * 0.9 - size_w, row.height };
        if (m > 0) rubraview_pal_render_draw_text(r, files_fit_text(cut, sizeof(cut), (proven_u8str_view_t){ (const proven_byte_t *)line, (size_t)(m < (int)sizeof(line) ? m : (int)sizeof(line) - 1) }, text.width, font),
                                                  text, font, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    }
    draw_list_drop_line(app, r, lw, true);
    if (shown > rows && rows > 0) {              /* where in a long list the rows are */
        double track = lw->height - head_h;
        double bh = track * (double)rows / (double)shown;
        if (bh < lw->row_height * 0.5) bh = lw->row_height * 0.5;
        double by = lw->y + head_h + (track - bh) * (double)lw->first / (double)(shown - rows);
        rubraview_pal_rect_t bar = { lw->x + lw->width - 5.0, by, 3.0, bh };
        rubraview_pal_render_fill_rect(r, bar, COLOR_BOX_BORDER | 0x60000000u, 1.5);
    }

    /* The preview: the picture fitted above two lines that say what it is. */
    rubraview_rect_t pv = app->files_preview_rect;
    if (pv.width > 0.0) {
        rubraview_pal_rect_t edge = { pv.x, pv.y, 1.0, pv.height };
        rubraview_pal_render_fill_rect(r, edge, COLOR_BOX_BORDER, 0.0);
        double caption_h = lw->row_height * 2.4;
        rubraview_rect_t room = { pv.x, pv.y, pv.width, pv.height - caption_h };
        int32_t page = app->files_preview_page;
        const char *say = NULL;
        switch (app->files_preview_state) {
            case FILES_PREVIEW_SHOWN: {
                int32_t tw = 0, th = 0;
                rubraview_pal_texture_size(app->files_preview, &tw, &th);
                rubraview_rect_t fit = rubraview_filewin_fit(room, (double)app->files_preview_w, (double)app->files_preview_h, 8.0 * dpi);
                if (fit.width > 0.0 && tw > 0 && th > 0) {
                    rubraview_mat3x2_t place = rubraview_mat3x2_multiply(
                        rubraview_mat3x2_scale(fit.width / (double)tw, fit.height / (double)th),
                        rubraview_mat3x2_translate(fit.x, fit.y));
                    rubraview_pal_render_draw_texture(r, app->files_preview, place, RUBRAVIEW_INTERP_LINEAR);
                }
                break;
            }
            case FILES_PREVIEW_FAILED: say = T("no preview: it could not be read as a picture"); break;
            case FILES_PREVIEW_FAR:    say = T("deep in a solid archive: Enter or a click opens it"); break;
            case FILES_PREVIEW_MEDIA:  say = T("a film or a sound: Enter or a click plays it"); break;
            default:                   say = page >= 0 ? NULL : T("point at a file, or use the arrow keys"); break;
        }
        if (say) {
            rubraview_pal_rect_t mid = { room.x + 8.0 * dpi, room.y, room.width - 16.0 * dpi, room.height };
            rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ (const proven_byte_t *)say, strlen(say) }, mid, font, 0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
        }
        if (page >= 0 && (size_t)page < count) {
            rubraview_pal_rect_t line1 = { pv.x + 8.0 * dpi, pv.y + pv.height - caption_h, pv.width - 16.0 * dpi, caption_h * 0.5 };
            rubraview_pal_rect_t line2 = { line1.x, line1.y + caption_h * 0.5 - 2.0, line1.width, caption_h * 0.5 };
            proven_u8str_view_t name = page_display_name(app, (size_t)page);
            rubraview_pal_render_draw_text(r, files_fit_text(cut, sizeof(cut), name, line1.width, font), line1, font, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
            char facts[160], size_text[32];
            int fn = 0;
            uint64_t bytes = rubraview_page_source_entry_size(&app->source, (size_t)page);
            if (bytes > 0) files_size_text(size_text, sizeof(size_text), bytes); else size_text[0] = '\0';
            if (app->files_preview_state == FILES_PREVIEW_SHOWN) {
                fn = bytes > 0 ? snprintf(facts, sizeof(facts), "%d x %d   %s", app->files_preview_w, app->files_preview_h, size_text)
                               : snprintf(facts, sizeof(facts), "%d x %d", app->files_preview_w, app->files_preview_h);
            } else if (bytes > 0) {
                fn = snprintf(facts, sizeof(facts), "%s", size_text);
            }
            if (fn > 0) rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ (const proven_byte_t *)facts, (size_t)fn }, line2, font * 0.9, 0xB0F0F0F0u, RUBRAVIEW_TEXT_CENTER);
        }
    }
    if (app->ctx_in_files) draw_ctxmenu_on(app, r, app->files_px, app->files_py);   /* RV-117: a file's own list */
    draw_chooser(app, r, CHOOSE_IN_FILES, app->files_px, app->files_py);           /* 1.25.0: .ext, the sort */
    if (!rubraview_pal_render_end(r)) files_close(app);   /* its device went: the key opens it again */
}

static size_t files_pump(app_state_t *app) {
    size_t handled = 0;
    rubraview_window_event_t event;
    while (app->files_open && rubraview_pal_window_poll_event(app->files_window, &event)) {
        handled++;
        size_t count = list_rows(app), index = 0;   /* 1.25.0: rows; a row's page by list_page */
        switch (event.kind) {
            case RUBRAVIEW_WINDOW_EVENT_CLOSE:
                files_close(app);
                return handled;
            case RUBRAVIEW_WINDOW_EVENT_TEXT:   /* 1.25.0: the filter typed in the tools row */
                if (app->list_filter_in_files) {
                    char next[sizeof(app->list_filter)];
                    size_t n = strlen(app->list_filter);
                    memcpy(next, app->list_filter, n);
                    (void)rubraview_rename_append(next, sizeof(next), &n, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)event.text.utf8, .size = event.text.length });
                    list_filter_set(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)next, .size = n });
                    app->files_dirty = true;
                }
                break;
            case RUBRAVIEW_WINDOW_EVENT_RESIZE:
                rubraview_pal_render_resize(app->files_renderer, event.resize.width, event.resize.height);
                files_layout(app);
                break;
            case RUBRAVIEW_WINDOW_EVENT_PAINT:
            case RUBRAVIEW_WINDOW_EVENT_DPI_CHANGED:
                files_layout(app);
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE: {
                int32_t was = app->files_hover;
                app->files_px = event.mouse.x;
                app->files_py = event.mouse.y;
                app->files_hover = rubraview_listwin_hit(&app->files_list, event.mouse.x, event.mouse.y, count, &index) == RUBRAVIEW_LISTWIN_ROW
                                     ? (int32_t)list_page(app, index) : -1;
                if (app->files_hover != was) app->files_dirty = true;
                if (app->chooser.open && app->chooser.where == CHOOSE_IN_FILES) app->files_dirty = true;
                if (app->files_list.editable || app->ctx_in_files) app->files_dirty = true;   /* its buttons light under the pointer */
                list_motion(app, event.mouse.x, event.mouse.y, true);
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_UP:
                if (app->list_held_in_files && listwin_release(app)) app->files_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_DROP:
                /* RV-117: files from Explorer join the list at the row they are let go over */
                if (event.drop.count > 0 && list_editable(app)) {
                    proven_u8str_view_t paths[16];
                    size_t n = event.drop.count < 16 ? event.drop.count : 16;
                    for (size_t i = 0; i < n; ++i) paths[i] = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)event.drop.paths[i], .size = event.drop.path_lengths[i] };
                    listwin_sync(app);
                    size_t at = event.drop.has_point && !list_viewed(app) ? rubraview_listwin_insert_index(&app->files_list, event.drop.y, page_count(app))
                                                                          : page_count(app);
                    list_insert_paths(app, paths, n, at);
                    app->files_dirty = true;
                }
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_LEAVE:
                if (app->files_hover != -1) { app->files_hover = -1; app->files_dirty = true; }
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN: {
                bool right = event.mouse.button == RUBRAVIEW_MOUSE_RIGHT;
                if (chooser_press(app, CHOOSE_IN_FILES, event.mouse.x, event.mouse.y)) { app->files_dirty = true; break; }   /* 1.25.0 */
                if (app->list_filter_in_files &&
                    !rubraview_rect_contains(rubraview_listwin_tool_rect(&app->files_list, RUBRAVIEW_LISTWIN_FILTER), event.mouse.x, event.mouse.y))
                    list_filter_files_end(app);   /* a press elsewhere: done typing, the filter kept */
                if (app->ctx_open && app->ctx_in_files) {          /* its own list takes the press first */
                    if (popups_press(app, event.mouse.x, event.mouse.y, right) || !right) { app->files_dirty = true; break; }
                }
                if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT || right) {
                    list_press(app, &app->files_list, event.mouse.x, event.mouse.y, right, true);   /* RV-117: as the floating list */
                }
                app->files_dirty = true;
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_WHEEL:
                if (chooser_wheel(app, CHOOSE_IN_FILES, event.mouse.wheel_delta)) break;   /* 1.25.0 */
                rubraview_listwin_scroll(&app->files_list, event.mouse.wheel_delta > 0 ? -3 : 3, count);
                /* the row under the pointer is another one now */
                app->files_hover = rubraview_listwin_hit(&app->files_list, event.mouse.x, event.mouse.y, count, &index) == RUBRAVIEW_LISTWIN_ROW
                                     ? (int32_t)list_page(app, index) : -1;
                app->files_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN: {
                rubraview_key_combo_t combo = event.key.combo;
                long rows = (long)rubraview_listwin_rows_visible(&app->files_list);
                long step = 0;
                if (app->ctx_open && app->ctx_in_files && popups_key(app, combo)) { app->files_dirty = true; break; }
                if (chooser_key(app, CHOOSE_IN_FILES, combo)) break;   /* 1.25.0 */
                if (app->list_filter_in_files) {   /* 1.25.0: the filter's keys */
                    if (key_is(combo, "Escape")) { list_filter_set(app, PROVEN_LIT("")); list_filter_files_end(app); break; }
                    if (key_is(combo, "Enter")) { list_filter_files_end(app); break; }
                    if (key_is(combo, "Backspace")) {
                        size_t n = strlen(app->list_filter);
                        while (n > 0 && ((unsigned char)app->list_filter[n - 1] & 0xC0u) == 0x80u) --n;
                        if (n > 0) --n;
                        list_filter_set(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->list_filter, .size = n });
                        app->files_dirty = true;
                        break;
                    }
                    if (!(combo.modifiers & (RUBRAVIEW_MOD_CTRL | RUBRAVIEW_MOD_ALT))) break;   /* letters come as text */
                }
                if (key_is(combo, "Escape")) { files_close(app); return handled; }
                if (key_is(combo, "Enter")) {
                    files_go_to(app, app->files_selected >= 0 ? app->files_selected : files_target(app));
                    break;
                }
                if (key_is(combo, "Up")) step = -1;
                else if (key_is(combo, "Down")) step = +1;
                else if (key_is(combo, "PageUp")) step = -(rows > 1 ? rows - 1 : 1);
                else if (key_is(combo, "PageDown")) step = rows > 1 ? rows - 1 : 1;
                else if (key_is(combo, "Home")) step = -(long)count;
                else if (key_is(combo, "End")) step = (long)count;
                if (step != 0) {   /* 1.25.0: through the rows shown */
                    int32_t current = current_page_index(app);
                    size_t from = app->files_selected >= 0 ? list_row_of(app, (size_t)app->files_selected) : SIZE_MAX;
                    size_t here = current >= 0 ? list_row_of(app, (size_t)current) : SIZE_MAX;
                    size_t to = rubraview_filewin_step(from, here, step, count);
                    if (to != SIZE_MAX && list_page(app, to) != SIZE_MAX) {
                        app->files_selected = (int32_t)list_page(app, to);
                        app->files_hover = -1;           /* the keys have it now, wherever the pointer was left */
                        app->files_want_now = true;
                        rubraview_listwin_reveal(&app->files_list, to, count);
                        app->files_dirty = true;
                    }
                    break;
                }
                /* Any other key is the viewer's: its own key closes this window, the rest work as there. */
                dispatch_key(app, combo);
                app->files_dirty = true;
                break;
            }
            default:
                break;
        }
    }
    return handled;
}

/* The spread on screen as the compositor lays it out: the question
   draw_spread asks. False when there is no spread or no window. */
static bool spread_composition(app_state_t *app, rubraview_composition_t *out) {
    if (app->spread_index >= app->layout.count) return false;

    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) return false;

    const rubraview_spread_t *spread = &app->layout.spreads[app->spread_index];
    app_page_t *left = ensure_page_loaded(app, spread->left_index);
    app_page_t *right = ensure_page_loaded(app, spread->right_index);
    rubraview_page_size_t left_size = {0}, right_size = {0};
    if (left && left->loaded) {
        rubraview_orientation_apply_size(app->orientation, (double)picture_w((left)), (double)picture_h((left)),
                                         &left_size.width, &left_size.height);
    }
    if (right && right->loaded) {
        rubraview_orientation_apply_size(app->orientation, (double)picture_w((right)), (double)picture_h((right)),
                                         &right_size.width, &right_size.height);
    }
    *out = compose_view(
        app, spread, (left && left->loaded) ? &left_size : NULL, (right && right->loaded) ? &right_size : NULL,
        (double)win_w, (double)win_h);
    return true;
}

/* Where the page on screen sits, in client pixels, and how many client
   pixels one of its own pixels takes. The crop overlay needs both to
   turn a drag into image coordinates; it asks the compositor the same
   question draw_spread asks, so the rectangle it draws is the one the
   picture is in. Returns false when nothing is drawn. */
bool page_screen_rect(app_state_t *app, rubraview_pal_rect_t *out_rect, double *out_scale) {
    int32_t page_index = current_page_index(app);
    rubraview_composition_t comp;
    if (page_index < 0 || !spread_composition(app, &comp)) return false;

    for (size_t i = 0; i < comp.count; ++i) {
        const rubraview_draw_command_t *cmd = &comp.commands[i];
        if (cmd->page_index != page_index) continue;
        double x0 = cmd->transform.e, y0 = cmd->transform.f;
        double w = (cmd->src_right - cmd->src_left) * cmd->transform.a;
        double h = (cmd->src_bottom - cmd->src_top) * cmd->transform.d;
        if (w <= 0.0 || h <= 0.0) return false;
        *out_rect = (rubraview_pal_rect_t){ .x = x0, .y = y0, .width = w, .height = h };
        *out_scale = cmd->transform.a;
        return true;
    }
    return false;
}

/* §3.13: the crop rectangle, dragged on the picture itself. The model
   (normalising, the ratio lock, staying inside the image) is
   rubraview_edit_crop_drag's; this turns client pixels into image
   pixels and back, and draws what the model then holds. */
/* The owner's edit: where the picture as the steps left it is on screen —
   the page's own place, or, after a crop or a resize, that shape fitted
   into it. */
bool edit_view_rect(app_state_t *app, rubraview_pal_rect_t *out, double *out_scale, bool *changed) {
    rubraview_pal_rect_t rect;
    double scale = 1.0;
    if (!page_screen_rect(app, &rect, &scale) || scale <= 0.0) return false;
    int32_t cw = 0, ch = 0;
    edit_cur_size(app, &cw, &ch);
    if (changed) *changed = false;
    if (cw > 0 && ch > 0 && (cw != app->edit_steps.width || ch != app->edit_steps.height)) {
        double s = rect.width / cw < rect.height / ch ? rect.width / cw : rect.height / ch;
        rubraview_pal_rect_t fit = { rect.x + (rect.width - cw * s) * 0.5, rect.y + (rect.height - ch * s) * 0.5, cw * s, ch * s };
        if (changed) *changed = true;
        rect = fit;
        scale = s;
    }
    *out = rect;
    *out_scale = scale;
    return true;
}

static bool crop_point_to_image(app_state_t *app, double px, double py, int32_t *out_x, int32_t *out_y) {
    rubraview_pal_rect_t rect;
    double scale = 1.0;
    if (!edit_view_rect(app, &rect, &scale, NULL) || scale <= 0.0) return false;
    double ix = (px - rect.x) / scale;
    double iy = (py - rect.y) / scale;
    if (ix < 0.0) ix = 0.0;
    if (iy < 0.0) iy = 0.0;
    if (ix > (double)app->edit.image_width) ix = (double)app->edit.image_width;
    if (iy > (double)app->edit.image_height) iy = (double)app->edit.image_height;
    *out_x = (int32_t)(ix + 0.5);
    *out_y = (int32_t)(iy + 0.5);
    return true;
}

bool edit_panel_open(const app_state_t *app) {
    return app->panel.open && !app->panel_is_export && !app->panel_is_batch && !app->panel_is_print && !app->panel_is_convert;
}

/* The boxes' size from settings.ini (owner, 2026-09-29), each frame so the
   settings window's change shows at once; not under a grip being dragged. */
void boxes_apply_size(app_state_t *app) {
    if (!app->menubox.grip_active) {
        rubraview_box_set_size(&app->menubox,
                               (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("menubox_columns"))),
                               rubraview_settings_get(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("menubox_size")) / 100.0);
    }
    if (!app->toolbox.grip_active) {
        rubraview_box_set_size(&app->toolbox,
                               (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("toolbox_columns"))),
                               rubraview_settings_get(&app->settings, PROVEN_LIT("ui"), PROVEN_LIT("toolbox_size")) / 100.0);
    }
}

/* RV-062: after a lost graphics device, a film decoded (or tone mapped, RV-150) on the card is
   opened again, where it was, paused or not, on the same tracks: its
   decoder lived on the lost card and would hand over no more frames.
   Software-decoded films and sound are not bound to the device and are
   left alone (reopening music would be a gap you can hear). */
static void media_reopen(app_state_t *app);

void media_reopen_on_new_device(app_state_t *app) {
    if (!app->media || !app->media_info.has_video || !(app->media_info.hardware_decode || app->media_info.hdr_tone_mapped)) return;
    media_reopen(app);
    osd_say(app, TU8("the graphics device was reset: the film was opened again"));
}

/* RV-153: once a second while an HDR film is tone mapped, whether its screen is still the one it was opened
   for (moved to another screen, Windows' HDR switched, the SDR brightness moved); if not, it is opened again. */
static void hdr_screen_tick(app_state_t *app, double dt) {
    if (!app->media || !app->media_info.hdr_tone_mapped) { app->hdr_screen_check = 0.0; return; }
    if (app->hdr_output_refused && app->media_info.hdr_output) {   /* the window would not go out as HDR */
        media_reopen(app);
        return;
    }
    app->hdr_screen_check += dt;
    if (app->hdr_screen_check < 1.0) return;
    app->hdr_screen_check = 0.0;
    int32_t hdr_out = (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("video"), PROVEN_LIT("hdr_output")));
    if (hdr_out != 1) return;
    rubraview_hdr_display_t now;
    rubraview_pal_render_hdr_display(app->renderer, &now);
    const rubraview_hdr_display_t *was = &app->media_hdr_screen;
    bool changed = now.hdr != was->hdr || (now.hdr && (now.peak_nits != was->peak_nits || now.sdr_white_nits != was->sdr_white_nits));
    if (!changed) return;
    app->hdr_output_refused = false;   /* another screen: ask again */
    media_reopen(app);
}

/* A film opened again where it was, paused or not, on the same tracks. */
static void media_reopen(app_state_t *app) {
    double position = app->media_position;
    bool paused = app->media_paused;
    int32_t subtitle = app->tracks.current_subtitle;
    int32_t audio = app->tracks.current_audio;
    media_close(app);
    media_prepare(app);
    if (!app->media) return;
    if (audio >= 0 && audio != app->tracks.current_audio && (size_t)audio < app->tracks.count &&
        app->tracks.tracks[audio].kind == RUBRAVIEW_TRACK_AUDIO &&
        rubraview_pal_media_select_audio_track(app->media, app->tracks.tracks[audio].stream_index)) {
        app->tracks.current_audio = audio;
    }
    if (subtitle != app->tracks.current_subtitle) subtitle_select(app, subtitle);
    media_seek_to(app, position);
    app->media_position = position;
    if (paused != app->media_paused) media_toggle_pause(app);
}

/* ---- per-frame timers ---- */

static void tick_timers(app_state_t *app, double dt) {
    rubraview_osd_tick(&app->osd, dt);
    hdr_screen_tick(app, dt);   /* RV-153 */
    picker_hold_tick(app);   /* 1.22.0 */
    view_hold_tick(app);     /* 1.25.0 */
    /* Where the pointer is now, not where it last moved — the same reason
       as the boxes below: a pointer resting on the titlebar must keep it. */
    /* With Windows' own title bar (Settings › General › Frameless window off) ours stays away. */
    if (app->frameless && !app->chrome_hidden) rubraview_titlebar_pointer_moved(&app->titlebar, app->pointer_y);
    else app->titlebar.shown = false;
    if (app->frameless && !app->chrome_hidden && app->menubox.headless && app->menubox.state != RUBRAVIEW_BOX_COLLAPSED) {
        app->titlebar.shown = true;   /* the bar stays while its menu is open (owner, 2026-10-15) */
        app->titlebar.idle_seconds = 0.0;
    }
    if (app->frameless && !app->chrome_hidden &&
        (app->picker_open || app->tmenu_open || rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("titlebar_pinned")) > 0.5)) {
        app->titlebar.shown = true;   /* 1.20.0: pinned; 1.24.0: and always over the open-file screen */
        app->titlebar.idle_seconds = 0.0;
    }
    {   /* 1.24.0: the window's title follows the open-file screen's folder, and goes back when it closes */
        uint64_t key = app->picker_open ? 1469598103934665603ull : 0;
        for (size_t i = 0; app->picker_open && i < app->picker_dir.size; ++i) key = (key ^ (unsigned char)app->picker_dir.ptr[i]) * 1099511628211ull;
        if (key != app->title_key) { app->title_key = key; update_window_title(app); }
    }
    rubraview_titlebar_tick(&app->titlebar, dt);
    rubraview_transition_tick(&app->transition, dt);
    animation_tick(app, dt);

    if (app->notice_seconds > 0.0) {
        /* A frame that waited on a long act (a save, a decode) must not
           spend the notice that act just raised before it is drawn. */
        app->notice_seconds -= dt > 0.25 ? 0.25 : dt;
        if (app->notice_seconds < 0.0) app->notice_seconds = 0.0;
    }

    double grace = 0.5; /* §3.6.3 */
    /* Where the pointer *is*, not where it last moved: a pointer resting
       on a box must hold it open. Pointer events stop arriving the moment
       the mouse stops, and the box used to fold up under it half a second
       later. */
    rubraview_tile_metrics_t box_metrics =
        tile_metrics(rubraview_pal_window_dpi_scale(app->window));
    rubraview_box_pointer(&app->toolbox, &box_metrics, app->pointer_x, app->pointer_y);
    rubraview_box_pointer(&app->menubox, &box_metrics, app->pointer_x, app->pointer_y);
    char hint_scratch[320];
    {
        /* RV-116: resting on a box's anchor, pin, grip or seek bar says what they do */
        rubraview_tile_metrics_t base = tile_metrics(rubraview_pal_window_dpi_scale(app->window));
        bool free_pointer = app->pointer_inside && !app->box_drag && !app->box_sizing && !app->ctx_open && !app->slidepop_open;
        proven_u8str_view_t hint = free_pointer ? rubraview_box_hint(&app->menubox, &base, app->pointer_x, app->pointer_y) : PROVEN_LIT("");
        const char *keys_of = NULL;   /* the owner (2026-10-18): every tooltip with its keys; the menu box is gone (1.21.0) */
        if (hint.size == 0 && free_pointer) {
            hint = rubraview_box_hint(&app->toolbox, &base, app->pointer_x, app->pointer_y);
            keys_of = rubraview_rect_contains(rubraview_box_pin_rect(&app->toolbox, &base), app->pointer_x, app->pointer_y) ? "toggle_toolbox_pin" : "toggle_toolbox";
        }
        if (hint.size > 0 && keys_of) hint = hint_with_keys(app, cstr(keys_of), (const char *)hint.ptr, hint_scratch, sizeof(hint_scratch));
        if (hint.size == 0 && free_pointer && app->titlebar.shown) {   /* 1.25.0: every title bar button says what it does */
            int32_t bw = 0, bh = 0;
            rubraview_pal_window_get_size(app->window, &bw, &bh);
            static const struct { rubraview_titlebar_button_t b; const char *action, *words; } TIPS[] = {
                { RUBRAVIEW_TITLEBAR_MENU, "", N_("Menu") },
                { RUBRAVIEW_TITLEBAR_NAME, "open_picker", N_("The open-file screen, here") },
                { RUBRAVIEW_TITLEBAR_SETTINGS, "open_settings", N_("Settings") },
                { RUBRAVIEW_TITLEBAR_BARPIN, "toggle_titlebar_pin", N_("P: keep the title bar shown") },
                { RUBRAVIEW_TITLEBAR_HIDE, "hide_boxes", N_("H: hide the title bar and the Floating Widget") },
                { RUBRAVIEW_TITLEBAR_PIN, "toggle_always_on_top", N_("T: always on top") },
                { RUBRAVIEW_TITLEBAR_FULLSCREEN, "toggle_fullscreen", N_("F: full screen") },
                { RUBRAVIEW_TITLEBAR_MINIMIZE, "", N_("Minimise") },
                { RUBRAVIEW_TITLEBAR_MAXIMIZE, "", N_("Maximise, or back to a window") },
                { RUBRAVIEW_TITLEBAR_RESIZE, "", N_("R: drag to size the window") },
                { RUBRAVIEW_TITLEBAR_CLOSE, "quit", N_("Close Rubraview") },
            };
            rubraview_titlebar_button_t over = rubraview_titlebar_hit(&app->titlebar, app->pointer_x, app->pointer_y, (double)bw);
            for (size_t k = 0; k < sizeof(TIPS) / sizeof(TIPS[0]) && hint.size == 0; ++k) {
                if (TIPS[k].b != over) continue;
                hint = TIPS[k].action[0] ? hint_with_keys(app, cstr(TIPS[k].action), rubraview_tr(TIPS[k].words), hint_scratch, sizeof(hint_scratch))
                                         : cstr(rubraview_tr(TIPS[k].words));
            }
        }
        if (hint.size == 0 && app->pointer_inside && app->toolbox.state != RUBRAVIEW_BOX_COLLAPSED &&
            app->toolbox.state != RUBRAVIEW_BOX_DETACHED && !app->ctx_open && !app->slidepop_open) {
            /* the layout's icon says which it is, as words, when the pointer rests on it (owner, 2026-10-14) */
            int32_t tile = rubraview_box_tile_at(&app->toolbox, &base, app->pointer_x, app->pointer_y);
            if (tile >= 0 && tile < app->toolbox_tile_count && proven_u8str_view_eq(app->toolbox_tiles[tile].action, PROVEN_LIT("toggle_layout"))) {
                hint = hint_with_keys(app, PROVEN_LIT("toggle_layout"), layout_said(rubraview_layout_choice_of(app->layout_opts.mode, app->layout_opts.direction)),
                                      hint_scratch, sizeof(hint_scratch));
            }
            if (tile >= 0 && tile < app->toolbox_tile_count && proven_u8str_view_eq(app->toolbox_tiles[tile].action, PROVEN_LIT("fit_cycle"))) {
                /* the fit it is in, in words (owner, 2026-10-15), and its keys */
                hint = hint_with_keys(app, cstr(fit_action_of(app->fit_mode)), rubraview_fit_tile_hint(app->fit_mode), hint_scratch, sizeof(hint_scratch));
            }
            if (hint.size == 0 && tile >= 0 && tile < app->toolbox_tile_count) {
                const char *said = rubraview_action_hint(app->toolbox_tiles[tile].action);
                hint = hint_with_keys(app, app->toolbox_tiles[tile].action, said, hint_scratch, sizeof(hint_scratch));
            }
        }
        if (hint.size == 0 && app->pointer_inside && app->menubox.state != RUBRAVIEW_BOX_COLLAPSED && !app->ctx_open) {
            /* A menu item whose name does not explain itself says what it does (owner, 2026-10-15: Crisp, Grid). */
            int32_t tile = rubraview_box_tile_at(&app->menubox, &base, app->pointer_x, app->pointer_y);
            const rubraview_menu_item_t *item = tile >= 0 ? rubraview_menu_item_at(&app->menu, tile) : NULL;
            const char *said = item ? rubraview_action_hint(item->action) : NULL;
            if (item) hint = hint_with_keys(app, item->action, said, hint_scratch, sizeof(hint_scratch));
        }
        /* Kept in the app's own buffer, so it is the same text however it was made. */
        if (hint.size >= sizeof(app->hint_text)) hint.size = sizeof(app->hint_text) - 1;
        if (hint.size != app->hint.size || memcmp(hint.ptr, app->hint_text, hint.size) != 0) {
            memmove(app->hint_text, hint.ptr, hint.size);
            app->hint_text[hint.size] = '\0';
            app->hint = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->hint_text, .size = hint.size };
            app->hint_since = rubraview_pal_time_now_seconds();
        }
    }
    rubraview_box_tick(&app->toolbox, dt, grace);
    rubraview_box_tick(&app->menubox, dt, grace);
    /* However it was opened, the menu opens at its root, Recent as it is now. */
    if (rubraview_box_just_opened(&app->menubox, &app->menubox_was_open)) menu_rebuild(app);

    if (app->slideshow_running) {
        /* §3.2.5: the pointer goes away over the slides only — not while something is open to be
           pointed at (the slide show's settings, a list, a box, the settings window). */
        bool pointing = app->slidepop_open || app->ctx_open || app->settings_open || app->panel.open || app->listwin.open ||
                        app->sub_list_open || app->menubox.state != RUBRAVIEW_BOX_COLLAPSED ||
                        (app->toolbox.state != RUBRAVIEW_BOX_COLLAPSED && app->toolbox.state != RUBRAVIEW_BOX_DETACHED);
        if (pointing) {
            if (rubraview_cursor_hide_notify_motion(&app->cursor)) rubraview_pal_window_set_cursor_visible(app->window, true);
        } else if (rubraview_cursor_hide_tick(&app->cursor, dt)) {
            rubraview_pal_window_set_cursor_visible(app->window, false);
        }
        /* What the slide show steps through is what is on screen (owner, 2026-10-14: it skipped files):
           the spreads as they are now, from the one shown, timed from when it is drawn. */
        slides_follow_screen(app);
        rubraview_slideshow_event_t event = slide_ready(app) ? rubraview_slideshow_tick(&app->slideshow, app->slides, dt)
                                                             : RUBRAVIEW_SLIDESHOW_NONE;
        if (event == RUBRAVIEW_SLIDESHOW_ADVANCED) {
            app->subs_leave_unasked = true;
            go_to_spread(app, app->slideshow.current_index);
            app->subs_leave_unasked = false;
        } else if (event == RUBRAVIEW_SLIDESHOW_ENDED) {
            app->slideshow_running = false;
            rubraview_pal_window_set_cursor_visible(app->window, true);
        }
    }
}

/* ---- startup ---- */

/* Settings › General › On startup (owner, 2026-09-30), when no file was
   given: nothing (the picker's PC page, as before), the last thing read,
   or the picker in the folder it was in. */
static void startup_without_file(app_state_t *app, bool no_file) {
    if (!no_file || app->history.count == 0) return;
    int mode = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("startup")));
    if (mode == 0) return;
    const rubraview_history_entry_t *last = &app->history.entries[0];
    for (size_t i = 1; i < app->history.count; ++i) {
        if (app->history.entries[i].timestamp > last->timestamp) last = &app->history.entries[i];
    }
    rubraview_fs_entry_t entry;
    if (!rubraview_pal_fs_stat(app->arena, last->path, &entry)) return;   /* gone since: start as blank does */
    if (mode == 1) {
        open_path(app, last->path);
    } else {
        proven_u8str_view_t folder = entry.is_directory ? last->path : rubraview_path_dirname(last->path);
        picker_open(app);
        picker_navigate(app, folder);
        app->picker.focus = 0;
    }
}

void load_keymap(app_state_t *app) {
    /* §3.7.5 / D-14: keymap.ini lives where settings.ini does — beside the
       program in portable mode, in AppData otherwise — and replaces the
       built-in bindings wholesale. Before the settings window could write
       one it was read only from the working folder, so a file left there
       still counts when AppData has none. */
    /* Settings › Keys › Use keymap.ini: off, the built-in keys, whatever the file says. */
    bool use_file = rubraview_settings_get(&app->settings, PROVEN_LIT("keys"), PROVEN_LIT("use_keymap_file")) > 0.5;
    proven_u8str_view_t text = use_file ? rubraview_pal_fs_read_file(app->arena, app->keymap_path, KEYMAP_MAX_BYTES) : PROVEN_LIT("");
    if (use_file && text.size == 0 && app->config_mode != RUBRAVIEW_CONFIG_PORTABLE) {
        text = rubraview_pal_fs_read_file(app->arena, PROVEN_LIT("keymap.ini"), KEYMAP_MAX_BYTES);
    }
    if (text.size == 0) text = cstr(rubraview_default_keymap());
    app->keymap = rubraview_keymap_parse(app->arena, text);
}

void build_slides(app_state_t *app) {
    if (app->layout.count == 0) return;
    /* the delay and the end chosen stay; a new list is not a new slide show */
    double interval = app->slideshow.interval_seconds > 0.0 ? app->slideshow.interval_seconds : 3.0;
    rubraview_loop_policy_t loop = app->slideshow.item_count > 0 ? app->slideshow.loop : RUBRAVIEW_LOOP_ALL;
    bool paused = app->slideshow.item_count == 0 || app->slideshow.paused;
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, app->layout.count, sizeof(rubraview_slideshow_item_t));
    if (!proven_is_ok(res.err)) return;

    app->slides = (rubraview_slideshow_item_t*)(void*)res.value.ptr;
    for (size_t i = 0; i < app->layout.count; ++i) {
        /* Every slide starts as a still. A page that turns out to hold
           an animation upgrades its own slide when it is opened, since
           only then is the cycle length known; video joins with M5
           (RV-061). */
        app->slides[i] = (rubraview_slideshow_item_t){ .kind = RUBRAVIEW_MEDIA_STILL, .duration_seconds = 0.0 };
    }
    app->slideshow = rubraview_slideshow_create(app->layout.count, interval, loop);
    app->slideshow.current_index = app->spread_index < app->layout.count ? app->spread_index : 0;
    if (paused || !app->slideshow_running) rubraview_slideshow_pause(&app->slideshow);
}

/* The spreads were made again (a wide page found, the layout changed, a page
   left out) or the reader turned a page while it ran: the slide show goes on
   from the spread on screen, its time from nothing. Before, its own count and
   place stayed as they were when the file was opened, and it went on from
   there — past spreads, or to the wrong ones. */
static void slides_follow_screen(app_state_t *app) {
    if (app->slideshow.item_count != app->layout.count) build_slides(app);
    if (app->slideshow.current_index != app->spread_index && app->spread_index < app->layout.count) {
        app->slideshow.current_index = app->spread_index;
        app->slideshow.elapsed_in_item = 0.0;
    }
}

/* The spread on screen has been drawn — its pages read, or found unreadable,
   or a film or a song: the delay counts from then, so a large picture still
   being read is not passed over before it was seen. */
static bool slide_ready(const app_state_t *app) {
    if (app->spread_index >= app->layout.count) return true;
    int32_t pages[2] = { app->layout.spreads[app->spread_index].left_index, app->layout.spreads[app->spread_index].right_index };
    for (int i = 0; i < 2; ++i) {
        if (pages[i] < 0 || (size_t)pages[i] >= page_count(app)) continue;
        const app_page_t *page = &app->pages[pages[i]];
        if (!page->loaded && !page->failed && !is_media_path(app->source.pages[pages[i]].path) &&
            !is_text_path(app->source.pages[pages[i]].path) && !is_text_path(app->source.pages[pages[i]].name)) return false;
    }
    return true;
}

/* The ring names pages it wants ready before the reader reaches them.
   Decoding them here would stall the flip that asked, and decoding them
   on a worker is unsound — WIC wants COM on the calling thread, the
   Direct2D context is single-threaded, and the arena is not locked — so
   they are queued and the main loop decodes one per pass. */
static void precache_decode(void *ctx, size_t page_index) {
    app_state_t *app = (app_state_t*)ctx;
    for (size_t i = 0; i < app->pending_decode_count; ++i) {
        if (app->pending_decode[i] == page_index) return;
    }
    if (app->pending_decode_count < PENDING_DECODE_MAX) {
        app->pending_decode[app->pending_decode_count++] = page_index;
    }
}

/* Decodes the oldest queued page. Returns whether there was one. */
static bool drain_one_pending_decode(app_state_t *app) {
    if (app->pending_decode_count == 0) return false;
    size_t index = app->pending_decode[0];
    app->pending_decode_count--;
    memmove(app->pending_decode, app->pending_decode + 1,
            app->pending_decode_count * sizeof(app->pending_decode[0]));
    ensure_page_loaded_how(app, (int32_t)index, false);
    return true;
}

static void release_evicted(app_state_t *app, const uint64_t *evicted, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        size_t index = (size_t)evicted[i];
        if (index >= page_count(app)) continue;
        if ((int32_t)index == app->media_page) continue; /* the playing video, not a cached picture */
        if (!app->pages[index].texture) continue;
        /* §7.4: the budget decided this page has to go. */
        rubraview_pal_texture_destroy(app->pages[index].texture);
        page_forget(&app->pages[index]);
    }
}

/* Keeps the ring in step with the page on screen, and lets the budget
   reclaim whatever fell out of it. */
void update_precache(app_state_t *app) {
    if (page_count(app) == 0) return;

    int32_t current = current_page_index(app);
    if (current < 0) return;

    rubraview_precache_set_interval(&app->precache,
                                    app->slideshow_running ? app->slideshow.interval_seconds : 0.0,
                                    0.5);

    uint64_t evicted[32];
    size_t evicted_count = 0;
    rubraview_precache_update(&app->precache, (size_t)current, precache_decode, app,
                              evicted, sizeof(evicted) / sizeof(evicted[0]), &evicted_count);
    /* §7.4: give back the textures the budget decided it could not keep. */
    release_evicted(app, evicted, evicted_count);
}

#ifdef _WIN32


/* ---- telling the reader why nothing appeared ---- */

/*
 * A viewer that shows nothing is the least useful failure there is, and
 * it cannot be diagnosed from another machine. So when the graphics
 * device will not start, the reason is put in front of the reader and
 * written to a file next to the program — with the operating system's
 * own error code, which is the part that identifies the cause.
 */
static void report_startup_failure(void) {
    proven_u8str_view_t where = rubraview_pal_render_last_error();
    uint32_t hr = rubraview_pal_render_last_hresult();

    char message[512];
    int n = snprintf(message, sizeof(message),
        "Rubraview " RUBRAVIEW_VERSION_STRING " could not start its graphics.\r\n\r\n"
        "It failed while %.*s.\r\n"
        "Windows reported error 0x%08lX.\r\n\r\n"
        "This has been written to rubraview-diag.txt next to the program.",
        (int)where.size, where.size > 0 ? (const char *)where.ptr : "starting up",
        (unsigned long)hr);
    if (n <= 0) return;

    /* The file first: a message box can be dismissed before it is read,
       and a file can be pasted into a report. */
    HANDLE file = CreateFileW(L"rubraview-diag.txt", GENERIC_WRITE, FILE_SHARE_READ,
                              NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, message, (DWORD)n, &written, NULL);
        CloseHandle(file);
    }

    WCHAR wide[1024];
    if (MultiByteToWideChar(CP_UTF8, 0, message, -1, wide, 1024) > 0) {
        MessageBoxW(NULL, wide, L"Rubraview", MB_OK | MB_ICONERROR);
    }
}

/* §3.22 [video] decoder (D-9): the backend a file is offered to first.
   The other one still gets its turn when this one cannot open it. */
static rubraview_media_backend_t preferred_backend(proven_arena_t *arena) {
    proven_u8str_view_t text = rubraview_pal_fs_read_file(arena, PROVEN_LIT("settings.ini"), 256u * 1024u);
    if (text.size == 0) return RUBRAVIEW_BACKEND_MEDIA_FOUNDATION;
    rubraview_ini_doc_t doc = rubraview_ini_parse(arena, text);
    const proven_u8str_view_t *decoder = rubraview_ini_get(&doc, PROVEN_LIT("video"), PROVEN_LIT("decoder"));
    return (decoder && proven_u8str_view_eq(*decoder, PROVEN_LIT("ffmpeg")))
        ? RUBRAVIEW_BACKEND_FFMPEG : RUBRAVIEW_BACKEND_MEDIA_FOUNDATION;
}

/* `--probe-media`: open a file with each backend in turn and say what
   happened — which one took it, what it thinks the file is, and whether
   frames really come out. There is no window, so it answers over a
   remote shell: the media PAL hands over CPU pixels and needs no
   renderer. mfprobe asks Windows what it *could* decode; this asks
   rubraview what it actually does with one file. */
static int probe_media_file(proven_arena_t *arena, proven_u8str_view_t path) {
    char line[512];
    /* Where the viewer itself would look (RV-122): the imported copy in the
       settings folder, and the folder settings.ini names. */
    proven_u8str_view_t chosen = PROVEN_LIT(""), exe = rubraview_pal_process_executable(arena);
    proven_u8str_view_t beside = exe.size > 0 ? rubraview_path_dirname(exe) : PROVEN_LIT(".");
    char appdata[1024];
    DWORD got = GetEnvironmentVariableA("APPDATA", appdata, (DWORD)sizeof(appdata));
    rubraview_config_mode_t mode = rubraview_config_mode_for(rubraview_pal_fs_exists(rubraview_path_join(arena, beside, PROVEN_LIT("settings.ini"))));
    proven_u8str_view_t settings = rubraview_config_path(arena, mode, beside,
                                             got > 0 && got < sizeof(appdata) ? (proven_u8str_view_t){ .ptr = (const proven_byte_t *)appdata, .size = got } : PROVEN_LIT("."),
                                             PROVEN_LIT("settings.ini"));
    proven_u8str_view_t ini = rubraview_pal_fs_read_file(arena, PROVEN_LIT("settings.ini"), 256u * 1024u);
    if (ini.size > 0) {
        rubraview_ini_doc_t doc = rubraview_ini_parse(arena, ini);
        const proven_u8str_view_t *dir = rubraview_ini_get(&doc, PROVEN_LIT("video"), PROVEN_LIT("ffmpeg_dir"));
        if (dir) chosen = rubraview_pal_fs_expand_env(arena, rubraview_path_typed_folder(*dir));
    }
    rubraview_pal_ffmpeg_set_folders(rubraview_path_join(arena, rubraview_path_dirname(settings), PROVEN_LIT("ffmpeg")), chosen);
    bool ffmpeg_here = rubraview_pal_media_backend_available(RUBRAVIEW_BACKEND_FFMPEG);
    console_line(ffmpeg_here
        ? "FFmpeg: its DLLs are here, at a version these headers know"
        : rubraview_pal_ffmpeg_refused() == RUBRAVIEW_FFMPEG_OTHER_VERSION
            ? "FFmpeg: DLLs are beside the program but not 9.0 (avcodec-63): not loaded (Media Foundation alone)"
        : rubraview_pal_ffmpeg_refused() == RUBRAVIEW_FFMPEG_NOT_WHOLE
            ? "FFmpeg: avcodec-63.dll is beside the program but the set could not be loaded — avformat-63, avutil-61, swscale-10 and swresample-7 are needed with it (Media Foundation alone)"
            : "FFmpeg: no usable DLLs beside the program (Media Foundation alone)");
    if (ffmpeg_here) {
        console_line(rubraview_pal_ffmpeg_jxl_state() == 2
            ? "FFmpeg: it has a JPEG XL decoder (libjxl) — .jxl pictures are read through it"
            : "FFmpeg: it has no JPEG XL decoder — .jxl pictures need a codec in Windows");
    }

    rubraview_media_backend_t preferred = preferred_backend(arena);
    console_line(preferred == RUBRAVIEW_BACKEND_FFMPEG
        ? "settings.ini [video] decoder = ffmpeg — FFmpeg is tried first"
        : "settings.ini [video] decoder = windows (or unset) — Media Foundation is tried first");

    rubraview_media_backend_t order[2];
    size_t count = rubraview_media_backend_order(preferred, ffmpeg_here, order);
    for (size_t i = 0; i < count; ++i) {
        const char *name = order[i] == RUBRAVIEW_BACKEND_FFMPEG ? "FFmpeg" : "Media Foundation";
        rubraview_media_open_result_t opened = rubraview_pal_media_open(path, order[i], NULL);
        if (!opened.media) {
            proven_u8str_view_t why = rubraview_media_failure_text_with(opened.failure, rubraview_pal_ffmpeg_refused());
            snprintf(line, sizeof(line), "%s: cannot open — %.*s", name, (int)why.size, why.ptr);
            console_line(line);
            continue;
        }

        snprintf(line, sizeof(line), "%s: opened — %dx%d, %.3f s, %.2f fps, picture %s, sound %s%s",
                 name, opened.info.width, opened.info.height, opened.info.duration_seconds,
                 opened.info.frame_rate, opened.info.has_video ? "yes" : "no",
                 opened.info.has_audio ? "yes" : "no",
                 opened.info.has_audio
                     ? (opened.info.audio_output ? " (going to a device)" : " (no audio device here)") : "");
        console_line(line);

        /* §3.16.2: what the container holds, as the menu would show it. */
        rubraview_track_set_t set = {0};
        if (rubraview_pal_media_tracks(opened.media, &set) && set.count > 0) {
            for (size_t t = 0; t < set.count; ++t) {
                char label[160];
                proven_u8str_view_t text = rubraview_track_label(label, sizeof(label), &set, (int32_t)t);
                bool current = (int32_t)t == set.current_video || (int32_t)t == set.current_audio;
                const char *kind = set.tracks[t].kind == RUBRAVIEW_TRACK_AUDIO ? "sound"
                                 : set.tracks[t].kind == RUBRAVIEW_TRACK_SUBTITLE ? "subtitle"
                                 : "picture";
                snprintf(line, sizeof(line), "%s: %-8s %s %.*s", name, kind,
                         current ? "*" : " ", (int)text.size, text.ptr);
                console_line(line);
            }
        } else {
            snprintf(line, sizeof(line), "%s: the track list is empty", name);
            console_line(line);
        }

        /* Opening is not playing: take some frames and see. */
        int frames = 0;
        double first = -1.0, last = -1.0;
        for (int spin = 0; spin < 600 && frames < 24; ++spin) {
            rubraview_video_frame_t frame;
            if (rubraview_pal_media_peek_frame(opened.media, &frame)) {
                if (first < 0.0) first = frame.pts;
                last = frame.pts;
                frames++;
                rubraview_pal_media_pop_frame(opened.media);
                continue;
            }
            if (rubraview_pal_media_finished(opened.media)) break;
            rubraview_pal_time_sleep_ms(10);
        }
        if (opened.info.has_video) {
            snprintf(line, sizeof(line), "%s: %d picture(s) decoded, %.3f s to %.3f s", name, frames, first, last);
        } else {
            snprintf(line, sizeof(line), "%s: sound only — nothing to decode into pictures", name);
        }
        console_line(line);
        rubraview_pal_media_close(opened.media);
    }

    /* §3.16.1: the same search the viewer does, said out loud — which
       of discovery, reading and parsing worked, and what came out. */
    proven_u8str_view_t sub_name = PROVEN_LIT("");
    const char *why = NULL;
    rubraview_subtitle_track_t track = subtitle_find(arena, path, &sub_name, &why);
    if (track.count == 0) {
        snprintf(line, sizeof(line), "Subtitles: none — %s", why ? why : "not looked for");
    } else {
        snprintf(line, sizeof(line), "Subtitles: %.*s — %zu line(s), the first %.3f s to %.3f s",
                 (int)sub_name.size, sub_name.ptr, track.count,
                 track.cues[0].start_seconds, track.cues[0].end_seconds);
    }
    console_line(line);
    return 0;
}

/* `--probe-gpu`: what graphics card this machine has, whether Media
   Foundation will take it for decoding, and — the question that decides
   RV-062's shape — whether a decoded frame then comes back as a texture
   or as system memory. Written before building the zero-copy path so
   that "can this be tried here at all" is measured, not assumed. */
/* D-38: the card's resize against the CPU's on the same picture: how many
   bytes differ, by how much, and how long each took. With `always` the
   software rasteriser stands in when there is no card, so this measures
   correctness anywhere and speed only on a machine with a card. */
static void probe_gpu_resize(void) {
    char line[256];
    bool on = rubraview_pal_gpu_resample_start(RUBRAVIEW_GPU_RESIZE_ON);
    snprintf(line, sizeof(line), "gpu resize with gpu_resize = on: %s%s", rubraview_pal_gpu_resample_describe(),
             on ? "" : " (not used)");
    console_line(line);
    bool started = rubraview_pal_gpu_resample_start(RUBRAVIEW_GPU_RESIZE_ALWAYS);
    snprintf(line, sizeof(line), "gpu resize with gpu_resize = always: %s", rubraview_pal_gpu_resample_describe());
    console_line(line);
    if (!started) return;
    size_t cap = (size_t)96 << 20;
    void *memory = malloc(cap);
    if (!memory) return;
    static const struct { int32_t sw, sh, dw, dh; } SIZES[] = { {1600, 1200, 2400, 1800}, {1600, 1200, 1000, 750}, {1600, 1200, 320, 240} };
    static const rubraview_resample_filter_t FILTERS[2] = { RUBRAVIEW_FILTER_BICUBIC, RUBRAVIEW_FILTER_LANCZOS3 };
    for (size_t k = 0; k < sizeof(SIZES) / sizeof(SIZES[0]); ++k) {
        for (int f = 0; f < 2; ++f) {
            proven_arena_t a = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)memory, .size = cap });
            rubraview_pixbuf_t src = rubraview_pixbuf_create(&a, SIZES[k].sw, SIZES[k].sh, RUBRAVIEW_PIXFMT_RGBA8);
            rubraview_pixbuf_t cpu = rubraview_pixbuf_create(&a, SIZES[k].dw, SIZES[k].dh, RUBRAVIEW_PIXFMT_RGBA8);
            rubraview_pixbuf_t gpu = rubraview_pixbuf_create(&a, SIZES[k].dw, SIZES[k].dh, RUBRAVIEW_PIXFMT_RGBA8);
            if (!rubraview_pixbuf_is_valid(&src) || !rubraview_pixbuf_is_valid(&cpu) || !rubraview_pixbuf_is_valid(&gpu)) break;
            unsigned seed = 11;
            for (int32_t y = 0; y < src.height; ++y)
                for (int32_t x = 0; x < src.width; ++x) {
                    uint8_t *p = src.pixels + (ptrdiff_t)y * src.stride + x * 4;
                    seed = seed * 1103515245u + 12345u;
                    int noise = (int)((seed >> 16) & 15) - 8;
                    bool edge = ((x / 97) + (y / 61)) % 2 == 0;
                    int v[3] = { x * 255 / src.width, y * 255 / src.height, 128 + (int)(100.0 * sin(x * 0.02 + y * 0.01)) };
                    for (int c = 0; c < 3; ++c) { int t = v[c] + noise + (edge ? 40 : 0); p[c] = (uint8_t)(t < 0 ? 0 : t > 255 ? 255 : t); }
                    p[3] = 255;
                }
            double t0 = rubraview_pal_time_now_seconds();
            rubraview_resample_band(&src, &cpu, FILTERS[f], 0, cpu.height);
            double t1 = rubraview_pal_time_now_seconds();
            bool ok = rubraview_resample_try_accel(&src, &gpu, FILTERS[f]);
            double t2 = rubraview_pal_time_now_seconds();
            size_t n = (size_t)cpu.stride * (size_t)cpu.height, changed = 0;
            int worst = 0;
            for (size_t i = 0; ok && i < n; ++i) {
                int d = abs((int)cpu.pixels[i] - (int)gpu.pixels[i]);
                if (d) changed++;
                if (d > worst) worst = d;
            }
            snprintf(line, sizeof(line), "gpu resize %s %dx%d -> %dx%d: %s; cpu %.3f s, card %.3f s; max diff %d, %.4f%% of bytes differ",
                     f ? "lanczos3" : "bicubic", SIZES[k].sw, SIZES[k].sh, SIZES[k].dw, SIZES[k].dh,
                     ok ? "done on the card" : "the card refused (CPU would do it)", t1 - t0, t2 - t1, worst,
                     n ? 100.0 * (double)changed / (double)n : 0.0);
            console_line(line);
        }
    }
    free(memory);
    rubraview_pal_gpu_resample_stop();
}

static int probe_gpu_file(proven_arena_t *arena, proven_u8str_view_t path) {
    (void)arena;
    char line[512];

    typedef HRESULT (WINAPI *create_dxgi_manager_fn)(UINT*, IMFDXGIDeviceManager**);
    HMODULE plat = LoadLibraryExW(L"mfplat.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE rw = LoadLibraryExW(L"mfreadwrite.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!plat || !rw) { console_line("Media Foundation is not installed here"); return 2; }
    create_dxgi_manager_fn create_manager =
        (create_dxgi_manager_fn)(void*)GetProcAddress(plat, "MFCreateDXGIDeviceManager");
    HRESULT (WINAPI *mf_startup)(ULONG, DWORD) =
        (HRESULT (WINAPI *)(ULONG, DWORD))(void*)GetProcAddress(plat, "MFStartup");
    HRESULT (WINAPI *mf_shutdown)(void) =
        (HRESULT (WINAPI *)(void))(void*)GetProcAddress(plat, "MFShutdown");
    HRESULT (WINAPI *mf_create_attributes)(IMFAttributes**, UINT32) =
        (HRESULT (WINAPI *)(IMFAttributes**, UINT32))(void*)GetProcAddress(plat, "MFCreateAttributes");
    HRESULT (WINAPI *mf_create_reader)(LPCWSTR, IMFAttributes*, IMFSourceReader**) =
        (HRESULT (WINAPI *)(LPCWSTR, IMFAttributes*, IMFSourceReader**))
            (void*)GetProcAddress(rw, "MFCreateSourceReaderFromURL");
    if (!create_manager || !mf_startup || !mf_shutdown || !mf_create_attributes || !mf_create_reader) {
        console_line("Media Foundation is here but not the calls this needs");
        return 2;
    }

    /* 1. A device. Hardware if there is one, otherwise Windows' own
          software rasteriser (WARP) — which is what a machine with no
          graphics card falls back to. */
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    D3D_FEATURE_LEVEL level = (D3D_FEATURE_LEVEL)0;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    const char *kind = "hardware";
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, NULL, 0,
                                   D3D11_SDK_VERSION, &device, &level, &context);
    if (FAILED(hr)) {
        kind = "WARP (software)";
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, flags, NULL, 0,
                               D3D11_SDK_VERSION, &device, &level, &context);
    }
    if (FAILED(hr) || !device) {
        console_line("no Direct3D 11 device at all, with or without video support");
        return 2;
    }
    snprintf(line, sizeof(line), "Device: %s, feature level %u.%u",
             kind, (unsigned)(level >> 12) & 0xF, (unsigned)(level >> 8) & 0xF);
    console_line(line);

    IDXGIDevice *dxgi = NULL;
    if (SUCCEEDED(ID3D11Device_QueryInterface(device, &IID_IDXGIDevice, (void**)&dxgi)) && dxgi) {
        IDXGIAdapter *adapter = NULL;
        if (SUCCEEDED(IDXGIDevice_GetAdapter(dxgi, &adapter)) && adapter) {
            DXGI_ADAPTER_DESC desc = {0};
            if (SUCCEEDED(IDXGIAdapter_GetDesc(adapter, &desc))) {
                char name[256] = {0};
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, (int)sizeof(name), NULL, NULL);
                snprintf(line, sizeof(line), "Adapter: %s (%u MB of its own memory)",
                         name, (unsigned)(desc.DedicatedVideoMemory / (1024u * 1024u)));
                console_line(line);
            }
            IDXGIAdapter_Release(adapter);
        }
        IDXGIDevice_Release(dxgi);
    }

    /* 2. Does it offer decoders at all? */
    ID3D11VideoDevice *video = NULL;
    UINT profile_count = 0;
    if (SUCCEEDED(ID3D11Device_QueryInterface(device, &IID_ID3D11VideoDevice, (void**)&video)) && video) {
        UINT profiles = ID3D11VideoDevice_GetVideoDecoderProfileCount(video);
        profile_count = profiles;
        snprintf(line, sizeof(line), "Decoder profiles the card offers: %u", profiles);
        console_line(line);
        ID3D11VideoDevice_Release(video);
    } else {
        console_line("Decoder profiles the card offers: none (no video device)");
    }

    /* 3. Hand the device to Media Foundation and read one frame. */
    if (FAILED(mf_startup(((ULONG)0x00020070), 0))) {
        console_line("Media Foundation would not start");
        return 2;
    }
    int code = 0;
    /* mingw-w64 declares the interface but ships no IID for it. */
    static const GUID iid_multithread =
        { 0x9B7E4E00, 0x342C, 0x4106, { 0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0 } };
    ID3D11Multithread *mt = NULL;
    if (SUCCEEDED(ID3D11DeviceContext_QueryInterface(context, &iid_multithread, (void**)&mt)) && mt) {
        /* Media Foundation uses the device from its own threads. */
        ID3D11Multithread_SetMultithreadProtected(mt, TRUE);
        ID3D11Multithread_Release(mt);
    }

    IMFDXGIDeviceManager *manager = NULL;
    UINT token = 0;
    if (FAILED(create_manager(&token, &manager)) || !manager ||
        FAILED(IMFDXGIDeviceManager_ResetDevice(manager, (IUnknown*)device, token))) {
        console_line("Media Foundation refused the device manager");
        code = 2;
    } else {
        WCHAR wide[MAX_PATH * 2];
        if (MultiByteToWideChar(CP_UTF8, 0, (const char *)path.ptr, (int)path.size, wide,
                                (int)(sizeof(wide) / sizeof(wide[0])) - 1) <= 0) {
            console_line("that path cannot be read");
            code = 2;
        } else {
            wide[MultiByteToWideChar(CP_UTF8, 0, (LPCCH)path.ptr, (int)path.size, NULL, 0)] = L'\0';
            IMFAttributes *attrs = NULL;
            IMFSourceReader *reader = NULL;
            if (SUCCEEDED(mf_create_attributes(&attrs, 3)) && attrs) {
                IMFAttributes_SetUnknown(attrs, &MF_SOURCE_READER_D3D_MANAGER, (IUnknown*)manager);
                IMFAttributes_SetUINT32(attrs, &MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
                IMFAttributes_SetUINT32(attrs, &MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
                hr = mf_create_reader(wide, attrs, &reader);
                IMFAttributes_Release(attrs);
            } else {
                hr = E_FAIL;
            }
            if (FAILED(hr) || !reader) {
                console_line("the reader would not open the file with the device attached");
                code = 2;
            } else {
                console_line("The reader took the device.");
                /* The first read often answers with a format change and
                   no picture at all; ask a few times before believing
                   that nothing is coming. */
                DWORD stream = 0, sample_flags = 0;
                LONGLONG timestamp = 0;
                IMFSample *sample = NULL;
                DWORD last_flags = 0;
                for (int attempt = 0; attempt < 40 && !sample; ++attempt) {
                    hr = IMFSourceReader_ReadSample(reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                                    &stream, &sample_flags, &timestamp, &sample);
                    last_flags = sample_flags;
                    if (FAILED(hr) || (sample_flags & MF_SOURCE_READERF_ENDOFSTREAM) ||
                        (sample_flags & MF_SOURCE_READERF_ERROR)) break;
                }
                if (FAILED(hr) || !sample) {
                    snprintf(line, sizeof(line),
                             "but no picture came out of it (hr=0x%08lX, last flags=0x%lX)",
                             (unsigned long)hr, (unsigned long)last_flags);
                    console_line(line);
                    code = 2;
                } else {
                    IMFMediaBuffer *buffer = NULL;
                    bool on_the_card = false;
                    if (SUCCEEDED(IMFSample_GetBufferByIndex(sample, 0, &buffer)) && buffer) {
                        IMFDXGIBuffer *dxgi_buffer = NULL;
                        if (SUCCEEDED(IMFMediaBuffer_QueryInterface(buffer, &IID_IMFDXGIBuffer,
                                                                    (void**)&dxgi_buffer)) && dxgi_buffer) {
                            on_the_card = true;
                            IMFDXGIBuffer_Release(dxgi_buffer);
                        }
                        IMFMediaBuffer_Release(buffer);
                    }
                    console_line(on_the_card
                        ? "The frame came back as a texture on the card — the zero-copy path can be tried here."
                        : "The frame came back as system memory — this machine cannot exercise the zero-copy path.");
                    IMFSample_Release(sample);
                }
                IMFSourceReader_Release(reader);
            }
        }
    }
    if (manager) IMFDXGIDeviceManager_Release(manager);
    mf_shutdown();

    /* 4. The viewer's own path (RV-062): the media PAL handed this device
          the way [video] hardware_decode = on and = always hand it over.
          Whether the film decodes on the card, whether pictures come, and
          whether they arrive as textures (zero copy) or as pixels. */
    static const char *const MODE_NAMES[] = { "off", "on", "always" };
    for (int32_t mode = 0; mode <= 2; ++mode) {
        rubraview_media_gpu_t gpu = { .device = device, .decoder_profiles = profile_count, .mode = mode };
        rubraview_media_open_result_t opened = rubraview_pal_media_open(path, RUBRAVIEW_BACKEND_MEDIA_FOUNDATION, &gpu);
        if (!opened.media) {
            /* Why, so "cannot open" and "the card broke it" read differently. */
            proven_u8str_view_t why = rubraview_media_failure_text(opened.failure);
            snprintf(line, sizeof(line), "hardware_decode = %s: the film did not open - %.*s",
                     MODE_NAMES[mode], (int)why.size, why.ptr);
            console_line(line);
            code = 2;
            continue;
        }
        /* As fast as it decodes, for four seconds: pictures per second and
           the CPU the whole program spent on them, so off and on compare. */
        FILETIME created, exited, kernel0, user0, kernel1, user1;
        GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel0, &user0);
        int pictures = 0, on_card = 0;
        double start = rubraview_pal_time_now_seconds(), elapsed = 0.0;
        while ((elapsed = rubraview_pal_time_now_seconds() - start) < 4.0) {
            rubraview_video_frame_t frame;
            if (rubraview_pal_media_peek_frame(opened.media, &frame)) {
                pictures++;
                if (frame.gpu_texture) on_card++;
                rubraview_pal_media_pop_frame(opened.media);
            } else if (rubraview_pal_media_finished(opened.media)) {
                break;
            } else {
                rubraview_pal_time_sleep_ms(1);
            }
        }
        GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel1, &user1);
        #define RV_FT(ft) ((double)(((uint64_t)(ft).dwHighDateTime << 32) | (ft).dwLowDateTime) / 1e7)
        double cpu = RV_FT(kernel1) - RV_FT(kernel0) + RV_FT(user1) - RV_FT(user0);
        #undef RV_FT
        uint32_t cc = opened.info.video_fourcc;
        char codec[5] = { (char)(cc & 0xFF), (char)((cc >> 8) & 0xFF), (char)((cc >> 16) & 0xFF), (char)(cc >> 24), 0 };
        for (int k = 0; k < 4; ++k) if (codec[k] < 32 || codec[k] > 126) codec[k] = '?';
        snprintf(line, sizeof(line),
                 "hardware_decode = %s: %s %dx%d, decoded %s; %d pictures in %.1f s (%.0f a second), "
                 "%d of them on the card; CPU %.2f s (%.0f%% of one core)",
                 MODE_NAMES[mode], codec, opened.info.width, opened.info.height,
                 opened.info.hardware_decode ? "on the card" : "in software",
                 pictures, elapsed, elapsed > 0.0 ? pictures / elapsed : 0.0, on_card,
                 cpu, elapsed > 0.0 ? 100.0 * cpu / elapsed : 0.0);
        console_line(line);
        if (pictures == 0 && opened.info.has_video) code = 2;
        rubraview_pal_media_close(opened.media);
    }

    ID3D11DeviceContext_Release(context);
    ID3D11Device_Release(device);

    /* 5. The viewer's own renderer (0.0.7 on the owner's card: the probe
          played the film, the viewer showed a black window). A hidden
          window and the real renderer; the decoder's device from it, the
          film opened with it, frames copied into a film texture and drawn
          once, and what was drawn read back. */
    HWND hidden = CreateWindowExW(0, L"STATIC", L"rubraview probe", WS_POPUP, 0, 0, 320, 240, NULL, NULL,
                                  GetModuleHandleW(NULL), NULL);
    rubraview_renderer_t *renderer = hidden ? rubraview_pal_render_create(arena, hidden, 320, 240) : NULL;
    if (!renderer) {
        console_line("viewer path: the renderer would not start here");
        if (hidden) DestroyWindow(hidden);
        return 2;
    }
    uint32_t viewer_profiles = 0;
    void *decode_device = rubraview_pal_render_video_device(renderer, &viewer_profiles);
    snprintf(line, sizeof(line), "viewer path: the decoder's own device %s, %u decoder profiles",
             decode_device ? "made" : "NOT made", (unsigned)viewer_profiles);
    if (!decode_device) {
        console_line(line);
        snprintf(line, sizeof(line), "viewer path: the last Direct3D answer was 0x%08X",
                 (unsigned)rubraview_pal_render_last_hresult());
    }
    console_line(line);
  for (int32_t vmode = 1; vmode <= 2; ++vmode) {
    rubraview_media_gpu_t vgpu = { .device = decode_device, .decoder_profiles = viewer_profiles, .mode = vmode };
    double opened_in = rubraview_pal_time_now_seconds();
    rubraview_media_open_result_t vopen = rubraview_pal_media_open(path, RUBRAVIEW_BACKEND_MEDIA_FOUNDATION, &vgpu);
    opened_in = rubraview_pal_time_now_seconds() - opened_in;
    if (!vopen.media) {
        proven_u8str_view_t why = rubraview_media_failure_text(vopen.failure);
        snprintf(line, sizeof(line), "viewer path (%s): the film did not open (%.1f s) - %.*s", MODE_NAMES[vmode], opened_in, (int)why.size, why.ptr);
        console_line(line);
        code = 2;
    } else {
        rubraview_texture_t *page = vopen.info.hardware_decode
            ? rubraview_pal_texture_create_video(renderer, vopen.info.width, vopen.info.height) : NULL;
        int pictures = 0, copied = 0, drawn = 0;
        double brightness = -1.0;
        double start = rubraview_pal_time_now_seconds();
        while (rubraview_pal_time_now_seconds() - start < 3.0 && pictures < 60) {
            rubraview_video_frame_t frame;
            if (!rubraview_pal_media_peek_frame(vopen.media, &frame)) { rubraview_pal_time_sleep_ms(2); continue; }
            pictures++;
            if (page && frame.gpu_texture &&
                rubraview_pal_texture_copy_video_frame(page, frame.gpu_texture, frame.gpu_subresource)) {
                copied++;
                rubraview_pal_render_begin(renderer, 0xFF000000u);
                rubraview_pal_render_draw_texture(renderer, page,
                    (rubraview_mat3x2_t){ .a = 320.0 / vopen.info.width, .d = 240.0 / vopen.info.height },
                    RUBRAVIEW_INTERP_LINEAR);
                if (rubraview_pal_render_end(renderer)) drawn++;
                if (pictures > 20) rubraview_pal_texture_video_brightness(page, &brightness);
            }
            rubraview_pal_media_pop_frame(vopen.media);
        }
        snprintf(line, sizeof(line),
                 "viewer path (%s): opened in %.1f s, decoded %s; %d pictures, %d copied to the screen texture, "
                 "%d drawn; brightness of the picture %.0f of 255",
                 MODE_NAMES[vmode], opened_in, vopen.info.hardware_decode ? "on the card" : "in software", pictures, copied, drawn,
                 brightness);
        console_line(line);
        if (vopen.info.hardware_decode && (copied == 0 || drawn == 0)) code = 2;
        if (page) rubraview_pal_texture_destroy(page);
        rubraview_pal_media_close(vopen.media);
    }
  }
    rubraview_pal_render_destroy(renderer);
    DestroyWindow(hidden);
    return code;
}

/* `--diag`: bring the graphics up, say what it got, and stop. One run
   from a command prompt answers "what is this machine actually using",
   which is otherwise guesswork from far away. */
/*
 * The stages are deliberately separated and each one announces itself
 * *before* it runs, so a hang names the step it hung in rather than
 * leaving a silent window. The first run of this found exactly that: the
 * report stopped after "walking the image path", which said the failure
 * was in the very first call and nowhere else.
 *
 * They also run in order of dependency — COM, then the codec, then a
 * window, then the GPU — so the earliest broken thing is found before
 * anything downstream can confuse the picture.
 */
static void pump_messages(void) {
    /* A window whose messages nobody reads is a window Windows calls
       "not responding", and an STA that never pumps can deadlock COM
       outright. Anywhere this code waits, it waits like this. */
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static void wait_pumping(int milliseconds) {
    for (int i = 0; i < milliseconds; i += 16) {
        pump_messages();
        rubraview_pal_time_sleep_ms(16);
    }
}

static int run_diagnostics(proven_arena_t *arena, proven_u8str_view_t image_path) {
    console_line("rubraview " RUBRAVIEW_VERSION_STRING " - diagnostics");
    console_line("rubraview: step 1 - starting COM's imaging factory (WIC)");
    /* Before any window exists, so a hang here cannot be blamed on one. */
    IWICImagingFactory *probe_factory = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory, (void**)&probe_factory);
    if (FAILED(hr) || !probe_factory) {
        char line[160];
        snprintf(line, sizeof(line), "rubraview: step 1 FAILED (0x%08lX) - no imaging codecs at all", (unsigned long)hr);
        console_line(line);
        return 1;
    }
    console_line("rubraview: step 1 ok");

    if (image_path.size > 0) {
        console_line("rubraview: step 2 - opening the file with WIC (no window, no GPU)");

        char narrow[1024];
        size_t n = image_path.size < sizeof(narrow) - 1 ? image_path.size : sizeof(narrow) - 1;
        memcpy(narrow, image_path.ptr, n);
        narrow[n] = '\0';

        WCHAR wide[1024];
        IWICBitmapDecoder *decoder = NULL;
        if (MultiByteToWideChar(CP_UTF8, 0, narrow, -1, wide, 1024) > 0) {
            hr = IWICImagingFactory_CreateDecoderFromFilename(probe_factory, wide, NULL, GENERIC_READ,
                                                              WICDecodeMetadataCacheOnDemand, &decoder);
        } else {
            hr = E_INVALIDARG;
        }

        char line[256];
        if (FAILED(hr) || !decoder) {
            snprintf(line, sizeof(line), "rubraview: step 2 FAILED (0x%08lX) - the path or the format", (unsigned long)hr);
            console_line(line);
            IWICImagingFactory_Release(probe_factory);
            return 1;
        }

        UINT frames = 0;
        IWICBitmapDecoder_GetFrameCount(decoder, &frames);
        IWICBitmapFrameDecode *frame = NULL;
        hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
        UINT w = 0, h = 0;
        if (SUCCEEDED(hr) && frame) IWICBitmapFrameDecode_GetSize(frame, &w, &h);
        snprintf(line, sizeof(line), "rubraview: step 2 ok - %u frame(s), %ux%u", frames, w, h);
        console_line(line);
        if (frame) IWICBitmapFrameDecode_Release(frame);
        IWICBitmapDecoder_Release(decoder);
    }
    IWICImagingFactory_Release(probe_factory);

    console_line("rubraview: step 3 - creating the window");
    rubraview_window_config_t config = { .title = "Rubraview " RUBRAVIEW_VERSION_STRING " diagnostics", .width = 640, .height = 400, .frameless = false };
    rubraview_window_t *window = rubraview_pal_window_create(arena, &config);
    if (!window) {
        console_line("rubraview: step 3 FAILED - the window could not be created");
        return 1;
    }
    pump_messages();
    console_line("rubraview: step 3 ok");

    console_line("rubraview: step 4 - starting the graphics device");
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(window, &w, &h);
    rubraview_renderer_t *renderer = rubraview_pal_render_create(
        arena, rubraview_pal_window_native_handle(window), w, h);

    if (!renderer) {
        char line[512];
        proven_u8str_view_t where = rubraview_pal_render_last_error();
        snprintf(line, sizeof(line), "rubraview: step 4 FAILED while %.*s (0x%08lX)",
                 (int)where.size, where.ptr, (unsigned long)rubraview_pal_render_last_hresult());
        console_line(line);
        rubraview_pal_window_destroy(window);
        return 1;
    }

    char described[256];
    proven_u8str_view_t info = rubraview_pal_render_describe(renderer, described, sizeof(described));
    char line[320];
    snprintf(line, sizeof(line), "rubraview: step 4 ok - %.*s", (int)info.size, info.ptr);
    console_line(line);

    console_line("rubraview: step 5 - drawing a plain rectangle");
    rubraview_pal_render_begin(renderer, 0xFF203040u);
    rubraview_pal_rect_t box = { 40.0, 40.0, 200.0, 120.0 };
    rubraview_pal_render_fill_rect(renderer, box, 0xFFCC4444u, 4.0);
    bool presented = rubraview_pal_render_end(renderer);
    console_line(presented ? "rubraview: step 5 ok - it was presented"
                           : "rubraview: step 5 FAILED - the device was lost");
    wait_pumping(800);

    if (image_path.size > 0) {
        console_line("rubraview: step 6 - the viewer's own decode path, stage by stage");

        char report[2048];
        proven_u8str_view_t text = rubraview_pal_image_diagnose(renderer, image_path, report, sizeof(report));

        size_t start = 0;
        for (size_t i = 0; i <= text.size; ++i) {
            bool at_end = i == text.size;
            if (!at_end && text.ptr[i] != '\n') continue;
            size_t stop = i;
            while (stop > start && (text.ptr[stop - 1] == '\r' || text.ptr[stop - 1] == '\n')) stop--;
            if (stop > start) {
                char one[256];
                size_t take = stop - start < sizeof(one) - 1 ? stop - start : sizeof(one) - 1;
                memcpy(one, text.ptr + start, take);
                one[take] = '\0';
                console_line(one);
            }
            start = i + 1;
        }

        console_line("rubraview: step 7 - loading it the way the viewer does");
        rubraview_image_load_result_t loaded =
            rubraview_pal_image_load_texture(renderer, image_path, true);
        console_line(loaded.ok ? "rubraview: step 7 ok - it became a texture"
                               : "rubraview: step 7 FAILED - it did not become a texture");

        if (loaded.ok && loaded.texture) {
            console_line("rubraview: step 8 - putting it on the screen (5 seconds)");
            rubraview_pal_render_begin(renderer, 0xFF101010u);
            rubraview_mat3x2_t place = rubraview_mat3x2_identity();
            rubraview_pal_render_draw_texture(renderer, loaded.texture, place, RUBRAVIEW_INTERP_LINEAR);
            bool shown = rubraview_pal_render_end(renderer);
            console_line(shown ? "rubraview: step 8 ok - look at the window now"
                               : "rubraview: step 8 FAILED - it did not present");
            wait_pumping(5000);
            rubraview_pal_texture_destroy(loaded.texture);
        }
    } else {
        wait_pumping(1200);
    }

    console_line("rubraview: done");
    rubraview_pal_render_destroy(renderer);
    rubraview_pal_window_destroy(window);
    return presented ? 0 : 1;
}

/* §3.6: dragging a floating box. The press is held: travel more than a
   few pixels and it was a drag, otherwise it was the click the anchor
   would have done anyway. */
#define BOX_DRAG_SLOP 4.0

static bool box_press(app_state_t *app, double x, double y) {
    rubraview_tile_metrics_t metrics =
        tile_metrics(rubraview_pal_window_dpi_scale(app->window));
    rubraview_box_t *boxes[2] = { &app->menubox, &app->toolbox };
    for (size_t i = 0; i < 2; ++i) {
        if (boxes[i]->state == RUBRAVIEW_BOX_DETACHED) continue;
        /* The grip beside the pin sizes the box (owner, 2026-09-29). */
        if (rubraview_box_grip_begin(boxes[i], &metrics, x, y)) {
            app->box_sizing = boxes[i];
            return true;
        }
        /* Either half is a handle (owner, 2026-10-15; the right one only
           hovered before): a press without a drag is the half's own click. */
        rubraview_anchor_half_t half = rubraview_box_anchor_half_at(boxes[i], &metrics, x, y);
        if (half == RUBRAVIEW_ANCHOR_NONE) continue;
        if (boxes[i] == &app->toolbox && half == RUBRAVIEW_ANCHOR_HOVER) {
            /* 1.25.0 (owner, 2026-10-20): T held and dragged — up and down the files, sideways the folders or
               the archives beside this one, a wider step; F still moves the Floating Widget */
            app->nav_drag = true;
            app->box_press_x = x;
            app->box_press_y = y;
            app->nav_done_x = app->nav_done_y = 0;
            return true;
        }
        app->box_drag = boxes[i];
        app->box_grab_dx = x - boxes[i]->anchor_x;
        app->box_grab_dy = y - boxes[i]->anchor_y;
        app->box_press_x = x;
        app->box_press_y = y;
        app->box_drag_moved = false;
        return true;
    }
    return false;
}

static void box_drag_motion(app_state_t *app, double x, double y) {
    if (app->nav_drag) {
        double dpi = rubraview_pal_window_dpi_scale(app->window);
        int32_t sy = (int32_t)((y - app->box_press_y) / (36.0 * dpi)), sx = (int32_t)((x - app->box_press_x) / (110.0 * dpi));
        if (sx != app->nav_done_x) {   /* sideways first: a folder or an archive is the bigger step */
            bool forward = sx > app->nav_done_x;
            app->nav_done_x += forward ? 1 : -1;
            app->nav_done_y = sy;      /* what the hand did up or down while going sideways is not a file step */
            bool opened = app->source.archive_path.size > 0 ? open_sibling_archive(app, forward) : open_sibling_folder(app, forward);
            if (!opened) osd_say(app, forward ? TU8("nothing after this one") : TU8("nothing before this one"));
            return;
        }
        while (sy != app->nav_done_y) {
            bool forward = sy > app->nav_done_y;
            app->nav_done_y += forward ? 1 : -1;
            handle_action(app, forward ? PROVEN_LIT("next_page") : PROVEN_LIT("prev_page"));
        }
        return;
    }
    if (app->box_sizing) {
        rubraview_tile_metrics_t metrics = tile_metrics(rubraview_pal_window_dpi_scale(app->window));
        (void)rubraview_box_grip_drag(app->box_sizing, &metrics, x, y);
        return;
    }
    if (!app->box_drag) return;
    if (!app->box_drag_moved) {
        double dx = x - app->box_press_x, dy = y - app->box_press_y;
        if (dx * dx + dy * dy < BOX_DRAG_SLOP * BOX_DRAG_SLOP) return;
        app->box_drag_moved = true;
    }
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    rubraview_tile_metrics_t metrics =
        tile_metrics(rubraview_pal_window_dpi_scale(app->window));
    rubraview_box_drag_to(app->box_drag, &metrics, x - app->box_grab_dx, y - app->box_grab_dy,
                          (double)win_w, (double)win_h);
    app->box_drag->open_side = 0;   /* 1.25.0: the open toolbar takes the side with room as the widget moves, live */
    /* §3.6.1 / RFC-0002 Q6: the toolbox dragged past the edge becomes a
       window of its own, and the drag carries on moving that window. */
    if (app->box_drag == &app->toolbox &&
        rubraview_box_update_detach(&app->toolbox, &metrics, (double)win_w, (double)win_h, 24.0 * rubraview_pal_window_dpi_scale(app->window))) {
        int32_t fx = 0, fy = 0, fw = 0, fh = 0;
        rubraview_pal_window_get_frame(app->window, &fx, &fy, &fw, &fh);
        app->box_drag = NULL;
        toolbox_detach(app, fx + (int32_t)app->toolbox.anchor_x, fy + (int32_t)app->toolbox.anchor_y, true);
    }
}

static void box_release(app_state_t *app, double x, double y) {
    if (app->nav_drag) { app->nav_drag = false; return; }   /* T: a press without a drag only hovers */
    if (app->box_sizing) {
        /* Kept in settings.ini, as the boxes' opacity is. */
        rubraview_box_t *box = app->box_sizing;
        app->box_sizing = NULL;
        rubraview_box_grip_end(box);
        bool menu = box == &app->menubox;
        rubraview_settings_set(&app->settings, PROVEN_LIT("ui"), menu ? PROVEN_LIT("menubox_columns") : PROVEN_LIT("toolbox_columns"), (double)box->columns);
        rubraview_settings_set(&app->settings, PROVEN_LIT("ui"), menu ? PROVEN_LIT("menubox_size") : PROVEN_LIT("toolbox_size"), box->scale * 100.0);
        return;
    }
    if (!app->box_drag) return;
    rubraview_box_t *box = app->box_drag;
    app->box_drag = NULL;
    if (app->box_drag_moved) return;   /* a drag: the box stays where it was let go */
    rubraview_tile_metrics_t metrics =
        tile_metrics(rubraview_pal_window_dpi_scale(app->window));
    if (box == &app->toolbox && rubraview_box_anchor_half_at(box, &metrics, x, y) == RUBRAVIEW_ANCHOR_CLICK) {
        rubraview_box_click_open(box);   /* 1.25.0: F opens and folds it, never pins it */
        return;
    }
    if (rubraview_box_click(box, &metrics, x, y) && box == &app->menubox) sync_menubox_tiles(app);
}

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
    double last_busy_seconds = app.last_frame_seconds;
    double last_idle_frame_seconds = app.last_frame_seconds;
    uint64_t last_scene_signature = 0;   /* the settled screen last drawn (see scene_signature) */
    double last_render_seconds = app.last_frame_seconds;
    double last_input_seconds = app.last_frame_seconds;

    while (!rubraview_pal_window_should_close(app.window)) {
        rubraview_window_event_t event;
        size_t handled = 0;
        {
            /* RFC-0002 §4: the boxes open inside the window, whatever its size now. */
            int32_t view_w = 0, view_h = 0;
            rubraview_pal_window_get_size(app.window, &view_w, &view_h);
            app.toolbox.view_width = app.menubox.view_width = (double)view_w;
            app.toolbox.view_height = app.menubox.view_height = (double)view_h;
            /* With our own title bar the menu is its ☰'s, under the bar; with Windows' frame the M box stays. */
            app.menubox.headless = true;   /* 1.21.0: no M box either; the menu lives in the right click */
            if (app.menubox.headless) {
                app.menubox.anchor_x = 0.0;
                app.menubox.anchor_y = app.titlebar.height;
                app.menubox.pinned = false;   /* no pin: it folds as a menu does */
            }
            boxes_place(&app);
            rubraview_tile_metrics_t side_metrics = tile_metrics(rubraview_pal_window_dpi_scale(app.window));
            rubraview_box_keep_side(&app.toolbox, &side_metrics);
            rubraview_box_keep_side(&app.menubox, &side_metrics);
            aspect_refresh(&app);   /* the orientation, the edit panel or the custom pair may have changed */
        }
        while (rubraview_pal_window_poll_event(app.window, &event)) {
            handled++;
            switch (event.kind) {
                case RUBRAVIEW_WINDOW_EVENT_CLOSE:
                    /* RV-115: the X or Alt+F4 with a subtitle's changes not saved is held once */
                    if (subs_may_leave(&app)) rubraview_pal_window_request_close(app.window);
                    else rubraview_pal_window_keep_open(app.window);
                    break;

                case RUBRAVIEW_WINDOW_EVENT_RESIZE:
                    rubraview_pal_render_resize(app.renderer, event.resize.width, event.resize.height);
                    app.filmstrip.viewport_extent = (double)event.resize.width;
                    app.needs_relayout = true;
                    panel_relayout(&app);
                    boxes_place(&app);   /* D-96: the same share of the new size (the clamp alone left them where they were) */
                    break;

                case RUBRAVIEW_WINDOW_EVENT_DPI_CHANGED:
                    app.titlebar = rubraview_titlebar_create(event.dpi.scale);
                    app.titlebar.trigger_zone = fmax(36.0, rubraview_settings_get(&app.settings, PROVEN_LIT("general"), PROVEN_LIT("titlebar_trigger_px"))) * event.dpi.scale;
                    app.titlebar.hide_delay = rubraview_settings_get(&app.settings, PROVEN_LIT("general"), PROVEN_LIT("titlebar_hide_ms")) / 1000.0;
                    app.needs_relayout = true;
                    break;

                case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN:
                    if (app.sel_mode && key_is(event.key.combo, "Escape")) {   /* stops choosing, not the program */
                        sel_mode_set(&app, false);
                        if (edit_panel_open(&app)) panel_build_edit(&app);
                        break;
                    }
                    dispatch_key(&app, event.key.combo);
                    break;

                case RUBRAVIEW_WINDOW_EVENT_TEXT:
                case RUBRAVIEW_WINDOW_EVENT_COMPOSITION:
                    rename_text(&app, &event);
                    break;

                case RUBRAVIEW_WINDOW_EVENT_MOUSE_LEAVE:
                    app.pointer_inside = false;   /* the edge buttons go with the pointer */
                    break;

                case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE: {
                    if (app.text && app.text->selecting) { text_drag(&app, event.mouse.x, event.mouse.y); break; }
                    if (!app.seekfan_detached) seekfan_pointer(&app, event.mouse.x, event.mouse.y);
                    speed_tile_move(&app, event.mouse.y, false);
                    if (app.view_pressed && !app.view_dragging) {
                        /* Owner, 2026-10-14: a click on a film or a song is Play / Pause, so a hand
                           that moves a little while pressing is still a click — a film is dragged
                           only past twelve pixels, and a song, which has nothing to drag, never. */
                        double slack = (app.media ? 12.0 : 4.0) * rubraview_pal_window_dpi_scale(app.window);
                        if (app.media && !app.media_info.has_video) slack = 1e9;
                        if (fabs(event.mouse.x - app.view_press_x) > slack || fabs(event.mouse.y - app.view_press_y) > slack) {
                            app.view_dragging = true;
                        }
                    }
                    if (app.view_dragging) {
                        app.pan_x += event.mouse.x - app.view_drag_x;
                        app.pan_y += event.mouse.y - app.view_drag_y;
                        app.view_drag_x = event.mouse.x;
                        app.view_drag_y = event.mouse.y;
                        note_activity(&app);
                    }
                    if (app.subbox.dragging != RUBRAVIEW_SUBBOX_NONE) {
                        subbox_drag_motion(&app, event.mouse.x, event.mouse.y);   /* D-33 */
                        app.pointer_x = event.mouse.x;
                        app.pointer_y = event.mouse.y;
                        break;
                    }
                    if (app.curve_dragging >= 0 && !app.edit_window) {
                        curve_widget_drag(&app, event.mouse.x, event.mouse.y);
                        app.pointer_x = event.mouse.x;
                        app.pointer_y = event.mouse.y;
                        break;
                    }
                    if (app.sel_grab) {
                        int32_t ix = 0, iy = 0;
                        if (crop_point_to_image(&app, event.mouse.x, event.mouse.y, &ix, &iy)) {
                            sel_drag_to(&app, ix, iy);
                            edit_sel_sync(&app);
                        }
                        app.pointer_x = event.mouse.x;
                        app.pointer_y = event.mouse.y;
                        break;
                    }
                    {
                        /* A box taken by its title follows the pointer. */
                        rubraview_panel_t *moving = app.edit_window ? (app.classify_panel.moving ? &app.classify_panel : NULL)
                                                  : app.panel.moving ? &app.panel : app.sel_panel.moving ? &app.sel_panel
                                                  : app.steps_panel.moving ? &app.steps_panel
                                                  : app.classify_panel.moving ? &app.classify_panel : NULL;
                        if (moving) {
                            int32_t ww = 0, wh = 0;
                            rubraview_pal_window_get_size(app.window, &ww, &wh);
                            rubraview_panel_move_to(moving, event.mouse.x, event.mouse.y, (double)ww, (double)wh);
                            app.pointer_x = event.mouse.x;
                            app.pointer_y = event.mouse.y;
                            break;
                        }
                    }
                    if (app.crop_dragging) {
                        int32_t ix = 0, iy = 0;
                        app.edit_rev++;
                        if (crop_point_to_image(&app, event.mouse.x, event.mouse.y, &ix, &iy)) {
                            rubraview_edit_crop_drag(&app.edit, app.crop_drag_x0, app.crop_drag_y0, ix, iy);
                            crop_shown_again(&app);
                        }
                        app.pointer_x = event.mouse.x;
                        app.pointer_y = event.mouse.y;
                        break;
                    }
                    if (app.panel.open && app.panel.active_row >= 0 && !(app.edit_window && edit_panel_open(&app))) {
                        int32_t dragged = -1;
                        if (rubraview_panel_drag(&app.panel, event.mouse.x, event.mouse.y, &dragged)
                                == RUBRAVIEW_PANEL_VALUE_CHANGED) {
                            panel_apply_row(&app, dragged);
                        }
                    }
                    if (app.pagebar_dragging && !app.pagebar_detached) pagebar_drag(&app, event.mouse.x, false);
                    if (app.picker_open && picker_details(&app)) picker_columns_motion(&app, event.mouse.x, event.mouse.y);   /* 1.25.0 */
                    if (app.picker_hold) {   /* 1.22.0: moved off: neither a tap nor a long press */
                        double dx = event.mouse.x - app.picker_hold_x, dy = event.mouse.y - app.picker_hold_y;
                        double slop = 10.0 * rubraview_pal_window_dpi_scale(app.window);
                        if (dx * dx + dy * dy > slop * slop) app.picker_hold = false;
                    }
                    if (app.fav_press >= 0) {
                        app.fav_drag_x = event.mouse.x;
                        if (fabs(event.mouse.x - app.fav_press_x) >= 6.0 * rubraview_pal_window_dpi_scale(app.window)) app.fav_dragging = true;
                    }
                    if (app.titlebar_held != RUBRAVIEW_TITLEBAR_NONE) {
                        double dx = event.mouse.x - app.titlebar_held_x, dy = event.mouse.y - app.titlebar_held_y;
                        double slop = 4.0 * rubraview_pal_window_dpi_scale(app.window);
                        if (dx * dx + dy * dy >= slop * slop) {
                            rubraview_titlebar_button_t held = app.titlebar_held;
                            app.titlebar_held = RUBRAVIEW_TITLEBAR_NONE;
                            if (held == RUBRAVIEW_TITLEBAR_RESIZE) rubraview_pal_window_begin_resize(app.window);
                            else rubraview_pal_window_begin_drag(app.window);
                            break;
                        }
                    }
                    if (app.eqwin.dragging || app.eqwin.sliding >= 0) {
                        int32_t eq_w = 0, eq_h = 0;
                        rubraview_pal_window_get_size(app.window, &eq_w, &eq_h);
                        if (app.eqwin.dragging) rubraview_eqwin_drag_to(&app.eqwin, event.mouse.x, event.mouse.y, (double)eq_w, (double)eq_h);
                        else eqwin_set_band(&app, (size_t)app.eqwin.sliding,
                                            rubraview_eqwin_db_at(&app.eqwin, (size_t)app.eqwin.sliding, event.mouse.y));
                    }
                    listwin_motion(&app, event.mouse.x, event.mouse.y);
                    recentwin_motion(&app, event.mouse.x, event.mouse.y);
                    favwin_motion(&app, event.mouse.x, event.mouse.y);
                    app.pointer_inside = true;
                    app.pointer_x = event.mouse.x;
                    app.pointer_y = event.mouse.y;
                    if (app.frameless && !app.chrome_hidden) rubraview_titlebar_pointer_moved(&app.titlebar, event.mouse.y);
                    if (rubraview_cursor_hide_notify_motion(&app.cursor)) {
                        rubraview_pal_window_set_cursor_visible(app.window, true);
                    }
                    note_activity(&app);

                    /* §3.6: the box model decides what a pointer over
                       it means — only the hover half opens anything. */
                    if (app.chrome_hidden) break;   /* D-110: nothing opens under the pointer */
                    rubraview_tile_metrics_t metrics = tile_metrics(rubraview_pal_window_dpi_scale(app.window));
                    rubraview_box_pointer(&app.toolbox, &metrics, event.mouse.x, event.mouse.y);
                    rubraview_box_pointer(&app.menubox, &metrics, event.mouse.x, event.mouse.y);
                    box_drag_motion(&app, event.mouse.x, event.mouse.y);
                    break;
                }

                case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN: {
                    if (chooser_press(&app, CHOOSE_IN_MAIN, event.mouse.x, event.mouse.y)) break;   /* 1.24.0 */
                    if (tmenu_press(&app, event.mouse.x, event.mouse.y)) break;   /* 1.25.0: the ☰ pull-down */
                    if (!(app.edit_window && edit_panel_open(&app) && (app.rename_is_value || app.rename_is_apply)) &&
                        rename_box_press(&app, event.mouse.x, event.mouse.y)) break;   /* 1.24.0: the box's buttons */
                    if (app.picker_open && app.favwin.open && event.mouse.button == RUBRAVIEW_MOUSE_LEFT &&
                        favwin_press(&app, event.mouse.x, event.mouse.y)) break;   /* 1.24.0: over the open-file screen too */
                    if (app.chrome_hidden) {
                        /* D-110: the click that brings the bar and the boxes back
                           is only that — it turns no page and pauses nothing. */
                        chrome_hide(&app, false);
                        osd_say(&app, TU8("the title bar and the Floating Widget are back"));
                        break;
                    }
                    if (app.picker_open) {
                        /* 1.22.0: a tile's right-click list takes the press first */
                        if (app.ctx_open && popups_press(&app, event.mouse.x, event.mouse.y, event.mouse.button == RUBRAVIEW_MOUSE_RIGHT)) break;
                        double dpi = rubraview_pal_window_dpi_scale(app.window);
                        double top = picker_top(&app);
                        double crumb_h = top + PICKER_CRUMB_HEIGHT * dpi;
                        double header_h = top + PICKER_HEADER_HEIGHT * dpi;
                        picker_layout(&app);
                        double grid_top = picker_grid_top(&app, dpi);

                        if (titlebar_click(&app, event.mouse.x, event.mouse.y)) break;
                        if (event.mouse.y < top) break;   /* the band holds nothing to press */
                        if (event.mouse.y < crumb_h && !app.picker_in_book) {   /* 1.22.0: the filter's chip */
                            int32_t fw = 0, fh = 0;
                            rubraview_pal_window_get_size(app.window, &fw, &fh);
                            char fbuf[200];
                            proven_u8str_view_t flabel;
                            rubraview_pal_rect_t chip = picker_filter_chip(&app, (double)fw, dpi, &flabel, fbuf, sizeof(fbuf));
                            if (pal_rect_has(chip, event.mouse.x, event.mouse.y)) {
                                if (app.picker_filter[0] && event.mouse.x >= chip.x + chip.width - 28.0 * dpi) picker_filter_set(&app, PROVEN_LIT(""));
                                else picker_filter_begin(&app);
                                break;
                            }
                        }
                        if (event.mouse.y < crumb_h) {
                            /* 1.24.0: back, up, forward */
                            int nav = -1;
                            for (int k = 0; k < 3; ++k) if (pal_rect_has(picker_nav_rect(&app, dpi, k), event.mouse.x, event.mouse.y)) nav = k;
                            if (nav >= 0) {
                                if (app.rename_active) rename_end(&app);
                                if (nav == 1) { if (!app.picker_in_book) picker_go_up(&app); }
                                else picker_history_go(&app, nav == 2);
                                break;
                            }
                            /* A breadcrumb chip navigates to its prefix; the
                               bar past the last chip takes a typed path. */
                            rubraview_breadcrumbs_t crumbs = rubraview_picker_breadcrumbs(app.picker_dir);
                            double x = PICKER_CRUMB_START * dpi;
                            bool on_chip = false;
                            for (size_t i = 0; i < crumbs.count; ++i) {
                                double w = picker_chip_width(crumbs.items[i].label, dpi);
                                if (event.mouse.x >= x && event.mouse.x < x + w) {
                                    if (app.rename_active) rename_end(&app);
                                    picker_navigate(&app, crumbs.items[i].prefix);
                                    app.picker.focus = 0;
                                    on_chip = true;
                                    break;
                                }
                                x += w + 4.0 * dpi;
                            }
                            proven_u8str_view_t star = on_chip ? PROVEN_LIT("") : picker_star_label(&app);
                            if (star.size > 0 && event.mouse.x >= x && event.mouse.x < x + picker_chip_width(star, dpi)) {
                                picker_toggle_favorite(&app);
                                break;
                            }
                            if (star.size > 0) x += picker_chip_width(star, dpi) + 4.0 * dpi;
                            if (!on_chip && event.mouse.x >= x && !app.rename_active) picker_path_begin(&app);
                            break;
                        }
                        if (event.mouse.y < header_h) {
                            int32_t bw0 = 0, bh0 = 0;
                            rubraview_pal_window_get_size(app.window, &bw0, &bh0);
                            if (pal_rect_has(picker_favwin_chip(&app, dpi, (double)bw0), event.mouse.x, event.mouse.y)) { favwin_toggle(&app); break; }
                            double x = 8.0 * dpi;
                            picker_bar_item_t item;
                            for (size_t i = 0; picker_bar_item(&app, i, &item); ++i) {
                                double w = picker_chip_width(item.label, dpi);
                                if (!item.favorite && i == app.favorites.count && i > 0) x += 10.0 * dpi;   /* as drawn */
                                if (event.mouse.x >= x && event.mouse.x < x + w) {
                                    if (app.rename_active) rename_end(&app);
                                    if (item.favorite && event.mouse.button == RUBRAVIEW_MOUSE_RIGHT) {
                                        favorite_rename_begin(&app, i);   /* owner, 2026-09-30 */
                                    } else if (item.favorite && event.mouse.button == RUBRAVIEW_MOUSE_LEFT) {
                                        /* opened on release; dragged, it moves (owner, 2026-09-30) */
                                        app.fav_press = (int32_t)i;
                                        app.fav_press_x = app.fav_drag_x = event.mouse.x;
                                        app.fav_dragging = false;
                                    } else {
                                        picker_navigate(&app, item.path);
                                        app.picker.focus = 0;
                                    }
                                    break;
                                }
                                x += w + 4.0 * dpi;
                            }
                            break;
                        }

                        int32_t pw = 0, ph = 0;
                        rubraview_pal_window_get_size(app.window, &pw, &ph);
                        if (event.mouse.y >= (double)ph - PICKER_ACTION_HEIGHT * dpi) {
                            for (int32_t b = 0; b < PICKER_BOTTOM_COUNT; ++b) {
                                if (pal_rect_has(picker_button_rect((double)pw, (double)ph, dpi, b), event.mouse.x, event.mouse.y)) {
                                    picker_button(&app, PICKER_BOTTOM[b]);
                                    break;
                                }
                            }
                            break;
                        }
                        if (event.mouse.y < header_h + PICKER_TOOL_HEIGHT * dpi) {   /* 1.24.0: the tool row */
                            for (int k = 0; k < PICKER_TOOL_COUNT; ++k) {
                                rubraview_pal_rect_t t = picker_tool_rect(&app, dpi, k);
                                if (!pal_rect_has(t, event.mouse.x, event.mouse.y)) continue;
                                switch (k) {
                                    case PICKER_TOOL_ALL:        picker_select_everything(&app); break;
                                    case PICKER_TOOL_INDIVIDUAL: picker_button(&app, PICKER_BTN_INDIVIDUAL); break;
                                    case PICKER_TOOL_RANGE:      picker_button(&app, PICKER_BTN_RANGE); break;
                                    case PICKER_TOOL_SAME:       picker_button(&app, PICKER_BTN_SAME_TYPE); break;
                                    case PICKER_TOOL_MEDIA:      picker_button(&app, PICKER_BTN_MEDIA); break;
                                    case PICKER_TOOL_SORT:       chooser_open_sort(&app, false, t.x, t.y + t.height, t.y); break;
                                    case PICKER_TOOL_ORDER:      chooser_open_sort(&app, true, t.x, t.y + t.height, t.y); break;
                                    case PICKER_TOOL_VIEW:       picker_button(&app, PICKER_BTN_VIEW); break;
                                    default: break;
                                }
                                break;
                            }
                            break;
                        }
                        if (event.mouse.y < grid_top) {
                            /* the columns' names: sort by the one pressed, again the other way round (owner, 2026-10-16) */
                            if (event.mouse.y >= grid_top - PICKER_DETAIL_HEAD * dpi) {
                                picker_columns_t cols = picker_detail_columns((double)pw, dpi);
                                /* 1.25.0: on a line between two heads its width is dragged; on a head, it is dragged to
                                   another place, or (not moved) sorts by it */
                                int edge = picker_column_edge(&cols, event.mouse.x, dpi);
                                int k = picker_column_at(&cols, event.mouse.x);
                                if (edge > 0) {
                                    app.col_drag = 1; app.col_drag_k = edge; app.col_drag_x0 = event.mouse.x; app.col_drag_w0 = g_col_w[edge];
                                } else if (k >= 0) {
                                    app.col_drag = 2; app.col_drag_k = k; app.col_drag_x0 = event.mouse.x; app.col_drag_moved = false;
                                }
                            }
                            break;
                        }
                        size_t columns = (size_t)app.picker.columns;
                        double cell_w = (double)pw / (double)columns;
                        size_t column = (size_t)(event.mouse.x / cell_w);
                        size_t row = (size_t)((event.mouse.y - grid_top + app.picker.scroll_offset) / app.picker.tile_extent);
                        size_t index = row * columns + column;
                        if (column < columns && index < app.picker_listing.count) {
                            app.picker.focus = index;
                            if (!app.picker_in_book) {   /* 1.22.0 */
                                /* a check box picks, in any mode; folders too */
                                if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT && !picker_is_up(&app, index) && app.picker_selected &&
                                    pal_rect_has(picker_check_rect(&app, index, (double)pw, dpi), event.mouse.x, event.mouse.y)) {
                                    app.picker_selected[index] = !app.picker_selected[index];
                                    app.picker_anchor = index;
                                    break;
                                }
                                if (event.mouse.button == RUBRAVIEW_MOUSE_RIGHT) { picker_context(&app, index, event.mouse.x, event.mouse.y); break; }
                                /* 1.24.0 (owner, 2026-10-19): Ctrl+click picks or lets go of one, Shift+click a range from the last one clicked */
                                bool ctrl = (event.mouse.modifiers & RUBRAVIEW_MOD_CTRL) != 0, shift = (event.mouse.modifiers & RUBRAVIEW_MOD_SHIFT) != 0;
                                if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT && (ctrl || shift) && app.picker_selected && !picker_is_up(&app, index)) {
                                    if (shift) {
                                        size_t from = app.picker_anchor < app.picker_listing.count ? app.picker_anchor : index;
                                        size_t lo = from < index ? from : index, hi = from < index ? index : from;
                                        if (!ctrl) for (size_t i = 0; i < app.picker_listing.count; ++i) app.picker_selected[i] = false;
                                        for (size_t i = lo; i <= hi; ++i) app.picker_selected[i] = !picker_is_up(&app, i);
                                    } else {
                                        app.picker_selected[index] = !app.picker_selected[index];
                                        app.picker_anchor = index;
                                    }
                                    break;
                                }
                                if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT) app.picker_anchor = index;
                                if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT) {   /* a tap on release; held still, a right click */
                                    app.picker_hold = true;
                                    app.picker_hold_index = index;
                                    app.picker_hold_x = event.mouse.x;
                                    app.picker_hold_y = event.mouse.y;
                                    app.picker_hold_at = rubraview_pal_time_now_seconds();
                                    break;
                                }
                            }
                            /* Right-click on an archive: inside it, not reading it yet. */
                            if (event.mouse.button == RUBRAVIEW_MOUSE_RIGHT && picker_enter_book(&app, index)) break;
                            /* In a picking mode the tap picks; otherwise it opens. */
                            /* In a book a tap always goes to the page, whatever mode was left on outside it. */
                            if (app.picker_in_book || !rubraview_picker_tap(&app.picker, index)) picker_activate(&app, index);
                        }
                        break;
                    }

                    /* RV-116: the right-click list and the slide show's settings take the next press */
                    if ((app.ctx_open || app.slidepop_open) &&
                        popups_press(&app, event.mouse.x, event.mouse.y, event.mouse.button == RUBRAVIEW_MOUSE_RIGHT)) break;

                    /* D-33: the list of subtitle tracks takes the next tap —
                       a row chooses it, anywhere else closes the list. */
                    if (app.sub_list_open) {
                        size_t rows = 0;
                        rubraview_rect_t list = sub_list_rect(&app, &rows);
                        int32_t row = rubraview_choice_list_row_at(list, SUB_LIST_ROW * rubraview_pal_window_dpi_scale(app.window),
                                                                   rows, event.mouse.x, event.mouse.y);
                        if (row >= 0) sub_list_choose(&app, row);
                        else if (!rubraview_rect_contains(list, event.mouse.x, event.mouse.y)) app.sub_list_open = false;
                        break;
                    }

                    /* The A-B box and the pixel box answer a click of their own: the X leaves,
                       the A-B box's buttons take this moment, a field is chosen (owner, 2026-10-15).
                       Elsewhere the click goes on (the seek bar, a crop drag). */
                    /* The strip at the bottom: a thumbnail clicked is the page to go to (owner, 2026-10-15).
                       The floating boxes are drawn over it and keep their own clicks. */
                    if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT && !app.panel.open) {
                        rubraview_tile_metrics_t m = tile_metrics(rubraview_pal_window_dpi_scale(app.window));
                        rubraview_rect_t tb = rubraview_box_bounds(&app.toolbox, &m);
                        bool on_box = app.toolbox.state != RUBRAVIEW_BOX_DETACHED && !app.chrome_hidden &&
                                      rubraview_rect_contains(tb, event.mouse.x, event.mouse.y);
                        if (!on_box && strip_press(&app, event.mouse.x, event.mouse.y)) break;
                    }
                    if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT && app.ab_edit_open) {
                        rubraview_pal_rect_t box = ab_box_rect(&app);
                        if (pal_rect_has(answer_box_close(&app, box), event.mouse.x, event.mouse.y)) { ab_edit_close(&app); break; }
                        if (pal_rect_has(ab_box_button(&app, 0), event.mouse.x, event.mouse.y)) { ab_edit_take(&app, 0); break; }
                        if (pal_rect_has(ab_box_button(&app, 1), event.mouse.x, event.mouse.y)) { ab_edit_take(&app, 1); break; }
                        if (pal_rect_has(box, event.mouse.x, event.mouse.y)) {
                            /* A field clicked is the one typed into; elsewhere in a half, that point's seconds. */
                            int32_t side = event.mouse.x < box.x + box.width * 0.5 ? 0 : 1, part = 2;
                            for (int32_t k = 0; k < 3; ++k) {
                                if (pal_rect_has(ab_part_rect(&app, side, k), event.mouse.x, event.mouse.y)) part = k;
                            }
                            ab_edit_goto(&app, side, part);
                            break;
                        }
                    }
                    if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT && app.px_edit_open) {
                        rubraview_pal_rect_t box = px_box_rect(&app);
                        if (pal_rect_has(answer_box_close(&app, box), event.mouse.x, event.mouse.y)) { px_edit_close(&app); break; }
                        if (pal_rect_has(box, event.mouse.x, event.mouse.y)) {
                            int32_t n = px_edit_fields(&app);
                            int32_t k = (int32_t)((event.mouse.x - box.x) / box.width * (double)n);
                            app.px_edit_field = k < 0 ? 0 : k >= n ? n - 1 : k;
                            break;
                        }
                    }

                    /* §3.13: with the workbench open, a drag on the picture
                       is the crop rectangle. It is asked before the panel,
                       whose "a click outside closes it" rule would otherwise
                       shut the workbench at the first press on the picture. */
                    /* The owner's edit: the boxes beside it, then the selection's handles and inside. */
                    if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT && classify_press(&app, event.mouse.x, event.mouse.y)) break;
                    bool edit_away = app.edit_window && edit_panel_open(&app);   /* RV-137: Edit's boxes are in their own window */
                    if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT && !edit_away && edit_side_press(&app, event.mouse.x, event.mouse.y)) break;
                    /* The floating lists over the picture are theirs, not the selection's ("Edit to all"). */
                    if (edit_panel_open(&app) && app.recentwin.open && event.mouse.button == RUBRAVIEW_MOUSE_LEFT &&
                        rubraview_rect_contains(rubraview_listwin_rect(&app.recentwin), event.mouse.x, event.mouse.y) &&
                        recentwin_press(&app, event.mouse.x, event.mouse.y)) break;
                    if (edit_panel_open(&app) && app.listwin.open &&
                        rubraview_rect_contains(rubraview_listwin_rect(&app.listwin), event.mouse.x, event.mouse.y) &&
                        listwin_press(&app, event.mouse.x, event.mouse.y, event.mouse.button == RUBRAVIEW_MOUSE_RIGHT)) break;
                    /* RV-137 (owner, 2026-10-18): in its own window, Edit chooses an area only after its button. */
                    bool choosing = app.sel_mode;   /* only after the button (owner, 2026-10-18: a drag moved boxes too) */
                    if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT && edit_panel_open(&app) && choosing &&
                        (edit_away || !rubraview_rect_contains(app.panel.bounds, event.mouse.x, event.mouse.y))) {
                        int32_t hit = sel_hit(&app, event.mouse.x, event.mouse.y);
                        if (hit && crop_point_to_image(&app, event.mouse.x, event.mouse.y, &app.sel_grab_x, &app.sel_grab_y)) {
                            app.sel_grab = hit;
                            app.sel_grab_rect = app.edit.crop;
                            break;
                        }
                    }
                    if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT && edit_panel_open(&app) && choosing &&
                        (edit_away || !rubraview_rect_contains(app.panel.bounds, event.mouse.x, event.mouse.y)) &&
                        !point_in_curve_widget(&app, event.mouse.x, event.mouse.y) &&
                        crop_point_to_image(&app, event.mouse.x, event.mouse.y,
                                            &app.crop_drag_x0, &app.crop_drag_y0)) {
                        app.crop_dragging = true;
                        rubraview_edit_crop_drag(&app.edit, app.crop_drag_x0, app.crop_drag_y0,
                                                 app.crop_drag_x0, app.crop_drag_y0);
                        break;
                    }

                    if (!edit_away && panel_handle_press(&app, event.mouse.x, event.mouse.y)) break;
                    if (app.eqwin.open && eqwin_press(&app, event.mouse.x, event.mouse.y,
                                                      event.mouse.button == RUBRAVIEW_MOUSE_RIGHT)) break;
                    if (app.recentwin.open && event.mouse.button == RUBRAVIEW_MOUSE_LEFT && recentwin_press(&app, event.mouse.x, event.mouse.y)) break;
                    if (app.favwin.open && event.mouse.button == RUBRAVIEW_MOUSE_LEFT && favwin_press(&app, event.mouse.x, event.mouse.y)) break;
                    if (app.listwin.open && listwin_press(&app, event.mouse.x, event.mouse.y, event.mouse.button == RUBRAVIEW_MOUSE_RIGHT)) break;
                    if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT && app.subbox.selected &&
                        subbox_press(&app, event.mouse.x, event.mouse.y, true)) break;   /* D-33: S M R X on top */
                    /* The anchors answer before the rest of the chrome: a
                       press there is a click *or* the start of a drag. */
                    if (box_press(&app, event.mouse.x, event.mouse.y)) break;
                    if (handle_chrome_click(&app, event.mouse.x, event.mouse.y)) break;
                    if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT && subbox_press(&app, event.mouse.x, event.mouse.y, false)) break;

                    rubraview_pointer_context_t ctx = pointer_context(&app);
                    rubraview_pointer_intent_t intent;
                    if (event.mouse.button == RUBRAVIEW_MOUSE_MIDDLE) {
                        intent = rubraview_pointer_middle_click(&ctx);
                    } else if (event.mouse.button == RUBRAVIEW_MOUSE_X1) {
                        intent = rubraview_pointer_side_button(false);
                    } else if (event.mouse.button == RUBRAVIEW_MOUSE_X2) {
                        intent = rubraview_pointer_side_button(true);
                    } else if (event.mouse.button == RUBRAVIEW_MOUSE_RIGHT && !app.edit_window &&
                               curve_widget_press(&app, event.mouse.x, event.mouse.y, true)) {
                        break;   /* a point taken off the curve */
                    } else if (event.mouse.button == RUBRAVIEW_MOUSE_RIGHT) {
                        intent = rubraview_pointer_right_click(&ctx);
                        if (intent == RUBRAVIEW_INTENT_CONTEXT_MENU) {   /* RV-116: the list, at the pointer */
                            ctx_open_at(&app, event.mouse.x, event.mouse.y);
                            break;
                        }
                    } else {
                        /* D-109: what a press on the picture means is known when it is
                           let go — moved, it was a drag and the picture followed, at any
                           zoom; not moved, a click. */
                        intent = RUBRAVIEW_INTENT_NONE;
                        if (text_press(&app, event.mouse.x, event.mouse.y, event.mouse.modifiers)) break;   /* RFC-0005: a selection */
                        app.view_pressed = true;
                        app.view_dragging = false;
                        app.view_press_at = rubraview_pal_time_now_seconds();   /* 1.25.0: held still, a right click */
                        app.view_press_x = app.view_drag_x = event.mouse.x;
                        app.view_press_y = app.view_drag_y = event.mouse.y;
                    }
                    apply_intent(&app, intent);
                    break;
                }

                case RUBRAVIEW_WINDOW_EVENT_MOUSE_UP:
                    if (app.col_drag && picker_columns_release(&app, event.mouse.x)) break;   /* 1.25.0 */
                    if (app.picker_hold && event.mouse.button == RUBRAVIEW_MOUSE_LEFT) {   /* 1.22.0: the tap, now it is one */
                        app.picker_hold = false;
                        size_t index = app.picker_hold_index;
                        if (app.picker_open && index < app.picker_listing.count &&
                            !rubraview_picker_tap(&app.picker, index)) picker_activate(&app, index);
                        break;
                    }
                    if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT && text_release(&app, event.mouse.x, event.mouse.y)) break;
                    if (app.view_pressed && !app.view_dragging && event.mouse.button == RUBRAVIEW_MOUSE_LEFT) {
                        /* D-109: a click. Over a film or music it pauses and plays; over a
                           picture it is what it was (a page turned at the sides, the boxes
                           shown), and nothing on one larger than the window. */
                        if (app.media) {
                            handle_action(&app, PROVEN_LIT("media_play_pause"));
                        } else {
                            rubraview_pointer_context_t ctx = pointer_context(&app);
                            rubraview_pointer_intent_t intent = rubraview_pointer_click(&ctx, app.view_press_x, false);
                            /* 1.25.0: two clicks in one place, quickly, where a click turns no page: the picture back in the middle */
                            double now_c = rubraview_pal_time_now_seconds(), slop = 6.0 * rubraview_pal_window_dpi_scale(app.window);
                            bool twice = now_c - app.last_click_at < 0.45 && fabs(event.mouse.x - app.last_click_x) < slop &&
                                         fabs(event.mouse.y - app.last_click_y) < slop;
                            app.last_click_at = twice ? 0.0 : now_c;
                            app.last_click_x = event.mouse.x;
                            app.last_click_y = event.mouse.y;
                            if (twice && (intent == RUBRAVIEW_INTENT_NONE || intent == RUBRAVIEW_INTENT_PAN))
                                intent = rubraview_pointer_click(&ctx, app.view_press_x, true);
                            if (intent != RUBRAVIEW_INTENT_PAN) apply_intent(&app, intent);
                        }
                    }
                    app.view_pressed = false;
                    app.view_dragging = false;
                    sub_tile_result(&app, rubraview_tap_release(&app.sub_tap, rubraview_pal_time_now_seconds()));
                    seekfan_let_go(&app);
                    speed_tile_release(&app, false);
                    app.titlebar_held = RUBRAVIEW_TITLEBAR_NONE;
                    if (app.fav_press >= 0) {
                        size_t from = (size_t)app.fav_press;
                        if (app.fav_dragging && from < app.favorites.count) {
                            int32_t fw = 0, fh = 0;
                            rubraview_pal_window_get_size(app.window, &fw, &fh);
                            size_t to = favorite_drop_index(&app, event.mouse.x, rubraview_pal_window_dpi_scale(app.window), (double)fw);
                            if (to != from && rubraview_favorites_move(&app.favorites, from, to)) {
                                favorites_save(&app);
                                osd_say(&app, TU8("favourite moved"));
                            }
                        } else if (from < app.favorites.count) {
                            rubraview_fs_entry_t st;   /* 1.24.0: a file among them opens in the viewer */
                            proven_u8str_view_t path = app_keep(&app, app.favorites.paths[from]);
                            if (rubraview_pal_fs_stat(app.arena, path, &st) && !st.is_directory) { app.picker_open = false; open_path(&app, path); }
                            else { picker_navigate(&app, path); app.picker.focus = 0; }
                        }
                        app.fav_press = -1;
                        app.fav_dragging = false;
                        break;
                    }
                    if (app.eqwin.dragging || app.eqwin.sliding >= 0) { app.eqwin.dragging = false; app.eqwin.sliding = -1; break; }
                    if (recentwin_release(&app)) break;
                    if (favwin_release(&app)) break;
                    if (listwin_release(&app)) break;
                    if (app.subbox.dragging != RUBRAVIEW_SUBBOX_NONE) {
                        rubraview_subbox_end_drag(&app.subbox);
                        break;
                    }
                    app.curve_dragging = -1;
                    rubraview_panel_release(&app.sel_panel);
                    rubraview_panel_release(&app.steps_panel);
                    rubraview_panel_release(&app.classify_panel);
                    app.panel.moving = false;
                    if (app.sel_grab) {
                        app.sel_grab = 0;
                        if (edit_panel_open(&app)) panel_build_edit(&app);   /* "Apply to the selection" */
                        break;
                    }
                    if (app.crop_dragging) {
                        app.crop_dragging = false;
                        app.edit_rev++;
                        int32_t ix = 0, iy = 0;
                        if (crop_point_to_image(&app, event.mouse.x, event.mouse.y, &ix, &iy)) {
                            rubraview_edit_crop_drag(&app.edit, app.crop_drag_x0, app.crop_drag_y0, ix, iy);
                        }
                        /* A click without a drag clears the rectangle rather
                           than leaving a one-pixel crop behind. */
                        if (app.edit.crop.width < 4 || app.edit.crop.height < 4) {
                            app.edit.crop_active = false;
                        }
                        crop_shown_again(&app);
                        if (edit_panel_open(&app)) panel_build_edit(&app);
                        edit_sel_sync(&app);
                        break;
                    }
                    if (app.pagebar_dragging && !app.pagebar_detached) {
                        pagebar_drag(&app, event.mouse.x, true);
                        break;
                    }
                    rubraview_panel_release(&app.panel);
                    box_release(&app, event.mouse.x, event.mouse.y);
                    break;

                case RUBRAVIEW_WINDOW_EVENT_MOUSE_WHEEL: {
                    if (chooser_wheel(&app, CHOOSE_IN_MAIN, event.mouse.wheel_delta)) break;   /* 1.24.0 */
                    if (app.picker_open) {
                        picker_layout(&app);
                        rubraview_picker_scroll_by(&app.picker, -event.mouse.wheel_delta * app.picker.tile_extent *
                                                                (picker_details(&app) ? 3.0 : 0.5));
                        break;
                    }
                    if (box_wheel_opacity(&app, event.mouse.x, event.mouse.y, event.mouse.wheel_delta, event.mouse.modifiers)) break;
                    {   /* 1.25.0: over the toolbar's bar or its name, the wheel sizes the name and the messages */
                        rubraview_tile_metrics_t wm = tile_metrics(rubraview_pal_window_dpi_scale(app.window));
                        rubraview_rect_t bar = rubraview_box_timeline_rect(&app.toolbox, &wm), title = rubraview_box_title_rect(&app.toolbox, &wm);
                        /* the band from a little over the bar down to the name's line (VM: the bar alone is a hairline) */
                        double reach = 8.0 * rubraview_pal_window_dpi_scale(app.window);
                        rubraview_rect_t band = bar;
                        if (bar.width > 0.0) {
                            double bottom = title.height > 0.0 ? title.y + title.height : bar.y + bar.height + reach;
                            band = (rubraview_rect_t){ bar.x, bar.y - reach, bar.width, bottom - (bar.y - reach) };
                        }
                        if (app.toolbox.state != RUBRAVIEW_BOX_DETACHED &&
                            (rubraview_rect_contains(band, event.mouse.x, event.mouse.y) || rubraview_rect_contains(title, event.mouse.x, event.mouse.y))) {
                            double pct = rubraview_settings_get(&app.settings, PROVEN_LIT("display"), PROVEN_LIT("name_scale")) + (event.mouse.wheel_delta > 0 ? 10.0 : -10.0);
                            rubraview_settings_set(&app.settings, PROVEN_LIT("display"), PROVEN_LIT("name_scale"), pct);
                            char line[48];
                            int n = snprintf(line, sizeof(line), T("names and messages %d%%"), (int)lround(name_scale(&app) * 100.0));
                            if (n > 0) osd_say(&app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
                            break;
                        }
                    }
                    {
                        /* The owner's edit: a box taller than the window scrolls. */
                        rubraview_panel_t *boxes[4] = { &app.classify_panel, &app.steps_panel, &app.sel_panel, &app.panel };
                        bool took = false;
                        for (int b = 0; b < (app.edit_window ? 1 : 4) && !took; ++b) {   /* RV-137: Edit's scroll in its window */
                            if (!boxes[b]->open || !rubraview_rect_contains(boxes[b]->bounds, event.mouse.x, event.mouse.y)) continue;
                            rubraview_panel_scroll_by(boxes[b], -event.mouse.wheel_delta * boxes[b]->row_height * 3.0);
                            took = true;
                        }
                        if (took) break;
                    }
                    if (app.eqwin.open && eqwin_wheel(&app, event.mouse.x, event.mouse.y, event.mouse.wheel_delta)) break;
                    if (app.favwin.open && rubraview_rect_contains(rubraview_listwin_rect(&app.favwin), event.mouse.x, event.mouse.y)) {
                        rubraview_listwin_scroll(&app.favwin, event.mouse.wheel_delta > 0 ? -3 : 3, app.favorites.count);
                        break;
                    }
                    if (app.recentwin.open && rubraview_rect_contains(rubraview_listwin_rect(&app.recentwin), event.mouse.x, event.mouse.y)) {
                        rubraview_listwin_scroll(&app.recentwin, event.mouse.wheel_delta > 0 ? -3 : 3, app.recent_row_count);
                        break;
                    }
                    if (app.listwin.open && rubraview_rect_contains(rubraview_listwin_rect(&app.listwin), event.mouse.x, event.mouse.y)) {
                        rubraview_listwin_scroll(&app.listwin, event.mouse.wheel_delta > 0 ? -3 : 3, list_rows(&app));
                        break;
                    }
                    /* 1.25.0: a tile first — over the text too (VM: the wheel on UTF8 scrolled nothing and chose nothing) */
                    if (event.mouse.modifiers == 0 &&
                        zoom_tile_wheel(&app, event.mouse.x, event.mouse.y, event.mouse.wheel_delta)) break;
                    if (text_wheel(&app, event.mouse.wheel_delta, event.mouse.modifiers)) break;   /* RFC-0005 */
                    if (strip_active(&app) && event.mouse.modifiers == 0) {
                        /* The wheel moves the strip, the way it moves any long page. */
                        strip_scroll_by(&app, -event.mouse.wheel_delta * 120.0 * rubraview_pal_window_dpi_scale(app.window));
                        note_activity(&app);
                        break;
                    }
                    if (event.mouse.modifiers == 0 && !app.media && strip_shown(&app) > 0.0 &&
                        pal_rect_has(strip_rect(&app), event.mouse.x, event.mouse.y)) {
                        /* Over the strip the wheel goes through the thumbnails, a page a notch. */
                        apply_intent(&app, event.mouse.wheel_delta > 0.0 ? RUBRAVIEW_INTENT_PREV_PAGE : RUBRAVIEW_INTENT_NEXT_PAGE);
                        strip_peek(&app);
                        break;
                    }
                    rubraview_pointer_context_t ctx = pointer_context(&app);
                    uint32_t wheel_mods = event.mouse.modifiers;
                    /* D-109: while a film or music plays the plain wheel is the volume
                       (Settings › Video › Wheel is the volume while playing); with that
                       off it zooms a film, as Ctrl + wheel does, as it did before. */
                    if (app.media && wheel_mods == 0) {
                        if (rubraview_settings_get(&app.settings, PROVEN_LIT("video"), PROVEN_LIT("wheel_volume")) > 0.5) {
                            handle_action(&app, event.mouse.wheel_delta > 0.0 ? PROVEN_LIT("media_volume_up") : PROVEN_LIT("media_volume_down"));
                            note_activity(&app);
                            break;
                        }
                        if (app.media_info.has_video) wheel_mods = RUBRAVIEW_MOD_CTRL;
                    }
                    rubraview_pointer_intent_t wheel_intent = rubraview_pointer_wheel(&ctx, event.mouse.wheel_delta, wheel_mods);
                    if (wheel_intent == RUBRAVIEW_INTENT_SCROLL_VERTICAL) {
                        /* Both ways: it only ever moved the page one way, whichever way the wheel turned. */
                        app.pan_y += event.mouse.wheel_delta > 0.0 ? PAN_STEP : -PAN_STEP;
                        note_activity(&app);
                        break;
                    }
                    apply_intent(&app, wheel_intent);
                    if (wheel_intent == RUBRAVIEW_INTENT_NEXT_PAGE || wheel_intent == RUBRAVIEW_INTENT_PREV_PAGE) strip_peek(&app);
                    break;
                }

                case RUBRAVIEW_WINDOW_EVENT_GESTURE_ZOOM:
                    /* §3.6.5: pinch-to-zoom about the gesture centroid. */
                    app.zoom *= event.gesture.scale_ratio;
                    if (app.zoom < 0.01) app.zoom = 0.01;
                    note_activity(&app);
                    break;

                case RUBRAVIEW_WINDOW_EVENT_DROP:
                case RUBRAVIEW_WINDOW_EVENT_OPEN_REQUEST:
                    handle_drop(&app, &event);
                    break;

                case RUBRAVIEW_WINDOW_EVENT_GESTURE_PAN:
                    if (app.picker_open) {
                        rubraview_picker_scroll_by(&app.picker, -event.gesture.dy);
                    } else {
                        if (!app.swipe_going) {
                            app.swipe_going = true;
                            app.swipe_dx = app.swipe_dy = 0.0;
                            app.swipe_pan_x = app.pan_x;
                            app.swipe_pan_y = app.pan_y;
                        }
                        app.swipe_dx += event.gesture.dx;
                        app.swipe_dy += event.gesture.dy;
                        app.pan_x += event.gesture.dx;
                        app.pan_y += event.gesture.dy;
                    }
                    note_activity(&app);
                    break;

                case RUBRAVIEW_WINDOW_EVENT_GESTURE_END: {
                    /* RV-107 (§3.7.4): a finger drawn sideways over a page that has nowhere
                       to go — fitted, not the strip — turns it; the page goes back where it was. */
                    bool was = app.swipe_going;
                    app.swipe_going = false;
                    if (!was || app.picker_open || app.zoom > 1.001 || strip_active(&app) || page_count(&app) < 2) break;
                    double across = app.swipe_dx < 0.0 ? -app.swipe_dx : app.swipe_dx;
                    double down = app.swipe_dy < 0.0 ? -app.swipe_dy : app.swipe_dy;
                    if (across < 2.0 * down) break;
                    rubraview_pointer_context_t ctx = { .reading_direction = app.layout_opts.direction };
                    rubraview_pointer_intent_t intent =
                        rubraview_pointer_swipe(&ctx, app.swipe_dx, 64.0 * rubraview_pal_window_dpi_scale(app.window));
                    if (intent != RUBRAVIEW_INTENT_NEXT_PAGE && intent != RUBRAVIEW_INTENT_PREV_PAGE) break;
                    app.pan_x = app.swipe_pan_x;
                    app.pan_y = app.swipe_pan_y;
                    handle_action(&app, intent == RUBRAVIEW_INTENT_NEXT_PAGE ? PROVEN_LIT("next_page") : PROVEN_LIT("prev_page"));
                    break;
                }

                default:
                    break;
            }
        }

        /* §3.22 / D-13: the settings window's own queue. Polling the main
           window pumped the thread's messages into it. */
        handled += shell_requests_pump(&app);
        handled += settings_pump(&app);
        handled += help_pump(&app);
        handled += info_pump(&app);
        handled += mini_pump(&app);
        handled += files_pump(&app);
        handled += fontpick_pump(&app);
        handled += edit_pump(&app);
        handled += subs_pump(&app);

        if (app.needs_relayout) {
            int32_t page = current_page_index(&app);
            rebuild_layout(&app);
            if (page >= 0) app.spread_index = spread_index_for_page(&app, page);
            if (page >= 0 && current_page_index(&app) != page) {
                /* D-98: the page that was on screen is left out now: the one in its place is prepared as a turn would. */
                animation_prepare(&app);
                media_prepare(&app);
                update_window_title(&app);
            }
        }
        if (strip_active(&app)) {
            /* Whatever panned the page up or down — a drag, a touch, a key —
               moved the strip; with nothing to move, this keeps it inside its ends. */
            double dy = app.pan_y;
            app.pan_y = 0.0;
            strip_scroll_by(&app, -dy);
        }

        double now = rubraview_pal_time_now_seconds();
        double dt = now - app.last_frame_seconds;
        if (dt < 0.0) dt = 0.0;
        app.last_frame_seconds = now;
        tick_timers(&app, dt);
        sub_tile_result(&app, rubraview_tap_tick(&app.sub_tap, now));   /* D-33: a tap decided, or a hold */
        if (rubraview_seekfan_tick(&app.seekfan, now)) app.toolbox_window_drawn = 0.0;   /* held: its choices open */
        /* D-34: thumbnails for the picker's tiles, a few a pass; given back when it closes. */
        if (app.picker_open) {
            picker_thumbs_step(&app);
        } else if (app.picker_thumbs) {
            picker_thumbs_free(&app);
            app.picker_generation++;   /* what is still being made is for nobody */
            if (app.thumbs) rubraview_pal_thumbs_generation(app.thumbs, app.picker_generation);
        }
        strip_step(&app, dt);   /* the wheel's strip: slide, fade, thumbnails */
        tiles_take_all(&app);   /* D-40 */
        save_job_poll(&app, false);
        apply_all_poll(&app, false);
        if (app.edit_after[0]) {   /* a film's frame saved for Edit: opened once it is on screen */
            int32_t page = current_page_index(&app);
            if (page >= 0 && app.pages[page].loaded && rubraview_path_same(app.source.pages[page].path, cstr(app.edit_after))) {
                app.edit_after[0] = '\0';
                if (!edit_panel_open(&app)) panel_open_edit(&app);
            }
        }
        arc_job_poll(&app, false);
        page_job_poll(&app);
        arena_refill(&app);
        memory_watch(&app, rubraview_pal_time_now_seconds());
        if (app.media_skip_pending) {
            /* D-9: the file nothing could open was reported; move past it. */
            app.media_skip_pending = false;
            next_spread(&app);
        }
        station_job_poll(&app);
        station_title_poll(&app);   /* RV-158 */
        gmic_list_poll(&app);   /* RFC-0009 C */
        media_tick(&app);
        bgm_tick(&app);

        /* Redraw only while something can change on screen. Without a
           GPU, Direct2D rasterises on the CPU, and a loop that redrew
           the same still image kept two cores busy doing it. */
        /* D-82: an animation is paced as a film is — a frame when it has a
           new picture, not on every pass (VM, 2026-10-09: every pass kept
           two cores busy on a GIF and the keys waited behind it). */
        bool media_playing = (app.media && !app.media_paused) || (anim_plays(&app) && !app.animation.paused);
        text_tick(&app, now);   /* RFC-0005: auto-scroll moves by the time since the last pass */
        /* D-154: two copies with two Windows 11 menus, said once (ever). */
        if (!app.copies_told && (app.packaged || app.other_copies.store) && app.other_copies.msi_menu) {
            app.copies_told = true;
            if (rubraview_settings_get(&app.settings, PROVEN_LIT("shell"), PROVEN_LIT("told_twice")) < 0.5) {
                rubraview_settings_set(&app.settings, PROVEN_LIT("shell"), PROVEN_LIT("told_twice"), 1.0);
                osd_say(&app, TU8("Rubraview is installed twice (MSI and Store): Windows 11's menu shows two; Settings › Explorer says what to do"));
            }
        }
        bool others_moving = app.slideshow_running || text_moving(&app) ||
                             app.notice_seconds > 0.0 || app.pending_decode_count > 0 ||
                             app.rename_active ||   /* the caret blinks (owner, 2026-09-23) */
                             rubraview_tap_waiting(&app.sub_tap) ||   /* D-33: a tap or a hold being decided */
                             app.seekfan.down ||                      /* a seek button held: its choices open and light up */
                             app.subbox.dragging != RUBRAVIEW_SUBBOX_NONE ||
                             app.picker_thumbs_arrived ||   /* D-34: a picture came in; the thread wakes the loop */
                             app.strip_arrived ||           /* a strip thumbnail came in */
                             (!app.filmstrip.visible && strip_shown(&app) > 0.0) ||   /* the wheel's strip is up or fading */
                             app.filmstrip.scroll_offset != app.strip_target ||      /* it slides */
                             app.tiles_arrived;             /* D-40: a sharper tile came in */
        if (handled > 0) last_input_seconds = now;
        if (handled > 0 || media_playing || others_moving) last_busy_seconds = now;
        bool settled = now - last_busy_seconds > IDLE_REDRAW_GRACE;
        /* Only the film or the sound moves: draw a new picture when one
           is up and the seek bar four times a second, not on every pass
           (T058 on the VM: every pass kept 1.75 of 2 cores busy on a
           sound-only page, and the keys waited behind it). For the grace
           after an input every pass still draws, as the chrome's fades
           expect. */
        /* The analyser moves some thirty times a second while a song plays on its page. */
        if (visualizer_live(&app) && now - app.viz_last >= 1.0 / 30.0) app.media_new_picture = true;
        bool paced = media_playing && !others_moving && now - last_input_seconds > IDLE_REDRAW_GRACE;
        /* Settled: a frame when what it would show has changed, checked
           each pass, and one every ten seconds whatever the summary says. */
        bool draw = paced ? rubraview_media_redraw_due(app.media_new_picture, now - last_render_seconds)
                          : (!settled || now - last_idle_frame_seconds >= 10.0 ||
                             scene_signature(&app) != last_scene_signature);
        if (draw) {
            render_frame(&app);
            last_scene_signature = scene_signature(&app);
            last_render_seconds = now;
            app.media_new_picture = false;
            if (settled) last_idle_frame_seconds = now;
        }
        if (app.settings_open && now - app.settings_info_seconds >= 1.0) {
            /* `info` lines (cache in use, the adapter) are live data: once a second. */
            app.settings_info_seconds = now;
            app.settings_dirty = true;
        }
        draw_settings_window(&app);
        draw_help_window(&app);
        draw_info_window(&app);
        /* The strip moves while the track does, so the little window is
           redrawn each pass it is playing. */
        if (app.mini_open && !mini_paused(&app)) app.mini_dirty = true;
        draw_mini_window(&app);
        files_preview_step(&app, now);
        draw_files_window(&app);
        draw_fontpick_window(&app);
        draw_edit_window(&app);
        takeout_step(&app);
        putin_step(&app);
        draw_subs_window(&app);
        toolbox_window_pump(&app);
        draw_toolbox_window(&app);

        page_failure_notice(&app);   /* RV-110 */
        int config_changed = rubraview_pal_instance_take_config_changed();   /* D-105 */
        if (config_changed) settings_follow(&app, config_changed);
        settings_share(&app, rubraview_pal_time_now_seconds());

        /* A queued decode takes the loop's idle slice. With nothing to
           get ready, a settled loop sleeps until the OS has something
           for it instead of spinning. */
        if (drain_one_pending_decode(&app)) {
            last_busy_seconds = now;
        } else if (settled && settings_pump(&app) == 0) {
            rubraview_pal_window_wait_event(app.window, 250);
        } else if (paced && !app.media_info.has_video) {
            /* Sound only: nothing is due before the next redraw, but the
               end and the A-B point are checked at least every 20 ms. */
            double wait = rubraview_media_redraw_wait(now - last_render_seconds) * 1000.0;
            rubraview_pal_window_wait_event(app.window, wait < 1.0 ? 1u : wait > 20.0 ? 20u : (uint32_t)wait);
        } else {
            rubraview_pal_time_sleep_ms(4);
        }
    }

    /* §3.22: closing the viewer with the settings window open still
       writes what was changed in it. */
    settings_close(&app);
    files_close(&app);
    subs_close(&app);
    takeout_end(&app);
    putin_end(&app, false);
    settings_write_if_changed(&app, false);   /* a volume or opacity changed with the window shut */
    if (app.toolbox_window) {
        rubraview_pal_render_destroy(app.toolbox_renderer);
        rubraview_pal_window_destroy(app.toolbox_window);
    }
    if (app.settings_window) {
        rubraview_pal_render_destroy(app.settings_renderer);
        rubraview_pal_window_destroy(app.settings_window);
    }

    /* §3.6 / §3.17.1: remember where the reader stopped, and where the
       boxes were left, before shutting down. */
    layout_save(&app);
    text_close(&app);   /* RFC-0005: its place is kept */
    history_remember(&app);
    if (app.history_path.size > 0 && app.history.count > 0) {
        /* D-99: other viewers wrote their places since this one read the file:
           theirs and this one's are put together, each book at its newer place. */
        bool locked = rubraview_pal_config_lock(CONFIG_LOCK_MS);
        rubraview_history_t all = rubraview_history_parse(arena, rubraview_pal_fs_read_file(arena, app.history_path, 1024u * 1024u));
        rubraview_history_merge(arena, &all, &app.history);
        proven_u8str_view_t skip_dirs[RECENT_SKIP_DIRS];
        (void)rubraview_history_drop_within(&all, skip_dirs, recent_skip_dirs(&app, skip_dirs));   /* D-104 */
        rubraview_history_prune(&all, HISTORY_MAX_ENTRIES);
        rubraview_pal_fs_write_file_atomic(app.history_path, rubraview_history_serialize(arena, &all));
        if (locked) rubraview_pal_config_unlock();
    }

    picker_thumbs_free(&app);
    rubraview_pal_thumbs_stop(app.thumbs);   /* D-34: before the renderer and the window go */
    rubraview_pal_thumbs_stop(app.strip_thumbs);
    strip_cells_free(&app);
    tiles_reset(&app);                       /* D-40: its textures, then its thread */
    save_job_poll(&app, true);   /* a copy being written is finished, not cut off */
    apply_all_poll(&app, true);  /* the copy in hand is finished, the rest left */
    arc_job_poll(&app, true);    /* D-88: an archive being written again is stopped; the old one stands */
    arc_undo_drop(&app);         /* D-89: the kept file of the last edit goes with the program */
    print_temp_drop(&app);
    for (size_t i = 0; i < TILE_SETS; ++i) rubraview_pal_tiles_stop(app.tile_sets[i].tiles);
    rubraview_pal_gpu_resample_stop();       /* D-38 */
    app.thumbs = NULL;
    if (app.jobs) {
        proven_job_system_close(app.jobs);
        proven_job_system_destroy(app.jobs);
        app.jobs = NULL;
    }

    media_close(&app);
    unload_all_pages(&app);
    source_close(&app);
    secure_wipe(app.passwords, sizeof(app.passwords));   /* owner, 2026-10-01: memory only, and not after */
    rubraview_pal_render_destroy(app.renderer);
    rubraview_pal_window_destroy(app.window);
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
