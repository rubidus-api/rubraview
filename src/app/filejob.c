/* RV-258 (owner, 2026-11-08: "압축파일 내에서 오건 어디서 오건 파일 관리자에서 동일한 이름의 파일이 있고 그걸 덮어씌우려 할 때,
   그 파일은 일단 킵해놓고 일괄적으로 묻게 해 줘요 ... 안겹치는 파일들을 복사 또는 이동하면서, 모달레스로 겹치는 파일 어떻게 할지
   작업 큐에 쌓아가면서 사용자에게 묻고 처리하게 해요. 윈도 파일 탐색기처럼 두개 비교하면서 체크박스로 체크할 수도 있고, 일괄적으로
   덮어씌울수도 있고, 둘다 보존하게 ... 할 수도 있고, 동일한 파일만 골라서 ..."; asked: a copy's same file left or recycled as
   chosen, a folder there merged, _2 everywhere, the window closed keeps its questions; D-310): copies and moves of the
   file manager as jobs of rubraview_fileop, stepped on one worker thread, one job after another; the clashes they
   park are asked in a modeless window of their own, and each answer goes back to its job.

   The operations are the worker's alone. The main thread sees a copy of the parked rows and the counts, and hands
   over new jobs and answers, all under one lock. A finished job comes back to the main thread for its undo, its
   words and what it changes on screen. */
#include "app.h"
#include "rubraview/fileop.h"

enum { FJ_JOBS = 16, FJ_ANSWERS = 1024 };
#define FJ_ARCADD 0xFFFFFFFFu   /* RV-260: a row of a writing into an archive (arcadd.c), answered on this thread */
#define FJ_EXTRACT 0xFFFFFFFEu  /* RV-258: a row of an extraction (archwork.c), answered on this thread */

typedef struct fj_job {
    rubraview_fileop_t op;
    uint32_t id;
    int      basket;    /* the basket it empties as it moves (0..4), or -1 */
    bool     from_cut;  /* a paste of a cut: the clipboard cleared after */
    int32_t  viewer_page;   /* RV-259: the viewer's own Copy / Move to folder of the page on screen, or -1 */
    char    *viewer_path;   /* that page's file, to know it is still the one on screen */
    char    *temp_dir;      /* a page out of an archive, written there first: removed after */
    bool     prepared, finished, said_parked;
} fj_job_t;

typedef struct fj_row {
    uint32_t job;
    size_t   index;
    char    *source, *target;
    uint64_t ssize, tsize;
    int64_t  smtime, tmtime;
    bool     kind_differs, keep_coming, keep_there;
} fj_row_t;

typedef struct fj_answer { uint32_t job; size_t index; bool all; rubraview_fileop_answer_t answer; } fj_answer_t;

static struct {
    SRWLOCK lock;
    HANDLE  thread, wake;
    volatile LONG quit;
    fj_job_t *jobs[FJ_JOBS];
    size_t   njobs;
    uint32_t next_id;
    fj_answer_t answers[FJ_ANSWERS];
    size_t   nanswers;
    bool     same_recycles_copy;
    /* what the worker shows the main thread */
    fj_row_t *rows;
    size_t   nrows;
    uint32_t version;
    size_t   total, done, failed, skipped;
} g = { .lock = SRWLOCK_INIT };

/* the main thread's copy, drawn in the window */
static fj_row_t *m_rows;
static size_t    m_nrows;
static uint32_t  m_version;
static int32_t   m_scroll;
static double    m_last_said;

static void rows_free(fj_row_t *rows, size_t n) {
    for (size_t i = 0; i < n; ++i) { rubraview_free(rows[i].source); rubraview_free(rows[i].target); }
    rubraview_free(rows);
}

