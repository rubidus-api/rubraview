/* ar.c - FultaArc: Unix ar archive reader (.a static libraries, .deb). MIT.
 * Original FultaArc code from the public ar format (BSD ar(5) + the documented SysV/GNU and BSD name extensions);
 * no GPL/LGPL source read or copied. An ar archive is the magic "!<arch>\n" then a sequence of members, each a
 * 60-byte header followed by its data padded to an even length. This reader lists each real member (resolving the
 * GNU "//" long-name table, GNU "/<n>" references and BSD "#1/<n>" inline names, and skipping symbol tables) and
 * streams the stored bytes. ar is uncompressed, no checksum, no encryption. No writer. See D-020. */
#include "../core/internal.h"

#define AR_MAGIC "!<arch>\n"
#define AR_HDR 60
#define AR_NAMETAB_MAX (16u << 20)   /* cap the GNU "//" long-name table before allocating (untrusted input) */

typedef struct ar_item { uint64_t off, size; } ar_item_t;

typedef struct ar_state {
    ar_item_t *items;
    size_t n, cap;
    char *nametab;        /* the GNU "//" long-name string table (may be NULL) */
    size_t nametab_len;
} ar_state_t;

/* Parse a left-justified decimal field of `len` bytes (space-padded). Returns false on a non-digit/overflow. */
static bool ar_dec(const uint8_t *f, size_t len, uint64_t *out) {
    uint64_t v = 0;
    size_t i = 0;
    while (i < len && f[i] == ' ') i++;
    bool any = false;
    for (; i < len && f[i] >= '0' && f[i] <= '9'; i++) {
        if (v > (UINT64_MAX - 9) / 10) return false;
        v = v * 10 + (uint64_t)(f[i] - '0');
        any = true;
    }
    while (i < len && (f[i] == ' ' || f[i] == '\0')) i++;
    if (i < len) return false;
    *out = v;
    return any;
}

static ar_item_t *ar_add(ar_state_t *st) {
    if (st->n == st->cap) {
        size_t nc = st->cap ? st->cap * 2 : 16;
        ar_item_t *p = fa_realloc(st->items, nc * sizeof *p);
        if (!p) return NULL;
        st->items = p;
        st->cap = nc;
    }
    return &st->items[st->n++];
}

static void ar_close(fulta_arc_t *arc);

/* Add a file entry with the resolved name and data range. */
static fulta_arc_err_t ar_emit(fulta_arc_t *arc, ar_state_t *st, const char *name, size_t nlen,
                               uint64_t off, uint64_t size) {
    char *safe = fa_malloc(nlen + 1);
    if (!safe) return FULTA_ARC_ERR_NOMEM;
    memcpy(safe, name, nlen);
    safe[nlen] = 0;
    if (!fa_sanitize_name(safe)) { fa_free(safe); safe = NULL; }
    const char *use = safe && safe[0] ? safe : "data";
    fa_entry_t *e = fa_add_entry(arc, (const uint8_t *)name, nlen, use, "store");
    ar_item_t *it = ar_add(st);
    fa_free(safe);
    if (!e || !it) return FULTA_ARC_ERR_NOMEM;
    e->pub.size = size;
    e->pub.packed_size = size;
    it->off = off;
    it->size = size;
    return FULTA_ARC_OK;
}

static bool ar_probe(const uint8_t *head, size_t n, uint64_t *offset) {
    if (n < 8 || memcmp(head, AR_MAGIC, 8) != 0) return false;
    *offset = 0;
    return true;
}

