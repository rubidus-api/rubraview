/* arc.c - FultaArc: opening an archive (format detection), entries, volumes, passwords, extraction. MIT. */
#include "internal.h"

#include <stdlib.h>

/* The formats FultaArc ships. ALZ and EGG (with AZO, EGG's own method) were held out until the
 * fultaarc-cleanroom-alzegg clean room delivered; that work is now integrated (ALZ from the clean room's black-box
 * reader, EGG/AZO from the project readers corrected per its reader-notes). RAR 1.4 / 1.5 archives are read by the
 * fultaarc-cleanroom-rar15 room's reader (fa_rar15_ops, D-029), which hands every other RAR archive to fa_rar_ops. */
static const fa_format_ops_t *const FORMATS[] = {
    &fa_rar15_ops, &fa_rar_ops, &fa_7z_ops, &fa_zip_ops, &fa_alz_ops, &fa_egg_ops,
    &fa_gzip_ops, &fa_bzip2_ops, &fa_xz_ops, &fa_zstd_ops, &fa_compress_ops, &fa_lzip_ops,
    &fa_ar_ops, &fa_cpio_ops, &fa_udf_ops, &fa_iso_ops, &fa_cab_ops, &fa_tar_ops,
    &fa_lha_ops,   /* last: its offset-0 rule is a pattern and its self-extractor search scans 1 MiB (src/lha/lha.c) */
};

const char *fulta_arc_format_name(fulta_arc_format_t f) {
    switch (f) {
    case FULTA_ARC_FORMAT_ZIP: return "ZIP";
    case FULTA_ARC_FORMAT_7Z: return "7z";
    case FULTA_ARC_FORMAT_RAR: return "RAR";
    case FULTA_ARC_FORMAT_ALZ: return "ALZ";
    case FULTA_ARC_FORMAT_EGG: return "EGG";
    case FULTA_ARC_FORMAT_GZIP: return "gzip";
    case FULTA_ARC_FORMAT_BZIP2: return "bzip2";
    case FULTA_ARC_FORMAT_XZ: return "xz";
    case FULTA_ARC_FORMAT_ZSTD: return "zstd";
    case FULTA_ARC_FORMAT_TAR: return "tar";
    case FULTA_ARC_FORMAT_COMPRESS: return "compress";
    case FULTA_ARC_FORMAT_LZIP: return "lzip";
    case FULTA_ARC_FORMAT_AR: return "ar";
    case FULTA_ARC_FORMAT_CPIO: return "cpio";
    case FULTA_ARC_FORMAT_ISO: return "ISO 9660";
    case FULTA_ARC_FORMAT_CAB: return "CAB";
    case FULTA_ARC_FORMAT_UDF: return "UDF";
    case FULTA_ARC_FORMAT_LHA: return "LHA";
    default: return "unknown";
    }
}

const char *fulta_arc_strerror(fulta_arc_err_t e) {
    switch (e) {
    case FULTA_ARC_OK: return "ok";
    case FULTA_ARC_ERR_NOMEM: return "out of memory";
    case FULTA_ARC_ERR_INVALID_ARG: return "invalid argument";
    case FULTA_ARC_ERR_IO: return "input/output error";
    case FULTA_ARC_ERR_NOT_ARCHIVE: return "not a supported archive";
    case FULTA_ARC_ERR_CORRUPT: return "archive is damaged";
    case FULTA_ARC_ERR_TRUNCATED: return "archive is cut short";
    case FULTA_ARC_ERR_UNSUPPORTED: return "not supported";
    case FULTA_ARC_ERR_PASSWORD_NEEDED: return "password needed";
    case FULTA_ARC_ERR_PASSWORD_WRONG: return "wrong password";
    case FULTA_ARC_ERR_CHECKSUM: return "checksum mismatch";
    case FULTA_ARC_ERR_VOLUME_MISSING: return "a volume is missing";
    case FULTA_ARC_ERR_LIMIT: return "a limit was exceeded";
    case FULTA_ARC_ERR_CANCELLED: return "cancelled";
    case FULTA_ARC_ERR_BAD_NAME: return "name may not be stored";
    default: return "error";
    }
}

