/* iso.c - FultaArc: ISO 9660 (ECMA-119) reader, with Joliet and Rock Ridge. MIT.
 * Original FultaArc code from freely published standards; no GPL/LGPL source read or copied. The volume descriptors
 * begin at sector 16 (offset 32768); the Primary Volume Descriptor (type 1, "CD001") points at the root directory,
 * whose extent holds directory records that name the files and subdirectories. This reader walks that tree and
 * streams each file's extent (ISO 9660 is uncompressed, no encryption).
 *   Joliet (Microsoft's Unicode extension) is a Supplementary Volume Descriptor (type 2) whose escape sequence is
 * %/@, %/C or %/E; its directory records carry the same layout but with UCS-2BE names, giving the real long names of
 * a Windows-authored disc. Rock Ridge (IEEE P1281 SUSP + P1282 RRIP) hangs POSIX metadata off each directory
 * record's System-Use area: NM (alternate name), SL (symlink target), PX (mode), CE (continuation), CL/RE (deep-
 * directory relocation). We prefer Rock Ridge, then Joliet, then the plain 8.3 identifier: RR carries symlinks and
 * modes and a `-R -J` disc agrees on names, so RR is the richer, lossless choice (7z prefers Joliet instead; a
 * both-trees disc with disagreeing names therefore lists differently - pinned in test_iso). No writer. See D-020. */
#include "../core/internal.h"

#define ISO_SYS_AREA 32768          /* 16 sectors of 2048 before the volume descriptors */
#define ISO_MAX_DEPTH 24
#define ISO_MAX_DIR   (1u << 20)    /* cap one directory's extent before reading (untrusted input; ~30k records) */
#define ISO_MAX_ENTRIES 200000      /* a bomb guard on the total record count */
#define ISO_MAX_DIRS 100000         /* visited-directory cap */
#define ISO_MAX_VDS  64             /* volume-descriptor scan cap */
#define ISO_MAX_CE   8              /* System-Use continuation hops per record (cycle guard) */
#define ISO_MAX_CE_AREA (1u << 16)  /* a single CE continuation area (within a sector by convention); read cap */
#define ISO_MAX_NAME 1000           /* assembled RR name / symlink target cap (path buffer is 1024) */
#define ISO_ZF_HDR   16             /* a zisofs file header: 8 magic + 4 uncompressed size + 1 hdr + 1 blog + 2 pad */
#define ISO_ZF_MAX   (1ull << 34)   /* a zisofs file's uncompressed size cap (16 GiB; a bomb guard) */

static const uint8_t ISO_ZF_MAGIC[8] = { 0x37, 0xE4, 0x53, 0x96, 0xC9, 0xDB, 0xD6, 0x07 };

enum iso_mode { ISO_PLAIN, ISO_JOLIET, ISO_RR };

typedef struct iso_item {
    uint64_t off, size;             /* a regular file's extent (for zisofs, the compressed extent) */
    uint8_t *link; uint32_t linklen;/* a Rock Ridge symlink's target (assembled, not a source range) */
    bool zisofs; uint8_t block_log2;/* zisofs transparent compression: block table + per-block zlib */
    uint64_t usize;                 /* zisofs uncompressed size */
} iso_item_t;

typedef struct iso_state {
    iso_item_t *items;
    size_t n, cap;
    uint32_t blocksize;
    uint64_t records;               /* total records parsed (bomb guard) */
    uint32_t *visited;              /* directory LBAs already walked (cycle guard) */
    size_t nvis, capvis;
} iso_state_t;

/* Rock Ridge facts gathered from one record's System-Use area. */
typedef struct rr_info {
    char name[ISO_MAX_NAME + 1]; size_t namelen; bool have_name;
    uint8_t target[ISO_MAX_NAME + 1]; size_t tlen; bool is_link;
    bool relocated;                 /* RE: this record is a relocated deep directory, skip it here */
    uint32_t cl_lba; bool have_cl;  /* CL: the record's real children live at this LBA */
} rr_info_t;

/* True if `lba` was already walked; otherwise remembers it. A directory cycle (a record pointing back at an
 * ancestor, or two directories referencing each other) is bounded by this, not just by the depth cap. */
