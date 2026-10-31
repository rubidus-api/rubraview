/* lzx.c - FultaArc: Microsoft Cabinet LZX decompression (CFFOLDER typeCompress 3). MIT.
 *
 * Original FultaArc code written from Microsoft's published open standard [MS-PATCH] "LZX DELTA Compression and
 * Decompression" (rev 14.1, 2025-08-19), whose section 1 states LZXD is "a derivative of the Microsoft Cabinet LZX
 * format"; the non-delta core it documents (bitstream, position slots, the main/length/aligned/pre trees, the
 * verbatim/aligned/uncompressed blocks, the R0/R1/R2 repeated-offset LRU, and the E8 x86 call-translation) is the
 * CAB LZX bitstream. CAB caps the match length at 257 and does NOT use LZXD's "Extra Length" field (that is an
 * Exchange/WIM delta extension; pinned against 7-Zip, which accepts a length-257 match encoded with no extra bits).
 * The Microsoft Open Specifications IP notice explicitly permits copying the documentation "to develop
 * implementations". No GPL/LGPL source (libmspack, cabextract) was read or copied; 7-Zip (which reads CAB-LZX) is
 * used only as the external cross-check oracle, together with FultaArc's own cabmake.py LZX encoder. See D-020, cab.md.
 *
 * CAB vs the standalone LZXD framing of [MS-PATCH] section 2.2.1: a CAB folder carries NO 16-bit chunk-size prefix.
 * Each CFDATA block *is* one LZXD "chunk" of <= 32768 uncompressed bytes; cbData/cbUncomp come from the CFDATA header.
 * The LZX state - window (the folder's cumulative output), R0/R1/R2, the Huffman trees and an in-progress block -
 * carries across CFDATA blocks, but the bitstream realigns to a 16-bit boundary at every 32768-byte (chunk) boundary,
 * which in CAB coincides with each CFDATA boundary; so each CFDATA payload is decoded with a FRESH bit reader. The E8
 * header bit (and its optional 32-bit size) is read once, at the very first bit of the folder's first CFDATA. E8
 * reversal is a post-pass over the finished output, in 32768-byte frames, so matches never see translated bytes.
 *
 * API (a per-folder state object, not a pull stream - the state spans CFDATAs): fa_lzx_create(window_bits, out,
 * out_cap), fa_lzx_chunk(payload, uncomp_size) once per CFDATA, fa_lzx_finish() for the E8 pass, fa_lzx_destroy(). */
#include "../core/internal.h"

#define LZX_MIN_WIN     15
#define LZX_MAX_WIN     21
#define LZX_NUM_CHARS   256
#define LZX_MAX_POSSLOT 50                         /* window 2^21 */
#define LZX_MAIN_MAX    (LZX_NUM_CHARS + LZX_MAX_POSSLOT * 8)   /* 656 */
#define LZX_NUM_LENGTH  249
#define LZX_NUM_ALIGNED 8
#define LZX_NUM_PRE     20
#define LZX_MIN_MATCH   2
#define LZX_FRAME       32768u                     /* LZXD chunk / E8 frame size */
#define LZX_FAST_BITS   10

/* ---- MSB-first canonical Huffman -------------------------------------------------------------------------------
 * LZX reads the bitstream most-significant-bit first (unlike Deflate's LSB-first), so the canonical codes are the
 * same assignment (shorter codes first, then by symbol index) but the fast table is indexed by the raw top bits. */
typedef struct {
    uint16_t fast[1 << LZX_FAST_BITS];   /* top LZX_FAST_BITS bits -> (len<<10)|sym, len 0 = long code (slow path) */
    uint16_t count[17];                  /* codes per length 1..16 */
    uint16_t first_code[17];             /* first canonical code of each length */
    uint16_t first_sym[17];              /* index into sym[] of the first symbol of each length */
    uint16_t sym[LZX_MAIN_MAX];          /* symbols ordered by (length, symbol) */
    int nsym;                            /* used symbols; 0 = empty tree (valid for an unused length tree) */
} lzx_huff_t;

/* Build a canonical tree from path lengths; CORRUPT on over-subscription or an incomplete non-empty set (LZX
 * requires a complete tree, or an entirely empty one - a single-symbol tree is not valid). */
