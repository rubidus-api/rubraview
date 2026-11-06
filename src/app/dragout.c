/*
 * RV-223 (D-278, owner 2026-11-06): what is picked in the open-file screen dragged to Explorer or another program — a
 * left drag copies, a right drag ends in Windows' menu — and Ctrl+C / Ctrl+X inside an archive put on the clipboard.
 * Files on disk go as they are. Picked inside an archive, they are written out to a folder of ours under the system's
 * temporary folder: for a drag when the button is let go over a target (Explorer asks for the files while the drag is
 * held, so they are named from the start and made at the end), for the clipboard at once (Windows reads it as soon as
 * it is set). A move or a cut that arrived takes them out of the archive — each only when its file has left our
 * folder — written again with the one before kept for Ctrl+Z, as a page's delete is (D-89). Our folders go when the
 * viewer closes, an older crash's at the next start.
 */
#include "app.h"
#include "rubraview/archive_job.h"
#include "rubraview/arcedit.h"
#include "rubraview/dragout.h"

void arc_reopen_at(app_state_t *app, proven_u8str_view_t path, size_t page);
bool picker_enter_archive(app_state_t *app, proven_u8str_view_t path);
bool arc_temp_path(char *dst, size_t cap, const char *tag, proven_u8str_view_t leaf);

/* ---- our folders under the temporary folder ---- */

static char **g_made;
static size_t g_made_count;

static bool out_folder(char *dst, size_t cap) {
    static unsigned serial;
    char leaf[16];
    rubraview_fmt(leaf, sizeof leaf, "{}", PROVEN_ARG(++serial));
    if (!arc_temp_path(dst, cap, "out", proven_u8str_view_from_cstr(leaf))) return false;
    if (!rubraview_pal_fs_make_dirs(proven_u8str_view_from_cstr(dst))) return false;
    char **more = rubraview_realloc(rubraview_mem_mut(g_made, (g_made_count + 1) * sizeof *g_made));
    if (!more) return true;   /* made; only not removed at the end */
    g_made = more;
    g_made[g_made_count] = rubraview_strdup(dst);
    if (g_made[g_made_count]) g_made_count++;
    return true;
}

/* ---- what a drag or a copy takes ---- */

typedef struct pick {
    size_t count;
    char **tops;                 /* each picked tile's path as handed over (on disk: its own) */
    char **rels;                 /* inside an archive: each one's name in it ("a/b"), for what a move takes out */
    char dest[RUBRAVIEW_ARCHIVE_PATH];
    char archive[RUBRAVIEW_ARCHIVE_PATH];
    char inner[RUBRAVIEW_ARCHIVE_PATH];   /* the folder they were picked in, "a/b/" */
    size_t *only;                /* the archive's entries they stand for */
    size_t only_count;
    bool from_archive, movable;
} pick_t;

static void pick_free(pick_t *p) {
    if (!p) return;
    for (size_t i = 0; i < p->count; ++i) {
        if (p->tops) rubraview_free(p->tops[i]);
        if (p->rels) rubraview_free(p->rels[i]);
    }
    rubraview_free(p->tops);
    rubraview_free(p->rels);
    rubraview_free(p->only);
    rubraview_free(p);
}

/* The tiles picked, or `pressed` alone when it is not among them (a drag picks what it starts on, as Explorer). */
static size_t picked_indices(app_state_t *app, size_t pressed, size_t **out) {
    size_t n = app->picker_listing.count, picked = 0;
    *out = NULL;
    bool pressed_picked = pressed < n && app->picker_selected && app->picker_selected[pressed];
    for (size_t i = 0; app->picker_selected && i < n; ++i) picked += app->picker_selected[i] && !picker_is_up(app, i);
    if (pressed < n && !pressed_picked) {
        if (picker_is_up(app, pressed) || rubraview_picker_is_places(app->picker_dir)) return 0;
        if (app->picker_selected) {
            for (size_t i = 0; i < n; ++i) app->picker_selected[i] = false;
            app->picker_selected[pressed] = true;
        }
        *out = rubraview_alloc(sizeof **out);
        if (!*out) return 0;
        (*out)[0] = pressed;
        return 1;
    }
    if (picked == 0) return 0;
    *out = rubraview_alloc(picked * sizeof **out);
    if (!*out) return 0;
    size_t k = 0;
    for (size_t i = 0; i < n; ++i) if (app->picker_selected[i] && !picker_is_up(app, i)) (*out)[k++] = i;
    return k;
}

