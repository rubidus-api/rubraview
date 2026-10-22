#include "rubraview/keepinfo.h"
#include "rubraview/exif.h"
#include "rubraview/fileinfo.h"
#include "rubraview/lang.h"
#include "rubraview/path.h"
#include "rubraview/version.h"

#include <stdio.h>
#include <string.h>

/* ---- bytes ---- */

static uint32_t be16(const uint8_t *p) { return ((uint32_t)p[0] << 8) | p[1]; }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void put_be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void put_le32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static uint32_t crc32_of(const uint8_t *a, size_t an, const uint8_t *b, size_t bn) {
    static uint32_t table[256];
    static bool made;
    if (!made) {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        made = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < an; ++i) c = table[(c ^ a[i]) & 0xFFu] ^ (c >> 8);
    for (size_t i = 0; i < bn; ++i) c = table[(c ^ b[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/* A buffer grown in an arena: what is written, and whether it all fitted. */
typedef struct out_buf {
    proven_arena_t *arena;
    uint8_t *p;
    size_t len, cap;
    bool failed;
} out_buf_t;

static void out_put(out_buf_t *o, const void *data, size_t n) {
    if (o->failed || n == 0) return;
    if (o->len + n > o->cap) {
        size_t cap = o->cap ? o->cap : 4096u;
        while (cap < o->len + n) {
            if (cap > SIZE_MAX / 2u) { o->failed = true; return; }
            cap *= 2u;
        }
        proven_result_mem_mut_t res = proven_arena_alloc(o->arena, cap);
        if (!proven_is_ok(res.err)) { o->failed = true; return; }
        if (o->len) memcpy(res.value.ptr, o->p, o->len);
        o->p = (uint8_t*)res.value.ptr;
        o->cap = cap;
    }
    memcpy(o->p + o->len, data, n);
    o->len += n;
}

static void out_byte(out_buf_t *o, uint8_t b) { out_put(o, &b, 1); }
static void out_str(out_buf_t *o, const char *s) { out_put(o, s, strlen(s)); }
static void out_u8(out_buf_t *o, proven_u8str_view_t s) { out_put(o, s.ptr, s.size); }

static proven_u8str_view_t out_done(out_buf_t *o) {
    if (o->failed) return PROVEN_LIT("");
    out_byte(o, 0);   /* a NUL after it, not counted */
    if (o->failed) return PROVEN_LIT("");
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)o->p, .size = o->len - 1 };
}

static bool valid_utf8(const uint8_t *s, size_t n) {
    for (size_t i = 0; i < n;) {
        uint8_t c = s[i];
        size_t k = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (k == 0 || i + k > n) return false;
        for (size_t j = 1; j < k; ++j) if ((s[i + j] & 0xC0) != 0x80) return false;
        i += k;
    }
    return true;
}

/* Text as UTF-8: as it is when it is, else read as Latin-1. Trailing NULs and blanks dropped. */
static proven_u8str_view_t as_text(proven_arena_t *arena, const uint8_t *s, size_t n) {
    while (n > 0 && (s[n - 1] == 0 || s[n - 1] == ' ' || s[n - 1] == '\r' || s[n - 1] == '\n')) n--;
    out_buf_t o = { .arena = arena };
    if (valid_utf8(s, n)) {
        out_put(&o, s, n);
    } else {
        for (size_t i = 0; i < n; ++i) {
            if (s[i] < 0x80) out_byte(&o, s[i]);
            else { out_byte(&o, (uint8_t)(0xC0 | (s[i] >> 6))); out_byte(&o, (uint8_t)(0x80 | (s[i] & 0x3F))); }
        }
    }
    return out_done(&o);
}

static void add_comment(proven_arena_t *arena, rubraview_details_src_t *d, const char *key, size_t key_len,
                        const uint8_t *text, size_t n) {
    if (d->comment_count >= RUBRAVIEW_DETAILS_MAX_COMMENTS) return;
    proven_u8str_view_t t = as_text(arena, text, n);
    if (t.size == 0) return;
    if (key && key_len > 0) {
        out_buf_t o = { .arena = arena };
        out_put(&o, key, key_len);
        out_str(&o, ": ");
        out_u8(&o, t);
        t = out_done(&o);
        if (t.size == 0) return;
    }
    d->comments[d->comment_count++] = t;
}

static bool is_tiff(const uint8_t *p, size_t n) {
    return n >= 8 && ((p[0] == 'I' && p[1] == 'I' && p[2] == 42 && p[3] == 0) || (p[0] == 'M' && p[1] == 'M' && p[2] == 0 && p[3] == 42));
}

/* ---- reading ---- */

static const char XMP_NS[] = "http://ns.adobe.com/xap/1.0/";   /* and its NUL: 29 bytes */

static void read_jpeg(proven_arena_t *arena, const uint8_t *b, size_t size, rubraview_details_src_t *d) {
    size_t at = 2;
    while (at + 4 <= size) {
        if (b[at] != 0xFF) break;
        uint8_t m = b[at + 1];
        if (m == 0xFF) { at++; continue; }
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { at += 2; continue; }
        if (m == 0xDA || m == 0xD9) break;
        size_t len = be16(b + at + 2);
        if (len < 2 || at + 2 + len > size) break;
        const uint8_t *p = b + at + 4;
        size_t n = len - 2;
        if (m == 0xE1 && n > 6 && memcmp(p, "Exif\0\0", 6) == 0 && d->exif.size == 0 && is_tiff(p + 6, n - 6)) {
            d->exif = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)p + 6, .size = n - 6 };
        } else if (m == 0xE1 && n > sizeof(XMP_NS) && memcmp(p, XMP_NS, sizeof(XMP_NS)) == 0 && d->xmp.size == 0) {
            d->xmp = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)p + sizeof(XMP_NS), .size = n - sizeof(XMP_NS) };
        } else if (m == 0xED && n > 14 && memcmp(p, "Photoshop 3.0\0", 14) == 0 && d->iptc_app13.size == 0) {
            d->iptc_app13 = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)p, .size = n };
        } else if (m == 0xFE) {
            add_comment(arena, d, NULL, 0, p, n);
        }
        at += 2 + len;
    }
}

static void read_png(proven_arena_t *arena, const uint8_t *b, size_t size, rubraview_details_src_t *d) {
    size_t at = 8;
    while (at + 12 <= size) {
        uint32_t len = be32(b + at);
        if (len > size - at - 12) break;
        const uint8_t *type = b + at + 4, *p = b + at + 8;
        if (memcmp(type, "IEND", 4) == 0) break;
        bool text = memcmp(type, "tEXt", 4) == 0 || memcmp(type, "zTXt", 4) == 0 || memcmp(type, "iTXt", 4) == 0;
        if (memcmp(type, "eXIf", 4) == 0 && d->exif.size == 0 && is_tiff(p, len)) {
            d->exif = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)p, .size = len };
        } else if (text) {
            size_t k = 0;
            while (k < len && k < 80 && p[k]) k++;
            bool keyed = k > 0 && k < len;
            if (keyed && type[0] == 'i' && k + 2 < len) {
                bool compressed = p[k + 1] != 0;
                size_t q = k + 3;
                while (q < len && p[q]) q++;          /* language */
                q++;
                while (q < len && p[q]) q++;          /* translated keyword */
                q++;
                bool xmp = k == 17 && memcmp(p, "XML:com.adobe.xmp", 17) == 0;
                if (!compressed && q <= len) {
                    if (xmp) { if (d->xmp.size == 0) d->xmp = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)p + q, .size = len - q }; }
                    else add_comment(arena, d, (const char*)p, k, p + q, len - q);
                } else {
                    d->unread++;
                }
                if ((!xmp || compressed) && d->png_chunk_count < RUBRAVIEW_DETAILS_MAX_CHUNKS)
                    d->png_chunks[d->png_chunk_count++] = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)b + at, .size = (size_t)len + 12u };
            } else if (keyed) {
                if (type[0] == 't') add_comment(arena, d, (const char*)p, k, p + k + 1, len - k - 1);
                else d->unread++;
                if (d->png_chunk_count < RUBRAVIEW_DETAILS_MAX_CHUNKS)
                    d->png_chunks[d->png_chunk_count++] = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)b + at, .size = (size_t)len + 12u };
            }
        }
        at += 12u + len;
    }
}

