/* edit.c - FultaArc: archive editing (delete / replace / add) for ZIP and 7z. MIT. See D-015 (RV-090).
 *
 * An edit starts from an open archive with every entry marked "copy", and remove/replace/add mutate that plan.
 * Writing in the source's own format copies untouched entries' packed bytes without re-encoding (the copy route,
 * currently ZIP); a different ZIP/7z re-encodes everything (the convert route). Editing an encrypted archive is
 * refused (fulta_arc_editable == FULTA_ARC_EDIT_ENCRYPTED). The source is never modified. */
#include "../core/internal.h"

#include "proven.h"

#include <stdlib.h>
#include <string.h>

struct fulta_arc_edit {
    proven_allocator_t alloc;     /* the source archive's: this plan and its names live there (D-038) */
    fulta_arc_t *arc;
    fa_edit_item_t *items;
    size_t n, cap;
    size_t nremoved;              /* originals marked removed */
    char **owned;                 /* copied names, released with the edit */
    size_t nown, cown;
    bool failed;
};

static bool push_item(fulta_arc_edit_t *ed, fa_edit_item_t it) {
    if (ed->n == ed->cap) {
        size_t nc = ed->cap ? ed->cap * 2 : 8;
        fa_edit_item_t *ni = fa_resize(ed->alloc, ed->items, ed->cap * sizeof *ni, nc * sizeof *ni);
        if (!ni) return false;
        ed->items = ni; ed->cap = nc;
    }
    ed->items[ed->n++] = it;
    return true;
}

/* A copy of `s` in the edit's owned list; returns the copy (stable), or NULL on OOM / NULL input. */
static const char *own_name(fulta_arc_edit_t *ed, const char *s) {
    if (!s) return NULL;
    char *c = fa_strdup_view(ed->alloc, proven_u8str_view_from_cstr(s));
    if (!c) return NULL;
    if (ed->nown == ed->cown) {
        size_t nc = ed->cown ? ed->cown * 2 : 8;
        char **no = fa_resize(ed->alloc, ed->owned, ed->cown * sizeof *no, nc * sizeof *no);
        if (!no) { fa_release(ed->alloc, c); return NULL; }
        ed->owned = no; ed->cown = nc;
    }
    ed->owned[ed->nown++] = c;
    return c;
}

/* The plan slot for original entry `index` (copied or replaced), or SIZE_MAX if it is gone (removed). */
static size_t find_original(fulta_arc_edit_t *ed, size_t index) {
    /* the originals stay in their slots (items[i] is source entry i; adds come after them), so this is O(1) */
    if (index >= ed->n || ed->items[index].removed) return SIZE_MAX;
    return index;
}

fulta_arc_err_t fulta_arc_edit_begin(fulta_arc_t *arc, fulta_arc_edit_t **out) {
    if (!arc || !out) return FULTA_ARC_ERR_INVALID_ARG;
    *out = NULL;
    fulta_arc_edit_t *ed = fa_zalloc(arc->alloc, 1, sizeof *ed);
    if (!ed) return FULTA_ARC_ERR_NOMEM;
    ed->alloc = arc->alloc;
    ed->arc = arc;
    for (size_t i = 0; i < fulta_arc_count(arc); i++) {
        fa_edit_item_t it = { .is_copy = true, .src = arc, .src_index = i };
        if (!push_item(ed, it)) { fulta_arc_edit_free(ed); return FULTA_ARC_ERR_NOMEM; }
    }
    *out = ed;
    return FULTA_ARC_OK;
}

fulta_arc_err_t fulta_arc_edit_remove(fulta_arc_edit_t *edit, size_t index) {
    if (!edit) return FULTA_ARC_ERR_INVALID_ARG;
    if (index >= fulta_arc_count(edit->arc)) return FULTA_ARC_ERR_INVALID_ARG;
    size_t at = find_original(edit, index);
    if (at == SIZE_MAX) return FULTA_ARC_ERR_INVALID_ARG;   /* already removed */
    edit->items[at].removed = true;                          /* compacted once, when the edit is written */
    edit->nremoved++;
    return FULTA_ARC_OK;
}

