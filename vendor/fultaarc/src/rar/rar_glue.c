/* rar_glue.c - FultaArc: the RAR reader (rar.c, codec/) behind FultaArc's API. MIT.
 *
 * rar.c and codec/ are the rar-decoder clean room's MIT code from Rubraview (written from docs/specs/rar.md Part A
 * without access to UnRAR or to the spec's sources), ported by renaming only (D-006). This file connects them:
 * volumes found by name and mapped, the password callback, names in a code page, entries, extraction into sinks.
 * RAR 1.5 compression is reported unsupported until the rar15-scratch clean room delivers (D-004). */
#include "../core/internal.h"

#include "rar.h"
#include "rarcodec.h"

#include <stdio.h>

typedef struct rar_glue {
    proven_arena_t arena;
    void *backing;
    fa_view_t *views;
    fa_rar_volume_t *vols;
    size_t nvol;
    fa_rar_archive_t ar;
    bool open;
    size_t *map;              /* FultaArc entry index -> rar.c entry index */
} rar_glue_t;

static bool rar_probe(const uint8_t *h, size_t n, uint64_t *off) {
    *off = 0;
    return n >= 7 && memcmp(h, "Rar!\x1a\x07", 6) == 0 && (h[6] == 0 || (h[6] == 1 && n >= 8 && h[7] == 0));
}

static fulta_arc_err_t map_err(fa_rar_err_t e) {
    switch (e) {
    case FA_RAR_OK: return FULTA_ARC_OK;
    case FA_RAR_ERR_NOT_A_RAR: return FULTA_ARC_ERR_NOT_ARCHIVE;
    case FA_RAR_ERR_CORRUPT: return FULTA_ARC_ERR_CORRUPT;
    case FA_RAR_ERR_CORRUPT_STREAM: return FULTA_ARC_ERR_CHECKSUM;
    case FA_RAR_ERR_ENCRYPTED: return FULTA_ARC_ERR_PASSWORD_NEEDED;
    case FA_RAR_ERR_UNSUPPORTED: return FULTA_ARC_ERR_UNSUPPORTED;
    case FA_RAR_ERR_TOO_LARGE: return FULTA_ARC_ERR_LIMIT;
    case FA_RAR_ERR_OUT_OF_MEMORY: return FULTA_ARC_ERR_NOMEM;
    case FA_RAR_ERR_BAD_INDEX: return FULTA_ARC_ERR_INVALID_ARG;
    case FA_RAR_ERR_CANCELLED: return FULTA_ARC_ERR_CANCELLED;
    case FA_RAR_ERR_BAD_PASSWORD: return FULTA_ARC_ERR_PASSWORD_WRONG;
    case FA_RAR_ERR_MISSING_VOLUME: return FULTA_ARC_ERR_VOLUME_MISSING;
    case FA_RAR_ERR_NO_CODEC: return FULTA_ARC_ERR_UNSUPPORTED;
    }
    return FULTA_ARC_ERR_CORRUPT;
}

static fulta_arc_err_t add_volume(rar_glue_t *g, const fulta_arc_source_t *src) {
    fa_view_t *v = fa_realloc(g->views, (g->nvol + 1) * sizeof *v);
    if (!v) return FULTA_ARC_ERR_NOMEM;
    g->views = v;
    fa_rar_volume_t *rv = fa_realloc(g->vols, (g->nvol + 1) * sizeof *rv);
    if (!rv) return FULTA_ARC_ERR_NOMEM;
    g->vols = rv;
    fulta_arc_err_t e = fa_view_open(src, &g->views[g->nvol]);
    if (e) return e;
    g->vols[g->nvol] = (fa_rar_volume_t){g->views[g->nvol].data, g->views[g->nvol].size};
    g->nvol++;
    return FULTA_ARC_OK;
}