static void read_gif(proven_arena_t *arena, const uint8_t *b, size_t size, rubraview_details_src_t *d) {
    if (size < 13) return;
    size_t at = 13;
    if (b[10] & 0x80) at += 3u * ((size_t)1 << ((b[10] & 7) + 1));
    while (at < size) {
        uint8_t kind = b[at];
        if (kind == 0x3B) break;
        if (kind == 0x21 && at + 2 <= size) {
            uint8_t label = b[at + 1];
            at += 2;
            out_buf_t o = { .arena = arena };
            while (at < size && b[at] != 0) {
                size_t n = b[at];
                if (at + 1 + n > size) return;
                if (label == 0xFE) out_put(&o, b + at + 1, n);
                at += 1 + n;
            }
            at++;
            if (label == 0xFE) {
                proven_u8str_view_t t = out_done(&o);
                if (t.size > 0) add_comment(arena, d, NULL, 0, (const uint8_t*)t.ptr, t.size);
            }
        } else if (kind == 0x2C && at + 10 <= size) {
            uint8_t flags = b[at + 9];
            at += 10;
            if (flags & 0x80) at += 3u * ((size_t)1 << ((flags & 7) + 1));
            at++;   /* LZW minimum code size */
            while (at < size && b[at] != 0) at += 1u + b[at];
            at++;
        } else {
            break;
        }
    }
}

static void read_webp(const uint8_t *b, size_t size, rubraview_details_src_t *d) {
    size_t at = 12;
    while (at + 8 <= size) {
        uint32_t len = le32(b + at + 4);
        if (len > size - at - 8) break;
        const uint8_t *p = b + at + 8;
        if (memcmp(b + at, "EXIF", 4) == 0 && d->exif.size == 0) {
            const uint8_t *e = p;
            size_t n = len;
            if (n > 6 && memcmp(e, "Exif\0\0", 6) == 0) { e += 6; n -= 6; }
            if (is_tiff(e, n)) d->exif = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)e, .size = n };
        } else if (memcmp(b + at, "XMP ", 4) == 0 && d->xmp.size == 0) {
            d->xmp = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)p, .size = len };
        }
        at += 8u + len + (len & 1u);
    }
}

/* The words: the information lines as the window lays them out, CRLF. */
static void build_words(proven_arena_t *arena, rubraview_details_src_t *d) {
    out_buf_t o = { .arena = arena };
    proven_u8str_view_t rows[512];
    size_t n = rubraview_info_rows(arena, &d->info, 24, rows, sizeof(rows) / sizeof(rows[0]));
    for (size_t i = 0; i < n; ++i) { out_u8(&o, rows[i]); out_str(&o, "\r\n"); }
    d->words = n > 0 ? out_done(&o) : PROVEN_LIT("");
}

/* ---- 1.31.0 (RV-139, D-185): ISO BMFF boxes — AVIF's HEIF items and JPEG XL's container ---- */

typedef struct bmff_box { size_t at, head, size; char type[4]; } bmff_box_t;   /* `size` the whole box */

static uint64_t be64(const uint8_t *p) { return ((uint64_t)be32(p) << 32) | be32(p + 4); }

/* The box at `at`, within [at, end). */
static bool bmff_at(const uint8_t *b, size_t end, size_t at, bmff_box_t *x) {
    if (at > end || end - at < 8) return false;
    uint64_t size = be32(b + at);
    size_t head = 8;
    if (size == 1) {
        if (end - at < 16) return false;
        size = be64(b + at + 8);
        head = 16;
    } else if (size == 0) {
        size = end - at;
    }
    if (size < head || size > end - at) return false;
    *x = (bmff_box_t){ .at = at, .head = head, .size = (size_t)size };
    memcpy(x->type, b + at + 4, 4);
    return true;
}

/* The first child of type `type` among the boxes in [from, end). */
static bool bmff_find(const uint8_t *b, size_t from, size_t end, const char *type, bmff_box_t *x) {
    for (size_t at = from; bmff_at(b, end, at, x); at += x->size)
        if (memcmp(x->type, type, 4) == 0) return true;
    return false;
}

static uint64_t be_n(const uint8_t *p, int n) {
    uint64_t v = 0;
    for (int i = 0; i < n; ++i) v = (v << 8) | p[i];
    return v;
}

static void out_be(out_buf_t *o, uint64_t v, int n) {
    for (int i = n - 1; i >= 0; --i) out_byte(o, (uint8_t)(v >> (8 * i)));
}

/* HEIF's item location box, read whole. */
typedef struct iloc_extent { uint64_t index, offset, length; } iloc_extent_t;
typedef struct iloc_item { uint32_t id; uint16_t method, dref; uint64_t base; size_t first, count; } iloc_item_t;
typedef struct iloc_box {
    int version, offset_size, length_size, base_size, index_size;
    iloc_item_t *items; size_t item_count;
    iloc_extent_t *extents; size_t extent_count;
} iloc_box_t;

static bool iloc_read(proven_arena_t *arena, const uint8_t *b, const bmff_box_t *box, iloc_box_t *out) {
    size_t at = box->at + box->head, end = box->at + box->size;
    if (end - at < 8) return false;
    *out = (iloc_box_t){ .version = b[at] };
    if (out->version > 2) return false;
    at += 4;
    out->offset_size = b[at] >> 4; out->length_size = b[at] & 15;
    out->base_size = b[at + 1] >> 4; out->index_size = out->version >= 1 ? (b[at + 1] & 15) : 0;
    at += 2;
    int sizes[] = { out->offset_size, out->length_size, out->base_size, out->index_size };
    for (int i = 0; i < 4; ++i) if (sizes[i] != 0 && sizes[i] != 4 && sizes[i] != 8) return false;
    size_t count;
    if (out->version < 2) { if (end - at < 2) return false; count = be16(b + at); at += 2; }
    else { if (end - at < 4) return false; count = be32(b + at); at += 4; }
    if (count > 4096) return false;
    proven_result_mem_mut_t ri = proven_arena_alloc(arena, (count ? count : 1) * sizeof(iloc_item_t));
    if (!proven_is_ok(ri.err)) return false;
    out->items = (iloc_item_t*)ri.value.ptr;
    size_t cap = 0;
    for (size_t i = 0; i < count; ++i) {
        iloc_item_t it = {0};
        size_t need = (out->version < 2 ? 2u : 4u) + (out->version >= 1 ? 2u : 0u) + 2u + (size_t)out->base_size + 2u;
        if (end - at < need) return false;
        if (out->version < 2) { it.id = be16(b + at); at += 2; } else { it.id = be32(b + at); at += 4; }
        if (out->version >= 1) { it.method = (uint16_t)(be16(b + at) & 15u); at += 2; }
        it.dref = (uint16_t)be16(b + at); at += 2;
        it.base = be_n(b + at, out->base_size); at += (size_t)out->base_size;
        it.count = be16(b + at); at += 2;
        it.first = out->extent_count;
        size_t per = (size_t)out->index_size + (size_t)out->offset_size + (size_t)out->length_size;
        if (it.count > 1024 || end - at < per * it.count) return false;
        if (out->extent_count + it.count > cap) {
            size_t ncap = (out->extent_count + it.count) * 2 + 8;
            proven_result_mem_mut_t re = proven_arena_alloc(arena, ncap * sizeof(iloc_extent_t));
            if (!proven_is_ok(re.err)) return false;
            if (out->extent_count) memcpy(re.value.ptr, out->extents, out->extent_count * sizeof(iloc_extent_t));
            out->extents = (iloc_extent_t*)re.value.ptr;
            cap = ncap;
        }
        for (size_t e = 0; e < it.count; ++e) {
            iloc_extent_t x = {0};
            x.index = be_n(b + at, out->index_size); at += (size_t)out->index_size;
            x.offset = be_n(b + at, out->offset_size); at += (size_t)out->offset_size;
            x.length = be_n(b + at, out->length_size); at += (size_t)out->length_size;
            out->extents[out->extent_count++] = x;
        }
        out->items[out->item_count++] = it;
    }
    return true;
}

