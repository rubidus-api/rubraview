/*
 * Rubraview's RAR codec (MIT): the machinery the three algorithms share -- packed input, the MSB-first bit
 * reader, canonical Huffman codes, the window with its output and filters. Written from
 * docs/specs/rar-decompression.md (sections 7, 10.2, 12.5). Internal to src/core/rarcodec/.
 */
#ifndef RUBRAVIEW_RARCODEC_UNPACK_H
#define RUBRAVIEW_RARCODEC_UNPACK_H

#include "rc_internal.h"
#include "Ppmd7.h"
#include "unpack15.h"

/* ---- spec 15: the readings chosen where the sources disagree; each can be flipped here ---- */

/* #1: a PPMd block header without flag 0x40 keeps the previous escape byte (RD), not 2 (LA4). */
#define RC_PPM_KEEP_ESCAPE 1
/* #2: lowDist / lowRepeat are reset at every LZ table read (RD); 0 resets them only at a non-solid start. */
#define RC_LOWDIST_RESET_AT_TABLES 1
/* #7: a filter-program reset (vmnum 0) keeps the filters already pending. */
#define RC_VM_RESET_KEEPS_PENDING 1
/* #16: RAR 3 filters with the same start and length run one after another on the output. */
#define RC_RAR3_CHAIN_FILTERS 1
/* #17: filters pending at the end of a solid file stay for the next one. */
#define RC_KEEP_FILTERS_ACROSS_FILES 1
/* A solid file's output begins where the previous file's size ends (7.5: bytes past it are the next
   file's; RAR 2.0 stops mid-block, 8.3). 0: at whatever the previous decode had reached. */
#define RC_FILE_STARTS_AT_SIZE 1

/* ---- packed input and bits (spec 7.1) ---- */

typedef struct rc_bits {
    rubraview_rar_read_fn read;
    void *ctx;
    uint8_t *buf;
    size_t pos, len, cap;
    bool eof;
    uint64_t acc;            /* the next bits, most significant first */
    unsigned n;              /* how many of acc's top bits are real or padding */
    uint64_t used, fed;      /* bits consumed and real bits taken in, since the file's packed data began */
} rc_bits_t;

void rc_bits_start(rc_bits_t *b, rubraview_rar_read_fn read, void *ctx);
void rc_bits_fill(rc_bits_t *b);

static inline uint32_t rc_peek(rc_bits_t *b, unsigned k) {   /* 1 <= k <= 32 */
    if (b->n < k) rc_bits_fill(b);
    return (uint32_t)(b->acc >> (64 - k));
}
static inline void rc_skip(rc_bits_t *b, unsigned k) {
    b->acc <<= k;
    b->n -= k;
    b->used += k;
}
static inline uint32_t rc_get(rc_bits_t *b, unsigned k) {   /* 0 <= k <= 32 */
    if (k == 0) return 0;
    uint32_t v = rc_peek(b, k);
    rc_skip(b, k);
    return v;
}
static inline uint64_t rc_get64(rc_bits_t *b, unsigned k) {  /* up to 64 */
    if (k <= 32) return rc_get(b, k);
    uint64_t hi = rc_get(b, k - 32);
    return hi << 32 | rc_get(b, 32);
}
static inline bool rc_overrun(const rc_bits_t *b) { return b->used > b->fed; }
static inline void rc_align(rc_bits_t *b) {
    if (b->n < 8) rc_bits_fill(b);
    rc_skip(b, (unsigned)((8 - (b->used & 7)) & 7));
}
/* No real bit is left to read. */
static inline bool rc_exhausted(rc_bits_t *b) {
    if (b->n == 0 || b->used >= b->fed) rc_bits_fill(b);
    return b->used >= b->fed;
}

/* ---- canonical Huffman codes (spec 7.2) ---- */

#define RC_HUFF_QUICK 10
#define RC_HUFF_MAX 512

typedef struct rc_huff {
    uint16_t quick[1 << RC_HUFF_QUICK];   /* symbol << 4 | length, for codes up to RC_HUFF_QUICK bits; 0 none */
    uint32_t first[16];                   /* first code of each length, left-aligned to 15 bits */
    uint32_t limit[16];                   /* one past the last code of each length, left-aligned */
    uint16_t offset[16];
    uint16_t sorted[RC_HUFF_MAX];
    unsigned symbols;
} rc_huff_t;

bool rc_huff_build(rc_huff_t *h, const uint8_t *lengths, unsigned n);
int rc_huff_decode(rc_huff_t *h, rc_bits_t *b);   /* -1: a code no symbol has */

/* ---- the window and its output (spec 7.4, 7.5, 10.2, 12.5) ---- */

enum { RC_F_DELTA = 1, RC_F_E8, RC_F_E8E9, RC_F_ARM, RC_F_ITANIUM, RC_F_RGB, RC_F_AUDIO };

