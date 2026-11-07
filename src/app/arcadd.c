/* RV-260 (owner, 2026-11-08: "zip나 7z 같은 압축이 우리가 가능한 압축파일을 루브라뷰 탐색기에서 열고 그 안의 폴더를 보여주고 있을
   때, 클립보드 붙이기나 드래그 앤 드롭으로 외부 파일이 들어가면 파일 추가 가능하게 해 주세요."; asked: a name already in the archive
   asked in the clashes window; a cut moves (its sources to the Recycle Bin once they are in), a copy and a drop copy; an
   archive that cannot be written as it is asks to become a ZIP or a 7z; D-312).

   Files and folders pasted or dropped while the open-file screen shows a folder inside an archive go into that folder:
   every name checked first against the archive; what clashes with nothing is written at once (one writing of the
   archive, rubraview_arcedit_add_files, on a thread); the names already there wait in the clashes window, and their
   answers are written in a second writing. Each writing puts the new archive in the old one's place and keeps the old
   one as name.rvold for Ctrl+Z (D-89), and the screen lists the folder again. */
#include "app.h"
#include "rubraview/arcedit.h"

enum { AA_PENDING = 0, AA_PARKED, AA_QUEUED, AA_DONE, AA_SKIPPED, AA_SAME, AA_FAILED };

typedef struct aa_item {
    char    *path;       /* on the disk */
    char    *name;       /* its name in the archive, as it will be written */
    uint64_t size;
    int64_t  mtime;
    size_t   existing;   /* the entry with that name, or RUBRAVIEW_ARCHIVE_NO_ITEM */
    uint64_t esize;
    int64_t  emtime;
    int      state;
    rubraview_fileop_answer_t answer;
    size_t   top;        /* which of the paths given it came from */
} aa_item_t;

typedef struct arcadd {
    char     archive[RUBRAVIEW_ARCHIVE_PATH];
    char     out[RUBRAVIEW_ARCHIVE_PATH];
    char     picker_dir[RUBRAVIEW_ARCHIVE_PATH];
    rubraview_codepage_t codepage;
    rubraview_arcedit_target_t target;
    bool     convert, moving;
    aa_item_t *items;
    size_t   count, cap;
    char   **tops;
    size_t   ntops;
    char   **names_taken;  /* the archive's file names, for a free "(2)" */
    size_t   ntaken;
    /* the writing under way */
    HANDLE   thread;
    volatile LONG done;
    int      result;
    rubraview_arcedit_job_t job;
    bool     same_recycles_copy;
    size_t   added, replaced, same, failed, rounds;
} arcadd_t;

static arcadd_t *g_aa;
static uint32_t  g_aa_version;
static bool      g_aa_asking_format;   /* the card: ZIP, 7z or leave it */
static rubraview_pal_rect_t g_aa_btn[3];

uint32_t arcadd_version(void) { return g_aa_version; }
bool arcadd_busy(void) { return g_aa != NULL; }

static bool name_same(const char *a, const char *b) {
    for (;; ++a, ++b) {
        char x = (char)rubraview_ascii_tolower((unsigned char)(*a == '\\' ? '/' : *a));
        char y = (char)rubraview_ascii_tolower((unsigned char)(*b == '\\' ? '/' : *b));
        if (x != y) return false;
        if (!x) return true;
    }
}

static void aa_free(arcadd_t *a) {
    if (!a) return;
    for (size_t i = 0; i < a->count; ++i) { rubraview_free(a->items[i].path); rubraview_free(a->items[i].name); }
    for (size_t i = 0; i < a->ntops; ++i) rubraview_free(a->tops[i]);
    for (size_t i = 0; i < a->ntaken; ++i) rubraview_free(a->names_taken[i]);
    rubraview_free(a->items);
    rubraview_free(a->tops);
    rubraview_free(a->names_taken);
    rubraview_arcedit_job_free(&a->job);
    rubraview_free(a);
}