/* Under the lock: the counts, and — when the parked ones changed — the parked items of every job as rows. */
static size_t g_parked_sig = (size_t)-1;
static void publish(void) {
    size_t n = 0, sig = 0;
    for (size_t j = 0; j < g.njobs; ++j) { n += g.jobs[j]->op.parked; sig = sig * 31u + g.jobs[j]->op.parked * 7u + g.jobs[j]->id; }
    if (sig == g_parked_sig && n == g.nrows) {   /* only the counts moved */
        size_t total = 0, done = 0, failed = 0, skipped = 0;
        for (size_t j = 0; j < g.njobs; ++j) { total += g.jobs[j]->op.count; done += g.jobs[j]->op.done; failed += g.jobs[j]->op.failed; skipped += g.jobs[j]->op.skipped; }
        g.total = total; g.done = done; g.failed = failed; g.skipped = skipped;
        return;
    }
    g_parked_sig = sig;
    fj_row_t *rows = n ? (fj_row_t *)rubraview_alloc_zero(n, sizeof *rows) : NULL;
    size_t k = 0, total = 0, done = 0, failed = 0, skipped = 0;
    const rubraview_fileop_fs_t *fs = rubraview_pal_fileop_fs();
    for (size_t j = 0; j < g.njobs; ++j) {
        rubraview_fileop_t *op = &g.jobs[j]->op;
        total += op->count; done += op->done; failed += op->failed; skipped += op->skipped;
        for (size_t i = 0; rows && i < op->count && k < n; ++i) {
            const rubraview_fileop_item_t *it = &op->items[i];
            if (it->state != RUBRAVIEW_FILEOP_PARKED) continue;
            rubraview_fileop_stat_t s = { 0 }, t = { 0 };
            (void)fs->stat(NULL, it->source, &s);
            (void)fs->stat(NULL, it->target, &t);
            rows[k++] = (fj_row_t){ .job = g.jobs[j]->id, .index = i, .source = rubraview_strdup(it->source), .target = rubraview_strdup(it->target),
                                    .ssize = s.size, .tsize = t.size, .smtime = s.mtime, .tmtime = t.mtime, .kind_differs = it->kind_differs,
                                    .keep_coming = true, .keep_there = true };
        }
    }
    rows_free(g.rows, g.nrows);
    g.rows = rows;
    g.nrows = k;
    g.total = total; g.done = done; g.failed = failed; g.skipped = skipped;
    g.version++;
}

static DWORD WINAPI worker(void *arg) {
    (void)arg;
    while (!InterlockedCompareExchange(&g.quit, 0, 0)) {
        fj_answer_t answers[FJ_ANSWERS];
        size_t nanswers = 0;
        fj_job_t *job = NULL;
        bool recycles = false;
        AcquireSRWLockExclusive(&g.lock);
        nanswers = g.nanswers;
        proven_sys_mem_copy(answers, g.answers, nanswers * sizeof *answers);
        g.nanswers = 0;
        recycles = g.same_recycles_copy;
        /* answers go to their jobs (the operations are this thread's; the lock keeps the list still) */
        for (size_t a = 0; a < nanswers; ++a)
            for (size_t j = 0; j < g.njobs; ++j) {
                if (g.jobs[j]->id != answers[a].job && !answers[a].all) continue;
                g.jobs[j]->op.same_recycles_copy = recycles;
                if (answers[a].all) (void)rubraview_fileop_answer_all(&g.jobs[j]->op, answers[a].answer);
                else (void)rubraview_fileop_answer(&g.jobs[j]->op, answers[a].index, answers[a].answer);
            }
        for (size_t j = 0; j < g.njobs && !job; ++j)
            if (!g.jobs[j]->finished && (!g.jobs[j]->prepared || !rubraview_fileop_finished(&g.jobs[j]->op))) job = g.jobs[j];
        if (nanswers) { g_parked_sig = (size_t)-1; publish(); }
        ReleaseSRWLockExclusive(&g.lock);
        if (!job) { WaitForSingleObject(g.wake, 250); continue; }
        bool more;
        if (!job->prepared) { (void)rubraview_fileop_prepare(&job->op); job->prepared = true; more = true; }
        else more = rubraview_fileop_step(&job->op, 1);   /* one item, so answers and Esc are taken between */
        AcquireSRWLockExclusive(&g.lock);
        if (rubraview_fileop_finished(&job->op)) job->finished = true;
        publish();
        ReleaseSRWLockExclusive(&g.lock);
        if (!more && !job->finished) WaitForSingleObject(g.wake, 250);   /* only answers can move it on */
    }
    return 0;
}

static bool ensure_worker(void) {
    if (g.thread) return true;
    g.wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!g.wake) return false;
    g.thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    return g.thread != NULL;
}

bool filejob_same_recycles_copy(void) {
    AcquireSRWLockShared(&g.lock);
    bool v = g.same_recycles_copy;
    ReleaseSRWLockShared(&g.lock);
    return v;
}

bool filejob_busy(void) {
    AcquireSRWLockShared(&g.lock);
    bool busy = g.njobs > 0;
    ReleaseSRWLockShared(&g.lock);
    return busy;
}

/* RV-258: a copy or a move of `sources` into `dir`, every name checked before anything is done; the clashes asked in
   the clashes window. `basket` (0..4, or -1) and `from_cut` say what is tidied when it ends. */
static fj_job_t *g_last_started;   /* the job filejob_start just queued, for filejob_start_viewer (main thread) */

