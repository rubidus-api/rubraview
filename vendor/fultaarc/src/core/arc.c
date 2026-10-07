/* arc.c - FultaArc: opening an archive (format detection), entries, volumes, passwords, extraction. MIT. */
#include "internal.h"

#include <stdlib.h>

/* The scope FultaArc ships (owner, 2026-10-08: "완성된 핵심만"): the fully-analysed formats. ALZ, EGG and AZO are
 * held out of the build and this registry until their clean rooms deliver and that work is ported; their sources
 * stay in src/alz, src/egg, src/azo and are excluded by the Makefile. RAR 1.5 compression is likewise pending. */
static const fa_format_ops_t *const FORMATS[] = {
    &fa_rar_ops, &fa_7z_ops, &fa_zip_ops,
};

const char *fulta_arc_format_name(fulta_arc_format_t f) {
    switch (f) {
    case FULTA_ARC_FORMAT_ZIP: return "ZIP";
    case FULTA_ARC_FORMAT_7Z: return "7z";
    case FULTA_ARC_FORMAT_RAR: return "RAR";
    case FULTA_ARC_FORMAT_ALZ: return "ALZ";
    case FULTA_ARC_FORMAT_EGG: return "EGG";
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

fulta_arc_err_t fulta_arc_open(const fulta_arc_source_t *source, const fulta_arc_options_t *options, fulta_arc_t **out) {
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

    /* detection: the head of the file; SFX stubs are searched by the readers that support them */
    uint8_t head[64] = {0};
    size_t n = 0;
    fulta_arc_err_t e = source->read_at(source->ctx, 0, head, sizeof head, &n);
    if (e) { free_all(arc); return e; }
    for (size_t i = 0; i < sizeof FORMATS / sizeof *FORMATS; i++) {
        uint64_t off = 0;
        if (FORMATS[i]->probe(head, n, &off)) {
            arc->ops = FORMATS[i];
            arc->format = FORMATS[i]->format;
            e = arc->ops->open(arc, off);
            if (e) { free_all(arc); return e; }
            *out = arc;
            return FULTA_ARC_OK;
        }
    }
    /* no signature at the start: let each reader search further (SFX stubs, appended archives) */
    for (size_t i = 0; i < sizeof FORMATS / sizeof *FORMATS; i++) {
        arc->ops = FORMATS[i];
        arc->format = FORMATS[i]->format;
        e = arc->ops->open(arc, UINT64_MAX);    /* UINT64_MAX: search for the signature */
        if (!e) { *out = arc; return FULTA_ARC_OK; }
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
    return arc->ops->extract(arc, index, sink);
}

typedef struct mem_sink { fa_buf_t b; uint64_t limit; } mem_sink_t;

static fulta_arc_err_t mem_sink_write(void *ctx, const void *data, size_t n) {
    mem_sink_t *m = ctx;
    if (m->b.size + n > m->limit) return FULTA_ARC_ERR_LIMIT;
    return fa_buf_append(&m->b, data, n);
}

fulta_arc_err_t fulta_arc_extract_alloc(fulta_arc_t *arc, size_t index, void **out_data, size_t *out_size) {
    if (!out_data || !out_size) return FULTA_ARC_ERR_INVALID_ARG;
    *out_data = NULL;
    *out_size = 0;
    mem_sink_t m = {.limit = SIZE_MAX};
    fulta_arc_sink_t sink = {.ctx = &m, .write = mem_sink_write};
    fulta_arc_err_t e = fulta_arc_extract(arc, index, &sink);
    if (e) { fa_buf_free(&m.b); return e; }
    if (!m.b.data) {
        m.b.data = malloc(1);
        if (!m.b.data) return FULTA_ARC_ERR_NOMEM;
    }
    *out_data = m.b.data;
    *out_size = m.b.size;
    return FULTA_ARC_OK;
}

/* ---- helpers for the format readers ----------------------------------------------------------------------- */

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
                        const fulta_arc_t *arc) {
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
        if (sink && sink->write) {
            e = sink->write(sink->ctx, buf, got);
            if (e) break;
        }
    }
    fa_free(buf);
    if (!e && has_crc && c != crc) e = FULTA_ARC_ERR_CHECKSUM;
    return e;
}
