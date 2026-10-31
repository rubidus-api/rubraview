/*
 * lhadec - compression method decoders.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 FultaArc contributors
 *
 * Implements docs/spec/lzh-methods.md: section 1 (stored), 2 (static-Huffman family), 3 (-lk7-),
 * 4 (LArc), 5 (-lh1-), 6 (-lh2-), 7 (-lh3-), 8 (-pm2-), 9 (-pm1-).  Section numbers are cited at each
 * part.
 */
#include "lha_internal.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------------------------------
 * Bit input, most significant bit first (spec methods, preamble).  Reading past the end of the
 * packed data is an error (LHA_ERR_TRUNCATED); peeking past it yields zero bits.
 */
typedef struct {
    lha_src *src;
    uint64_t acc;        /* bits are left-aligned at bit 63 */
    int nacc;            /* valid bits in acc */
    int eof_pad;         /* zero bits appended after the source ended */
    int err;
} bitin;

static void bi_fill(bitin *b)
{
    while (b->nacc <= 56) {
        int c = lha_src_getc(b->src);
        if (c < 0) {
            if (c == LHA_SRC_ERROR) b->err = LHA_ERR_IO;
            /* pad with zeros for peeking; consumption beyond real bits is checked in bi_drop */
            b->eof_pad += 8;
            c = 0;
        }
        b->acc |= (uint64_t)(unsigned)c << (56 - b->nacc);
        b->nacc += 8;
    }
}

static unsigned bi_peek(bitin *b, int n) /* n <= 32 */
{
    if (n == 0) return 0;
    if (b->nacc < n) bi_fill(b);
    return (unsigned)(b->acc >> (64 - n));
}

static int bi_drop(bitin *b, int n)
{
    if (b->nacc < n) bi_fill(b);
    b->acc <<= n;
    b->nacc -= n;
    if (b->nacc < b->eof_pad) { /* consumed padding bits: the stream ended early */
        b->err = LHA_ERR_TRUNCATED;
        return -1;
    }
    return 0;
}

static unsigned bi_get(bitin *b, int n)
{
    unsigned v;
    if (n == 0) return 0;
    if (n > 24) {
        unsigned hi = bi_get(b, n - 16);
        return (hi << 16) | bi_get(b, 16);
    }
    v = bi_peek(b, n);
    bi_drop(b, n);
    return v;
}

static void bi_init(bitin *b, lha_src *src)
{
    memset(b, 0, sizeof *b);
    b->src = src;
}

/* ------------------------------------------------------------------------------------------------
 * Output with a history window (power of two) feeding the sink.
 */
typedef struct {
    unsigned char *ring;
    size_t mask;          /* ring size - 1 */
    uint64_t total;       /* bytes produced */
    uint64_t want;        /* bytes to produce */
    size_t flushed;       /* ring index up to which data was handed to the sink */
    lha_sink *sink;
} hist;

static int hist_init(hist *h, size_t size, uint64_t want, lha_sink *sink)
{
    h->ring = (unsigned char *)malloc(size);
    if (!h->ring) return LHA_ERR_NOMEM;
    h->mask = size - 1;
    h->total = 0;
    h->want = want;
    h->flushed = 0;
    h->sink = sink;
    return LHA_OK;
}

static int hist_flush(hist *h)
{
    /* hist_put hands the ring over whenever it fills, so [flushed, end) never wraps past a full ring */
    size_t end = (size_t)(h->total & h->mask);
    size_t start = h->flushed;
    int rc = LHA_OK;
    if (end >= start) {
        if (end > start) rc = lha_sink_write(h->sink, h->ring + start, end - start);
    } else {
        rc = lha_sink_write(h->sink, h->ring + start, h->mask + 1 - start);
        if (rc == LHA_OK && end) rc = lha_sink_write(h->sink, h->ring, end);
    }
    h->flushed = end;
    return rc;
}

static int hist_put(hist *h, unsigned char c)
{
    size_t i = (size_t)(h->total & h->mask);
    h->ring[i] = c;
    h->total++;
    if (((size_t)(h->total & h->mask)) == h->flushed) {
        /* the ring just filled up to the unflushed part: hand everything over */
        int rc = lha_sink_write(h->sink, h->ring + h->flushed, h->mask + 1 - h->flushed);
        if (rc == LHA_OK && h->flushed) rc = lha_sink_write(h->sink, h->ring, h->flushed);
        if (rc != LHA_OK) return rc;
    }
    return LHA_OK;
}

static void hist_free(hist *h)
{
    free(h->ring);
    h->ring = NULL;
}

/* ------------------------------------------------------------------------------------------------
 * Canonical prefix codes (spec methods §2.2).
 */
#define MAXSYM 512

typedef struct {
    int nsym;
    int constant;          /* >= 0: every read returns this symbol, no bits consumed */
    uint16_t count[17];    /* codes per length */
    uint16_t first[17];    /* first code of each length */
    uint16_t offset[17];   /* index into sorted[] of the first symbol of each length */
    uint16_t sorted[MAXSYM];
} pcode;

static int pcode_build(pcode *pc, const unsigned char *len, int nsym)
{
    int l, s, idx;
    uint32_t code = 0, kraft = 0;
    pc->nsym = nsym;
    pc->constant = -1;
    memset(pc->count, 0, sizeof pc->count);
    for (s = 0; s < nsym; s++) {
        if (len[s] > 16) return LHA_ERR_DATA;
        if (len[s]) pc->count[len[s]]++;
    }
    for (l = 1; l <= 16; l++) kraft += (uint32_t)pc->count[l] << (16 - l);
    if (kraft > 65536u) return LHA_ERR_DATA;          /* over-subscribed */
    idx = 0;
    for (l = 1; l <= 16; l++) {
        pc->first[l] = (uint16_t)code;
        pc->offset[l] = (uint16_t)idx;
        idx += pc->count[l];
        code = (code + pc->count[l]) << 1;
    }
    {
        uint16_t fill[17];
        memcpy(fill, pc->offset, sizeof fill);
        for (s = 0; s < nsym; s++)
            if (len[s]) pc->sorted[fill[len[s]]++] = (uint16_t)s;
    }
    return LHA_OK;
}

static int pcode_read(pcode *pc, bitin *b)
{
    unsigned bits, code;
    int l;
    if (pc->constant >= 0) return pc->constant;
    bits = bi_peek(b, 16);
    for (l = 1; l <= 16; l++) {
        code = bits >> (16 - l);
        if (pc->count[l] && code >= pc->first[l] && code - pc->first[l] < pc->count[l]) {
            if (bi_drop(b, l) < 0) return -1;
            return pc->sorted[pc->offset[l] + (code - pc->first[l])];
        }
    }
    b->err = LHA_ERR_DATA;   /* unassigned code (incomplete length set) */
    return -1;
}

