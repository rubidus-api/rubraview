/*
 * RV-314 (owner, 2026-11-20: "폴더 위치 지정하는거도 외부 윈api 보다는 내부 파일관리창으로 지정할 수 있게 ... 파일 선택도 내부
 * api로. 파일 내부 관리와 공통 루틴으로"; approach B, D-384): a folder, files or a name to save as, chosen in Rubraview's
 * own open-file screen, run as a process of its own (`--choose <request in hex>`), its foot holding the kind of file,
 * the name (saving), Windows' window, Cancel and Choose / Open / Save. The asker goes on; the answer comes back to its
 * window (WM_COPYDATA) and to the callback it gave. One file for both sides: the asker's half, then the chooser's.
 */
#include "app.h"
#include "rubraview/choose.h"

/* ---- the asker ---- */

#define CHOOSE_PENDING 8
static struct { uint32_t id; choose_done_fn done; void *ctx; } g_pending[CHOOSE_PENDING];
static uint32_t g_next_id;

bool choose_ask(app_state_t *app, const choose_opts_t *o, choose_done_fn done, void *ctx) {
    size_t slot = CHOOSE_PENDING;
    for (size_t i = 0; i < CHOOSE_PENDING && slot == CHOOSE_PENDING; ++i) if (!g_pending[i].done) slot = i;
    if (slot == CHOOSE_PENDING || !o || !done) { osd_say(app, TU8("too many choosing windows are open")); return false; }
    rubraview_choose_request_t q = {
        .mode = (uint8_t)o->mode, .id = ++g_next_id, .reply_to = (uint64_t)(uintptr_t)rubraview_pal_window_native_handle(app->window),
        .over = (uint64_t)(uintptr_t)o->over,
        .title = cstr(o->title ? o->title : ""), .start_dir = o->start_dir, .name = o->name, .filter = o->filter,
    };
    for (size_t i = 0; i < o->filter_count && i < RUBRAVIEW_CHOOSE_FILTERS; ++i) {
        q.filter_name[i] = cstr(o->filters[i].description ? o->filters[i].description : "");
        q.filter_pattern[i] = cstr(o->filters[i].pattern ? o->filters[i].pattern : "");
        q.filter_count++;
    }
    rubraview_jobwire_out_t out = { 0 };
    rubraview_choose_put_request(&out, &q);
    char hex[16384], line[17000];
    bool ok = !out.failed && out.len > 0 && rubraview_choose_hex(out.data, out.len, hex, sizeof hex);
    rubraview_jobwire_out_free(&out);
    proven_u8str_view_t exe = rubraview_pal_process_executable(app->arena);
    int k = ok && exe.size ? rubraview_fmt(line, sizeof line, "\"{}\" --choose {}", PROVEN_ARG(exe), PROVEN_ARG(hex)) : -1;
    if (k <= 0 || (size_t)k >= sizeof line ||
        !rubraview_pal_process_start_apart((proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)k })) {
        osd_say(app, TU8("the choosing window could not be started"));
        return false;
    }
    g_pending[slot].id = q.id;
    g_pending[slot].done = done;
    g_pending[slot].ctx = ctx;
    return true;
}

/* Once a pass of the main loop: the answers come in; the chooser's own end is watched. */
size_t choose_pump(app_state_t *app) {
    size_t handled = 0;
    char *blob = NULL;
    size_t len = 0;
    while (rubraview_pal_choose_take(&blob, &len)) {
        rubraview_choose_answer_t a;
        rubraview_jobwire_msg_t msg;
        if (rubraview_choose_get_answer((const uint8_t *)blob, len, &a, &msg)) {
            for (size_t i = 0; i < CHOOSE_PENDING; ++i) {
                if (!g_pending[i].done || g_pending[i].id != a.id) continue;
                choose_done_fn done = g_pending[i].done;
                void *ctx = g_pending[i].ctx;
                g_pending[i].done = NULL;
                proven_u8str_view_t *paths = a.count ? (proven_u8str_view_t *)rubraview_malloc_product(a.count, sizeof *paths, 1) : NULL;
                size_t n = 0;
                for (uint32_t p = 0; paths && p < a.count; ++p) paths[n++] = app_keep(app, rubraview_jobwire_get_str(&msg));
                done(app, ctx, a.accepted && !msg.bad && n > 0, paths, n, a.filter);
                rubraview_free(paths);
                break;
            }
        }
        rubraview_free(blob);
        handled++;
    }
    handled += choose_watch(app);
    return handled;
}