/* An item's bytes when they lie in the file in one extent (what the readers here need). */
static proven_u8str_view_t iloc_bytes(const iloc_box_t *l, uint32_t id, const uint8_t *b, size_t size) {
    for (size_t i = 0; i < l->item_count; ++i) {
        const iloc_item_t *it = &l->items[i];
        if (it->id != id || it->method != 0 || it->count != 1) continue;
        const iloc_extent_t *x = &l->extents[it->first];
        uint64_t at = it->base + x->offset, len = x->length ? x->length : (at < size ? size - at : 0);
        if (at > size || len > size - at) return PROVEN_LIT("");
        return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)b + at, .size = (size_t)len };
    }
    return PROVEN_LIT("");
}

/* An AVIF's (HEIF's) Exif and XMP items. */
static void read_avif(const uint8_t *b, size_t size, rubraview_details_src_t *d, proven_arena_t *arena) {
    bmff_box_t meta, iinf, iloc;
    if (!bmff_find(b, 0, size, "meta", &meta)) return;
    size_t from = meta.at + meta.head + 4, end = meta.at + meta.size;
    iloc_box_t l;
    if (!bmff_find(b, from, end, "iinf", &iinf) || !bmff_find(b, from, end, "iloc", &iloc) || !iloc_read(arena, b, &iloc, &l)) return;
    size_t at = iinf.at + iinf.head;
    if (iinf.size - iinf.head < 6) return;
    int v = b[at];
    at += 4 + (v == 0 ? 2u : 4u);
    bmff_box_t infe;
    for (; bmff_at(b, iinf.at + iinf.size, at, &infe); at += infe.size) {
        if (memcmp(infe.type, "infe", 4) != 0) continue;
        size_t p = infe.at + infe.head, e = infe.at + infe.size;
        if (e - p < 12) continue;
        int iv = b[p];
        if (iv < 2) continue;
        p += 4;
        uint32_t id = iv == 2 ? be16(b + p) : be32(b + p);
        p += iv == 2 ? 2u : 4u;
        p += 2;   /* protection */
        if (e - p < 4) continue;
        const uint8_t *type = b + p;
        p += 4;
        while (p < e && b[p]) ++p;   /* the item's name */
        ++p;
        proven_u8str_view_t data = iloc_bytes(&l, id, b, size);
        if (memcmp(type, "Exif", 4) == 0 && data.size >= 4) {
            uint32_t skip = be32((const uint8_t*)data.ptr);
            if (skip <= data.size - 4 && is_tiff((const uint8_t*)data.ptr + 4 + skip, data.size - 4 - skip))
                d->exif = (proven_u8str_view_t){ .ptr = data.ptr + 4 + skip, .size = data.size - 4 - skip };
        } else if (memcmp(type, "mime", 4) == 0 && p < e) {
            const char *ct = (const char*)b + p;
            size_t ctl = 0;
            while (p + ctl < e && ct[ctl]) ++ctl;
            if (ctl == 19 && memcmp(ct, "application/rdf+xml", 19) == 0 && data.size > 0) d->xmp = data;
        }
    }
}

/* A JPEG XL container's Exif and xml boxes. */
static void read_jxl(const uint8_t *b, size_t size, rubraview_details_src_t *d) {
    bmff_box_t x;
    for (size_t at = 0; bmff_at(b, size, at, &x); at += x.size) {
        const uint8_t *p = b + x.at + x.head;
        size_t n = x.size - x.head;
        if (memcmp(x.type, "Exif", 4) == 0 && n >= 4) {
            uint32_t skip = be32(p);
            if (skip <= n - 4 && is_tiff(p + 4 + skip, n - 4 - skip)) d->exif = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)p + 4 + skip, .size = n - 4 - skip };
        } else if (memcmp(x.type, "xml ", 4) == 0 && n > 0) {
            d->xmp = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)p, .size = n };
        }
    }
}

static const uint8_t JXL_SIGNATURE[12] = { 0, 0, 0, 12, 'J', 'X', 'L', ' ', 0x0D, 0x0A, 0x87, 0x0A };

static bool is_avif(const uint8_t *b, size_t size) {
    bmff_box_t f;
    if (!bmff_at(b, size, 0, &f) || memcmp(f.type, "ftyp", 4) != 0 || f.size < 16) return false;
    for (size_t at = 8; at + 4 <= f.size; at += 4) {
        if (at == 12) continue;   /* the minor version */
        if (memcmp(b + at, "avif", 4) == 0 || memcmp(b + at, "avis", 4) == 0) return true;
    }
    return false;
}

bool rubraview_details_read(proven_arena_t *arena, const uint8_t *b, size_t size, rubraview_details_src_t *out) {
    if (!out) return false;
    *out = (rubraview_details_src_t){0};
    if (!arena || !b || size < 12) return false;

    if (b[0] == 0xFF && b[1] == 0xD8) read_jpeg(arena, b, size, out);
    else if (size >= 8 && memcmp(b, "\x89PNG\r\n\x1a\n", 8) == 0) read_png(arena, b, size, out);
    else if (memcmp(b, "GIF8", 4) == 0) read_gif(arena, b, size, out);
    else if (memcmp(b, "RIFF", 4) == 0 && memcmp(b + 8, "WEBP", 4) == 0) read_webp(b, size, out);
    else if (size >= 12 && memcmp(b, JXL_SIGNATURE, 12) == 0) read_jxl(b, size, out);   /* 1.31.0 */
    else if (is_avif(b, size)) read_avif(b, size, out, arena);

    /* In words, as the information window has them: what every format can hold as text. */
    rubraview_info_t info = {0};
    rubraview_exif_info_t exif;
    bool exif_found = rubraview_exif_read(b, size, &exif) && exif.found;
    if (exif_found) rubraview_info_add_exif(arena, &info, &exif);
    rubraview_iptc_info_t iptc;
    out->has_iptc = rubraview_iptc_read(b, size, &iptc) && iptc.found;
    if (out->has_iptc) rubraview_info_add_iptc(arena, &info, &iptc);
    if (out->comment_count > 0) rubraview_info_heading(arena, &info, T("Comments"));
    for (size_t i = 0; i < out->comment_count; ++i) rubraview_info_add(arena, &info, T("Comment"), out->comments[i]);
    if (out->unread > 0) rubraview_info_addf(arena, &info, T("Compressed texts"), T("%zu, not read"), out->unread);

    out->info = info;
    build_words(arena, out);

    out->any = exif_found || out->exif.size > 0 || out->xmp.size > 0 || out->iptc_app13.size > 0 || out->has_iptc ||
               out->comment_count > 0 || out->png_chunk_count > 0 || out->unread > 0;
    return out->any;
}

/* ---- putting ---- */

/* The EXIF block copied, saying orientation 1 (normal): the pixels were turned upright. */
static proven_u8str_view_t exif_upright(proven_arena_t *arena, proven_u8str_view_t exif) {
    proven_result_mem_mut_t res = proven_arena_alloc(arena, exif.size);
    if (!proven_is_ok(res.err)) return PROVEN_LIT("");
    uint8_t *t = (uint8_t*)res.value.ptr;
    memcpy(t, exif.ptr, exif.size);
    size_t n = exif.size;
    bool le = t[0] == 'I';
    uint32_t ifd = le ? le32(t + 4) : be32(t + 4);
    if (ifd >= 8 && (size_t)ifd + 2 <= n) {
        uint32_t count = le ? le16(t + ifd) : be16(t + ifd);
        for (uint32_t e = 0; e < count; ++e) {
            size_t at = (size_t)ifd + 2u + 12u * e;
            if (at + 12 > n) break;
            uint32_t tag = le ? le16(t + at) : be16(t + at);
            uint32_t type = le ? le16(t + at + 2) : be16(t + at + 2);
            if (tag == 0x0112 && type == 3) {
                if (le) { t[at + 8] = 1; t[at + 9] = 0; } else { t[at + 8] = 0; t[at + 9] = 1; }
            }
        }
    }
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)t, .size = n };
}

/* Words for what has no block of its own in this file: IPTC read but not copied, an EXIF only read, an XMP left out. */
static bool words_needed(const rubraview_details_src_t *s, bool exif_in, bool iptc_in, bool xmp_in) {
    bool exif_words = s->words.size > 0 && !exif_in;
    return (s->has_iptc && !iptc_in) || exif_words || (s->xmp.size > 0 && !xmp_in) || s->unread > 0;
}

static void jpeg_segment(out_buf_t *o, uint8_t marker, const char *head, size_t head_len, proven_u8str_view_t body) {
    out_byte(o, 0xFF);
    out_byte(o, marker);
    size_t len = 2 + head_len + body.size;
    out_byte(o, (uint8_t)(len >> 8));
    out_byte(o, (uint8_t)len);
    out_put(o, head, head_len);
    out_u8(o, body);
}

