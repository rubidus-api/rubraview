/* legacy.c - FultaArc: PKZIP 1.x's methods - Shrink (ZIP method 1), Reduce (2-5), Implode (6). MIT.
 * Written from docs/specs/zip.md sections 6.2-6.4 (FultaArc's document of PKWARE's APPNOTE, with the open points
 * settled by black-box tests against Info-ZIP UnZip and 7-Zip, section 6.5). Each decoder is a pull stream: a step
 * decodes one code into `ob`, read() hands `ob` out. Bits are read least significant first. */
#include "../core/internal.h"

#define OB_SIZE 16384                  /* more than any step's output: a Shrink string is under 8192 bytes */

typedef struct bits {
    fa_bytes_t in;
    uint32_t buf;                      /* the next bits, lowest first */
    int n;                             /* bits in buf */
    int over;                          /* zero bits supplied past the end of the data */
} bits_t;

static fulta_arc_err_t need(bits_t *b, int k) {
    while (b->n < k) {
        int c = fa_bytes_get(&b->in);
        if (c < 0) {
            if (b->in.err) return b->in.err;
            c = 0;
            b->over += 8;
        }
        b->buf |= (uint32_t)c << b->n;
        b->n += 8;
    }
    return FULTA_ARC_OK;
}

/* k bits (k <= 24); FULTA_ARC_ERR_TRUNCATED when they run past the data */
static fulta_arc_err_t get(bits_t *b, int k, uint32_t *v) {
    fulta_arc_err_t e = need(b, k);
    if (e) return e;
    if (b->over && b->n - k < b->over) return FULTA_ARC_ERR_TRUNCATED;
    *v = b->buf & ((UINT32_C(1) << k) - 1);
    b->buf >>= k;
    b->n -= k;
    return FULTA_ARC_OK;
}

/* The common stream: a step function fills ob; matches copy from the window (bytes before the start read 0). */
typedef struct legacy legacy_t;
typedef fulta_arc_err_t (*step_fn)(legacy_t *);

struct legacy {
    fa_stream_t base;
    bits_t b;
    step_fn step;
    uint64_t left;                     /* uncompressed bytes still to give */
    uint8_t *win;                      /* window of the last WIN bytes (Reduce, Implode) */
    uint32_t wpos;
    uint8_t ob[OB_SIZE];
    size_t olen, opos;
    void *state;
};

#define WIN 32768u

static void put(legacy_t *L, uint8_t c) {
    L->ob[L->olen++] = c;
    if (L->win) { L->win[L->wpos] = c; L->wpos = (L->wpos + 1) & (WIN - 1); }
}

static void copy_match(legacy_t *L, uint32_t dist, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) put(L, L->win[(L->wpos - dist) & (WIN - 1)]);
}

static fulta_arc_err_t legacy_read(fa_stream_t *s, void *buf, size_t n, size_t *got) {
    legacy_t *L = (legacy_t *)s;
    *got = 0;
    uint8_t *o = buf;
    while (*got < n && L->left) {
        if (L->opos == L->olen) {
            L->opos = L->olen = 0;
            fulta_arc_err_t e = L->step(L);
            if (e) return e;
            continue;
        }
        size_t k = L->olen - L->opos;
        if (k > n - *got) k = n - *got;
        if (k > L->left) k = (size_t)L->left;
        memcpy(o + *got, L->ob + L->opos, k);
        L->opos += k;
        *got += k;
        L->left -= k;
    }
    return FULTA_ARC_OK;
}

static void legacy_destroy(fa_stream_t *s) {
    legacy_t *L = (legacy_t *)s;
    fa_stream_destroy(L->b.in.in);
    fa_free(L->win);
    fa_free(L->state);
    fa_free(L);
}

