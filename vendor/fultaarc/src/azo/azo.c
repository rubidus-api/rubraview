/* azo.c - FultaArc: AZO decoder (EGG compression method 3), as a pull stream. MIT.
 * Written from docs/specs/azo.md: the round-3 decoding model of Rubraview's azo-blackbox clean room (worked out
 * from ALZip 8.6's output alone; 18,809 of 18,809 retained pairs decode). Decoding only: the AZO compression method
 * is claimed by ESTsoft's patent KR101049699B1; FultaArc never compresses AZO. */
#include "../core/internal.h"

#define CHUNK_MAX (64u << 20)   /* an output bound per chunk (ALZip writes at most 512 KiB) */

/* ---- binary arithmetic decoder (azo.md 2) -------------------------------------------------------------------- */

typedef struct coder {
    const uint8_t *d;
    uint64_t nbits, pos;
    uint64_t low, high, value;
    bool bad;
} coder_t;

static unsigned next_bit(coder_t *c) {
    uint64_t p = c->pos++;
    if (p >= c->nbits) {
        if (p >= c->nbits + 64) c->bad = true;        /* azo.md 3: at most 64 zero bits past the end */
        return 0;
    }
    return (c->d[p >> 3] >> (7 - (p & 7))) & 1;
}

/* the next k bits (1..32), first bit highest: next_bit k times, a whole 64-bit load where the input allows */
static uint32_t take(coder_t *c, unsigned k) {
    uint64_t p = c->pos;
    if ((p >> 3) + 8 <= c->nbits >> 3) {
        const uint8_t *q = c->d + (p >> 3);
        uint64_t w = (uint64_t)q[0] << 56 | (uint64_t)q[1] << 48 | (uint64_t)q[2] << 40 | (uint64_t)q[3] << 32 |
                     (uint64_t)q[4] << 24 | (uint64_t)q[5] << 16 | (uint64_t)q[6] << 8 | q[7];
        c->pos = p + k;
        return (uint32_t)((w << (p & 7)) >> (64 - k));   /* (p & 7) + k <= 39 bits of the 64 */
    }
    uint32_t r = 0;
    for (unsigned i = 0; i < k; i++) r = (r << 1) | next_bit(c);
    return r;
}

static unsigned clz32(uint32_t x) {                    /* x != 0 */
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_clz(x);
#else
    unsigned n = 0;
    if (!(x & 0xFFFF0000u)) { n += 16; x <<= 16; }
    if (!(x & 0xFF000000u)) { n += 8; x <<= 8; }
    if (!(x & 0xF0000000u)) { n += 4; x <<= 4; }
    if (!(x & 0xC0000000u)) { n += 2; x <<= 2; }
    if (!(x & 0x80000000u)) n += 1;
    return n;
#endif
}

/* Renormalise (azo.md 2): while the interval lies in one half shift a bit out (E1: below 2^31, E2: above), and
 * while it straddles the middle within the middle half, expand that (E3: subtract 2^30, shift). Done in two steps
 * rather than one bit at a time: the k leading bits low and high agree on are shifted out together, then the run
 * of E3 steps, the m bits after the top one where low has 1 and high 0, each of which drops bit 30 and keeps bit
 * 31. low, high and value stay below 2^32 and value within [low, high] (bit() and uniform() check it first). */
static void normalize_slow(coder_t *c) {
    uint32_t lo = (uint32_t)c->low, hi = (uint32_t)c->high, v = (uint32_t)c->value;
    uint32_t x = lo ^ hi;
    if (!(x & 0x80000000u)) {
        if (!x) { lo = 0; hi = 0xFFFFFFFFu; v = take(c, 32); }    /* low == high: all 32 bits out */
        else {
            unsigned k = clz32(x);
            lo <<= k;
            hi = (hi << k) | ((1u << k) - 1);
            v = (v << k) | take(c, k);
        }
    }
    if (lo & ~hi & 0x40000000u) {                    /* now low < 2^31 <= high */
        unsigned m = clz32(~((lo & ~hi) << 1));
        lo = (lo << m) & 0x7FFFFFFFu;
        hi = 0x80000000u | ((hi << m) & 0x7FFFFFFFu) | ((1u << m) - 1);
        v = (v & 0x80000000u) | ((v << m) & 0x7FFFFFFFu) | take(c, m);
    }
    c->low = lo;
    c->high = hi;
    c->value = v;
}

