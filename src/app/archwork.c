/*
 * RV-207: extracting and compressing from the open-file screen (D-252). The picked archives are written out, or the
 * picked files and folders packed, on a worker thread of their own while the screen stays usable; a line under the
 * picture says how far it is and Esc stops it. A locked archive asks its password in the password box, once; the
 * Compress box takes the new archive's place and name (its ending says ZIP or 7z) and its level, then a password.
 * Explorer's Rubraview menu hands its files to the same work (archwork_extract_paths, archwork_compress_paths).
 */
#include "app.h"
#include "rubraview/archive_job.h"
#include "rubraview/fileop.h"

/* RV-258's last step: an entry whose name is taken, parked by the extraction to be asked in the clashes window */
typedef struct xpark {
    size_t archive;                           /* which of the work's archives */
    char root[RUBRAVIEW_ARCHIVE_PATH];        /* the folder it went into */
    rubraview_extract_parked_t p;
} xpark_t;

typedef struct archwork {
    bool compress;
    bool windowed;                            /* RV-272: asked from Explorer: shown in its own window, not the viewer */
    bool ask;                                 /* RV-294: the Compress window is the job's: it asks there, then packs */
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
    xpark_t *parked;                          /* RV-258: the names taken, every archive's */
    size_t parked_n, parked_cap;
    uint8_t *rules;                           /* RV-258: a second extraction, an answer's rule for each of `only` */
    char last_root[RUBRAVIEW_ARCHIVE_PATH];
    /* compressing */
    char **sources;
    size_t source_count;
    rubraview_compress_job_t pk;
    char output[RUBRAVIEW_ARCHIVE_PATH];
    char dir[RUBRAVIEW_ARCHIVE_PATH];         /* the files' folder: the archive offered there, a name alone put there */
    /* RV-292 (owner, 2026-11-15: C — the whole work apart): the job's process doing it, and how far it said it is */
    int32_t job;
    rubraview_jobwire_progress_t prog;
    bool stopping;
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
    for (size_t i = 0; i < w->parked_n; ++i) rubraview_free(w->parked[i].p.path);
    rubraview_free(w->parked);
    rubraview_free(w->rules);
    secure_wipe(rubraview_mem_mut(w->password, sizeof w->password));
    rubraview_free(w);
}

/* RV-292: the work goes to a job's process of its own (jobproc.c runs rubraview_extract_run / rubraview_compress_run
   there, archive by archive as this file did on its thread); what to do is all in the message. */
static bool work_start(app_state_t *app, archwork_t *w) {
    /* A password typed for work kept waiting while other work started (Explorer's menu does that): one at a time. */
    if (app->archwork) { osd_say(app, TU8("an archive is already being worked on (Esc stops it)")); work_free(w); return false; }
    rubraview_jobwire_archive_t a = { .kind = w->compress ? RUBRAVIEW_JOBWIRE_COMPRESS : RUBRAVIEW_JOBWIRE_EXTRACT, .quiet = w->windowed,
                                      .password = proven_u8str_view_from_cstr(w->password) };
    size_t n = w->compress ? w->source_count : w->count;
    proven_u8str_view_t *names = (proven_u8str_view_t *)rubraview_alloc_zero(n ? n : 1, sizeof *names);
    if (!names) { work_free(w); return false; }
    for (size_t i = 0; i < n; ++i) names[i] = proven_u8str_view_from_cstr(w->compress ? w->sources[i] : w->archives[i]);
    a.count = (uint32_t)n;
    if (w->compress) {
        a.output = proven_u8str_view_from_cstr(w->output);
        a.format = (uint8_t)w->pk.format;
        a.method = (uint8_t)w->pk.method;
        a.level = w->pk.level;
        a.solid_off = w->pk.solid_off;
        a.names_hidden = w->pk.names_hidden;
        a.volume_bytes = w->pk.volume_bytes;
        if (w->ask) { a.mode = 1; a.dest = proven_u8str_view_from_cstr(w->dir); }   /* RV-294: asked in the job, a bare name put here */
    } else {
        a.dest = proven_u8str_view_from_cstr(w->dest);
        a.mode = (uint8_t)w->mode;
        a.only_count = (uint32_t)w->only_count;
        a.inner = w->only ? proven_u8str_view_from_cstr(w->inner) : PROVEN_LIT("");
        a.rules_count = w->rules ? (uint32_t)w->only_count : 0;
        a.codepage = (int32_t)app->archive_codepage;
    }
    w->job = filejob_archive_start(app, &a, names, w->only, w->rules);
    rubraview_free(names);
    if (w->job < 0) { work_free(w); return false; }
    w->prog = (rubraview_jobwire_progress_t){0};
    w->stopping = false;
    app->archwork = w;
    return true;
}