fulta_arc_err_t fulta_arc_edit_replace(fulta_arc_edit_t *edit, size_t index, const char *name,
                                       const fulta_arc_reader_t *reader) {
    if (!edit) return FULTA_ARC_ERR_INVALID_ARG;
    if (index >= fulta_arc_count(edit->arc)) return FULTA_ARC_ERR_INVALID_ARG;
    size_t at = find_original(edit, index);
    if (at == SIZE_MAX) return FULTA_ARC_ERR_INVALID_ARG;
    const char *nm = NULL;
    if (name) { nm = own_name(edit, name); if (!nm) return FULTA_ARC_ERR_NOMEM; }
    fa_edit_item_t *it = &edit->items[at];
    if (reader && reader->read) {                            /* new data: re-encode */
        it->is_copy = false;
        it->reader = *reader;
        it->name = nm ? nm : NULL;                           /* a data replace must carry a name */
        if (!it->name) {                                     /* keep the source name as a UTF-8 copy */
            const fulta_arc_entry_t *e = fulta_arc_entry(edit->arc, index);
            if (!e) return FULTA_ARC_ERR_INVALID_ARG;
            it->name = own_name(edit, e->name);
            if (!it->name) return FULTA_ARC_ERR_NOMEM;
        }
    } else {                                                 /* rename only: keep the packed bytes */
        if (!nm) return FULTA_ARC_ERR_INVALID_ARG;           /* nothing to do */
        it->is_copy = true;
        it->name = nm;
    }
    return FULTA_ARC_OK;
}

fulta_arc_err_t fulta_arc_edit_add(fulta_arc_edit_t *edit, const char *name, const fulta_arc_reader_t *reader) {
    if (!edit || !name) return FULTA_ARC_ERR_INVALID_ARG;
    const char *nm = own_name(edit, name);
    if (!nm) return FULTA_ARC_ERR_NOMEM;
    fa_edit_item_t it = { .is_copy = false, .src = NULL, .name = nm };
    if (reader) it.reader = *reader;
    if (!push_item(edit, it)) return FULTA_ARC_ERR_NOMEM;
    return FULTA_ARC_OK;
}

/* The write dispatcher. `spill_hint` is the output file path when writing to a file (so the 7z convert's temp spill
 * can go beside it), or NULL for a bare sink (the spill then goes to the system temp directory). */
static fulta_arc_err_t edit_write_to(fulta_arc_edit_t *edit, fulta_arc_format_t format,
                                     const fulta_arc_write_sink_t *sink, const fulta_arc_write_options_t *opts,
                                     const char *spill_hint) {
    if (!edit || !sink || !sink->write || !sink->patch) return FULTA_ARC_ERR_INVALID_ARG;
    if (format != FULTA_ARC_FORMAT_ZIP && format != FULTA_ARC_FORMAT_7Z) return FULTA_ARC_ERR_INVALID_ARG;
    if (opts && opts->volume_size && !fa_vol_of(sink)) return FULTA_ARC_ERR_INVALID_ARG;   /* one sink, one file */
    if (!fa_write_alloc_ok(opts)) return FULTA_ARC_ERR_INVALID_ARG;   /* one or two allocator functions set */
    if (!fa_write_dos_ok(opts)) return FULTA_ARC_ERR_INVALID_ARG;
    if (format == FULTA_ARC_FORMAT_ZIP && opts && opts->password && opts->encrypt_names) return FULTA_ARC_ERR_INVALID_ARG;
    fulta_arc_editable_t v = fulta_arc_editable(edit->arc);
    if (v == FULTA_ARC_EDIT_ENCRYPTED) return FULTA_ARC_ERR_UNSUPPORTED;   /* editing encrypted is refused (D-015) */
    int level = opts ? opts->level : 6;
    if (level < 0 || level > 9) level = 6;
    bool same_format = (format == fulta_arc_format(edit->arc));
    bool encrypt = opts && opts->password;                   /* copied bytes cannot be encrypted: convert instead */

    /* the plan without the removed originals, made once here (removal only marks, so it stays O(1)) */
    const fa_edit_item_t *items = edit->items;
    size_t n = edit->n;
    fa_edit_item_t *packed = NULL;
    if (edit->nremoved) {
        packed = fa_alloc(edit->alloc, (edit->n - edit->nremoved) * sizeof *packed);
        if (!packed) return FULTA_ARC_ERR_NOMEM;
        n = 0;
        for (size_t i = 0; i < edit->n; i++)
            if (!edit->items[i].removed) packed[n++] = edit->items[i];
        items = packed;
    }
    fulta_arc_err_t e;
    if (same_format && v == FULTA_ARC_EDIT_OK && !encrypt) { /* copy route */
        if (format == FULTA_ARC_FORMAT_ZIP)   /* copied entries stay verbatim; added ones take opts->method */
            e = fa_write_zip_edited(items, n, level, opts && opts->zip64, opts, sink);
        else  /* 7z copy route honours method/filter/BCJ2 on added/replaced entries; copied folders stay verbatim.
                 SOLID_OFF is moot here (the source is already non-solid) but a BCJ2+password mix is caught by write_7z. */
            e = fa_write_7z_edited(items, n, level, opts, sink);
    } else if (format == FULTA_ARC_FORMAT_ZIP) {
        /* convert route: re-encode everything into the target format (also the same format when a password is
           given: every entry is then encrypted, D-016). Both honour the method; the 7z route also filter and solid. */
        e = fa_write_zip_convert(items, n, level, opts && opts->zip64, opts, sink, spill_hint);
    } else {
        e = fa_write_7z_convert(items, n, level, opts, sink, spill_hint);
    }
    fa_release(edit->alloc, packed);
    return e;
}

