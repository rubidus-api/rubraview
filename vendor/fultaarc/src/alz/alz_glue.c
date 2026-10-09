/* alz_glue.c - FultaArc: the ALZ reader (alz.c) behind FultaArc's API. MIT.
 *
 * alz.c's container parser is transcribed from the black-box clean room fultaarc-cleanroom-alzegg (ae_alz.c +
 * docs/spec/alz.md, 1f844a6; D-017, replacing the earlier unalz-derived reader for provenance); its bzip2 is
 * vendor/bzip2_alz with ALZ's framing, kept apart from the standard bzip2 (D-005 #2). This file: volumes found by
 * name (name.a00, ...) and checked, the password callback
 * (UTF-8, then CP949), names in a code page (CP949 by default), extraction into sinks. */
#include "../core/internal.h"

#include "alz.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct alz_glue {
    proven_arena_t arena;
    void *backing;
    fa_view_t *views;
    fa_alz_volume_t *vols;
    size_t nvol;
    fa_alz_archive_t ar;
    bool open;
} alz_glue_t;

static bool alz_probe(const uint8_t *h, size_t n, uint64_t *off) {
    *off = 0;
    return n >= 4 && memcmp(h, "ALZ\x01", 4) == 0;
}

static fulta_arc_err_t map_err(fa_alz_err_t e) {
    switch (e) {
    case FA_ALZ_OK: return FULTA_ARC_OK;
    case FA_ALZ_ERR_NOT_AN_ALZ: return FULTA_ARC_ERR_NOT_ARCHIVE;
    case FA_ALZ_ERR_CORRUPT_STREAM: return FULTA_ARC_ERR_CHECKSUM;
    case FA_ALZ_ERR_TRUNCATED: return FULTA_ARC_ERR_TRUNCATED;
    case FA_ALZ_ERR_ENCRYPTED: return FULTA_ARC_ERR_PASSWORD_NEEDED;
    case FA_ALZ_ERR_BAD_PASSWORD: return FULTA_ARC_ERR_PASSWORD_WRONG;
    case FA_ALZ_ERR_UNSUPPORTED: return FULTA_ARC_ERR_UNSUPPORTED;
    case FA_ALZ_ERR_TOO_LARGE: return FULTA_ARC_ERR_LIMIT;
    case FA_ALZ_ERR_OUT_OF_MEMORY: return FULTA_ARC_ERR_NOMEM;
    case FA_ALZ_ERR_BAD_INDEX: return FULTA_ARC_ERR_INVALID_ARG;
    }
    return FULTA_ARC_ERR_CORRUPT;
}

static fulta_arc_err_t add_volume(alz_glue_t *g, const fulta_arc_source_t *src) {
    fa_view_t *v = fa_realloc(g->views, (g->nvol + 1) * sizeof *v);
    if (!v) return FULTA_ARC_ERR_NOMEM;
    g->views = v;
    fa_alz_volume_t *av = fa_realloc(g->vols, (g->nvol + 1) * sizeof *av);
    if (!av) return FULTA_ARC_ERR_NOMEM;
    g->vols = av;
    fulta_arc_err_t e = fa_view_open(src, &g->views[g->nvol]);
    if (e) return e;
    g->vols[g->nvol] = (fa_alz_volume_t){g->views[g->nvol].data, g->views[g->nvol].size};
    g->nvol++;
    return FULTA_ARC_OK;
}

static fulta_arc_err_t alz_open(fulta_arc_t *arc, uint64_t search) {
    if (search == UINT64_MAX) return FULTA_ARC_ERR_NOT_ARCHIVE;   /* ALZ has no stub form */
    alz_glue_t *g = fa_calloc(1, sizeof *g);
    if (!g) return FULTA_ARC_ERR_NOMEM;
    arc->state = g;
    size_t asz = (size_t)1 << 20;
    g->backing = fa_malloc(asz);
    if (!g->backing) return FULTA_ARC_ERR_NOMEM;
    g->arena = proven_arena_create((proven_mem_mut_t){.ptr = g->backing, .size = asz});
    fulta_arc_err_t e = add_volume(g, arc->volumes[0]);
    if (e) return e;
    /* volumes (alz.md 5): a whole .alz ends in "CLZ" 02; otherwise .a00, .a01, ... until the last */
    if (arc->opt.name) {
        u8str_t first = {arc->opt.name, strlen(arc->opt.name)};
        for (unsigned k = 1; k < 2600 && !fa_alz_volume_is_last(g->vols[g->nvol - 1].data, g->vols[g->nvol - 1].size); k++) {
            u8str_t p = fa_alz_volume_path(&g->arena, first, k);
            if (!p.ptr) break;
            char *path = fa_strndup(p.ptr, p.len);
            if (!path) return FULTA_ARC_ERR_NOMEM;
            const fulta_arc_source_t *src;
            e = fa_volume(arc, k, path, &src);
            fa_free(path);
            if (e == FULTA_ARC_ERR_VOLUME_MISSING) break;
            if (e) return e;
            if ((e = add_volume(g, src))) return e;
            int32_t num = fa_alz_volume_number(g->vols[g->nvol - 1].data, g->vols[g->nvol - 1].size);
            if (num >= 0 && (uint32_t)num != k) { fa_view_close(&g->views[--g->nvol]); break; }
        }
    }
    for (;;) {
        proven_arena_reset(&g->arena);
        fa_alz_result_t r = fa_alz_open_volumes(&g->arena, g->vols, g->nvol);
        if (r.err == FA_ALZ_ERR_OUT_OF_MEMORY && asz < ((size_t)1 << 30)) {
            asz *= 4;
            fa_free(g->backing);
            g->backing = fa_malloc(asz);
            if (!g->backing) return FULTA_ARC_ERR_NOMEM;
            g->arena = proven_arena_create((proven_mem_mut_t){.ptr = g->backing, .size = asz});
            continue;
        }
        if (r.err) return map_err(r.err);
        g->ar = r.value;
        g->open = true;
        break;
    }
    for (size_t i = 0; i < g->ar.entry_count; i++) {
        const fa_alz_entry_t *ae = &g->ar.entries[i];
        char *uname = fa_codepage_to_utf8((const uint8_t *)ae->name.ptr, ae->name.len, arc->opt.codepage, FULTA_ARC_CP_949);
        if (!uname) return FULTA_ARC_ERR_NOMEM;
        const char *m = ae->method == FA_ALZ_METHOD_STORED ? "Store" : ae->method == FA_ALZ_METHOD_DEFLATE ? "Deflate"
                      : ae->method == FA_ALZ_METHOD_BZIP2 ? "BZip2-ALZ" : "Unknown";
        char method[32];
        snprintf(method, sizeof method, "%s%s", m, ae->encrypted ? ":ZipCrypto" : "");
        fa_entry_t *en = fa_add_entry(arc, (const uint8_t *)ae->name.ptr, ae->name.len, uname, method);
        fa_free(uname);
        if (!en) return FULTA_ARC_ERR_NOMEM;
        en->pub.size = ae->size;
        en->pub.packed_size = ae->packed_size;
        en->pub.crc32 = ae->crc32;
        en->pub.flags |= FULTA_ARC_ENTRY_HAS_CRC32 | FULTA_ARC_ENTRY_HAS_MTIME;
        en->pub.mtime = fa_dos_time_ns((uint16_t)(ae->dos_time >> 16), (uint16_t)ae->dos_time);
        en->pub.attributes = ae->attributes;
        if (ae->attributes & 0x10) en->pub.flags |= FULTA_ARC_ENTRY_DIR;
        if (ae->encrypted) en->pub.flags |= FULTA_ARC_ENTRY_ENCRYPTED;
        if (g->nvol > 1) en->pub.flags |= FULTA_ARC_ENTRY_SPLIT;
        if (ae->method > 2) en->pub.flags |= FULTA_ARC_ENTRY_UNSUPPORTED;
    }
    return FULTA_ARC_OK;
}

