/* RV-258 (D-310): a copy or a move into a folder as items — every name checked first, the taken ones parked and
   asked, the rest carried out; the rules as rubraman's (../rubraman, spec-04), the suffix as the owner's (_2). */
#include "rubraview/fileop.h"
#include "rubraview/path.h"

static char *dup_cstr(const char *s) { return rubraview_strdup(s ? s : ""); }

static bool is_sep(char c) { return c == '/' || c == '\\'; }

/* `dir` and `name` joined with the separator `dir` uses. */
static char *join(const char *dir, const char *name) {
    size_t a = proven_cstr_len(dir), b = proven_cstr_len(name);
    char sep = '/';
    for (size_t i = 0; i < a; ++i) if (dir[i] == '\\') { sep = '\\'; break; }
    bool has = a > 0 && is_sep(dir[a - 1]);
    char *out = (char *)rubraview_alloc(a + b + 2);
    if (!out) return NULL;
    proven_sys_mem_copy(out, dir, a);
    size_t at = a;
    if (!has && a > 0) out[at++] = sep;
    proven_sys_mem_copy(out + at, name, b);
    out[at + b] = '\0';
    return out;
}

/* The last part of `path`, trailing separators left out, as a view into it. */
static proven_u8str_view_t base_of(const char *path) {
    size_t n = proven_cstr_len(path);
    while (n > 1 && is_sep(path[n - 1])) --n;
    size_t start = n;
    while (start > 0 && !is_sep(path[start - 1])) --start;
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)path + start, .size = n - start };
}

static proven_u8str_view_t dir_of(const char *path) {
    proven_u8str_view_t b = base_of(path);
    size_t n = (size_t)((const char *)b.ptr - path);
    while (n > 1 && is_sep(path[n - 1])) --n;
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)path, .size = n };
}

static bool view_same(proven_u8str_view_t a, proven_u8str_view_t b) {
    if (a.size != b.size) return false;
    for (size_t i = 0; i < a.size; ++i) {
        char x = (char)a.ptr[i], y = (char)b.ptr[i];
        if (is_sep(x) && is_sep(y)) continue;
        if (rubraview_ascii_tolower((unsigned char)x) != rubraview_ascii_tolower((unsigned char)y)) return false;
    }
    return true;
}

bool rubraview_fileop_suffix_name(const char *path, bool is_dir, unsigned n, char *out, size_t cap) {
    if (!path || !out || cap == 0) return false;
    size_t len = proven_cstr_len(path);
    proven_u8str_view_t b = base_of(path);
    size_t bstart = (size_t)((const char *)b.ptr - path), end = bstart + b.size, dot = end;
    if (!is_dir) for (size_t i = end; i > bstart + 1; --i) if (path[i - 1] == '.') { dot = i - 1; break; }   /* ".hidden" keeps no extension */
    /* RV-259 (owner, 2026-11-08: "겹칠때 이름을 윈도 스타일로 (2) (3) ... (10) (11) 이렇게 하는게 더 좋아보여요"): "name (2).ext", as Windows names one */
    int w = rubraview_fmt(out, cap, "{} ({}){}", PROVEN_ARG(rubraview_bytes_view(path, (int)dot)), PROVEN_ARG(n),
                          PROVEN_ARG(rubraview_bytes_view(path + dot, (int)(len - dot))));
    return w > 0 && (size_t)w < cap;
}

rubraview_fileop_answer_t rubraview_fileop_answer_of_checks(bool keep_coming, bool keep_there) {
    if (keep_coming && keep_there) return RUBRAVIEW_FILEOP_ANSWER_KEEP_BOTH;
    if (keep_coming) return RUBRAVIEW_FILEOP_ANSWER_OVERWRITE;
    return RUBRAVIEW_FILEOP_ANSWER_SKIP;
}

void rubraview_fileop_init(rubraview_fileop_t *op, rubraview_fileop_kind_t kind, const rubraview_fileop_fs_t *fs, const char *dir) {
    *op = (rubraview_fileop_t){ .kind = kind };
    if (fs) op->fs = *fs;
    op->dir = dup_cstr(dir);
}

void rubraview_fileop_free(rubraview_fileop_t *op) {
    if (!op) return;
    for (size_t i = 0; i < op->count; ++i) { rubraview_free(op->items[i].source); rubraview_free(op->items[i].target); }
    rubraview_free(op->items);
    rubraview_free(op->dir);
    *op = (rubraview_fileop_t){ 0 };
}