static inline void normalize(coder_t *c) {
    uint64_t lo = c->low, hi = c->high;
    if (((lo ^ hi) & 0x80000000u) && !(lo & ~hi & 0x40000000u)) return;   /* the usual case: nothing to shift */
    normalize_slow(c);
}

static inline unsigned bit(coder_t *c, uint32_t p, unsigned B) {
    if (c->value < c->low || c->value > c->high) { c->bad = true; return 0; }
    uint64_t zero = ((c->high - c->low + 1) >> B) * p;
    if (zero == 0 || zero > c->high - c->low) { c->bad = true; return 0; }
    unsigned b;
    if (c->value - c->low < zero) { c->high = c->low + zero - 1; b = 0; }
    else { c->low += zero; b = 1; }
    normalize(c);
    return b;
}

static uint64_t uniform(coder_t *c, unsigned n) {
    if (c->value < c->low || c->value > c->high || n > 40) { c->bad = true; return 0; }
    uint64_t w = (c->high - c->low + 1) >> n;
    if (!w) { c->bad = true; return 0; }
    uint64_t e = (c->value - c->low) / w;
    if (e >> n) { c->bad = true; return 0; }
    c->low += e * w;
    c->high = c->low + w - 1;
    normalize(c);
    return e;
}

/* ---- the model (azo.md 4-7) -------------------------------------------------------------------------------- */

typedef struct model {
    coder_t c;
    uint16_t flag[4][256];                 /* 'flag', 'history', 'short', 'repeat' contexts: B 12, s 6 */
    uint16_t t_short[2], t_long[128], t_rep[2], t_dist[128];
    uint16_t lit_local[256][256], lit_group[8][256];
    int32_t lit_counter[256];
    uint16_t len_local[128][128], len_group[8][128];
    int32_t len_counter[128];
} model_t;

static void model_init(model_t *m) {
    for (int i = 0; i < 4; i++) for (int k = 0; k < 256; k++) m->flag[i][k] = 2048;
    for (int i = 0; i < 2; i++) m->t_short[i] = m->t_rep[i] = 512;
    for (int i = 0; i < 128; i++) m->t_long[i] = m->t_dist[i] = 512;
    for (int i = 0; i < 256; i++) for (int k = 0; k < 256; k++) m->lit_local[i][k] = 512;
    for (int i = 0; i < 8; i++) for (int k = 0; k < 256; k++) m->lit_group[i][k] = 512;
    for (int i = 0; i < 128; i++) for (int k = 0; k < 128; k++) m->len_local[i][k] = 512;
    for (int i = 0; i < 8; i++) for (int k = 0; k < 128; k++) m->len_group[i][k] = 512;
    memset(m->lit_counter, 0, sizeof m->lit_counter);
    memset(m->len_counter, 0, sizeof m->len_counter);
}

static uint16_t upd(uint16_t p, unsigned b, uint32_t total, unsigned s) {
    return b == 0 ? (uint16_t)(p + ((total - p) >> s)) : (uint16_t)(p - (p >> s));
}

static unsigned flag(model_t *m, int kind, unsigned ctx) {
    uint16_t *p = &m->flag[kind][ctx & 0xFF];
    unsigned b = bit(&m->c, *p, 12);
    *p = upd(*p, b, 4096, 6);
    return b;
}

static unsigned tree(model_t *m, uint16_t *probs, unsigned depth) {
    unsigned node = 1;
    for (unsigned i = 0; i < depth; i++) {
        uint16_t *p = &probs[node - 1];
        unsigned b = bit(&m->c, *p, 10);
        *p = upd(*p, b, 1024, 4);
        node = 2 * node + b;
    }
    return node - (1u << depth);
}

