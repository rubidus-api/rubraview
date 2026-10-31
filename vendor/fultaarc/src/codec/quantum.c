/* quantum.c - FultaArc: Cabinet Quantum (CFFOLDER typeCompress type 2) folder decoder. MIT.
 * Original FultaArc code, written in the clean room fultaarc-cleanroom-quantum-lzx from that room's own
 * specification (a black-box derivation of the format against Windows' own cabinet reader, confirmed on Microsoft-made
 * cabinets); no third-party Quantum code or description (libmspack, cabextract, 7-Zip, wimlib, ...) was read. From the
 * first #define on, the code is the room's port/quantum.c unchanged; only this header comment and the include block
 * differ (the port's own includes and its standalone-build allocator switch give way to internal.h). The format, the
 * method and how each rule was found: docs/provenance/quantum/ (format.md, algorithm.md, derivation.md,
 * provenance.md). Section references in comments ("spec: Coder") point at the room's specification, whose sections
 * algorithm.md follows: Coder = section 2, Symbols = 4-5, Adaptation = 6, Window = 7; Container = format.md.
 *
 * Summary of the format as observed:
 *  - per CFDATA block a fresh 16-bit arithmetic decoder (Witten-Neal-Cleary 1987) over an MSB-first bit reader;
 *    the adaptive tables and the output window persist across the folder's blocks;
 *  - a 7-entry selector: 3..6 = literal class (then a 64-entry class table), 2 = 3-byte copy, 1 = 4-byte copy,
 *    0 = slot-coded length (27 slots) copy; distances are slot-coded with raw extra bits taken from the same bit
 *    reader; the three copy classes have distance tables of 2*min(X,12), 2*min(X,18) and 2*X slots;
 *  - every decoded entry gets +8; when a table's total exceeds 3800 the cumulative counts are halved with a
 *    min-1 fix-up, except at the table's 4th rescale and every 50th after it (54, 104, ...), where the table is
 *    reordered by an exchange process on the ceil-halved weights and the weights are halved rounding up. */
#include "../core/internal.h"

#define QT_LIMIT      3800u   /* spec: Adaptation - rescale when the total exceeds this after an increment */
#define QT_INC        8u
#define QT_SORT_EVENT 4u      /* spec: Adaptation - the reorder happens at a table's 4th rescale ... */
#define QT_SORT_PERIOD 50u    /* ... and at every 50th rescale after it (54, 104, ...) */
#define QT_MAX        64u

typedef struct {
    unsigned n;
    uint16_t w[QT_MAX];       /* weight per position (code order) */
    uint8_t  sym[QT_MAX];     /* symbol carried by each position (identity until the 4th-rescale reorder) */
    uint32_t total;
    unsigned events;          /* rescales so far */
} qt_table;

struct fa_quantum {
    unsigned X;
    uint8_t *out; uint64_t cap, produced;
    qt_table sel, lit[4], len, dist[3];
    const uint8_t *in; size_t in_len; size_t bitpos; bool overrun;   /* bit reader of the current block */
    uint32_t low, high, code;                                        /* arithmetic decoder registers */
};

/* spec: Symbols - length slots of selector 0 in slot order s = 0..26 (table index = 26 - s) */
static const uint16_t qt_len_base[27] = { 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
                                          67, 83, 99, 115, 131, 163, 195, 227, 259 };
static const uint8_t  qt_len_bits[27] = { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
                                          4, 4, 4, 4, 5, 5, 5, 5, 0 };

static void qt_init(qt_table *t, unsigned n) {
    t->n = n; t->total = n; t->events = 0;
    for (unsigned i = 0; i < n; i++) { t->w[i] = 1; t->sym[i] = (uint8_t)i; }
}

static inline unsigned qt_bit(fa_quantum_t *q) {
    size_t i = q->bitpos >> 3;
    if (i >= q->in_len) { q->overrun = true; return 0; }
    unsigned b = (q->in[i] >> (7 - (q->bitpos & 7))) & 1u;
    q->bitpos++;
    return b;
}

