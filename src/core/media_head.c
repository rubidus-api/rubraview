/*
 * RV-210: a file's size as shown and its length from small pieces of it, by our own parsers
 * (see media_head.h). Written from the formats' specifications: JPEG (T.81) and EXIF, PNG, GIF89a,
 * BMP, TIFF 6.0 and BigTIFF, WebP's RIFF container, ICO, HEIF / AVIF (ISO/IEC 23008-12), JPEG XL
 * (ISO/IEC 18181), ISO BMFF (14496-12), Matroska / EBML (RFC 8794, 9559), AVI and OpenDML, ASF,
 * MPEG-2 systems (13818-1) with MPEG-2 video, H.264 and H.265 parameter sets, FLV, Ogg (RFC 3533)
 * with Vorbis, Opus (RFC 7845) and Theora, FLAC, RIFF WAVE and RF64, MPEG audio with its Xing and
 * VBRI frames, and AAC's ADTS. Every size and offset in a file is a claim, checked before it is used.
 */
#include "rubraview/media_head.h"
#include "rubraview/mp4.h"
#include "rubraview/matroska.h"
#include "rubraview/exif.h"

#include <stdlib.h>
#include <string.h>

/* ---- the source: a few windows of the file ---- */

void rubraview_head_src_reset(rubraview_head_src_t *src, rubraview_head_read_fn read, void *context, uint64_t size) {
    src->read = read;
    src->context = context;
    src->size = size;
    for (unsigned i = 0; i < 3; ++i) { src->win[i].len = 0; src->win[i].at = 0; }
    src->next = 1;
    src->reads = 0;
    src->bytes = 0;
}

void rubraview_head_src_free(rubraview_head_src_t *src) {
    if (!src) return;
    for (unsigned i = 0; i < 3; ++i) free(src->win[i].data);
    free(src->scratch);
    *src = (rubraview_head_src_t){0};
}

static bool load(rubraview_head_src_t *src, unsigned slot, uint64_t at, size_t len) {
    if (!src->read || at >= src->size) return false;
    if ((uint64_t)len > src->size - at) len = (size_t)(src->size - at);
    if (len == 0 || src->reads >= RUBRAVIEW_HEAD_MAX_READS || src->bytes + len > RUBRAVIEW_HEAD_MAX_BYTES) return false;
    rubraview_head_window_t *w = &src->win[slot];
    if (w->cap < len) {
        uint8_t *grown = (uint8_t *)realloc(w->data, len);
        if (!grown) return false;
        w->data = grown;
        w->cap = len;
    }
    src->reads++;
    src->bytes += len;
    size_t got = 0;
    w->len = 0;
    if (!src->read(src->context, at, w->data, len, &got) || got == 0) return false;
    w->at = at;
    w->len = got < len ? got : len;
    return true;
}

static const rubraview_head_window_t *holding(const rubraview_head_src_t *src, uint64_t at, size_t len) {
    for (unsigned i = 0; i < 3; ++i) {
        const rubraview_head_window_t *w = &src->win[i];
        if (w->len > 0 && at >= w->at && at - w->at < w->len && len <= w->len - (size_t)(at - w->at)) return w;
    }
    return NULL;
}

const uint8_t *rubraview_head_view(rubraview_head_src_t *src, uint64_t at, size_t want, size_t *out_got) {
    *out_got = 0;
    if (!src || at >= src->size || want == 0) return NULL;
    if (src->reads == 0 && !load(src, 0, 0, RUBRAVIEW_HEAD_FIRST)) return NULL;
    size_t need = (uint64_t)want < src->size - at ? want : (size_t)(src->size - at);
    const rubraview_head_window_t *w = holding(src, at, need);
    if (!w) {
        unsigned slot = src->next;
        if (load(src, slot, at, need > RUBRAVIEW_HEAD_LATER ? need : RUBRAVIEW_HEAD_LATER)) {
            src->next = slot == 1 ? 2 : 1;
            w = &src->win[slot];
        } else {
            w = holding(src, at, 1);   /* what is there already, short of what was wanted */
        }
    }
    if (!w) return NULL;
    size_t off = (size_t)(at - w->at), left = w->len - off;
    *out_got = left < want ? left : want;
    return w->data + off;
}

const uint8_t *rubraview_head_bytes(rubraview_head_src_t *src, uint64_t at, size_t len) {
    size_t got = 0;
    const uint8_t *p = rubraview_head_view(src, at, len, &got);
    return p && got >= len ? p : NULL;
}

static void *scratch(rubraview_head_src_t *src, size_t size) {
    if (src->scratch_cap < size) {
        void *grown = realloc(src->scratch, size);
        if (!grown) return NULL;
        src->scratch = grown;
        src->scratch_cap = size;
    }
    memset(src->scratch, 0, size);
    return src->scratch;
}

