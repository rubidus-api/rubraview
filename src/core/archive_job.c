/*
 * RV-207: extracting and compressing (see archive_job.h). FultaArc reads and writes the archives; this file
 * decides the names and the places, keeps what is there, and checks what it made.
 */
#include "rubraview/archive_job.h"
#include "rubraview/lang.h"
#include "rubraview/pal/pal_fs.h"
#include "fulta/arc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *rubraview_archive_result_text(rubraview_archive_result_t result) {
    switch (result) {
        case RUBRAVIEW_ARCHIVE_DONE:            return T("done");
        case RUBRAVIEW_ARCHIVE_PARTLY:          return T("done, but some files could not be written");
        case RUBRAVIEW_ARCHIVE_CANCELLED:       return T("stopped; nothing half-written was left");
        case RUBRAVIEW_ARCHIVE_PASSWORD_NEEDED: return T("this archive asks a password");
        case RUBRAVIEW_ARCHIVE_PASSWORD_WRONG:  return T("the password is not this archive's");
        case RUBRAVIEW_ARCHIVE_CANNOT_READ:     return T("it could not be read");
        case RUBRAVIEW_ARCHIVE_CANNOT_WRITE:    return T("it could not be written");
        case RUBRAVIEW_ARCHIVE_CHECK_FAILED:    return T("the new archive did not read back the same, so it was not kept");
        case RUBRAVIEW_ARCHIVE_NOTHING:         return T("there was nothing to do");
        default:                                return "";
    }
}

/* ---- names ---- */

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }

static bool device_name(const char *s, size_t n) {
    static const char *const DEV[] = { "con", "prn", "aux", "nul" };
    size_t stem = 0;
    while (stem < n && s[stem] != '.') stem++;
    for (size_t i = 0; i < 4; ++i)
        if (stem == 3 && lower(s[0]) == DEV[i][0] && lower(s[1]) == DEV[i][1] && lower(s[2]) == DEV[i][2]) return true;
    if (stem == 4 && s[3] >= '1' && s[3] <= '9') {
        char a = lower(s[0]), b = lower(s[1]), c = lower(s[2]);
        if ((a == 'c' && b == 'o' && c == 'm') || (a == 'l' && b == 'p' && c == 't')) return true;
    }
    return false;
}

size_t rubraview_archive_safe_part(const char *part, size_t len, char *out, size_t cap) {
    if (!out || cap == 0) return 0;
    size_t n = 0;
    if (part && len > 0 && device_name(part, len) && n + 1 < cap) out[n++] = '_';
    for (size_t i = 0; part && i < len && n + 1 < cap; ++i) {
        unsigned char c = (unsigned char)part[i];
        bool bad = c < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' || c == '|' || c == '?' || c == '*' ||
                   c == '/' || c == '\\';
        out[n++] = bad ? '_' : (char)c;
    }
    while (n > 0 && (out[n - 1] == '.' || out[n - 1] == ' ')) n--;   /* Windows drops them, and then names clash */
    if (n == 0 || (n == 1 && out[0] == '_' && len == 0)) { out[0] = '_'; n = 1; }
    out[n] = '\0';
    return n;
}

static bool ends_ci(const char *s, size_t n, const char *tail) {
    size_t t = strlen(tail);
    if (n < t) return false;
    for (size_t i = 0; i < t; ++i) if (lower(s[n - t + i]) != tail[i]) return false;
    return true;
}

size_t rubraview_archive_stem(proven_u8str_view_t archive_path, char *out, size_t cap) {
    if (!out || cap == 0) return 0;
    const char *p = (const char *)archive_path.ptr;
    size_t n = archive_path.size, from = 0;
    for (size_t i = 0; i < n; ++i) if (p[i] == '/' || p[i] == '\\') from = i + 1;
    p += from; n -= from;
    size_t dot = n;
    for (size_t i = n; i > 0; --i) if (p[i - 1] == '.') { dot = i - 1; break; }
    if (dot > 0 && dot < n) {
        bool digits = dot + 1 < n;
        for (size_t i = dot + 1; i < n; ++i) if (p[i] < '0' || p[i] > '9') digits = false;
        n = dot;   /* ".zip", or ".001" of "book.7z.001" */
        if (digits) {   /* a split one's own extension too */
            static const char *const KIND[] = { ".7z", ".zip", ".rar", ".egg", ".alz" };
            for (size_t k = 0; k < 5; ++k) if (ends_ci(p, n, KIND[k])) { n -= strlen(KIND[k]); break; }
        }
        /* "book.part1" of "book.part1.rar" */
        size_t d = n;
        while (d > 0 && p[d - 1] >= '0' && p[d - 1] <= '9') d--;
        if (d < n && d >= 5 && ends_ci(p, d, ".part")) n = d - 5;
    }
    if (n == 0) { p = "archive"; n = 7; }
    return rubraview_archive_safe_part(p, n, out, cap);
}