static uint32_t qt_raw(fa_quantum_t *q, unsigned n) {           /* spec: Symbols - extra bits, MSB first */
    uint32_t v = 0;
    while (n--) v = (v << 1) | qt_bit(q);
    return v;
}

/* spec: Adaptation - rescale */
static void qt_rescale(qt_table *t) {
    t->events++;
    if (t->events % QT_SORT_PERIOD == QT_SORT_EVENT) {
        uint16_t k[QT_MAX];
        for (unsigned i = 0; i < t->n; i++) k[i] = (uint16_t)((t->w[i] + 1u) / 2u);
        bool swapped;
        do {                                                     /* the exchange process on the ceil-halved keys */
            swapped = false;
            for (int i = (int)t->n - 1; i >= 1; i--)
                for (int j = i - 1; j >= 0; j--)
                    if (k[j] > k[i]) {
                        uint16_t tk = k[i]; k[i] = k[j]; k[j] = tk;
                        uint16_t tw = t->w[i]; t->w[i] = t->w[j]; t->w[j] = tw;
                        uint8_t ts = t->sym[i]; t->sym[i] = t->sym[j]; t->sym[j] = ts;
                        swapped = true;
                        break;
                    }
        } while (swapped);
        t->total = 0;
        for (unsigned i = 0; i < t->n; i++) { t->w[i] = (uint16_t)((t->w[i] + 1u) / 2u); t->total += t->w[i]; }
    } else {                                                     /* cumulative halving with the min-1 fix-up */
        uint32_t cum = 0, prev = 0;
        t->total = 0;
        for (unsigned i = 0; i < t->n; i++) {
            cum += t->w[i];
            uint32_t c = cum / 2u;
            if (c < prev + 1u) c = prev + 1u;
            t->w[i] = (uint16_t)(c - prev); prev = c; t->total += t->w[i];
        }
    }
}

static inline void qt_update(qt_table *t, unsigned pos) {        /* spec: Adaptation - +8 per decoded entry */
    t->w[pos] = (uint16_t)(t->w[pos] + QT_INC); t->total += QT_INC;
    if (t->total > QT_LIMIT) qt_rescale(t);
}

/* spec: Coder - one decision in table t; returns the decoded position, or -1 on an impossible register state */
static int qt_decode(fa_quantum_t *q, qt_table *t) {
    if (q->code < q->low || q->code > q->high) return -1;
    uint32_t rng = q->high - q->low + 1u, T = t->total;
    uint32_t cum = ((q->code - q->low + 1u) * T - 1u) / rng;
    if (cum >= T) return -1;
    unsigned i = 0; uint32_t lo = 0;
    while (lo + t->w[i] <= cum) { lo += t->w[i]; i++; }
    uint32_t hi = lo + t->w[i];
    q->high = q->low + rng * hi / T - 1u;
    q->low  = q->low + rng * lo / T;
    for (;;) {
        if (q->high < 0x8000u) { }
        else if (q->low >= 0x8000u) { q->low -= 0x8000u; q->high -= 0x8000u; q->code -= 0x8000u; }
        else if (q->low >= 0x4000u && q->high < 0xC000u) { q->low -= 0x4000u; q->high -= 0x4000u; q->code -= 0x4000u; }
        else break;
        q->low = 2u * q->low; q->high = 2u * q->high + 1u; q->code = 2u * q->code + qt_bit(q);
    }
    return (int)i;
}

