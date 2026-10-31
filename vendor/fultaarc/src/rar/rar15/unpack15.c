/* unpack15.c - decompressor for the RAR 1.5 compressed stream, written from docs/spec/rar15.md.
 * SPDX-License-Identifier: MIT (full text in unpack15.h)
 * Section numbers (2.x) refer to docs/spec/rar15.md. */
#include "unpack15.h"

#include <string.h>

/* ---- canonical codes (2.2) ------------------------------------------------------------------------------------ */

typedef struct code {
    uint8_t maxlen;
    uint16_t count[13];     /* codes per length 1..12 */
} code;

static const code T0 = {12, {[5] = 4, [6] = 40, [7] = 16, [8] = 16, [9] = 4, [11] = 47, [12] = 130}};
static const code T1 = {10, {[5] = 2, [6] = 5, [7] = 46, [8] = 64, [9] = 116, [10] = 24}};
static const code T2 = {10, {[6] = 2, [7] = 14, [8] = 202, [9] = 33, [10] = 6}};
static const code T3 = {9, {[8] = 255, [9] = 2}};
static const code TL = {12, {[4] = 8, [5] = 8, [6] = 8, [7] = 9, [12] = 224}};
static const code CR = {12, {[2] = 2, [3] = 1, [4] = 2, [5] = 2, [6] = 4, [7] = 5, [8] = 4, [9] = 4, [10] = 8,
                             [12] = 224}};                                  /* length code R */
static const code CC = {12, {[3] = 5, [4] = 2, [5] = 2, [6] = 4, [7] = 5, [8] = 4, [9] = 4, [10] = 8, [11] = 2,
                             [12] = 220}};                                  /* length code C */
/* Q layouts (2.9) as canonical codes; symbols in index order below */
static const code QA = {8, {[1] = 1, [3] = 2, [4] = 3, [5] = 1, [6] = 1, [7] = 1, [8] = 2}};
static const code QB = {6, {[2] = 1, [3] = 4, [4] = 3, [5] = 1, [6] = 2}};
/* after the far escape (2.11): '101' splits into '1010' (the same length) and '1011' (far match) */
static const code QAF = {8, {[1] = 1, [3] = 1, [4] = 5, [5] = 1, [6] = 1, [7] = 1, [8] = 2}};
static const code QBF = {6, {[2] = 1, [3] = 3, [4] = 5, [5] = 1, [6] = 2}};
enum { Q_R = 100, Q_REP = 101, Q_FAR = 102 };
static const uint8_t QA_SYM[11] = {2, Q_R, 3, Q_REP, 4, 5, 6, 7, 8, 9, 10};
static const uint8_t QB_SYM[11] = {2, 3, 4, Q_R, 5, Q_REP, 6, 7, 8, 9, 10};
static const uint8_t QAF_SYM[12] = {2, Q_R, 3, Q_FAR, Q_REP, 4, 5, 6, 7, 8, 9, 10};
static const uint8_t QBF_SYM[12] = {2, 3, 4, Q_R, 5, Q_FAR, Q_REP, 6, 7, 8, 9, 10};

typedef struct bits {
    const uint8_t *p;
    size_t len;
    size_t pos;             /* in bits */
} bits;

static int get_bit(bits *b, uint32_t *v) {
    size_t i = b->pos >> 3;
    if (i >= b->len)
        return RAR15_E_TRUNCATED;
    *v = (uint32_t)(b->p[i] >> (7 - (b->pos & 7))) & 1u;
    b->pos++;
    return RAR15_OK;
}

static int get_bits(bits *b, unsigned n, uint32_t *v) {
    uint32_t x = 0, t;
    for (unsigned k = 0; k < n; k++) {
        int e = get_bit(b, &t);
        if (e)
            return e;
        x = (x << 1) | t;
    }
    *v = x;
    return RAR15_OK;
}

/* read one code; *idx = its index in canonical order */
static int get_code(bits *b, const code *c, uint32_t *idx) {
    uint32_t v = 0, first = 0, base = 0, t;
    for (unsigned L = 1; L <= c->maxlen; L++) {
        int e = get_bit(b, &t);
        if (e)
            return e;
        v = (v << 1) | t;
        uint32_t n = c->count[L];
        if (v - first < n) {
            *idx = base + (v - first);
            return RAR15_OK;
        }
        base += n;
        first = (first + n) << 1;
    }
    return RAR15_E_UNOBSERVED;      /* not reached for the complete codes above */
}

/* x >> n rounded down, also for negative x (2.4) */
static int32_t shr(int32_t x, unsigned n) {
    return x >= 0 ? x >> n : -((-(x + 1)) >> n) - 1;
}

/* ---- count lists (2.3) ------------------------------------------------------------------------------------------ */