/* A comment as COM segments of at most 65533 bytes, cut between characters. */
static void jpeg_comment(out_buf_t *o, proven_u8str_view_t text) {
    size_t at = 0;
    while (at < text.size) {
        size_t n = text.size - at;
        if (n > 65533) {
            n = 65533;
            while (n > 0 && ((uint8_t)text.ptr[at + n] & 0xC0) == 0x80) n--;
        }
        jpeg_segment(o, 0xFE, NULL, 0, (proven_u8str_view_t){ .ptr = text.ptr + at, .size = n });
        at += n;
    }
}

static proven_u8str_view_t joined_words(proven_arena_t *arena, const rubraview_details_src_t *s, bool with_comments, bool with_xmp) {
    out_buf_t o = { .arena = arena };
    out_str(&o, T("Details kept by Rubraview"));
    out_str(&o, "\r\n");
    out_u8(&o, s->words);   /* the comments are among the words */
    (void)with_comments;
    if (with_xmp && s->xmp.size > 0) { out_str(&o, "XMP\r\n"); out_u8(&o, s->xmp); out_str(&o, "\r\n"); }
    return out_done(&o);
}

static rubraview_details_put_t put_jpeg(proven_arena_t *arena, proven_u8str_view_t file, const rubraview_details_src_t *s) {
    rubraview_details_put_t r = { .file = file, .all_kept = false };
    const uint8_t *b = (const uint8_t*)file.ptr;
    if (file.size < 4 || b[0] != 0xFF || b[1] != 0xD8) return r;
    size_t head = 2;
    if (file.size >= 6 && b[2] == 0xFF && b[3] == 0xE0) {   /* after JFIF's APP0, which must come first */
        size_t len = be16(b + 4);
        if (len >= 2 && 4 + len <= file.size) head = 4 + len;
    }
    out_buf_t o = { .arena = arena };
    out_put(&o, b, head);
    bool exif_in = false, xmp_in = false, iptc_in = false;
    if (s->exif.size > 0 && s->exif.size + 6 + 2 <= 65535) {
        proven_u8str_view_t e = exif_upright(arena, s->exif);
        if (e.size > 0) { jpeg_segment(&o, 0xE1, "Exif\0\0", 6, e); exif_in = true; }
    }
    if (s->xmp.size > 0 && s->xmp.size + sizeof(XMP_NS) + 2 <= 65535) { jpeg_segment(&o, 0xE1, XMP_NS, sizeof(XMP_NS), s->xmp); xmp_in = true; }
    if (s->iptc_app13.size > 0 && s->iptc_app13.size + 2 <= 65535) { jpeg_segment(&o, 0xED, NULL, 0, s->iptc_app13); iptc_in = true; }
    if (words_needed(s, exif_in, iptc_in, xmp_in)) {
        jpeg_comment(&o, joined_words(arena, s, true, !xmp_in));
    } else {
        for (size_t i = 0; i < s->comment_count; ++i) jpeg_comment(&o, s->comments[i]);
    }
    out_put(&o, b + head, file.size - head);
    proven_u8str_view_t made = out_done(&o);
    if (made.size == 0) return r;
    r.file = made;
    r.all_kept = true;
    return r;
}

static void png_chunk(out_buf_t *o, const char *type, const uint8_t *data, size_t n) {
    uint8_t len[4];
    put_be32(len, (uint32_t)n);
    out_put(o, len, 4);
    out_put(o, type, 4);
    out_put(o, data, n);
    uint8_t crc[4];
    put_be32(crc, crc32_of((const uint8_t*)type, 4, data, n));
    out_put(o, crc, 4);
}

/* An iTXt chunk: keyword, uncompressed, no language, the UTF-8 text. */
static void png_itxt(proven_arena_t *arena, out_buf_t *o, const char *key, proven_u8str_view_t text) {
    out_buf_t d = { .arena = arena };
    out_str(&d, key);
    out_byte(&d, 0); out_byte(&d, 0); out_byte(&d, 0);   /* NUL, uncompressed, method */
    out_byte(&d, 0); out_byte(&d, 0);                    /* no language, no translated keyword */
    out_u8(&d, text);
    if (d.failed) { o->failed = true; return; }
    png_chunk(o, "iTXt", d.p, d.len);
}

static rubraview_details_put_t put_png(proven_arena_t *arena, proven_u8str_view_t file, const rubraview_details_src_t *s) {
    rubraview_details_put_t r = { .file = file, .all_kept = false };
    const uint8_t *b = (const uint8_t*)file.ptr;
    if (file.size < 33 || memcmp(b, "\x89PNG\r\n\x1a\n", 8) != 0 || memcmp(b + 12, "IHDR", 4) != 0) return r;
    size_t head = 8 + 12 + be32(b + 8);
    if (head > file.size) return r;
    out_buf_t o = { .arena = arena };
    out_put(&o, b, head);
    bool exif_in = false;
    if (s->exif.size > 0 && s->exif.size <= 0x7FFFFFFFu) {
        proven_u8str_view_t e = exif_upright(arena, s->exif);
        if (e.size > 0) { png_chunk(&o, "eXIf", (const uint8_t*)e.ptr, e.size); exif_in = true; }
    }
    if (s->xmp.size > 0) png_itxt(arena, &o, "XML:com.adobe.xmp", s->xmp);
    for (size_t i = 0; i < s->png_chunk_count; ++i) out_u8(&o, s->png_chunks[i]);   /* a PNG's own texts, as they were */
    bool words = words_needed(s, exif_in, false, true);
    if (words) {
        png_itxt(arena, &o, "Comment", joined_words(arena, s, s->png_chunk_count == 0, false));
    } else if (s->png_chunk_count == 0) {
        for (size_t i = 0; i < s->comment_count; ++i) png_itxt(arena, &o, "Comment", s->comments[i]);
    }
    out_put(&o, b + head, file.size - head);
    proven_u8str_view_t made = out_done(&o);
    if (made.size == 0) return r;
    r.file = made;
    r.all_kept = true;
    return r;
}

static rubraview_details_put_t put_gif(proven_arena_t *arena, proven_u8str_view_t file, const rubraview_details_src_t *s) {
    rubraview_details_put_t r = { .file = file, .all_kept = false };
    const uint8_t *b = (const uint8_t*)file.ptr;
    if (file.size < 14 || memcmp(b, "GIF8", 4) != 0 || b[file.size - 1] != 0x3B) return r;
    proven_u8str_view_t text = (s->words.size > 0 || s->xmp.size > 0) ? joined_words(arena, s, true, true) : PROVEN_LIT("");
    if (text.size == 0) { r.all_kept = true; return r; }
    out_buf_t o = { .arena = arena };
    out_put(&o, b, file.size - 1);
    out_byte(&o, 0x21);
    out_byte(&o, 0xFE);
    for (size_t at = 0; at < text.size;) {
        size_t n = text.size - at > 255 ? 255 : text.size - at;
        out_byte(&o, (uint8_t)n);
        out_put(&o, text.ptr + at, n);
        at += n;
    }
    out_byte(&o, 0);
    out_byte(&o, 0x3B);
    proven_u8str_view_t made = out_done(&o);
    if (made.size == 0) return r;
    r.file = made;
    r.all_kept = true;
    return r;
}

static void riff_chunk(out_buf_t *o, const char *fourcc, proven_u8str_view_t data) {
    uint8_t len[4];
    put_le32(len, (uint32_t)data.size);
    out_put(o, fourcc, 4);
    out_put(o, len, 4);
    out_u8(o, data);
    if (data.size & 1u) out_byte(o, 0);
}

