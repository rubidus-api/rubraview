#include "rubraview/keepinfo.h"
#include "rubraview/exif.h"
#include "rubraview/fileinfo.h"
#include "rubraview/lang.h"
#include "rubraview/path.h"

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
static void out_u8(out_buf_t *o, u8str_t s) { out_put(o, s.ptr, s.len); }

static u8str_t out_done(out_buf_t *o) {
    if (o->failed) return (u8str_t){ .ptr = "", .len = 0 };
    out_byte(o, 0);   /* a NUL after it, not counted */
    if (o->failed) return (u8str_t){ .ptr = "", .len = 0 };
    return (u8str_t){ .ptr = (const char*)o->p, .len = o->len - 1 };
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
static u8str_t as_text(proven_arena_t *arena, const uint8_t *s, size_t n) {
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
    u8str_t t = as_text(arena, text, n);
    if (t.len == 0) return;
    if (key && key_len > 0) {
        out_buf_t o = { .arena = arena };
        out_put(&o, key, key_len);
        out_str(&o, ": ");
        out_u8(&o, t);
        t = out_done(&o);
        if (t.len == 0) return;
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
        if (m == 0xE1 && n > 6 && memcmp(p, "Exif\0\0", 6) == 0 && d->exif.len == 0 && is_tiff(p + 6, n - 6)) {
            d->exif = (u8str_t){ .ptr = (const char*)p + 6, .len = n - 6 };
        } else if (m == 0xE1 && n > sizeof(XMP_NS) && memcmp(p, XMP_NS, sizeof(XMP_NS)) == 0 && d->xmp.len == 0) {
            d->xmp = (u8str_t){ .ptr = (const char*)p + sizeof(XMP_NS), .len = n - sizeof(XMP_NS) };
        } else if (m == 0xED && n > 14 && memcmp(p, "Photoshop 3.0\0", 14) == 0 && d->iptc_app13.len == 0) {
            d->iptc_app13 = (u8str_t){ .ptr = (const char*)p, .len = n };
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
        if (memcmp(type, "eXIf", 4) == 0 && d->exif.len == 0 && is_tiff(p, len)) {
            d->exif = (u8str_t){ .ptr = (const char*)p, .len = len };
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
                    if (xmp) { if (d->xmp.len == 0) d->xmp = (u8str_t){ .ptr = (const char*)p + q, .len = len - q }; }
                    else add_comment(arena, d, (const char*)p, k, p + q, len - q);
                } else {
                    d->unread++;
                }
                if ((!xmp || compressed) && d->png_chunk_count < RUBRAVIEW_DETAILS_MAX_CHUNKS)
                    d->png_chunks[d->png_chunk_count++] = (u8str_t){ .ptr = (const char*)b + at, .len = (size_t)len + 12u };
            } else if (keyed) {
                if (type[0] == 't') add_comment(arena, d, (const char*)p, k, p + k + 1, len - k - 1);
                else d->unread++;
                if (d->png_chunk_count < RUBRAVIEW_DETAILS_MAX_CHUNKS)
                    d->png_chunks[d->png_chunk_count++] = (u8str_t){ .ptr = (const char*)b + at, .len = (size_t)len + 12u };
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
                u8str_t t = out_done(&o);
                if (t.len > 0) add_comment(arena, d, NULL, 0, (const uint8_t*)t.ptr, t.len);
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
        if (memcmp(b + at, "EXIF", 4) == 0 && d->exif.len == 0) {
            const uint8_t *e = p;
            size_t n = len;
            if (n > 6 && memcmp(e, "Exif\0\0", 6) == 0) { e += 6; n -= 6; }
            if (is_tiff(e, n)) d->exif = (u8str_t){ .ptr = (const char*)e, .len = n };
        } else if (memcmp(b + at, "XMP ", 4) == 0 && d->xmp.len == 0) {
            d->xmp = (u8str_t){ .ptr = (const char*)p, .len = len };
        }
        at += 8u + len + (len & 1u);
    }
}

bool rubraview_details_read(proven_arena_t *arena, const uint8_t *b, size_t size, rubraview_details_src_t *out) {
    if (!out) return false;
    *out = (rubraview_details_src_t){0};
    if (!arena || !b || size < 12) return false;

    if (b[0] == 0xFF && b[1] == 0xD8) read_jpeg(arena, b, size, out);
    else if (size >= 8 && memcmp(b, "\x89PNG\r\n\x1a\n", 8) == 0) read_png(arena, b, size, out);
    else if (memcmp(b, "GIF8", 4) == 0) read_gif(arena, b, size, out);
    else if (memcmp(b, "RIFF", 4) == 0 && memcmp(b + 8, "WEBP", 4) == 0) read_webp(b, size, out);

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

    out_buf_t o = { .arena = arena };
    u8str_t rows[512];
    size_t n = rubraview_info_rows(arena, &info, 24, rows, sizeof(rows) / sizeof(rows[0]));
    for (size_t i = 0; i < n; ++i) { out_u8(&o, rows[i]); out_str(&o, "\r\n"); }
    out->words = n > 0 ? out_done(&o) : (u8str_t){ .ptr = "", .len = 0 };

    out->any = exif_found || out->exif.len > 0 || out->xmp.len > 0 || out->iptc_app13.len > 0 || out->has_iptc ||
               out->comment_count > 0 || out->png_chunk_count > 0 || out->unread > 0;
    return out->any;
}

/* ---- putting ---- */

/* The EXIF block copied, saying orientation 1 (normal): the pixels were turned upright. */
static u8str_t exif_upright(proven_arena_t *arena, u8str_t exif) {
    proven_result_mem_mut_t res = proven_arena_alloc(arena, exif.len);
    if (!proven_is_ok(res.err)) return (u8str_t){ .ptr = "", .len = 0 };
    uint8_t *t = (uint8_t*)res.value.ptr;
    memcpy(t, exif.ptr, exif.len);
    size_t n = exif.len;
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
    return (u8str_t){ .ptr = (const char*)t, .len = n };
}

/* Words for what has no block of its own in this file: IPTC read but not copied, an EXIF only read, an XMP left out. */
static bool words_needed(const rubraview_details_src_t *s, bool exif_in, bool iptc_in, bool xmp_in) {
    bool exif_words = s->words.len > 0 && !exif_in;
    return (s->has_iptc && !iptc_in) || exif_words || (s->xmp.len > 0 && !xmp_in) || s->unread > 0;
}

static void jpeg_segment(out_buf_t *o, uint8_t marker, const char *head, size_t head_len, u8str_t body) {
    out_byte(o, 0xFF);
    out_byte(o, marker);
    size_t len = 2 + head_len + body.len;
    out_byte(o, (uint8_t)(len >> 8));
    out_byte(o, (uint8_t)len);
    out_put(o, head, head_len);
    out_u8(o, body);
}

/* A comment as COM segments of at most 65533 bytes, cut between characters. */
static void jpeg_comment(out_buf_t *o, u8str_t text) {
    size_t at = 0;
    while (at < text.len) {
        size_t n = text.len - at;
        if (n > 65533) {
            n = 65533;
            while (n > 0 && ((uint8_t)text.ptr[at + n] & 0xC0) == 0x80) n--;
        }
        jpeg_segment(o, 0xFE, NULL, 0, (u8str_t){ .ptr = text.ptr + at, .len = n });
        at += n;
    }
}

static u8str_t joined_words(proven_arena_t *arena, const rubraview_details_src_t *s, bool with_comments, bool with_xmp) {
    out_buf_t o = { .arena = arena };
    out_str(&o, T("Details kept by Rubraview"));
    out_str(&o, "\r\n");
    out_u8(&o, s->words);   /* the comments are among the words */
    (void)with_comments;
    if (with_xmp && s->xmp.len > 0) { out_str(&o, "XMP\r\n"); out_u8(&o, s->xmp); out_str(&o, "\r\n"); }
    return out_done(&o);
}

static rubraview_details_put_t put_jpeg(proven_arena_t *arena, u8str_t file, const rubraview_details_src_t *s) {
    rubraview_details_put_t r = { .file = file, .all_kept = false };
    const uint8_t *b = (const uint8_t*)file.ptr;
    if (file.len < 4 || b[0] != 0xFF || b[1] != 0xD8) return r;
    size_t head = 2;
    if (file.len >= 6 && b[2] == 0xFF && b[3] == 0xE0) {   /* after JFIF's APP0, which must come first */
        size_t len = be16(b + 4);
        if (len >= 2 && 4 + len <= file.len) head = 4 + len;
    }
    out_buf_t o = { .arena = arena };
    out_put(&o, b, head);
    bool exif_in = false, xmp_in = false, iptc_in = false;
    if (s->exif.len > 0 && s->exif.len + 6 + 2 <= 65535) {
        u8str_t e = exif_upright(arena, s->exif);
        if (e.len > 0) { jpeg_segment(&o, 0xE1, "Exif\0\0", 6, e); exif_in = true; }
    }
    if (s->xmp.len > 0 && s->xmp.len + sizeof(XMP_NS) + 2 <= 65535) { jpeg_segment(&o, 0xE1, XMP_NS, sizeof(XMP_NS), s->xmp); xmp_in = true; }
    if (s->iptc_app13.len > 0 && s->iptc_app13.len + 2 <= 65535) { jpeg_segment(&o, 0xED, NULL, 0, s->iptc_app13); iptc_in = true; }
    if (words_needed(s, exif_in, iptc_in, xmp_in)) {
        jpeg_comment(&o, joined_words(arena, s, true, !xmp_in));
    } else {
        for (size_t i = 0; i < s->comment_count; ++i) jpeg_comment(&o, s->comments[i]);
    }
    out_put(&o, b + head, file.len - head);
    u8str_t made = out_done(&o);
    if (made.len == 0) return r;
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
static void png_itxt(proven_arena_t *arena, out_buf_t *o, const char *key, u8str_t text) {
    out_buf_t d = { .arena = arena };
    out_str(&d, key);
    out_byte(&d, 0); out_byte(&d, 0); out_byte(&d, 0);   /* NUL, uncompressed, method */
    out_byte(&d, 0); out_byte(&d, 0);                    /* no language, no translated keyword */
    out_u8(&d, text);
    if (d.failed) { o->failed = true; return; }
    png_chunk(o, "iTXt", d.p, d.len);
}

static rubraview_details_put_t put_png(proven_arena_t *arena, u8str_t file, const rubraview_details_src_t *s) {
    rubraview_details_put_t r = { .file = file, .all_kept = false };
    const uint8_t *b = (const uint8_t*)file.ptr;
    if (file.len < 33 || memcmp(b, "\x89PNG\r\n\x1a\n", 8) != 0 || memcmp(b + 12, "IHDR", 4) != 0) return r;
    size_t head = 8 + 12 + be32(b + 8);
    if (head > file.len) return r;
    out_buf_t o = { .arena = arena };
    out_put(&o, b, head);
    bool exif_in = false;
    if (s->exif.len > 0 && s->exif.len <= 0x7FFFFFFFu) {
        u8str_t e = exif_upright(arena, s->exif);
        if (e.len > 0) { png_chunk(&o, "eXIf", (const uint8_t*)e.ptr, e.len); exif_in = true; }
    }
    if (s->xmp.len > 0) png_itxt(arena, &o, "XML:com.adobe.xmp", s->xmp);
    for (size_t i = 0; i < s->png_chunk_count; ++i) out_u8(&o, s->png_chunks[i]);   /* a PNG's own texts, as they were */
    bool words = words_needed(s, exif_in, false, true);
    if (words) {
        png_itxt(arena, &o, "Comment", joined_words(arena, s, s->png_chunk_count == 0, false));
    } else if (s->png_chunk_count == 0) {
        for (size_t i = 0; i < s->comment_count; ++i) png_itxt(arena, &o, "Comment", s->comments[i]);
    }
    out_put(&o, b + head, file.len - head);
    u8str_t made = out_done(&o);
    if (made.len == 0) return r;
    r.file = made;
    r.all_kept = true;
    return r;
}

static rubraview_details_put_t put_gif(proven_arena_t *arena, u8str_t file, const rubraview_details_src_t *s) {
    rubraview_details_put_t r = { .file = file, .all_kept = false };
    const uint8_t *b = (const uint8_t*)file.ptr;
    if (file.len < 14 || memcmp(b, "GIF8", 4) != 0 || b[file.len - 1] != 0x3B) return r;
    u8str_t text = (s->words.len > 0 || s->xmp.len > 0) ? joined_words(arena, s, true, true) : (u8str_t){ .ptr = "", .len = 0 };
    if (text.len == 0) { r.all_kept = true; return r; }
    out_buf_t o = { .arena = arena };
    out_put(&o, b, file.len - 1);
    out_byte(&o, 0x21);
    out_byte(&o, 0xFE);
    for (size_t at = 0; at < text.len;) {
        size_t n = text.len - at > 255 ? 255 : text.len - at;
        out_byte(&o, (uint8_t)n);
        out_put(&o, text.ptr + at, n);
        at += n;
    }
    out_byte(&o, 0);
    out_byte(&o, 0x3B);
    u8str_t made = out_done(&o);
    if (made.len == 0) return r;
    r.file = made;
    r.all_kept = true;
    return r;
}

static void riff_chunk(out_buf_t *o, const char *fourcc, u8str_t data) {
    uint8_t len[4];
    put_le32(len, (uint32_t)data.len);
    out_put(o, fourcc, 4);
    out_put(o, len, 4);
    out_u8(o, data);
    if (data.len & 1u) out_byte(o, 0);
}

static rubraview_details_put_t put_webp(proven_arena_t *arena, u8str_t file, const rubraview_details_src_t *s) {
    rubraview_details_put_t r = { .file = file, .all_kept = false };
    const uint8_t *b = (const uint8_t*)file.ptr;
    if (file.len < 20 || memcmp(b, "RIFF", 4) != 0 || memcmp(b + 8, "WEBP", 4) != 0) return r;
    bool text_left = s->comment_count > 0 || s->has_iptc || s->unread > 0 || (s->exif.len == 0 && s->words.len > 0);
    if (s->exif.len == 0 && s->xmp.len == 0) { r.all_kept = !text_left; return r; }

    const uint8_t *first = b + 12;
    uint32_t first_len = le32(first + 4);
    if ((size_t)first_len > file.len - 20) return r;
    uint8_t flags = 0;
    uint32_t w = 0, h = 0;
    bool extended = memcmp(first, "VP8X", 4) == 0;
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
    bool exif_in = s->exif.len > 0 && s->exif.len < 0xFFFFFFF0u;
    bool xmp_in = s->xmp.len > 0 && s->xmp.len < 0xFFFFFFF0u;
    if (exif_in) flags |= 0x08;
    if (xmp_in) flags |= 0x04;

    out_buf_t o = { .arena = arena };
    out_put(&o, b, 12);
    if (extended) {
        out_put(&o, first, 8);
        out_byte(&o, (uint8_t)(first[8] | flags));
        out_put(&o, first + 9, (size_t)first_len - 1u + (first_len & 1u));
        size_t rest = 12u + 8u + first_len + (first_len & 1u);
        if (rest <= file.len) out_put(&o, b + rest, file.len - rest);
    } else {
        uint8_t x[10] = { flags, 0, 0, 0,
                          (uint8_t)(w - 1u), (uint8_t)((w - 1u) >> 8), (uint8_t)((w - 1u) >> 16),
                          (uint8_t)(h - 1u), (uint8_t)((h - 1u) >> 8), (uint8_t)((h - 1u) >> 16) };
        riff_chunk(&o, "VP8X", (u8str_t){ .ptr = (const char*)x, .len = 10 });
        out_put(&o, b + 12, file.len - 12);
    }
    if (exif_in) {
        u8str_t e = exif_upright(arena, s->exif);
        if (e.len == 0) return r;
        riff_chunk(&o, "EXIF", e);
    }
    if (xmp_in) riff_chunk(&o, "XMP ", s->xmp);
    if (o.failed || o.len < 12 || o.len - 8 > 0xFFFFFFFFu) return r;
    put_le32(o.p + 4, (uint32_t)(o.len - 8));
    u8str_t made = out_done(&o);
    if (made.len == 0) return r;
    r.file = made;
    r.all_kept = !text_left;
    return r;
}

rubraview_details_put_t rubraview_details_put(proven_arena_t *arena, rubraview_export_format_t format,
                                              u8str_t file, const rubraview_details_src_t *src) {
    rubraview_details_put_t none = { .file = file, .all_kept = !src || !src->any };
    if (!arena || !src || !src->any || file.len == 0) return none;
    switch (format) {
        case RUBRAVIEW_EXPORT_JPEG: return put_jpeg(arena, file, src);
        case RUBRAVIEW_EXPORT_PNG:  return put_png(arena, file, src);
        case RUBRAVIEW_EXPORT_GIF:  return put_gif(arena, file, src);
        case RUBRAVIEW_EXPORT_WEBP: return put_webp(arena, file, src);
        default: return none;   /* BMP, TIFF, ICO: no place this writer can fill */
    }
}

u8str_t rubraview_details_txt(proven_arena_t *arena, const rubraview_details_src_t *src, u8str_t source_name) {
    if (!arena || !src || !src->any) return (u8str_t){ .ptr = "", .len = 0 };
    out_buf_t o = { .arena = arena };
    char head[600];
    int n = snprintf(head, sizeof(head), T("Details of %.*s, kept by Rubraview"), (int)(source_name.len < 400 ? source_name.len : 400), source_name.ptr);
    if (n > 0) out_put(&o, head, (size_t)n < sizeof(head) ? (size_t)n : sizeof(head) - 1);
    out_str(&o, "\r\n\r\n");
    out_u8(&o, src->words);
    if (src->xmp.len > 0) { out_str(&o, "\r\nXMP\r\n"); out_u8(&o, src->xmp); out_str(&o, "\r\n"); }
    return out_done(&o);
}

u8str_t rubraview_details_txt_path(proven_arena_t *arena, u8str_t written,
                                   bool (*exists)(u8str_t path, void *ctx), void *ctx) {
    u8str_t empty = { .ptr = "", .len = 0 };
    if (!arena || written.len == 0 || written.len > 4000) return empty;
    u8str_t ext = rubraview_path_ext(written);
    size_t base = written.len - ext.len;
    for (unsigned k = 0; k < 10000; ++k) {
        char tail[16];
        int t = k == 0 ? snprintf(tail, sizeof(tail), ".txt") : snprintf(tail, sizeof(tail), "-%u.txt", k);
        if (t <= 0) return empty;
        proven_result_mem_mut_t res = proven_arena_alloc(arena, base + (size_t)t + 1);
        if (!proven_is_ok(res.err)) return empty;
        char *p = (char*)res.value.ptr;
        memcpy(p, written.ptr, base);
        memcpy(p + base, tail, (size_t)t + 1);
        u8str_t path = { .ptr = p, .len = base + (size_t)t };
        if (!exists || !exists(path, ctx)) return path;
    }
    return empty;
}