/* Length list (spec methods §2.3): count field of nbit bits; zero-run slot after index `slot`. */
static int read_length_list(pcode *pc, bitin *b, int nsym, int nbit, int slot)
{
    unsigned char len[MAXSYM];
    int n, i = 0;
    n = (int)bi_get(b, nbit);
    if (b->err) return b->err;
    if (n == 0) {
        int c = (int)bi_get(b, nbit);
        if (b->err) return b->err;
        if (c >= nsym) return LHA_ERR_DATA;
        pc->nsym = nsym;
        pc->constant = c;
        return LHA_OK;
    }
    if (n > nsym) return LHA_ERR_DATA;
    memset(len, 0, sizeof len);
    while (i < n) {
        int v = (int)bi_get(b, 3);
        if (v == 7) {
            while (bi_get(b, 1)) {
                if (b->err) return b->err;
                if (++v > 16) return LHA_ERR_DATA;
            }
        }
        if (b->err) return b->err;
        len[i++] = (unsigned char)v;
        if (i == slot) {
            int z = (int)bi_get(b, 2);
            if (i + z > nsym) return LHA_ERR_DATA;
            i += z;                       /* zero lengths, already 0 */
        }
    }
    return pcode_build(pc, len, nsym);
}

#define NT 19
#define NC 510
#define CBIT 9

static int read_c_lengths(pcode *c, pcode *t, bitin *b)
{
    unsigned char len[NC];
    int n, i = 0;
    n = (int)bi_get(b, CBIT);
    if (b->err) return b->err;
    if (n == 0) {
        int s = (int)bi_get(b, CBIT);
        if (b->err) return b->err;
        if (s >= NC) return LHA_ERR_DATA;
        c->nsym = NC;
        c->constant = s;
        return LHA_OK;
    }
    if (n > NC) return LHA_ERR_DATA;
    memset(len, 0, sizeof len);
    while (i < n) {
        int s = pcode_read(t, b);
        if (s < 0 || b->err) return b->err ? b->err : LHA_ERR_DATA;
        if (s <= 2) {
            int z = s == 0 ? 1 : (s == 1 ? (int)bi_get(b, 4) + 3 : (int)bi_get(b, CBIT) + 20);
            if (b->err) return b->err;
            if (i + z > NC) return LHA_ERR_DATA;
            i += z;
        } else {
            len[i++] = (unsigned char)(s - 2);
        }
    }
    return pcode_build(c, len, NC);
}

/* ------------------------------------------------------------------------------------------------
 * Static-Huffman family (spec §2) and -lk7- (spec §3).
 */
typedef struct {
    int pbit;            /* position count width */
    int np;              /* position alphabet size: 2^pbit, so any single-symbol value is legal;
                            distances are bounded by `window` */
    uint32_t window;     /* largest distance accepted (spec §2.1 / §3) */
    int lk7;             /* spec §3 variant */
} huf_params;

static int lk7_length(int c, bitin *b)
{
    int i = c - 256, g, r;
    if (i < 8) return i + 3;
    g = (i - 8) / 4 + 1;
    r = (i - 8) % 4;
    if (g > 5) return -1;                      /* beyond the flagged extrapolation (spec §3) */
    return 3 + (1 << (g + 2)) + r * (1 << g) + (int)bi_get(b, g);
}

static int64_t lk7_distance(int s, bitin *b)
{
    int k;
    if (s < 4) return s + 1;
    if (s > 31) return -1;
    k = (s - 2) / 2;
    return 1 + ((int64_t)(2 + (s & 1)) << k) + (int64_t)bi_get(b, k);
}

static int decode_huf(lha_src *src, lha_sink *sink, uint64_t osize, const huf_params *p)
{
    bitin b;
    hist h;
    pcode *t, *c, *pp;
    size_t ring = 1;
    unsigned left = 0;
    int rc;

    while (ring < p->window) ring <<= 1;
    rc = hist_init(&h, ring, osize, sink);
    if (rc != LHA_OK) return rc;
    t = (pcode *)malloc(sizeof *t);
    c = (pcode *)malloc(sizeof *c);
    pp = (pcode *)malloc(sizeof *pp);
    if (!t || !c || !pp) { rc = LHA_ERR_NOMEM; goto done; }
    bi_init(&b, src);

    while (h.total < osize) {
        int sym;
        if (left == 0) {
            left = bi_get(&b, 16);
            if (b.err) { rc = b.err; goto done; }
            if (left == 0) { rc = LHA_ERR_DATA; goto done; }     /* spec §2.3 item 1 */
            rc = read_length_list(t, &b, NT, 5, 3);
            if (rc == LHA_OK) rc = read_c_lengths(c, t, &b);
            if (rc == LHA_OK) rc = read_length_list(pp, &b, p->np, p->pbit, -1);
            if (rc != LHA_OK) goto done;
        }
        left--;
        sym = pcode_read(c, &b);
        if (sym < 0 || b.err) { rc = b.err ? b.err : LHA_ERR_DATA; goto done; }
        if (sym < 256) {
            rc = hist_put(&h, (unsigned char)sym);
            if (rc != LHA_OK) goto done;
        } else {
            int len, j;
            int64_t dist;
            if (p->lk7) {
                len = lk7_length(sym, &b);
                if (len < 0) { rc = LHA_ERR_DATA; goto done; }
                j = pcode_read(pp, &b);
                if (j < 0) { rc = b.err ? b.err : LHA_ERR_DATA; goto done; }
                dist = lk7_distance(j, &b);
                if (dist < 0) { rc = LHA_ERR_DATA; goto done; }
            } else {
                len = sym - 253;
                j = pcode_read(pp, &b);
                if (j < 0) { rc = b.err ? b.err : LHA_ERR_DATA; goto done; }
                dist = j == 0 ? 1 : (int64_t)((1u << (j - 1)) + bi_get(&b, j - 1)) + 1;
            }
            if (b.err) { rc = b.err; goto done; }
            if ((uint64_t)dist > h.total || (uint64_t)dist > p->window) { rc = LHA_ERR_DATA; goto done; }
            while (len-- > 0 && h.total < osize) {
                unsigned char v = h.ring[(size_t)((h.total - (uint64_t)dist) & h.mask)];
                rc = hist_put(&h, v);
                if (rc != LHA_OK) goto done;
            }
        }
    }
    rc = hist_flush(&h);
done:
    free(t); free(c); free(pp);
    hist_free(&h);
    return rc;
}

