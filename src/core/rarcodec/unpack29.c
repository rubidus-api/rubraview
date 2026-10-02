/*
 * Rubraview's RAR codec (MIT): the RAR 2.9 / 3.x algorithm -- LZ blocks, PPMd blocks and the RAR 3 filter
 * records. Written from docs/specs/rar-decompression.md, sections 9, 10.1-10.3 and 11; the PPMd model and
 * its original range coder are the LZMA SDK's public-domain Ppmd7.c / Ppmd7aDec.c (vendor/lzma), used through
 * the entry points spec 11 names.
 */
#include "rc_unpack.h"
#include <stdlib.h>
#include <string.h>

typedef rubraview_rar_unpack_status_t status_t;
#define S_OK RUBRAVIEW_RAR_UNPACK_OK
#define S_BAD RUBRAVIEW_RAR_UNPACK_CORRUPT

#define MC 299
#define DC 60
#define LDC 17
#define RC 28

/* ---- PPMd glue (spec 11) ---- */

static void *ppm_alloc(ISzAllocPtr p, size_t size) { (void)p; return malloc(size); }
static void ppm_free(ISzAllocPtr p, void *a) { (void)p; free(a); }
static const ISzAlloc PPM_ALLOC = { ppm_alloc, ppm_free };

/* The range coder's bytes: the next 8 bits of the packed stream, which is byte-aligned here. */
static Byte ppm_read(IByteInPtr p) {
    rc_unpack_t *u = ((const struct { IByteIn vt; rc_unpack_t *u; }*)(const void*)p)->u;
    return (Byte)rc_get(&u->bits, 8);
}

void rc_unpack29_free(rc_unpack_t *u) {
    if (u->v29.ppmd_alloc) Ppmd7_Free(&u->v29.ppmd, &PPM_ALLOC);
    u->v29.ppmd_alloc = false;
    u->v29.ppmd_ready = false;
}

/* One PPMd symbol, or -1 on anything wrong (a negative symbol, packed data run out). */
static int ppm_sym(rc_unpack_t *u) {
    int c = Ppmd7a_DecodeSymbol(&u->v29.ppmd);
    if (c < 0 || rc_overrun(&u->bits)) return -1;
    return c;
}

/* Spec 11.1, after the block's ppm bit. */
static bool ppm_header(rc_unpack_t *u) {
    rc_bits_t *b = &u->bits;
    unsigned f = rc_get(b, 7);
    unsigned mem = 0;
    if (f & 0x20) mem = rc_get(b, 8);
    if (f & 0x40) u->v29.esc = (int)rc_get(b, 8);
#if !RC_PPM_KEEP_ESCAPE
    else u->v29.esc = 2;
#endif
    unsigned order = (f & 0x1F) + 1;
    if (order > 16) order = 16 + (order - 16) * 3;
    if (rc_overrun(b)) return false;
    if (f & 0x20) {
        if ((f & 0x1F) == 0) return false;
        uint32_t size = (mem + 1) << 20;
        if (!u->v29.ppmd_constructed) {
            Ppmd7_Construct(&u->v29.ppmd);
            u->v29.ppmd_constructed = true;
        }
        if (u->v29.ppmd_alloc && u->v29.ppmd_mem != size) rc_unpack29_free(u);
        if (!u->v29.ppmd_alloc) {
            if (!Ppmd7_Alloc(&u->v29.ppmd, size, &PPM_ALLOC)) return false;
            u->v29.ppmd_alloc = true;
            u->v29.ppmd_mem = size;
        }
        Ppmd7_Init(&u->v29.ppmd, order);
        u->v29.ppmd_ready = true;
    } else if (!u->v29.ppmd_ready) return false;
    u->v29.byte_in.vt.Read = ppm_read;
    u->v29.byte_in.u = u;
    u->v29.ppmd.rc.dec.Stream = &u->v29.byte_in.vt;
    if (!Ppmd7a_RangeDec_Init(&u->v29.ppmd.rc.dec)) return false;
    return !rc_overrun(b);
}

/* ---- RAR 3 filter records (spec 10.1) ---- */

typedef struct membr { const uint8_t *p; size_t n, bit; bool bad; } membr_t;

static uint32_t mb_get(membr_t *m, unsigned k) {
    uint32_t v = 0;
    for (unsigned i = 0; i < k; ++i) {
        unsigned bit = 0;
        if (m->bit / 8 < m->n) bit = (m->p[m->bit / 8] >> (7 - m->bit % 8)) & 1;
        else m->bad = true;
        m->bit++;
        v = v << 1 | bit;
    }
    return v;
}

static uint32_t vmnum(membr_t *m) {
    switch (mb_get(m, 2)) {
    case 0: return mb_get(m, 4);
    case 1: {
        uint32_t v = mb_get(m, 8);
        if (v >= 16) return v;
        return 0xFFFFFF00u | v << 4 | mb_get(m, 4);
    }
    case 2: return mb_get(m, 16);
    default: return mb_get(m, 32);
    }
}

