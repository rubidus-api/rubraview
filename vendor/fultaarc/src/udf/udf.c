/* udf.c - FultaArc: UDF (ECMA-167 / OSTA UDF) optical-disc filesystem reader. MIT.
 * Original FultaArc code written from the freely published standard ECMA-167 (the volume and file structure of
 * write-once and rewritable media, i.e. UDF's base) and the public OSTA UDF notes; no GPL/LGPL source (udftools,
 * libudf, wimlib) read or copied. 7-Zip is the external test oracle (it reads UDF). UDF is a separate filesystem
 * that commonly shares a disc with ISO 9660 (a "bridge" disc); when the UDF volume-recognition sequence (an "NSR0x"
 * descriptor) is present FultaArc prefers UDF, matching 7-Zip.
 *   Path: the Anchor Volume Descriptor Pointer at sector 256 -> the Main Volume Descriptor Sequence (Partition
 * Descriptor gives the partition's start sector; Logical Volume Descriptor gives the block size, the type-1
 * partition map and the File Set Descriptor's address) -> the File Set Descriptor -> the root directory's ICB
 * (a File Entry) -> the directory's File Identifier Descriptors -> each file's File Entry -> its data extents.
 * Every descriptor carries an ECMA-167 tag checksum and a CRC-ITU (CRC-16/CCITT); this reader validates both and
 * rejects a descriptor that fails, so a malformed image is caught rather than followed. Addresses inside the
 * partition are partition-relative logical blocks (add the partition start); the anchor and the volume sequence
 * use absolute sectors. Scope: 2048-byte blocks, one type-1 partition map, File Entry (261) and Extended File
 * Entry (266), short_ad and long_ad allocation; VAT/metadata/sparable partitions, named streams, extended
 * attributes and embedded (inline) data are not decoded (UNSUPPORTED/NOT_ARCHIVE). No writer. See D-020. */
#include "../core/internal.h"

#define UDF_SS        2048u         /* this reader supports 2048-byte logical blocks only */
#define UDF_MAX_DEPTH 24
#define UDF_MAX_ENTRIES 200000
#define UDF_MAX_DIRS  100000
#define UDF_MAX_DIR   (1u << 22)    /* a single directory's total FID bytes (bomb guard) */
#define UDF_MAX_VDS   64            /* volume-descriptor-sequence scan cap (sectors) */
#define UDF_MAX_EXT   4096          /* allocation descriptors per file */

typedef struct udf_piece { uint64_t off; uint32_t len; } udf_piece_t;

typedef struct udf_item {
    udf_piece_t *ext;               /* the file's data extents (absolute byte offsets), in order */
    size_t next;
    uint64_t size;
} udf_item_t;

typedef struct udf_state {
    uint32_t part_start;            /* partition starting sector (absolute) */
    uint32_t part_len;              /* partition length in blocks */
    udf_item_t *items;
    size_t n, cap;
    uint64_t records;
    uint32_t *visited; size_t nvis, capvis;   /* visited directory blocks (cycle guard) */
} udf_state_t;

/* CRC-ITU-T (CRC-16/CCITT, poly 0x1021, seed 0, no reflection) - ECMA-167 7.2.6. */
static uint16_t udf_crc(const uint8_t *p, size_t n) {
    uint16_t c = 0;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)p[i] << 8;
        for (int k = 0; k < 8; k++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}

/* Validate a descriptor tag in `d` (at least 16 + crc_len bytes). Returns the tag id, or 0 on any failure. */
static uint16_t udf_tag(const uint8_t *d, size_t avail, uint16_t *out_id) {
    if (avail < 16) return 0;
    uint16_t id = fa_le16(d);
    uint16_t crc_len = fa_le16(d + 10);
    if (16u + crc_len > avail) return 0;
    uint32_t sum = 0;                          /* tag checksum: all 16 tag bytes except byte 4 */
    for (int i = 0; i < 16; i++) if (i != 4) sum += d[i];
    if ((sum & 0xff) != d[4]) return 0;
    if (udf_crc(d + 16, crc_len) != fa_le16(d + 8)) return 0;
    if (out_id) *out_id = id;
    return id ? id : 0xffff;                    /* a valid tag id 0 is not expected; map it away from "fail" */
}

