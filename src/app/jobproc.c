/* RV-289 (owner, 2026-11-15: "이것들은 어떤 독립된 프로세스로 따로 처리하는 것이 바람직하지 않을까요? 메인 창이 죽거나 해도
   그대로 이어서 진행되고, 반대로 이 프로세스가 죽어도 메인 창이 죽지 않게요. 둘 다 외부 창으로"; "작업마다 프로세스";
   "창을 끄면 겹치는 남은 파일들에 대해서는 건너뛰고 안겹치는 파일들은 계속"): a copy or a move in a process of its own,
   `rubraview.exe --file-job <pipe>`. It takes its job from the viewer over the pipe (core jobwire), runs it on a worker
   thread (the steps of filejob.c's worker, one job), asks its clashes in a window of its own (clashui), and says how
   far it is, then every item and the end. A viewer gone does not stop it: what it would have said is dropped.

   Threads: the main one (the pipe's reading, the window, the rows taken over), the worker (the operation, its
   items its alone), the writer (the pipe's writing, so a viewer that hangs never holds the copy). */
#include "app.h"
#include "clashui.h"
#include "rubraview/fileop.h"
#include "rubraview/jobwire.h"
#include "rubraview/jobnote.h"
#include "rubraview/memory.h"

enum { JP_ANSWERS = 1024 };

typedef struct jp_answer { size_t index; bool all; rubraview_fileop_answer_t answer; } jp_answer_t;

static struct {
    SRWLOCK lock;
    HANDLE  wake, worker;
    volatile LONG quit, stop;
    rubraview_fileop_t op;
    _Atomic uint64_t copied;
    bool    prepared, finished, skip_clashes;   /* skip_clashes: the clash window closed unanswered (RV-289.5) */
    bool    recycles;
    jp_answer_t answers[JP_ANSWERS];
    size_t  nanswers;
    /* what the worker shows the main thread */
    clashui_row_t *rows;
    size_t  nrows;
    uint32_t version;
    rubraview_jobwire_progress_t progress;
} J = { .lock = SRWLOCK_INIT };

/* ---- the pipe ---- */

static struct {
    HANDLE pipe;
    SRWLOCK lock;
    HANDLE wake, thread;
    rubraview_jobwire_out_t out;     /* written by the main thread, sent by the writer */
    volatile LONG broken, quit;
    uint8_t *in;
    size_t   in_len, in_cap;
} P = { .lock = SRWLOCK_INIT };

static DWORD WINAPI writer(void *arg) {
    (void)arg;
    rubraview_jobwire_out_t mine = {0};
    for (;;) {
        AcquireSRWLockExclusive(&P.lock);
        rubraview_jobwire_out_t swap = P.out;   /* what is queued, taken whole */
        P.out = mine;
        P.out.len = 0; P.out.start = 0;
        mine = swap;
        bool quit = P.quit != 0;
        ReleaseSRWLockExclusive(&P.lock);
        size_t at = 0;
        while (at < mine.len && !P.broken) {
            DWORD wrote = 0;
            DWORD chunk = (DWORD)((mine.len - at) > (1u << 20) ? (1u << 20) : (mine.len - at));
            if (!WriteFile(P.pipe, mine.data + at, chunk, &wrote, NULL) || wrote == 0) { InterlockedExchange(&P.broken, 1); break; }
            at += wrote;
        }
        mine.len = 0; mine.start = 0;
        if (quit) break;
        WaitForSingleObject(P.wake, 200);
    }
    rubraview_jobwire_out_free(&mine);
    return 0;
}

/* A message queued by `put` (it begins and finishes it) — dropped when the viewer is gone. */
static void send_with(void (*put)(rubraview_jobwire_out_t *, const void *), const void *what) {
    if (P.broken) return;
    AcquireSRWLockExclusive(&P.lock);
    put(&P.out, what);
    ReleaseSRWLockExclusive(&P.lock);
    SetEvent(P.wake);
}

