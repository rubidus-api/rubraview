/* codecs.c - FultaArc: pull-stream wrappers over the vendored decoders (bzip2 1.0.8, LZMA SDK, zstd 1.5.7) and the
 * filters.
 * MIT. Formats: docs/specs/codec-bzip2.md, codec-lzma.md, codec-ppmd7.md, codec-filters.md. */
#include "../core/internal.h"

#include "bzlib.h"
#include "Bra.h"
#include "Delta.h"
#include "Lzma2Dec.h"
#include "LzmaDec.h"
#include "Ppmd7.h"
#include "zstd.h"

#include <stdlib.h>

#define INBUF 65536

static void *sz_alloc(ISzAllocPtr p, size_t n) { (void)p; return fa_malloc(n); }
static void sz_free(ISzAllocPtr p, void *a) { (void)p; fa_free(a); }
static const ISzAlloc g_alloc = {sz_alloc, sz_free};

/* bzip2 needs this when built with BZ_NO_STDIO */
void bz_internal_error(int errcode) { (void)errcode; abort(); }

/* ---- input buffer shared by the wrappers --------------------------------------------------------------------- */

typedef struct inbuf {
    fa_stream_t *src;
    uint8_t *data;
    size_t pos, len;
    bool eof;
} inbuf_t;

static fulta_arc_err_t inbuf_fill(inbuf_t *b) {
    if (b->pos < b->len || b->eof) return FULTA_ARC_OK;
    size_t got = 0;
    fulta_arc_err_t e = b->src->read(b->src, b->data, INBUF, &got);
    if (e) return e;
    b->pos = 0;
    b->len = got;
    if (!got) b->eof = true;
    return FULTA_ARC_OK;
}

static fulta_arc_err_t inbuf_init(inbuf_t *b, fa_stream_t *src) {
    b->src = src;
    b->data = fa_malloc(INBUF);
    b->pos = b->len = 0;
    b->eof = false;
    return b->data ? FULTA_ARC_OK : FULTA_ARC_ERR_NOMEM;
}

static void inbuf_free(inbuf_t *b) {
    fa_stream_destroy(b->src);
    fa_free(b->data);
}

/* ---- bzip2 (standard; one or more concatenated streams) ------------------------------------------------------ */

typedef struct bz_stream_w {
    fa_stream_t base;
    inbuf_t in;
    bz_stream bz;
    bool open, ended;
    uint64_t out_size, produced;
} bz_w_t;

static fulta_arc_err_t bz_read(fa_stream_t *s, void *buf, size_t n, size_t *got) {
    bz_w_t *z = (bz_w_t *)s;
    *got = 0;
    if (z->out_size != UINT64_MAX && n > z->out_size - z->produced) n = (size_t)(z->out_size - z->produced);
    while (*got < n) {
        if (z->ended) {
            /* another stream may follow (pbzip2-style concatenation) */
            fulta_arc_err_t e = inbuf_fill(&z->in);
            if (e) return e;
            if (z->in.pos >= z->in.len) break;
            if (z->in.data[z->in.pos] != 'B') break;
            BZ2_bzDecompressEnd(&z->bz);
            memset(&z->bz, 0, sizeof z->bz);
            if (BZ2_bzDecompressInit(&z->bz, 0, 0) != BZ_OK) return FULTA_ARC_ERR_NOMEM;
            z->ended = false;
        }
        fulta_arc_err_t e = inbuf_fill(&z->in);
        if (e) return e;
        z->bz.next_in = (char *)z->in.data + z->in.pos;
        z->bz.avail_in = (unsigned)(z->in.len - z->in.pos);
        z->bz.next_out = (char *)buf + *got;
        size_t want = n - *got;
        z->bz.avail_out = want > 0x40000000 ? 0x40000000u : (unsigned)want;
        unsigned before_in = z->bz.avail_in, before_out = z->bz.avail_out;
        int r = BZ2_bzDecompress(&z->bz);
        z->in.pos += before_in - z->bz.avail_in;
        *got += before_out - z->bz.avail_out;
        if (r == BZ_STREAM_END) { z->ended = true; continue; }
        if (r != BZ_OK) return r == BZ_MEM_ERROR ? FULTA_ARC_ERR_NOMEM : FULTA_ARC_ERR_CORRUPT;
        if (before_in == z->bz.avail_in && before_out == z->bz.avail_out) {
            if (z->in.eof) return *got ? FULTA_ARC_OK : FULTA_ARC_ERR_TRUNCATED;
        }
    }
    z->produced += *got;
    if (!*got && z->out_size != UINT64_MAX && z->produced < z->out_size) return FULTA_ARC_ERR_TRUNCATED;
    return FULTA_ARC_OK;
}