static fulta_arc_err_t lzx_huff_build(lzx_huff_t *h, const uint8_t *len, int n) {
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[len[i]]++;
    h->count[0] = 0;
    int used = 0;
    for (int i = 0; i < n; i++) used += len[i] != 0;
    h->nsym = used;
    if (used == 0) { memset(h->fast, 0, sizeof h->fast); return FULTA_ARC_OK; }   /* empty tree */
    int left = 1;
    for (int l = 1; l < 17; l++) { left <<= 1; left -= h->count[l]; if (left < 0) return FULTA_ARC_ERR_CORRUPT; }
    if (left > 0) return FULTA_ARC_ERR_CORRUPT;                                   /* incomplete */

    uint16_t offs[17];
    offs[1] = 0;
    for (int l = 1; l < 16; l++) offs[l + 1] = (uint16_t)(offs[l] + h->count[l]);
    for (int i = 0; i < n; i++) if (len[i]) h->sym[offs[len[i]]++] = (uint16_t)i;

    memset(h->fast, 0, sizeof h->fast);
    uint32_t code = 0;
    int idx = 0;
    for (int l = 1; l < 17; l++) {
        h->first_code[l] = (uint16_t)code;
        h->first_sym[l] = (uint16_t)idx;
        if (l <= LZX_FAST_BITS) {                                                /* fill the fast table */
            for (int c = 0; c < h->count[l]; c++) {
                uint32_t base = (code + (uint32_t)c) << (LZX_FAST_BITS - l);
                uint16_t ent = (uint16_t)((l << LZX_FAST_BITS) | h->sym[idx + c]);
                for (uint32_t f = 0; f < (1u << (LZX_FAST_BITS - l)); f++) h->fast[base + f] = ent;
            }
        }
        idx += h->count[l];
        code += h->count[l];
        code <<= 1;
    }
    return FULTA_ARC_OK;
}

/* ---- bit reader: a sequence of 16-bit little-endian words, bits consumed MSB-first (high end of a 32-bit buffer) */
typedef struct {
    const uint8_t *p, *end;
    uint32_t buf;    /* valid bits occupy the high `cnt` bits */
    int cnt;
    bool oob;        /* ran past the end of this chunk's payload */
} lzx_bits_t;

static void lzx_bits_init(lzx_bits_t *b, const uint8_t *p, size_t n) {
    b->p = p; b->end = p + n; b->buf = 0; b->cnt = 0; b->oob = false;
}
static inline void lzx_ensure(lzx_bits_t *b, int n) {               /* n <= 17 */
    while (b->cnt < n) {
        uint32_t w;
        if (b->p + 2 <= b->end) { w = (uint32_t)b->p[0] | ((uint32_t)b->p[1] << 8); b->p += 2; }
        else if (b->p < b->end) { w = (uint32_t)b->p[0]; b->p += 1; }            /* odd tail (not expected) */
        else { w = 0; b->oob = true; }                                          /* out of input: inject zeros */
        b->buf |= w << (16 - b->cnt);
        b->cnt += 16;
    }
}
static inline uint32_t lzx_get(lzx_bits_t *b, int n) {
    if (n == 0) return 0;
    lzx_ensure(b, n);
    uint32_t v = b->buf >> (32 - n);
    b->buf <<= n;
    b->cnt -= n;
    return v;
}
/* Discard 0..15 padding bits so the next read is on a 16-bit boundary, and hand back the raw byte cursor (the
 * whole words still in the buffer are given back for a byte-stream read). [MS-PATCH] 2.3.2.1. */
static const uint8_t *lzx_byte_align(lzx_bits_t *b) {
    int pad = b->cnt & 15;
    b->buf <<= pad;
    b->cnt -= pad;                                   /* now a multiple of 16 */
    b->p -= (b->cnt >> 3);
    b->cnt = 0; b->buf = 0;
    return b->p;
}

static int lzx_decode(lzx_bits_t *b, const lzx_huff_t *h) {
    if (h->nsym == 0) return -1;                                     /* empty tree: nothing decodes */
    lzx_ensure(b, LZX_FAST_BITS);
    if (b->cnt >= LZX_FAST_BITS) {
        uint16_t f = h->fast[b->buf >> (32 - LZX_FAST_BITS)];
        int l = f >> LZX_FAST_BITS;
        if (l) { b->buf <<= l; b->cnt -= l; return f & ((1 << LZX_FAST_BITS) - 1); }
    }
    uint32_t code = 0;                                               /* long code: walk bit by bit, MSB-first */
    for (int l = 1; l < 17; l++) {
        code = (code << 1) | lzx_get(b, 1);
        if (h->count[l] && code - h->first_code[l] < h->count[l])
            return h->sym[h->first_sym[l] + (code - h->first_code[l])];
    }
    return -1;
}

