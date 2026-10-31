/* compress.c - FultaArc: Unix compress (.Z) LZW decoder. MIT.
 * Original FultaArc code from the public-domain `compress`/LZW stream format (the algorithm published with BSD
 * `compress(1)` and widely re-described; the Unisys LZW patents have expired). No GPL/LGPL source read or copied.
 * Format: magic 0x1F 0x9D, then one flags byte = maxbits (bits 0-4, 9..16) | block_mode (0x80); bits 0x60 reserved.
 * Codes are packed least-significant-bit-first and start 9 bits wide, widening by one bit each time the code table
 * fills, up to maxbits. The stream is byte-grouped: codes are read in groups of 8 at the current width (so a group
 * is `n_bits` bytes), and a width change or a CLEAR (block_mode code 256) re-aligns to the next group boundary -
 * the decoder mirrors that by reading exactly `n_bits` bytes per group and refilling immediately on a width change.
 * See docs/research/formats/compress.md. */
#include "../core/internal.h"

#define CMP_INIT_BITS 9
#define CMP_CLEAR     256       /* block_mode reset code */
#define CMP_FIRST     257       /* first free table entry in block_mode */
#define CMP_INBUF     65536

typedef struct comp {
    fa_stream_t base;
    fa_stream_t *in;
    uint8_t *inbuf;             /* raw input buffer */
    size_t inpos, inlen;
    bool ineof;

    int maxbits;
    bool block_mode;
    int n_bits;
    uint32_t maxcode, maxmaxcode;
    uint32_t free_ent;
    int clear_flg;
    int32_t oldcode;
    uint8_t finchar;
    bool started, done;

    uint8_t rbuf[16];           /* one code group: up to maxbits (<=16) bytes */
    int roffset, rsize;         /* in bits */

    uint16_t *prefix;           /* [maxmaxcode] */
    uint8_t *suffix;            /* [maxmaxcode] */
    uint8_t *destack;           /* [maxmaxcode] output expansion, built backwards */
    size_t out_pos, out_len;    /* pending expanded bytes in destack[out_pos..out_len) */
} comp_t;

static int cmp_in_byte(comp_t *z) {
    if (z->inpos >= z->inlen) {
        if (z->ineof) return -1;
        size_t got = 0;
        if (z->in->read(z->in, z->inbuf, CMP_INBUF, &got)) { z->ineof = true; return -1; }
        z->inpos = 0;
        z->inlen = got;
        if (!got) { z->ineof = true; return -1; }
    }
    return z->inbuf[z->inpos++];
}

/* One LZW code, or -1 at end of input. Mirrors compress's getcode: a group is n_bits bytes (8 codes); a width
 * change (free_ent > maxcode) or a pending clear forces a fresh group, re-aligning the bit stream. */
static int32_t cmp_getcode(comp_t *z) {
    if (z->clear_flg > 0 || z->roffset >= z->rsize || z->free_ent > z->maxcode) {
        if (z->free_ent > z->maxcode && z->n_bits < z->maxbits) {   /* n_bits stays <= maxbits (<=16): rbuf is safe */
            z->n_bits++;
            z->maxcode = (z->n_bits == z->maxbits) ? z->maxmaxcode : ((1u << z->n_bits) - 1);
        }
        if (z->clear_flg > 0) {
            z->n_bits = CMP_INIT_BITS;
            z->maxcode = (1u << z->n_bits) - 1;
            z->clear_flg = 0;
        }
        int got = 0;
        for (; got < z->n_bits; got++) {
            int b = cmp_in_byte(z);
            if (b < 0) break;
            z->rbuf[got] = (uint8_t)b;
        }
        if (got == 0) return -1;
        z->roffset = 0;
        z->rsize = (got << 3) - (z->n_bits - 1);   /* highest valid code-start bit + 1 */
        if (z->rsize <= 0) return -1;
    }
    uint32_t off = (uint32_t)z->roffset;
    uint32_t code = 0;
    for (int i = 0; i < z->n_bits; i++) {
        uint32_t bit = (z->rbuf[(off + (uint32_t)i) >> 3] >> ((off + (uint32_t)i) & 7)) & 1u;
        code |= bit << i;
    }
    z->roffset += z->n_bits;
    return (int32_t)code;
}

