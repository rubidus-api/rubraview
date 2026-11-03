/* source.c - FultaArc: random-access sources over files (proven_c_lib's fs: UTF-8 paths on every platform) and
 * memory, the default volume resolver (files next to the first one), and the write-to-file sink. MIT. */
#include "../core/internal.h"

#include "proven.h"

/* A file source takes no allocator (it is public and made before any archive): it lives in proven's heap. */
typedef struct file_src { proven_file_t f; } file_src_t;

static fulta_arc_err_t file_read_at(void *ctx, uint64_t offset, void *buf, size_t n, size_t *got) {
    file_src_t *fs = ctx;
    *got = 0;
    if (!n) return FULTA_ARC_OK;
    proven_result_size_t r = proven_fs_pread(fs->f, (proven_mem_mut_t){.ptr = buf, .size = n}, offset);
    if (r.err == PROVEN_ERR_EOF) return FULTA_ARC_OK;
    if (r.err) return FULTA_ARC_ERR_IO;
    *got = r.value;
    return FULTA_ARC_OK;
}

static void file_close(void *ctx) {
    file_src_t *fs = ctx;
    proven_err_t ignored = proven_fs_close(fs->f);   /* a read-only file: nothing to report */
    (void)ignored;
    fa_release(proven_heap_allocator(), fs);
}

fulta_arc_err_t fulta_arc_source_file(const char *path, fulta_arc_source_t *out) {
    if (!path || !out) return FULTA_ARC_ERR_INVALID_ARG;
    file_src_t *fs = fa_zalloc(proven_heap_allocator(), 1, sizeof *fs);
    if (!fs) return FULTA_ARC_ERR_NOMEM;
    proven_result_file_t r = proven_fs_open(proven_heap_allocator(), proven_u8str_view_from_cstr(path), PROVEN_FS_READ);
    if (r.err) { fa_release(proven_heap_allocator(), fs); return r.err == PROVEN_ERR_NOT_FOUND ? FULTA_ARC_ERR_VOLUME_MISSING : FULTA_ARC_ERR_IO; }
    fs->f = r.value;
    proven_result_size_t sz = proven_fs_size(fs->f);
    if (sz.err) {
        proven_err_t ignored = proven_fs_close(fs->f);
        (void)ignored;
        fa_release(proven_heap_allocator(), fs);
        return FULTA_ARC_ERR_IO;
    }
    *out = (fulta_arc_source_t){.ctx = fs, .size = sz.value, .read_at = file_read_at, .close = file_close};
    return FULTA_ARC_OK;
}

/* A memory source lives in `alloc` (proven's heap for the public constructors, which take no allocator; the
 * archive's for an owned buffer), and an owned buffer is released through it too. */
typedef struct mem_src { proven_allocator_t alloc; const uint8_t *p; size_t n; uint8_t *owned; } mem_src_t;

static fulta_arc_err_t mem_read_at(void *ctx, uint64_t offset, void *buf, size_t n, size_t *got) {
    const mem_src_t *m = ctx;
    *got = 0;
    if (offset >= m->n) return FULTA_ARC_OK;
    size_t left = m->n - (size_t)offset;
    if (n > left) n = left;
    memcpy(buf, m->p + offset, n);
    *got = n;
    return FULTA_ARC_OK;
}

static void mem_close(void *ctx) {
    mem_src_t *m = ctx;
    if (!m) return;
    proven_allocator_t a = m->alloc;
    fa_release(a, m->owned);   /* NULL for a referencing source; the owned buffer for fa_source_memory_owned */
    fa_release(a, m);
}

fulta_arc_source_t fulta_arc_source_memory(const void *data, size_t size) {
    proven_allocator_t a = proven_heap_allocator();
    mem_src_t *m = fa_zalloc(a, 1, sizeof *m);
    if (!m) return (fulta_arc_source_t){0};
    m->alloc = a;
    m->p = data;
    m->n = size;
    return (fulta_arc_source_t){.ctx = m, .size = size, .read_at = mem_read_at, .close = mem_close};
}

fulta_arc_source_t fa_source_memory_owned(proven_allocator_t a, uint8_t *buf, size_t size) {
    mem_src_t *m = fa_zalloc(a, 1, sizeof *m);
    if (!m) return (fulta_arc_source_t){0};
    m->alloc = a;
    m->p = buf;
    m->n = size;
    m->owned = buf;   /* freed by mem_close */
    return (fulta_arc_source_t){.ctx = m, .size = size, .read_at = mem_read_at, .close = mem_close};
}

/* Default resolver: open the named file. */
static fulta_arc_err_t file_volume(void *ctx, uint32_t index, const char *name, fulta_arc_source_t *out) {
    (void)ctx;
    (void)index;
    if (!name) return FULTA_ARC_ERR_VOLUME_MISSING;
    return fulta_arc_source_file(name, out);
}

fulta_arc_err_t fulta_arc_open_file(const char *path, const fulta_arc_options_t *options, fulta_arc_t **out) {
    if (!path || !out) return FULTA_ARC_ERR_INVALID_ARG;
    *out = NULL;
    fulta_arc_source_t src;
    fulta_arc_err_t e = fulta_arc_source_file(path, &src);
    if (e) return e == FULTA_ARC_ERR_VOLUME_MISSING ? FULTA_ARC_ERR_IO : e;
    fulta_arc_options_t o = options ? *options : (fulta_arc_options_t){0};
    if (!o.name) o.name = path;
    if (!o.volume) o.volume = file_volume;
    return fulta_arc_open(&src, &o, out);
}