static bool udf_seen(udf_state_t *st, uint32_t blk) {
    for (size_t i = 0; i < st->nvis; i++) if (st->visited[i] == blk) return true;
    if (st->nvis < UDF_MAX_DIRS) {
        if (st->nvis == st->capvis) {
            size_t nc = st->capvis ? st->capvis * 2 : 64;
            uint32_t *p = fa_realloc(st->visited, nc * sizeof *p);
            if (!p) return true;
            st->visited = p; st->capvis = nc;
        }
        st->visited[st->nvis++] = blk;
    }
    return false;
}

static void udf_close(fulta_arc_t *arc);

/* Convert a partition-relative logical block to an absolute byte offset, bounded to the partition. */
static bool udf_pr2abs(udf_state_t *st, uint32_t lbn, uint64_t *out) {
    if (lbn >= st->part_len) return false;
    *out = ((uint64_t)st->part_start + lbn) * UDF_SS;           /* widen before adding: no 32-bit overflow */
    return true;
}

/* Decode an OSTA CS0 d-characters name (compression id 8 or 16) into UTF-8. Returns a fresh string or NULL. */
static char *udf_name(const uint8_t *fi, uint8_t len) {
    if (len == 0) return fa_strndup("", 0);
    uint8_t comp = fi[0];
    if (comp == 8) {                                             /* 8-bit CS0 = Latin-1; CP_1252 is the nearest map */
        return fa_codepage_to_utf8(fi + 1, len - 1, FULTA_ARC_CP_1252, FULTA_ARC_CP_1252);
    } else if (comp == 16) {
        size_t units = (len - 1) / 2;
        if (units == 0) return fa_strndup("", 0);
        uint8_t stackbuf[512];
        uint8_t *le = units * 2 <= sizeof stackbuf ? stackbuf : fa_malloc(units * 2);
        if (!le) return NULL;
        for (size_t i = 0; i < units; i++) { le[2 * i] = fi[1 + 2 * i + 1]; le[2 * i + 1] = fi[1 + 2 * i]; }
        char *u = fa_utf16le_to_utf8(le, units);
        if (le != stackbuf) fa_free(le);
        return u;
    }
    return fa_strndup("", 0);                                     /* unknown compression: empty (skipped by caller) */
}

static udf_item_t *udf_add(udf_state_t *st) {
    if (st->n == st->cap) {
        size_t nc = st->cap ? st->cap * 2 : 64;
        udf_item_t *q = fa_realloc(st->items, nc * sizeof *q);
        if (!q) return NULL;
        st->items = q; st->cap = nc;
    }
    st->items[st->n] = (udf_item_t){0};
    return &st->items[st->n++];
}

/* Parse a File Entry (261) or Extended File Entry (266) in `fe` (one block): set *file_type and, for the allocation
 * descriptors, the extents (collected into `pieces`/`npieces`, bounded) with their total length *info_len. */