/* Expand one input code into destack[out_pos..out_len); sets z->done at end of input. */
static fulta_arc_err_t cmp_step(comp_t *z) {
    if (!z->started) {
        int32_t c = cmp_getcode(z);
        if (c < 0) { z->done = true; return FULTA_ARC_OK; }
        if (c >= 256) return FULTA_ARC_ERR_CORRUPT;            /* the first code must be a literal */
        z->oldcode = c;
        z->finchar = (uint8_t)c;
        z->destack[0] = (uint8_t)c;
        z->out_pos = 0;
        z->out_len = 1;
        z->started = true;
        return FULTA_ARC_OK;
    }

    int32_t code = cmp_getcode(z);
    if (code < 0) { z->done = true; return FULTA_ARC_OK; }

    if (z->block_mode && code == CMP_CLEAR) {                   /* reset the table; re-align handled by cmp_getcode */
        z->free_ent = CMP_FIRST - 1;                           /* the next code's add makes it FIRST again */
        z->clear_flg = 1;
        code = cmp_getcode(z);
        if (code < 0) { z->done = true; return FULTA_ARC_OK; }
        /* fall through and process `code` normally: suffix[0..255] is the identity, so a literal expands to itself,
         * and the entry-add below resyncs free_ent (mirrors compress's decompress, which does not special-case it). */
    }

    uint32_t incode = (uint32_t)code;
    uint32_t cur = (uint32_t)code;
    size_t sp = z->maxmaxcode;                                 /* build the string backwards into destack */

    if (cur >= z->free_ent) {                                  /* KwKwK: code not yet in the table */
        if (cur > z->free_ent || z->oldcode < 0) return FULTA_ARC_ERR_CORRUPT;
        z->destack[--sp] = z->finchar;
        cur = (uint32_t)z->oldcode;
    }
    uint32_t guard = 0;
    while (cur >= 256) {                                       /* walk the prefix chain to a literal */
        if (sp == 0 || ++guard > z->maxmaxcode) return FULTA_ARC_ERR_CORRUPT;
        z->destack[--sp] = z->suffix[cur];
        cur = z->prefix[cur];
    }
    z->finchar = (uint8_t)cur;
    if (sp == 0) return FULTA_ARC_ERR_CORRUPT;
    z->destack[--sp] = z->finchar;

    z->out_pos = sp;
    z->out_len = z->maxmaxcode;

    if (z->free_ent < z->maxmaxcode) {                         /* add prev + first-char-of-current */
        z->prefix[z->free_ent] = (uint16_t)z->oldcode;
        z->suffix[z->free_ent] = z->finchar;
        z->free_ent++;
    }
    z->oldcode = (int32_t)incode;
    return FULTA_ARC_OK;
}

static fulta_arc_err_t cmp_read(fa_stream_t *s, void *buf, size_t n, size_t *got) {
    comp_t *z = (comp_t *)s;
    *got = 0;
    uint8_t *o = buf;
    while (*got < n) {
        if (z->out_pos >= z->out_len) {
            if (z->done) break;
            fulta_arc_err_t e = cmp_step(z);
            if (e) return e;
            continue;
        }
        size_t k = z->out_len - z->out_pos;
        if (k > n - *got) k = n - *got;
        memcpy(o + *got, z->destack + z->out_pos, k);
        z->out_pos += k;
        *got += k;
    }
    return FULTA_ARC_OK;
}

static void cmp_destroy(fa_stream_t *s) {
    comp_t *z = (comp_t *)s;
    fa_stream_destroy(z->in);
    fa_free(z->inbuf);
    fa_free(z->prefix);
    fa_free(z->suffix);
    fa_free(z->destack);
    fa_free(z);
}

fulta_arc_err_t fa_dec_compress(fa_stream_t *in, fa_stream_t **out) {
    comp_t *z = fa_calloc(1, sizeof *z);
    if (!z) { fa_stream_destroy(in); return FULTA_ARC_ERR_NOMEM; }
    z->in = in;
    z->inbuf = fa_malloc(CMP_INBUF);
    if (!z->inbuf) { fa_free(z); fa_stream_destroy(in); return FULTA_ARC_ERR_NOMEM; }

    /* header: magic + flags */
    if (cmp_in_byte(z) != 0x1F || cmp_in_byte(z) != 0x9D) { cmp_destroy(&z->base); return FULTA_ARC_ERR_NOT_ARCHIVE; }
    int flags = cmp_in_byte(z);
    if (flags < 0) { cmp_destroy(&z->base); return FULTA_ARC_ERR_TRUNCATED; }
    z->maxbits = flags & 0x1F;
    z->block_mode = (flags & 0x80) != 0;
    if ((flags & 0x60) || z->maxbits < CMP_INIT_BITS || z->maxbits > 16) { cmp_destroy(&z->base); return FULTA_ARC_ERR_CORRUPT; }

    z->maxmaxcode = 1u << z->maxbits;
    z->n_bits = CMP_INIT_BITS;
    /* when maxbits == 9 the start width is already the top width, so maxcode is maxmaxcode and no bump ever fires;
     * otherwise it is the ordinary 9-bit max. (Getcode caps the bump at maxbits too.) */
    z->maxcode = (z->n_bits == z->maxbits) ? z->maxmaxcode : ((1u << z->n_bits) - 1);
    z->free_ent = z->block_mode ? CMP_FIRST : 256;
    z->oldcode = -1;

    z->prefix = fa_calloc(z->maxmaxcode, sizeof *z->prefix);
    z->suffix = fa_malloc(z->maxmaxcode);
    z->destack = fa_malloc(z->maxmaxcode);
    if (!z->prefix || !z->suffix || !z->destack) { cmp_destroy(&z->base); return FULTA_ARC_ERR_NOMEM; }
    for (uint32_t i = 0; i < 256; i++) z->suffix[i] = (uint8_t)i;   /* literals: suffix[c] == c */

    z->base.read = cmp_read;
    z->base.destroy = cmp_destroy;
    *out = &z->base;
    return FULTA_ARC_OK;
}