bool filejob_start(app_state_t *app, proven_u8str_view_t dir, const proven_u8str_view_t *sources, size_t n, bool moving, int basket, bool from_cut) {
    g_last_started = NULL;
    if (n == 0 || dir.size == 0) return false;
    if (!ensure_worker()) { osd_say(app, TU8("the copy could not be started")); return false; }
    fj_job_t *job = (fj_job_t *)rubraview_alloc_zero(1, sizeof *job);
    char *d = rubraview_strndup_view(dir);
    if (!job || !d) { rubraview_free(job); rubraview_free(d); return false; }
    rubraview_fileop_init(&job->op, moving ? RUBRAVIEW_FILEOP_MOVE : RUBRAVIEW_FILEOP_COPY, rubraview_pal_fileop_fs(), d);
    rubraview_free(d);
    for (size_t i = 0; i < n; ++i) {
        char *s = rubraview_strndup_view(sources[i]);
        if (s) (void)rubraview_fileop_add(&job->op, s);
        rubraview_free(s);
    }
    job->basket = basket;
    job->from_cut = from_cut;
    job->viewer_page = -1;
    AcquireSRWLockExclusive(&g.lock);
    bool room = g.njobs < FJ_JOBS;
    if (room) { job->id = ++g.next_id; g.jobs[g.njobs++] = job; g_last_started = job; }
    ReleaseSRWLockExclusive(&g.lock);
    if (!room) { rubraview_fileop_free(&job->op); rubraview_free(job); osd_say(app, TU8("too many copies at once: wait for one to end")); return false; }
    SetEvent(g.wake);
    char line[200];
    int w = rubraview_fmt(line, sizeof line, moving ? T("moving {} into {}") : T("copying {} into {}"), PROVEN_ARG(n), PROVEN_ARG(rubraview_path_basename(dir)));
    if (w > 0) { osd_say(app, cstr(line)); }
    return true;
}

static void answer(uint32_t job, size_t index, bool all, rubraview_fileop_answer_t a) {
    AcquireSRWLockExclusive(&g.lock);
    if (g.nanswers < FJ_ANSWERS) g.answers[g.nanswers++] = (fj_answer_t){ .job = job, .index = index, .all = all, .answer = a };
    ReleaseSRWLockExclusive(&g.lock);
    SetEvent(g.wake);
}

/* RV-259 (owner, 2026-11-08: "화면의 그림 하나를 \"폴더로 복사/이동\"하는 뷰어 자체 기능은 ... <- 이것도 묻고 처리하게 해 주세요"): the file on screen to a folder through the same check — a
   name there is asked about; a page inside an archive is written to a folder of its own in %TEMP% first and copied
   from there. When it ends, a move takes the page out of the list as before. */
bool filejob_start_viewer(app_state_t *app, proven_u8str_view_t dir, proven_u8str_view_t path, bool moving, int32_t page, proven_u8str_view_t temp_dir) {
    char *vp = rubraview_strndup_view(path), *td = temp_dir.size ? rubraview_strndup_view(temp_dir) : NULL;
    if (!filejob_start(app, dir, &path, 1, moving, -1, false) || !g_last_started) { rubraview_free(vp); rubraview_free(td); return false; }
    AcquireSRWLockExclusive(&g.lock);   /* a finished job is only ended by filejob_pump, on this thread: it is set in time */
    g_last_started->viewer_page = page;
    g_last_started->viewer_path = vp;
    g_last_started->temp_dir = td;
    ReleaseSRWLockExclusive(&g.lock);
    return true;
}