/* ------------------------------------------------------------------------------------------------
 * LArc (spec §4).
 */
static void lz5_initial_ring(unsigned char *r)
{
    int v, k, i = 0;
    for (v = 0; v < 256; v++)
        for (k = 0; k < 13; k++) r[i++] = (unsigned char)v;   /* 0x000-0xCFF */
    for (v = 0; v < 256; v++) r[i++] = (unsigned char)v;      /* 0xD00-0xDFF */
    for (v = 255; v >= 0; v--) r[i++] = (unsigned char)v;     /* 0xE00-0xEFF */
    for (k = 0; k < 128; k++) r[i++] = 0;                     /* 0xF00-0xF7F */
    for (k = 0; k < 110; k++) r[i++] = 0x20;                  /* 0xF80-0xFED */
    for (k = 0; k < 18; k++) r[i++] = 0;                      /* 0xFEE-0xFFF */
}

static int decode_lz5(lha_src *src, lha_sink *sink, uint64_t osize)
{
    unsigned char ring[4096];
    unsigned char buf[4096];
    size_t nbuf = 0;
    uint64_t out = 0;
    int rc = LHA_OK;
    lz5_initial_ring(ring);
    while (out < osize) {
        int bit, flags = lha_src_getc(src);
        if (flags < 0) return flags == LHA_SRC_ERROR ? LHA_ERR_IO : LHA_ERR_TRUNCATED;
        for (bit = 0; bit < 8 && out < osize; bit++) {
            if (flags >> bit & 1) {
                int v = lha_src_getc(src);
                if (v < 0) return v == LHA_SRC_ERROR ? LHA_ERR_IO : LHA_ERR_TRUNCATED;
                ring[(out + 4078) & 4095] = (unsigned char)v;
                buf[nbuf++] = (unsigned char)v;
                out++;
            } else {
                int b0 = lha_src_getc(src), b1, k, len;
                unsigned pos;
                if (b0 < 0) return b0 == LHA_SRC_ERROR ? LHA_ERR_IO : LHA_ERR_TRUNCATED;
                b1 = lha_src_getc(src);
                if (b1 < 0) return b1 == LHA_SRC_ERROR ? LHA_ERR_IO : LHA_ERR_TRUNCATED;
                pos = (unsigned)b0 | ((unsigned)(b1 & 0xF0) << 4);
                len = (b1 & 0x0F) + 3;
                for (k = 0; k < len && out < osize; k++) {
                    unsigned char v = ring[(pos + (unsigned)k) & 4095];
                    ring[(out + 4078) & 4095] = v;
                    buf[nbuf++] = v;
                    out++;
                    if (nbuf == sizeof buf) {
                        rc = lha_sink_write(sink, buf, nbuf);
                        if (rc != LHA_OK) return rc;
                        nbuf = 0;
                    }
                }
            }
            if (nbuf >= sizeof buf - 1) {
                rc = lha_sink_write(sink, buf, nbuf);
                if (rc != LHA_OK) return rc;
                nbuf = 0;
            }
        }
    }
    return nbuf ? lha_sink_write(sink, buf, nbuf) : LHA_OK;
}

static int decode_lzs(lha_src *src, lha_sink *sink, uint64_t osize)
{
    unsigned char ring[2048];
    unsigned char buf[4096];
    size_t nbuf = 0;
    uint64_t out = 0;
    bitin b;
    int rc;
    memset(ring, 0x20, sizeof ring);
    bi_init(&b, src);
    while (out < osize) {
        if (bi_get(&b, 1)) {
            unsigned v = bi_get(&b, 8);
            if (b.err) return b.err;
            ring[(out + 2031) & 2047] = (unsigned char)v;
            buf[nbuf++] = (unsigned char)v;
            out++;
        } else {
            unsigned pos = bi_get(&b, 11);
            int k, len = (int)bi_get(&b, 4) + 2;
            if (b.err) return b.err;
            for (k = 0; k < len && out < osize; k++) {
                unsigned char v = ring[(pos + (unsigned)k) & 2047];
                ring[(out + 2031) & 2047] = v;
                buf[nbuf++] = v;
                out++;
                if (nbuf == sizeof buf) {
                    rc = lha_sink_write(sink, buf, nbuf);
                    if (rc != LHA_OK) return rc;
                    nbuf = 0;
                }
            }
        }
        if (b.err) return b.err;
        if (nbuf >= sizeof buf - 1) {
            rc = lha_sink_write(sink, buf, nbuf);
            if (rc != LHA_OK) return rc;
            nbuf = 0;
        }
    }
    return nbuf ? lha_sink_write(sink, buf, nbuf) : LHA_OK;
}

static int decode_stored(lha_src *src, lha_sink *sink, uint64_t osize)
{
    unsigned char buf[8192];
    while (osize) {
        size_t n = osize < sizeof buf ? (size_t)osize : sizeof buf;
        size_t got = lha_src_read(src, buf, n);
        int rc;
        if (got == (size_t)-1) return LHA_ERR_IO;
        if (got < n) return LHA_ERR_TRUNCATED;
        rc = lha_sink_write(sink, buf, got);
        if (rc != LHA_OK) return rc;
        osize -= got;
    }
    return LHA_OK;
}

/* ------------------------------------------------------------------------------------------------
 * -lh1- (spec §5) and -lh2- (spec §6): adaptive Huffman character model, 314 or 286 symbols.
 */
#define A_NMAX 314
#define A_TMAX (2 * A_NMAX - 1)    /* 627 nodes */

typedef struct {
    int n, t, root;                /* symbols, nodes (2n - 1), root index (t - 1) */
    unsigned freq[A_TMAX + 1];     /* freq[t] is a sentinel larger than any count */
    int child[A_TMAX];             /* >= t: leaf for symbol child - t; else first child index */
    int parent[A_TMAX + A_NMAX];   /* parent[t + s] = node of symbol s */
} adaptive;

static void adaptive_init(adaptive *a, int nsym)   /* spec §5.2, §6.2 */
{
    int i, j;
    a->n = nsym; a->t = 2 * nsym - 1; a->root = a->t - 1;
    for (i = 0; i < a->n; i++) { a->freq[i] = 1; a->child[i] = i + a->t; a->parent[i + a->t] = i; }
    for (i = 0, j = a->n; j < a->t; i += 2, j++) {
        a->freq[j] = a->freq[i] + a->freq[i + 1];
        a->child[j] = i;
        a->parent[i] = a->parent[i + 1] = j;
    }
    a->freq[a->t] = 0xFFFFFFFFu;
    a->parent[a->root] = 0;
}