/* Spec 10.3: the six standard programs, by CRC-32 and length. */
static int identify(const uint8_t *code, size_t len) {
    static const struct { uint32_t crc; uint32_t len; int type; } STD[6] = {
        { 0xAD576887u, 53, RC_F_E8 }, { 0x3CD7E57Eu, 57, RC_F_E8E9 }, { 0x3769893Fu, 120, RC_F_ITANIUM },
        { 0x0E06077Du, 29, RC_F_DELTA }, { 0x1C2C5DC8u, 149, RC_F_RGB }, { 0xBC85E701u, 216, RC_F_AUDIO },
    };
    uint32_t crc = rc_crc32(0, code, len);
    for (int i = 0; i < 6; ++i) if (STD[i].crc == crc && STD[i].len == len) return STD[i].type;
    return 0;   /* a program for the VM (Appendix A): not supported */
}

/* `next` gives the record's bytes: raw bits in LZ blocks, PPMd symbols in PPMd blocks. */
static status_t read_filter(rc_unpack_t *u, bool ppm) {
    uint8_t head[3];
    size_t hn = 0;
#define NEXT_BYTE(dst)                                                     \
    do {                                                                   \
        if (ppm) { int c_ = ppm_sym(u); if (c_ < 0) return S_BAD; (dst) = (uint8_t)c_; } \
        else (dst) = (uint8_t)rc_get(&u->bits, 8);                         \
    } while (0)
    NEXT_BYTE(head[hn]); hn++;
    unsigned flags = head[0];
    size_t n = (flags & 7) + 1;
    if (n == 7) { uint8_t x; NEXT_BYTE(x); n = (size_t)x + 7; }
    else if (n == 8) { uint8_t x, y; NEXT_BYTE(x); NEXT_BYTE(y); n = (size_t)x << 8 | y; }
    uint8_t *data = (uint8_t*)malloc(n ? n : 1);
    if (!data) return RUBRAVIEW_RAR_UNPACK_NO_MEMORY;
    for (size_t i = 0; i < n; ++i) {
        if (ppm) {
            int c = ppm_sym(u);
            if (c < 0) { free(data); return S_BAD; }
            data[i] = (uint8_t)c;
        } else data[i] = (uint8_t)rc_get(&u->bits, 8);
    }
#undef NEXT_BYTE
    if (rc_overrun(&u->bits)) { free(data); return S_BAD; }

    membr_t m = { data, n, 0, false };
    status_t st = S_BAD;
    size_t num;
    if (flags & 0x80) {
        uint32_t v = vmnum(&m);
        if (v == 0) {
            u->v29.prog_count = 0;
#if !RC_VM_RESET_KEEPS_PENDING
            u->fcount = 0;
#endif
            num = 0;
        } else num = v - 1;
        if (num > u->v29.prog_count) goto out;
        u->v29.last_num = num;
    } else num = u->v29.last_num;
    if (num > u->v29.prog_count) goto out;
    uint64_t rel = (uint64_t)vmnum(&m) + rc_file_pos(u);
    if (flags & 0x40) rel += 258;
    uint32_t length = (flags & 0x20) ? vmnum(&m) : (num < u->v29.prog_count ? u->v29.prog[num].last_length : 0);
    uint32_t R[7] = {0};
    R[3] = 0x3C000; R[4] = length; R[5] = num < u->v29.prog_count ? u->v29.prog[num].uses : 0;
    if (flags & 0x10) {
        unsigned mask = mb_get(&m, 7);
        for (unsigned i = 0; i < 7; ++i) if (mask & (1u << i)) R[i] = vmnum(&m);
    }
    if (num == u->v29.prog_count) {
        uint32_t clen = vmnum(&m);
        if (clen < 1 || clen > 0x10000 || u->v29.prog_count >= 1024 || m.bad) goto out;
        uint8_t *code = (uint8_t*)malloc(clen);
        if (!code) { st = RUBRAVIEW_RAR_UNPACK_NO_MEMORY; goto out; }
        uint8_t x = 0;
        for (uint32_t i = 0; i < clen; ++i) {
            code[i] = (uint8_t)mb_get(&m, 8);
            if (i > 0) x ^= code[i];
        }
        bool ok = !m.bad && x == code[0];
        int type = ok ? identify(code, clen) : 0;
        free(code);
        if (!ok) goto out;
        u->v29.prog[num].type = type;
        u->v29.prog[num].uses = 0;
        u->v29.prog_count++;
    }
    u->v29.prog[num].last_length = length;
    if (flags & 0x08) {
        uint32_t glen = vmnum(&m);
        if (glen > 0x2000 - 0x40) goto out;
        for (uint32_t i = 0; i < glen; ++i) (void)mb_get(&m, 8);   /* only the VM uses it */
    }
    if (m.bad) goto out;
    int type = u->v29.prog[num].type;
    u->v29.prog[num].uses++;
    if (type == 0) { st = RUBRAVIEW_RAR_UNPACK_UNSUPPORTED; goto out; }

    /* validity (spec 10.1) */
    if (length > 0x3C000) goto out;
    if ((type == RC_F_E8 || type == RC_F_E8E9) && length <= 4) goto out;
    if ((type == RC_F_DELTA || type == RC_F_RGB || type == RC_F_AUDIO) && length > 0x3C000 / 2) goto out;
    if ((type == RC_F_DELTA || type == RC_F_AUDIO) && (R[0] == 0 || R[0] > length)) goto out;
    if (type == RC_F_RGB && (length < 3 || R[0] < 3 || R[0] > length || R[1] > 2)) goto out;
    rc_filter_t f = {0};
    f.type = type;
    f.rar5 = false;
    f.start = u->file_start + rel;
    f.length = length;
    f.file_off = rel;
    memcpy(f.r, R, sizeof(f.r));
    if (u->fcount > 0 && f.start < u->filters[u->fcount - 1].start) goto out;
    if (length == 0) { st = S_OK; goto out; }
    st = rc_add_filter(u, &f) ? S_OK : S_BAD;
out:
    free(data);
    return st;
}