/* A password in the forms ALZip may have stored it: UTF-8 as given, then CP949 (alz.md 7) when it converts. */
static fa_alz_err_t try_password(const fulta_arc_t *arc, fa_alz_archive_t *ar, const char *pw) {
    fa_alz_err_t r = fa_alz_set_password(ar, (u8str_t){pw, strlen(pw)});
    if (r != FA_ALZ_ERR_BAD_PASSWORD) return r;
    fulta_arc_codepage_t cps[2] = {arc->opt.codepage, FULTA_ARC_CP_949};
    for (int k = 0; k < 2; k++) {
        if (cps[k] == FULTA_ARC_CP_AUTO || cps[k] == FULTA_ARC_CP_UTF8 || (k == 1 && cps[0] == FULTA_ARC_CP_949)) continue;
        char *c = fa_utf8_to_codepage(pw, cps[k]);
        if (!c) continue;
        r = fa_alz_set_password(ar, (u8str_t){c, strlen(c)});
        memset(c, 0, strlen(c));
        fa_free(c);
        if (r != FA_ALZ_ERR_BAD_PASSWORD) return r;
    }
    return FA_ALZ_ERR_BAD_PASSWORD;
}

static fulta_arc_err_t alz_extract(fulta_arc_t *arc, size_t index, const fulta_arc_sink_t *sink) {
    alz_glue_t *g = arc->state;
    const fa_alz_entry_t *ae = &g->ar.entries[index];
    if (ae->truncated) return FULTA_ARC_ERR_TRUNCATED;
    if (ae->method > 2) return FULTA_ARC_ERR_UNSUPPORTED;
    if (fa_alz_needs_password(&g->ar)) {
        for (uint32_t attempt = 0;; attempt++) {
            const char *pw;
            fulta_arc_err_t e = fa_password(arc, attempt, &pw);
            if (e) return attempt ? FULTA_ARC_ERR_PASSWORD_WRONG : e;
            fa_alz_err_t r = try_password(arc, &g->ar, pw);
            if (r == FA_ALZ_OK) { fa_password_ok(arc, pw); break; }
            if (r != FA_ALZ_ERR_BAD_PASSWORD) return map_err(r);
            if (attempt > 64) return FULTA_ARC_ERR_PASSWORD_WRONG;
        }
    }
    if (ae->size > SIZE_MAX - 4096) return FULTA_ARC_ERR_LIMIT;
    size_t need = (size_t)ae->size + 4096;
    void *mem = fa_malloc(need);
    if (!mem) return FULTA_ARC_ERR_NOMEM;
    proven_arena_t a = proven_arena_create((proven_mem_mut_t){.ptr = mem, .size = need});
    fa_alz_data_result_t r = fa_alz_read_entry(&a, &g->ar, index, ae->size);
    fulta_arc_err_t e = map_err(r.err);
    if (!e && sink && r.data.len) e = sink->write(sink->ctx, r.data.ptr, r.data.len);
    fa_free(mem);
    return e;
}

static void alz_close(fulta_arc_t *arc) {
    alz_glue_t *g = arc->state;
    if (!g) return;
    if (g->open) fa_alz_close(&g->ar);
    for (size_t i = 0; i < g->nvol; i++) fa_view_close(&g->views[i]);
    fa_free(g->views);
    fa_free(g->vols);
    fa_free(g->backing);
    fa_free(g);
    arc->state = NULL;
}

const fa_format_ops_t fa_alz_ops = {FULTA_ARC_FORMAT_ALZ, alz_probe, alz_open, alz_extract, alz_close, NULL, NULL};