static rubraview_details_put_t put_webp(proven_arena_t *arena, proven_u8str_view_t file, const rubraview_details_src_t *s) {
    rubraview_details_put_t r = { .file = file, .all_kept = false };
    const uint8_t *b = (const uint8_t*)file.ptr;
    if (file.size < 20 || memcmp(b, "RIFF", 4) != 0 || memcmp(b + 8, "WEBP", 4) != 0) return r;
    bool text_left = s->comment_count > 0 || s->has_iptc || s->unread > 0 || (s->exif.size == 0 && s->words.size > 0);
    if (s->exif.size == 0 && s->xmp.size == 0) { r.all_kept = !text_left; return r; }

    const uint8_t *first = b + 12;
    uint32_t first_len = le32(first + 4);
    /* RV-156: the chunk and its pad byte inside the file, and a VP8X its ten bytes. */
    if ((size_t)first_len + (first_len & 1u) > file.size - 20) return r;
    uint8_t flags = 0;
    uint32_t w = 0, h = 0;
    bool extended = memcmp(first, "VP8X", 4) == 0;
    if (extended && first_len < 10) return r;
    if (!extended) {
        const uint8_t *p = first + 8;
        if (memcmp(first, "VP8 ", 4) == 0 && first_len >= 10) {
            w = le16(p + 6) & 0x3FFFu;
            h = le16(p + 8) & 0x3FFFu;
        } else if (memcmp(first, "VP8L", 4) == 0 && first_len >= 5 && p[0] == 0x2F) {
            uint32_t bits = le32(p + 1);
            w = (bits & 0x3FFFu) + 1u;
            h = ((bits >> 14) & 0x3FFFu) + 1u;
            if ((bits >> 28) & 1u) flags |= 0x10;   /* alpha */
        } else {
            return r;
        }
        if (w == 0 || h == 0) return r;
    }
    bool exif_in = s->exif.size > 0 && s->exif.size < 0xFFFFFFF0u;
    bool xmp_in = s->xmp.size > 0 && s->xmp.size < 0xFFFFFFF0u;
    if (exif_in) flags |= 0x08;
    if (xmp_in) flags |= 0x04;

    out_buf_t o = { .arena = arena };
    out_put(&o, b, 12);
    if (extended) {
        out_put(&o, first, 8);
        out_byte(&o, (uint8_t)(first[8] | flags));
        out_put(&o, first + 9, (size_t)first_len - 1u + (first_len & 1u));
        size_t rest = 12u + 8u + first_len + (first_len & 1u);
        if (rest <= file.size) out_put(&o, b + rest, file.size - rest);
    } else {
        uint8_t x[10] = { flags, 0, 0, 0,
                          (uint8_t)(w - 1u), (uint8_t)((w - 1u) >> 8), (uint8_t)((w - 1u) >> 16),
                          (uint8_t)(h - 1u), (uint8_t)((h - 1u) >> 8), (uint8_t)((h - 1u) >> 16) };
        riff_chunk(&o, "VP8X", (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)x, .size = 10 });
        out_put(&o, b + 12, file.size - 12);
    }
    if (exif_in) {
        proven_u8str_view_t e = exif_upright(arena, s->exif);
        if (e.size == 0) return r;
        riff_chunk(&o, "EXIF", e);
    }
    if (xmp_in) riff_chunk(&o, "XMP ", s->xmp);
    if (o.failed || o.len < 12 || o.len - 8 > 0xFFFFFFFFu) return r;
    put_le32(o.p + 4, (uint32_t)(o.len - 8));
    proven_u8str_view_t made = out_done(&o);
    if (made.size == 0) return r;
    r.file = made;
    r.all_kept = !text_left;
    return r;
}

/* ---- 1.31.0 (RV-139, D-185): into a JPEG XL and an AVIF ---- */

static void bmff_box(out_buf_t *o, const char *type, const uint8_t *head, size_t head_len, proven_u8str_view_t body) {
    out_be(o, 8u + head_len + body.size, 4);
    out_put(o, type, 4);
    out_put(o, head, head_len);
    out_u8(o, body);
}

/* A JPEG XL: the bare codestream put in the container (ISO/IEC 18181-2), or a container's boxes kept; the
   Exif and xml boxes right after `ftyp`. */
static rubraview_details_put_t put_jxl(proven_arena_t *arena, proven_u8str_view_t file, const rubraview_details_src_t *s) {
    rubraview_details_put_t r = { .file = file, .all_kept = false };
    const uint8_t *b = (const uint8_t*)file.ptr;
    bool text_left = s->comment_count > 0 || s->has_iptc || s->unread > 0 || (s->exif.size == 0 && s->words.size > 0);
    if (s->exif.size == 0 && s->xmp.size == 0) { r.all_kept = !text_left; return r; }
    bool bare = file.size >= 2 && b[0] == 0xFF && b[1] == 0x0A;
    bmff_box_t ftyp = {0};
    if (!bare && !(file.size >= 12 && memcmp(b, JXL_SIGNATURE, 12) == 0 && bmff_at(b, file.size, 12, &ftyp) && memcmp(ftyp.type, "ftyp", 4) == 0))
        return r;
    out_buf_t o = { .arena = arena };
    if (bare) {
        out_put(&o, JXL_SIGNATURE, 12);
        static const uint8_t FTYP[20] = { 0, 0, 0, 20, 'f', 't', 'y', 'p', 'j', 'x', 'l', ' ', 0, 0, 0, 0, 'j', 'x', 'l', ' ' };
        out_put(&o, FTYP, 20);
    } else {
        out_put(&o, b, ftyp.at + ftyp.size);
    }
    if (s->exif.size > 0) {
        proven_u8str_view_t e = exif_upright(arena, s->exif);
        if (e.size == 0) return r;
        static const uint8_t NO_SKIP[4] = { 0, 0, 0, 0 };   /* the TIFF header right after */
        bmff_box(&o, "Exif", NO_SKIP, 4, e);
    }
    if (s->xmp.size > 0) bmff_box(&o, "xml ", NULL, 0, s->xmp);
    if (bare) bmff_box(&o, "jxlc", NULL, 0, file);
    else out_put(&o, b + ftyp.at + ftyp.size, file.size - ftyp.at - ftyp.size);
    proven_u8str_view_t made = out_done(&o);
    if (made.size == 0) return r;
    r.file = made;
    r.all_kept = !text_left;
    return r;
}

/* The chunk offsets of every track (an AVIF that moves): those at or past `from` moved by `delta`, in place. */
static void bmff_shift_chunks(uint8_t *b, size_t from_at, size_t end, size_t from, size_t delta) {
    bmff_box_t x;
    for (size_t at = from_at; bmff_at(b, end, at, &x); at += x.size) {
        size_t body = x.at + x.head;
        if (memcmp(x.type, "trak", 4) == 0 || memcmp(x.type, "mdia", 4) == 0 || memcmp(x.type, "minf", 4) == 0 ||
            memcmp(x.type, "stbl", 4) == 0 || memcmp(x.type, "moov", 4) == 0) {
            bmff_shift_chunks(b, body, x.at + x.size, from, delta);
        } else if ((memcmp(x.type, "stco", 4) == 0 || memcmp(x.type, "co64", 4) == 0) && x.size - x.head >= 8) {
            int n = x.type[0] == 's' ? 4 : 8;
            uint32_t count = be32(b + body + 4);
            if ((size_t)count > (x.size - x.head - 8) / (size_t)n) continue;
            for (uint32_t k = 0; k < count; ++k) {
                uint8_t *p = b + body + 8 + (size_t)k * (size_t)n;
                uint64_t v = be_n(p, n);
                if (v >= from) { v += delta; for (int i = n - 1; i >= 0; --i) { p[i] = (uint8_t)v; v >>= 8; } }
            }
        }
    }
}

/* `iloc` written again: the items as they were (moved by `delta` where they lay past `from`), and the new ones. */
static void iloc_write(out_buf_t *o, const iloc_box_t *l, size_t from, size_t delta,
                       const uint32_t *ids, const uint64_t *offsets, const uint64_t *lengths, size_t added) {
    int osz = l->offset_size < 4 ? 4 : l->offset_size, lsz = l->length_size < 4 ? 4 : l->length_size;
    out_buf_t body = { .arena = o->arena };
    out_byte(&body, (uint8_t)l->version); out_be(&body, 0, 3);
    out_byte(&body, (uint8_t)((osz << 4) | lsz));
    out_byte(&body, (uint8_t)((l->base_size << 4) | (l->version >= 1 ? l->index_size : 0)));
    size_t count = l->item_count + added;
    if (l->version < 2) out_be(&body, count, 2); else out_be(&body, count, 4);
    for (size_t i = 0; i < count; ++i) {
        bool old = i < l->item_count;
        const iloc_item_t *it = old ? &l->items[i] : NULL;
        uint32_t id = old ? it->id : ids[i - l->item_count];
        if (l->version < 2) out_be(&body, id, 2); else out_be(&body, id, 4);
        if (l->version >= 1) out_be(&body, old ? it->method : 0, 2);
        out_be(&body, old ? it->dref : 0, 2);
        out_be(&body, old ? it->base : 0, l->base_size);
        out_be(&body, old ? it->count : 1, 2);
        size_t n = old ? it->count : 1;
        for (size_t e = 0; e < n; ++e) {
            if (l->version >= 1 && l->index_size) out_be(&body, old ? l->extents[it->first + e].index : 0, l->index_size);
            uint64_t off, len;
            if (old) {
                const iloc_extent_t *x = &l->extents[it->first + e];
                off = x->offset;
                len = x->length;
                if (it->method == 0 && it->base + off >= from) off += delta;   /* in the file, past the meta box */
            } else {
                off = offsets[i - l->item_count];
                len = lengths[i - l->item_count];
            }
            out_be(&body, off, osz);
            out_be(&body, len, lsz);
        }
    }
    if (body.failed) { o->failed = true; return; }
    out_be(o, 8u + 4u + body.len - 4u, 4);
    out_str(o, "iloc");
    out_put(o, body.p, body.len);
}