/* An item at the end; its index, or -1. The array may move: callers hold indexes, not pointers, across it. */
static ptrdiff_t push(rubraview_fileop_t *op, char *source, char *target, int32_t parent, bool merged_child) {
    if (!source || !target) { rubraview_free(source); rubraview_free(target); return -1; }
    if (op->count == op->capacity) {
        size_t cap = op->capacity ? op->capacity * 2 : 16;
        rubraview_fileop_item_t *grown = (rubraview_fileop_item_t *)rubraview_alloc_zero(cap, sizeof *grown);
        if (!grown) { rubraview_free(source); rubraview_free(target); return -1; }
        if (op->count) proven_sys_mem_copy(grown, op->items, op->count * sizeof *grown);
        rubraview_free(op->items);
        op->items = grown;
        op->capacity = cap;
    }
    op->items[op->count] = (rubraview_fileop_item_t){ .source = source, .target = target, .parent = parent, .merged_child = merged_child };
    return (ptrdiff_t)op->count++;
}

bool rubraview_fileop_add(rubraview_fileop_t *op, const char *source) {
    if (!op || !source || !*source) return false;
    proven_u8str_view_t b = base_of(source);
    char name[1024];
    if (b.size == 0 || b.size >= sizeof name) return false;
    proven_sys_mem_copy(name, b.ptr, b.size);
    name[b.size] = '\0';
    return push(op, dup_cstr(source), join(op->dir, name), -1, false) >= 0;
}

typedef struct { rubraview_fileop_t *op; size_t parent; bool merged; bool ok; } expand_ctx_t;

static void expand_one(void *user, const char *name) {
    expand_ctx_t *e = (expand_ctx_t *)user;
    rubraview_fileop_t *op = e->op;
    char *src = join(op->items[e->parent].source, name), *dst = join(op->items[e->parent].target, name);
    if (push(op, src, dst, (int32_t)e->parent, e->merged) < 0) e->ok = false;
}

/* A folder's entries as items of their own under it. */
static bool expand(rubraview_fileop_t *op, size_t i, bool merged) {
    if (!op->fs.list) return false;
    expand_ctx_t e = { op, i, merged, true };
    char *src = dup_cstr(op->items[i].source);   /* the array may move while it is listed */
    bool listed = src && op->fs.list(op->fs.ctx, src, expand_one, &e);
    rubraview_free(src);
    return listed && e.ok;
}

static bool same_identity(const rubraview_fileop_stat_t *a, const rubraview_fileop_stat_t *b) {
    return a->file_id != 0 && a->volume == b->volume && a->file_id == b->file_id;
}

static void fail(rubraview_fileop_item_t *it, const char *why) { it->state = RUBRAVIEW_FILEOP_FAILED; it->why = why; }
static void skip(rubraview_fileop_item_t *it, const char *why) { it->state = RUBRAVIEW_FILEOP_SKIPPED; it->why = why; }

static void recount(rubraview_fileop_t *op) {
    op->done = op->skipped = op->failed = op->parked = 0;
    for (size_t i = 0; i < op->count; ++i) {
        switch (op->items[i].state) {
            case RUBRAVIEW_FILEOP_DONE:    op->done++; break;
            case RUBRAVIEW_FILEOP_SKIPPED: op->skipped++; break;
            case RUBRAVIEW_FILEOP_FAILED:  op->failed++; break;
            case RUBRAVIEW_FILEOP_PARKED:  op->parked++; break;
            default: break;
        }
    }
}