fulta_arc_err_t fa_quantum_create(unsigned window_bits, uint8_t *out, uint64_t out_cap, fa_quantum_t **qp) {
    *qp = NULL;
    if (window_bits < 10 || window_bits > 21) return FULTA_ARC_ERR_UNSUPPORTED;   /* spec: Container - X */
    if (!out && out_cap) return FULTA_ARC_ERR_INVALID_ARG;
    fa_quantum_t *q = fa_malloc(sizeof *q);
    if (!q) return FULTA_ARC_ERR_NOMEM;
    memset(q, 0, sizeof *q);
    q->X = window_bits; q->out = out; q->cap = out_cap; q->produced = 0;
    qt_init(&q->sel, 7);
    for (int k = 0; k < 4; k++) qt_init(&q->lit[k], 64);
    qt_init(&q->len, 27);
    qt_init(&q->dist[0], 2u * window_bits);                                    /* selector 0: full window */
    qt_init(&q->dist[1], 2u * (window_bits < 18 ? window_bits : 18));          /* selector 1: 4-byte copies */
    qt_init(&q->dist[2], 2u * (window_bits < 12 ? window_bits : 12));          /* selector 2: 3-byte copies */
    *qp = q;
    return FULTA_ARC_OK;
}

uint64_t fa_quantum_produced(const fa_quantum_t *q) { return q ? q->produced : 0; }

void fa_quantum_destroy(fa_quantum_t *q) { if (q) fa_free(q); }

fulta_arc_err_t fa_quantum_chunk(fa_quantum_t *q, const uint8_t *in, size_t in_len, uint64_t uncomp) {
    if (!q || (!in && in_len)) return FULTA_ARC_ERR_INVALID_ARG;
    if (uncomp > q->cap - q->produced) return FULTA_ARC_ERR_CORRUPT;
    const uint64_t end = q->produced + uncomp;
    q->in = in; q->in_len = in_len; q->bitpos = 0; q->overrun = false;       /* spec: Container - coder restart */
    q->low = 0; q->high = 0xFFFFu; q->code = qt_raw(q, 16);
    while (q->produced < end) {
        int p = qt_decode(q, &q->sel);
        if (p < 0) return FULTA_ARC_ERR_CORRUPT;
        unsigned k = q->sel.sym[p];
        qt_update(&q->sel, (unsigned)p);
        if (k >= 3) {                                                            /* literal class k */
            qt_table *t = &q->lit[k - 3];
            p = qt_decode(q, t);
            if (p < 0) return FULTA_ARC_ERR_CORRUPT;
            unsigned idx = t->sym[p];
            qt_update(t, (unsigned)p);
            q->out[q->produced++] = (uint8_t)(((6u - k) << 6) | (63u - idx));  /* spec: Symbols - literal */
        } else {
            uint32_t length, d;
            if (k == 2) length = 3;
            else if (k == 1) length = 4;
            else {                                                               /* slot-coded length */
                p = qt_decode(q, &q->len);
                if (p < 0) return FULTA_ARC_ERR_CORRUPT;
                unsigned slot = 26u - q->len.sym[p];
                qt_update(&q->len, (unsigned)p);
                length = qt_len_base[slot] + qt_raw(q, qt_len_bits[slot]);
            }
            qt_table *t = &q->dist[k];
            p = qt_decode(q, t);
            if (p < 0) return FULTA_ARC_ERR_CORRUPT;
            unsigned slot = t->n - 1u - t->sym[p];                               /* index 0 = farthest slot */
            qt_update(t, (unsigned)p);
            if (slot <= 3) d = slot + 1u;
            else {
                unsigned e = (slot - 4u) / 2u + 1u;
                d = (1u << (e + 1)) + 1u + (((slot - 4u) & 1u) << e) + qt_raw(q, e);
            }
            if (length > end - q->produced) return FULTA_ARC_ERR_CORRUPT;        /* H1: no crossing of cbUncomp */
            if (d > q->produced) return FULTA_ARC_ERR_CORRUPT;                   /* spec: Window - policy */
            for (uint32_t i = 0; i < length; i++, q->produced++) q->out[q->produced] = q->out[q->produced - d];
        }
        /* spec: Coder - a block that needs a bit beyond its last byte is truncated (Q0048: Windows rejects a body
         * one byte shorter than the 16-bit register model reads, including the renormalisation after the last
         * symbol); the final symbol's output is still complete when produced == end, but the block is rejected. */
        if (q->overrun) return FULTA_ARC_ERR_TRUNCATED;
    }
    return FULTA_ARC_OK;
}
