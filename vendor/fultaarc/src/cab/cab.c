/* cab.c - FultaArc: Microsoft Cabinet (.cab) reader. MIT.
 * Original FultaArc code written from Microsoft's published Cabinet format ([MS-CAB]) and RFC 1951 (Deflate, for
 * MSZIP); no GPL/LGPL source (libmspack, cabextract) was read or copied. A cabinet is a CFHEADER, then cFolders
 * CFFOLDER records (each a compressed stream split into CFDATA blocks), then cFiles CFFILE records (each a name plus
 * an offset+length into one folder's *uncompressed* stream). To extract a file we decompress its folder's blocks in
 * order and slice out the file's range. Three compressions are supported: Store (type 0, raw), MSZIP (type 1, a
 * per-CFDATA "CK"+Deflate whose LZ77 window carries across blocks - handled with fa_dec_deflate_dict) and LZX
 * (type 3, src/codec/lzx.c, from the [MS-PATCH] open standard - one continuous bitstream across the folder's
 * CFDATAs) and Quantum (type 2, src/codec/quantum.c, from the sibling clean room's black-box record: adaptive tables and
 * window span the folder, the arithmetic coder restarts per CFDATA). A cabinet SET (cfhdrNEXT_CABINET): when a file
 * continues into the next cabinet and a volume resolver is available, the next cabinets are opened by the name the
 * header declares (same directory), a folder continued across the boundary is decoded as ONE stream (its parts are
 * the per-cabinet CFDATA runs; a CFDATA split at the boundary carries cbUncomp 0 in its first part and the payloads
 * join), and every file of the set is listed once, from the cabinet where it starts. Opened in the middle of a set the
 * reader also walks BACK through szCabinetPrev while a file is continued from the previous cabinet, so the set reads
 * the same from any of its cabinets; when a predecessor cannot be reached, the whole continued folder is unsupported
 * (its offsets count from a start we do not have). No writer, no encryption (CAB carries none we implement). See
 * D-020, cab.md. */
#include "../core/internal.h"

#define CAB_FLAG_PREV    0x0001
#define CAB_FLAG_NEXT    0x0002
#define CAB_FLAG_RESERVE 0x0004
#define CAB_MAX_FOLDERS  20000
#define CAB_BLOCK_MAX    32768u          /* [MS-CAB]: a CFDATA holds at most 32768 uncompressed bytes */
#define CAB_CDATA_MAX    (CAB_BLOCK_MAX + 12u)   /* Store/MSZIP: that + a small MSZIP expansion for compressed bytes */
#define CAB_CDATA_MAX_LZX (CAB_BLOCK_MAX + 6144u) /* LZX: an incompressible chunk (raw block + 12-byte R0-R2 + hdr) */
#define CAB_CDATA_MAX_QUANTUM (CAB_BLOCK_MAX + 6144u) /* Quantum: Diamond writes <= 1.01x on incompressible blocks */
#define CAB_CACHE_CAP    (1u << 30)       /* total decoded folder bytes held in memory across the cabinet (1 GiB) */
#define CAB_NAME_MAX     1024

/* typeCompress packs the compression type in the low nibble and, for LZX (15..21) and Quantum (10..21), the window-size
 * exponent in the next byte: tcompMASK_TYPE 0x000F, tcompMASK_LZX_WINDOW 0x1F00 >> tcompSHIFT_LZX_WINDOW 8 ([MS-CAB]). */
#define CAB_COMP_TYPE(c)  ((c) & 0x000F)
#define CAB_LZX_WBITS(c)  (((c) >> 8) & 0x1F)

#define CAB_MAX_CABINETS 256             /* cabinets followed through NEXT_CABINET links from the opened one */

/* One run of CFDATA blocks of a logical folder inside one cabinet (volume). */
typedef struct cab_part {
    uint32_t vol;           /* index into arc->volumes */
    uint64_t data_off;      /* file offset of this part's first CFDATA */
    uint32_t nblocks;       /* CFDATA block count in this part */
    uint8_t data_reserve;   /* that cabinet's cbCFData: bytes between a CFDATA header and its payload */
} cab_part_t;

/* A logical folder: one compressed stream, usually one part; a folder continued across cabinets has one part per
 * cabinet. uoffFolderStart of its files counts from the logical start. */
typedef struct cab_folder {
    cab_part_t *parts;
    uint32_t nparts;
    uint64_t nblocks;       /* total CFDATA count over the parts */
    uint16_t comp;          /* raw typeCompress word: type in CAB_COMP_TYPE, LZX/Quantum window in CAB_LZX_WBITS */
    uint8_t *cache;         /* decoded uncompressed stream (lazy) */
    uint64_t cache_len;
    bool decoded;
} cab_folder_t;

