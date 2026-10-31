/*
 * lhadec - archive container reader.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 FultaArc contributors
 *
 * Implements docs/spec/lzh-container.md.  Section numbers are cited at each part.
 */
#include "lha_internal.h"

#include <stdlib.h>
#include <string.h>

#define MAX_HEADER (1u << 20)   /* reader policy: refuse headers longer than 1 MiB */

/* ------------------------------------------------------------------------------------------------
 * CRC-16, reflected polynomial 0xA001, initial value 0 (spec container §5.1).
 */
static uint16_t crc_table[256];
static int crc_ready;

static void crc_init(void)
{
    unsigned i, j;
    for (i = 0; i < 256; i++) {
        unsigned r = i;
        for (j = 0; j < 8; j++) r = (r & 1) ? (r >> 1) ^ 0xA001u : r >> 1;
        crc_table[i] = (uint16_t)r;
    }
    crc_ready = 1;
}

uint16_t lha_crc16(uint16_t crc, const void *data, size_t n)
{
    const unsigned char *p = (const unsigned char *)data;
    if (!crc_ready) crc_init();
    while (n--) crc = (uint16_t)(crc_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8));
    return crc;
}

/* ------------------------------------------------------------------------------------------------
 * Input layer.
 */
size_t lha_in_need(lha_in *in, size_t n)
{
    size_t avail = in->len - in->pos;
    if (avail >= n || in->eof || in->err) return avail < n ? avail : n;
    if (in->pos > 0) {                       /* compact */
        memmove(in->buf, in->buf + in->pos, avail);
        in->len = avail;
        in->pos = 0;
    }
    if (n > in->cap) {
        size_t nc = in->cap;
        unsigned char *nb;
        while (nc < n) nc *= 2;
        nb = (unsigned char *)realloc(in->buf, nc);
        if (!nb) { in->err = LHA_ERR_NOMEM; return avail; }
        in->buf = nb;
        in->cap = nc;
    }
    while (in->len < n && !in->eof) {
        size_t got = in->rd(in->ctx, in->buf + in->len, in->cap - in->len);
        if (got == (size_t)-1) { in->err = LHA_ERR_IO; break; }
        if (got == 0) { in->eof = 1; break; }
        in->len += got;
    }
    avail = in->len - in->pos;
    return avail < n ? avail : n;
}

static void in_consume(lha_in *in, size_t n)
{
    in->pos += n;
    in->offset += n;
}

/* skip n bytes (may exceed the buffer) */
static int in_skip(lha_in *in, uint64_t n)
{
    while (n) {
        size_t avail = lha_in_need(in, 1);
        size_t take;
        if (in->err) return in->err;
        if (avail == 0) return LHA_ERR_TRUNCATED;
        avail = in->len - in->pos;
        take = n < avail ? (size_t)n : avail;
        in_consume(in, take);
        n -= take;
    }
    return LHA_OK;
}

int lha_src_getc(lha_src *s)
{
    int c;
    if (s->remaining == 0) return LHA_SRC_END;
    if (s->in->pos >= s->in->len) {
        lha_in_need(s->in, 1);
        if (s->in->err) return LHA_SRC_ERROR;
        if (s->in->pos >= s->in->len) return LHA_SRC_END;
    }
    c = s->in->buf[s->in->pos];
    in_consume(s->in, 1);
    s->remaining--;
    return c;
}

size_t lha_src_read(lha_src *s, unsigned char *dst, size_t n)
{
    size_t done = 0;
    if (n > s->remaining) n = (size_t)s->remaining;
    while (done < n) {
        size_t avail, take;
        if (s->in->pos >= s->in->len) {
            lha_in_need(s->in, 1);
            if (s->in->err) return (size_t)-1;
            if (s->in->pos >= s->in->len) break;
        }
        avail = s->in->len - s->in->pos;
        take = n - done < avail ? n - done : avail;
        memcpy(dst + done, s->in->buf + s->in->pos, take);
        in_consume(s->in, take);
        done += take;
    }
    s->remaining -= done;
    return done;
}