static void list_init(rar15_list *l, int32_t lim) {
    for (int i = 0; i < 256; i++) {
        l->sym[i] = (uint16_t)i;
        l->cnt[i] = 0;
    }
    l->lim = lim;
}

static uint32_t list_use(rar15_list *l, uint32_t r) {
    uint16_t s = l->sym[r];
    int32_t cs = ++l->cnt[s];
    for (uint32_t i = 0; i < r; i++) {
        if (l->cnt[l->sym[i]] < cs) {
            l->sym[r] = l->sym[i];
            l->sym[i] = s;
            break;
        }
    }
    if (cs > l->lim) {
        for (int i = 0; i < 256; i++)
            l->cnt[l->sym[i]] = 7 - (i >> 5);
        l->cnt[s]++;
    }
    return s;
}

/* ---- state ------------------------------------------------------------------------------------------------------ */

void rar15_init(rar15_state *st) {
    if (!st)
        return;
    memset(st, 0, sizeof *st);
    list_init(&st->lit, 161);
    list_init(&st->flag, 255);
    st->flag.sym[0] = 0xff;
    for (int i = 1; i < 256; i++)
        st->flag.sym[i] = (uint16_t)(i - 1);
    list_init(&st->block, 255);
    for (int i = 0; i < 256; i++)
        st->block.cnt[i] = 7 - (i >> 5);
    for (int i = 0; i < 256; i++)
        st->dlist[i] = (uint16_t)(i + 1);
    st->S = 53;
    st->kv = 288;                                   /* H64, H80: two units */
    st->pfar = true;
}

typedef struct job {
    rar15_state *st;
    bits b;
    uint8_t *out;
    size_t size;
    size_t done;            /* bytes of this file produced (may pass size within the last step) */
} job;

static void put(job *j, uint8_t c) {
    rar15_state *st = j->st;
    st->win[st->total & 0xffffu] = c;
    st->total++;
    if (j->done < j->size)
        j->out[j->done] = c;
    j->done++;
}

/* 2.13: d in 1..65536 */
static void copy(job *j, uint32_t n, uint32_t d) {
    rar15_state *st = j->st;
    for (uint32_t k = 0; k < n; k++) {
        uint8_t c = st->total >= d ? st->win[(st->total - d) & 0xffffu] : 0;
        put(j, c);
    }
}

static void push(rar15_state *st, uint32_t d) {
    st->recent[3] = st->recent[2];
    st->recent[2] = st->recent[1];
    st->recent[1] = st->recent[0];
    st->recent[0] = d;
}

/* 2.6: *f = the flag bit, or *f = 2 when the step switches to literal mode */
static int flag_bit(job *j, bool first, uint32_t *f) {
    rar15_state *st = j->st;
    if (st->fleft == 0) {
        if (first && st->run >= 17) {
            st->lmode = true;
            *f = 2;
            return RAR15_OK;
        }
        uint32_t i;
        int e = get_code(&j->b, &T1, &i);
        if (e)
            return e;
        if (i > 255)
            return RAR15_E_UNOBSERVED;
        st->fbyte = (uint8_t)list_use(&st->flag, i);
        st->fleft = 8;
    }
    st->fleft--;
    *f = (uint32_t)(st->fbyte >> st->fleft) & 1u;
    return RAR15_OK;
}

/* 2.7, H70: the counter's halving: floor, toward zero in the swapped state */
static int32_t half(const rar15_state *st, int32_t x) {
    return shr(st->swapped ? x + 1 : x, 1);         /* H85: the ceiling in the swapped state (H70: toward zero) */
}

/* 2.7: t_is_p = the step is a P step (else a literal) */
static void kind_step(rar15_state *st, bool t_is_p) {
    const int32_t U = 144;                          /* H80: quarter units of H64's (which had U = 36) */
    if (t_is_p == st->swapped) {                    /* kind A */
        st->nA++;
        if (st->nA % 7 == 1) {
            if (st->kpost) {
                st->kpost = false;
                st->kv = half(st, st->kv - U);
                return;
            }
            st->kv = half(st, st->kv);
        }
        st->kv -= U;
        return;
    }
    if (st->kv >= U) {                              /* H68: first check (H65: 20) */
        bool kpost0 = st->kpost;
        st->swapped = !st->swapped;
        st->kpost = false;
        if (!st->swapped) {
            st->kv = U;                             /* H68 (H65: 20) */
            if (kpost0) {                           /* H66, H79: counted when kpost is still set, as an A step */
                st->kv = st->kK;                    /* from kv = K: halving when due, then - U */
                st->nA++;
                if (st->nA % 7 == 1)
                    st->kv = half(st, st->kv);
                st->kv -= U;
            }
            return;
        }
        st->kv = 2 * U;
        st->nA++;
        if (st->nA % 7 == 1)
            st->kv = half(st, st->kv);
        st->kv -= U;
        return;
    }
    st->kv += U;
    if (st->kv >= (st->swapped ? 145 : 153)) {     /* H70, H80: second check, 145 for a swap back */
        st->swapped = !st->swapped;
        if (st->swapped)
            st->kK = st->kv;                        /* H66: the value that made the swap */
        st->kv = U;
        st->kpost = st->swapped;
    }
}

