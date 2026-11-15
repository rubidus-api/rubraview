/* RV-258 (owner, 2026-11-08: "압축파일 내에서 오건 어디서 오건 파일 관리자에서 동일한 이름의 파일이 있고 그걸 덮어씌우려 할 때,
   그 파일은 일단 킵해놓고 일괄적으로 묻게 해 줘요 ..."; D-310): copies and moves of the file manager as jobs of
   rubraview_fileop, the clashes they park asked together.

   RV-289 (owner, 2026-11-15: "이것들은 어떤 독립된 프로세스로 따로 처리하는 것이 바람직하지 않을까요? 메인 창이 죽거나 해도
   그대로 이어서 진행되고, 반대로 이 프로세스가 죽어도 메인 창이 죽지 않게요"; "작업마다 프로세스"): each job now runs in a
   process of its own (jobproc.c), started here with a pipe of its own; this side only sends the job, shows how far
   it is, and when it ends takes its items for the undo, the basket, the clipboard and the screen, as before. A job
   whose process goes away without its end is said and forgotten. The viewer keeps the clashes window (clashui) for
   an extraction's and an archive's questions, which run here still. */
#include "app.h"
#include "clashui.h"
#include "rubraview/fileop.h"
#include "rubraview/jobwire.h"
#include "rubraview/jobnote.h"
#include <sddl.h>

#define FJ_ARCADD 0xFFFFFFFFu   /* RV-260: a row of a writing into an archive (arcadd.c), answered on this thread */
#define FJ_EXTRACT 0xFFFFFFFEu  /* RV-258: a row of an extraction (archwork.c), answered on this thread */
enum { FJ_JOBS = 16 };

typedef struct fj_item { char *source, *target, *why; uint8_t did, state; bool overwrote; int32_t parent; } fj_item_t;

typedef struct fj_job {
    HANDLE   pipe;
    bool     connected, broken, ended;
    double   started;
    rubraview_jobwire_out_t out;   /* waiting to be written */
    uint8_t *in;
    size_t   in_len, in_cap;
    bool     moving;
    int      basket;          /* the basket it empties as it moves (0..4), or -1 */
    bool     from_cut;        /* a paste of a cut: the clipboard cleared after */
    int32_t  viewer_page;     /* RV-259: the viewer's own Copy / Move to folder of the page on screen, or -1 */
    char    *viewer_path;
    char    *temp_dir;        /* removed by the job; by us only when the job never started */
    rubraview_jobwire_progress_t progress;
    fj_item_t *items;
    size_t   nitems, cap;
    rubraview_jobwire_end_t end;
    /* RV-292: an extraction's or a compression's job: its answer kept for archwork, not ended here */
    bool     archive;
    int32_t  id;
    uint8_t *done_fields;
    size_t   done_len;
} fj_job_t;

static fj_job_t *g_jobs[FJ_JOBS];
static size_t    g_njobs;
static int32_t   g_next_id;
static unsigned  g_serial;
static bool      g_same_recycles_copy;   /* the copy's same file: the last the reader chose */

static clashui_t g_ui;   /* an extraction's and an archive's questions */
static uint32_t  m_aa_version, m_xx_version;

bool filejob_same_recycles_copy(void) { return g_same_recycles_copy; }

bool filejob_busy(void) {
    for (size_t j = 0; j < g_njobs; ++j) if (!g_jobs[j]->ended) return true;
    return false;
}

static void items_free(fj_job_t *job) {
    for (size_t i = 0; i < job->nitems; ++i) { rubraview_free(job->items[i].source); rubraview_free(job->items[i].target); rubraview_free(job->items[i].why); }
    rubraview_free(job->items);
    job->items = NULL;
    job->nitems = job->cap = 0;
}

static void job_free(fj_job_t *job) {
    if (job->pipe && job->pipe != INVALID_HANDLE_VALUE) CloseHandle(job->pipe);
    rubraview_jobwire_out_free(&job->out);
    rubraview_free(job->in);
    items_free(job);
    rubraview_free(job->viewer_path);
    rubraview_free(job->temp_dir);
    rubraview_free(job->done_fields);
    rubraview_free(job);
}

/* ---- starting a job's process ---- */