bool rubraview_archive_free_name(proven_u8str_view_t path, bool (*exists)(proven_u8str_view_t path), char *out, size_t cap) {
    if (!out || cap == 0 || path.size + 1 > cap) return false;
    memcpy(out, path.ptr, path.size);
    out[path.size] = '\0';
    if (!exists || !exists(path)) return true;
    const char *s = (const char *)path.ptr;
    size_t dot = path.size, sep = 0;
    for (size_t i = 0; i < path.size; ++i) if (s[i] == '/' || s[i] == '\\') sep = i + 1;
    for (size_t i = path.size; i > sep; --i) if (s[i - 1] == '.') { dot = i - 1; break; }
    if (dot == sep) dot = path.size;   /* ".hidden" has no extension to keep */
    for (unsigned k = 1; k < 100000; ++k) {
        int w = snprintf(out, cap, "%.*s-%u%.*s", (int)dot, s, k, (int)(path.size - dot), s + dot);
        if (w <= 0 || (size_t)w >= cap) return false;
        if (!exists((proven_u8str_view_t){ .ptr = (const proven_byte_t *)out, .size = (size_t)w })) return true;
    }
    return false;
}

static proven_u8str_view_t view(const char *s) { return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)s, .size = s ? strlen(s) : 0 }; }
static bool exists_fn(proven_u8str_view_t path) { return rubraview_pal_fs_exists(path); }

/* A file's facts; the path's copy kept in a small arena of the caller's stack. */
static bool stat_of(const char *path, rubraview_fs_entry_t *out) {
    static _Thread_local uint8_t room[3 * RUBRAVIEW_ARCHIVE_PATH];
    proven_arena_t a = proven_arena_create((proven_mem_mut_t){ .ptr = room, .size = sizeof room });
    return rubraview_pal_fs_stat(&a, view(path), out);
}

static char separator_of(const char *path) {
    for (const char *p = path; *p; ++p) if (*p == '\\') return '\\';
    return '/';
}

static void say(char *why, size_t cap, const char *what, const char *name) {
    if (why[0]) return;   /* the first one */
    snprintf(why, cap, "%s: %s", name ? name : "", what);
}

/* ---- extracting ---- */

typedef struct ex_ctx {
    rubraview_extract_job_t *job;
    rubraview_fs_writer_t writer;
} ex_ctx_t;

static fulta_arc_err_t ex_password(void *ctx, uint32_t attempt, const char **utf8_out) {
    rubraview_extract_job_t *job = ctx;
    if (attempt > 0 || !job->password || !job->password[0]) return FULTA_ARC_ERR_PASSWORD_NEEDED;
    *utf8_out = job->password;
    return FULTA_ARC_OK;
}

static int ex_cancel(void *ctx) { return atomic_load(&((rubraview_extract_job_t *)ctx)->cancel) ? 1 : 0; }

static fulta_arc_err_t ex_volume(void *ctx, uint32_t index, const char *name, fulta_arc_source_t *out) {
    (void)ctx; (void)index;
    if (!name || !name[0]) return FULTA_ARC_ERR_VOLUME_MISSING;
    return fulta_arc_source_file(name, out) == FULTA_ARC_OK ? FULTA_ARC_OK : FULTA_ARC_ERR_VOLUME_MISSING;
}

static fulta_arc_err_t ex_write(void *ctx, const void *data, size_t n) {
    ex_ctx_t *x = ctx;
    if (atomic_load(&x->job->cancel)) return FULTA_ARC_ERR_CANCELLED;
    if (!rubraview_pal_fs_writer_write(&x->writer, data, n)) return FULTA_ARC_ERR_IO;
    atomic_fetch_add(&x->job->done, n);
    return FULTA_ARC_OK;
}

static fulta_arc_err_t open_archive(const char *path, rubraview_codepage_t codepage, rubraview_extract_job_t *job, fulta_arc_t **out) {
    fulta_arc_source_t src;
    fulta_arc_err_t err = fulta_arc_source_file(path, &src);
    if (err != FULTA_ARC_OK) return err;
    fulta_arc_options_t opt = {
        .name = path, .volume = ex_volume,
        .password = job ? ex_password : NULL, .password_ctx = job,
        .codepage = rubraview_page_source_fulta_codepage(codepage),
        .cancel = job ? ex_cancel : NULL, .cancel_ctx = job,
    };
    return fulta_arc_open(&src, &opt, out);
}

