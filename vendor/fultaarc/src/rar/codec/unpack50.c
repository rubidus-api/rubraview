/*
 * Rubraview's RAR codec (MIT): the RAR 5.0 algorithm and its RAR 7.0 variant (compression information
 * versions 0 and 1). Written from docs/specs/rar-decompression.md, sections 12.1 to 12.6.
 */
#include "rc_unpack.h"

#define NC 306
#define LDC 16
#define RC 44

typedef fa_rar_unpack_status_t status_t;
#define S_OK FA_RAR_UNPACK_OK
#define S_BAD FA_RAR_UNPACK_CORRUPT

/* Spec 12.2: one precode-coded list, cut into the four tables. */
static bool read_tables(rc_unpack_t *u, bool v7) {
    unsigned dc = v7 ? 80 : 64;
    uint8_t len[NC + 80 + LDC + RC];
    if (!rc_read_lengths(&u->bits, len, NC + dc + LDC + RC, false)) return false;
    return rc_huff_build(&u->v50.nc, len, NC) && rc_huff_build(&u->v50.dc, len + NC, dc) &&
           rc_huff_build(&u->v50.ldc, len + NC + dc, LDC) && rc_huff_build(&u->v50.rc, len + NC + dc + LDC, RC);
}

static uint64_t slot_to_length(rc_bits_t *b, unsigned s) {
    if (s < 8) return s + 2;
    unsigned bits = s / 4 - 1;
    return 2 + ((uint64_t)(4 | (s & 3)) << bits) + rc_get(b, bits);
}

/* Spec 12.3; 0 on a code no symbol has. */
static uint64_t read_distance(rc_unpack_t *u) {
    rc_bits_t *b = &u->bits;
    int k = rc_huff_decode(&u->v50.dc, b);
    if (k < 0) return 0;
    if (k < 4) return (uint64_t)k + 1;
    unsigned bits = (unsigned)k / 2 - 1;
    uint64_t d = 1 + ((uint64_t)(2 | (k & 1)) << bits);
    if (bits >= 4) {
        if (bits > 4) d += rc_get64(b, bits - 4) << 4;
        int low = rc_huff_decode(&u->v50.ldc, b);
        if (low < 0) return 0;
        d += (uint64_t)low;
    } else d += rc_get(b, bits);
    return d;
}

/* Spec 12.5. */
static bool read_filter(rc_unpack_t *u) {
    rc_bits_t *b = &u->bits;
    uint64_t v[2];
    for (int i = 0; i < 2; ++i) {
        unsigned c = rc_get(b, 2) + 1;
        v[i] = 0;
        for (unsigned k = 0; k < c; ++k) v[i] |= (uint64_t)rc_get(b, 8) << (8 * k);
    }
    rc_filter_t f = {0};
    f.rar5 = true;
    f.file_off = rc_file_pos(u) + v[0];
    f.start = u->pos + v[0];
    f.length = v[1];
    unsigned type = rc_get(b, 3);
    if (type == 0) { f.type = RC_F_DELTA; f.r[0] = rc_get(b, 5) + 1; }
    else if (type == 1) f.type = RC_F_E8;
    else if (type == 2) f.type = RC_F_E8E9;
    else if (type == 3) f.type = RC_F_ARM;
    else return false;
    if (f.length < 4 || f.length > 0x400000 || f.start < u->v50.last_filter_end) return false;
    u->v50.last_filter_end = f.start + f.length;
    return rc_add_filter(u, &f);
}

static void skip_to(rc_bits_t *b, uint64_t target) {
    while (b->used < target) {
        uint64_t n = target - b->used;
        unsigned k = n > 32 ? 32 : (unsigned)n;
        (void)rc_get(b, k);
    }
}

status_t rc_unpack50(rc_unpack_t *u, bool solid, bool drain, bool v7) {
    rc_bits_t *b = &u->bits;
    if (!solid) {
        for (int i = 0; i < 4; ++i) u->v50.D[i] = 0;
        u->v50.L = 0;
        u->v50.tables_ok = false;
        u->v50.last_filter_end = 0;
    }
    for (;;) {
        if (!drain && rc_file_done(u)) return S_OK;
        /* spec 12.1: the block header, at a byte boundary */
        rc_align(b);
        if (rc_exhausted(b)) return rc_file_done(u) || u->file_end == UINT64_MAX ? S_OK : S_BAD;
        unsigned flags = rc_get(b, 8), check = rc_get(b, 8);
        unsigned nsize = ((flags >> 3) & 7) + 1;
        if (nsize > 3) return S_BAD;
        uint64_t size = 0;
        unsigned sum = 0x5A ^ flags;
        for (unsigned i = 0; i < nsize; ++i) {
            unsigned byte = rc_get(b, 8);
            size |= (uint64_t)byte << (8 * i);
            sum ^= byte;
        }
        if ((sum & 0xFF) != check || size == 0 || rc_overrun(b)) return S_BAD;
        uint64_t body = b->used;
        uint64_t end = body + (size - 1) * 8 + (flags & 7) + 1;
        uint64_t next = body + size * 8;
        if (flags & 0x80) {
            if (!read_tables(u, v7)) return S_BAD;
            u->v50.tables_ok = true;
        } else if (!u->v50.tables_ok) return S_BAD;
        if (b->used > end) return S_BAD;

        while (b->used < end) {
            if (!drain && rc_file_done(u)) return S_OK;
            if (!rc_room(u)) return u->stopped ? FA_RAR_UNPACK_STOPPED : S_BAD;
            int s = rc_huff_decode(&u->v50.nc, b);
            if (s < 0) return S_BAD;
            if (s < 256) rc_put(u, (uint8_t)s);
            else if (s == 256) {
                if (!read_filter(u)) return S_BAD;
            } else if (s == 257) {
                if (u->v50.L != 0 && !rc_copy(u, u->v50.D[0], u->v50.L)) return S_BAD;
            } else if (s < 262) {
                unsigned j = (unsigned)s - 258;
                uint64_t d = u->v50.D[j];
                for (unsigned k = j; k > 0; --k) u->v50.D[k] = u->v50.D[k - 1];
                u->v50.D[0] = d;
                int ls = rc_huff_decode(&u->v50.rc, b);
                if (ls < 0) return S_BAD;
                u->v50.L = slot_to_length(b, (unsigned)ls);
                if (!rc_copy(u, d, u->v50.L)) return S_BAD;
            } else {
                uint64_t len = slot_to_length(b, (unsigned)s - 262);
                uint64_t d = read_distance(u);
                if (d == 0) return S_BAD;
                if (d > 0x100) len++;
                if (d > 0x2000) len++;
                if (d > 0x40000) len++;
                u->v50.D[3] = u->v50.D[2]; u->v50.D[2] = u->v50.D[1]; u->v50.D[1] = u->v50.D[0]; u->v50.D[0] = d;
                u->v50.L = len;
                if (!rc_copy(u, d, len)) return S_BAD;
            }
            if (b->used > end || rc_overrun(b)) return S_BAD;
        }
        if (b->used > next) return S_BAD;
        skip_to(b, next);
        if (flags & 0x40) return S_OK;   /* the last block of this file's stream */
    }
}