static fulta_arc_err_t udf_parse_fe(udf_state_t *st, const uint8_t *fe, uint16_t tagid,
                                    uint8_t *file_type, uint64_t *info_len, udf_piece_t **pieces, size_t *npieces) {
    const uint8_t *body = fe + 16;
    *file_type = body[11];
    uint8_t adtype = body[18] & 0x07;          /* icbtag flags: 0 short_ad, 1 long_ad, 3 embedded */
    uint32_t len_ea, len_ad, ad_base;
    if (tagid == 261) { *info_len = fa_le64(body + 40); len_ea = fa_le32(body + 152); len_ad = fa_le32(body + 156); ad_base = 160; }
    else               { *info_len = fa_le64(body + 40); len_ea = fa_le32(body + 192); len_ad = fa_le32(body + 196); ad_base = 200; }
    if (adtype == 3) return FULTA_ARC_ERR_UNSUPPORTED;            /* embedded data: not handled */
    if (adtype != 0 && adtype != 1) return FULTA_ARC_ERR_UNSUPPORTED;
    uint64_t ad_off = (uint64_t)ad_base + len_ea;
    if (16u + ad_off + len_ad > UDF_SS) return FULTA_ARC_ERR_CORRUPT;   /* ADs live in the FE's own block (tag+body) */

    size_t stride = adtype == 0 ? 8 : 16;
    size_t cnt = len_ad / stride;
    if (cnt > UDF_MAX_EXT) return FULTA_ARC_ERR_CORRUPT;
    udf_piece_t *pv = cnt ? fa_malloc(cnt * sizeof *pv) : NULL;
    if (cnt && !pv) return FULTA_ARC_ERR_NOMEM;
    size_t np = 0;
    for (size_t i = 0; i < cnt; i++) {
        const uint8_t *a = body + ad_off + i * stride;
        uint32_t elen = fa_le32(a) & 0x3fffffff;
        uint32_t etype = fa_le32(a) >> 30;
        uint32_t blk = fa_le32(a + 4);          /* short_ad extent position / long_ad logical block (both at +4) */
        if (etype == 3) break;                  /* allocation-extent continuation: not followed (extract -> short) */
        if (elen > (uint64_t)st->part_len * UDF_SS) { fa_free(pv); return FULTA_ARC_ERR_CORRUPT; }
        if (etype != 0) {                        /* unallocated / not-recorded: a hole of `elen` zero bytes */
            pv[np].off = UINT64_MAX; pv[np].len = elen; np++; continue;
        }
        uint64_t off;
        if (!udf_pr2abs(st, blk, &off)) { fa_free(pv); return FULTA_ARC_ERR_CORRUPT; }
        pv[np].off = off; pv[np].len = elen; np++;
    }
    *pieces = pv; *npieces = np;
    return FULTA_ARC_OK;
}

/* Read the File Entry at partition block `fe_blk`, and if it is a regular file record its extents as an entry. */
static fulta_arc_err_t udf_file(fulta_arc_t *arc, udf_state_t *st, uint32_t fe_blk, const char *path);