static bool picked(const rubraview_extract_job_t *job, size_t index) {
    if (!job->only) return true;
    for (size_t i = 0; i < job->only_count; ++i) if (job->only[i] == index) return true;
    return false;
}

/* The entry's name as it goes under the root: `inner` left off, each part made safe, the root's separator between. */
static bool place_of(const rubraview_extract_job_t *job, const char *name, char sep, char *out, size_t cap) {
    size_t skip = 0;
    if (job->only && job->inner && job->inner[0]) {
        size_t k = strlen(job->inner);
        if (strncmp(name, job->inner, k) == 0) skip = k;
    }
    const char *p = name + skip;
    size_t n = 0;
    out[0] = '\0';
    while (*p) {
        const char *end = strchr(p, '/');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len > 0) {
            if (n > 0) { if (n + 1 >= cap) return false; out[n++] = sep; }
            char part[512];
            size_t m = rubraview_archive_safe_part(p, len, part, sizeof part);
            if (n + m + 1 >= cap) return false;
            memcpy(out + n, part, m);
            n += m;
            out[n] = '\0';
        }
        if (!end) break;
        p = end + 1;
    }
    return n > 0;
}

/* Everything picked is in one top folder, or is one file: SMART writes it out as it is. */
static bool one_top(const fulta_arc_t *arc, const rubraview_extract_job_t *job) {
    const char *top = NULL;
    size_t top_len = 0, files = 0;
    bool foldered = true;
    for (size_t i = 0; i < fulta_arc_count(arc); ++i) {
        if (!picked(job, i)) continue;
        const fulta_arc_entry_t *e = fulta_arc_entry(arc, i);
        const char *name = e->name;
        if (job->only && job->inner && strncmp(name, job->inner, strlen(job->inner)) == 0) name += strlen(job->inner);
        const char *slash = strchr(name, '/');
        size_t len = slash ? (size_t)(slash - name) : strlen(name);
        if (!(e->flags & FULTA_ARC_ENTRY_DIR)) files++;
        if (!slash && !(e->flags & FULTA_ARC_ENTRY_DIR)) foldered = false;   /* a file at the top */
        if (!top) { top = name; top_len = len; }
        else if (len != top_len || strncmp(name, top, len) != 0) return false;
    }
    return top && (foldered || files == 1);
}

static rubraview_archive_result_t result_of(fulta_arc_err_t err) {
    switch (err) {
        case FULTA_ARC_ERR_PASSWORD_NEEDED: return RUBRAVIEW_ARCHIVE_PASSWORD_NEEDED;
        case FULTA_ARC_ERR_PASSWORD_WRONG:  return RUBRAVIEW_ARCHIVE_PASSWORD_WRONG;
        case FULTA_ARC_ERR_CANCELLED:       return RUBRAVIEW_ARCHIVE_CANCELLED;
        default:                            return RUBRAVIEW_ARCHIVE_CANNOT_READ;
    }
}

int rubraview_archive_wants_password(const char *archive, rubraview_codepage_t codepage) {
    fulta_arc_t *arc = NULL;
    fulta_arc_err_t err = open_archive(archive, codepage, NULL, &arc);
    if (err == FULTA_ARC_ERR_PASSWORD_NEEDED) return 1;   /* its names are hidden too */
    if (err != FULTA_ARC_OK) return -1;
    int wants = 0;
    for (size_t i = 0; i < fulta_arc_count(arc) && !wants; ++i)
        if (fulta_arc_entry(arc, i)->flags & FULTA_ARC_ENTRY_ENCRYPTED) wants = 1;
    fulta_arc_close(arc);
    return wants;
}

