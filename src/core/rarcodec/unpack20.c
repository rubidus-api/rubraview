/*
 * Rubraview's RAR codec (MIT): the RAR 2.0 algorithm (unpack versions 20 and 26), LZ and audio blocks.
 * Written from docs/specs/rar-decompression.md, section 8.
 */
#include "rc_unpack.h"
#include <string.h>

typedef rubraview_rar_unpack_status_t status_t;
#define S_OK RUBRAVIEW_RAR_UNPACK_OK
#define S_BAD RUBRAVIEW_RAR_UNPACK_CORRUPT

#define MC20 298
#define DC20 48
#define RC20 28

static int iabs(int v) { return v < 0 ? -v : v; }
static int signed8(unsigned v) { v &= 0xFF; return v >= 0x80 ? (int)v - 256 : (int)v; }

/* Spec 8.2: 19 precode lengths, then n lengths added to the saved ones. */
static bool read_lengths20(rc_bits_t *b, uint8_t *len, unsigned n) {
    uint8_t bl[19];
    for (int i = 0; i < 19; ++i) bl[i] = (uint8_t)rc_get(b, 4);
    rc_huff_t pre;
    if (!rc_huff_build(&pre, bl, 19)) return false;
    for (unsigned i = 0; i < n;) {
        int s = rc_huff_decode(&pre, b);
        if (s < 0 || rc_overrun(b)) return false;
        if (s < 16) {
            len[i] = (uint8_t)((len[i] + s) & 15);
            i++;
        } else if (s == 16) {
            if (i == 0) return false;
            unsigned r = 3 + rc_get(b, 2);
            for (unsigned k = 0; k < r && i < n; ++k, ++i) len[i] = len[i - 1];
        } else {
            unsigned r = s == 17 ? 3 + rc_get(b, 3) : 11 + rc_get(b, 7);
            for (unsigned k = 0; k < r && i < n; ++k) len[i++] = 0;
        }
    }
    return !rc_overrun(b);
}

/* Spec 8.1. */
static bool block_header(rc_unpack_t *u) {
    rc_bits_t *b = &u->bits;
    bool audio = rc_get(b, 1);
    if (!rc_get(b, 1)) memset(u->v20.lengths, 0, sizeof(u->v20.lengths));
    u->v20.audio_block = audio;
    if (audio) {
        u->v20.channels = rc_get(b, 2) + 1;
        if (u->v20.channel >= u->v20.channels) u->v20.channel = 0;
        if (!read_lengths20(b, u->v20.lengths, u->v20.channels * 257)) return false;
        for (unsigned c = 0; c < u->v20.channels; ++c)
            if (!rc_huff_build(&u->v20.audio[c], u->v20.lengths + 257 * c, 257)) return false;
    } else {
        if (!read_lengths20(b, u->v20.lengths, MC20 + DC20 + RC20)) return false;
        const uint8_t *l = u->v20.lengths;
        if (!rc_huff_build(&u->v20.main, l, MC20) || !rc_huff_build(&u->v20.dist, l + MC20, DC20) ||
            !rc_huff_build(&u->v20.len, l + MC20 + DC20, RC20))
            return false;
    }
    u->v20.tables_read = true;
    return true;
}

static void push(rc_unpack_t *u, uint32_t d) {
    u->v20.D[3] = u->v20.D[2]; u->v20.D[2] = u->v20.D[1]; u->v20.D[1] = u->v20.D[0]; u->v20.D[0] = d;
}