/* dual trees with a selector counter (azo.md 6); `local`/`group` hold nodes 1..2^depth-1 at index node-1 */
static unsigned dual(model_t *m, uint16_t *local, uint16_t *group, int32_t *counter, unsigned depth) {
    int32_t cnt = *counter;
    const uint16_t *sel = cnt > 0 ? group : local;
    unsigned node = 1;
    uint32_t sl = 1u << 20, sg = 1u << 20;         /* both scores start at 2^20 and stay below 2^32 */
    for (unsigned i = 0; i < depth; i++) {
        uint16_t *pl = &local[node - 1], *pg = &group[node - 1];
        unsigned b = bit(&m->c, sel[node - 1], 10);
        *pl = upd(*pl, b, 1024, 4);
        *pg = upd(*pg, b, 1024, 4);
        uint32_t ql = b == 0 ? *pl : 1024u - *pl, qg = b == 0 ? *pg : 1024u - *pg;
        /* shift both together only when one would overflow the product, to keep near-equal pairs separable */
        if (sl >= (1u << 22) || sg >= (1u << 22)) { sl >>= 10; sg >>= 10; }
        sl *= ql;
        sg *= qg;
        node = 2 * node + b;
    }
    if (sg > sl && cnt < INT32_MAX) cnt++;
    else if (sl > sg && cnt > INT32_MIN) cnt--;
    *counter = cnt;
    return node - (1u << depth);
}

static uint64_t distance_from(model_t *m, unsigned g) {
    if (g < 20) return g + 1;
    unsigned n = (g - 16) >> 2, j = (g - 16) & 3;
    return 17 + 4 * ((UINT64_C(1) << n) - 1) + (uint64_t)j * (UINT64_C(1) << n) + uniform(&m->c, n);
}

static unsigned clz64(uint64_t x) {                    /* x != 0 */
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_clzll(x);
#else
    unsigned n = 0;
    if (!(x & 0xFFFFFFFF00000000ull)) { n += 32; x <<= 32; }
    if (!(x & 0xFFFF000000000000ull)) { n += 16; x <<= 16; }
    if (!(x & 0xFF00000000000000ull)) { n += 8; x <<= 8; }
    if (!(x & 0xF000000000000000ull)) { n += 4; x <<= 4; }
    if (!(x & 0xC000000000000000ull)) { n += 2; x <<= 2; }
    if (!(x & 0x8000000000000000ull)) n += 1;
    return n;
#endif
}

static unsigned slot_of(uint64_t d) {
    uint64_t v = d - 1;
    if (v < 20) return (unsigned)v;
    uint64_t x = v - 12;                               /* v >= 20 => x >= 8 => bl >= 4 => n >= 1 */
    unsigned bl = 64u - clz64(x);
    unsigned n = bl - 3;
    uint64_t s = 16 + 4 * (uint64_t)n + (x >> n) - 4;
    return s > 127 ? 127 : (unsigned)s;
}

static uint64_t length_from(model_t *m, unsigned g) {
    if (g < 40) return g + 2;
    unsigned n = (g - 32) >> 3, j = (g - 32) & 7;
    return 34 + 8 * ((UINT64_C(1) << n) - 1) + (uint64_t)j * (UINT64_C(1) << n) + uniform(&m->c, n);
}

