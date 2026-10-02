/* RAR 1.5 decompression (unpack version 15). MIT licence (see LICENSE).
   Written from docs/specs/rar15.md (rar15-blackbox clean room); the section
   numbers in the comments are that document's. Bit order: docs/specs/
   rar-decompression.md, Conventions (most significant bit first). */
#include "unpack15.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define WIN_SIZE 0x10000u
#define WIN_MASK 0xFFFFu

/* --- 4.1 code tables: shortest length, then codes per length ------------- */

typedef struct {
    uint8_t min_len, max_len;
    uint16_t count[13]; /* by length 0..12 */
    uint16_t first[13]; /* first code of that length (canonical) */
    uint16_t base[13];  /* value of that first code */
} code_table;

enum { P0, P1, P2, P3, P4, N1, N2, NTABLES };

typedef struct { uint8_t len; uint16_t count; } len_count;

static const len_count P0_lens[] = {{4, 8}, {5, 8}, {6, 8}, {7, 9}, {12, 224}, {0, 0}};
static const len_count P1_lens[] = {{5, 4}, {6, 40}, {7, 16}, {8, 16}, {9, 4}, {11, 47}, {12, 130}, {0, 0}};
static const len_count P2_lens[] = {{5, 2}, {6, 5}, {7, 46}, {8, 64}, {9, 116}, {10, 24}, {0, 0}};
static const len_count P3_lens[] = {{6, 2}, {7, 14}, {8, 202}, {9, 33}, {10, 6}, {0, 0}};
static const len_count P4_lens[] = {{8, 255}, {9, 2}, {0, 0}};
static const len_count N1_lens[] = {{2, 2}, {3, 1}, {4, 2}, {5, 2}, {6, 4}, {7, 5}, {8, 4}, {9, 4}, {10, 8},
                                    {12, 224}, {0, 0}};
static const len_count N2_lens[] = {{3, 5}, {4, 2}, {5, 2}, {6, 4}, {7, 5}, {8, 4}, {9, 4}, {10, 8}, {11, 2},
                                    {12, 220}, {0, 0}};

static code_table g_tables[NTABLES];
static bool g_tables_ready;

static void build_table(code_table *t, const len_count *lc) {
    memset(t, 0, sizeof(*t));
    t->min_len = lc[0].len;
    uint32_t code = 0, value = 0;
    uint8_t prev = lc[0].len;
    for (size_t i = 0; lc[i].len; i++) {
        code <<= (lc[i].len - prev);
        prev = lc[i].len;
        t->count[lc[i].len] = lc[i].count;
        t->first[lc[i].len] = (uint16_t)code;
        t->base[lc[i].len] = (uint16_t)value;
        code += lc[i].count;
        value += lc[i].count;
        t->max_len = lc[i].len;
    }
}

static void init_tables(void) {
    if (g_tables_ready)
        return;
    build_table(&g_tables[P0], P0_lens);
    build_table(&g_tables[P1], P1_lens);
    build_table(&g_tables[P2], P2_lens);
    build_table(&g_tables[P3], P3_lens);
    build_table(&g_tables[P4], P4_lens);
    build_table(&g_tables[N1], N1_lens);
    build_table(&g_tables[N2], N2_lens);
    g_tables_ready = true;
}

/* --- 5 order tables ------------------------------------------------------ */

typedef struct {
    uint16_t e[256];
    uint16_t start[256];
} order_table;

/* 6.1 state */
struct rar15_unpacker {
    uint8_t window[WIN_SIZE];
    uint32_t pos;

    order_table lo, fo, go; /* literal, far, flag orders */
    uint8_t so[256];        /* short order */

    uint32_t litavg, distavg, shortavg, longavg, repavg;
    uint32_t litrun, litw, matchw, farlimit, fartoggle;
    int stored;
    uint32_t repcount;
    uint32_t old[4];
    uint32_t oldi;
    uint32_t lastdist, lastlen;
    uint32_t flags;
    int flagcnt;

    /* per call */
    const uint8_t *in;
    size_t in_size;
    uint64_t bitpos;
    int64_t left;
    uint8_t *out;
    uint64_t out_size, out_done;
};

static void regroup(order_table *o) {
    for (int k = 0; k < 256; k++)
        o->e[k] = (uint16_t)((o->e[k] & 0xFF00u) | (uint16_t)(7 - k / 32));
    memset(o->start, 0, sizeof(o->start));
    for (int w = 0; w <= 6; w++)
        o->start[w] = (uint16_t)((7 - w) * 32);
}