static void free_all(fulta_arc_t *arc) {
    if (arc->ops && arc->ops->close) arc->ops->close(arc);
    for (size_t i = 0; i < arc->count; i++) {
        fa_free(arc->entries[i].name_owned);
        fa_free(arc->entries[i].raw_owned);
        fa_free(arc->entries[i].method_owned);
    }
    fa_free(arc->entries);
    for (size_t i = 0; i < arc->nvolumes; i++) {
        if (arc->volumes[i]->close) arc->volumes[i]->close(arc->volumes[i]->ctx);
        fa_free(arc->volumes[i]);
    }
    fa_free(arc->volumes);
    fa_free(arc->name);
    if (arc->password) {
        memset(arc->password, 0, strlen(arc->password));
        fa_free(arc->password);
    }
    fa_free(arc);
}

/* ---- transparent unwrap (reprobe): a gzip/bzip2/xz/zstd stream wrapping a container -----------------------------
 * When a single-entry wrapper decompresses to a known container (tar, …), the archive is re-opened on the
 * decompressed bytes so its members list directly (so `tar.gz` lists the tar's files). The payload is materialised
 * to memory, bounded by FA_REPROBE_CAP; a larger payload stays a single stored entry (still fully extractable by
 * streaming). The temp-file spill in the plan is a later refinement; this ships memory-only. See D-020. */

#define FA_REPROBE_CAP       (UINT64_C(1) << 30)   /* 1 GiB: in-memory decompress cap for reprobe */
#define FA_REPROBE_MAX_DEPTH 6                      /* nested-wrapper guard */

static bool is_wrapper(fulta_arc_format_t f) {
    return f == FULTA_ARC_FORMAT_GZIP || f == FULTA_ARC_FORMAT_BZIP2 ||
           f == FULTA_ARC_FORMAT_XZ   || f == FULTA_ARC_FORMAT_ZSTD ||
           f == FULTA_ARC_FORMAT_COMPRESS || f == FULTA_ARC_FORMAT_LZIP;
}

/* The closed list of containers the reprobe promotes to, each paired with the prefix-detector that must agree with
 * it. Adding a container that unwraps through a wrapper is one row here (plus its fa_*_detect). */
static const struct reprobe_container {
    bool (*detect)(const uint8_t *h, size_t n);
    fulta_arc_format_t format;
} REPROBE_CONTAINERS[] = {
    { fa_tar_detect, FULTA_ARC_FORMAT_TAR },
};

static bool is_reprobe_container(fulta_arc_format_t f) {
    for (size_t i = 0; i < sizeof REPROBE_CONTAINERS / sizeof *REPROBE_CONTAINERS; i++)
        if (REPROBE_CONTAINERS[i].format == f) return true;
    return false;
}

/* peek sink: capture up to one 512-byte block of the decompressed stream, then stop (FA_SINK_STOP). */
typedef struct { uint8_t buf[512]; size_t n; } peek_sink_t;
static fulta_arc_err_t peek_write(void *ctx, const void *d, size_t n) {
    peek_sink_t *p = ctx;
    size_t take = sizeof p->buf - p->n;
    if (take > n) take = n;
    memcpy(p->buf + p->n, d, take);
    p->n += take;
    return p->n >= sizeof p->buf ? (fulta_arc_err_t)FA_SINK_STOP : FULTA_ARC_OK;
}

/* grow sink: accumulate the whole decompressed stream into a heap buffer, capped at FA_REPROBE_CAP. */
typedef struct { uint8_t *data; size_t n, cap; bool over; } grow_sink_t;
static fulta_arc_err_t grow_write(void *ctx, const void *d, size_t n) {
    grow_sink_t *g = ctx;
    if (n > FA_REPROBE_CAP - g->n) { g->over = true; return (fulta_arc_err_t)FA_SINK_STOP; }
    if (g->n + n > g->cap) {
        size_t nc = g->cap ? g->cap : 65536;
        while (nc < g->n + n) nc *= 2;
        uint8_t *p = fa_realloc(g->data, nc);
        if (!p) return FULTA_ARC_ERR_NOMEM;
        g->data = p;
        g->cap = nc;
    }
    memcpy(g->data + g->n, d, n);
    g->n += n;
    return FULTA_ARC_OK;
}