typedef struct cab_item {
    uint32_t folder;
    uint64_t uoff, size;
    uint32_t cab;           /* cabinet index the file was listed from */
    bool to_next;           /* iFolder 0xFFFE/0xFFFF: the file runs into the next cabinet */
    bool ok;                /* false: continued from a cabinet we do not have / bad folder -> extract returns UNSUPPORTED */
} cab_item_t;

typedef struct cab_state {
    cab_folder_t *folders;
    size_t nfolders, capfolders;
    cab_item_t *items;
    size_t nitems;
    uint64_t cache_total;   /* running sum of folder cache bytes (bomb guard) */
} cab_state_t;

static void cab_close(fulta_arc_t *arc);

/* [MS-CAB] CFDATA checksum: XOR a buffer as little-endian ULONGs, then a big-endian-positioned 1..3-byte tail,
 * onto `seed`. A stored csum of 0 means "no checksum" and is not verified. */
static uint32_t cab_fold(const uint8_t *p, size_t cb, uint32_t seed) {
    uint32_t c = seed;
    size_t n = cb >> 2;
    while (n--) { c ^= (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); p += 4; }
    uint32_t t = 0;
    switch (cb & 3) {
    case 3: t = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | (uint32_t)p[2]; break;
    case 2: t = (uint32_t)p[0] << 8  | (uint32_t)p[1]; break;
    case 1: t = (uint32_t)p[0]; break;
    default: break;
    }
    return c ^ t;
}
/* The abReserve and ab byte arrays are folded INDEPENDENTLY (each starts a fresh 4-byte grouping, so a reserve
 * length not a multiple of 4 does not shift ab's alignment), seeded with (cbData | cbUncomp<<16). Matches 7-Zip. */
static uint32_t cab_checksum(const uint8_t *resv, size_t rlen, const uint8_t *data, size_t dlen,
                             uint16_t cbD, uint16_t cbU) {
    return cab_fold(data, dlen, cab_fold(resv, rlen, (uint32_t)cbD | ((uint32_t)cbU << 16)));
}

/* Read a NUL-terminated string starting at `off`, at most `max` bytes, into `dst` (NUL-terminated). Returns the
 * number of bytes consumed including the NUL, or 0 if no NUL was found within range / the source. */
static size_t cab_read_cstr(const fulta_arc_source_t *src, uint64_t off, char *dst, size_t max) {
    for (size_t i = 0; i < max; i++) {
        uint8_t c;
        if (off + i >= src->size || fa_source_read_exact(src, off + i, &c, 1)) return 0;
        if (i < max - 1) dst[i] = (char)c; else dst[i] = 0;
        if (c == 0) { dst[i] = 0; return i + 1; }
    }
    return 0;
}

/* Decode folder `fi` fully into its cache (idempotent). Bounded by the per-block and total-cache caps. A folder's
 * parts (one per cabinet) are one stream: the MSZIP window, the LZX state and the Quantum tables/window carry across
 * them, and a CFDATA split at a cabinet boundary (cbUncomp 0 in the first part) is decoded once from the joined
 * payloads. */