/* A pipe only this user may open, one end, not across the network, that never makes us wait. */
static HANDLE make_pipe(wchar_t *name, size_t cap) {
    SECURITY_ATTRIBUTES sa = { .nLength = sizeof sa };
    PSECURITY_DESCRIPTOR sd = NULL;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;OW)(A;;GA;;;SY)", SDDL_REVISION_1, &sd, NULL)) return INVALID_HANDLE_VALUE;
    sa.lpSecurityDescriptor = sd;
    DWORD session = 0;
    (void)ProcessIdToSessionId(GetCurrentProcessId(), &session);
    HANDLE h = INVALID_HANDLE_VALUE;
    for (int tries = 0; tries < 4 && h == INVALID_HANDLE_VALUE; ++tries) {
        LARGE_INTEGER q;
        QueryPerformanceCounter(&q);
        unsigned noise = (unsigned)(q.QuadPart ^ (q.QuadPart >> 32) ^ (GetTickCount64() * 2654435761u) ^ ((uintptr_t)&q >> 4));
        char n8[160];
        int k = rubraview_fmt(n8, sizeof n8, "\\\\.\\pipe\\rubraview-job-{}-{}-{}-{:08x}", PROVEN_ARG((uint32_t)session),
                              PROVEN_ARG((uint32_t)GetCurrentProcessId()), PROVEN_ARG(++g_serial), PROVEN_ARG(noise));
        if (k <= 0 || (size_t)k >= sizeof n8 || MultiByteToWideChar(CP_UTF8, 0, n8, -1, name, (int)cap) <= 0) continue;
        h = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                             PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 1u << 16, 1u << 16, 0, &sa);
    }
    LocalFree(sd);
    return h;
}

/* ---- RV-292: the notes the jobs leave while they run ---- */

static char g_notes_dir[2048];
static double g_notes_at;

static proven_u8str_view_t jobnotes_dir(app_state_t *app) {
    proven_u8str_view_t dir = rubraview_path_dirname(app->settings_path);
    int k = dir.size ? rubraview_fmt(g_notes_dir, sizeof g_notes_dir, "{}\\jobs", PROVEN_ARG(dir)) : -1;
    return k > 0 && (size_t)k < sizeof g_notes_dir ? cstr(g_notes_dir) : PROVEN_LIT("");
}

static const char *kind_words(const char *kind) {
    if (rubraview_cstr_cmp(kind, "move") == 0) return T("a move");
    if (rubraview_cstr_cmp(kind, "extract") == 0) return T("an extraction");
    if (rubraview_cstr_cmp(kind, "archive") == 0) return T("a writing into an archive");
    if (rubraview_cstr_cmp(kind, "compress") == 0) return T("a compression");
    return T("a copy");
}

/* The notes whose process is no longer running: said in the log, then removed; the OSD says how many. */
void jobnotes_check(app_state_t *app) {
    g_notes_at = rubraview_pal_time_now_seconds();
    proven_u8str_view_t dir = jobnotes_dir(app);
    if (dir.size == 0 || !rubraview_pal_fs_exists(dir)) return;
    rubraview_scratch_t sc;
    proven_arena_t *sa = scratch_begin(app, &sc, 1u << 20);
    rubraview_fs_listing_t list = rubraview_pal_fs_list_dir(sa, dir);
    size_t said = 0;
    for (size_t i = 0; i < list.count; ++i) {
        const rubraview_fs_entry_t *e = &list.entries[i];
        proven_u8str_view_t ext = rubraview_path_ext(e->name);
        if (e->is_directory || !proven_u8str_view_eq(ext, PROVEN_LIT(".job"))) continue;
        proven_u8str_view_t text = rubraview_pal_fs_read_file(sa, e->path, 64u << 10);
        rubraview_jobnote_t note;
        if (!rubraview_jobnote_parse(text, &note)) continue;
        uint64_t started = 0;
        bool running = rubraview_pal_process_started(note.pid, &started);
        if (!rubraview_jobnote_stale(&note, running, started)) continue;
        char when[64], line[2600];
        info_local_time(when, sizeof when, note.when);
        int n = rubraview_fmt(line, sizeof line, T("{} did not end as it should (its process is gone): {} of {} done, into {}, the first {}, started {}"),
                              PROVEN_ARG(kind_words(note.kind)), PROVEN_ARG(note.done), PROVEN_ARG(note.count), PROVEN_ARG(note.where),
                              PROVEN_ARG(note.first), PROVEN_ARG(when));
        if (n > 0) app_log(app, "job", (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n < sizeof line ? (size_t)n : sizeof line - 1 });
        if (rubraview_cstr_cmp(note.kind, "copy") == 0 || rubraview_cstr_cmp(note.kind, "move") == 0)   /* not removed for it: another job there may be writing its own */
            app_log(app, "job", TU8("a file named .rubraview-tmp-... in that folder is the one it was writing when it stopped: it can be deleted"));
        (void)rubraview_pal_fs_delete(e->path);
        said++;
    }
    scratch_end(app, &sc);
    if (said) {
        char words[200];
        int n = rubraview_fmt(words, sizeof words, T("{} file jobs did not end as they should: see the log (Ctrl+Shift+L)"), PROVEN_ARG(said));
        if (n > 0) osd_say(app, cstr(words));
    }
}