/* One item's names checked: pending, parked, merged (its entries added), skipped or failed. */
static void prepare_one(rubraview_fileop_t *op, size_t i) {
    rubraview_fileop_item_t *it = &op->items[i];
    it->prepared = true;
    rubraview_fileop_stat_t s = { 0 }, t = { 0 };
    if (!op->fs.stat || !op->fs.stat(op->fs.ctx, it->source, &s) || !s.exists) { fail(it, "it is not there any more"); return; }
    it->is_dir = s.is_dir;
    it->size = s.size;
    if (s.is_link) { skip(it, "a link or a junction is not followed"); return; }
    if (it->parent < 0) {
        proven_u8str_view_t src = { .ptr = (const proven_byte_t *)it->source, .size = proven_cstr_len(it->source) };
        proven_u8str_view_t dir = { .ptr = (const proven_byte_t *)op->dir, .size = proven_cstr_len(op->dir) };
        while (src.size > 1 && is_sep((char)src.ptr[src.size - 1])) src.size--;
        while (dir.size > 1 && is_sep((char)dir.ptr[dir.size - 1])) dir.size--;
        if (s.is_dir && dir.size >= src.size && view_same((proven_u8str_view_t){ .ptr = dir.ptr, .size = src.size }, src) &&
            (dir.size == src.size || is_sep((char)dir.ptr[src.size]))) { skip(it, "a folder into itself"); return; }
        if (op->kind == RUBRAVIEW_FILEOP_MOVE && view_same(dir_of(it->source), dir)) { skip(it, "it is in this folder already"); return; }
    }
    bool there = op->fs.stat(op->fs.ctx, it->target, &t) && t.exists;
    if (!there) {
        bool moved_whole = op->kind == RUBRAVIEW_FILEOP_MOVE && op->fs.same_volume && op->fs.same_volume(op->fs.ctx, it->source, op->dir);
        if (s.is_dir && !moved_whole) {   /* made at the destination, its entries copied one by one */
            it->expanded = true;
            if (!expand(op, i, false)) fail(&op->items[i], "the folder could not be listed");
        }
        return;   /* pending */
    }
    proven_u8str_view_t sv = { .ptr = (const proven_byte_t *)it->source, .size = proven_cstr_len(it->source) };
    proven_u8str_view_t tv = { .ptr = (const proven_byte_t *)it->target, .size = proven_cstr_len(it->target) };
    if (same_identity(&s, &t) || view_same(sv, tv)) {
        if (op->kind == RUBRAVIEW_FILEOP_COPY) it->answer = RUBRAVIEW_FILEOP_ANSWER_KEEP_BOTH;   /* a copy onto itself: beside it */
        else skip(it, "it is there already");
        return;
    }
    if (s.is_dir && t.is_dir) {   /* merged: its entries go into the one there, asked about one by one */
        it->state = RUBRAVIEW_FILEOP_MERGED;
        if (!expand(op, i, true)) fail(&op->items[i], "the folder could not be listed");
        return;
    }
    it->kind_differs = s.is_dir != t.is_dir;
    it->state = RUBRAVIEW_FILEOP_PARKED;
}

size_t rubraview_fileop_prepare(rubraview_fileop_t *op) {
    if (!op) return 0;
    for (size_t i = 0; i < op->count; ++i)   /* the count grows as folders are expanded: their entries are checked too */
        if (!op->items[i].prepared) prepare_one(op, i);
    recount(op);
    return op->parked;
}

/* A staging name beside `target` in its folder. */
static char *staging_name(rubraview_fileop_t *op, const char *target) {
    char name[96];
    proven_u8str_view_t b = base_of(target);
    int w = rubraview_fmt(name, sizeof name, ".rubraview-tmp-{}-{}", PROVEN_ARG(++op->serial),
                          PROVEN_ARG(rubraview_bytes_view((const char *)b.ptr, (int)(b.size < 40 ? b.size : 40))));
    if (w <= 0) return NULL;
    proven_u8str_view_t d = dir_of(target);
    char *dir = (char *)rubraview_alloc(d.size + 1);
    if (!dir) return NULL;
    proven_sys_mem_copy(dir, d.ptr, d.size);
    dir[d.size] = '\0';
    char *out = join(dir, name);
    rubraview_free(dir);
    return out;
}

/* The first free name_2, _3 ... for the item's target. */
static bool keep_beside(rubraview_fileop_t *op, rubraview_fileop_item_t *it) {
    char buf[4096];
    for (unsigned n = 2; n < 100000; ++n) {
        if (!rubraview_fileop_suffix_name(it->target, it->is_dir, n, buf, sizeof buf)) return false;
        rubraview_fileop_stat_t t = { 0 };
        if (op->fs.stat(op->fs.ctx, buf, &t) && t.exists) continue;
        char *named = dup_cstr(buf);
        if (!named) return false;
        rubraview_free(it->target);
        it->target = named;
        return true;
    }
    return false;
}

static void gone(rubraview_fileop_t *op, const char *staged) { if (staged && op->fs.remove) (void)op->fs.remove(op->fs.ctx, staged); }