int lha_sink_write(lha_sink *k, const unsigned char *p, size_t n)
{
    if (n == 0) return LHA_OK;
    k->crc = lha_crc16(k->crc, p, n);
    k->count += n;
    if (k->wr && k->wr(k->ctx, p, n) != 0) return LHA_ERR_IO;
    return LHA_OK;
}

/* ------------------------------------------------------------------------------------------------
 * Reader.
 */
struct lha_reader {
    lha_in in;
    unsigned char *hdr;          /* header bytes of the current entry (entry start .. data start) */
    size_t hdr_len;
    unsigned char *names;        /* path, name, link storage */
    size_t names_cap;
    lha_entry cur;
    uint64_t data_left;          /* packed bytes of the current entry not yet consumed */
    int have_entry;
    int done;
    int scan_mode;               /* opened with a scan limit (self-extractor policy, spec §6) */
};

static uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const unsigned char *p) { return (uint32_t)rd16(p) | (uint32_t)rd16(p + 2) << 16; }
static uint64_t rd64(const unsigned char *p) { return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32; }

static const struct { const char *id; lha_method m; } method_ids[] = {
    {"-lh0-", LHA_M_LH0}, {"-lhd-", LHA_M_LHD}, {"-lh1-", LHA_M_LH1}, {"-lh2-", LHA_M_LH2},
    {"-lh3-", LHA_M_LH3}, {"-lh4-", LHA_M_LH4}, {"-lh5-", LHA_M_LH5}, {"-lh6-", LHA_M_LH6},
    {"-lh7-", LHA_M_LH7}, {"-lhx-", LHA_M_LHX}, {"-lk7-", LHA_M_LK7}, {"-lz4-", LHA_M_LZ4},
    {"-lz5-", LHA_M_LZ5}, {"-lzs-", LHA_M_LZS}, {"-pm0-", LHA_M_PM0}, {"-pm1-", LHA_M_PM1},
    {"-pm2-", LHA_M_PM2}
};

static lha_method method_of(const unsigned char *id)
{
    size_t i;
    for (i = 0; i < sizeof method_ids / sizeof method_ids[0]; i++)
        if (memcmp(id, method_ids[i].id, 5) == 0) return method_ids[i].m;
    return LHA_M_UNKNOWN;
}

const char *lha_method_name(lha_method m)
{
    size_t i;
    for (i = 0; i < sizeof method_ids / sizeof method_ids[0]; i++)
        if (method_ids[i].m == m) return method_ids[i].id;
    return "?";
}

/* Plausibility of an entry start at buffer offset p (used for the SFX scan and the end marker). */
static int looks_like_header(const unsigned char *b, size_t avail)
{
    if (avail < 22) return 0;
    if (b[2] != '-' || b[6] != '-') return 0;
    if (!((b[3] == 'l' && (b[4] == 'h' || b[4] == 'z' || b[4] == 'k')) || (b[3] == 'p' && b[4] == 'm')))
        return 0;
    if (b[20] > 3) return 0;
    if (b[20] <= 1) {                      /* sum check (spec container §3) */
        unsigned s = 0, i, S = b[0];
        if (S < 22 || (size_t)S + 2 > avail) return 0;
        for (i = 0; i < S; i++) s += b[2 + i];
        return (s & 0xFF) == b[1];
    }
    return 1;
}

static int ensure_names(lha_reader *r, size_t n)
{
    if (n <= r->names_cap) return LHA_OK;
    {
        unsigned char *p = (unsigned char *)realloc(r->names, n);
        if (!p) return LHA_ERR_NOMEM;
        r->names = p;
        r->names_cap = n;
    }
    return LHA_OK;
}