static void put_start(fj_job_t *job, proven_u8str_view_t dir, const proven_u8str_view_t *sources, size_t n, app_state_t *app) {
    rubraview_jobwire_start_t st = {
        .moving = job->moving, .dir = dir, .basket = job->basket, .from_cut = job->from_cut, .viewer_page = job->viewer_page,
        .viewer_path = cstr(job->viewer_path ? job->viewer_path : ""), .temp_dir = cstr(job->temp_dir ? job->temp_dir : ""),
        .same_recycles_copy = g_same_recycles_copy, .lang_file = cstr(lang_file_in_use()),
        .ui_font = rubraview_settings_get_text(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("ui_font")),
        .journal_dir = jobnotes_dir(app),
    };
    rubraview_jobwire_put_start(&job->out, &st, sources, n);
}

/* The job's pipe made, its first message written by `put` (START, or ARCHIVE), and the process started. False (the
   job freed, the OSD told) when it could not be. */
static bool spawn_with(app_state_t *app, fj_job_t *job, void (*put)(fj_job_t *, app_state_t *, const void *), const void *what) {
    wchar_t name[160];
    job->pipe = make_pipe(name, sizeof name / sizeof name[0]);
    if (job->pipe == INVALID_HANDLE_VALUE) { job->pipe = NULL; job_free(job); osd_say(app, TU8("the copy could not be started")); return false; }
    put(job, app, what);
    if (job->out.failed || job->out.len == 0) { job_free(job); osd_say(app, TU8("the copy could not be started")); return false; }
    /* the same program, a job of its own: "<exe>" --file-job <pipe> */
    char line[4096], pipe8[400];
    int pl = WideCharToMultiByte(CP_UTF8, 0, name, -1, pipe8, (int)sizeof pipe8, NULL, NULL);
    proven_u8str_view_t exe = rubraview_pal_process_executable(app->arena);
    int k = pl > 0 ? rubraview_fmt(line, sizeof line, "\"{}\" --file-job {}", PROVEN_ARG(exe), PROVEN_ARG(pipe8)) : -1;
    if (k <= 0 || (size_t)k >= sizeof line || exe.size == 0 ||
        !rubraview_pal_process_start_apart((proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)k })) {
        job_free(job);
        osd_say(app, TU8("the copy could not be started"));
        return false;
    }
    return true;
}

typedef struct { proven_u8str_view_t dir; const proven_u8str_view_t *sources; size_t n; } start_what_t;
static void put_start_of(fj_job_t *job, app_state_t *app, const void *what) {
    const start_what_t *w = (const start_what_t *)what;
    put_start(job, w->dir, w->sources, w->n, app);
}

static bool spawn_job(app_state_t *app, fj_job_t *job, proven_u8str_view_t dir, const proven_u8str_view_t *sources, size_t n) {
    start_what_t w = { dir, sources, n };
    return spawn_with(app, job, put_start_of, &w);
}

static bool start_job(app_state_t *app, proven_u8str_view_t dir, const proven_u8str_view_t *sources, size_t n, bool moving, int basket,
                      bool from_cut, int32_t page, proven_u8str_view_t viewer_path, proven_u8str_view_t temp_dir) {
    if (n == 0 || dir.size == 0) return false;
    if (g_njobs >= FJ_JOBS) { osd_say(app, TU8("too many copies at once: wait for one to end")); return false; }
    fj_job_t *job = (fj_job_t *)rubraview_alloc_zero(1, sizeof *job);
    if (!job) return false;
    job->moving = moving;
    job->basket = basket;
    job->from_cut = from_cut;
    job->viewer_page = page;
    job->viewer_path = viewer_path.size ? rubraview_strndup_view(viewer_path) : NULL;
    job->temp_dir = temp_dir.size ? rubraview_strndup_view(temp_dir) : NULL;
    if (!spawn_job(app, job, dir, sources, n)) return false;
    job->started = rubraview_pal_time_now_seconds();
    job->id = ++g_next_id;
    g_jobs[g_njobs++] = job;
    char said[200];
    int w = rubraview_fmt(said, sizeof said, moving ? T("moving {} into {}") : T("copying {} into {}"), PROVEN_ARG(n), PROVEN_ARG(rubraview_path_basename(dir)));
    if (w > 0) osd_say(app, cstr(said));
    return true;
}