void rubraview_extract_run(rubraview_extract_job_t *job) {
    if (!job) return;
    job->result = RUBRAVIEW_ARCHIVE_DONE;
    job->written = job->skipped = job->failed = 0;
    job->why[0] = job->root[0] = '\0';
    fulta_arc_t *arc = NULL;
    fulta_arc_err_t err = open_archive(job->archive, job->codepage, job, &arc);
    if (err != FULTA_ARC_OK) { job->result = result_of(err); say(job->why, sizeof job->why, fulta_arc_strerror(err), job->archive); return; }

    char sep = separator_of(job->dest);
    size_t dn = strlen(job->dest);
    while (dn > 1 && (job->dest[dn - 1] == '/' || job->dest[dn - 1] == '\\')) dn--;
    bool own_folder = job->mode == RUBRAVIEW_EXTRACT_FOLDER || (job->mode == RUBRAVIEW_EXTRACT_SMART && !one_top(arc, job));
    if (own_folder) {
        char stem[512], want[RUBRAVIEW_ARCHIVE_PATH];
        rubraview_archive_stem(view(job->archive), stem, sizeof stem);
        snprintf(want, sizeof want, "%.*s%c%s", (int)dn, job->dest, sep, stem);
        rubraview_fs_entry_t st;
        proven_u8str_view_t w = view(want);
        bool file_there = rubraview_pal_fs_exists(w) && stat_of(want, &st) && !st.is_directory;
        if (file_there) rubraview_archive_free_name(w, exists_fn, job->root, sizeof job->root);   /* a file of that name: book-1 */
        else snprintf(job->root, sizeof job->root, "%s", want);
    } else {
        snprintf(job->root, sizeof job->root, "%.*s", (int)dn, job->dest);
    }

    uint64_t total = 0;
    for (size_t i = 0; i < fulta_arc_count(arc); ++i) if (picked(job, i)) total += fulta_arc_entry(arc, i)->size;
    atomic_store(&job->total, total);
    atomic_store(&job->done, 0);
    if (!rubraview_pal_fs_make_dirs(view(job->root))) {
        job->result = RUBRAVIEW_ARCHIVE_CANNOT_WRITE;
        say(job->why, sizeof job->why, T("the folder could not be made"), job->root);
        fulta_arc_close(arc);
        return;
    }

    char rel[RUBRAVIEW_ARCHIVE_PATH], path[RUBRAVIEW_ARCHIVE_PATH], final[RUBRAVIEW_ARCHIVE_PATH], part[RUBRAVIEW_ARCHIVE_PATH + 16];
    for (size_t i = 0; i < fulta_arc_count(arc); ++i) {
        if (atomic_load(&job->cancel)) { job->result = RUBRAVIEW_ARCHIVE_CANCELLED; break; }
        if (!picked(job, i)) continue;
        const fulta_arc_entry_t *e = fulta_arc_entry(arc, i);
        if (!place_of(job, e->name, sep, rel, sizeof rel)) continue;   /* the picked folder itself */
        int w = snprintf(path, sizeof path, "%s%c%s", job->root, sep, rel);
        if (w <= 0 || (size_t)w >= sizeof path) { job->failed++; say(job->why, sizeof job->why, T("the name is too long"), e->name); continue; }
        if (e->flags & FULTA_ARC_ENTRY_DIR) { (void)rubraview_pal_fs_make_dirs(view(path)); continue; }
        if (e->flags & FULTA_ARC_ENTRY_SYMLINK) { job->skipped++; continue; }   /* a link is not followed out of the folder */
        /* its folder */
        char *last = strrchr(path, sep);
        if (last) { *last = '\0'; (void)rubraview_pal_fs_make_dirs(view(path)); *last = sep; }
        proven_u8str_view_t pv = view(path);
        bool there = rubraview_pal_fs_exists(pv);
        if (there && job->exists == RUBRAVIEW_EXISTS_SKIP) { job->skipped++; atomic_fetch_add(&job->done, e->size); continue; }
        if (there && job->exists == RUBRAVIEW_EXISTS_NUMBER) {
            if (!rubraview_archive_free_name(pv, exists_fn, final, sizeof final)) { job->failed++; continue; }
        } else {
            snprintf(final, sizeof final, "%s", path);
        }
        snprintf(part, sizeof part, "%s.rvpart", final);
        (void)rubraview_pal_fs_delete(view(part));   /* one left by a run that was stopped hard */
        ex_ctx_t x = { .job = job };
        if (!rubraview_pal_fs_writer_open(&x.writer, view(part))) {
            job->failed++; say(job->why, sizeof job->why, T("it could not be written"), final); continue;
        }
        fulta_arc_sink_t sink = { .ctx = &x, .write = ex_write };
        err = fulta_arc_extract(arc, i, &sink);
        if (err != FULTA_ARC_OK) {
            rubraview_pal_fs_writer_abort(&x.writer, view(part));
            if (err == FULTA_ARC_ERR_CANCELLED || atomic_load(&job->cancel)) { job->result = RUBRAVIEW_ARCHIVE_CANCELLED; break; }
            if (err == FULTA_ARC_ERR_PASSWORD_NEEDED || err == FULTA_ARC_ERR_PASSWORD_WRONG) { job->result = result_of(err); break; }
            job->failed++;
            say(job->why, sizeof job->why, fulta_arc_strerror(err), e->name);
            continue;
        }
        bool closed = rubraview_pal_fs_writer_close(&x.writer);
        bool placed = closed && (there && job->exists == RUBRAVIEW_EXISTS_REPLACE ? rubraview_pal_fs_replace(view(part), view(final))
                                                                                    : rubraview_pal_fs_move(view(part), view(final)));
        if (!placed) {
            (void)rubraview_pal_fs_delete(view(part));
            job->failed++;
            say(job->why, sizeof job->why, T("it could not be written"), final);
            continue;
        }
        if (e->flags & FULTA_ARC_ENTRY_HAS_MTIME) (void)rubraview_pal_fs_set_mtime(view(final), e->mtime / 1000000000);
        job->written++;
        atomic_fetch_add(&job->files, 1);
    }
    fulta_arc_close(arc);
    if (job->result == RUBRAVIEW_ARCHIVE_DONE) {
        if (job->failed > 0) job->result = RUBRAVIEW_ARCHIVE_PARTLY;
        else if (job->written == 0 && job->skipped == 0) job->result = RUBRAVIEW_ARCHIVE_NOTHING;
    }
}