static fulta_arc_err_t open_core(const fulta_arc_source_t *source, const fulta_arc_options_t *options,
                                 fulta_arc_t **out, int depth);

/* If `arc` is a single-entry wrapper whose payload is a known container, re-open on the decompressed bytes and hand
 * that inner archive back. Otherwise leave `arc`. Never fails the open: on any trouble the wrapper is kept. */
static fulta_arc_err_t reprobe(fulta_arc_t *arc, const fulta_arc_options_t *options, fulta_arc_t **out, int depth) {
    *out = arc;
    /* count != 1: the wrapper readers list the first member/stream only (T010–T013), so a wrapper is always one
     * entry today; if one ever grows multi-member, it must not be promoted (promotion drops all but the payload). */
    if (depth >= FA_REPROBE_MAX_DEPTH || !is_wrapper(arc->format) || arc->count != 1) return FULTA_ARC_OK;
    if (arc->entries[0].pub.flags & (FULTA_ARC_ENTRY_DIR | FULTA_ARC_ENTRY_UNSUPPORTED)) return FULTA_ARC_OK;

    /* peek the first block; bail cheaply unless it looks like a container we promote to */
    peek_sink_t pk = {0};
    fulta_arc_sink_t psink = { &pk, peek_write };
    fulta_arc_err_t pe = fulta_arc_extract(arc, 0, &psink);
    if (pe && pe != (fulta_arc_err_t)FA_SINK_STOP) return FULTA_ARC_OK;   /* decode failed: keep the wrapper */
    bool container = false;
    for (size_t i = 0; i < sizeof REPROBE_CONTAINERS / sizeof *REPROBE_CONTAINERS; i++)
        if (REPROBE_CONTAINERS[i].detect(pk.buf, pk.n)) { container = true; break; }
    if (!container) return FULTA_ARC_OK;

    /* a container: decompress the whole payload into memory (bounded by the cap) */
    grow_sink_t g = {0};
    fulta_arc_sink_t gsink = { &g, grow_write };
    fulta_arc_err_t ge = fulta_arc_extract(arc, 0, &gsink);
    if ((ge && ge != (fulta_arc_err_t)FA_SINK_STOP) || g.over || g.n == 0) { fa_free(g.data); return FULTA_ARC_OK; }

    fulta_arc_source_t msrc = fa_source_memory_owned(g.data, g.n);
    if (!msrc.ctx) { fa_free(g.data); return FULTA_ARC_OK; }     /* not yet owned: free the buffer */
    /* g.data is now owned by msrc; open_core consumes msrc on success or failure (free_all closes it) */

    fulta_arc_options_t io = options ? *options : (fulta_arc_options_t){0};
    io.name = arc->entries[0].pub.name;   /* the de-suffixed name; open_core strndups it before detection */
    io.volume = NULL;                     /* an unwrapped stream has no sibling volumes */
    io.volume_ctx = NULL;
    fulta_arc_t *inner = NULL;
    fulta_arc_err_t ie = open_core(&msrc, &io, &inner, depth + 1);
    if (ie == FULTA_ARC_OK && inner && is_reprobe_container(inner->format)) {
        fulta_arc_close(arc);             /* drop the wrapper and its original source */
        *out = inner;
        return FULTA_ARC_OK;
    }
    if (inner) fulta_arc_close(inner);    /* not a promotable container: frees msrc + the buffer */
    return FULTA_ARC_OK;                  /* keep the wrapper */
}

fulta_arc_err_t fulta_arc_open(const fulta_arc_source_t *source, const fulta_arc_options_t *options, fulta_arc_t **out) {
    return open_core(source, options, out, 0);
}