static void put_progress(rubraview_jobwire_out_t *o, const void *p) { rubraview_jobwire_put_progress(o, (const rubraview_jobwire_progress_t *)p); }

/* Every item, in messages of a few hundred, then the end. */
static void send_items_and_end(bool stopped) {
    if (P.broken) return;
    const rubraview_fileop_t *op = &J.op;
    AcquireSRWLockExclusive(&P.lock);
    for (size_t first = 0; first < op->count; first += 400) {
        size_t n = op->count - first < 400 ? op->count - first : 400;
        rubraview_jobwire_begin(&P.out, RUBRAVIEW_JOBWIRE_ITEMS);
        rubraview_jobwire_put_u32(&P.out, (uint32_t)n);
        for (size_t i = first; i < first + n; ++i) {
            const rubraview_fileop_item_t *it = &op->items[i];
            rubraview_jobwire_item_t w = { .source = cstr(it->source), .target = cstr(it->target), .why = cstr(it->why ? it->why : ""),
                                           .did = (uint8_t)it->did, .state = (uint8_t)it->state, .overwrote = it->overwrote, .parent = it->parent };
            rubraview_jobwire_put_item(&P.out, &w);
        }
        (void)rubraview_jobwire_finish(&P.out);
    }
    rubraview_jobwire_end_t e = { .moving = op->kind == RUBRAVIEW_FILEOP_MOVE, .skipped = op->skipped, .failed = op->failed, .stopped = stopped };
    rubraview_jobwire_put_end(&P.out, &e);
    ReleaseSRWLockExclusive(&P.lock);
    SetEvent(P.wake);
}

/* What has come, read without waiting; false when the pipe broke. */
static bool pipe_read_some(void) {
    if (P.broken) return false;
    DWORD avail = 0;
    if (!PeekNamedPipe(P.pipe, NULL, 0, NULL, &avail, NULL)) { InterlockedExchange(&P.broken, 1); return false; }
    if (avail == 0) return true;
    if (P.in_len + avail > P.in_cap) {
        size_t cap = P.in_cap ? P.in_cap : 4096;
        while (cap < P.in_len + avail) cap *= 2;
        uint8_t *more = (uint8_t *)rubraview_realloc((proven_mem_mut_t){ .ptr = (proven_byte_t *)P.in, .size = cap });
        if (!more) return true;
        P.in = more; P.in_cap = cap;
    }
    DWORD got = 0;
    if (!ReadFile(P.pipe, P.in + P.in_len, avail, &got, NULL)) { InterlockedExchange(&P.broken, 1); return false; }
    P.in_len += got;
    return true;
}

/* ---- the worker (filejob.c's, one job) ---- */

/* Under the lock: the counts, and — when the parked ones changed — the parked items as rows. */
static size_t g_parked_sig = (size_t)-1;
static void publish(void) {
    rubraview_fileop_t *op = &J.op;
    uint64_t bd = 0, bt = 0;
    rubraview_fileop_bytes(op, &bd, &bt);
    J.progress = (rubraview_jobwire_progress_t){ .total = op->count, .done = op->done, .failed = op->failed, .skipped = op->skipped,
                                                 .bytes_total = bt, .bytes_done = bd, .clashes = op->parked };
    size_t sig = op->parked * 7u + op->done * 131u + op->skipped * 17u;
    if (sig == g_parked_sig) return;
    g_parked_sig = sig;
    size_t n = op->parked, k = 0;
    clashui_row_t *rows = n ? (clashui_row_t *)rubraview_alloc_zero(n, sizeof *rows) : NULL;
    const rubraview_fileop_fs_t *fs = rubraview_pal_fileop_fs();
    for (size_t i = 0; rows && i < op->count && k < n; ++i) {
        const rubraview_fileop_item_t *it = &op->items[i];
        if (it->state != RUBRAVIEW_FILEOP_PARKED) continue;
        rubraview_fileop_stat_t s = { 0 }, t = { 0 };
        (void)fs->stat(NULL, it->source, &s);
        (void)fs->stat(NULL, it->target, &t);
        rows[k++] = (clashui_row_t){ .owner = 1, .index = i, .source = rubraview_strdup(it->source), .target = rubraview_strdup(it->target),
                                     .ssize = s.size, .tsize = t.size, .smtime = s.mtime, .tmtime = t.mtime, .kind_differs = it->kind_differs,
                                     .keep_coming = true, .keep_there = true };
    }
    clashui_rows_free(J.rows, J.nrows);
    J.rows = rows;
    J.nrows = k;
    J.version++;
}

