/*
 * Rubraview's RAR codec (MIT): what the three algorithms share -- bit input (spec 7.1), canonical Huffman
 * codes and the precode (7.2, 7.3), the window, its output and the filters in it (7.4, 7.5, 10.2, 12.5) --
 * and the entry point that picks the algorithm (2.4, 3.5). Written from docs/specs/rar-decompression.md.
 */
#include "rc_unpack.h"
#include <stdlib.h>
#include <string.h>

/* ---- bits ---- */

#define IN_CAP 65536

void rc_bits_start(rc_bits_t *b, fa_rar_read_fn read, void *ctx) {
    uint8_t *buf = b->buf;
    *b = (rc_bits_t){0};
    b->buf = buf;
    b->cap = IN_CAP;
    b->read = read;
    b->ctx = ctx;
}

void rc_bits_fill(rc_bits_t *b) {
    while (b->n <= 56) {
        uint8_t byte = 0;
        if (b->pos == b->len && !b->eof) {
            b->len = b->read(b->ctx, b->buf, b->cap);
            b->pos = 0;
            if (b->len == 0) b->eof = true;
        }
        if (b->pos < b->len) {
            byte = b->buf[b->pos++];
            b->fed += 8;
        }
        b->acc |= (uint64_t)byte << (56 - b->n);   /* past the end: zeros, for peeking only */
        b->n += 8;
    }
}

/* ---- Huffman ---- */

bool rc_huff_build(rc_huff_t *h, const uint8_t *lengths, unsigned n) {
    unsigned count[16] = {0};
    if (n > RC_HUFF_MAX) return false;
    for (unsigned i = 0; i < n; ++i) {
        if (lengths[i] > 15) return false;
        count[lengths[i]]++;
    }
    memset(h->quick, 0, sizeof(h->quick));
    uint32_t code = 0;
    unsigned off = 0;
    h->first[0] = h->limit[0] = 0;
    for (unsigned l = 1; l <= 15; ++l) {
        if (code + count[l] > (1u << l)) return false;   /* over-subscribed */
        h->first[l] = code << (15 - l);
        h->limit[l] = (code + count[l]) << (15 - l);
        h->offset[l] = (uint16_t)off;
        off += count[l];
        code = (code + count[l]) << 1;
    }
    unsigned at[16];
    for (unsigned l = 1; l <= 15; ++l) at[l] = h->offset[l];
    for (unsigned i = 0; i < n; ++i) {
        unsigned l = lengths[i];
        if (l == 0) continue;
        unsigned k = at[l]++;
        h->sorted[k] = (uint16_t)i;
        if (l <= RC_HUFF_QUICK) {
            uint32_t c = (h->first[l] >> (15 - l)) + (k - h->offset[l]);
            uint32_t from = c << (RC_HUFF_QUICK - l), span = 1u << (RC_HUFF_QUICK - l);
            for (uint32_t q = 0; q < span; ++q) h->quick[from + q] = (uint16_t)(i << 4 | l);
        }
    }
    h->symbols = n;
    return true;
}

int rc_huff_decode(rc_huff_t *h, rc_bits_t *b) {
    uint32_t v = rc_peek(b, 15);
    uint16_t q = h->quick[v >> (15 - RC_HUFF_QUICK)];
    if (q) {
        rc_skip(b, q & 15);
        return q >> 4;
    }
    for (unsigned l = RC_HUFF_QUICK + 1; l <= 15; ++l) {
        if (v < h->limit[l]) {
            if (v < h->first[l]) return -1;
            rc_skip(b, l);
            return h->sorted[h->offset[l] + ((v - h->first[l]) >> (15 - l))];
        }
    }
    return -1;   /* code space no symbol has (spec 7.2, 15 #11) */
}

