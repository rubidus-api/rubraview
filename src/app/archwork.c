/*
 * RV-207: extracting and compressing from the open-file screen (D-252). The picked archives are written out, or the
 * picked files and folders packed, on a worker thread of their own while the screen stays usable; a line under the
 * picture says how far it is and Esc stops it. A locked archive asks its password in the password box, once; the
 * Compress box takes the new archive's place and name (its ending says ZIP or 7z) and its level, then a password.
 * Explorer's Rubraview menu hands its files to the same work (archwork_extract_paths, archwork_compress_paths).
 */
#include "app.h"
#include "rubraview/archive_job.h"

typedef struct archwork {
    bool compress;
    /* extracting: the archives in turn */
    char **archives;
    size_t count, at;
    rubraview_extract_mode_t mode;
    size_t *only;                             /* picked inside the archive: its entries (malloc'd); NULL: all */
    size_t only_count;
    char inner[RUBRAVIEW_ARCHIVE_PATH];       /* and the folder they were picked in, "a/b/" */
    char dest[RUBRAVIEW_ARCHIVE_PATH];        /* "" : each beside itself */
    char password[256];
    rubraview_extract_job_t ex;
    uint32_t written, skipped, failed;
    char last_root[RUBRAVIEW_ARCHIVE_PATH];
    /* compressing */
    char **sources;
    size_t source_count;
    rubraview_compress_job_t pk;
    char output[RUBRAVIEW_ARCHIVE_PATH];
    char dir[RUBRAVIEW_ARCHIVE_PATH];         /* the files' folder: the archive offered there, a name alone put there */
    /* the thread */
    HANDLE thread;
    volatile LONG done;
    rubraview_window_t *wake;
} archwork_t;

static char *dup_view(proven_u8str_view_t v) {
    char *s = rubraview_alloc(v.size + 1);
    if (s) { proven_sys_mem_copy(s, v.ptr, v.size); s[v.size] = '\0'; }
    return s;
}

static void free_list(char **list, size_t n) {
    for (size_t i = 0; list && i < n; ++i) rubraview_free(list[i]);
    rubraview_free(list);
}

static void work_free(archwork_t *w) {
    if (!w) return;
    free_list(w->archives, w->count);
    free_list(w->sources, w->source_count);
    rubraview_free(w->only);
    secure_wipe(rubraview_mem_mut(w->password, sizeof w->password));
    rubraview_free(w);
}

static DWORD WINAPI work_main(void *arg) {
    archwork_t *w = arg;
    if (w->compress) {
        rubraview_compress_run(&w->pk);
    } else {
        for (; w->at < w->count; ++w->at) {
            char dest[RUBRAVIEW_ARCHIVE_PATH];
            if (w->dest[0]) rubraview_fmt(dest, sizeof dest, "{}", PROVEN_ARG(w->dest));
            else {   /* beside the archive */
                rubraview_fmt(dest, sizeof dest, "{}", PROVEN_ARG(w->archives[w->at]));
                char *cut = rubraview_cstr_rchr(dest, '\\'), *cut2 = rubraview_cstr_rchr(dest, '/');
                if (cut2 > cut) cut = cut2;
                if (cut) *cut = '\0'; else rubraview_fmt(dest, sizeof dest, ".");
            }
            rubraview_extract_job_t *ex = &w->ex;
            bool stop = atomic_load(&ex->cancel);
            proven_sys_mem_set(ex, 0, sizeof *ex);
            atomic_store(&ex->cancel, stop);
            ex->archive = w->archives[w->at];
            ex->dest = dest;
            ex->mode = w->mode;
            ex->exists = RUBRAVIEW_EXISTS_NUMBER;
            ex->password = w->password[0] ? w->password : NULL;
            ex->only = w->only;
            ex->only_count = w->only_count;
            ex->inner = w->only ? w->inner : NULL;
            rubraview_extract_run(ex);
            w->written += ex->written; w->skipped += ex->skipped; w->failed += ex->failed;
            if (ex->root[0]) rubraview_fmt(w->last_root, sizeof w->last_root, "{}", PROVEN_ARG(ex->root));
            if (ex->result == RUBRAVIEW_ARCHIVE_CANCELLED || ex->result == RUBRAVIEW_ARCHIVE_PASSWORD_NEEDED ||
                ex->result == RUBRAVIEW_ARCHIVE_PASSWORD_WRONG) break;   /* stopped here: asked, or let go */
        }
    }
    InterlockedExchange(&w->done, 1);
    if (w->wake) rubraview_pal_window_wake(w->wake);
    return 0;
}

static bool work_start(app_state_t *app, archwork_t *w) {
    /* A password typed for work kept waiting while other work started (Explorer's menu does that): one at a time. */
    if (app->archwork) { osd_say(app, TU8("an archive is already being worked on (Esc stops it)")); work_free(w); return false; }
    w->wake = app->window;
    w->done = 0;
    w->thread = CreateThread(NULL, 0, work_main, w, 0, NULL);
    if (!w->thread) { osd_say(app, TU8("the work could not be started")); work_free(w); return false; }
    app->archwork = w;
    return true;
}