/* ---- position slot tables (computed for the window) ------------------------------------------------------------ */
static const uint8_t LZX_POSSLOTS[LZX_MAX_WIN - LZX_MIN_WIN + 1] = {30, 32, 34, 36, 38, 42, 50};

struct fa_lzx {
    uint8_t *out;            /* the folder's cumulative output buffer (caller-owned); matches read from it */
    uint64_t out_cap;        /* its capacity */
    uint64_t pos;            /* bytes produced so far */
    unsigned window_size;    /* 1 << window_bits (max match offset) */
    unsigned num_pos_slots;
    uint32_t base_pos[LZX_MAX_POSSLOT];
    uint8_t footer_bits[LZX_MAX_POSSLOT];
    uint32_t R0, R1, R2;
    bool header_done;        /* the E8 header bit has been read (first bit of the folder) */
    bool e8;
    uint32_t e8_size;
    /* in-progress block state (spans CFDATA chunks) */
    int block_type;          /* 0 none, 1 verbatim, 2 aligned, 3 uncompressed */
    uint32_t block_remaining;/* output bytes left in the current block */
    bool uncomp_odd;         /* the current uncompressed block has an odd size (trailing pad byte) */
    uint8_t main_len[LZX_MAIN_MAX];
    uint8_t length_len[LZX_NUM_LENGTH];
    lzx_huff_t main, length, aligned;
};

fulta_arc_err_t fa_lzx_create(unsigned window_bits, uint8_t *out, uint64_t out_cap, fa_lzx_t **lzx) {
    if (window_bits < LZX_MIN_WIN || window_bits > LZX_MAX_WIN) return FULTA_ARC_ERR_UNSUPPORTED;
    fa_lzx_t *lz = fa_calloc(1, sizeof *lz);
    if (!lz) return FULTA_ARC_ERR_NOMEM;
    lz->out = out; lz->out_cap = out_cap; lz->pos = 0;
    lz->window_size = 1u << window_bits;
    lz->num_pos_slots = LZX_POSSLOTS[window_bits - LZX_MIN_WIN];
    uint32_t base = 0;
    for (unsigned s = 0; s < lz->num_pos_slots; s++) {
        unsigned fb = s < 4 ? 0 : (s >> 1) - 1;
        if (fb > 17) fb = 17;
        lz->footer_bits[s] = (uint8_t)fb;
        lz->base_pos[s] = base;
        base += 1u << fb;
    }
    lz->R0 = lz->R1 = lz->R2 = 1;
    *lzx = lz;
    return FULTA_ARC_OK;
}

void fa_lzx_destroy(fa_lzx_t *lz) { fa_free(lz); }

/* Read one tree's path lengths: a 20-element pretree (4 bits each), then delta-decode `num` lengths starting at
 * `first` against the existing values in `len` (which persist across blocks). [MS-PATCH] 2.5. */
static fulta_arc_err_t lzx_read_lengths(lzx_bits_t *b, uint8_t *len, unsigned first, unsigned num) {
    uint8_t pre[LZX_NUM_PRE];
    for (int i = 0; i < LZX_NUM_PRE; i++) pre[i] = (uint8_t)lzx_get(b, 4);
    lzx_huff_t pt;
    fulta_arc_err_t e = lzx_huff_build(&pt, pre, LZX_NUM_PRE);
    if (e) return e;
    unsigned i = first, end = first + num;
    while (i < end) {
        int sym = lzx_decode(b, &pt);
        if (sym < 0) return FULTA_ARC_ERR_CORRUPT;
        if (sym <= 16) {
            len[i] = (uint8_t)(((int)len[i] - sym + 17) % 17);
            i++;
        } else if (sym == 17) {
            unsigned z = lzx_get(b, 4) + 4;
            while (z-- && i < end) len[i++] = 0;
        } else if (sym == 18) {
            unsigned z = lzx_get(b, 5) + 20;
            while (z-- && i < end) len[i++] = 0;
        } else {   /* 19: a run of (4 + 1 extra bit) elements sharing one delta against the run's first prev_len */
            unsigned run = lzx_get(b, 1) + 4;
            int c = lzx_decode(b, &pt);
            if (c < 0 || c > 16) return FULTA_ARC_ERR_CORRUPT;
            uint8_t val = (uint8_t)(((int)len[i] - c + 17) % 17);
            while (run-- && i < end) len[i++] = val;
        }
        if (b->oob) return FULTA_ARC_ERR_TRUNCATED;
    }
    return FULTA_ARC_OK;
}

