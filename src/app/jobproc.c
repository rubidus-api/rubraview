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
#include "packwin.h"
#include "rubraview/fileop.h"
#include "rubraview/jobwire.h"
#include "rubraview/jobnote.h"
#include "rubraview/archive_job.h"
#include "rubraview/arcedit.h"
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
    bool    quiet, quiet_shown;                 /* RV-297: the viewer's file on screen: a window only when it takes long or did not go well */
    double  began;
    jp_answer_t answers[JP_ANSWERS];
    size_t  nanswers;
    /* what the worker shows the main thread */
    clashui_row_t *rows;
    size_t  nrows;
    uint32_t version;
    rubraview_jobwire_progress_t progress;
} J = { .lock = SRWLOCK_INIT };

static uint32_t g_close_after;   /* RV-299: seconds the window stays once the work has ended (0: until closed) */
static double   g_ended_at;      /* when it ended, for that */

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
    const char *title;                 /* RV-292: the window's title for an archive's work ("" the copy's or move's) */
    const char *yes;                   /* RV-294: a question with the summary (an older split's parts): its button */
    rubraview_pal_rect_t yes_btn;
    bool   yes_pressed;
    /* RV-312 (owner, 2026-11-20: "압축을 풀고 나서, 혹은 파일 이동이나 복사를 끝마치고 나서 ... 메인창 내에 위젯이 떠서 안내하는데,
       이건 창 외부의 별개 프로세스 창에서 처리해야 할 일 ... 해당 위치를 열기 같은 것도 역시 외부 창에서"): where the work went,
       opened from here when it has ended — in Rubraview's open-file screen or in Explorer — and a locked archive's password
       asked here, so the main window keeps its own work */
    char   dest[1100];                 /* the folder ("" none) */
    char   dest_file[1100];            /* a file made there (a new archive), shown selected in Explorer */
    rubraview_pal_rect_t here_btn, explorer_btn;
    bool   here_pressed, explorer_pressed;
    bool   asking_pw;                  /* the archive is locked: the field and its button (`yes`) are up */
    rubraview_editctl_t pw_ec;
    char   pw_buf[256], pw_comp[64];
    editbox_view_t pw_view;
} jp_win_t;