/* An AVIF (HEIF): an Exif item and an XMP item ("mime", application/rdf+xml) in its meta box, said to describe
   the primary picture (iref cdsc), their bytes in an mdat at the end; what lay past the meta box moved. */
static rubraview_details_put_t put_avif(proven_arena_t *arena, proven_u8str_view_t file, const rubraview_details_src_t *s) {
    rubraview_details_put_t r = { .file = file, .all_kept = false };
    const uint8_t *b = (const uint8_t*)file.ptr;
    size_t size = file.size;
    bool text_left = s->comment_count > 0 || s->has_iptc || s->unread > 0 || (s->exif.size == 0 && s->words.size > 0);
    if (s->exif.size == 0 && s->xmp.size == 0) { r.all_kept = !text_left; return r; }
    if (!is_avif(b, size)) return r;
    bmff_box_t meta, iinf, iloc, pitm, iref = {0};
    if (!bmff_find(b, 0, size, "meta", &meta) || meta.head != 8) return r;
    size_t from = meta.at + meta.head + 4, end = meta.at + meta.size;
    iloc_box_t l;
    if (!bmff_find(b, from, end, "iinf", &iinf) || !bmff_find(b, from, end, "iloc", &iloc) ||
        !bmff_find(b, from, end, "pitm", &pitm) || !iloc_read(arena, b, &iloc, &l)) return r;
    bool has_iref = bmff_find(b, from, end, "iref", &iref);
    /* RV-156: every field read below inside its box: the meta box's version, pitm's item, iref's version. */
    size_t pitm_body = pitm.size - pitm.head;
    if (meta.size < 12 || pitm_body < 6 || (b[pitm.at + pitm.head] != 0 && pitm_body < 8)) return r;
    if (has_iref && iref.size - iref.head < 4) return r;
    if (has_iref && b[iref.at + iref.head] != 0) return r;   /* 32-bit references: not written here */
    uint32_t primary = b[pitm.at + pitm.head] == 0 ? be16(b + pitm.at + pitm.head + 4) : be32(b + pitm.at + pitm.head + 4);
    uint32_t next = 0;
    for (size_t i = 0; i < l.item_count; ++i) if (l.items[i].id > next) next = l.items[i].id;
    if (next + 2 > 0xFFFFu || l.version >= 2) return r;

    proven_u8str_view_t exif = s->exif.size > 0 ? exif_upright(arena, s->exif) : PROVEN_LIT("");
    uint32_t ids[2];
    uint64_t lengths[2], offsets[2] = { 0, 0 };
    size_t added = 0;
    if (exif.size > 0) { ids[added] = ++next; lengths[added++] = 4u + exif.size; }
    if (s->xmp.size > 0) { ids[added] = ++next; lengths[added++] = s->xmp.size; }
    if (added == 0) return r;

    /* twice: the first time for its size, the second with the offsets that size gives */
    proven_u8str_view_t made = PROVEN_LIT("");
    for (int pass = 0; pass < 2; ++pass) {
        out_buf_t m = { .arena = arena };
        out_be(&m, 0, 4); out_str(&m, "meta"); out_put(&m, b + meta.at + 8, 4);
        bmff_box_t c;
        for (size_t at = from; bmff_at(b, end, at, &c); at += c.size) {
            if (memcmp(c.type, "iinf", 4) == 0) {
                size_t p = c.at + c.head;
                if (c.size - c.head < 6 || (b[p] != 0 && c.size - c.head < 8)) return r;   /* RV-156 */
                int v = b[p];
                size_t count = v == 0 ? be16(b + p + 4) : be32(b + p + 4);
                size_t body_at = p + 4 + (v == 0 ? 2u : 4u);
                out_buf_t ib = { .arena = arena };
                out_put(&ib, b + p, 4);
                out_be(&ib, count + added, v == 0 ? 2 : 4);
                out_put(&ib, b + body_at, c.at + c.size - body_at);
                for (size_t k = 0; k < added; ++k) {
                    bool is_exif = exif.size > 0 && k == 0;
                    const char *ct = "application/rdf+xml";
                    size_t infe_len = 8u + 4u + 2u + 2u + 4u + 1u + (is_exif ? 0u : strlen(ct) + 1u);
                    out_be(&ib, infe_len, 4); out_str(&ib, "infe");
                    out_byte(&ib, 2); out_be(&ib, 0, 3);
                    out_be(&ib, ids[k], 2); out_be(&ib, 0, 2);
                    out_str(&ib, is_exif ? "Exif" : "mime");
                    out_byte(&ib, 0);   /* no name */
                    if (!is_exif) { out_str(&ib, ct); out_byte(&ib, 0); }
                }
                if (ib.failed) return r;
                out_be(&m, 8u + ib.len, 4); out_str(&m, "iinf"); out_put(&m, ib.p, ib.len);
                if (!has_iref) {   /* the references, right after */
                    out_be(&m, 12u + 14u * added, 4); out_str(&m, "iref"); out_be(&m, 0, 4);
                    for (size_t k = 0; k < added; ++k) { out_be(&m, 14, 4); out_str(&m, "cdsc"); out_be(&m, ids[k], 2); out_be(&m, 1, 2); out_be(&m, primary, 2); }
                }
            } else if (memcmp(c.type, "iref", 4) == 0) {
                out_be(&m, c.size + 14u * added, 4);
                out_put(&m, b + c.at + 4, c.size - 4);
                for (size_t k = 0; k < added; ++k) { out_be(&m, 14, 4); out_str(&m, "cdsc"); out_be(&m, ids[k], 2); out_be(&m, 1, 2); out_be(&m, primary, 2); }
            } else if (memcmp(c.type, "iloc", 4) == 0) {
                /* the second time, how much the meta box grew (its size the first time gave) */
                iloc_write(&m, &l, end, pass == 0 ? 0 : (size_t)(made.size - meta.size), ids, offsets, lengths, added);
            } else {
                out_put(&m, b + c.at, c.size);
            }
        }
        if (m.failed || m.len < 8) return r;
        put_be32(m.p, (uint32_t)m.len);
        made = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)m.p, .size = m.len };
        if (pass == 0) {   /* the new items' bytes: an mdat at the very end */
            size_t mdat_at = size - meta.size + made.size;
            uint64_t at = mdat_at + 8;
            for (size_t k = 0; k < added; ++k) { offsets[k] = at; at += lengths[k]; }
        }
    }
    size_t delta = made.size - meta.size;
    out_buf_t o = { .arena = arena };
    out_put(&o, b, meta.at);
    out_u8(&o, made);
    size_t rest_at = o.len;
    out_put(&o, b + end, size - end);
    if (o.failed) return r;
    bmff_shift_chunks(o.p, rest_at, o.len, end, delta);   /* an AVIF that moves: its tracks' chunks */
    uint64_t mdat_len = 8;
    for (size_t k = 0; k < added; ++k) mdat_len += lengths[k];
    out_be(&o, mdat_len, 4); out_str(&o, "mdat");
    if (exif.size > 0) { out_be(&o, 0, 4); out_u8(&o, exif); }
    if (s->xmp.size > 0) out_u8(&o, s->xmp);
    proven_u8str_view_t done = out_done(&o);
    if (done.size == 0) return r;
    r.file = done;
    r.all_kept = !text_left;
    return r;
}

