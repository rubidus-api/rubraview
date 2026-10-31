/* xz.c - FultaArc: xz stream reader (.xz, the outer wrapper of tar.xz). MIT.
 * Written from docs/research/formats/xz.md and the public .xz format spec; the payload is the vendored LZMA SDK /
 * FultaArc's xz container decoder (codec-xz.md, also used for ZIP method 95), which checks the stream's CRCs. A
 * .xz file is one xz stream from offset 0: one entry (named from the archive name, since .xz stores no filename),
 * streamed to the sink. No writer; no encryption. See D-020. */
#include "../core/internal.h"
static const uint8_t XZ_MAGIC[6] = {0xFD, '7', 'z', 'X', 'Z', 0x00};

static bool xz_probe(const uint8_t *head, size_t n, uint64_t *offset) {
    if (n < 6 || memcmp(head, XZ_MAGIC, 6) != 0) return false;
    *offset = 0;
    return true;
}

static char *xz_derive_name(const char *archive_name) {
    const char *base = archive_name ? archive_name : "";
    for (const char *p = base; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    size_t len = strlen(base);
    const char *tar = NULL;
    #define EQ(i, c) ((base[len-(i)] | 32) == (c))
    if (len >= 3 && base[len-3] == '.' && EQ(2,'x') && EQ(1,'z')) len -= 3;                       /* .xz */
    else if (len >= 4 && base[len-4] == '.' && EQ(3,'t') && EQ(2,'x') && EQ(1,'z')) { len -= 4; tar = ".tar"; } /* .txz */
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

static fulta_arc_err_t xz_open(fulta_arc_t *arc, uint64_t offset) {
    (void)offset;
    const fulta_arc_source_t *src = arc->volumes[0];
    uint8_t h[6];
    if (src->size < 6 || fa_source_read_exact(src, 0, h, 6)) return FULTA_ARC_ERR_NOT_ARCHIVE;
    if (memcmp(h, XZ_MAGIC, 6) != 0) return FULTA_ARC_ERR_NOT_ARCHIVE;

    char *name = xz_derive_name(arc->name);
    const char *use = name ? name : "data";
    fa_entry_t *e = fa_add_entry(arc, (const uint8_t *)use, strlen(use), use, "xz");
    fa_free(name);
    if (!e) return FULTA_ARC_ERR_NOMEM;
    e->pub.size = 0;                        /* decoded from the stream; not pre-read here */
    e->pub.packed_size = src->size;
    return FULTA_ARC_OK;
}

static fulta_arc_err_t xz_extract(fulta_arc_t *arc, size_t index, const fulta_arc_sink_t *sink) {
    if (index != 0) return FULTA_ARC_ERR_INVALID_ARG;
    const fulta_arc_source_t *src = arc->volumes[0];
    fa_piece_t piece = { src, 0, src->size };
    fa_range_t range = { &piece, 1, src->size };
    fa_stream_t *d = NULL;
    fulta_arc_err_t e = fa_dec_xz_range(&range, 0, src->size, &arc->limits, &d);
    if (e) return e;
    e = fa_pump(d, UINT64_MAX, false, 0, sink, arc, arc->opt.keep_damaged);   /* xz checks its own CRCs */
    fa_stream_destroy(d);
    return e;
}

const fa_format_ops_t fa_xz_ops = {
    .format = FULTA_ARC_FORMAT_XZ,
    .probe = xz_probe,
    .open = xz_open,
    .extract = xz_extract,
    .close = NULL,
};