/* A file's bytes into place: staged, checked when the source is to go, then renamed (or swapped) in. */
static void put_file(rubraview_fileop_t *op, size_t i, bool overwrite) {
    rubraview_fileop_item_t *it = &op->items[i];
    bool moving = op->kind == RUBRAVIEW_FILEOP_MOVE;
    bool same_vol = moving && op->fs.same_volume && op->fs.same_volume(op->fs.ctx, it->source, it->target);
    if (same_vol && !overwrite) {   /* a rename: atomic, never over a name */
        if (op->fs.rename(op->fs.ctx, it->source, it->target)) { it->state = RUBRAVIEW_FILEOP_DONE; it->did = RUBRAVIEW_FILEOP_DID_MOVE; }
        else fail(it, "it could not be moved");
        return;
    }
    char *staged = staging_name(op, it->target);
    if (!staged) { fail(it, "out of memory"); return; }
    bool ok;
    if (same_vol) ok = op->fs.rename(op->fs.ctx, it->source, staged);   /* moved beside it, then swapped in */
    else {
        ok = op->fs.copy(op->fs.ctx, it->source, staged);
        if (ok && moving) {   /* the source goes only when the copy is checked and on the disk */
            ok = op->fs.same_bytes && op->fs.same_bytes(op->fs.ctx, it->source, staged) == 1 && (!op->fs.flush || op->fs.flush(op->fs.ctx, staged));
            if (!ok) { gone(op, staged); fail(it, "the copy did not match its source: the source is kept"); rubraview_free(staged); return; }
        }
    }
    if (!ok) { gone(op, staged); fail(it, same_vol ? "it could not be moved" : "it could not be copied"); rubraview_free(staged); return; }
    bool placed = overwrite ? op->fs.replace(op->fs.ctx, it->target, staged) : op->fs.rename(op->fs.ctx, staged, it->target);
    if (!placed) {
        if (same_vol) (void)op->fs.rename(op->fs.ctx, staged, it->source);   /* back where it was */
        else gone(op, staged);
        rubraview_fileop_stat_t t = { 0 };
        if (!overwrite && op->fs.stat(op->fs.ctx, it->target, &t) && t.exists) it->state = RUBRAVIEW_FILEOP_PARKED;   /* taken meanwhile: asked */
        else fail(it, overwrite ? "the file there could not be replaced (is it open, or not for the Recycle Bin?)" : "it could not be put in place");
        rubraview_free(staged);
        return;
    }
    rubraview_free(staged);
    it->overwrote = overwrite;
    it->state = RUBRAVIEW_FILEOP_DONE;
    it->did = moving && same_vol ? RUBRAVIEW_FILEOP_DID_MOVE : RUBRAVIEW_FILEOP_DID_COPY;
    if (moving && !same_vol) {
        if (op->fs.recycle(op->fs.ctx, it->source)) it->did = RUBRAVIEW_FILEOP_DID_MOVE;
        else it->why = "copied, but the source could not be removed: both are kept";
    }
}

static void execute(rubraview_fileop_t *op, size_t i) {
    rubraview_fileop_item_t *it = &op->items[i];
    if (it->parent >= 0) {
        rubraview_fileop_state_t ps = op->items[it->parent].state;
        if (ps == RUBRAVIEW_FILEOP_FAILED || ps == RUBRAVIEW_FILEOP_SKIPPED) { skip(it, "its folder was not made"); return; }
    }
    rubraview_fileop_answer_t a = it->answer;
    if (a == RUBRAVIEW_FILEOP_ANSWER_SKIP) { skip(it, "kept as it was there"); return; }
    if (a == RUBRAVIEW_FILEOP_ANSWER_SAME_ONLY) {
        rubraview_fileop_stat_t s = { 0 }, t = { 0 };
        bool same = op->fs.stat(op->fs.ctx, it->source, &s) && op->fs.stat(op->fs.ctx, it->target, &t) && t.exists && !t.is_dir &&
                    (same_identity(&s, &t) || (s.size == t.size && op->fs.same_bytes && op->fs.same_bytes(op->fs.ctx, it->source, it->target) == 1));
        if (same) {
            if (op->kind == RUBRAVIEW_FILEOP_MOVE || op->same_recycles_copy) {
                if (op->fs.recycle(op->fs.ctx, it->source)) { it->state = RUBRAVIEW_FILEOP_DONE; it->did = RUBRAVIEW_FILEOP_DID_RECYCLE_SOURCE; }
                else fail(it, "the same file is there; the source could not go to the Recycle Bin");
            } else skip(it, "the same file is there");
            return;
        }
        a = RUBRAVIEW_FILEOP_ANSWER_KEEP_BOTH;   /* different: both kept */
    }
    if (a == RUBRAVIEW_FILEOP_ANSWER_KEEP_BOTH && !keep_beside(op, it)) { fail(it, "no free name beside it"); return; }
    it = &op->items[i];
    if (a == RUBRAVIEW_FILEOP_ANSWER_NONE) {   /* a name taken since the check: asked, as any */
        rubraview_fileop_stat_t t = { 0 };
        if (!it->expanded && op->fs.stat(op->fs.ctx, it->target, &t) && t.exists) {
            if (!(it->is_dir && t.is_dir)) { it->kind_differs = it->is_dir != t.is_dir; it->state = RUBRAVIEW_FILEOP_PARKED; return; }
        }
    }
    if (it->is_dir) {
        bool moving = op->kind == RUBRAVIEW_FILEOP_MOVE;
        bool same_vol = moving && op->fs.same_volume && op->fs.same_volume(op->fs.ctx, it->source, it->target);
        if (moving && same_vol && !it->expanded) {
            if (op->fs.rename(op->fs.ctx, it->source, it->target)) { it->state = RUBRAVIEW_FILEOP_DONE; it->did = RUBRAVIEW_FILEOP_DID_MOVE; }
            else fail(it, "the folder could not be moved");
            return;
        }
        if (!it->expanded) {   /* kept beside the one there: made, its entries under the new name */
            it->expanded = true;
            if (!expand(op, i, false)) { fail(&op->items[i], "the folder could not be listed"); return; }
            for (size_t k = i + 1; k < op->count; ++k) if (!op->items[k].prepared && op->items[k].parent == (int32_t)i) prepare_one(op, k);
            it = &op->items[i];
        }
        if (op->fs.make_dir(op->fs.ctx, it->target)) { it->state = RUBRAVIEW_FILEOP_DONE; it->did = RUBRAVIEW_FILEOP_DID_MAKE_DIR; }
        else fail(it, "the folder could not be made");
        return;
    }
    put_file(op, i, a == RUBRAVIEW_FILEOP_ANSWER_OVERWRITE);
}