/* RV-258: a copy or a move of `sources` into `dir`, every name checked before anything is done; the clashes asked in
   the job's window. `basket` (0..4, or -1) and `from_cut` say what is tidied when it ends. */
bool filejob_start(app_state_t *app, proven_u8str_view_t dir, const proven_u8str_view_t *sources, size_t n, bool moving, int basket, bool from_cut) {
    return start_job(app, dir, sources, n, moving, basket, from_cut, -1, PROVEN_LIT(""), PROVEN_LIT(""));
}

/* RV-259: the file on screen to a folder through the same check; a page inside an archive was written to a folder
   of its own in %TEMP% first (the job removes it). When it ends, a move takes the page out of the list as before. */
bool filejob_start_viewer(app_state_t *app, proven_u8str_view_t dir, proven_u8str_view_t path, bool moving, int32_t page, proven_u8str_view_t temp_dir) {
    return start_job(app, dir, &path, 1, moving, -1, false, page, path, temp_dir);
}

/* ---- a finished job, on the main thread: the undo, the basket, the clipboard, the words ---- */

static void job_ended(app_state_t *app, fj_job_t *job) {
    uint32_t batch = ++app->undo_batch;
    size_t done = 0, replaced = 0, same = 0;
    bool *moved = NULL;
    size_t top = 0;
    for (size_t i = 0; i < job->nitems; ++i) if (job->items[i].parent < 0) top++;
    if (job->basket >= 0 && top) moved = (bool *)rubraview_alloc_zero(top, sizeof *moved);
    size_t t = 0;
    for (size_t i = 0; i < job->nitems; ++i) {
        const fj_item_t *it = &job->items[i];
        bool is_top = it->parent < 0;
        if (it->did == RUBRAVIEW_FILEOP_DID_COPY || it->did == RUBRAVIEW_FILEOP_DID_MOVE || it->did == RUBRAVIEW_FILEOP_DID_MAKE_DIR) {
            rubraview_undo_push(&app->undo, (rubraview_file_action_t){
                .op = it->did == RUBRAVIEW_FILEOP_DID_MOVE ? RUBRAVIEW_FILE_OP_MOVE : RUBRAVIEW_FILE_OP_COPY,
                .source_path = app_keep(app, cstr(it->source)), .target_path = app_keep(app, cstr(it->target)), .batch = batch });
            if (it->did != RUBRAVIEW_FILEOP_DID_MAKE_DIR) done++;
        } else if (it->did == RUBRAVIEW_FILEOP_DID_RECYCLE_SOURCE) {
            rubraview_undo_push(&app->undo, (rubraview_file_action_t){ .op = RUBRAVIEW_FILE_OP_RECYCLE, .source_path = app_keep(app, cstr(it->source)), .batch = batch });
            same++;
        }
        if (it->overwrote) replaced++;
        bool why = it->why && it->why[0];
        if (it->state == RUBRAVIEW_FILEOP_FAILED || (why && it->did == RUBRAVIEW_FILEOP_DID_COPY && job->moving)) {
            char line[700];
            int w = rubraview_fmt(line, sizeof line, "{}: {}", PROVEN_ARG(cstr(it->source)), PROVEN_ARG(it->why ? it->why : ""));
            if (w > 0) app_log(app, "file", (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)w });
        }
        if (is_top) {
            if (moved && t < top) moved[t] = it->did == RUBRAVIEW_FILEOP_DID_MOVE || it->did == RUBRAVIEW_FILEOP_DID_RECYCLE_SOURCE ||
                                             (it->state == RUBRAVIEW_FILEOP_MERGED);
            t++;
        }
    }
    bool moving = job->moving;
    if (moved && moving) rubraview_basket_keep_unmoved(&app->baskets, job->basket, moved);
    rubraview_free(moved);
    if (job->from_cut && moving && done > 0) { dragout_clip_pasted(app); if (job->end.failed == 0) rubraview_pal_clipboard_clear(app->window); }
    char line[260];
    int w = rubraview_fmt(line, sizeof line, moving ? T("{} moved, {} replaced, {} the same, {} left, {} could not be  (Ctrl+Z takes it back)")
                                                   : T("{} copied, {} replaced, {} the same, {} left, {} could not be  (Ctrl+Z takes it back)"),
                          PROVEN_ARG(done), PROVEN_ARG(replaced), PROVEN_ARG(same), PROVEN_ARG(job->end.skipped), PROVEN_ARG(job->end.failed));
    if (w > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)w });
    if (job->end.stopped) app_log(app, "file", PROVEN_LIT("the copy or move was stopped; what was not reached is where it was"));
    if (replaced > 0) app_log(app, "file", PROVEN_LIT("replaced files are in the Recycle Bin; Ctrl+Z takes the new ones away, not the old ones back"));
    if (app->picker_open) picker_navigate(app, app->picker_dir);   /* what arrived, listed */
    if (job->viewer_page >= 0 && job->nitems > 0) {   /* RV-259: the viewer's own; its name there said, a move leaves the list */
        const fj_item_t *it = &job->items[0];
        if (it->state == RUBRAVIEW_FILEOP_DONE || it->did == RUBRAVIEW_FILEOP_DID_RECYCLE_SOURCE) {
            char said[sizeof app->notice];
            proven_u8str_view_t put = rubraview_path_basename(cstr(it->target));
            int k = rubraview_fmt(said, sizeof said, "{}: {}", PROVEN_ARG(moving ? T("moved") : T("copied")), PROVEN_ARG(put));
            if (k > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)said, .size = (size_t)k < sizeof said ? (size_t)k : sizeof said - 1 });
        }
        bool gone = it->did == RUBRAVIEW_FILEOP_DID_MOVE || it->did == RUBRAVIEW_FILEOP_DID_RECYCLE_SOURCE;
        int32_t now = current_page_index(app);
        if (moving && gone && now >= 0 && (size_t)job->viewer_page < page_count(app) && job->viewer_path &&
            rubraview_path_same(app->source.pages[job->viewer_page].path, cstr(job->viewer_path)))
            reopen_after_removal(app, (size_t)job->viewer_page);
    }
}