static bool iso_seen(iso_state_t *st, uint32_t lba) {
    for (size_t i = 0; i < st->nvis; i++) if (st->visited[i] == lba) return true;
    if (st->nvis < ISO_MAX_DIRS) {
        if (st->nvis == st->capvis) {
            size_t nc = st->capvis ? st->capvis * 2 : 64;
            uint32_t *p = fa_realloc(st->visited, nc * sizeof *p);
            if (!p) return true;    /* out of memory for the set: treat as seen, stop descending */
            st->visited = p;
            st->capvis = nc;
        }
        st->visited[st->nvis++] = lba;
    }
    return false;
}

static iso_item_t *iso_add(iso_state_t *st) {
    if (st->n == st->cap) {
        size_t nc = st->cap ? st->cap * 2 : 64;
        iso_item_t *q = fa_realloc(st->items, nc * sizeof *q);
        if (!q) return NULL;
        st->items = q;
        st->cap = nc;
    }
    st->items[st->n] = (iso_item_t){0};         /* zero link/off/size; realloc leaves new slots uninitialised */
    return &st->items[st->n++];
}

/* Clean an ISO 9660 file identifier: drop the ";version" suffix and a single trailing '.'. Edits in place. */
static void iso_clean_name(char *name, size_t *len) {
    size_t n = *len;
    for (size_t i = 0; i < n; i++) if (name[i] == ';') { n = i; break; }
    if (n && name[n - 1] == '.') n--;
    name[n] = 0;
    *len = n;
}

/* Decode a Joliet (UCS-2BE) identifier into UTF-8. Returns a fresh string (caller frees) or NULL. `units` is the
 * number of 16-bit code units (lenfi/2). We byte-swap into a temp and reuse the UTF-16LE converter. */
static char *iso_joliet_name(const uint8_t *fi, size_t units) {
    if (units == 0 || units > ISO_MAX_NAME) return NULL;
    uint8_t stackbuf[512];
    uint8_t *le = units * 2 <= sizeof stackbuf ? stackbuf : fa_malloc(units * 2);
    if (!le) return NULL;
    for (size_t i = 0; i < units; i++) { le[2 * i] = fi[2 * i + 1]; le[2 * i + 1] = fi[2 * i]; }
    char *u = fa_utf16le_to_utf8(le, units);
    if (le != stackbuf) fa_free(le);
    if (!u) return NULL;
    size_t n = strlen(u);                       /* strip ;version and a trailing '.' on the decoded side */
    for (size_t i = 0; i < n; i++) if (u[i] == ';') { n = i; break; }
    if (n && u[n - 1] == '.') n--;
    u[n] = 0;
    return u;
}

/* Append one Rock Ridge SL component to the assembled symlink target, mapping the special component flags to path
 * pieces and separating components with '/'. `cont` means the previous component carried CONTINUE, so this text
 * joins it with no separator. Bounded by ISO_MAX_NAME. */
static void rr_sl_component(rr_info_t *rr, uint8_t cflags, const uint8_t *txt, size_t len, bool cont) {
    if (cflags & 0x08) {                                            /* ROOT: an absolute "/" prefix */
        if (rr->tlen == 0 && rr->tlen < ISO_MAX_NAME) rr->target[rr->tlen++] = '/';
        return;
    }
    if (!cont && rr->tlen && rr->target[rr->tlen - 1] != '/' && rr->tlen < ISO_MAX_NAME)
        rr->target[rr->tlen++] = '/';
    const char *lit = (cflags & 0x02) ? "." : (cflags & 0x04) ? ".." : NULL;
    if (lit) { for (size_t i = 0; lit[i] && rr->tlen < ISO_MAX_NAME; i++) rr->target[rr->tlen++] = (uint8_t)lit[i]; return; }
    for (size_t i = 0; i < len && rr->tlen < ISO_MAX_NAME; i++) rr->target[rr->tlen++] = txt[i];
}

/* Scan one System-Use byte range [p,end) for Rock Ridge entries, filling `rr`. Follows CE continuations into other
 * blocks (bounded by *ce_budget). `sl_cont`/`nm_cont` carry the "continues" state across CE boundaries. */