/* ---- the chooser: Rubraview started with --choose ---- */

static struct {
    bool on, answered, armed;          /* armed: Save pressed once over a name already there */
    rubraview_choose_request_t req;
    uint8_t raw[8192];                 /* the request's bytes */
    char strs[12288];                  /* its strings again, each ended by a NUL, which the request's views point into */
    size_t strs_at;
    uint32_t filter;
    rubraview_editctl_t name_ec;
    char name_buf[1024], name_comp[64];
    editbox_view_t name_view;
    bool name_focus;
    double armed_at;
    rubraview_pal_rect_t kind_rect, name_rect;
} C;

bool choose_mode_take(const wchar_t *hex) {
    char h8[17000];
    int n = WideCharToMultiByte(CP_UTF8, 0, hex, -1, h8, (int)sizeof h8, NULL, NULL);
    size_t got = 0;
    if (n <= 1 || !rubraview_choose_unhex((proven_u8str_view_t){ .ptr = (const proven_byte_t *)h8, .size = (size_t)n - 1 }, C.raw, sizeof C.raw, &got) ||
        !rubraview_choose_get_request(C.raw, got, &C.req))
        return false;
    /* each string again with its NUL: they are handed on as C strings (a title, Windows' filters) */
    proven_u8str_view_t *all[3 + 2 * RUBRAVIEW_CHOOSE_FILTERS] = { &C.req.title, &C.req.start_dir, &C.req.name };
    size_t nall = 3;
    for (uint32_t i = 0; i < C.req.filter_count; ++i) { all[nall++] = &C.req.filter_name[i]; all[nall++] = &C.req.filter_pattern[i]; }
    for (size_t i = 0; i < nall; ++i) {
        if (C.strs_at + all[i]->size + 1 > sizeof C.strs) return false;
        char *at = C.strs + C.strs_at;
        proven_sys_mem_copy(at, all[i]->ptr, all[i]->size);
        at[all[i]->size] = '\0';
        C.strs_at += all[i]->size + 1;
        all[i]->ptr = (const proven_byte_t *)at;
    }
    C.on = true;
    C.filter = C.req.filter;
    rubraview_pal_window_helper_process();   /* its window never takes another launch's request (RV-312) */
    return true;
}

bool choose_mode_on(void) { return C.on; }

bool choose_lists(proven_u8str_view_t name, bool is_directory) { return rubraview_choose_shows(&C.req, C.filter, name, is_directory); }

