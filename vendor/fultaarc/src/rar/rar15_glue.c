/* rar15_glue.c - FultaArc: the RAR 1.4 / 1.5 reader (src/rar/rar15/) behind FultaArc's API. MIT.
 *
 * src/rar/rar15/ is the fultaarc-cleanroom-rar15 clean room's deliverable (rar15arc and unpack15, MIT), copied
 * byte-identical from the room's final state b9fd1c0 (src/rar/rar15.sha256, tests/tools/rar15-pin.sh; D-029). This
 * file only calls its API (rar15arc.h): it states no rule of the format of its own.
 *
 * Which archives it takes: those the room's reader opens whose entries all carry the version rar15arc.h names for
 * what RAR 1.5x writes (0x0f), and the older container (rar15arc's `old_format`). Anything else - RAR 2.0 to 7.0,
 * and an archive mixing RAR 1.5 entries with later ones - is handed to rar_glue.c (fa_rar_ops) untouched.
 *
 * Encryption (owner, 2026-10-22): the older format's cipher, which the room read in full, is used - the password
 * callback is asked and the password handed to rar15_archive_set_password (D-030). A RAR 1.5 encrypted entry is
 * listed as encrypted and unsupported and no password is asked for: its cipher was not obtained (D-029). The older
 * format checks a file with a 16-bit sum only (rar15arc.h), so a wrong password is caught by that sum or by the
 * stream failing, not as surely as by a CRC-32. Format, algorithm, derivation and provenance: docs/provenance/rar15/
 * (the room's pages, FultaArc's edition). */
#include "../core/internal.h"

#include "rar.h"
#include "rar15/rar15arc.h"

typedef struct rar15_glue {
    fa_view_t *views;
    proven_mem_view_t *vols;
    size_t nvol;
    rar15_archive_t ar;
    bool open;
    char *password;           /* rar15arc borrows the password: this copy outlives every extraction */
} rar15_glue_t;

static bool rar15_probe(const uint8_t *h, size_t n, uint64_t *off) {
    *off = 0;
    /* the two markers rar15arc.c looks for (MARK, MARK14) */
    return n >= 7 && (memcmp(h, "Rar!\x1a\x07\x00", 7) == 0 || memcmp(h, "RE~^", 4) == 0);
}

static fulta_arc_err_t add_volume(rar15_glue_t *g, const fulta_arc_source_t *src) {
    fa_view_t *v = fa_realloc(g->views, (g->nvol + 1) * sizeof *v);
    if (!v) return FULTA_ARC_ERR_NOMEM;
    g->views = v;
    proven_mem_view_t *mv = fa_realloc(g->vols, (g->nvol + 1) * sizeof *mv);
    if (!mv) return FULTA_ARC_ERR_NOMEM;
    g->vols = mv;
    fulta_arc_err_t e = fa_view_open(src, &g->views[g->nvol]);
    if (e) return e;
    g->vols[g->nvol] = (proven_mem_view_t){.ptr = g->views[g->nvol].data, .size = g->views[g->nvol].size};
    g->nvol++;
    return FULTA_ARC_OK;
}

static void close_archive(rar15_glue_t *g) {
    if (g->open) rar15_archive_close(&g->ar);
    g->open = false;
}

static fulta_arc_err_t open_archive(fulta_arc_t *arc, rar15_glue_t *g) {
    close_archive(g);
    fa_budget_arm(&arc->budget);
    rar15_err_t r = rar15_archive_open_mem(&g->ar, fa_budget_as_allocator(&arc->budget), g->vols, g->nvol);
    if (r == PROVEN_OK) { g->open = true; return FULTA_ARC_OK; }
    if (r == PROVEN_ERR_NOMEM) return arc->budget.exceeded ? FULTA_ARC_ERR_LIMIT : FULTA_ARC_ERR_NOMEM;
    return FULTA_ARC_ERR_NOT_ARCHIVE;
}

/* Taken here: the older container, or every entry of the RAR 1.5 version (rar15arc.h, rar15_entry_t.version). */
static bool is_ours(const rar15_archive_t *a) {
    if (a->old_format) return true;
    if (a->count == 0) return false;
    for (size_t i = 0; i < a->count; i++)
        if (a->entries[i].version != 0x0f) return false;
    return true;
}