static void rr_scan(fulta_arc_t *arc, iso_state_t *st, const uint8_t *buf, uint32_t p, uint32_t end,
                    rr_info_t *rr, int *ce_budget, bool *sl_sep) {
    while (p + 4 <= end) {
        const uint8_t *e = buf + p;
        uint8_t len = e[2];
        if (len < 4 || p + len > end) break;            /* malformed entry: stop, don't loop */
        if (e[0] == 'S' && e[1] == 'T') break;          /* explicit terminator */
        else if (e[0] == 'N' && e[1] == 'M' && len >= 5) {
            uint8_t flags = e[4];
            if (!(flags & 0x06)) {                       /* not the "." / ".." alternate name */
                for (size_t i = 5; i < len && rr->namelen < ISO_MAX_NAME; i++) rr->name[rr->namelen++] = (char)e[i];
                rr->have_name = true;                    /* bit0 CONTINUE: more NM follows, keep appending */
            }
        } else if (e[0] == 'S' && e[1] == 'L' && len >= 5) {
            rr->is_link = true;
            uint32_t cp = p + 5;                         /* component records after the 1-byte SL flags */
            while (cp + 2 <= p + len) {
                uint8_t cflags = buf[cp];
                uint8_t clen = buf[cp + 1];
                if (cp + 2 + clen > p + len) break;
                rr_sl_component(rr, cflags, buf + cp + 2, clen, *sl_sep);
                *sl_sep = (cflags & 0x01) != 0;          /* CONTINUE: next piece joins this component, no '/' */
                cp += 2 + clen;
            }
        } else if (e[0] == 'C' && e[1] == 'E' && len >= 28) {
            if ((*ce_budget)-- > 0) {                    /* continuation area in another block */
                uint32_t clba = fa_le32(e + 4), coff = fa_le32(e + 12), clen = fa_le32(e + 20);
                const fulta_arc_source_t *src = arc->volumes[0];
                uint64_t cbase = (uint64_t)clba * st->blocksize + coff;
                if (clen && clen <= ISO_MAX_CE_AREA && cbase < src->size && clen <= src->size - cbase) {
                    uint8_t *cbuf = fa_malloc(clen);
                    if (cbuf && fa_source_read_exact(src, cbase, cbuf, clen) == 0)
                        rr_scan(arc, st, cbuf, 0, clen, rr, ce_budget, sl_sep);
                    fa_free(cbuf);
                }
            }
            break;                                       /* CE is the last usable entry of this area */
        } else if (e[0] == 'R' && e[1] == 'E') {
            rr->relocated = true;
        } else if (e[0] == 'C' && e[1] == 'L' && len >= 12) {
            rr->cl_lba = fa_le32(e + 4);
            rr->have_cl = true;
        }
        p += len;
    }
}

static void iso_close(fulta_arc_t *arc);

/* Read a directory's own length from the "." record at the start of its extent (used to follow a Rock Ridge CL
 * relocation, whose placeholder carries no length). Returns the directory's data length, or 0 if out of range or
 * the first record is not a well-formed "." entry. */
static uint32_t iso_dir_length(const fulta_arc_source_t *src, iso_state_t *st, uint32_t lba) {
    uint64_t base = (uint64_t)lba * st->blocksize;
    if (base >= src->size || src->size - base < 34) return 0;
    uint8_t rec[34];
    if (fa_source_read_exact(src, base, rec, sizeof rec)) return 0;
    if (rec[0] < 34 || rec[32] != 1 || rec[33] != 0) return 0;   /* record 0 must be "." (0x00) */
    uint32_t len = fa_le32(rec + 10);
    return len <= ISO_MAX_DIR ? len : 0;                          /* cap here, not only at the iso_walk callee */
}

/* Recursively walk a directory extent at `lba`, `length` bytes. `prefix` is the path so far ("" for root).
 * `mode` selects the name layer; `skip` is the SUSP LEN_SKP (bytes to ignore at the start of each System-Use area). */