static fulta_arc_err_t udf_walk(fulta_arc_t *arc, udf_state_t *st, uint32_t dir_fe_blk, int depth, const char *prefix) {
    const fulta_arc_source_t *src = arc->volumes[0];
    if (depth > UDF_MAX_DEPTH) return FULTA_ARC_OK;
    if (udf_seen(st, dir_fe_blk)) return FULTA_ARC_OK;
    uint64_t fe_off;
    if (!udf_pr2abs(st, dir_fe_blk, &fe_off)) return FULTA_ARC_OK;

    uint8_t fe[UDF_SS];
    if (fa_source_read_exact(src, fe_off, fe, UDF_SS)) return FULTA_ARC_OK;
    uint16_t id = 0;
    if (!udf_tag(fe, UDF_SS, &id) || (id != 261 && id != 266)) return FULTA_ARC_OK;
    uint8_t ftype; uint64_t dlen; udf_piece_t *dpieces = NULL; size_t dnp = 0;
    fulta_arc_err_t e = udf_parse_fe(st, fe, id, &ftype, &dlen, &dpieces, &dnp);
    if (e == FULTA_ARC_ERR_UNSUPPORTED) return FULTA_ARC_OK;     /* an exotic directory we cannot read: skip */
    if (e) return e;
    if (ftype != 4 || dlen == 0 || dlen > UDF_MAX_DIR) { fa_free(dpieces); return FULTA_ARC_OK; }

    uint8_t *data = fa_malloc((size_t)dlen);                     /* gather the directory's FID bytes */
    if (!data) { fa_free(dpieces); return FULTA_ARC_ERR_NOMEM; }
    uint64_t got = 0;
    for (size_t i = 0; i < dnp && got < dlen; i++) {
        uint32_t want = (uint32_t)(dlen - got < dpieces[i].len ? dlen - got : dpieces[i].len);
        if (dpieces[i].off + want > src->size || fa_source_read_exact(src, dpieces[i].off, data + got, want)) break;
        got += want;
    }
    fa_free(dpieces);
    if (got < dlen) dlen = got;

    uint32_t p = 0;
    while (p + 38 <= dlen) {                                     /* FID: 16 tag + 20 fixed + names */
        uint16_t fid_id = 0;
        if (!udf_tag(data + p, (size_t)(dlen - p), &fid_id) || fid_id != 257) break;
        const uint8_t *b = data + p + 16;
        uint8_t fc = b[2], len_fi = b[3];
        uint32_t blk = fa_le32(b + 4 + 4);                      /* ICB long_ad: extent_length(4) then lb block(4) */
        uint16_t len_iu = fa_le16(b + 20);
        uint32_t fi_off = 22u + len_iu;
        uint32_t reclen = 38u + len_iu + len_fi;
        reclen = (reclen + 3) & ~3u;                            /* padded to 4 bytes */
        if (p + 16 + fi_off + len_fi > dlen) break;
        if (++st->records > UDF_MAX_ENTRIES) break;
        if (!(fc & 0x08) && !(fc & 0x04)) {                     /* skip the parent entry and deleted entries */
            char *nm = len_fi ? udf_name(b + fi_off, len_fi) : fa_strndup("", 0);
            if (!nm) { fa_free(data); return FULTA_ARC_ERR_NOMEM; }
            if (nm[0]) {
                char path[1024];
                size_t pl = strlen(prefix), nl = strlen(nm);
                if (pl + 1 + nl + 1 <= sizeof path) {
                    size_t k = pl; memcpy(path, prefix, pl);
                    if (pl) path[k++] = '/';
                    memcpy(path + k, nm, nl); k += nl; path[k] = 0;
                    if (fc & 0x02) e = udf_walk(arc, st, blk, depth + 1, path);   /* directory */
                    else            e = udf_file(arc, st, blk, path);             /* regular file */
                }
            }
            fa_free(nm);
            if (e) break;
        }
        if (reclen < 38) break;
        p += reclen;
    }
    fa_free(data);
    return e;
}

static fulta_arc_err_t udf_file(fulta_arc_t *arc, udf_state_t *st, uint32_t fe_blk, const char *path) {
    const fulta_arc_source_t *src = arc->volumes[0];
    uint64_t fe_off;
    if (!udf_pr2abs(st, fe_blk, &fe_off)) return FULTA_ARC_OK;
    uint8_t fe[UDF_SS];
    if (fa_source_read_exact(src, fe_off, fe, UDF_SS)) return FULTA_ARC_OK;
    uint16_t id = 0;
    if (!udf_tag(fe, UDF_SS, &id) || (id != 261 && id != 266)) return FULTA_ARC_OK;
    uint8_t ftype; uint64_t ilen; udf_piece_t *pieces = NULL; size_t np = 0;
    fulta_arc_err_t e = udf_parse_fe(st, fe, id, &ftype, &ilen, &pieces, &np);
    if (e == FULTA_ARC_ERR_UNSUPPORTED) { return FULTA_ARC_OK; }  /* list nothing we cannot read back */
    if (e) return e;
    if (ftype != 5) { fa_free(pieces); return FULTA_ARC_OK; }     /* only regular files become entries */

    char *safe = fa_strndup(path, strlen(path));
    if (safe && !fa_sanitize_name(safe)) { fa_free(safe); safe = NULL; }
    const char *use = safe && safe[0] ? safe : "data";
    fa_entry_t *en = fa_add_entry(arc, (const uint8_t *)path, strlen(path), use, "store");
    udf_item_t *it = udf_add(st);
    fa_free(safe);
    if (!en || !it) { fa_free(pieces); return FULTA_ARC_ERR_NOMEM; }
    en->pub.size = ilen;
    en->pub.packed_size = ilen;
    it->ext = pieces; it->next = np; it->size = ilen;
    return FULTA_ARC_OK;
}