bool archwork_busy(const app_state_t *app) { return app->archwork != NULL; }

/* Esc: the work in hand stops (true when there was one). */
bool archwork_stop(app_state_t *app) {
    archwork_t *w = app->archwork;
    if (!w) return false;
    atomic_store(w->compress ? &w->pk.cancel : &w->ex.cancel, true);
    osd_say(app, TU8("stopping..."));
    return true;
}

/* ---- what is picked ---- */

static size_t picked_paths(app_state_t *app, char ***out, bool archives_only) {
    rubraview_fs_entry_t *entries = NULL;
    size_t n = picker_picked_entries(app, &entries);
    char **list = n ? rubraview_alloc_zero(n, sizeof *list) : NULL;
    size_t k = 0;
    for (size_t i = 0; list && i < n; ++i) {
        if (archives_only && (entries[i].is_directory || !rubraview_glob_match_list(entries[i].name, PROVEN_LIT(ARCHIVE_FILTER_ALL)))) continue;
        if ((list[k] = dup_view(entries[i].path)) != NULL) k++;
    }
    if (k == 0) { rubraview_free(list); list = NULL; }
    *out = list;
    return k;
}

/* Explorer's files: the archives among them, or all of them. */
static bool is_archive_path(proven_u8str_view_t path) {
    return rubraview_glob_match_list(rubraview_path_basename(path), PROVEN_LIT(ARCHIVE_FILTER_ALL));
}

static size_t given_paths(const proven_u8str_view_t *paths, size_t n, char ***out, bool archives_only) {
    char **list = n ? rubraview_alloc_zero(n, sizeof *list) : NULL;
    size_t k = 0;
    for (size_t i = 0; list && i < n; ++i) {
        if (paths[i].size == 0 || (archives_only && !is_archive_path(paths[i]))) continue;
        if ((list[k] = dup_view(paths[i])) != NULL) k++;
    }
    if (k == 0) { rubraview_free(list); list = NULL; }
    *out = list;
    return k;
}

/* ---- extracting ---- */

static void extract_start(app_state_t *app, archwork_t *w, int mode, bool choose_folder) {
    if (w->count == 0) { work_free(w); osd_say(app, TU8("pick an archive first")); return; }
    w->mode = (rubraview_extract_mode_t)mode;
    if (choose_folder) {
        rubraview_file_dialog_opts_t opts = { .title = T("Extract into which folder"), .folder_mode = true,
                                              .parent_window_handle = rubraview_pal_window_native_handle(app->window) };
        rubraview_file_dialog_result_t picked = rubraview_pal_file_dialog_pick_folder(app->arena, &opts);
        if (!picked.accepted || picked.count == 0 || picked.paths[0].size >= sizeof w->dest) { work_free(w); return; }
        proven_sys_mem_copy(w->dest, picked.paths[0].ptr, picked.paths[0].size);
        w->dest[picked.paths[0].size] = '\0';
    }
    /* the passwords that worked this session first */
    if (app->password_count > 0 && rubraview_archive_wants_password(w->archives[0], app->archive_codepage) == 1)
        rubraview_fmt(w->password, sizeof w->password, "{}", PROVEN_ARG(rubraview_bytes_view(app->passwords[app->password_count - 1], (int)app->password_lens[app->password_count - 1])));
    work_start(app, w);
}

void archwork_extract(app_state_t *app, int mode, bool choose_folder) {
    if (app->archwork) { osd_say(app, TU8("an archive is already being worked on (Esc stops it)")); return; }
    archwork_t *w = rubraview_alloc_zero(1, sizeof *w);
    if (!w) return;
    w->count = picked_paths(app, &w->archives, true);
    extract_start(app, w, mode, choose_folder);
}

void archwork_extract_paths(app_state_t *app, const proven_u8str_view_t *paths, size_t count, int mode, bool choose_folder) {
    if (app->archwork) { osd_say(app, TU8("an archive is already being worked on (Esc stops it)")); return; }
    archwork_t *w = rubraview_alloc_zero(1, sizeof *w);
    if (!w) return;
    w->count = given_paths(paths, count, &w->archives, true);
    extract_start(app, w, mode, choose_folder);
}

/* RV-207: what is picked in the archive looked into (all of it when nothing is), named from the folder it was picked
   in: a folder by everything in it. */