rubraview_details_put_t rubraview_details_put(proven_arena_t *arena, rubraview_export_format_t format,
                                              proven_u8str_view_t file, const rubraview_details_src_t *src) {
    rubraview_details_put_t none = { .file = file, .all_kept = !src || !src->any };
    if (!arena || !src || !src->any || file.size == 0) return none;
    switch (format) {
        case RUBRAVIEW_EXPORT_JPEG: return put_jpeg(arena, file, src);
        case RUBRAVIEW_EXPORT_PNG:  return put_png(arena, file, src);
        case RUBRAVIEW_EXPORT_GIF:  return put_gif(arena, file, src);
        case RUBRAVIEW_EXPORT_WEBP: return put_webp(arena, file, src);
        case RUBRAVIEW_EXPORT_JXL:  return put_jxl(arena, file, src);    /* 1.31.0 */
        case RUBRAVIEW_EXPORT_AVIF: return put_avif(arena, file, src);
        default: return none;   /* BMP, TIFF, ICO: no place this writer can fill */
    }
}

/* ---- the .txt: a subset of TOML (1.18.0) ---- */

static void toml_string(out_buf_t *o, proven_u8str_view_t s) {
    out_byte(o, '"');
    for (size_t i = 0; i < s.size; ++i) {
        uint8_t c = (uint8_t)s.ptr[i];
        if (c == '"') out_str(o, "\\\"");
        else if (c == '\\') out_str(o, "\\\\");
        else if (c == '\n') out_str(o, "\\n");
        else if (c == '\r') out_str(o, "\\r");
        else if (c == '\t') out_str(o, "\\t");
        else if (c < 0x20 || c == 0x7F) { char u[8]; snprintf(u, sizeof(u), "\\u%04X", (unsigned)c); out_str(o, u); }
        else out_byte(o, c);
    }
    out_byte(o, '"');
}

static bool same_text(proven_u8str_view_t a, proven_u8str_view_t b) { return a.size == b.size && memcmp(a.ptr, b.ptr, a.size) == 0; }

proven_u8str_view_t rubraview_details_txt(proven_arena_t *arena, const rubraview_details_src_t *src, proven_u8str_view_t source_name, proven_u8str_view_t file_name) {
    if (!arena || !src || !src->any) return PROVEN_LIT("");
    out_buf_t o = { .arena = arena };
    out_str(&o, "# Rubraview " RUBRAVIEW_VERSION_STRING " details\r\n# ");
    out_str(&o, T("What this picture said that the file named here cannot hold, kept by Rubraview, which reads it back."));
    out_str(&o, "\r\nfile = ");
    toml_string(&o, file_name);
    out_str(&o, "\r\nsource = ");
    toml_string(&o, source_name);
    out_str(&o, "\r\n");

    /* the information lines: a table a heading, a key a label, an array when a label comes again;
       the comments' own table is written from the comments themselves */
    const rubraview_info_t *info = &src->info;
    for (size_t i = 0; i < info->count;) {
        proven_u8str_view_t table = info->lines[i].heading ? info->lines[i].label : PROVEN_LIT("Details");
        size_t first = info->lines[i].heading ? i + 1 : i, end = first;
        while (end < info->count && !info->lines[end].heading) end++;
        bool comments = false;
        for (size_t k = first; k < end && !comments; ++k)
            for (size_t c = 0; c < src->comment_count; ++c) if (same_text(info->lines[k].value, src->comments[c])) { comments = true; break; }
        if (!comments && end > first) {
            out_str(&o, "\r\n[");
            toml_string(&o, table);
            out_str(&o, "]\r\n");
            for (size_t k = first; k < end; ++k) {
                bool seen = false;
                for (size_t p = first; p < k && !seen; ++p) seen = same_text(info->lines[p].label, info->lines[k].label);
                if (seen) continue;
                size_t count = 0;
                for (size_t q = k; q < end; ++q) count += same_text(info->lines[q].label, info->lines[k].label);
                toml_string(&o, info->lines[k].label);
                out_str(&o, " = ");
                if (count > 1) out_byte(&o, '[');
                bool more = false;
                for (size_t q = k; q < end; ++q) {
                    if (!same_text(info->lines[q].label, info->lines[k].label)) continue;
                    if (more) out_str(&o, ", ");
                    toml_string(&o, info->lines[q].value);
                    more = true;
                }
                if (count > 1) out_byte(&o, ']');
                out_str(&o, "\r\n");
            }
        }
        i = end > i ? end : i + 1;
    }
    if (src->comment_count > 0) {
        out_str(&o, "\r\n[comments]\r\nitems = [");
        for (size_t c = 0; c < src->comment_count; ++c) {
            if (c) out_str(&o, ", ");
            toml_string(&o, src->comments[c]);
        }
        out_str(&o, "]\r\n");
    }
    if (src->xmp.size > 0) {
        out_str(&o, "\r\n[xmp]\r\npacket = ");
        toml_string(&o, src->xmp);
        out_str(&o, "\r\n");
    }
    return out_done(&o);
}

/* A basic string at `*at` (on its quote), unescaped; false when it is not one. */
static bool toml_read_string(proven_arena_t *arena, proven_u8str_view_t line, size_t *at, proven_u8str_view_t *out) {
    size_t i = *at;
    if (i >= line.size || line.ptr[i] != '"') return false;
    out_buf_t o = { .arena = arena };
    for (++i; i < line.size; ++i) {
        char c = line.ptr[i];
        if (c == '"') {
            *at = i + 1;
            if (o.len == 0) { *out = PROVEN_LIT(""); return true; }
            *out = out_done(&o);
            return !o.failed;
        }
        if ((uint8_t)c < 0x20 && c != '\t') return false;
        if (c != '\\') { out_byte(&o, (uint8_t)c); continue; }
        if (++i >= line.size) return false;
        char e = line.ptr[i];
        if (e == '"' || e == '\\') out_byte(&o, (uint8_t)e);
        else if (e == 'n') out_byte(&o, '\n');
        else if (e == 'r') out_byte(&o, '\r');
        else if (e == 't') out_byte(&o, '\t');
        else if (e == 'u' && i + 4 < line.size) {
            uint32_t cp = 0;
            for (int k = 1; k <= 4; ++k) {
                char h = line.ptr[i + k];
                uint32_t d = h >= '0' && h <= '9' ? (uint32_t)(h - '0') : h >= 'a' && h <= 'f' ? (uint32_t)(h - 'a' + 10)
                           : h >= 'A' && h <= 'F' ? (uint32_t)(h - 'A' + 10) : 99u;
                if (d > 15) return false;
                cp = cp * 16u + d;
            }
            i += 4;
            if (cp == 0 || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
            if (cp < 0x80) out_byte(&o, (uint8_t)cp);
            else if (cp < 0x800) { out_byte(&o, (uint8_t)(0xC0 | (cp >> 6))); out_byte(&o, (uint8_t)(0x80 | (cp & 0x3F))); }
            else { out_byte(&o, (uint8_t)(0xE0 | (cp >> 12))); out_byte(&o, (uint8_t)(0x80 | ((cp >> 6) & 0x3F))); out_byte(&o, (uint8_t)(0x80 | (cp & 0x3F))); }
        } else {
            return false;
        }
    }
    return false;   /* no closing quote on the line */
}

static void skip_blanks(proven_u8str_view_t line, size_t *at) {
    while (*at < line.size && (line.ptr[*at] == ' ' || line.ptr[*at] == '\t')) (*at)++;
}

/* A key: bare (A-Z a-z 0-9 _ -) or quoted; NUL-terminated either way. */
static bool toml_read_key(proven_arena_t *arena, proven_u8str_view_t line, size_t *at, proven_u8str_view_t *out) {
    if (*at < line.size && line.ptr[*at] == '"') return toml_read_string(arena, line, at, out) && out->size > 0;
    size_t start = *at;
    while (*at < line.size) {
        char c = line.ptr[*at];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) break;
        (*at)++;
    }
    if (*at == start) return false;
    out_buf_t o = { .arena = arena };
    out_put(&o, line.ptr + start, *at - start);
    *out = out_done(&o);
    return !o.failed;
}

static bool only_comment_left(proven_u8str_view_t line, size_t at) {
    skip_blanks(line, &at);
    return at >= line.size || line.ptr[at] == '#';
}

