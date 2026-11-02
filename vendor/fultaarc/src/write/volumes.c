/* volumes.c - FultaArc split output: one archive written as volumes of a fixed size (D-035).
 *
 * The writers (src/write/write.c) see one ordinary sink: appends and patches at logical offsets. The volume writer
 * behind it cuts the bytes into volumes of `volume_size`, maps each patch to the volume that holds that offset, and
 * hands volumes to the caller's fulta_arc_volume_sink_t: opened when the first byte reaches them, closed once no
 * patch can reach them, then committed or discarded as a set by finish(). A 7z set is a plain byte cut (7-Zip's
 * `name.7z.001`, ...); only volume 0 is ever patched (the signature header), so later volumes close as soon as they
 * are full. A ZIP writer is told the room left in the volume, so it can keep a header whole, and settles the offset
 * below which it will patch no more (fa_vol_settle), which closes the volumes before it. */
#include "../core/internal.h"

#include "proven.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct fa_vol_part {
    fulta_arc_write_sink_t s;
    uint64_t start, len;
    bool open;
} fa_vol_part_t;

struct fa_vol {
    const fulta_arc_volume_sink_t *vs;
    uint64_t vsize;
    bool zip;                 /* volumes close when settled, not when full */
    bool full;                /* the current volume takes no more bytes (fa_vol_next) */
    uint32_t n;               /* volumes opened */
    uint32_t cap;
    fa_vol_part_t *v;
    uint64_t at;              /* logical bytes written */
};

static fulta_arc_err_t vol_close(fa_vol_t *w, uint32_t i) {
    if (!w->v[i].open) return FULTA_ARC_OK;
    w->v[i].open = false;
    return w->vs->close ? w->vs->close(w->vs->ctx, i) : FULTA_ARC_OK;
}

static fulta_arc_err_t vol_open_next(fa_vol_t *w) {
    if (w->n == UINT32_MAX) return FULTA_ARC_ERR_LIMIT;
    if (w->n && !w->zip && w->n - 1 > 0) {       /* 7z: a full volume past the first is never patched */
        fulta_arc_err_t e = vol_close(w, w->n - 1);
        if (e) return e;
    }
    if (w->n == w->cap) {
        uint32_t nc = w->cap ? w->cap * 2 : 16;
        fa_vol_part_t *nv = realloc(w->v, (size_t)nc * sizeof *nv);
        if (!nv) return FULTA_ARC_ERR_NOMEM;
        w->v = nv; w->cap = nc;
    }
    fa_vol_part_t *p = &w->v[w->n];
    *p = (fa_vol_part_t){.start = w->at};
    fulta_arc_err_t e = w->vs->open(w->vs->ctx, w->n, &p->s);
    if (e) return e;
    if (!p->s.write || !p->s.patch) return FULTA_ARC_ERR_INVALID_ARG;
    p->open = true;
    w->n++;
    w->full = false;
    return FULTA_ARC_OK;
}

static bool cur_full(const fa_vol_t *w) {
    return !w->n || w->full || w->v[w->n - 1].len == w->vsize;
}

fulta_arc_err_t fa_vol_write(void *ctx, const void *data, size_t n) {
    fa_vol_t *w = ctx;
    const uint8_t *p = data;
    while (n) {
        if (cur_full(w)) {
            fulta_arc_err_t e = vol_open_next(w);
            if (e) return e;
        }
        fa_vol_part_t *c = &w->v[w->n - 1];
        uint64_t room = w->vsize - c->len;
        size_t k = n < room ? n : (size_t)room;
        fulta_arc_err_t e = c->s.write(c->s.ctx, p, k);
        if (e) return e;
        c->len += k; w->at += k; p += k; n -= k;
    }
    return FULTA_ARC_OK;
}