static void bz_destroy(fa_stream_t *s) {
    bz_w_t *z = (bz_w_t *)s;
    if (z->open) BZ2_bzDecompressEnd(&z->bz);
    inbuf_free(&z->in);
    fa_free(z);
}

fulta_arc_err_t fa_dec_bzip2(fa_stream_t *in, uint64_t out_size, fa_stream_t **out) {
    bz_w_t *z = fa_calloc(1, sizeof *z);
    if (!z) { fa_stream_destroy(in); return FULTA_ARC_ERR_NOMEM; }
    if (inbuf_init(&z->in, in)) { fa_free(z->in.data); fa_free(z); fa_stream_destroy(in); return FULTA_ARC_ERR_NOMEM; }
    if (BZ2_bzDecompressInit(&z->bz, 0, 0) != BZ_OK) { inbuf_free(&z->in); fa_free(z); return FULTA_ARC_ERR_NOMEM; }
    z->open = true;
    z->out_size = out_size;
    z->base.read = bz_read;
    z->base.destroy = bz_destroy;
    *out = &z->base;
    return FULTA_ARC_OK;
}

/* ---- LZMA and LZMA2 ------------------------------------------------------------------------------------------ */

typedef struct lzma_w {
    fa_stream_t base;
    inbuf_t in;
    bool is2;
    CLzmaDec dec;
    CLzma2Dec dec2;
    uint64_t out_size, produced;
    bool finished;
} lzma_w_t;

static fulta_arc_err_t lzma_read(fa_stream_t *s, void *buf, size_t n, size_t *got) {
    lzma_w_t *z = (lzma_w_t *)s;
    *got = 0;
    if (z->out_size != UINT64_MAX && n > z->out_size - z->produced) n = (size_t)(z->out_size - z->produced);
    while (*got < n && !z->finished) {
        fulta_arc_err_t e = inbuf_fill(&z->in);
        if (e) return e;
        SizeT outlen = n - *got, inlen = z->in.len - z->in.pos;
        ELzmaStatus st;
        ELzmaFinishMode fm = LZMA_FINISH_ANY;
        SRes r = z->is2 ? Lzma2Dec_DecodeToBuf(&z->dec2, (Byte *)buf + *got, &outlen, z->in.data + z->in.pos, &inlen,
                                               fm, &st)
                        : LzmaDec_DecodeToBuf(&z->dec, (Byte *)buf + *got, &outlen, z->in.data + z->in.pos, &inlen,
                                              fm, &st);
        z->in.pos += inlen;
        *got += outlen;
        if (r != SZ_OK) return r == SZ_ERROR_MEM ? FULTA_ARC_ERR_NOMEM : FULTA_ARC_ERR_CORRUPT;
        if (st == LZMA_STATUS_FINISHED_WITH_MARK) { z->finished = true; break; }
        if (inlen == 0 && outlen == 0) {
            if (z->in.eof) {
                if (z->out_size == UINT64_MAX && st == LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK) { z->finished = true; break; }
                return *got ? FULTA_ARC_OK : FULTA_ARC_ERR_TRUNCATED;
            }
            if (z->in.pos < z->in.len) return FULTA_ARC_ERR_CORRUPT;
        }
    }
    z->produced += *got;
    if (!*got && z->out_size != UINT64_MAX && z->produced < z->out_size) return FULTA_ARC_ERR_TRUNCATED;
    return FULTA_ARC_OK;
}