static void answer(app_state_t *app, bool accepted, const proven_u8str_view_t *given, size_t count) {
    if (C.answered) return;
    /* the owner, 2026-11-20: "확인을 누르면 원래 어머니 프로세스가 살아있는지 확인하고 경로명을 메시지로 보내는 것으로. 취소일 때에는
       취소 메시지 전송": a choice for a window gone (or not answering) is not sent and the window stays, saying so */
    if (accepted && !rubraview_pal_window_alive(C.req.reply_to)) {
        osd_say(app, TU8("the window that asked is gone: there is nothing to send this to (Cancel closes)"));
        return;
    }
    C.answered = true;
    proven_u8str_view_t *paths = count ? (proven_u8str_view_t *)rubraview_malloc_product(count, sizeof *paths, 1) : NULL;
    for (size_t i = 0; paths && i < count; ++i) {   /* Windows' separators, as its own dialog gives them */
        proven_result_mem_mut_t m = proven_arena_alloc(app->arena, given[i].size + 1);
        if (!proven_is_ok(m.err)) { paths[i] = given[i]; continue; }
        char *p = (char *)(void *)m.value.ptr;
        for (size_t k = 0; k < given[i].size; ++k) p[k] = given[i].ptr[k] == '/' ? '\\' : (char)given[i].ptr[k];
        p[given[i].size] = '\0';
        paths[i] = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)p, .size = given[i].size };
    }
    if (count && !paths) count = 0;
    rubraview_jobwire_out_t out = { 0 };
    rubraview_choose_put_answer(&out, &(rubraview_choose_answer_t){ .id = C.req.id, .accepted = accepted, .filter = C.filter }, paths, count);
    bool sent = !out.failed && out.len && rubraview_pal_choose_send(C.req.reply_to, (proven_u8str_view_t){ .ptr = out.data, .size = out.len });
    if (accepted && !sent) {   /* it went away or hung as it was sent: kept, said */
        C.answered = false;
        rubraview_jobwire_out_free(&out);
        rubraview_free(paths);
        osd_say(app, TU8("the window that asked did not take it: try again, or Cancel"));
        return;
    }
    rubraview_jobwire_out_free(&out);
    rubraview_free(paths);
    rubraview_pal_window_request_close(app->window);
}

void choose_end(app_state_t *app) { if (C.on) answer(app, false, NULL, 0); }   /* closed without an answer: cancelled */

bool choose_title(app_state_t *app) {
    if (!C.on) return false;
    rubraview_pal_window_set_title(app->window, C.req.title.size ? (const char *)C.req.title.ptr : T("Rubraview - choose"));
    return true;
}

static proven_u8str_view_t kind_pattern(void) {
    return C.req.filter_count && C.filter < C.req.filter_count ? C.req.filter_pattern[C.filter] : PROVEN_LIT("");
}

static bool real_folder(app_state_t *app, proven_u8str_view_t dir) {
    rubraview_fs_entry_t st;
    return dir.size > 0 && !rubraview_picker_is_places(dir) && !picker_in_archive(app) && !app->picker_in_book &&
           rubraview_pal_fs_stat(app->arena, dir, &st) && st.is_directory;
}

static proven_u8str_view_t choose_real_dir(app_state_t *app, proven_u8str_view_t dir) {
    return rubraview_picker_is_desktop(dir) ? rubraview_pal_fs_desktop_dir(app->arena) : dir;
}

static void name_set(proven_u8str_view_t text) {
    rubraview_textedit_set(&C.name_ec.te, text.size < sizeof C.name_buf ? text : PROVEN_LIT(""));
    rubraview_textedit_select_all(&C.name_ec.te);
    C.armed = false;
}

void choose_begin(app_state_t *app) {
    if (!C.on) return;
    if (C.req.mode == RUBRAVIEW_CHOOSE_SAVE) {
        editbox_begin(&C.name_ec, C.name_buf, sizeof C.name_buf, false);
        name_set(C.req.name);
        C.name_focus = true;
        rubraview_pal_window_text_input(app->window, true);
    }
    choose_title(app);
    rubraview_pal_window_place_over(app->window, C.req.over ? C.req.over : C.req.reply_to, 980, 700);
    if (C.req.start_dir.size) app->picker_dir = app_keep(app, C.req.start_dir);
    picker_open(app);
    app->picker_open = true;
}

