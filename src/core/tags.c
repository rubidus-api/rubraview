#include "rubraview/tags.h"
#include "rubraview/path.h"
#include <string.h>

/* Five formats keep their tags in five different places, and every one of
   them has a detail that a first reading gets wrong:

     ID3v2   the size is seven bits to the byte (2.4 counts a frame's size
             that way too, 2.3 does not), the text may be Latin-1, UTF-16
             with a mark, UTF-16BE or UTF-8, and the whole tag may have
             had every 0xFF 0x00 pair put in to keep a decoder from
             mistaking it for a frame.
     ID3v1   the last 128 bytes, fixed fields, no lengths at all.
     FLAC    blocks by type: the stream's shape, the comments, the cover.
     MP4     atoms inside atoms, and `meta` has four bytes of version
             before its children that no other atom has.
     Ogg     the same comments as FLAC, and a cover base64'd into one.

   Everything here reads bounded: a length that does not fit in what the
   caller handed over ends that tag and nothing else. */

typedef struct {
    const uint8_t *p;
    size_t         size;
} span_t;

static uint32_t be16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }
static uint32_t be24(const uint8_t *p) { return (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2]; }
static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static uint32_t le16(const uint8_t *p) { return (uint32_t)p[1] << 8 | p[0]; }
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}
/* Seven bits to the byte: the size ID3v2 writes so it can never look
   like the start of an MPEG frame. */
static uint32_t syncsafe(const uint8_t *p) {
    return ((uint32_t)(p[0] & 0x7F) << 21) | ((uint32_t)(p[1] & 0x7F) << 14) |
           ((uint32_t)(p[2] & 0x7F) << 7) | (uint32_t)(p[3] & 0x7F);
}

static u8str_t none(void) { return (u8str_t){ .ptr = "", .len = 0 }; }

static u8str_t arena_bytes(proven_arena_t *arena, const char *text, size_t len) {
    if (len == 0) return none();
    proven_result_mem_mut_t res = proven_arena_alloc(arena, len + 1);
    if (!proven_is_ok(res.err)) return none();
    char *out = (char*)res.value.ptr;
    memcpy(out, text, len);
    out[len] = '\0';
    return (u8str_t){ .ptr = out, .len = len };
}

/* Trailing NULs and blanks are padding in every one of these formats. */
static u8str_t trim(proven_arena_t *arena, const uint8_t *p, size_t len) {
    while (len > 0 && (p[len - 1] == '\0' || p[len - 1] == ' ' || p[len - 1] == '\r' ||
                       p[len - 1] == '\n' || p[len - 1] == '\t')) len--;
    size_t at = 0;
    while (at < len && (p[at] == ' ' || p[at] == '\t')) at++;
    return arena_bytes(arena, (const char*)p + at, len - at);
}

static size_t utf8_put(char *out, uint32_t cp) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static u8str_t latin1_to_utf8(proven_arena_t *arena, const uint8_t *p, size_t len) {
    proven_result_mem_mut_t res = proven_arena_alloc(arena, len * 2 + 1);
    if (!proven_is_ok(res.err)) return none();
    char *out = (char*)res.value.ptr;
    size_t at = 0;
    for (size_t i = 0; i < len; ++i) {
        if (p[i] == '\0') break;
        at += utf8_put(out + at, p[i]);
    }
    out[at] = '\0';
    return trim(arena, (const uint8_t*)out, at);
}

/* `big` unless the text opens with a byte-order mark saying otherwise. */
static u8str_t utf16_to_utf8(proven_arena_t *arena, const uint8_t *p, size_t len, bool big) {
    if (len >= 2 && p[0] == 0xFF && p[1] == 0xFE) { big = false; p += 2; len -= 2; }
    else if (len >= 2 && p[0] == 0xFE && p[1] == 0xFF) { big = true; p += 2; len -= 2; }

    proven_result_mem_mut_t res = proven_arena_alloc(arena, len * 2 + 4);
    if (!proven_is_ok(res.err)) return none();
    char *out = (char*)res.value.ptr;
    size_t at = 0;
    for (size_t i = 0; i + 1 < len; i += 2) {
        uint32_t unit = big ? ((uint32_t)p[i] << 8 | p[i + 1]) : ((uint32_t)p[i + 1] << 8 | p[i]);
        if (unit == 0) break;
        if (unit >= 0xD800 && unit < 0xDC00 && i + 3 < len) {     /* a pair */
            uint32_t low = big ? ((uint32_t)p[i + 2] << 8 | p[i + 3]) : ((uint32_t)p[i + 3] << 8 | p[i + 2]);
            if (low >= 0xDC00 && low < 0xE000) {
                unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            }
        }
        at += utf8_put(out + at, unit);
    }
    out[at] = '\0';
    return trim(arena, (const uint8_t*)out, at);
}