static DWORD WINAPI worker(void *arg) {
    (void)arg;
    while (!InterlockedCompareExchange(&J.quit, 0, 0)) {
        jp_answer_t answers[JP_ANSWERS];
        AcquireSRWLockExclusive(&J.lock);
        size_t nanswers = J.nanswers;
        proven_sys_mem_copy(answers, J.answers, nanswers * sizeof *answers);
        J.nanswers = 0;
        J.op.same_recycles_copy = J.recycles;
        for (size_t a = 0; a < nanswers; ++a) {
            if (answers[a].all) (void)rubraview_fileop_answer_all(&J.op, answers[a].answer);
            else (void)rubraview_fileop_answer(&J.op, answers[a].index, answers[a].answer);
        }
        if (J.skip_clashes && J.op.parked) (void)rubraview_fileop_answer_all(&J.op, RUBRAVIEW_FILEOP_ANSWER_SKIP);   /* RV-289.5: from now on too */
        bool stop = J.stop != 0;
        if (nanswers) { g_parked_sig = (size_t)-1; publish(); }
        bool done = J.finished;
        ReleaseSRWLockExclusive(&J.lock);
        if (done) break;
        bool more;
        if (stop) { rubraview_fileop_stop(&J.op); more = false; }   /* between items: nothing left half-written */
        else if (!J.prepared) { (void)rubraview_fileop_prepare(&J.op); J.prepared = true; more = true; }
        else more = rubraview_fileop_step(&J.op, 1);   /* one item, so answers and Stop are taken between */
        AcquireSRWLockExclusive(&J.lock);
        if (stop || (J.prepared && rubraview_fileop_finished(&J.op))) J.finished = true;
        publish();
        ReleaseSRWLockExclusive(&J.lock);
        if (!more && !J.finished) WaitForSingleObject(J.wake, 250);   /* only answers can move it on */
    }
    return 0;
}

static void queue_answer(size_t index, bool all, rubraview_fileop_answer_t a) {
    AcquireSRWLockExclusive(&J.lock);
    if (J.nanswers < JP_ANSWERS) J.answers[J.nanswers++] = (jp_answer_t){ .index = index, .all = all, .answer = a };
    ReleaseSRWLockExclusive(&J.lock);
    SetEvent(J.wake);
}

static void ui_answer(void *ctx, const clashui_row_t *row, rubraview_fileop_answer_t a) { (void)ctx; queue_answer(row->index, false, a); }
static void ui_answer_all(void *ctx, rubraview_fileop_answer_t a) { (void)ctx; queue_answer(0, true, a); }
static void ui_closed(void *ctx) {   /* RV-289.5: the clashes left are skipped, and any found later; the rest goes on */
    (void)ctx;
    AcquireSRWLockExclusive(&J.lock);
    J.skip_clashes = true;
    ReleaseSRWLockExclusive(&J.lock);
    SetEvent(J.wake);
}
static void ui_recycle(void *ctx, bool on) {
    (void)ctx;
    AcquireSRWLockExclusive(&J.lock);
    J.recycles = on;
    ReleaseSRWLockExclusive(&J.lock);
}