/* Read the Huffman trees at the start of a verbatim (type 1) or aligned-offset (type 2) block. */
static fulta_arc_err_t lzx_read_block_trees(fa_lzx_t *lz, lzx_bits_t *b) {
    fulta_arc_err_t e;
    if (lz->block_type == 2) {                                       /* aligned offset tree: 8 x 3 bits, direct */
        uint8_t al[LZX_NUM_ALIGNED];
        for (int i = 0; i < LZX_NUM_ALIGNED; i++) al[i] = (uint8_t)lzx_get(b, 3);
        if ((e = lzx_huff_build(&lz->aligned, al, LZX_NUM_ALIGNED))) return e;
    }
    unsigned main_syms = LZX_NUM_CHARS + lz->num_pos_slots * 8;
    if ((e = lzx_read_lengths(b, lz->main_len, 0, LZX_NUM_CHARS)))                   return e;  /* first 256 */
    if ((e = lzx_read_lengths(b, lz->main_len, LZX_NUM_CHARS, main_syms - LZX_NUM_CHARS))) return e;  /* matches */
    if ((e = lzx_huff_build(&lz->main, lz->main_len, (int)main_syms)))               return e;
    if ((e = lzx_read_lengths(b, lz->length_len, 0, LZX_NUM_LENGTH)))                return e;
    if ((e = lzx_huff_build(&lz->length, lz->length_len, LZX_NUM_LENGTH)))           return e;
    return FULTA_ARC_OK;
}