static fulta_arc_err_t fa_vol_patch(void *ctx, uint64_t off, const void *data, size_t n) {
    fa_vol_t *w = ctx;
    const uint8_t *p = data;
    if (off > w->at || n > w->at - off) return FULTA_ARC_ERR_IO;
    uint32_t i = 0;
    while (i < w->n && w->v[i].start + w->v[i].len <= off) i++;   /* volumes are few next to the bytes in them */
    while (n) {
        if (i >= w->n || !w->v[i].open) return FULTA_ARC_ERR_IO;     /* a closed volume: a writer bug */
        fa_vol_part_t *c = &w->v[i];
        uint64_t in = off - c->start, left = c->len - in;
        size_t k = n < left ? n : (size_t)left;
        fulta_arc_err_t e = c->s.patch(c->s.ctx, in, p, k);
        if (e) return e;
        off += k; p += k; n -= k; i++;
    }
    return FULTA_ARC_OK;
}

fa_vol_t *fa_vol_of(const fulta_arc_write_sink_t *sink) {
    return sink && sink->write == fa_vol_write ? (fa_vol_t *)sink->ctx : NULL;
}

uint64_t fa_vol_room(const fa_vol_t *w) {
    return cur_full(w) ? w->vsize : w->vsize - w->v[w->n - 1].len;
}

void fa_vol_next(fa_vol_t *w) {
    if (w->n && w->v[w->n - 1].len) w->full = true;   /* an empty volume is already a fresh start */
}

uint32_t fa_vol_disk(const fa_vol_t *w) {
    return cur_full(w) ? w->n : w->n - 1;
}

uint64_t fa_vol_disk_offset(const fa_vol_t *w) {
    return cur_full(w) ? 0 : w->v[w->n - 1].len;
}

uint32_t fa_vol_count(const fa_vol_t *w) {
    return w->n;
}

fulta_arc_err_t fa_vol_settle(fa_vol_t *w, uint64_t off) {
    for (uint32_t i = 1; i + 1 < w->n; i++) {
        if (w->v[i].open && w->v[i].start + w->v[i].len <= off) {
            fulta_arc_err_t e = vol_close(w, i);
            if (e) return e;
        }
    }
    return FULTA_ARC_OK;
}

/* Run `body` against a volume writer over `vs`, then close every volume and commit or discard the set. */
fulta_arc_err_t fa_vol_run(fulta_arc_format_t format, uint64_t volume_size, const fulta_arc_volume_sink_t *vs,
                           fulta_arc_err_t (*body)(void *ctx, const fulta_arc_write_sink_t *sink), void *ctx) {
    if (!vs || !vs->open || !vs->finish) return FULTA_ARC_ERR_INVALID_ARG;
    if (format != FULTA_ARC_FORMAT_ZIP && format != FULTA_ARC_FORMAT_7Z) return FULTA_ARC_ERR_INVALID_ARG;
    if (volume_size < FULTA_ARC_VOLUME_MIN) return FULTA_ARC_ERR_INVALID_ARG;
    fa_vol_t w = {.vs = vs, .vsize = volume_size, .zip = format == FULTA_ARC_FORMAT_ZIP};
    fulta_arc_write_sink_t front = {.ctx = &w, .write = fa_vol_write, .patch = fa_vol_patch};
    fulta_arc_err_t e = body(ctx, &front);
    if (!e && w.n > 65535u && w.zip) e = FULTA_ARC_ERR_LIMIT;   /* the reader takes no more disks than this */
    for (uint32_t i = 0; i < w.n; i++) {
        fulta_arc_err_t ce = vol_close(&w, i);
        if (ce && !e) e = ce;
    }
    fulta_arc_err_t fe = vs->finish(vs->ctx, w.n, e == FULTA_ARC_OK);
    if (fe && !e) e = fe;
    free(w.v);
    return e;
}

/* ---- names ---------------------------------------------------------------------------------------------------- */