void archwork_extract_inside(app_state_t *app, int mode, bool choose_folder) {
    if (app->archwork) { osd_say(app, TU8("an archive is already being worked on (Esc stops it)")); return; }
    const rubraview_archive_index_t *ix = app->picker_arc_index;
    if (!ix || !app->picker_arc_path) return;
    archwork_t *w = rubraview_alloc_zero(1, sizeof *w);
    if (!w) return;
    w->archives = rubraview_alloc_zero(1, sizeof *w->archives);
    if (!w->archives || !(w->archives[0] = dup_view(proven_u8str_view_from_cstr(app->picker_arc_path)))) { work_free(w); return; }
    w->count = 1;
    char rel[RUBRAVIEW_ARCHIVE_PATH];
    if (picker_archive_rel(app, app->picker_dir, rel, sizeof rel - 1) && rel[0]) {
        size_t n = proven_cstr_len(rel);   /* below RUBRAVIEW_ARCHIVE_PATH - 1: room for the '/' */
        proven_sys_mem_copy(w->inner, rel, n);
        w->inner[n] = '/';
        w->inner[n + 1] = '\0';
    }
    size_t picked = 0;
    for (size_t i = 0; app->picker_selected && i < app->picker_listing.count; ++i)
        if (app->picker_selected[i] && !picker_is_up(app, i)) picked++;
    if (picked > 0) {
        w->only = rubraview_alloc(ix->count * sizeof *w->only);
        if (!w->only) { work_free(w); return; }
        for (size_t i = 0; i < app->picker_listing.count; ++i) {
            if (!app->picker_selected[i] || picker_is_up(app, i)) continue;
            if (!picker_archive_rel(app, app->picker_listing.entries[i].path, rel, sizeof rel)) continue;
            size_t room = ix->count - w->only_count;
            size_t k = rubraview_archive_index_under(ix, proven_u8str_view_from_cstr(rel), w->only + w->only_count, room);
            w->only_count += k < room ? k : room;   /* siblings share nothing; never past the room */
        }
        if (w->only_count == 0) { work_free(w); osd_say(app, TU8("nothing picked")); return; }
    } else {
        w->inner[0] = '\0';   /* the whole archive, as it is named */
    }
    extract_start(app, w, mode, choose_folder);
}

/* The password box answered for the archive the work stopped at: on from there with it. */
static archwork_t *g_waiting;   /* the work kept while its password is asked */

void archwork_password_typed(app_state_t *app, proven_u8str_view_t password) {
    archwork_t *w = g_waiting;
    g_waiting = NULL;
    if (!w) return;
    if (password.size == 0 || password.size >= sizeof w->password) { work_free(w); osd_say(app, TU8("not extracted: it needs its password")); return; }
    proven_sys_mem_copy(w->password, password.ptr, password.size);
    w->password[password.size] = '\0';
    work_start(app, w);
}

/* ---- compressing ---- */

/* "\\" after a folder, none after one that has it ("C:\\"). */
static const char *dir_sep(const char *dir) {
    size_t n = proven_cstr_len(dir);
    return n && (dir[n - 1] == '\\' || dir[n - 1] == '/') ? "" : "\\";
}

/* The archive's name offered: the one thing's own name, or the folder's for many, beside them. */
static void offered_name(app_state_t *app, archwork_t *w, const char *ext, char *out, size_t cap) {
    proven_u8str_view_t dir = proven_u8str_view_from_cstr(w->dir);
    char **sources = w->sources;
    size_t n = w->source_count;
    char stem[512];
    if (n == 1) {
        proven_u8str_view_t base = rubraview_path_basename(proven_u8str_view_from_cstr(sources[0]));
        size_t len = base.size;
        rubraview_fs_entry_t st;
        bool folder = rubraview_pal_fs_stat(app->arena, proven_u8str_view_from_cstr(sources[0]), &st) && st.is_directory;
        if (!folder) for (size_t i = len; i > 0; --i) if (base.ptr[i - 1] == '.') { len = i - 1; break; }
        rubraview_fmt(stem, sizeof stem, "{}", PROVEN_ARG(rubraview_bytes_view((const char *)base.ptr, (int)(len ? len : base.size))));
    } else {
        proven_u8str_view_t base = rubraview_path_basename(dir);
        rubraview_fmt(stem, sizeof stem, "{}", PROVEN_ARG(rubraview_bytes_view((const char *)base.ptr, (int)base.size)));
        if (!stem[0]) rubraview_fmt(stem, sizeof stem, "archive");
    }
    rubraview_fmt(out, cap, "{}{}{}.{}", PROVEN_ARG(rubraview_bytes_view((const char *)dir.ptr, (int)dir.size)), PROVEN_ARG(dir_sep(w->dir)), PROVEN_ARG(stem), PROVEN_ARG(ext));
}

static archwork_t *g_packing;   /* the work being set up by the Compress box and its password */
static archwork_t *g_older;     /* D-267: the work refused for an older set's parts, waiting for the answer */

static void pack_start(app_state_t *app, archwork_t *w, rubraview_pack_format_t format, int level, rubraview_pack_method_t method,
                       bool solid_off, const char *password) {
    w->compress = true;
    w->pk.method = method;
    w->pk.solid_off = solid_off;
    w->pk.sources = (const char *const *)w->sources;
    w->pk.source_count = w->source_count;
    w->pk.output = w->output;
    w->pk.format = format;
    w->pk.level = level;
    if (password && password[0]) { rubraview_fmt(w->password, sizeof w->password, "{}", PROVEN_ARG(password)); w->pk.password = w->password; }
    work_start(app, w);
}