/* A move's source folders that are empty now, deepest first. */
static void tidy_sources(rubraview_fileop_t *op) {
    if (op->kind != RUBRAVIEW_FILEOP_MOVE || !op->fs.remove_empty_dir) return;
    for (size_t k = op->count; k > 0; --k) {
        rubraview_fileop_item_t *it = &op->items[k - 1];
        if (it->is_dir && (it->state == RUBRAVIEW_FILEOP_MERGED || (it->expanded && it->state == RUBRAVIEW_FILEOP_DONE)))
            (void)op->fs.remove_empty_dir(op->fs.ctx, it->source);
    }
}

bool rubraview_fileop_step(rubraview_fileop_t *op, size_t max) {
    if (!op) return false;
    size_t n = 0;
    for (size_t i = 0; i < op->count && n < max; ++i) {
        if (!op->items[i].prepared) prepare_one(op, i);
        if (op->items[i].state != RUBRAVIEW_FILEOP_PENDING) continue;
        execute(op, i);
        n++;
    }
    bool left = false;
    for (size_t i = 0; i < op->count && !left; ++i) left = op->items[i].state == RUBRAVIEW_FILEOP_PENDING || !op->items[i].prepared;
    recount(op);
    if (!left && op->parked == 0) tidy_sources(op);
    return left;
}

bool rubraview_fileop_answer(rubraview_fileop_t *op, size_t index, rubraview_fileop_answer_t answer) {
    if (!op || index >= op->count || op->items[index].state != RUBRAVIEW_FILEOP_PARKED || answer == RUBRAVIEW_FILEOP_ANSWER_NONE) return false;
    rubraview_fileop_item_t *it = &op->items[index];
    if (it->kind_differs && (answer == RUBRAVIEW_FILEOP_ANSWER_OVERWRITE || answer == RUBRAVIEW_FILEOP_ANSWER_SAME_ONLY)) return false;
    it->answer = answer;
    it->state = RUBRAVIEW_FILEOP_PENDING;
    recount(op);
    return true;
}

size_t rubraview_fileop_answer_all(rubraview_fileop_t *op, rubraview_fileop_answer_t answer) {
    size_t n = 0;
    for (size_t i = 0; op && i < op->count; ++i) {
        if (op->items[i].state != RUBRAVIEW_FILEOP_PARKED) continue;
        rubraview_fileop_answer_t a = answer;
        if (op->items[i].kind_differs && (a == RUBRAVIEW_FILEOP_ANSWER_OVERWRITE || a == RUBRAVIEW_FILEOP_ANSWER_SAME_ONLY))
            a = RUBRAVIEW_FILEOP_ANSWER_KEEP_BOTH;   /* a file and a folder: both kept */
        if (rubraview_fileop_answer(op, i, a)) n++;
    }
    return n;
}

bool rubraview_fileop_finished(const rubraview_fileop_t *op) {
    for (size_t i = 0; op && i < op->count; ++i)
        if (op->items[i].state == RUBRAVIEW_FILEOP_PENDING || op->items[i].state == RUBRAVIEW_FILEOP_PARKED || !op->items[i].prepared) return false;
    return true;
}