/* An ID3 text frame opens with the byte that says which of the four. */
static u8str_t id3_text(proven_arena_t *arena, const uint8_t *p, size_t len) {
    if (len == 0) return none();
    uint8_t encoding = p[0];
    p++;
    len--;
    switch (encoding) {
        case 1:  return utf16_to_utf8(arena, p, len, false);   /* with a mark */
        case 2:  return utf16_to_utf8(arena, p, len, true);    /* big, no mark */
        case 3:  return trim(arena, p, len);                   /* UTF-8 already */
        default: return latin1_to_utf8(arena, p, len);
    }
}

static bool four(const uint8_t *p, const char *id) {
    return p[0] == (uint8_t)id[0] && p[1] == (uint8_t)id[1] &&
           p[2] == (uint8_t)id[2] && p[3] == (uint8_t)id[3];
}
static bool three(const uint8_t *p, const char *id) {
    return p[0] == (uint8_t)id[0] && p[1] == (uint8_t)id[1] && p[2] == (uint8_t)id[2];
}

/* ---- ID3v2 ---- */

/* Every 0xFF 0x00 put in to hide the 0xFF is taken out again. */
static span_t de_unsynchronise(proven_arena_t *arena, const uint8_t *p, size_t len) {
    proven_result_mem_mut_t res = proven_arena_alloc(arena, len > 0 ? len : 1);
    if (!proven_is_ok(res.err)) return (span_t){ .p = p, .size = len };
    uint8_t *out = (uint8_t*)res.value.ptr;
    size_t at = 0;
    for (size_t i = 0; i < len; ++i) {
        out[at++] = p[i];
        if (p[i] == 0xFF && i + 1 < len && p[i + 1] == 0x00) i++;
    }
    return (span_t){ .p = out, .size = at };
}

/* `image/jpeg\0` then the kind, the description, then the picture. */
static void id3_picture(proven_arena_t *arena, const uint8_t *p, size_t len,
                        bool short_id, rubraview_tags_t *out) {
    if (len < 4) return;
    uint8_t encoding = p[0];
    size_t at = 1;
    u8str_t mime = none();
    if (short_id) {                       /* 2.2: three letters, "JPG" or "PNG" */
        if (len < 7) return;
        mime = three(p + 1, "PNG") ? U8("image/png") : U8("image/jpeg");
        at = 4;
    } else {
        size_t start = at;
        while (at < len && p[at] != '\0') at++;
        mime = arena_bytes(arena, (const char*)p + start, at - start);
        at++;                             /* the NUL */
    }
    if (at >= len) return;
    at++;                                 /* the kind: front cover, back, ... */

    /* The description ends the way its own encoding ends. */
    if (encoding == 1 || encoding == 2) {
        while (at + 1 < len && !(p[at] == 0 && p[at + 1] == 0)) at += 2;
        at += 2;
    } else {
        while (at < len && p[at] != '\0') at++;
        at++;
    }
    if (at >= len) return;
    size_t art = len - at;
    if (art == 0 || art > RUBRAVIEW_TAGS_MAX_ART) return;
    if (out->art_size > 0) return;        /* the first cover is the cover */
    out->art = p + at;
    out->art_size = art;
    out->art_mime = mime;
}