static fulta_arc_err_t legacy_new(fa_stream_t *in, uint64_t out_size, step_fn step, size_t state_size, bool window,
                                  legacy_t **out) {
    legacy_t *L = fa_calloc(1, sizeof *L);
    if (!L) { fa_stream_destroy(in); return FULTA_ARC_ERR_NOMEM; }
    fa_bytes_init(&L->b.in, in);
    L->state = fa_calloc(1, state_size);
    L->win = window ? fa_calloc(1, WIN) : NULL;
    if (!L->state || (window && !L->win)) { legacy_destroy(&L->base); return FULTA_ARC_ERR_NOMEM; }
    L->step = step;
    L->left = out_size;
    L->base.read = legacy_read;
    L->base.destroy = legacy_destroy;
    *out = L;
    return FULTA_ARC_OK;
}

/* ---- Shrink (zip.md 6.2) ------------------------------------------------------------------------------------- */

typedef struct shrink {
    uint16_t prefix[8192];
    uint8_t suffix[8192];
    bool used[8192];
    uint8_t stack[8192];
    int width;
    int prev;                          /* -1 before the first code */
    unsigned free;                     /* lowest free code, 8192 when full */
} shrink_t;

static void next_free(shrink_t *t, unsigned from) {
    t->free = from;
    while (t->free < 8192 && t->used[t->free]) t->free++;
}

/* The string of code c into the end of t->stack; returns its start (0 on a broken chain). */
static size_t shrink_string(shrink_t *t, unsigned c, bool *ok) {
    size_t p = sizeof t->stack;
    *ok = true;
    while (c >= 257) {
        if (p == 1) { *ok = false; return 0; }
        t->stack[--p] = t->suffix[c];
        c = t->prefix[c];
    }
    t->stack[--p] = (uint8_t)c;
    return p;
}

static void partial_clear(shrink_t *t) {
    uint8_t *m = t->stack;             /* 8192 bytes of scratch: the string stack is free here */
    memset(m, 0, sizeof t->stack);
    for (unsigned c = 257; c < 8192; c++)
        if (t->used[c] && t->prefix[c] >= 257) m[t->prefix[c]] = 1;
    for (unsigned c = 257; c < 8192; c++)
        if (t->used[c] && !m[c]) t->used[c] = false;
    next_free(t, 257);
}

static fulta_arc_err_t shrink_step(legacy_t *L) {
    shrink_t *t = L->state;
    for (;;) {
        uint32_t c;
        fulta_arc_err_t e = get(&L->b, t->width, &c);
        if (e) return e;
        if (c == 256) {
            uint32_t sub;
            if ((e = get(&L->b, t->width, &sub))) return e;
            if (sub == 1) {
                if (t->width >= 13) return FULTA_ARC_ERR_CORRUPT;
                t->width++;
            } else if (sub == 2) {
                partial_clear(t);
            } else {
                return FULTA_ARC_ERR_CORRUPT;
            }
            continue;
        }
        if (t->prev < 0) {
            if (c > 255) return FULTA_ARC_ERR_CORRUPT;
            put(L, (uint8_t)c);
            t->prev = (int)c;
            return FULTA_ARC_OK;
        }
        size_t p;
        bool ok;
        uint8_t first;
        if (c < 257 || t->used[c]) {
            p = shrink_string(t, c, &ok);
            if (!ok) return FULTA_ARC_ERR_CORRUPT;
            first = t->stack[p];
            for (size_t i = p; i < sizeof t->stack; i++) put(L, t->stack[i]);
        } else if (c == t->free) {
            /* KwKwK: the previous string and its own first byte */
            p = shrink_string(t, (unsigned)t->prev, &ok);
            if (!ok) return FULTA_ARC_ERR_CORRUPT;
            first = t->stack[p];
            for (size_t i = p; i < sizeof t->stack; i++) put(L, t->stack[i]);
            put(L, first);
        } else {
            return FULTA_ARC_ERR_CORRUPT;
        }
        if (t->free < 8192) {
            unsigned k = t->free;
            t->prefix[k] = (uint16_t)t->prev;
            t->suffix[k] = first;
            t->used[k] = true;
            next_free(t, k + 1);
        }
        t->prev = (int)c;
        return FULTA_ARC_OK;
    }
}