fulta_arc_err_t fulta_arc_open_memory(const void *data, size_t size, const fulta_arc_options_t *options,
                                      fulta_arc_t **out) {
    if (!out || (!data && size)) return FULTA_ARC_ERR_INVALID_ARG;
    *out = NULL;
    fulta_arc_source_t src = fulta_arc_source_memory(data, size);
    if (!src.ctx) return FULTA_ARC_ERR_NOMEM;
    return fulta_arc_open(&src, options, out);
}

/* ---- writing to a file ---------------------------------------------------------------------------------------- */

static fulta_arc_err_t fw_write(void *ctx, const void *data, size_t n) {
    proven_file_t *f = ctx;
    return proven_fs_write_all(*f, (proven_mem_view_t){.ptr = data, .size = n}) ? FULTA_ARC_ERR_IO : FULTA_ARC_OK;
}

static fulta_arc_err_t fw_patch(void *ctx, uint64_t offset, const void *data, size_t n) {
    proven_file_t *f = ctx;
    proven_result_size_t r = proven_fs_pwrite(*f, (proven_mem_view_t){.ptr = data, .size = n}, offset);
    return r.err || r.value != n ? FULTA_ARC_ERR_IO : FULTA_ARC_OK;
}

fulta_arc_err_t fulta_arc_write_file(const char *path, fulta_arc_format_t format,
                                     const fulta_arc_write_entry_t *entries, size_t count,
                                     const fulta_arc_write_options_t *options) {
    if (!path) return FULTA_ARC_ERR_INVALID_ARG;
    if (options && options->volume_size) return fa_write_file_split(path, format, entries, count, options);
    proven_result_file_t r = proven_fs_open(proven_heap_allocator(), proven_u8str_view_from_cstr(path),
                                            PROVEN_FS_WRITE | PROVEN_FS_CREATE | PROVEN_FS_TRUNC);
    if (r.err) return FULTA_ARC_ERR_IO;
    proven_file_t f = r.value;
    fulta_arc_write_sink_t sink = {.ctx = &f, .write = fw_write, .patch = fw_patch};
    fulta_arc_err_t e = fulta_arc_write(format, entries, count, options, &sink);
    if (proven_fs_close(f) && !e) e = FULTA_ARC_ERR_IO;
    if (e) {
        proven_err_t ignored = proven_fs_remove(proven_heap_allocator(), proven_u8str_view_from_cstr(path));
        (void)ignored;
    }
    return e;
}

/* ---- views ------------------------------------------------------------------------------------------------- */

enum { VIEW_EMPTY, VIEW_MEMORY, VIEW_MMAP, VIEW_HEAP };

fulta_arc_err_t fa_view_open(proven_allocator_t a, const fulta_arc_source_t *s, fa_view_t *v) {
    *v = (fa_view_t){.alloc = a};
    if (s->size > SIZE_MAX) return FULTA_ARC_ERR_LIMIT;
    if (s->size == 0) { v->bytes = fa_bytes("", 0); v->kind = VIEW_EMPTY; return FULTA_ARC_OK; }
    if (s->read_at == mem_read_at) {
        const mem_src_t *m = s->ctx;
        v->bytes = fa_bytes(m->p, m->n);
        v->kind = VIEW_MEMORY;
        return FULTA_ARC_OK;
    }
    if (s->read_at == file_read_at) {
        const file_src_t *fs = s->ctx;
        proven_mmap_t *mm = fa_zalloc(a, 1, sizeof *mm);
        if (!mm) return FULTA_ARC_ERR_NOMEM;
        proven_result_mmap_t r = proven_mmap_create(fs->f, 0, (size_t)s->size, PROVEN_MMAP_READ, PROVEN_MMAP_PRIVATE);
        if (!r.err) {
            *mm = r.value;
            v->bytes = fa_bytes(mm->ptr, (size_t)s->size);
            v->kind = VIEW_MMAP;
            v->priv = mm;
            return FULTA_ARC_OK;
        }
        fa_release(a, mm);   /* fall back to reading it in */
    }
    uint8_t *b = fa_alloc(a, (size_t)s->size);
    if (!b) return FULTA_ARC_ERR_NOMEM;
    fulta_arc_err_t e = fa_source_read_exact(s, 0, fa_bytes_mut(b, (size_t)s->size));
    if (e) { fa_release(a, b); return e; }
    v->bytes = fa_bytes(b, (size_t)s->size);
    v->kind = VIEW_HEAP;
    v->priv = b;
    return FULTA_ARC_OK;
}

void fa_view_close(fa_view_t *v) {
    if (v->kind == VIEW_MMAP) {
        proven_err_t ignored = proven_mmap_destroy(v->priv);
        (void)ignored;
        fa_release(v->alloc, v->priv);
    } else if (v->kind == VIEW_HEAP) {
        fa_release(v->alloc, v->priv);
    }
    *v = (fa_view_t){0};
}