static void read_id3v2(proven_arena_t *arena, const uint8_t *bytes, size_t size,
                       rubraview_tags_t *out, size_t *out_end) {
    if (size < 10 || !three(bytes, "ID3")) return;
    uint8_t major = bytes[3];
    uint8_t flags = bytes[5];
    size_t tag_size = syncsafe(bytes + 6);
    if (tag_size == 0) return;
    size_t have = size - 10 < tag_size ? size - 10 : tag_size;
    *out_end = 10 + tag_size;

    span_t body = { .p = bytes + 10, .size = have };
    if (flags & 0x80) body = de_unsynchronise(arena, body.p, body.size);
    size_t at = 0;
    if ((flags & 0x40) && body.size >= 4) {          /* an extended header, skipped */
        size_t ext = major >= 4 ? syncsafe(body.p) : be32(body.p) + 4;
        if (ext < body.size) at = ext;
    }

    bool short_id = major <= 2;
    size_t id_len = short_id ? 3u : 4u;
    size_t head_len = short_id ? 6u : 10u;
    while (at + head_len <= body.size) {
        const uint8_t *frame = body.p + at;
        if (frame[0] == '\0') break;                 /* the padding at the end */
        size_t frame_size = short_id ? be24(frame + 3)
                          : (major >= 4 ? syncsafe(frame + 4) : be32(frame + 4));
        size_t data_at = at + head_len;
        if (frame_size == 0 || data_at + frame_size > body.size) break;
        const uint8_t *data = body.p + data_at;

        if (short_id) {
            if (three(frame, "TT2")) out->title = id3_text(arena, data, frame_size);
            else if (three(frame, "TP1")) out->artist = id3_text(arena, data, frame_size);
            else if (three(frame, "TAL")) out->album = id3_text(arena, data, frame_size);
            else if (three(frame, "TYE")) out->year = id3_text(arena, data, frame_size);
            else if (three(frame, "TRK")) out->track_number = id3_text(arena, data, frame_size);
            else if (three(frame, "TCO")) out->genre = id3_text(arena, data, frame_size);
            else if (three(frame, "PIC")) id3_picture(arena, data, frame_size, true, out);
        } else {
            if (four(frame, "TIT2")) out->title = id3_text(arena, data, frame_size);
            else if (four(frame, "TPE1")) out->artist = id3_text(arena, data, frame_size);
            else if (four(frame, "TALB")) out->album = id3_text(arena, data, frame_size);
            else if (four(frame, "TYER") || four(frame, "TDRC")) {
                if (out->year.len == 0) out->year = id3_text(arena, data, frame_size);
            }
            else if (four(frame, "TRCK")) out->track_number = id3_text(arena, data, frame_size);
            else if (four(frame, "TCON")) out->genre = id3_text(arena, data, frame_size);
            else if (four(frame, "APIC")) id3_picture(arena, data, frame_size, false, out);
        }
        at = data_at + frame_size;
        (void)id_len;
    }
}

/* ---- ID3v1: the last 128 bytes, and nothing says how long anything is ---- */

static void read_id3v1(proven_arena_t *arena, const uint8_t *tail, size_t size, rubraview_tags_t *out) {
    if (!tail || size < 128) return;
    const uint8_t *p = tail + size - 128;
    if (!three(p, "TAG")) return;
    if (out->title.len == 0) out->title = trim(arena, p + 3, 30);
    if (out->artist.len == 0) out->artist = trim(arena, p + 33, 30);
    if (out->album.len == 0) out->album = trim(arena, p + 63, 30);
    if (out->year.len == 0) out->year = trim(arena, p + 93, 4);
    if (out->track_number.len == 0 && p[125] == 0 && p[126] != 0) {
        char n[4];
        int at = 0;
        uint8_t v = p[126];
        if (v >= 100) n[at++] = (char)('0' + v / 100);
        if (v >= 10) n[at++] = (char)('0' + (v / 10) % 10);
        n[at++] = (char)('0' + v % 10);
        out->track_number = arena_bytes(arena, n, (size_t)at);
    }
}

/* ---- Vorbis comments: FLAC's, Ogg's and Opus's, all the same ---- */

static bool key_is(const uint8_t *p, size_t len, const char *key) {
    size_t n = strlen(key);
    if (len < n + 1 || p[n] != '=') return false;
    for (size_t i = 0; i < n; ++i) {
        uint8_t a = p[i];
        if (a >= 'a' && a <= 'z') a = (uint8_t)(a - 'a' + 'A');
        if (a != (uint8_t)key[i]) return false;
    }
    return true;
}

static void picture_block(proven_arena_t *arena, const uint8_t *p, size_t len, rubraview_tags_t *out);