/* A finished job, on the main thread: the undo, the basket, the clipboard, the words. */
static void job_ended(app_state_t *app, fj_job_t *job) {
    rubraview_fileop_t *op = &job->op;
    uint32_t batch = ++app->undo_batch;
    size_t done = 0, replaced = 0, same = 0;
    bool *moved = NULL;
    size_t top = 0;
    for (size_t i = 0; i < op->count; ++i) if (op->items[i].parent < 0) top++;
    if (job->basket >= 0 && top) moved = (bool *)rubraview_alloc_zero(top, sizeof *moved);
    size_t t = 0;
    for (size_t i = 0; i < op->count; ++i) {
        const rubraview_fileop_item_t *it = &op->items[i];
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
        if (it->state == RUBRAVIEW_FILEOP_FAILED || (it->why && it->did == RUBRAVIEW_FILEOP_DID_COPY && op->kind == RUBRAVIEW_FILEOP_MOVE)) {
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
    bool moving = op->kind == RUBRAVIEW_FILEOP_MOVE;
    if (moved && moving) rubraview_basket_keep_unmoved(&app->baskets, job->basket, moved);
    rubraview_free(moved);
    if (job->from_cut && moving && done > 0) { dragout_clip_pasted(app); if (op->failed == 0) rubraview_pal_clipboard_clear(app->window); }
    char line[260];
    int w = rubraview_fmt(line, sizeof line, moving ? T("{} moved, {} replaced, {} the same, {} left, {} could not be  (Ctrl+Z takes it back)")
                                                   : T("{} copied, {} replaced, {} the same, {} left, {} could not be  (Ctrl+Z takes it back)"),
                          PROVEN_ARG(done), PROVEN_ARG(replaced), PROVEN_ARG(same), PROVEN_ARG(op->skipped), PROVEN_ARG(op->failed));
    if (w > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)w });
    if (replaced > 0) app_log(app, "file", PROVEN_LIT("replaced files are in the Recycle Bin; Ctrl+Z takes the new ones away, not the old ones back"));
    if (app->picker_open) picker_navigate(app, app->picker_dir);   /* what arrived, listed */
    if (job->viewer_page >= 0 && op->count > 0) {   /* RV-259: the viewer's own; its name there said, a move leaves the list */
        const rubraview_fileop_item_t *it = &op->items[0];
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
    if (job->temp_dir) (void)rubraview_pal_fs_delete_tree(cstr(job->temp_dir));
    rubraview_free(job->viewer_path);
    rubraview_free(job->temp_dir);
}

/* ---- the clashes window ---- */

void clashwin_close(app_state_t *app) {
    if (!app->clash_open) return;
    rubraview_pal_window_get_frame(app->clash_window, &app->clash_frame[0], &app->clash_frame[1], &app->clash_frame[2], &app->clash_frame[3]);
    if (app->clash_renderer) rubraview_pal_render_destroy(app->clash_renderer);
    if (app->clash_window) rubraview_pal_window_destroy(app->clash_window);
    window_block_give(app, &app->clash_block);
    app->clash_renderer = NULL;
    app->clash_window = NULL;
    app->clash_open = false;   /* the questions stay: the owner's answer */
}

void clashwin_show(app_state_t *app) {
    if (app->clash_open) return;
    if (m_nrows == 0) { osd_say(app, TU8("no name clash is waiting")); return; }
    rubraview_window_config_t config = { .title = T("Rubraview - names already there"), .width = 980, .height = 620, .owner = app->window };
    app->clash_window = rubraview_pal_window_create(window_block_take(app, &app->clash_block), &config);
    if (!app->clash_window) return;
    if (app->clash_frame[2] > 0 && app->clash_frame[3] > 0)
        rubraview_pal_window_set_frame(app->clash_window, app->clash_frame[0], app->clash_frame[1], app->clash_frame[2], app->clash_frame[3]);
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->clash_window, &w, &h);
    app->clash_renderer = rubraview_pal_render_create(window_block_arena(app, &app->clash_block), rubraview_pal_window_native_handle(app->clash_window), w, h);
    ui_font_apply_all(app);
    if (!app->clash_renderer) { rubraview_pal_window_destroy(app->clash_window); window_block_give(app, &app->clash_block); app->clash_window = NULL; return; }
    app->clash_open = true;
    app->clash_dirty = true;
    m_scroll = 0;
}

void clashwin_toggle(app_state_t *app) { if (app->clash_open) clashwin_close(app); else clashwin_show(app); }

#define CW_BACK   0xFF1B1E23u
#define CW_TEXT   0xFFE6E6E6u
#define CW_DIM    0xFF9AA2ACu
#define CW_LINE   0xFF30353Cu
#define CW_ON     0xFF3E8BF0u

enum { CW_BTN_APPLY = 0, CW_BTN_OVERWRITE, CW_BTN_BOTH, CW_BTN_SKIP, CW_BTN_SAME, CW_BTN_RECYCLE, CW_BTNS };
static rubraview_pal_rect_t cw_btn[CW_BTNS];
static rubraview_pal_rect_t cw_box[256][2];
static size_t cw_box_row[256];
static size_t cw_nbox;

static void size_text(char *buf, size_t cap, uint64_t bytes) { (void)rubraview_picker_size_text(buf, cap, bytes); }

static void when_text(char *buf, size_t cap, int64_t t) {
    if (t <= 0) { buf[0] = '\0'; return; }
    info_local_time(buf, cap, t);
}