/* Decode one compressed chunk of `size` bytes into out (azo.md 5). */
static fulta_arc_err_t decode_chunk(model_t *m, const uint8_t *data, size_t n, uint8_t *out, uint32_t size) {
    model_init(m);
    m->c = (coder_t){.d = data, .nbits = (uint64_t)n * 8, .low = 0, .high = 0xFFFFFFFFu};
    m->c.value = take(&m->c, 32);
    unsigned th = 0, hh = 0, rh = 0, sh = 0;
    uint64_t dist[2] = {1, 2};
    unsigned idx[2] = {0, 1};
    /* the 128 most recent matches, most recent first: logical entry i is ring[(head + i) & 127] */
    uint32_t esrc[128], elen[128];
    unsigned head = 0;
    for (unsigned i = 0; i < 128; i++) { esrc[i] = 0; elen[i] = i + 2; }
    uint32_t pos = 0;
    while (pos < size) {
        unsigned isMatch = pos == 0 ? 0 : flag(m, 0, th);
        th = ((th << 1) | isMatch) & 0xFF;
        if (m->c.bad) return FULTA_ARC_ERR_CORRUPT;
        if (!isMatch) {
            unsigned prev = pos ? out[pos - 1] : 0;
            out[pos++] = (uint8_t)dual(m, m->lit_local[prev], m->lit_group[prev >> 5], &m->lit_counter[prev], 8);
            continue;
        }
        uint64_t d, L;
        unsigned a = flag(m, 1, hh);
        hh = ((hh << 1) | a) & 0xFF;
        if (a) {
            unsigned a1 = flag(m, 2, sh), i;
            sh = ((sh << 1) | a1) & 0xFF;
            if (a1) {
                unsigned k = tree(m, m->t_short, 1);
                i = idx[k];
                if (k == 1) { idx[1] = idx[0]; idx[0] = i; }
            } else {
                i = tree(m, m->t_long, 7);
                idx[1] = idx[0];
                idx[0] = i;
            }
            uint32_t s = esrc[(head + i) & 127], l = elen[(head + i) & 127];   /* move entry i to the front */
            for (unsigned k = i; k > 0; k--) {
                esrc[(head + k) & 127] = esrc[(head + k - 1) & 127];
                elen[(head + k) & 127] = elen[(head + k - 1) & 127];
            }
            esrc[head] = s;
            elen[head] = l;
            d = pos - s;
            L = l;
            if (s > pos) return FULTA_ARC_ERR_CORRUPT;
        } else {
            unsigned S = flag(m, 3, rh), slot;
            rh = ((rh << 1) | S) & 0xFF;
            if (S) {
                unsigned k = tree(m, m->t_rep, 1);
                d = dist[k];
                if (k == 1) { dist[1] = dist[0]; dist[0] = d; }
                slot = slot_of(d);
            } else {
                slot = tree(m, m->t_dist, 7);
                d = distance_from(m, slot);
                dist[1] = dist[0];
                dist[0] = d;
            }
            L = length_from(m, dual(m, m->len_local[slot], m->len_group[slot >> 4], &m->len_counter[slot], 7));
            if (L > size - pos) return FULTA_ARC_ERR_CORRUPT;
            head = (head - 1) & 127;                                    /* push front; the oldest falls off */
            esrc[head] = pos;
            elen[head] = (uint32_t)L;
        }
        if (m->c.bad || d < 1 || d > pos || L > size - pos) return FULTA_ARC_ERR_CORRUPT;
        if (d == 1) {
            memset(out + pos, out[pos - 1], (size_t)L);
            pos += (uint32_t)L;
        } else if (d >= L) {
            memcpy(out + pos, out + pos - d, (size_t)L);
            pos += (uint32_t)L;
        } else {
            for (uint64_t k = 0; k < L; k++, pos++) out[pos] = out[pos - d];
        }
    }
    return m->c.bad ? FULTA_ARC_ERR_CORRUPT : FULTA_ARC_OK;
}

/* ---- the stream (azo.md 1) --------------------------------------------------------------------------------- */

typedef struct azo_stream {
    fa_stream_t base;
    fa_stream_t *in;
    model_t *m;
    uint8_t *out, *packed;
    size_t out_cap, packed_cap;
    uint32_t out_len, out_pos;
    bool started, ended, xform;   /* xform: the chunks' bytes went through the E8/E9 call/jump filter */
    fulta_arc_err_t err;
    proven_allocator_t ba;        /* the chunk buffers and the model, sized by the stream: the archive's budget */
    fa_budget_t *budget;          /* NULL when the caller gave no limits */
} azo_stream_t;

static fulta_arc_err_t azo_nomem(const azo_stream_t *z) {
    return z->budget && z->budget->exceeded ? FULTA_ARC_ERR_LIMIT : FULTA_ARC_ERR_NOMEM;
}

/* Undo the E8/E9 call/jump filter over one chunk's output (azo.md: positions counted from the chunk's start;
 * applied to stored and compressed chunks alike when the stream's flag bit is set). */
static void undo_calljump(uint8_t *d, uint32_t n) {
    for (uint32_t i = 0; i + 4 < n;) {
        if (d[i] == 0xE8 || d[i] == 0xE9) {
            if (d[i + 4] == 0x00 || d[i + 4] == 0xFF) {
                uint32_t val = (uint32_t)d[i + 1] | ((uint32_t)d[i + 2] << 8) | ((uint32_t)d[i + 3] << 16) |
                               ((uint32_t)d[i + 4] << 24);
                uint32_t v = val - i;
                d[i + 1] = (uint8_t)v;
                d[i + 2] = (uint8_t)(v >> 8);
                d[i + 3] = (uint8_t)(v >> 16);
                d[i + 4] = (v & (1u << 24)) ? 0xFF : 0x00;
            }
            i += 5;
        } else {
            i++;
        }
    }
}