static void lzma_destroy(fa_stream_t *s) {
    lzma_w_t *z = (lzma_w_t *)s;
    if (z->is2) Lzma2Dec_Free(&z->dec2, &g_alloc);
    else LzmaDec_Free(&z->dec, &g_alloc);
    inbuf_free(&z->in);
    fa_free(z);
}

static uint64_t max_dict(const fa_limits_t *lim) {
    return lim && lim->max_dictionary ? lim->max_dictionary : (UINT64_C(1) << 30);
}

fulta_arc_err_t fa_dec_lzma(fa_stream_t *in, const uint8_t props[5], uint64_t out_size, const fa_limits_t *lim,
                            fa_stream_t **out) {
    uint32_t dict = fa_le32(props + 1);
    if (dict > max_dict(lim) && (out_size == UINT64_MAX || out_size > max_dict(lim))) {
        fa_stream_destroy(in);
        return FULTA_ARC_ERR_LIMIT;
    }
    lzma_w_t *z = fa_calloc(1, sizeof *z);
    if (!z || inbuf_init(&z->in, in)) {
        if (z) fa_free(z->in.data);
        fa_free(z);
        fa_stream_destroy(in);
        return FULTA_ARC_ERR_NOMEM;
    }
    /* the window need not exceed the output: allocate min(dictionary, output size) */
    uint8_t p2[5];
    memcpy(p2, props, 5);
    if (out_size != UINT64_MAX && out_size < dict) {
        uint32_t d = out_size < 4096 ? 4096u : (uint32_t)out_size;
        fa_put_le32(p2 + 1, d);
    }
    LzmaDec_CONSTRUCT(&z->dec);
    SRes r = LzmaDec_Allocate(&z->dec, p2, 5, &g_alloc);
    if (r != SZ_OK) {
        inbuf_free(&z->in);
        fa_free(z);
        return r == SZ_ERROR_MEM ? FULTA_ARC_ERR_NOMEM : FULTA_ARC_ERR_UNSUPPORTED;
    }
    LzmaDec_Init(&z->dec);
    z->out_size = out_size;
    z->base.read = lzma_read;
    z->base.destroy = lzma_destroy;
    *out = &z->base;
    return FULTA_ARC_OK;
}

fulta_arc_err_t fa_dec_lzma2(fa_stream_t *in, uint8_t prop, uint64_t out_size, const fa_limits_t *lim,
                             fa_stream_t **out) {
    if (prop > 40) { fa_stream_destroy(in); return FULTA_ARC_ERR_UNSUPPORTED; }
    uint64_t dict = prop == 40 ? 0xFFFFFFFFu : (uint64_t)(2 | (prop & 1)) << (prop / 2 + 11);
    if (dict > max_dict(lim) && (out_size == UINT64_MAX || out_size > max_dict(lim))) {
        fa_stream_destroy(in);
        return FULTA_ARC_ERR_LIMIT;
    }
    lzma_w_t *z = fa_calloc(1, sizeof *z);
    if (!z || inbuf_init(&z->in, in)) {
        if (z) fa_free(z->in.data);
        fa_free(z);
        fa_stream_destroy(in);
        return FULTA_ARC_ERR_NOMEM;
    }
    z->is2 = true;
    uint8_t p = prop;
    if (out_size != UINT64_MAX && out_size < dict) {
        /* smallest property whose dictionary still holds the whole output */
        for (p = 0; p < 40; p++)
            if (((uint64_t)(2 | (p & 1)) << (p / 2 + 11)) >= out_size) break;
    }
    Lzma2Dec_CONSTRUCT(&z->dec2);
    SRes r = Lzma2Dec_Allocate(&z->dec2, p, &g_alloc);
    if (r != SZ_OK) {
        inbuf_free(&z->in);
        fa_free(z);
        return r == SZ_ERROR_MEM ? FULTA_ARC_ERR_NOMEM : FULTA_ARC_ERR_UNSUPPORTED;
    }
    Lzma2Dec_Init(&z->dec2);
    z->out_size = out_size;
    z->base.read = lzma_read;
    z->base.destroy = lzma_destroy;
    *out = &z->base;
    return FULTA_ARC_OK;
}

/* ---- PPMd var.H with 7-Zip's range coder --------------------------------------------------------------------- */