void draw_clash_window(app_state_t *app) {
    if (!app->clash_open || !app->clash_dirty) return;
    app->clash_dirty = false;
    rubraview_renderer_t *r = app->clash_renderer;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->clash_window, &w, &h);
    double dpi = rubraview_pal_window_dpi_scale(app->clash_window), pad = 12.0 * dpi, row_h = 54.0 * dpi, box = 20.0 * dpi;
    rubraview_pal_render_begin(r, CW_BACK);
    char head[200];
    int n = rubraview_fmt(head, sizeof head, T("{} names already there — the other files go on being copied or moved meanwhile"), PROVEN_ARG(m_nrows));
    if (n > 0) rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)head, .size = (size_t)n },
                                              (rubraview_pal_rect_t){ pad, pad * 0.5, (double)w - 2 * pad, 26.0 * dpi }, 15.0 * dpi, CW_TEXT, RUBRAVIEW_TEXT_LEFT);
    double col0 = pad + 10.0 * dpi, col1 = pad + 64.0 * dpi, text_x = pad + 104.0 * dpi, top = 38.0 * dpi;
    rubraview_pal_render_draw_text(r, TU8("coming"), (rubraview_pal_rect_t){ col0 + box * 0.5 - 26.0 * dpi, top, 52.0 * dpi, 18.0 * dpi }, 11.0 * dpi, CW_DIM, RUBRAVIEW_TEXT_CENTER);
    rubraview_pal_render_draw_text(r, TU8("there"), (rubraview_pal_rect_t){ col1 + box * 0.5 - 26.0 * dpi, top, 52.0 * dpi, 18.0 * dpi }, 11.0 * dpi, CW_DIM, RUBRAVIEW_TEXT_CENTER);
    double y = top + 22.0 * dpi, foot = (double)h - 96.0 * dpi;
    int32_t rows = (int32_t)((foot - y) / row_h);
    if (rows < 1) rows = 1;
    if (m_scroll > (int32_t)m_nrows - rows) m_scroll = (int32_t)m_nrows - rows;
    if (m_scroll < 0) m_scroll = 0;
    cw_nbox = 0;
    for (int32_t k = 0; k < rows && (size_t)(m_scroll + k) < m_nrows; ++k) {
        fj_row_t *row = &m_rows[m_scroll + k];
        double ry = y + (double)k * row_h;
        rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ pad * 0.5, ry + row_h - 1.0, (double)w - pad, 1.0 }, CW_LINE, 0.0);
        for (int c = 0; c < 2; ++c) {
            rubraview_pal_rect_t b = { c == 0 ? col0 : col1, ry + (row_h - box) * 0.5, box, box };
            bool on = c == 0 ? row->keep_coming : row->keep_there;
            rubraview_pal_render_fill_rect(r, b, on ? CW_ON : 0xFF2A2F36u, 3.0 * dpi);
            rubraview_pal_render_stroke_rect(r, b, 0xFF6A7685u, 1.0, 3.0 * dpi);
            if (on) rubraview_pal_render_draw_text(r, TU8("\xE2\x9C\x93"), b, 14.0 * dpi, 0xFFFFFFFFu, RUBRAVIEW_TEXT_CENTER);
            if (cw_nbox < 256) { cw_box[cw_nbox][c] = b; }
        }
        if (cw_nbox < 256) cw_box_row[cw_nbox++] = (size_t)(m_scroll + k);
        proven_u8str_view_t name = rubraview_path_basename(cstr(row->target));
        rubraview_pal_render_draw_text(r, name, (rubraview_pal_rect_t){ text_x, ry + 4.0 * dpi, (double)w - text_x - pad, 22.0 * dpi }, 14.0 * dpi, CW_TEXT, RUBRAVIEW_TEXT_LEFT);
        char ss[32], ts[32], sw[64], tw[64], line[1400];
        size_text(ss, sizeof ss, row->ssize); size_text(ts, sizeof ts, row->tsize);
        when_text(sw, sizeof sw, row->smtime); when_text(tw, sizeof tw, row->tmtime);
        int m = rubraview_fmt(line, sizeof line, T("coming: {}  {}  {}      there: {}  {}{}"), PROVEN_ARG(ss), PROVEN_ARG(sw),
                              PROVEN_ARG(rubraview_path_dirname(cstr(row->source))), PROVEN_ARG(ts), PROVEN_ARG(tw),
                              PROVEN_ARG(row->kind_differs ? T("   (a file and a folder: skip or keep both)") : ""));
        if (m > 0) rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)m < sizeof line ? (size_t)m : sizeof line - 1 },
                                                  (rubraview_pal_rect_t){ text_x, ry + 26.0 * dpi, (double)w - text_x - pad, 20.0 * dpi }, 12.0 * dpi, CW_DIM, RUBRAVIEW_TEXT_LEFT);
    }
    /* the foot: what the boxes mean, the answers for all, the copy's same file */
    rubraview_pal_render_draw_text(r, TU8("Both ticked: both kept (the one coming as \"name (2)\"). Only the one coming: it replaces the one there (that one to the Recycle Bin). Otherwise: left as it is."),
                                   (rubraview_pal_rect_t){ pad, foot + 4.0 * dpi, (double)w - 2 * pad, 20.0 * dpi }, 12.0 * dpi, CW_DIM, RUBRAVIEW_TEXT_LEFT);
    const char *words[CW_BTNS] = { T("Apply the ticks"), T("Overwrite all"), T("Keep both, all"), T("Skip all"), T("Only the same ones"),
                                   g.same_recycles_copy ? T("A copy's same file: its source to the Recycle Bin") : T("A copy's same file: its source left") };
    double bx = pad, by = (double)h - 50.0 * dpi, bh = 36.0 * dpi;
    for (int i = 0; i < CW_BTNS; ++i) {
        double tw = 0.0;
        (void)rubraview_pal_render_measure_text(r, cstr(words[i]), 13.0 * dpi, &tw);
        rubraview_pal_rect_t b = { bx, by, tw + 22.0 * dpi, bh };
        if (bx + b.width > (double)w - pad && i > 0) { b.x = bx = pad; b.y = by -= bh + 6.0 * dpi; }
        cw_btn[i] = b;
        bool over = pal_rect_has(b, app->clash_px, app->clash_py);
        rubraview_pal_render_fill_rect(r, b, i == 0 ? (over ? 0xFF4F9CF7u : CW_ON) : over ? 0x50FFFFFFu : 0xFF2A2F36u, 4.0 * dpi);
        rubraview_pal_render_stroke_rect(r, b, 0xFF4A525Cu, 1.0, 4.0 * dpi);
        rubraview_pal_render_draw_text(r, cstr(words[i]), b, 13.0 * dpi, CW_TEXT, RUBRAVIEW_TEXT_CENTER);
        bx += b.width + 8.0 * dpi;
    }
    if (!rubraview_pal_render_end(r)) app->clash_dirty = true;
}