static fulta_arc_err_t iso_walk(fulta_arc_t *arc, iso_state_t *st, uint32_t lba, uint32_t length,
                                int depth, const char *prefix, int mode, unsigned skip) {
    const fulta_arc_source_t *src = arc->volumes[0];
    if (depth > ISO_MAX_DEPTH || length == 0 || length > ISO_MAX_DIR) return FULTA_ARC_OK;
    if (iso_seen(st, lba)) return FULTA_ARC_OK;   /* already walked: a cycle, or a shared/looped extent */
    uint64_t base = (uint64_t)lba * st->blocksize;
    if (base >= src->size || length > src->size - base) return FULTA_ARC_OK;

    uint8_t *buf = fa_malloc(length);
    if (!buf) return FULTA_ARC_ERR_NOMEM;
    if (fa_source_read_exact(src, base, buf, length)) { fa_free(buf); return FULTA_ARC_OK; }

    fulta_arc_err_t e = FULTA_ARC_OK;
    uint32_t p = 0;
    while (p + 33 <= length) {
        uint8_t reclen = buf[p];
        if (reclen == 0) {                              /* pad to the next sector; records never span a sector */
            uint32_t next = (p / st->blocksize + 1) * st->blocksize;
            if (next <= p) break;
            p = next;
            continue;
        }
        if (reclen < 33 || p + reclen > length) break;
        if (++st->records > ISO_MAX_ENTRIES) break;

        uint32_t ext_lba = fa_le32(buf + p + 2);
        uint32_t datalen = fa_le32(buf + p + 10);
        uint8_t flags = buf[p + 25];
        uint8_t lenfi = buf[p + 32];
        if (33u + lenfi > reclen) { p += reclen; continue; }
        const uint8_t *fi = buf + p + 33;

        bool special = lenfi == 1 && (fi[0] == 0 || fi[0] == 1);   /* "." (0x00) and ".." (0x01) */
        if (special) { p += reclen; continue; }

        /* Rock Ridge System-Use area: begins after the name and its pad byte, past the SUSP skip. */
        rr_info_t rr = {0};
        if (mode == ISO_RR) {
            uint32_t su = 33u + lenfi + ((lenfi & 1) ? 0u : 1u) + skip;
            if (su < reclen) {
                int ce_budget = ISO_MAX_CE; bool sl_sep = false;
                rr_scan(arc, st, buf, p + su, p + reclen, &rr, &ce_budget, &sl_sep);
            }
            /* RE marks the relocated copy of a deep directory, living under RR_MOVED; the logical tree reaches it
             * through the CL placeholder at its real parent, so skip it here (and don't list RR_MOVED's artifacts). */
            if (rr.relocated) { p += reclen; continue; }
        }

        bool is_dir = (flags & 0x02) != 0;

        /* Resolve the display name for this record. */
        char nmbuf[ISO_MAX_NAME + 1];
        const char *nm = nmbuf; size_t nl;
        char *jol = NULL;
        if (mode == ISO_RR && rr.have_name && rr.namelen) {
            nl = rr.namelen; memcpy(nmbuf, rr.name, nl); nmbuf[nl] = 0;
        } else if (mode == ISO_JOLIET) {
            if (lenfi & 1) { p += reclen; continue; }               /* UCS-2 names are an even byte count */
            jol = iso_joliet_name(fi, lenfi / 2);
            if (!jol || !jol[0]) { fa_free(jol); p += reclen; continue; }
            nm = jol; nl = strlen(jol);
        } else {
            nl = lenfi;                                             /* lenfi <= 255 < sizeof nmbuf */
            memcpy(nmbuf, fi, nl); nmbuf[nl] = 0;
            if (!is_dir) iso_clean_name(nmbuf, &nl);
        }

        char path[1024];
        size_t pl = strlen(prefix);
        if (pl + 1 + nl + 1 > sizeof path) { fa_free(jol); p += reclen; continue; }   /* path too long */
        size_t k = pl;
        memcpy(path, prefix, pl);
        if (pl) path[k++] = '/';
        memcpy(path + k, nm, nl);
        k += nl;
        path[k] = 0;
        fa_free(jol);

        if (mode == ISO_RR && rr.have_cl) {
            /* CL: a (file-flagged) placeholder for a relocated deep directory. Its own data length is 0, so read
             * the real directory's "." record at cl_lba for the length, then walk it under this path. CL takes
             * precedence over the record's own directory bit (the placeholder should be a file, but if a mastering
             * tool set the dir bit we still follow CL, not the empty placeholder extent). */
            uint32_t clen = iso_dir_length(src, st, rr.cl_lba);
            if (clen && (e = iso_walk(arc, st, rr.cl_lba, clen, depth + 1, path, mode, skip))) break;
        } else if (is_dir) {
            if ((e = iso_walk(arc, st, ext_lba, datalen, depth + 1, path, mode, skip))) break;
        } else if (mode == ISO_RR && rr.is_link) {
            char *safe = fa_malloc(strlen(path) + 1);
            if (!safe) { e = FULTA_ARC_ERR_NOMEM; break; }
            strcpy(safe, path);
            if (!fa_sanitize_name(safe)) { fa_free(safe); safe = NULL; }
            const char *use = safe && safe[0] ? safe : "data";
            fa_entry_t *en = fa_add_entry(arc, (const uint8_t *)path, strlen(path), use, "store");
            iso_item_t *it = iso_add(st);
            fa_free(safe);
            if (!en || !it) { e = FULTA_ARC_ERR_NOMEM; break; }
            uint8_t *tgt = fa_malloc(rr.tlen ? rr.tlen : 1);
            if (!tgt) { e = FULTA_ARC_ERR_NOMEM; break; }
            memcpy(tgt, rr.target, rr.tlen);
            en->pub.flags |= FULTA_ARC_ENTRY_SYMLINK;              /* data is the link target (as tar/cpio do) */
            en->pub.size = rr.tlen; en->pub.packed_size = rr.tlen;
            it->off = 0; it->size = 0; it->link = tgt; it->linklen = rr.tlen;
        } else {
            uint64_t foff = (uint64_t)ext_lba * st->blocksize;
            if (foff <= src->size && datalen <= src->size - foff) {
                /* zisofs: a transparently-compressed file begins with the zisofs magic; the directory extent is
                 * the COMPRESSED size, the real size is in the header. Peek it so the listing shows the true size. */
                bool zf = false; uint8_t zblog = 0; uint64_t zusz = 0;
                if (datalen >= ISO_ZF_HDR) {
                    uint8_t zh[ISO_ZF_HDR];
                    if (fa_source_read_exact(src, foff, zh, ISO_ZF_HDR) == 0 &&
                        memcmp(zh, ISO_ZF_MAGIC, 8) == 0 && zh[12] == 4 /* 16-byte header */ &&
                        zh[13] >= 15 && zh[13] <= 20) {
                        zf = true; zblog = zh[13]; zusz = fa_le32(zh + 8);
                    }
                }
                char *safe = fa_malloc(strlen(path) + 1);
                if (!safe) { e = FULTA_ARC_ERR_NOMEM; break; }
                strcpy(safe, path);
                if (!fa_sanitize_name(safe)) { fa_free(safe); safe = NULL; }
                const char *use = safe && safe[0] ? safe : "data";
                fa_entry_t *en = fa_add_entry(arc, (const uint8_t *)path, strlen(path), use, zf ? "zisofs" : "store");
                iso_item_t *it = iso_add(st);
                fa_free(safe);
                if (!en || !it) { e = FULTA_ARC_ERR_NOMEM; break; }
                en->pub.size = zf ? zusz : datalen;
                en->pub.packed_size = datalen;
                it->off = foff;
                it->size = datalen;
                it->zisofs = zf; it->block_log2 = zblog; it->usize = zusz;
            }
        }
        p += reclen;
    }
    fa_free(buf);
    return e;
}