/* Spec 7.3. */
bool rc_read_lengths(rc_bits_t *b, uint8_t *lengths, unsigned n, bool add) {
    uint8_t bl[20];
    for (unsigned i = 0; i < 20;) {
        unsigned v = rc_get(b, 4);
        if (v == 15) {
            unsigned z = rc_get(b, 4);
            if (z == 0) bl[i++] = 15;
            else for (unsigned k = 0; k < z + 2 && i < 20; ++k) bl[i++] = 0;
        } else bl[i++] = (uint8_t)v;
    }
    rc_huff_t pre;
    if (!rc_huff_build(&pre, bl, 20)) return false;
    for (unsigned i = 0; i < n;) {
        int s = rc_huff_decode(&pre, b);
        if (s < 0 || rc_overrun(b)) return false;
        if (s < 16) {
            lengths[i] = add ? (uint8_t)((lengths[i] + s) & 15) : (uint8_t)s;
            i++;
        } else if (s == 16 || s == 17) {
            if (i == 0) return false;
            unsigned r = s == 16 ? 3 + rc_get(b, 3) : 11 + rc_get(b, 7);
            for (unsigned k = 0; k < r && i < n; ++k, ++i) lengths[i] = lengths[i - 1];
        } else {
            unsigned r = s == 18 ? 3 + rc_get(b, 3) : 11 + rc_get(b, 7);
            for (unsigned k = 0; k < r && i < n; ++k) lengths[i++] = 0;
        }
    }
    return !rc_overrun(b);
}

/* ---- window ---- */

#define RC_MARGIN 0x40000u   /* room kept for one symbol's worth of output */
#define RC_MIN_WINDOW 0x400000u   /* the largest filter block (RAR 5: 4 MiB) must fit unoutput */

bool rc_copy(rc_unpack_t *u, uint64_t dist, uint64_t len) {
    if (dist == 0 || dist > u->wsize) return false;
    if (dist > u->pos) {   /* before the stream's start: zeros (spec 7.4) */
        while (len > 0 && dist > u->pos) { rc_put(u, 0); len--; }
        if (len == 0) return true;
    }
    uint64_t src = u->widx >= dist ? u->widx - dist : u->widx + u->wsize - dist;
    if (dist >= len && src + len <= u->wsize && u->widx + len <= u->wsize) {
        memmove(u->win + u->widx, u->win + src, (size_t)len);
        u->widx += len;
        if (u->widx == u->wsize) u->widx = 0;
        u->pos += len;
        return true;
    }
    while (len--) {   /* byte by byte: d < L repeats a pattern */
        u->win[u->widx] = u->win[src];
        if (++src == u->wsize) src = 0;
        if (++u->widx == u->wsize) u->widx = 0;
        u->pos++;
    }
    return true;
}

static void emit(rc_unpack_t *u, const uint8_t *p, size_t n) {
    if (n == 0 || u->stopped) return;
    if (!u->write(u->write_ctx, p, n)) u->stopped = true;
}

/* Window bytes [from, to) of the stream, as they are. */
static void emit_window(rc_unpack_t *u, uint64_t from, uint64_t to) {
    if (to <= from) return;
    uint64_t back = u->pos - from;
    uint64_t i = u->widx >= back ? u->widx - back : u->widx + u->wsize - back;
    uint64_t n = to - from;
    while (n > 0 && !u->stopped) {
        uint64_t run = u->wsize - i < n ? u->wsize - i : n;
        emit(u, u->win + i, (size_t)run);
        n -= run;
        i = 0;
    }
}

static bool copy_out(rc_unpack_t *u, uint64_t from, uint8_t *dst, size_t n) {
    uint64_t back = u->pos - from;
    uint64_t i = u->widx >= back ? u->widx - back : u->widx + u->wsize - back;
    size_t done = 0;
    while (done < n) {
        size_t run = (size_t)(u->wsize - i) < n - done ? (size_t)(u->wsize - i) : n - done;
        memcpy(dst + done, u->win + i, run);
        done += run;
        i = 0;
    }
    return true;
}

bool rc_add_filter(rc_unpack_t *u, const rc_filter_t *f) {
    if (u->fcount >= RC_MAX_FILTERS) return false;
    if (u->fcount == u->fcap) {
        size_t cap = u->fcap ? u->fcap * 2 : 16;
        rc_filter_t *nf = (rc_filter_t*)realloc(u->filters, cap * sizeof(*nf));
        if (!nf) return false;
        u->filters = nf;
        u->fcap = cap;
    }
    u->filters[u->fcount++] = *f;
    return true;
}