static void cw_press(app_state_t *app, double x, double y) {
    for (size_t k = 0; k < cw_nbox; ++k)
        for (int c = 0; c < 2; ++c)
            if (pal_rect_has(cw_box[k][c], x, y) && cw_box_row[k] < m_nrows) {
                fj_row_t *row = &m_rows[cw_box_row[k]];
                if (c == 0) row->keep_coming = !row->keep_coming; else row->keep_there = !row->keep_there;
                app->clash_dirty = true;
                return;
            }
    int hit = -1;
    for (int i = 0; i < CW_BTNS; ++i) if (pal_rect_has(cw_btn[i], x, y)) hit = i;
    switch (hit) {
        case CW_BTN_APPLY:
            for (size_t i = 0; i < m_nrows; ++i) {
                rubraview_fileop_answer_t a = rubraview_fileop_answer_of_checks(m_rows[i].keep_coming, m_rows[i].keep_there);
                if (m_rows[i].kind_differs && a == RUBRAVIEW_FILEOP_ANSWER_OVERWRITE) a = RUBRAVIEW_FILEOP_ANSWER_SKIP;   /* never a file over a folder */
                if (m_rows[i].job == FJ_ARCADD) arcadd_answer(app, m_rows[i].index, a);
                else if (m_rows[i].job == FJ_EXTRACT) archwork_clash_answer(app, m_rows[i].index, a);
                else answer(m_rows[i].job, m_rows[i].index, false, a);
            }
            app_log(app, "clash", PROVEN_LIT("the ticks applied"));
            break;
        case CW_BTN_OVERWRITE: answer(0, 0, true, RUBRAVIEW_FILEOP_ANSWER_OVERWRITE); arcadd_answer_all(app, RUBRAVIEW_FILEOP_ANSWER_OVERWRITE); archwork_clash_answer_all(app, RUBRAVIEW_FILEOP_ANSWER_OVERWRITE); app_log(app, "clash", PROVEN_LIT("overwrite all")); break;
        case CW_BTN_BOTH:      answer(0, 0, true, RUBRAVIEW_FILEOP_ANSWER_KEEP_BOTH); arcadd_answer_all(app, RUBRAVIEW_FILEOP_ANSWER_KEEP_BOTH); archwork_clash_answer_all(app, RUBRAVIEW_FILEOP_ANSWER_KEEP_BOTH); app_log(app, "clash", PROVEN_LIT("keep both, all")); break;
        case CW_BTN_SKIP:      answer(0, 0, true, RUBRAVIEW_FILEOP_ANSWER_SKIP); arcadd_answer_all(app, RUBRAVIEW_FILEOP_ANSWER_SKIP); archwork_clash_answer_all(app, RUBRAVIEW_FILEOP_ANSWER_SKIP); app_log(app, "clash", PROVEN_LIT("skip all")); break;
        case CW_BTN_SAME:      answer(0, 0, true, RUBRAVIEW_FILEOP_ANSWER_SAME_ONLY); arcadd_answer_all(app, RUBRAVIEW_FILEOP_ANSWER_SAME_ONLY); archwork_clash_answer_all(app, RUBRAVIEW_FILEOP_ANSWER_SAME_ONLY); app_log(app, "clash", PROVEN_LIT("only the same ones")); break;
        case CW_BTN_RECYCLE:
            AcquireSRWLockExclusive(&g.lock);
            g.same_recycles_copy = !g.same_recycles_copy;
            ReleaseSRWLockExclusive(&g.lock);
            break;
        default: return;
    }
    app->clash_dirty = true;
}