/* ---- numbers ---- */

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be24(const uint8_t *p) { return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]; }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint64_t be64(const uint8_t *p) { return ((uint64_t)be32(p) << 32) | be32(p + 4); }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le24(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16); }
static uint32_t le32(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t le64(const uint8_t *p) { return le32(p) | ((uint64_t)le32(p + 4) << 32); }

#define SIDE_MAX 1000000u   /* a side longer than this is no claim to believe */

/* The size, turned by an EXIF orientation (5 to 8 a quarter turn); nothing when either side is not believable. */
static void set_size(rubraview_media_facts_t *out, uint64_t w, uint64_t h, int32_t orientation) {
    if (w == 0 || h == 0 || w > SIDE_MAX || h > SIDE_MAX) return;
    bool side = orientation >= 5 && orientation <= 8;
    out->width = (int32_t)(side ? h : w);
    out->height = (int32_t)(side ? w : h);
}

static void set_seconds(rubraview_media_facts_t *out, double seconds, bool estimated) {
    if (!(seconds > 0.0) || seconds > 1e7) return;   /* past 115 days: not a length */
    out->seconds = seconds;
    out->estimated = estimated;
}

/* Bits, first bit the lowest (JPEG XL) or the highest (H.264, H.265). */
typedef struct bits { const uint8_t *p; size_t n, at; bool bad; } bits_t;

static uint32_t lsb(bits_t *b, unsigned k) {
    uint32_t v = 0;
    for (unsigned i = 0; i < k; ++i, ++b->at) {
        if ((b->at >> 3) >= b->n) { b->bad = true; return 0; }
        v |= (uint32_t)((b->p[b->at >> 3] >> (b->at & 7)) & 1u) << i;
    }
    return v;
}

static uint32_t msb(bits_t *b, unsigned k) {
    uint32_t v = 0;
    for (unsigned i = 0; i < k; ++i, ++b->at) {
        if ((b->at >> 3) >= b->n) { b->bad = true; return 0; }
        v = (v << 1) | ((b->p[b->at >> 3] >> (7 - (b->at & 7))) & 1u);
    }
    return v;
}

static uint32_t ue(bits_t *b) {
    unsigned zeros = 0;
    while (!b->bad && msb(b, 1) == 0) { if (++zeros > 31) { b->bad = true; return 0; } }
    if (zeros == 0) return 0;
    return (uint32_t)(((1ull << zeros) - 1) + msb(b, zeros));
}

static int32_t se(bits_t *b) {
    uint32_t k = ue(b);
    return k & 1 ? (int32_t)((k + 1) / 2) : -(int32_t)(k / 2);
}

/* ---- pictures ---- */

static bool is_sof(uint8_t code) {
    return code >= 0xC0 && code <= 0xCF && code != 0xC4 && code != 0xC8 && code != 0xCC;
}

static bool read_jpeg(rubraview_head_src_t *src, rubraview_media_facts_t *out) {
    out->format = "jpeg";
    int32_t orientation = 1;
    uint64_t at = 2;
    for (unsigned guard = 0; guard < 1024; ++guard) {
        const uint8_t *m = rubraview_head_bytes(src, at, 4);
        if (!m || m[0] != 0xFF) return true;
        uint8_t code = m[1];
        if (code == 0xFF) { at += 1; continue; }                                    /* fill */
        if (code == 0x01 || code == 0xD8 || (code >= 0xD0 && code <= 0xD7)) { at += 2; continue; }
        if (code == 0xD9 || code == 0xDA) return true;                              /* no frame before the scan */
        uint16_t len = be16(m + 2);
        if (len < 2) return true;
        if (is_sof(code)) {
            const uint8_t *f = rubraview_head_bytes(src, at + 4, 5);
            if (f) set_size(out, be16(f + 3), be16(f + 1), orientation);
            return true;
        }
        if (code == 0xE1 && len >= 8 && orientation == 1) {
            size_t got = 0;
            const uint8_t *p = rubraview_head_view(src, at + 4, (size_t)len - 2, &got);
            if (p && got >= 14 && memcmp(p, "Exif\0\0", 6) == 0) orientation = rubraview_exif_tiff_orientation(p + 6, got - 6);
        }
        at += 2u + len;
    }
    return true;
}

static bool read_png(rubraview_head_src_t *src, rubraview_media_facts_t *out) {
    out->format = "png";
    const uint8_t *p = rubraview_head_bytes(src, 0, 24);
    if (p && memcmp(p + 12, "IHDR", 4) == 0) set_size(out, be32(p + 16), be32(p + 20), 1);
    return true;
}

static bool read_gif(rubraview_head_src_t *src, rubraview_media_facts_t *out) {
    out->format = "gif";
    const uint8_t *p = rubraview_head_bytes(src, 0, 10);
    if (p) set_size(out, le16(p + 6), le16(p + 8), 1);
    return true;
}

static bool read_bmp(rubraview_head_src_t *src, rubraview_media_facts_t *out) {
    out->format = "bmp";
    const uint8_t *p = rubraview_head_bytes(src, 0, 26);
    if (!p) return true;
    if (le32(p + 14) == 12) { set_size(out, le16(p + 18), le16(p + 20), 1); return true; }   /* OS/2's core header */
    int32_t w = (int32_t)le32(p + 18), h = (int32_t)le32(p + 22);
    if (w > 0 && h != 0 && h != INT32_MIN) set_size(out, (uint64_t)w, (uint64_t)(h < 0 ? -h : h), 1);   /* below 0: top-down */
    return true;
}

static bool read_tiff(rubraview_head_src_t *src, rubraview_media_facts_t *out) {
    out->format = "tiff";
    const uint8_t *p = rubraview_head_bytes(src, 0, 16);
    if (!p) return true;
    bool big_endian = p[0] == 'M';
    uint16_t (*u16)(const uint8_t *) = big_endian ? be16 : le16;
    uint32_t (*u32)(const uint8_t *) = big_endian ? be32 : le32;
    bool big = u16(p + 2) == 43;
    uint64_t ifd = big ? (big_endian ? be64(p + 8) : le64(p + 8)) : u32(p + 4);
    const uint8_t *c = rubraview_head_bytes(src, ifd, big ? 8 : 2);
    if (!c) return true;
    uint64_t count = big ? (big_endian ? be64(c) : le64(c)) : u16(c);
    size_t entry = big ? 20 : 12;
    if (count == 0 || count > 4096) return true;
    const uint8_t *e = rubraview_head_bytes(src, ifd + (big ? 8 : 2), (size_t)count * entry);
    if (!e) return true;
    uint64_t w = 0, h = 0;
    int32_t orientation = 1;
    for (size_t i = 0; i < (size_t)count; ++i, e += entry) {
        uint16_t tag = u16(e), type = u16(e + 2);
        const uint8_t *v = e + (big ? 12 : 8);
        uint64_t value = type == 3 ? u16(v) : type == 4 ? u32(v) : type == 16 ? (big_endian ? be64(v) : le64(v)) : 0;
        if (tag == 256) w = value;
        else if (tag == 257) h = value;
        else if (tag == 274 && value >= 1 && value <= 8) orientation = (int32_t)value;
    }
    set_size(out, w, h, orientation);
    return true;
}

/* WebP: the canvas (VP8X), or the one frame's size (VP8, VP8L). The viewer turns no WebP by its EXIF. */
static bool read_webp(rubraview_head_src_t *src, rubraview_media_facts_t *out) {
    out->format = "webp";
    const uint8_t *p = rubraview_head_bytes(src, 0, 30);
    if (!p) return true;
    if (memcmp(p + 12, "VP8X", 4) == 0) set_size(out, 1u + le24(p + 24), 1u + le24(p + 27), 1);
    else if (memcmp(p + 12, "VP8 ", 4) == 0 && p[23] == 0x9D && p[24] == 0x01 && p[25] == 0x2A)
        set_size(out, le16(p + 26) & 0x3FFFu, le16(p + 28) & 0x3FFFu, 1);
    else if (memcmp(p + 12, "VP8L", 4) == 0 && p[20] == 0x2F) {
        const uint8_t *b = p + 21;
        set_size(out, 1u + (((uint32_t)(b[1] & 0x3F) << 8) | b[0]),
                 1u + (((uint32_t)(b[3] & 0x0F) << 10) | ((uint32_t)b[2] << 2) | ((uint32_t)(b[1] & 0xC0) >> 6)), 1);
    }
    return true;
}

/* An icon: its largest picture (a side of 0 is 256). */
static bool read_ico(rubraview_head_src_t *src, rubraview_media_facts_t *out) {
    out->format = "ico";
    const uint8_t *p = rubraview_head_bytes(src, 0, 6);
    if (!p) return true;
    uint16_t count = le16(p + 4);
    const uint8_t *e = count ? rubraview_head_bytes(src, 6, (size_t)count * 16) : NULL;
    uint32_t best_w = 0, best_h = 0;
    for (uint16_t i = 0; e && i < count; ++i) {
        uint32_t w = e[i * 16u] ? e[i * 16u] : 256u, h = e[i * 16u + 1] ? e[i * 16u + 1] : 256u;
        if (w * h > best_w * best_h) { best_w = w; best_h = h; }
    }
    set_size(out, best_w, best_h, 1);
    return true;
}

/* ISO BMFF boxes over the source: a header at `at` (32-bit size, 1 and a 64-bit one, or 0 for "to the end"). */
typedef struct head_box { uint32_t type; uint64_t at, body, end; } head_box_t;

static bool box_next(rubraview_head_src_t *src, uint64_t at, uint64_t limit, head_box_t *out) {
    if (at >= limit || limit - at < 8) return false;
    const uint8_t *p = rubraview_head_bytes(src, at, limit - at >= 16 ? 16 : 8);
    if (!p) return false;
    uint64_t size = be32(p), header = 8;
    if (size == 1) {
        if (limit - at < 16) return false;
        size = be64(p + 8);
        header = 16;
    } else if (size == 0) {
        size = limit - at;
    }
    if (size < header || size > limit - at) return false;
    *out = (head_box_t){ .type = be32(p + 4), .at = at, .body = at + header, .end = at + size };
    return true;
}

#define FOURCC(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

/* A box inside a buffer: as box_next, over memory. */
static bool mem_box(const uint8_t *p, size_t n, size_t at, uint32_t *type, size_t *body, size_t *end) {
    if (at >= n || n - at < 8) return false;
    uint64_t size = be32(p + at), header = 8;
    if (size == 1) {
        if (n - at < 16) return false;
        size = be64(p + at + 8);
        header = 16;
    } else if (size == 0) {
        size = n - at;
    }
    if (size < header || size > n - at) return false;
    *type = be32(p + at + 4);
    *body = at + (size_t)header;
    *end = at + (size_t)size;
    return true;
}

/* HEIF and AVIF: the primary item's `ispe`, turned by its `irot`. */
static bool heif_meta(const uint8_t *m, size_t n, rubraview_media_facts_t *out) {
    uint32_t primary = 0;
    size_t ipco = 0, ipco_end = 0, ipma = 0, ipma_end = 0;
    uint32_t type = 0;
    size_t body = 0, end = 0;
    for (size_t at = 4; mem_box(m, n, at, &type, &body, &end); at = end) {   /* past the FullBox's version and flags */
        if (type == FOURCC('p', 'i', 't', 'm') && end - body >= 6) primary = m[body] == 0 ? be16(m + body + 4) : (end - body >= 8 ? be32(m + body + 4) : 0);
        if (type == FOURCC('i', 'p', 'r', 'p')) {
            uint32_t t2 = 0;
            size_t b2 = 0, e2 = 0;
            for (size_t q = body; mem_box(m, end, q, &t2, &b2, &e2); q = e2) {
                if (t2 == FOURCC('i', 'p', 'c', 'o')) { ipco = b2; ipco_end = e2; }
                if (t2 == FOURCC('i', 'p', 'm', 'a')) { ipma = b2; ipma_end = e2; }
            }
        }
    }
    if (!ipco) return false;
    /* the properties the primary item has, by their place in ipco (from 1) */
    uint16_t wanted[64];
    size_t nwanted = 0;
    bool all = primary == 0 || !ipma;
    if (!all && ipma_end - ipma >= 8) {
        uint8_t version = m[ipma];
        bool wide = m[ipma + 3] & 1;
        uint32_t entries = be32(m + ipma + 4);
        size_t at = ipma + 8;
        for (uint32_t i = 0; i < entries && at < ipma_end; ++i) {
            uint32_t item = 0;
            if (version < 1) { if (ipma_end - at < 2) break; item = be16(m + at); at += 2; }
            else { if (ipma_end - at < 4) break; item = be32(m + at); at += 4; }
            if (at >= ipma_end) break;
            uint8_t count = m[at++];
            for (uint8_t k = 0; k < count; ++k) {
                uint16_t index = 0;
                if (wide) { if (ipma_end - at < 2) break; index = be16(m + at) & 0x7FFF; at += 2; }
                else { if (ipma_end - at < 1) break; index = m[at] & 0x7F; at += 1; }
                if (item == primary && nwanted < 64) wanted[nwanted++] = index;
            }
            if (item == primary) break;
        }
    }
    uint64_t w = 0, h = 0;
    int32_t turn = 1;
    uint16_t index = 0;
    for (size_t at = ipco; mem_box(m, ipco_end, at, &type, &body, &end); at = end) {
        ++index;
        bool mine = all;
        for (size_t k = 0; k < nwanted && !mine; ++k) mine = wanted[k] == index;
        if (!mine) continue;
        if (type == FOURCC('i', 's', 'p', 'e') && end - body >= 12 && w == 0) { w = be32(m + body + 4); h = be32(m + body + 8); }
        if (type == FOURCC('i', 'r', 'o', 't') && end - body >= 1 && (m[body] & 1)) turn = 6;   /* a quarter turn either way */
    }
    set_size(out, w, h, turn);
    return w > 0;
}

static bool read_mp4(rubraview_head_src_t *src, rubraview_media_facts_t *out);

static bool read_heif(rubraview_head_src_t *src, rubraview_media_facts_t *out, const char *format) {
    out->format = format;
    head_box_t b;
    for (uint64_t at = 0; box_next(src, at, src->size, &b); at = b.end) {
        if (b.type == FOURCC('m', 'e', 't', 'a') && b.end - b.body <= (1u << 20)) {
            const uint8_t *m = rubraview_head_bytes(src, b.body, (size_t)(b.end - b.body));
            if (m && heif_meta(m, (size_t)(b.end - b.body), out)) return true;
            break;
        }
        if (b.end <= at) break;
    }
    (void)read_mp4(src, out);   /* an image sequence with no still: its track */
    out->format = format;
    return true;
}

/* JPEG XL: SizeHeader, then the image's orientation (ISO/IEC 18181-1, A.2 and A.3). */
static uint32_t jxl_u32(bits_t *b) {
    static const unsigned extra[4] = { 9, 13, 18, 30 };
    return 1u + lsb(b, extra[lsb(b, 2)]);
}

static bool read_jxl_code(rubraview_head_src_t *src, uint64_t at, rubraview_media_facts_t *out) {
    size_t got = 0;
    const uint8_t *p = rubraview_head_view(src, at, 64, &got);
    if (!p || got < 4 || p[0] != 0xFF || p[1] != 0x0A) return true;
    bits_t b = { .p = p + 2, .n = got - 2 };
    bool div8 = lsb(&b, 1);
    uint64_t h = div8 ? (1u + lsb(&b, 5)) * 8u : jxl_u32(&b);
    uint32_t ratio = lsb(&b, 3);
    uint64_t w = 0;
    if (ratio == 0) w = div8 ? (1u + lsb(&b, 5)) * 8u : jxl_u32(&b);
    else {
        static const uint32_t num[8] = { 0, 1, 12, 4, 3, 16, 5, 2 }, den[8] = { 1, 1, 10, 3, 2, 9, 4, 1 };
        w = h * num[ratio] / den[ratio];
    }
    int32_t orientation = 1;
    if (!lsb(&b, 1) && lsb(&b, 1)) orientation = 1 + (int32_t)lsb(&b, 3);   /* not all_default; extra_fields */
    if (!b.bad) set_size(out, w, h, orientation);
    return true;
}

static bool read_jxl(rubraview_head_src_t *src, rubraview_media_facts_t *out, bool boxed) {
    out->format = "jxl";
    if (!boxed) return read_jxl_code(src, 0, out);
    head_box_t b;
    unsigned guard = 0;
    for (uint64_t at = 0; guard++ < 64 && box_next(src, at, src->size, &b); at = b.end) {
        if (b.type == FOURCC('j', 'x', 'l', 'c')) return read_jxl_code(src, b.body, out);
        if (b.type == FOURCC('j', 'x', 'l', 'p')) return read_jxl_code(src, b.body + 4, out);
        if (b.end <= at) break;
    }
    return true;
}

/* ---- films ---- */

/* ISO BMFF: the top level hopped to `moov`, read whole and given to the MP4 reader. */
static bool read_mp4(rubraview_head_src_t *src, rubraview_media_facts_t *out) {
    out->format = "mp4";
    head_box_t b;
    unsigned guard = 0;
    for (uint64_t at = 0; guard++ < 256 && box_next(src, at, src->size, &b); at = b.end) {
        if (b.type == FOURCC('m', 'o', 'o', 'v')) {
            uint64_t size = b.end - b.at;
            const uint8_t *p = size <= RUBRAVIEW_HEAD_MAX_BYTES ? rubraview_head_bytes(src, b.at, (size_t)size) : NULL;
            rubraview_mp4_t *mp4 = p ? (rubraview_mp4_t *)scratch(src, sizeof(rubraview_mp4_t)) : NULL;
            if (mp4 && rubraview_mp4_open_moov(mp4, p, size)) {
                int32_t w = 0, h = 0;
                double seconds = 0.0;
                (void)rubraview_mp4_frame(mp4, &w, &h, &seconds);
                set_size(out, w > 0 ? (uint64_t)w : 0, h > 0 ? (uint64_t)h : 0, 1);
                set_seconds(out, seconds, false);
            }
            return true;
        }
        if (b.end <= at) break;
    }
    return true;
}

/* EBML: an element's ID (its marker kept) and size at `p` (n bytes there); false when they do not fit. */
static bool ebml_head(const uint8_t *p, size_t n, uint32_t *id, uint64_t *size, bool *unknown, size_t *header) {
    if (n < 2) return false;
    unsigned id_len = p[0] & 0x80 ? 1 : p[0] & 0x40 ? 2 : p[0] & 0x20 ? 3 : p[0] & 0x10 ? 4 : 0;
    if (id_len == 0 || n < id_len + 1u) return false;
    uint32_t v = 0;
    for (unsigned i = 0; i < id_len; ++i) v = (v << 8) | p[i];
    uint8_t lead = p[id_len];
    unsigned size_len = 0;
    for (unsigned bit = 0; bit < 8; ++bit) if (lead & (0x80u >> bit)) { size_len = bit + 1; break; }
    if (size_len == 0 || n < id_len + size_len) return false;
    uint64_t s = lead & (0xFFu >> size_len);
    bool ones = s == (0xFFu >> size_len);
    for (unsigned i = 1; i < size_len; ++i) {
        s = (s << 8) | p[id_len + i];
        if (p[id_len + i] != 0xFF) ones = false;
    }
    *id = v;
    *size = s;
    *unknown = ones;
    *header = id_len + size_len;
    return true;
}

static uint64_t ebml_uint(const uint8_t *p, uint64_t n) {
    uint64_t v = 0;
    for (uint64_t i = 0; i < n && i < 8; ++i) v = (v << 8) | p[i];
    return v;
}

static double ebml_float(const uint8_t *p, uint64_t n) {
    if (n == 4) { uint32_t u = be32(p); float f; memcpy(&f, &u, 4); return (double)f; }
    if (n == 8) { uint64_t u = be64(p); double d; memcpy(&d, &u, 8); return d; }
    return 0.0;
}

#define MKV_SEGMENT   0x18538067u
#define MKV_SEEKHEAD  0x114D9B74u
#define MKV_SEEK      0x4DBBu
#define MKV_SEEKID    0x53ABu
#define MKV_SEEKPOS   0x53ACu
#define MKV_INFO      0x1549A966u
#define MKV_TRACKS    0x1654AE6Bu
#define MKV_CLUSTER   0x1F43B675u
#define MKV_TIMESCALE 0x2AD7B1u
#define MKV_DURATION  0x4489u
#define MKV_ENTRY     0xAEu
#define MKV_TYPE      0x83u
#define MKV_VIDEO     0xE0u
#define MKV_TIMESTAMP 0xE7u

static void mkv_info(const uint8_t *p, size_t n, rubraview_mkv_t *mkv) {
    uint32_t id = 0;
    uint64_t size = 0;
    bool unknown = false;
    size_t header = 0;
    for (size_t at = 0; at < n && ebml_head(p + at, n - at, &id, &size, &unknown, &header) && size <= n - at - header; at += header + (size_t)size) {
        if (id == MKV_TIMESCALE && size > 0) { uint64_t s = ebml_uint(p + at + header, size); if (s > 0) mkv->timestamp_scale = s; }
        if (id == MKV_DURATION) { double d = ebml_float(p + at + header, size); if (d > 0.0 && d < 1e18) mkv->duration_ticks = d; }
    }
}

static void mkv_tracks(const uint8_t *p, size_t n, rubraview_mkv_t *mkv) {
    uint32_t id = 0;
    uint64_t size = 0;
    bool unknown = false;
    size_t header = 0;
    for (size_t at = 0; at < n && ebml_head(p + at, n - at, &id, &size, &unknown, &header) && size <= n - at - header; at += header + (size_t)size) {
        if (id != MKV_ENTRY || mkv->track_count >= RUBRAVIEW_MKV_MAX_TRACKS) continue;
        rubraview_mkv_track_t *t = &mkv->tracks[mkv->track_count++];
        const uint8_t *e = p + at + header;
        size_t en = (size_t)size;
        uint32_t i2 = 0;
        uint64_t s2 = 0;
        size_t h2 = 0;
        for (size_t q = 0; q < en && ebml_head(e + q, en - q, &i2, &s2, &unknown, &h2) && s2 <= en - q - h2; q += h2 + (size_t)s2) {
            if (i2 == MKV_TYPE) t->type = (uint8_t)ebml_uint(e + q + h2, s2);
            if (i2 != MKV_VIDEO) continue;
            const uint8_t *v = e + q + h2;
            size_t vn = (size_t)s2;
            uint32_t i3 = 0;
            uint64_t s3 = 0;
            size_t h3 = 0;
            for (size_t r = 0; r < vn && ebml_head(v + r, vn - r, &i3, &s3, &unknown, &h3) && s3 <= vn - r - h3; r += h3 + (size_t)s3) {
                uint64_t x = ebml_uint(v + r + h3, s3);
                if (i3 == 0xB0) t->pixel_width = x;
                else if (i3 == 0xBA) t->pixel_height = x;
                else if (i3 == 0x54B0) t->display_width = x;
                else if (i3 == 0x54BA) t->display_height = x;
                else if (i3 == 0x54B2) t->display_unit = x;
            }
        }
    }
}

/* An element's content read whole (a megabyte at most), for Info and Tracks. */
static const uint8_t *mkv_body(rubraview_head_src_t *src, uint64_t at, size_t *out_n) {
    size_t got = 0;
    const uint8_t *p = rubraview_head_view(src, at, 12, &got);
    uint32_t id = 0;
    uint64_t size = 0;
    bool unknown = false;
    size_t header = 0;
    if (!p || !ebml_head(p, got, &id, &size, &unknown, &header) || unknown || size > (1u << 20)) return NULL;
    *out_n = (size_t)size;
    return size ? rubraview_head_bytes(src, at + header, (size_t)size) : NULL;
}

static bool read_mkv(rubraview_head_src_t *src, rubraview_media_facts_t *out) {
    out->format = "matroska";
    rubraview_mkv_t *mkv = (rubraview_mkv_t *)scratch(src, sizeof(rubraview_mkv_t));
    if (!mkv) return true;
    mkv->timestamp_scale = 1000000u;
    size_t got = 0;
    uint32_t id = 0;
    uint64_t size = 0;
    bool unknown = false;
    size_t header = 0;
    /* the EBML head, then the segment */
    const uint8_t *p = rubraview_head_view(src, 0, 12, &got);
    if (!p || !ebml_head(p, got, &id, &size, &unknown, &header) || unknown) return true;
    uint64_t at = header + size;
    p = rubraview_head_view(src, at, 12, &got);
    if (!p || !ebml_head(p, got, &id, &size, &unknown, &header) || id != MKV_SEGMENT) return true;
    uint64_t body = at + header, end = unknown || size > src->size - body ? src->size : body + size;
    uint64_t info_at = 0, tracks_at = 0;
    bool have_info = false, have_tracks = false;
    at = body;
    for (unsigned guard = 0; guard < 64 && at < end; ++guard) {
        p = rubraview_head_view(src, at, 12, &got);
        if (!p || !ebml_head(p, got, &id, &size, &unknown, &header)) break;
        if (id == MKV_CLUSTER || unknown) break;   /* the head is behind us */
        size_t n = 0;
        const uint8_t *e = NULL;
        if (id == MKV_SEEKHEAD && (e = mkv_body(src, at, &n)) != NULL) {
            uint32_t i2 = 0, i3 = 0;
            uint64_t s2 = 0, s3 = 0;
            size_t h2 = 0, h3 = 0;
            for (size_t q = 0; q < n && ebml_head(e + q, n - q, &i2, &s2, &unknown, &h2) && s2 <= n - q - h2; q += h2 + (size_t)s2) {
                if (i2 != MKV_SEEK) continue;
                uint32_t which = 0;
                uint64_t where = 0;
                const uint8_t *s = e + q + h2;
                for (size_t r = 0; r < s2 && ebml_head(s + r, (size_t)s2 - r, &i3, &s3, &unknown, &h3) && s3 <= s2 - r - h3; r += h3 + (size_t)s3) {
                    if (i3 == MKV_SEEKID) which = (uint32_t)ebml_uint(s + r + h3, s3);
                    if (i3 == MKV_SEEKPOS) where = ebml_uint(s + r + h3, s3);
                }
                if (which == MKV_INFO && where) info_at = body + where;
                if (which == MKV_TRACKS && where) tracks_at = body + where;
            }
        } else if (id == MKV_INFO && (e = mkv_body(src, at, &n)) != NULL) {
            mkv_info(e, n, mkv);
            have_info = true;
        } else if (id == MKV_TRACKS && (e = mkv_body(src, at, &n)) != NULL) {
            mkv_tracks(e, n, mkv);
            have_tracks = true;
        }
        if (have_info && have_tracks) break;
        at += header + size;
    }
    /* what the head did not hold, where the SeekHead says it is */
    size_t n = 0;
    const uint8_t *e = NULL;
    if (!have_tracks && tracks_at && (e = mkv_body(src, tracks_at, &n)) != NULL) mkv_tracks(e, n, mkv);
    if (!have_info && info_at && (e = mkv_body(src, info_at, &n)) != NULL) mkv_info(e, n, mkv);
    int32_t w = 0, h = 0;
    double seconds = 0.0;
    (void)rubraview_mkv_frame(mkv, &w, &h, &seconds);
    set_size(out, w > 0 ? (uint64_t)w : 0, h > 0 ? (uint64_t)h : 0, 1);
    set_seconds(out, seconds, false);
    if (out->seconds > 0.0 || src->size < 64) return true;
    /* No Duration (a recording cut short): the last cluster in the file's end, its time and its last block's */
    uint64_t tail = src->size > RUBRAVIEW_HEAD_LATER ? src->size - RUBRAVIEW_HEAD_LATER : 0;
    p = rubraview_head_view(src, tail, RUBRAVIEW_HEAD_LATER, &got);
    for (size_t i = got >= 12 ? got - 12 : 0; p && i-- > 0;) {
        if (be32(p + i) != MKV_CLUSTER || !ebml_head(p + i, got - i, &id, &size, &unknown, &header)) continue;
        uint64_t base = 0, latest = 0;
        bool have_base = false;
        uint32_t i2 = 0;
        uint64_t s2 = 0;
        size_t h2 = 0;
        for (size_t q = i + header; q < got && ebml_head(p + q, got - q, &i2, &s2, &unknown, &h2) && !unknown; q += h2 + (size_t)s2) {
            if (s2 > got - q - h2) break;   /* cut by the window */
            const uint8_t *b = p + q + h2;
            uint64_t bn = s2;
            uint32_t kind = i2;
            if (kind == MKV_TIMESTAMP) { base = ebml_uint(b, bn); have_base = true; }
            if (kind == 0xA0 && bn > 2) {    /* a BlockGroup: its Block */
                uint32_t i3 = 0;
                uint64_t s3 = 0;
                size_t h3 = 0;
                if (ebml_head(b, (size_t)bn, &i3, &s3, &unknown, &h3) && i3 == 0xA1 && s3 <= bn - h3) { b += h3; bn = s3; kind = 0xA3; }
            }
            if (kind != 0xA3 || bn < 4) continue;   /* a block: its track (a number of one to eight bytes), its time */
            unsigned len = 0;
            for (unsigned bit = 0; bit < 8; ++bit) if (b[0] & (0x80u >> bit)) { len = bit + 1; break; }
            if (len == 0 || len + 2u > bn) continue;
            int16_t rel = (int16_t)be16(b + len);
            uint64_t at_tick = rel < 0 && (uint64_t)-(int32_t)rel > base ? 0 : base + (uint64_t)(int64_t)rel;
            if (at_tick > latest) latest = at_tick;
        }
        if (have_base) set_seconds(out, (double)(latest > base ? latest : base) * (double)mkv->timestamp_scale / 1e9, true);
        break;
    }
    return true;
}

/* AVI: `hdrl`'s main header, its video stream's header and format, OpenDML's frame count. */
typedef struct avi_facts { uint32_t usec, frames, w, h, scale, rate, length, odml, vw, vh; bool video; } avi_facts_t;

static void avi_list(const uint8_t *p, size_t n, avi_facts_t *f, int depth) {
    bool video = false;
    for (size_t at = 0; at + 8 <= n && depth < 4;) {
        uint32_t size = le32(p + at + 4);
        if (size > n - at - 8) size = (uint32_t)(n - at - 8);
        const uint8_t *b = p + at + 8;
        if (memcmp(p + at, "LIST", 4) == 0 && size >= 4) avi_list(b + 4, size - 4, f, depth + 1);
        else if (memcmp(p + at, "avih", 4) == 0 && size >= 40) { f->usec = le32(b); f->frames = le32(b + 16); f->w = le32(b + 32); f->h = le32(b + 36); }
        else if (memcmp(p + at, "strh", 4) == 0 && size >= 36) {
            video = memcmp(b, "vids", 4) == 0;
            if (video && !f->video) { f->scale = le32(b + 20); f->rate = le32(b + 24); f->length = le32(b + 32); }
        } else if (memcmp(p + at, "strf", 4) == 0 && size >= 12 && video && !f->video) {
            int32_t h = (int32_t)le32(b + 8);
            f->vw = le32(b + 4);
            f->vh = (uint32_t)(h < 0 ? -(int64_t)h : h);
            f->video = true;
        } else if (memcmp(p + at, "dmlh", 4) == 0 && size >= 4) f->odml = le32(b);
        at += 8u + size + (size & 1u);
    }
}

static bool read_avi(rubraview_head_src_t *src, rubraview_media_facts_t *out) {
    out->format = "avi";
    const uint8_t *p = rubraview_head_bytes(src, 12, 12);
    if (!p || memcmp(p, "LIST", 4) != 0 || memcmp(p + 8, "hdrl", 4) != 0) return true;
    uint32_t size = le32(p + 4);
    if (size < 4 || size > (1u << 20)) return true;
    const uint8_t *h = rubraview_head_bytes(src, 24, size - 4u);
    if (!h) return true;
    avi_facts_t f = {0};
    avi_list(h, size - 4u, &f, 0);
    set_size(out, f.video ? f.vw : f.w, f.video ? f.vh : f.h, 1);
    uint32_t frames = f.odml > f.length ? f.odml : f.length;
    if (f.rate > 0 && f.scale > 0 && frames > 0) set_seconds(out, (double)frames * f.scale / f.rate, false);
    else set_seconds(out, (double)f.usec * f.frames / 1e6, false);
    return true;
}

/* ASF (WMV, WMA): File Properties' play duration less its preroll; the video stream's encoded size. */
static const uint8_t ASF_HEADER[16] = { 0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11, 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C };
static const uint8_t ASF_FILE[16]   = { 0xA1, 0xDC, 0xAB, 0x8C, 0x47, 0xA9, 0xCF, 0x11, 0x8E, 0xE4, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 };
static const uint8_t ASF_STREAM[16] = { 0x91, 0x07, 0xDC, 0xB7, 0xB7, 0xA9, 0xCF, 0x11, 0x8E, 0xE6, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 };
static const uint8_t ASF_VIDEO[16]  = { 0xC0, 0xEF, 0x19, 0xBC, 0x4D, 0x5B, 0xCF, 0x11, 0xA8, 0xFD, 0x00, 0x80, 0x5F, 0x5C, 0x44, 0x2B };

static bool read_asf(rubraview_head_src_t *src, rubraview_media_facts_t *out) {
    out->format = "asf";
    const uint8_t *p = rubraview_head_bytes(src, 0, 30);
    if (!p) return true;
    uint64_t size = le64(p + 16);
    if (size < 30 || size > (1u << 20)) return true;
    const uint8_t *h = rubraview_head_bytes(src, 0, (size_t)size);
    if (!h) return true;
    for (size_t at = 30; at + 24 <= size;) {
        uint64_t len = le64(h + at + 16);
        if (len < 24 || len > size - at) break;
        const uint8_t *b = h + at + 24;
        size_t n = (size_t)len - 24;
        if (memcmp(h + at, ASF_FILE, 16) == 0 && n >= 68 && !(le32(b + 64) & 1u)) {   /* not a broadcast */
            double seconds = (double)le64(b + 40) / 1e7 - (double)le64(b + 56) / 1000.0;
            set_seconds(out, seconds, false);
        }
        if (memcmp(h + at, ASF_STREAM, 16) == 0 && n >= 54 + 8 && memcmp(b, ASF_VIDEO, 16) == 0 && out->width == 0)
            set_size(out, le32(b + 54), le32(b + 58), 1);
        at += (size_t)len;
    }
    return true;
}

/* MPEG-2 systems: a PES header's PTS (90 kHz), MPEG-2's or MPEG-1's. */
static bool pes_pts(const uint8_t *p, size_t n, uint64_t *out) {
    if (n < 9 || p[0] != 0 || p[1] != 0 || p[2] != 1) return false;
    uint8_t sid = p[3];
    if (!(sid == 0xBD || (sid >= 0xC0 && sid <= 0xEF))) return false;
    const uint8_t *q = NULL;
    if ((p[6] & 0xC0) == 0x80) {
        if (!(p[7] & 0x80) || n < 14) return false;
        q = p + 9;
    } else {
        size_t at = 6;
        while (at < n && at < 6 + 16 && p[at] == 0xFF) ++at;
        if (at < n && (p[at] & 0xC0) == 0x40) at += 2;
        if (at + 5 > n || (p[at] & 0xE0) != 0x20) return false;
        if ((p[at] & 0xF0) != 0x20 && (p[at] & 0xF0) != 0x30) return false;
        q = p + at;
    }
    if (!(q[0] & 1) || !(q[2] & 1) || !(q[4] & 1)) return false;
    *out = ((uint64_t)(q[0] >> 1 & 7) << 30) | ((uint64_t)q[1] << 22) | ((uint64_t)(q[2] >> 1) << 15) | ((uint64_t)q[3] << 7) | (q[4] >> 1);
    return true;
}

static double pts_span(uint64_t first, uint64_t last) {
    uint64_t d = (last - first) & ((1ull << 33) - 1);   /* past a wrap of the 33-bit clock */
    return (double)d / 90000.0;
}

/* H.264's SPS (7.3.2.1.1): its cropped size, its sample aspect applied. */
static bool h264_sps(const uint8_t *nal, size_t n, uint64_t *w, uint64_t *h) {
    bits_t b = { .p = nal + 1, .n = n - 1 };
    uint32_t profile = msb(&b, 8);
    msb(&b, 16);   /* constraints and level */
    ue(&b);
    uint32_t chroma = 1;
    bool separate = false;
    if (profile == 100 || profile == 110 || profile == 122 || profile == 244 || profile == 44 || profile == 83 ||
        profile == 86 || profile == 118 || profile == 128 || profile == 138 || profile == 139 || profile == 134 || profile == 135) {
        chroma = ue(&b);
        if (chroma == 3) separate = msb(&b, 1);
        ue(&b); ue(&b); msb(&b, 1);
        if (msb(&b, 1)) {
            for (int i = 0; i < (chroma != 3 ? 8 : 12) && !b.bad; ++i) {
                if (!msb(&b, 1)) continue;
                int size = i < 6 ? 16 : 64, last = 8, next = 8;
                for (int j = 0; j < size && !b.bad; ++j) {
                    if (next != 0) next = (last + se(&b) + 256) % 256;
                    last = next == 0 ? last : next;
                }
            }
        }
    }
    ue(&b);
    uint32_t poc = ue(&b);
    if (poc == 0) ue(&b);
    else if (poc == 1) {
        msb(&b, 1); se(&b); se(&b);
        uint32_t cycle = ue(&b);
        for (uint32_t i = 0; i < cycle && i < 256 && !b.bad; ++i) se(&b);
    }
    ue(&b); msb(&b, 1);
    uint64_t mbs_w = ue(&b) + 1u, map_h = ue(&b) + 1u;
    uint32_t frame_only = msb(&b, 1);
    if (!frame_only) msb(&b, 1);
    msb(&b, 1);
    uint64_t width = mbs_w * 16u, height = map_h * 16u * (2u - frame_only);
    if (msb(&b, 1)) {
        uint32_t l = ue(&b), r = ue(&b), t = ue(&b), bo = ue(&b);
        uint32_t ux = chroma == 0 || separate ? 1 : (chroma == 3 ? 1 : 2);
        uint32_t uy = (chroma == 1 && !separate ? 2 : 1) * (2u - frame_only);
        uint64_t cx = (uint64_t)ux * (l + r), cy = (uint64_t)uy * (t + bo);
        if (cx < width && cy < height) { width -= cx; height -= cy; }
    }
    if (b.bad) return false;
    if (msb(&b, 1) && msb(&b, 1)) {   /* VUI, its aspect ratio */
        static const uint16_t sar[17][2] = { {0,0},{1,1},{12,11},{10,11},{16,11},{40,33},{24,11},{20,11},{32,11},{80,33},
                                             {18,11},{15,11},{64,33},{160,99},{4,3},{3,2},{2,1} };
        uint32_t idc = msb(&b, 8), sw = 0, sh = 0;
        if (idc == 255) { sw = msb(&b, 16); sh = msb(&b, 16); }
        else if (idc < 17) { sw = sar[idc][0]; sh = sar[idc][1]; }
        if (!b.bad && sw && sh) width = width * sw / sh;
    }
    *w = width;
    *h = height;
    return true;
}

/* H.265's SPS (7.3.2.2): its size with the conformance window taken off. */
static bool h265_sps(const uint8_t *nal, size_t n, uint64_t *w, uint64_t *h) {
    bits_t b = { .p = nal + 2, .n = n - 2 };
    msb(&b, 4);
    uint32_t sub = msb(&b, 3);
    msb(&b, 1);
    msb(&b, 32); msb(&b, 32); msb(&b, 32);   /* the general profile, tier and level: 96 bits */
    uint32_t present[8] = {0};
    for (uint32_t i = 0; i < sub; ++i) present[i] = msb(&b, 2);
    if (sub > 0) for (uint32_t i = sub; i < 8; ++i) msb(&b, 2);
    for (uint32_t i = 0; i < sub; ++i) {
        if (present[i] & 2) { msb(&b, 32); msb(&b, 32); msb(&b, 24); }
        if (present[i] & 1) msb(&b, 8);
    }
    ue(&b);
    uint32_t chroma = ue(&b);
    if (chroma == 3) msb(&b, 1);
    uint64_t width = ue(&b), height = ue(&b);
    if (msb(&b, 1)) {
        uint32_t l = ue(&b), r = ue(&b), t = ue(&b), bo = ue(&b);
        uint32_t ux = chroma == 1 || chroma == 2 ? 2 : 1, uy = chroma == 1 ? 2 : 1;
        uint64_t cx = (uint64_t)ux * (l + r), cy = (uint64_t)uy * (t + bo);
        if (cx < width && cy < height) { width -= cx; height -= cy; }
    }
    if (b.bad) return false;
    *w = width;
    *h = height;
    return true;
}

/* A NAL unit without its emulation prevention bytes, its first 256 kept. */
static size_t unescape(const uint8_t *p, size_t n, uint8_t *dst, size_t cap) {
    size_t k = 0, zeros = 0;
    for (size_t i = 0; i < n && k < cap; ++i) {
        if (zeros >= 2 && p[i] == 3) { zeros = 0; continue; }
        if (zeros >= 2 && p[i] <= 2) break;   /* the next start code */
        zeros = p[i] == 0 ? zeros + 1 : 0;
        dst[k++] = p[i];
    }
    return k;
}

/* A video's size from its elementary stream: MPEG-1/2's sequence header (its display aspect when MPEG-2 says one),
   or H.264's or H.265's SPS. `kind` 0 MPEG video, 1 H.264, 2 H.265, -1 any. */
static bool es_size(const uint8_t *p, size_t n, int kind, uint64_t *w, uint64_t *h) {
    for (size_t i = 0; i + 4 < n; ++i) {
        if (p[i] != 0 || p[i + 1] != 0 || p[i + 2] != 1) continue;
        uint8_t c = p[i + 3];
        if ((kind == 0 || kind < 0) && c == 0xB3 && i + 8 <= n) {
            const uint8_t *s = p + i + 4;
            uint64_t sw = ((uint32_t)s[0] << 4) | (s[1] >> 4), sh = ((uint32_t)(s[1] & 0x0F) << 8) | s[2];
            uint8_t aspect = s[3] >> 4;
            bool mpeg2 = false;   /* a sequence extension next says MPEG-2, whose aspect is the display's */
            for (size_t j = i + 12; j + 4 < n && j < i + 200 && !mpeg2; ++j)
                mpeg2 = p[j] == 0 && p[j + 1] == 0 && p[j + 2] == 1 && p[j + 3] == 0xB5 && (p[j + 4] >> 4) == 1;
            if (mpeg2 && sh > 0) {
                if (aspect == 2) sw = sh * 4 / 3;
                else if (aspect == 3) sw = sh * 16 / 9;
                else if (aspect == 4) sw = sh * 221 / 100;
            }
            *w = sw; *h = sh;
            return sw > 0 && sh > 0;
        }
        uint8_t nal[256];
        if ((kind == 1 || kind < 0) && (c & 0x1F) == 7 && (c & 0x80) == 0) {
            size_t k = unescape(p + i + 3, n - i - 3, nal, sizeof(nal));
            if (k > 4 && h264_sps(nal, k, w, h)) return true;
        }
        if ((kind == 2 || kind < 0) && ((c >> 1) & 0x3F) == 33 && (c & 0x80) == 0) {
            size_t k = unescape(p + i + 3, n - i - 3, nal, sizeof(nal));
            if (k > 4 && h265_sps(nal, k, w, h)) return true;
        }
    }
    return false;
}

/* A transport stream of 188-byte packets (or 192, the first four a time code): PAT, PMT, the video's PID;
   its first PTS and size from the head, its last PTS from the end. */
static size_t ts_payload(const uint8_t *k, size_t *out_len) {
    uint8_t afc = (k[3] >> 4) & 3;
    if (!(afc & 1)) return 0;
    size_t at = 4;
    if (afc & 2) at += 1u + k[4];
    if (at >= 188) return 0;
    *out_len = 188 - at;
    return at;
}

static bool read_ts(rubraview_head_src_t *src, rubraview_media_facts_t *out, size_t step, size_t lead) {
    out->format = "mpegts";
    size_t got = 0;
    const uint8_t *p = rubraview_head_view(src, 0, RUBRAVIEW_HEAD_FIRST, &got);
    if (!p) return true;
    uint16_t pmt = 0x1FFF, video = 0x1FFF;
    int kind = -1;
    bool have_first = false;
    uint64_t first = 0, last = 0;
    uint8_t *es = (uint8_t *)scratch(src, RUBRAVIEW_HEAD_FIRST);
    size_t es_n = 0;
    for (size_t at = 0; at + step <= got; at += step) {
        const uint8_t *k = p + at + lead;
        if (k[0] != 0x47) continue;
        uint16_t pid = (uint16_t)(((k[1] & 0x1F) << 8) | k[2]);
        bool start = k[1] & 0x40;
        size_t len = 0, off = ts_payload(k, &len);
        if (!off) continue;
        const uint8_t *pl = k + off;
        if (pid == 0 && start && len > 1 && pmt == 0x1FFF) {
            size_t t = 1u + pl[0];
            if (t + 8 > len || pl[t] != 0) continue;
            size_t section = ((size_t)(pl[t + 1] & 0x0F) << 8) | pl[t + 2];
            for (size_t e = t + 8; e + 4 <= len && e + 4 <= t + 3 + section - 4; e += 4)
                if (be16(pl + e) != 0) { pmt = (uint16_t)(((pl[e + 2] & 0x1F) << 8) | pl[e + 3]); break; }
        } else if (pid == pmt && start && len > 1 && video == 0x1FFF) {
            size_t t = 1u + pl[0];
            if (t + 12 > len || pl[t] != 2) continue;
            size_t section = ((size_t)(pl[t + 1] & 0x0F) << 8) | pl[t + 2];
            size_t info = ((size_t)(pl[t + 10] & 0x0F) << 8) | pl[t + 11];
            size_t stop = t + 3 + section - 4 < len ? t + 3 + section - 4 : len;
            for (size_t e = t + 12 + info; e + 5 <= stop;) {
                uint8_t type = pl[e];
                uint16_t es_pid = (uint16_t)(((pl[e + 1] & 0x1F) << 8) | pl[e + 2]);
                size_t es_info = ((size_t)(pl[e + 3] & 0x0F) << 8) | pl[e + 4];
                int k2 = type == 0x01 || type == 0x02 ? 0 : type == 0x1B ? 1 : type == 0x24 ? 2 : -2;
                if (k2 >= 0) { video = es_pid; kind = k2; break; }
                e += 5 + es_info;
            }
        } else if (pid == video) {
            uint64_t pts = 0;
            if (start && pes_pts(pl, len, &pts) && (!have_first || ((first - pts) & ((1ull << 33) - 1)) < (1ull << 32))) { first = pts; have_first = true; }   /* the earliest: frames come reordered */
            size_t skip = 0;
            if (start && len >= 9 && pl[0] == 0 && pl[1] == 0 && pl[2] == 1) skip = 9u + pl[8];
            if (es && skip < len && es_n + (len - skip) <= RUBRAVIEW_HEAD_FIRST) { memcpy(es + es_n, pl + skip, len - skip); es_n += len - skip; }
        }
    }
    uint64_t w = 0, h = 0;
    if (es && es_size(es, es_n, kind, &w, &h)) set_size(out, w, h, 1);
    if (!have_first || video == 0x1FFF) return true;
    uint64_t tail = src->size > RUBRAVIEW_HEAD_LATER ? src->size - RUBRAVIEW_HEAD_LATER : 0;
    p = rubraview_head_view(src, tail, RUBRAVIEW_HEAD_LATER, &got);
    if (!p) return true;
    size_t align = 0;   /* the first packet wholly in the tail */
    while (align < step && !(align + lead + step < got && p[align + lead] == 0x47 && p[align + lead + step] == 0x47)) ++align;
    bool have_last = false;
    for (size_t at = align; at + step <= got; at += step) {
        const uint8_t *k = p + at + lead;
        if (k[0] != 0x47 || (((k[1] & 0x1F) << 8) | k[2]) != video || !(k[1] & 0x40)) continue;
        size_t len = 0, off = ts_payload(k, &len);
        uint64_t pts = 0;
        if (off && pes_pts(k + off, len, &pts) && (!have_last || ((pts - last) & ((1ull << 33) - 1)) < (1ull << 32))) { last = pts; have_last = true; }   /* the latest */
    }
    if (have_last) set_seconds(out, pts_span(first, last), false);
    return true;
}

/* A program stream: its first and last PTS, the sequence header's size. */
static bool read_ps(rubraview_head_src_t *src, rubraview_media_facts_t *out) {
    out->format = "mpegps";
    size_t got = 0;
    const uint8_t *p = rubraview_head_view(src, 0, RUBRAVIEW_HEAD_FIRST, &got);
    if (!p) return true;
    uint64_t w = 0, h = 0, first = 0, last = 0, pts = 0;
    if (es_size(p, got, -1, &w, &h)) set_size(out, w, h, 1);
    bool have_first = false, have_last = false;
    for (size_t i = 0; i + 14 <= got && !have_first; ++i)
        if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1 && p[i + 3] >= 0xE0 && p[i + 3] <= 0xEF && pes_pts(p + i, got - i, &pts)) { first = pts; have_first = true; }
    if (!have_first) return true;
    uint64_t tail = src->size > RUBRAVIEW_HEAD_LATER ? src->size - RUBRAVIEW_HEAD_LATER : 0;
    p = rubraview_head_view(src, tail, RUBRAVIEW_HEAD_LATER, &got);
    for (size_t i = 0; p && i + 14 <= got; ++i)
        if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1 && p[i + 3] >= 0xE0 && p[i + 3] <= 0xEF && pes_pts(p + i, got - i, &pts)) { last = pts; have_last = true; }
    if (have_last) set_seconds(out, pts_span(first, last), false);
    return true;
}

