/*
 * RV-207: extracting and compressing (see archive_job.h). FultaArc reads and writes the archives; this file
 * decides the names and the places, keeps what is there, and checks what it made.
 */
#include "rubraview/core.h"
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

static bool device_name(proven_u8str_view_t s_view) {
    const char *s = (const char *)s_view.ptr;
    size_t n = (size_t)s_view.size;
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

size_t rubraview_archive_safe_part(proven_u8str_view_t part_view, char *out, size_t cap) {
    const char *part = (const char *)part_view.ptr;
    size_t len = (size_t)part_view.size;
    if (!out || cap == 0) return 0;
    size_t n = 0;
    if (part && len > 0 && device_name(rubraview_bytes_view(part, len)) && n + 1 < cap) out[n++] = '_';
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

static bool ends_ci(proven_u8str_view_t s_view, const char *tail) {
    const char *s = (const char *)s_view.ptr;
    size_t n = (size_t)s_view.size;
    size_t t = proven_cstr_len(tail);
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
            for (size_t k = 0; k < 5; ++k) if (ends_ci(rubraview_bytes_view(p, n), KIND[k])) { n -= proven_cstr_len(KIND[k]); break; }
        }
        /* "book.part1" of "book.part1.rar" */
        size_t d = n;
        while (d > 0 && p[d - 1] >= '0' && p[d - 1] <= '9') d--;
        if (d < n && d >= 5 && ends_ci(rubraview_bytes_view(p, d), ".part")) n = d - 5;
    }
    if (n == 0) { p = "archive"; n = 7; }
    return rubraview_archive_safe_part(rubraview_bytes_view(p, n), out, cap);
}

bool rubraview_archive_free_name(proven_u8str_view_t path, bool (*exists)(proven_u8str_view_t path), char *out, size_t cap) {
    if (!out || cap == 0 || path.size + 1 > cap) return false;
    proven_sys_mem_copy(out, path.ptr, path.size);
    out[path.size] = '\0';
    if (!exists || !exists(path)) return true;
    const char *s = (const char *)path.ptr;
    size_t dot = path.size, sep = 0;
    for (size_t i = 0; i < path.size; ++i) if (s[i] == '/' || s[i] == '\\') sep = i + 1;
    for (size_t i = path.size; i > sep; --i) if (s[i - 1] == '.') { dot = i - 1; break; }
    if (dot == sep) dot = path.size;   /* ".hidden" has no extension to keep */
    for (unsigned k = 2; k < 100000; ++k) {   /* RV-259 (D-311): "name (2).ext", (3) ... */
        int w = rubraview_fmt(out, cap, "{} ({}){}", PROVEN_ARG(rubraview_bytes_view(s, (int)dot)), PROVEN_ARG((unsigned)k), PROVEN_ARG(rubraview_bytes_view(s + dot, (int)(path.size - dot))));
        if (w <= 0 || (size_t)w >= cap) return false;
        if (!exists((proven_u8str_view_t){ .ptr = (const proven_byte_t *)out, .size = (size_t)w })) return true;
    }
    return false;
}

bool rubraview_archive_free_folder_name(proven_u8str_view_t path, bool (*exists)(proven_u8str_view_t path), char *out, size_t cap) {
    if (!out || cap == 0 || path.size + 1 > cap) return false;
    proven_sys_mem_copy(out, path.ptr, path.size);
    out[path.size] = '\0';
    if (!exists || !exists(path)) return true;
    for (unsigned k = 2; k < 100000; ++k) {   /* RV-259 (D-311): "folder (2)", (3) ... */
        int w = rubraview_fmt(out, cap, "{} ({})", PROVEN_ARG(rubraview_bytes_view((const char *)path.ptr, (int)path.size)), PROVEN_ARG((unsigned)k));
        if (w <= 0 || (size_t)w >= cap) return false;
        if (!exists((proven_u8str_view_t){ .ptr = (const proven_byte_t *)out, .size = (size_t)w })) return true;
    }
    return false;
}

static proven_u8str_view_t view(const char *s) { return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)s, .size = s ? proven_cstr_len(s) : 0 }; }
static bool exists_fn(proven_u8str_view_t path) { return rubraview_pal_fs_exists(path); }

/* D-263: a split set's names. Volume `i` of the set named `set` ("x.7z", "x.zip"), not its last (ZIP's last is the
   set's own name); false past the name's room. */
static bool volume_name(rubraview_pack_format_t format, const char *set, uint32_t i, char *out, size_t cap) {
    return fulta_arc_volume_name(format == RUBRAVIEW_PACK_7Z ? FULTA_ARC_FORMAT_7Z : FULTA_ARC_FORMAT_ZIP, set, i, UINT32_MAX,
                                 out, cap) == FULTA_ARC_OK;
}

/* Taken when the name or the set's first volume is there (for the free name a split set is given). */
static _Thread_local rubraview_pack_format_t g_set_format;
static bool set_exists_fn(proven_u8str_view_t path) {
    char set[RUBRAVIEW_ARCHIVE_PATH], first[RUBRAVIEW_ARCHIVE_PATH + 16];
    if (path.size >= sizeof set) return true;
    proven_sys_mem_copy(set, path.ptr, path.size);
    set[path.size] = '\0';
    return rubraview_pal_fs_exists(path) || !volume_name(g_set_format, set, 0, first, sizeof first) || rubraview_pal_fs_exists(view(first));
}