/* Read the root "." record's System-Use area and look for the SUSP SP indicator (0xBE 0xEF); return its LEN_SKP in
 * *skip and true if Rock Ridge is in use. Parsing SU bytes only when SP is present avoids false positives. */
static bool iso_has_susp(fulta_arc_t *arc, iso_state_t *st, uint32_t root_lba, uint32_t root_len, unsigned *skip) {
    const fulta_arc_source_t *src = arc->volumes[0];
    uint64_t base = (uint64_t)root_lba * st->blocksize;
    uint32_t len = root_len < st->blocksize ? root_len : st->blocksize;
    if (len < 34 || base >= src->size || len > src->size - base) return false;
    uint8_t rec[2048];
    if (len > sizeof rec) len = sizeof rec;
    if (fa_source_read_exact(src, base, rec, len)) return false;
    uint8_t reclen = rec[0], lenfi = rec[32];
    if (reclen < 34 || reclen > len) return false;
    uint32_t su = 33u + lenfi + ((lenfi & 1) ? 0u : 1u);
    while (su + 4 <= reclen) {
        uint8_t l = rec[su + 2];
        if (l < 4 || su + l > reclen) break;
        if (rec[su] == 'S' && rec[su + 1] == 'P' && l >= 7 && rec[su + 4] == 0xBE && rec[su + 5] == 0xEF) {
            *skip = rec[su + 6];
            return true;
        }
        su += l;
    }
    return false;
}