fulta_arc_err_t fa_dec_unshrink(fa_stream_t *in, uint64_t out_size, fa_stream_t **out) {
    legacy_t *L;
    fulta_arc_err_t e = legacy_new(in, out_size, shrink_step, sizeof(shrink_t), false, &L);
    if (e) return e;
    shrink_t *t = L->state;
    t->width = 9;
    t->prev = -1;
    t->free = 257;
    *out = &L->base;
    return FULTA_ARC_OK;
}

/* ---- Reduce (zip.md 6.3) ------------------------------------------------------------------------------------- */

typedef struct reduce {
    uint8_t set[256][64];
    uint8_t n[256];
    bool sets_read;
    int factor;
    uint8_t last;
    int st;                            /* LZ state 0-3 */
    uint32_t v, len;
} reduce_t;

static int bits_for(unsigned n) {
    int b = 1;
    while ((1u << b) < n) b++;
    return b;                          /* B(N) = max(1, bit length of N - 1) */
}

static fulta_arc_err_t reduce_step(legacy_t *L) {
    reduce_t *r = L->state;
    fulta_arc_err_t e;
    uint32_t x;
    if (!r->sets_read) {
        for (int j = 255; j >= 0; j--) {
            if ((e = get(&L->b, 6, &x))) return e;
            r->n[j] = (uint8_t)x;
            for (unsigned k = 0; k < r->n[j]; k++) {
                if ((e = get(&L->b, 8, &x))) return e;
                r->set[j][k] = (uint8_t)x;
            }
        }
        r->sets_read = true;
    }
    static const uint32_t LMASK[5] = {0, 0x7F, 0x3F, 0x1F, 0x0F};
    for (;;) {
        uint8_t c;
        unsigned n = r->n[r->last];
        if (n == 0) {
            if ((e = get(&L->b, 8, &x))) return e;
            c = (uint8_t)x;
        } else {
            if ((e = get(&L->b, 1, &x))) return e;
            if (x) {
                if ((e = get(&L->b, 8, &x))) return e;
                c = (uint8_t)x;
            } else {
                if ((e = get(&L->b, bits_for(n), &x))) return e;
                if (x >= n) return FULTA_ARC_ERR_CORRUPT;
                c = r->set[r->last][x];
            }
        }
        r->last = c;
        switch (r->st) {
        case 0:
            if (c == 144) { r->st = 1; continue; }
            put(L, c);
            return FULTA_ARC_OK;
        case 1:
            if (c == 0) { r->st = 0; put(L, 144); return FULTA_ARC_OK; }
            r->v = c;
            r->len = c & LMASK[r->factor];
            r->st = r->len == LMASK[r->factor] ? 2 : 3;
            continue;
        case 2:
            r->len += c;
            r->st = 3;
            continue;
        default:
            r->st = 0;
            copy_match(L, ((r->v >> (8 - r->factor)) << 8) + c + 1, r->len + 3);
            return FULTA_ARC_OK;
        }
    }
}

fulta_arc_err_t fa_dec_unreduce(fa_stream_t *in, int factor, uint64_t out_size, fa_stream_t **out) {
    if (factor < 1 || factor > 4) { fa_stream_destroy(in); return FULTA_ARC_ERR_UNSUPPORTED; }
    legacy_t *L;
    fulta_arc_err_t e = legacy_new(in, out_size, reduce_step, sizeof(reduce_t), true, &L);
    if (e) return e;
    ((reduce_t *)L->state)->factor = factor;
    *out = &L->base;
    return FULTA_ARC_OK;
}

/* ---- Implode (zip.md 6.4) ------------------------------------------------------------------------------------ */

typedef struct sf_tree {
    uint16_t sym[1u << 16];            /* indexed by the next 16 bits; 0xFFFF = no code */
    uint8_t len[1u << 16];
    int max;                           /* longest code */
} sf_tree_t;

typedef struct explode {
    sf_tree_t lit, len, dist;
    bool trees_read, big, lit3;
    uint32_t min;                      /* minimum match length (zip.md 6.4) */
} explode_t;