/* Promote(O, S, r, limit): returns the moved entry with its new weight. */
static uint32_t promote(order_table *o, uint32_t r, bool literal_limit) {
    uint32_t e, q;
    for (;;) {
        e = o->e[r];
        q = o->start[e & 0xFFu];
        o->start[e & 0xFFu]++;
        e++;
        bool over = literal_limit ? (e & 0xFFu) > 0xA1u : (e & 0xFFu) == 0;
        if (!over)
            break;
        regroup(o);
    }
    o->e[r] = o->e[q & 0xFFu];
    o->e[q & 0xFFu] = (uint16_t)e;
    return e;
}

static void reset_state(rar15_unpacker *u) {
    memset(u->window, 0, sizeof(u->window));
    u->pos = 0;
    for (int i = 0; i < 256; i++) {
        u->lo.e[i] = (uint16_t)(i << 8);
        u->fo.e[i] = (uint16_t)(i << 8);
        u->go.e[i] = (uint16_t)(((256 - i) & 0xFF) << 8);
        u->so[i] = (uint8_t)i;
    }
    memset(u->lo.start, 0, sizeof(u->lo.start));
    memset(u->go.start, 0, sizeof(u->go.start));
    regroup(&u->fo);
    u->litavg = 0x3500;
    u->distavg = u->shortavg = u->longavg = u->repavg = 0;
    u->litrun = 0;
    u->litw = u->matchw = 0x80;
    u->farlimit = 0x2001;
    u->fartoggle = 0;
    u->stored = 0;
    u->repcount = 0;
    memset(u->old, 0, sizeof(u->old));
    u->oldi = 0;
    u->lastdist = u->lastlen = 0;
    u->flags = 0;
    u->flagcnt = 0;
}

/* --- 3 bit input ---------------------------------------------------------- */

static uint32_t peek16(const rar15_unpacker *u) {
    uint64_t byte = u->bitpos >> 3;
    uint32_t v = 0;
    for (int k = 0; k < 3; k++) {
        v <<= 8;
        if (byte + (uint64_t)k < u->in_size)
            v |= u->in[byte + (uint64_t)k];
    }
    return (v >> (8 - (u->bitpos & 7))) & 0xFFFFu;
}

/* bits past the data area read as 0 (spec 3); a damaged stream is caught by FILE_CRC */
static void skip(rar15_unpacker *u, uint32_t n) {
    u->bitpos += n;
}

/* 4.1 decode, with 4.3 for the value past the last one of P0..P4 */
static uint32_t decode(rar15_unpacker *u, int table) {
    const code_table *t = &g_tables[table];
    uint32_t b = peek16(u);
    for (uint32_t len = t->min_len; len <= t->max_len; len++) {
        if (!t->count[len])
            continue;
        uint32_t code = b >> (16 - len);
        if (code >= t->first[len] && code - t->first[len] < t->count[len]) {
            uint32_t v = t->base[len] + (code - t->first[len]);
            if (v == 256 && table <= P4 && !u->stored) {
                skip(u, t->min_len); /* 4.3: shortest length, value 0 */
                return 0;
            }
            skip(u, len);
            return v;
        }
    }
    return 0; /* unreachable: every table is a complete code */
}

/* --- 11 output ------------------------------------------------------------ */

static void emit(rar15_unpacker *u, uint8_t c) {
    u->window[u->pos] = c;
    u->pos = (u->pos + 1) & WIN_MASK;
    if (u->out_done < u->out_size)
        u->out[u->out_done++] = c;
}

static void put(rar15_unpacker *u, uint8_t c) {
    emit(u, c);
    u->left -= 1;
}

static void copy(rar15_unpacker *u, uint32_t d, uint32_t len) {
    for (uint32_t i = 0; i < len; i++)
        emit(u, u->window[(u->pos - d) & WIN_MASK]);
    u->left -= len;
}

static void push_old(rar15_unpacker *u, uint32_t d, uint32_t len) {
    u->old[u->oldi] = d;
    u->oldi = (u->oldi + 1) & 3;
    u->lastlen = len;
    u->lastdist = d;
}

/* --- 7 flags -------------------------------------------------------------- */

static uint32_t read_flags(rar15_unpacker *u) {
    uint32_t r = decode(u, P2) & 0xFFu;
    uint32_t f = u->go.e[r] >> 8;
    /* the flags byte is the symbol before the move (Promote keeps it) */
    promote(&u->go, r, false);
    return f;
}