/* FLV: the script tag's onMetaData (AMF0): duration, width, height. */
static bool amf_skip(const uint8_t *p, size_t n, size_t *at, int depth);

static bool amf_props(const uint8_t *p, size_t n, size_t *at, int depth, rubraview_media_facts_t *out) {
    double w = 0.0, h = 0.0;
    for (unsigned guard = 0; guard < 4096; ++guard) {
        if (*at + 3 > n) return false;
        size_t key = be16(p + *at);
        if (key == 0 && p[*at + 2] == 9) { *at += 3; break; }
        if (*at + 2 + key + 1 > n) return false;
        const char *name = (const char *)p + *at + 2;
        *at += 2 + key;
        if (out && p[*at] == 0 && *at + 9 <= n) {
            uint64_t u = be64(p + *at + 1);
            double d;
            memcpy(&d, &u, 8);
            if (key == 8 && memcmp(name, "duration", 8) == 0) set_seconds(out, d, false);
            if (key == 5 && memcmp(name, "width", 5) == 0) w = d;
            if (key == 6 && memcmp(name, "height", 6) == 0) h = d;
        }
        if (!amf_skip(p, n, at, depth + 1)) return false;
    }
    if (out && w >= 1.0 && h >= 1.0 && w <= SIDE_MAX && h <= SIDE_MAX) set_size(out, (uint64_t)(w + 0.5), (uint64_t)(h + 0.5), 1);
    return true;
}