size_t rubraview_pack_set_parts(rubraview_pack_format_t format, const char *set, char (*out)[RUBRAVIEW_PACK_PART_PATH], size_t cap) {
    size_t n = 0;
    char name[RUBRAVIEW_PACK_PART_PATH];
    for (uint32_t i = 0; i < 1000 && n < cap && volume_name(format, set, i, name, sizeof name); ++i)   /* gaps too: older sets' */
        if (rubraview_pal_fs_exists(view(name))) rubraview_fmt(out[n++], RUBRAVIEW_PACK_PART_PATH, "{}", PROVEN_ARG(name));
    if (format == RUBRAVIEW_PACK_ZIP && n < cap && rubraview_pal_fs_exists(view(set))) rubraview_fmt(out[n++], RUBRAVIEW_PACK_PART_PATH, "{}", PROVEN_ARG(set));
    return n;
}

/* Every volume of a set and the set's own name, deleted (a set that failed its check). */
static void delete_set(rubraview_pack_format_t format, const char *set) {
    char name[RUBRAVIEW_ARCHIVE_PATH + 16];
    for (uint32_t i = 0; i < 100000 && volume_name(format, set, i, name, sizeof name) && rubraview_pal_fs_exists(view(name)); ++i)
        (void)rubraview_pal_fs_delete(view(name));
    (void)rubraview_pal_fs_delete(view(set));
}

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
    rubraview_fmt(why, cap, "{}: {}", PROVEN_ARG(name ? name : ""), PROVEN_ARG(what));
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
    if (!rubraview_pal_fs_writer_write(&x->writer, rubraview_mem_view(data, n))) return FULTA_ARC_ERR_IO;
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
        .auto_codepage = rubraview_fulta_auto_codepage(),   /* RV-239: the machine's, not CP437 */
        .cancel = job ? ex_cancel : NULL, .cancel_ctx = job,
    };
    return fulta_arc_open(&src, &opt, out);
}

static bool picked(const rubraview_extract_job_t *job, size_t index) {
    if (!job->only) return true;
    for (size_t i = 0; i < job->only_count; ++i) if (job->only[i] == index) return true;
    return false;
}

void rubraview_extract_parked_free(rubraview_extract_parked_t *parked, size_t count) {
    for (size_t i = 0; parked && i < count; ++i) rubraview_free(parked[i].path);
    rubraview_free(parked);
}

/* RV-258: what to do with entry `index` when its name is taken: its own rule, or the job's. */
static rubraview_exists_rule_t rule_of(const rubraview_extract_job_t *job, size_t index) {
    if (job->only && job->rules)
        for (size_t i = 0; i < job->only_count; ++i) if (job->only[i] == index) return (rubraview_exists_rule_t)job->rules[i];
    return job->exists;
}

/* RV-258: whether `a` (`size` bytes, just written) and `b` hold the same bytes. An empty file maps to nothing:
   the one there is the same when it maps to nothing or to no bytes either. */
static bool same_bytes(proven_u8str_view_t a, proven_u8str_view_t b, uint64_t size) {
    rubraview_fs_mapping_t ma = {0}, mb = {0};
    if (size == 0) {
        bool empty = !rubraview_pal_fs_map(b, &mb) || mb.size == 0;
        if (mb.os) rubraview_pal_fs_unmap(&mb);
        return empty;
    }
    bool same = rubraview_pal_fs_map(a, &ma) && rubraview_pal_fs_map(b, &mb) && ma.size == mb.size;
    for (uint64_t at = 0; same && at < ma.size; at += 1u << 30) {
        uint64_t n = ma.size - at < (1u << 30) ? ma.size - at : (1u << 30);
        same = proven_sys_mem_cmp(ma.data + at, mb.data + at, (size_t)n) == 0;
    }
    if (ma.os) rubraview_pal_fs_unmap(&ma);
    if (mb.os) rubraview_pal_fs_unmap(&mb);
    return same;
}