static int base64_value(uint8_t c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static span_t base64_decode(proven_arena_t *arena, const uint8_t *p, size_t len) {
    proven_result_mem_mut_t res = proven_arena_alloc(arena, len / 4 * 3 + 3);
    if (!proven_is_ok(res.err)) return (span_t){ .p = NULL, .size = 0 };
    uint8_t *out = (uint8_t*)res.value.ptr;
    size_t at = 0;
    uint32_t bits = 0;
    int have = 0;
    for (size_t i = 0; i < len; ++i) {
        int v = base64_value(p[i]);
        if (v < 0) continue;
        bits = (bits << 6) | (uint32_t)v;
        have += 6;
        if (have >= 8) {
            have -= 8;
            out[at++] = (uint8_t)(bits >> have);
        }
    }
    return (span_t){ .p = out, .size = at };
}

static void read_vorbis_comment(proven_arena_t *arena, const uint8_t *p, size_t len,
                                rubraview_tags_t *out) {
    if (len < 8) return;
    size_t at = 0;
    uint32_t vendor = le32(p);
    at = 4 + vendor;
    if (at + 4 > len) return;
    uint32_t count = le32(p + at);
    at += 4;
    for (uint32_t i = 0; i < count && at + 4 <= len; ++i) {
        uint32_t size = le32(p + at);
        at += 4;
        if (size > len - at) return;
        const uint8_t *entry = p + at;
        if (key_is(entry, size, "TITLE") && out->title.len == 0)
            out->title = trim(arena, entry + 6, size - 6);
        else if (key_is(entry, size, "ARTIST") && out->artist.len == 0)
            out->artist = trim(arena, entry + 7, size - 7);
        else if (key_is(entry, size, "ALBUM") && out->album.len == 0)
            out->album = trim(arena, entry + 6, size - 6);
        else if (key_is(entry, size, "DATE") && out->year.len == 0)
            out->year = trim(arena, entry + 5, size - 5);
        else if (key_is(entry, size, "TRACKNUMBER") && out->track_number.len == 0)
            out->track_number = trim(arena, entry + 12, size - 12);
        else if (key_is(entry, size, "GENRE") && out->genre.len == 0)
            out->genre = trim(arena, entry + 6, size - 6);
        else if (key_is(entry, size, "METADATA_BLOCK_PICTURE") && out->art_size == 0) {
            /* A FLAC picture block, base64'd, because a comment is text. */
            span_t raw = base64_decode(arena, entry + 23, size - 23);
            if (raw.p) picture_block(arena, raw.p, raw.size, out);
        }
        at += size;
    }
}

/* ---- FLAC ---- */

static void picture_block(proven_arena_t *arena, const uint8_t *p, size_t len, rubraview_tags_t *out) {
    if (len < 32) return;
    size_t at = 4;                        /* the kind: front cover, back, ... */
    uint32_t mime_len = be32(p + at);
    at += 4;
    if (mime_len > len - at) return;
    u8str_t mime = arena_bytes(arena, (const char*)p + at, mime_len);
    at += mime_len;
    if (at + 4 > len) return;
    uint32_t desc_len = be32(p + at);
    at += 4;
    if (desc_len > len - at) return;
    at += desc_len;
    if (at + 20 > len) return;
    at += 16;                             /* width, height, depth, colours */
    uint32_t art_len = be32(p + at);
    at += 4;
    if (art_len == 0 || art_len > len - at || art_len > RUBRAVIEW_TAGS_MAX_ART) return;
    if (out->art_size > 0) return;
    out->art = p + at;
    out->art_size = art_len;
    out->art_mime = mime;
}

static void read_flac(proven_arena_t *arena, const uint8_t *p, size_t len, rubraview_tags_t *out) {
    out->codec = U8("FLAC");
    size_t at = 4;
    for (int guard = 0; guard < 128 && at + 4 <= len; ++guard) {
        uint8_t type = (uint8_t)(p[at] & 0x7F);
        bool last = (p[at] & 0x80) != 0;
        size_t size = be24(p + at + 1);
        at += 4;
        if (size > len - at) break;
        if (type == 0 && size >= 18) {                     /* the stream's shape */
            const uint8_t *s = p + at;
            out->sample_rate = ((uint32_t)s[10] << 12) | ((uint32_t)s[11] << 4) | ((uint32_t)s[12] >> 4);
            out->channels = (uint32_t)(((s[12] >> 1) & 0x07) + 1);
            out->bits_per_sample = (uint32_t)((((s[12] & 0x01) << 4) | (s[13] >> 4)) + 1);
            uint64_t samples = ((uint64_t)(s[13] & 0x0F) << 32) | be32(s + 14);
            if (out->sample_rate > 0 && samples > 0) {
                out->duration_seconds = (double)samples / (double)out->sample_rate;
            }
        } else if (type == 4) {
            read_vorbis_comment(arena, p + at, size, out);
        } else if (type == 6) {
            picture_block(arena, p + at, size, out);
        }
        at += size;
        if (last) break;
    }
}

/* ---- Ogg: Vorbis and Opus keep the same comments in their second packet ---- */

static void read_ogg(proven_arena_t *arena, const uint8_t *p, size_t len, rubraview_tags_t *out) {
    out->codec = U8("Vorbis");
    size_t limit = len > 512u * 1024u ? 512u * 1024u : len;
    for (size_t i = 0; i + 8 < limit; ++i) {
        if (p[i] == 'O' && memcmp(p + i, "OpusHead", 8) == 0 && i + 19 <= len) {
            out->codec = U8("Opus");
            out->channels = p[i + 9];
            out->sample_rate = le32(p + i + 12);       /* the original rate; Opus runs at 48 kHz */
            if (out->sample_rate == 0) out->sample_rate = 48000;
        } else if (p[i] == 'O' && memcmp(p + i, "OpusTags", 8) == 0) {
            read_vorbis_comment(arena, p + i + 8, len - i - 8, out);
        } else if (p[i] == 0x01 && i + 30 < len && memcmp(p + i + 1, "vorbis", 6) == 0) {
            out->channels = p[i + 11];
            out->sample_rate = le32(p + i + 12);
        } else if (p[i] == 0x03 && i + 7 < len && memcmp(p + i + 1, "vorbis", 6) == 0) {
            read_vorbis_comment(arena, p + i + 7, len - i - 7, out);
        }
    }
}

/* ---- MP4 / M4A: atoms inside atoms ---- */

static void mp4_ilst(proven_arena_t *arena, const uint8_t *p, size_t len, rubraview_tags_t *out) {
    size_t at = 0;
    while (at + 8 <= len) {
        uint32_t size = be32(p + at);
        if (size < 8 || size > len - at) break;
        const uint8_t *name = p + at + 4;
        /* Every value sits in a `data` atom: four bytes of kind, four of
           locale, then the thing itself. */
        const uint8_t *body = p + at + 8;
        size_t body_len = size - 8;
        u8str_t value = none();
        const uint8_t *raw = NULL;
        size_t raw_len = 0;
        uint32_t kind = 0;
        if (body_len >= 16 && four(body + 4, "data")) {
            uint32_t data_size = be32(body);
            if (data_size >= 16 && data_size <= body_len) {
                kind = be32(body + 8) & 0x00FFFFFFu;
                raw = body + 16;
                raw_len = data_size - 16;
                if (kind == 1) value = trim(arena, raw, raw_len);   /* UTF-8 */
            }
        }
        if (four(name, "\xA9nam")) { if (out->title.len == 0) out->title = value; }
        else if (four(name, "\xA9""ART")) { if (out->artist.len == 0) out->artist = value; }
        else if (four(name, "\xA9""alb")) { if (out->album.len == 0) out->album = value; }
        else if (four(name, "\xA9""day")) { if (out->year.len == 0) out->year = value; }
        else if (four(name, "\xA9gen")) { if (out->genre.len == 0) out->genre = value; }
        else if (four(name, "trkn") && raw && raw_len >= 4 && out->track_number.len == 0) {
            uint32_t number = be16(raw + 2);
            char n[8];
            int wrote = 0;
            if (number >= 100) n[wrote++] = (char)('0' + number / 100 % 10);
            if (number >= 10) n[wrote++] = (char)('0' + number / 10 % 10);
            n[wrote++] = (char)('0' + number % 10);
            out->track_number = arena_bytes(arena, n, (size_t)wrote);
        } else if (four(name, "covr") && raw && raw_len > 0 && out->art_size == 0 &&
                   raw_len <= RUBRAVIEW_TAGS_MAX_ART) {
            out->art = raw;
            out->art_size = raw_len;
            /* 13 is JPEG and 14 is PNG; the bytes say so too. */
            out->art_mime = (raw_len > 8 && raw[0] == 0x89 && raw[1] == 'P')
                          ? U8("image/png") : U8("image/jpeg");
        }
        at += size;
    }
}

static void mp4_walk(proven_arena_t *arena, const uint8_t *p, size_t len, int depth,
                     rubraview_tags_t *out) {
    if (depth > 8) return;
    size_t at = 0;
    while (at + 8 <= len) {
        uint64_t size = be32(p + at);
        const uint8_t *name = p + at + 4;
        size_t header = 8;
        if (size == 1) {                        /* a 64-bit size follows the name */
            if (at + 16 > len) break;
            size = ((uint64_t)be32(p + at + 8) << 32) | be32(p + at + 12);
            header = 16;
        }
        if (size < header || size > len - at) break;
        const uint8_t *body = p + at + header;
        size_t body_len = (size_t)size - header;

        if (four(name, "moov") || four(name, "udta") || four(name, "trak") ||
            four(name, "mdia") || four(name, "minf") || four(name, "stbl")) {
            mp4_walk(arena, body, body_len, depth + 1, out);
        } else if (four(name, "meta") && body_len > 4) {
            mp4_walk(arena, body + 4, body_len - 4, depth + 1, out);   /* the version nothing else has */
        } else if (four(name, "ilst")) {
            mp4_ilst(arena, body, body_len, out);
        } else if (four(name, "mvhd") && body_len >= 20) {
            uint32_t timescale = be32(body + 12);
            uint32_t duration = be32(body + 16);
            if (timescale > 0 && duration > 0 && out->duration_seconds == 0.0) {
                out->duration_seconds = (double)duration / (double)timescale;
            }
        } else if (four(name, "stsd") && body_len >= 36) {
            const uint8_t *entry = body + 8;
            if (four(entry + 4, "mp4a") || four(entry + 4, "alac")) {
                out->codec = four(entry + 4, "alac") ? U8("ALAC") : U8("AAC");
                out->channels = be16(entry + 24);
                out->bits_per_sample = be16(entry + 26);
                out->sample_rate = be16(entry + 32);   /* 16.16 fixed point: the whole part */
            }
        }
        at += (size_t)size;
    }
}

/* ---- WAV ---- */

static void read_wav(proven_arena_t *arena, const uint8_t *p, size_t len, rubraview_tags_t *out) {
    out->codec = U8("WAV");
    size_t at = 12;
    while (at + 8 <= len) {
        uint32_t size = le32(p + at + 4);
        const uint8_t *body = p + at + 8;
        /* Only the head of the file is here, so `data` — which is the
           whole recording — is nearly always longer than what we hold.
           Its *declared* length is what the length is worked out from. */
        if (four(p + at, "data")) {
            if (out->sample_rate > 0 && out->channels > 0 && out->bits_per_sample > 0) {
                double frame = (double)out->channels * (double)out->bits_per_sample / 8.0;
                if (frame > 0.0) out->duration_seconds = (double)size / frame / (double)out->sample_rate;
            }
            break;
        }
        if (size > len - at - 8) break;
        if (four(p + at, "fmt ") && size >= 16) {
            out->channels = le16(body + 2);
            out->sample_rate = le32(body + 4);
            out->bits_per_sample = le16(body + 14);
            uint32_t bytes_per_second = le32(body + 8);
            if (bytes_per_second > 0) out->bitrate_kbps = bytes_per_second * 8 / 1000;
        } else if (four(p + at, "LIST") && size >= 4 && four(body, "INFO")) {
            size_t in = 4;
            while (in + 8 <= size) {
                uint32_t item = le32(body + in + 4);
                if (item > size - in - 8) break;
                const uint8_t *text = body + in + 8;
                if (four(body + in, "INAM") && out->title.len == 0) out->title = trim(arena, text, item);
                else if (four(body + in, "IART") && out->artist.len == 0) out->artist = trim(arena, text, item);
                else if (four(body + in, "IPRD") && out->album.len == 0) out->album = trim(arena, text, item);
                else if (four(body + in, "ICRD") && out->year.len == 0) out->year = trim(arena, text, item);
                in += 8 + item + (item & 1);
            }
        }
        at += 8 + size + (size & 1);           /* chunks are padded to an even length */
    }
}

/* ---- MP3: the first frame header says the rest ---- */

static void read_mp3_frame(const uint8_t *p, size_t len, size_t from, rubraview_tags_t *out) {
    static const uint32_t BITRATE_V1_L3[16] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 };
    static const uint32_t BITRATE_V2_L3[16] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 };
    static const uint32_t RATE_V1[4] = { 44100, 48000, 32000, 0 };

    out->codec = U8("MP3");
    size_t limit = from + 65536 < len ? from + 65536 : len;
    for (size_t i = from; i + 4 <= limit; ++i) {
        if (p[i] != 0xFF || (p[i + 1] & 0xE0) != 0xE0) continue;
        uint32_t version = (uint32_t)(p[i + 1] >> 3) & 0x03;   /* 3 = MPEG-1, 2 = 2, 0 = 2.5 */
        uint32_t layer = (uint32_t)(p[i + 1] >> 1) & 0x03;
        uint32_t bitrate_index = (uint32_t)(p[i + 2] >> 4);
        uint32_t rate_index = (uint32_t)(p[i + 2] >> 2) & 0x03;
        if (version == 1 || layer == 0 || bitrate_index == 0 || bitrate_index == 15 || rate_index == 3) continue;
        uint32_t rate = RATE_V1[rate_index];
        if (version == 2) rate /= 2;
        else if (version == 0) rate /= 4;
        out->sample_rate = rate;
        out->bitrate_kbps = version == 3 ? BITRATE_V1_L3[bitrate_index] : BITRATE_V2_L3[bitrate_index];
        out->channels = ((p[i + 3] >> 6) & 0x03) == 3 ? 1u : 2u;
        return;
    }
}