/* ---- the progress window (RV-289.3, phase C; owner: "그 창을 꺼야 안겹치는 파일들 복사도 끝나고") ----
   What the viewer's card showed, in a window of the job's own: what it does, a bar, the files and bytes, the time
   left, the names waiting. Closed (X, Esc, Stop) while it works: the job stops after the file in hand and the window
   goes. When the job has ended it stays with the summary until closed. */

#define PW_BACK 0xFF1B1E23u
#define PW_TEXT 0xFFE6E6E6u
#define PW_DIM  0xFF9AA2ACu
#define PW_ON   0xFF3E8BF0u

typedef struct jp_win {
    rubraview_window_t   *window;
    rubraview_renderer_t *renderer;
    bool   open, dirty, moving;
    double px, py;
    rubraview_pal_rect_t button;
    char   head[300], summary[300];
    rubraview_progress_t rate;
} jp_win_t;

static bool pw_show(jp_win_t *w, proven_arena_t *arena, proven_u8str_view_t ui_font, double now) {
    rubraview_window_config_t config = { .title = w->moving ? T("Rubraview - moving") : T("Rubraview - copying"), .width = 600, .height = 230 };
    w->window = rubraview_pal_window_create(arena, &config);
    if (!w->window) return false;
    int32_t ww = 0, hh = 0;
    rubraview_pal_window_get_size(w->window, &ww, &hh);
    w->renderer = rubraview_pal_render_create(arena, rubraview_pal_window_native_handle(w->window), ww, hh);
    if (!w->renderer) { rubraview_pal_window_destroy(w->window); w->window = NULL; return false; }
    rubraview_font_spec_t face = rubraview_font_spec_parse(ui_font);
    rubraview_pal_render_set_ui_font(w->renderer, face.family, face.weight, face.italic);
    rubraview_progress_begin(&w->rate, now);
    w->open = w->dirty = true;
    return true;
}

static void pw_close(jp_win_t *w) {
    if (!w->open) return;
    if (w->renderer) rubraview_pal_render_destroy(w->renderer);
    if (w->window) rubraview_pal_window_destroy(w->window);
    w->renderer = NULL;
    w->window = NULL;
    w->open = false;
}

static void pw_line(rubraview_renderer_t *r, const char *text, int n, rubraview_pal_rect_t at, double size, uint32_t argb) {
    if (n > 0) rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)text, .size = (size_t)n }, at, size, argb, RUBRAVIEW_TEXT_LEFT);
}