typedef struct ppmd_w {
    fa_stream_t base;
    IByteIn vt;                 /* must stay addressable: the model reads through it */
    fa_bytes_t in;
    fa_stream_t *src;
    CPpmd7 model;
    uint64_t out_size, produced;
    bool started;
    unsigned order;
} ppmd_w_t;

static Byte ppmd_byte(IByteInPtr p) {
    ppmd_w_t *z = (ppmd_w_t *)((char *)p - offsetof(ppmd_w_t, vt));
    int c = fa_bytes_get(&z->in);
    return c < 0 ? 0 : (Byte)c;
}

static fulta_arc_err_t ppmd_read(fa_stream_t *s, void *buf, size_t n, size_t *got) {
    ppmd_w_t *z = (ppmd_w_t *)s;
    *got = 0;
    if (n > z->out_size - z->produced) n = (size_t)(z->out_size - z->produced);
    if (!n) return FULTA_ARC_OK;
    if (!z->started) {
        z->model.rc.dec.Stream = &z->vt;
        if (!Ppmd7z_RangeDec_Init(&z->model.rc.dec)) return FULTA_ARC_ERR_CORRUPT;
        Ppmd7_Init(&z->model, z->order);
        z->started = true;
    }
    uint8_t *o = buf;
    for (size_t i = 0; i < n; i++) {
        int c = Ppmd7z_DecodeSymbol(&z->model);
        if (z->in.err) return z->in.err;
        if (c < 0) return FULTA_ARC_ERR_CORRUPT;   /* an end marker before the size, or bad data */
        o[i] = (uint8_t)c;
        (*got)++;
    }
    if (z->in.err) return z->in.err;
    z->produced += *got;
    return FULTA_ARC_OK;
}

static void ppmd_destroy(fa_stream_t *s) {
    ppmd_w_t *z = (ppmd_w_t *)s;
    Ppmd7_Free(&z->model, &g_alloc);
    fa_stream_destroy(z->src);
    fa_free(z);
}

fulta_arc_err_t fa_dec_ppmd7z(fa_stream_t *in, const uint8_t *props, size_t nprops, uint64_t out_size,
                              const fa_limits_t *lim, fa_stream_t **out) {
    if (nprops != 5 || out_size == UINT64_MAX) { fa_stream_destroy(in); return FULTA_ARC_ERR_UNSUPPORTED; }
    unsigned order = props[0];
    uint32_t mem = fa_le32(props + 1);
    if (order < PPMD7_MIN_ORDER || order > PPMD7_MAX_ORDER || mem < PPMD7_MIN_MEM_SIZE || mem > PPMD7_MAX_MEM_SIZE) {
        fa_stream_destroy(in);
        return FULTA_ARC_ERR_UNSUPPORTED;
    }
    if (mem > max_dict(lim)) { fa_stream_destroy(in); return FULTA_ARC_ERR_LIMIT; }
    ppmd_w_t *z = fa_calloc(1, sizeof *z);
    if (!z) { fa_stream_destroy(in); return FULTA_ARC_ERR_NOMEM; }
    Ppmd7_Construct(&z->model);
    if (!Ppmd7_Alloc(&z->model, mem, &g_alloc)) { fa_free(z); fa_stream_destroy(in); return FULTA_ARC_ERR_NOMEM; }
    z->vt.Read = ppmd_byte;
    z->src = in;
    fa_bytes_init(&z->in, in);
    z->order = order;
    z->out_size = out_size;
    z->base.read = ppmd_read;
    z->base.destroy = ppmd_destroy;
    *out = &z->base;
    return FULTA_ARC_OK;
}

/* ---- branch converters, Delta, Swap -------------------------------------------------------------------------- */

#define FBUF 65536

typedef struct filt_w {
    fa_stream_t base;
    fa_stream_t *src;
    fa_filter_kind_t kind;
    uint32_t param;           /* Delta: distance; ARM64/RISCV: start offset */
    uint8_t buf[FBUF + 64];
    size_t start, conv, end;  /* [start, conv): converted, ready; [conv, end): not yet converted */
    bool eof;
    uint32_t pc;              /* position of buf[conv] in the stream (+ start offset) */
    UInt32 x86_state;
    Byte delta_state[DELTA_STATE_SIZE];
} filt_w_t;