bool archwork_busy(const app_state_t *app) { return app->archwork != NULL; }

/* Esc: the work in hand stops (true when there was one). */
bool archwork_stop(app_state_t *app) {
    archwork_t *w = app->archwork;
    if (!w) return false;
    filejob_archive_stop(w->job);   /* RV-292: its process stops */
    w->stopping = true;
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

/* RV-240 (owner, 2026-11-07: "압축 풀기 버튼을 누르면 바로 그 압축 파일이 풀리는데 ... 어디 저장할지 묻고, 저장하고 나면 그
   폴더를 열거나, 외부 윈도 기본 탐색기에서 그 폴더를 열거나"): the folder asked in a card, the archive's own offered */
static archwork_t *g_dest_wait;   /* the work kept while its folder is asked */
static void extract_go(app_state_t *app, archwork_t *w);

static void extract_start(app_state_t *app, archwork_t *w, int mode, bool choose_folder) {
    if (w->count == 0) { work_free(w); osd_say(app, TU8("pick an archive first")); return; }
    w->mode = (rubraview_extract_mode_t)mode;
    if (choose_folder) {
        if (g_dest_wait) work_free(g_dest_wait);
        g_dest_wait = w;
        proven_u8str_view_t beside = rubraview_path_dirname(proven_u8str_view_from_cstr(w->archives[0]));
        if (app->rename_active) rename_end(app);
        rename_field_begin(app);
        rubraview_textedit_set(&app->rename_ec.te, beside.size < sizeof(app->rename_buffer) ? beside : PROVEN_LIT(""));
        rubraview_textedit_select_all(&app->rename_ec.te);
        app->rename_active = true;
        app->rename_is_extract = true;
        app->rename_composing_length = 0;
        rubraview_pal_window_text_input(app->window, true);
        return;
    }
    extract_go(app, w);
}

void archwork_extract_dest_typed(app_state_t *app) {
    archwork_t *w = g_dest_wait;
    if (!w) { rename_end(app); return; }
    proven_u8str_view_t typed = rubraview_target_folder_trim(rubraview_textedit_text(&app->rename_ec.te));
    proven_u8str_view_t base = rubraview_path_dirname(proven_u8str_view_from_cstr(w->archives[0]));
    proven_u8str_view_t path = typed.size ? rubraview_picker_typed_path(app->arena, typed, base) : base;
    rubraview_fs_entry_t found;
    if (path.size == 0 || path.size >= sizeof w->dest) { osd_say(app, TU8("that folder could not be made")); return; }
    if (rubraview_pal_fs_stat(app->arena, path, &found) && !found.is_directory) { osd_say(app, TU8("that is a file, not a folder - put it right, or Esc")); return; }
    if (!rubraview_pal_fs_exists(path) && !rubraview_pal_fs_make_dirs(path)) { osd_say(app, TU8("that folder could not be made")); return; }
    proven_sys_mem_copy(w->dest, path.ptr, path.size);
    w->dest[path.size] = '\0';
    g_dest_wait = NULL;
    rename_end(app);
    extract_go(app, w);
}

void archwork_extract_dest_dropped(app_state_t *app) {
    if (g_dest_wait) { work_free(g_dest_wait); g_dest_wait = NULL; osd_say(app, TU8("not extracted")); }
}

void archwork_extract_dest_browse(app_state_t *app) {
    rubraview_file_dialog_opts_t opts = { .title = T("Extract into which folder"), .folder_mode = true,
                                          .parent_window_handle = rubraview_pal_window_native_handle(app->window) };
    rubraview_file_dialog_result_t picked = rubraview_pal_file_dialog_pick_folder(app->arena, &opts);
    if (picked.accepted && picked.count > 0 && picked.paths[0].size < sizeof(app->rename_buffer))
        rubraview_textedit_set(&app->rename_ec.te, picked.paths[0]);   /* in the box, for Enter */
}

static void extract_go(app_state_t *app, archwork_t *w) {
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

/* RV-272: Explorer's Extract, in the extraction's own window: into `dest` when one was chosen, else beside each. */
void archwork_extract_windowed(app_state_t *app, const proven_u8str_view_t *paths, size_t count, int mode, proven_u8str_view_t dest) {
    archwork_t *w = rubraview_alloc_zero(1, sizeof *w);
    if (!w) return;
    w->windowed = true;
    w->count = given_paths(paths, count, &w->archives, true);
    if (w->count == 0) { work_free(w); extractwin_finished(app, false, false, TU8("no archive among them"), PROVEN_LIT("")); return; }
    if (dest.size > 0 && dest.size < sizeof w->dest) { proven_sys_mem_copy(w->dest, dest.ptr, dest.size); w->dest[dest.size] = '\0'; }
    w->mode = (rubraview_extract_mode_t)mode;
    extract_go(app, w);
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

bool archwork_windowed(const app_state_t *app) {
    return (app->archwork && app->archwork->windowed) || (g_waiting && g_waiting->windowed);
}

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
    double split = rubraview_settings_get(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("pack_split_bytes"));   /* RV-248 */
    app->pack_split_len = rubraview_pack_split_text(app->pack_split, sizeof app->pack_split, split > 0.0 ? (uint64_t)split : 0u);
    app->pack_names_hidden = rubraview_settings_get(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("pack_names")) > 0.5;
}

static void compress_go(app_state_t *app, archwork_t *w, bool ask) {
    if (w->source_count == 0) { work_free(w); osd_say(app, TU8("pick the files to compress first")); return; }
    sources_dir(w);
    if (!ask) {
        offered_name(app, w, "zip", w->output, sizeof w->output);
        pack_start(app, w, RUBRAVIEW_PACK_ZIP, 6, RUBRAVIEW_PACK_METHOD_DEFAULT, false, NULL);
        return;
    }
    /* RV-294 (owner, 2026-11-16: "압축을 실행하는 분리된 프로세스에서 별개의 창으로"): the window is the job's; it is
       handed the offered name and the kept choices (D-266), and sends back what was chosen to be kept again. */
    pack_choices_load(app);
    bool sevenz = rubraview_settings_get(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("pack_format")) > 0.5;
    offered_name(app, w, sevenz ? "7z" : "zip", w->output, sizeof w->output);
    uint64_t split = 0;
    (void)rubraview_pack_split_parse((proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->pack_split, .size = app->pack_split_len }, &split);
    w->ask = true;
    w->compress = true;
    w->pk.format = sevenz ? RUBRAVIEW_PACK_7Z : RUBRAVIEW_PACK_ZIP;
    w->pk.level = app->pack_level;   /* the step: the window's chips */
    w->pk.method = (rubraview_pack_method_t)app->pack_method;
    w->pk.solid_off = app->pack_solid_off;
    w->pk.volume_bytes = split;
    w->pk.names_hidden = app->pack_names_hidden;
    w->pk.sources = (const char *const *)w->sources;
    w->pk.source_count = w->source_count;
    w->pk.output = w->output;
    work_start(app, w);
}

/* RV-294: the Compress window's choices, sent by its job, kept for the next one (D-266). */
void archwork_pack_choices_taken(app_state_t *app, bool sevenz, int32_t level, int32_t method, bool solid_off, uint64_t split, bool names_hidden) {
    double v[] = { sevenz ? 1.0 : 0.0, level >= 0 && level < RUBRAVIEW_PACK_LEVEL_STEPS ? level : RUBRAVIEW_PACK_LEVEL_NORMAL,
                   method >= RUBRAVIEW_PACK_METHOD_DEFAULT && method <= RUBRAVIEW_PACK_METHOD_ZSTD ? method : RUBRAVIEW_PACK_METHOD_DEFAULT,
                   solid_off ? 1.0 : 0.0, (double)split, names_hidden ? 1.0 : 0.0 };
    static const char *const KEYS[] = { "pack_format", "pack_level", "pack_method", "pack_solid", "pack_split_bytes", "pack_names" };
    for (size_t i = 0; i < sizeof KEYS / sizeof *KEYS; ++i)
        rubraview_settings_set(&app->settings, PROVEN_LIT("archive"), proven_u8str_view_from_cstr(KEYS[i]), v[i]);
    settings_write_if_changed(app, false);
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

/* ---- each pass ---- */

/* RV-268: where it is, for the progress card (it was a line at the top). */
static void say_progress(app_state_t *app, archwork_t *w) {
    progress_view_t v = { .source = PROGRESS_ARCHWORK, .bytes = true, .can_stop = true, .windowed = w->windowed };
    if (w->compress) {
        int phase = (int)w->prog.skipped;   /* RV-292: the job sends the compression's phase there */
        v.done = w->prog.bytes_done;
        v.total = w->prog.bytes_total;
        v.stopping = w->stopping;
        if (phase == 1) { v.files_at = (uint32_t)w->prog.done; v.files_total = (uint32_t)w->prog.total; }
        proven_u8str_view_t name = rubraview_path_basename(proven_u8str_view_from_cstr(w->output));
        rubraview_fmt(v.title, sizeof v.title, phase == 0 ? T("Listing the files for {}") : phase == 2 ? T("Checking {}") : T("Compressing into {}"),
                      PROVEN_ARG(rubraview_bytes_view((const char *)name.ptr, (int)name.size)));
    } else {
        v.done = w->prog.bytes_done;
        v.total = w->prog.bytes_total;
        v.stopping = w->stopping;
        v.files_at = (uint32_t)w->prog.done;
        v.files_total = (uint32_t)w->prog.total;
        w->at = (size_t)w->prog.failed;   /* RV-292: the archive it is on */
        size_t at = w->at < w->count ? w->at : w->count - 1;
        proven_u8str_view_t name = rubraview_path_basename(proven_u8str_view_from_cstr(w->archives[at]));
        if (w->count > 1) rubraview_fmt(v.title, sizeof v.title, T("Extracting {} ({} of {})"), PROVEN_ARG(rubraview_bytes_view((const char *)name.ptr, (int)name.size)),
                                        PROVEN_ARG((size_t)(at + 1)), PROVEN_ARG((size_t)w->count));
        else rubraview_fmt(v.title, sizeof v.title, T("Extracting {}"), PROVEN_ARG(rubraview_bytes_view((const char *)name.ptr, (int)name.size)));
    }
    rubraview_fmt(v.note, sizeof v.note, "{}", PROVEN_ARG(T("Esc or Stop stops it")));
    progress_show(app, &v);
}

/* RV-292: the job's answer where the work's thread left it: the result and counts, the folder, the reason, the names
   parked (each archive's index, its folder, the entry, its size and time, the path taken), and for a compression what
   was made. */
static void archwork_take_done(archwork_t *w, const uint8_t *fields, size_t len) {
    rubraview_jobwire_msg_t m = { .type = RUBRAVIEW_JOBWIRE_ARCHIVE_DONE, .p = fields, .left = len };
    rubraview_jobwire_archive_done_t d;
    rubraview_jobwire_get_archive_done(&m, &d);
    if (m.bad) d = (rubraview_jobwire_archive_done_t){ .result = RUBRAVIEW_ARCHIVE_CANNOT_READ };
    if (w->compress) {
        w->pk.result = (rubraview_archive_result_t)d.result;
        rubraview_fmt(w->pk.written, sizeof w->pk.written, "{}", PROVEN_ARG(d.made));
        rubraview_fmt(w->pk.set_name, sizeof w->pk.set_name, "{}", PROVEN_ARG(d.set_name));
        rubraview_fmt(w->pk.why, sizeof w->pk.why, "{}", PROVEN_ARG(d.why));
        w->pk.files = d.files;
        w->pk.folders = d.folders;
        w->pk.older_parts = d.older_parts;
        return;
    }
    w->ex.result = (rubraview_archive_result_t)d.result;
    w->written += d.written; w->skipped += d.skipped; w->failed += d.failed;
    w->at = d.at;
    rubraview_fmt(w->ex.why, sizeof w->ex.why, "{}", PROVEN_ARG(d.why));
    if (d.root.size) rubraview_fmt(w->last_root, sizeof w->last_root, "{}", PROVEN_ARG(d.root));
    for (uint32_t k = 0; k < d.parked && !m.bad; ++k) {
        uint32_t arc = rubraview_jobwire_get_u32(&m);
        proven_u8str_view_t root = rubraview_jobwire_get_str(&m);
        uint64_t entry = rubraview_jobwire_get_u64(&m), size = rubraview_jobwire_get_u64(&m);
        int64_t mtime = rubraview_jobwire_get_i64(&m);
        proven_u8str_view_t path = rubraview_jobwire_get_str(&m);
        if (m.bad) break;
        if (w->parked_n == w->parked_cap) {
            size_t cap = w->parked_cap ? w->parked_cap * 2 : 16;
            xpark_t *grown = rubraview_alloc(cap * sizeof *grown);
            if (!grown) break;
            if (w->parked) proven_sys_mem_copy(grown, w->parked, w->parked_n * sizeof *grown);
            rubraview_free(w->parked);
            w->parked = grown;
            w->parked_cap = cap;
        }
        xpark_t *x = &w->parked[w->parked_n++];
        *x = (xpark_t){ .archive = arc < w->count ? arc : 0 };
        rubraview_fmt(x->root, sizeof x->root, "{}", PROVEN_ARG(root));
        x->p = (rubraview_extract_parked_t){ .index = (size_t)entry, .size = size, .mtime = mtime, .path = rubraview_strndup_view(path) };
    }
}

static size_t xrows_take(app_state_t *app, archwork_t *w);   /* RV-258, below */
static void xrows_carry_out(app_state_t *app);

void archwork_poll(app_state_t *app, bool quitting) {
    archwork_t *w = app->archwork;
    if (!w && !quitting) xrows_carry_out(app);   /* RV-258: what was answered about names taken */
    if (!w) return;
    if (quitting) {   /* RV-292: the viewer goes; the work goes on in its own process */
        filejob_archive_forget(w->job);
        app->archwork = NULL;
        work_free(w);
        return;
    }
    const uint8_t *fields = NULL;
    size_t flen = 0;
    int state = filejob_archive_state(w->job, &w->prog, &fields, &flen);
    if (state == 0) { if (!w->ask) say_progress(app, w); return; }   /* RV-294: an asked compression shows in its own windows */
    app->archwork = NULL;
    progress_end(app, PROGRESS_ARCHWORK);   /* RV-268 */
    if (state < 0) {   /* its process went before it said how it went */
        filejob_archive_forget(w->job);
        osd_say(app, TU8("the work's process ended without its report: see the folders"));
        app_log(app, "archive", PROVEN_LIT("an extraction's or a compression's process ended before it said how it went"));
        jobnotes_check(app);
        if (w->windowed) extractwin_finished(app, false, false, TU8("the work's process ended without its report"), PROVEN_LIT(""));
        if (app->picker_open) picker_navigate(app, app->picker_dir);
        work_free(w);
        return;
    }
    archwork_take_done(w, fields, flen);   /* its answer into the fields this file read when the work was its thread's */
    filejob_archive_forget(w->job);
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
        if (w->windowed) {   /* RV-272: asked in its own window */
            extractwin_ask_password(app, proven_u8str_view_from_cstr(w->archives[w->at]), r == RUBRAVIEW_ARCHIVE_PASSWORD_WRONG);
            return;
        }
        if (app->rename_active) rename_end(app);
        rename_field_begin(app);
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
        size_t asked = xrows_take(app, w);   /* RV-258: the names taken, to the clashes window */
        if (asked) {
            size_t len = proven_cstr_len(line);
            rubraview_fmt(line + len, sizeof line - len, T("; {} names already there are asked (Ctrl+Shift+K)"), PROVEN_ARG(asked));
        }
        if (w->windowed) {   /* RV-272: its own window says it, and the setting decides what follows */
            extractwin_finished(app, r == RUBRAVIEW_ARCHIVE_DONE && w->failed == 0, asked > 0, cstr(line), cstr(w->last_root));
            work_free(w);
            return;
        }
        if (w->written > 0 && w->last_root[0]) {   /* RV-240: what next, asked */
            rubraview_fmt(app->extract_done_line, sizeof app->extract_done_line, "{}", PROVEN_ARG(line));
            rubraview_fmt(app->extract_done_path, sizeof app->extract_done_path, "{}", PROVEN_ARG(w->last_root));
            app->extract_done = true;
        }
    } else {
        rubraview_fmt(line, sizeof line, "{}{}{}", PROVEN_ARG(rubraview_archive_result_text(r)), PROVEN_ARG(w->ex.why[0] ? "  -  " : ""), PROVEN_ARG(w->ex.why));
        if (w->windowed) { extractwin_finished(app, false, false, cstr(line), cstr(w->last_root)); work_free(w); return; }   /* RV-272 */
    }
    osd_say(app, cstr(line));
    if (app->picker_open) picker_navigate(app, app->picker_dir);
    work_free(w);
}

/* ---- RV-258's last step: names taken by an extraction, asked with the copies' (D-310) ---- */

typedef struct xrow {
    size_t id;
    char *archive, *root, *inner, *coming, *password;
    size_t entry;
    char *target;
    uint64_t csize, tsize;
    int64_t cmtime, tmtime;
    int answer;               /* -1 not yet, else a rubraview_fileop_answer_t */
} xrow_t;

static xrow_t *g_x;
static size_t g_xn, g_xcap, g_xid;
static uint32_t g_xversion;

static void xrow_free(xrow_t *r) {
    rubraview_free(r->archive); rubraview_free(r->root); rubraview_free(r->inner); rubraview_free(r->coming); rubraview_free(r->target);
    if (r->password) { secure_wipe(rubraview_mem_mut(r->password, proven_cstr_len(r->password))); rubraview_free(r->password); }
}

/* The work's parked entries become rows of the clashes window. */
static size_t xrows_take(app_state_t *app, archwork_t *w) {
    size_t added = 0;
    for (size_t i = 0; i < w->parked_n; ++i) {
        xpark_t *x = &w->parked[i];
        if (g_xn == g_xcap) {
            size_t cap = g_xcap ? g_xcap * 2 : 32;
            xrow_t *grown = rubraview_alloc(cap * sizeof *grown);
            if (!grown) break;
            if (g_x) proven_sys_mem_copy(grown, g_x, g_xn * sizeof *grown);
            rubraview_free(g_x);
            g_x = grown;
            g_xcap = cap;
        }
        rubraview_fs_entry_t st = {0};
        (void)rubraview_pal_fs_stat(app->arena, proven_u8str_view_from_cstr(x->p.path), &st);
        char coming[RUBRAVIEW_ARCHIVE_PATH * 2];
        proven_u8str_view_t name = rubraview_path_basename(proven_u8str_view_from_cstr(x->p.path));
        (void)rubraview_fmt(coming, sizeof coming, "{}/{}", PROVEN_ARG(w->archives[x->archive]), PROVEN_ARG(name));
        g_x[g_xn++] = (xrow_t){ .id = ++g_xid, .archive = rubraview_strdup(w->archives[x->archive]), .root = rubraview_strdup(x->root),
                                .inner = w->only ? rubraview_strdup(w->inner) : NULL, .coming = rubraview_strdup(coming),
                                .password = w->password[0] ? rubraview_strdup(w->password) : NULL, .entry = x->p.index,
                                .target = rubraview_strdup(x->p.path), .csize = x->p.size, .tsize = st.size_bytes,
                                .cmtime = x->p.mtime, .tmtime = st.mtime, .answer = -1 };
        added++;
    }
    if (added) g_xversion++;
    return added;
}

size_t archwork_clash_rows(xclash_row_t *out, size_t cap) {
    size_t n = 0;
    for (size_t i = 0; i < g_xn; ++i) {
        if (g_x[i].answer >= 0) continue;
        if (out && n < cap) out[n] = (xclash_row_t){ .index = g_x[i].id, .coming = g_x[i].coming, .there = g_x[i].target,
                                                     .csize = g_x[i].csize, .tsize = g_x[i].tsize, .cmtime = g_x[i].cmtime, .tmtime = g_x[i].tmtime };
        n++;
    }
    return n;
}

uint32_t archwork_clash_version(void) { return g_xversion; }

void archwork_clash_answer(app_state_t *app, size_t id, rubraview_fileop_answer_t answer) {
    (void)app;
    for (size_t i = 0; i < g_xn; ++i) if (g_x[i].id == id && g_x[i].answer < 0) { g_x[i].answer = (int)answer; g_xversion++; }
}

void archwork_clash_answer_all(app_state_t *app, rubraview_fileop_answer_t answer) {
    (void)app;
    bool any = false;
    for (size_t i = 0; i < g_xn; ++i) if (g_x[i].answer < 0) { g_x[i].answer = (int)answer; any = true; }
    if (any) g_xversion++;
}

static bool same_text(const char *a, const char *b) { return (!a && !b) || (a && b && rubraview_cstr_cmp(a, b) == 0); }

/* Once a pass, when no other work runs: the answered rows of one archive and folder written out as answered. */
static void xrows_carry_out(app_state_t *app) {
    if (app->archwork || g_waiting) return;
    size_t first = SIZE_MAX;
    for (size_t i = 0; i < g_xn && first == SIZE_MAX; ++i) if (g_x[i].answer >= 0) first = i;
    if (first == SIZE_MAX) return;
    /* the group's keys, copied: the rows (the first among them) are freed as they are taken */
    char *lead_archive = rubraview_strdup(g_x[first].archive), *lead_root = rubraview_strdup(g_x[first].root);
    char *lead_inner = g_x[first].inner ? rubraview_strdup(g_x[first].inner) : NULL;
    bool has_inner = g_x[first].inner != NULL;
    if (!lead_archive || !lead_root || (has_inner && !lead_inner)) {
        rubraview_free(lead_archive); rubraview_free(lead_root); rubraview_free(lead_inner);
        return;
    }
    xrow_t lead = { .archive = lead_archive, .root = lead_root, .inner = lead_inner };
    size_t n = 0;
    for (size_t i = 0; i < g_xn; ++i)
        if (g_x[i].answer >= 0 && same_text(g_x[i].archive, lead.archive) && same_text(g_x[i].root, lead.root) && same_text(g_x[i].inner, lead.inner)) n++;
    archwork_t *w = rubraview_alloc_zero(1, sizeof *w);
    size_t *only = w ? rubraview_alloc_zero(n, sizeof *only) : NULL;
    uint8_t *rules = w ? rubraview_alloc_zero(n, 1) : NULL;
    char **archives = w ? rubraview_alloc_zero(1, sizeof *archives) : NULL;
    if (!w || !only || !rules || !archives || !(archives[0] = rubraview_strdup(lead.archive))) {
        rubraview_free(only); rubraview_free(rules); rubraview_free(archives); rubraview_free(w);
        rubraview_free(lead_archive); rubraview_free(lead_root); rubraview_free(lead_inner);
        return;
    }
    size_t k = 0, recycled = 0, kept = 0;
    for (size_t i = 0; i < g_xn;) {
        xrow_t *r = &g_x[i];
        bool mine = r->answer >= 0 && same_text(r->archive, lead.archive) && same_text(r->root, lead.root) && same_text(r->inner, lead.inner);
        if (!mine) { ++i; continue; }
        rubraview_fileop_answer_t a = (rubraview_fileop_answer_t)r->answer;
        uint8_t rule = a == RUBRAVIEW_FILEOP_ANSWER_KEEP_BOTH ? RUBRAVIEW_EXISTS_NUMBER
                     : a == RUBRAVIEW_FILEOP_ANSWER_SAME_ONLY ? RUBRAVIEW_EXISTS_SAME
                     : a == RUBRAVIEW_FILEOP_ANSWER_OVERWRITE ? RUBRAVIEW_EXISTS_REPLACE : RUBRAVIEW_EXISTS_SKIP;
        /* overwrite: the one there to the Recycle Bin first, as a copy's (D-310); refused, it stays and is left */
        if (rule == RUBRAVIEW_EXISTS_REPLACE) {
            if (rubraview_pal_fs_recycle(proven_u8str_view_from_cstr(r->target))) recycled++;
            else rule = RUBRAVIEW_EXISTS_SKIP;
        }
        if (rule != RUBRAVIEW_EXISTS_SKIP) { only[k] = r->entry; rules[k] = rule; k++; } else kept++;
        if (k == 1 && rule != RUBRAVIEW_EXISTS_SKIP && r->password) rubraview_fmt(w->password, sizeof w->password, "{}", PROVEN_ARG(r->password));
        if (r->inner) rubraview_fmt(w->inner, sizeof w->inner, "{}", PROVEN_ARG(r->inner));
        rubraview_fmt(w->dest, sizeof w->dest, "{}", PROVEN_ARG(r->root));
        xrow_free(r);
        g_x[i] = g_x[--g_xn];
    }
    g_xversion++;
    rubraview_free(lead_archive); rubraview_free(lead_root); rubraview_free(lead_inner);
    lead = (xrow_t){0};
    char line[256];
    if (k == 0) {
        rubraview_fmt(line, sizeof line, T("{} names already there left as they were"), PROVEN_ARG(kept));
        osd_say(app, cstr(line));
        free_list(archives, 1); rubraview_free(only); rubraview_free(rules); rubraview_free(w);
        return;
    }
    w->archives = archives;
    w->count = 1;
    w->mode = RUBRAVIEW_EXTRACT_AS_IS;   /* into the folder the first writing went into, the same names */
    w->only = only;
    w->only_count = k;
    w->rules = rules;
    if (!has_inner) w->inner[0] = '\0';
    app_log(app, "clash", PROVEN_LIT("names taken by an extraction: written out as answered"));
    (void)work_start(app, w);
}

void archwork_clash_shutdown(void) {
    for (size_t i = 0; i < g_xn; ++i) xrow_free(&g_x[i]);
    rubraview_free(g_x);
    g_x = NULL;
    g_xn = g_xcap = 0;
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
