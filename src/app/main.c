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

app_page_t *ensure_page_loaded_how(app_state_t *app, int32_t index, bool on_screen);

app_page_t *ensure_page_loaded(app_state_t *app, int32_t index) {
    return ensure_page_loaded_how(app, index, true);
}

/* `on_screen`: the page is wanted now. A far page then goes to the thread;
   the look-ahead never starts a far read, nor waits for one. */
app_page_t *ensure_page_loaded_how(app_state_t *app, int32_t index, bool on_screen) {
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

void next_spread(app_state_t *app);

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
void picture_copy(app_state_t *app) {
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
void show_frame(app_state_t *app, size_t frame_index) {
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

void animation_tick(app_state_t *app, double dt) {
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
bool spread_composition(app_state_t *app, rubraview_composition_t *out);

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
void zoom_cycle(app_state_t *app) {
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
double strip_width(app_state_t *app, double win_w) {
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

double strip_page_height(void *context, size_t index) {
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
double strip_scroll_by(app_state_t *app, double delta) {
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

void next_spread(app_state_t *app) {
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

void prev_spread(app_state_t *app) {
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

void toggle_slideshow(app_state_t *app) {
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