/* Spec 8.4: one delta of the current channel. */
static uint8_t audio_byte(rc_unpack_t *u, unsigned s) {
    typeof(u->v20.a[0]) *v = &u->v20.a[u->v20.channel];
    v->count++;
    v->Dl[3] = v->Dl[2];
    v->Dl[2] = v->Dl[1];
    v->Dl[1] = v->last_delta - v->Dl[0];
    v->Dl[0] = v->last_delta;
    int cd = u->v20.chan_delta;
    int p = 8 * v->last_char + v->K[0] * v->Dl[0] + v->K[1] * v->Dl[1] + v->K[2] * v->Dl[2] + v->K[3] * v->Dl[3] + v->K[4] * cd;
    p = (p >> 3) & 0xFF;   /* arithmetic shift */
    unsigned ch = ((unsigned)p - s) & 0xFF;
    int e = signed8(s) * 8;
    v->dif[0] += iabs(e);
    v->dif[1] += iabs(e - v->Dl[0]); v->dif[2] += iabs(e + v->Dl[0]);
    v->dif[3] += iabs(e - v->Dl[1]); v->dif[4] += iabs(e + v->Dl[1]);
    v->dif[5] += iabs(e - v->Dl[2]); v->dif[6] += iabs(e + v->Dl[2]);
    v->dif[7] += iabs(e - v->Dl[3]); v->dif[8] += iabs(e + v->Dl[3]);
    v->dif[9] += iabs(e - cd); v->dif[10] += iabs(e + cd);
    u->v20.chan_delta = signed8(ch - (unsigned)v->last_char);
    v->last_delta = u->v20.chan_delta;
    v->last_char = (int)ch;
    if ((v->count & 0x1F) == 0) {
        int j = 0;
        for (int q = 1; q < 11; ++q) if (v->dif[q] < v->dif[j]) j = q;
        for (int q = 0; q < 11; ++q) v->dif[q] = 0;
        if (j > 0) {
            int t = (j - 1) / 2;
            if (((j - 1) & 1) == 0) { if (v->K[t] >= -16) v->K[t]--; }
            else if (v->K[t] < 16) v->K[t]++;
        }
    }
    u->v20.channel = (u->v20.channel + 1) % u->v20.channels;
    return (uint8_t)ch;
}

status_t rc_unpack20(rc_unpack_t *u, bool solid, uint64_t dest) {
    rc_bits_t *b = &u->bits;
    if (dest == RUBRAVIEW_RAR_SIZE_UNKNOWN) return S_BAD;   /* no end-of-file code to stop at (8.3) */
    if (!solid) {
        memset(&u->v20, 0, sizeof(u->v20));   /* spec 15 #4: distances and length reset too */
    }
    if (!u->v20.tables_read && !rc_file_done(u) && !block_header(u)) return S_BAD;
    while (!rc_file_done(u)) {
        if (rc_overrun(b)) return S_BAD;
        if (!rc_room(u)) return u->stopped ? RUBRAVIEW_RAR_UNPACK_STOPPED : S_BAD;
        if (u->v20.audio_block) {
            int s = rc_huff_decode(&u->v20.audio[u->v20.channel], b);
            if (s < 0) return S_BAD;
            if (s == 256) {
                if (!block_header(u)) return S_BAD;
                continue;
            }
            rc_put(u, audio_byte(u, (unsigned)s));
            continue;
        }
        int s = rc_huff_decode(&u->v20.main, b);
        if (s < 0) return S_BAD;
        if (s < 256) { rc_put(u, (uint8_t)s); continue; }
        if (s == 269) {
            if (!block_header(u)) return S_BAD;
            continue;
        }
        uint32_t d, len;
        if (s == 256) {
            d = u->v20.D[0];
            push(u, d);
            len = u->v20.L;
        } else if (s < 261) {
            d = u->v20.D[s - 257];
            push(u, d);
            int l = rc_huff_decode(&u->v20.len, b);
            if (l < 0) return S_BAD;
            len = rc_lbase[l] + 2 + rc_get(b, rc_lbits[l]);
            if (d >= 0x101) len++;
            if (d >= 0x2000) len++;
            if (d >= 0x40000) len++;
            u->v20.L = len;
        } else if (s < 269) {
            unsigned j = (unsigned)s - 261;
            d = rc_sdbase[j] + 1 + rc_get(b, rc_sdbits[j]);
            push(u, d);
            len = 2;
            u->v20.L = len;
        } else {
            unsigned j = (unsigned)s - 270;
            len = rc_lbase[j] + 3 + rc_get(b, rc_lbits[j]);
            int k = rc_huff_decode(&u->v20.dist, b);
            if (k < 0) return S_BAD;
            d = rc_dbase[k] + 1 + rc_get(b, rc_dbits[k]);
            if (d >= 0x2000) len++;
            if (d >= 0x40000) len++;
            push(u, d);
            u->v20.L = len;
        }
        if (rc_overrun(b)) return S_BAD;
        if (len > 0 && !rc_copy(u, d, len)) return S_BAD;
    }
    return S_OK;
}