static size_t convert(filt_w_t *z, uint8_t *p, size_t n) {
    Byte *e = p;
    switch (z->kind) {
    case FA_FILTER_X86: e = z7_BranchConvSt_X86_Dec(p, n, z->pc, &z->x86_state); break;
    case FA_FILTER_ARM: e = z7_BranchConv_ARM_Dec(p, n, z->pc); break;
    case FA_FILTER_ARMT: e = z7_BranchConv_ARMT_Dec(p, n, z->pc); break;
    case FA_FILTER_ARM64: e = z7_BranchConv_ARM64_Dec(p, n, z->pc); break;
    case FA_FILTER_PPC: e = z7_BranchConv_PPC_Dec(p, n, z->pc); break;
    case FA_FILTER_SPARC: e = z7_BranchConv_SPARC_Dec(p, n, z->pc); break;
    case FA_FILTER_IA64: e = z7_BranchConv_IA64_Dec(p, n, z->pc); break;
    case FA_FILTER_RISCV: e = z7_BranchConv_RISCV_Dec(p, n, z->pc); break;
    case FA_FILTER_DELTA: Delta_Decode(z->delta_state, z->param, p, n); e = p + n; break;
    case FA_FILTER_SWAP2:
    case FA_FILTER_SWAP4: {
        size_t w = z->kind == FA_FILTER_SWAP2 ? 2 : 4, k = 0;
        for (; k + w <= n; k += w)
            for (size_t a = 0, b = w - 1; a < b; a++, b--) { uint8_t t = p[k + a]; p[k + a] = p[k + b]; p[k + b] = t; }
        e = p + k;
        break;
    }
    }
    size_t done = (size_t)(e - p);
    z->pc += (uint32_t)done;
    return done;
}

static fulta_arc_err_t filt_read(fa_stream_t *s, void *buf, size_t n, size_t *got) {
    filt_w_t *z = (filt_w_t *)s;
    *got = 0;
    while (z->start == z->conv) {
        /* move the unconverted tail to the front and refill */
        size_t tail = z->end - z->conv;
        memmove(z->buf, z->buf + z->conv, tail);
        z->start = z->conv = 0;
        z->end = tail;
        while (!z->eof && z->end < FBUF) {
            size_t g = 0;
            fulta_arc_err_t e = z->src->read(z->src, z->buf + z->end, FBUF - z->end, &g);
            if (e) return e;
            if (!g) z->eof = true;
            z->end += g;
        }
        size_t c = convert(z, z->buf, z->end);
        z->conv = c;
        if (z->eof) z->conv = z->end;   /* the stream's last bytes that hold no whole instruction pass unchanged */
        if (z->conv == 0) return FULTA_ARC_OK;   /* empty */
    }
    size_t k = z->conv - z->start;
    if (k > n) k = n;
    memcpy(buf, z->buf + z->start, k);
    z->start += k;
    *got = k;
    return FULTA_ARC_OK;
}

static void filt_destroy(fa_stream_t *s) {
    filt_w_t *z = (filt_w_t *)s;
    fa_stream_destroy(z->src);
    fa_free(z);
}

fulta_arc_err_t fa_dec_filter(fa_stream_t *in, fa_filter_kind_t kind, uint32_t param, fa_stream_t **out) {
    filt_w_t *z = fa_calloc(1, sizeof *z);
    if (!z) { fa_stream_destroy(in); return FULTA_ARC_ERR_NOMEM; }
    z->src = in;
    z->kind = kind;
    z->param = param;
    if (kind == FA_FILTER_DELTA) {
        if (param < 1 || param > 256) { fa_free(z); fa_stream_destroy(in); return FULTA_ARC_ERR_UNSUPPORTED; }
        Delta_Init(z->delta_state);
    } else {
        z->pc = param;
    }
    z->x86_state = Z7_BRANCH_CONV_ST_X86_STATE_INIT_VAL;
    z->base.read = filt_read;
    z->base.destroy = filt_destroy;
    *out = &z->base;
    return FULTA_ARC_OK;
}