size_t clash_pump(app_state_t *app) {
    size_t handled = 0;
    rubraview_window_event_t event;
    while (app->clash_open && rubraview_pal_window_poll_event(app->clash_window, &event)) {
        handled++;
        switch (event.kind) {
            case RUBRAVIEW_WINDOW_EVENT_CLOSE: clashwin_close(app); return handled;
            case RUBRAVIEW_WINDOW_EVENT_RESIZE:
                rubraview_pal_render_resize(app->clash_renderer, event.resize.width, event.resize.height);
                app->clash_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_PAINT: app->clash_dirty = true; break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_WHEEL: m_scroll += event.mouse.wheel_delta > 0 ? -2 : 2; app->clash_dirty = true; break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE: app->clash_px = event.mouse.x; app->clash_py = event.mouse.y; app->clash_dirty = true; break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN:
                if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT) cw_press(app, event.mouse.x, event.mouse.y);
                break;
            case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN:
                if (key_is(event.key.combo, "Escape")) { clashwin_close(app); return handled; }
                if (key_is(event.key.combo, "Enter")) cw_press(app, cw_btn[CW_BTN_APPLY].x + 1.0, cw_btn[CW_BTN_APPLY].y + 1.0);
                if (key_is(event.key.combo, "PageDown")) { m_scroll += 5; app->clash_dirty = true; }
                if (key_is(event.key.combo, "PageUp")) { m_scroll -= 5; app->clash_dirty = true; }
                break;
            default: break;
        }
    }
    return handled;
}

/* Once a pass of the main loop: the worker's rows taken over (the ticks kept), new clashes shown, finished jobs
   brought home, a line now and then while it works. */
static uint32_t m_aa_version, m_xx_version;