static bool amf_skip(const uint8_t *p, size_t n, size_t *at, int depth) {
    if (depth > 8 || *at >= n) return false;
    uint8_t type = p[(*at)++];
    switch (type) {
        case 0: *at += 8; break;
        case 1: *at += 1; break;
        case 2: if (*at + 2 > n) return false; *at += 2u + be16(p + *at); break;
        case 3: return amf_props(p, n, at, depth, NULL);
        case 5: case 6: break;
        case 7: *at += 2; break;
        case 8: *at += 4; return amf_props(p, n, at, depth, NULL);
        case 10: {
            if (*at + 4 > n) return false;
            uint32_t count = be32(p + *at);
            *at += 4;
            for (uint32_t i = 0; i < count; ++i) if (!amf_skip(p, n, at, depth + 1)) return false;
            break;
        }
        case 11: *at += 10; break;
        case 12: if (*at + 4 > n) return false; *at += 4u + be32(p + *at); break;
        default: return false;
    }
    return *at <= n;
}

static bool read_flv(rubraview_head_src_t *src, rubraview_media_facts_t *out) {
    out->format = "flv";
    const uint8_t *p = rubraview_head_bytes(src, 0, 9);
    if (!p) return true;
    uint64_t tag = (uint64_t)be32(p + 5) + 4u;
    for (unsigned guard = 0; guard < 8; ++guard) {
        const uint8_t *t = rubraview_head_bytes(src, tag, 11);
        if (!t) return true;
        uint32_t size = be24(t + 1);
        if (t[0] == 18) {
            const uint8_t *b = size ? rubraview_head_bytes(src, tag + 11, size) : NULL;
            if (!b || size < 13 || b[0] != 2 || be16(b + 1) != 10 || memcmp(b + 3, "onMetaData", 10) != 0) return true;
            size_t at = 13;
            if (b[at] == 8) at += 5;
            else if (b[at] == 3) at += 1;
            else return true;
            (void)amf_props(b, size, &at, 0, out);
            return true;
        }
        tag += 11u + size + 4u;
    }
    return true;
}