static fulta_arc_err_t cab_decode_folder(fulta_arc_t *arc, cab_state_t *st, size_t fi) {
    cab_folder_t *f = &st->folders[fi];
    if (f->decoded) return FULTA_ARC_OK;
    uint16_t type = CAB_COMP_TYPE(f->comp);
    if (type > 3) return FULTA_ARC_ERR_UNSUPPORTED;
    unsigned wbits = CAB_LZX_WBITS(f->comp);
    if (type == 3 && (wbits < 15 || wbits > 21)) return FULTA_ARC_ERR_UNSUPPORTED;
    if (type == 2 && (wbits < 10 || wbits > 21)) return FULTA_ARC_ERR_UNSUPPORTED;
    uint32_t cdata_max = (type == 3) ? CAB_CDATA_MAX_LZX : (type == 2) ? CAB_CDATA_MAX_QUANTUM : CAB_CDATA_MAX;
    if (f->nblocks == 0) { f->decoded = true; return FULTA_ARC_OK; }

    /* First pass: validate every CFDATA header and sum the real uncompressed size, so we allocate exactly what the
     * blocks declare (not nblocks x 32768) and the cache cap counts real bytes - a tiny cabinet claiming 32768
     * blocks cannot force a huge allocation before it is caught. A cbUncomp-0 block is legal only as the last block
     * of a part that has a successor (the split block's first half); the joined payload must respect the cap. */
    uint64_t cap = 0;
    uint32_t pend = 0;
    for (uint32_t pi = 0; pi < f->nparts; pi++) {
        const cab_part_t *p = &f->parts[pi];
        const fulta_arc_source_t *src = arc->volumes[p->vol];
        uint64_t scan = p->data_off;
        for (uint32_t b = 0; b < p->nblocks; b++) {
            uint8_t hd[8];
            if (scan + 8 > src->size || fa_source_read_exact(src, scan, hd, 8)) return FULTA_ARC_ERR_TRUNCATED;
            uint16_t cbData = fa_le16(hd + 4), cbUncomp = fa_le16(hd + 6);
            if (cbUncomp > CAB_BLOCK_MAX || cbData > cdata_max || pend + cbData > cdata_max) return FULTA_ARC_ERR_CORRUPT;
            uint64_t next = scan + 8 + p->data_reserve + cbData;
            if (next > src->size) return FULTA_ARC_ERR_TRUNCATED;
            if (cbUncomp == 0) {
                if (b + 1 != p->nblocks) return FULTA_ARC_ERR_CORRUPT;
                if (pi + 1 == f->nparts) break;                  /* the set ends here: the half block is unusable */
                pend += cbData;
            } else {
                pend = 0;
                cap += cbUncomp;
            }
            scan = next;
        }
    }
    if (pend) return FULTA_ARC_ERR_CORRUPT;
    if (cap == 0) { f->decoded = true; return FULTA_ARC_OK; }
    if (cap > CAB_CACHE_CAP || st->cache_total > CAB_CACHE_CAP - cap) return FULTA_ARC_ERR_LIMIT;
    uint8_t *buf = fa_malloc(cap);
    if (!buf) return FULTA_ARC_ERR_NOMEM;
    uint8_t *join = NULL;                                             /* the split block's joined payload */
    uint32_t join_len = 0;

    fa_lzx_t *lz = NULL;                                              /* LZX spans the folder: one state object */
    fa_quantum_t *qt = NULL;                                          /* so does Quantum (tables + window) */
    fulta_arc_err_t e = FULTA_ARC_OK;
    if (type == 3) e = fa_lzx_create(wbits, buf, cap, &lz);
    else if (type == 2) e = fa_quantum_create(wbits, buf, cap, &qt);
    if (e) { fa_free(buf); return e; }

    uint64_t produced = 0;
    for (uint32_t pi = 0; pi < f->nparts && !e; pi++) {
        const cab_part_t *p = &f->parts[pi];
        const fulta_arc_source_t *src = arc->volumes[p->vol];
        uint64_t off = p->data_off;
        for (uint32_t b = 0; b < p->nblocks; b++) {
            uint8_t hdr[8];
            if (off + 8 > src->size || fa_source_read_exact(src, off, hdr, 8)) { e = FULTA_ARC_ERR_TRUNCATED; break; }
            uint32_t csum = fa_le32(hdr);
            uint16_t cbData = fa_le16(hdr + 4), cbUncomp = fa_le16(hdr + 6);
            uint64_t rvoff = off + 8;                                    /* abReserve, then the cbData payload */
            uint32_t total = (uint32_t)p->data_reserve + cbData;         /* the checksum covers abReserve + ab */
            if (cbUncomp > CAB_BLOCK_MAX || cbData > cdata_max) { e = FULTA_ARC_ERR_CORRUPT; break; }
            if (rvoff + total > src->size) { e = FULTA_ARC_ERR_TRUNCATED; break; }
            if (produced + cbUncomp > cap) { e = FULTA_ARC_ERR_CORRUPT; break; }

            uint8_t *rv = fa_malloc(total ? total : 1);
            if (!rv) { e = FULTA_ARC_ERR_NOMEM; break; }
            if (total && fa_source_read_exact(src, rvoff, rv, total)) { fa_free(rv); e = FULTA_ARC_ERR_TRUNCATED; break; }
            if (csum && cab_checksum(rv, p->data_reserve, rv + p->data_reserve, cbData, (uint16_t)cbData, cbUncomp) != csum) {
                fa_free(rv); e = FULTA_ARC_ERR_CHECKSUM; break;
            }
            uint8_t *pay = rv + p->data_reserve;                         /* the compressed/stored block itself */
            uint32_t paylen = cbData;
            off = rvoff + total;
            if (cbUncomp == 0) {                                         /* first half of a block split at the boundary */
                if (pi + 1 == f->nparts) { fa_free(rv); break; }          /* no second half: the files before it are whole */
                if (!join) { join = fa_malloc(cdata_max); if (!join) { fa_free(rv); e = FULTA_ARC_ERR_NOMEM; break; } }
                if (join_len + cbData > cdata_max) { fa_free(rv); e = FULTA_ARC_ERR_CORRUPT; break; }
                memcpy(join + join_len, pay, cbData); join_len += cbData;
                fa_free(rv);
                continue;
            }
            if (join_len) {                                              /* second half: decode the joined payload */
                if (join_len + cbData > cdata_max) { fa_free(rv); e = FULTA_ARC_ERR_CORRUPT; break; }
                memcpy(join + join_len, pay, cbData); paylen = join_len + cbData; join_len = 0;
                pay = join;
            }

            if (type == 0) {                                             /* Store: raw bytes */
                if (paylen != cbUncomp) { fa_free(rv); e = FULTA_ARC_ERR_CORRUPT; break; }
                memcpy(buf + produced, pay, cbUncomp);
            } else if (type == 1) {                                      /* MSZIP: "CK" + Deflate, dict = prior tail */
                if (paylen < 2 || pay[0] != 'C' || pay[1] != 'K') { fa_free(rv); e = FULTA_ARC_ERR_CORRUPT; break; }
                fa_stream_t *ms = NULL, *dec = NULL;                      /* `rv`/`join` back the memory stream; both
                                                                            live until after dec is destroyed, below */
                if ((e = fa_stream_memory(pay + 2, paylen - 2, &ms))) { fa_free(rv); break; }
                size_t dlen = produced < CAB_BLOCK_MAX ? (size_t)produced : CAB_BLOCK_MAX;
                if ((e = fa_dec_deflate_dict(ms, cbUncomp, buf + produced - dlen, dlen, &dec))) { fa_free(rv); break; }
                e = fa_stream_read_exact(dec, buf + produced, cbUncomp);
                fa_stream_destroy(dec);
                if (e) { fa_free(rv); break; }
            } else if (type == 3) {                                      /* LZX: continuous bitstream, this chunk */
                if ((e = fa_lzx_chunk(lz, pay, paylen, cbUncomp))) { fa_free(rv); break; }
            } else {                                                     /* Quantum: coder restarts, tables persist */
                if ((e = fa_quantum_chunk(qt, pay, paylen, cbUncomp))) { fa_free(rv); break; }
            }
            fa_free(rv);
            produced += cbUncomp;
        }
    }
    fa_free(join);
    if (!e && lz) fa_lzx_finish(lz);                                     /* undo E8 x86 translation over the output */
    fa_lzx_destroy(lz);
    fa_quantum_destroy(qt);
    if (e) { fa_free(buf); return e; }
    f->cache = buf;
    f->cache_len = produced;
    f->decoded = true;
    st->cache_total += cap;
    return FULTA_ARC_OK;
}