size_t filejob_pump(app_state_t *app) {
    if (!g.thread && arcadd_version() == m_aa_version && archwork_clash_version() == m_xx_version) return 0;
    fj_job_t *ended[FJ_JOBS];
    size_t nended = 0;
    bool changed = false, grew = false;
    size_t total = 0, done = 0;
    AcquireSRWLockExclusive(&g.lock);
    if (g.version != m_version || arcadd_version() != m_aa_version || archwork_clash_version() != m_xx_version) {
        size_t na = arcadd_rows(NULL, 0), nx = archwork_clash_rows(NULL, 0);
        fj_row_t *rows = g.nrows + na + nx ? (fj_row_t *)rubraview_alloc_zero(g.nrows + na + nx, sizeof *rows) : NULL;
        size_t k = 0;
        if (rows && nx) {   /* RV-258: the names an extraction found taken */
            xclash_row_t *xr = (xclash_row_t *)rubraview_alloc_zero(nx, sizeof *xr);
            size_t got = xr ? archwork_clash_rows(xr, nx) : 0;
            for (size_t i = 0; i < got; ++i) {
                rows[k] = (fj_row_t){ .job = FJ_EXTRACT, .index = xr[i].index, .source = rubraview_strdup(xr[i].coming), .target = rubraview_strdup(xr[i].there),
                                      .ssize = xr[i].csize, .tsize = xr[i].tsize, .smtime = xr[i].cmtime, .tmtime = xr[i].tmtime,
                                      .keep_coming = true, .keep_there = true };
                for (size_t o = 0; o < m_nrows; ++o)
                    if (m_rows[o].job == FJ_EXTRACT && m_rows[o].index == rows[k].index) { rows[k].keep_coming = m_rows[o].keep_coming; rows[k].keep_there = m_rows[o].keep_there; }
                k++;
            }
            rubraview_free(xr);
        }
        m_xx_version = archwork_clash_version();
        if (rows && na) {   /* RV-260: the names already in an archive being written into */
            arcadd_row_t *ar = (arcadd_row_t *)rubraview_alloc_zero(na, sizeof *ar);
            size_t got = ar ? arcadd_rows(ar, na) : 0;
            for (size_t i = 0; i < got; ++i) {
                char there[RUBRAVIEW_ARCHIVE_PATH * 2];
                (void)rubraview_fmt(there, sizeof there, "{}/{}", PROVEN_ARG(cstr(ar[i].archive)), PROVEN_ARG(cstr(ar[i].name)));
                rows[k] = (fj_row_t){ .job = FJ_ARCADD, .index = ar[i].index, .source = rubraview_strdup(ar[i].source), .target = rubraview_strdup(there),
                                      .ssize = ar[i].ssize, .tsize = ar[i].tsize, .smtime = ar[i].smtime, .tmtime = ar[i].tmtime,
                                      .keep_coming = true, .keep_there = true };
                for (size_t o = 0; o < m_nrows; ++o)
                    if (m_rows[o].job == FJ_ARCADD && m_rows[o].index == rows[k].index) { rows[k].keep_coming = m_rows[o].keep_coming; rows[k].keep_there = m_rows[o].keep_there; }
                k++;
            }
            rubraview_free(ar);
        }
        m_aa_version = arcadd_version();
        for (size_t i = 0; rows && i < g.nrows; ++i) {
            rows[k] = g.rows[i];
            rows[k].source = rubraview_strdup(g.rows[i].source);
            rows[k].target = rubraview_strdup(g.rows[i].target);
            for (size_t o = 0; o < m_nrows; ++o)   /* the ticks the reader set are kept */
                if (m_rows[o].job == rows[k].job && m_rows[o].index == rows[k].index) { rows[k].keep_coming = m_rows[o].keep_coming; rows[k].keep_there = m_rows[o].keep_there; }
            k++;
        }
        grew = k > m_nrows;
        rows_free(m_rows, m_nrows);
        m_rows = rows;
        m_nrows = k;
        m_version = g.version;
        changed = true;
    }
    total = g.total; done = g.done;
    for (size_t j = 0; j < g.njobs;) {
        if (g.jobs[j]->finished) { ended[nended++] = g.jobs[j]; g.jobs[j] = g.jobs[--g.njobs]; }
        else ++j;
    }
    ReleaseSRWLockExclusive(&g.lock);
    for (size_t e = 0; e < nended; ++e) { job_ended(app, ended[e]); rubraview_fileop_free(&ended[e]->op); rubraview_free(ended[e]); }
    if (changed) {
        app->clash_dirty = true;
        if (grew && !app->clash_open) clashwin_show(app);   /* new questions: the window comes */
        if (m_nrows == 0 && app->clash_open) clashwin_close(app);
    }
    double now = rubraview_pal_time_now_seconds();
    if (filejob_busy() && total > 0 && now - m_last_said > 1.0) {
        m_last_said = now;
        char line[160];
        int w = m_nrows ? rubraview_fmt(line, sizeof line, T("files: {} of {} done, {} names waiting for an answer (Ctrl+Shift+K)"), PROVEN_ARG(done), PROVEN_ARG(total), PROVEN_ARG(m_nrows))
                        : rubraview_fmt(line, sizeof line, T("files: {} of {} done"), PROVEN_ARG(done), PROVEN_ARG(total));
        if (w > 0) { size_t len = (size_t)w; proven_sys_mem_copy(app->notice, line, len < sizeof app->notice ? len : sizeof app->notice - 1);
                     app->notice_length = len < sizeof app->notice ? len : sizeof app->notice - 1; app->notice[app->notice_length] = '\0'; app->notice_seconds = 1.5; }
    }
    return nended + (changed ? 1 : 0);
}

/* At the end of the program: the worker stopped after its item; what waited is left as it is (said in the log). */
void filejob_shutdown(app_state_t *app) {
    clashwin_close(app);
    archwork_clash_shutdown();   /* RV-258: an extraction's questions left unasked */
    if (!g.thread) return;
    InterlockedExchange(&g.quit, 1);
    SetEvent(g.wake);
    WaitForSingleObject(g.thread, 10000);
    CloseHandle(g.thread);
    CloseHandle(g.wake);
    g.thread = NULL;
    for (size_t j = 0; j < g.njobs; ++j) { rubraview_fileop_free(&g.jobs[j]->op); rubraview_free(g.jobs[j]); }
    g.njobs = 0;
    rows_free(g.rows, g.nrows); g.rows = NULL; g.nrows = 0;
    rows_free(m_rows, m_nrows); m_rows = NULL; m_nrows = 0;
}
