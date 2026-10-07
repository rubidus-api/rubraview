/* xz.c - FultaArc: the .xz container (ZIP method 95), over a known byte range. MIT.
 * Written from docs/specs/codec-xz.md (FultaArc's document of the public-domain "The .xz File Format" 1.2.1 by Lasse
 * Collin and Igor Pavlov). Blocks are located through the index at the end of each stream, so each block's packed
 * size is exact; LZMA2 and the filters are FultaArc's codecs. Checks: none, CRC-32, CRC-64, SHA-256. */
#include "../core/internal.h"

typedef struct xz_blk { uint64_t data_off, data_len, unc; uint8_t check_type; uint64_t check_off;
                        uint8_t nfilt; uint64_t fid[4]; uint8_t fprops[4][8]; uint8_t fplen[4]; } xz_blk_t;

typedef struct xz_stream {
    fa_stream_t base;
    fa_range_t range;
    xz_blk_t *blk;
    size_t nblk, cur;
    fa_stream_t *s;                     /* the current block's decoder */
    uint64_t left;                      /* bytes of the current block still to give */
    uint32_t crc32;
    uint64_t crc64;
    fa_sha256_t sha;
    const fa_limits_t *lim;
    fa_limits_t limits;
} xz_stream_t;

static uint64_t crc64_table[256];
static bool crc64_ready;