/* The files' folder: the first one's. */
static void sources_dir(archwork_t *w) {
    proven_u8str_view_t d = rubraview_path_dirname(proven_u8str_view_from_cstr(w->sources[0]));
    rubraview_fmt(w->dir, sizeof w->dir, "{}", PROVEN_ARG(rubraview_bytes_view((const char *)d.ptr, (int)d.size)));
}

static void compress_go(app_state_t *app, archwork_t *w, bool ask);

/* "Compress to <name>.zip": at once, ZIP, normal, no password. "Compress...": the box first. */
void archwork_compress_quick(app_state_t *app) {
    if (app->archwork) { osd_say(app, TU8("an archive is already being worked on (Esc stops it)")); return; }
    archwork_t *w = rubraview_alloc_zero(1, sizeof *w);
    if (!w) return;
    w->source_count = picked_paths(app, &w->sources, false);
    compress_go(app, w, false);
}

void archwork_compress_ask(app_state_t *app) {
    if (app->archwork) { osd_say(app, TU8("an archive is already being worked on (Esc stops it)")); return; }
    archwork_t *w = rubraview_alloc_zero(1, sizeof *w);
    if (!w) return;
    w->source_count = picked_paths(app, &w->sources, false);
    compress_go(app, w, true);
}

void archwork_compress_paths(app_state_t *app, const proven_u8str_view_t *paths, size_t count, bool ask) {
    if (app->archwork) { osd_say(app, TU8("an archive is already being worked on (Esc stops it)")); return; }
    archwork_t *w = rubraview_alloc_zero(1, sizeof *w);
    if (!w) return;
    w->source_count = given_paths(paths, count, &w->sources, false);
    compress_go(app, w, ask);
}

/* D-266 (owner, 2026-11-03: "압축 선택 기억"): the Compress window's choices, kept in Settings › Files › Compressing. */
static void pack_choices_load(app_state_t *app) {
    app->pack_level = (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("pack_level")));
    if (app->pack_level < 0 || app->pack_level >= RUBRAVIEW_PACK_LEVEL_STEPS) app->pack_level = RUBRAVIEW_PACK_LEVEL_NORMAL;
    app->pack_method = (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("pack_method")));
    if (app->pack_method < RUBRAVIEW_PACK_METHOD_DEFAULT || app->pack_method > RUBRAVIEW_PACK_METHOD_ZSTD) app->pack_method = RUBRAVIEW_PACK_METHOD_DEFAULT;
    app->pack_solid_off = rubraview_settings_get(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("pack_solid")) > 0.5;
    app->pack_split = (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("pack_split")));
    if (app->pack_split < 0 || app->pack_split > 4) app->pack_split = 0;
    app->pack_names_hidden = rubraview_settings_get(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("pack_names")) > 0.5;
}

static void pack_choices_save(app_state_t *app) {
    static const char *const KEYS[] = { "pack_format", "pack_level", "pack_method", "pack_solid", "pack_split", "pack_names" };
    double v[] = { archwork_pack_is_7z(app) ? 1.0 : 0.0, app->pack_level, app->pack_method, app->pack_solid_off ? 1.0 : 0.0,
                   app->pack_split, app->pack_names_hidden ? 1.0 : 0.0 };
    for (size_t i = 0; i < sizeof KEYS / sizeof *KEYS; ++i)
        rubraview_settings_set(&app->settings, PROVEN_LIT("archive"), proven_u8str_view_from_cstr(KEYS[i]), v[i]);
    settings_write_if_changed(app, false);
}

static void compress_go(app_state_t *app, archwork_t *w, bool ask) {
    if (w->source_count == 0) { work_free(w); osd_say(app, TU8("pick the files to compress first")); return; }
    sources_dir(w);
    if (!ask) {
        offered_name(app, w, "zip", w->output, sizeof w->output);
        pack_start(app, w, RUBRAVIEW_PACK_ZIP, 6, RUBRAVIEW_PACK_METHOD_DEFAULT, false, NULL);
        return;
    }
    char offer[RUBRAVIEW_ARCHIVE_PATH];
    pack_choices_load(app);
    offered_name(app, w, rubraview_settings_get(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("pack_format")) > 0.5 ? "7z" : "zip", offer, sizeof offer);
    if (g_packing) work_free(g_packing);
    g_packing = w;
    if (app->rename_active) rename_end(app);
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    rubraview_textedit_set(&app->rename_edit, proven_u8str_view_from_cstr(offer));
    rubraview_textedit_select_all(&app->rename_edit);
    app->rename_active = true;
    app->rename_is_pack = true;
    app->pack_focus = 0;   /* D-261: the name first; the password fields empty */
    secure_wipe(rubraview_mem_mut(app->pack_pw, sizeof app->pack_pw));
    app->pack_pw_len[0] = app->pack_pw_len[1] = 0;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, true);
    osd_say(app, TU8("where the new archive goes, and how it is packed (Tab: the password)"));
}

static bool ends_with_ci(const char *s, const char *tail) {
    size_t n = proven_cstr_len(s), t = proven_cstr_len(tail);
    return n >= t && rubraview_cstr_cmp_ci(s + n - t, tail) == 0;
}