static fulta_arc_err_t open_core(const fulta_arc_source_t *source, const fulta_arc_options_t *options,
                                 fulta_arc_t **out, int depth) {
    if (!out) return FULTA_ARC_ERR_INVALID_ARG;
    *out = NULL;
    if (!source || !source->read_at) return FULTA_ARC_ERR_INVALID_ARG;
    fulta_arc_t *arc = fa_calloc(1, sizeof *arc);
    if (!arc) {
        if (source->close) source->close(source->ctx);
        return FULTA_ARC_ERR_NOMEM;
    }
    arc->volumes = fa_calloc(4, sizeof *arc->volumes);
    fulta_arc_source_t *first = fa_malloc(sizeof *first);
    if (!arc->volumes || !first) {
        fa_free(arc->volumes);
        fa_free(first);
        fa_free(arc);
        if (source->close) source->close(source->ctx);
        return FULTA_ARC_ERR_NOMEM;
    }
    *first = *source;
    arc->capvolumes = 4;
    arc->volumes[0] = first;
    arc->nvolumes = 1;
    if (options) arc->opt = *options;
    arc->opt.name = NULL;
    if (options && options->name) {
        arc->name = fa_strndup(options->name, strlen(options->name));
        if (!arc->name) { free_all(arc); return FULTA_ARC_ERR_NOMEM; }
        arc->opt.name = arc->name;
    }
    arc->limits.max_dictionary = arc->opt.max_dictionary ? arc->opt.max_dictionary : (UINT64_C(1) << 30);
    arc->limits.cancel = arc->opt.cancel;
    arc->limits.cancel_ctx = arc->opt.cancel_ctx;
    {   /* back the codec budget with the caller's allocator when supplied; all three fns or none */
        const fulta_arc_allocator_t *ua = &arc->opt.allocator;
        int set = (ua->alloc != NULL) + (ua->realloc != NULL) + (ua->free != NULL);
        if (set != 0 && set != 3) { free_all(arc); return FULTA_ARC_ERR_INVALID_ARG; }
        proven_allocator_t backing = set == 3 ? fa_allocator_from_user(ua) : proven_heap_allocator();
        arc->budget = (fa_budget_t){ .backing = backing, .limit = arc->opt.max_memory };
    }
    arc->limits.budget = &arc->budget;

    /* detection: the head of the file; SFX stubs are searched by the readers that support them */
    uint8_t head[64] = {0};
    size_t n = 0;
    fulta_arc_err_t e = source->read_at(source->ctx, 0, head, sizeof head, &n);
    if (e) { free_all(arc); return e; }
    for (size_t i = 0; i < sizeof FORMATS / sizeof *FORMATS; i++) {
        uint64_t off = 0;
        if (FORMATS[i]->probe && FORMATS[i]->probe(head, n, &off)) {
            arc->ops = FORMATS[i];
            arc->format = FORMATS[i]->format;
            e = arc->ops->open(arc, off);
            if (e) { free_all(arc); return e; }
            return reprobe(arc, options, out, depth);
        }
    }
    /* no signature at the start: let each reader search further (SFX stubs, appended archives) */
    for (size_t i = 0; i < sizeof FORMATS / sizeof *FORMATS; i++) {
        arc->ops = FORMATS[i];
        arc->format = FORMATS[i]->format;
        e = arc->ops->open(arc, UINT64_MAX);    /* UINT64_MAX: search for the signature */
        if (!e) return reprobe(arc, options, out, depth);
        if (arc->ops->close) arc->ops->close(arc);
        arc->state = NULL;
        for (size_t k = 0; k < arc->count; k++) {
            fa_free(arc->entries[k].name_owned);
            fa_free(arc->entries[k].raw_owned);
            fa_free(arc->entries[k].method_owned);
        }
        arc->count = 0;
        if (e != FULTA_ARC_ERR_NOT_ARCHIVE) { arc->ops = NULL; free_all(arc); return e; }
    }
    arc->ops = NULL;
    free_all(arc);
    return FULTA_ARC_ERR_NOT_ARCHIVE;
}

void fulta_arc_close(fulta_arc_t *arc) {
    if (arc) free_all(arc);
}

fulta_arc_format_t fulta_arc_format(const fulta_arc_t *arc) { return arc ? arc->format : FULTA_ARC_FORMAT_UNKNOWN; }
size_t fulta_arc_count(const fulta_arc_t *arc) { return arc ? arc->count : 0; }