static void drop_filters(rc_unpack_t *u, size_t n) {
    memmove(u->filters, u->filters + n, (u->fcount - n) * sizeof(rc_filter_t));
    u->fcount -= n;
}

bool rc_flush(rc_unpack_t *u, bool all) {
    (void)all;
    for (;;) {
        if (u->stopped) return false;
        uint64_t limit = u->pos < u->file_end ? u->pos : u->file_end;
        if (u->fcount > 0) {
            const rc_filter_t *f = &u->filters[0];
            if (f->start < u->flushed) return false;   /* it covers bytes already gone: broken data */
            if (f->start < limit) limit = f->start;
            if (u->flushed == f->start && u->pos >= f->start + f->length) {
                size_t n = (size_t)f->length;
                if (u->fbuf_cap < n) {
                    uint8_t *a = (uint8_t*)realloc(u->fbuf, n), *b2 = a ? (uint8_t*)realloc(u->fbuf2, n) : NULL;
                    if (a) u->fbuf = a;
                    if (b2) u->fbuf2 = b2;
                    if (!a || !b2) return false;
                    u->fbuf_cap = n;
                }
                copy_out(u, f->start, u->fbuf, n);
                rc_run_filter(f, u->fbuf, u->fbuf2, n);
                size_t used = 1;
#if RC_RAR3_CHAIN_FILTERS
                while (used < u->fcount && !f->rar5 && u->filters[used].start == f->start && u->filters[used].length == f->length) {
                    rc_run_filter(&u->filters[used], u->fbuf, u->fbuf2, n);
                    used++;
                }
#endif
                uint64_t end = f->start + f->length;
                size_t out = n;
                if (u->file_end != UINT64_MAX && end > u->file_end) out = (size_t)(u->file_end - f->start);
                emit(u, u->fbuf, out);
                u->flushed = end;
                drop_filters(u, used);
                if (u->fcount > 0 && u->filters[0].start < end) return false;   /* overlapping filters */
                continue;
            }
        }
        if (limit > u->flushed) {
            emit_window(u, u->flushed, limit);
            u->flushed = limit;
            if (u->fcount > 0 && u->flushed == u->filters[0].start) continue;   /* the filter may be ready */
        }
        return !u->stopped;
    }
}

bool rc_room(rc_unpack_t *u) {
    if (u->pos - u->flushed + RC_MARGIN <= u->wsize) return true;
    if (!rc_flush(u, false)) return false;
    /* Bytes past this file's end wait for the next file; more than a window of them is broken data. */
    return u->pos - u->flushed + RC_MARGIN <= u->wsize;
}

/* Spec 9.3: shared by RAR 2.0 and 2.9. */
const uint8_t rc_lbase[28] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 20, 24, 28, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224 };
const uint8_t rc_lbits[28] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5 };
const uint32_t rc_dbase[60] = {
    0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072,
    4096, 6144, 8192, 12288, 16384, 24576, 32768, 49152, 65536, 98304, 131072, 196608,
    262144, 327680, 393216, 458752, 524288, 589824, 655360, 720896, 786432, 851968, 917504, 983040,
    1048576, 1310720, 1572864, 1835008, 2097152, 2359296, 2621440, 2883584, 3145728, 3407872, 3670016, 3932160 };
const uint8_t rc_dbits[60] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14,
    15, 15, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18 };
const uint8_t rc_sdbase[8] = { 0, 4, 8, 16, 32, 64, 128, 192 };
const uint8_t rc_sdbits[8] = { 2, 2, 3, 4, 5, 6, 6, 6 };

/* ---- the codec's entry points ---- */

void *rc_unpack_create(void) {
    rc_unpack_t *u = (rc_unpack_t*)calloc(1, sizeof(rc_unpack_t));
    if (!u) return NULL;
    u->bits.buf = (uint8_t*)malloc(IN_CAP);
    if (!u->bits.buf) { free(u); return NULL; }
    return u;
}