static void adaptive_rescale(adaptive *a)   /* spec §5.4, first part */
{
    int i, j, k, t = a->t;
    for (i = 0, j = 0; i < t; i++) {
        if (a->child[i] >= t) {
            a->freq[j] = (a->freq[i] + 1) / 2;
            a->child[j] = a->child[i];
            j++;
        }
    }
    for (i = 0, j = a->n; j < t; i += 2, j++) {
        unsigned f = a->freq[i] + a->freq[i + 1];
        /* f >= freq[i + 1] and nodes 0..j-1 are in non-decreasing order, so this stops at k >= i + 1;
         * counts are generated by the model itself, never read from the input */
        for (k = j - 1; f < a->freq[k]; k--) { }
        k++;
        memmove(&a->freq[k + 1], &a->freq[k], (size_t)(j - k) * sizeof a->freq[0]);
        a->freq[k] = f;
        memmove(&a->child[k + 1], &a->child[k], (size_t)(j - k) * sizeof a->child[0]);
        a->child[k] = i;
    }
    for (i = 0; i < t; i++) {
        k = a->child[i];
        if (k >= t) a->parent[k] = i;
        else a->parent[k] = a->parent[k + 1] = i;
    }
}

static void adaptive_update(adaptive *a, int sym)   /* spec §5.4 */
{
    int c, t = a->t;
    if (a->freq[a->root] == 0x8000u) adaptive_rescale(a);
    c = a->parent[sym + t];
    for (;;) {
        unsigned k = ++a->freq[c];
        int l = c + 1;
        if (k > a->freq[l]) {
            int i, j;
            while (k > a->freq[l + 1]) l++;
            a->freq[c] = a->freq[l];
            a->freq[l] = k;
            i = a->child[c];
            a->parent[i] = l;
            if (i < t) a->parent[i + 1] = l;
            j = a->child[l];
            a->child[l] = i;
            a->parent[j] = c;
            if (j < t) a->parent[j + 1] = c;
            a->child[c] = j;
            c = l;
        }
        if (c == a->root) break;   /* the root is never exchanged: it has the largest count */
        c = a->parent[c];
    }
}

static int adaptive_decode(adaptive *a, bitin *b)
{
    int c = a->child[a->root];
    while (c < a->t) {
        c = a->child[c + (int)bi_get(b, 1)];
        if (b->err) return -1;
    }
    c -= a->t;
    adaptive_update(a, c);
    return c;
}

static int lh1_upper(bitin *b)   /* spec §5.5: canonical code, lengths 3..8 */
{
    static const int count[9] = {0, 0, 0, 1, 3, 8, 12, 24, 16};
    int len, code = 0, first = 0, base = 0;
    for (len = 1; len <= 8; len++) {
        code = (code << 1) | (int)bi_get(b, 1);
        if (b->err) return -1;
        first <<= 1;
        if (code - first < count[len]) return base + (code - first);
        base += count[len];
        first += count[len];
    }
    return -1;
}

/* Position tree of -lh2- (spec §6.3): node list in non-decreasing count order, grows to 128 leaves. */
#define G_LMAX 128
#define G_NMAX (2 * G_LMAX - 1)

typedef struct {
    int leaves, nodes, bias;       /* bias: 1 until the first rescale (spec §6.3) */
    unsigned freq[G_NMAX + 1];     /* freq[nodes] is a sentinel larger than any count */
    int child[G_NMAX];             /* < 0: leaf for block -child - 1; else first child index */
    int parent[G_NMAX];
    int leaf[G_LMAX];              /* node of each block */
} growing;

static void growing_links(growing *g)
{
    int i;
    for (i = 0; i < g->nodes; i++) {
        int c = g->child[i];
        if (c < 0) g->leaf[-c - 1] = i;
        else g->parent[c] = g->parent[c + 1] = i;
    }
    g->freq[g->nodes] = 0xFFFFFFFFu;
}

static void growing_init(growing *g)
{
    g->leaves = 1; g->nodes = 1; g->bias = 1;
    g->freq[0] = 1; g->child[0] = -1;
    growing_links(g);
}

static void growing_rescale(growing *g)   /* spec §6.3 -> §5.4 rebuild */
{
    int i, j, k, n = g->leaves;
    for (i = 0, j = 0; i < g->nodes; i++) {
        if (g->child[i] < 0) {
            g->freq[j] = (g->freq[i] + 1) / 2;
            g->child[j] = g->child[i];
            j++;
        }
    }
    for (i = 0, j = n; j < g->nodes; i += 2, j++) {
        unsigned f = g->freq[i] + g->freq[i + 1];
        for (k = j - 1; f < g->freq[k]; k--) { }
        k++;
        memmove(&g->freq[k + 1], &g->freq[k], (size_t)(j - k) * sizeof g->freq[0]);
        g->freq[k] = f;
        memmove(&g->child[k + 1], &g->child[k], (size_t)(j - k) * sizeof g->child[0]);
        g->child[k] = i;
    }
    growing_links(g);
}

static void growing_update(growing *g, int c)   /* spec §6.3 "Update"; c = node index */
{
    int root = g->nodes - 1;
    if (g->freq[root] - (unsigned)g->bias >= 0x8000u) {
        int blk = -g->child[c] - 1;
        growing_rescale(g);
        g->bias = 0;
        c = g->leaf[blk];
    }
    for (;;) {
        unsigned k = ++g->freq[c];
        int l = c + 1;
        if (k > g->freq[l]) {
            int i, j;
            while (k > g->freq[l + 1]) l++;
            g->freq[c] = g->freq[l];
            g->freq[l] = k;
            i = g->child[c];
            j = g->child[l];
            g->child[c] = j;
            g->child[l] = i;
            if (i < 0) g->leaf[-i - 1] = l; else g->parent[i] = g->parent[i + 1] = l;
            if (j < 0) g->leaf[-j - 1] = c; else g->parent[j] = g->parent[j + 1] = c;
            c = l;
        }
        if (c == root) break;
        c = g->parent[c];
    }
}

static int growing_add(growing *g)   /* spec §6.3 "Growth" */
{
    int i;
    if (g->leaves >= G_LMAX || g->child[0] >= 0) return LHA_ERR_DATA;   /* position 0 is a leaf */
    memmove(&g->freq[2], &g->freq[0], (size_t)g->nodes * sizeof g->freq[0]);
    memmove(&g->child[2], &g->child[0], (size_t)g->nodes * sizeof g->child[0]);
    for (i = 3; i < g->nodes + 2; i++)
        if (g->child[i] >= 0) g->child[i] += 2;
    g->child[0] = -(g->leaves + 1);       /* new leaf for block `leaves`, count 0 */
    g->freq[0] = 0;
    g->child[1] = g->child[2];            /* the old lowest node (a leaf) */
    g->freq[1] = g->freq[2];
    g->child[2] = 0;                      /* internal node over positions 0 and 1 */
    g->nodes += 2;
    g->leaves++;
    growing_links(g);
    growing_update(g, 0);
    return LHA_OK;
}