/* ---- opening: one cabinet, then the NEXT chain ----------------------------------------------------------------- */

typedef struct cab_hdr {
    uint32_t coff_files;
    uint16_t cfolders, cfiles, flags, set_id, icab;
    uint8_t folder_reserve, data_reserve;
    uint64_t folders_off;       /* first CFFOLDER */
    char prev[256];             /* szCabinetPrev (the file name only), "" when absent */
    char next[256];             /* szCabinetNext (the file name only), "" when absent */
} cab_hdr_t;

static fulta_arc_err_t cab_read_header(const fulta_arc_source_t *src, cab_hdr_t *h) {
    uint8_t b[36];
    memset(h, 0, sizeof *h);
    if (src->size < 36 || fa_source_read_exact(src, 0, b, 36)) return FULTA_ARC_ERR_NOT_ARCHIVE;
    if (memcmp(b, "MSCF", 4) != 0 || fa_le32(b + 4) != 0) return FULTA_ARC_ERR_NOT_ARCHIVE;   /* magic + reserved1 */
    h->coff_files = fa_le32(b + 16);
    h->cfolders = fa_le16(b + 26); h->cfiles = fa_le16(b + 28); h->flags = fa_le16(b + 30);
    h->set_id = fa_le16(b + 32); h->icab = fa_le16(b + 34);
    if (h->cfolders == 0 || h->cfolders > CAB_MAX_FOLDERS) return FULTA_ARC_ERR_NOT_ARCHIVE;   /* cFiles is u16-bounded */
    uint64_t pos = 36;
    if (h->flags & CAB_FLAG_RESERVE) {
        uint8_t r[4];
        if (pos + 4 > src->size || fa_source_read_exact(src, pos, r, 4)) return FULTA_ARC_ERR_NOT_ARCHIVE;
        h->folder_reserve = r[2];
        h->data_reserve = r[3];
        pos += 4 + fa_le16(r);                                   /* skip the per-cabinet header reserve */
    }
    for (int side = 0; side < 2; side++) {                      /* PREV then NEXT: two NUL-terminated strings each */
        if ((side == 0 && !(h->flags & CAB_FLAG_PREV)) || (side == 1 && !(h->flags & CAB_FLAG_NEXT))) continue;
        char tmp[256];
        size_t a = cab_read_cstr(src, pos, side ? h->next : h->prev, 256);
        if (!a) return FULTA_ARC_ERR_NOT_ARCHIVE;
        pos += a;
        size_t d = cab_read_cstr(src, pos, tmp, sizeof tmp);    /* the disk name, unused */
        if (!d) return FULTA_ARC_ERR_NOT_ARCHIVE;
        pos += d;
    }
    h->folders_off = pos;
    return FULTA_ARC_OK;
}