static fulta_arc_err_t iso_open(fulta_arc_t *arc, uint64_t offset) {
    (void)offset;
    const fulta_arc_source_t *src = arc->volumes[0];
    if (src->size < ISO_SYS_AREA + 2048) return FULTA_ARC_ERR_NOT_ARCHIVE;
    uint8_t vd[2048];
    if (fa_source_read_exact(src, ISO_SYS_AREA, vd, 2048)) return FULTA_ARC_ERR_NOT_ARCHIVE;
    if (vd[0] != 1 || memcmp(vd + 1, "CD001", 5) != 0) return FULTA_ARC_ERR_NOT_ARCHIVE;   /* Primary Volume Descriptor */

    uint32_t bs = (uint32_t)(vd[128] | (vd[129] << 8));
    if (bs < 512 || bs > 65536 || (bs & (bs - 1))) return FULTA_ARC_ERR_NOT_ARCHIVE;       /* power of two, 512..64K */

    iso_state_t *st = fa_calloc(1, sizeof *st);
    if (!st) return FULTA_ARC_ERR_NOMEM;
    st->blocksize = bs;
    arc->state = st;

    const uint8_t *root = vd + 156;                 /* the root directory record */
    uint32_t root_lba = fa_le32(root + 2);
    uint32_t root_len = fa_le32(root + 10);

    /* Scan the volume descriptors for a Joliet supplementary descriptor (same block size). */
    uint32_t jol_lba = 0, jol_len = 0;
    for (uint32_t s = 17; s < 16 + ISO_MAX_VDS; s++) {
        uint64_t vo = (uint64_t)s * 2048;
        if (vo + 2048 > src->size) break;
        uint8_t v[2048];
        if (fa_source_read_exact(src, vo, v, 2048)) break;
        if (memcmp(v + 1, "CD001", 5) != 0) break;
        if (v[0] == 255) break;                     /* volume-descriptor set terminator */
        if (v[0] == 2) {                            /* supplementary: Joliet if the escape is %/@, %/C or %/E */
            const uint8_t *esc = v + 88;
            bool joliet = esc[0] == '%' && esc[1] == '/' && (esc[2] == '@' || esc[2] == 'C' || esc[2] == 'E');
            uint32_t vbs = (uint32_t)(v[128] | (v[129] << 8));
            if (joliet && vbs == bs) { jol_lba = fa_le32(v + 156 + 2); jol_len = fa_le32(v + 156 + 10); }
        }
    }

    /* Prefer Rock Ridge, then Joliet, then plain. */
    unsigned skip = 0;
    int mode; uint32_t wlba, wlen;
    if (iso_has_susp(arc, st, root_lba, root_len, &skip)) { mode = ISO_RR; wlba = root_lba; wlen = root_len; }
    else if (jol_lba) { mode = ISO_JOLIET; wlba = jol_lba; wlen = jol_len; }
    else { mode = ISO_PLAIN; wlba = root_lba; wlen = root_len; }

    fulta_arc_err_t e = iso_walk(arc, st, wlba, wlen, 0, "", mode, skip);
    if (e) { iso_close(arc); return e; }
    if (arc->count == 0) { iso_close(arc); return FULTA_ARC_ERR_NOT_ARCHIVE; }   /* root out of range / no files */
    return FULTA_ARC_OK;
}

/* Decode a zisofs file: the 16-byte header at it->off is followed by a (nblocks+1) LE32 pointer table, then the
 * per-block zlib streams. A pointer is a byte offset from the file start; block i is [ptr[i], ptr[i+1]); a
 * zero-length block is a run of zero bytes. Each non-empty block is one zlib stream (2-byte header + Deflate +
 * Adler-32) that expands to the block size (the last block to the remainder). */