static bool pw_show(jp_win_t *w, proven_arena_t *arena, proven_u8str_view_t ui_font, double now) {
    rubraview_window_config_t config = { .title = w->title ? w->title : w->moving ? T("Rubraview - moving") : T("Rubraview - copying"), .width = 600, .height = 230,
                                         .no_activate = true };   /* RV-296: the viewer keeps the keys */
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
    if (ended && w->asking_pw) {   /* RV-312: the password, typed here (hidden) */
        rubraview_pal_rect_t box = { pad, y + 30.0 * dpi, width, 28.0 * dpi };
        rubraview_pal_render_fill_rect(r, box, 0xFF101215u, 3.0 * dpi);
        rubraview_pal_render_stroke_rect(r, box, PW_ON, 1.0, 3.0 * dpi);
        w->pw_view.box = (rubraview_pal_rect_t){ box.x + 6.0 * dpi, box.y + 4.0 * dpi, box.width - 12.0 * dpi, box.height - 8.0 * dpi };
        w->pw_view.size = 14.0 * dpi;
        w->pw_view.secret = true;
        editbox_draw(r, &w->pw_ec, &w->pw_view, proven_u8str_view_from_cstr(w->pw_comp), true, PW_TEXT, dpi);
    }
    w->here_btn = w->explorer_btn = (rubraview_pal_rect_t){0};
    if (ended && !w->yes && !w->asking_pw && w->dest[0]) {   /* RV-312: where it went, left of Close */
        const char *words[2] = { T("Show in Explorer"), T("Open it here") };
        rubraview_pal_rect_t *rects[2] = { &w->explorer_btn, &w->here_btn };
        double right = w->button.x - 8.0 * dpi;
        for (int i = 0; i < 2; ++i) {
            double bw = 0.0;
            (void)rubraview_pal_render_measure_text(r, cstr(words[i]), 13.0 * dpi, &bw);
            *rects[i] = (rubraview_pal_rect_t){ right - bw - 32.0 * dpi, w->button.y, bw + 32.0 * dpi, w->button.height };
            right = rects[i]->x - 8.0 * dpi;
            bool on = pal_rect_has(*rects[i], w->px, w->py);
            rubraview_pal_render_fill_rect(r, *rects[i], i == 1 ? (on ? 0xFF4F9CF7u : PW_ON) : (on ? 0x50FFFFFFu : 0xFF2A2F36u), 4.0 * dpi);
            if (i == 0) rubraview_pal_render_stroke_rect(r, *rects[i], 0xFF4A525Cu, 1.0, 4.0 * dpi);
            rubraview_pal_render_draw_text(r, cstr(words[i]), *rects[i], 13.0 * dpi, PW_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
    }
    w->yes_btn = (rubraview_pal_rect_t){0};
    if (ended && w->yes) {   /* RV-294: the question's own button, left of Close */
        double yw = 0.0;
        (void)rubraview_pal_render_measure_text(r, cstr(w->yes), 13.0 * dpi, &yw);
        w->yes_btn = (rubraview_pal_rect_t){ w->button.x - yw - 48.0 * dpi, w->button.y, yw + 40.0 * dpi, w->button.height };
        bool on = pal_rect_has(w->yes_btn, w->px, w->py);
        rubraview_pal_render_fill_rect(r, w->yes_btn, on ? 0xFF4F9CF7u : PW_ON, 4.0 * dpi);
        rubraview_pal_render_draw_text(r, cstr(w->yes), w->yes_btn, 13.0 * dpi, PW_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
    if (!rubraview_pal_render_end(r)) w->dirty = true;
}

/* RV-312: Open it here — the folder in the open-file screen of the viewer that is open (`--browse`, handed over as
   Explorer's verbs are); Show in Explorer — Windows' own, a new archive selected. */
static void pw_where_went(jp_win_t *w, proven_arena_t *arena) {
    if (w->here_pressed) {
        w->here_pressed = false;
        proven_u8str_view_t exe = rubraview_pal_process_executable(arena);
        char line[2400];
        int n = rubraview_fmt(line, sizeof line, "\"{}\" --browse \"{}\"", PROVEN_ARG(exe), PROVEN_ARG(w->dest));
        if (n > 0 && (size_t)n < sizeof line) (void)rubraview_pal_process_start_apart(proven_u8str_view_from_cstr(line));
    }
    if (w->explorer_pressed) {
        w->explorer_pressed = false;
        if (w->dest_file[0]) (void)rubraview_pal_shell_reveal(cstr(w->dest_file));
        else (void)rubraview_pal_shell_open_folder(cstr(w->dest), false);
    }
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
                if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT && w->yes && pal_rect_has(w->yes_btn, event.mouse.x, event.mouse.y)) w->yes_pressed = true;
                if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT && w->here_btn.width > 0 && pal_rect_has(w->here_btn, event.mouse.x, event.mouse.y)) w->here_pressed = true;
                if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT && w->explorer_btn.width > 0 && pal_rect_has(w->explorer_btn, event.mouse.x, event.mouse.y)) w->explorer_pressed = true;
                if (w->asking_pw && event.mouse.button == RUBRAVIEW_MOUSE_LEFT)
                    (void)editbox_pointer_down(w->renderer, &w->pw_ec, &w->pw_view, event.mouse.x, event.mouse.y, false);
                w->dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_TEXT:
            case RUBRAVIEW_WINDOW_EVENT_COMPOSITION:
                if (w->asking_pw) {
                    proven_u8str_view_t typed = { .ptr = (const proven_byte_t *)event.text.utf8, .size = event.text.length };
                    if (event.kind == RUBRAVIEW_WINDOW_EVENT_TEXT) { editbox_text(&w->pw_ec, typed, true); w->pw_comp[0] = '\0'; }
                    else (void)rubraview_fmt(w->pw_comp, sizeof w->pw_comp, "{}", PROVEN_ARG(typed));
                    w->dirty = true;
                }
                break;
            case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN:
                if (w->asking_pw) {   /* RV-312: Enter extracts with it, Esc leaves the archive locked */
                    if (key_is(event.key.combo, "Enter")) w->yes_pressed = true;
                    else if (key_is(event.key.combo, "Escape")) closed = true;
                    else (void)editbox_key(w->window, &w->pw_ec, event.key.combo, true);
                    w->dirty = true;
                    break;
                }
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

/* ---- RV-292 (owner, 2026-11-15: C — "새 압축 파일 만들기도 그렇게"): an extraction or a compression in this process.
   archwork.c asks everything first (the folder, the password, the Compress box) and sends what to do; the work runs
   here on a thread as it ran there, archive by archive; how it went goes back as ARCHIVE_DONE, the names an
   extraction found taken with it, for the viewer to ask. */

typedef struct jp_park { uint32_t archive; char root[RUBRAVIEW_ARCHIVE_PATH]; rubraview_extract_parked_t p; } jp_park_t;

static struct {
    bool on, quiet, step;   /* RV-295: step, a drag's: the window goes by itself when it ended well */
    uint8_t kind;
    char **names;
    size_t n, at;
    char *dest, *inner, *password, *output;
    uint8_t mode;
    size_t *only, only_count;
    uint8_t *rules;
    int32_t codepage;
    rubraview_extract_job_t ex;
    rubraview_compress_job_t pk;
    HANDLE thread;
    volatile LONG done, stop;
    uint32_t written, skipped, failed;
    char last_root[RUBRAVIEW_ARCHIVE_PATH];
    jp_park_t *parked;
    size_t pn, pcap;
    bool asking;                      /* RV-294: the Compress window is open, the work not begun */
    packwin_t pack;
    char *dir;                        /* the sources' folder: a bare name goes there */
    /* RV-292 X2: files into an archive */
    rubraview_arcedit_job_t aj;
    int add_result;
    char made[RUBRAVIEW_ARCHIVE_PATH], backup[RUBRAVIEW_ARCHIVE_PATH];
} A;

/* The new archive written beside the old one, then in its place — the old one kept as name.rvold in the same step
   (ReplaceFileW), or for a conversion the new one under its own free name and the old set to the Recycle Bin. */
static void archive_add(void) {
    size_t n = A.n / 2;
    rubraview_arcedit_add_t *adds = (rubraview_arcedit_add_t *)rubraview_alloc_zero(n ? n : 1, sizeof *adds);
    if (!adds) { A.add_result = RUBRAVIEW_ARCEDIT_NO_MEMORY; return; }
    for (size_t i = 0; i < n; ++i) adds[i] = (rubraview_arcedit_add_t){ .path = A.names[2 * i], .name = A.names[2 * i + 1] };
    A.aj = (rubraview_arcedit_job_t){ .archive_path = A.dest, .codepage = (rubraview_codepage_t)A.codepage, .entries = A.only_count ? A.only : NULL,
                                      .entry_count = A.only_count, .target = (rubraview_arcedit_target_t)A.pk.format, .out_path = A.output, .level = 6 };
    A.add_result = (int)rubraview_arcedit_add_files(&A.aj, adds, n);
    rubraview_free(adds);
    if (A.add_result != RUBRAVIEW_ARCEDIT_OK) { (void)rubraview_pal_fs_delete(cstr(A.output)); return; }
    proven_u8str_view_t archive = cstr(A.dest);
    bool ok;
    if (A.mode == 1) {   /* made a ZIP or a 7z of its own name */
        char name[1024], want[RUBRAVIEW_ARCHIVE_PATH];
        size_t k = rubraview_arcedit_converted_name(rubraview_path_basename(archive), (rubraview_arcedit_target_t)A.pk.format, name, sizeof name);
        int w = k ? rubraview_fmt(want, sizeof want, "{}/{}", PROVEN_ARG(rubraview_path_dirname(archive)), PROVEN_ARG(rubraview_bytes_view(name, (int)k))) : -1;
        ok = w > 0 && rubraview_archive_free_name(cstr(want), rubraview_pal_fs_exists, A.made, sizeof A.made) &&
             rubraview_pal_fs_move(cstr(A.output), cstr(A.made));
        if (ok) for (size_t v = 0; v < A.aj.volume_count; ++v) (void)rubraview_pal_fs_recycle(cstr(A.aj.volumes[v]));
    } else {
        ok = rubraview_fmt(A.backup, sizeof A.backup, "{}.rvold", PROVEN_ARG(archive)) < (int)sizeof A.backup &&
             rubraview_pal_fs_replace_keeping(archive, cstr(A.output), cstr(A.backup));
        if (ok) rubraview_fmt(A.made, sizeof A.made, "{}", PROVEN_ARG(archive));
        else A.backup[0] = '\0';
    }
    if (!ok) { (void)rubraview_pal_fs_delete(cstr(A.output)); A.add_result = RUBRAVIEW_ARCEDIT_UNWRITABLE; }
}

static DWORD WINAPI archive_worker(void *arg) {
    (void)arg;
    if (A.kind == RUBRAVIEW_JOBWIRE_ADD) {
        archive_add();
    } else if (A.kind == RUBRAVIEW_JOBWIRE_COMPRESS) {
        rubraview_compress_run(&A.pk);
    } else {
        for (; A.at < A.n; ++A.at) {
            char dest[RUBRAVIEW_ARCHIVE_PATH];
            if (A.dest && A.dest[0]) rubraview_fmt(dest, sizeof dest, "{}", PROVEN_ARG(A.dest));
            else {   /* beside the archive */
                rubraview_fmt(dest, sizeof dest, "{}", PROVEN_ARG(A.names[A.at]));
                char *cut = rubraview_cstr_rchr(dest, '\\'), *cut2 = rubraview_cstr_rchr(dest, '/');
                if (cut2 > cut) cut = cut2;
                if (cut) *cut = '\0'; else rubraview_fmt(dest, sizeof dest, ".");
            }
            rubraview_extract_job_t *ex = &A.ex;
            bool stop = atomic_load(&ex->cancel) || A.stop;
            proven_sys_mem_set(ex, 0, sizeof *ex);
            atomic_store(&ex->cancel, stop);
            ex->archive = A.names[A.at];
            ex->dest = dest;
            ex->mode = (rubraview_extract_mode_t)A.mode;
            ex->exists = RUBRAVIEW_EXISTS_PARK;   /* RV-258: a name taken is asked by the viewer, not numbered unasked */
            ex->rules = A.rules;
            ex->password = A.password && A.password[0] ? A.password : NULL;
            ex->only = A.only;
            ex->only_count = A.only_count;
            ex->inner = A.only ? A.inner : NULL;
            ex->codepage = (rubraview_codepage_t)A.codepage;
            rubraview_extract_run(ex);
            for (size_t k = 0; k < ex->parked_count; ++k) {
                if (A.pn == A.pcap) {
                    size_t cap = A.pcap ? A.pcap * 2 : 16;
                    jp_park_t *grown = (jp_park_t *)rubraview_realloc((proven_mem_mut_t){ .ptr = (proven_byte_t *)A.parked, .size = cap * sizeof *grown });
                    if (!grown) { rubraview_free(ex->parked[k].path); continue; }
                    A.parked = grown;
                    A.pcap = cap;
                }
                jp_park_t *x = &A.parked[A.pn++];
                x->archive = (uint32_t)A.at;
                rubraview_fmt(x->root, sizeof x->root, "{}", PROVEN_ARG(ex->root));
                x->p = ex->parked[k];
            }
            rubraview_free(ex->parked);
            ex->parked = NULL;
            ex->parked_count = 0;
            A.written += ex->written; A.skipped += ex->skipped; A.failed += ex->failed;
            if (ex->root[0]) rubraview_fmt(A.last_root, sizeof A.last_root, "{}", PROVEN_ARG(ex->root));
            if (ex->result == RUBRAVIEW_ARCHIVE_CANCELLED || ex->result == RUBRAVIEW_ARCHIVE_PASSWORD_NEEDED ||
                ex->result == RUBRAVIEW_ARCHIVE_PASSWORD_WRONG) break;   /* stopped here: the viewer asks, or lets go */
        }
    }
    InterlockedExchange(&A.done, 1);
    return 0;
}

static char *dup_of(proven_u8str_view_t v) { return rubraview_strndup_view(v); }

static bool start_archive(proven_arena_t *arena, rubraview_jobwire_msg_t *m, clashui_t *ui, rubraview_lang_t *lang, jp_win_t *pw) {
    rubraview_jobwire_archive_t a;
    rubraview_jobwire_get_archive(m, &a);
    if (m->bad || a.count == 0 || (a.kind != RUBRAVIEW_JOBWIRE_EXTRACT && a.kind != RUBRAVIEW_JOBWIRE_COMPRESS && a.kind != RUBRAVIEW_JOBWIRE_ADD)) return false;
    if (a.lang_file.size > 0) {
        proven_u8str_view_t text = rubraview_pal_fs_read_file(arena, a.lang_file, RUBRAVIEW_LANG_MAX_FILE);
        rubraview_lang_stats_t stats;
        if (text.size > 0 && rubraview_lang_load(lang, text, &stats)) rubraview_lang_use(lang);
    }
    char *face = dup_of(a.ui_font);
    ui->ui_font = face ? proven_u8str_view_from_cstr(face) : PROVEN_LIT("");
    A.on = true;
    A.kind = a.kind;
    A.quiet = a.quiet;
    g_close_after = a.close_after;
    A.step = a.step;
    A.n = a.count;
    A.names = (char **)rubraview_alloc_zero(a.count, sizeof *A.names);
    if (!A.names) return false;
    A.dest = dup_of(a.dest); A.inner = dup_of(a.inner); A.password = dup_of(a.password); A.output = dup_of(a.output);
    A.mode = a.mode;
    A.codepage = a.codepage;
    for (uint32_t i = 0; i < a.count; ++i) A.names[i] = dup_of(rubraview_jobwire_get_str(m));
    if (a.only_count) {
        A.only = (size_t *)rubraview_alloc_zero(a.only_count, sizeof *A.only);
        if (!A.only) return false;
        for (uint32_t i = 0; i < a.only_count; ++i) A.only[i] = (size_t)rubraview_jobwire_get_u64(m);
        A.only_count = a.only_count;
    }
    if (a.rules_count) {
        A.rules = (uint8_t *)rubraview_alloc_zero(a.rules_count, 1);
        if (!A.rules) return false;
        for (uint32_t i = 0; i < a.rules_count; ++i) A.rules[i] = rubraview_jobwire_get_u8(m);
    }
    if (m->bad) return false;
    if (A.kind == RUBRAVIEW_JOBWIRE_ADD) A.pk.format = (rubraview_pack_format_t)a.format;   /* the arcedit target, kept here */
    if (A.kind == RUBRAVIEW_JOBWIRE_COMPRESS && a.mode == 1) {   /* RV-294: asked here, in a window of this process */
        A.asking = true;
        A.dir = dup_of(a.dest);
        packwin_init(&A.pack, a.output, a.level, (rubraview_pack_method_t)a.method, a.solid_off, a.volume_bytes, a.names_hidden);
        if (A.n == 1) (void)rubraview_fmt(A.pack.what, sizeof A.pack.what, "{}", PROVEN_ARG(rubraview_path_basename(cstr(A.names[0]))));
        else (void)rubraview_fmt(A.pack.what, sizeof A.pack.what, T("{} files and folders"), PROVEN_ARG(A.n));
        if (!packwin_show(&A.pack, arena, ui->ui_font)) return false;
        note_begin(a.journal_dir, false, a.output, proven_u8str_view_from_cstr(A.names[0]), A.n);
        if (g_note_path[0]) { (void)rubraview_fmt(g_note.kind, sizeof g_note.kind, "compress"); note_write(); }
        pw->title = T("Rubraview - compressing");
        return true;
    }
    if (A.kind == RUBRAVIEW_JOBWIRE_COMPRESS) {
        A.pk = (rubraview_compress_job_t){ .sources = (const char *const *)A.names, .source_count = A.n, .output = A.output,
                                           .format = (rubraview_pack_format_t)a.format, .level = a.level, .method = (rubraview_pack_method_t)a.method,
                                           .solid_off = a.solid_off, .volume_bytes = a.volume_bytes, .names_hidden = a.names_hidden,
                                           .password = A.password && A.password[0] ? A.password : NULL };
    }
    note_begin(a.journal_dir, false, A.kind == RUBRAVIEW_JOBWIRE_COMPRESS ? a.output : a.dest, proven_u8str_view_from_cstr(A.names[0]),
               A.kind == RUBRAVIEW_JOBWIRE_ADD ? A.n / 2 : A.n);
    if (g_note_path[0]) {   /* what it is, in the note */
        (void)rubraview_fmt(g_note.kind, sizeof g_note.kind, "{}",
                            PROVEN_ARG(A.kind == RUBRAVIEW_JOBWIRE_COMPRESS ? "compress" : A.kind == RUBRAVIEW_JOBWIRE_ADD ? "archive" : "extract"));
        note_write();
    }
    pw->title = A.kind == RUBRAVIEW_JOBWIRE_COMPRESS ? T("Rubraview - compressing") : A.kind == RUBRAVIEW_JOBWIRE_ADD ? T("Rubraview - writing into an archive")
                                                     : T("Rubraview - extracting");
    proven_u8str_view_t first = rubraview_path_basename(proven_u8str_view_from_cstr(A.kind == RUBRAVIEW_JOBWIRE_COMPRESS ? A.output
                                                                                    : A.kind == RUBRAVIEW_JOBWIRE_ADD ? A.dest : A.names[0]));
    (void)rubraview_fmt(pw->head, sizeof pw->head, A.kind == RUBRAVIEW_JOBWIRE_COMPRESS ? T("Compressing into {}")
                                                   : A.kind == RUBRAVIEW_JOBWIRE_ADD ? T("Putting files into {}") : T("Extracting {}"), PROVEN_ARG(first));
    A.thread = CreateThread(NULL, 0, archive_worker, NULL, 0, NULL);
    return A.thread != NULL;
}

/* RV-294: Compress pressed: the archive's place from the name (a bare one beside the files, ".zip" when it has no
   ending), the choices to the viewer for its settings, the window gone, the work begun. */
static bool pack_begin(jp_win_t *pw, proven_arena_t *arena, proven_u8str_view_t ui_font) {
    packwin_t *p = &A.pack;
    proven_u8str_view_t t = rubraview_textedit_text(&p->field[PACKWIN_NAME].te);
    bool bare = true;
    for (size_t i = 0; i < t.size; ++i) if (t.ptr[i] == '\\' || t.ptr[i] == '/' || t.ptr[i] == ':') bare = false;
    char out[RUBRAVIEW_ARCHIVE_PATH];
    int n = bare ? rubraview_fmt(out, sizeof out, "{}{}{}", PROVEN_ARG(A.dir ? A.dir : ""), PROVEN_ARG(A.dir && A.dir[0] && A.dir[proven_cstr_len(A.dir) - 1] != '\\' ? "\\" : ""), PROVEN_ARG(t))
                 : rubraview_fmt(out, sizeof out, "{}", PROVEN_ARG(t));
    if (n <= 0 || (size_t)n >= sizeof out - 5) return false;
    bool sevenz = packwin_7z(p);
    bool zip_end = n >= 4 && (out[n - 4] == '.' && (out[n - 3] | 0x20) == 'z' && (out[n - 2] | 0x20) == 'i' && (out[n - 1] | 0x20) == 'p');
    if (!sevenz && !zip_end) rubraview_cstr_append(out, sizeof out, ".zip");
    rubraview_free(A.output);
    A.output = rubraview_strdup(out);
    rubraview_free(A.password);
    A.password = rubraview_strndup_view(rubraview_textedit_text(&p->field[PACKWIN_PW].te));
    rubraview_pack_format_t format = sevenz ? RUBRAVIEW_PACK_7Z : RUBRAVIEW_PACK_ZIP;
    A.pk = (rubraview_compress_job_t){ .sources = (const char *const *)A.names, .source_count = A.n, .output = A.output, .format = format,
                                       .level = rubraview_pack_level_of(p->level), .method = packwin_method(p), .solid_off = sevenz && p->solid_off,
                                       .volume_bytes = p->split, .names_hidden = sevenz && p->names_hidden && A.password && A.password[0],
                                       .password = A.password && A.password[0] ? A.password : NULL };
    if (!P.broken) {   /* the choices kept by the viewer (D-266) */
        AcquireSRWLockExclusive(&P.lock);
        rubraview_jobwire_begin(&P.out, RUBRAVIEW_JOBWIRE_PACK_CHOICES);
        rubraview_jobwire_put_u8(&P.out, sevenz);
        rubraview_jobwire_put_i32(&P.out, p->level);
        rubraview_jobwire_put_i32(&P.out, (int32_t)p->method);
        rubraview_jobwire_put_u8(&P.out, p->solid_off);
        rubraview_jobwire_put_u64(&P.out, p->split);
        rubraview_jobwire_put_u8(&P.out, p->names_hidden);
        (void)rubraview_jobwire_finish(&P.out);
        ReleaseSRWLockExclusive(&P.lock);
        SetEvent(P.wake);
    }
    packwin_close(p);
    packwin_free(p);
    A.asking = false;
    if (g_note_path[0]) { (void)rubraview_fmt(g_note.where, sizeof g_note.where, "{}", PROVEN_ARG(A.output)); note_write(); }
    (void)rubraview_fmt(pw->head, sizeof pw->head, T("Compressing into {}"), PROVEN_ARG(rubraview_path_basename(cstr(A.output))));
    (void)pw_show(pw, arena, ui_font, rubraview_pal_time_now_seconds());
    A.thread = CreateThread(NULL, 0, archive_worker, NULL, 0, NULL);
    return A.thread != NULL;
}

/* How far: the bytes, the files, and in `skipped` the compression's phase, in `failed` the archive it is on. */
static rubraview_jobwire_progress_t archive_progress(void) {
    if (A.kind == RUBRAVIEW_JOBWIRE_ADD)
        return (rubraview_jobwire_progress_t){ .bytes_done = (uint64_t)atomic_load(&A.aj.done), .bytes_total = (uint64_t)atomic_load(&A.aj.total), .total = A.n / 2 };
    if (A.kind == RUBRAVIEW_JOBWIRE_COMPRESS)
        return (rubraview_jobwire_progress_t){ .bytes_done = atomic_load(&A.pk.done), .bytes_total = atomic_load(&A.pk.total),
                                               .done = atomic_load(&A.pk.files_done), .total = atomic_load(&A.pk.files_total),
                                               .skipped = (uint64_t)atomic_load(&A.pk.phase) };
    return (rubraview_jobwire_progress_t){ .bytes_done = atomic_load(&A.ex.done), .bytes_total = atomic_load(&A.ex.total),
                                           .done = atomic_load(&A.ex.files_at), .total = atomic_load(&A.ex.files_total), .failed = A.at };
}

static void archive_send_done(void) {
    rubraview_jobwire_archive_done_t d = { .written = A.written, .skipped = A.skipped, .failed = A.failed, .at = (uint32_t)A.at,
                                           .root = cstr(A.last_root), .parked = (uint32_t)A.pn };
    if (A.kind == RUBRAVIEW_JOBWIRE_ADD) {
        d.result = (uint8_t)A.add_result;
        d.made = cstr(A.made);
        d.set_name = cstr(A.backup);
        d.root = PROVEN_LIT("");
        d.why = PROVEN_LIT("");
        d.parked = 0;
    } else if (A.kind == RUBRAVIEW_JOBWIRE_COMPRESS) {
        d.result = (uint8_t)A.pk.result;
        d.why = cstr(A.pk.why);
        d.made = cstr(A.pk.written);
        d.set_name = cstr(A.pk.set_name);
        d.files = A.pk.files;
        d.folders = A.pk.folders;
        d.older_parts = A.pk.older_parts;
        d.root = PROVEN_LIT("");
        d.parked = 0;
    } else {
        d.result = (uint8_t)A.ex.result;
        d.why = cstr(A.ex.why);
    }
    if (P.broken) return;
    AcquireSRWLockExclusive(&P.lock);
    rubraview_jobwire_begin_archive_done(&P.out, &d);
    for (size_t k = 0; k < d.parked; ++k) {
        rubraview_jobwire_put_u32(&P.out, A.parked[k].archive);
        rubraview_jobwire_put_str(&P.out, cstr(A.parked[k].root));
        rubraview_jobwire_put_u64(&P.out, A.parked[k].p.index);
        rubraview_jobwire_put_u64(&P.out, A.parked[k].p.size);
        rubraview_jobwire_put_i64(&P.out, A.parked[k].p.mtime);
        rubraview_jobwire_put_str(&P.out, cstr(A.parked[k].p.path));
    }
    (void)rubraview_jobwire_finish(&P.out);
    ReleaseSRWLockExclusive(&P.lock);
    SetEvent(P.wake);
}

/* RV-312: the folder a finished archive's work went to, for its window's Open it here and Show in Explorer */
static void archive_dest(jp_win_t *pw) {
    pw->dest[0] = pw->dest_file[0] = '\0';
    const char *file = A.kind == RUBRAVIEW_JOBWIRE_ADD ? (A.add_result == RUBRAVIEW_ARCEDIT_OK ? A.made : "")
                     : A.kind == RUBRAVIEW_JOBWIRE_COMPRESS ? (A.pk.result == RUBRAVIEW_ARCHIVE_DONE ? A.pk.written : "") : "";
    if (file[0]) {
        (void)rubraview_fmt(pw->dest_file, sizeof pw->dest_file, "{}", PROVEN_ARG(file));
        (void)rubraview_fmt(pw->dest, sizeof pw->dest, "{}", PROVEN_ARG(rubraview_path_dirname(cstr(file))));
    } else if (A.kind == RUBRAVIEW_JOBWIRE_EXTRACT && A.written > 0 && A.last_root[0]) {
        (void)rubraview_fmt(pw->dest, sizeof pw->dest, "{}", PROVEN_ARG(A.last_root));
    }
}

static void archive_summary(jp_win_t *pw) {
    archive_dest(pw);
    if (A.kind == RUBRAVIEW_JOBWIRE_ADD) {
        if (A.add_result == RUBRAVIEW_ARCEDIT_OK)
            (void)rubraview_fmt(pw->summary, sizeof pw->summary, T("{} files put into {}"), PROVEN_ARG(A.n / 2), PROVEN_ARG(rubraview_path_basename(cstr(A.made))));
        else
            (void)rubraview_fmt(pw->summary, sizeof pw->summary, T("not put into the archive: {}"), PROVEN_ARG(rubraview_arcedit_result_text((rubraview_arcedit_result_t)A.add_result)));
        pw->dirty = true;
        return;
    }
    rubraview_archive_result_t r = A.kind == RUBRAVIEW_JOBWIRE_COMPRESS ? A.pk.result : A.ex.result;
    const char *why = A.kind == RUBRAVIEW_JOBWIRE_COMPRESS ? A.pk.why : A.ex.why;
    if (A.kind == RUBRAVIEW_JOBWIRE_COMPRESS && r == RUBRAVIEW_ARCHIVE_DONE)
        (void)rubraview_fmt(pw->summary, sizeof pw->summary, T("compressed: {} ({} files, {} folders)"),
                            PROVEN_ARG(rubraview_path_basename(cstr(A.pk.written))), PROVEN_ARG(A.pk.files), PROVEN_ARG(A.pk.folders));
    else if (A.kind == RUBRAVIEW_JOBWIRE_EXTRACT && (r == RUBRAVIEW_ARCHIVE_DONE || r == RUBRAVIEW_ARCHIVE_PARTLY || r == RUBRAVIEW_ARCHIVE_NOTHING))
        (void)rubraview_fmt(pw->summary, sizeof pw->summary, T("extracted {} files into {}"), PROVEN_ARG(A.written),
                            PROVEN_ARG(rubraview_path_basename(cstr(A.last_root))));
    else
        (void)rubraview_fmt(pw->summary, sizeof pw->summary, "{}{}{}", PROVEN_ARG(rubraview_archive_result_text(r)), PROVEN_ARG(why[0] ? "  -  " : ""), PROVEN_ARG(why));
    pw->dirty = true;
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
    J.quiet = st.quiet;
    g_close_after = st.close_after;
    J.began = rubraview_pal_time_now_seconds();
    (void)rubraview_fmt(pw->head, sizeof pw->head, st.moving ? T("moving {} into {}") : T("copying {} into {}"),
                        PROVEN_ARG(st.count), PROVEN_ARG(rubraview_path_basename(st.dir)));
    ui->same_recycles_copy = J.recycles = st.same_recycles_copy;
    *temp_dir = st.temp_dir.size ? rubraview_strndup_view(st.temp_dir) : NULL;
    char *d = rubraview_strndup_view(st.dir);
    if (!d) return false;
    (void)rubraview_fmt(pw->dest, sizeof pw->dest, "{}", PROVEN_ARG(st.dir));   /* RV-312: where they went */
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
    rubraview_pal_window_helper_process();   /* RV-312: its windows never take another launch's request */
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
                    if (!J.quiet) (void)pw_show(&pw, arena, ui.ui_font, rubraview_pal_time_now_seconds());
                } else if (m.type == RUBRAVIEW_JOBWIRE_ARCHIVE && !started) {   /* RV-292 */
                    started = start_archive(arena, &m, &ui, &lang, &pw);
                    if (!started) { code = 3; goto out; }
                    if (!A.quiet && !A.asking) (void)pw_show(&pw, arena, ui.ui_font, rubraview_pal_time_now_seconds());
                } else if (m.type == RUBRAVIEW_JOBWIRE_STOP) {
                    InterlockedExchange(&J.stop, 1);
                    if (J.wake) SetEvent(J.wake);
                    InterlockedExchange(&A.stop, 1);   /* RV-292 */
                    atomic_store(&A.ex.cancel, true);
                    atomic_store(&A.pk.cancel, true);
                    atomic_store(&A.aj.cancel, true);
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
        if (A.on && A.asking) {   /* RV-294: the Compress window, then the work */
            int v = packwin_pump(&A.pack);
            packwin_draw(&A.pack);
            if (v > 0 && !pack_begin(&pw, arena, ui.ui_font)) v = -1;
            if (v < 0) {   /* not compressed: said to the viewer, nothing left behind */
                packwin_close(&A.pack);
                packwin_free(&A.pack);
                A.asking = false;
                A.pk.result = RUBRAVIEW_ARCHIVE_CANCELLED;
                archive_send_done();
                note_end();
                break;
            }
            (void)MsgWaitForMultipleObjects(0, NULL, FALSE, 30, QS_ALLINPUT);
            continue;
        }
        if (A.on) {   /* RV-292: an extraction or a compression */
            double t = rubraview_pal_time_now_seconds();
            bool fin = InterlockedCompareExchange(&A.done, 0, 0) != 0;
            rubraview_jobwire_progress_t now = archive_progress();
            if (t - last_progress > 0.25 || fin) {
                send_with(put_progress, &now);
                last_progress = t;
                rubraview_progress_sample(&pw.rate, now.bytes_done, t);
                shown = now;
                shown.done = now.done; shown.total = now.total; shown.skipped = 0; shown.failed = 0;
                pw.dirty = true;
            }
            if (t - noted > 3.0 && !fin && g_note_path[0]) { g_note.done = now.done; note_write(); noted = t; }
            if (fin && !ended) {
                ended = true;
                g_ended_at = t;
                WaitForSingleObject(A.thread, 5000);
                if (A.kind == RUBRAVIEW_JOBWIRE_COMPRESS && A.pk.older_parts && A.pk.set_name[0] && !pw.open && !A.quiet)
                    (void)pw_show(&pw, arena, ui.ui_font, t);   /* RV-312: asked here, never in the main window */
                bool locked = A.kind == RUBRAVIEW_JOBWIRE_EXTRACT && !A.quiet &&
                              (A.ex.result == RUBRAVIEW_ARCHIVE_PASSWORD_NEEDED || A.ex.result == RUBRAVIEW_ARCHIVE_PASSWORD_WRONG);
                if (locked && !pw.open) (void)pw_show(&pw, arena, ui.ui_font, t);
                if (locked && pw.open) {   /* RV-312: the password asked here, the main window left alone */
                    (void)rubraview_fmt(pw.summary, sizeof pw.summary,
                                        A.ex.result == RUBRAVIEW_ARCHIVE_PASSWORD_WRONG ? T("wrong password for {}: type it again, then Enter")
                                                                                         : T("{} is locked: type its password, then Enter"),
                                        PROVEN_ARG(rubraview_path_basename(cstr(A.names[A.at < A.n ? A.at : A.n - 1]))));
                    editbox_begin(&pw.pw_ec, pw.pw_buf, sizeof pw.pw_buf, false);
                    pw.pw_comp[0] = '\0';
                    pw.asking_pw = true;
                    pw.yes = T("Extract");
                    rubraview_pal_window_text_input(pw.window, true);
                    rubraview_pal_window_text_ime(pw.window, false);
                    pw.dirty = true;
                } else if (A.kind == RUBRAVIEW_JOBWIRE_COMPRESS && A.pk.older_parts && A.pk.set_name[0] && pw.open) {
                    /* D-267, RV-294: asked here, where the password is: the older set's parts to the Recycle Bin and again */
                    char parts[64][RUBRAVIEW_PACK_PART_PATH];
                    size_t np = rubraview_pack_set_parts(A.pk.format, A.pk.set_name, parts, 64);
                    (void)rubraview_fmt(pw.summary, sizeof pw.summary, T("Parts of an older split {} are in the way ({} files)."),
                                        PROVEN_ARG(rubraview_path_basename(cstr(A.pk.set_name))), PROVEN_ARG(np));
                    pw.yes = T("Recycle them and compress");
                    pw.dirty = true;
                } else {
                    archive_send_done();
                    note_end();
                    archive_summary(&pw);
                    if (A.step && A.kind == RUBRAVIEW_JOBWIRE_EXTRACT && A.ex.result == RUBRAVIEW_ARCHIVE_DONE && A.failed == 0 && pw.open)
                        pw_close(&pw);   /* RV-295: the next step's window says the rest */
                }
            }
            if (ended && pw.asking_pw && pw.yes_pressed) {   /* RV-312: on with the password, from the archive it stopped at */
                pw.yes_pressed = false;
                proven_u8str_view_t typed = rubraview_textedit_text(&pw.pw_ec.te);
                if (typed.size > 0) {
                    if (A.password) { secure_wipe(rubraview_mem_mut(A.password, proven_cstr_len(A.password))); rubraview_free(A.password); }
                    A.password = rubraview_strndup_view(typed);
                    secure_wipe(rubraview_mem_mut(pw.pw_buf, sizeof pw.pw_buf));
                    pw.asking_pw = false;
                    pw.yes = NULL;
                    rubraview_pal_window_text_input(pw.window, false);
                    InterlockedExchange(&A.done, 0);
                    CloseHandle(A.thread);
                    A.thread = CreateThread(NULL, 0, archive_worker, NULL, 0, NULL);
                    ended = false;
                    pw.dirty = true;
                }
            }
            if (ended) pw_where_went(&pw, arena);
            if (ended && pw.yes && !pw.asking_pw && pw.yes_pressed) {
                char parts[64][RUBRAVIEW_PACK_PART_PATH];
                size_t np = rubraview_pack_set_parts(A.pk.format, A.pk.set_name, parts, 64), gone = 0;
                for (size_t i = 0; i < np; ++i) gone += rubraview_pal_fs_recycle(cstr(parts[i])) ? 1u : 0u;
                pw.yes = NULL;
                pw.yes_pressed = false;
                if (gone == np) {   /* again, as it was asked */
                    A.pk.result = RUBRAVIEW_ARCHIVE_DONE;
                    A.pk.why[0] = A.pk.written[0] = '\0';
                    A.pk.older_parts = false;
                    A.pk.files = A.pk.folders = 0;
                    atomic_store(&A.pk.cancel, false);
                    InterlockedExchange(&A.done, 0);
                    CloseHandle(A.thread);
                    A.thread = CreateThread(NULL, 0, archive_worker, NULL, 0, NULL);
                    ended = false;
                } else {
                    (void)rubraview_fmt(A.pk.why, sizeof A.pk.why, "{}", PROVEN_ARG(T("the older parts could not all be moved to the Recycle Bin")));
                    A.pk.older_parts = false;
                    archive_send_done();
                    note_end();
                    archive_summary(&pw);
                }
            }
            if (ended && pw.open && !pw.yes && g_close_after > 0 && t - g_ended_at >= (double)g_close_after) pw_close(&pw);   /* RV-299 */
            if (pw_pump(&pw)) {
                if (!ended) { InterlockedExchange(&A.stop, 1); atomic_store(&A.ex.cancel, true); atomic_store(&A.pk.cancel, true); }
                else if (pw.asking_pw) { pw.asking_pw = false; pw.yes = NULL; archive_send_done(); note_end(); }   /* RV-312: left locked */
                else if (pw.yes) { pw.yes = NULL; A.pk.older_parts = false; archive_send_done(); note_end(); }   /* the question left: not compressed */
                pw_close(&pw);
            }
            pw_draw(&pw, &shown, ended, A.stop != 0, t);
            if (ended && !pw.open) break;
            (void)MsgWaitForMultipleObjects(0, NULL, FALSE, 40, QS_ALLINPUT);
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
            g_ended_at = t;
            WaitForSingleObject(J.worker, 5000);
            send_items_and_end(J.stop != 0);
            note_end();   /* RV-292: it ended as it should (finished or stopped) */
            if (temp_dir) (void)rubraview_pal_fs_delete_tree(cstr(temp_dir));   /* a page out of an archive, written for the job */
            pw_summary(&pw);
            clashui_close(&ui);
            if (J.quiet) {   /* RV-297 (owner, 2026-11-16: "창이 자동으로 닫혀야 ... 메인 창에서 잠시 메시지만"): the viewer says it */
                bool well = now.failed == 0 && J.stop == 0;
                if (well) pw_close(&pw);
                else if (!pw.open) (void)pw_show(&pw, arena, ui.ui_font, t);   /* what went wrong is read here */
            }
        }
        if (J.quiet && !J.quiet_shown && !ended && !pw.open && t - J.began > 1.5) {   /* a long one shows how far it is, once */
            J.quiet_shown = true;
            (void)pw_show(&pw, arena, ui.ui_font, t);
        }
        (void)clashui_pump(&ui);
        clashui_tick(&ui);
        clashui_draw(&ui);
        if (ended && pw.open && g_close_after > 0 && t - g_ended_at >= (double)g_close_after) pw_close(&pw);   /* RV-299 */
        if (ended) pw_where_went(&pw, arena);   /* RV-312 */
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
    if (A.thread) { InterlockedExchange(&A.stop, 1); atomic_store(&A.ex.cancel, true); atomic_store(&A.pk.cancel, true);
                    WaitForSingleObject(A.thread, 10000); CloseHandle(A.thread); }
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