static const code *lit_table(int32_t S) {
    if (S <= -9971)
        return &TL;
    if (S >= 16758)
        return &T3;
    if (S >= 10590)
        return &T2;
    if (S >= 310)
        return &T1;
    return &T0;
}

static void literal(job *j, uint32_t r) {
    rar15_state *st = j->st;
    put(j, (uint8_t)list_use(&st->lit, r));
    st->S = st->S + (int32_t)r - 53 - shr(st->S, 8);
    kind_step(st, false);
}

/* 2.11 */
static int step_r(job *j) {
    rar15_state *st = j->st;
    uint32_t sel = 0, t, x;
    int e;
    while (sel < 3) {
        if ((e = get_bit(&j->b, &t)))
            return e;
        if (!t)
            break;
        sel++;
    }
    if ((e = get_code(&j->b, &CR, &x)))
        return e;
    if (sel == 0 && x == 255) {                     /* the far escape: no output, far code on */
        st->far = true;
        return RAR15_OK;
    }
    uint32_t n = x + 2, d = st->recent[sel];
    if (d > 256)
        n++;
    if (d > 8192 && (st->pfar || d >= 32512))       /* H73, H78: +1 beyond 8192, as for a P */
        n++;
    for (uint32_t k = 3; k > 0; k--)                /* H69: pushed again at the front, the oldest drops */
        st->recent[k] = st->recent[k - 1];
    st->recent[0] = d;
    st->last_n = n;
    st->last_d = d;
    st->has_last = true;
    copy(j, n, d ? d : 65536);
    return RAR15_OK;
}

static void repeat(job *j) {
    rar15_state *st = j->st;
    st->rep++;
    if (st->has_last)
        copy(j, st->last_n, st->last_d ? st->last_d : 65536);
}

/* 2.9 */
static int step_q(job *j) {
    rar15_state *st = j->st;
    uint32_t t, i;
    int e;
    if (st->rep >= 2) {
        if ((e = get_bit(&j->b, &t)))
            return e;
        if (t) {
            repeat(j);
            return RAR15_OK;
        }
    }
    bool layout_b = st->qs >= 39;
    const code *qc = st->far ? (layout_b ? &QBF : &QAF) : (layout_b ? &QB : &QA);
    const uint8_t *qsym = st->far ? (layout_b ? QBF_SYM : QAF_SYM) : (layout_b ? QB_SYM : QA_SYM);
    if ((e = get_code(&j->b, qc, &i)))
        return e;
    uint32_t sym = qsym[i];
    if (sym == Q_FAR) {                             /* 2.11: far match */
        uint32_t c, low;
        st->rep = 0;
        if ((e = get_code(&j->b, &CC, &c)) || (e = get_bits(&j->b, 15, &low)))
            return e;
        uint32_t n = 5 + c, d = 32768 + low;
        st->last_n = n;                             /* H71: not pushed to recent; last = this match */
        st->last_d = d;
        st->has_last = true;
        copy(j, n, d);
        return RAR15_OK;
    }
    if (sym == Q_REP) {
        repeat(j);
        return RAR15_OK;
    }
    st->rep = 0;
    if (sym == Q_R)
        return step_r(j);
    uint32_t n = sym, jx;
    if ((e = get_code(&j->b, &T1, &jx)))
        return e;
    if (jx > 255)
        return RAR15_E_UNOBSERVED;
    uint32_t d = st->dlist[jx];
    if (jx > 0) {
        st->dlist[jx] = st->dlist[jx - 1];
        st->dlist[jx - 1] = (uint16_t)d;
    }
    st->qs = st->qs + (int32_t)n - 2 - shr(st->qs, 4);
    push(st, d);
    st->last_n = n;
    st->last_d = d;
    st->has_last = true;
    copy(j, n, d);
    return RAR15_OK;
}

