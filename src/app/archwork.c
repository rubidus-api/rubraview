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
    char *s = malloc(v.size + 1);
    if (s) { memcpy(s, v.ptr, v.size); s[v.size] = '\0'; }
    return s;
}

static void free_list(char **list, size_t n) {
    for (size_t i = 0; list && i < n; ++i) free(list[i]);
    free(list);
}

static void work_free(archwork_t *w) {
    if (!w) return;
    free_list(w->archives, w->count);
    free_list(w->sources, w->source_count);
    free(w->only);
    secure_wipe(w->password, sizeof w->password);
    free(w);
}

static DWORD WINAPI work_main(void *arg) {
    archwork_t *w = arg;
    if (w->compress) {
        rubraview_compress_run(&w->pk);
    } else {
        for (; w->at < w->count; ++w->at) {
            char dest[RUBRAVIEW_ARCHIVE_PATH];
            if (w->dest[0]) snprintf(dest, sizeof dest, "%s", w->dest);
            else {   /* beside the archive */
                snprintf(dest, sizeof dest, "%s", w->archives[w->at]);
                char *cut = strrchr(dest, '\\'), *cut2 = strrchr(dest, '/');
                if (cut2 > cut) cut = cut2;
                if (cut) *cut = '\0'; else snprintf(dest, sizeof dest, ".");
            }
            rubraview_extract_job_t *ex = &w->ex;
            bool stop = atomic_load(&ex->cancel);
            memset(ex, 0, sizeof *ex);
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
            if (ex->root[0]) snprintf(w->last_root, sizeof w->last_root, "%s", ex->root);
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
    char **list = n ? calloc(n, sizeof *list) : NULL;
    size_t k = 0;
    for (size_t i = 0; list && i < n; ++i) {
        if (archives_only && (entries[i].is_directory || !rubraview_glob_match_list(entries[i].name, PROVEN_LIT(ARCHIVE_FILTER_ALL)))) continue;
        if ((list[k] = dup_view(entries[i].path)) != NULL) k++;
    }
    if (k == 0) { free(list); list = NULL; }
    *out = list;
    return k;
}

/* Explorer's files: the archives among them, or all of them. */
static bool is_archive_path(proven_u8str_view_t path) {
    return rubraview_glob_match_list(rubraview_path_basename(path), PROVEN_LIT(ARCHIVE_FILTER_ALL));
}

static size_t given_paths(const proven_u8str_view_t *paths, size_t n, char ***out, bool archives_only) {
    char **list = n ? calloc(n, sizeof *list) : NULL;
    size_t k = 0;
    for (size_t i = 0; list && i < n; ++i) {
        if (paths[i].size == 0 || (archives_only && !is_archive_path(paths[i]))) continue;
        if ((list[k] = dup_view(paths[i])) != NULL) k++;
    }
    if (k == 0) { free(list); list = NULL; }
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
        memcpy(w->dest, picked.paths[0].ptr, picked.paths[0].size);
        w->dest[picked.paths[0].size] = '\0';
    }
    /* the passwords that worked this session first */
    if (app->password_count > 0 && rubraview_archive_wants_password(w->archives[0], app->archive_codepage) == 1)
        snprintf(w->password, sizeof w->password, "%.*s", (int)app->password_lens[app->password_count - 1],
                 app->passwords[app->password_count - 1]);
    work_start(app, w);
}

void archwork_extract(app_state_t *app, int mode, bool choose_folder) {
    if (app->archwork) { osd_say(app, TU8("an archive is already being worked on (Esc stops it)")); return; }
    archwork_t *w = calloc(1, sizeof *w);
    if (!w) return;
    w->count = picked_paths(app, &w->archives, true);
    extract_start(app, w, mode, choose_folder);
}

void archwork_extract_paths(app_state_t *app, const proven_u8str_view_t *paths, size_t count, int mode, bool choose_folder) {
    if (app->archwork) { osd_say(app, TU8("an archive is already being worked on (Esc stops it)")); return; }
    archwork_t *w = calloc(1, sizeof *w);
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
    archwork_t *w = calloc(1, sizeof *w);
    if (!w) return;
    w->archives = calloc(1, sizeof *w->archives);
    if (!w->archives || !(w->archives[0] = dup_view(proven_u8str_view_from_cstr(app->picker_arc_path)))) { work_free(w); return; }
    w->count = 1;
    char rel[RUBRAVIEW_ARCHIVE_PATH];
    if (picker_archive_rel(app, app->picker_dir, rel, sizeof rel - 1) && rel[0]) {
        size_t n = strlen(rel);   /* below RUBRAVIEW_ARCHIVE_PATH - 1: room for the '/' */
        memcpy(w->inner, rel, n);
        w->inner[n] = '/';
        w->inner[n + 1] = '\0';
    }
    size_t picked = 0;
    for (size_t i = 0; app->picker_selected && i < app->picker_listing.count; ++i)
        if (app->picker_selected[i] && !picker_is_up(app, i)) picked++;
    if (picked > 0) {
        w->only = malloc(ix->count * sizeof *w->only);
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
    memcpy(w->password, password.ptr, password.size);
    w->password[password.size] = '\0';
    work_start(app, w);
}

/* ---- compressing ---- */

static const char *const LEVEL_WORD[4] = { N_("store"), N_("fast"), N_("normal"), N_("best") };
static const int LEVEL_OF[4] = { 0, 1, 6, 9 };

/* "\\" after a folder, none after one that has it ("C:\\"). */
static const char *dir_sep(const char *dir) {
    size_t n = strlen(dir);
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
        snprintf(stem, sizeof stem, "%.*s", (int)(len ? len : base.size), (const char *)base.ptr);
    } else {
        proven_u8str_view_t base = rubraview_path_basename(dir);
        snprintf(stem, sizeof stem, "%.*s", (int)base.size, (const char *)base.ptr);
        if (!stem[0]) snprintf(stem, sizeof stem, "archive");
    }
    snprintf(out, cap, "%.*s%s%s.%s", (int)dir.size, (const char *)dir.ptr, dir_sep(w->dir), stem, ext);
}

static archwork_t *g_packing;   /* the work being set up by the Compress box and its password */

static void pack_start(app_state_t *app, archwork_t *w, rubraview_pack_format_t format, int level, const char *password) {
    w->compress = true;
    w->pk.sources = (const char *const *)w->sources;
    w->pk.source_count = w->source_count;
    w->pk.output = w->output;
    w->pk.format = format;
    w->pk.level = level;
    if (password && password[0]) { snprintf(w->password, sizeof w->password, "%s", password); w->pk.password = w->password; }
    work_start(app, w);
}

/* The files' folder: the first one's. */
static void sources_dir(archwork_t *w) {
    proven_u8str_view_t d = rubraview_path_dirname(proven_u8str_view_from_cstr(w->sources[0]));
    snprintf(w->dir, sizeof w->dir, "%.*s", (int)d.size, (const char *)d.ptr);
}

static void compress_go(app_state_t *app, archwork_t *w, bool ask);

/* "Compress to <name>.zip": at once, ZIP, normal, no password. "Compress...": the box first. */
void archwork_compress_quick(app_state_t *app) {
    if (app->archwork) { osd_say(app, TU8("an archive is already being worked on (Esc stops it)")); return; }
    archwork_t *w = calloc(1, sizeof *w);
    if (!w) return;
    w->source_count = picked_paths(app, &w->sources, false);
    compress_go(app, w, false);
}

void archwork_compress_ask(app_state_t *app) {
    if (app->archwork) { osd_say(app, TU8("an archive is already being worked on (Esc stops it)")); return; }
    archwork_t *w = calloc(1, sizeof *w);
    if (!w) return;
    w->source_count = picked_paths(app, &w->sources, false);
    compress_go(app, w, true);
}

void archwork_compress_paths(app_state_t *app, const proven_u8str_view_t *paths, size_t count, bool ask) {
    if (app->archwork) { osd_say(app, TU8("an archive is already being worked on (Esc stops it)")); return; }
    archwork_t *w = calloc(1, sizeof *w);
    if (!w) return;
    w->source_count = given_paths(paths, count, &w->sources, false);
    compress_go(app, w, ask);
}

static void compress_go(app_state_t *app, archwork_t *w, bool ask) {
    if (w->source_count == 0) { work_free(w); osd_say(app, TU8("pick the files to compress first")); return; }
    sources_dir(w);
    if (!ask) {
        offered_name(app, w, "zip", w->output, sizeof w->output);
        pack_start(app, w, RUBRAVIEW_PACK_ZIP, 6, NULL);
        return;
    }
    char offer[RUBRAVIEW_ARCHIVE_PATH];
    offered_name(app, w, "zip", offer, sizeof offer);
    if (g_packing) work_free(g_packing);
    g_packing = w;
    if (app->rename_active) rename_end(app);
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    rubraview_textedit_set(&app->rename_edit, proven_u8str_view_from_cstr(offer));
    rubraview_textedit_select_all(&app->rename_edit);
    app->rename_active = true;
    app->rename_is_pack = true;
    if (app->pack_level < 0 || app->pack_level > 3) app->pack_level = 2;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, true);
    osd_say(app, TU8("where the new archive goes: its ending says ZIP or 7z"));
}