static bool push_item(arcadd_t *a, const char *path, const char *name, uint64_t size, int64_t mtime, size_t top) {
    if (a->count == a->cap) {
        size_t cap = a->cap ? a->cap * 2 : 32;
        aa_item_t *grown = (aa_item_t *)rubraview_alloc_zero(cap, sizeof *grown);
        if (!grown) return false;
        if (a->count) proven_sys_mem_copy(grown, a->items, a->count * sizeof *grown);
        rubraview_free(a->items);
        a->items = grown;
        a->cap = cap;
    }
    aa_item_t *it = &a->items[a->count];
    *it = (aa_item_t){ .path = rubraview_strdup(path), .name = rubraview_strdup(name), .size = size, .mtime = mtime,
                       .existing = RUBRAVIEW_ARCHIVE_NO_ITEM, .top = top };
    if (!it->path || !it->name) { rubraview_free(it->path); rubraview_free(it->name); return false; }
    a->count++;
    return true;
}

/* A file, or a folder's files with their places under it, as items. */
static bool gather(app_state_t *app, arcadd_t *a, proven_u8str_view_t path, const char *prefix, size_t top, int depth) {
    rubraview_fs_entry_t st;
    if (depth > 32 || !rubraview_pal_fs_stat(app->arena, path, &st)) return false;
    char name[2048];
    proven_u8str_view_t base = rubraview_path_basename(path);
    int w = prefix[0] ? rubraview_fmt(name, sizeof name, "{}/{}", PROVEN_ARG(prefix), PROVEN_ARG(base))
                      : rubraview_fmt(name, sizeof name, "{}", PROVEN_ARG(base));
    if (w <= 0 || (size_t)w >= sizeof name) return false;
    if (!st.is_directory) {
        char p[RUBRAVIEW_ARCHIVE_PATH];
        if (path.size >= sizeof p) return false;
        proven_sys_mem_copy(p, path.ptr, path.size);
        p[path.size] = '\0';
        return push_item(a, p, name, st.size_bytes, st.mtime, top);
    }
    rubraview_fs_listing_t list = rubraview_pal_fs_list_dir(app->arena, path);
    bool ok = true;
    for (size_t i = 0; i < list.count && ok; ++i) ok = gather(app, a, list.entries[i].path, name, top, depth + 1);
    return ok;
}

static bool taken_in_archive(const arcadd_t *a, const char *name, size_t skip_item) {
    for (size_t i = 0; i < a->ntaken; ++i) if (name_same(a->names_taken[i], name)) return true;
    for (size_t i = 0; i < a->count; ++i)
        if (i != skip_item && a->items[i].state != AA_SKIPPED && a->items[i].state != AA_SAME && a->items[i].state != AA_FAILED &&
            a->items[i].answer != RUBRAVIEW_FILEOP_ANSWER_SKIP && name_same(a->items[i].name, name)) return true;
    return false;
}

/* A free "name (2).ext" in the archive for item `i`. */
static bool keep_beside(arcadd_t *a, size_t i) {
    char buf[2048];
    for (unsigned n = 2; n < 100000; ++n) {
        if (!rubraview_fileop_suffix_name(a->items[i].name, false, n, buf, sizeof buf)) return false;
        if (taken_in_archive(a, buf, i)) continue;
        char *named = rubraview_strdup(buf);
        if (!named) return false;
        rubraview_free(a->items[i].name);
        a->items[i].name = named;
        a->items[i].existing = RUBRAVIEW_ARCHIVE_NO_ITEM;
        return true;
    }
    return false;
}

