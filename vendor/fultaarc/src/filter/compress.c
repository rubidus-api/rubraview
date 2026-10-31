/* compress.c - FultaArc: Unix compress (.Z) stream reader - the outer wrapper of tar.Z. MIT.
 * A .Z file is one LZW stream (magic 0x1F 0x9D) from offset 0; this reader lists one entry (named from the archive
 * name, since compress stores none) and streams the decompressed bytes. The LZW decode is src/codec/compress.c
 * (FultaArc's own, from the public compress format). No writer; no encryption. See D-020. */
#include "../core/internal.h"

static bool compress_probe(const uint8_t *head, size_t n, uint64_t *offset) {
    if (n < 3 || head[0] != 0x1F || head[1] != 0x9D) return false;   /* magic */
    if (head[2] & 0x60) return false;                                /* reserved flag bits */
    int maxbits = head[2] & 0x1F;
    if (maxbits < 9 || maxbits > 16) return false;
    *offset = 0;
    return true;
}

/* Derive a member name from the archive name: basename with a compress suffix stripped (.Z, or .taz -> .tar). */
static char *compress_derive_name(const char *archive_name) {
    const char *base = archive_name ? archive_name : "";
    for (const char *p = base; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    size_t len = strlen(base);
    const char *tar = NULL;
    #define EQ(i, c) ((base[len-(i)] | 32) == (c))
    if (len >= 2 && base[len-2] == '.' && base[len-1] == 'Z') len -= 2;                       /* .Z */
    else if (len >= 4 && base[len-4] == '.' && EQ(3,'t') && EQ(2,'a') && EQ(1,'z')) { len -= 4; tar = ".tar"; }  /* .taz */
    #undef EQ
    if (len == 0) { base = "data"; len = 4; tar = NULL; }
    size_t extra = tar ? 4 : 0;
    char *outn = fa_malloc(len + extra + 1);
    if (!outn) return NULL;
    memcpy(outn, base, len);
    if (tar) memcpy(outn + len, tar, 4);
    outn[len + extra] = 0;
    return outn;
}

static fulta_arc_err_t compress_open(fulta_arc_t *arc, uint64_t offset) {
    (void)offset;
    const fulta_arc_source_t *src = arc->volumes[0];
    uint8_t h[3];
    if (src->size < 3 || fa_source_read_exact(src, 0, h, 3)) return FULTA_ARC_ERR_NOT_ARCHIVE;
    if (h[0] != 0x1F || h[1] != 0x9D || (h[2] & 0x60) || (h[2] & 0x1F) < 9 || (h[2] & 0x1F) > 16)
        return FULTA_ARC_ERR_NOT_ARCHIVE;

    char *name = compress_derive_name(arc->name);
    const char *use = name ? name : "data";
    fa_entry_t *e = fa_add_entry(arc, (const uint8_t *)use, strlen(use), use, "compress");
    fa_free(name);
    if (!e) return FULTA_ARC_ERR_NOMEM;
    e->pub.size = 0;                        /* compress stores no uncompressed size */
    e->pub.packed_size = src->size;
    return FULTA_ARC_OK;
}

static fulta_arc_err_t compress_extract(fulta_arc_t *arc, size_t index, const fulta_arc_sink_t *sink) {
    if (index != 0) return FULTA_ARC_ERR_INVALID_ARG;
    const fulta_arc_source_t *src = arc->volumes[0];
    fa_piece_t piece = { src, 0, src->size };
    fa_range_t range = { &piece, 1, src->size };
    fa_stream_t *s = NULL, *d = NULL;
    fulta_arc_err_t e = fa_stream_range(&range, 0, src->size, &s);
    if (e) return e;
    if ((e = fa_dec_compress(s, &d))) { fa_stream_destroy(s); return e; }
    e = fa_pump(d, UINT64_MAX, false, 0, sink, arc, arc->opt.keep_damaged);
    fa_stream_destroy(d);
    return e;
}

const fa_format_ops_t fa_compress_ops = {
    .format = FULTA_ARC_FORMAT_COMPRESS,
    .probe = compress_probe,
    .open = compress_open,
    .extract = compress_extract,
    .close = NULL,
};