/* Ogg: the streams begun on the first pages (Vorbis, Opus, Theora), their last granules from the file's end. */
typedef struct ogg_stream { uint32_t serial; int kind; uint32_t rate; uint32_t preskip; uint32_t frn, frd, shift; uint64_t w, h; } ogg_stream_t;
enum { OGG_VORBIS = 1, OGG_OPUS, OGG_THEORA };

static double ogg_seconds(const ogg_stream_t *s, uint64_t granule) {
    if (s->kind == OGG_VORBIS && s->rate) return (double)granule / s->rate;
    if (s->kind == OGG_OPUS) return granule > s->preskip ? (double)(granule - s->preskip) / 48000.0 : 0.0;
    if (s->kind == OGG_THEORA && s->frn && s->frd) {
        uint64_t frames = (granule >> s->shift) + (granule & ((1ull << s->shift) - 1));
        return (double)frames * s->frd / s->frn;
    }
    return 0.0;
}

static bool read_ogg(rubraview_head_src_t *src, rubraview_media_facts_t *out) {
    out->format = "ogg";
    size_t got = 0;
    const uint8_t *p = rubraview_head_view(src, 0, RUBRAVIEW_HEAD_FIRST, &got);
    if (!p) return true;
    ogg_stream_t streams[4];
    size_t count = 0;
    for (size_t at = 0; at + 27 <= got && count < 4;) {
        if (memcmp(p + at, "OggS", 4) != 0 || !(p[at + 5] & 2)) break;   /* the beginning pages only */
        size_t segs = p[at + 26], body = at + 27 + segs, len = 0;
        if (body > got) break;
        for (size_t i = 0; i < segs; ++i) len += p[at + 27 + i];
        if (body + len > got) break;
        const uint8_t *k = p + body;
        ogg_stream_t s = { .serial = le32(p + at + 14) };
        if (len >= 16 && memcmp(k, "\x01vorbis", 7) == 0) { s.kind = OGG_VORBIS; s.rate = le32(k + 12); }
        else if (len >= 12 && memcmp(k, "OpusHead", 8) == 0) { s.kind = OGG_OPUS; s.preskip = le16(k + 10); }
        else if (len >= 42 && memcmp(k, "\x80theora", 7) == 0) {
            s.kind = OGG_THEORA;
            s.w = be24(k + 14);
            s.h = be24(k + 17);
            s.frn = be32(k + 22);
            s.frd = be32(k + 26);
            uint32_t parn = be24(k + 30), pard = be24(k + 33);
            if (parn && pard) s.w = s.w * parn / pard;
            s.shift = (be16(k + 40) >> 5) & 0x1F;
        }
        if (s.kind) streams[count++] = s;
        at = body + len;
    }
    for (size_t i = 0; i < count; ++i) if (streams[i].kind == OGG_THEORA) { set_size(out, streams[i].w, streams[i].h, 1); break; }
    if (count == 0) return true;
    uint64_t tail = src->size > RUBRAVIEW_HEAD_LATER ? src->size - RUBRAVIEW_HEAD_LATER : 0;
    p = rubraview_head_view(src, tail, RUBRAVIEW_HEAD_LATER, &got);
    double seconds = 0.0;
    for (size_t at = 0; p && at + 27 <= got; ++at) {
        if (p[at] != 'O' || memcmp(p + at, "OggS", 4) != 0 || p[at + 4] != 0) continue;
        uint64_t granule = le64(p + at + 6);
        if (granule == UINT64_MAX) continue;
        uint32_t serial = le32(p + at + 14);
        for (size_t i = 0; i < count; ++i) {
            if (streams[i].serial != serial) continue;
            double s = ogg_seconds(&streams[i], granule);
            if (s > seconds) seconds = s;
        }
    }
    set_seconds(out, seconds, false);
    return true;
}