/* The writing's thread: same-only answers decided by unpacking, then one writing of every queued item. */
static DWORD WINAPI aa_main(void *arg) {
    arcadd_t *a = (arcadd_t *)arg;
    size_t n = 0, ngone = 0;
    rubraview_arcedit_add_t *adds = (rubraview_arcedit_add_t *)rubraview_alloc_zero(a->count + 1, sizeof *adds);
    size_t *gone = (size_t *)rubraview_alloc_zero(a->count + 1, sizeof *gone);
    if (!adds || !gone) { a->result = RUBRAVIEW_ARCEDIT_NO_MEMORY; goto end; }
    for (size_t i = 0; i < a->count; ++i) {
        aa_item_t *it = &a->items[i];
        if (it->state != AA_QUEUED) continue;
        if (it->answer == RUBRAVIEW_FILEOP_ANSWER_SAME_ONLY) {
            int same = it->existing != RUBRAVIEW_ARCHIVE_NO_ITEM ? rubraview_arcedit_entry_same(a->archive, a->codepage, it->existing, it->path) : 0;
            if (same == 1) { it->state = AA_SAME; continue; }
            if (!keep_beside(a, i)) { it->state = AA_FAILED; continue; }
        }
        if (it->answer == RUBRAVIEW_FILEOP_ANSWER_OVERWRITE && it->existing != RUBRAVIEW_ARCHIVE_NO_ITEM) gone[ngone++] = it->existing;
        adds[n++] = (rubraview_arcedit_add_t){ .path = it->path, .name = it->name };
    }
    if (n == 0) { a->result = RUBRAVIEW_ARCEDIT_OK; a->out[0] = '\0'; goto end; }
    a->job = (rubraview_arcedit_job_t){ .archive_path = a->archive, .codepage = a->codepage, .entries = ngone ? gone : NULL,
                                        .entry_count = ngone, .target = a->target, .out_path = a->out, .level = 6 };
    a->result = (int)rubraview_arcedit_add_files(&a->job, adds, n);
end:
    rubraview_free(adds);
    rubraview_free(gone);
    InterlockedExchange(&a->done, 1);
    return 0;
}

static void aa_start_round(app_state_t *app) {
    arcadd_t *a = g_aa;
    bool any = false;
    for (size_t i = 0; i < a->count; ++i) if (a->items[i].state == AA_PENDING) { a->items[i].state = AA_QUEUED; any = true; }
    if (!any) return;
    rubraview_fmt(a->out, sizeof a->out, "{}.rvnew", PROVEN_ARG(cstr(a->archive)));
    a->done = 0;
    a->rounds++;
    a->thread = CreateThread(NULL, 0, aa_main, a, 0, NULL);
    if (!a->thread) {
        for (size_t i = 0; i < a->count; ++i) if (a->items[i].state == AA_QUEUED) a->items[i].state = AA_FAILED;
        osd_say(app, TU8("the archive could not be written"));
        return;
    }
    osd_say(app, TU8("putting them into the archive..."));
}