/* ---- compressing ---- */

typedef struct pack_item {
    char *name;        /* in the archive, '/' between parts; a folder's ends in '/' */
    char *path;        /* on the disk */
    uint64_t size;
    int64_t mtime;
    bool dir;
} pack_item_t;

typedef struct pack_list {
    pack_item_t *items;
    size_t count, cap;
    bool failed;
} pack_list_t;

static char *dup_n(const char *s, size_t n) {
    char *d = malloc(n + 1);
    if (d) { memcpy(d, s, n); d[n] = '\0'; }
    return d;
}

static void list_add(pack_list_t *l, const char *name, const char *path, uint64_t size, int64_t mtime, bool dir) {
    if (l->failed) return;
    if (l->count == l->cap) {
        size_t cap = l->cap ? l->cap * 2 : 64;
        pack_item_t *more = realloc(l->items, cap * sizeof *more);
        if (!more) { l->failed = true; return; }
        l->items = more;
        l->cap = cap;
    }
    pack_item_t it = { .name = dup_n(name, strlen(name)), .path = dup_n(path, strlen(path)), .size = size, .mtime = mtime, .dir = dir };
    if (!it.name || !it.path) { free(it.name); free(it.path); l->failed = true; return; }
    l->items[l->count++] = it;
}

static void list_free(pack_list_t *l) {
    for (size_t i = 0; i < l->count; ++i) { free(l->items[i].name); free(l->items[i].path); }
    free(l->items);
    *l = (pack_list_t){ 0 };
}

#define LIST_ARENA (16u << 20)

static void walk(pack_list_t *l, const char *name, const char *path, rubraview_compress_job_t *job, int depth) {
    if (depth > 64 || l->failed || atomic_load(&job->cancel)) return;
    void *mem = malloc(LIST_ARENA);
    if (!mem) { l->failed = true; return; }
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = mem, .size = LIST_ARENA });
    rubraview_fs_listing_t listing = rubraview_pal_fs_list_dir(&arena, view(path));
    char sep = separator_of(path);
    for (size_t i = 0; i < listing.count && !l->failed; ++i) {
        const rubraview_fs_entry_t *e = &listing.entries[i];
        char child_name[RUBRAVIEW_ARCHIVE_PATH], child_path[RUBRAVIEW_ARCHIVE_PATH];
        int a = snprintf(child_name, sizeof child_name, "%s/%.*s%s", name, (int)e->name.size, (const char *)e->name.ptr, e->is_directory ? "/" : "");
        int b = snprintf(child_path, sizeof child_path, "%s%c%.*s", path, sep, (int)e->name.size, (const char *)e->name.ptr);
        if (a <= 0 || b <= 0 || (size_t)a >= sizeof child_name || (size_t)b >= sizeof child_path) continue;
        list_add(l, child_name, child_path, e->size_bytes, e->mtime, e->is_directory);
        if (e->is_directory) {
            child_name[a - 1] = '\0';   /* without its '/' */
            walk(l, child_name, child_path, job, depth + 1);
        }
    }
    free(mem);
}

typedef struct pack_reader {
    const pack_item_t *item;
    rubraview_fs_mapping_t map;
    uint64_t at;
    bool mapped, failed;
    rubraview_compress_job_t *job;
} pack_reader_t;