static bool same_name(proven_u8str_view_t a, proven_u8str_view_t b) {
    if (a.size != b.size) return false;
    for (size_t i = 0; i < a.size; ++i) {
        char x = a.ptr[i], y = b.ptr[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

bool rubraview_details_from_txt(proven_arena_t *arena, proven_u8str_view_t text, proven_u8str_view_t picture_name, rubraview_details_src_t *out) {
    if (!out) return false;
    *out = (rubraview_details_src_t){0};
    if (!arena || text.size == 0) return false;
    if (text.size >= 3 && memcmp(text.ptr, "\xEF\xBB\xBF", 3) == 0) { text.ptr += 3; text.size -= 3; }
    static const char ID[] = "# Rubraview ";
    if (text.size < sizeof(ID) - 1 || memcmp(text.ptr, ID, sizeof(ID) - 1) != 0) return false;

    rubraview_details_src_t d = {0};
    proven_u8str_view_t table = PROVEN_LIT(""), file = PROVEN_LIT("");
    bool first = true, heading_put = false;
    for (size_t at = 0; at < text.size;) {
        size_t end = at;
        while (end < text.size && text.ptr[end] != '\n') end++;
        proven_u8str_view_t line = { .ptr = text.ptr + at, .size = end - at };
        if (line.size > 0 && line.ptr[line.size - 1] == '\r') line.size--;
        at = end + 1;
        if (first) {   /* "# Rubraview <version> details" */
            first = false;
            if (line.size < 8 || memcmp(line.ptr + line.size - 8, " details", 8) != 0) return false;
            continue;
        }
        size_t i = 0;
        skip_blanks(line, &i);
        if (i >= line.size || line.ptr[i] == '#') continue;
        if (line.ptr[i] == '[') {
            ++i;
            skip_blanks(line, &i);
            if (!toml_read_key(arena, line, &i, &table)) return false;
            skip_blanks(line, &i);
            if (i >= line.size || line.ptr[i] != ']' || !only_comment_left(line, i + 1)) return false;
            heading_put = false;
            continue;
        }
        proven_u8str_view_t key;
        if (!toml_read_key(arena, line, &i, &key)) return false;
        skip_blanks(line, &i);
        if (i >= line.size || line.ptr[i] != '=') return false;
        ++i;
        skip_blanks(line, &i);
        proven_u8str_view_t values[64];
        size_t count = 0;
        if (i < line.size && line.ptr[i] == '[') {
            ++i;
            for (;;) {
                skip_blanks(line, &i);
                if (i < line.size && line.ptr[i] == ']') { ++i; break; }
                proven_u8str_view_t v;
                if (!toml_read_string(arena, line, &i, &v)) return false;
                if (count < 64) values[count++] = v;
                skip_blanks(line, &i);
                if (i < line.size && line.ptr[i] == ',') { ++i; continue; }
                if (i < line.size && line.ptr[i] == ']') { ++i; break; }
                return false;
            }
        } else {
            if (!toml_read_string(arena, line, &i, &values[0])) return false;
            count = 1;
        }
        if (!only_comment_left(line, i)) return false;

        if (table.size == 0) {
            if (proven_u8str_view_eq(key, PROVEN_LIT("file")) && count == 1) file = values[0];
            continue;   /* "source", and anything else at the top: not shown */
        }
        if (proven_u8str_view_eq(table, PROVEN_LIT("comments"))) {
            if (proven_u8str_view_eq(key, PROVEN_LIT("items")))
                for (size_t k = 0; k < count && d.comment_count < RUBRAVIEW_DETAILS_MAX_COMMENTS; ++k)
                    if (values[k].size > 0) d.comments[d.comment_count++] = values[k];
            continue;
        }
        if (proven_u8str_view_eq(table, PROVEN_LIT("xmp"))) {
            if (proven_u8str_view_eq(key, PROVEN_LIT("packet")) && count == 1) d.xmp = values[0];
            continue;
        }
        if (!heading_put) { rubraview_info_heading(arena, &d.info, (const char *)table.ptr); heading_put = true; }
        for (size_t k = 0; k < count; ++k) rubraview_info_add(arena, &d.info, (const char *)key.ptr, values[k]);
    }
    if (file.size == 0 || !same_name(file, rubraview_path_basename(picture_name))) return false;
    if (d.comment_count > 0) rubraview_info_heading(arena, &d.info, T("Comments"));
    for (size_t c = 0; c < d.comment_count; ++c) rubraview_info_add(arena, &d.info, T("Comment"), d.comments[c]);
    build_words(arena, &d);
    d.beside = true;
    d.any = d.info.count > 0 || d.xmp.size > 0;
    *out = d;
    return d.any;
}

proven_u8str_view_t rubraview_details_txt_candidate(proven_arena_t *arena, proven_u8str_view_t picture_path, unsigned k) {
    proven_u8str_view_t empty = PROVEN_LIT("");
    if (!arena || picture_path.size == 0) return empty;
    proven_u8str_view_t ext = rubraview_path_ext(picture_path);
    size_t base = picture_path.size - ext.size;
    char tail[16];
    int t = k == 0 ? snprintf(tail, sizeof(tail), ".txt") : snprintf(tail, sizeof(tail), "-%u.txt", k);
    if (t <= 0) return empty;
    proven_result_mem_mut_t res = proven_arena_alloc(arena, base + (size_t)t + 1);
    if (!proven_is_ok(res.err)) return empty;
    char *p = (char*)res.value.ptr;
    memcpy(p, picture_path.ptr, base);
    memcpy(p + base, tail, (size_t)t + 1);
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)p, .size = base + (size_t)t };
}

void rubraview_details_merge(proven_arena_t *arena, rubraview_details_src_t *into, const rubraview_details_src_t *extra) {
    if (!arena || !into || !extra || !extra->any) return;
    /* the tables the picture's own reading has not, under the headings they had */
    for (size_t i = 0; i < extra->info.count;) {
        size_t end = i + 1;
        while (end < extra->info.count && !extra->info.lines[end].heading) end++;
        bool have = false;
        if (extra->info.lines[i].heading)
            for (size_t k = 0; k < into->info.count && !have; ++k)
                have = into->info.lines[k].heading && same_text(into->info.lines[k].label, extra->info.lines[i].label);
        if (!have) {
            for (size_t k = i; k < end; ++k) {
                const rubraview_info_line_t *l = &extra->info.lines[k];
                if (l->heading) rubraview_info_heading(arena, &into->info, (const char *)l->label.ptr);
                else rubraview_info_add(arena, &into->info, (const char *)l->label.ptr, l->value);
            }
        }
        i = end;
    }
    for (size_t c = 0; c < extra->comment_count && into->comment_count < RUBRAVIEW_DETAILS_MAX_COMMENTS; ++c) {
        bool dup = false;
        for (size_t k = 0; k < into->comment_count && !dup; ++k) dup = same_text(into->comments[k], extra->comments[c]);
        if (!dup) into->comments[into->comment_count++] = extra->comments[c];
    }
    if (into->xmp.size == 0) into->xmp = extra->xmp;
    build_words(arena, into);
    into->any = true;
}

proven_u8str_view_t rubraview_details_txt_path(proven_arena_t *arena, proven_u8str_view_t written,
                                   bool (*exists)(proven_u8str_view_t path, void *ctx), void *ctx) {
    proven_u8str_view_t empty = PROVEN_LIT("");
    if (!arena || written.size == 0 || written.size > 4000) return empty;
    proven_u8str_view_t ext = rubraview_path_ext(written);
    size_t base = written.size - ext.size;
    for (unsigned k = 0; k < 10000; ++k) {
        char tail[16];
        int t = k == 0 ? snprintf(tail, sizeof(tail), ".txt") : snprintf(tail, sizeof(tail), "-%u.txt", k);
        if (t <= 0) return empty;
        proven_result_mem_mut_t res = proven_arena_alloc(arena, base + (size_t)t + 1);
        if (!proven_is_ok(res.err)) return empty;
        char *p = (char*)res.value.ptr;
        memcpy(p, written.ptr, base);
        memcpy(p + base, tail, (size_t)t + 1);
        proven_u8str_view_t path = { .ptr = (const proven_byte_t *)p, .size = base + (size_t)t };
        if (!exists || !exists(path, ctx)) return path;
    }
    return empty;
}
