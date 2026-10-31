/* gzip.c - FultaArc: gzip stream reader (.gz, the outer wrapper of tar.gz). MIT.
 * Written from docs/research/formats/gzip.md, which follows RFC 1952 (the public gzip format spec); the DEFLATE
 * payload is RFC 1951 through the vendored miniz decoder (codec-deflate.md), and the CRC-32 is the project's.
 * A gzip member is one stored stream: this reader lists one entry (named by FNAME, else the archive name with a
 * .gz/.tgz/.taz suffix stripped) and streams the inflated bytes to the sink, checking the trailer CRC-32. No gzip
 * writer; no encryption (gzip has none). See D-020. */
#include "../core/internal.h"
#define GZ_FHCRC   0x02u
#define GZ_FEXTRA  0x04u
#define GZ_FNAME   0x08u
#define GZ_FCOMMENT 0x10u
#define GZ_RESERVED 0xE0u

typedef struct gzip_state {
    uint64_t payload_off;   /* first DEFLATE byte */
    uint64_t payload_len;   /* bytes of DEFLATE (trailer excluded when present) */
    uint32_t crc;           /* trailer CRC-32 (valid when has_crc) */
    bool has_crc;
} gzip_state_t;

static bool gzip_probe(const uint8_t *head, size_t n, uint64_t *offset) {
    if (n < 3 || head[0] != 0x1F || head[1] != 0x8B || head[2] != 8) return false;  /* ID1 ID2 CM=deflate */
    if (head[3] & GZ_RESERVED) return false;                                         /* reserved FLG bits: not gzip */
    *offset = 0;
    return true;
}