static void pw_draw(jp_win_t *w, const rubraview_jobwire_progress_t *p, bool ended, bool stopping, double now) {
    if (!w->open || !w->dirty) return;
    w->dirty = false;
    rubraview_renderer_t *r = w->renderer;
    int32_t ww = 0, hh = 0;
    rubraview_pal_window_get_size(w->window, &ww, &hh);
    double dpi = rubraview_pal_window_dpi_scale(w->window), pad = 18.0 * dpi, width = (double)ww - 2.0 * pad;
    rubraview_pal_render_begin(r, PW_BACK);
    double y = pad * 0.7;
    pw_line(r, w->head, (int)proven_cstr_len(w->head), (rubraview_pal_rect_t){ pad, y, width, 26.0 * dpi }, 15.0 * dpi, PW_TEXT);
    y += 34.0 * dpi;
    bool bytes = p->bytes_total > 0;
    uint64_t done = bytes ? p->bytes_done : p->done + p->skipped + p->failed, total = bytes ? p->bytes_total : p->total;
    if (ended) { done = total; }
    double f = total ? (double)done / (double)total : 0.0;
    if (f > 1.0) f = 1.0;
    rubraview_pal_rect_t bar = { pad, y, width, 14.0 * dpi };
    rubraview_pal_render_fill_rect(r, bar, 0xFF141414u, bar.height * 0.5);
    if (f > 0.0) rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ bar.x, bar.y, fmax(bar.height, bar.width * f), bar.height }, PW_ON, bar.height * 0.5);
    y += 24.0 * dpi;
    char line[400], a[32], b[32];
    int n;
    if (ended) {
        n = (int)proven_cstr_len(w->summary);
        pw_line(r, w->summary, n, (rubraview_pal_rect_t){ pad, y, width, 22.0 * dpi }, 13.0 * dpi, PW_TEXT);
    } else {
        (void)rubraview_picker_size_text(a, sizeof a, p->bytes_done);
        (void)rubraview_picker_size_text(b, sizeof b, p->bytes_total);
        n = rubraview_fmt(line, sizeof line, T("{} of {} files, {} of {} ({}%)"), PROVEN_ARG(p->done + p->skipped + p->failed), PROVEN_ARG(p->total),
                          PROVEN_ARG(a), PROVEN_ARG(b), PROVEN_ARG(rubraview_progress_percent(done, total)));
        pw_line(r, line, n, (rubraview_pal_rect_t){ pad, y, width, 22.0 * dpi }, 13.0 * dpi, PW_TEXT);
        y += 24.0 * dpi;
        if (stopping) n = rubraview_fmt(line, sizeof line, "{}", PROVEN_ARG(T("stopping after the file in hand...")));
        else n = (int)time_left_text(line, sizeof line, rubraview_progress_left(&w->rate, done, total, now));
        pw_line(r, line, n, (rubraview_pal_rect_t){ pad, y, width, 22.0 * dpi }, 13.0 * dpi, PW_DIM);
        if (p->clashes > 0) {
            y += 24.0 * dpi;
            n = rubraview_fmt(line, sizeof line, T("{} names already there wait in the other window"), PROVEN_ARG(p->clashes));
            pw_line(r, line, n, (rubraview_pal_rect_t){ pad, y, width, 22.0 * dpi }, 13.0 * dpi, PW_DIM);
        }
    }
    const char *word = ended ? T("Close") : T("Stop");
    double tw = 0.0;
    (void)rubraview_pal_render_measure_text(r, cstr(word), 13.0 * dpi, &tw);
    w->button = (rubraview_pal_rect_t){ (double)ww - pad - tw - 40.0 * dpi, (double)hh - pad - 36.0 * dpi, tw + 40.0 * dpi, 36.0 * dpi };
    bool over = pal_rect_has(w->button, w->px, w->py);
    if (!stopping || ended) {
        rubraview_pal_render_fill_rect(r, w->button, over ? 0x50FFFFFFu : 0xFF2A2F36u, 4.0 * dpi);
        rubraview_pal_render_stroke_rect(r, w->button, 0xFF4A525Cu, 1.0, 4.0 * dpi);
        rubraview_pal_render_draw_text(r, cstr(word), w->button, 13.0 * dpi, PW_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
    if (!rubraview_pal_render_end(r)) w->dirty = true;
}

/* Its messages: true when it was closed (X, Esc, its button). */
static bool pw_pump(jp_win_t *w) {
    bool closed = false;
    rubraview_window_event_t event;
    while (w->open && rubraview_pal_window_poll_event(w->window, &event)) {
        switch (event.kind) {
            case RUBRAVIEW_WINDOW_EVENT_CLOSE: closed = true; break;
            case RUBRAVIEW_WINDOW_EVENT_RESIZE: rubraview_pal_render_resize(w->renderer, event.resize.width, event.resize.height); w->dirty = true; break;
            case RUBRAVIEW_WINDOW_EVENT_PAINT: w->dirty = true; break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE: w->px = event.mouse.x; w->py = event.mouse.y; w->dirty = true; break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN:
                if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT && pal_rect_has(w->button, event.mouse.x, event.mouse.y)) closed = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN:
                if (key_is(event.key.combo, "Escape") || key_is(event.key.combo, "Enter")) closed = true;
                break;
            default: break;
        }
    }
    return closed;
}