static pick_t *pick_make(app_state_t *app, size_t pressed) {
    if (app->picker_in_book || rubraview_picker_is_places(app->picker_dir) || rubraview_picker_is_recent(app->picker_dir)) return NULL;
    size_t *ix = NULL;
    size_t n = picked_indices(app, pressed, &ix);
    if (n == 0) return NULL;
    pick_t *p = rubraview_alloc_zero(1, sizeof *p);
    if (!p || !(p->tops = rubraview_alloc_zero(n, sizeof *p->tops)) || !(p->rels = rubraview_alloc_zero(n, sizeof *p->rels))) {
        rubraview_free(ix);
        pick_free(p);
        return NULL;
    }
    p->count = n;
    p->from_archive = picker_in_archive(app);
    bool ok = true;
    if (!p->from_archive) {
        for (size_t i = 0; ok && i < n; ++i) ok = (p->tops[i] = rubraview_strndup_view(app->picker_listing.entries[ix[i]].path)) != NULL;
    } else {
        const rubraview_archive_index_t *index = app->picker_arc_index;
        rubraview_fmt(p->archive, sizeof p->archive, "{}", PROVEN_ARG(app->picker_arc_path));
        char rel[RUBRAVIEW_ARCHIVE_PATH];
        if (picker_archive_rel(app, app->picker_dir, rel, sizeof rel - 1) && rel[0]) rubraview_fmt(p->inner, sizeof p->inner, "{}/", PROVEN_ARG(rel));
        p->only = index && index->count ? rubraview_alloc(index->count * sizeof *p->only) : NULL;
        static bool swept;   /* an older run's folders, once a session, when the first is made */
        if (!swept) { swept = true; dragout_sweep(app); }
        ok = p->only != NULL && out_folder(p->dest, sizeof p->dest);
        bool locked = false;
        for (size_t i = 0; ok && i < n; ++i) {
            const rubraview_fs_entry_t *e = &app->picker_listing.entries[ix[i]];
            char top[RUBRAVIEW_ARCHIVE_PATH];
            ok = picker_archive_rel(app, e->path, rel, sizeof rel) && rubraview_dragout_path(cstr(p->dest), e->name, top, sizeof top) > 0 &&
                 (p->tops[i] = rubraview_strdup(top)) != NULL && (p->rels[i] = rubraview_strdup(rel)) != NULL;
            if (!ok) break;
            size_t room = index->count - p->only_count;
            size_t k = rubraview_archive_index_under(index, proven_u8str_view_from_cstr(rel), p->only + p->only_count, room);
            for (size_t j = 0; j < (k < room ? k : room); ++j) locked = locked || index->items[p->only[p->only_count + j]].encrypted;
            p->only_count += k < room ? k : room;
        }
        ok = ok && p->only_count > 0;
        p->movable = rubraview_dragout_archive_movable(cstr(p->archive), locked);
    }
    rubraview_free(ix);
    if (!ok) { pick_free(p); return NULL; }
    return p;
}

static proven_u8str_view_t *pick_views(app_state_t *app, const pick_t *p) {
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, p->count, sizeof(proven_u8str_view_t));
    if (!proven_is_ok(res.err)) return NULL;
    proven_u8str_view_t *v = (proven_u8str_view_t *)(void *)res.value.ptr;
    for (size_t i = 0; i < p->count; ++i) v[i] = proven_u8str_view_from_cstr(p->tops[i]);
    return v;
}