/* The entry's name as it goes under the root: `inner` left off, each part made safe, the root's separator between. */
static bool place_of(const rubraview_extract_job_t *job, const char *name, char sep, char *out, size_t cap) {
    size_t skip = 0;
    if (job->only && job->inner && job->inner[0]) {
        size_t k = proven_cstr_len(job->inner);
        if (rubraview_cstr_cmp_n(name, rubraview_bytes_view(job->inner, k)) == 0) skip = k;
    }
    const char *p = name + skip;
    size_t n = 0;
    out[0] = '\0';
    while (*p) {
        const char *end = rubraview_cstr_chr(p, '/');
        size_t len = end ? (size_t)(end - p) : proven_cstr_len(p);
        if (len > 0) {
            if (n > 0) { if (n + 1 >= cap) return false; out[n++] = sep; }
            char part[512];
            size_t m = rubraview_archive_safe_part(rubraview_bytes_view(p, len), part, sizeof part);
            if (n + m + 1 >= cap) return false;
            proven_sys_mem_copy(out + n, part, m);
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
        if (job->only && job->inner && rubraview_cstr_cmp_n(name, rubraview_bytes_view(job->inner, proven_cstr_len(job->inner))) == 0) name += proven_cstr_len(job->inner);
        const char *slash = rubraview_cstr_chr(name, '/');
        size_t len = slash ? (size_t)(slash - name) : proven_cstr_len(name);
        if (!(e->flags & FULTA_ARC_ENTRY_DIR)) files++;
        if (!slash && !(e->flags & FULTA_ARC_ENTRY_DIR)) foldered = false;   /* a file at the top */
        if (!top) { top = name; top_len = len; }
        else if (len != top_len || rubraview_cstr_cmp_n(name, rubraview_bytes_view(top, len)) != 0) return false;
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

/* ---- the tree ---- */

typedef struct { const char *password; } ix_ctx_t;

static fulta_arc_err_t ix_password(void *ctx, uint32_t attempt, const char **utf8_out) {
    ix_ctx_t *x = ctx;
    if (attempt > 0 || !x->password || !x->password[0]) return FULTA_ARC_ERR_PASSWORD_NEEDED;
    *utf8_out = x->password;
    return FULTA_ARC_OK;
}

rubraview_archive_result_t rubraview_archive_index_read(const char *archive, rubraview_codepage_t codepage,
                                                        const char *password, rubraview_archive_index_t *out) {
    if (!out) return RUBRAVIEW_ARCHIVE_CANNOT_READ;
    *out = (rubraview_archive_index_t){ 0 };
    fulta_arc_source_t src;
    fulta_arc_err_t err = fulta_arc_source_file(archive, &src);
    if (err != FULTA_ARC_OK) return RUBRAVIEW_ARCHIVE_CANNOT_READ;
    ix_ctx_t x = { password };
    fulta_arc_options_t opt = { .name = archive, .volume = ex_volume, .password = ix_password, .password_ctx = &x,
                                .codepage = rubraview_page_source_fulta_codepage(codepage),
                                .auto_codepage = rubraview_fulta_auto_codepage() };   /* RV-239: the machine's, not CP437 */
    fulta_arc_t *arc = NULL;
    err = fulta_arc_open(&src, &opt, &arc);
    if (err != FULTA_ARC_OK) return result_of(err);
    size_t n = fulta_arc_count(arc);
    out->items = n ? rubraview_alloc_zero(n, sizeof *out->items) : NULL;
    if (n && !out->items) { fulta_arc_close(arc); return RUBRAVIEW_ARCHIVE_CANNOT_READ; }
    for (size_t i = 0; i < n; ++i) {
        const fulta_arc_entry_t *e = fulta_arc_entry(arc, i);
        const char *name = e->name ? e->name : "";
        while (*name == '/') name++;
        size_t len = proven_cstr_len(name);
        while (len > 0 && name[len - 1] == '/') len--;
        rubraview_archive_item_t *it = &out->items[out->count++];
        it->name = rubraview_alloc(len + 1);
        if (!it->name) { rubraview_archive_index_free(out); fulta_arc_close(arc); return RUBRAVIEW_ARCHIVE_CANNOT_READ; }
        proven_sys_mem_copy(it->name, name, len);
        it->name[len] = '\0';
        it->size = e->size;
        it->mtime = (e->flags & FULTA_ARC_ENTRY_HAS_MTIME) ? e->mtime / 1000000000 : 0;
        it->is_dir = (e->flags & FULTA_ARC_ENTRY_DIR) != 0;
        it->encrypted = (e->flags & FULTA_ARC_ENTRY_ENCRYPTED) != 0;
    }
    fulta_arc_close(arc);
    return RUBRAVIEW_ARCHIVE_DONE;
}

void rubraview_archive_index_free(rubraview_archive_index_t *index) {
    if (!index) return;
    for (size_t i = 0; i < index->count; ++i) rubraview_free(index->items[i].name);
    rubraview_free(index->items);
    *index = (rubraview_archive_index_t){ 0 };
}

/* The part of `name` after the folder `inner` and its '/', when it is in it. */
static bool in_folder(const char *name, proven_u8str_view_t inner, const char **rest) {
    if (inner.size == 0) { *rest = name; return name[0] != '\0'; }
    if (rubraview_cstr_cmp_n(name, rubraview_bytes_view((const char *)inner.ptr, inner.size)) != 0 || name[inner.size] != '/' || !name[inner.size + 1]) return false;
    *rest = name + inner.size + 1;
    return true;
}

size_t rubraview_archive_index_level(const rubraview_archive_index_t *index, proven_u8str_view_t inner,
                                     rubraview_archive_child_t *out, size_t cap) {
    if (!index) return 0;
    while (inner.size > 0 && inner.ptr[inner.size - 1] == '/') inner.size--;
    size_t n = 0;
    /* the folders: named by an entry of their own or by what is in them */
    for (size_t i = 0; i < index->count; ++i) {
        const rubraview_archive_item_t *it = &index->items[i];
        const char *rest;
        if (!in_folder(it->name, inner, &rest)) continue;
        const char *slash = rubraview_cstr_chr(rest, '/');
        if (!slash && !it->is_dir) continue;
        size_t len = slash ? (size_t)(slash - rest) : proven_cstr_len(rest);
        size_t k = 0;
        while (k < n && !(out[k].name.size == len && proven_sys_mem_cmp(out[k].name.ptr, rest, len) == 0)) k++;
        if (k == n) {   /* a new one */
            if (n == cap) continue;
            out[n++] = (rubraview_archive_child_t){ .name = { .ptr = (const proven_byte_t *)rest, .size = len },
                                                    .is_dir = true, .item = RUBRAVIEW_ARCHIVE_NO_ITEM };
        }
        if (!slash) { out[k].item = i; if (it->mtime) out[k].mtime = it->mtime; }
        else if (!it->is_dir) out[k].size += it->size;
    }
    for (size_t i = 0; i < index->count; ++i) {
        const rubraview_archive_item_t *it = &index->items[i];
        const char *rest;
        if (it->is_dir || !in_folder(it->name, inner, &rest) || rubraview_cstr_chr(rest, '/')) continue;
        if (n == cap) break;
        out[n++] = (rubraview_archive_child_t){ .name = { .ptr = (const proven_byte_t *)rest, .size = proven_cstr_len(rest) },
                                                .is_dir = false, .item = i, .size = it->size, .mtime = it->mtime };
    }
    return n;
}

size_t rubraview_archive_index_under(const rubraview_archive_index_t *index, proven_u8str_view_t path, size_t *out, size_t cap) {
    if (!index) return 0;
    while (path.size > 0 && path.ptr[path.size - 1] == '/') path.size--;
    size_t n = 0;
    for (size_t i = 0; i < index->count; ++i) {
        const char *name = index->items[i].name;
        bool here = path.size == 0 ||
                    (rubraview_cstr_cmp_n(name, rubraview_bytes_view((const char *)path.ptr, path.size)) == 0 && (name[path.size] == '\0' || name[path.size] == '/'));
        if (!here) continue;
        if (n < cap && out) out[n] = i;
        n++;
    }
    return n;
}

void rubraview_extract_run(rubraview_extract_job_t *job) {
    if (!job) return;
    job->result = RUBRAVIEW_ARCHIVE_DONE;
    job->written = job->skipped = job->failed = job->same = 0;
    job->parked = NULL;
    job->parked_count = 0;
    size_t parked_cap = 0;
    job->why[0] = job->root[0] = '\0';
    fulta_arc_t *arc = NULL;
    fulta_arc_err_t err = open_archive(job->archive, job->codepage, job, &arc);
    if (err != FULTA_ARC_OK) { job->result = result_of(err); say(job->why, sizeof job->why, fulta_arc_strerror(err), job->archive); return; }

    char sep = separator_of(job->dest);
    size_t dn = proven_cstr_len(job->dest);
    while (dn > 1 && (job->dest[dn - 1] == '/' || job->dest[dn - 1] == '\\')) dn--;
    bool own_folder = job->mode == RUBRAVIEW_EXTRACT_FOLDER || (job->mode == RUBRAVIEW_EXTRACT_SMART && !one_top(arc, job));
    if (own_folder) {
        char stem[512], want[RUBRAVIEW_ARCHIVE_PATH];
        rubraview_archive_stem(view(job->archive), stem, sizeof stem);
        rubraview_fmt(want, sizeof want, "{}{}{}", PROVEN_ARG(rubraview_bytes_view(job->dest, (int)dn)), PROVEN_ARG((char)sep), PROVEN_ARG(stem));
        /* a file or a folder of that name there already: book-1, so nothing is mixed into what was there */
        if (!rubraview_archive_free_folder_name(view(want), exists_fn, job->root, sizeof job->root)) rubraview_fmt(job->root, sizeof job->root, "{}", PROVEN_ARG(want));
    } else {
        rubraview_fmt(job->root, sizeof job->root, "{}", PROVEN_ARG(rubraview_bytes_view(job->dest, (int)dn)));
    }

    uint64_t total = 0;
    for (size_t i = 0; i < fulta_arc_count(arc); ++i) if (picked(job, i)) total += fulta_arc_entry(arc, i)->size;
    atomic_store(&job->total, total);
    atomic_store(&job->done, 0);
    bool made_root = !rubraview_pal_fs_exists(view(job->root));   /* taken back if nothing comes of it */
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
        int w = rubraview_fmt(path, sizeof path, "{}{}{}", PROVEN_ARG(job->root), PROVEN_ARG((char)sep), PROVEN_ARG(rel));
        if (w <= 0 || (size_t)w >= sizeof path) { job->failed++; say(job->why, sizeof job->why, T("the name is too long"), e->name); continue; }
        if (e->flags & FULTA_ARC_ENTRY_DIR) { (void)rubraview_pal_fs_make_dirs(view(path)); continue; }
        if (e->flags & FULTA_ARC_ENTRY_SYMLINK) { job->skipped++; continue; }   /* a link is not followed out of the folder */
        /* its folder */
        char *last = rubraview_cstr_rchr(path, sep);
        if (last) { *last = '\0'; (void)rubraview_pal_fs_make_dirs(view(path)); *last = sep; }
        proven_u8str_view_t pv = view(path);
        bool there = rubraview_pal_fs_exists(pv);
        rubraview_exists_rule_t rule = rule_of(job, i);
        if (there && rule == RUBRAVIEW_EXISTS_PARK) {   /* RV-258: kept to be asked */
            if (job->parked_count == parked_cap) {
                size_t cap = parked_cap ? parked_cap * 2 : 16;
                rubraview_extract_parked_t *grown = (rubraview_extract_parked_t *)rubraview_alloc(cap * sizeof *grown);
                if (!grown) { job->failed++; continue; }
                if (job->parked) proven_sys_mem_copy(grown, job->parked, job->parked_count * sizeof *grown);
                rubraview_free(job->parked);
                job->parked = grown;
                parked_cap = cap;
            }
            job->parked[job->parked_count++] = (rubraview_extract_parked_t){
                .index = i, .size = e->size, .mtime = (e->flags & FULTA_ARC_ENTRY_HAS_MTIME) ? e->mtime / 1000000000 : 0,
                .path = rubraview_strdup(path) };
            atomic_fetch_add(&job->done, e->size);
            continue;
        }
        if (there && rule == RUBRAVIEW_EXISTS_SKIP) { job->skipped++; atomic_fetch_add(&job->done, e->size); continue; }
        if (there && (rule == RUBRAVIEW_EXISTS_NUMBER || rule == RUBRAVIEW_EXISTS_SAME)) {
            if (!rubraview_archive_free_name(pv, exists_fn, final, sizeof final)) { job->failed++; continue; }
        } else {
            rubraview_fmt(final, sizeof final, "{}", PROVEN_ARG(path));
        }
        rubraview_fmt(part, sizeof part, "{}.rvpart", PROVEN_ARG(final));
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
        if (closed && there && rule == RUBRAVIEW_EXISTS_SAME && same_bytes(view(part), pv, e->size)) {   /* RV-258: the one there is it */
            (void)rubraview_pal_fs_delete(view(part));
            job->skipped++;
            job->same++;
            continue;
        }
        bool placed = closed && (there && rule == RUBRAVIEW_EXISTS_REPLACE ? rubraview_pal_fs_replace(view(part), view(final))
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
    if (made_root && job->written == 0 && job->result != RUBRAVIEW_ARCHIVE_DONE) {
        (void)rubraview_pal_fs_delete_tree(view(job->root));   /* a wrong password or a stop leaves no empty folder */
        job->root[0] = '\0';
    }
    if (job->result == RUBRAVIEW_ARCHIVE_DONE) {
        if (job->failed > 0) job->result = RUBRAVIEW_ARCHIVE_PARTLY;
        else if (job->written == 0 && job->skipped == 0 && job->parked_count == 0) job->result = RUBRAVIEW_ARCHIVE_NOTHING;
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

static char *dup_n(proven_u8str_view_t s_view) {
    const char *s = (const char *)s_view.ptr;
    size_t n = (size_t)s_view.size;
    char *d = rubraview_alloc(n + 1);
    if (d) { proven_sys_mem_copy(d, s, n); d[n] = '\0'; }
    return d;
}

static void list_add(pack_list_t *l, const char *name, proven_u8str_view_t path_view, int64_t mtime, bool dir) {
    const char *path = (const char *)path_view.ptr;
    uint64_t size = (uint64_t)path_view.size;
    if (l->failed) return;
    if (l->count == l->cap) {
        size_t cap = l->cap ? l->cap * 2 : 64;
        pack_item_t *more = rubraview_realloc(rubraview_mem_mut(l->items, cap * sizeof *more));
        if (!more) { l->failed = true; return; }
        l->items = more;
        l->cap = cap;
    }
    pack_item_t it = { .name = dup_n(rubraview_bytes_view(name, proven_cstr_len(name))), .path = dup_n(rubraview_bytes_view(path, proven_cstr_len(path))), .size = size, .mtime = mtime, .dir = dir };
    if (!it.name || !it.path) { rubraview_free(it.name); rubraview_free(it.path); l->failed = true; return; }
    l->items[l->count++] = it;
}

static void list_free(pack_list_t *l) {
    for (size_t i = 0; i < l->count; ++i) { rubraview_free(l->items[i].name); rubraview_free(l->items[i].path); }
    rubraview_free(l->items);
    *l = (pack_list_t){ 0 };
}

#define LIST_ARENA (16u << 20)

static void walk(pack_list_t *l, const char *name, const char *path, rubraview_compress_job_t *job, int depth) {
    if (depth > 64 || l->failed || atomic_load(&job->cancel)) return;
    void *mem = rubraview_alloc(LIST_ARENA);
    if (!mem) { l->failed = true; return; }
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = mem, .size = LIST_ARENA });
    rubraview_fs_listing_t listing = rubraview_pal_fs_list_dir(&arena, view(path));
    char sep = separator_of(path);
    for (size_t i = 0; i < listing.count && !l->failed; ++i) {
        const rubraview_fs_entry_t *e = &listing.entries[i];
        char child_name[RUBRAVIEW_ARCHIVE_PATH], child_path[RUBRAVIEW_ARCHIVE_PATH];
        int a = rubraview_fmt(child_name, sizeof child_name, "{}/{}{}", PROVEN_ARG(name), PROVEN_ARG(rubraview_bytes_view((const char *)e->name.ptr, (int)e->name.size)), PROVEN_ARG(e->is_directory ? "/" : ""));
        int b = rubraview_fmt(child_path, sizeof child_path, "{}{}{}", PROVEN_ARG(path), PROVEN_ARG((char)sep), PROVEN_ARG(rubraview_bytes_view((const char *)e->name.ptr, (int)e->name.size)));
        if (a <= 0 || b <= 0 || (size_t)a >= sizeof child_name || (size_t)b >= sizeof child_path) continue;
        list_add(l, child_name, rubraview_bytes_view(child_path, e->size_bytes), e->mtime, e->is_directory);
        if (e->is_directory) {
            child_name[a - 1] = '\0';   /* without its '/' */
            walk(l, child_name, child_path, job, depth + 1);
        }
    }
    rubraview_free(mem);
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
    if (n) proven_sys_mem_copy(buf, r->map.data + r->at, n);
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
        uint8_t *more = rubraview_realloc(rubraview_mem_mut(m->data, cap));
        if (!more) return FULTA_ARC_ERR_NOMEM;
        m->data = more;
        m->cap = cap;
    }
    if (n) proven_sys_mem_copy(m->data + m->size, data, n);
    m->size += n;
    return FULTA_ARC_OK;
}

static fulta_arc_err_t mem_patch(void *ctx, uint64_t offset, const void *data, size_t n) {
    mem_sink_t *m = ctx;
    if (offset > m->size || n > m->size - offset) return FULTA_ARC_ERR_IO;
    proven_sys_mem_copy(m->data + offset, data, n);
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
    if (c->at + n > c->size || proven_sys_mem_cmp(c->src + c->at, data, n) != 0) { c->same = false; return FULTA_ARC_ERR_CHECKSUM; }
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
    return rubraview_cstr_cmp(((const pack_item_t *)a)->name, ((const pack_item_t *)b)->name);
}

/* The archive at `path` holds exactly `list`'s files and folders, byte for byte. */
static bool check_written(const char *path, pack_list_t *list, rubraview_compress_job_t *job) {
    fulta_arc_options_t opt = { .name = path, .password = cmp_password, .password_ctx = (void *)job->password,
                                .codepage = FULTA_ARC_CP_UTF8 };
    fulta_arc_t *arc = NULL;
    if (fulta_arc_open_file(path, &opt, &arc) != FULTA_ARC_OK) return false;   /* a split set's volumes found by name */
    bool ok = fulta_arc_count(arc) == list->count;
    qsort(list->items, list->count, sizeof list->items[0], cmp_order);
    for (size_t i = 0; ok && i < fulta_arc_count(arc); ++i) {
        const fulta_arc_entry_t *e = fulta_arc_entry(arc, i);
        size_t n = proven_cstr_len(e->name);
        while (n > 0 && e->name[n - 1] == '/') n--;
        pack_item_t key = { .name = dup_n(rubraview_bytes_view(e->name, n)) };
        if (!key.name) { ok = false; break; }
        /* the list's folders end in '/'; look both ways */
        pack_item_t *hit = bsearch(&key, list->items, list->count, sizeof list->items[0], cmp_order);
        if (!hit) {
            char *slashed = rubraview_alloc(n + 2);
            if (slashed) { proven_sys_mem_copy(slashed, e->name, n); slashed[n] = '/'; slashed[n + 1] = '\0'; }
            pack_item_t k2 = { .name = slashed };
            hit = slashed ? bsearch(&k2, list->items, list->count, sizeof list->items[0], cmp_order) : NULL;
            rubraview_free(slashed);
        }
        rubraview_free(key.name);
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

/* D-260: the Compress window's choices */
static const rubraview_pack_method_t ZIP_METHODS[] = { RUBRAVIEW_PACK_METHOD_DEFLATE, RUBRAVIEW_PACK_METHOD_BZIP2, RUBRAVIEW_PACK_METHOD_LZMA,
                                                       RUBRAVIEW_PACK_METHOD_PPMD, RUBRAVIEW_PACK_METHOD_ZSTD };
static const rubraview_pack_method_t SZ_METHODS[] = { RUBRAVIEW_PACK_METHOD_LZMA2, RUBRAVIEW_PACK_METHOD_LZMA, RUBRAVIEW_PACK_METHOD_PPMD,
                                                      RUBRAVIEW_PACK_METHOD_BZIP2, RUBRAVIEW_PACK_METHOD_DEFLATE };

size_t rubraview_pack_methods(rubraview_pack_format_t format, rubraview_pack_method_t *out, size_t cap) {
    const rubraview_pack_method_t *m = format == RUBRAVIEW_PACK_7Z ? SZ_METHODS : ZIP_METHODS;
    size_t n = format == RUBRAVIEW_PACK_7Z ? sizeof SZ_METHODS / sizeof *SZ_METHODS : sizeof ZIP_METHODS / sizeof *ZIP_METHODS;
    for (size_t i = 0; out && i < n && i < cap; ++i) out[i] = m[i];
    return n;
}

bool rubraview_pack_method_fits(rubraview_pack_format_t format, rubraview_pack_method_t method) {
    if (method == RUBRAVIEW_PACK_METHOD_DEFAULT) return true;
    rubraview_pack_method_t m[8];
    size_t n = rubraview_pack_methods(format, m, 8);
    for (size_t i = 0; i < n; ++i) if (m[i] == method) return true;
    return false;
}

bool rubraview_pack_method_ready(rubraview_pack_format_t format, rubraview_pack_method_t method) {
    return rubraview_pack_method_fits(format, method);   /* FultaArc 1bc526c writes them all, ZIP's too (D-263) */
}

bool rubraview_pack_method_plain(rubraview_pack_format_t format, rubraview_pack_method_t method) {
    return format == RUBRAVIEW_PACK_ZIP && (method == RUBRAVIEW_PACK_METHOD_DEFAULT || method == RUBRAVIEW_PACK_METHOD_DEFLATE);
}

int rubraview_pack_level_of(int step) {
    static const int LEVELS[RUBRAVIEW_PACK_LEVEL_STEPS] = { 0, 1, 3, 5, 7, 9 };
    return LEVELS[step >= 0 && step < RUBRAVIEW_PACK_LEVEL_STEPS ? step : RUBRAVIEW_PACK_LEVEL_NORMAL];
}

bool rubraview_pack_split_parse(proven_u8str_view_t text, uint64_t *out_bytes) {
    if (out_bytes) *out_bytes = 0;
    size_t i = 0, n = text.size;
    const proven_byte_t *s = text.ptr;
    uint64_t whole = 0, frac = 0, frac_div = 1;
    bool any = false, point = false;
    for (; i < n; ++i) {
        char c = (char)s[i];
        if (c == ',' || c == ' ') continue;
        if (c == '.' && !point) { point = true; continue; }
        if (c < '0' || c > '9') break;
        any = true;
        if (point) {
            if (frac_div < 1000000u) { frac = frac * 10u + (uint64_t)(c - '0'); frac_div *= 10u; }
        } else {
            if (whole > (UINT64_C(1) << 62) / 10u) return false;
            whole = whole * 10u + (uint64_t)(c - '0');
        }
    }
    int shift = 0;
    if (i < n) {
        switch (rubraview_ascii_tolower((unsigned char)s[i])) {
            case 'k': shift = 10; break;
            case 'm': shift = 20; break;
            case 'g': shift = 30; break;
            case 't': shift = 40; break;
            case 'p': shift = 50; break;
            default:  shift = -1; break;
        }
        if (shift > 0) ++i;
        else shift = 0;
        if (shift > 0 && i < n && rubraview_ascii_tolower((unsigned char)s[i]) == 'i') ++i;
        if (i < n && rubraview_ascii_tolower((unsigned char)s[i]) == 'b') ++i;
        while (i < n && s[i] == ' ') ++i;
        if (i < n) return false;
    }
    if (!any) {
        for (size_t k = 0; k < n; ++k) if (s[k] != ' ') return false;   /* empty: no split */
        return true;
    }
    if (whole > ((UINT64_C(1) << 62) >> shift)) return false;
    uint64_t bytes = (whole << shift) + (((frac << shift) + frac_div / 2u) / frac_div);
    if (bytes > (UINT64_C(1) << 62)) return false;
    if (out_bytes) *out_bytes = bytes;
    return true;
}

size_t rubraview_pack_split_text(char *buf, size_t cap, uint64_t bytes) {
    if (!buf || cap == 0) return 0;
    buf[0] = '\0';
    if (bytes == 0) return 0;
    static const char UNIT[] = "KMGTP";
    int k = 5;
    while (k > 0 && (bytes & ((UINT64_C(1) << (10 * k)) - 1u)) != 0) --k;
    int n = k > 0 ? rubraview_fmt(buf, cap, "{}{}", PROVEN_ARG(bytes >> (10 * k)), PROVEN_ARG(rubraview_bytes_view(&UNIT[k - 1], 1)))
                  : rubraview_fmt(buf, cap, "{}", PROVEN_ARG(bytes));
    return n > 0 ? ((size_t)n < cap ? (size_t)n : cap - 1) : 0;
}

bool rubraview_pack_split_supported(rubraview_pack_format_t format) {
    (void)format;
    return true;   /* D-263: FultaArc 1bc526c writes 7z's name.7z.001... and ZIP's name.z01...name.zip */
}

bool rubraview_pack_names_hidden_supported(rubraview_pack_format_t format) {
    return format == RUBRAVIEW_PACK_7Z;   /* D-263: FultaArc's encrypt_names; ZIP has no standard way */
}

static bool plain_byte(proven_byte_t c) { return c >= 0x21 && c <= 0x7E; }

bool rubraview_pack_password_plain(proven_u8str_view_t password) {
    for (size_t i = 0; i < password.size; ++i) if (!plain_byte(password.ptr[i])) return false;
    return true;
}

size_t rubraview_password_keep_plain(proven_u8str_view_t in, char *out, size_t cap, bool *dropped) {
    size_t n = 0;
    bool lost = false;
    for (size_t i = 0; i < in.size; ++i) {
        if (plain_byte(in.ptr[i]) && n + 1 < cap) out[n++] = (char)in.ptr[i];
        else lost = true;
    }
    if (cap) out[n] = '\0';
    if (dropped) *dropped = lost;
    return n;
}

static fulta_arc_method_t fulta_method(rubraview_pack_method_t m) {
    switch (m) {
        case RUBRAVIEW_PACK_METHOD_DEFLATE: return FULTA_ARC_METHOD_DEFLATE;
        case RUBRAVIEW_PACK_METHOD_BZIP2:   return FULTA_ARC_METHOD_BZIP2;
        case RUBRAVIEW_PACK_METHOD_LZMA:    return FULTA_ARC_METHOD_LZMA;
        case RUBRAVIEW_PACK_METHOD_LZMA2:   return FULTA_ARC_METHOD_LZMA2;
        case RUBRAVIEW_PACK_METHOD_PPMD:    return FULTA_ARC_METHOD_PPMD;
        case RUBRAVIEW_PACK_METHOD_ZSTD:    return FULTA_ARC_METHOD_ZSTD;
        default:                            return FULTA_ARC_METHOD_DEFAULT;
    }
}

void rubraview_compress_run(rubraview_compress_job_t *job) {
    if (!job) return;
    job->result = RUBRAVIEW_ARCHIVE_DONE;
    if (job->volume_bytes > 0 && !rubraview_pack_split_supported(job->format)) {
        job->result = RUBRAVIEW_ARCHIVE_CANNOT_WRITE;
        say(job->why, sizeof job->why, T("split volumes are not written yet"), "");
        return;
    }
    if (job->password && !rubraview_pack_password_plain(proven_u8str_view_from_cstr(job->password))) {
        job->result = RUBRAVIEW_ARCHIVE_CANNOT_WRITE;
        say(job->why, sizeof job->why, T("a new archive's password takes English letters, digits and symbols only"), "");
        return;
    }
    if (job->names_hidden && !rubraview_pack_names_hidden_supported(job->format)) {
        job->result = RUBRAVIEW_ARCHIVE_CANNOT_WRITE;
        say(job->why, sizeof job->why, T("hidden names are not written yet"), "");
        return;
    }
    if (!rubraview_pack_method_ready(job->format, job->method)) {
        job->result = RUBRAVIEW_ARCHIVE_CANNOT_WRITE;
        say(job->why, sizeof job->why, rubraview_pack_method_fits(job->format, job->method) ? T("that codec is not written in ZIP yet")
                                                                                         : T("that codec does not go in this format"), "");
        return;
    }
    job->files = job->folders = 0;
    job->why[0] = job->written[0] = job->set_name[0] = '\0';
    job->older_parts = false;
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
        size_t n = proven_cstr_len(path);
        while (n > 1 && (path[n - 1] == '/' || path[n - 1] == '\\')) n--;
        size_t from = 0;
        for (size_t i = 0; i < n; ++i) if (path[i] == '/' || path[i] == '\\') from = i + 1;
        char base[512];
        rubraview_archive_safe_part(rubraview_bytes_view(path + from, n - from), base, sizeof base);
        if (st.is_directory) {
            char slashed[520];
            rubraview_fmt(slashed, sizeof slashed, "{}/", PROVEN_ARG(base));
            list_add(&list, slashed, rubraview_bytes_view(path, 0), st.mtime, true);
            walk(&list, base, path, job, 0);
        } else {
            list_add(&list, base, rubraview_bytes_view(path, st.size_bytes), st.mtime, false);
        }
    }
    if (list.failed) { job->result = RUBRAVIEW_ARCHIVE_CANNOT_READ; say(job->why, sizeof job->why, T("not enough memory"), ""); list_free(&list); return; }
    if (atomic_load(&job->cancel)) { job->result = RUBRAVIEW_ARCHIVE_CANCELLED; list_free(&list); return; }
    if (list.count == 0) { job->result = RUBRAVIEW_ARCHIVE_NOTHING; list_free(&list); return; }

    /* two sources of one name would be one entry twice: the second is numbered */
    for (size_t i = 0; i < list.count; ++i) {
        for (size_t j = 0; j < i; ++j) {
            if (rubraview_cstr_cmp(list.items[i].name, list.items[j].name) != 0) continue;
            char renamed[RUBRAVIEW_ARCHIVE_PATH];
            rubraview_fmt(renamed, sizeof renamed, "{}-{}", PROVEN_ARG(list.items[i].name), PROVEN_ARG((size_t)i));
            rubraview_free(list.items[i].name);
            list.items[i].name = dup_n(rubraview_bytes_view(renamed, proven_cstr_len(renamed)));
            break;
        }
    }

    uint64_t total = 0;
    for (size_t i = 0; i < list.count; ++i) {
        if (list.items[i].dir) job->folders++;
        else { job->files++; total += list.items[i].size; }
    }
    bool split = job->volume_bytes > 0;
    g_set_format = job->format;
    if (!rubraview_archive_free_name(view(job->output), split ? set_exists_fn : exists_fn, job->written, sizeof job->written)) {
        job->result = RUBRAVIEW_ARCHIVE_CANNOT_WRITE; list_free(&list); return;
    }

    /* An empty archive of the other kind to begin from: FultaArc's edit then takes its convert route, which streams
       each file from the disk as it packs it (its copy route would hold a new file whole in memory). */
    static const uint8_t EMPTY_ZIP[22] = { 'P', 'K', 5, 6 };
    mem_sink_t empty7z = { 0 };
    fulta_arc_write_sink_t ms = { .ctx = &empty7z, .write = mem_write, .patch = mem_patch };
    fulta_arc_t *base = NULL;
    fulta_arc_edit_t *edit = NULL;
    pack_reader_t *readers = rubraview_alloc_zero(list.count, sizeof *readers);
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
        /* level 0 stores, whatever the codec */
        .method = job->level <= 0 ? FULTA_ARC_METHOD_DEFAULT : fulta_method(job->method),
        .volume_size = split ? (job->volume_bytes < FULTA_ARC_VOLUME_MIN ? FULTA_ARC_VOLUME_MIN : job->volume_bytes) : 0,
        .encrypt_names = secret && job->names_hidden && job->format == RUBRAVIEW_PACK_7Z,
        .solid = job->format == RUBRAVIEW_PACK_7Z && job->solid_off ? FULTA_ARC_SOLID_OFF : FULTA_ARC_SOLID_DEFAULT,
    };
    fulta_arc_format_t format = job->format == RUBRAVIEW_PACK_7Z ? FULTA_ARC_FORMAT_7Z : FULTA_ARC_FORMAT_ZIP;
    if (err == FULTA_ARC_OK) err = fulta_arc_edit_write_file(edit, format, job->written, &wo);
    for (size_t i = 0; readers && i < list.count; ++i) {
        if (readers[i].mapped) rubraview_pal_fs_unmap(&readers[i].map);
        if (readers[i].failed && err != FULTA_ARC_OK) say(job->why, sizeof job->why, T("it could not be read"), list.items[i].path);
    }
    rubraview_free(readers);
    if (edit) fulta_arc_edit_free(edit);
    if (base) fulta_arc_close(base);
    rubraview_free(empty7z.data);
    if (err != FULTA_ARC_OK) {
        (void)rubraview_pal_fs_delete(view(job->written));   /* FultaArc renames only a whole one over it; nothing of ours stays */
        job->result = err == FULTA_ARC_ERR_CANCELLED || atomic_load(&job->cancel) ? RUBRAVIEW_ARCHIVE_CANCELLED
                    : job->why[0] ? RUBRAVIEW_ARCHIVE_CANNOT_READ : RUBRAVIEW_ARCHIVE_CANNOT_WRITE;
        job->older_parts = err == FULTA_ARC_ERR_EXISTS;
        if (split) rubraview_fmt(job->set_name, sizeof job->set_name, "{}", PROVEN_ARG(job->written));
        if (err == FULTA_ARC_ERR_EXISTS) say(job->why, sizeof job->why, T("a later part of an older split set of that name is there: move it away first"), job->written);
        if (!job->why[0] && job->result == RUBRAVIEW_ARCHIVE_CANNOT_WRITE) say(job->why, sizeof job->why, fulta_arc_strerror(err), job->written);
        job->written[0] = '\0';
        list_free(&list);
        return;
    }

    /* read back and compared before it is kept; a split set opened as Rubraview opens it (D-263): a 7z's first volume,
       a ZIP's own name */
    char set[RUBRAVIEW_ARCHIVE_PATH];
    rubraview_fmt(set, sizeof set, "{}", PROVEN_ARG(job->written));
    if (split && job->format == RUBRAVIEW_PACK_7Z && !volume_name(job->format, set, 0, job->written, sizeof job->written))
        rubraview_fmt(job->written, sizeof job->written, "{}", PROVEN_ARG(set));
    atomic_store(&job->phase, 2);
    atomic_store(&job->done, 0);
    if (!check_written(job->written, &list, job)) {
        if (split) delete_set(job->format, set);
        else (void)rubraview_pal_fs_delete(view(job->written));
        job->result = atomic_load(&job->cancel) ? RUBRAVIEW_ARCHIVE_CANCELLED : RUBRAVIEW_ARCHIVE_CHECK_FAILED;
        job->written[0] = '\0';
    }
    list_free(&list);
}