/* Decode exactly `uncomp` output bytes for one CFDATA chunk (payload `in`/`in_len`), with a fresh bit reader. */
fulta_arc_err_t fa_lzx_chunk(fa_lzx_t *lz, const uint8_t *in, size_t in_len, uint64_t uncomp) {
    if (uncomp > lz->out_cap - lz->pos) return FULTA_ARC_ERR_CORRUPT;
    uint64_t target = lz->pos + uncomp;
    lzx_bits_t b;
    lzx_bits_init(&b, in, in_len);

    if (!lz->header_done) {                                          /* E8 header: first bit of the folder */
        lz->e8 = lzx_get(&b, 1) != 0;
        if (lz->e8) { uint32_t hi = lzx_get(&b, 16), lo = lzx_get(&b, 16); lz->e8_size = (hi << 16) | lo; }
        lz->header_done = true;
    }

    while (lz->pos < target) {
        if (lz->block_remaining == 0) {                              /* start a new block */
            int type = (int)lzx_get(&b, 3);
            uint32_t sz = (lzx_get(&b, 8) << 16) | (lzx_get(&b, 8) << 8) | lzx_get(&b, 8);  /* 24-bit, MSB first */
            if (b.oob) return FULTA_ARC_ERR_TRUNCATED;
            if (sz == 0) return FULTA_ARC_ERR_CORRUPT;
            lz->block_type = type;
            lz->block_remaining = sz;
            if (type == 1 || type == 2) {
                fulta_arc_err_t e = lzx_read_block_trees(lz, &b);
                if (e) return e;
            } else if (type == 3) {                                  /* uncompressed: byte-aligned R0/R1/R2 + raw */
                const uint8_t *bp = lzx_byte_align(&b);
                if (bp + 12 > b.end) return FULTA_ARC_ERR_TRUNCATED;
                lz->R0 = fa_le32(bp); lz->R1 = fa_le32(bp + 4); lz->R2 = fa_le32(bp + 8);
                b.p = bp + 12;
                lz->uncomp_odd = (sz & 1) != 0;
            } else {
                return FULTA_ARC_ERR_CORRUPT;
            }
        }

        if (lz->block_type == 3) {                                   /* raw copy (may span chunks) */
            uint64_t n = target - lz->pos;
            if (n > lz->block_remaining) n = lz->block_remaining;
            if (b.p + n > b.end) return FULTA_ARC_ERR_TRUNCATED;
            memcpy(lz->out + lz->pos, b.p, (size_t)n);
            b.p += n; lz->pos += n; lz->block_remaining -= (uint32_t)n;
            if (lz->block_remaining == 0 && lz->uncomp_odd) { if (b.p < b.end) b.p++; }   /* trailing pad byte */
            continue;
        }

        /* verbatim / aligned: decode tokens until the block or the chunk ends */
        while (lz->block_remaining && lz->pos < target) {
            int main = lzx_decode(&b, &lz->main);
            if (main < 0) return FULTA_ARC_ERR_CORRUPT;
            if (main < 256) {
                lz->out[lz->pos++] = (uint8_t)main;
                lz->block_remaining--;
            } else {
                int m = main - 256;
                int length_header = m & 7;
                int pos_slot = m >> 3;
                if ((unsigned)pos_slot >= lz->num_pos_slots) return FULTA_ARC_ERR_CORRUPT;
                /* Match length. CAB LZX caps the length at 257 (length tree symbol 248 -> 7+2+248 = 257, final);
                 * the LZXD "Extra Length" field for lengths > 257 is an Exchange/WIM delta extension not present in
                 * CAB (verified against 7-Zip - see cab.md), so it is never read here. */
                uint32_t len;
                if (length_header == 7) {
                    int lf = lzx_decode(&b, &lz->length);
                    if (lf < 0) return FULTA_ARC_ERR_CORRUPT;
                    len = (uint32_t)lf + 7 + LZX_MIN_MATCH;
                } else {
                    len = (uint32_t)length_header + LZX_MIN_MATCH;
                }
                uint32_t off;
                if (pos_slot == 0) { off = lz->R0; }
                else if (pos_slot == 1) { off = lz->R1; lz->R1 = lz->R0; lz->R0 = off; }
                else if (pos_slot == 2) { off = lz->R2; lz->R2 = lz->R0; lz->R0 = off; }
                else {
                    int fb = lz->footer_bits[pos_slot];
                    uint32_t verb, al = 0;
                    if (lz->block_type == 2 && fb >= 3) {
                        verb = lzx_get(&b, fb - 3) << 3;
                        int a = lzx_decode(&b, &lz->aligned);
                        if (a < 0) return FULTA_ARC_ERR_CORRUPT;
                        al = (uint32_t)a;
                    } else {
                        verb = lzx_get(&b, fb);
                    }
                    off = lz->base_pos[pos_slot] + verb + al - 2;
                    lz->R2 = lz->R1; lz->R1 = lz->R0; lz->R0 = off;
                }
                if (b.oob) return FULTA_ARC_ERR_TRUNCATED;
                if (off == 0 || off > lz->pos || off > lz->window_size) return FULTA_ARC_ERR_CORRUPT;
                if (len > lz->block_remaining || lz->pos + len > target) return FULTA_ARC_ERR_CORRUPT;
                uint8_t *o = lz->out + lz->pos;
                for (uint32_t k = 0; k < len; k++) o[k] = o[(int64_t)k - off];
                lz->pos += len;
                lz->block_remaining -= len;
            }
        }
        if (b.oob) return FULTA_ARC_ERR_TRUNCATED;
    }
    return FULTA_ARC_OK;
}

/* Undo the x86 E8 call translation, post-decompression, in 32768-byte frames. [MS-PATCH] 2.2.2 (reverse). */
void fa_lzx_finish(fa_lzx_t *lz) {
    if (!lz->e8 || lz->e8_size == 0) return;
    uint64_t total = lz->pos;
    for (uint64_t frame = 0; frame < total; frame += LZX_FRAME) {
        uint64_t chunk_off = frame;
        uint64_t chunk_size = total - frame;
        if (chunk_size > LZX_FRAME) chunk_size = LZX_FRAME;
        if (chunk_off >= 0x40000000u || chunk_size <= 10) continue;   /* E8 off after 1 GiB; tiny frame untouched */
        uint8_t *buf = lz->out + frame;
        for (uint64_t i = 0; i + 10 < chunk_size; i++) {
            if (buf[i] != 0xE8) continue;
            int32_t value = (int32_t)fa_le32(buf + i + 1);
            int32_t cur = (int32_t)(chunk_off + i);
            if (value >= -cur && value < (int32_t)lz->e8_size) {
                int32_t disp = (value >= 0) ? value - cur : value + (int32_t)lz->e8_size;
                fa_put_le32(buf + i + 1, (uint32_t)disp);
            }
            i += 4;
        }
    }
}
