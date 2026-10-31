/* zstd.c - FultaArc: Zstandard stream reader (.zst, the outer wrapper of tar.zst). MIT.
 * Written from docs/research/formats/zstd.md and RFC 8878 (the Zstandard frame format); the payload is the vendored
 * zstd library (BSD-3), which checks the optional frame content checksum. A .zst file is one zstd frame from
 * offset 0: one entry (named from the archive name, since the frame stores no filename), streamed to the sink.
 * No writer; no encryption. See D-020. */
#include "../core/internal.h"
static const uint8_t ZSTD_MAGIC[4] = {0x28, 0xB5, 0x2F, 0xFD};

static bool zstd_probe(const uint8_t *head, size_t n, uint64_t *offset) {
    if (n < 4 || memcmp(head, ZSTD_MAGIC, 4) != 0) return false;
    *offset = 0;
    return true;
}

static char *zstd_derive_name(const char *archive_name) {
    const char *base = archive_name ? archive_name : "";
    for (const char *p = base; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    size_t len = strlen(base);
    const char *tar = NULL;
    #define EQ(i, c) ((base[len-(i)] | 32) == (c))
    if (len >= 4 && base[len-4] == '.' && EQ(3,'z') && EQ(2,'s') && EQ(1,'t')) len -= 4;                        /* .zst */
    else if (len >= 5 && base[len-5] == '.' && EQ(4,'t') && EQ(3,'z') && EQ(2,'s') && EQ(1,'t')) { len -= 5; tar = ".tar"; } /* .tzst */
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

static fulta_arc_err_t zstd_open(fulta_arc_t *arc, uint64_t offset) {
    (void)offset;
    const fulta_arc_source_t *src = arc->volumes[0];
    uint8_t h[4];
    if (src->size < 4 || fa_source_read_exact(src, 0, h, 4)) return FULTA_ARC_ERR_NOT_ARCHIVE;
    if (memcmp(h, ZSTD_MAGIC, 4) != 0) return FULTA_ARC_ERR_NOT_ARCHIVE;

    char *name = zstd_derive_name(arc->name);
    const char *use = name ? name : "data";
    fa_entry_t *e = fa_add_entry(arc, (const uint8_t *)use, strlen(use), use, "zstd");
    fa_free(name);
    if (!e) return FULTA_ARC_ERR_NOMEM;
    e->pub.size = 0;                        /* the frame may carry a content size; not pre-read here */
    e->pub.packed_size = src->size;
    return FULTA_ARC_OK;
}

static fulta_arc_err_t zstd_extract(fulta_arc_t *arc, size_t index, const fulta_arc_sink_t *sink) {
    if (index != 0) return FULTA_ARC_ERR_INVALID_ARG;
    const fulta_arc_source_t *src = arc->volumes[0];
    fa_piece_t piece = { src, 0, src->size };
    fa_range_t range = { &piece, 1, src->size };
    fa_stream_t *s = NULL, *d = NULL;
    fulta_arc_err_t e = fa_stream_range(&range, 0, src->size, &s);
    if (e) return e;
    if ((e = fa_dec_zstd(s, UINT64_MAX, &arc->limits, &d))) { fa_stream_destroy(s); return e; }
    e = fa_pump(d, UINT64_MAX, false, 0, sink, arc, arc->opt.keep_damaged);   /* zstd checks its own checksum */
    fa_stream_destroy(d);
    return e;
}

const fa_format_ops_t fa_zstd_ops = {
    .format = FULTA_ARC_FORMAT_ZSTD,
    .probe = zstd_probe,
    .open = zstd_open,
    .extract = zstd_extract,
    .close = NULL,
};