/* ---- writing them out ---- */

typedef struct out_job {
    rubraview_extract_job_t ex;
    pick_t *pick;
    char passwords[9][257];      /* none first, then the session's, newest first */
    size_t password_count;
    HANDLE thread;
    volatile LONG done;
} out_job_t;

static DWORD WINAPI out_main(void *arg) {
    out_job_t *j = arg;
    for (size_t k = 0; k < j->password_count; ++k) {
        bool stop = atomic_load(&j->ex.cancel);
        proven_sys_mem_set(&j->ex, 0, sizeof j->ex);
        atomic_store(&j->ex.cancel, stop);
        j->ex.archive = j->pick->archive;
        j->ex.dest = j->pick->dest;
        j->ex.mode = RUBRAVIEW_EXTRACT_AS_IS;
        j->ex.exists = RUBRAVIEW_EXISTS_NUMBER;
        j->ex.only = j->pick->only;
        j->ex.only_count = j->pick->only_count;
        j->ex.inner = j->pick->inner;
        j->ex.password = j->passwords[k][0] ? j->passwords[k] : NULL;
        rubraview_extract_run(&j->ex);
        if (j->ex.result != RUBRAVIEW_ARCHIVE_PASSWORD_NEEDED && j->ex.result != RUBRAVIEW_ARCHIVE_PASSWORD_WRONG) break;
    }
    InterlockedExchange(&j->done, 1);
    return 0;
}

static out_job_t *out_start(app_state_t *app, pick_t *pick) {
    out_job_t *j = rubraview_alloc_zero(1, sizeof *j);
    if (!j) return NULL;
    j->pick = pick;
    j->password_count = 1;   /* "" */
    for (size_t k = app->password_count; k > 0 && j->password_count < 9; --k)
        rubraview_fmt(j->passwords[j->password_count++], sizeof j->passwords[0], "{}",
                      PROVEN_ARG(rubraview_bytes_view(app->passwords[k - 1], (int)app->password_lens[k - 1])));
    j->thread = CreateThread(NULL, 0, out_main, j, 0, NULL);
    if (!j->thread) { secure_wipe(rubraview_mem_mut(j->passwords, sizeof j->passwords)); rubraview_free(j); return NULL; }
    return j;
}

/* The job ended (waited for): whether everything was written; what went wrong said. */
static bool out_end(app_state_t *app, out_job_t *j) {
    WaitForSingleObject(j->thread, INFINITE);
    CloseHandle(j->thread);
    secure_wipe(rubraview_mem_mut(j->passwords, sizeof j->passwords));
    rubraview_archive_result_t r = j->ex.result;
    bool ok = (r == RUBRAVIEW_ARCHIVE_DONE || r == RUBRAVIEW_ARCHIVE_NOTHING) && j->ex.failed == 0;
    if (!ok) {
        char line[384];
        if (r == RUBRAVIEW_ARCHIVE_PASSWORD_NEEDED || r == RUBRAVIEW_ARCHIVE_PASSWORD_WRONG)
            rubraview_fmt(line, sizeof line, "{}", PROVEN_ARG(T("its files are locked: extract them once with the password, then drag or copy them")));
        else if (r == RUBRAVIEW_ARCHIVE_CANCELLED) rubraview_fmt(line, sizeof line, "{}", PROVEN_ARG(T("not taken out: stopped")));
        else rubraview_fmt(line, sizeof line, "{}{}{}", PROVEN_ARG(rubraview_archive_result_text(r)), PROVEN_ARG(j->ex.why[0] ? "  -  " : ""), PROVEN_ARG(j->ex.why));
        osd_say(app, cstr(line));
        (void)rubraview_pal_fs_delete_tree(cstr(j->pick->dest));
    }
    return ok;
}

/* ---- a move or a cut that arrived: out of the archive ---- */