int lha_open(lha_reader **out, lha_read_fn rd, void *ctx, size_t scan_limit)
{
    lha_reader *r;
    *out = NULL;
    r = (lha_reader *)calloc(1, sizeof *r);
    if (!r) return LHA_ERR_NOMEM;
    r->in.rd = rd;
    r->in.ctx = ctx;
    r->in.cap = 65536;
    r->in.buf = (unsigned char *)malloc(r->in.cap);
    if (!r->in.buf) { free(r); return LHA_ERR_NOMEM; }
    if (scan_limit) {
        r->scan_mode = 1;
        /* spec container §6: find the first offset where an entry parses */
        size_t avail = lha_in_need(&r->in, scan_limit + 4096), i;
        int found = -1;
        if (r->in.err) { int e = r->in.err; lha_close(r); return e; }
        for (i = 0; i + 22 <= avail && i <= scan_limit; i++) {
            if (looks_like_header(r->in.buf + r->in.pos + i, avail - i)) { found = (int)i; break; }
        }
        if (found < 0) {
            if (!(avail >= 1 && r->in.buf[r->in.pos] == 0)) { lha_close(r); return LHA_ERR_NOT_FOUND; }
        } else {
            in_consume(&r->in, (size_t)found);
        }
    }
    *out = r;
    return LHA_OK;
}

void lha_close(lha_reader *r)
{
    if (!r) return;
    free(r->in.buf);
    free(r->hdr);
    free(r->names);
    free(r);
}

/* Copy the header bytes [0, n) out of the input buffer (they must be available). */
static int keep_header(lha_reader *r, size_t n)
{
    unsigned char *p = (unsigned char *)realloc(r->hdr, n ? n : 1);
    if (!p) return LHA_ERR_NOMEM;
    r->hdr = p;
    memcpy(r->hdr, r->in.buf + r->in.pos, n);
    r->hdr_len = n;
    return LHA_OK;
}

typedef struct {
    const unsigned char *name; size_t name_len; int name_set;
    const unsigned char *path; size_t path_len; int path_set;  /* raw, 0xFF or '/' separated */
    int path_ff;
} name_parts;

/* Apply one extended-header unit (spec container §4.2). */
static int apply_unit(lha_entry *e, name_parts *np, unsigned type, const unsigned char *pl, size_t n,
                      size_t crc_field_off, size_t *crc_off)
{
    switch (type) {
    case 0x00:
        if (n >= 2) { *crc_off = crc_field_off; e->flags |= LHA_HAS_HEADER_CRC; }
        break;
    case 0x01: np->name = pl; np->name_len = n; np->name_set = 1; break;
    case 0x02: np->path = pl; np->path_len = n; np->path_set = 1; np->path_ff = 1; break;
    case 0x41:
        if (n >= 24) {
            e->win_ctime = rd64(pl); e->win_mtime = rd64(pl + 8); e->win_atime = rd64(pl + 16);
            e->flags |= LHA_HAS_WIN_TIMES;
        }
        break;
    case 0x42:
        if (n >= 16) { e->packed_size = rd64(pl); e->original_size = rd64(pl + 8); }
        break;
    case 0x50:
        if (n >= 2) {
            if (e->os_type == 'K') { e->os9_perm = rd16(pl); e->flags |= LHA_HAS_OS9_PERM; }
            else { e->unix_perm = rd16(pl); e->flags |= LHA_HAS_UNIX_PERM; }
        }
        break;
    case 0x51:
        if (n >= 4) { e->unix_gid = rd16(pl); e->unix_uid = rd16(pl + 2); e->flags |= LHA_HAS_UNIX_IDS; }
        break;
    case 0x54:
        if (n >= 4) { e->unix_time = rd32(pl); e->flags |= LHA_HAS_UNIX_TIME; }
        break;
    case 0xCC:
        if (n >= 8 && e->os_type == '9') { e->os9_perm = pl[7]; e->flags |= LHA_HAS_OS9_PERM; }
        break;
    default:
        break;                              /* no tested meaning: ignored (kept in the raw header) */
    }
    return LHA_OK;
}

/* Walk an extended-header chain inside r->hdr starting at `p` with first size `first`.
 * Returns the chain end offset in *end. Size fields are w bytes wide (spec container §4.1). */