const fulta_arc_entry_t *fulta_arc_entry(const fulta_arc_t *arc, size_t index) {
    if (!arc || index >= arc->count) return NULL;
    return &arc->entries[index].pub;
}

fulta_arc_err_t fulta_arc_extract(fulta_arc_t *arc, size_t index, const fulta_arc_sink_t *sink) {
    if (!arc || !sink || !sink->write) return FULTA_ARC_ERR_INVALID_ARG;
    if (index >= arc->count) return FULTA_ARC_ERR_INVALID_ARG;
    if (arc->entries[index].pub.flags & FULTA_ARC_ENTRY_DIR) return FULTA_ARC_OK;
    fa_budget_arm(&arc->budget);              /* a fresh decode: clear any sticky budget trip from a prior extract */
    arc->progress_done = 0;
    arc->progress_total = (arc->entries[index].pub.flags & FULTA_ARC_ENTRY_UNKNOWN_SIZE) ? 0 : arc->entries[index].pub.size;
    return arc->ops->extract(arc, index, sink);
}

fulta_arc_err_t fulta_arc_check_password(fulta_arc_t *arc, size_t index) {
    if (!arc || index >= arc->count) return FULTA_ARC_ERR_INVALID_ARG;
    if (!arc->ops->check_password) return FULTA_ARC_ERR_UNSUPPORTED;
    fa_budget_arm(&arc->budget);              /* check_password decodes (7z): clear any sticky budget trip */
    return arc->ops->check_password(arc, index);
}

fulta_arc_editable_t fulta_arc_editable(const fulta_arc_t *arc) {
    if (!arc) return FULTA_ARC_EDIT_FORMAT_NOT_WRITABLE;
    /* An encrypted archive is refused outright (the owner's rule): no in-place edit, no conversion. */
    for (size_t i = 0; i < arc->count; i++)
        if (arc->entries[i].pub.flags & FULTA_ARC_ENTRY_ENCRYPTED) return FULTA_ARC_EDIT_ENCRYPTED;
    if (arc->nvolumes > 1) return FULTA_ARC_EDIT_MULTIVOLUME;
    if (arc->format != FULTA_ARC_FORMAT_ZIP && arc->format != FULTA_ARC_FORMAT_7Z)
        return FULTA_ARC_EDIT_FORMAT_NOT_WRITABLE;
    if (arc->ops->editable) return arc->ops->editable(arc);   /* 7z: solid-folder check */
    return FULTA_ARC_EDIT_OK;                                 /* ZIP */
}

/* The one buffer that is handed to the caller to release with free(): it grows with the C library's own
 * malloc/realloc, NOT the fa_* seam, because the seam's allocator may hand out memory (e.g. _aligned_malloc
 * on Windows) that plain free() cannot release. The public contract on fulta_arc_extract_alloc is
 * "allocated with malloc (free it with free())"; keeping this path on libc preserves it on every platform. */
typedef struct mem_sink { uint8_t *data; size_t size, cap; uint64_t limit; } mem_sink_t;

static fulta_arc_err_t mem_sink_write(void *ctx, const void *data, size_t n) {
    mem_sink_t *m = ctx;
    if (n > SIZE_MAX - m->size) return FULTA_ARC_ERR_NOMEM;
    if (m->size + n > m->limit) return FULTA_ARC_ERR_LIMIT;
    if (m->size + n > m->cap) {
        size_t cap = m->cap ? m->cap : 64;
        while (cap < m->size + n) {
            if (cap > SIZE_MAX / 2) { cap = m->size + n; break; }
            cap *= 2;
        }
        uint8_t *p = realloc(m->data, cap);
        if (!p) return FULTA_ARC_ERR_NOMEM;
        m->data = p;
        m->cap = cap;
    }
    if (n) memcpy(m->data + m->size, data, n);
    m->size += n;
    return FULTA_ARC_OK;
}