/* ---- the pipe, without waiting ---- */

static void job_take(app_state_t *app, fj_job_t *job, rubraview_jobwire_msg_t *m) {
    (void)app;
    if (m->type == RUBRAVIEW_JOBWIRE_PROGRESS) {
        rubraview_jobwire_progress_t p;
        rubraview_jobwire_get_progress(m, &p);
        if (rubraview_jobwire_done(m)) job->progress = p;
    } else if (m->type == RUBRAVIEW_JOBWIRE_ITEMS) {
        uint32_t n = rubraview_jobwire_get_u32(m);
        for (uint32_t i = 0; i < n && !m->bad; ++i) {
            rubraview_jobwire_item_t w;
            rubraview_jobwire_get_item(m, &w);
            if (m->bad) break;
            if (job->nitems == job->cap) {
                size_t cap = job->cap ? job->cap * 2 : 64;
                fj_item_t *more = (fj_item_t *)rubraview_realloc((proven_mem_mut_t){ .ptr = (proven_byte_t *)job->items, .size = cap * sizeof *more });
                if (!more) break;
                job->items = more;
                job->cap = cap;
            }
            job->items[job->nitems++] = (fj_item_t){ .source = rubraview_strndup_view(w.source), .target = rubraview_strndup_view(w.target),
                                                     .why = w.why.size ? rubraview_strndup_view(w.why) : NULL, .did = w.did, .state = w.state,
                                                     .overwrote = w.overwrote, .parent = w.parent };
        }
    } else if (m->type == RUBRAVIEW_JOBWIRE_ARCHIVE_DONE && job->archive) {   /* RV-292: kept whole for archwork */
        rubraview_free(job->done_fields);
        job->done_fields = (uint8_t *)rubraview_alloc(m->left ? m->left : 1);
        if (job->done_fields) { proven_sys_mem_copy(job->done_fields, m->p, m->left); job->done_len = m->left; }
        job->ended = true;
    } else if (m->type == RUBRAVIEW_JOBWIRE_END) {
        rubraview_jobwire_end_t e;
        rubraview_jobwire_get_end(m, &e);
        if (rubraview_jobwire_done(m)) { job->end = e; job->ended = true; }
    }
}