typedef struct cut_job {
    rubraview_arcedit_job_t job;
    char archive[RUBRAVIEW_ARCHIVE_PATH], out[RUBRAVIEW_ARCHIVE_PATH], dir[RUBRAVIEW_ARCHIVE_PATH];
    size_t *entries;
    size_t gone, kept;           /* tiles that left, tiles that stayed */
    HANDLE thread;
    volatile LONG done;
    int result;
} cut_job_t;

static cut_job_t *g_cut;

static DWORD WINAPI cut_main(void *arg) {
    cut_job_t *c = arg;
    c->result = (int)rubraview_arcedit_run(&c->job);
    InterlockedExchange(&c->done, 1);
    return 0;
}

/* Each tile whose file left our folder is taken out of the archive; one that is still there stays in it. */
static void cut_arrived(app_state_t *app, const pick_t *p) {
    if (!p || !p->from_archive) return;
    if (g_cut || app->arc_job) { osd_say(app, TU8("moved out; the archive is still being written, so they stay in it")); return; }
    if (!p->movable) { osd_say(app, TU8("copied: this archive cannot be written again, so they stay in it")); return; }
    rubraview_archive_index_t ix = { 0 };
    rubraview_codepage_t cp = app->archive_codepage;
    if (picker_index_read(app, p->archive, cp, &ix) != RUBRAVIEW_ARCHIVE_DONE) { osd_say(app, TU8("moved out; the archive could not be read again")); return; }
    cut_job_t *c = rubraview_alloc_zero(1, sizeof *c);
    if (c) c->entries = rubraview_alloc((ix.count ? ix.count : 1) * sizeof *c->entries);
    size_t count = 0;
    for (size_t i = 0; c && c->entries && i < p->count; ++i) {
        if (rubraview_pal_fs_exists(proven_u8str_view_from_cstr(p->tops[i]))) { c->kept++; continue; }
        c->gone++;
        count += rubraview_archive_index_under(&ix, proven_u8str_view_from_cstr(p->rels[i]), c->entries + count, ix.count - count);
    }
    rubraview_archive_index_free(&ix);
    if (!c || !c->entries || count == 0) {
        if (c) { rubraview_free(c->entries); rubraview_free(c); }
        if (count == 0) osd_say(app, TU8("copied; nothing left the folder, so the archive is as it was"));
        return;
    }
    rubraview_fmt(c->archive, sizeof c->archive, "{}", PROVEN_ARG(p->archive));
    rubraview_fmt(c->out, sizeof c->out, "{}.rvnew", PROVEN_ARG(p->archive));
    rubraview_fmt(c->dir, sizeof c->dir, "{}", PROVEN_ARG(app->picker_dir));
    c->job = (rubraview_arcedit_job_t){ .archive_path = c->archive, .codepage = cp, .op = RUBRAVIEW_ARCEDIT_DELETE, .entries = c->entries,
                                        .entry_count = count, .target = RUBRAVIEW_ARCEDIT_SAME, .out_path = c->out, .level = 6 };
    c->thread = CreateThread(NULL, 0, cut_main, c, 0, NULL);
    if (!c->thread) { rubraview_free(c->entries); rubraview_free(c); osd_say(app, TU8("moved out; the archive could not be written again")); return; }
    arc_undo_drop(app);   /* one step back, and it is this one's from here */
    g_cut = c;
    osd_say(app, TU8("taking them out of the archive..."));
}