static int walk_chain(lha_reader *r, size_t p, uint32_t first, int w, size_t *end,
                      lha_entry *e, name_parts *np, size_t *crc_off, int read_more)
{
    uint32_t n = first;
    while (n) {
        if (n < (uint32_t)(1 + w)) return LHA_ERR_FORMAT;
        if (p + n > MAX_HEADER) return LHA_ERR_FORMAT;
        if (read_more) {
            if (lha_in_need(&r->in, p + n) < p + n) return r->in.err ? r->in.err : LHA_ERR_TRUNCATED;
        } else if (p + n > r->hdr_len) {
            return LHA_ERR_FORMAT;
        }
        p += n;
        n = w == 2 ? rd16(r->in.buf + r->in.pos + p - 2) : rd32(r->in.buf + r->in.pos + p - 4);
    }
    *end = p;
    (void)e; (void)np; (void)crc_off;
    return LHA_OK;
}

static void apply_chain(lha_reader *r, size_t p, uint32_t first, int w, lha_entry *e, name_parts *np,
                        size_t *crc_off)
{
    uint32_t n = first;
    while (n) {
        const unsigned char *u = r->hdr + p;
        apply_unit(e, np, u[0], u + 1, n - 1 - (uint32_t)w, p + 1, crc_off);
        p += n;
        n = w == 2 ? rd16(r->hdr + p - 2) : rd32(r->hdr + p - 4);
    }
}

static int finish_names(lha_reader *r, lha_entry *e, name_parts *np)
{
    /* Build path (with '/'), name, and the symlink split (spec container §3.3, §4.2, §4.5). */
    size_t total = np->path_len + np->name_len + 2, i, plen = 0, nlen;
    unsigned char *buf, *full;
    int rc = ensure_names(r, total * 2 + 2);
    if (rc != LHA_OK) return rc;
    buf = r->names;
    for (i = 0; i < np->path_len; i++) {
        unsigned char c = np->path[i];
        buf[plen++] = (c == 0xFF || c == '\\') ? '/' : c;
    }
    if (plen && buf[plen - 1] != '/') buf[plen++] = '/';
    memcpy(buf + plen, np->name, np->name_len);
    nlen = np->name_len;
    full = buf;
    {
        size_t flen = plen + nlen, bar = (size_t)-1, slash = (size_t)-1;
        for (i = 0; i < flen; i++) if (full[i] == '|') { bar = i; break; }
        if (bar != (size_t)-1) {
            unsigned char *link = buf + total;
            memcpy(link, full + bar + 1, flen - bar - 1);
            e->link = link;
            e->link_len = flen - bar - 1;
            e->flags |= LHA_HAS_SYMLINK;
            flen = bar;
        }
        for (i = 0; i < flen; i++) if (full[i] == '/') slash = i;
        if (slash == (size_t)-1) { e->path = buf; e->path_len = 0; e->name = full; e->name_len = flen; }
        else { e->path = full; e->path_len = slash + 1; e->name = full + slash + 1; e->name_len = flen - slash - 1; }
    }
    return LHA_OK;
}