fulta_arc_err_t fulta_arc_edit_write(fulta_arc_edit_t *edit, fulta_arc_format_t format,
                                     const fulta_arc_write_sink_t *sink, const fulta_arc_write_options_t *opts) {
    return edit_write_to(edit, format, sink, opts, NULL);
}

/* ---- split output: the edit written through a volume writer ---- */

typedef struct edit_job {
    fulta_arc_edit_t *edit;
    fulta_arc_format_t format;
    const fulta_arc_write_options_t *opts;
    const char *spill_hint;
} edit_job_t;

static fulta_arc_err_t edit_job_run(void *ctx, const fulta_arc_write_sink_t *sink) {
    const edit_job_t *j = ctx;
    return edit_write_to(j->edit, j->format, sink, j->opts, j->spill_hint);
}

fulta_arc_err_t fulta_arc_edit_write_volumes(fulta_arc_edit_t *edit, fulta_arc_format_t format,
                                             const fulta_arc_volume_sink_t *volumes,
                                             const fulta_arc_write_options_t *opts) {
    if (!edit || !opts) return FULTA_ARC_ERR_INVALID_ARG;
    edit_job_t j = {edit, format, opts, NULL};
    return fa_vol_run(fa_write_alloc(opts), format, opts->volume_size, volumes, edit_job_run, &j);
}

/* ---- to a file: temp beside `path`, renamed over it only on success ---- */

static fulta_arc_err_t ew_write(void *ctx, const void *data, size_t n) {
    proven_file_t *f = ctx;
    return proven_fs_write_all(*f, (proven_mem_view_t){.ptr = data, .size = n}) ? FULTA_ARC_ERR_IO : FULTA_ARC_OK;
}
static fulta_arc_err_t ew_patch(void *ctx, uint64_t offset, const void *data, size_t n) {
    proven_file_t *f = ctx;
    proven_result_size_t r = proven_fs_pwrite(*f, (proven_mem_view_t){.ptr = data, .size = n}, offset);
    return r.err || r.value != n ? FULTA_ARC_ERR_IO : FULTA_ARC_OK;
}

fulta_arc_err_t fulta_arc_edit_write_file(fulta_arc_edit_t *edit, fulta_arc_format_t format, const char *path,
                                          const fulta_arc_write_options_t *opts) {
    if (!edit || !path) return FULTA_ARC_ERR_INVALID_ARG;
    if (opts && opts->volume_size) {                         /* a split set: volumes.c names and renames */
        edit_job_t j = {edit, format, opts, path};
        return fa_vol_files(fa_write_alloc(opts), path, format, opts->volume_size, edit_job_run, &j);
    }
    size_t pl = strlen(path);
    char *tmp = fa_alloc(edit->alloc, pl + 7);
    if (!tmp) return FULTA_ARC_ERR_NOMEM;
    memcpy(tmp, path, pl);
    memcpy(tmp + pl, ".fatmp", 7);                           /* same directory -> rename stays on one volume */
    proven_result_file_t r = proven_fs_open(proven_heap_allocator(), proven_u8str_view_from_cstr(tmp),
                                            PROVEN_FS_WRITE | PROVEN_FS_CREATE | PROVEN_FS_TRUNC);
    if (r.err) { fa_release(edit->alloc, tmp); return FULTA_ARC_ERR_IO; }
    proven_file_t f = r.value;
    fulta_arc_write_sink_t sink = {.ctx = &f, .write = ew_write, .patch = ew_patch};
    fulta_arc_err_t e = edit_write_to(edit, format, &sink, opts, tmp);   /* 7z convert spills to `tmp.d` */
    if (proven_fs_close(f) && !e) e = FULTA_ARC_ERR_IO;
    if (!e) {                                                /* replace the target atomically */
        if (proven_fs_rename(proven_heap_allocator(), proven_u8str_view_from_cstr(tmp),
                             proven_u8str_view_from_cstr(path)))
            e = FULTA_ARC_ERR_IO;
    }
    if (e) {
        proven_err_t ignored = proven_fs_remove(proven_heap_allocator(), proven_u8str_view_from_cstr(tmp));
        (void)ignored;
    }
    fa_release(edit->alloc, tmp);
    return e;
}

void fulta_arc_edit_free(fulta_arc_edit_t *edit) {
    if (!edit) return;
    proven_allocator_t a = edit->alloc;
    for (size_t i = 0; i < edit->nown; i++) fa_release(a, edit->owned[i]);
    fa_release(a, edit->owned);
    fa_release(a, edit->items);
    fa_release(a, edit);
}