/* D-260: the Compress window's format is the name's ending; a chip changes the ending. D-261: the name is in the box
   while it is typed, in pack_name while a password is. */
static proven_u8str_view_t pack_name_text(const app_state_t *app) {
    return app->pack_focus == 0 ? rubraview_textedit_text(&app->rename_edit)
                                : (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->pack_name, .size = app->pack_name_len };
}

bool archwork_pack_is_7z(const app_state_t *app) {
    proven_u8str_view_t t = pack_name_text(app);
    return t.size >= 3 && t.ptr[t.size - 3] == '.' && t.ptr[t.size - 2] == '7' && (t.ptr[t.size - 1] | 0x20) == 'z';
}

void archwork_pack_set_format(app_state_t *app, bool sevenz) {
    if (archwork_pack_is_7z(app) == sevenz) return;
    proven_u8str_view_t t = pack_name_text(app);
    char s[RUBRAVIEW_ARCHIVE_PATH];
    if (t.size + 5 >= sizeof s || t.size + 5 >= sizeof app->pack_name) return;
    proven_sys_mem_copy(s, t.ptr, t.size);
    s[t.size] = '\0';
    size_t n = t.size;
    if (n >= 4 && rubraview_cstr_cmp_ci(s + n - 4, ".zip") == 0) n -= 4;
    else if (n >= 3 && rubraview_cstr_cmp_ci(s + n - 3, ".7z") == 0) n -= 3;
    rubraview_fmt(s + n, sizeof s - n, sevenz ? ".7z" : ".zip");
    if (app->pack_focus == 0) rubraview_textedit_set(&app->rename_edit, proven_u8str_view_from_cstr(s));
    else { app->pack_name_len = proven_cstr_len(s); proven_sys_mem_copy(app->pack_name, s, app->pack_name_len); }
    /* a codec the other format does not take goes back to the format's first */
    if (!rubraview_pack_method_fits(sevenz ? RUBRAVIEW_PACK_7Z : RUBRAVIEW_PACK_ZIP, (rubraview_pack_method_t)app->pack_method))
        app->pack_method = RUBRAVIEW_PACK_METHOD_DEFAULT;
}

/* What the box holds put back in its field. */
static void pack_field_keep(app_state_t *app) {
    rename_insert(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->rename_composing, .size = app->rename_composing_length });
    app->rename_composing_length = 0;
    proven_u8str_view_t t = rubraview_textedit_text(&app->rename_edit);
    if (app->pack_focus == 0) {
        app->pack_name_len = t.size < sizeof app->pack_name ? t.size : sizeof app->pack_name - 1;
        proven_sys_mem_copy(app->pack_name, t.ptr, app->pack_name_len);
    } else {
        int k = app->pack_focus - 1;
        app->pack_pw_len[k] = t.size < sizeof app->pack_pw[k] ? t.size : sizeof app->pack_pw[k] - 1;
        proven_sys_mem_copy(app->pack_pw[k], t.ptr, app->pack_pw_len[k]);
    }
}

/* D-261: the box moves to the name, the password or its repeat (Tab, or a click on the field). */
void archwork_pack_focus(app_state_t *app, int32_t to) {
    if (!app->rename_is_pack || to < 0 || to > 2 || to == app->pack_focus) return;
    pack_field_keep(app);
    secure_wipe(rubraview_mem_mut(app->rename_buffer, sizeof app->rename_buffer));
    secure_wipe(rubraview_mem_mut(app->rename_composing, sizeof app->rename_composing));
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof app->rename_buffer);
    proven_u8str_view_t v = to == 0 ? (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->pack_name, .size = app->pack_name_len }
                                    : (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->pack_pw[to - 1], .size = app->pack_pw_len[to - 1] };
    rubraview_textedit_set(&app->rename_edit, v);
    app->pack_focus = to;
    app->rename_is_password = to > 0;   /* dots, never copied out, wiped when it ends */
    rubraview_pal_window_text_ime(app->window, to == 0);   /* D-262: a new archive's password in plain letters */
}

/* The codec the window shows lit: the one chosen, or the format's first. */
rubraview_pack_method_t archwork_pack_method(const app_state_t *app, bool sevenz) {
    rubraview_pack_format_t f = sevenz ? RUBRAVIEW_PACK_7Z : RUBRAVIEW_PACK_ZIP;
    rubraview_pack_method_t m = (rubraview_pack_method_t)app->pack_method;
    if (m != RUBRAVIEW_PACK_METHOD_DEFAULT && rubraview_pack_method_ready(f, m)) return m;
    rubraview_pack_method_t first[8];
    return rubraview_pack_methods(f, first, 8) ? first[0] : RUBRAVIEW_PACK_METHOD_DEFAULT;
}

