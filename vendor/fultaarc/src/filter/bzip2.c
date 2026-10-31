/* bzip2.c - FultaArc: bzip2 stream reader (.bz2, the outer wrapper of tar.bz2). MIT.
 * Written from docs/research/formats/bzip2.md (the bzip2 file format) and codec-bzip2.md; the payload is decoded by
 * the vendored bzip2 (bzip2 licence), which also verifies bzip2's own per-block and combined CRCs. A .bz2 file is
 * one bzip2 stream from offset 0, so this reader lists one entry (named from the archive name, since bzip2 stores
 * no filename) and streams the decompressed bytes to the sink. bzip2 stores no uncompressed size and no plaintext
 * filename or mtime. No writer; no encryption. See D-020. */
#include "../core/internal.h"
static bool bzip2_probe(const uint8_t *head, size_t n, uint64_t *offset) {
    if (n < 4 || head[0] != 'B' || head[1] != 'Z' || head[2] != 'h') return false;  /* "BZh" */
    if (head[3] < '1' || head[3] > '9') return false;                                /* block size 1..9 (100k..900k) */
    *offset = 0;
    return true;
}

/* Derive a member name from the archive name: basename, with a bzip2 suffix stripped. Caller frees. */
static char *bzip2_derive_name(const char *archive_name) {
    const char *base = archive_name ? archive_name : "";
    for (const char *p = base; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    size_t len = strlen(base);
    const char *tar = NULL;
    /* case-insensitive suffix tests */
    #define EQ(i, c) ((base[len-(i)] | 32) == (c))
    if (len >= 4 && base[len-4] == '.' && EQ(3,'b') && EQ(2,'z') && base[len-1] == '2') len -= 4;          /* .bz2 */
    else if (len >= 5 && base[len-5] == '.' && EQ(4,'t') && EQ(3,'b') && EQ(2,'z') && base[len-1]=='2') { len -= 5; tar = ".tar"; }  /* .tbz2 */
    else if (len >= 4 && base[len-4] == '.' && EQ(3,'t') && EQ(2,'b') && EQ(1,'z')) { len -= 4; tar = ".tar"; } /* .tbz */
    else if (len >= 4 && base[len-4] == '.' && EQ(3,'t') && EQ(2,'z') && base[len-1]=='2') { len -= 4; tar = ".tar"; } /* .tz2 */
    #undef EQ
    if (len == 0) { base = "data"; len = 4; tar = NULL; }
    size_t extra = tar ? 4 : 0;
    char *out = fa_malloc(len + extra + 1);
    if (!out) return NULL;
    memcpy(out, base, len);
    if (tar) memcpy(out + len, tar, 4);
    out[len + extra] = 0;
    return out;
}

static fulta_arc_err_t bzip2_open(fulta_arc_t *arc, uint64_t offset) {
    (void)offset;
    const fulta_arc_source_t *src = arc->volumes[0];
    uint8_t h[4];
    if (src->size < 4 || fa_source_read_exact(src, 0, h, 4)) return FULTA_ARC_ERR_NOT_ARCHIVE;
    if (h[0] != 'B' || h[1] != 'Z' || h[2] != 'h' || h[3] < '1' || h[3] > '9') return FULTA_ARC_ERR_NOT_ARCHIVE;

    char *name = bzip2_derive_name(arc->name);
    const char *use = name ? name : "data";
    fa_entry_t *e = fa_add_entry(arc, (const uint8_t *)use, strlen(use), use, "BZip2");
    fa_free(name);
    if (!e) return FULTA_ARC_ERR_NOMEM;
    e->pub.size = 0;                        /* bzip2 stores no uncompressed size (0 = not known) */
    e->pub.packed_size = src->size;
    return FULTA_ARC_OK;
}

static fulta_arc_err_t bzip2_extract(fulta_arc_t *arc, size_t index, const fulta_arc_sink_t *sink) {
    if (index != 0) return FULTA_ARC_ERR_INVALID_ARG;
    const fulta_arc_source_t *src = arc->volumes[0];
    fa_piece_t piece = { src, 0, src->size };
    fa_range_t range = { &piece, 1, src->size };
    fa_stream_t *s = NULL, *d = NULL;
    fulta_arc_err_t e = fa_stream_range(&range, 0, src->size, &s);
    if (e) return e;
    if ((e = fa_dec_bzip2(s, UINT64_MAX, &arc->limits, &d))) { fa_stream_destroy(s); return e; }
    e = fa_pump(d, UINT64_MAX, false, 0, sink, arc, arc->opt.keep_damaged);   /* bzip2 checks its own CRCs */
    fa_stream_destroy(d);
    return e;
}

const fa_format_ops_t fa_bzip2_ops = {
    .format = FULTA_ARC_FORMAT_BZIP2,
    .probe = bzip2_probe,
    .open = bzip2_open,
    .extract = bzip2_extract,
    .close = NULL,
};