const char *archwork_level_word(const app_state_t *app) {
    int l = app->pack_level >= 0 && app->pack_level <= 3 ? app->pack_level : 2;
    return rubraview_tr(LEVEL_WORD[l]);
}

/* The box's ZIP / 7z button: the ending changed in the text. */
void archwork_pack_toggle_format(app_state_t *app) {
    proven_u8str_view_t t = rubraview_textedit_text(&app->rename_edit);
    char s[RUBRAVIEW_ARCHIVE_PATH];
    if (t.size + 5 >= sizeof s) return;
    memcpy(s, t.ptr, t.size);
    s[t.size] = '\0';
    size_t n = t.size;
    bool zip = n >= 4 && _stricmp(s + n - 4, ".zip") == 0, sz = n >= 3 && _stricmp(s + n - 3, ".7z") == 0;
    if (zip) snprintf(s + n - 4, sizeof s - (n - 4), ".7z");
    else if (sz) snprintf(s + n - 3, sizeof s - (n - 3), ".zip");
    else snprintf(s + n, sizeof s - n, ".zip");
    rubraview_textedit_set(&app->rename_edit, proven_u8str_view_from_cstr(s));
}

void archwork_pack_next_level(app_state_t *app) {
    app->pack_level = (app->pack_level + 1) % 4;
}

