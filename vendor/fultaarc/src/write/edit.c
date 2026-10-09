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
    fulta_arc_t *arc;
    fa_edit_item_t *items;
    size_t n, cap;
    char **owned;                 /* strdup'd names, freed with the edit */
    size_t nown, cown;
    bool failed;
};

static bool push_item(fulta_arc_edit_t *ed, fa_edit_item_t it) {
    if (ed->n == ed->cap) {
        size_t nc = ed->cap ? ed->cap * 2 : 8;
        fa_edit_item_t *ni = realloc(ed->items, nc * sizeof *ni);
        if (!ni) return false;
        ed->items = ni; ed->cap = nc;
    }
    ed->items[ed->n++] = it;
    return true;
}

/* strdup `s` into the edit's owned list; returns the copy (stable), or NULL on OOM / NULL input. */
static const char *own_name(fulta_arc_edit_t *ed, const char *s) {
    if (!s) return NULL;
    char *c = strdup(s);
    if (!c) return NULL;
    if (ed->nown == ed->cown) {
        size_t nc = ed->cown ? ed->cown * 2 : 8;
        char **no = realloc(ed->owned, nc * sizeof *no);
        if (!no) { free(c); return NULL; }
        ed->owned = no; ed->cown = nc;
    }
    ed->owned[ed->nown++] = c;
    return c;
}

/* The plan slot for original entry `index` (copied or replaced), or SIZE_MAX if it is gone (removed). */
static size_t find_original(fulta_arc_edit_t *ed, size_t index) {
    for (size_t i = 0; i < ed->n; i++)
        if (ed->items[i].src == ed->arc && ed->items[i].src_index == index) return i;
    return SIZE_MAX;
}

fulta_arc_err_t fulta_arc_edit_begin(fulta_arc_t *arc, fulta_arc_edit_t **out) {
    if (!arc || !out) return FULTA_ARC_ERR_INVALID_ARG;
    *out = NULL;
    fulta_arc_edit_t *ed = calloc(1, sizeof *ed);
    if (!ed) return FULTA_ARC_ERR_NOMEM;
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
    memmove(&edit->items[at], &edit->items[at + 1], (edit->n - at - 1) * sizeof *edit->items);
    edit->n--;
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
    fulta_arc_editable_t v = fulta_arc_editable(edit->arc);
    if (v == FULTA_ARC_EDIT_ENCRYPTED) return FULTA_ARC_ERR_UNSUPPORTED;   /* editing encrypted is refused (D-015) */
    int level = opts ? opts->level : 6;
    if (level < 0 || level > 9) level = 6;
    bool same_format = (format == fulta_arc_format(edit->arc));
    if (same_format && v == FULTA_ARC_EDIT_OK) {             /* copy route */
        if (format == FULTA_ARC_FORMAT_ZIP)
            return fa_write_zip_edited(edit->items, edit->n, level, opts && opts->zip64, opts, sink);
        return fa_write_7z_edited(edit->items, edit->n, level, opts, sink);
    }
    /* convert route: re-encode everything into the target format */
    if (format == FULTA_ARC_FORMAT_ZIP)
        return fa_write_zip_convert(edit->items, edit->n, level, opts && opts->zip64, opts, sink);
    return fa_write_7z_convert(edit->items, edit->n, level, opts, sink, spill_hint);
}

fulta_arc_err_t fulta_arc_edit_write(fulta_arc_edit_t *edit, fulta_arc_format_t format,
                                     const fulta_arc_write_sink_t *sink, const fulta_arc_write_options_t *opts) {
    return edit_write_to(edit, format, sink, opts, NULL);
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
    size_t pl = strlen(path);
    char *tmp = malloc(pl + 7);
    if (!tmp) return FULTA_ARC_ERR_NOMEM;
    memcpy(tmp, path, pl);
    memcpy(tmp + pl, ".fatmp", 7);                           /* same directory -> rename stays on one volume */
    proven_result_file_t r = proven_fs_open(proven_heap_allocator(), proven_u8str_view_from_cstr(tmp),
                                            PROVEN_FS_WRITE | PROVEN_FS_CREATE | PROVEN_FS_TRUNC);
    if (r.err) { free(tmp); return FULTA_ARC_ERR_IO; }
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
    free(tmp);
    return e;
}

void fulta_arc_edit_free(fulta_arc_edit_t *edit) {
    if (!edit) return;
    for (size_t i = 0; i < edit->nown; i++) free(edit->owned[i]);
    free(edit->owned);
    free(edit->items);
    free(edit);
}