static fulta_arc_err_t next_chunk(azo_stream_t *z) {
    if (!z->started) {
        uint8_t h[2];
        fulta_arc_err_t e = fa_stream_read_exact(z->in, fa_bytes_mut(h, 2));
        if (e) return e;
        if (h[0] != 0x31) return FULTA_ARC_ERR_UNSUPPORTED;   /* profile 0x31; the second byte is a flag (below) */
        z->xform = h[1] & 1;                                  /* bit 0: the call/jump transform; other bits ignored */
        z->started = true;
    }
    uint8_t h[12];
    fulta_arc_err_t e = fa_stream_read_exact(z->in, fa_bytes_mut(h, 12));
    if (e) return e;
    uint32_t U = fa_be32(h), C = fa_be32(h + 4), X = fa_be32(h + 8);
    if (!U && !C && !X) {
        uint8_t extra;
        size_t got = 0;
        e = z->in->read(z->in, &extra, 1, &got);
        if (e) return e;
        if (got) return FULTA_ARC_ERR_CORRUPT;    /* nothing may follow the end */
        z->ended = true;
        return FULTA_ARC_OK;
    }
    if (!U || !C || (U ^ C) != X || U > CHUNK_MAX || C > CHUNK_MAX) return FULTA_ARC_ERR_CORRUPT;
    if (U > z->out_cap) {
        fa_release(z->ba, z->out);
        z->out = NULL;
        z->out_cap = 0;
        uint8_t *p = fa_alloc(z->ba, U);
        if (!p) return azo_nomem(z);
        z->out = p;
        z->out_cap = U;
    }
    if (U == C) {
        e = fa_stream_read_exact(z->in, fa_bytes_mut(z->out, U));
        if (e) return e;
    } else {
        if (C > z->packed_cap) {
            uint8_t *p = fa_resize(z->ba, z->packed, z->packed_cap, C);
            if (!p) return azo_nomem(z);
            z->packed = p;
            z->packed_cap = C;
        }
        if ((e = fa_stream_read_exact(z->in, fa_bytes_mut(z->packed, C)))) return e;
        if (!z->m) {
            z->m = fa_alloc(z->ba, sizeof *z->m);
            if (!z->m) return azo_nomem(z);
        }
        if ((e = decode_chunk(z->m, z->packed, C, z->out, U))) return e;
    }
    if (z->xform) undo_calljump(z->out, U);
    z->out_len = U;
    z->out_pos = 0;
    return FULTA_ARC_OK;
}

static fulta_arc_err_t azo_read(fa_stream_t *s, void *buf, size_t n, size_t *got) {
    azo_stream_t *z = (azo_stream_t *)s;
    *got = 0;
    if (z->err) return z->err;
    while (*got < n) {
        if (z->out_pos == z->out_len) {
            if (z->ended) break;
            fulta_arc_err_t e = next_chunk(z);
            if (e) { z->err = e; return *got ? FULTA_ARC_OK : e; }
            continue;
        }
        size_t k = z->out_len - z->out_pos;
        if (k > n - *got) k = n - *got;
        memcpy((uint8_t *)buf + *got, z->out + z->out_pos, k);
        z->out_pos += (uint32_t)k;
        *got += k;
    }
    return FULTA_ARC_OK;
}

static void azo_destroy(fa_stream_t *s) {
    azo_stream_t *z = (azo_stream_t *)s;
    fa_stream_destroy(z->in);
    fa_release(z->ba, z->m);
    fa_release(z->ba, z->out);
    fa_release(z->ba, z->packed);
    fa_release(s->alloc, z);
}

fulta_arc_err_t fa_dec_azo(fa_stream_t *in, const fa_limits_t *lim, fa_stream_t **out) {
    proven_allocator_t a = in->alloc;
    azo_stream_t *z = fa_zalloc(a, 1, sizeof *z);
    if (!z) { fa_stream_destroy(in); return FULTA_ARC_ERR_NOMEM; }
    z->in = in;
    z->base.alloc = a;
    z->budget = lim ? lim->budget : NULL;
    z->ba = z->budget ? fa_budget_as_allocator(z->budget) : a;
    z->base.read = azo_read;
    z->base.destroy = azo_destroy;
    *out = &z->base;
    return FULTA_ARC_OK;
}