int lha_next(lha_reader *r, lha_entry *e)
{
    unsigned char *b;
    size_t avail, hdr_total, data_start, crc_off = 0;
    lha_entry ent;
    name_parts np;
    int rc, level;

    if (r->done) return LHA_END;
    if (r->have_entry && r->data_left) {
        rc = in_skip(&r->in, r->data_left);
        r->data_left = 0;
        if (rc != LHA_OK) { r->done = 1; return rc; }
    }
    r->have_entry = 0;
    memset(&ent, 0, sizeof ent);
    memset(&np, 0, sizeof np);
    ent.header_offset = r->in.offset;

    avail = lha_in_need(&r->in, 24);
    if (r->in.err) return r->in.err;
    if (avail == 0) { r->done = 1; return LHA_END; }          /* spec container §1.2 */
    b = r->in.buf + r->in.pos;
    if (b[0] == 0 && !(avail >= 22 && (b[20] == 2 || b[20] == 3) && looks_like_header(b, avail))) {
        /* end marker (§1.1).  Self-extractor policy (§6): a stub may hold a small archive of its own
         * followed by the real one; continue if a valid entry starts within 16 bytes after the marker. */
        if (r->scan_mode) {
            size_t k, av2 = lha_in_need(&r->in, 16 + 4096);
            b = r->in.buf + r->in.pos;
            for (k = 1; k <= 16 && k + 22 <= av2; k++) {
                if (looks_like_header(b + k, av2 - k)) {
                    in_consume(&r->in, k);
                    return lha_next(r, e);
                }
            }
        }
        r->done = 1;
        return LHA_END;
    }
    if (avail < 22) { r->done = 1; return LHA_ERR_TRUNCATED; }
    level = b[20];
    memcpy(ent.method_id, b + 2, 5);
    ent.method_id[5] = 0;
    ent.method = method_of(b + 2);
    ent.level = level;
    ent.packed_size = rd32(b + 7);
    ent.original_size = rd32(b + 11);

    if (level == 0 || level == 1) {                            /* spec container §3 */
        unsigned S = b[0], n, sum = 0, i;
        size_t hl = (size_t)S + 2;
        if (lha_in_need(&r->in, hl) < hl) { r->done = 1; return r->in.err ? r->in.err : LHA_ERR_TRUNCATED; }
        b = r->in.buf + r->in.pos;
        for (i = 0; i < S; i++) sum += b[2 + i];
        if ((sum & 0xFF) != b[1]) { r->done = 1; return LHA_ERR_HEADER_SUM; }
        n = b[21];
        if ((size_t)22 + n + 2 > hl) { r->done = 1; return LHA_ERR_FORMAT; }
        ent.crc = rd16(b + 22 + n);
        ent.dos_time = rd32(b + 15);
        ent.flags |= LHA_HAS_DOS_TIME;
        {   /* base name: ends at NUL; '\' and '/' separate (§3.3) */
            size_t k = 0;
            while (k < n && b[22 + k] != 0) k++;
            np.name = NULL;
            np.name_len = k;
        }
        if (level == 0) {
            size_t xs = 24 + n;
            hdr_total = hl;
            rc = keep_header(r, hdr_total);
            if (rc != LHA_OK) { r->done = 1; return rc; }
            if (xs < hl) {                                     /* extra area (§3.1) */
                const unsigned char *x = r->hdr + xs;
                size_t xl = hl - xs;
                if (memcmp(ent.method_id, "-pm", 3) != 0) ent.os_type = x[0];
                if (xl >= 12 && x[0] == 'U' && x[1] == 0) {
                    ent.unix_time = rd32(x + 2); ent.unix_perm = rd16(x + 6);
                    ent.unix_uid = rd16(x + 8); ent.unix_gid = rd16(x + 10);
                    ent.flags |= LHA_HAS_UNIX_TIME | LHA_HAS_UNIX_PERM | LHA_HAS_UNIX_IDS;
                } else if (xl >= 16 && x[0] == 'K' && x[1] == 0) {
                    ent.unix_time = rd32(x + 2); ent.os9_perm = rd16(x + 10);
                    ent.unix_uid = rd16(x + 12); ent.unix_gid = rd16(x + 14);
                    ent.flags |= LHA_HAS_UNIX_TIME | LHA_HAS_OS9_PERM | LHA_HAS_UNIX_IDS;
                } else if (xl >= 2 && x[0] == '9') {
                    ent.os9_perm = x[1];
                    ent.flags |= LHA_HAS_OS9_PERM;
                }
            }
            data_start = hdr_total;
        } else {
            size_t chain_end;
            uint32_t first;
            if (hl < (size_t)27 + n) { r->done = 1; return LHA_ERR_FORMAT; }   /* os byte + first unit size */
            ent.os_type = b[24 + n];
            first = rd16(b + hl - 2);
            rc = walk_chain(r, hl, first, 2, &chain_end, &ent, &np, &crc_off, 1);
            if (rc != LHA_OK) { r->done = 1; return rc; }
            hdr_total = chain_end;
            rc = keep_header(r, hdr_total);
            if (rc != LHA_OK) { r->done = 1; return rc; }
            if (ent.packed_size < (uint64_t)(chain_end - hl)) { r->done = 1; return LHA_ERR_FORMAT; }
            ent.packed_size -= (uint64_t)(chain_end - hl);     /* §3.2 */
            data_start = hdr_total;
            np.name = r->hdr + 22; /* base name; may be replaced by unit 0x01 */
            apply_chain(r, hl, first, 2, &ent, &np, &crc_off);
        }
        if (!np.name_set) {
            /* split the base name at its last separator */
            const unsigned char *nm = r->hdr + 22;
            size_t k = np.name_len, last = (size_t)-1, i2;
            for (i2 = 0; i2 < k; i2++) if (nm[i2] == '\\' || nm[i2] == '/') last = i2;
            if (last != (size_t)-1) {
                if (!np.path_set) { np.path = nm; np.path_len = last + 1; np.path_set = 1; }
                np.name = nm + last + 1; np.name_len = k - last - 1;
            } else {
                np.name = nm;
            }
        }
    } else if (level == 2 || level == 3) {                    /* spec container §4.3 */
        size_t chain_end, start = level == 2 ? 26 : 32;
        uint32_t T, first;
        int w = level == 2 ? 2 : 4;
        if (lha_in_need(&r->in, start) < start) { r->done = 1; return r->in.err ? r->in.err : LHA_ERR_TRUNCATED; }
        b = r->in.buf + r->in.pos;
        T = level == 2 ? rd16(b) : rd32(b + 24);
        first = level == 2 ? rd16(b + 24) : rd32(b + 28);
        ent.crc = rd16(b + 21);
        ent.os_type = b[23];
        ent.unix_time = rd32(b + 15);
        ent.flags |= LHA_HAS_UNIX_TIME;
        if (T > MAX_HEADER) { r->done = 1; return LHA_ERR_FORMAT; }
        rc = walk_chain(r, start, first, w, &chain_end, &ent, &np, &crc_off, 1);
        if (rc != LHA_OK) { r->done = 1; return rc; }
        data_start = T > chain_end ? T : chain_end;            /* §4.4 */
        if (lha_in_need(&r->in, data_start) < data_start) { r->done = 1; return r->in.err ? r->in.err : LHA_ERR_TRUNCATED; }
        hdr_total = data_start;
        rc = keep_header(r, hdr_total);
        if (rc != LHA_OK) { r->done = 1; return rc; }
        apply_chain(r, start, first, w, &ent, &np, &crc_off);
        if (!np.name_set) { np.name = r->hdr; np.name_len = 0; }
    } else {
        r->done = 1;
        return LHA_ERR_FORMAT;
    }

    if (ent.flags & LHA_HAS_HEADER_CRC) {                      /* spec container §5.2 */
        uint16_t stored = rd16(r->hdr + crc_off), calc;
        r->hdr[crc_off] = 0; r->hdr[crc_off + 1] = 0;
        calc = lha_crc16(0, r->hdr, hdr_total);
        r->hdr[crc_off] = (unsigned char)stored; r->hdr[crc_off + 1] = (unsigned char)(stored >> 8);
        if (calc != stored) { r->done = 1; return LHA_ERR_HEADER_CRC; }
    }
    if (!np.path_set) { np.path = r->hdr; np.path_len = 0; }
    rc = finish_names(r, &ent, &np);
    if (rc != LHA_OK) { r->done = 1; return rc; }

    /* reader policies (spec container §6) */
    if (ent.method == LHA_M_LH7 && ent.os_type == 0x20) ent.method = LHA_M_LK7;
    if (ent.method == LHA_M_LHD || (ent.method == LHA_M_LH0 && ent.name_len == 0 && ent.original_size == 0))
        ent.flags |= LHA_IS_DIR;

    in_consume(&r->in, data_start);
    r->cur = ent;
    *e = ent;
    r->data_left = ent.packed_size;
    r->have_entry = 1;
    return LHA_OK;
}