/* Find the volumes after the first by name (spec 4.1: name.partN.rar, or name.rar, .r00, .r01, ...). */
static fulta_arc_err_t collect_volumes(fulta_arc_t *arc, rar_glue_t *g) {
    fa_rar_volume_info_t info;
    if (!fa_rar_volume_info(g->vols[0].data, g->vols[0].size, &info) || !arc->opt.name) return FULTA_ARC_OK;
    u8str_t name = {arc->opt.name, strlen(arc->opt.name)};
    fa_rar_volume_name_kind_t kind = fa_rar_volume_name_kind(name);
    if (!info.is_volume && !info.headers_encrypted && kind == FA_RAR_NAME_PLAIN) return FULTA_ARC_OK;
    bool old = !info.new_numbering && kind != FA_RAR_NAME_PART;
    u8str_t cur = name;
    for (uint32_t k = 1; k < 100000; k++) {
        u8str_t next = fa_rar_next_volume(&g->arena, cur, old);
        if (!next.ptr || !next.len) break;
        char *path = fa_strndup(next.ptr, next.len);
        if (!path) return FULTA_ARC_ERR_NOMEM;
        const fulta_arc_source_t *src;
        fulta_arc_err_t e = fa_volume(arc, k, path, &src);
        fa_free(path);
        if (e == FULTA_ARC_ERR_VOLUME_MISSING) break;
        if (e) return e;
        if ((e = add_volume(g, src))) return e;
        cur = next;
    }
    return FULTA_ARC_OK;
}

static fulta_arc_err_t make_arena(rar_glue_t *g, size_t size) {
    fa_free(g->backing);
    g->backing = fa_malloc(size);
    if (!g->backing) return FULTA_ARC_ERR_NOMEM;
    g->arena = proven_arena_create((proven_mem_mut_t){.ptr = g->backing, .size = size});
    return FULTA_ARC_OK;
}

static fulta_arc_err_t rar_open(fulta_arc_t *arc, uint64_t search) {
    (void)search;   /* rar.c finds a signature after an SFX stub itself */
    rar_glue_t *g = fa_calloc(1, sizeof *g);
    if (!g) return FULTA_ARC_ERR_NOMEM;
    arc->state = g;
    fa_rar_set_codec(fa_rarcodec());
    fulta_arc_err_t e = add_volume(g, arc->volumes[0]);
    if (e) return e;
    if (search == UINT64_MAX && !fa_rar_is_rar(g->vols[0].data, g->vols[0].size)) return FULTA_ARC_ERR_NOT_ARCHIVE;
    size_t arena_size = (size_t)1 << 20;
    if ((e = make_arena(g, arena_size))) return e;
    if ((e = collect_volumes(arc, g))) return e;
    /* headers: plain, or encrypted (ask for the password) */
    for (uint32_t attempt = 0;;) {
        const char *pw = "";
        if (attempt > 0) {
            e = fa_password(arc, attempt - 1, &pw);
            if (e) return attempt > 1 ? FULTA_ARC_ERR_PASSWORD_WRONG : e;
        }
        proven_arena_reset(&g->arena);
        fa_rar_result_t r = fa_rar_open_volumes(&g->arena, g->vols, g->nvol, (u8str_t){pw, strlen(pw)});
        if (r.err == FA_RAR_ERR_OUT_OF_MEMORY && arena_size < ((size_t)1 << 30)) {
            arena_size *= 4;
            if ((e = make_arena(g, arena_size))) return e;
            continue;
        }
        if (r.err == FA_RAR_ERR_ENCRYPTED || r.err == FA_RAR_ERR_BAD_PASSWORD) {
            attempt++;
            if (attempt > 64) return FULTA_ARC_ERR_PASSWORD_WRONG;
            continue;
        }
        if (r.err) return map_err(r.err);
        g->ar = r.value;
        g->open = true;
        if (*pw) fa_password_ok(arc, pw);
        break;
    }
    g->map = fa_calloc(g->ar.entry_count ? g->ar.entry_count : 1, sizeof *g->map);
    if (!g->map) return FULTA_ARC_ERR_NOMEM;
    uint32_t rar_group = 0;   /* solid-group counter (API: per-entry solid_group) */
    for (size_t i = 0; i < g->ar.entry_count; i++) {
        const fa_rar_entry_t *re = &g->ar.entries[i];
        bool name_nu = false;
        char *uname = re->name_is_legacy
                          ? fa_decode_name(arc, (const uint8_t *)re->name.ptr, re->name.len, false, FULTA_ARC_CP_437, &name_nu)
                          : fa_strndup(re->name.ptr, re->name.len);
        if (!uname) return FULTA_ARC_ERR_NOMEM;
        char method[32];
        if (re->method == 0) snprintf(method, sizeof method, "Store");
        else snprintf(method, sizeof method, "RAR%u.%u", re->method / 10, re->method % 10);
        fa_entry_t *en = fa_add_entry(arc, (const uint8_t *)re->name.ptr, re->name.len, uname, method);
        fa_free(uname);
        if (!en) return FULTA_ARC_ERR_NOMEM;
        en->pub.size = re->size == FA_RAR_SIZE_UNKNOWN ? 0 : re->size;
        en->pub.packed_size = re->packed_size;
        if (re->size == FA_RAR_SIZE_UNKNOWN) en->pub.flags |= FULTA_ARC_ENTRY_UNKNOWN_SIZE;
        if (re->has_crc && !re->hash_mac && re->piece_count) { en->pub.crc32 = re->crc32; en->pub.flags |= FULTA_ARC_ENTRY_HAS_CRC32; }
        if (re->encrypted) en->pub.flags |= FULTA_ARC_ENTRY_ENCRYPTED;
        if (re->solid) en->pub.flags |= FULTA_ARC_ENTRY_SOLID;
        else rar_group++;   /* a non-solid entry starts a new solid group */
        en->pub.solid_group = rar_group;
        if (name_nu) en->pub.flags |= FULTA_ARC_ENTRY_NAME_NOT_UNICODE;
        if (re->piece_count > 1) en->pub.flags |= FULTA_ARC_ENTRY_SPLIT;
        if (re->method == 15 || re->method == 255 || re->crypt == FA_RAR_CRYPT_OLD || re->split)
            en->pub.flags |= FULTA_ARC_ENTRY_UNSUPPORTED;
        g->map[arc->count - 1] = i;
    }
    return FULTA_ARC_OK;
}