/* The archive written without them takes the old one's place, the old one kept for Ctrl+Z (D-89). */
static void cut_finish(app_state_t *app, cut_job_t *c) {
    proven_u8str_view_t archive = cstr(c->archive);
    bool book = app->source.archive_path.size > 0 &&
                (rubraview_path_same(app->source.archive_path, archive) || rubraview_path_same(rubraview_page_source_first_volume(&app->source), archive));
    size_t page = book && current_page_index(app) >= 0 ? (size_t)current_page_index(app) : 0;
    proven_u8str_view_t opened = book ? app_keep(app, app->source.archive_path) : PROVEN_LIT("");
    if (book) { history_remember(app); unload_all_pages(app); source_close(app); }
    char backup[sizeof app->arc_undo_backup];
    bool kept = rubraview_fmt(backup, sizeof backup, "{}.rvold", PROVEN_ARG(c->archive)) < (int)sizeof backup &&
                rubraview_pal_fs_replace(archive, cstr(backup));
    bool done = kept && rubraview_pal_fs_replace(cstr(c->out), archive);
    if (!done && kept) (void)rubraview_pal_fs_replace(cstr(backup), archive);
    if (!done) (void)rubraview_pal_fs_delete(cstr(c->out));
    if (done) {
        rubraview_fmt(app->arc_undo_backup, sizeof app->arc_undo_backup, "{}", PROVEN_ARG(backup));
        rubraview_fmt(app->arc_undo_path, sizeof app->arc_undo_path, "{}", PROVEN_ARG(c->archive));
    }
    if (book) arc_reopen_at(app, opened, page);
    if (app->picker_open && app->picker_arc_path && rubraview_path_same(cstr(app->picker_arc_path), archive) && picker_enter_archive(app, archive))
        picker_navigate(app, app_keep(app, cstr(c->dir)));   /* the listing keeps the view: ours goes with the job */
    char line[256];
    if (!done) rubraview_fmt(line, sizeof line, "{}", PROVEN_ARG(T("moved out; the archive could not be replaced (is it open elsewhere?)")));
    else if (c->kept) rubraview_fmt(line, sizeof line, T("{} taken out of the archive, {} left in it (they did not arrive)  (Ctrl+Z takes it back)"),
                                    PROVEN_ARG(c->gone), PROVEN_ARG(c->kept));
    else rubraview_fmt(line, sizeof line, T("{} moved out of the archive  (Ctrl+Z takes it back)"), PROVEN_ARG(c->gone));
    osd_say(app, cstr(line));
}

/* A move or a cut told: Explorer may still be moving the files when it says so, so each tile is looked at again on
   each pass until all have left our folder or ten seconds have gone. */
static pick_t *g_pending;
static double g_pending_until;

static void move_told(app_state_t *app, pick_t *p) {
    if (g_pending) { cut_arrived(app, g_pending); pick_free(g_pending); }
    g_pending = p;
    g_pending_until = rubraview_pal_time_now_seconds() + 10.0;
}

static void pending_poll(app_state_t *app, bool quitting) {
    if (!g_pending) return;
    bool all_gone = true;
    for (size_t i = 0; all_gone && i < g_pending->count; ++i) all_gone = !rubraview_pal_fs_exists(proven_u8str_view_from_cstr(g_pending->tops[i]));
    if (!all_gone && !quitting && rubraview_pal_time_now_seconds() < g_pending_until) return;
    if (!quitting) cut_arrived(app, g_pending);
    pick_free(g_pending);
    g_pending = NULL;
}

/* ---- the drag ---- */

/* RV-226 (D-283): a drop the target still takes on its own thread (Explorer), and since when. */
static pick_t *g_async_pick;
static double g_async_since;

typedef struct drag_ctx { app_state_t *app; pick_t *pick; bool ok; } drag_ctx_t;

static bool out_done(void *ctx, double *share) {
    out_job_t *j = ctx;
    uint64_t done = atomic_load(&j->ex.done), total = atomic_load(&j->ex.total);
    *share = total ? (double)done / (double)total : 0.0;
    return InterlockedCompareExchange(&j->done, 0, 0) != 0;
}