/* A chip of the Compress window pressed (D-260). True when the press was one of them. */
bool pack_window_press(app_state_t *app, double x, double y) {
    for (int32_t i = 0; i < app->pack_chip_n; ++i) {
        if (!pal_rect_has(app->pack_chip[i], x, y)) continue;
        int32_t row = app->pack_chip_kind[i] / 16, k = app->pack_chip_kind[i] % 16;
        bool sevenz = archwork_pack_is_7z(app);
        if (app->pack_chip_off[i]) {
            osd_say(app, row == PACK_ROW_SOLID ? TU8("ZIP packs each file on its own")
                       : row == PACK_ROW_NAMES && !sevenz ? TU8("ZIP keeps its names readable; 7z can hide them")
                       : row == PACK_ROW_METHOD && app->pack_level == 0 ? TU8("Store keeps the files as they are: no codec")
                       : TU8("FultaArc does not write that yet"));
            return true;
        }
        switch (row) {
            case PACK_ROW_FORMAT: archwork_pack_set_format(app, k == 1); break;
            case PACK_ROW_LEVEL:  app->pack_level = k; break;
            case PACK_ROW_METHOD: {
                rubraview_pack_method_t m[8];
                size_t n = rubraview_pack_methods(sevenz ? RUBRAVIEW_PACK_7Z : RUBRAVIEW_PACK_ZIP, m, 8);
                if ((size_t)k < n) app->pack_method = (int32_t)m[k];
                break;
            }
            case PACK_ROW_SOLID:  app->pack_solid_off = k == 1; break;
            case PACK_ROW_SPLIT:  app->pack_split = k; break;
            case PACK_ROW_NAMES:  app->pack_names_hidden = k == 1; break;
            default:              break;
        }
        pack_choices_save(app);   /* D-266 */
        return true;
    }
    for (int32_t f = 0; f < 3; ++f) if (pal_rect_has(app->pack_field[f], x, y)) { archwork_pack_focus(app, f); return true; }   /* D-261 */
    return pal_rect_has(app->pack_panel, x, y);   /* between the chips: nothing under it is pressed */
}

/* Enter in the Compress window (D-261): the two passwords the same, then it is packed. */
void archwork_pack_typed(app_state_t *app) {
    pack_field_keep(app);
    if (app->pack_names_hidden && archwork_pack_is_7z(app) && app->pack_pw_len[0] == 0) {   /* D-263 */
        osd_say(app, TU8("hidden names need a password: type one"));
        archwork_pack_focus(app, 1);
        return;
    }
    if (app->pack_pw_len[0] != app->pack_pw_len[1] || proven_sys_mem_cmp(app->pack_pw[0], app->pack_pw[1], app->pack_pw_len[0]) != 0) {
        osd_say(app, TU8("the two passwords differ: type it again"));
        archwork_pack_focus(app, app->pack_pw_len[0] ? 2 : 1);
        return;
    }
    pack_choices_save(app);   /* D-266: the format the name's ending says, too */
    archwork_t *w = g_packing;
    g_packing = NULL;
    proven_u8str_view_t t = { .ptr = (const proven_byte_t *)app->pack_name, .size = app->pack_name_len };
    char pw[256];
    proven_sys_mem_copy(pw, app->pack_pw[0], app->pack_pw_len[0]);
    pw[app->pack_pw_len[0]] = '\0';
    secure_wipe(rubraview_mem_mut(app->pack_pw, sizeof app->pack_pw));
    app->pack_pw_len[0] = app->pack_pw_len[1] = 0;
    rename_end(app);
    if (w && (t.size == 0 || t.size >= sizeof w->output)) { osd_say(app, TU8("not compressed: no name")); work_free(w); w = NULL; }
    bool bare = true;   /* a name alone goes beside the files, not where the program was started */
    for (size_t i = 0; w && i < t.size; ++i) if (t.ptr[i] == '\\' || t.ptr[i] == '/' || t.ptr[i] == ':') bare = false;
    if (w && bare) {
        int n = rubraview_fmt(w->output, sizeof w->output, "{}{}{}", PROVEN_ARG(w->dir), PROVEN_ARG(dir_sep(w->dir)), PROVEN_ARG(rubraview_bytes_view((const char *)t.ptr, (int)t.size)));
        if (n <= 0 || (size_t)n >= sizeof w->output) { osd_say(app, TU8("not compressed: no name")); work_free(w); w = NULL; }
    } else if (w) {
        proven_sys_mem_copy(w->output, t.ptr, t.size);
        w->output[t.size] = '\0';
    }
    if (w && !ends_with_ci(w->output, ".zip") && !ends_with_ci(w->output, ".7z")) {
        if (proven_cstr_len(w->output) + 5 < sizeof w->output) rubraview_cstr_append(w->output, sizeof w->output, ".zip");
    }
    if (w) {
        bool sevenz = ends_with_ci(w->output, ".7z");
        w->pk.names_hidden = sevenz && pw[0] && app->pack_names_hidden;
        /* D-263: the window's split sizes in MiB, 4 GB as FAT32's largest file allows */
        static const uint64_t SPLIT_MIB[] = { 0, 10, 100, 700, 4095 };
        int32_t k = app->pack_split;
        w->pk.volume_bytes = k > 0 && k < (int32_t)(sizeof SPLIT_MIB / sizeof *SPLIT_MIB) ? SPLIT_MIB[k] << 20 : 0;
        pack_start(app, w, sevenz ? RUBRAVIEW_PACK_7Z : RUBRAVIEW_PACK_ZIP, rubraview_pack_level_of(app->pack_level),
                   archwork_pack_method(app, sevenz), sevenz && app->pack_solid_off, pw);
    }
    secure_wipe(rubraview_mem_mut(pw, sizeof pw));
}