static void job_io(app_state_t *app, fj_job_t *job) {
    if (job->broken || job->ended) return;
    if (!job->connected) {
        if (ConnectNamedPipe(job->pipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) job->connected = true;
        else if (GetLastError() == ERROR_NO_DATA) { job->broken = true; return; }   /* came and went */
        else {
            if (rubraview_pal_time_now_seconds() - job->started > 20.0) job->broken = true;   /* it never came */
            return;
        }
    }
    while (job->out.len > 0) {   /* what is waiting, as much as the pipe takes now */
        DWORD wrote = 0;
        if (!WriteFile(job->pipe, job->out.data, (DWORD)(job->out.len > (1u << 16) ? (1u << 16) : job->out.len), &wrote, NULL)) {
            if (GetLastError() != ERROR_PIPE_LISTENING) job->broken = true;   /* ERROR_NO_DATA here: the other end is closing */
            break;
        }
        if (wrote == 0) break;   /* the pipe is full: the rest next pass */
        rubraview_jobwire_out_drop(&job->out, wrote);
    }
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(job->pipe, NULL, 0, NULL, &avail, NULL)) { job->broken = true; break; }
        if (avail == 0) break;
        if (job->in_len + avail > job->in_cap) {
            size_t cap = job->in_cap ? job->in_cap : 4096;
            while (cap < job->in_len + avail) cap *= 2;
            uint8_t *more = (uint8_t *)rubraview_realloc((proven_mem_mut_t){ .ptr = (proven_byte_t *)job->in, .size = cap });
            if (!more) break;
            job->in = more; job->in_cap = cap;
        }
        DWORD got = 0;
        if (!ReadFile(job->pipe, job->in + job->in_len, avail, &got, NULL)) { if (GetLastError() != ERROR_NO_DATA) job->broken = true; break; }
        job->in_len += got;
        size_t at = 0, used = 0;
        rubraview_jobwire_msg_t m;
        int r;
        while ((r = rubraview_jobwire_take(job->in + at, job->in_len - at, &m, &used)) == 1) { at += used; job_take(app, job, &m); }
        if (r < 0) { job->broken = true; break; }
        if (at) { proven_sys_mem_move(job->in, job->in + at, job->in_len - at); job->in_len -= at; }
        if (job->ended) break;
    }
}

/* ---- the viewer's clashes window: an extraction's and an archive's questions ---- */

static void vui_answer(void *ctx, const clashui_row_t *row, rubraview_fileop_answer_t a) {
    app_state_t *app = (app_state_t *)ctx;
    if (row->owner == FJ_ARCADD) arcadd_answer(app, row->index, a);
    else if (row->owner == FJ_EXTRACT) archwork_clash_answer(app, row->index, a);
}
static void vui_answer_all(void *ctx, rubraview_fileop_answer_t a) {
    app_state_t *app = (app_state_t *)ctx;
    arcadd_answer_all(app, a);
    archwork_clash_answer_all(app, a);
    app_log(app, "clash", PROVEN_LIT("an answer for all"));
}
static void vui_recycle(void *ctx, bool on) { (void)ctx; g_same_recycles_copy = on; }

static void vui_ready(app_state_t *app) {
    g_ui.ctx = app;
    g_ui.answer = vui_answer;
    g_ui.answer_all = vui_answer_all;
    g_ui.recycle_set = vui_recycle;
    g_ui.same_recycles_copy = g_same_recycles_copy;
    g_ui.ui_font = rubraview_settings_get_text(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("ui_font"));
}

void clashwin_close(app_state_t *app) {
    if (!g_ui.open) return;
    clashui_close(&g_ui);
    window_block_give(app, &app->clash_block);   /* the questions stay */
}

void clashwin_show(app_state_t *app) {
    if (g_ui.open) return;
    if (g_ui.nrows == 0) {   /* RV-289: a job's questions are in its own window */
        for (size_t j = g_njobs; j-- > 0;) {
            fj_job_t *job = g_jobs[j];
            if (job->ended || job->broken || job->progress.clashes == 0) continue;
            rubraview_jobwire_begin(&job->out, RUBRAVIEW_JOBWIRE_SHOW_CLASHES);
            (void)rubraview_jobwire_finish(&job->out);
            return;
        }
        osd_say(app, TU8("no name clash is waiting"));
        return;
    }
    vui_ready(app);
    if (!clashui_show(&g_ui, window_block_take(app, &app->clash_block))) window_block_give(app, &app->clash_block);
}