/* 2.10 */
static int step_p(job *j) {
    rar15_state *st = j->st;
    uint32_t n, t, p, low;
    int e;
    int layout = st->ky < 66 ? 0 : st->ky < 125 ? 1 : 2;
    if (layout == 0) {
        uint32_t z = 0;
        for (;;) {
            if ((e = get_bit(&j->b, &t)))
                return e;
            if (t)
                break;
            if (++z == 8)
                break;
        }
        if (z == 8) {
            if ((e = get_bits(&j->b, 8, &t)))
                return e;
            n = 3 + t;
        } else {
            n = 3 + z;
        }
    } else {
        if ((e = get_code(&j->b, layout == 1 ? &CR : &CC, &t)))
            return e;
        n = 3 + t;
    }
    int32_t z = (int32_t)n - 3;
    const code *tab = st->kd < 1799 ? &TL : st->kd < 10537 ? &T0 : &T1;   /* H52, H75 */
    if ((e = get_code(&j->b, tab, &p)))
        return e;
    if (p > 255)
        return RAR15_E_UNOBSERVED;
    if ((e = get_bits(&j->b, 7, &low)))
        return e;
    uint32_t blk = list_use(&st->block, p);
    st->kd = st->kd + (int32_t)p - shr(st->kd, 8);
    uint32_t d = (blk << 7) | low;
    if (d <= 256)
        n += 8;
    bool took = d > 8192 && (st->pfar || d >= 32512);   /* H53, H78 (S0285) */
    if (took)
        n += 1;
    static const int8_t hstep[6] = {-1, 0, 1, 1, 0, 1};       /* H77: balance step by length code value z */
    bool on = st->ph - 1 > -178;                    /* H81: decided from the balance before this step */
    st->ph += (z == 0 && d > (st->pfar ? 8193u : 32512u)) ? 1 : hstep[z < 5 ? z : 5];   /* H88, H89: one beyond the +1's limit */
    if (st->ph > 0)                                 /* H82: never above its start value */
        st->ph = 0;
    if (st->ph < -255)                              /* H83: never below -255 */
        st->ph = -255;
    st->pfar = on && (st->S <= -2775 || layout != 0);   /* 2.10 step 4: H53, H62, H81, H84 */
    if (d == 0)
        d = 65536;
    st->ky = st->ky + z - shr(st->ky, 5);
    kind_step(st, true);
    push(st, d);
    st->last_n = n;
    st->last_d = d;
    st->has_last = true;
    copy(j, n, d);
    return RAR15_OK;
}

/* 2.12 */
static int step_lmode(job *j) {
    rar15_state *st = j->st;
    uint32_t i, t;
    int e;
    if ((e = get_code(&j->b, lit_table(st->S), &i)))
        return e;
    if (i >= 1) {
        literal(j, i - 1);
        return RAR15_OK;
    }
    if ((e = get_bit(&j->b, &t)))
        return e;
    if (t) {
        st->lmode = false;
        st->run = 0;
        return RAR15_OK;
    }
    uint32_t b3, pos, low;
    if ((e = get_bit(&j->b, &b3)) || (e = get_code(&j->b, &T1, &pos)) || (e = get_bits(&j->b, 5, &low)))
        return e;
    uint32_t n = 3 + b3, d = (pos << 5) | low;
    copy(j, n, d ? d : 65536);                      /* H86: not `last` */
    return RAR15_OK;
}

/* 2.5 */
static int step(job *j) {
    rar15_state *st = j->st;
    if (st->lmode)
        return step_lmode(j);
    uint32_t f, k;
    int e;
    if ((e = flag_bit(j, true, &f)))
        return e;
    if (f == 2)
        return step_lmode(j);
    bool lit;
    if (f == 0) {
        lit = !st->swapped;
    } else {
        if ((e = flag_bit(j, false, &k)))
            return e;
        if (k == 1) {
            st->run = 0;
            return step_q(j);
        }
        lit = st->swapped;
    }
    if (lit) {
        uint32_t i;
        if ((e = get_code(&j->b, lit_table(st->S), &i)))
            return e;
        literal(j, i == 256 ? 0 : i);
        if (st->run < UINT32_MAX)
            st->run++;
        return RAR15_OK;
    }
    st->run = 0;
    return step_p(j);
}

int rar15_unpack(rar15_state *st, const uint8_t *in, size_t in_len, uint8_t *out, size_t size, size_t *produced) {
    if (produced)
        *produced = 0;
    if (!st || (!in && in_len) || (!out && size))
        return RAR15_E_ARG;
    job j = {st, {in, in_len, 0}, out, size, 0};
    st->fleft = 0;                                  /* 1.7: a file starts with a new flag byte */
    st->lmode = false;                              /* 1.7, H67, H74: literal mode ends, run kept below 17 */
    if (st->run > 16)
        st->run = 16;
    st->rep = 0;                                    /* H87: the count of repeats in a row starts again */
    int e = RAR15_OK;
    while (j.done < size && !(e = step(&j)))
        ;
    if (produced)
        *produced = j.done < size ? j.done : size;
    return e;
}