/* --- 8 literals ----------------------------------------------------------- */

static void literal(rar15_unpacker *u) {
    uint32_t b = peek16(u);
    int t = u->litavg > 0x75FF ? P4 : u->litavg > 0x5DFF ? P3 : u->litavg > 0x35FF ? P2
          : u->litavg > 0x0DFF ? P1 : P0;
    int32_t r = (int32_t)(decode(u, t) & 0xFFu);
    if (u->stored) {
        if (r == 0 && b > 0x0FFF)
            r = 256;
        r -= 1;
        if (r < 0) { /* 8.2 escape */
            b = peek16(u);
            skip(u, 1);
            if (b & 0x8000u) {
                u->litrun = 0;
                u->stored = 0;
                return;
            }
            uint32_t len = (b & 0x4000u) ? 4 : 3;
            skip(u, 1);
            uint32_t d = decode(u, P2);
            d = (d << 5) | (peek16(u) >> 11);
            skip(u, 5);
            copy(u, d, len);
            return;
        }
    } else {
        u->litrun++;
        if (u->litrun - 1 >= 16 && u->flagcnt == 0)
            u->stored = 1;
    }
    u->litavg += (uint32_t)r;
    u->litavg -= u->litavg >> 8;
    u->litw += 16;
    if (u->litw > 0xFF) {
        u->litw = 0x90;
        u->matchw >>= 1;
    }
    put(u, (uint8_t)(u->lo.e[r] >> 8));
    promote(&u->lo, (uint32_t)r, true);
}

/* --- 9 short matches ------------------------------------------------------ */

typedef struct { uint8_t code, len; } short_code; /* code left-aligned in 8 bits */

/* 9.2; entry 1 (S1) and 3 (S2) have length 3 + fartoggle */
static const short_code S1[15] = {
    {0x00, 1}, {0xA0, 3}, {0xD0, 4}, {0xE0, 4}, {0xF0, 5}, {0xF8, 6}, {0xFC, 7}, {0xFE, 8},
    {0xFF, 8}, {0xC0, 4}, {0x80, 4}, {0x90, 5}, {0x98, 6}, {0x9C, 6}, {0xB0, 4}};
static const short_code S2[15] = {
    {0x00, 2}, {0x40, 3}, {0x60, 3}, {0xA0, 3}, {0xD0, 4}, {0xE0, 4}, {0xF0, 5}, {0xF8, 6},
    {0xFC, 6}, {0xC0, 4}, {0x80, 4}, {0x90, 5}, {0x98, 6}, {0x9C, 6}, {0xB0, 4}};

static void short_match(rar15_unpacker *u) {
    u->litrun = 0;
    uint32_t b = peek16(u);
    if (u->repcount == 2) { /* 9.1 */
        skip(u, 1);
        if (b & 0x8000u) {
            copy(u, u->lastdist, u->lastlen);
            return;
        }
        b = (b << 1) & 0xFFFFu; /* repcount kept */
    }
    uint32_t c = b >> 8;
    bool use_s1 = u->shortavg < 37;
    const short_code *tab = use_s1 ? S1 : S2;
    int dyn = use_s1 ? 1 : 3;
    uint32_t k = 0, len = 0;
    for (k = 0; k < 15; k++) {
        len = tab[k].len;
        if ((int)k == dyn)
            len = 3 + u->fartoggle;
        uint32_t mask = (0xFF00u >> len) & 0xFFu;
        if (((c ^ tab[k].code) & mask) == 0)
            break;
    }
    if (k == 15)
        return; /* unreachable: the codes cover every byte */
    skip(u, len);

    if (k == 9) { /* 9.4 repeat last match */
        u->repcount++;
        copy(u, u->lastdist, u->lastlen);
        return;
    }
    if (k == 14) { /* 9.5 far match */
        u->repcount = 0;
        uint32_t ml = decode(u, N2) + 5;
        uint32_t d = (peek16(u) >> 1) | 0x8000u;
        skip(u, 15);
        u->lastlen = ml;
        u->lastdist = d;
        copy(u, d, ml);
        return;
    }
    if (k >= 10) { /* 9.4 old distances */
        u->repcount = 0;
        uint32_t d = u->old[(u->oldi - (k - 9)) & 3];
        uint32_t ml = decode(u, N1) + 2;
        if (ml == 0x101 && k == 10) {
            u->fartoggle ^= 1;
            return;
        }
        ml &= 0xFF;
        if (d > 256)
            ml++;
        if (d >= u->farlimit)
            ml++;
        push_old(u, d, ml);
        copy(u, d, ml);
        return;
    }
    /* 9.3 new short match */
    u->repcount = 0;
    u->shortavg += k;
    u->shortavg -= u->shortavg >> 4;
    uint32_t r = decode(u, P2) & 0xFFu;
    uint32_t d = u->so[r];
    if (r > 0) {
        u->so[r] = u->so[r - 1];
        u->so[r - 1] = (uint8_t)d;
    }
    uint32_t ml = k + 2;
    d += 1;
    push_old(u, d, ml);
    copy(u, d, ml);
}

