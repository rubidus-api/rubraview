/*
 * Rubraview's RAR codec (MIT): the filters that RAR 3 (standard VM programs) and RAR 5 apply to ranges of
 * the output. Written from docs/specs/rar-decompression.md, sections 10.4 and 12.5.
 */
#include "rc_unpack.h"
#include <string.h>

static uint32_t ld32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void st32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static int iabs(int v) { return v < 0 ? -v : v; }
static int signed8(unsigned v) { v &= 0xFF; return v >= 0x80 ? (int)v - 256 : (int)v; }

/* E8 / E8E9: 10.4; RAR 5 takes the position modulo 2^24 (12.5). */
static void filter_e8(uint8_t *buf, size_t n, uint64_t off, bool e9, bool rar5) {
    const uint32_t FS = 0x1000000;
    if (n < 5) return;
    for (size_t i = 0; i <= n - 5;) {
        if (buf[i] == 0xE8 || (e9 && buf[i] == 0xE9)) {
            uint32_t pos = (uint32_t)(off + i + 1);
            if (rar5) pos &= 0xFFFFFF;
            uint32_t addr = ld32(buf + i + 1);
            if (addr & 0x80000000u) {
                if (!((addr + pos) & 0x80000000u)) st32(buf + i + 1, addr + FS);
            } else if (addr < FS) st32(buf + i + 1, addr - pos);
            i += 5;
        } else i++;
    }
}

static void filter_delta(const uint8_t *in, uint8_t *out, size_t n, unsigned ch) {
    size_t src = 0;
    for (unsigned c = 0; c < ch; ++c) {
        uint8_t prev = 0;
        for (size_t j = c; j < n; j += ch) {
            prev = (uint8_t)(prev - in[src++]);
            out[j] = prev;
        }
    }
}

static void filter_arm(uint8_t *buf, size_t n, uint64_t off) {
    for (size_t i = 0; i + 3 < n; i += 4) {
        if (buf[i + 3] != 0xEB) continue;
        uint32_t v = (uint32_t)buf[i] | (uint32_t)buf[i + 1] << 8 | (uint32_t)buf[i + 2] << 16;
        v = (v - (uint32_t)((off + i) / 4)) & 0xFFFFFF;
        buf[i] = (uint8_t)v; buf[i + 1] = (uint8_t)(v >> 8); buf[i + 2] = (uint8_t)(v >> 16);
    }
}

static uint32_t getbits(const uint8_t *p, unsigned bp, unsigned c) {
    return (ld32(p + bp / 8) >> (bp % 8)) & ((1u << c) - 1);
}
static void setbits(uint8_t *p, unsigned bp, unsigned c, uint32_t v) {
    uint32_t mask = ((1u << c) - 1) << (bp % 8);
    uint32_t x = ld32(p + bp / 8);
    st32(p + bp / 8, (x & ~mask) | ((v << (bp % 8)) & mask));
}

static void filter_itanium(uint8_t *buf, size_t n, uint64_t off) {
    static const uint8_t MASK[16] = { 4, 4, 6, 6, 0, 0, 7, 7, 4, 4, 0, 0, 4, 4, 0, 0 };
    uint32_t fo = (uint32_t)(off >> 4);
    for (size_t p = 0; n - p > 21; p += 16, fo++) {
        int t = (buf[p] & 0x1F) - 0x10;
        if (t < 0 || MASK[t] == 0) continue;
        for (unsigned slot = 0; slot < 3; ++slot) {
            if (!(MASK[t] & (1u << slot))) continue;
            unsigned bp = slot * 41 + 18;
            if (getbits(buf + p, bp + 24, 4) == 5) {
                uint32_t v = getbits(buf + p, bp, 20);
                setbits(buf + p, bp, 20, (v - fo) & 0xFFFFF);
            }
        }
    }
}