/* The answer from what is shown and picked; false (said why) when there is none yet. */
static void commit(app_state_t *app) {
    proven_u8str_view_t dir = app->picker_dir;
    size_t n = app->picker_listing.count, picked = 0;
    rubraview_picker_operand_t op = picker_operand(app);   /* R364: the ticked, else the focus */
    for (size_t i = 0; i < n; ++i) if (picker_operand_has(app, &op, i)) picked++;
    if (C.req.mode == RUBRAVIEW_CHOOSE_SAVE) {
        proven_u8str_view_t save_dir = choose_real_dir(app, dir);
        if (!real_folder(app, save_dir)) { osd_say(app, TU8("open the folder to save in first")); return; }
        char name[1024];
        size_t k = rubraview_choose_save_name(rubraview_textedit_text(&C.name_ec.te), kind_pattern(), name, sizeof name);
        if (k == 0) { osd_say(app, TU8("type a name a file can have")); C.name_focus = true; return; }
        proven_u8str_view_t path = rubraview_path_join(app->arena, save_dir, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)name, .size = k });
        rubraview_fs_entry_t st;
        if (rubraview_pal_fs_stat(app->arena, path, &st)) {
            if (st.is_directory) { osd_say(app, TU8("a folder has that name")); return; }
            double now = rubraview_pal_time_now_seconds();
            if (!C.armed || now - C.armed_at > 5.0) {   /* asked once: the second Save replaces it */
                C.armed = true;
                C.armed_at = now;
                osd_say(app, TU8("a file of that name is there: Save again to replace it"));
                return;
            }
        }
        answer(app, true, &path, 1);
        return;
    }
    if (C.req.mode == RUBRAVIEW_CHOOSE_FOLDER) {
        proven_u8str_view_t chosen = choose_real_dir(app, dir);
        for (size_t i = 0; i < n; ++i)   /* a folder picked: that one */
            if (picker_operand_has(app, &op, i) && app->picker_listing.entries[i].is_directory) { chosen = app->picker_listing.entries[i].path; break; }
        if (!real_folder(app, chosen)) { osd_say(app, TU8("open a folder of the disk first")); return; }
        answer(app, true, &chosen, 1);
        return;
    }
    /* a file or files: those picked, else the one with the focus */
    proven_u8str_view_t *paths = (proven_u8str_view_t *)rubraview_malloc_product(picked ? picked : 1, sizeof *paths, 1);
    size_t got = 0;
    for (size_t i = 0; paths && i < n; ++i) {
        const rubraview_fs_entry_t *e = &app->picker_listing.entries[i];
        if (!picker_operand_has(app, &op, i) || e->is_directory) continue;
        paths[got++] = e->path;
        if (C.req.mode == RUBRAVIEW_CHOOSE_FILE) break;
    }
    if (paths && got == 0 && app->picker.focus < n && !app->picker_listing.entries[app->picker.focus].is_directory)
        paths[got++] = app->picker_listing.entries[app->picker.focus].path;
    if (got == 0 || picker_in_archive(app) || app->picker_in_book) osd_say(app, TU8("pick a file of the disk first"));
    else answer(app, true, paths, got);
    rubraview_free(paths);
}

/* Windows' own window, asked the same, its answer given as ours. */
static void windows_window(app_state_t *app) {
    rubraview_file_filter_t filters[RUBRAVIEW_CHOOSE_FILTERS];
    for (uint32_t i = 0; i < C.req.filter_count; ++i)
        filters[i] = (rubraview_file_filter_t){ .description = (const char *)C.req.filter_name[i].ptr, .pattern = (const char *)C.req.filter_pattern[i].ptr };
    char name[1024];
    size_t k = C.req.mode == RUBRAVIEW_CHOOSE_SAVE ? rubraview_choose_save_name(rubraview_textedit_text(&C.name_ec.te), kind_pattern(), name, sizeof name) : 0;
    name[k] = '\0';
    char here[2048] = "";
    proven_u8str_view_t here_dir = choose_real_dir(app, app->picker_dir);
    if (real_folder(app, here_dir) && here_dir.size < sizeof here) { proven_sys_mem_copy(here, here_dir.ptr, here_dir.size); here[here_dir.size] = '\0'; }
    rubraview_file_dialog_opts_t opts = {
        .title = C.req.title.size ? (const char *)C.req.title.ptr : NULL, .default_dir = here[0] ? here : NULL,
        .filters = C.req.filter_count ? filters : NULL, .filter_count = C.req.filter_count,
        .allow_multi = C.req.mode == RUBRAVIEW_CHOOSE_FILES, .folder_mode = C.req.mode == RUBRAVIEW_CHOOSE_FOLDER,
        .parent_window_handle = rubraview_pal_window_native_handle(app->window),
    };
    rubraview_file_dialog_result_t r = C.req.mode == RUBRAVIEW_CHOOSE_FOLDER ? rubraview_pal_file_dialog_pick_folder(app->arena, &opts)
                                     : C.req.mode == RUBRAVIEW_CHOOSE_SAVE ? rubraview_pal_file_dialog_save(app->arena, &opts)
                                                                           : rubraview_pal_file_dialog_open(app->arena, &opts);
    if (r.accepted && r.count > 0) answer(app, true, r.paths, r.count);
}

