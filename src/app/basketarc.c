/* RV-320 (D-402; owner, 2026-11-26: "장바구니에 넣을 때도 마찬가지로", Move here "(나)로 하되 경고와 확인을 거칩시다",
   picks of several folders flat "예"): a basket's archive entries put into the folder on screen. Each group — one
   archive, one folder in it — is an extraction straight into the folder (archwork_extract_into: names already there
   asked, RV-258), one after another. A move, once asked and confirmed (popups.c), takes each archive's entries that
   arrived — their group done with nothing failed, skipped or asked — out of it (dragout_take_out: written again, the
   old one kept for Ctrl+Z), and only then out of the basket; the rest stay in both. */
#include "app.h"
#include "rubraview/basket.h"

typedef struct ba_group {
    char *archive, *inner;
    char **names;          /* the entries' names inside the archive */
    size_t n;
    bool arrived;          /* written out whole: nothing failed, skipped or asked */
} ba_group_t;

enum { BA_NEXT, BA_EXTRACTING, BA_CUT_DUE, BA_CUTTING };

static struct {
    bool on, moving;
    int which, phase;
    char dest[RUBRAVIEW_ARCHIVE_PATH];
    ba_group_t *g;
    size_t ng, at;
    size_t cut_from;       /* the first group of the archive whose cut is due */
    size_t moved, stayed;  /* entries taken out of their archives, entries left in them (a move) */
} B;

static void ba_free(void) {
    for (size_t i = 0; i < B.ng; ++i) {
        rubraview_free(B.g[i].archive);
        rubraview_free(B.g[i].inner);
        for (size_t k = 0; k < B.g[i].n; ++k) rubraview_free(B.g[i].names[k]);
        rubraview_free(B.g[i].names);
    }
    rubraview_free(B.g);
    B = (typeof(B)){ 0 };
}

bool basketarc_busy(void) { return B.on; }

static char *dup_view(proven_u8str_view_t v) {
    char *s = rubraview_alloc(v.size + 1);
    if (!s) return NULL;
    if (v.size) proven_sys_mem_copy(s, v.ptr, v.size);
    s[v.size] = '\0';
    return s;
}

/* The groups of basket `which` copied (the basket may change while they run). False when it holds no archive entry. */
bool basketarc_start(app_state_t *app, int which, proven_u8str_view_t dir, bool moving) {
    if (B.on) { osd_say(app, TU8("a basket is already being put out of its archives")); return false; }
    size_t count = rubraview_basket_count(&app->baskets, which);
    if (count == 0 || dir.size == 0 || dir.size >= sizeof B.dest) return false;
    rubraview_basket_group_t *groups = rubraview_alloc(count * sizeof *groups);
    size_t *members = rubraview_alloc(count * sizeof *members);
    size_t ng = groups && members ? rubraview_basket_groups(&app->baskets, which, groups, count, members) : 0;
    if (ng == 0) { rubraview_free(groups); rubraview_free(members); return false; }
    B.g = rubraview_alloc_zero(ng, sizeof *B.g);
    bool ok = B.g != NULL;
    for (size_t i = 0; ok && i < ng; ++i) {
        ba_group_t *g = &B.g[i];
        B.ng = i + 1;
        g->archive = dup_view(groups[i].archive);
        g->inner = dup_view(groups[i].inner);
        g->names = rubraview_alloc_zero(groups[i].count, sizeof *g->names);
        ok = g->archive && g->inner && g->names;
        for (size_t k = 0; ok && k < groups[i].count; ++k) {
            ok = (g->names[k] = dup_view(rubraview_basket_entry(&app->baskets, which, members[groups[i].first + k]))) != NULL;
            if (ok) g->n = k + 1;
        }
    }
    rubraview_free(groups);
    rubraview_free(members);
    if (!ok) { ba_free(); return false; }
    proven_sys_mem_copy(B.dest, dir.ptr, dir.size);
    B.dest[dir.size] = '\0';
    B.on = true;
    B.moving = moving;
    B.which = which;
    B.phase = BA_NEXT;
    return true;
}

/* The group's extraction ended. */
static void group_told(app_state_t *app, const archwork_told_t *how, void *ctx) {
    (void)app; (void)ctx;
    if (!B.on || B.phase != BA_EXTRACTING || B.at >= B.ng) return;
    ba_group_t *g = &B.g[B.at];
    g->arrived = how->result == RUBRAVIEW_ARCHIVE_DONE && how->failed == 0 && how->skipped == 0 && how->asked == 0;
    B.at++;
    bool last_of_archive = B.at == B.ng || !rubraview_path_same(cstr(B.g[B.at].archive), cstr(g->archive));
    B.phase = B.moving && last_of_archive ? BA_CUT_DUE : BA_NEXT;
}