static fulta_arc_err_t pack_read(void *ctx, void *buf, size_t cap, size_t *got) {
    pack_reader_t *r = ctx;
    *got = 0;
    if (atomic_load(&r->job->cancel)) return FULTA_ARC_ERR_CANCELLED;
    if (r->item->size == 0) return FULTA_ARC_OK;
    if (!r->mapped) {
        if (!rubraview_pal_fs_map(view(r->item->path), &r->map) || r->map.size != r->item->size) {
            if (r->map.os) rubraview_pal_fs_unmap(&r->map);
            r->failed = true;
            return FULTA_ARC_ERR_IO;
        }
        r->mapped = true;
    }
    uint64_t left = r->map.size - r->at;
    size_t n = left < cap ? (size_t)left : cap;
    if (n) memcpy(buf, r->map.data + r->at, n);
    r->at += n;
    *got = n;
    if (r->at == r->map.size) { rubraview_pal_fs_unmap(&r->map); r->mapped = false; r->at = r->item->size; }
    return FULTA_ARC_OK;
}

/* An archive written into memory (the empty 7z begun from). */
typedef struct mem_sink { uint8_t *data; size_t size, cap; } mem_sink_t;

static fulta_arc_err_t mem_write(void *ctx, const void *data, size_t n) {
    mem_sink_t *m = ctx;
    if (m->size + n > m->cap) {
        size_t cap = (m->size + n) * 2 + 64;
        uint8_t *more = realloc(m->data, cap);
        if (!more) return FULTA_ARC_ERR_NOMEM;
        m->data = more;
        m->cap = cap;
    }
    if (n) memcpy(m->data + m->size, data, n);
    m->size += n;
    return FULTA_ARC_OK;
}

static fulta_arc_err_t mem_patch(void *ctx, uint64_t offset, const void *data, size_t n) {
    mem_sink_t *m = ctx;
    if (offset > m->size || n > m->size - offset) return FULTA_ARC_ERR_IO;
    memcpy(m->data + offset, data, n);
    return FULTA_ARC_OK;
}

static int pack_progress(void *ctx, uint64_t done, uint64_t total) {
    rubraview_compress_job_t *job = ctx;
    (void)total;
    atomic_store(&job->done, done);
    return atomic_load(&job->cancel) ? 1 : 0;
}

/* Checking: an entry's bytes against its source's, as they come. */
typedef struct cmp_ctx {
    const uint8_t *src;
    uint64_t size, at;
    bool same;
    rubraview_compress_job_t *job;
} cmp_ctx_t;

static fulta_arc_err_t cmp_write(void *ctx, const void *data, size_t n) {
    cmp_ctx_t *c = ctx;
    if (atomic_load(&c->job->cancel)) return FULTA_ARC_ERR_CANCELLED;
    if (c->at + n > c->size || memcmp(c->src + c->at, data, n) != 0) { c->same = false; return FULTA_ARC_ERR_CHECKSUM; }
    c->at += n;
    atomic_fetch_add(&c->job->done, n);
    return FULTA_ARC_OK;
}

static fulta_arc_err_t cmp_password(void *ctx, uint32_t attempt, const char **utf8_out) {
    const char *pw = ctx;
    if (attempt > 0 || !pw || !pw[0]) return FULTA_ARC_ERR_PASSWORD_NEEDED;
    *utf8_out = pw;
    return FULTA_ARC_OK;
}

static int cmp_order(const void *a, const void *b) {
    return strcmp(((const pack_item_t *)a)->name, ((const pack_item_t *)b)->name);
}