fulta_arc_err_t fulta_arc_volume_name(fulta_arc_format_t format, const char *set_name, uint32_t index,
                                      uint32_t count, char *buf, size_t cap) {
    if (!set_name || !buf || index >= count) return FULTA_ARC_ERR_INVALID_ARG;
    size_t L = strlen(set_name);
    int k;
    if (format == FULTA_ARC_FORMAT_7Z) {
        k = snprintf(buf, cap, "%s.%03u", set_name, (unsigned)index + 1);
    } else if (format == FULTA_ARC_FORMAT_ZIP) {
        if (index + 1 == count) {
            k = snprintf(buf, cap, "%s", set_name);
        } else {                                   /* the extension replaced by .zNN (zip.md 7.4) */
            size_t stem = L;
            for (size_t i = L; i > 0; i--) {
                char c = set_name[i - 1];
                if (c == '/' || c == '\\') break;
                if (c == '.') { stem = i - 1; break; }
            }
            k = snprintf(buf, cap, "%.*s.z%02u", (int)stem, set_name, (unsigned)index + 1);
        }
    } else {
        return FULTA_ARC_ERR_INVALID_ARG;
    }
    return k < 0 || (size_t)k >= cap ? FULTA_ARC_ERR_LIMIT : FULTA_ARC_OK;
}

/* ---- the file volume sink: temporaries beside the set name, renamed together on success ----------------------- */

typedef struct fv_file {
    proven_file_t f;
    bool open;
} fv_file_t;

typedef struct fv_set {
    const char *path;
    fulta_arc_format_t format;
    fv_file_t **files;        /* one allocation each: a volume's sink keeps its address */
    uint32_t n, cap;
    char *tmp;                /* scratch for names */
    size_t tmpcap;
} fv_set_t;

static fulta_arc_err_t fv_write(void *ctx, const void *data, size_t n) {
    fv_file_t *f = ctx;
    return proven_fs_write_all(f->f, (proven_mem_view_t){.ptr = data, .size = n}) ? FULTA_ARC_ERR_IO : FULTA_ARC_OK;
}

static fulta_arc_err_t fv_patch(void *ctx, uint64_t offset, const void *data, size_t n) {
    fv_file_t *f = ctx;
    proven_result_size_t r = proven_fs_pwrite(f->f, (proven_mem_view_t){.ptr = data, .size = n}, offset);
    return r.err || r.value != n ? FULTA_ARC_ERR_IO : FULTA_ARC_OK;
}

/* `<path>.fatmp<index>`: one directory with the set, so the renames stay on one volume. */
static const char *fv_tmp_name(fv_set_t *s, uint32_t index) {
    snprintf(s->tmp, s->tmpcap, "%s.fatmp%u", s->path, (unsigned)index);
    return s->tmp;
}

static fulta_arc_err_t fv_open(void *ctx, uint32_t index, fulta_arc_write_sink_t *out) {
    fv_set_t *s = ctx;
    if (index != s->n) return FULTA_ARC_ERR_INVALID_ARG;
    if (s->n == s->cap) {
        uint32_t nc = s->cap ? s->cap * 2 : 16;
        fv_file_t **nf = realloc(s->files, (size_t)nc * sizeof *nf);
        if (!nf) return FULTA_ARC_ERR_NOMEM;
        s->files = nf; s->cap = nc;
    }
    fv_file_t *f = malloc(sizeof *f);
    if (!f) return FULTA_ARC_ERR_NOMEM;
    proven_result_file_t r = proven_fs_open(proven_heap_allocator(), proven_u8str_view_from_cstr(fv_tmp_name(s, index)),
                                            PROVEN_FS_WRITE | PROVEN_FS_CREATE | PROVEN_FS_TRUNC);
    if (r.err) { free(f); return FULTA_ARC_ERR_IO; }
    *f = (fv_file_t){.f = r.value, .open = true};
    s->files[s->n++] = f;
    out->ctx = f;
    out->write = fv_write;
    out->patch = fv_patch;
    return FULTA_ARC_OK;
}