static cab_folder_t *cab_new_folder(cab_state_t *st, uint16_t comp) {
    if (st->nfolders == st->capfolders) {
        size_t nc = st->capfolders ? st->capfolders * 2 : 16;
        cab_folder_t *q = fa_realloc(st->folders, nc * sizeof *q);
        if (!q) return NULL;
        st->folders = q; st->capfolders = nc;
    }
    cab_folder_t *f = &st->folders[st->nfolders++];
    memset(f, 0, sizeof *f);
    f->comp = comp;
    return f;
}

static bool cab_add_part(cab_folder_t *f, uint32_t vol, uint64_t data_off, uint32_t nblocks, uint8_t reserve) {
    cab_part_t *q = fa_realloc(f->parts, (f->nparts + 1) * sizeof *q);
    if (!q) return false;
    f->parts = q;
    f->parts[f->nparts++] = (cab_part_t){vol, data_off, nblocks, reserve};
    f->nblocks += nblocks;
    return true;
}

/* Does cabinet `h` hold files continued from the previous cabinet (iFolder 0xFFFD/0xFFFF)? A header-only scan of
 * the CFFILE records, used to decide whether the PREV chain must be walked before anything is listed. */
static fulta_arc_err_t cab_has_from_prev(const fulta_arc_source_t *src, const cab_hdr_t *h, bool *from_prev) {
    *from_prev = false;
    uint64_t fp = h->coff_files;
    for (uint16_t i = 0; i < h->cfiles; i++) {
        uint8_t fe[16];
        if (fp + 16 > src->size || fa_source_read_exact(src, fp, fe, 16)) return FULTA_ARC_ERR_TRUNCATED;
        uint16_t ifolder = fa_le16(fe + 8);
        if (ifolder == 0xFFFD || ifolder == 0xFFFF) *from_prev = true;
        char raw[CAB_NAME_MAX];
        size_t nl = cab_read_cstr(src, fp + 16, raw, sizeof raw);
        if (!nl) return FULTA_ARC_ERR_CORRUPT;
        fp += 16 + nl;
    }
    return FULTA_ARC_OK;
}

/* Parse cabinet `vol` (volume index) into the state: its folders become logical folders - folder 0 continues the
 * previous cabinet's last logical folder when `prev_cont` - and its files are listed (a file continued FROM the
 * previous cabinet is the same file we listed there, so it is skipped when prev_cont; a file continued INTO the next
 * cabinet is listed here and marked to_next). *next_cont says whether a file runs into the next cabinet. */