int lha_skip(lha_reader *r)
{
    int rc;
    if (!r->have_entry) return LHA_ERR_STATE;
    rc = in_skip(&r->in, r->data_left);
    r->data_left = 0;
    return rc;
}

int lha_extract(lha_reader *r, lha_write_fn wr, void *ctx)
{
    lha_src src;
    lha_sink sink;
    int rc;
    if (!r->have_entry) return LHA_ERR_STATE;
    src.in = &r->in;
    src.remaining = r->data_left;
    sink.wr = wr; sink.ctx = ctx; sink.crc = 0; sink.count = 0;
    if (r->cur.flags & LHA_IS_DIR && r->cur.method == LHA_M_LHD) {
        rc = LHA_OK;
    } else {
        rc = lha_decode_method(r->cur.method, &src, &sink, r->cur.original_size);
    }
    r->data_left = src.remaining;                              /* lha_next skips what is left */
    if (rc != LHA_OK) return rc;
    if (sink.count != r->cur.original_size) return LHA_ERR_DATA;
    if (sink.crc != r->cur.crc) return LHA_ERR_CRC;
    return LHA_OK;
}

/* ------------------------------------------------------------------------------------------------ */

typedef struct { const unsigned char *p; size_t n; } memsrc;
typedef struct { unsigned char *p; size_t n, used; } memdst;