void clashwin_toggle(app_state_t *app) { if (g_ui.open) clashwin_close(app); else clashwin_show(app); }

void draw_clash_window(app_state_t *app) { (void)app; clashui_draw(&g_ui); }

size_t clash_pump(app_state_t *app) {
    bool was = g_ui.open;
    size_t n = clashui_pump(&g_ui);
    clashui_tick(&g_ui);
    if (was && !g_ui.open) window_block_give(app, &app->clash_block);
    return n;
}

/* The viewer's own questions (an extraction's, an archive's) taken over when they changed. */
static bool viewer_rows(app_state_t *app) {
    if (arcadd_version() == m_aa_version && archwork_clash_version() == m_xx_version) return false;
    size_t na = arcadd_rows(NULL, 0), nx = archwork_clash_rows(NULL, 0);
    clashui_row_t *rows = na + nx ? (clashui_row_t *)rubraview_alloc_zero(na + nx, sizeof *rows) : NULL;
    size_t k = 0;
    if (rows && nx) {   /* RV-258: the names an extraction found taken */
        xclash_row_t *xr = (xclash_row_t *)rubraview_alloc_zero(nx, sizeof *xr);
        size_t got = xr ? archwork_clash_rows(xr, nx) : 0;
        for (size_t i = 0; i < got; ++i)
            rows[k++] = (clashui_row_t){ .owner = FJ_EXTRACT, .index = xr[i].index, .source = rubraview_strdup(xr[i].coming), .target = rubraview_strdup(xr[i].there),
                                         .ssize = xr[i].csize, .tsize = xr[i].tsize, .smtime = xr[i].cmtime, .tmtime = xr[i].tmtime,
                                         .keep_coming = true, .keep_there = true };
        rubraview_free(xr);
    }
    m_xx_version = archwork_clash_version();
    if (rows && na) {   /* RV-260: the names already in an archive being written into */
        arcadd_row_t *ar = (arcadd_row_t *)rubraview_alloc_zero(na, sizeof *ar);
        size_t got = ar ? arcadd_rows(ar, na) : 0;
        for (size_t i = 0; i < got; ++i) {
            char there[RUBRAVIEW_ARCHIVE_PATH * 2];
            (void)rubraview_fmt(there, sizeof there, "{}/{}", PROVEN_ARG(cstr(ar[i].archive)), PROVEN_ARG(cstr(ar[i].name)));
            rows[k++] = (clashui_row_t){ .owner = FJ_ARCADD, .index = ar[i].index, .source = rubraview_strdup(ar[i].source), .target = rubraview_strdup(there),
                                         .ssize = ar[i].ssize, .tsize = ar[i].tsize, .smtime = ar[i].smtime, .tmtime = ar[i].tmtime,
                                         .keep_coming = true, .keep_there = true };
        }
        rubraview_free(ar);
    }
    m_aa_version = arcadd_version();
    bool grew = k > g_ui.nrows;
    clashui_set_rows(&g_ui, rows, k);
    if (grew && !g_ui.open) clashwin_show(app);   /* new questions: the window comes */
    if (k == 0 && g_ui.open) clashwin_close(app);
    return true;
}

/* Once a pass of the main loop: each job's pipe read and written, finished jobs brought home, the progress card. */
size_t filejob_pump(app_state_t *app) {
    size_t handled = viewer_rows(app) ? 1 : 0;
    if (rubraview_pal_time_now_seconds() - g_notes_at > 60.0) jobnotes_check(app);   /* RV-292: once a minute */
    if (g_njobs == 0) return handled;
    for (size_t j = 0; j < g_njobs;) {
        fj_job_t *job = g_jobs[j];
        job_io(app, job);
        if (job->archive) { ++j; continue; }   /* RV-292: archwork reads it and lets it go */
        if (job->ended) {
            job_ended(app, job);
            handled++;
        } else if (job->broken) {
            if (job->connected) {
                osd_say(app, TU8("the copy's process ended without its report: see the folders (Ctrl+Z cannot take it back)"));
                app_log(app, "file", PROVEN_LIT("a file job's process ended before it said how it went"));
                jobnotes_check(app);   /* RV-292: what its note says */
                if (app->picker_open) picker_navigate(app, app->picker_dir);   /* what did arrive, listed */
            } else {
                osd_say(app, TU8("the copy could not be started"));
                if (job->temp_dir) (void)rubraview_pal_fs_delete_tree(cstr(job->temp_dir));   /* it never took it over */
            }
            handled++;
        } else {
            ++j;
            continue;
        }
        job_free(job);
        g_jobs[j] = g_jobs[--g_njobs];
    }
    /* RV-289.3: how far each job is shows in its own window, not on a card here */
    return handled;
}