static fulta_arc_err_t cab_add_cabinet(fulta_arc_t *arc, cab_state_t *st, uint32_t vol, const cab_hdr_t *h,
                                       bool prev_cont, bool *next_cont) {
    /* a cabinet with files continued from a predecessor we did not reach: ALL files of its first folder are
     * unsupported, since their offsets count from a start we do not have (not only the continued files) */
    bool incomplete0 = false;
    if (!prev_cont) {
        fulta_arc_err_t e = cab_has_from_prev(arc->volumes[vol], h, &incomplete0);
        if (e) return e;
    }
    const fulta_arc_source_t *src = arc->volumes[vol];
    size_t base = st->nfolders;                                 /* logical index of this cabinet's first new folder */
    uint64_t pos = h->folders_off;
    for (uint16_t i = 0; i < h->cfolders; i++) {
        uint8_t fd[8];
        if (pos + 8 > src->size || fa_source_read_exact(src, pos, fd, 8)) return FULTA_ARC_ERR_TRUNCATED;
        uint16_t comp = fa_le16(fd + 6);
        cab_folder_t *f;
        if (i == 0 && prev_cont) {
            f = &st->folders[base - 1];
            if (f->comp != comp) return FULTA_ARC_ERR_CORRUPT;  /* a continued folder keeps its compression */
        } else {
            f = cab_new_folder(st, comp);
            if (!f) return FULTA_ARC_ERR_NOMEM;
        }
        if (!cab_add_part(f, vol, fa_le32(fd), fa_le16(fd + 4), h->data_reserve)) return FULTA_ARC_ERR_NOMEM;
        pos += 8 + h->folder_reserve;
    }
    /* logical index of this cabinet's folder i */
#define CAB_LOGICAL(i) ((prev_cont) ? ((i) == 0 ? base - 1 : base + (i) - 1) : base + (i))

    *next_cont = false;
    uint64_t fp = h->coff_files;
    for (uint16_t i = 0; i < h->cfiles; i++) {
        uint8_t fe[16];
        if (fp + 16 > src->size || fa_source_read_exact(src, fp, fe, 16)) return FULTA_ARC_ERR_TRUNCATED;
        uint32_t cbFile = fa_le32(fe);
        uint32_t uoff = fa_le32(fe + 4);
        uint16_t ifolder = fa_le16(fe + 8);
        uint16_t attribs = fa_le16(fe + 14);
        char raw[CAB_NAME_MAX];
        size_t nl = cab_read_cstr(src, fp + 16, raw, sizeof raw);
        if (!nl) return FULTA_ARC_ERR_CORRUPT;
        fp += 16 + nl;

        bool from_prev = ifolder == 0xFFFD || ifolder == 0xFFFF;
        bool to_next = ifolder == 0xFFFE || ifolder == 0xFFFF;
        if (to_next) *next_cont = true;
        if (from_prev && prev_cont) continue;                   /* listed from the cabinet where it starts */
        bool ok = !from_prev && (to_next || ifolder < h->cfolders);
        uint32_t lf = !ok ? 0 : to_next ? (uint32_t)CAB_LOGICAL(h->cfolders - 1) : (uint32_t)CAB_LOGICAL(ifolder);
        if (ok && incomplete0 && lf == (uint32_t)CAB_LOGICAL(0)) ok = false;

        char *name = (attribs & 0x80)                                  /* _A_NAME_IS_UTF: declared UTF-8 - still
                                                                          validated/repaired, as zip.c does */
            ? fa_codepage_to_utf8((const uint8_t *)raw, strlen(raw), FULTA_ARC_CP_UTF8, FULTA_ARC_CP_UTF8)
            : fa_codepage_to_utf8((const uint8_t *)raw, strlen(raw), arc->opt.codepage, FULTA_ARC_CP_1252);
        if (!name) return FULTA_ARC_ERR_NOMEM;
        for (char *p = name; *p; p++) if (*p == '\\') *p = '/';          /* CAB uses backslash path separators */

        char *safe = fa_strndup(name, strlen(name));
        if (safe && !fa_sanitize_name(safe)) { fa_free(safe); safe = NULL; }
        const char *use = safe && safe[0] ? safe : "data";
        uint16_t ftype = ok ? CAB_COMP_TYPE(st->folders[lf].comp) : 0xFF;
        const char *method = !ok ? "unsupported"
            : ftype == 0 ? "store"
            : ftype == 1 ? "MSZIP"
            : ftype == 2 ? "Quantum"
            : ftype == 3 ? "LZX" : "unsupported";
        fa_entry_t *en = fa_add_entry(arc, (const uint8_t *)name, strlen(name), use, method);
        fa_free(safe);
        fa_free(name);
        if (!en) return FULTA_ARC_ERR_NOMEM;
        en->pub.size = cbFile;
        /* a CFFILE has no per-file compressed size (CFDATA compression is folder-level), so report it only for the
         * uncompressed Store case and leave MSZIP at 0 (unknown) rather than overstating it as the file size */
        en->pub.packed_size = (ok && st->folders[lf].comp == 0) ? cbFile : 0;
        if (to_next) en->pub.flags |= FULTA_ARC_ENTRY_SPLIT;

        if (st->nitems == 0 || (st->nitems & (st->nitems - 1)) == 0) {   /* grow by powers of two */
            size_t nc = st->nitems ? st->nitems * 2 : 64;
            cab_item_t *q = fa_realloc(st->items, nc * sizeof *q);
            if (!q) return FULTA_ARC_ERR_NOMEM;
            st->items = q;
        }
        st->items[st->nitems++] = (cab_item_t){lf, uoff, cbFile, vol, to_next, ok};
    }
#undef CAB_LOGICAL
    return FULTA_ARC_OK;
}

/* The path of the next cabinet: the header's file name (a bare name - separators, drive letters and "."/".." are
 * refused) in the directory of the opened file. When the archive has no name, the bare name goes to the resolver. */