/* The words the viewer says at the end, said here too (its items counted as job_ended counts them). */
static void pw_summary(jp_win_t *w) {
    size_t done = 0, replaced = 0, same = 0;
    for (size_t i = 0; i < J.op.count; ++i) {
        const rubraview_fileop_item_t *it = &J.op.items[i];
        if (it->did == RUBRAVIEW_FILEOP_DID_COPY || it->did == RUBRAVIEW_FILEOP_DID_MOVE) done++;
        else if (it->did == RUBRAVIEW_FILEOP_DID_RECYCLE_SOURCE) same++;
        if (it->overwrote) replaced++;
    }
    (void)rubraview_fmt(w->summary, sizeof w->summary,
                        w->moving ? T("{} moved, {} replaced, {} the same, {} left, {} could not be  (Ctrl+Z takes it back)")
                                  : T("{} copied, {} replaced, {} the same, {} left, {} could not be  (Ctrl+Z takes it back)"),
                        PROVEN_ARG(done), PROVEN_ARG(replaced), PROVEN_ARG(same), PROVEN_ARG(J.op.skipped), PROVEN_ARG(J.op.failed));
    w->dirty = true;
}

/* ---- RV-292: the note left while the job runs (owner: "프로세스 시작할 때 어떤 로그를 남기고 끝날 때 그걸 지우도록") ---- */

static rubraview_jobnote_t g_note;
static char g_note_path[2048];

static void note_write(void) {
    if (!g_note_path[0]) return;
    char text[4096];
    size_t n = rubraview_jobnote_format(&g_note, text, sizeof text);
    if (n) (void)rubraview_pal_fs_write_file(cstr(g_note_path), (proven_u8str_view_t){ .ptr = (const proven_byte_t *)text, .size = n });
}

static void note_begin(proven_u8str_view_t dir, bool moving, proven_u8str_view_t where, proven_u8str_view_t first, uint64_t count) {
    if (dir.size == 0 || !rubraview_pal_fs_make_dirs(dir)) return;   /* no note can be left: the job runs without one */
    g_note = (rubraview_jobnote_t){ .when = (int64_t)time(NULL), .count = count };
    g_note.pid = rubraview_pal_process_self(&g_note.started);
    (void)rubraview_fmt(g_note.kind, sizeof g_note.kind, "{}", PROVEN_ARG(moving ? "move" : "copy"));
    (void)rubraview_fmt(g_note.where, sizeof g_note.where, "{}", PROVEN_ARG(where));
    (void)rubraview_fmt(g_note.first, sizeof g_note.first, "{}", PROVEN_ARG(first));
    char name[64];
    if (!rubraview_jobnote_name(&g_note, name, sizeof name)) return;
    int k = rubraview_fmt(g_note_path, sizeof g_note_path, "{}\\{}", PROVEN_ARG(dir), PROVEN_ARG(name));
    if (k <= 0 || (size_t)k >= sizeof g_note_path) { g_note_path[0] = '\0'; return; }
    note_write();
}

/* It ended as it should: the note goes. */
static void note_end(void) {
    if (g_note_path[0]) (void)rubraview_pal_fs_delete(cstr(g_note_path));
    g_note_path[0] = '\0';
}

/* ---- the process ---- */