static int growing_decode(growing *g, bitin *b)
{
    int c = g->nodes - 1, blk;
    while (g->child[c] >= 0) {
        c = g->child[c] + (int)bi_get(b, 1);
        if (b->err) return -1;
    }
    blk = -g->child[c] - 1;
    growing_update(g, c);
    return blk;
}

/* -lh1- (spec §5) and -lh2- (spec §6) share the token loop. */
static int decode_adaptive(lha_src *src, lha_sink *sink, uint64_t osize, int lh2)
{
    typedef struct { adaptive a; growing g; unsigned char win[8192]; } state;
    state *st = (state *)malloc(sizeof *st);
    unsigned wmask = lh2 ? 8191u : 4095u;
    unsigned char buf[4096];
    size_t nbuf = 0;
    uint64_t out = 0;
    bitin b;
    int rc = LHA_OK;
    if (!st) return LHA_ERR_NOMEM;
    adaptive_init(&st->a, lh2 ? 286 : 314);
    growing_init(&st->g);
    memset(st->win, 0x20, sizeof st->win);      /* spec §5.6, §6.5 */
    bi_init(&b, src);
    while (out < osize) {
        int s = adaptive_decode(&st->a, &b);
        if (s < 0) { rc = b.err ? b.err : LHA_ERR_DATA; break; }
        if (s < 256) {
            st->win[out & wmask] = (unsigned char)s;
            buf[nbuf++] = (unsigned char)s;
            out++;
        } else {
            int len, u, k;
            unsigned dist;
            if (!lh2) {
                len = s - 253;
                u = lh1_upper(&b);
            } else {
                uint64_t reach = out < 8192 ? out : 8192;
                len = s < 285 ? s - 253 : 32 + (int)bi_get(&b, 8);   /* spec §6.1 */
                while ((uint64_t)st->g.leaves * 64u < reach) {
                    rc = growing_add(&st->g);
                    if (rc != LHA_OK) break;
                }
                if (rc != LHA_OK) break;
                u = st->g.leaves > 1 ? growing_decode(&st->g, &b) : 0;
            }
            if (u < 0 || b.err) { rc = b.err ? b.err : LHA_ERR_DATA; break; }
            dist = (unsigned)u * 64u + bi_get(&b, 6) + 1u;
            if (b.err) { rc = b.err; break; }
            for (k = 0; k < len && out < osize; k++) {
                unsigned char v = st->win[(out - dist) & wmask];
                st->win[out & wmask] = v;
                buf[nbuf++] = v;
                out++;
                if (nbuf == sizeof buf) {
                    rc = lha_sink_write(sink, buf, nbuf);
                    if (rc != LHA_OK) break;
                    nbuf = 0;
                }
            }
            if (rc != LHA_OK) break;
        }
        if (nbuf >= sizeof buf - 1) {
            rc = lha_sink_write(sink, buf, nbuf);
            if (rc != LHA_OK) break;
            nbuf = 0;
        }
    }
    if (rc == LHA_OK && nbuf) rc = lha_sink_write(sink, buf, nbuf);
    free(st);
    return rc;
}

/* ------------------------------------------------------------------------------------------------
 * -lh3- (spec §7): static Huffman blocks, 286 character symbols, 128 position blocks, window 8192.
 */
static int complete_code(const unsigned char *len, int n)   /* spec §7.2: Kraft sum 1, >= 2 symbols */
{
    uint32_t kraft = 0;
    int s, used = 0;
    for (s = 0; s < n; s++)
        if (len[s]) { kraft += 1u << (16 - len[s]); used++; }
    return used >= 2 && kraft == 65536u;
}

static int decode_lh3(lha_src *src, lha_sink *sink, uint64_t osize)
{
    static const unsigned char fixed_count[10] = {0, 0, 1, 0, 2, 3, 7, 18, 47, 50};   /* spec §7.3 */
    unsigned char len[286], plen[128];
    bitin b;
    hist h;
    pcode *c, *pp;
    int rc, s;

    rc = hist_init(&h, 8192, osize, sink);
    if (rc != LHA_OK) return rc;
    memset(h.ring, 0x20, 8192);                       /* spec §7.5 */
    c = (pcode *)malloc(sizeof *c);
    pp = (pcode *)malloc(sizeof *pp);
    if (!c || !pp) { rc = LHA_ERR_NOMEM; goto done; }
    bi_init(&b, src);

    while (h.total < osize) {
        unsigned left = bi_get(&b, 16);               /* spec §7.1 */
        if (b.err) { rc = b.err; goto done; }
        if (left == 0) { rc = LHA_ERR_DATA; goto done; }
        for (s = 0; s < 286; s++)                     /* spec §7.2 */
            len[s] = bi_get(&b, 1) ? (unsigned char)(bi_get(&b, 4) + 1) : 0;
        if (b.err) { rc = b.err; goto done; }
        if (!complete_code(len, 286)) { rc = LHA_ERR_DATA; goto done; }
        rc = pcode_build(c, len, 286);
        if (rc != LHA_OK) goto done;
        if (bi_get(&b, 1)) {                          /* spec §7.3, flag 1 */
            int any = 0;
            for (s = 0; s < 128; s++) { plen[s] = (unsigned char)bi_get(&b, 4); any |= plen[s]; }
            if (b.err) { rc = b.err; goto done; }
            if (!any) {
                rc = pcode_build(pp, plen, 128);
                pp->constant = 0;
            } else if (!complete_code(plen, 128)) {
                rc = LHA_ERR_DATA;
            } else {
                rc = pcode_build(pp, plen, 128);
            }
        } else {                                      /* flag 0: fixed code */
            int l, k = 0;
            for (l = 2; l <= 9; l++) {
                int i;
                for (i = 0; i < fixed_count[l]; i++) plen[k++] = (unsigned char)l;
            }
            rc = pcode_build(pp, plen, 128);
        }
        if (rc != LHA_OK) goto done;
        while (left-- > 0 && h.total < osize) {       /* spec §7.4 */
            int sym = pcode_read(c, &b);
            if (sym < 0 || b.err) { rc = b.err ? b.err : LHA_ERR_DATA; goto done; }
            if (sym < 256) {
                rc = hist_put(&h, (unsigned char)sym);
                if (rc != LHA_OK) goto done;
            } else {
                int mlen = sym < 285 ? sym - 253 : 32 + (int)bi_get(&b, 8);
                int u = pcode_read(pp, &b);
                unsigned dist;
                if (u < 0 || b.err) { rc = b.err ? b.err : LHA_ERR_DATA; goto done; }
                dist = (unsigned)u * 64u + bi_get(&b, 6) + 1u;
                if (b.err) { rc = b.err; goto done; }
                while (mlen-- > 0 && h.total < osize) {
                    unsigned char v = h.ring[(size_t)((h.total - dist) & h.mask)];
                    rc = hist_put(&h, v);
                    if (rc != LHA_OK) goto done;
                }
            }
        }
    }
    rc = hist_flush(&h);
done:
    free(c); free(pp);
    hist_free(&h);
    return rc;
}