static size_t mem_read(void *ctx, void *buf, size_t n)
{
    memsrc *m = (memsrc *)ctx;
    size_t k = n < m->n ? n : m->n;
    memcpy(buf, m->p, k);
    m->p += k;
    m->n -= k;
    return k;
}

static int mem_write(void *ctx, const void *buf, size_t n)
{
    memdst *d = (memdst *)ctx;
    if (n > d->n - d->used) return 1;
    memcpy(d->p + d->used, buf, n);
    d->used += n;
    return 0;
}

int lha_decode_buffer(lha_method m, const unsigned char *in, size_t in_len, unsigned char *out, size_t out_len)
{
    lha_in li;
    lha_src src;
    lha_sink sink;
    memsrc ms;
    memdst md;
    int rc;
    memset(&li, 0, sizeof li);
    ms.p = in; ms.n = in_len;
    md.p = out; md.n = out_len; md.used = 0;
    li.rd = mem_read; li.ctx = &ms; li.cap = 65536;
    li.buf = (unsigned char *)malloc(li.cap);
    if (!li.buf) return LHA_ERR_NOMEM;
    src.in = &li; src.remaining = in_len;
    sink.wr = mem_write; sink.ctx = &md; sink.crc = 0; sink.count = 0;
    rc = lha_decode_method(m, &src, &sink, out_len);
    free(li.buf);
    if (rc == LHA_OK && md.used != out_len) rc = LHA_ERR_DATA;
    return rc;
}

const char *lha_strerror(int code)
{
    switch (code) {
    case LHA_OK: return "ok";
    case LHA_END: return "end of archive";
    case LHA_ERR_IO: return "I/O error";
    case LHA_ERR_FORMAT: return "malformed header";
    case LHA_ERR_HEADER_SUM: return "header checksum mismatch";
    case LHA_ERR_HEADER_CRC: return "header CRC mismatch";
    case LHA_ERR_UNSUPPORTED: return "unsupported compression method";
    case LHA_ERR_DATA: return "malformed compressed data";
    case LHA_ERR_TRUNCATED: return "truncated data";
    case LHA_ERR_CRC: return "data CRC mismatch";
    case LHA_ERR_NOMEM: return "out of memory";
    case LHA_ERR_STATE: return "API used out of order";
    case LHA_ERR_NOT_FOUND: return "no archive header found";
    default: return "unknown error";
    }
}

int lha_dos_time_fields(uint32_t dos, int *year, int *mon, int *day, int *hour, int *min, int *sec)
{
    unsigned t = dos & 0xFFFF, d = dos >> 16;
    int y = (int)(d >> 9) + 1980, mo = (int)(d >> 5 & 15), da = (int)(d & 31);
    int h = (int)(t >> 11), mi = (int)(t >> 5 & 63), s = (int)(t & 31) * 2;
    static const int mdays[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    *year = y; *mon = mo; *day = da; *hour = h; *min = mi; *sec = s;
    if (mo < 1 || mo > 12 || da < 1 || da > mdays[mo - 1] || h > 23 || mi > 59 || s > 59) return 0;
    if (mo == 2 && da == 29 && !((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 0;
    return 1;
}