/* The volumes after the first, named as RAR's old numbering (x.rar, x.r00, x.r01, ...; rar.h), while they exist. */
static fulta_arc_err_t collect_volumes(fulta_arc_t *arc, rar15_glue_t *g) {
    if (!(g->ar.main_flags & RAR15_MAIN_VOLUME) || !arc->opt.name) return FULTA_ARC_OK;
    size_t cap = strlen(arc->opt.name) + 64;
    void *mem = fa_malloc(cap);
    char *cur = fa_strndup(arc->opt.name, strlen(arc->opt.name));
    fulta_arc_err_t e = mem && cur ? FULTA_ARC_OK : FULTA_ARC_ERR_NOMEM;
    for (uint32_t k = 1; !e && k < 100000; k++) {
        proven_arena_t arena = proven_arena_create((proven_mem_mut_t){.ptr = mem, .size = cap});
        proven_u8str_view_t next = fa_rar_next_volume(&arena, proven_u8str_view_from_cstr(cur), true);
        if (!next.ptr || !next.size) break;
        char *path = fa_strndup((const char *)next.ptr, next.size);
        if (!path) { e = FULTA_ARC_ERR_NOMEM; break; }
        fa_free(cur);
        cur = path;
        const fulta_arc_source_t *src;
        e = fa_volume(arc, k, path, &src);
        if (e == FULTA_ARC_ERR_VOLUME_MISSING) { e = FULTA_ARC_OK; break; }
        if (!e) e = add_volume(g, src);
    }
    fa_free(cur);
    fa_free(mem);
    if (e || g->nvol == 1) return e;
    e = open_archive(arc, g);                           /* again, over all the volumes */
    return e == FULTA_ARC_ERR_NOT_ARCHIVE ? FULTA_ARC_ERR_CORRUPT : e;
}

static bool entry_unsupported(const rar15_archive_t *a, const rar15_entry_t *e) {
    if ((e->flags & RAR15_FILE_PASSWORD) && !a->old_format) return true;   /* RAR 1.5's cipher: not read */
    return a->old_format && e->version != 2;            /* rar15arc.h: 2 in the older format */
}

static fulta_arc_err_t list_entries(fulta_arc_t *arc, rar15_glue_t *g) {
    const rar15_archive_t *a = &g->ar;
    bool solid = (a->main_flags & RAR15_MAIN_SOLID) != 0, first_packed = true;
    uint32_t group = 0;
    for (size_t i = 0; i < a->count; i++) {
        const rar15_entry_t *re = &a->entries[i];
        bool stored = re->method == (a->old_format ? 0 : 0x30);
        bool not_unicode = false;
        char *name = fa_decode_name(arc, re->name.ptr, re->name.size, false, FULTA_ARC_CP_437, &not_unicode);
        if (!name) return FULTA_ARC_ERR_NOMEM;
        const char *method = stored ? "Store" : a->old_format ? "RAR1.4" : "RAR1.5";
        fa_entry_t *en = fa_add_entry(arc, re->name.ptr, re->name.size, name, method);
        fa_free(name);
        if (!en) return FULTA_ARC_ERR_NOMEM;
        bool dir = rar15_entry_is_dir(re);
        en->pub.size = dir ? 0 : re->size;
        en->pub.packed_size = re->packed_size;
        en->pub.mtime = fa_dos_time_ns(re->dos_date, re->dos_time);
        en->pub.flags |= FULTA_ARC_ENTRY_HAS_MTIME;
        if (dir) en->pub.flags |= FULTA_ARC_ENTRY_DIR;
        if (!a->old_format && !dir && !(re->flags & RAR15_FILE_TO_NEXT)) {
            en->pub.crc32 = re->crc;                       /* the older container holds a 16-bit sum instead */
            en->pub.flags |= FULTA_ARC_ENTRY_HAS_CRC32;
        }
        if (re->flags & (RAR15_FILE_FROM_PREV | RAR15_FILE_TO_NEXT)) en->pub.flags |= FULTA_ARC_ENTRY_SPLIT;
        if (re->flags & RAR15_FILE_PASSWORD) en->pub.flags |= FULTA_ARC_ENTRY_ENCRYPTED;
        if (entry_unsupported(a, re)) en->pub.flags |= FULTA_ARC_ENTRY_UNSUPPORTED;
        if (not_unicode) en->pub.flags |= FULTA_ARC_ENTRY_NAME_NOT_UNICODE;
        /* A solid archive is one stream over its compressed entries; stored ones leave it alone (rar15arc.h). */
        if (solid && !stored && !dir) {
            if (!first_packed) en->pub.flags |= FULTA_ARC_ENTRY_SOLID;
            first_packed = false;
        }
        if (!(en->pub.flags & FULTA_ARC_ENTRY_SOLID)) group++;
        en->pub.solid_group = group;
    }
    return FULTA_ARC_OK;
}

static void rar15_close(fulta_arc_t *arc);