/* At the end of the viewer: the jobs go on in their own processes; only our ends of their pipes are let go. */
void filejob_shutdown(app_state_t *app) {
    clashwin_close(app);
    archwork_clash_shutdown();   /* RV-258: an extraction's questions left unasked */
    double until = rubraview_pal_time_now_seconds() + 3.0;   /* a job just started: its START handed over first */
    for (bool waiting = true; waiting && rubraview_pal_time_now_seconds() < until;) {
        waiting = false;
        for (size_t j = 0; j < g_njobs; ++j) {
            fj_job_t *job = g_jobs[j];
            if (job->out.len && !job->broken && !job->ended) { job_io(app, job); waiting = waiting || job->out.len > 0; }
        }
        if (waiting) Sleep(20);
    }
    for (size_t j = 0; j < g_njobs; ++j) job_free(g_jobs[j]);
    g_njobs = 0;
    clashui_rows_free(g_ui.rows, g_ui.nrows);
    g_ui.rows = NULL;
    g_ui.nrows = 0;
}

/* ---- RV-292: an extraction or a compression in a job's process (archwork.c asks and answers; the work is there) ---- */

typedef struct { const rubraview_jobwire_archive_t *a; const proven_u8str_view_t *names; const size_t *only; const uint8_t *rules; } archive_what_t;
static void put_archive_of(fj_job_t *job, app_state_t *app, const void *what) {
    const archive_what_t *w = (const archive_what_t *)what;
    rubraview_jobwire_archive_t a = *w->a;
    a.journal_dir = jobnotes_dir(app);
    a.lang_file = cstr(lang_file_in_use());
    a.ui_font = rubraview_settings_get_text(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("ui_font"));
    rubraview_jobwire_put_archive(&job->out, &a, w->names, w->only, w->rules);
}

int32_t filejob_archive_start(app_state_t *app, const rubraview_jobwire_archive_t *a, const proven_u8str_view_t *names, const size_t *only, const uint8_t *rules) {
    if (g_njobs >= FJ_JOBS) { osd_say(app, TU8("too many copies at once: wait for one to end")); return -1; }
    fj_job_t *job = (fj_job_t *)rubraview_alloc_zero(1, sizeof *job);
    if (!job) return -1;
    job->archive = true;
    archive_what_t w = { a, names, only, rules };
    if (!spawn_with(app, job, put_archive_of, &w)) return -1;
    job->started = rubraview_pal_time_now_seconds();
    job->id = ++g_next_id;
    g_jobs[g_njobs++] = job;
    return job->id;
}

static fj_job_t *by_id(int32_t id) {
    for (size_t j = 0; j < g_njobs; ++j) if (g_jobs[j]->id == id) return g_jobs[j];
    return NULL;
}

/* 0 working (its progress in `*p`), 1 done (its answer's fields in `*fields`), -1 its process went, or none. */
int filejob_archive_state(int32_t id, rubraview_jobwire_progress_t *p, const uint8_t **fields, size_t *len) {
    fj_job_t *job = by_id(id);
    if (!job) return -1;
    if (p) *p = job->progress;
    if (job->ended && job->done_fields) { if (fields) *fields = job->done_fields; if (len) *len = job->done_len; return 1; }
    return job->broken ? -1 : 0;
}

void filejob_archive_stop(int32_t id) {
    fj_job_t *job = by_id(id);
    if (!job || job->ended || job->broken) return;
    rubraview_jobwire_begin(&job->out, RUBRAVIEW_JOBWIRE_STOP);
    (void)rubraview_jobwire_finish(&job->out);
}

/* Let go: the job, if still at work, goes on in its process on its own (the viewer quitting does not stop it). */
void filejob_archive_forget(int32_t id) {
    for (size_t j = 0; j < g_njobs; ++j) {
        if (g_jobs[j]->id != id) continue;
        job_free(g_jobs[j]);
        g_jobs[j] = g_jobs[--g_njobs];
        return;
    }
}