/* ---- songs ---- */

static bool read_flac(rubraview_head_src_t *src, uint64_t base, rubraview_media_facts_t *out) {
    out->format = "flac";
    const uint8_t *p = rubraview_head_bytes(src, base, 42);
    if (!p || (p[4] & 0x7F) != 0) return true;
    const uint8_t *s = p + 8;
    uint32_t rate = ((uint32_t)s[10] << 12) | ((uint32_t)s[11] << 4) | (s[12] >> 4);
    uint64_t total = ((uint64_t)(s[13] & 0x0F) << 32) | be32(s + 14);
    if (rate) set_seconds(out, (double)total / rate, false);
    return true;
}

static bool read_wav(rubraview_head_src_t *src, rubraview_media_facts_t *out) {
    out->format = "wav";
    uint64_t data = 0, data64 = 0, fact = 0, at = 12, start = 0;
    uint32_t byte_rate = 0, rate = 0;
    uint16_t tag = 0;
    bool have_data = false;
    for (unsigned guard = 0; guard < 64 && !have_data; ++guard) {
        const uint8_t *c = rubraview_head_bytes(src, at, 8);
        if (!c) break;
        uint32_t size = le32(c + 4);
        const uint8_t *b = NULL;
        if (memcmp(c, "fmt ", 4) == 0 && (b = rubraview_head_bytes(src, at + 8, 12)) != NULL) { tag = le16(b); rate = le32(b + 4); byte_rate = le32(b + 8); }
        else if (memcmp(c, "ds64", 4) == 0 && (b = rubraview_head_bytes(src, at + 8, 16)) != NULL) data64 = le64(b + 8);
        else if (memcmp(c, "fact", 4) == 0 && (b = rubraview_head_bytes(src, at + 8, 4)) != NULL) fact = le32(b);
        else if (memcmp(c, "data", 4) == 0) { data = size; start = at + 8; have_data = true; }
        at += 8u + size + (size & 1u);
    }
    if (!have_data) return true;
    if (data == 0xFFFFFFFFu && data64) data = data64;
    if (data == 0 || data == 0xFFFFFFFFu || data > src->size - start) data = src->size - start;   /* written while streaming */
    bool plain = tag == 1 || tag == 3 || tag == 0xFFFE;
    if (!plain && fact && rate) set_seconds(out, (double)fact / rate, false);
    else if (byte_rate) set_seconds(out, (double)data / byte_rate, false);
    return true;
}