/* Scan the Volume Recognition Sequence at sector 16+ for an "NSR02"/"NSR03": the mark that this is UDF. */
static bool udf_present(const fulta_arc_source_t *src) {
    for (uint32_t s = 16; s < 16 + 8; s++) {
        uint64_t o = (uint64_t)s * UDF_SS;
        if (o + 7 > src->size) break;
        uint8_t h[7];
        if (fa_source_read_exact(src, o, h, 7)) break;
        if (memcmp(h + 1, "NSR02", 5) == 0 || memcmp(h + 1, "NSR03", 5) == 0) return true;
        if (memcmp(h + 1, "TEA01", 5) == 0) break;              /* end of the extended area */
    }
    return false;
}

static fulta_arc_err_t udf_open(fulta_arc_t *arc, uint64_t offset) {
    (void)offset;
    const fulta_arc_source_t *src = arc->volumes[0];
    if (src->size < 257ull * UDF_SS) return FULTA_ARC_ERR_NOT_ARCHIVE;
    if (!udf_present(src)) return FULTA_ARC_ERR_NOT_ARCHIVE;

    uint8_t avdp[UDF_SS];
    if (fa_source_read_exact(src, 256ull * UDF_SS, avdp, UDF_SS)) return FULTA_ARC_ERR_NOT_ARCHIVE;
    uint16_t id = 0;
    if (!udf_tag(avdp, UDF_SS, &id) || id != 2) return FULTA_ARC_ERR_NOT_ARCHIVE;   /* Anchor VD Pointer */
    uint32_t vds_len = fa_le32(avdp + 16), vds_loc = fa_le32(avdp + 20);
    if (vds_loc == 0 || vds_len == 0) return FULTA_ARC_ERR_NOT_ARCHIVE;

    udf_state_t *st = fa_calloc(1, sizeof *st);
    if (!st) return FULTA_ARC_ERR_NOMEM;
    arc->state = st;

    bool have_part = false, have_lvd = false;
    uint32_t fsd_blk = 0, lb_size = 0;
    uint32_t nsec = vds_len / UDF_SS; if (nsec > UDF_MAX_VDS) nsec = UDF_MAX_VDS;
    for (uint32_t i = 0; i < nsec; i++) {
        uint8_t d[UDF_SS];
        if (fa_source_read_exact(src, ((uint64_t)vds_loc + i) * UDF_SS, d, UDF_SS)) break;
        uint16_t t = 0;
        if (!udf_tag(d, UDF_SS, &t)) continue;                  /* skip an unreadable slot, keep scanning */
        if (t == 8) break;                                       /* Terminating Descriptor */
        if (t == 5) {                                            /* Partition Descriptor */
            st->part_start = fa_le32(d + 16 + 188 - 16);         /* partition starting location */
            st->part_len = fa_le32(d + 16 + 192 - 16);
            have_part = true;
        } else if (t == 6) {                                     /* Logical Volume Descriptor */
            lb_size = fa_le32(d + 16 + 196);
            const uint8_t *fsd_ad = d + 16 + 232;                /* long_ad to the File Set Descriptor */
            fsd_blk = fa_le32(fsd_ad + 4);
            const uint8_t *pmap = d + 16 + 424;                  /* body+424 = first partition map entry */
            /* a non-type-1 map (e.g. a VAT) is UDF we cannot read; return NOT_ARCHIVE (not UNSUPPORTED) so a bridge
             * disc still falls through to the ISO 9660 reader (open_core only continues on NOT_ARCHIVE). */
            if (pmap[0] != 1) { udf_close(arc); return FULTA_ARC_ERR_NOT_ARCHIVE; }
            have_lvd = true;
        }
    }
    if (!have_part || !have_lvd) { udf_close(arc); return FULTA_ARC_ERR_NOT_ARCHIVE; }
    if (lb_size != UDF_SS) { udf_close(arc); return FULTA_ARC_ERR_NOT_ARCHIVE; }    /* only 2048-byte blocks; let ISO try */
    if ((uint64_t)st->part_start + st->part_len > src->size / UDF_SS) { udf_close(arc); return FULTA_ARC_ERR_NOT_ARCHIVE; }

    uint64_t fsd_off;
    if (!udf_pr2abs(st, fsd_blk, &fsd_off)) { udf_close(arc); return FULTA_ARC_ERR_NOT_ARCHIVE; }
    uint8_t fsd[UDF_SS];
    if (fa_source_read_exact(src, fsd_off, fsd, UDF_SS)) { udf_close(arc); return FULTA_ARC_ERR_NOT_ARCHIVE; }
    if (!udf_tag(fsd, UDF_SS, &id) || id != 256) { udf_close(arc); return FULTA_ARC_ERR_NOT_ARCHIVE; }
    uint32_t root_blk = fa_le32(fsd + 16 + 384 + 4);             /* root directory ICB long_ad */

    fulta_arc_err_t e = udf_walk(arc, st, root_blk, 0, "");
    if (e) { udf_close(arc); return e; }
    return FULTA_ARC_OK;
}

