#include "rubraview/arcedit.h"
#include "rubraview/path.h"
#include "fulta/arc.h"
#include "rubraview/pal/pal_fs.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

/* ---- which way ---- */

rubraview_arcedit_route_t rubraview_arcedit_route(const rubraview_page_source_t *source) {
    if (!source || source->kind == RUBRAVIEW_PAGE_SOURCE_FOLDER || !source->arc) return RUBRAVIEW_ARCEDIT_NOT_AN_ARCHIVE;
    /* Asked first and apart from FultaArc's answer: an archive opened
       with a password is still one whose pages are not to be changed. */
    if (rubraview_page_source_is_encrypted(source)) return RUBRAVIEW_ARCEDIT_ENCRYPTED;
    switch (fulta_arc_editable(source->arc)) {
        case FULTA_ARC_EDIT_OK:        return RUBRAVIEW_ARCEDIT_IN_PLACE;
        case FULTA_ARC_EDIT_ENCRYPTED: return RUBRAVIEW_ARCEDIT_ENCRYPTED;
        default:                       return RUBRAVIEW_ARCEDIT_CONVERT;
    }
}

/* ---- names ---- */

static char lower(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

static bool ends_with(u8str_t s, const char *tail) {
    size_t n = strlen(tail);
    if (s.len < n) return false;
    for (size_t i = 0; i < n; ++i) if (lower(s.ptr[s.len - n + i]) != tail[i]) return false;
    return true;
}

static bool all_digits(const char *p, size_t n) {
    if (n == 0) return false;
    for (size_t i = 0; i < n; ++i) if (p[i] < '0' || p[i] > '9') return false;
    return true;
}

/* The name without its extension and without a set's volume mark. */
static u8str_t book_stem(u8str_t name, bool *out_comic) {
    *out_comic = ends_with(name, ".cbr") || ends_with(name, ".cbz") || ends_with(name, ".cb7");
    u8str_t s = name;
    /* x.7z.001, x.zip.001: the number, then the extension below. */
    size_t dot = s.len;
    while (dot > 0 && s.ptr[dot - 1] != '.') dot--;
    if (dot > 0 && all_digits(s.ptr + dot, s.len - dot) && s.len - dot >= 2) {
        u8str_t inner = { .ptr = s.ptr, .len = dot - 1 };
        if (ends_with(inner, ".7z") || ends_with(inner, ".zip")) s = inner;
    }
    /* the extension */
    dot = s.len;
    while (dot > 0 && s.ptr[dot - 1] != '.') dot--;
    if (dot > 1) s.len = dot - 1;
    /* x.part1(.rar), x.vol1(.egg) */
    dot = s.len;
    while (dot > 0 && s.ptr[dot - 1] != '.') dot--;
    if (dot > 1) {
        const char *mark = s.ptr + dot;
        size_t n = s.len - dot;
        bool part = n > 4 && lower(mark[0]) == 'p' && lower(mark[1]) == 'a' && lower(mark[2]) == 'r' && lower(mark[3]) == 't' &&
                    all_digits(mark + 4, n - 4);
        bool vol = n > 3 && lower(mark[0]) == 'v' && lower(mark[1]) == 'o' && lower(mark[2]) == 'l' && all_digits(mark + 3, n - 3);
        if (part || vol) s.len = dot - 1;
    }
    return s;
}

size_t rubraview_arcedit_converted_name(u8str_t archive_name, rubraview_arcedit_target_t target, char *dst, size_t cap) {
    if (archive_name.len == 0 || !dst) return 0;
    bool comic = false;
    u8str_t stem = book_stem(archive_name, &comic);
    if (stem.len == 0) stem = archive_name;
    bool zip = target == RUBRAVIEW_ARCEDIT_ZIP;
    if (target == RUBRAVIEW_ARCEDIT_SAME) zip = !(ends_with(archive_name, ".7z") || ends_with(archive_name, ".cb7"));
    const char *ext = comic ? (zip ? ".cbz" : ".cb7") : (zip ? ".zip" : ".7z");
    size_t need = stem.len + strlen(ext);
    if (need + 1 > cap) return 0;
    memcpy(dst, stem.ptr, stem.len);
    memcpy(dst + stem.len, ext, strlen(ext) + 1);
    return need;
}

size_t rubraview_arcedit_sibling_name(u8str_t entry_name, u8str_t new_base, char *dst, size_t cap) {
    if (new_base.len == 0 || !dst) return 0;
    size_t dir = entry_name.len;
    while (dir > 0 && entry_name.ptr[dir - 1] != '/' && entry_name.ptr[dir - 1] != '\\') dir--;
    size_t need = dir + new_base.len;
    if (need + 1 > cap) return 0;
    for (size_t i = 0; i < dir; ++i) dst[i] = entry_name.ptr[i] == '\\' ? '/' : entry_name.ptr[i];
    memcpy(dst + dir, new_base.ptr, new_base.len);
    dst[need] = '\0';
    return need;
}

const char *rubraview_arcedit_result_text(rubraview_arcedit_result_t result) {
    static const char *const TEXT[RUBRAVIEW_ARCEDIT_RESULT_COUNT] = {
        "the archive was written again",
        "stopped: the archive is as it was",
        "this archive is encrypted: its pages cannot be changed",
        "not changed: the archive could not be read",
        "not changed: the new archive could not be written",
        "not changed: the archive already holds that name",
        "not changed: that name cannot be stored",
        "not changed: the new archive did not check out",
        "not changed: not enough memory",
    };
    return (int)result >= 0 && result < RUBRAVIEW_ARCEDIT_RESULT_COUNT ? TEXT[result] : "";
}

/* ---- the run ---- */

typedef struct run {
    rubraview_arcedit_job_t *job;
    size_t at;                 /* how much of `data` FultaArc has pulled */
} run_t;

static bool job_note_volume(rubraview_arcedit_job_t *job, const char *path) {
    char **more = (char**)realloc(job->volumes, (job->volume_count + 1) * sizeof(char*));
    if (!more) return false;
    job->volumes = more;
    size_t n = strlen(path);
    char *copy = (char*)malloc(n + 1);
    if (!copy) return false;
    memcpy(copy, path, n + 1);
    job->volumes[job->volume_count++] = copy;
    return true;
}

static fulta_arc_err_t run_volume(void *ctx, uint32_t index, const char *name, fulta_arc_source_t *out) {
    rubraview_arcedit_job_t *job = (rubraview_arcedit_job_t*)ctx;
    (void)index;
    if (!name || !name[0]) return FULTA_ARC_ERR_VOLUME_MISSING;
    fulta_arc_err_t err = fulta_arc_source_file(name, out);
    if (err != FULTA_ARC_OK) return FULTA_ARC_ERR_VOLUME_MISSING;
    if (job && !job_note_volume(job, name)) {
        if (out->close) out->close(out->ctx);
        return FULTA_ARC_ERR_NOMEM;
    }
    return FULTA_ARC_OK;
}

static int run_cancelled(void *ctx) {
    return atomic_load(&((rubraview_arcedit_job_t*)ctx)->cancel) ? 1 : 0;
}

static int run_progress(void *ctx, uint64_t done, uint64_t total) {
    rubraview_arcedit_job_t *job = (rubraview_arcedit_job_t*)ctx;
    atomic_store(&job->done, done);
    atomic_store(&job->total, total);
    return atomic_load(&job->cancel) ? 1 : 0;
}

static fulta_arc_err_t run_read(void *ctx, void *buf, size_t cap, size_t *got) {
    run_t *run = (run_t*)ctx;
    size_t left = run->job->size - run->at;
    size_t n = left < cap ? left : cap;
    if (n) memcpy(buf, run->job->data + run->at, n);
    run->at += n;
    *got = n;
    return FULTA_ARC_OK;
}

/* The archive at `path`, read with the viewer's reading of names; its
   later volumes are noted in `job` when one is given. */
static fulta_arc_err_t open_at(const char *path, rubraview_codepage_t codepage, rubraview_arcedit_job_t *job,
                               rubraview_arcedit_job_t *cancel_of, fulta_arc_t **out) {
    fulta_arc_source_t src;
    fulta_arc_err_t err = fulta_arc_source_file(path, &src);
    if (err != FULTA_ARC_OK) return err;
    fulta_arc_options_t opt = {
        .name = path,
        .volume = run_volume, .volume_ctx = job,
        .codepage = rubraview_page_source_fulta_codepage(codepage),
        .cancel = cancel_of ? run_cancelled : NULL, .cancel_ctx = cancel_of,
    };
    return fulta_arc_open(&src, &opt, out);
}

static bool same_name(const char *a, const char *b) {
    for (;; ++a, ++b) {
        char x = *a == '\\' ? '/' : lower(*a), y = *b == '\\' ? '/' : lower(*b);
        if (x != y) return false;
        if (!x) return true;
    }
}

/* What the new archive is to hold: a file's name, its size, and its
   checksum where the old archive gave one and the bytes did not change. */
typedef struct expect {
    const char *name;
    uint64_t size;
    uint32_t crc;
    bool has_crc;
} expect_t;

static int expect_cmp(const void *a, const void *b) {
    return strcmp(((const expect_t*)a)->name, ((const expect_t*)b)->name);
}

typedef struct mem_sink {
    const uint8_t *want;
    size_t size, at;
    bool same;
} mem_sink_t;

static fulta_arc_err_t mem_sink_write(void *ctx, const void *data, size_t n) {
    mem_sink_t *m = (mem_sink_t*)ctx;
    if (m->at + n > m->size || memcmp(m->want + m->at, data, n) != 0) m->same = false;
    m->at += n;
    return m->same ? FULTA_ARC_OK : FULTA_ARC_ERR_CHECKSUM;
}

/* The new archive opened again: the files expected, no others, and the
   new bytes read back. */
static bool verify(rubraview_arcedit_job_t *job, expect_t *want, size_t want_count, const char *changed_name) {
    fulta_arc_t *arc = NULL;
    if (open_at(job->out_path, job->codepage, NULL, job, &arc) != FULTA_ARC_OK) return false;
    bool ok = true;
    size_t count = fulta_arc_count(arc), files = 0, changed = SIZE_MAX;
    expect_t *got = (expect_t*)calloc(count ? count : 1, sizeof(expect_t));
    if (!got) ok = false;
    for (size_t i = 0; ok && i < count; ++i) {
        const fulta_arc_entry_t *en = fulta_arc_entry(arc, i);
        if (!en || (en->flags & FULTA_ARC_ENTRY_DIR)) continue;
        if (changed_name && same_name(en->name, changed_name)) changed = i;
        got[files++] = (expect_t){ .name = en->name, .size = en->size, .crc = en->crc32,
                                   .has_crc = (en->flags & FULTA_ARC_ENTRY_HAS_CRC32) != 0 };
    }
    if (ok && files != want_count) ok = false;
    if (ok) {
        qsort(got, files, sizeof(expect_t), expect_cmp);
        qsort(want, want_count, sizeof(expect_t), expect_cmp);
        for (size_t i = 0; ok && i < files; ++i) {
            if (strcmp(got[i].name, want[i].name) != 0 || got[i].size != want[i].size) ok = false;
            else if (got[i].has_crc && want[i].has_crc && got[i].crc != want[i].crc) ok = false;
        }
    }
    if (ok && changed_name) {
        mem_sink_t m = { .want = job->data, .size = job->size, .at = 0, .same = true };
        fulta_arc_sink_t sink = { .ctx = &m, .write = mem_sink_write };
        if (changed == SIZE_MAX || fulta_arc_extract(arc, changed, &sink) != FULTA_ARC_OK || !m.same || m.at != job->size) ok = false;
    }
    free(got);
    fulta_arc_close(arc);
    return ok;
}

static rubraview_arcedit_result_t result_of(fulta_arc_err_t err, bool writing) {
    switch (err) {
        case FULTA_ARC_OK:              return RUBRAVIEW_ARCEDIT_OK;
        case FULTA_ARC_ERR_CANCELLED:   return RUBRAVIEW_ARCEDIT_CANCELLED;
        case FULTA_ARC_ERR_NOMEM:       return RUBRAVIEW_ARCEDIT_NO_MEMORY;
        case FULTA_ARC_ERR_BAD_NAME:    return RUBRAVIEW_ARCEDIT_BAD_NAME;
        case FULTA_ARC_ERR_PASSWORD_NEEDED:
        case FULTA_ARC_ERR_PASSWORD_WRONG: return RUBRAVIEW_ARCEDIT_REFUSED_ENCRYPTED;
        case FULTA_ARC_ERR_IO:          return writing ? RUBRAVIEW_ARCEDIT_UNWRITABLE : RUBRAVIEW_ARCEDIT_UNREADABLE;
        default:                        return RUBRAVIEW_ARCEDIT_UNREADABLE;
    }
}

/* The new file is not wanted after all. */
static void discard(const char *path) {
#ifdef _WIN32
    (void)rubraview_pal_fs_delete((u8str_t){ .ptr = path, .len = strlen(path) });
#else
    (void)remove(path);   /* the host's PAL deletes nothing (pal_fs_posix.c) */
#endif
}

rubraview_arcedit_result_t rubraview_arcedit_run(rubraview_arcedit_job_t *job) {
    if (!job || !job->archive_path || !job->out_path) return RUBRAVIEW_ARCEDIT_UNREADABLE;
    job->error = 0;
    bool with_data = job->op == RUBRAVIEW_ARCEDIT_REPLACE || job->op == RUBRAVIEW_ARCEDIT_ADD;
    bool with_name = job->op == RUBRAVIEW_ARCEDIT_RENAME || job->op == RUBRAVIEW_ARCEDIT_ADD;
    if ((with_data && !job->data && job->size > 0) || (with_name && (!job->name || !job->name[0]))) return RUBRAVIEW_ARCEDIT_BAD_NAME;
    if (job->name && (strchr(job->name, '/') || strchr(job->name, '\\'))) return RUBRAVIEW_ARCEDIT_BAD_NAME;

    if (!job_note_volume(job, job->archive_path)) return RUBRAVIEW_ARCEDIT_NO_MEMORY;
    fulta_arc_t *arc = NULL;
    fulta_arc_err_t err = open_at(job->archive_path, job->codepage, job, job, &arc);
    if (err != FULTA_ARC_OK) { job->error = err; return result_of(err, false); }

    rubraview_arcedit_result_t result = RUBRAVIEW_ARCEDIT_OK;
    fulta_arc_edit_t *edit = NULL;
    expect_t *want = NULL;
    char *full = NULL;           /* `name`, with the folder of the entry it is about */
    char **names = NULL;         /* the viewer's reading of a name, where the new archive takes it */
    size_t count = fulta_arc_count(arc), want_count = 0;
    fulta_arc_format_t format = fulta_arc_format(arc);
    fulta_arc_editable_t editable = fulta_arc_editable(arc);
    fulta_arc_format_t out_format = job->target == RUBRAVIEW_ARCEDIT_ZIP ? FULTA_ARC_FORMAT_ZIP
                                  : job->target == RUBRAVIEW_ARCEDIT_7Z ? FULTA_ARC_FORMAT_7Z : format;
    bool in_place = out_format == format && editable == FULTA_ARC_EDIT_OK;
    run_t run = { .job = job, .at = 0 };

    if (editable == FULTA_ARC_EDIT_ENCRYPTED) { result = RUBRAVIEW_ARCEDIT_REFUSED_ENCRYPTED; goto done; }
    if (out_format != FULTA_ARC_FORMAT_ZIP && out_format != FULTA_ARC_FORMAT_7Z) { result = RUBRAVIEW_ARCEDIT_UNWRITABLE; goto done; }
    {
        const fulta_arc_entry_t *en = fulta_arc_entry(arc, job->entry_index);
        if (!en || (en->flags & FULTA_ARC_ENTRY_DIR)) { result = RUBRAVIEW_ARCEDIT_UNREADABLE; goto done; }
    }

    want = (expect_t*)calloc(count + 1, sizeof(expect_t));
    names = (char**)calloc(count + 1, sizeof(char*));
    if (!want || !names) { result = RUBRAVIEW_ARCEDIT_NO_MEMORY; goto done; }
    err = fulta_arc_edit_begin(arc, &edit);
    if (err != FULTA_ARC_OK) { result = result_of(err, false); goto done; }

    /* A converted archive stores every name as Unicode, so it takes the
       names as the viewer shows them: a ZIP's names in the machine's code
       page are read by the viewer, not by FultaArc (§3.8.3). */
    if (!in_place) {
        enum { SCRATCH = 8192 };
        uint8_t *scratch = (uint8_t*)malloc(SCRATCH);
        if (!scratch) { result = RUBRAVIEW_ARCEDIT_NO_MEMORY; goto done; }
        for (size_t i = 0; i < count && result == RUBRAVIEW_ARCEDIT_OK; ++i) {
            const fulta_arc_entry_t *en = fulta_arc_entry(arc, i);
            if (!en || !en->name) continue;
            proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = scratch, .size = SCRATCH });
            u8str_t shown = rubraview_page_source_entry_name(&arena, en, format, job->codepage);
            if (shown.len == 0 || (shown.len == strlen(en->name) && memcmp(shown.ptr, en->name, shown.len) == 0)) continue;
            bool dir = (en->flags & FULTA_ARC_ENTRY_DIR) != 0;
            bool slash = shown.ptr[shown.len - 1] == '/';
            names[i] = (char*)malloc(shown.len + 2);
            if (!names[i]) { result = RUBRAVIEW_ARCEDIT_NO_MEMORY; break; }
            memcpy(names[i], shown.ptr, shown.len);
            names[i][shown.len] = '\0';
            if (dir && !slash) { names[i][shown.len] = '/'; names[i][shown.len + 1] = '\0'; }
            if (i == job->entry_index && job->op != RUBRAVIEW_ARCEDIT_ADD && job->op != RUBRAVIEW_ARCEDIT_DELETE) continue;
            err = fulta_arc_edit_replace(edit, i, names[i], NULL);
            if (err != FULTA_ARC_OK) result = result_of(err, false);
        }
        free(scratch);
        if (result != RUBRAVIEW_ARCEDIT_OK) goto done;
    }

    /* The name asked for, in the folder of the entry it is about; it is
       not one another entry has. */
    if (job->name) {
        const fulta_arc_entry_t *beside = fulta_arc_entry(arc, job->entry_index);
        const char *beside_name = !beside ? "" : names[job->entry_index] ? names[job->entry_index] : beside->name;
        size_t room = strlen(beside_name) + strlen(job->name) + 2;
        full = (char*)malloc(room);
        if (!full) { result = RUBRAVIEW_ARCEDIT_NO_MEMORY; goto done; }
        if (rubraview_arcedit_sibling_name((u8str_t){ .ptr = beside_name, .len = strlen(beside_name) },
                                           (u8str_t){ .ptr = job->name, .len = strlen(job->name) }, full, room) == 0) {
            result = RUBRAVIEW_ARCEDIT_BAD_NAME;
            goto done;
        }
        for (size_t i = 0; i < count; ++i) {
            const fulta_arc_entry_t *en = fulta_arc_entry(arc, i);
            if (!en || !en->name) continue;
            if (job->op != RUBRAVIEW_ARCEDIT_ADD && i == job->entry_index) continue;
            if (same_name(names[i] ? names[i] : en->name, full)) { result = RUBRAVIEW_ARCEDIT_NAME_TAKEN; goto done; }
        }
    }

    const char *changed_name = NULL;
    /* An empty file is given as one of unknown length: FultaArc at 4fb7c06
       frees its buffer twice for a reader that says 0 (reported to it). */
    fulta_arc_reader_t reader = { .ctx = &run, .read = run_read, .size = job->size > 0 ? job->size : UINT64_MAX };
    switch (job->op) {
        case RUBRAVIEW_ARCEDIT_DELETE:
            err = fulta_arc_edit_remove(edit, job->entry_index);
            break;
        case RUBRAVIEW_ARCEDIT_RENAME:
            err = fulta_arc_edit_replace(edit, job->entry_index, full, NULL);
            break;
        case RUBRAVIEW_ARCEDIT_REPLACE: {
            const fulta_arc_entry_t *en = fulta_arc_entry(arc, job->entry_index);
            changed_name = full ? full : names[job->entry_index] ? names[job->entry_index] : en->name;
            reader.mtime = (int64_t)time(NULL);
            reader.attributes = en->attributes;
            err = fulta_arc_edit_replace(edit, job->entry_index, changed_name, &reader);
            break;
        }
        case RUBRAVIEW_ARCEDIT_ADD:
            changed_name = full;
            reader.mtime = (int64_t)time(NULL);
            err = fulta_arc_edit_add(edit, full, &reader);
            break;
    }
    if (err != FULTA_ARC_OK) { job->error = err; result = result_of(err, false); goto done; }

    for (size_t i = 0; i < count; ++i) {
        const fulta_arc_entry_t *en = fulta_arc_entry(arc, i);
        if (!en || (en->flags & FULTA_ARC_ENTRY_DIR)) continue;
        bool it = job->op != RUBRAVIEW_ARCEDIT_ADD && i == job->entry_index;
        if (it && job->op == RUBRAVIEW_ARCEDIT_DELETE) continue;
        expect_t e = { .name = names[i] ? names[i] : en->name, .size = en->size, .crc = en->crc32,
                       .has_crc = (en->flags & FULTA_ARC_ENTRY_HAS_CRC32) != 0 };
        if (it && job->op == RUBRAVIEW_ARCEDIT_RENAME) e.name = full;
        if (it && job->op == RUBRAVIEW_ARCEDIT_REPLACE) { e.name = changed_name; e.size = job->size; e.has_crc = false; }
        want[want_count++] = e;
    }
    if (job->op == RUBRAVIEW_ARCEDIT_ADD) want[want_count++] = (expect_t){ .name = full, .size = job->size };

    /* Pictures and films do not pack further: fast, not small. */
    fulta_arc_write_options_t opts = { .level = 1, .progress = run_progress, .progress_ctx = job };
    err = fulta_arc_edit_write_file(edit, out_format, job->out_path, &opts);
    if (err != FULTA_ARC_OK) {
        job->error = err;
        result = atomic_load(&job->cancel) ? RUBRAVIEW_ARCEDIT_CANCELLED : result_of(err, true);
        goto done;
    }
    if (atomic_load(&job->cancel)) result = RUBRAVIEW_ARCEDIT_CANCELLED;
    else if (!verify(job, want, want_count, changed_name)) result = RUBRAVIEW_ARCEDIT_NOT_VERIFIED;

done:
    if (edit) fulta_arc_edit_free(edit);
    fulta_arc_close(arc);
    if (names) for (size_t i = 0; i < count; ++i) free(names[i]);
    free(names);
    free(want);
    free(full);
    if (result != RUBRAVIEW_ARCEDIT_OK) discard(job->out_path);
    return result;
}

void rubraview_arcedit_job_free(rubraview_arcedit_job_t *job) {
    if (!job) return;
    for (size_t i = 0; i < job->volume_count; ++i) free(job->volumes[i]);
    free(job->volumes);
    job->volumes = NULL;
    job->volume_count = 0;
}