/* MPEG audio: a frame's header read; false when it is none. */
typedef struct mpa { uint32_t rate, bitrate, length, samples; bool mono; int version; } mpa_t;

static bool mpa_frame(const uint8_t *p, mpa_t *f) {
    if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0) return false;
    int version = (p[1] >> 3) & 3, layer = (p[1] >> 1) & 3;   /* version 3 MPEG-1, 2 MPEG-2, 0 MPEG-2.5; layer 1 is III */
    int bi = p[2] >> 4, si = (p[2] >> 2) & 3, pad = (p[2] >> 1) & 1;
    if (version == 1 || layer == 0 || bi == 0 || bi == 15 || si == 3) return false;
    static const uint16_t rates[3] = { 44100, 48000, 32000 };
    static const uint16_t br[2][3][15] = {
        { { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 },
          { 0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384 },
          { 0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448 } },
        { { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160 },
          { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160 },
          { 0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256 } },
    };
    bool v1 = version == 3;
    int li = layer - 1;   /* 0 III, 1 II, 2 I */
    f->rate = rates[si] >> (v1 ? 0 : version == 2 ? 1 : 2);
    f->bitrate = br[v1 ? 0 : 1][li][bi] * 1000u;
    f->samples = li == 2 ? 384 : (li == 1 || v1) ? 1152 : 576;
    f->length = li == 2 ? (12u * f->bitrate / f->rate + (uint32_t)pad) * 4u : f->samples / 8u * f->bitrate / f->rate + (uint32_t)pad;
    f->mono = (p[3] >> 6) == 3;
    f->version = version;
    return f->length >= 4;
}