/* ---- BCJ2 (codec-filters.md section 3) ----------------------------------------------------------------------- */

typedef struct bcj2_w {
    fa_stream_t base;
    fa_stream_t *ins[4];
    fa_bytes_t main, rc;
    fa_bytes_t call, jump;
    uint16_t probs[2 + 256];
    uint32_t range, code;
    bool rc_started;
    uint64_t out_size, produced, ip;
    uint8_t prev;
    uint8_t pend[4];
    int npend, ipend;
} bcj2_w_t;

static fulta_arc_err_t bcj2_bit(bcj2_w_t *z, int i, int *bit) {
    if (z->range < (1u << 24)) {
        int c = fa_bytes_get(&z->rc);
        if (c < 0) { if (z->rc.err) return z->rc.err; c = 0; }
        z->range <<= 8;
        z->code = (z->code << 8) | (uint32_t)c;
    }
    uint32_t prob = z->probs[i];
    uint32_t bound = (z->range >> 11) * prob;
    if (z->code < bound) {
        z->range = bound;
        z->probs[i] = (uint16_t)(prob + ((2048 - prob) >> 5));
        *bit = 0;
    } else {
        z->range -= bound;
        z->code -= bound;
        z->probs[i] = (uint16_t)(prob - (prob >> 5));
        *bit = 1;
    }
    return FULTA_ARC_OK;
}

static fulta_arc_err_t bcj2_read(fa_stream_t *s, void *buf, size_t n, size_t *got) {
    bcj2_w_t *z = (bcj2_w_t *)s;
    uint8_t *o = buf;
    *got = 0;
    if (n > z->out_size - z->produced) n = (size_t)(z->out_size - z->produced);
    if (!z->rc_started && n) {
        int c0 = fa_bytes_get(&z->rc);
        if (c0 != 0) return z->rc.err ? z->rc.err : FULTA_ARC_ERR_CORRUPT;
        z->code = 0;
        for (int k = 0; k < 4; k++) {
            int c = fa_bytes_get(&z->rc);
            if (c < 0) return z->rc.err ? z->rc.err : FULTA_ARC_ERR_TRUNCATED;
            z->code = (z->code << 8) | (uint32_t)c;
        }
        if (z->code == 0xFFFFFFFFu) return FULTA_ARC_ERR_CORRUPT;
        z->range = 0xFFFFFFFFu;
        z->rc_started = true;
    }
    size_t k = 0;
    while (k < n) {
        if (z->ipend < z->npend) { o[k++] = z->pend[z->ipend++]; continue; }
        int c = fa_bytes_get(&z->main);
        if (c < 0) { if (z->main.err) return z->main.err; break; }
        uint8_t b = (uint8_t)c;
        o[k++] = b;
        z->ip++;
        if ((b & 0xFE) == 0xE8 || (z->prev == 0x0F && (b & 0xF0) == 0x80)) {
            int idx = b == 0xE8 ? 2 + z->prev : (b == 0xE9 ? 1 : 0), bit;
            fulta_arc_err_t e = bcj2_bit(z, idx, &bit);
            if (e) return e;
            if (bit) {
                fa_bytes_t *src = b == 0xE8 ? &z->call : &z->jump;
                uint8_t t[4];
                for (int i = 0; i < 4; i++) {
                    int v = fa_bytes_get(src);
                    if (v < 0) return src->err ? src->err : FULTA_ARC_ERR_CORRUPT;
                    t[i] = (uint8_t)v;
                }
                uint32_t dest = fa_be32(t) - (uint32_t)(z->ip + 4);
                fa_put_le32(z->pend, dest);
                z->npend = 4;
                z->ipend = 0;
                z->ip += 4;
                z->prev = (uint8_t)(dest >> 24);
                continue;
            }
        }
        z->prev = b;
    }
    z->produced += k;
    *got = k;
    return FULTA_ARC_OK;
}

static void bcj2_destroy(fa_stream_t *s) {
    bcj2_w_t *z = (bcj2_w_t *)s;
    for (int i = 0; i < 4; i++) fa_stream_destroy(z->ins[i]);
    fa_free(z);
}