static bool ends_with_ci(const char *s, const char *tail) {
    size_t n = strlen(s), t = strlen(tail);
    return n >= t && _stricmp(s + n - t, tail) == 0;
}

/* Enter in the Compress box: then its password, or none. */
void archwork_pack_typed(app_state_t *app) {
    rename_insert(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->rename_composing, .size = app->rename_composing_length });
    app->rename_composing_length = 0;
    proven_u8str_view_t t = rubraview_textedit_text(&app->rename_edit);
    archwork_t *w = g_packing;
    rename_end(app);
    if (!w) return;
    if (t.size == 0 || t.size >= sizeof w->output) { osd_say(app, TU8("not compressed: no name")); work_free(w); g_packing = NULL; return; }
    bool bare = true;   /* a name alone goes beside the files, not where the program was started */
    for (size_t i = 0; i < t.size; ++i) if (t.ptr[i] == '\\' || t.ptr[i] == '/' || t.ptr[i] == ':') bare = false;
    if (bare) {
        int n = snprintf(w->output, sizeof w->output, "%s%s%.*s", w->dir, dir_sep(w->dir), (int)t.size, (const char *)t.ptr);
        if (n <= 0 || (size_t)n >= sizeof w->output) { osd_say(app, TU8("not compressed: no name")); work_free(w); g_packing = NULL; return; }
    } else {
        memcpy(w->output, t.ptr, t.size);
        w->output[t.size] = '\0';
    }
    if (!ends_with_ci(w->output, ".zip") && !ends_with_ci(w->output, ".7z")) {
        if (strlen(w->output) + 5 < sizeof w->output) strcat(w->output, ".zip");
    }
    /* the password box, for the new archive */
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    app->rename_active = true;
    app->rename_is_password = true;
    app->password_for_pack = true;
    size_t pn = strlen(w->output);   /* the box says whose */
    if (pn < sizeof(app->password_for)) { memcpy(app->password_for, w->output, pn); app->password_for_len = pn; }
    rubraview_pal_window_text_input(app->window, true);
    osd_say(app, ends_with_ci(w->output, ".7z") ? TU8("a password for it (AES-256; its names stay readable), or Enter for none")
                                                : TU8("a password for it (AES-256), or Enter for none"));
}