static char *cab_sibling_path(const char *first, const char *next) {
    size_t n = strlen(next);
    if (n == 0 || strcmp(next, ".") == 0 || strcmp(next, "..") == 0) return NULL;
    for (size_t i = 0; i < n; i++) if (next[i] == '/' || next[i] == '\\' || next[i] == ':') return NULL;
    size_t dir = 0;
    if (first) {
        for (size_t i = 0; first[i]; i++) if (first[i] == '/' || first[i] == '\\') dir = i + 1;
    }
    char *p = fa_malloc(dir + n + 1);
    if (!p) return NULL;
    if (dir) memcpy(p, first, dir);
    memcpy(p + dir, next, n + 1);
    return p;
}

/* Ask the resolver for a sibling cabinet (NEXT or PREV) by its declared name, then by that name in lower and upper
 * case (sets copied between file systems often carry a different case than their headers: PRECOPY2.CAB / win95_03.cab). */
static fulta_arc_err_t cab_sibling_volume(fulta_arc_t *arc, uint32_t k, const char *next, const fulta_arc_source_t **src) {
    char *path = cab_sibling_path(arc->opt.name, next);
    if (!path) return FULTA_ARC_ERR_VOLUME_MISSING;
    fulta_arc_err_t e = fa_volume(arc, k, path, src);
    size_t dir = strlen(path) - strlen(next);
    for (int variant = 0; e == FULTA_ARC_ERR_VOLUME_MISSING && variant < 2; variant++) {
        bool changed = false;
        for (char *p = path + dir; *p; p++) {
            char c = *p, d = variant == 0 ? (char)((c >= 'A' && c <= 'Z') ? c + 32 : c) : (char)((c >= 'a' && c <= 'z') ? c - 32 : c);
            if (d != c) changed = true;
            *p = d;
        }
        if (changed) e = fa_volume(arc, k, path, src);
    }
    fa_free(path);
    return e;
}

/* ASCII case-insensitive equality of two bare file names (cabinet sets are copied across file systems). */
static bool cab_name_eq(const char *a, const char *b) {
    for (;; a++, b++) {
        char x = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a, y = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (x != y) return false;
        if (!x) return true;
    }
}