/* The archive at `path` holds exactly `list`'s files and folders, byte for byte. */
static bool check_written(const char *path, pack_list_t *list, rubraview_compress_job_t *job) {
    fulta_arc_source_t src;
    if (fulta_arc_source_file(path, &src) != FULTA_ARC_OK) return false;
    fulta_arc_options_t opt = { .name = path, .password = cmp_password, .password_ctx = (void *)job->password,
                                .codepage = FULTA_ARC_CP_UTF8 };
    fulta_arc_t *arc = NULL;
    if (fulta_arc_open(&src, &opt, &arc) != FULTA_ARC_OK) return false;
    bool ok = fulta_arc_count(arc) == list->count;
    qsort(list->items, list->count, sizeof list->items[0], cmp_order);
    for (size_t i = 0; ok && i < fulta_arc_count(arc); ++i) {
        const fulta_arc_entry_t *e = fulta_arc_entry(arc, i);
        size_t n = strlen(e->name);
        while (n > 0 && e->name[n - 1] == '/') n--;
        pack_item_t key = { .name = dup_n(e->name, n) };
        if (!key.name) { ok = false; break; }
        /* the list's folders end in '/'; look both ways */
        pack_item_t *hit = bsearch(&key, list->items, list->count, sizeof list->items[0], cmp_order);
        if (!hit) {
            char *slashed = malloc(n + 2);
            if (slashed) { memcpy(slashed, e->name, n); slashed[n] = '/'; slashed[n + 1] = '\0'; }
            pack_item_t k2 = { .name = slashed };
            hit = slashed ? bsearch(&k2, list->items, list->count, sizeof list->items[0], cmp_order) : NULL;
            free(slashed);
        }
        free(key.name);
        if (!hit || hit->dir != ((e->flags & FULTA_ARC_ENTRY_DIR) != 0) || (!hit->dir && e->size != hit->size)) { ok = false; break; }
        if (hit->dir || hit->size == 0) continue;
        rubraview_fs_mapping_t map = { 0 };
        if (!rubraview_pal_fs_map(view(hit->path), &map)) { ok = false; break; }
        cmp_ctx_t c = { .src = map.data, .size = map.size, .same = true, .job = job };
        fulta_arc_sink_t sink = { .ctx = &c, .write = cmp_write };
        ok = fulta_arc_extract(arc, i, &sink) == FULTA_ARC_OK && c.same && c.at == c.size;
        rubraview_pal_fs_unmap(&map);
    }
    fulta_arc_close(arc);
    return ok;
}