/* --- 10 long matches ------------------------------------------------------ */

static void long_match(rar15_unpacker *u) {
    u->litrun = 0;
    u->matchw += 16;
    if (u->matchw > 0xFF) {
        u->matchw = 0x90;
        u->litw >>= 1;
    }
    uint32_t oldlong = u->longavg;
    uint32_t b = peek16(u);
    uint32_t len;
    if (u->longavg >= 122)
        len = decode(u, N2);
    else if (u->longavg >= 64)
        len = decode(u, N1);
    else if (b < 0x100) {
        len = b;
        skip(u, 16);
    } else {
        len = 0;
        while (!((b << len) & 0x8000u))
            len++;
        skip(u, len + 1);
    }
    u->longavg += len;
    u->longavg -= u->longavg >> 5;

    int t = u->distavg > 0x28FF ? P2 : u->distavg > 0x06FF ? P1 : P0;
    uint32_t r = decode(u, t);
    u->distavg += r;
    u->distavg -= u->distavg >> 8;
    uint32_t e = promote(&u->fo, r & 0xFFu, false);
    uint32_t d = ((e & 0xFF00u) | (peek16(u) >> 8)) >> 1;
    skip(u, 7);

    uint32_t oldrep = u->repavg;
    if (len != 1 && len != 4) {
        if (len == 0 && d <= u->farlimit) {
            u->repavg++;
            u->repavg -= u->repavg >> 8;
        } else if (u->repavg > 0) {
            u->repavg--;
        }
    }
    len += 3;
    if (d >= u->farlimit)
        len++;
    if (d <= 256)
        len += 8;
    if (oldrep > 0xB0 || (u->litavg >= 0x2A00 && oldlong < 0x40))
        u->farlimit = 0x7F00;
    else
        u->farlimit = 0x2001;
    push_old(u, d, len);
    copy(u, d, len);
}

/* --- 7 token choice, 6.2 one file ----------------------------------------- */

static void next_flag_bit(rar15_unpacker *u) {
    if (--u->flagcnt < 0) {
        u->flags = read_flags(u);
        u->flagcnt = 7;
    }
}

static void decode_token(rar15_unpacker *u) {
    if (u->stored) {
        literal(u);
        return;
    }
    next_flag_bit(u);
    if (u->flags & 0x80u) {
        u->flags = (u->flags << 1) & 0xFFu;
        if (u->matchw > u->litw)
            long_match(u);
        else
            literal(u);
        return;
    }
    u->flags = (u->flags << 1) & 0xFFu;
    next_flag_bit(u);
    if (u->flags & 0x80u) {
        u->flags = (u->flags << 1) & 0xFFu;
        if (u->matchw > u->litw)
            literal(u);
        else
            long_match(u);
    } else {
        u->flags = (u->flags << 1) & 0xFFu;
        short_match(u);
    }
}

rar15_unpacker *rar15_new(void) {
    init_tables();
    rar15_unpacker *u = calloc(1, sizeof(*u));
    if (u)
        reset_state(u);
    return u;
}

void rar15_free(rar15_unpacker *u) {
    free(u);
}

int rar15_unpack(rar15_unpacker *u, const uint8_t *packed, size_t packed_size,
                 uint8_t *out, uint64_t unp_size, int solid) {
    if (!solid)
        reset_state(u);
    u->in = packed;
    u->in_size = packed_size;
    u->bitpos = 0;
    u->out = out;
    u->out_size = unp_size;
    u->out_done = 0;
    u->stored = 0; /* 6.3 */
    u->left = (int64_t)unp_size - 1;
    if (u->left >= 0) {
        u->flags = read_flags(u);
        u->flagcnt = 8;
    }
    while (u->left >= 0)
        decode_token(u);
    return RAR15_OK;
}