static fulta_arc_err_t cab_open(fulta_arc_t *arc, uint64_t offset) {
    (void)offset;
    cab_hdr_t h;
    fulta_arc_err_t e = cab_read_header(arc->volumes[0], &h);
    if (e) return e;
    cab_state_t *st = fa_calloc(1, sizeof *st);
    if (!st) return FULTA_ARC_ERR_NOMEM;
    arc->state = st;

    char open_buf[256];
    const char *open_name = NULL;                               /* bare name of the opened cabinet, when known */
    if (arc->opt.name) {
        const char *q0 = arc->opt.name;
        for (const char *q = arc->opt.name; *q; q++) if (*q == '/' || *q == '\\') q0 = q + 1;
        size_t ql = strlen(q0);
        if (ql >= sizeof open_buf) ql = sizeof open_buf - 1;
        memcpy(open_buf, q0, ql); open_buf[ql] = 0;
        open_name = open_buf;
    }
    uint16_t set_id = h.set_id;
    /* volume indices come from arc->nvolumes at each step: a sibling that resolved but failed validation keeps its
     * slot, so it is never handed back as the answer to the next request */

    /* 1. walk BACK through PREV while this cabinet has files continued from the previous one: the chain is collected
     *    first (headers only) and added earliest-first below, so logical folders get their parts in stream order */
    typedef struct { cab_hdr_t h; uint32_t vol; } cab_back_t;
    cab_back_t *back = NULL;                                    /* back[0] = the opened cabinet's predecessor, ... */
    uint32_t nback = 0;
    {
        cab_hdr_t cur = h;
        const char *cur_name = open_name;
        char cur_buf[256];
        bool from_prev = false;
        if ((e = cab_has_from_prev(arc->volumes[0], &cur, &from_prev))) { cab_close(arc); return e; }
        while ((cur.flags & CAB_FLAG_PREV) && from_prev && cur.icab > 0 && arc->nvolumes < CAB_MAX_CABINETS) {
            const fulta_arc_source_t *src = NULL;
            uint32_t vol = (uint32_t)arc->nvolumes;
            if (cab_sibling_volume(arc, vol, cur.prev, &src) != FULTA_ARC_OK) break;      /* missing: start here */
            cab_hdr_t hp;
            if (cab_read_header(src, &hp) || hp.set_id != set_id || hp.icab + 1 != cur.icab || !(hp.flags & CAB_FLAG_NEXT)) break;
            if (cur_name && !cab_name_eq(hp.next, cur_name)) break;   /* it must point forward at the cabinet we left */
            cab_back_t *q = fa_realloc(back, (nback + 1) * sizeof *q);
            if (!q) { fa_free(back); cab_close(arc); return FULTA_ARC_ERR_NOMEM; }
            back = q; back[nback].h = hp; back[nback].vol = vol; nback++;
            memcpy(cur_buf, cur.prev, sizeof cur_buf);              /* the name we followed is that cabinet's */
            cur = hp; cur_name = cur_buf;
            if ((e = cab_has_from_prev(src, &cur, &from_prev))) { fa_free(back); cab_close(arc); return e; }
        }
    }
    /* 2. add the cabinets in set order: the earliest reached predecessor first ... the opened cabinet (volume 0) */
    bool next_cont = false;
    for (uint32_t i = nback; i > 0; i--) {
        if ((e = cab_add_cabinet(arc, st, back[i - 1].vol, &back[i - 1].h, i != nback, &next_cont))) { fa_free(back); cab_close(arc); return e; }
    }
    if ((e = cab_add_cabinet(arc, st, 0, &h, nback > 0, &next_cont))) { fa_free(back); cab_close(arc); return e; }
    fa_free(back);

    /* 3. follow the NEXT chain while a file runs into the next cabinet (the files that start there are listed too) */
    uint32_t last = 0;                                          /* volume index of the last cabinet added */
    char cur_buf[256];
    const char *cur_name = open_name;
    while ((h.flags & CAB_FLAG_NEXT) && next_cont && arc->nvolumes < CAB_MAX_CABINETS) {
        const fulta_arc_source_t *src = NULL;
        uint32_t vol = (uint32_t)arc->nvolumes;
        if (cab_sibling_volume(arc, vol, h.next, &src) != FULTA_ARC_OK) break;         /* missing: the chain ends here */
        cab_hdr_t h2;
        if (cab_read_header(src, &h2) || h2.set_id != set_id || h2.icab != h.icab + 1 || !(h2.flags & CAB_FLAG_PREV)) break;
        if (cur_name && !cab_name_eq(h2.prev, cur_name)) break;   /* it must point back at the cabinet we came from */
        bool nc = false;
        if ((e = cab_add_cabinet(arc, st, vol, &h2, true, &nc))) { cab_close(arc); return e; }
        memcpy(cur_buf, h.next, sizeof cur_buf);                   /* the name we followed is the new cabinet's */
        h = h2; next_cont = nc; last = vol;
        cur_name = cur_buf;
    }
    /* files continued into a cabinet the chain did not reach stay unsupported; sizes are bounded by the folder */
    for (size_t i = 0; i < st->nitems; i++) {
        cab_item_t *it = &st->items[i];
        if (it->to_next && it->cab == last && next_cont) it->ok = false;
        if (it->ok && it->uoff + it->size > st->folders[it->folder].nblocks * (uint64_t)CAB_BLOCK_MAX) { cab_close(arc); return FULTA_ARC_ERR_CORRUPT; }
    }
    if (arc->count == 0) { cab_close(arc); return FULTA_ARC_ERR_NOT_ARCHIVE; }
    return FULTA_ARC_OK;
}

static fulta_arc_err_t cab_extract(fulta_arc_t *arc, size_t index, const fulta_arc_sink_t *sink) {
    cab_state_t *st = arc->state;
    if (index >= st->nitems) return FULTA_ARC_ERR_INVALID_ARG;
    cab_item_t *it = &st->items[index];
    if (!it->ok) return FULTA_ARC_ERR_UNSUPPORTED;
    if (it->size == 0) return FULTA_ARC_OK;
    fulta_arc_err_t e = cab_decode_folder(arc, st, it->folder);
    if (e) return e;
    cab_folder_t *f = &st->folders[it->folder];
    if (it->uoff + it->size > f->cache_len) return FULTA_ARC_ERR_CORRUPT;              /* the set ended early */
    return sink->write(sink->ctx, f->cache + it->uoff, (size_t)it->size);
}

static void cab_close(fulta_arc_t *arc) {
    cab_state_t *st = arc->state;
    if (!st) return;
    for (size_t i = 0; i < st->nfolders; i++) { fa_free(st->folders[i].cache); fa_free(st->folders[i].parts); }
    fa_free(st->folders);
    fa_free(st->items);
    fa_free(st);
    arc->state = NULL;
}

static bool cab_probe(const uint8_t *head, size_t n, uint64_t *offset) {
    if (n >= 4 && memcmp(head, "MSCF", 4) == 0) { *offset = 0; return true; }
    return false;
}

const fa_format_ops_t fa_cab_ops = {
    .format = FULTA_ARC_FORMAT_CAB,
    .probe = cab_probe,
    .open = cab_open,
    .extract = cab_extract,
    .close = cab_close,
};