/* Let go over a target: written out now, the drag waiting (Esc gives it up). */
static bool drag_ready(void *ctx) {
    drag_ctx_t *d = ctx;
    if (!d->pick->from_archive) return d->ok = true;
    out_job_t *j = out_start(d->app, d->pick);
    if (!j) return d->ok = false;
    char text[256];
    rubraview_fmt(text, sizeof text, "{}", PROVEN_ARG(T("Taking them out of the archive... (Esc stops it)")));
    if (!rubraview_pal_wait_with_progress(d->app->window, out_done, j, cstr(text))) atomic_store(&j->ex.cancel, true);
    d->ok = out_end(d->app, j);
    rubraview_free(j);
    return d->ok;
}

/* What the target did with a drag, once it has said: a move out of an archive takes them out of it once they arrived;
   on disk the folder is listed again. Frees `p` (or hands it on). */
static void drag_done(app_state_t *app, pick_t *p, uint32_t done) {
    if (done & RUBRAVIEW_DROP_MOVE) {
        if (p->from_archive) { move_told(app, p); p = NULL; }
        else if (app->picker_open) picker_navigate(app, app_keep(app, app->picker_dir));   /* moved away: listed again */
    } else if (done && p->from_archive) {
        char line[96];
        rubraview_fmt(line, sizeof line, T("{} copied out of the archive"), PROVEN_ARG(p->count));
        osd_say(app, cstr(line));
    }
    pick_free(p);
}

void picker_drag_out(app_state_t *app, size_t pressed, bool right) {
    if (rubraview_pal_dragging_out() || archwork_busy(app)) return;
    pick_t *p = pick_make(app, pressed);
    if (!p) return;
    proven_u8str_view_t *views = pick_views(app, p);
    uint32_t allowed = rubraview_dragout_effects(p->from_archive, p->movable, right);
    drag_ctx_t ctx = { .app = app, .pick = p };
    /* RV-227: files on disk may be let go on the basket button (an archive's entries have no path to keep) */
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    rubraview_pal_rect_t basket = picker_basket_rect(app, rubraview_pal_window_dpi_scale(app->window), (double)win_w, 0);
    rubraview_pal_window_set_own_drop(app->window, !p->from_archive && picker_basket_shown(app), basket.x, basket.y, basket.width, basket.height);
    uint32_t done = views ? rubraview_pal_drag_out(app->window, views, p->count, allowed, right, drag_ready, &ctx) : 0u;
    rubraview_pal_window_set_own_drop(app->window, false, 0.0, 0.0, 0.0, 0.0);
    if (views && rubraview_pal_window_own_drop_taken(app->window)) {
        proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, p->count, sizeof(proven_u8str_view_t));
        if (proven_is_ok(res.err)) {
            app->basket_pending = (proven_u8str_view_t*)(void*)res.value.ptr;
            for (size_t i = 0; i < p->count; ++i) app->basket_pending[i] = app_keep(app, views[i]);
            app->basket_pending_count = p->count;
            rubraview_pal_rect_t upper = picker_basket_rect(app, rubraview_pal_window_dpi_scale(app->window), (double)win_w, 1);
            chooser_open_basket(app, BASKET_LIST_PUT_IN, basket.x, basket.y + basket.height, upper.y);
        }
        pick_free(p);
        return;
    }
    if (done & RUBRAVIEW_DROP_PENDING) {   /* RV-226: the target takes them on its own thread; the viewer goes on */
        g_async_pick = p;
        g_async_since = rubraview_pal_time_now_seconds();
        return;
    }
    drag_done(app, p, done);
}

/* ---- the clipboard ---- */

static out_job_t *g_clip_job;    /* being written out for the clipboard */
static bool g_clip_moving;
static pick_t *g_clip_pick;      /* on the clipboard now, from an archive */

static void clip_forget(void) {
    pick_free(g_clip_pick);
    g_clip_pick = NULL;
}