static fulta_arc_err_t fv_close(void *ctx, uint32_t index) {
    fv_set_t *s = ctx;
    if (index >= s->n || !s->files[index]->open) return FULTA_ARC_OK;
    s->files[index]->open = false;
    return proven_fs_close(s->files[index]->f) ? FULTA_ARC_ERR_IO : FULTA_ARC_OK;
}

static bool fv_exists(const char *name) {
    proven_result_file_t r = proven_fs_open(proven_heap_allocator(), proven_u8str_view_from_cstr(name), PROVEN_FS_READ);
    if (r.err) return false;
    proven_err_t ig = proven_fs_close(r.value); (void)ig;
    return true;
}

static void fv_remove(const char *name) {
    proven_err_t ig = proven_fs_remove(proven_heap_allocator(), proven_u8str_view_from_cstr(name)); (void)ig;
}

static fulta_arc_err_t fv_finish(void *ctx, uint32_t count, int ok) {
    fv_set_t *s = ctx;
    fulta_arc_err_t e = ok ? FULTA_ARC_OK : FULTA_ARC_ERR_IO;
    char *fin = malloc(s->tmpcap);
    if (!fin) e = FULTA_ARC_ERR_NOMEM;
    /* a continuation volume past the new set's last would join it when read: refuse rather than delete it */
    if (!e && count) {
        const char *next = NULL;
        if (s->format == FULTA_ARC_FORMAT_7Z) {
            if (!fulta_arc_volume_name(s->format, s->path, count, count + 1, fin, s->tmpcap)) next = fin;
        } else if (!fulta_arc_volume_name(s->format, s->path, count - 1, count + 1, fin, s->tmpcap)) {
            next = fin;                            /* the .zNN after the last .z(count-1) */
        }
        if (next && fv_exists(next)) e = FULTA_ARC_ERR_EXISTS;
    }
    uint32_t done = 0;
    for (; !e && done < count; done++) {
        if (fulta_arc_volume_name(s->format, s->path, done, count, fin, s->tmpcap)) { e = FULTA_ARC_ERR_LIMIT; break; }
        if (proven_fs_rename(proven_heap_allocator(), proven_u8str_view_from_cstr(fv_tmp_name(s, done)),
                             proven_u8str_view_from_cstr(fin)))
            e = FULTA_ARC_ERR_IO;
        if (e) break;
    }
    if (e) {                                       /* what was renamed, then every temporary left */
        for (uint32_t i = 0; fin && i < done; i++)
            if (!fulta_arc_volume_name(s->format, s->path, i, count, fin, s->tmpcap)) fv_remove(fin);
        for (uint32_t i = done; i < s->n; i++) fv_remove(fv_tmp_name(s, i));
    }
    free(fin);
    return e;
}

fulta_arc_err_t fa_vol_files(const char *path, fulta_arc_format_t format, uint64_t volume_size,
                             fulta_arc_err_t (*body)(void *ctx, const fulta_arc_write_sink_t *sink), void *ctx) {
    if (!path) return FULTA_ARC_ERR_INVALID_ARG;
    fv_set_t s = {.path = path, .format = format, .tmpcap = strlen(path) + 32};
    s.tmp = malloc(s.tmpcap);
    if (!s.tmp) return FULTA_ARC_ERR_NOMEM;
    fulta_arc_volume_sink_t vs = {.ctx = &s, .open = fv_open, .close = fv_close, .finish = fv_finish};
    fulta_arc_err_t e = fa_vol_run(format, volume_size, &vs, body, ctx);
    for (uint32_t i = 0; i < s.n; i++) {            /* fa_vol_run closed them all; free what they were */
        if (s.files[i]->open) { proven_err_t ig = proven_fs_close(s.files[i]->f); (void)ig; }
        free(s.files[i]);
    }
    free(s.files); free(s.tmp);
    return e;
}