static fulta_arc_err_t rar15_open(fulta_arc_t *arc, uint64_t search) {
    rar15_glue_t *g = fa_calloc(1, sizeof *g);
    if (!g) return FULTA_ARC_ERR_NOMEM;
    arc->state = g;
    fulta_arc_err_t e = add_volume(g, arc->volumes[0]);
    if (!e) e = open_archive(arc, g);
    if (e == FULTA_ARC_ERR_NOT_ARCHIVE || (!e && !is_ours(&g->ar))) {
        rar15_close(arc);
        if (search == UINT64_MAX) return FULTA_ARC_ERR_NOT_ARCHIVE;   /* the search loop tries fa_rar_ops next */
        arc->ops = &fa_rar_ops;                                       /* a signature matched: RAR 2.0 and later */
        return fa_rar_ops.open(arc, search);
    }
    if (!e) e = collect_volumes(arc, g);
    if (!e) e = list_entries(arc, g);
    return e;
}

static fulta_arc_err_t rar15_extract(fulta_arc_t *arc, size_t index, const fulta_arc_sink_t *sink) {
    rar15_glue_t *g = arc->state;
    if (index >= g->ar.count) return FULTA_ARC_ERR_INVALID_ARG;
    uint32_t flags = arc->entries[index].pub.flags;
    if (flags & FULTA_ARC_ENTRY_DIR) return FULTA_ARC_OK;
    if (flags & FULTA_ARC_ENTRY_UNSUPPORTED) return FULTA_ARC_ERR_UNSUPPORTED;
    if (fa_cancelled(arc)) return FULTA_ARC_ERR_CANCELLED;
    /* An encrypted entry of the older format: try the passwords the callback gives until one decodes it and passes
     * its check; a wrong one shows as a failed sum or damaged data (rar15arc.h). */
    rar15_extracted_t x;
    for (uint32_t attempt = 0;; attempt++) {
        bool encrypted = (flags & FULTA_ARC_ENTRY_ENCRYPTED) != 0;
        const char *pw = NULL;
        if (encrypted) {
            fulta_arc_err_t pe = fa_password(arc, attempt, &pw);
            if (pe) return attempt ? FULTA_ARC_ERR_PASSWORD_WRONG : pe;
            char *copy = fa_strndup(pw, strlen(pw));
            if (!copy) return FULTA_ARC_ERR_NOMEM;
            fa_free(g->password);
            g->password = copy;
            rar15_archive_set_password(&g->ar, proven_u8str_view_from_cstr(g->password));
        }
        fa_budget_arm(&arc->budget);
        x = rar15_archive_extract_alloc(&g->ar, index);
        if (!encrypted) break;
        if (x.err == PROVEN_OK) { fa_password_ok(arc, pw); break; }
        if (x.err != RAR15_E_CRC && x.err != RAR15_E_DATA) break;
        rar15_extracted_free(&g->ar, &x);
        if (attempt >= 64) return FULTA_ARC_ERR_PASSWORD_WRONG;
    }
    fulta_arc_err_t e;
    switch (x.err) {
    case PROVEN_OK: e = FULTA_ARC_OK; break;
    case PROVEN_ERR_NOMEM: e = arc->budget.exceeded ? FULTA_ARC_ERR_LIMIT : FULTA_ARC_ERR_NOMEM; break;
    case RAR15_E_CRC: e = FULTA_ARC_ERR_CHECKSUM; break;
    case RAR15_E_INCOMPLETE: e = FULTA_ARC_ERR_VOLUME_MISSING; break;
    case RAR15_E_ENCRYPTED:                         /* an earlier entry of the solid stream is encrypted */
    case RAR15_E_VERSION: e = FULTA_ARC_ERR_UNSUPPORTED; break;
    default: e = FULTA_ARC_ERR_CORRUPT; break;
    }
    bool damaged = e == FULTA_ARC_ERR_CHECKSUM || e == FULTA_ARC_ERR_CORRUPT || e == FULTA_ARC_ERR_VOLUME_MISSING;
    if (e == FULTA_ARC_OK || (damaged && arc->opt.keep_damaged)) {
        fa_progress(arc, x.data.size);
        fulta_arc_err_t w = sink && sink->write && x.data.size ? sink->write(sink->ctx, x.data.ptr, x.data.size)
                                                               : FULTA_ARC_OK;
        if (w) e = w;
        else if (damaged) e = FULTA_ARC_DAMAGED;
    }
    rar15_extracted_free(&g->ar, &x);
    return e;
}

static void rar15_close(fulta_arc_t *arc) {
    rar15_glue_t *g = arc->state;
    if (!g) return;
    close_archive(g);
    fa_free(g->password);
    for (size_t i = 0; i < g->nvol; i++) fa_view_close(&g->views[i]);
    fa_free(g->views);
    fa_free(g->vols);
    fa_free(g);
    arc->state = NULL;
}

const fa_format_ops_t fa_rar15_ops = {
    .format = FULTA_ARC_FORMAT_RAR,
    .probe = rar15_probe,    /* RAR 1.5-4.x's marker or the older one; anything not RAR 1.4/1.5 goes to fa_rar_ops */
    .open = rar15_open,
    .extract = rar15_extract,
    .close = rar15_close,
};