/* The writing done: the new archive in the old one's place (the old one kept for Ctrl+Z), the screen listed again. */
static void aa_round_ended(app_state_t *app) {
    arcadd_t *a = g_aa;
    WaitForSingleObject(a->thread, INFINITE);
    CloseHandle(a->thread);
    a->thread = NULL;
    bool ok = a->result == RUBRAVIEW_ARCEDIT_OK;
    if (ok && a->out[0]) {
        proven_u8str_view_t archive = cstr(a->archive);
        bool book = app->source.archive_path.size > 0 &&
                    (rubraview_path_same(app->source.archive_path, archive) || rubraview_path_same(rubraview_page_source_first_volume(&app->source), archive));
        size_t page = book && current_page_index(app) >= 0 ? (size_t)current_page_index(app) : 0;
        proven_u8str_view_t opened = book ? app_keep(app, app->source.archive_path) : PROVEN_LIT("");
        if (book) { history_remember(app); unload_all_pages(app); source_close(app); }
        proven_u8str_view_t final = archive;
        if (a->convert) {   /* the new ZIP or 7z beside it under its own name; the old one's files to the Recycle Bin */
            char name[1024];
            size_t k = rubraview_arcedit_converted_name(rubraview_path_basename(archive), a->target, name, sizeof name);
            char want[RUBRAVIEW_ARCHIVE_PATH], free_name[RUBRAVIEW_ARCHIVE_PATH];
            proven_u8str_view_t dir = rubraview_path_dirname(archive);
            int w = k ? rubraview_fmt(want, sizeof want, "{}/{}", PROVEN_ARG(dir), PROVEN_ARG(rubraview_bytes_view(name, (int)k))) : -1;
            ok = w > 0 && rubraview_archive_free_name(cstr(want), rubraview_pal_fs_exists, free_name, sizeof free_name) &&
                 rubraview_pal_fs_move(cstr(a->out), cstr(free_name));
            if (ok) {
                for (size_t v = 0; v < a->job.volume_count; ++v) (void)rubraview_pal_fs_recycle(cstr(a->job.volumes[v]));
                rubraview_fmt(a->archive, sizeof a->archive, "{}", PROVEN_ARG(cstr(free_name)));
                a->convert = false;   /* a second round writes the new one as it is */
                a->target = RUBRAVIEW_ARCEDIT_SAME;
                final = app_keep(app, cstr(a->archive));
                arc_undo_drop(app);
            }
        } else {
            char backup[sizeof app->arc_undo_backup];
            bool kept = rubraview_fmt(backup, sizeof backup, "{}.rvold", PROVEN_ARG(archive)) < (int)sizeof backup &&
                        rubraview_pal_fs_replace(archive, cstr(backup));
            ok = kept && rubraview_pal_fs_replace(cstr(a->out), archive);
            if (!ok && kept) (void)rubraview_pal_fs_replace(cstr(backup), archive);
            if (ok) {
                rubraview_fmt(app->arc_undo_backup, sizeof app->arc_undo_backup, "{}", PROVEN_ARG(backup));
                rubraview_fmt(app->arc_undo_path, sizeof app->arc_undo_path, "{}", PROVEN_ARG(archive));
            }
        }
        if (!ok) (void)rubraview_pal_fs_delete(cstr(a->out));
        if (book) arc_reopen_at(app, ok ? final : opened, page);
        if (app->picker_open && picker_enter_archive(app, final) && !a->convert)
            picker_navigate(app, app_keep(app, cstr(a->picker_dir)));   /* the folder inside it, listed again */
    }
    for (size_t i = 0; i < a->count; ++i) {
        aa_item_t *it = &a->items[i];
        if (it->state == AA_QUEUED) {
            it->state = ok ? AA_DONE : AA_FAILED;
            if (ok) { a->added++; if (it->answer == RUBRAVIEW_FILEOP_ANSWER_OVERWRITE) a->replaced++; }
            else a->failed++;
        }
        if (it->state == AA_SAME) { it->state = AA_SKIPPED; it->answer = RUBRAVIEW_FILEOP_ANSWER_SAME_ONLY; a->same++; }
    }
    if (!ok) {
        char line[300];
        int w = rubraview_fmt(line, sizeof line, T("not put into the archive: {}"), PROVEN_ARG(rubraview_arcedit_result_text((rubraview_arcedit_result_t)a->result)));
        if (w > 0) osd_say(app, cstr(line));
        app_log(app, "archive", cstr(line));
    }
    g_aa_version++;
}

