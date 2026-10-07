/* inflate.c - FultaArc: Deflate (RFC 1951) and Deflate64 decoder, as a pull stream. MIT.
 * Written from docs/specs/codec-deflate.md (FultaArc's format document; RFC 1951 and PKWARE's Deflate64 notes). */
#include "../core/internal.h"

#define FAST_BITS 10

typedef struct huff {
    /* fast table: index = next FAST_BITS bits (LSB-first as read), entry = (len << 9) | sym, len 0 = slow path */
    uint16_t fast[1 << FAST_BITS];
    uint16_t count[16];          /* codes per length */
    uint16_t symbol[320];        /* symbols ordered by (length, value) */
    int nsym;
} huff_t;

typedef struct inflate_stream {
    fa_stream_t base;
    fa_bytes_t in;
    fa_stream_t *src;
    bool d64;
    uint64_t out_size, produced;
    uint64_t bitbuf;
    int bitcnt;
    uint8_t *window;             /* 65536 bytes */
    uint32_t wmask;
    uint64_t wtotal;             /* bytes written into the window */
    int state;                   /* 0 need header, 1 stored, 2 huffman, 3 done */
    bool final;
    uint32_t stored_left;
    uint32_t copy_len, copy_dist;
    fulta_arc_err_t err;         /* an error held back after a partial read */
    huff_t lit, dist;
} inflate_stream_t;

static const uint16_t LBASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83,
                                   99, 115, 131, 163, 195, 227, 258};
static const uint8_t LEXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5,
                                   5, 5, 0};
static const uint32_t DBASE[32] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                   1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577, 32769, 49153};
static const uint8_t DEXTRA[32] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11,
                                   12, 12, 13, 13, 14, 14};

static fulta_arc_err_t need(inflate_stream_t *z, int n) {
    while (z->bitcnt < n) {
        int c = fa_bytes_get(&z->in);
        if (c < 0) return z->in.err ? z->in.err : FULTA_ARC_ERR_TRUNCATED;
        z->bitbuf |= (uint64_t)c << z->bitcnt;
        z->bitcnt += 8;
    }
    return FULTA_ARC_OK;
}

static fulta_arc_err_t bits(inflate_stream_t *z, int n, uint32_t *v) {
    if (n == 0) { *v = 0; return FULTA_ARC_OK; }
    fulta_arc_err_t e = need(z, n);
    if (e) return e;
    *v = (uint32_t)(z->bitbuf & ((1u << n) - 1));
    z->bitbuf >>= n;
    z->bitcnt -= n;
    return FULTA_ARC_OK;
}

static uint32_t rev(uint32_t v, int n) {
    uint32_t r = 0;
    for (int i = 0; i < n; i++) { r = (r << 1) | (v & 1); v >>= 1; }
    return r;
}

/* Build from code lengths; returns CORRUPT on over-subscription, or an incomplete set other than the allowed ones. */
static fulta_arc_err_t build(huff_t *h, const uint8_t *len, int n, bool is_dist) {
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[len[i]]++;
    h->count[0] = 0;
    int left = 1;
    for (int l = 1; l < 16; l++) {
        left <<= 1;
        left -= h->count[l];
        if (left < 0) return FULTA_ARC_ERR_CORRUPT;
    }
    int used = 0;
    for (int i = 0; i < n; i++) used += len[i] != 0;
    if (left > 0 && !(used == 0 || (used == 1 && h->count[1] == 1))) {
        (void)is_dist;
        return FULTA_ARC_ERR_CORRUPT;
    }
    uint16_t offs[16];
    offs[1] = 0;
    for (int l = 1; l < 15; l++) offs[l + 1] = (uint16_t)(offs[l] + h->count[l]);
    for (int i = 0; i < n; i++)
        if (len[i]) h->symbol[offs[len[i]]++] = (uint16_t)i;
    h->nsym = used;
    memset(h->fast, 0, sizeof h->fast);
    /* canonical codes; fill the fast table for codes of up to FAST_BITS bits */
    uint32_t code = 0;
    int k = 0;
    for (int l = 1; l < 16; l++) {
        for (int c = 0; c < h->count[l]; c++, k++) {
            if (l <= FAST_BITS) {
                uint32_t r = rev(code, l);
                for (uint32_t f = r; f < (1u << FAST_BITS); f += 1u << l)
                    h->fast[f] = (uint16_t)((l << 9) | h->symbol[k]);
            }
            code++;
        }
        code <<= 1;
    }
    return FULTA_ARC_OK;
}