/* Derive a member name from the archive name: basename, with a gzip suffix stripped. Caller frees. */
static char *gzip_derive_name(const char *archive_name) {
    const char *base = archive_name ? archive_name : "";
    for (const char *p = base; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    size_t len = strlen(base);
    /* strip a trailing .gz; map .tgz/.taz to .tar */
    const char *tar = NULL;
    if (len >= 3 && (base[len-3]=='.') && (base[len-2]|32)=='g' && (base[len-1]|32)=='z') len -= 3;
    else if (len >= 4 && base[len-4]=='.' && (base[len-3]|32)=='t' && (base[len-2]|32)=='g' && (base[len-1]|32)=='z') { len -= 4; tar = ".tar"; }
    else if (len >= 4 && base[len-4]=='.' && (base[len-3]|32)=='t' && (base[len-2]|32)=='a' && (base[len-1]|32)=='z') { len -= 4; tar = ".tar"; }
    if (len == 0) { base = "data"; len = 4; tar = NULL; }
    size_t extra = tar ? 4 : 0;
    char *out = fa_malloc(len + extra + 1);
    if (!out) return NULL;
    memcpy(out, base, len);
    if (tar) memcpy(out + len, tar, 4);
    out[len + extra] = 0;
    return out;
}

static fulta_arc_err_t gzip_open(fulta_arc_t *arc, uint64_t offset) {
    (void)offset;
    const fulta_arc_source_t *src = arc->volumes[0];
    uint8_t h[10];
    if (src->size < 10 || fa_source_read_exact(src, 0, h, 10)) return FULTA_ARC_ERR_NOT_ARCHIVE;
    if (h[0] != 0x1F || h[1] != 0x8B) return FULTA_ARC_ERR_NOT_ARCHIVE;
    if (h[2] != 8) return FULTA_ARC_ERR_UNSUPPORTED;                /* CM must be DEFLATE */
    uint8_t flg = h[3];
    if (flg & GZ_RESERVED) return FULTA_ARC_ERR_NOT_ARCHIVE;
    uint32_t mtime = fa_le32(h + 4);

    uint64_t off = 10;
    char *fname = NULL;                                            /* FNAME bytes (NUL-terminated), if present */

    if (flg & GZ_FEXTRA) {
        uint8_t x[2];
        if (off + 2 > src->size || fa_source_read_exact(src, off, x, 2)) return FULTA_ARC_ERR_TRUNCATED;
        off += 2 + (uint64_t)fa_le16(x);
        if (off > src->size) return FULTA_ARC_ERR_TRUNCATED;
    }
    if (flg & GZ_FNAME) {
        fa_buf_t nb = {0};
        for (;;) {
            uint8_t c;
            if (off >= src->size || fa_source_read_exact(src, off, &c, 1)) { fa_buf_free(&nb); return FULTA_ARC_ERR_TRUNCATED; }
            off++;
            if (c == 0) break;
            if (nb.size >= 4096) { off++; continue; }              /* cap the stored name; keep scanning to the NUL */
            if (fa_buf_byte(&nb, c)) { fa_buf_free(&nb); return FULTA_ARC_ERR_NOMEM; }
        }
        if (nb.size) {
            fname = fa_malloc(nb.size + 1);
            if (!fname) { fa_buf_free(&nb); return FULTA_ARC_ERR_NOMEM; }
            memcpy(fname, nb.data, nb.size);
            fname[nb.size] = 0;
        }
        fa_buf_free(&nb);
    }
    if (flg & GZ_FCOMMENT) {
        for (;;) {
            uint8_t c;
            if (off >= src->size || fa_source_read_exact(src, off, &c, 1)) { fa_free(fname); return FULTA_ARC_ERR_TRUNCATED; }
            off++;
            if (c == 0) break;
        }
    }
    if (flg & GZ_FHCRC) {
        off += 2;
        if (off > src->size) { fa_free(fname); return FULTA_ARC_ERR_TRUNCATED; }
    }

    gzip_state_t *st = fa_calloc(1, sizeof *st);
    if (!st) { fa_free(fname); return FULTA_ARC_ERR_NOMEM; }
    st->payload_off = off;

    uint64_t size_hint = UINT64_MAX;
    if (src->size >= off + 8) {                                    /* single-member trailer: CRC-32 then ISIZE */
        uint8_t t[8];
        if (fa_source_read_exact(src, src->size - 8, t, 8)) { fa_free(fname); fa_free(st); return FULTA_ARC_ERR_TRUNCATED; }
        st->crc = fa_le32(t);
        st->has_crc = true;
        size_hint = fa_le32(t + 4);                                /* ISIZE, modulo 2^32: a hint for listing only */
        st->payload_len = (src->size - 8) - off;
    } else {
        st->payload_len = src->size - off;                         /* too short for a trailer; decode to the end */
    }

    char *name = fname ? NULL : gzip_derive_name(arc->name);
    const char *use = fname ? fname : (name ? name : "data");
    fa_entry_t *e = fa_add_entry(arc, (const uint8_t *)use, strlen(use), use, "gzip");
    fa_free(fname);
    fa_free(name);
    if (!e) { fa_free(st); return FULTA_ARC_ERR_NOMEM; }
    e->pub.size = size_hint;
    e->pub.packed_size = st->payload_len;
    if (st->has_crc) { e->pub.crc32 = st->crc; e->pub.flags |= FULTA_ARC_ENTRY_HAS_CRC32; }
    if (mtime) e->pub.mtime = (int64_t)mtime * 1000000000;

    arc->state = st;
    return FULTA_ARC_OK;
}

static fulta_arc_err_t gzip_extract(fulta_arc_t *arc, size_t index, const fulta_arc_sink_t *sink) {
    gzip_state_t *st = arc->state;
    if (index != 0 || !st) return FULTA_ARC_ERR_INVALID_ARG;
    const fulta_arc_source_t *src = arc->volumes[0];
    fa_piece_t piece = { src, 0, src->size };
    fa_range_t range = { &piece, 1, src->size };
    fa_stream_t *s = NULL, *d = NULL;
    fulta_arc_err_t e = fa_stream_range(&range, st->payload_off, st->payload_len, &s);
    if (e) return e;
    if ((e = fa_dec_deflate(s, false, UINT64_MAX, &d))) { fa_stream_destroy(s); return e; }
    e = fa_pump(d, UINT64_MAX, st->has_crc, st->crc, sink, arc, arc->opt.keep_damaged);
    fa_stream_destroy(d);                                     /* owns s; fa_pump does not take the stream */
    return e;
}

static void gzip_close(fulta_arc_t *arc) {
    fa_free(arc->state);
    arc->state = NULL;
}

const fa_format_ops_t fa_gzip_ops = {
    .format = FULTA_ARC_FORMAT_GZIP,
    .probe = gzip_probe,
    .open = gzip_open,
    .extract = gzip_extract,
    .close = gzip_close,
};