void rubraview_compress_run(rubraview_compress_job_t *job) {
    if (!job) return;
    job->result = RUBRAVIEW_ARCHIVE_DONE;
    job->files = job->folders = 0;
    job->why[0] = job->written[0] = '\0';
    atomic_store(&job->phase, 0);
    pack_list_t list = { 0 };
    for (size_t s = 0; s < job->source_count && !list.failed; ++s) {
        const char *path = job->sources[s];
        rubraview_fs_entry_t st;
        if (!stat_of(path, &st)) {
            job->result = RUBRAVIEW_ARCHIVE_CANNOT_READ;
            say(job->why, sizeof job->why, T("it is not there"), path);
            list_free(&list);
            return;
        }
        size_t n = strlen(path);
        while (n > 1 && (path[n - 1] == '/' || path[n - 1] == '\\')) n--;
        size_t from = 0;
        for (size_t i = 0; i < n; ++i) if (path[i] == '/' || path[i] == '\\') from = i + 1;
        char base[512];
        rubraview_archive_safe_part(path + from, n - from, base, sizeof base);
        if (st.is_directory) {
            char slashed[520];
            snprintf(slashed, sizeof slashed, "%s/", base);
            list_add(&list, slashed, path, 0, st.mtime, true);
            walk(&list, base, path, job, 0);
        } else {
            list_add(&list, base, path, st.size_bytes, st.mtime, false);
        }
    }
    if (list.failed) { job->result = RUBRAVIEW_ARCHIVE_CANNOT_READ; say(job->why, sizeof job->why, T("not enough memory"), ""); list_free(&list); return; }
    if (atomic_load(&job->cancel)) { job->result = RUBRAVIEW_ARCHIVE_CANCELLED; list_free(&list); return; }
    if (list.count == 0) { job->result = RUBRAVIEW_ARCHIVE_NOTHING; list_free(&list); return; }

    /* two sources of one name would be one entry twice: the second is numbered */
    for (size_t i = 0; i < list.count; ++i) {
        for (size_t j = 0; j < i; ++j) {
            if (strcmp(list.items[i].name, list.items[j].name) != 0) continue;
            char renamed[RUBRAVIEW_ARCHIVE_PATH];
            snprintf(renamed, sizeof renamed, "%s-%zu", list.items[i].name, i);
            free(list.items[i].name);
            list.items[i].name = dup_n(renamed, strlen(renamed));
            break;
        }
    }

    uint64_t total = 0;
    for (size_t i = 0; i < list.count; ++i) {
        if (list.items[i].dir) job->folders++;
        else { job->files++; total += list.items[i].size; }
    }
    if (!rubraview_archive_free_name(view(job->output), exists_fn, job->written, sizeof job->written)) {
        job->result = RUBRAVIEW_ARCHIVE_CANNOT_WRITE; list_free(&list); return;
    }

    /* An empty archive of the other kind to begin from: FultaArc's edit then takes its convert route, which streams
       each file from the disk as it packs it (its copy route would hold a new file whole in memory). */
    static const uint8_t EMPTY_ZIP[22] = { 'P', 'K', 5, 6 };
    mem_sink_t empty7z = { 0 };
    fulta_arc_write_sink_t ms = { .ctx = &empty7z, .write = mem_write, .patch = mem_patch };
    fulta_arc_t *base = NULL;
    fulta_arc_edit_t *edit = NULL;
    pack_reader_t *readers = calloc(list.count, sizeof *readers);
    fulta_arc_err_t err = readers ? FULTA_ARC_OK : FULTA_ARC_ERR_NOMEM;
    if (err == FULTA_ARC_OK && job->format == RUBRAVIEW_PACK_ZIP) err = fulta_arc_write(FULTA_ARC_FORMAT_7Z, NULL, 0, NULL, &ms);
    if (err == FULTA_ARC_OK)
        err = job->format == RUBRAVIEW_PACK_ZIP ? fulta_arc_open_memory(empty7z.data, empty7z.size, NULL, &base)
                                                : fulta_arc_open_memory(EMPTY_ZIP, sizeof EMPTY_ZIP, NULL, &base);
    if (err == FULTA_ARC_OK) err = fulta_arc_edit_begin(base, &edit);
    for (size_t i = 0; err == FULTA_ARC_OK && i < list.count; ++i) {
        readers[i] = (pack_reader_t){ .item = &list.items[i], .job = job };
        fulta_arc_reader_t r = { .ctx = &readers[i], .read = pack_read, .size = list.items[i].dir ? 0 : list.items[i].size,
                                 .mtime = list.items[i].mtime };
        err = fulta_arc_edit_add(edit, list.items[i].name, &r);
    }
    atomic_store(&job->total, total);
    atomic_store(&job->done, 0);
    atomic_store(&job->phase, 1);
    bool secret = job->password && job->password[0];
    fulta_arc_write_options_t wo = {
        .level = job->level < 0 ? 0 : job->level > 9 ? 9 : job->level,
        .progress = pack_progress, .progress_ctx = job,
        .password = secret ? job->password : NULL,
        .encryption = !secret ? FULTA_ARC_ENCRYPT_DEFAULT : job->format == RUBRAVIEW_PACK_7Z ? FULTA_ARC_ENCRYPT_7Z_AES256 : FULTA_ARC_ENCRYPT_ZIP_AES256,
    };
    fulta_arc_format_t format = job->format == RUBRAVIEW_PACK_7Z ? FULTA_ARC_FORMAT_7Z : FULTA_ARC_FORMAT_ZIP;
    if (err == FULTA_ARC_OK) err = fulta_arc_edit_write_file(edit, format, job->written, &wo);
    for (size_t i = 0; readers && i < list.count; ++i) {
        if (readers[i].mapped) rubraview_pal_fs_unmap(&readers[i].map);
        if (readers[i].failed && err != FULTA_ARC_OK) say(job->why, sizeof job->why, T("it could not be read"), list.items[i].path);
    }
    free(readers);
    if (edit) fulta_arc_edit_free(edit);
    if (base) fulta_arc_close(base);
    free(empty7z.data);
    if (err != FULTA_ARC_OK) {
        (void)rubraview_pal_fs_delete(view(job->written));   /* FultaArc renames only a whole one over it; nothing of ours stays */
        job->result = err == FULTA_ARC_ERR_CANCELLED || atomic_load(&job->cancel) ? RUBRAVIEW_ARCHIVE_CANCELLED
                    : job->why[0] ? RUBRAVIEW_ARCHIVE_CANNOT_READ : RUBRAVIEW_ARCHIVE_CANNOT_WRITE;
        if (!job->why[0] && job->result == RUBRAVIEW_ARCHIVE_CANNOT_WRITE) say(job->why, sizeof job->why, fulta_arc_strerror(err), job->written);
        job->written[0] = '\0';
        list_free(&list);
        return;
    }

    /* read back and compared before it is kept */
    atomic_store(&job->phase, 2);
    atomic_store(&job->done, 0);
    if (!check_written(job->written, &list, job)) {
        (void)rubraview_pal_fs_delete(view(job->written));
        job->result = atomic_load(&job->cancel) ? RUBRAVIEW_ARCHIVE_CANCELLED : RUBRAVIEW_ARCHIVE_CHECK_FAILED;
        job->written[0] = '\0';
    }
    list_free(&list);
}