static fulta_arc_err_t udf_extract(fulta_arc_t *arc, size_t index, const fulta_arc_sink_t *sink) {
    udf_state_t *st = arc->state;
    if (index >= st->n) return FULTA_ARC_ERR_INVALID_ARG;
    udf_item_t *it = &st->items[index];
    const fulta_arc_source_t *src = arc->volumes[0];
    uint64_t remain = it->size;
    uint8_t buf[UDF_SS];
    for (size_t i = 0; i < it->next && remain; i++) {
        uint64_t off = it->ext[i].off;
        uint32_t left = it->ext[i].len;
        bool hole = off == UINT64_MAX;          /* an unallocated extent: emit zeros */
        while (left && remain) {
            uint32_t n = left < sizeof buf ? left : (uint32_t)sizeof buf;
            if (n > remain) n = (uint32_t)remain;
            if (hole) {
                memset(buf, 0, n);
            } else if (off + n > src->size || fa_source_read_exact(src, off, buf, n)) {
                return FULTA_ARC_ERR_TRUNCATED;
            }
            fulta_arc_err_t e = sink->write(sink->ctx, buf, n);
            if (e) return e;
            off += n; left -= n; remain -= n;
        }
    }
    if (remain) return FULTA_ARC_ERR_TRUNCATED; /* extents ran out before the declared size (e.g. an AD continuation) */
    return FULTA_ARC_OK;
}

static void udf_close(fulta_arc_t *arc) {
    udf_state_t *st = arc->state;
    if (!st) return;
    for (size_t i = 0; i < st->n; i++) fa_free(st->items[i].ext);
    fa_free(st->items);
    fa_free(st->visited);
    fa_free(st);
    arc->state = NULL;
}

static bool udf_probe(const uint8_t *head, size_t n, uint64_t *offset) {
    (void)head; (void)n; (void)offset;
    return false;                               /* the NSR0x mark is at sector 16+, past the probe window */
}

const fa_format_ops_t fa_udf_ops = {
    .format = FULTA_ARC_FORMAT_UDF,
    .probe = udf_probe,
    .open = udf_open,
    .extract = udf_extract,
    .close = udf_close,
};