/* ---- LZ (spec 9) ---- */

static bool lz_tables(rc_unpack_t *u) {
    rc_bits_t *b = &u->bits;
    if (!rc_get(b, 1)) memset(u->v29.lengths, 0, sizeof(u->v29.lengths));   /* keep = 0 */
    if (!rc_read_lengths(b, u->v29.lengths, MC + DC + LDC + RC, true)) return false;
    const uint8_t *l = u->v29.lengths;
    if (!rc_huff_build(&u->v29.mc, l, MC) || !rc_huff_build(&u->v29.dc, l + MC, DC) ||
        !rc_huff_build(&u->v29.ldc, l + MC + DC, LDC) || !rc_huff_build(&u->v29.rc, l + MC + DC + LDC, RC))
        return false;
#if RC_LOWDIST_RESET_AT_TABLES
    u->v29.low_dist = u->v29.low_repeat = 0;
#endif
    return true;
}

/* Spec 9.1: a block header at a byte boundary. */
static bool block_header(rc_unpack_t *u) {
    rc_bits_t *b = &u->bits;
    rc_align(b);
    u->v29.ppm_block = rc_get(b, 1);
    if (u->v29.ppm_block) return ppm_header(u);
    u->v29.tables_ok = lz_tables(u);
    return u->v29.tables_ok;
}

static void push(rc_unpack_t *u, uint32_t d) {
    u->v29.D[3] = u->v29.D[2]; u->v29.D[2] = u->v29.D[1]; u->v29.D[1] = u->v29.D[0]; u->v29.D[0] = d;
}

typedef enum { RUN_MORE, RUN_BLOCK, RUN_EOF } run_t;