void rc_unpack_destroy(void *p) {
    rc_unpack_t *u = (rc_unpack_t*)p;
    if (!u) return;
    rc_unpack29_free(u);
    free(u->win);
    free(u->filters);
    free(u->fbuf);
    free(u->fbuf2);
    free(u->bits.buf);
    free(u);
}

/* RAR 1.5 (unpack version 15) is not part of FultaArc yet: the earlier black-box decoder is being redone from scratch
   by the rar15-scratch clean room (FultaArc D-004) and is connected when it is delivered. */

fa_rar_unpack_status_t rc_unpack_file(void *p, const fa_rar_unpack_params_t *prm,
                                             fa_rar_read_fn read, void *read_ctx,
                                             fa_rar_write_fn write, void *write_ctx) {
    rc_unpack_t *u = (rc_unpack_t*)p;
    if (!u || !prm || !read || !write) return FA_RAR_UNPACK_CORRUPT;
    unsigned family;
    switch (prm->method) {
    case 15: return FA_RAR_UNPACK_UNSUPPORTED;   /* RAR 1.5: D-004 */
    case 20: case 26: family = 20; break;
    case 29: family = 29; break;
    case 50: case 70: family = 50; break;
    default: return FA_RAR_UNPACK_UNSUPPORTED;   /* the unknown */
    }
    if (prm->dict_size > ((uint64_t)1 << 36)) return FA_RAR_UNPACK_TOO_LARGE;
    bool solid = prm->solid;
    if (solid && (!u->alive || u->family != family)) return FA_RAR_UNPACK_CORRUPT;   /* spec 7.5 */

    uint64_t need = (prm->dict_size > RC_MIN_WINDOW ? prm->dict_size : RC_MIN_WINDOW) + 2 * (uint64_t)RC_MARGIN;
    if (need > SIZE_MAX) return FA_RAR_UNPACK_TOO_LARGE;
    if (solid && need > u->wsize) return FA_RAR_UNPACK_CORRUPT;   /* a solid file keeps the window */
    if (!u->win || u->wsize < need) {
        free(u->win);
        u->wsize = 0;
        u->win = (uint8_t*)malloc((size_t)need);
        if (!u->win) { u->alive = false; return FA_RAR_UNPACK_NO_MEMORY; }
        u->wsize = need;
    }
    if (!solid) {
        u->pos = u->widx = u->flushed = u->file_start = 0;
        u->fcount = 0;
    } else {
#if RC_FILE_STARTS_AT_SIZE
        u->file_start = u->file_end != UINT64_MAX ? u->file_end : u->pos;
#else
        u->file_start = u->pos;
#endif
        if (u->flushed < u->file_start) u->flushed = u->file_start;
#if !RC_KEEP_FILTERS_ACROSS_FILES
        u->fcount = 0;
#endif
    }
    u->file_end = prm->dest_size == FA_RAR_SIZE_UNKNOWN ? UINT64_MAX : u->file_start + prm->dest_size;
    if (u->file_end < u->file_start) return FA_RAR_UNPACK_CORRUPT;
    u->write = write;
    u->write_ctx = write_ctx;
    u->stopped = false;
    u->alive = false;
    rc_bits_start(&u->bits, read, read_ctx);

    fa_rar_unpack_status_t st;
    if (family == 20) st = rc_unpack20(u, solid, prm->dest_size);
    else if (family == 29) st = rc_unpack29(u, solid, prm->drain);
    else st = rc_unpack50(u, solid, prm->drain, prm->method == 70);
    u->family = family;

    if (st == FA_RAR_UNPACK_OK) {
        if (!rc_flush(u, true)) st = u->stopped ? FA_RAR_UNPACK_STOPPED : FA_RAR_UNPACK_CORRUPT;
        else if (u->file_end != UINT64_MAX && u->flushed < u->file_end) st = FA_RAR_UNPACK_CORRUPT;   /* short */
    }
    if (u->stopped) st = FA_RAR_UNPACK_STOPPED;
    if (u->file_end == UINT64_MAX) u->file_end = u->pos;
    u->alive = st == FA_RAR_UNPACK_OK;
    return st;
}