/* ------------------------------------------------------------------------------------------------
 * -pm2- (spec §8): move-to-front list classes + LZ copies, window 8192, header every 4096 bytes.
 */
static int pm2_classes(uint64_t pos)            /* spec §8.5: distance classes reachable so far */
{
    return 5 + (pos >= 1024) + (pos >= 2048) + (pos >= 4096);
}

static int pm2_region(pcode *d, bitin *b, int n) /* spec §8.5 */
{
    unsigned char len[8];
    int i, used = 0, last = 0;
    for (i = 0; i < n; i++) {
        len[i] = (unsigned char)bi_get(b, 3);
        if (len[i]) { used++; last = i; }
    }
    if (b->err) return b->err;
    if (used == 0) { d->constant = -2; return LHA_OK; }          /* no distance code */
    if (pcode_build(d, len, n) != LHA_OK) return LHA_ERR_DATA;
    if (used == 1) d->constant = last;
    return LHA_OK;
}

static int decode_pm2(lha_src *src, lha_sink *sink, uint64_t osize)
{
    static const unsigned char cbase[8] = {0, 8, 16, 32, 64, 96, 128, 192}, cbits[8] = {3, 3, 4, 5, 5, 5, 6, 6};
    static const unsigned short dbase[8] = {1, 65, 129, 257, 513, 1025, 2049, 4097};
    static const unsigned char dbits[8] = {6, 6, 7, 8, 9, 10, 11, 12};
    unsigned char list[256], len[32];
    bitin b;
    hist h;
    pcode *t, *d;
    uint64_t next_flag = 0;
    int rc, i, have = 0, ncopy = 0, nd = 0;

    rc = hist_init(&h, 8192, osize, sink);
    if (rc != LHA_OK) return rc;
    t = (pcode *)malloc(sizeof *t);
    d = (pcode *)malloc(sizeof *d);
    if (!t || !d) { rc = LHA_ERR_NOMEM; goto done; }
    for (i = 0; i < 96; i++) list[i] = (unsigned char)(0x20 + i);      /* spec §8.1 */
    for (i = 0; i < 32; i++) list[96 + i] = (unsigned char)i;
    for (i = 0; i < 64; i++) list[128 + i] = (unsigned char)(0xa0 + i);
    for (i = 0; i < 32; i++) list[192 + i] = (unsigned char)(0x80 + i);
    for (i = 0; i < 32; i++) list[224 + i] = (unsigned char)(0xe0 + i);
    bi_init(&b, src);

    while (h.total < osize) {
        int sym;
        if (h.total >= next_flag) {                                   /* spec §8.4 */
            next_flag += 4096;
            if (bi_get(&b, 1)) {
                int n = (int)bi_get(&b, 5), p = (int)bi_get(&b, 3), w;
                if (n > 29 || n == 0) { rc = LHA_ERR_DATA; goto done; }
                if (p == 0) {                                 /* spec §8.3: only symbol N - 1 */
                    memset(len, 0, sizeof len);
                    len[n - 1] = 1;
                } else {
                    w = (int)bi_get(&b, 3);
                    for (i = 0; i < n; i++) {
                        int f = (int)bi_get(&b, w), l = f ? f + p - 1 : 0;
                        if (l > 16) { rc = LHA_ERR_DATA; goto done; }
                        len[i] = (unsigned char)l;
                    }
                }
                if (b.err) { rc = b.err; goto done; }
                {
                    int used = 0, last = 0;
                    for (i = 0; i < n; i++) if (len[i]) { used++; last = i; }
                    if (!used) { rc = LHA_ERR_DATA; goto done; }
                    if (pcode_build(t, len, n) != LHA_OK) { rc = LHA_ERR_DATA; goto done; }
                    if (used == 1) t->constant = last;
                }
                have = 1;
                ncopy = n > 8;
                if (ncopy) {
                    nd = pm2_classes(h.total);
                    rc = pm2_region(d, &b, nd);
                    if (rc != LHA_OK) goto done;
                }
            } else if (!have) {
                rc = LHA_ERR_DATA; goto done;
            }
        }
        if (ncopy && pm2_classes(h.total) > nd) {
            nd = pm2_classes(h.total);
            rc = pm2_region(d, &b, nd);
            if (rc != LHA_OK) goto done;
        }
        sym = pcode_read(t, &b);
        if (sym < 0 || b.err) { rc = b.err ? b.err : LHA_ERR_DATA; goto done; }
        if (sym < 8) {                                               /* spec §8.2: literal */
            int k = cbase[sym] + (int)bi_get(&b, cbits[sym]);
            unsigned char v = list[k];
            if (b.err) { rc = b.err; goto done; }
            memmove(&list[1], &list[0], (size_t)k);
            list[0] = v;
            rc = hist_put(&h, v);
            if (rc != LHA_OK) goto done;
        } else {
            int mlen;
            unsigned dist;
            if (sym == 28) {
                mlen = 256; dist = 1;
            } else if (sym == 8) {
                mlen = 2; dist = bi_get(&b, 6) + 1u;
            } else {
                int c;
                if (sym <= 22) mlen = sym - 6;
                else if (sym == 23) mlen = 17 + (int)bi_get(&b, 3);
                else if (sym == 24) mlen = 25 + (int)bi_get(&b, 3);
                else if (sym == 25) mlen = 33 + (int)bi_get(&b, 5);
                else if (sym == 26) mlen = 65 + (int)bi_get(&b, 6);
                else mlen = 129 + (int)bi_get(&b, 7);
                if (d->constant == -2) { rc = LHA_ERR_DATA; goto done; }   /* spec §8.6 */
                c = pcode_read(d, &b);
                if (c < 0 || b.err) { rc = b.err ? b.err : LHA_ERR_DATA; goto done; }
                dist = dbase[c] + bi_get(&b, dbits[c]);
            }
            if (b.err) { rc = b.err; goto done; }
            if ((uint64_t)dist > h.total) { rc = LHA_ERR_DATA; goto done; }  /* spec §8.6 */
            while (mlen-- > 0 && h.total < osize) {
                unsigned char v = h.ring[(size_t)((h.total - dist) & h.mask)];
                int k = 0;
                while (list[k] != v) k++;
                memmove(&list[1], &list[0], (size_t)k);
                list[0] = v;
                rc = hist_put(&h, v);
                if (rc != LHA_OK) goto done;
            }
        }
    }
    rc = hist_flush(&h);
done:
    free(t); free(d);
    hist_free(&h);
    return rc;
}