static bool start_job(proven_arena_t *arena, rubraview_jobwire_msg_t *m, clashui_t *ui, rubraview_lang_t *lang, char **temp_dir, jp_win_t *pw) {
    rubraview_jobwire_start_t st;
    rubraview_jobwire_get_start(m, &st);
    if (m->bad || st.dir.size == 0 || st.count == 0) return false;
    if (st.lang_file.size > 0) {   /* the viewer's language for the windows */
        proven_u8str_view_t text = rubraview_pal_fs_read_file(arena, st.lang_file, RUBRAVIEW_LANG_MAX_FILE);
        rubraview_lang_stats_t stats;
        if (text.size > 0 && rubraview_lang_load(lang, text, &stats)) rubraview_lang_use(lang);
    }
    char *face = rubraview_strndup_view(st.ui_font);
    ui->ui_font = face ? proven_u8str_view_from_cstr(face) : PROVEN_LIT("");
    pw->moving = st.moving;
    (void)rubraview_fmt(pw->head, sizeof pw->head, st.moving ? T("moving {} into {}") : T("copying {} into {}"),
                        PROVEN_ARG(st.count), PROVEN_ARG(rubraview_path_basename(st.dir)));
    ui->same_recycles_copy = J.recycles = st.same_recycles_copy;
    *temp_dir = st.temp_dir.size ? rubraview_strndup_view(st.temp_dir) : NULL;
    char *d = rubraview_strndup_view(st.dir);
    if (!d) return false;
    rubraview_fileop_init(&J.op, st.moving ? RUBRAVIEW_FILEOP_MOVE : RUBRAVIEW_FILEOP_COPY, rubraview_pal_fileop_fs(), d);
    J.op.fs.ctx = (void *)&J.copied;   /* RV-269: the copy says how far it is in the file in hand */
    rubraview_free(d);
    proven_u8str_view_t first = {0};
    for (uint32_t i = 0; i < st.count; ++i) {
        proven_u8str_view_t s = rubraview_jobwire_get_str(m);
        if (i == 0) first = s;
        char *c = rubraview_strndup_view(s);
        if (c && s.size) (void)rubraview_fileop_add(&J.op, c);
        rubraview_free(c);
    }
    if (m->bad) return false;
    note_begin(st.journal_dir, st.moving, st.dir, first, st.count);
    J.wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    J.worker = J.wake ? CreateThread(NULL, 0, worker, NULL, 0, NULL) : NULL;
    return J.worker != NULL;
}