static uint64_t crc64(uint64_t crc, const uint8_t *p, size_t n) {
    if (!crc64_ready) {
        for (int i = 0; i < 256; i++) {
            uint64_t c = (uint64_t)i;
            for (int k = 0; k < 8; k++) c = (c >> 1) ^ ((c & 1) ? UINT64_C(0xC96C5795D7870F42) : 0);
            crc64_table[i] = c;
        }
        crc64_ready = true;
    }
    crc = ~crc;
    while (n--) crc = crc64_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static const uint8_t CHECK_SIZE[16] = {0, 4, 4, 4, 8, 8, 8, 16, 16, 16, 32, 32, 32, 64, 64, 64};

/* VLI (codec-xz.md 1.2): 7 bits per byte, least significant first, at most 9 bytes */
static bool vli(const uint8_t *p, size_t n, size_t *pos, uint64_t *v) {
    *v = 0;
    for (int i = 0; i < 9; i++) {
        if (*pos >= n) return false;
        uint8_t b = p[(*pos)++];
        *v |= (uint64_t)(b & 0x7F) << (7 * i);
        if (!(b & 0x80)) return !(i > 0 && b == 0);
    }
    return false;
}

static fulta_arc_err_t parse(xz_stream_t *z, uint64_t off, uint64_t len) {
    uint64_t end = off + len;
    while (end > off) {
        /* skip stream padding (zero bytes in groups of four) */
        uint8_t ft[12];
        fulta_arc_err_t e = fa_range_read_exact(&z->range, end - 4, ft, 4);
        if (e) return e;
        if (!fa_le32(ft)) { end -= 4; continue; }
        if (end - off < 24) return FULTA_ARC_ERR_CORRUPT;
        if ((e = fa_range_read_exact(&z->range, end - 12, ft, 12))) return e;
        if (ft[10] != 'Y' || ft[11] != 'Z' || fa_crc32(0, ft + 4, 6) != fa_le32(ft)) return FULTA_ARC_ERR_CORRUPT;
        uint8_t check = ft[9] & 0x0F;
        uint64_t isize = ((uint64_t)fa_le32(ft + 4) + 1) * 4;
        if (isize > end - off - 24 || isize > (64u << 20)) return FULTA_ARC_ERR_CORRUPT;
        uint64_t ipos = end - 12 - isize;
        uint8_t *ix = fa_malloc((size_t)isize);
        if (!ix) return FULTA_ARC_ERR_NOMEM;
        if ((e = fa_range_read_exact(&z->range, ipos, ix, (size_t)isize))) { fa_free(ix); return e; }
        size_t p = 1;
        uint64_t nrec;
        if (ix[0] != 0 || !vli(ix, (size_t)isize, &p, &nrec) || nrec > isize || fa_crc32(0, ix, (size_t)isize - 4) != fa_le32(ix + isize - 4)) {
            fa_free(ix);
            return FULTA_ARC_ERR_CORRUPT;
        }
        uint64_t total = 0;
        uint64_t *uns = fa_calloc(nrec ? (size_t)nrec : 1, sizeof *uns), *unc = fa_calloc(nrec ? (size_t)nrec : 1, sizeof *unc);
        if (!uns || !unc) { fa_free(ix); fa_free(uns); fa_free(unc); return FULTA_ARC_ERR_NOMEM; }
        for (uint64_t i = 0; i < nrec; i++) {
            if (!vli(ix, (size_t)isize, &p, &uns[i]) || !vli(ix, (size_t)isize, &p, &unc[i]) || !uns[i]) { e = FULTA_ARC_ERR_CORRUPT; break; }
            total += (uns[i] + 3) & ~(uint64_t)3;
        }
        fa_free(ix);
        if (!e && ipos < off + 12 + total) e = FULTA_ARC_ERR_CORRUPT;
        uint64_t sstart = ipos - total - 12;
        uint8_t sh[12];
        if (!e) e = fa_range_read_exact(&z->range, sstart, sh, 12);
        if (!e && (memcmp(sh, "\xFD" "7zXZ\0", 6) != 0 || memcmp(sh + 6, ft + 8, 2) != 0 || fa_crc32(0, sh + 6, 2) != fa_le32(sh + 8)))
            e = FULTA_ARC_ERR_CORRUPT;
        /* blocks of this stream go in front of those of later streams */
        xz_blk_t *nb = e ? NULL : fa_realloc(z->blk, (z->nblk + nrec) * sizeof *nb);
        if (!e && !nb) e = FULTA_ARC_ERR_NOMEM;
        if (e) { fa_free(uns); fa_free(unc); return e; }
        memmove(nb + nrec, nb, z->nblk * sizeof *nb);
        z->blk = nb;
        uint64_t bp = sstart + 12;
        for (uint64_t i = 0; i < nrec && !e; i++) {
            xz_blk_t *b = &nb[i];
            memset(b, 0, sizeof *b);
            uint8_t hdr[1024];
            if ((e = fa_range_read_exact(&z->range, bp, hdr, 1))) break;
            size_t hs = ((size_t)hdr[0] + 1) * 4;
            if (!hdr[0] || hs > uns[i]) { e = FULTA_ARC_ERR_CORRUPT; break; }
            if ((e = fa_range_read_exact(&z->range, bp, hdr, hs))) break;
            if (fa_crc32(0, hdr, hs - 4) != fa_le32(hdr + hs - 4)) { e = FULTA_ARC_ERR_CORRUPT; break; }
            uint8_t fl = hdr[1];
            if (fl & 0x3C) { e = FULTA_ARC_ERR_UNSUPPORTED; break; }
            size_t q = 2;
            uint64_t v;
            if (fl & 0x40 && !vli(hdr, hs - 4, &q, &v)) { e = FULTA_ARC_ERR_CORRUPT; break; }
            if (fl & 0x80 && !vli(hdr, hs - 4, &q, &v)) { e = FULTA_ARC_ERR_CORRUPT; break; }
            b->nfilt = (fl & 3) + 1;
            for (int k = 0; k < b->nfilt; k++) {
                uint64_t ps;
                if (!vli(hdr, hs - 4, &q, &b->fid[k]) || !vli(hdr, hs - 4, &q, &ps) || ps > 8 || q + ps > hs - 4) { e = FULTA_ARC_ERR_CORRUPT; break; }
                memcpy(b->fprops[k], hdr + q, (size_t)ps);
                b->fplen[k] = (uint8_t)ps;
                q += (size_t)ps;
            }
            if (e) break;
            uint64_t cs = CHECK_SIZE[check];
            if (uns[i] < hs + cs) { e = FULTA_ARC_ERR_CORRUPT; break; }
            b->data_off = bp + hs;
            b->data_len = uns[i] - hs - cs;
            b->unc = unc[i];
            b->check_type = check;
            b->check_off = bp + uns[i] - cs;
            bp += (uns[i] + 3) & ~(uint64_t)3;
        }
        fa_free(uns);
        fa_free(unc);
        if (e) return e;
        z->nblk += nrec;
        end = sstart;
    }
    return FULTA_ARC_OK;
}

static fulta_arc_err_t open_block(xz_stream_t *z) {
    xz_blk_t *b = &z->blk[z->cur];
    fa_stream_t *s;
    fulta_arc_err_t e = fa_stream_range(&z->range, b->data_off, b->data_len, &s);
    if (e) return e;
    for (int k = b->nfilt - 1; k >= 0 && !e; k--) {
        uint64_t id = b->fid[k];
        const uint8_t *pp = b->fprops[k];
        if (id == 0x21) {
            if (b->fplen[k] != 1 || k != b->nfilt - 1) { fa_stream_destroy(s); return FULTA_ARC_ERR_CORRUPT; }
            e = fa_dec_lzma2(s, pp[0], b->unc, z->lim, &s);
        } else if (id == 0x03) {
            if (b->fplen[k] != 1) { fa_stream_destroy(s); return FULTA_ARC_ERR_CORRUPT; }
            e = fa_dec_filter(s, FA_FILTER_DELTA, (uint32_t)pp[0] + 1, &s);
        } else if (id >= 0x04 && id <= 0x0B) {
            static const fa_filter_kind_t K[8] = {FA_FILTER_X86, FA_FILTER_PPC, FA_FILTER_IA64, FA_FILTER_ARM,
                                                  FA_FILTER_ARMT, FA_FILTER_SPARC, FA_FILTER_ARM64, FA_FILTER_RISCV};
            uint32_t start = b->fplen[k] == 4 ? fa_le32(pp) : 0;
            e = fa_dec_filter(s, K[id - 4], start, &s);
        } else {
            fa_stream_destroy(s);
            return FULTA_ARC_ERR_UNSUPPORTED;
        }
    }
    if (e) return e;
    z->s = s;
    z->left = b->unc;
    z->crc32 = 0;
    z->crc64 = 0;
    fa_sha256_init(&z->sha);
    return FULTA_ARC_OK;
}

static fulta_arc_err_t finish_block(xz_stream_t *z) {
    xz_blk_t *b = &z->blk[z->cur];
    uint8_t want[64], got[32];
    size_t cs = CHECK_SIZE[b->check_type];
    fulta_arc_err_t e = fa_range_read_exact(&z->range, b->check_off, want, cs);
    if (e) return e;
    if (b->check_type == 1) { fa_put_le32(got, z->crc32); if (memcmp(got, want, 4)) return FULTA_ARC_ERR_CHECKSUM; }
    else if (b->check_type == 4) { fa_put_le64(got, z->crc64); if (memcmp(got, want, 8)) return FULTA_ARC_ERR_CHECKSUM; }
    else if (b->check_type == 10) { fa_sha256_final(&z->sha, got); if (memcmp(got, want, 32)) return FULTA_ARC_ERR_CHECKSUM; }
    fa_stream_destroy(z->s);
    z->s = NULL;
    z->cur++;
    return FULTA_ARC_OK;
}

static fulta_arc_err_t xz_read(fa_stream_t *st, void *buf, size_t n, size_t *got) {
    xz_stream_t *z = (xz_stream_t *)st;
    *got = 0;
    while (*got < n) {
        if (!z->s) {
            if (z->cur >= z->nblk) break;
            fulta_arc_err_t e = open_block(z);
            if (e) return e;
        }
        if (!z->left) {
            fulta_arc_err_t e = finish_block(z);
            if (e) return e;
            continue;
        }
        size_t want = n - *got;
        if (want > z->left) want = (size_t)z->left;
        size_t g = 0;
        fulta_arc_err_t e = z->s->read(z->s, (uint8_t *)buf + *got, want, &g);
        if (e) return e;
        if (!g) return FULTA_ARC_ERR_TRUNCATED;
        uint8_t *p = (uint8_t *)buf + *got;
        uint8_t ct = z->blk[z->cur].check_type;
        if (ct == 1) z->crc32 = fa_crc32(z->crc32, p, g);
        else if (ct == 4) z->crc64 = crc64(z->crc64, p, g);
        else if (ct == 10) fa_sha256_update(&z->sha, p, g);
        *got += g;
        z->left -= g;
    }
    return FULTA_ARC_OK;
}

static void xz_destroy(fa_stream_t *st) {
    xz_stream_t *z = (xz_stream_t *)st;
    fa_stream_destroy(z->s);
    fa_free(z->blk);
    fa_free(z->range.pieces);
    fa_free(z);
}

fulta_arc_err_t fa_dec_xz_range(const fa_range_t *r, uint64_t off, uint64_t len, const fa_limits_t *lim,
                                fa_stream_t **out) {
    xz_stream_t *z = fa_calloc(1, sizeof *z);
    if (!z) return FULTA_ARC_ERR_NOMEM;
    z->range.pieces = fa_calloc(r->count, sizeof *r->pieces);
    if (!z->range.pieces) { fa_free(z); return FULTA_ARC_ERR_NOMEM; }
    memcpy(z->range.pieces, r->pieces, r->count * sizeof *r->pieces);
    z->range.count = r->count;
    z->range.size = r->size;
    if (lim) z->limits = *lim;
    z->lim = &z->limits;
    z->base.read = xz_read;
    z->base.destroy = xz_destroy;
    fulta_arc_err_t e = len < 12 ? FULTA_ARC_ERR_CORRUPT : parse(z, off, len);
    if (e) { xz_destroy(&z->base); return e; }
    *out = &z->base;
    return FULTA_ARC_OK;
}