fulta_arc_err_t fa_dec_bcj2(fa_stream_t *ins[4], uint64_t out_size, fa_stream_t **out) {
    bcj2_w_t *z = fa_calloc(1, sizeof *z);
    if (!z) {
        for (int i = 0; i < 4; i++) fa_stream_destroy(ins[i]);
        return FULTA_ARC_ERR_NOMEM;
    }
    for (int i = 0; i < 4; i++) z->ins[i] = ins[i];
    fa_bytes_init(&z->main, ins[0]);
    fa_bytes_init(&z->call, ins[1]);
    fa_bytes_init(&z->jump, ins[2]);
    fa_bytes_init(&z->rc, ins[3]);
    for (int i = 0; i < 2 + 256; i++) z->probs[i] = 1024;
    z->out_size = out_size;
    z->base.read = bcj2_read;
    z->base.destroy = bcj2_destroy;
    *out = &z->base;
    return FULTA_ARC_OK;
}

/* ---- PPMd var.I rev. 1 (ZIP method 98; zip.md 6.1) ----------------------------------------------------------- */

#include "Ppmd8.h"

typedef struct ppmd8_w {
    fa_stream_t base;
    IByteIn vt;
    fa_bytes_t in;
    fa_stream_t *src;
    CPpmd8 model;
    uint64_t out_size, produced;
    bool started;
    unsigned order, restore;
} ppmd8_w_t;

static Byte ppmd8_byte(IByteInPtr p) {
    ppmd8_w_t *z = (ppmd8_w_t *)((char *)p - offsetof(ppmd8_w_t, vt));
    int c = fa_bytes_get(&z->in);
    return c < 0 ? 0 : (Byte)c;
}

static fulta_arc_err_t ppmd8_read(fa_stream_t *s, void *buf, size_t n, size_t *got) {
    ppmd8_w_t *z = (ppmd8_w_t *)s;
    *got = 0;
    if (n > z->out_size - z->produced) n = (size_t)(z->out_size - z->produced);
    if (!n) return FULTA_ARC_OK;
    if (!z->started) {
        z->model.Stream.In = &z->vt;
        if (!Ppmd8_Init_RangeDec(&z->model)) return FULTA_ARC_ERR_CORRUPT;
        Ppmd8_Init(&z->model, z->order, z->restore);
        z->started = true;
    }
    uint8_t *o = buf;
    for (size_t i = 0; i < n; i++) {
        int c = Ppmd8_DecodeSymbol(&z->model);
        if (z->in.err) return z->in.err;
        if (c < 0) return FULTA_ARC_ERR_CORRUPT;
        o[i] = (uint8_t)c;
        (*got)++;
    }
    z->produced += *got;
    return FULTA_ARC_OK;
}

static void ppmd8_destroy(fa_stream_t *s) {
    ppmd8_w_t *z = (ppmd8_w_t *)s;
    Ppmd8_Free(&z->model, &g_alloc);
    fa_stream_destroy(z->src);
    fa_free(z);
}

fulta_arc_err_t fa_dec_ppmd8_zip(fa_stream_t *in, uint64_t out_size, const fa_limits_t *lim, fa_stream_t **out) {
    uint8_t h[2];
    fulta_arc_err_t e = fa_stream_read_exact(in, h, 2);
    if (e) { fa_stream_destroy(in); return e; }
    unsigned v = fa_le16(h), order = (v & 15) + 1, mem = ((v >> 4) & 0xFF) + 1, restore = v >> 12;
    if (order < 2 || restore > 2) { fa_stream_destroy(in); return FULTA_ARC_ERR_UNSUPPORTED; }
    if (((uint64_t)mem << 20) > max_dict(lim)) { fa_stream_destroy(in); return FULTA_ARC_ERR_LIMIT; }
    ppmd8_w_t *z = fa_calloc(1, sizeof *z);
    if (!z) { fa_stream_destroy(in); return FULTA_ARC_ERR_NOMEM; }
    Ppmd8_Construct(&z->model);
    if (!Ppmd8_Alloc(&z->model, (UInt32)mem << 20, &g_alloc)) { fa_free(z); fa_stream_destroy(in); return FULTA_ARC_ERR_NOMEM; }
    z->vt.Read = ppmd8_byte;
    z->src = in;
    fa_bytes_init(&z->in, in);
    z->order = order;
    z->restore = restore;
    z->out_size = out_size;
    z->base.read = ppmd8_read;
    z->base.destroy = ppmd8_destroy;
    *out = &z->base;
    return FULTA_ARC_OK;
}

