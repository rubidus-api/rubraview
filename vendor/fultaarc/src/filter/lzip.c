/* lzip.c - FultaArc: lzip stream reader (.lz, the outer wrapper of tar.lz). MIT.
 * Original FultaArc code written from the public lzip format specification. A lzip member is: "LZIP", a version
 * byte (1), a coded dictionary-size byte, an LZMA1 stream (always lc=3 lp=0 pb=2; dictionary from the header), and
 * a 20-byte trailer: CRC-32 of the uncompressed data (LE), the uncompressed size (LE64), and the member size
 * (LE64). The LZMA payload is decoded by the vendored LZMA SDK via fa_dec_lzma (the same decoder as ZIP method 14),
 * so there is no new codec. This reader lists one entry (named from the archive name, since lzip stores none) and,
 * for a single-member file, verifies the trailer CRC-32. No writer; no encryption. See D-020. */
#include "../core/internal.h"

#define LZIP_TRAILER 20

typedef struct lzip_state {
    uint64_t off, len;      /* the LZMA1 stream range within volumes[0] */
    uint64_t out_size;      /* uncompressed size */
    uint32_t crc;
    bool has_crc;
    bool unsupported;       /* a multi-member file: the first member's trailer cannot be located without decoding */
    uint32_t dict;
} lzip_state_t;

/* Decode lzip's coded dictionary-size byte: base = 1<<(b&0x1F) (exponent 12..29), minus (base/16)*((b>>5)&7). */
static bool lzip_dict(uint8_t b, uint32_t *out) {
    int n = b & 0x1F;
    if (n < 12 || n > 29) return false;
    uint32_t base = 1u << n;
    uint32_t dict = base - (base >> 4) * (uint32_t)((b >> 5) & 7);
    if (dict < 4096) dict = 4096;
    *out = dict;
    return true;
}

static bool lzip_probe(const uint8_t *head, size_t n, uint64_t *offset) {
    if (n < 6 || head[0] != 'L' || head[1] != 'Z' || head[2] != 'I' || head[3] != 'P') return false;
    if (head[4] != 1) return false;                 /* format version 1 */
    uint32_t d;
    if (!lzip_dict(head[5], &d)) return false;
    *offset = 0;
    return true;
}

static char *lzip_derive_name(const char *archive_name) {
    const char *base = archive_name ? archive_name : "";
    for (const char *p = base; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    size_t len = strlen(base);
    const char *tar = NULL;
    #define EQ(i, c) ((base[len-(i)] | 32) == (c))
    if (len >= 3 && base[len-3] == '.' && EQ(2,'l') && EQ(1,'z')) len -= 3;                        /* .lz */
    else if (len >= 4 && base[len-4] == '.' && EQ(3,'t') && EQ(2,'l') && EQ(1,'z')) { len -= 4; tar = ".tar"; }  /* .tlz */
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

static fulta_arc_err_t lzip_open(fulta_arc_t *arc, uint64_t offset) {
    (void)offset;
    const fulta_arc_source_t *src = arc->volumes[0];
    uint8_t h[6];
    if (src->size < 6 + LZIP_TRAILER || fa_source_read_exact(src, 0, h, 6)) return FULTA_ARC_ERR_NOT_ARCHIVE;
    if (h[0] != 'L' || h[1] != 'Z' || h[2] != 'I' || h[3] != 'P' || h[4] != 1) return FULTA_ARC_ERR_NOT_ARCHIVE;
    uint32_t dict;
    if (!lzip_dict(h[5], &dict)) return FULTA_ARC_ERR_NOT_ARCHIVE;

    lzip_state_t *st = fa_calloc(1, sizeof *st);
    if (!st) return FULTA_ARC_ERR_NOMEM;
    st->dict = dict;
    st->off = 6;

    /* The trailer is the last 20 bytes. When its member-size equals the whole file, this is a single member and we
     * can decode exactly `data_size` bytes and verify the CRC-32. Otherwise (multi-member) decode the first member
     * by its LZMA end-of-stream marker, size and CRC unverified (first-member-only, like the other wrappers). */
    uint8_t t[LZIP_TRAILER];
    if (fa_source_read_exact(src, src->size - LZIP_TRAILER, t, LZIP_TRAILER)) { fa_free(st); return FULTA_ARC_ERR_TRUNCATED; }
    uint32_t crc = fa_le32(t);
    uint64_t data_size = fa_le32(t + 4) | ((uint64_t)fa_le32(t + 8) << 32);
    uint64_t member_size = fa_le32(t + 12) | ((uint64_t)fa_le32(t + 16) << 32);
    if (member_size < 6 + LZIP_TRAILER || member_size > src->size) { fa_free(st); return FULTA_ARC_ERR_NOT_ARCHIVE; }

    uint64_t esize = 0;
    if (member_size == src->size) {                 /* single member: the trailer is trustworthy */
        st->len = src->size - 6 - LZIP_TRAILER;
        st->out_size = data_size;
        st->crc = crc;
        st->has_crc = true;
        esize = data_size;
    } else {
        /* multi-member: the first member's trailer cannot be located without decoding, and decoding past it (into
         * the trailer + later members) is unsafe on crafted input (a missing end marker would yield garbage). List
         * it but refuse to extract, rather than offer an unverifiable, untested path. */
        st->unsupported = true;
    }
    arc->state = st;

    char *name = lzip_derive_name(arc->name);
    const char *use = name ? name : "data";
    fa_entry_t *e = fa_add_entry(arc, (const uint8_t *)use, strlen(use), use, "lzma");
    fa_free(name);
    if (!e) return FULTA_ARC_ERR_NOMEM;
    e->pub.size = esize;
    e->pub.packed_size = src->size;
    if (st->has_crc) { e->pub.crc32 = crc; e->pub.flags |= FULTA_ARC_ENTRY_HAS_CRC32; }
    if (st->unsupported) e->pub.flags |= FULTA_ARC_ENTRY_UNSUPPORTED;   /* multi-member: listed, not extractable */
    return FULTA_ARC_OK;
}

static fulta_arc_err_t lzip_extract(fulta_arc_t *arc, size_t index, const fulta_arc_sink_t *sink) {
    if (index != 0) return FULTA_ARC_ERR_INVALID_ARG;
    lzip_state_t *st = arc->state;
    if (st->unsupported) return FULTA_ARC_ERR_UNSUPPORTED;   /* multi-member */
    const fulta_arc_source_t *src = arc->volumes[0];
    fa_piece_t piece = { src, st->off, st->len };
    fa_range_t range = { &piece, 1, st->len };
    fa_stream_t *s = NULL, *d = NULL;
    fulta_arc_err_t e = fa_stream_range(&range, 0, st->len, &s);
    if (e) return e;
    uint8_t props[5] = { 93, 0, 0, 0, 0 };          /* lc=3,lp=0,pb=2 -> (pb*5+lp)*9+lc = 93; dict LE follows */
    fa_put_le32(props + 1, st->dict);
    if ((e = fa_dec_lzma(s, props, st->out_size, &arc->limits, &d))) { fa_stream_destroy(s); return e; }
    e = fa_pump(d, st->out_size, st->has_crc, st->crc, sink, arc, arc->opt.keep_damaged);
    fa_stream_destroy(d);
    return e;
}

static void lzip_close(fulta_arc_t *arc) {
    fa_free(arc->state);
    arc->state = NULL;
}

const fa_format_ops_t fa_lzip_ops = {
    .format = FULTA_ARC_FORMAT_LZIP,
    .probe = lzip_probe,
    .open = lzip_open,
    .extract = lzip_extract,
    .close = lzip_close,
};