static fulta_arc_err_t decode(inflate_stream_t *z, const huff_t *h, int *sym) {
    (void)need(z, FAST_BITS);      /* near the end fewer bits may be there; the slow path reads exactly */
    if (z->bitcnt >= FAST_BITS || z->bitcnt > 0) {
        uint16_t f = h->fast[z->bitbuf & ((1u << FAST_BITS) - 1)];
        int l = f >> 9;
        if (l && l <= z->bitcnt) {
            z->bitbuf >>= l;
            z->bitcnt -= l;
            *sym = f & 511;
            return FULTA_ARC_OK;
        }
    }
    /* slow path: bit by bit (puff's canonical decoding) */
    int code = 0, first = 0, index = 0;
    for (int l = 1; l < 16; l++) {
        uint32_t b;
        fulta_arc_err_t e = bits(z, 1, &b);
        if (e) return e;
        code |= (int)b;
        int count = h->count[l];
        if (code - count < first) { *sym = h->symbol[index + (code - first)]; return FULTA_ARC_OK; }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return FULTA_ARC_ERR_CORRUPT;
}

static fulta_arc_err_t read_header(inflate_stream_t *z) {
    uint32_t v;
    fulta_arc_err_t e;
    if ((e = bits(z, 1, &v))) return e;
    z->final = v;
    uint32_t type;
    if ((e = bits(z, 2, &type))) return e;
    if (type == 0) {
        z->bitbuf >>= z->bitcnt & 7;
        z->bitcnt -= z->bitcnt & 7;
        uint32_t len, nlen;
        if ((e = bits(z, 16, &len)) || (e = bits(z, 16, &nlen))) return e;
        if (len != (~nlen & 0xFFFF)) return FULTA_ARC_ERR_CORRUPT;
        z->stored_left = len;
        z->state = 1;
        return FULTA_ARC_OK;
    }
    if (type == 3) return FULTA_ARC_ERR_CORRUPT;
    uint8_t lens[320];
    if (type == 1) {
        int i = 0;
        for (; i < 144; i++) lens[i] = 8;
        for (; i < 256; i++) lens[i] = 9;
        for (; i < 280; i++) lens[i] = 7;
        for (; i < 288; i++) lens[i] = 8;
        if ((e = build(&z->lit, lens, 288, false))) return e;
        for (i = 0; i < 32; i++) lens[i] = 5;
        if ((e = build(&z->dist, lens, 32, true))) return e;
        z->state = 2;
        return FULTA_ARC_OK;
    }
    uint32_t hlit, hdist, hclen;
    if ((e = bits(z, 5, &hlit)) || (e = bits(z, 5, &hdist)) || (e = bits(z, 4, &hclen))) return e;
    hlit += 257;
    hdist += 1;
    hclen += 4;
    if (hlit > 286 || hdist > (z->d64 ? 32u : 30u)) return FULTA_ARC_ERR_CORRUPT;
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    uint8_t cl[19] = {0};
    for (uint32_t i = 0; i < hclen; i++) {
        if ((e = bits(z, 3, &v))) return e;
        cl[order[i]] = (uint8_t)v;
    }
    huff_t clh;
    if ((e = build(&clh, cl, 19, false))) return e;
    uint32_t n = 0, total = hlit + hdist;
    while (n < total) {
        int sym;
        if ((e = decode(z, &clh, &sym))) return e;
        if (sym < 16) { lens[n++] = (uint8_t)sym; continue; }
        uint32_t rep;
        uint8_t val = 0;
        if (sym == 16) {
            if (n == 0) return FULTA_ARC_ERR_CORRUPT;
            val = lens[n - 1];
            if ((e = bits(z, 2, &rep))) return e;
            rep += 3;
        } else if (sym == 17) {
            if ((e = bits(z, 3, &rep))) return e;
            rep += 3;
        } else {
            if ((e = bits(z, 7, &rep))) return e;
            rep += 11;
        }
        if (n + rep > total) return FULTA_ARC_ERR_CORRUPT;
        while (rep--) lens[n++] = val;
    }
    if (lens[256] == 0) return FULTA_ARC_ERR_CORRUPT;
    if ((e = build(&z->lit, lens, (int)hlit, false))) return e;
    if ((e = build(&z->dist, lens + hlit, (int)hdist, true))) return e;
    z->state = 2;
    return FULTA_ARC_OK;
}

static inline void put(inflate_stream_t *z, uint8_t c) {
    z->window[z->wtotal & z->wmask] = c;
    z->wtotal++;
}

static fulta_arc_err_t inflate_read(fa_stream_t *s, void *buf, size_t n, size_t *got) {
    inflate_stream_t *z = (inflate_stream_t *)s;
    uint8_t *out = buf;
    size_t done = 0;
    fulta_arc_err_t e = FULTA_ARC_OK;
    *got = 0;
    if (z->err) return z->err;
    if (z->out_size != UINT64_MAX && n > z->out_size - z->produced) n = (size_t)(z->out_size - z->produced);
    while (done < n) {
        if (z->copy_len) {
            uint64_t from = z->wtotal - z->copy_dist;
            while (z->copy_len && done < n) {
                uint8_t c = z->window[from++ & z->wmask];
                put(z, c);
                out[done++] = c;
                z->copy_len--;
            }
            continue;
        }
        if (z->state == 3) break;
        if (z->state == 0) {
            if ((e = read_header(z))) break;
            continue;
        }
        if (z->state == 1) {
            if (!z->stored_left) { z->state = z->final ? 3 : 0; continue; }
            uint32_t v;
            if ((e = bits(z, 8, &v))) break;
            put(z, (uint8_t)v);
            out[done++] = (uint8_t)v;
            z->stored_left--;
            continue;
        }
        int sym;
        if ((e = decode(z, &z->lit, &sym))) break;
        if (sym < 256) {
            put(z, (uint8_t)sym);
            out[done++] = (uint8_t)sym;
            continue;
        }
        if (sym == 256) { z->state = z->final ? 3 : 0; continue; }
        sym -= 257;
        if (sym >= 29) { e = FULTA_ARC_ERR_CORRUPT; break; }
        uint32_t extra, len;
        if (z->d64 && sym == 28) {
            if ((e = bits(z, 16, &extra))) break;
            len = 3 + extra;
        } else {
            if ((e = bits(z, LEXTRA[sym], &extra))) break;
            len = LBASE[sym] + extra;
        }
        int dsym;
        if ((e = decode(z, &z->dist, &dsym))) break;
        if (dsym >= (z->d64 ? 32 : 30)) { e = FULTA_ARC_ERR_CORRUPT; break; }
        if ((e = bits(z, DEXTRA[dsym], &extra))) break;
        uint32_t dist = DBASE[dsym] + extra;
        if (dist > z->wtotal || dist > z->wmask + 1) { e = FULTA_ARC_ERR_CORRUPT; break; }
        z->copy_len = len;
        z->copy_dist = dist;
    }
    if (e && done == 0) { z->err = e; return e; }
    if (e) z->err = e;                 /* report what was produced; the error comes back on the next call */
    z->produced += done;
    *got = done;
    if (!done && z->out_size != UINT64_MAX && z->produced < z->out_size) return FULTA_ARC_ERR_TRUNCATED;
    return FULTA_ARC_OK;
}

static void inflate_destroy(fa_stream_t *s) {
    inflate_stream_t *z = (inflate_stream_t *)s;
    fa_stream_destroy(z->src);
    fa_free(z->window);
    fa_free(z);
}

fulta_arc_err_t fa_dec_deflate(fa_stream_t *in, bool deflate64, uint64_t out_size, fa_stream_t **out) {
    inflate_stream_t *z = fa_calloc(1, sizeof *z);
    if (!z) { fa_stream_destroy(in); return FULTA_ARC_ERR_NOMEM; }
    z->window = fa_malloc(65536);
    if (!z->window) { fa_free(z); fa_stream_destroy(in); return FULTA_ARC_ERR_NOMEM; }
    z->wmask = 65535;
    z->src = in;
    fa_bytes_init(&z->in, in);
    z->d64 = deflate64;
    z->out_size = out_size;
    z->base.read = inflate_read;
    z->base.destroy = inflate_destroy;
    *out = &z->base;
    return FULTA_ARC_OK;
}