static fulta_arc_err_t ar_open(fulta_arc_t *arc, uint64_t offset) {
    (void)offset;
    const fulta_arc_source_t *src = arc->volumes[0];
    uint8_t magic[8];
    if (src->size < 8 || fa_source_read_exact(src, 0, magic, 8) || memcmp(magic, AR_MAGIC, 8) != 0)
        return FULTA_ARC_ERR_NOT_ARCHIVE;

    ar_state_t *st = fa_calloc(1, sizeof *st);
    if (!st) return FULTA_ARC_ERR_NOMEM;
    arc->state = st;

    fulta_arc_err_t e = FULTA_ARC_OK;
    /* A malformed or untrustworthy header ends the scan (keep the members listed so far), uniformly - a bad "`\n"
     * terminator, a non-numeric or over-long size, a short read, or an out-of-range name reference all just `break`.
     * Only out-of-memory is a hard failure. This keeps a slightly damaged archive (e.g. a truncated .deb) listing
     * what is intact instead of flapping between "N entries" and "corrupt" on a one-byte change. */
    uint64_t pos = 8;
    while (pos + AR_HDR <= src->size) {
        uint8_t h[AR_HDR];
        if (fa_source_read_exact(src, pos, h, AR_HDR)) break;
        if (h[58] != 0x60 || h[59] != 0x0A) break;        /* no "`\n" terminator */
        uint64_t size;
        if (!ar_dec(h + 48, 10, &size)) break;            /* non-numeric size */
        uint64_t data = pos + AR_HDR;
        if (size > src->size - data) break;               /* member runs past the file */
        uint64_t advance = data + size + (size & 1);       /* members are 2-byte aligned */

        const uint8_t *nm = h;                            /* the 16-byte name field */
        if (nm[0] == '/' && nm[1] == '/') {               /* GNU long-name string table */
            if (size > AR_NAMETAB_MAX) break;
            if (!st->nametab && size) {
                st->nametab = fa_malloc((size_t)size);
                if (!st->nametab) { e = FULTA_ARC_ERR_NOMEM; break; }
                if (fa_source_read_exact(src, data, st->nametab, (size_t)size)) break;
                st->nametab_len = (size_t)size;
            }
            pos = advance;
            continue;
        }
        if (nm[0] == '/' && nm[1] == ' ') { pos = advance; continue; }                  /* SysV symbol table "/" */
        if (memcmp(nm, "/SYM64/", 7) == 0) { pos = advance; continue; }

        if (nm[0] == '/' && nm[1] >= '0' && nm[1] <= '9') {   /* GNU "/<offset>" into the long-name table */
            uint64_t ofs = 0;
            for (size_t i = 1; i < 16 && nm[i] >= '0' && nm[i] <= '9'; i++) ofs = ofs * 10 + (uint64_t)(nm[i] - '0');
            if (ofs >= st->nametab_len) break;            /* missing or short "//" table */
            const char *p = st->nametab + ofs;
            size_t maxn = st->nametab_len - (size_t)ofs, nlen = 0;
            while (nlen < maxn && p[nlen] != '/' && p[nlen] != '\n') nlen++;   /* GNU ends a name with "/\n" */
            if ((e = ar_emit(arc, st, p, nlen, data, size))) break;
            pos = advance;
            continue;
        }

        if (memcmp(nm, "#1/", 3) == 0 && nm[3] >= '0' && nm[3] <= '9') {   /* BSD "#1/<len>": name in the data */
            uint64_t namelen = 0;
            for (size_t i = 3; i < 16 && nm[i] >= '0' && nm[i] <= '9'; i++) namelen = namelen * 10 + (uint64_t)(nm[i] - '0');
            if (namelen == 0 || namelen > size) break;
            char nbuf[256];
            size_t rn = namelen > sizeof nbuf ? sizeof nbuf : (size_t)namelen;   /* names in practice are < 100 */
            if (fa_source_read_exact(src, data, nbuf, rn)) break;
            size_t nlen = 0;
            while (nlen < rn && nbuf[nlen]) nlen++;        /* BSD may NUL-pad the inline name */
            bool symtab = nlen >= 9 && memcmp(nbuf, "__.SYMDEF", 9) == 0;   /* "__.SYMDEF" / "__.SYMDEF SORTED" */
            if (!symtab && (e = ar_emit(arc, st, nbuf, nlen, data + namelen, size - namelen))) break;
            pos = advance;
            continue;
        }

        /* plain short name: GNU terminates with '/', else trim trailing spaces */
        size_t nlen = 0;
        while (nlen < 16 && nm[nlen] != '/' && nm[nlen] != ' ') nlen++;
        if (nlen == 9 && memcmp(nm, "__.SYMDEF", 9) == 0) { pos = advance; continue; }   /* BSD symbol table */
        if ((e = ar_emit(arc, st, (const char *)nm, nlen, data, size))) break;
        pos = advance;
    }

    if (e) { ar_close(arc); return e; }   /* only NOMEM reaches here */
    return FULTA_ARC_OK;                   /* an empty ar (magic only) is still a valid, if empty, archive */
}

static fulta_arc_err_t ar_extract(fulta_arc_t *arc, size_t index, const fulta_arc_sink_t *sink) {
    ar_state_t *st = arc->state;
    if (index >= st->n) return FULTA_ARC_ERR_INVALID_ARG;
    ar_item_t *it = &st->items[index];
    if (it->size == 0) return FULTA_ARC_OK;
    const fulta_arc_source_t *src = arc->volumes[0];
    fa_piece_t piece = { src, it->off, it->size };
    fa_range_t range = { &piece, 1, it->size };
    fa_stream_t *s = NULL;
    fulta_arc_err_t e = fa_stream_range(&range, 0, it->size, &s);
    if (e) return e;
    e = fa_pump(s, it->size, false, 0, sink, arc, arc->opt.keep_damaged);
    fa_stream_destroy(s);
    return e;
}

static void ar_close(fulta_arc_t *arc) {
    ar_state_t *st = arc->state;
    if (!st) return;
    fa_free(st->items);
    fa_free(st->nametab);
    fa_free(st);
    arc->state = NULL;
}

const fa_format_ops_t fa_ar_ops = {
    .format = FULTA_ARC_FORMAT_AR,
    .probe = ar_probe,      /* "!<arch>\n" at offset 0 */
    .open = ar_open,
    .extract = ar_extract,
    .close = ar_close,
};