/* D-267 (owner, 2026-11-03: "예전 분할 조각 지우기 제안"): the older set's parts to the Recycle Bin and the same work
   again, or nothing. */
void archwork_pack_ask_answer(app_state_t *app, bool yes) {
    archwork_t *w = g_older;
    g_older = NULL;
    app->pack_ask = false;
    if (!w) return;
    if (!yes) { osd_say(app, TU8("not compressed")); work_free(w); return; }
    char parts[64][RUBRAVIEW_PACK_PART_PATH];
    size_t n = rubraview_pack_set_parts(w->pk.format, w->pk.set_name, parts, 64), gone = 0;
    for (size_t i = 0; i < n; ++i) gone += rubraview_pal_fs_recycle(proven_u8str_view_from_cstr(parts[i])) ? 1u : 0u;
    if (gone < n) { osd_say(app, TU8("the older parts could not all be moved to the Recycle Bin")); work_free(w); return; }
    w->pk.result = RUBRAVIEW_ARCHIVE_DONE;
    w->pk.why[0] = w->pk.written[0] = '\0';
    w->pk.older_parts = false;
    w->pk.files = w->pk.folders = 0;
    atomic_store(&w->pk.cancel, false);
    if (work_start(app, w)) osd_say(app, TU8("older parts recycled: compressing again"));
}

void archwork_pack_cancelled(app_state_t *app) {
    if (g_packing) { work_free(g_packing); g_packing = NULL; }
    secure_wipe(rubraview_mem_mut(app->pack_pw, sizeof app->pack_pw));
    app->pack_pw_len[0] = app->pack_pw_len[1] = 0;
}

/* ---- each pass ---- */

static void say_progress(app_state_t *app, archwork_t *w) {
    char line[256];
    if (w->compress) {
        uint64_t done = atomic_load(&w->pk.done), total = atomic_load(&w->pk.total);
        int pct = total ? (int)(done * 100u / total) : 0;
        int phase = atomic_load(&w->pk.phase);
        proven_u8str_view_t name = rubraview_path_basename(proven_u8str_view_from_cstr(w->output));
        if (atomic_load(&w->pk.cancel)) rubraview_fmt(line, sizeof line, "{}", PROVEN_ARG(T("stopping...")));
        else rubraview_fmt(line, sizeof line, phase == 2 ? T("checking {}... {} %  (Esc stops it)") : T("compressing into {}... {} %  (Esc stops it)"),
                      PROVEN_ARG(rubraview_bytes_view((const char *)name.ptr, (int)name.size)), PROVEN_ARG((int)pct));
    } else {
        uint64_t done = atomic_load(&w->ex.done), total = atomic_load(&w->ex.total);
        int pct = total ? (int)(done * 100u / total) : 0;
        size_t at = w->at < w->count ? w->at : w->count - 1;
        proven_u8str_view_t name = rubraview_path_basename(proven_u8str_view_from_cstr(w->archives[at]));
        if (atomic_load(&w->ex.cancel)) rubraview_fmt(line, sizeof line, "{}", PROVEN_ARG(T("stopping...")));
        else if (w->count > 1) rubraview_fmt(line, sizeof line, T("extracting {} ({} of {})... {} %  (Esc stops it)"),
                                        PROVEN_ARG(rubraview_bytes_view((const char *)name.ptr, (int)name.size)), PROVEN_ARG((size_t)(at + 1)), PROVEN_ARG((size_t)w->count), PROVEN_ARG((int)pct));
        else rubraview_fmt(line, sizeof line, T("extracting {}... {} %  (Esc stops it)"), PROVEN_ARG(rubraview_bytes_view((const char *)name.ptr, (int)name.size)), PROVEN_ARG((int)pct));
    }
    osd_say(app, cstr(line));
}