/* Ctrl+C / Ctrl+X inside an archive: written out now (Windows reads the clipboard as soon as it is set). */
void picker_clip_put_archive(app_state_t *app, bool moving) {
    if (g_clip_job) { osd_say(app, TU8("still taking the last ones out (Esc stops it)")); return; }
    size_t focus = app->picker.focus;
    bool any = false;
    for (size_t i = 0; app->picker_selected && i < app->picker_listing.count; ++i) any = any || (app->picker_selected[i] && !picker_is_up(app, i));
    pick_t *p = pick_make(app, any ? SIZE_MAX : focus);
    if (!p) { osd_say(app, TU8("pick some files first")); return; }
    if (moving && !p->movable) {
        pick_free(p);
        osd_say(app, TU8("this archive cannot be written again: Ctrl+C copies out of it"));
        return;
    }
    g_clip_job = out_start(app, p);
    if (!g_clip_job) { pick_free(p); return; }
    g_clip_moving = moving;
    osd_say(app, TU8("taking them out of the archive..."));
}

/* Our own Ctrl+V took a cut of ours: the same as Explorer's paste saying it moved. */
void dragout_clip_pasted(app_state_t *app) {
    if (g_clip_pick && g_clip_moving && rubraview_pal_clipboard_is_ours()) {
        cut_arrived(app, g_clip_pick);
        clip_forget();
    }
}

bool dragout_stop(app_state_t *app) {
    if (!g_clip_job) return false;
    atomic_store(&g_clip_job->ex.cancel, true);
    osd_say(app, TU8("stopping..."));
    return true;
}

bool dragout_busy(void) { return g_clip_job != NULL || g_cut != NULL || g_pending != NULL; }

/* Each pass: the writing out for the clipboard, a paste of a cut told back, the archive written without them. */
void dragout_poll(app_state_t *app, bool quitting) {
    if (g_clip_job) {
        out_job_t *j = g_clip_job;
        if (quitting) atomic_store(&j->ex.cancel, true);
        if (!quitting && !InterlockedCompareExchange(&j->done, 0, 0)) {
            uint64_t done = atomic_load(&j->ex.done), total = atomic_load(&j->ex.total);
            char line[128];
            rubraview_fmt(line, sizeof line, T("taking them out of the archive... {} %  (Esc stops it)"), PROVEN_ARG(total ? (int)(done * 100u / total) : 0));
            osd_say(app, cstr(line));
        } else {
            g_clip_job = NULL;
            bool ok = out_end(app, j);
            pick_t *p = j->pick;
            rubraview_free(j);
            if (ok && !quitting) {
                proven_u8str_view_t *views = pick_views(app, p);
                if (views && rubraview_pal_clipboard_set_files_told(app->window, views, p->count, g_clip_moving)) {
                    clip_forget();
                    g_clip_pick = p;
                    p = NULL;
                    char line[128];
                    rubraview_fmt(line, sizeof line, g_clip_moving ? T("{} to move: Ctrl+V in another folder") : T("{} to copy: Ctrl+V where they go"),
                                  PROVEN_ARG(g_clip_pick->count));
                    osd_say(app, cstr(line));
                } else {
                    osd_say(app, TU8("the clipboard would not take it"));
                }
            }
            pick_free(p);
        }
    }
    if (g_clip_pick && !quitting) {
        uint32_t said = rubraview_pal_clipboard_said();
        if ((said & RUBRAVIEW_DROP_MOVE) && g_clip_moving) {
            move_told(app, g_clip_pick);
            g_clip_pick = NULL;
        } else if (!rubraview_pal_clipboard_is_ours()) {
            clip_forget();   /* something else was copied since */
        }
    }
    if (g_async_pick) {   /* RV-226: has the target said what it did? */
        uint32_t done = 0;
        if (!quitting && rubraview_pal_drag_out_finished(&done)) {
            pick_t *p = g_async_pick;
            g_async_pick = NULL;
            drag_done(app, p, done);
        } else if (quitting || rubraview_pal_time_now_seconds() - g_async_since > RUBRAVIEW_DRAGOUT_ASYNC_WAIT) {
            rubraview_pal_drag_out_forget();   /* unheard: nothing taken out of an archive */
            pick_free(g_async_pick);
            g_async_pick = NULL;
        }
    }
    if (!g_cut) pending_poll(app, quitting);
    if (g_cut) {
        cut_job_t *c = g_cut;
        if (quitting) atomic_store(&c->job.cancel, true);
        if (!quitting && !InterlockedCompareExchange(&c->done, 0, 0)) return;
        WaitForSingleObject(c->thread, INFINITE);
        CloseHandle(c->thread);
        g_cut = NULL;
        if (!quitting && c->result == RUBRAVIEW_ARCEDIT_OK) cut_finish(app, c);
        else if (!quitting) osd_say(app, cstr(rubraview_arcedit_result_text((rubraview_arcedit_result_t)c->result)));
        else (void)rubraview_pal_fs_delete(cstr(c->out));
        rubraview_arcedit_job_free(&c->job);
        rubraview_free(c->entries);
        rubraview_free(c);
    }
}