static void filter_rgb(const uint8_t *in, uint8_t *out, size_t n, uint32_t w, uint32_t pos_r) {
    size_t src = 0;
    for (unsigned c = 0; c < 3; ++c) {
        unsigned prev = 0;
        for (size_t j = c; j < n; j += 3) {
            unsigned pred = prev;
            if (j >= w) {
                int up = out[j - w + 3], upleft = out[j - w];
                int pa = iabs(up - upleft), pb = iabs((int)prev - upleft), pc = iabs(up - upleft + (int)prev - upleft);
                if (pa <= pb && pa <= pc) pred = prev;
                else if (pb <= pc) pred = (unsigned)up;
                else pred = (unsigned)upleft;
            }
            prev = (pred - in[src++]) & 0xFF;
            out[j] = (uint8_t)prev;
        }
    }
    for (size_t i = pos_r; n >= 3 && i <= n - 3; i += 3) {
        out[i] = (uint8_t)(out[i] + out[i + 1]);
        out[i + 2] = (uint8_t)(out[i + 2] + out[i + 1]);
    }
}

static void filter_audio(const uint8_t *in, uint8_t *out, size_t n, unsigned ch) {
    size_t src = 0;
    for (unsigned c = 0; c < ch; ++c) {
        int W[3] = {0}, Dl[3] = {0}, last_delta = 0, dif[7] = {0}, count = 0;
        unsigned last = 0;
        for (size_t j = c; j < n; j += ch) {
            Dl[2] = Dl[1];
            Dl[1] = last_delta - Dl[0];
            Dl[0] = last_delta;
            int p = 8 * (int)last + W[0] * Dl[0] + W[1] * Dl[1] + W[2] * Dl[2];
            unsigned pred = (unsigned)(p >> 3) & 0xFF;   /* arithmetic shift of a signed value */
            int e = signed8(in[src++]);
            unsigned bt = (pred - (unsigned)e) & 0xFF;
            int e8 = e * 8;
            dif[0] += iabs(e8);
            dif[1] += iabs(e8 - Dl[0]); dif[2] += iabs(e8 + Dl[0]);
            dif[3] += iabs(e8 - Dl[1]); dif[4] += iabs(e8 + Dl[1]);
            dif[5] += iabs(e8 - Dl[2]); dif[6] += iabs(e8 + Dl[2]);
            last_delta = signed8(bt - last);
            last = bt;
            out[j] = (uint8_t)bt;
            if ((count & 0x1F) == 0) {
                int k = 0;
                for (int q = 1; q < 7; ++q) if (dif[q] < dif[k]) k = q;
                for (int q = 0; q < 7; ++q) dif[q] = 0;
                switch (k) {
                case 1: if (W[0] >= -16) W[0]--; break;
                case 2: if (W[0] < 16) W[0]++; break;
                case 3: if (W[1] >= -16) W[1]--; break;
                case 4: if (W[1] < 16) W[1]++; break;
                case 5: if (W[2] >= -16) W[2]--; break;
                case 6: if (W[2] < 16) W[2]++; break;
                default: break;
                }
            }
            count++;
        }
    }
}

void rc_run_filter(const rc_filter_t *f, uint8_t *data, uint8_t *tmp, size_t n) {
    switch (f->type) {
    case RC_F_E8: filter_e8(data, n, f->file_off, false, f->rar5); break;
    case RC_F_E8E9: filter_e8(data, n, f->file_off, true, f->rar5); break;
    case RC_F_ARM: filter_arm(data, n, f->file_off); break;
    case RC_F_ITANIUM: filter_itanium(data, n, f->file_off); break;
    case RC_F_DELTA:
        filter_delta(data, tmp, n, f->r[0]);
        memcpy(data, tmp, n);
        break;
    case RC_F_RGB:
        filter_rgb(data, tmp, n, f->r[0], f->r[1]);
        memcpy(data, tmp, n);
        break;
    case RC_F_AUDIO:
        filter_audio(data, tmp, n, f->r[0]);
        memcpy(data, tmp, n);
        break;
    default: break;
    }
}