void archwork_pack_password_typed(app_state_t *app, proven_u8str_view_t password) {
    archwork_t *w = g_packing;
    g_packing = NULL;
    app->password_for_pack = false;
    if (!w) return;
    char pw[256];
    size_t n = password.size < sizeof pw ? password.size : 0;
    memcpy(pw, password.ptr, n);
    pw[n] = '\0';
    int level = LEVEL_OF[app->pack_level >= 0 && app->pack_level <= 3 ? app->pack_level : 2];
    pack_start(app, w, ends_with_ci(w->output, ".7z") ? RUBRAVIEW_PACK_7Z : RUBRAVIEW_PACK_ZIP, level, pw);
    secure_wipe(pw, sizeof pw);
}

void archwork_pack_cancelled(app_state_t *app) {
    (void)app;
    if (g_packing) { work_free(g_packing); g_packing = NULL; }
    app->password_for_pack = false;
}

/* ---- each pass ---- */

static void say_progress(app_state_t *app, archwork_t *w) {
    char line[256];
    if (w->compress) {
        uint64_t done = atomic_load(&w->pk.done), total = atomic_load(&w->pk.total);
        int pct = total ? (int)(done * 100u / total) : 0;
        int phase = atomic_load(&w->pk.phase);
        proven_u8str_view_t name = rubraview_path_basename(proven_u8str_view_from_cstr(w->output));
        if (atomic_load(&w->pk.cancel)) snprintf(line, sizeof line, "%s", T("stopping..."));
        else snprintf(line, sizeof line, phase == 2 ? T("checking %.*s... %d %%  (Esc stops it)") : T("compressing into %.*s... %d %%  (Esc stops it)"),
                      (int)name.size, (const char *)name.ptr, pct);
    } else {
        uint64_t done = atomic_load(&w->ex.done), total = atomic_load(&w->ex.total);
        int pct = total ? (int)(done * 100u / total) : 0;
        size_t at = w->at < w->count ? w->at : w->count - 1;
        proven_u8str_view_t name = rubraview_path_basename(proven_u8str_view_from_cstr(w->archives[at]));
        if (atomic_load(&w->ex.cancel)) snprintf(line, sizeof line, "%s", T("stopping..."));
        else if (w->count > 1) snprintf(line, sizeof line, T("extracting %.*s (%zu of %zu)... %d %%  (Esc stops it)"),
                                        (int)name.size, (const char *)name.ptr, at + 1, w->count, pct);
        else snprintf(line, sizeof line, T("extracting %.*s... %d %%  (Esc stops it)"), (int)name.size, (const char *)name.ptr, pct);
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
        if (r == RUBRAVIEW_ARCHIVE_DONE) {
            proven_u8str_view_t name = rubraview_path_basename(proven_u8str_view_from_cstr(w->pk.written));
            snprintf(line, sizeof line, T("compressed: %.*s (%u files, %u folders)"), (int)name.size, (const char *)name.ptr, w->pk.files, w->pk.folders);
        } else {
            snprintf(line, sizeof line, "%s%s%s", rubraview_archive_result_text(r), w->pk.why[0] ? "  -  " : "", w->pk.why);
        }
        osd_say(app, cstr(line));
        if (app->picker_open) picker_navigate(app, app->picker_dir);
        work_free(w);
        return;
    }
    rubraview_archive_result_t r = w->ex.result;
    if (r == RUBRAVIEW_ARCHIVE_PASSWORD_NEEDED || r == RUBRAVIEW_ARCHIVE_PASSWORD_WRONG) {
        /* asked once, here; the work waits for it */
        secure_wipe(w->password, sizeof w->password);
        g_waiting = w;
        if (app->rename_active) rename_end(app);
        app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
        app->rename_active = true;
        app->rename_is_password = true;
        app->password_for_extract = true;
        size_t pn = strlen(w->archives[w->at]);   /* the box says whose */
        if (pn < sizeof(app->password_for)) { memcpy(app->password_for, w->archives[w->at], pn); app->password_for_len = pn; }
        rubraview_pal_window_text_input(app->window, true);
        proven_u8str_view_t name = rubraview_path_basename(proven_u8str_view_from_cstr(w->archives[w->at]));
        snprintf(line, sizeof line, r == RUBRAVIEW_ARCHIVE_PASSWORD_WRONG ? T("wrong password for %.*s: type it again, then Enter")
                                                                       : T("%.*s is locked: type its password, then Enter"),
                 (int)name.size, (const char *)name.ptr);
        osd_say(app, cstr(line));
        return;
    }
    if (r == RUBRAVIEW_ARCHIVE_DONE || r == RUBRAVIEW_ARCHIVE_PARTLY || r == RUBRAVIEW_ARCHIVE_NOTHING) {
        proven_u8str_view_t where = rubraview_path_basename(proven_u8str_view_from_cstr(w->last_root));
        if (w->failed) snprintf(line, sizeof line, T("extracted %u files into %.*s; %u could not be written%s%s"), w->written,
                                (int)where.size, (const char *)where.ptr, w->failed, w->ex.why[0] ? "  -  " : "", w->ex.why);
        else if (w->skipped) snprintf(line, sizeof line, T("extracted %u files into %.*s (%u left out)"), w->written, (int)where.size, (const char *)where.ptr, w->skipped);
        else snprintf(line, sizeof line, T("extracted %u files into %.*s"), w->written, (int)where.size, (const char *)where.ptr);
        if (w->password[0]) password_remember(app, proven_u8str_view_from_cstr(w->password));
    } else {
        snprintf(line, sizeof line, "%s%s%s", rubraview_archive_result_text(r), w->ex.why[0] ? "  -  " : "", w->ex.why);
    }
    osd_say(app, cstr(line));
    if (app->picker_open) picker_navigate(app, app->picker_dir);
    work_free(w);
}

/* The password box's Enter or Esc, when it was asked for the work. True when it was. */
bool archwork_password_box(app_state_t *app, bool entered, proven_u8str_view_t typed) {
    if (app->password_for_pack) {
        if (entered) archwork_pack_password_typed(app, typed); else archwork_pack_cancelled(app);
        return true;
    }
    if (app->password_for_extract) {
        app->password_for_extract = false;
        if (entered) archwork_password_typed(app, typed);
        else { if (g_waiting) work_free(g_waiting); g_waiting = NULL; osd_say(app, TU8("not extracted: it needs its password")); }
        return true;
    }
    return false;
}