/* ------------------------------------------------------------------------------------------------
 * -pm1- (spec §9): start value, literal runs with move-to-front classes, LZ copies, window 10816.
 */
typedef struct {
    unsigned char n;
    struct { unsigned char len, code, base, ebits; } e[5];
} pm1_lit;

static const pm1_lit pm1_lits[32] = {   /* spec §9.6, generated from the spec table by tools/l0031_ctab.py */
    {1, {{0, 0x0, 0, 8}}},
    {4, {{1, 0x1, 64, 7}, {2, 0x0, 0, 5}, {3, 0x2, 32, 5}, {3, 0x3, 192, 6}}},
    {3, {{1, 0x0, 0, 6}, {2, 0x2, 64, 6}, {2, 0x3, 128, 7}}},
    {5, {{2, 0x0, 0, 4}, {2, 0x2, 64, 6}, {2, 0x3, 128, 7}, {3, 0x2, 16, 4}, {3, 0x3, 32, 5}}},
    {5, {{2, 0x0, 0, 4}, {2, 0x2, 32, 5}, {2, 0x3, 128, 7}, {3, 0x2, 16, 4}, {3, 0x3, 64, 6}}},
    {5, {{2, 0x0, 0, 4}, {2, 0x2, 32, 5}, {2, 0x3, 64, 6}, {3, 0x2, 16, 4}, {3, 0x3, 128, 7}}},
    {4, {{1, 0x0, 0, 5}, {2, 0x3, 128, 7}, {3, 0x4, 32, 5}, {3, 0x5, 64, 6}}},
    {4, {{1, 0x0, 0, 5}, {2, 0x3, 64, 6}, {3, 0x4, 32, 5}, {3, 0x5, 128, 7}}},
    {4, {{1, 0x0, 0, 5}, {2, 0x2, 32, 5}, {3, 0x6, 64, 6}, {3, 0x7, 128, 7}}},
    {5, {{1, 0x0, 0, 4}, {2, 0x3, 64, 7}, {3, 0x5, 32, 5}, {4, 0x8, 16, 4}, {4, 0x9, 192, 6}}},
    {5, {{1, 0x0, 0, 4}, {2, 0x3, 64, 6}, {3, 0x5, 32, 5}, {4, 0x8, 16, 4}, {4, 0x9, 128, 7}}},
    {5, {{1, 0x0, 0, 4}, {2, 0x3, 128, 7}, {3, 0x5, 64, 6}, {4, 0x8, 16, 4}, {4, 0x9, 32, 5}}},
    {5, {{1, 0x0, 0, 4}, {2, 0x3, 64, 7}, {3, 0x4, 16, 4}, {4, 0xa, 32, 5}, {4, 0xb, 192, 6}}},
    {5, {{1, 0x0, 0, 4}, {3, 0x4, 16, 4}, {3, 0x5, 32, 5}, {3, 0x6, 64, 6}, {3, 0x7, 128, 7}}},
    {5, {{1, 0x0, 0, 4}, {2, 0x3, 32, 5}, {3, 0x4, 16, 4}, {4, 0xa, 64, 6}, {4, 0xb, 128, 7}}},
    {5, {{1, 0x0, 0, 4}, {2, 0x2, 16, 4}, {3, 0x7, 128, 7}, {4, 0xc, 32, 5}, {4, 0xd, 64, 6}}},
    {5, {{1, 0x0, 0, 4}, {2, 0x2, 16, 4}, {3, 0x6, 32, 5}, {4, 0xe, 64, 6}, {4, 0xf, 128, 7}}},
    {3, {{1, 0x1, 64, 7}, {2, 0x0, 64, 7}, {2, 0x1, 32, 5}}},
    {5, {{2, 0x0, 0, 4}, {2, 0x2, 32, 5}, {2, 0x3, 64, 6}, {3, 0x2, 16, 4}, {3, 0x3, 128, 6}}},
    {3, {{1, 0x0, 0, 5}, {2, 0x2, 32, 5}, {2, 0x3, 64, 7}}},
    {5, {{1, 0x0, 0, 4}, {2, 0x3, 64, 6}, {3, 0x5, 32, 5}, {4, 0x8, 16, 4}, {4, 0x9, 128, 6}}},
    {4, {{1, 0x0, 0, 4}, {2, 0x3, 64, 7}, {3, 0x4, 16, 4}, {3, 0x5, 32, 5}}},
    {4, {{1, 0x0, 0, 4}, {2, 0x3, 32, 5}, {3, 0x4, 16, 4}, {3, 0x5, 64, 7}}},
    {4, {{1, 0x0, 0, 4}, {2, 0x2, 16, 4}, {3, 0x6, 32, 5}, {3, 0x7, 64, 7}}},
    {1, {{0, 0x0, 0, 7}}},
    {4, {{1, 0x1, 32, 5}, {2, 0x0, 0, 4}, {3, 0x2, 16, 4}, {3, 0x3, 64, 6}}},
    {3, {{1, 0x0, 0, 5}, {2, 0x2, 32, 5}, {2, 0x3, 64, 6}}},
    {4, {{1, 0x0, 0, 4}, {2, 0x3, 32, 5}, {3, 0x4, 16, 4}, {3, 0x5, 64, 6}}},
    {4, {{1, 0x0, 0, 4}, {2, 0x2, 16, 4}, {3, 0x6, 32, 5}, {3, 0x7, 64, 6}}},
    {3, {{1, 0x0, 0, 4}, {2, 0x2, 16, 4}, {2, 0x3, 32, 5}}},
    {1, {{0, 0x0, 0, 5}}},
    {1, {{0, 0x0, 0, 4}}},
};

static int pm1_run(bitin *b)                     /* spec §9.4 */
{
    unsigned v;
    if (!bi_get(b, 1)) return 0;
    v = bi_get(b, 2);
    if (v < 3) return 1 + (int)v;
    v = bi_get(b, 3);
    if (v < 7) return 4 + (int)v;
    v = bi_get(b, 4);
    if (v < 14) return 11 + (int)v;
    return v == 14 ? 25 + (int)bi_get(b, 6) : 89 + (int)bi_get(b, 7);
}