fulta_arc_err_t fulta_arc_extract_alloc(fulta_arc_t *arc, size_t index, void **out_data, size_t *out_size) {
    if (!out_data || !out_size) return FULTA_ARC_ERR_INVALID_ARG;
    *out_data = NULL;
    *out_size = 0;
    mem_sink_t m = {.limit = SIZE_MAX};
    fulta_arc_sink_t sink = {.ctx = &m, .write = mem_sink_write};
    fulta_arc_err_t e = fulta_arc_extract(arc, index, &sink);
    if (e && e != FULTA_ARC_DAMAGED) { free(m.data); return e; }
    if (!m.data) {
        m.data = malloc(1);
        if (!m.data) return FULTA_ARC_ERR_NOMEM;
    }
    *out_data = m.data;
    *out_size = m.size;
    return e;   /* FULTA_ARC_OK, or FULTA_ARC_DAMAGED with the bytes that decoded */
}

/* ---- helpers for the format readers ----------------------------------------------------------------------- */

void fa_progress(fulta_arc_t *arc, uint64_t delta) {
    if (!arc) return;
    arc->progress_done += delta;
    if (arc->opt.progress) arc->opt.progress(arc->opt.progress_ctx, arc->progress_done, arc->progress_total);
}

char *fa_decode_name(const fulta_arc_t *arc, const uint8_t *raw, size_t n, bool unicode_flagged,
                     fulta_arc_codepage_t fallback, bool *not_unicode) {
    if (not_unicode) *not_unicode = false;
    if (unicode_flagged || fa_utf8_valid(raw, n))
        return fa_codepage_to_utf8(raw, n, FULTA_ARC_CP_UTF8, FULTA_ARC_CP_UTF8);   /* already Unicode */
    fulta_arc_codepage_t cp = arc->opt.codepage;
    if (cp == FULTA_ARC_CP_AUTO) cp = arc->opt.auto_codepage ? arc->opt.auto_codepage : fallback;
    if (not_unicode) *not_unicode = true;
    return fa_codepage_to_utf8(raw, n, cp, fallback);
}

fa_entry_t *fa_add_entry(fulta_arc_t *arc, const uint8_t *raw_name, size_t raw_size, const char *utf8_name,
                         const char *method) {
    if (arc->count == arc->cap) {
        size_t cap = arc->cap ? arc->cap * 2 : 16;
        fa_entry_t *p = fa_realloc(arc->entries, cap * sizeof *p);
        if (!p) return NULL;
        arc->entries = p;
        arc->cap = cap;
    }
    fa_entry_t *en = &arc->entries[arc->count];
    memset(en, 0, sizeof *en);
    en->pub.solid_group = (uint32_t)arc->count;   /* distinct unless the reader groups a solid unit */
    en->raw_owned = fa_malloc(raw_size);
    en->name_owned = fa_strndup(utf8_name, strlen(utf8_name));
    en->method_owned = fa_strndup(method ? method : "", method ? strlen(method) : 0);
    if (!en->raw_owned || !en->name_owned || !en->method_owned) {
        fa_free(en->raw_owned);
        fa_free(en->name_owned);
        fa_free(en->method_owned);
        return NULL;
    }
    if (raw_size) memcpy(en->raw_owned, raw_name, raw_size);
    if (!fa_sanitize_name(en->name_owned)) {
        char *p = fa_strndup("_", 1);
        if (!p) { fa_free(en->raw_owned); fa_free(en->name_owned); fa_free(en->method_owned); return NULL; }
        fa_free(en->name_owned);
        en->name_owned = p;
    }
    en->pub.name = en->name_owned;
    en->pub.name_raw = en->raw_owned;
    en->pub.name_raw_size = raw_size;
    en->pub.method = en->method_owned;
    arc->count++;
    return en;
}