typedef struct rc_filter {
    uint64_t start, length;   /* in the stream's bytes */
    uint64_t file_off;        /* where it starts in its file */
    int type;
    bool rar5;
    uint32_t r[7];
} rc_filter_t;

#define RC_MAX_FILTERS 8192

typedef struct rc_unpack {
    /* window */
    uint8_t *win;
    uint64_t wsize;           /* bytes allocated; any size, not only powers of two */
    uint64_t widx;            /* pos mod wsize */
    uint64_t pos;             /* bytes decoded since the stream began */
    uint64_t flushed;         /* bytes before this one are output (or belonged to earlier files) */
    uint64_t file_start, file_end;   /* this file's bytes in the stream; file_end UINT64_MAX: unknown */
    rubraview_rar_write_fn write;
    void *write_ctx;
    bool stopped;

    /* filters */
    rc_filter_t *filters;
    size_t fcount, fcap;
    uint8_t *fbuf, *fbuf2;
    size_t fbuf_cap;

    /* which algorithm the stream is in, and whether it may continue */
    unsigned family;          /* 20, 29, 50; 0 none */
    bool alive;

    rc_bits_t bits;

    /* RAR 1.5 (unpack15.c, from docs/specs/rar15.md): its own 64 KiB window and state */
    rar15_unpacker *v15;

    /* RAR 2.0 (unpack20.c) */
    struct {
        uint8_t lengths[1028];
        rc_huff_t main, dist, len, audio[4];
        bool audio_block;
        unsigned channels, channel;
        uint32_t D[4];
        uint32_t L;
        int chan_delta;
        struct { int K[5], Dl[4], last_delta, dif[11], count, last_char; } a[4];
        bool tables_read;
    } v20;

    /* RAR 2.9 (unpack29.c) */
    struct {
        uint8_t lengths[404];
        rc_huff_t mc, dc, ldc, rc;
        uint32_t D[4];
        uint32_t L;
        uint32_t low_dist, low_repeat;
        bool need_header, ppm_block, tables_ok;
        /* PPMd */
        CPpmd7 ppmd;
        bool ppmd_constructed, ppmd_alloc, ppmd_ready;
        uint32_t ppmd_mem;
        int esc;
        bool ppm_error;
        struct { IByteIn vt; struct rc_unpack *u; } byte_in;   /* the model reads the packed bytes through this */
        /* RAR 3 filter programs (spec 10.1) */
        struct { int type; uint32_t last_length; uint32_t uses; } prog[1024];
        size_t prog_count;
        size_t last_num;
    } v29;

    /* RAR 5 (unpack50.c) */
    struct {
        rc_huff_t nc, dc, ldc, rc;
        uint64_t D[4];
        uint64_t L;
        bool tables_ok;
        uint64_t last_filter_end;
    } v50;
} rc_unpack_t;

/* Window operations. The decoders check rc_room before each symbol. */
static inline void rc_put(rc_unpack_t *u, uint8_t b) {
    u->win[u->widx] = b;
    if (++u->widx == u->wsize) u->widx = 0;
    u->pos++;
}
bool rc_copy(rc_unpack_t *u, uint64_t dist, uint64_t len);   /* false: a distance the window cannot hold */
/* Make room for another symbol: flush what can go. False when the window is full of bytes a pending
   filter holds back (corrupt data), or the writer stopped. */
bool rc_room(rc_unpack_t *u);
/* Output everything decoded so far that may go (`all`: the end of this file's stream). */
bool rc_flush(rc_unpack_t *u, bool all);
bool rc_add_filter(rc_unpack_t *u, const rc_filter_t *f);
/* Bytes of this file decoded so far. */
static inline uint64_t rc_file_pos(const rc_unpack_t *u) { return u->pos - u->file_start; }
static inline bool rc_file_done(const rc_unpack_t *u) { return u->file_end != UINT64_MAX && u->pos >= u->file_end; }

void rc_run_filter(const rc_filter_t *f, uint8_t *data, uint8_t *tmp, size_t n);   /* filters.c; result in data */

/* Spec 9.3's bases and extra bits (unpack.c). */
extern const uint8_t rc_lbase[28], rc_lbits[28], rc_dbits[60], rc_sdbase[8], rc_sdbits[8];
extern const uint32_t rc_dbase[60];

/* The algorithms: one file each, the state in `u` set up by unpack.c. */
rubraview_rar_unpack_status_t rc_unpack20(rc_unpack_t *u, bool solid, uint64_t dest);
rubraview_rar_unpack_status_t rc_unpack29(rc_unpack_t *u, bool solid, bool drain);
rubraview_rar_unpack_status_t rc_unpack50(rc_unpack_t *u, bool solid, bool drain, bool v7);
void rc_unpack29_free(rc_unpack_t *u);

/* The precode of spec 7.3; `add` sums into `lengths` (RAR 2.9) instead of assigning (RAR 5). */
bool rc_read_lengths(rc_bits_t *b, uint8_t *lengths, unsigned n, bool add);

#endif