/* All answered and written: a cut's sources that went in whole go to the Recycle Bin; the words; the end. */
static void aa_finish(app_state_t *app) {
    arcadd_t *a = g_aa;
    size_t recycled = 0;
    for (size_t t = 0; t < a->ntops; ++t) {
        bool whole = true, any = false;
        for (size_t i = 0; i < a->count; ++i) {
            if (a->items[i].top != t) continue;
            any = true;
            bool same = a->items[i].state == AA_SKIPPED && a->items[i].answer == RUBRAVIEW_FILEOP_ANSWER_SAME_ONLY;
            if (!(a->items[i].state == AA_DONE || same)) whole = false;
        }
        if (a->moving) {   /* a cut: what went in leaves the disk — the whole folder when all of it went in */
            if (any && whole) { if (rubraview_pal_fs_recycle(cstr(a->tops[t]))) recycled++; continue; }
            for (size_t i = 0; i < a->count; ++i) {
                bool same = a->items[i].state == AA_SKIPPED && a->items[i].answer == RUBRAVIEW_FILEOP_ANSWER_SAME_ONLY;
                if (a->items[i].top == t && (a->items[i].state == AA_DONE || same) && rubraview_pal_fs_recycle(cstr(a->items[i].path))) recycled++;
            }
        } else if (a->same_recycles_copy) {   /* a copy: only the same ones, as the clashes window says */
            for (size_t i = 0; i < a->count; ++i)
                if (a->items[i].top == t && a->items[i].state == AA_SKIPPED && a->items[i].answer == RUBRAVIEW_FILEOP_ANSWER_SAME_ONLY &&
                    rubraview_pal_fs_recycle(cstr(a->items[i].path))) recycled++;
        }
    }
    if (a->moving && recycled > 0) rubraview_pal_clipboard_clear(app->window);
    char line[260];
    int w = rubraview_fmt(line, sizeof line, T("into the archive: {} put in, {} replaced, {} the same, {} could not be  (Ctrl+Z takes the last writing back)"),
                          PROVEN_ARG(a->added), PROVEN_ARG(a->replaced), PROVEN_ARG(a->same), PROVEN_ARG(a->failed));
    if (w > 0) { osd_say(app, cstr(line)); app_log(app, "archive", cstr(line)); }
    aa_free(a);
    g_aa = NULL;
    g_aa_version++;
}

static void aa_begin(app_state_t *app, rubraview_arcedit_target_t target) {
    g_aa->target = target;
    g_aa->convert = target != RUBRAVIEW_ARCEDIT_SAME;
    arc_undo_drop(app);
    aa_start_round(app);
    g_aa_version++;
}

static bool writable_as_is(const char *archive) {
    return rubraview_glob_match_list(rubraview_path_basename(cstr(archive)), PROVEN_LIT("*.zip;*.cbz;*.7z;*.cb7"));
}

/* RV-260: paths pasted or dropped while the open-file screen shows a folder inside an archive. */
void arcadd_paths(app_state_t *app, const proven_u8str_view_t *paths, size_t n, bool moving) {
    if (n == 0 || !picker_in_archive(app)) return;
    if (g_aa || app->arc_job) { osd_say(app, TU8("the archive is still being written: try again when it is done")); return; }
    arcadd_t *a = (arcadd_t *)rubraview_alloc_zero(1, sizeof *a);
    if (!a) return;
    rubraview_fmt(a->archive, sizeof a->archive, "{}", PROVEN_ARG(cstr(app->picker_arc_path)));
    rubraview_fmt(a->picker_dir, sizeof a->picker_dir, "{}", PROVEN_ARG(app->picker_dir));
    a->codepage = app->archive_codepage;
    a->moving = moving;
    a->same_recycles_copy = filejob_same_recycles_copy();
    char inner[RUBRAVIEW_ARCHIVE_PATH] = "";
    (void)picker_archive_rel(app, app->picker_dir, inner, sizeof inner);
    a->tops = (char **)rubraview_alloc_zero(n, sizeof *a->tops);
    bool ok = a->tops != NULL;
    for (size_t i = 0; i < n && ok; ++i) {
        a->tops[i] = rubraview_strndup_view(paths[i]);
        if (!a->tops[i]) { ok = false; break; }
        a->ntops++;
        /* the archive itself, or a folder holding it, never goes into it */
        if (rubraview_path_same(paths[i], cstr(a->archive)) || rubraview_paste_check(cstr(a->archive), paths[i], false) == RUBRAVIEW_PASTE_INTO_ITSELF) continue;
        ok = gather(app, a, paths[i], inner, i, 0);
    }
    rubraview_archive_index_t ix = { 0 };
    if (ok && picker_index_read(app, a->archive, a->codepage, &ix) != RUBRAVIEW_ARCHIVE_DONE) ok = false;
    if (ok) {
        a->names_taken = (char **)rubraview_alloc_zero(ix.count + 1, sizeof *a->names_taken);
        ok = a->names_taken != NULL;
        for (size_t e = 0; ok && e < ix.count; ++e) {
            if (ix.items[e].is_dir || !ix.items[e].name) continue;
            a->names_taken[a->ntaken++] = rubraview_strdup(ix.items[e].name);
            for (size_t i = 0; i < a->count; ++i)
                if (a->items[i].existing == RUBRAVIEW_ARCHIVE_NO_ITEM && name_same(a->items[i].name, ix.items[e].name)) {
                    a->items[i].existing = e;
                    a->items[i].esize = ix.items[e].size;
                    a->items[i].emtime = ix.items[e].mtime;
                    a->items[i].state = AA_PARKED;
                }
        }
    }
    rubraview_archive_index_free(&ix);
    if (!ok || a->count == 0) { aa_free(a); osd_say(app, ok ? TU8("nothing to put into the archive") : TU8("the files or the archive could not be read")); return; }
    g_aa = a;
    char line[200];
    int w = rubraview_fmt(line, sizeof line, T("{} file(s) to put into {}"), PROVEN_ARG(a->count), PROVEN_ARG(rubraview_path_basename(cstr(a->archive))));
    if (w > 0) app_log(app, "archive", cstr(line));
    if (!writable_as_is(a->archive)) { g_aa_asking_format = true; return; }   /* RAR, ALZ, EGG ...: asked */
    aa_begin(app, RUBRAVIEW_ARCEDIT_SAME);
}