/* ---- what it all adds up to ---- */

rubraview_tags_t rubraview_tags_read(proven_arena_t *arena, rubraview_tags_source_t source) {
    rubraview_tags_t out = {0};
    out.title = out.artist = out.album = out.year = out.track_number = out.genre = none();
    out.codec = out.art_mime = none();
    if (!arena || !source.head || source.head_size < 12) return out;

    const uint8_t *p = source.head;
    size_t len = source.head_size;

    size_t after_id3 = 0;
    read_id3v2(arena, p, len, &out, &after_id3);       /* MP3 and often FLAC too */

    if (four(p, "fLaC")) {
        read_flac(arena, p, len, &out);
    } else if (four(p, "OggS")) {
        read_ogg(arena, p, len, &out);
    } else if (len > 12 && four(p + 4, "ftyp")) {
        mp4_walk(arena, p, len, 0, &out);
        if (out.codec.len == 0) out.codec = U8("AAC");
    } else if (four(p, "RIFF") && len > 12 && four(p + 8, "WAVE")) {
        read_wav(arena, p, len, &out);
    } else {
        read_mp3_frame(p, len, after_id3 < len ? after_id3 : 0, &out);
    }

    read_id3v1(arena, source.tail, source.tail_size, &out);

    /* When nothing measured it, the file's own size and length do. */
    if (out.bitrate_kbps == 0 && out.duration_seconds > 0.0 && source.file_size > 0) {
        double kbps = (double)source.file_size * 8.0 / out.duration_seconds / 1000.0;
        if (kbps > 0.0 && kbps < 100000.0) out.bitrate_kbps = (uint32_t)(kbps + 0.5);
    }
    return out;
}

bool rubraview_tags_is_music_name(u8str_t filename) {
    static const char *const EXTS[] = {
        ".mp3", ".flac", ".wav", ".ogg", ".oga", ".opus", ".m4a", ".aac",
        ".wma", ".aiff", ".aif", ".ape", ".alac", ".ra",
    };
    u8str_t ext = rubraview_path_ext(filename);
    if (ext.len == 0) return false;
    for (size_t i = 0; i < sizeof(EXTS) / sizeof(EXTS[0]); ++i) {
        size_t n = strlen(EXTS[i]);
        if (ext.len != n) continue;
        bool same = true;
        for (size_t k = 0; k < n && same; ++k) {
            char a = ext.ptr[k];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            same = a == EXTS[i][k];
        }
        if (same) return true;
    }
    return false;
}