/* Decodes LZ symbols until a block or the file ends (or enough is decoded, when not draining). */
static status_t lz_run(rc_unpack_t *u, bool drain, run_t *how) {
    rc_bits_t *b = &u->bits;
    *how = RUN_MORE;
    for (;;) {
        if (!drain && rc_file_done(u)) return S_OK;
        if (rc_overrun(b)) {
            if (drain && rc_file_done(u)) { *how = RUN_EOF; u->v29.need_header = true; return S_OK; }
            return S_BAD;
        }
        if (!rc_room(u)) return u->stopped ? RUBRAVIEW_RAR_UNPACK_STOPPED : S_BAD;
        int s = rc_huff_decode(&u->v29.mc, b);
        if (s < 0) {
            if (drain && rc_file_done(u) && rc_overrun(b)) { *how = RUN_EOF; u->v29.need_header = true; return S_OK; }
            return S_BAD;
        }
        if (s < 256) { rc_put(u, (uint8_t)s); continue; }
        if (s == 256) {
            if (rc_get(b, 1)) { *how = RUN_BLOCK; return S_OK; }
            u->v29.need_header = rc_get(b, 1);
            *how = RUN_EOF;
            return rc_overrun(b) ? S_BAD : S_OK;
        }
        if (s == 257) {
            status_t st = read_filter(u, false);
            if (st != S_OK) return st;
            continue;
        }
        uint32_t d, len;
        if (s == 258) {
            if (u->v29.L == 0) continue;
            d = u->v29.D[0];
            len = u->v29.L;
        } else if (s < 263) {
            unsigned j = (unsigned)s - 259;
            d = u->v29.D[j];
            for (unsigned k = j; k > 0; --k) u->v29.D[k] = u->v29.D[k - 1];
            u->v29.D[0] = d;
            int l = rc_huff_decode(&u->v29.rc, b);
            if (l < 0) return S_BAD;
            len = rc_lbase[l] + 2 + rc_get(b, rc_lbits[l]);
            u->v29.L = len;
        } else if (s < 271) {
            unsigned j = (unsigned)s - 263;
            d = rc_sdbase[j] + 1 + rc_get(b, rc_sdbits[j]);
            push(u, d);
            len = 2;
            u->v29.L = len;
        } else {
            unsigned j = (unsigned)s - 271;
            len = rc_lbase[j] + 3 + rc_get(b, rc_lbits[j]);
            int k = rc_huff_decode(&u->v29.dc, b);
            if (k < 0) return S_BAD;
            d = rc_dbase[k] + 1;
            unsigned bits = rc_dbits[k];
            if (bits >= 4) {
                if (bits > 4) d += rc_get(b, bits - 4) << 4;
                if (u->v29.low_repeat > 0) {
                    u->v29.low_repeat--;
                    d += u->v29.low_dist;
                } else {
                    int x = rc_huff_decode(&u->v29.ldc, b);
                    if (x < 0) return S_BAD;
                    if (x == 16) { u->v29.low_repeat = 15; d += u->v29.low_dist; }
                    else { d += (uint32_t)x; u->v29.low_dist = (uint32_t)x; }
                }
            } else if (bits > 0) d += rc_get(b, bits);
            if (d >= 0x2000) len++;
            if (d >= 0x40000) len++;
            push(u, d);
            u->v29.L = len;
        }
        if (rc_overrun(b) && !(drain && rc_file_done(u))) return S_BAD;
        if (!rc_copy(u, d, len)) return S_BAD;
    }
}

/* Spec 11.3. */
static status_t ppm_run(rc_unpack_t *u, bool drain, run_t *how) {
    *how = RUN_MORE;
    for (;;) {
        if (!drain && rc_file_done(u)) return S_OK;
        if (!rc_room(u)) return u->stopped ? RUBRAVIEW_RAR_UNPACK_STOPPED : S_BAD;
        int c = ppm_sym(u);
        if (c < 0) {
            if (drain && rc_file_done(u)) { *how = RUN_EOF; u->v29.need_header = true; return S_OK; }
            return S_BAD;
        }
        if (c != u->v29.esc) { rc_put(u, (uint8_t)c); continue; }
        int code = ppm_sym(u);
        if (code < 0) return S_BAD;
        switch (code) {
        case 0: *how = RUN_BLOCK; return S_OK;
        case 2: u->v29.need_header = true; *how = RUN_EOF; return S_OK;
        case 3: {
            status_t st = read_filter(u, true);
            if (st != S_OK) return st;
            break;
        }
        case 4: {
            uint32_t d = 0;
            for (int i = 0; i < 3; ++i) {
                int x = ppm_sym(u);
                if (x < 0) return S_BAD;
                d = d << 8 | (uint32_t)x;
            }
            int l = ppm_sym(u);
            if (l < 0 || !rc_copy(u, (uint64_t)d + 2, (uint64_t)l + 32)) return S_BAD;
            break;
        }
        case 5: {
            int l = ppm_sym(u);
            if (l < 0 || !rc_copy(u, 1, (uint64_t)l + 4)) return S_BAD;
            break;
        }
        default: rc_put(u, (uint8_t)u->v29.esc); break;
        }
    }
}

status_t rc_unpack29(rc_unpack_t *u, bool solid, bool drain) {
    if (!solid) {
        memset(u->v29.lengths, 0, sizeof(u->v29.lengths));
        memset(u->v29.D, 0, sizeof(u->v29.D));
        u->v29.L = 0;
        u->v29.low_dist = u->v29.low_repeat = 0;
        u->v29.need_header = true;
        u->v29.tables_ok = false;
        u->v29.ppm_block = false;
        u->v29.ppmd_ready = false;
        u->v29.esc = 2;
        u->v29.prog_count = 0;
        u->v29.last_num = 0;
    }
    for (;;) {
        if (u->v29.need_header) {
            if (!drain && rc_file_done(u)) return S_OK;
            if (rc_exhausted(&u->bits) && rc_file_done(u)) return S_OK;
            if (!block_header(u)) return S_BAD;
            u->v29.need_header = false;
        } else if (!u->v29.ppm_block && !u->v29.tables_ok) return S_BAD;
        run_t how;
        status_t st = u->v29.ppm_block ? ppm_run(u, drain, &how) : lz_run(u, drain, &how);
        if (st != S_OK) return st;
        if (how == RUN_EOF) return S_OK;
        if (how == RUN_MORE) return S_OK;   /* enough decoded (not draining) */
        u->v29.need_header = true;          /* end of block: another follows in this file */
    }
}