static fulta_arc_err_t read_tree(bits_t *b, sf_tree_t *t, unsigned nvals) {
    uint32_t x, count;
    uint8_t lens[256];
    unsigned k = 0;
    fulta_arc_err_t e = get(b, 8, &count);
    if (e) return e;
    for (uint32_t i = 0; i <= count; i++) {
        if ((e = get(b, 8, &x))) return e;
        unsigned rep = (x >> 4) + 1, l = (x & 15) + 1;
        if (k + rep > nvals) return FULTA_ARC_ERR_CORRUPT;
        for (unsigned r = 0; r < rep; r++) lens[k++] = (uint8_t)l;
    }
    if (k != nvals) return FULTA_ARC_ERR_CORRUPT;
    /* values sorted by (length, value), codes assigned from the end */
    uint16_t order[256];
    unsigned m = 0;
    for (unsigned l = 1; l <= 16; l++)
        for (unsigned v = 0; v < nvals; v++)
            if (lens[v] == l) order[m++] = (uint16_t)v;
    memset(t->sym, 0xFF, sizeof t->sym);
    t->max = 0;
    uint32_t code = 0, inc = 0, last = 0;
    for (unsigned i = m; i-- > 0;) {
        unsigned v = order[i], l = lens[v];
        code = (code + inc) & 0xFFFF;
        if (l != last) { last = l; inc = UINT32_C(1) << (16 - l); }
        /* the code's top bit comes first: reversed, it is the low bits of the next 16 read */
        uint32_t r = 0;
        for (unsigned bit = 0; bit < l; bit++) r |= ((code >> (15 - bit)) & 1) << bit;
        for (uint32_t k2 = r; k2 < (1u << 16); k2 += 1u << l) { t->sym[k2] = (uint16_t)v; t->len[k2] = (uint8_t)l; }
        if ((int)l > t->max) t->max = (int)l;
    }
    return FULTA_ARC_OK;
}

static fulta_arc_err_t read_sym(bits_t *b, const sf_tree_t *t, uint32_t *v) {
    fulta_arc_err_t e = need(b, 16);
    if (e) return e;
    uint32_t k = b->buf & 0xFFFF;
    if (t->sym[k] == 0xFFFF) return FULTA_ARC_ERR_CORRUPT;
    uint32_t dummy;
    *v = t->sym[k];
    return get(b, t->len[k], &dummy);
}

static fulta_arc_err_t explode_step(legacy_t *L) {
    explode_t *x = L->state;
    fulta_arc_err_t e;
    if (!x->trees_read) {
        if (x->lit3 && (e = read_tree(&L->b, &x->lit, 256))) return e;
        if ((e = read_tree(&L->b, &x->len, 64)) || (e = read_tree(&L->b, &x->dist, 64))) return e;
        x->trees_read = true;
    }
    uint32_t f, v, lo, hi, n;
    if ((e = get(&L->b, 1, &f))) return e;
    if (f) {
        if (x->lit3) e = read_sym(&L->b, &x->lit, &v);
        else e = get(&L->b, 8, &v);
        if (e) return e;
        put(L, (uint8_t)v);
        return FULTA_ARC_OK;
    }
    int low = x->big ? 7 : 6;
    if ((e = get(&L->b, low, &lo)) || (e = read_sym(&L->b, &x->dist, &hi)) || (e = read_sym(&L->b, &x->len, &n)))
        return e;
    if (n == 63) {
        uint32_t extra;
        if ((e = get(&L->b, 8, &extra))) return e;
        n += extra;
    }
    copy_match(L, ((hi << low) | lo) + 1, n + x->min);
    return FULTA_ARC_OK;
}

fulta_arc_err_t fa_dec_explode(fa_stream_t *in, uint16_t flags, int min_len, uint64_t out_size, fa_stream_t **out) {
    legacy_t *L;
    fulta_arc_err_t e = legacy_new(in, out_size, explode_step, sizeof(explode_t), true, &L);
    if (e) return e;
    explode_t *x = L->state;
    x->big = flags & 2;
    x->lit3 = flags & 4;
    x->min = min_len ? min_len : (x->lit3 ? 3 : 2);
    *out = &L->base;
    return FULTA_ARC_OK;
}