static fulta_arc_err_t iso_zisofs_extract(fulta_arc_t *arc, iso_item_t *it, const fulta_arc_sink_t *sink) {
    const fulta_arc_source_t *src = arc->volumes[0];
    if (it->usize > ISO_ZF_MAX) return FULTA_ARC_ERR_LIMIT;
    if (it->usize == 0) return FULTA_ARC_OK;
    uint32_t bsize = 1u << it->block_log2;
    uint64_t nblocks = (it->usize + bsize - 1) / bsize;
    uint64_t tbl_bytes = (nblocks + 1) * 4;
    if (it->size < ISO_ZF_HDR || tbl_bytes > it->size - ISO_ZF_HDR) return FULTA_ARC_ERR_CORRUPT;   /* table in extent */

    uint32_t *tbl = fa_malloc((size_t)tbl_bytes);
    if (!tbl) return FULTA_ARC_ERR_NOMEM;
    if (fa_source_read_exact(src, it->off + ISO_ZF_HDR, (uint8_t *)tbl, (size_t)tbl_bytes)) { fa_free(tbl); return FULTA_ARC_ERR_TRUNCATED; }

    uint8_t *obuf = fa_malloc(bsize), *cbuf = NULL;
    size_t ccap = 0;
    fulta_arc_err_t e = obuf ? FULTA_ARC_OK : FULTA_ARC_ERR_NOMEM;
    uint64_t produced = 0;
    for (uint64_t b = 0; b < nblocks && !e; b++) {
        uint32_t start = fa_le32((uint8_t *)&tbl[b]), end = fa_le32((uint8_t *)&tbl[b + 1]);
        uint32_t want = (uint32_t)(it->usize - produced < bsize ? it->usize - produced : bsize);
        if (end < start || end > it->size) { e = FULTA_ARC_ERR_CORRUPT; break; }
        if (start == end) {                                 /* a hole: `want` zero bytes */
            memset(obuf, 0, want);
            e = sink->write(sink->ctx, obuf, want);
        } else {
            uint32_t clen = end - start;
            if (clen < 2) { e = FULTA_ARC_ERR_CORRUPT; break; }
            if (clen > ccap) { uint8_t *nc = fa_realloc(cbuf, clen); if (!nc) { e = FULTA_ARC_ERR_NOMEM; break; } cbuf = nc; ccap = clen; }
            if (fa_source_read_exact(src, it->off + start, cbuf, clen)) { e = FULTA_ARC_ERR_TRUNCATED; break; }
            fa_stream_t *ms = NULL, *dec = NULL;            /* skip the 2-byte zlib header; Adler trailer is ignored */
            if ((e = fa_stream_memory(cbuf + 2, clen - 2, &ms))) break;
            if ((e = fa_dec_deflate(ms, false, want, &dec))) break;
            e = fa_stream_read_exact(dec, obuf, want);
            fa_stream_destroy(dec);
            if (!e) e = sink->write(sink->ctx, obuf, want);
        }
        produced += want;
    }
    fa_free(tbl); fa_free(obuf); fa_free(cbuf);
    return e;
}

static fulta_arc_err_t iso_extract(fulta_arc_t *arc, size_t index, const fulta_arc_sink_t *sink) {
    iso_state_t *st = arc->state;
    if (index >= st->n) return FULTA_ARC_ERR_INVALID_ARG;
    iso_item_t *it = &st->items[index];
    if (it->link) return it->linklen ? sink->write(sink->ctx, it->link, it->linklen) : FULTA_ARC_OK;   /* RR symlink */
    if (it->zisofs) return iso_zisofs_extract(arc, it, sink);
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

static void iso_close(fulta_arc_t *arc) {
    iso_state_t *st = arc->state;
    if (!st) return;
    for (size_t i = 0; i < st->n; i++) fa_free(st->items[i].link);
    fa_free(st->items);
    fa_free(st->visited);
    fa_free(st);
    arc->state = NULL;
}

static bool iso_probe(const uint8_t *head, size_t n, uint64_t *offset) {
    (void)head; (void)n; (void)offset;
    return false;   /* the "CD001" magic is at offset 32769, past the probe window; iso_open validates it */
}

const fa_format_ops_t fa_iso_ops = {
    .format = FULTA_ARC_FORMAT_ISO,
    .probe = iso_probe,
    .open = iso_open,
    .extract = iso_extract,
    .close = iso_close,
};