fulta_arc_err_t fa_volume(fulta_arc_t *arc, uint32_t index, const char *name, const fulta_arc_source_t **out) {
    if (index < arc->nvolumes) { *out = arc->volumes[index]; return FULTA_ARC_OK; }
    if (index != arc->nvolumes) {
        /* volumes are asked for in order; fill the gap */
        const fulta_arc_source_t *dummy;
        fulta_arc_err_t e = fa_volume(arc, index - 1, NULL, &dummy);
        if (e) return e;
        (void)dummy;
        if (index != arc->nvolumes) return FULTA_ARC_ERR_VOLUME_MISSING;
    }
    if (!arc->opt.volume) return FULTA_ARC_ERR_VOLUME_MISSING;
    if (arc->nvolumes == arc->capvolumes) {
        /* the array of pointers may move; the sources themselves never do */
        fulta_arc_source_t **p = fa_realloc(arc->volumes, 2 * arc->capvolumes * sizeof *p);
        if (!p) return FULTA_ARC_ERR_NOMEM;
        arc->volumes = p;
        arc->capvolumes *= 2;
    }
    fulta_arc_source_t *s = fa_calloc(1, sizeof *s);
    if (!s) return FULTA_ARC_ERR_NOMEM;
    fulta_arc_err_t e = arc->opt.volume(arc->opt.volume_ctx, index, name, s);
    if (e || !s->read_at) { fa_free(s); return FULTA_ARC_ERR_VOLUME_MISSING; }
    arc->volumes[arc->nvolumes++] = s;
    *out = s;
    return FULTA_ARC_OK;
}

fulta_arc_err_t fa_password(fulta_arc_t *arc, uint32_t attempt, const char **out) {
    if (attempt == 0 && arc->password) { *out = arc->password; return FULTA_ARC_OK; }
    if (!arc->opt.password) return FULTA_ARC_ERR_PASSWORD_NEEDED;
    const char *pw = NULL;
    fulta_arc_err_t e = arc->opt.password(arc->opt.password_ctx, arc->password_attempts++, &pw);
    if (e || !pw) return FULTA_ARC_ERR_PASSWORD_NEEDED;
    *out = pw;
    return FULTA_ARC_OK;
}

void fa_password_ok(fulta_arc_t *arc, const char *pw) {
    if (arc->password && strcmp(arc->password, pw) == 0) return;
    char *c = fa_strndup(pw, strlen(pw));
    if (!c) return;
    if (arc->password) { memset(arc->password, 0, strlen(arc->password)); fa_free(arc->password); }
    arc->password = c;
}

bool fa_cancelled(const fulta_arc_t *arc) { return arc->opt.cancel && arc->opt.cancel(arc->opt.cancel_ctx); }

fulta_arc_err_t fa_pump(fa_stream_t *s, uint64_t size, bool has_crc, uint32_t crc, const fulta_arc_sink_t *sink,
                        fulta_arc_t *arc, bool allow_damaged) {
    uint8_t *buf = fa_malloc(65536);
    if (!buf) return FULTA_ARC_ERR_NOMEM;
    uint64_t total = 0;
    uint32_t c = 0;
    fulta_arc_err_t e = FULTA_ARC_OK;
    for (;;) {
        if (arc && fa_cancelled(arc)) { e = FULTA_ARC_ERR_CANCELLED; break; }
        size_t want = 65536;
        if (size != UINT64_MAX && size - total < want) want = (size_t)(size - total);
        if (!want) break;
        size_t got = 0;
        e = s->read(s, buf, want, &got);
        if (e) break;
        if (!got) {
            if (size != UINT64_MAX && total < size) e = FULTA_ARC_ERR_TRUNCATED;
            break;
        }
        c = fa_crc32(c, buf, got);
        total += got;
        fa_progress(arc, got);
        if (sink && sink->write) {
            fulta_arc_err_t we = sink->write(sink->ctx, buf, got);
            if (we) { fa_free(buf); return we; }   /* a sink failure is the caller's, never "damaged" */
        }
    }
    fa_free(buf);
    if (!e && has_crc && c != crc) e = FULTA_ARC_ERR_CHECKSUM;
    /* keep what decoded: a decode-side failure becomes FULTA_ARC_DAMAGED, the sink already has the bytes */
    if (e && allow_damaged && arc && arc->opt.keep_damaged && e != FULTA_ARC_ERR_CANCELLED &&
        e != FULTA_ARC_ERR_NOMEM)
        return FULTA_ARC_DAMAGED;
    return e;
}