/* The foot's own buttons (picker_bottom_list gives them in this mode). */
bool choose_button(app_state_t *app, picker_button_t button) {
    if (!C.on) return false;
    if (button == PICKER_BTN_CANCEL) { answer(app, false, NULL, 0); return true; }
    if (button == PICKER_BTN_OPEN) { commit(app); return true; }
    if (button == PICKER_BTN_WINDOWS) { windows_window(app); return true; }
    return false;
}

/* A file opened (a double tap, Enter): chosen, or its name put in the box; a folder is gone into as ever. */
bool choose_activate(app_state_t *app, const rubraview_fs_entry_t *entry) {
    if (!C.on || entry->is_directory) return false;
    if (C.req.mode == RUBRAVIEW_CHOOSE_SAVE) { name_set(entry->name); C.name_focus = true; rubraview_pal_window_text_input(app->window, true); return true; }
    if (C.req.mode == RUBRAVIEW_CHOOSE_FOLDER) return true;
    if (picker_in_archive(app) || app->picker_in_book) { osd_say(app, TU8("pick a file of the disk first")); return true; }
    proven_u8str_view_t path = entry->path;
    answer(app, true, &path, 1);
    return true;
}

const char *choose_open_word(void) {
    return C.req.mode == RUBRAVIEW_CHOOSE_SAVE ? T("Save") : C.req.mode == RUBRAVIEW_CHOOSE_FOLDER ? T("Choose this folder") : T("Open");
}

/* Its bottom row: Windows' window, Cancel, and Choose / Open / Save at the right; the kind and the name at the left. */
int32_t choose_bottom_list(picker_button_t *out) {
    out[0] = PICKER_BTN_WINDOWS; out[1] = PICKER_BTN_CANCEL; out[2] = PICKER_BTN_OPEN;
    return 3;
}

void choose_draw_foot(app_state_t *app, double win_w, double win_h, double dpi) {
    if (!C.on) return;
    rubraview_pal_rect_t a = picker_button_rect(win_w, win_h, dpi, 0), b = picker_button_rect(win_w, win_h, dpi, 1);
    C.kind_rect = C.req.filter_count > 1 ? (rubraview_pal_rect_t){ a.x, a.y, b.x + b.width - a.x, a.height } : (rubraview_pal_rect_t){ 0 };
    if (C.kind_rect.width > 0) {   /* the kind of file: a press goes to the next */
        bool over = pal_rect_has(C.kind_rect, app->pointer_x, app->pointer_y);
        rubraview_pal_render_fill_rect(app->renderer, C.kind_rect, over ? 0x40FFFFFFu : COLOR_TILE_FILL, 3.0);
        rubraview_pal_render_stroke_rect(app->renderer, C.kind_rect, COLOR_BOX_BORDER, 1.0, 3.0);
        char line[300];
        int k = rubraview_fmt(line, sizeof line, "{} \xE2\x96\xBE", PROVEN_ARG(C.req.filter_name[C.filter]));
        if (k > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)k < sizeof line ? (size_t)k : sizeof line - 1 },
                                                  C.kind_rect, a.height * 0.30, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
    C.name_rect = (rubraview_pal_rect_t){ 0 };
    if (C.req.mode == RUBRAVIEW_CHOOSE_SAVE) {   /* the name, in the slots up to Windows' window */
        rubraview_pal_rect_t from = picker_button_rect(win_w, win_h, dpi, 2), to = picker_button_rect(win_w, win_h, dpi, PICKER_BOTTOM_COUNT - 4);
        if (C.kind_rect.width == 0) from = a;
        C.name_rect = (rubraview_pal_rect_t){ from.x, from.y, to.x + to.width - from.x, from.height };
        rubraview_pal_render_fill_rect(app->renderer, C.name_rect, 0xFF101215u, 3.0);
        rubraview_pal_render_stroke_rect(app->renderer, C.name_rect, C.name_focus ? COLOR_TILE_CURRENT : COLOR_BOX_BORDER, 1.0, 3.0);
        C.name_view.box = (rubraview_pal_rect_t){ C.name_rect.x + 8.0 * dpi, C.name_rect.y + 4.0 * dpi, C.name_rect.width - 16.0 * dpi, C.name_rect.height - 8.0 * dpi };
        C.name_view.size = 14.0 * dpi;
        editbox_draw(app->renderer, &C.name_ec, &C.name_view, proven_u8str_view_from_cstr(C.name_comp), C.name_focus, COLOR_TEXT, dpi);
    }
}