/* Ctrl+Z in the open-file screen right after a move out of the archive it shows: the archive as it was. */
bool dragout_undo(app_state_t *app) {
    if (!app->picker_open || !app->arc_undo_backup[0] || app->arc_job || g_cut || !app->picker_arc_path ||
        !rubraview_path_same(cstr(app->picker_arc_path), cstr(app->arc_undo_path)))
        return false;
    proven_u8str_view_t archive = app_keep(app, cstr(app->arc_undo_path)), dir = app_keep(app, app->picker_dir);
    bool book = app->source.archive_path.size > 0 && rubraview_path_same(app->source.archive_path, archive);
    size_t page = book && current_page_index(app) >= 0 ? (size_t)current_page_index(app) : 0;
    if (book) { history_remember(app); unload_all_pages(app); source_close(app); }
    bool ok = rubraview_pal_fs_replace(cstr(app->arc_undo_backup), archive);
    if (ok) { app->arc_undo_backup[0] = '\0'; app->arc_undo_path[0] = '\0'; }
    if (book) arc_reopen_at(app, archive, page);
    if (picker_enter_archive(app, archive)) picker_navigate(app, dir);
    osd_say(app, ok ? TU8("the archive is as it was before the last change") : TU8("could not take the change back"));
    return true;
}

/* The end: ours off the clipboard (never written out for it), our folders gone. An older run's: dragout_sweep. */
void dragout_quit(app_state_t *app) {
    /* RV-226: a target still copying out of our folders keeps them: dragout_sweep takes them a day later */
    bool still_taken = g_async_pick != NULL;
    dragout_poll(app, true);
    clip_forget();
    rubraview_pal_clipboard_let_go();
    for (size_t i = 0; i < g_made_count; ++i) {
        if (!still_taken) (void)rubraview_pal_fs_delete_tree(cstr(g_made[i]));
        rubraview_free(g_made[i]);
    }
    rubraview_free(g_made);
    g_made = NULL;
    g_made_count = 0;
}

void dragout_sweep(app_state_t *app) {
    char probe[RUBRAVIEW_ARCHIVE_PATH];
    if (!arc_temp_path(probe, sizeof probe, "out", PROVEN_LIT("x"))) return;
    proven_u8str_view_t temp = rubraview_path_dirname(cstr(probe));
    rubraview_fs_listing_t listing = rubraview_pal_fs_list_dir(app->arena, temp);
    int64_t now = (int64_t)time(NULL);
    for (size_t i = 0; i < listing.count; ++i) {
        const rubraview_fs_entry_t *e = &listing.entries[i];
        if (!e->is_directory || e->name.size < 14 || proven_sys_mem_cmp(e->name.ptr, "rubraview-out-", 14) != 0) continue;
        if (now - e->mtime > 24 * 3600) (void)rubraview_pal_fs_delete_tree(e->path);
    }
}