/* The archive's cut ended: what went out of it goes out of the basket too. */
static void cut_told(app_state_t *app, bool ok, void *ctx) {
    (void)ctx;
    if (!B.on || B.phase != BA_CUTTING) return;
    size_t count = rubraview_basket_count(&app->baskets, B.which);
    bool *moved = count ? rubraview_alloc_zero(count, sizeof *moved) : NULL;
    for (size_t gi = B.cut_from; gi < B.at; ++gi) {
        const ba_group_t *g = &B.g[gi];
        if (!g->arrived || !ok) { B.stayed += g->n; continue; }
        B.moved += g->n;
        for (size_t i = 0; moved && i < count; ++i) {
            proven_u8str_view_t entry = rubraview_basket_entry(&app->baskets, B.which, i);
            if (entry.size == 0 || !rubraview_path_same(rubraview_basket_path(&app->baskets, B.which, i), cstr(g->archive))) continue;
            for (size_t k = 0; k < g->n && !moved[i]; ++k) moved[i] = proven_u8str_view_eq(entry, cstr(g->names[k]));
        }
    }
    if (moved) (void)rubraview_basket_keep_unmoved(&app->baskets, B.which, moved);
    rubraview_free(moved);
    B.phase = BA_NEXT;
}

static void start_cut(app_state_t *app) {
    const char *archive = B.g[B.at - 1].archive;
    B.cut_from = B.at - 1;
    while (B.cut_from > 0 && rubraview_path_same(cstr(B.g[B.cut_from - 1].archive), cstr(archive))) B.cut_from--;
    size_t n = 0;
    for (size_t gi = B.cut_from; gi < B.at; ++gi) if (B.g[gi].arrived) n += B.g[gi].n;
    proven_u8str_view_t *names = n ? rubraview_alloc(n * sizeof *names) : NULL;
    size_t k = 0;
    for (size_t gi = B.cut_from; names && gi < B.at; ++gi)
        for (size_t j = 0; B.g[gi].arrived && j < B.g[gi].n; ++j) names[k++] = cstr(B.g[gi].names[j]);
    B.phase = BA_CUTTING;
    bool started = k > 0 && dragout_take_out(app, archive, names, k, cut_told, NULL);
    rubraview_free(names);
    if (!started) {   /* nothing arrived whole, or the archive could not be written again: all stay */
        for (size_t gi = B.cut_from; gi < B.at; ++gi) B.stayed += B.g[gi].n;
        B.phase = BA_NEXT;
    }
}

static void start_group(app_state_t *app) {
    ba_group_t *g = &B.g[B.at];
    rubraview_archive_index_t ix = { 0 };
    size_t *only = NULL, k = 0;
    if (picker_index_read(app, g->archive, app->archive_codepage, &ix) == RUBRAVIEW_ARCHIVE_DONE && ix.count > 0 &&
        (only = rubraview_alloc(ix.count * sizeof *only)) != NULL) {
        for (size_t i = 0; i < g->n; ++i) k += rubraview_archive_index_under(&ix, cstr(g->names[i]), only + k, ix.count - k);
    }
    rubraview_archive_index_free(&ix);
    B.phase = BA_EXTRACTING;
    if (k == 0 || !archwork_extract_into(app, g->archive, g->inner, only, k, B.dest, group_told, NULL)) {
        proven_u8str_view_t name = rubraview_path_basename(cstr(g->archive));
        char line[400];
        rubraview_fmt(line, sizeof line, T("{}: its entries could not be read (is it there, or locked?)"),
                      PROVEN_ARG(rubraview_bytes_view((const char *)name.ptr, (int)name.size)));
        osd_say(app, cstr(line));
        archwork_told_t none = { .result = RUBRAVIEW_ARCHIVE_CANNOT_READ };
        group_told(app, &none, NULL);
    }
    rubraview_free(only);
}

/* Once a pass: the next step when nothing else is writing. */
void basketarc_poll(app_state_t *app, bool quitting) {
    if (!B.on) return;
    if (quitting) { ba_free(); return; }
    if (B.phase == BA_EXTRACTING || B.phase == BA_CUTTING) return;   /* told when they end */
    if (app->archwork || dragout_cut_busy(app)) return;
    if (B.phase == BA_CUT_DUE) { start_cut(app); return; }
    if (B.at < B.ng) { start_group(app); return; }
    if (B.moving) {
        char line[260];
        rubraview_fmt(line, sizeof line, T("basket {}: {} taken out of their archives, {} left in them"), PROVEN_ARG(B.which + 1),
                      PROVEN_ARG(B.moved), PROVEN_ARG(B.stayed));
        osd_say(app, cstr(line));
        app_log(app, "basket", cstr(line));
    }
    if (app->picker_open) picker_navigate(app, app->picker_dir);
    ba_free();
}