/* ---- Zstandard (ZIP method 93, 7z coder 04F71101): vendored zstd 1.5.7 decoder; frames may follow each other ---- */

typedef struct zstd_w {
    fa_stream_t base;
    inbuf_t in;
    ZSTD_DStream *ds;
    uint64_t out_size, produced;
    size_t pending;                    /* ZSTD_decompressStream's last hint: 0 at a frame's end */
} zstd_w_t;

static fulta_arc_err_t zstd_read(fa_stream_t *s, void *buf, size_t n, size_t *got) {
    zstd_w_t *z = (zstd_w_t *)s;
    *got = 0;
    if (z->out_size != UINT64_MAX && n > z->out_size - z->produced) n = (size_t)(z->out_size - z->produced);
    while (*got < n) {
        fulta_arc_err_t e = inbuf_fill(&z->in);
        if (e) return e;
        ZSTD_inBuffer ib = {z->in.data, z->in.len, z->in.pos};
        ZSTD_outBuffer ob = {buf, n, *got};
        size_t r = ZSTD_decompressStream(z->ds, &ob, &ib);
        if (ZSTD_isError(r)) {
            ZSTD_ErrorCode c = ZSTD_getErrorCode(r);
            return c == ZSTD_error_memory_allocation ? FULTA_ARC_ERR_NOMEM
                 : c == ZSTD_error_frameParameter_windowTooLarge ? FULTA_ARC_ERR_LIMIT : FULTA_ARC_ERR_CORRUPT;
        }
        bool moved = ib.pos != z->in.pos || ob.pos != *got;
        z->in.pos = ib.pos;
        *got = ob.pos;
        z->pending = r;
        if (!moved && z->in.eof) {
            if (*got || z->pending == 0) break;      /* the end: after a whole frame, or with output to hand back */
            return FULTA_ARC_ERR_TRUNCATED;
        }
    }
    z->produced += *got;
    if (!*got && z->out_size != UINT64_MAX && z->produced < z->out_size) return FULTA_ARC_ERR_TRUNCATED;
    return FULTA_ARC_OK;
}

static void zstd_destroy(fa_stream_t *s) {
    zstd_w_t *z = (zstd_w_t *)s;
    ZSTD_freeDStream(z->ds);
    inbuf_free(&z->in);
    fa_free(z);
}

fulta_arc_err_t fa_dec_zstd(fa_stream_t *in, uint64_t out_size, const fa_limits_t *lim, fa_stream_t **out) {
    zstd_w_t *z = fa_calloc(1, sizeof *z);
    if (!z) { fa_stream_destroy(in); return FULTA_ARC_ERR_NOMEM; }
    if (inbuf_init(&z->in, in)) { fa_free(z->in.data); fa_free(z); fa_stream_destroy(in); return FULTA_ARC_ERR_NOMEM; }
    z->ds = ZSTD_createDStream();
    if (!z->ds) { inbuf_free(&z->in); fa_free(z); return FULTA_ARC_ERR_NOMEM; }
    /* the window is the dictionary: refuse frames whose window passes the limit */
    int wlog = 10;
    while (wlog < 31 && (UINT64_C(1) << (wlog + 1)) <= max_dict(lim)) wlog++;
    if (ZSTD_isError(ZSTD_DCtx_setParameter(z->ds, ZSTD_d_windowLogMax, wlog))) {
        ZSTD_freeDStream(z->ds);
        inbuf_free(&z->in);
        fa_free(z);
        return FULTA_ARC_ERR_NOMEM;
    }
    z->pending = 1;
    z->out_size = out_size;
    z->base.read = zstd_read;
    z->base.destroy = zstd_destroy;
    *out = &z->base;
    return FULTA_ARC_OK;
}