static bool read_mp3(rubraview_head_src_t *src, uint64_t base, rubraview_media_facts_t *out) {
    out->format = "mp3";
    size_t got = 0;
    const uint8_t *p = rubraview_head_view(src, base, RUBRAVIEW_HEAD_FIRST, &got);
    mpa_t f = {0}, g = {0};
    size_t at = 0;
    for (; p && at + 4 <= got; ++at) {   /* a frame whose next one is where it says */
        if (!mpa_frame(p + at, &f)) continue;
        if (at + f.length + 4 > got || mpa_frame(p + at + f.length, &g)) break;
    }
    if (!p || at + 4 > got) return true;
    const uint8_t *x = p + at + 4 + (f.version == 3 ? (f.mono ? 17 : 32) : (f.mono ? 9 : 17));
    if (x + 12 <= p + got && (memcmp(x, "Xing", 4) == 0 || memcmp(x, "Info", 4) == 0) && (be32(x + 4) & 1u)) {
        set_seconds(out, (double)be32(x + 8) * f.samples / f.rate, false);
        return true;
    }
    const uint8_t *v = p + at + 36;
    if (v + 18 <= p + got && memcmp(v, "VBRI", 4) == 0) {
        set_seconds(out, (double)be32(v + 14) * f.samples / f.rate, false);
        return true;
    }
    uint64_t audio = src->size - base - at;
    if (f.bitrate) set_seconds(out, (double)audio * 8.0 / f.bitrate, true);
    return true;
}

/* ADTS (bare AAC): the frames in the head give the mean frame; the length is the file over it. */
static bool read_adts(rubraview_head_src_t *src, uint64_t base, rubraview_media_facts_t *out) {
    out->format = "aac";
    static const uint32_t rates[13] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };
    size_t got = 0;
    const uint8_t *p = rubraview_head_view(src, base, RUBRAVIEW_HEAD_FIRST, &got);
    uint32_t rate = 0;
    uint64_t frames = 0, bytes = 0;
    for (size_t at = 0; p && at + 7 <= got;) {
        if (p[at] != 0xFF || (p[at + 1] & 0xF6) != 0xF0) break;
        uint32_t si = (p[at + 2] >> 2) & 0x0F;
        size_t len = ((size_t)(p[at + 3] & 3) << 11) | ((size_t)p[at + 4] << 3) | (p[at + 5] >> 5);
        if (si >= 13 || len < 7) break;
        rate = rates[si];
        frames++;
        bytes += len;
        at += len;
    }
    if (frames && rate) set_seconds(out, (double)(src->size - base) / ((double)bytes / frames) * 1024.0 / rate, true);
    return true;
}

/* ---- which kind ---- */

static bool name_is(proven_u8str_view_t name, const char *ext) {
    size_t n = strlen(ext);
    if (name.size <= n || name.ptr[name.size - n - 1] != '.') return false;
    for (size_t i = 0; i < n; ++i) {
        char c = (char)name.ptr[name.size - n + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != ext[i]) return false;
    }
    return true;
}

static bool image_brand(const uint8_t *b) {
    static const char *const brands[] = { "avif", "avis", "heic", "heix", "heim", "heis", "mif1", "msf1" };
    for (size_t i = 0; i < sizeof(brands) / sizeof(brands[0]); ++i) if (memcmp(b, brands[i], 4) == 0) return true;
    return false;
}

/* What follows an ID3v2 tag (or several, or padding): FLAC, ADTS or MPEG audio. */
static bool read_after_id3(rubraview_head_src_t *src, proven_u8str_view_t name, rubraview_media_facts_t *out) {
    uint64_t at = 0;
    for (unsigned guard = 0; guard < 4; ++guard) {
        const uint8_t *t = rubraview_head_bytes(src, at, 10);
        if (!t || memcmp(t, "ID3", 3) != 0) break;
        uint64_t size = ((uint64_t)(t[6] & 0x7F) << 21) | ((uint64_t)(t[7] & 0x7F) << 14) | ((uint64_t)(t[8] & 0x7F) << 7) | (t[9] & 0x7F);
        at += 10u + size + (t[5] & 0x10 ? 10u : 0u);
    }
    size_t got = 0;
    const uint8_t *p = rubraview_head_view(src, at, 4, &got);
    if (p && got >= 4 && memcmp(p, "fLaC", 4) == 0) return read_flac(src, at, out);
    if (p && got >= 2 && p[0] == 0xFF && (p[1] & 0xF6) == 0xF0) return read_adts(src, at, out);
    if (name_is(name, "aac")) return read_adts(src, at, out);
    return read_mp3(src, at, out);
}

bool rubraview_media_head_read(rubraview_head_src_t *src, proven_u8str_view_t name, rubraview_media_facts_t *out) {
    *out = (rubraview_media_facts_t){ .format = "" };
    size_t n = 0;
    const uint8_t *h = rubraview_head_view(src, 0, 400, &n);
    if (!h || n < 4) return false;
    if (h[0] == 0xFF && h[1] == 0xD8 && h[2] == 0xFF) return read_jpeg(src, out);
    if (n >= 8 && memcmp(h, "\x89PNG\r\n\x1a\n", 8) == 0) return read_png(src, out);
    if (n >= 6 && (memcmp(h, "GIF87a", 6) == 0 || memcmp(h, "GIF89a", 6) == 0)) return read_gif(src, out);
    if (n >= 18 && h[0] == 'B' && h[1] == 'M') {
        uint32_t dib = le32(h + 14);
        if (dib == 12 || dib == 40 || dib == 52 || dib == 56 || dib == 64 || dib == 108 || dib == 124) return read_bmp(src, out);
    }
    if ((h[0] == 'I' && h[1] == 'I' && (le16(h + 2) == 42 || le16(h + 2) == 43)) ||
        (h[0] == 'M' && h[1] == 'M' && (be16(h + 2) == 42 || be16(h + 2) == 43))) return read_tiff(src, out);
    if (n >= 12 && (memcmp(h, "RIFF", 4) == 0 || memcmp(h, "RF64", 4) == 0)) {
        if (memcmp(h + 8, "WEBP", 4) == 0) return read_webp(src, out);
        if (memcmp(h + 8, "AVI ", 4) == 0) return read_avi(src, out);
        if (memcmp(h + 8, "WAVE", 4) == 0) return read_wav(src, out);
    }
    if (n >= 12 && memcmp(h, "\0\0\0\x0CJXL \r\n\x87\n", 12) == 0) return read_jxl(src, out, true);
    if (h[0] == 0xFF && h[1] == 0x0A) return read_jxl(src, out, false);
    if (n >= 12 && memcmp(h + 4, "ftyp", 4) == 0) return image_brand(h + 8) ? read_heif(src, out, memcmp(h + 8, "avi", 3) == 0 ? "avif" : "heif") : read_mp4(src, out);
    if (n >= 8 && (memcmp(h + 4, "moov", 4) == 0 || memcmp(h + 4, "mdat", 4) == 0 || memcmp(h + 4, "wide", 4) == 0 ||
                   memcmp(h + 4, "free", 4) == 0 || memcmp(h + 4, "skip", 4) == 0 || memcmp(h + 4, "pnot", 4) == 0)) return read_mp4(src, out);
    if (h[0] == 0x1A && h[1] == 0x45 && h[2] == 0xDF && h[3] == 0xA3) return read_mkv(src, out);
    if (n >= 16 && memcmp(h, ASF_HEADER, 16) == 0) return read_asf(src, out);
    if (memcmp(h, "FLV\x01", 4) == 0) return read_flv(src, out);
    if (memcmp(h, "OggS", 4) == 0) return read_ogg(src, out);
    if (memcmp(h, "fLaC", 4) == 0) return read_flac(src, 0, out);
    if (memcmp(h, "ID3", 3) == 0) return read_after_id3(src, name, out);
    if (h[0] == 0 && h[1] == 0 && h[2] == 1 && (h[3] == 0xBA || h[3] == 0xB3)) return read_ps(src, out);
    if (n > 376 && h[0] == 0x47 && h[188] == 0x47 && h[376] == 0x47) return read_ts(src, out, 188, 0);
    if (n > 388 && h[4] == 0x47 && h[196] == 0x47 && h[388] == 0x47) return read_ts(src, out, 192, 4);
    if (h[0] == 0 && h[1] == 0 && h[2] == 1 && h[3] == 0 && n >= 6 && le16(h + 4) > 0 && name_is(name, "ico")) return read_ico(src, out);
    if (h[0] == 0xFF && (h[1] & 0xF6) == 0xF0) return read_adts(src, 0, out);
    mpa_t f;
    if (mpa_frame(h, &f) || name_is(name, "mp3")) return read_mp3(src, 0, out);
    return false;
}