int jobproc_main(const wchar_t *pipe_name) {
    SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS);
    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) return 1;
    static rubraview_mem_t mem;
    if (!rubraview_mem_init(&mem)) { CoUninitialize(); return 1; }
    proven_arena_t *arena = &mem.persist.arena;

    for (int tries = 0; tries < 50; ++tries) {   /* the viewer made it just before starting us */
        P.pipe = CreateFileW(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (P.pipe != INVALID_HANDLE_VALUE) break;
        if (GetLastError() == ERROR_PIPE_BUSY) (void)WaitNamedPipeW(pipe_name, 200); else Sleep(100);
    }
    if (P.pipe == INVALID_HANDLE_VALUE) { rubraview_mem_release(&mem); CoUninitialize(); return 2; }
    P.wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    P.thread = P.wake ? CreateThread(NULL, 0, writer, NULL, 0, NULL) : NULL;

    clashui_t ui = { .answer = ui_answer, .answer_all = ui_answer_all, .closed = ui_closed, .recycle_set = ui_recycle };
    static jp_win_t pw;
    static rubraview_lang_t lang;
    char *temp_dir = NULL;
    bool started = false, ended = false;
    double began = rubraview_pal_time_now_seconds(), last_progress = 0.0;
    uint32_t seen_version = 0;
    rubraview_jobwire_progress_t sent = { .total = UINT64_MAX }, shown = {0};
    double noted = 0.0;
    int code = 0;

    for (;;) {
        /* the viewer's messages */
        if (pipe_read_some()) {
            size_t at = 0, used = 0;
            rubraview_jobwire_msg_t m;
            int got;
            while ((got = rubraview_jobwire_take(P.in + at, P.in_len - at, &m, &used)) == 1) {
                at += used;
                if (m.type == RUBRAVIEW_JOBWIRE_START && !started) {
                    started = start_job(arena, &m, &ui, &lang, &temp_dir, &pw);
                    if (!started) { code = 3; goto out; }
                    (void)pw_show(&pw, arena, ui.ui_font, rubraview_pal_time_now_seconds());
                } else if (m.type == RUBRAVIEW_JOBWIRE_STOP) {
                    InterlockedExchange(&J.stop, 1);
                    SetEvent(J.wake);
                } else if (m.type == RUBRAVIEW_JOBWIRE_SHOW_CLASHES) {
                    if (ui.nrows) (void)clashui_show(&ui, arena);
                }
            }
            if (got < 0) InterlockedExchange(&P.broken, 1);   /* not our viewer's: no more is read */
            if (at > 0) {
                proven_sys_mem_move(P.in, P.in + at, P.in_len - at);
                P.in_len -= at;
            }
        }
        if (!started) {
            if (P.broken || rubraview_pal_time_now_seconds() - began > 15.0) { code = 4; goto out; }
            Sleep(20);
            continue;
        }
        /* the worker's rows and counts */
        rubraview_jobwire_progress_t now;
        bool finished, skipping;
        AcquireSRWLockExclusive(&J.lock);
        now = J.progress;
        now.bytes_done += atomic_load(&J.copied);
        finished = J.finished;
        skipping = J.skip_clashes;
        if (J.version != seen_version) {
            seen_version = J.version;
            clashui_row_t *rows = J.nrows ? (clashui_row_t *)rubraview_alloc_zero(J.nrows, sizeof *rows) : NULL;
            size_t n = 0;
            for (size_t i = 0; rows && i < J.nrows; ++i) {
                rows[n] = J.rows[i];
                rows[n].source = rubraview_strdup(J.rows[i].source);
                rows[n].target = rubraview_strdup(J.rows[i].target);
                n++;
            }
            bool grew = n > ui.nrows;
            clashui_set_rows(&ui, rows, n);
            if (n == 0) clashui_close(&ui);
            else if (grew && !skipping) (void)clashui_show(&ui, arena);   /* new questions: the window comes */
        }
        ReleaseSRWLockExclusive(&J.lock);
        double t = rubraview_pal_time_now_seconds();
        if (proven_sys_mem_cmp(&now, &sent, sizeof now) != 0 && (t - last_progress > 0.25 || finished)) {
            send_with(put_progress, &now);
            sent = now;
            last_progress = t;
            rubraview_progress_sample(&pw.rate, now.bytes_total ? now.bytes_done : now.done, t);
            pw.dirty = true;
            shown = now;
        }
        if (t - noted > 3.0 && !finished) {   /* RV-292: how far, now and then */
            g_note.done = now.done + now.skipped + now.failed;
            note_write();
            noted = t;
        }
        if (finished && !ended) {
            ended = true;
            WaitForSingleObject(J.worker, 5000);
            send_items_and_end(J.stop != 0);
            note_end();   /* RV-292: it ended as it should (finished or stopped) */
            if (temp_dir) (void)rubraview_pal_fs_delete_tree(cstr(temp_dir));   /* a page out of an archive, written for the job */
            pw_summary(&pw);
            clashui_close(&ui);
        }
        (void)clashui_pump(&ui);
        clashui_tick(&ui);
        clashui_draw(&ui);
        if (pw_pump(&pw)) {   /* closed: while it works, the job stops after the file in hand */
            if (!ended) { InterlockedExchange(&J.stop, 1); SetEvent(J.wake); }
            pw_close(&pw);
        }
        pw_draw(&pw, &shown, ended, J.stop != 0, t);
        if (ended && !ui.open && !pw.open) break;
        (void)MsgWaitForMultipleObjects(0, NULL, FALSE, 40, QS_ALLINPUT);
    }

out:
    clashui_close(&ui);
    pw_close(&pw);
    InterlockedExchange(&J.quit, 1);
    if (J.wake) SetEvent(J.wake);
    if (J.worker) { WaitForSingleObject(J.worker, 5000); CloseHandle(J.worker); }
    InterlockedExchange(&P.quit, 1);
    if (P.wake) SetEvent(P.wake);
    if (P.thread) { WaitForSingleObject(P.thread, 3000); CloseHandle(P.thread); }
    CloseHandle(P.pipe);
    rubraview_free(temp_dir);
    rubraview_mem_release(&mem);
    CoUninitialize();
    return code;
}