static int pm1_length(bitin *b)                  /* spec §9.5, length of a long copy */
{
    unsigned v = bi_get(b, 2);
    if (v < 3) return 3 + (int)v;
    v = bi_get(b, 3);
    if (v < 5) return 6 + (int)v;
    if (v == 5) return 11 + (int)bi_get(b, 2);
    if (v == 6) return 15 + (int)bi_get(b, 3);
    v = bi_get(b, 6);
    if (v < 62) return 23 + (int)v;
    return v == 62 ? 85 + (int)bi_get(b, 5) : 117 + (int)bi_get(b, 7);
}

static unsigned pm1_far(bitin *b, unsigned base, int maxw, uint64_t q)   /* spec §9.5, far-k field */
{
    int w = 8;
    while (w < maxw && base + (1u << w) - 1 < q) w++;
    return base + bi_get(b, w);
}

static int decode_pm1(lha_src *src, lha_sink *sink, uint64_t osize)
{
    unsigned char list[256];
    const pm1_lit *lit;
    bitin b;
    hist h;
    int rc, i;

    rc = hist_init(&h, 16384, osize, sink);
    if (rc != LHA_OK) return rc;
    for (i = 0; i < 96; i++) list[i] = (unsigned char)(0x20 + i);      /* spec §9.1 = §8.1 */
    for (i = 0; i < 32; i++) list[96 + i] = (unsigned char)i;
    for (i = 0; i < 64; i++) list[128 + i] = (unsigned char)(0xa0 + i);
    for (i = 0; i < 32; i++) list[192 + i] = (unsigned char)(0x80 + i);
    for (i = 0; i < 32; i++) list[224 + i] = (unsigned char)(0xe0 + i);
    bi_init(&b, src);
    lit = &pm1_lits[bi_get(&b, 5)];                                    /* spec §9.2 */

    while (h.total < osize) {
        int run = pm1_run(&b), mlen;
        unsigned dist;
        uint64_t q;
        if (b.err) { rc = b.err; goto done; }
        for (i = 0; i < run; i++) {                                    /* spec §9.3, §9.6 */
            unsigned code = 0, k;
            int len = 0, j;
            unsigned char v;
            for (;;) {
                for (j = 0; j < lit->n; j++)
                    if (lit->e[j].len == len && lit->e[j].code == code) break;
                if (j < lit->n || len == 8) break;
                code = (code << 1) | bi_get(&b, 1);
                len++;
            }
            if (j == lit->n) { rc = LHA_ERR_DATA; goto done; }
            k = lit->e[j].base + bi_get(&b, lit->e[j].ebits);
            if (b.err) { rc = b.err; goto done; }
            v = list[k];
            memmove(&list[1], &list[0], (size_t)k);
            list[0] = v;
            rc = hist_put(&h, v);
            if (rc != LHA_OK) goto done;
            if (h.total >= osize) goto finish;
        }
        if (run == 216) continue;                                      /* spec §9.3 */
        q = h.total + 1;                                               /* spec §9.5 */
        if (q < 65) {                                                  /* one class */
            mlen = bi_get(&b, 1) ? pm1_length(&b) : 2;
            dist = bi_get(&b, 6) + 1u;
        } else {
            int nclass = 2 + (q >= 577) + (q >= 2625);
            unsigned t = bi_get(&b, 2);
            if (nclass == 2 && t < 2) {
                mlen = 2; dist = t == 0 ? bi_get(&b, 6) + 1u : 65 + bi_get(&b, 8);
            } else if (nclass > 2 && t == 0) {
                mlen = 2; dist = bi_get(&b, 1) ? 65 + bi_get(&b, 8) : bi_get(&b, 6) + 1u;
            } else {
                int far3 = t == 3 && nclass == 4 && !bi_get(&b, 1);    /* `110` far-3, `111` near */
                mlen = pm1_length(&b);
                if (t == 1) dist = pm1_far(&b, 577, 11, q);
                else if (t == 2) dist = pm1_far(&b, 65, 9, q);
                else if (far3) dist = pm1_far(&b, 2625, 13, q);
                else dist = bi_get(&b, 6) + 1u;
            }
        }
        if (b.err) { rc = b.err; goto done; }
        if ((uint64_t)dist > h.total) { rc = LHA_ERR_DATA; goto done; }  /* spec §9.7 */
        while (mlen-- > 0 && h.total < osize) {
            unsigned char v = h.ring[(size_t)((h.total - dist) & h.mask)];
            int k = 0;
            while (list[k] != v) k++;
            memmove(&list[1], &list[0], (size_t)k);
            list[0] = v;
            rc = hist_put(&h, v);
            if (rc != LHA_OK) goto done;
        }
    }
finish:
    rc = hist_flush(&h);
done:
    hist_free(&h);
    return rc;
}

int lha_decode_method(lha_method m, lha_src *src, lha_sink *sink, uint64_t osize)
{
    huf_params p;
    memset(&p, 0, sizeof p);
    switch (m) {
    case LHA_M_LH0: case LHA_M_LZ4: case LHA_M_PM0:
        return decode_stored(src, sink, osize);
    case LHA_M_LHD:
        return osize ? LHA_ERR_FORMAT : LHA_OK;
    case LHA_M_LH4: case LHA_M_LH5:              /* spec §2.1 */
        p.pbit = 4; p.np = 16; p.window = 16384; break;
    case LHA_M_LH6:
        p.pbit = 5; p.np = 32; p.window = 65536; break;
    case LHA_M_LH7:
        p.pbit = 5; p.np = 32; p.window = 131072; break;
    case LHA_M_LHX:
        p.pbit = 5; p.np = 32; p.window = 1048576; break;
    case LHA_M_LK7:                              /* spec §3 */
        p.pbit = 6; p.np = 64; p.window = 65536; p.lk7 = 1; break;
    case LHA_M_LH1:
        return decode_adaptive(src, sink, osize, 0);
    case LHA_M_LH2:
        return decode_adaptive(src, sink, osize, 1);
    case LHA_M_LH3:
        return decode_lh3(src, sink, osize);
    case LHA_M_PM1:
        return decode_pm1(src, sink, osize);
    case LHA_M_PM2:
        return decode_pm2(src, sink, osize);
    case LHA_M_LZ5:
        return decode_lz5(src, sink, osize);
    case LHA_M_LZS:
        return decode_lzs(src, sink, osize);
    default:
        return LHA_ERR_UNSUPPORTED;
    }
    return decode_huf(src, sink, osize, &p);
}