/* ---- the clashes window's rows and answers ---- */

size_t arcadd_rows(arcadd_row_t *out, size_t cap) {
    size_t k = 0;
    for (size_t i = 0; g_aa && i < g_aa->count; ++i) {
        const aa_item_t *it = &g_aa->items[i];
        if (it->state != AA_PARKED) continue;
        if (out && k < cap) out[k] = (arcadd_row_t){ .index = i, .source = it->path, .archive = g_aa->archive, .name = it->name,
                                                     .ssize = it->size, .smtime = it->mtime, .tsize = it->esize, .tmtime = it->emtime };
        k++;
    }
    return k;
}

void arcadd_answer(app_state_t *app, size_t index, rubraview_fileop_answer_t answer) {
    (void)app;
    if (!g_aa || index >= g_aa->count || g_aa->items[index].state != AA_PARKED) return;
    aa_item_t *it = &g_aa->items[index];
    it->answer = answer;
    if (answer == RUBRAVIEW_FILEOP_ANSWER_SKIP) it->state = AA_SKIPPED;
    else if (answer == RUBRAVIEW_FILEOP_ANSWER_KEEP_BOTH) it->state = keep_beside(g_aa, index) ? AA_PENDING : AA_FAILED;
    else it->state = AA_PENDING;   /* overwrite, or only the same ones: decided as it is written */
    g_aa_version++;
}

void arcadd_answer_all(app_state_t *app, rubraview_fileop_answer_t answer) {
    for (size_t i = 0; g_aa && i < g_aa->count; ++i) if (g_aa->items[i].state == AA_PARKED) arcadd_answer(app, i, answer);
}

/* Once a pass: a writing that ended handled; the answered ones written next; the end when nothing is left. */
void arcadd_poll(app_state_t *app) {
    if (!g_aa || g_aa_asking_format) return;
    if (g_aa->thread) {
        if (!InterlockedCompareExchange(&g_aa->done, 0, 0)) return;
        aa_round_ended(app);
    }
    bool pending = false, parked = false;
    for (size_t i = 0; i < g_aa->count; ++i) {
        if (g_aa->items[i].state == AA_PENDING) pending = true;
        if (g_aa->items[i].state == AA_PARKED) parked = true;
    }
    if (pending) { aa_start_round(app); return; }
    if (!parked) aa_finish(app);
}