typedef struct sink_ctx { const fulta_arc_sink_t *sink; fulta_arc_err_t err; const fulta_arc_t *arc; } sink_ctx_t;

static bool rar_write(void *ctx, const uint8_t *data, size_t size) {
    sink_ctx_t *s = ctx;
    if (fa_cancelled(s->arc)) { s->err = FULTA_ARC_ERR_CANCELLED; return false; }
    if (!s->sink) return true;
    s->err = s->sink->write(s->sink->ctx, data, size);
    return s->err == FULTA_ARC_OK;
}

static fulta_arc_err_t rar_extract(fulta_arc_t *arc, size_t index, const fulta_arc_sink_t *sink) {
    rar_glue_t *g = arc->state;
    size_t ri = g->map[index];
    if (arc->entries[index].pub.flags & FULTA_ARC_ENTRY_UNSUPPORTED) {
        const fa_rar_entry_t *re = &g->ar.entries[ri];
        return re->split ? FULTA_ARC_ERR_VOLUME_MISSING : FULTA_ARC_ERR_UNSUPPORTED;
    }
    if (fa_rar_needs_password(&g->ar)) {
        for (uint32_t attempt = 0;; attempt++) {
            const char *pw;
            fulta_arc_err_t e = fa_password(arc, attempt, &pw);
            if (e) return attempt ? FULTA_ARC_ERR_PASSWORD_WRONG : e;
            fa_rar_err_t r = fa_rar_set_password(&g->ar, (u8str_t){pw, strlen(pw)});
            if (r == FA_RAR_OK) { fa_password_ok(arc, pw); break; }
            if (r != FA_RAR_ERR_BAD_PASSWORD) return map_err(r);
            if (attempt > 64) return FULTA_ARC_ERR_PASSWORD_WRONG;
        }
    }
    sink_ctx_t sc = {sink, FULTA_ARC_OK, arc};
    fa_rar_err_t r = fa_rar_extract(&g->ar, ri, rar_write, &sc);
    if (sc.err) return sc.err;
    return map_err(r);
}

static void rar_close(fulta_arc_t *arc) {
    rar_glue_t *g = arc->state;
    if (!g) return;
    if (g->open) fa_rar_close(&g->ar);
    for (size_t i = 0; i < g->nvol; i++) fa_view_close(&g->views[i]);
    fa_free(g->views);
    fa_free(g->vols);
    fa_free(g->map);
    fa_free(g->backing);
    fa_free(g);
    arc->state = NULL;
}

const fa_format_ops_t fa_rar_ops = {FULTA_ARC_FORMAT_RAR, rar_probe, rar_open, rar_extract, rar_close, NULL, NULL};