/* Before the screen's own handling: the kind, the name box, Esc, the window's X. True: taken. */
bool choose_event(app_state_t *app, const rubraview_window_event_t *event) {
    if (!C.on) return false;
    switch (event->kind) {
        case RUBRAVIEW_WINDOW_EVENT_CLOSE:
            answer(app, false, NULL, 0);
            return false;
        case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN:
            if (event->mouse.button != RUBRAVIEW_MOUSE_LEFT) return false;
            if (C.kind_rect.width > 0 && pal_rect_has(C.kind_rect, event->mouse.x, event->mouse.y)) {
                C.filter = (C.filter + 1) % C.req.filter_count;
                picker_navigate(app, app->picker_dir);   /* listed again by the kind */
                return true;
            }
            if (C.name_rect.width > 0 && pal_rect_has(C.name_rect, event->mouse.x, event->mouse.y)) {
                C.name_focus = true;
                rubraview_pal_window_text_input(app->window, true);
                (void)editbox_pointer_down(app->renderer, &C.name_ec, &C.name_view, event->mouse.x, event->mouse.y, false);
                return true;
            }
            if (C.req.mode == RUBRAVIEW_CHOOSE_SAVE) C.name_focus = false;   /* the list pressed: keys go to it */
            return false;
        case RUBRAVIEW_WINDOW_EVENT_TEXT:
        case RUBRAVIEW_WINDOW_EVENT_COMPOSITION:
            if (!C.name_focus || app->rename_active) return false;
            {
                proven_u8str_view_t typed = { .ptr = (const proven_byte_t *)event->text.utf8, .size = event->text.length };
                if (event->kind == RUBRAVIEW_WINDOW_EVENT_TEXT) { editbox_text(&C.name_ec, typed, true); C.name_comp[0] = '\0'; C.armed = false; }
                else (void)rubraview_fmt(C.name_comp, sizeof C.name_comp, "{}", PROVEN_ARG(typed));
            }
            return true;
        case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN:
            if (app->rename_active) return false;   /* the screen's own box (a path, a filter, a name) first */
            if (key_is(event->key.combo, "Escape")) { answer(app, false, NULL, 0); return true; }
            if (C.req.mode == RUBRAVIEW_CHOOSE_SAVE && key_is(event->key.combo, "Tab")) {
                C.name_focus = !C.name_focus;
                rubraview_pal_window_text_input(app->window, C.name_focus);
                return true;
            }
            if (C.name_focus) {
                if (key_is(event->key.combo, "Enter")) commit(app);
                else { (void)editbox_key(app->window, &C.name_ec, event->key.combo, false); C.armed = false; }
                return true;
            }
            return false;
        default:
            return false;
    }
}

/* The screen closed some other way (its own Cancel, a key): cancelled. */
size_t choose_watch(app_state_t *app) {
    if (!C.on || C.answered || app->picker_open) return 0;
    answer(app, false, NULL, 0);
    return 1;
}