void archwork_poll(app_state_t *app, bool quitting) {
    archwork_t *w = app->archwork;
    if (!w) return;
    if (quitting) atomic_store(w->compress ? &w->pk.cancel : &w->ex.cancel, true);
    if (!quitting && !InterlockedCompareExchange(&w->done, 0, 0)) { say_progress(app, w); return; }
    WaitForSingleObject(w->thread, INFINITE);
    CloseHandle(w->thread);
    w->thread = NULL;
    app->archwork = NULL;
    if (quitting) { work_free(w); return; }
    char line[512];
    if (w->compress) {
        rubraview_archive_result_t r = w->pk.result;
        if (r != RUBRAVIEW_ARCHIVE_DONE && w->pk.older_parts && w->pk.set_name[0]) {   /* D-267: asked, the work kept */
            char parts[64][RUBRAVIEW_PACK_PART_PATH];
            app->pack_ask_parts = rubraview_pack_set_parts(w->pk.format, w->pk.set_name, parts, 64);
            rubraview_fmt(app->pack_ask_set, sizeof app->pack_ask_set, "{}", PROVEN_ARG(w->pk.set_name));
            if (g_older) work_free(g_older);
            g_older = w;
            app->pack_ask = true;
            return;
        }
        if (r == RUBRAVIEW_ARCHIVE_DONE) {
            proven_u8str_view_t name = rubraview_path_basename(proven_u8str_view_from_cstr(w->pk.written));
            rubraview_fmt(line, sizeof line, T("compressed: {} ({} files, {} folders)"), PROVEN_ARG(rubraview_bytes_view((const char *)name.ptr, (int)name.size)), PROVEN_ARG((unsigned)w->pk.files), PROVEN_ARG((unsigned)w->pk.folders));
        } else {
            rubraview_fmt(line, sizeof line, "{}{}{}", PROVEN_ARG(rubraview_archive_result_text(r)), PROVEN_ARG(w->pk.why[0] ? "  -  " : ""), PROVEN_ARG(w->pk.why));
        }
        osd_say(app, cstr(line));
        if (app->picker_open) picker_navigate(app, app->picker_dir);
        work_free(w);
        return;
    }
    rubraview_archive_result_t r = w->ex.result;
    if (r == RUBRAVIEW_ARCHIVE_PASSWORD_NEEDED || r == RUBRAVIEW_ARCHIVE_PASSWORD_WRONG) {
        /* asked once, here; the work waits for it */
        secure_wipe(rubraview_mem_mut(w->password, sizeof w->password));
        g_waiting = w;
        if (app->rename_active) rename_end(app);
        app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
        app->rename_active = true;
        app->rename_is_password = true;
        app->password_for_extract = true;
        size_t pn = proven_cstr_len(w->archives[w->at]);   /* the box says whose */
        if (pn < sizeof(app->password_for)) { proven_sys_mem_copy(app->password_for, w->archives[w->at], pn); app->password_for_len = pn; }
        rubraview_pal_window_text_input(app->window, true);
        rubraview_pal_window_text_ime(app->window, false);   /* D-262: hidden, plain letters; the eye brings the IME */
        proven_u8str_view_t name = rubraview_path_basename(proven_u8str_view_from_cstr(w->archives[w->at]));
        rubraview_fmt(line, sizeof line, r == RUBRAVIEW_ARCHIVE_PASSWORD_WRONG ? T("wrong password for {}: type it again, then Enter")
                                                                       : T("{} is locked: type its password, then Enter"),
                 PROVEN_ARG(rubraview_bytes_view((const char *)name.ptr, (int)name.size)));
        osd_say(app, cstr(line));
        return;
    }
    if (r == RUBRAVIEW_ARCHIVE_DONE || r == RUBRAVIEW_ARCHIVE_PARTLY || r == RUBRAVIEW_ARCHIVE_NOTHING) {
        proven_u8str_view_t where = rubraview_path_basename(proven_u8str_view_from_cstr(w->last_root));
        if (w->failed) rubraview_fmt(line, sizeof line, T("extracted {} files into {}; {} could not be written{}{}"), PROVEN_ARG((unsigned)w->written),
                                PROVEN_ARG(rubraview_bytes_view((const char *)where.ptr, (int)where.size)), PROVEN_ARG((unsigned)w->failed), PROVEN_ARG(w->ex.why[0] ? "  -  " : ""), PROVEN_ARG(w->ex.why));
        else if (w->skipped) rubraview_fmt(line, sizeof line, T("extracted {} files into {} ({} left out)"), PROVEN_ARG((unsigned)w->written), PROVEN_ARG(rubraview_bytes_view((const char *)where.ptr, (int)where.size)), PROVEN_ARG((unsigned)w->skipped));
        else rubraview_fmt(line, sizeof line, T("extracted {} files into {}"), PROVEN_ARG((unsigned)w->written), PROVEN_ARG(rubraview_bytes_view((const char *)where.ptr, (int)where.size)));
        if (w->password[0]) password_remember(app, proven_u8str_view_from_cstr(w->password));
    } else {
        rubraview_fmt(line, sizeof line, "{}{}{}", PROVEN_ARG(rubraview_archive_result_text(r)), PROVEN_ARG(w->ex.why[0] ? "  -  " : ""), PROVEN_ARG(w->ex.why));
    }
    osd_say(app, cstr(line));
    if (app->picker_open) picker_navigate(app, app->picker_dir);
    work_free(w);
}

/* The password box's Enter or Esc, when it was asked for the work. True when it was. */
bool archwork_password_box(app_state_t *app, bool entered, proven_u8str_view_t typed) {
    if (app->password_for_extract) {
        app->password_for_extract = false;
        if (entered) archwork_password_typed(app, typed);
        else { if (g_waiting) work_free(g_waiting); g_waiting = NULL; osd_say(app, TU8("not extracted: it needs its password")); }
        return true;
    }
    return false;
}