void arcadd_quit(void) {
    if (!g_aa) return;
    if (g_aa->thread) {
        atomic_store(&g_aa->job.cancel, true);
        WaitForSingleObject(g_aa->thread, 10000);
        CloseHandle(g_aa->thread);
        (void)rubraview_pal_fs_delete(cstr(g_aa->out));
    }
    aa_free(g_aa);
    g_aa = NULL;
}

/* ---- the card: an archive that cannot be written as it is ---- */

bool arcadd_asking(void) { return g_aa_asking_format; }

void arcadd_format_answer(app_state_t *app, int which) {   /* 0 ZIP, 1 7z, 2 leave it */
    if (!g_aa_asking_format) return;
    g_aa_asking_format = false;
    if (which == 2 || !g_aa) { aa_free(g_aa); g_aa = NULL; osd_say(app, TU8("nothing put into the archive")); g_aa_version++; return; }
    aa_begin(app, which == 0 ? RUBRAVIEW_ARCEDIT_ZIP : RUBRAVIEW_ARCEDIT_7Z);
}

bool arcadd_press(app_state_t *app, double x, double y) {
    if (!g_aa_asking_format) return false;
    for (int i = 0; i < 3; ++i) if (pal_rect_has(g_aa_btn[i], x, y)) { arcadd_format_answer(app, i); return true; }
    return true;
}

void draw_arcadd_card(app_state_t *app, double win_w, double win_h, double dpi) {
    if (!g_aa_asking_format || !g_aa) return;
    double cw = fmin(640.0 * dpi, win_w - 16.0 * dpi), ch = 190.0 * dpi, pad = 20.0 * dpi;
    rubraview_pal_rect_t card = { (win_w - cw) * 0.5, (win_h - ch) * 0.45, cw, ch };
    rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ 0.0, 0.0, win_w, win_h }, 0x90000000u, 0.0);
    rubraview_pal_render_fill_rect(app->renderer, card, 0xFA242424u, 8.0 * dpi);
    rubraview_pal_render_stroke_rect(app->renderer, card, COLOR_BOX_BORDER, 1.0 * dpi, 8.0 * dpi);
    rubraview_pal_render_draw_text(app->renderer, TU8("This archive cannot be written as it is"),
                                   (rubraview_pal_rect_t){ card.x + pad, card.y + pad - 4.0 * dpi, cw - 2 * pad, 28.0 * dpi }, 18.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    rubraview_pal_render_draw_text(app->renderer, TU8("The files go into a new ZIP or 7z of its name, with everything it holds; the old one goes to the Recycle Bin."),
                                   (rubraview_pal_rect_t){ card.x + pad, card.y + pad + 30.0 * dpi, cw - 2 * pad, 40.0 * dpi }, 13.0 * dpi, 0xC0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
    const char *words[3] = { T("Make it a ZIP"), T("Make it a 7z"), T("Leave it  (Esc)") };
    double bw = (cw - 2 * pad - 2 * 10.0 * dpi) / 3.0, bh = 40.0 * dpi, by = card.y + ch - pad - bh;
    for (int i = 0; i < 3; ++i) {
        rubraview_pal_rect_t b = { card.x + pad + (double)i * (bw + 10.0 * dpi), by, bw, bh };
        g_aa_btn[i] = b;
        bool over = pal_rect_has(b, app->pointer_x, app->pointer_y);
        rubraview_pal_render_fill_rect(app->renderer, b, i == 0 ? COLOR_TILE_CURRENT : over ? 0x50FFFFFFu : COLOR_TILE_FILL, 5.0 * dpi);
        rubraview_pal_render_stroke_rect(app->renderer, b, COLOR_BOX_BORDER, 1.0, 5.0 * dpi);
        rubraview_pal_render_draw_text(app->renderer, cstr(words[i]), b, 14.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
}
