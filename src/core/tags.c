#include "rubraview/core.h"
#include "rubraview/tags.h"
#include "rubraview/audio_exts.h"
#include "rubraview/path.h"
#include "rubraview/number.h"
#include "rubraview/media_head.h"   /* R367: a Wave64's chunks */
#include "rubraview/adpcm.h"
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

static proven_u8str_view_t none(void) { return PROVEN_LIT(""); }

static proven_u8str_view_t arena_bytes(proven_arena_t *arena, proven_u8str_view_t text_view) {
    const char *text = (const char *)text_view.ptr;
    size_t len = (size_t)text_view.size;
    if (len == 0) return none();
    proven_result_mem_mut_t res = proven_arena_alloc(arena, len + 1);
    if (!proven_is_ok(res.err)) return none();
    char *out = (char*)res.value.ptr;
    proven_sys_mem_copy(out, text, len);
    out[len] = '\0';
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)out, .size = len };
}

/* Trailing NULs and blanks are padding in every one of these formats. */
static proven_u8str_view_t trim(proven_arena_t *arena, const uint8_t *p, size_t len) {
    while (len > 0 && (p[len - 1] == '\0' || p[len - 1] == ' ' || p[len - 1] == '\r' ||
                       p[len - 1] == '\n' || p[len - 1] == '\t')) len--;
    size_t at = 0;
    while (at < len && (p[at] == ' ' || p[at] == '\t')) at++;
    return arena_bytes(arena, rubraview_bytes_view((const char*)p + at, len - at));
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

static proven_u8str_view_t latin1_to_utf8(proven_arena_t *arena, proven_mem_view_t p_view) {
    const uint8_t *p = (const uint8_t *)p_view.ptr;
    size_t len = (size_t)p_view.size;
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
static proven_u8str_view_t utf16_to_utf8(proven_arena_t *arena, proven_mem_view_t p_view, bool big) {
    const uint8_t *p = (const uint8_t *)p_view.ptr;
    size_t len = (size_t)p_view.size;
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
static proven_u8str_view_t id3_text(proven_arena_t *arena, proven_mem_view_t p_view) {
    const uint8_t *p = (const uint8_t *)p_view.ptr;
    size_t len = (size_t)p_view.size;
    if (len == 0) return none();
    uint8_t encoding = p[0];
    p++;
    len--;
    switch (encoding) {
        case 1:  return utf16_to_utf8(arena, rubraview_mem_view(p, len), false);   /* with a mark */
        case 2:  return utf16_to_utf8(arena, rubraview_mem_view(p, len), true);    /* big, no mark */
        case 3:  return trim(arena, p, len);                   /* UTF-8 already */
        default: return latin1_to_utf8(arena, rubraview_mem_view(p, len));
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
static span_t de_unsynchronise(proven_arena_t *arena, proven_mem_view_t p_view) {
    const uint8_t *p = (const uint8_t *)p_view.ptr;
    size_t len = (size_t)p_view.size;
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
static void id3_picture(proven_arena_t *arena, proven_mem_view_t p_view,
                        bool short_id, rubraview_tags_t *out) {
    const uint8_t *p = (const uint8_t *)p_view.ptr;
    size_t len = (size_t)p_view.size;
    if (len < 4) return;
    uint8_t encoding = p[0];
    size_t at = 1;
    proven_u8str_view_t mime = none();
    if (short_id) {                       /* 2.2: three letters, "JPG" or "PNG" */
        if (len < 7) return;
        mime = three(p + 1, "PNG") ? PROVEN_LIT("image/png") : PROVEN_LIT("image/jpeg");
        at = 4;
    } else {
        size_t start = at;
        while (at < len && p[at] != '\0') at++;
        mime = arena_bytes(arena, rubraview_bytes_view((const char*)p + start, at - start));
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

static void id3_txxx(proven_arena_t *arena, proven_mem_view_t p_view, rubraview_tags_t *out);
static void id3_uslt(proven_arena_t *arena, proven_mem_view_t p_view, rubraview_tags_t *out);
static void id3_sylt(proven_arena_t *arena, proven_mem_view_t p_view, rubraview_tags_t *out);

static void read_id3v2(proven_arena_t *arena, proven_mem_view_t bytes_view,
                       rubraview_tags_t *out, size_t *out_end) {
    const uint8_t *bytes = (const uint8_t *)bytes_view.ptr;
    size_t size = (size_t)bytes_view.size;
    if (size < 10 || !three(bytes, "ID3")) return;
    uint8_t major = bytes[3];
    uint8_t flags = bytes[5];
    size_t tag_size = syncsafe(bytes + 6);
    if (tag_size == 0) return;
    size_t have = size - 10 < tag_size ? size - 10 : tag_size;
    *out_end = 10 + tag_size;

    span_t body = { .p = bytes + 10, .size = have };
    if (flags & 0x80) body = de_unsynchronise(arena, rubraview_mem_view(body.p, body.size));
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
            if (three(frame, "TT2")) out->title = id3_text(arena, rubraview_mem_view(data, frame_size));
            else if (three(frame, "TP1")) out->artist = id3_text(arena, rubraview_mem_view(data, frame_size));
            else if (three(frame, "TAL")) out->album = id3_text(arena, rubraview_mem_view(data, frame_size));
            else if (three(frame, "TYE")) out->year = id3_text(arena, rubraview_mem_view(data, frame_size));
            else if (three(frame, "TRK")) out->track_number = id3_text(arena, rubraview_mem_view(data, frame_size));
            else if (three(frame, "TCO")) out->genre = id3_text(arena, rubraview_mem_view(data, frame_size));
            else if (three(frame, "PIC")) id3_picture(arena, rubraview_mem_view(data, frame_size), true, out);
            else if (three(frame, "ULT")) id3_uslt(arena, rubraview_mem_view(data, frame_size), out);
            else if (three(frame, "SLT")) id3_sylt(arena, rubraview_mem_view(data, frame_size), out);
        } else {
            if (four(frame, "TIT2")) out->title = id3_text(arena, rubraview_mem_view(data, frame_size));
            else if (four(frame, "TPE1")) out->artist = id3_text(arena, rubraview_mem_view(data, frame_size));
            else if (four(frame, "TALB")) out->album = id3_text(arena, rubraview_mem_view(data, frame_size));
            else if (four(frame, "TYER") || four(frame, "TDRC")) {
                if (out->year.size == 0) out->year = id3_text(arena, rubraview_mem_view(data, frame_size));
            }
            else if (four(frame, "TRCK")) out->track_number = id3_text(arena, rubraview_mem_view(data, frame_size));
            else if (four(frame, "TCON")) out->genre = id3_text(arena, rubraview_mem_view(data, frame_size));
            else if (four(frame, "APIC")) id3_picture(arena, rubraview_mem_view(data, frame_size), false, out);
            else if (four(frame, "TXXX")) id3_txxx(arena, rubraview_mem_view(data, frame_size), out);
            else if (four(frame, "USLT")) id3_uslt(arena, rubraview_mem_view(data, frame_size), out);
            else if (four(frame, "SYLT")) id3_sylt(arena, rubraview_mem_view(data, frame_size), out);
        }
        at = data_at + frame_size;
        (void)id_len;
    }
}

/* ---- ID3v1: the last 128 bytes, and nothing says how long anything is ---- */

static void read_id3v1(proven_arena_t *arena, proven_mem_view_t tail_view, rubraview_tags_t *out) {
    const uint8_t *tail = (const uint8_t *)tail_view.ptr;
    size_t size = (size_t)tail_view.size;
    if (!tail || size < 128) return;
    const uint8_t *p = tail + size - 128;
    if (!three(p, "TAG")) return;
    if (out->title.size == 0) out->title = trim(arena, p + 3, 30);
    if (out->artist.size == 0) out->artist = trim(arena, p + 33, 30);
    if (out->album.size == 0) out->album = trim(arena, p + 63, 30);
    if (out->year.size == 0) out->year = trim(arena, p + 93, 4);
    if (out->track_number.size == 0 && p[125] == 0 && p[126] != 0) {
        char n[4];
        int at = 0;
        uint8_t v = p[126];
        if (v >= 100) n[at++] = (char)('0' + v / 100);
        if (v >= 10) n[at++] = (char)('0' + (v / 10) % 10);
        n[at++] = (char)('0' + v % 10);
        out->track_number = arena_bytes(arena, rubraview_bytes_view(n, (size_t)at));
    }
}

/* ---- ReplayGain, by name, from whichever tag carries it ---- */

static bool same_ci(proven_u8str_view_t a, const char *b) {
    size_t n = proven_cstr_len(b);
    if (a.size != n) return false;
    for (size_t i = 0; i < n; ++i) {
        char c = a.ptr[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c != b[i]) return false;
    }
    return true;
}

/* A name and its value: a gain ("-7.23 dB") or a peak ("0.988"). */
static void replaygain_value(rubraview_tags_t *out, proven_u8str_view_t name, proven_u8str_view_t value) {
    double v = 0.0;
    size_t used = 0;
    while (value.size > 0 && (value.ptr[0] == ' ' || value.ptr[0] == '+')) { value.ptr++; value.size--; }
    if (same_ci(name, "REPLAYGAIN_TRACK_GAIN") && rubraview_replaygain_parse_db(value, &v)) {
        out->replaygain.has_track_gain = true; out->replaygain.track_gain_db = v;
    } else if (same_ci(name, "REPLAYGAIN_ALBUM_GAIN") && rubraview_replaygain_parse_db(value, &v)) {
        out->replaygain.has_album_gain = true; out->replaygain.album_gain_db = v;
    } else if (same_ci(name, "REPLAYGAIN_TRACK_PEAK") && rubraview_parse_double_prefix(value, &v, &used) && v > 0.0) {
        out->replaygain.track_peak = v;
    } else if (same_ci(name, "REPLAYGAIN_ALBUM_PEAK") && rubraview_parse_double_prefix(value, &v, &used) && v > 0.0) {
        out->replaygain.album_peak = v;
    }
}

/* ID3v2's TXXX: an encoding byte, a description ending in a NUL (two for
   UTF-16), then the value. */
static void id3_txxx(proven_arena_t *arena, proven_mem_view_t p_view, rubraview_tags_t *out) {
    const uint8_t *p = (const uint8_t *)p_view.ptr;
    size_t len = (size_t)p_view.size;
    if (len < 3) return;
    uint8_t enc = p[0];
    bool wide = enc == 1 || enc == 2;
    size_t end = 1;
    if (wide) { while (end + 1 < len && !(p[end] == 0 && p[end + 1] == 0)) end += 2; }
    else      { while (end < len && p[end] != 0) end++; }
    if (end >= len) return;
    size_t value_at = end + (wide ? 2u : 1u);
    proven_u8str_view_t name = id3_text(arena, rubraview_mem_view(p, end));   /* the encoding byte and the description */
    proven_result_mem_mut_t res = proven_arena_alloc(arena, len - value_at + 1);
    if (!proven_is_ok(res.err)) return;
    uint8_t *buf = (uint8_t*)res.value.ptr;
    buf[0] = enc;
    proven_sys_mem_copy(buf + 1, p + value_at, len - value_at);
    proven_u8str_view_t value = id3_text(arena, rubraview_mem_view(buf, len - value_at + 1));
    replaygain_value(out, name, value);
}

/* ---- the words: USLT and SYLT ---- */

/* Where a string in `enc` ends: the offset of its terminator (one NUL, or
   two on a two-byte boundary for UTF-16), or `len` when it runs to the end. */
static size_t id3_string_end(proven_mem_view_t p_view, uint8_t enc) {
    const uint8_t *p = (const uint8_t *)p_view.ptr;
    size_t len = (size_t)p_view.size;
    if (enc == 1 || enc == 2) {
        size_t at = 0;
        while (at + 1 < len && !(p[at] == 0 && p[at + 1] == 0)) at += 2;
        return at + 1 < len ? at : len;
    }
    size_t at = 0;
    while (at < len && p[at] != 0) at++;
    return at;
}

static proven_u8str_view_t id3_string(proven_arena_t *arena, uint8_t enc, proven_mem_view_t p_view) {
    const uint8_t *p = (const uint8_t *)p_view.ptr;
    size_t len = (size_t)p_view.size;
    switch (enc) {
        case 1:  return utf16_to_utf8(arena, rubraview_mem_view(p, len), false);
        case 2:  return utf16_to_utf8(arena, rubraview_mem_view(p, len), true);
        case 3:  return trim(arena, p, len);
        default: return latin1_to_utf8(arena, rubraview_mem_view(p, len));
    }
}

/* USLT: encoding, a language of three letters, a description, the text.
   The first one found is kept. */
static void id3_uslt(proven_arena_t *arena, proven_mem_view_t p_view, rubraview_tags_t *out) {
    const uint8_t *p = (const uint8_t *)p_view.ptr;
    size_t len = (size_t)p_view.size;
    if (len < 5 || out->lyrics_text.size > 0) return;
    uint8_t enc = p[0];
    size_t at = 4;
    size_t end = id3_string_end(rubraview_mem_view(p + at, len - at), enc);
    at += end + ((enc == 1 || enc == 2) ? 2u : 1u);
    if (at >= len) return;
    out->lyrics_text = id3_string(arena, enc, rubraview_mem_view(p + at, len - at));
}

/* SYLT: encoding, language, the stamps' unit, the content's kind, a
   description, then text and a four-byte big-endian stamp, again and again.
   Only milliseconds (unit 2) are kept: MPEG frames need the frame rate. */
static void id3_sylt(proven_arena_t *arena, proven_mem_view_t p_view, rubraview_tags_t *out) {
    const uint8_t *p = (const uint8_t *)p_view.ptr;
    size_t len = (size_t)p_view.size;
    if (len < 7 || out->synced_lyrics_count > 0) return;
    uint8_t enc = p[0], unit = p[4];
    if (unit != 2) return;
    size_t term = (enc == 1 || enc == 2) ? 2u : 1u;
    size_t at = 6;
    at += id3_string_end(rubraview_mem_view(p + at, len - at), enc) + term;          /* the description */
    size_t cap = 0, count = 0;
    rubraview_lyric_line_t *lines = NULL;
    while (at < len) {
        size_t end = id3_string_end(rubraview_mem_view(p + at, len - at), enc);
        if (at + end + term + 4 > len) break;
        proven_u8str_view_t text = id3_string(arena, enc, rubraview_mem_view(p + at, end));
        /* a new line is often written as a leading line break */
        while (text.size > 0 && (text.ptr[0] == '\n' || text.ptr[0] == '\r')) { text.ptr++; text.size--; }
        uint32_t ms = be32(p + at + end + term);
        at += end + term + 4;
        if (count == cap) {
            size_t grown = cap ? cap * 2 : 64;
            proven_result_mem_mut_t res = proven_arena_alloc(arena, grown * sizeof(rubraview_lyric_line_t));
            if (!proven_is_ok(res.err)) break;
            if (count) proven_sys_mem_copy(res.value.ptr, lines, count * sizeof(rubraview_lyric_line_t));
            lines = (rubraview_lyric_line_t*)(void*)res.value.ptr;
            cap = grown;
        }
        lines[count++] = (rubraview_lyric_line_t){ .time_seconds = (double)ms / 1000.0, .text = text };
    }
    out->synced_lyrics = lines;
    out->synced_lyrics_count = count;
}

/* ---- Vorbis comments: FLAC's, Ogg's and Opus's, all the same ---- */

static bool key_is(const uint8_t *p, size_t len, const char *key) {
    size_t n = proven_cstr_len(key);
    if (len < n + 1 || p[n] != '=') return false;
    return proven_u8str_view_eq_ascii_ci(proven_u8str_view_from_bytes(p, n), proven_u8str_view_from_bytes(key, n));
}

static void picture_block(proven_arena_t *arena, proven_mem_view_t p_view, rubraview_tags_t *out);

static int base64_value(uint8_t c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static span_t base64_decode(proven_arena_t *arena, proven_mem_view_t p_view) {
    const uint8_t *p = (const uint8_t *)p_view.ptr;
    size_t len = (size_t)p_view.size;
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

static void read_vorbis_comment(proven_arena_t *arena, proven_mem_view_t p_view,
                                rubraview_tags_t *out) {
    const uint8_t *p = (const uint8_t *)p_view.ptr;
    size_t len = (size_t)p_view.size;
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
        if (key_is(entry, size, "TITLE") && out->title.size == 0)
            out->title = trim(arena, entry + 6, size - 6);
        else if (key_is(entry, size, "ARTIST") && out->artist.size == 0)
            out->artist = trim(arena, entry + 7, size - 7);
        else if (key_is(entry, size, "ALBUM") && out->album.size == 0)
            out->album = trim(arena, entry + 6, size - 6);
        else if (key_is(entry, size, "DATE") && out->year.size == 0)
            out->year = trim(arena, entry + 5, size - 5);
        else if (key_is(entry, size, "TRACKNUMBER") && out->track_number.size == 0)
            out->track_number = trim(arena, entry + 12, size - 12);
        else if (key_is(entry, size, "GENRE") && out->genre.size == 0)
            out->genre = trim(arena, entry + 6, size - 6);
        else if (key_is(entry, size, "REPLAYGAIN_TRACK_GAIN") || key_is(entry, size, "REPLAYGAIN_ALBUM_GAIN") ||
                 key_is(entry, size, "REPLAYGAIN_TRACK_PEAK") || key_is(entry, size, "REPLAYGAIN_ALBUM_PEAK")) {
            proven_u8str_view_t name = { .ptr = (const proven_byte_t *)(const char*)entry, .size = 21 };
            proven_u8str_view_t value = trim(arena, entry + 22, size - 22);
            replaygain_value(out, name, value);
        }
        else if (key_is(entry, size, "LYRICS") && out->lyrics_text.size == 0)
            out->lyrics_text = trim(arena, entry + 7, size - 7);
        else if (key_is(entry, size, "UNSYNCEDLYRICS") && out->lyrics_text.size == 0)
            out->lyrics_text = trim(arena, entry + 15, size - 15);
        else if (key_is(entry, size, "METADATA_BLOCK_PICTURE") && out->art_size == 0) {
            /* A FLAC picture block, base64'd, because a comment is text. */
            span_t raw = base64_decode(arena, rubraview_mem_view(entry + 23, size - 23));
            if (raw.p) picture_block(arena, rubraview_mem_view(raw.p, raw.size), out);
        }
        at += size;
    }
}

/* ---- FLAC ---- */

static void picture_block(proven_arena_t *arena, proven_mem_view_t p_view, rubraview_tags_t *out) {
    const uint8_t *p = (const uint8_t *)p_view.ptr;
    size_t len = (size_t)p_view.size;
    if (len < 32) return;
    size_t at = 4;                        /* the kind: front cover, back, ... */
    uint32_t mime_len = be32(p + at);
    at += 4;
    if (mime_len > len - at) return;
    proven_u8str_view_t mime = arena_bytes(arena, rubraview_bytes_view((const char*)p + at, mime_len));
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
    out->codec = PROVEN_LIT("FLAC");
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
            read_vorbis_comment(arena, rubraview_mem_view(p + at, size), out);
        } else if (type == 6) {
            picture_block(arena, rubraview_mem_view(p + at, size), out);
        }
        at += size;
        if (last) break;
    }
}

/* ---- Ogg: Vorbis and Opus keep the same comments in their second packet ---- */

static void read_ogg(proven_arena_t *arena, const uint8_t *p, size_t len, rubraview_tags_t *out) {
    out->codec = PROVEN_LIT("Vorbis");
    size_t limit = len > 512u * 1024u ? 512u * 1024u : len;
    for (size_t i = 0; i + 8 < limit; ++i) {
        if (p[i] == 'O' && proven_sys_mem_cmp(p + i, "OpusHead", 8) == 0 && i + 19 <= len) {
            out->codec = PROVEN_LIT("Opus");
            out->channels = p[i + 9];
            out->sample_rate = le32(p + i + 12);       /* the original rate; Opus runs at 48 kHz */
            if (out->sample_rate == 0) out->sample_rate = 48000;
        } else if (p[i] == 'O' && proven_sys_mem_cmp(p + i, "OpusTags", 8) == 0) {
            read_vorbis_comment(arena, rubraview_mem_view(p + i + 8, len - i - 8), out);
        } else if (p[i] == 0x01 && i + 30 < len && proven_sys_mem_cmp(p + i + 1, "vorbis", 6) == 0) {
            out->channels = p[i + 11];
            out->sample_rate = le32(p + i + 12);
        } else if (p[i] == 0x03 && i + 7 < len && proven_sys_mem_cmp(p + i + 1, "vorbis", 6) == 0) {
            read_vorbis_comment(arena, rubraview_mem_view(p + i + 7, len - i - 7), out);
        }
    }
}

/* ---- MP4 / M4A: atoms inside atoms ---- */

static void mp4_ilst(proven_arena_t *arena, proven_mem_view_t p_view, rubraview_tags_t *out) {
    const uint8_t *p = (const uint8_t *)p_view.ptr;
    size_t len = (size_t)p_view.size;
    size_t at = 0;
    while (at + 8 <= len) {
        uint32_t size = be32(p + at);
        if (size < 8 || size > len - at) break;
        const uint8_t *name = p + at + 4;
        /* Every value sits in a `data` atom: four bytes of kind, four of
           locale, then the thing itself. */
        const uint8_t *body = p + at + 8;
        size_t body_len = size - 8;
        proven_u8str_view_t value = none();
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
        if (four(name, "\xA9nam")) { if (out->title.size == 0) out->title = value; }
        else if (four(name, "\xA9""ART")) { if (out->artist.size == 0) out->artist = value; }
        else if (four(name, "\xA9""alb")) { if (out->album.size == 0) out->album = value; }
        else if (four(name, "\xA9""day")) { if (out->year.size == 0) out->year = value; }
        else if (four(name, "\xA9gen")) { if (out->genre.size == 0) out->genre = value; }
        else if (four(name, "\xA9lyr")) { if (out->lyrics_text.size == 0) out->lyrics_text = value; }
        else if (four(name, "trkn") && raw && raw_len >= 4 && out->track_number.size == 0) {
            uint32_t number = be16(raw + 2);
            char n[8];
            int wrote = 0;
            if (number >= 100) n[wrote++] = (char)('0' + number / 100 % 10);
            if (number >= 10) n[wrote++] = (char)('0' + number / 10 % 10);
            n[wrote++] = (char)('0' + number % 10);
            out->track_number = arena_bytes(arena, rubraview_bytes_view(n, (size_t)wrote));
        } else if (four(name, "covr") && raw && raw_len > 0 && out->art_size == 0 &&
                   raw_len <= RUBRAVIEW_TAGS_MAX_ART) {
            out->art = raw;
            out->art_size = raw_len;
            /* 13 is JPEG and 14 is PNG; the bytes say so too. */
            out->art_mime = (raw_len > 8 && raw[0] == 0x89 && raw[1] == 'P')
                          ? PROVEN_LIT("image/png") : PROVEN_LIT("image/jpeg");
        }
        at += size;
    }
}

static void mp4_walk(proven_arena_t *arena, proven_mem_view_t p_view, int depth,
                     rubraview_tags_t *out) {
    const uint8_t *p = (const uint8_t *)p_view.ptr;
    size_t len = (size_t)p_view.size;
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
            mp4_walk(arena, rubraview_mem_view(body, body_len), depth + 1, out);
        } else if (four(name, "meta") && body_len > 4) {
            mp4_walk(arena, rubraview_mem_view(body + 4, body_len - 4), depth + 1, out);   /* the version nothing else has */
        } else if (four(name, "ilst")) {
            mp4_ilst(arena, rubraview_mem_view(body, body_len), out);
        } else if (four(name, "mvhd") && body_len >= 20) {
            uint32_t timescale = be32(body + 12);
            uint32_t duration = be32(body + 16);
            if (timescale > 0 && duration > 0 && out->duration_seconds == 0.0) {
                out->duration_seconds = (double)duration / (double)timescale;
            }
        } else if (four(name, "stsd") && body_len >= 36) {
            const uint8_t *entry = body + 8;
            if (four(entry + 4, "mp4a") || four(entry + 4, "alac")) {
                out->codec = four(entry + 4, "alac") ? PROVEN_LIT("ALAC") : PROVEN_LIT("AAC");
                out->channels = be16(entry + 24);
                out->bits_per_sample = be16(entry + 26);
                out->sample_rate = be16(entry + 32);   /* 16.16 fixed point: the whole part */
            }
        }
        at += (size_t)size;
    }
}

/* ---- MP4 chapters (RV-319, R362): Nero's chpl, else the QuickTime chapter track ---- */

#define MP4_MAX_CHAPTERS 2000u

/* The first child box `name` in [p, p + len): its body. */
static bool mp4_child(const uint8_t *p, size_t len, const char *name, const uint8_t **body, size_t *body_len) {
    size_t at = 0;
    while (at + 8 <= len) {
        uint64_t size = be32(p + at);
        size_t header = 8;
        if (size == 1) {
            if (at + 16 > len) return false;
            size = ((uint64_t)be32(p + at + 8) << 32) | be32(p + at + 12);
            header = 16;
        } else if (size == 0) {
            size = len - at;                    /* to the end */
        }
        if (size < header || size > len - at) return false;
        if (four(p + at + 4, name)) { *body = p + at + header; *body_len = (size_t)size - header; return true; }
        at += (size_t)size;
    }
    return false;
}

static bool mp4_path(const uint8_t *p, size_t len, const char *const *names, size_t n, const uint8_t **body, size_t *body_len) {
    for (size_t i = 0; i < n; ++i) {
        if (!mp4_child(p, len, names[i], &p, &len)) return false;
    }
    *body = p; *body_len = len;
    return true;
}

static bool chapters_alloc(proven_arena_t *arena, size_t count, rubraview_tags_t *out) {
    if (count == 0 || count > MP4_MAX_CHAPTERS) return false;
    proven_result_mem_mut_t res = proven_arena_alloc(arena, count * sizeof(rubraview_cue_track_t));
    if (!proven_is_ok(res.err)) return false;
    out->chapters = (rubraview_cue_track_t *)(void *)res.value.ptr;
    out->chapter_count = 0;
    return true;
}

static void chapter_add(rubraview_tags_t *out, double start, proven_u8str_view_t title) {
    if (start < 0.0 || start != start) return;
    if (out->chapter_count > 0 && start < out->chapters[out->chapter_count - 1].start_seconds) return;   /* out of order */
    rubraview_cue_track_t *c = &out->chapters[out->chapter_count];
    *c = (rubraview_cue_track_t){ .number = (int32_t)out->chapter_count + 1, .title = title, .performer = none(),
                                  .start_seconds = start, .end_seconds = 0.0 };
    out->chapter_count++;
}

/* chpl: version and flags, a reserved word when the version is 1, a count byte, then each chapter's start
   (64 bits of 100 ns) and its title (a length byte and UTF-8). */
static void mp4_chpl(proven_arena_t *arena, const uint8_t *p, size_t len, rubraview_tags_t *out) {
    if (len < 5) return;
    size_t at = p[0] == 1 ? 8 : 4;
    if (at + 1 > len) return;
    size_t count = p[at++];
    if (!chapters_alloc(arena, count, out)) return;
    for (size_t k = 0; k < count && at + 9 <= len; ++k) {
        uint64_t start = ((uint64_t)be32(p + at) << 32) | be32(p + at + 4);
        size_t tl = p[at + 8];
        at += 9;
        if (tl > len - at) break;
        chapter_add(out, (double)start / 1e7, trim(arena, p + at, tl));
        at += tl;
    }
    if (out->chapter_count == 0) out->chapters = NULL;
}

/* The chapter track: its samples' times (stts, in mdhd's timescale) and places (stsc, stco or co64, stsz);
   a sample is a 16-bit length and the title (UTF-8, or UTF-16 with its mark). */
static void mp4_qt_chapters(proven_arena_t *arena, const uint8_t *file, size_t file_len,
                            const uint8_t *trak, size_t trak_len, rubraview_tags_t *out) {
    static const char *const MDHD[] = { "mdia", "mdhd" }, *const STBL[] = { "mdia", "minf", "stbl" };
    const uint8_t *mdhd, *stbl, *stts, *stsc, *stsz, *stco;
    size_t mdhd_len, stbl_len, stts_len, stsc_len, stsz_len, stco_len;
    if (!mp4_path(trak, trak_len, MDHD, 2, &mdhd, &mdhd_len) || !mp4_path(trak, trak_len, STBL, 3, &stbl, &stbl_len)) return;
    uint32_t timescale = mdhd_len >= 24 && mdhd[0] == 1 ? be32(mdhd + 20) : mdhd_len >= 16 && mdhd[0] == 0 ? be32(mdhd + 12) : 0;   /* after 64- or 32-bit dates */
    if (timescale == 0) return;
    if (!mp4_child(stbl, stbl_len, "stts", &stts, &stts_len) || !mp4_child(stbl, stbl_len, "stsc", &stsc, &stsc_len) ||
        !mp4_child(stbl, stbl_len, "stsz", &stsz, &stsz_len)) return;
    bool wide = false;
    if (!mp4_child(stbl, stbl_len, "stco", &stco, &stco_len)) {
        if (!mp4_child(stbl, stbl_len, "co64", &stco, &stco_len)) return;
        wide = true;
    }
    if (stts_len < 8 || stsc_len < 8 || stsz_len < 12 || stco_len < 8) return;
    uint32_t samples = be32(stsz + 8), fixed = be32(stsz + 4);
    if (samples == 0 || samples > MP4_MAX_CHAPTERS || (fixed == 0 && (stsz_len - 12) / 4 < samples)) return;
    uint32_t chunks = be32(stco + 4), stsc_n = be32(stsc + 4), stts_n = be32(stts + 4);
    if (chunks > (stco_len - 8) / (wide ? 8u : 4u) || stsc_n == 0 || stsc_n > (stsc_len - 8) / 12 || stts_n > (stts_len - 8) / 8) return;
    if (!chapters_alloc(arena, samples, out)) return;

    uint64_t time = 0;
    uint32_t stts_i = 0, stts_left = stts_n ? be32(stts + 8) : 0, sample = 0;
    for (uint32_t chunk = 1; chunk <= chunks && sample < samples; ++chunk) {
        uint32_t per = 0;                       /* the stsc entry that holds this chunk: the last whose first is <= it */
        for (uint32_t e = 0; e < stsc_n; ++e) {
            if (be32(stsc + 8 + 12 * e) > chunk) break;
            per = be32(stsc + 8 + 12 * e + 4);
        }
        uint64_t off = wide ? ((uint64_t)be32(stco + 8 + 8 * (chunk - 1)) << 32) | be32(stco + 12 + 8 * (chunk - 1))
                            : be32(stco + 8 + 4 * (chunk - 1));
        for (uint32_t k = 0; k < per && sample < samples; ++k, ++sample) {
            uint32_t size = fixed ? fixed : be32(stsz + 12 + 4 * sample);
            proven_u8str_view_t title = none();
            if (off <= (uint64_t)file_len && size >= 2 && size <= file_len - (size_t)off) {
                const uint8_t *s = file + off;
                size_t tl = be16(s);
                if (tl <= size - 2) {
                    if (tl >= 2 && ((s[2] == 0xFE && s[3] == 0xFF) || (s[2] == 0xFF && s[3] == 0xFE)))
                        title = utf16_to_utf8(arena, rubraview_mem_view(s + 2, tl), true);
                    else
                        title = trim(arena, s + 2, tl);
                }
            }
            chapter_add(out, (double)time / (double)timescale, title);
            off += size;
            while (stts_left == 0 && stts_i + 1 < stts_n) { stts_i++; stts_left = be32(stts + 8 + 8 * stts_i); }
            if (stts_left > 0) { time += be32(stts + 12 + 8 * stts_i); stts_left--; }
        }
    }
    if (out->chapter_count == 0) out->chapters = NULL;
}

static void mp4_chapters(proven_arena_t *arena, const uint8_t *p, size_t len, rubraview_tags_t *out) {
    const uint8_t *moov, *b;
    size_t moov_len, b_len;
    if (!mp4_child(p, len, "moov", &moov, &moov_len)) return;
    static const char *const CHPL[] = { "udta", "chpl" };
    if (mp4_path(moov, moov_len, CHPL, 2, &b, &b_len)) mp4_chpl(arena, b, b_len, out);
    if (out->chapter_count > 0) return;
    /* the tracks an audio track's tref/chap names */
    uint32_t chap_ids[8];
    size_t chap_n = 0;
    for (size_t at = 0; at + 8 <= moov_len;) {
        uint32_t size = be32(moov + at);
        if (size < 8 || size > moov_len - at) break;
        if (four(moov + at + 4, "trak")) {
            static const char *const CHAP[] = { "tref", "chap" };
            if (mp4_path(moov + at + 8, size - 8, CHAP, 2, &b, &b_len))
                for (size_t i = 0; i + 4 <= b_len && chap_n < 8; i += 4) chap_ids[chap_n++] = be32(b + i);
        }
        at += size;
    }
    for (size_t at = 0; chap_n > 0 && at + 8 <= moov_len;) {
        uint32_t size = be32(moov + at);
        if (size < 8 || size > moov_len - at) break;
        const uint8_t *tkhd;
        size_t tkhd_len;
        if (four(moov + at + 4, "trak") && mp4_child(moov + at + 8, size - 8, "tkhd", &tkhd, &tkhd_len)) {
            uint32_t id = tkhd_len >= 24 && tkhd[0] == 1 ? be32(tkhd + 20) : tkhd_len >= 16 ? be32(tkhd + 12) : 0;
            for (size_t i = 0; i < chap_n; ++i) {
                if (id != 0 && id == chap_ids[i]) { mp4_qt_chapters(arena, p, len, moov + at + 8, size - 8, out); break; }
            }
            if (out->chapter_count > 0) return;
        }
        at += size;
    }
}

/* ---- WAV ---- */

/* RV-319 (R363): with the whole file there (`whole`, mapped), the chunks after `data` too: a LIST/INFO and an
   "id3 " (or "ID3 ") chunk holding an ID3v2 tag, as tag editors append them. `data` is passed by its size; the
   ID3 tag's values are taken over INFO's (they say their encoding; INFO's are a code page's bytes). */
static void read_wav(proven_arena_t *arena, const uint8_t *p, size_t len, const uint8_t *whole, size_t whole_len,
                     rubraview_tags_t *out) {
    out->codec = PROVEN_LIT("WAV");
    if (whole && whole_len >= len) { p = whole; len = whole_len; }
    size_t at = 12;
    uint32_t tag = 1, fact = 0;   /* R367: a compressed sound's length is its fact chunk's frames */
    while (at + 8 <= len) {
        uint32_t size = le32(p + at + 4);
        const uint8_t *body = p + at + 8;
        /* With the head only, `data` — the whole recording — is nearly always longer than what is held: its
           *declared* length gives the length, and nothing after it is read. */
        if (four(p + at, "data")) {
            if (tag != 1 && tag != 3 && tag != 0xFFFE && fact > 0 && out->sample_rate > 0) {
                out->duration_seconds = (double)fact / (double)out->sample_rate;
            } else if (out->sample_rate > 0 && out->channels > 0 && out->bits_per_sample > 0) {
                double frame = (double)out->channels * (double)out->bits_per_sample / 8.0;
                if (frame > 0.0) out->duration_seconds = (double)size / frame / (double)out->sample_rate;
            }
            if (!whole || size > len - at - 8) break;
            at += 8 + (size_t)size + (size & 1);
            continue;
        }
        if (size > len - at - 8) break;
        if (four(p + at, "fmt ") && size >= 16) {
            out->channels = le16(body + 2);
            out->sample_rate = le32(body + 4);
            out->bits_per_sample = le16(body + 14);
            tag = le16(body);   /* R366: the recorders' ADPCM named as what it is */
            if (tag == 0x0011) out->codec = PROVEN_LIT("IMA ADPCM");
            else if (tag == 0x0002) out->codec = PROVEN_LIT("MS ADPCM");
            uint32_t bytes_per_second = le32(body + 8);
            rubraview_adpcm_t a;   /* R368: an encoder may write a PCM's byte rate there; the blocks say the real one */
            if ((tag == 0x0011 || tag == 0x0002) && rubraview_adpcm_from_fmt(body, size, &a) && a.frames_per_block)
                bytes_per_second = (uint32_t)((uint64_t)a.block_align * a.rate / a.frames_per_block);
            if (bytes_per_second > 0) out->bitrate_kbps = bytes_per_second * 8 / 1000;
        } else if (four(p + at, "fact") && size >= 4) {
            fact = le32(body);
        } else if (four(p + at, "LIST") && size >= 4 && four(body, "INFO")) {
            size_t in = 4;
            while (in + 8 <= size) {
                uint32_t item = le32(body + in + 4);
                if (item > size - in - 8) break;
                const uint8_t *text = body + in + 8;
                if (four(body + in, "INAM") && out->title.size == 0) out->title = trim(arena, text, item);
                else if (four(body + in, "IART") && out->artist.size == 0) out->artist = trim(arena, text, item);
                else if (four(body + in, "IPRD") && out->album.size == 0) out->album = trim(arena, text, item);
                else if (four(body + in, "ICRD") && out->year.size == 0) out->year = trim(arena, text, item);
                else if (four(body + in, "IGNR") && out->genre.size == 0) out->genre = trim(arena, text, item);
                else if ((four(body + in, "ITRK") || four(body + in, "IPRT")) && out->track_number.size == 0)
                    out->track_number = trim(arena, text, item);
                in += 8 + (size_t)item + (item & 1);
            }
        } else if ((four(p + at, "id3 ") || four(p + at, "ID3 ")) && size >= 10) {
            size_t end = 0;
            read_id3v2(arena, rubraview_mem_view(body, size), out, &end);
        }
        at += 8 + (size_t)size + (size & 1);           /* chunks are padded to an even length */
    }
}

/* ---- R367 (RV-319): the songs FFmpeg plays, and AIFF and Wave64: what each says of itself ----
   Before these, any file that was none of the kinds above was scanned for MPEG frames, and the stray 0xFFE bytes
   of a PCM or a lossless stream were taken for one ("MP3 · 160 kbps" on an AIFF). */

static uint64_t be64(const uint8_t *p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }
static uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p + 4) << 32 | le32(p); }

static void set_length(rubraview_tags_t *out, uint64_t frames) {
    if (out->sample_rate > 0 && frames > 0) out->duration_seconds = (double)frames / (double)out->sample_rate;
}

/* AIFF and AIFF-C: FORM, then chunks of a big-endian size, padded to even; COMM says the sound, "ID3 " the tags */
static void read_aiff(proven_arena_t *arena, const uint8_t *p, size_t len, const uint8_t *whole, size_t whole_len,
                      rubraview_tags_t *out) {
    out->codec = PROVEN_LIT("AIFF");
    if (whole && whole_len >= len) { p = whole; len = whole_len; }
    for (size_t at = 12; at + 8 <= len;) {
        uint32_t size = be32(p + at + 4);
        const uint8_t *body = p + at + 8;
        bool fits = size <= len - at - 8;
        if (four(p + at, "COMM") && fits && size >= 18) {
            out->channels = be16(body);
            out->bits_per_sample = be16(body + 6);
            int e = (int)(be16(body + 8) & 0x7FFF) - 16383;   /* the rate, an 80-bit float */
            uint64_t m = be64(body + 10);
            out->sample_rate = e >= 0 && e < 32 ? (uint32_t)(m >> (63 - e)) : 0;
            set_length(out, be32(body + 2));
            if (out->sample_rate && out->channels && out->bits_per_sample)
                out->bitrate_kbps = (uint32_t)(((uint64_t)out->sample_rate * out->channels * out->bits_per_sample + 500) / 1000);
        } else if ((four(p + at, "ID3 ") || four(p + at, "id3 ")) && fits && size >= 10) {
            size_t end = 0;
            read_id3v2(arena, rubraview_mem_view(body, size), out, &end);
        } else if (!fits) {
            break;   /* the sound, longer than what is held; what follows is not read */
        }
        at += 8 + (size_t)size + (size & 1u);
    }
}

/* Wave64: its fmt as a WAVE's */
static void read_w64(const uint8_t *p, size_t len, uint64_t file_size, rubraview_tags_t *out) {
    out->codec = PROVEN_LIT("W64");
    rubraview_wave_chunks_t c;
    if (!rubraview_media_wave_chunks(p, len, file_size ? file_size : len, &c)) return;
    out->channels = le16(c.fmt + 2);
    out->sample_rate = le32(c.fmt + 4);
    out->bits_per_sample = le16(c.fmt + 14);
    uint32_t bytes_per_second = le32(c.fmt + 8), align = le16(c.fmt + 12);
    if (bytes_per_second > 0) out->bitrate_kbps = bytes_per_second * 8 / 1000;
    if (align) set_length(out, c.data_len / align);
}

/* WavPack: a block's 32-byte header: its samples and the flags' bytes a sample, mono and rate */
static void read_wavpack(const uint8_t *p, size_t len, rubraview_tags_t *out) {
    static const uint32_t RATES[15] = { 6000, 8000, 9600, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 64000, 88200, 96000, 192000 };
    out->codec = PROVEN_LIT("WavPack");
    if (len < 32) return;
    uint32_t flags = le32(p + 24), total = le32(p + 12), ri = (flags >> 23) & 15u;
    out->bits_per_sample = (flags & 3u) * 8u + 8u;
    out->channels = (flags & 4u) ? 1u : 2u;
    out->sample_rate = ri < 15 ? RATES[ri] : 0;
    if (total != 0xFFFFFFFFu) set_length(out, total);
}

/* TTA: "TTA1", format, channels, bits, rate, samples */
static void read_tta(const uint8_t *p, size_t len, rubraview_tags_t *out) {
    out->codec = PROVEN_LIT("TTA");
    if (len < 18) return;
    out->channels = le16(p + 6);
    out->bits_per_sample = le16(p + 8);
    out->sample_rate = le32(p + 10);
    set_length(out, le32(p + 14));
}

/* Monkey's Audio: "MAC ", its version; from 3.98 a descriptor, then the header */
static void read_ape(const uint8_t *p, size_t len, rubraview_tags_t *out) {
    out->codec = PROVEN_LIT("APE");
    if (len < 8) return;
    uint32_t version = le16(p + 4);
    if (version >= 3980) {
        uint32_t desc = len >= 12 ? le32(p + 8) : 0;
        if (desc < 52 || desc > len || len - desc < 24) return;
        const uint8_t *h = p + desc;
        uint32_t per_frame = le32(h + 4), last = le32(h + 8), frames = le32(h + 12);
        out->bits_per_sample = le16(h + 16);
        out->channels = le16(h + 18);
        out->sample_rate = le32(h + 20);
        if (frames) set_length(out, (uint64_t)(frames - 1) * per_frame + last);
    } else if (len >= 32) {
        out->channels = le16(p + 10);
        out->sample_rate = le32(p + 12);
        out->bits_per_sample = (le16(p + 8) & 1u) ? 8u : (le16(p + 8) & 8u) ? 24u : 16u;
        uint32_t frames = le32(p + 24), last = le32(p + 28);
        uint32_t per_frame = version >= 3950 ? 73728u * 4u : version >= 3900 ? 73728u : 9216u;
        if (frames) set_length(out, (uint64_t)(frames - 1) * per_frame + last);
    }
}

/* DSF: "DSD " chunk, then "fmt ": channels, the 1-bit rate, the samples; the ID3v2 tag where the header points */
static void read_dsf(proven_arena_t *arena, const uint8_t *p, size_t len, const uint8_t *whole, size_t whole_len,
                     rubraview_tags_t *out) {
    out->codec = PROVEN_LIT("DSD");
    if (len < 80 || !four(p + 28, "fmt ")) return;
    out->channels = le32(p + 52);
    out->sample_rate = le32(p + 56);
    out->bits_per_sample = 1;
    set_length(out, le64(p + 64));
    uint64_t meta = le64(p + 20);
    if (whole && meta > 0 && meta < whole_len && whole_len - meta >= 10) {
        size_t end = 0;
        read_id3v2(arena, rubraview_mem_view(whole + meta, (size_t)(whole_len - meta)), out, &end);
    }
}

/* DSDIFF: "FRM8", 8-byte big-endian sizes; PROP's "SND " holds FS (the rate) and CHNL (the channels) */
static void read_dff(const uint8_t *p, size_t len, rubraview_tags_t *out) {
    out->codec = PROVEN_LIT("DSD");
    out->bits_per_sample = 1;
    for (size_t at = 16; at + 12 <= len;) {
        uint64_t size = be64(p + at + 4);
        if (size > len - at - 12) size = len - at - 12;
        if (four(p + at, "PROP") && size >= 4 && four(p + at + 12, "SND ")) {
            for (size_t in = at + 16; in + 12 <= at + 12 + size;) {
                uint64_t n = be64(p + in + 4);
                if (n > at + 12 + size - in - 12) break;
                if (four(p + in, "FS  ") && n >= 4) out->sample_rate = be32(p + in + 12);
                else if (four(p + in, "CHNL") && n >= 2) out->channels = be16(p + in + 12);
                in += 12 + (size_t)n + (size_t)(n & 1u);
            }
        }
        if (four(p + at, "DSD ") && out->sample_rate && out->channels) set_length(out, be64(p + at + 4) * 8u / out->channels);
        at += 12 + (size_t)size + (size_t)(size & 1u);
    }
}

/* Matroska sound: the first track's CodecID ("A_FLAC" and such, element 0x86) names it */
static void read_mka(const uint8_t *p, size_t len, rubraview_tags_t *out) {
    static const struct { const char *id; const char *name; } NAMES[] = {
        { "A_FLAC", "FLAC" }, { "A_OPUS", "Opus" }, { "A_VORBIS", "Vorbis" }, { "A_AAC", "AAC" }, { "A_MPEG/L3", "MP3" },
        { "A_AC3", "AC-3" }, { "A_EAC3", "E-AC-3" }, { "A_DTS", "DTS" }, { "A_TRUEHD", "TrueHD" }, { "A_PCM", "PCM" },
        { "A_ALAC", "ALAC" }, { "A_WAVPACK4", "WavPack" }, { "A_TTA1", "TTA" },
    };
    out->codec = PROVEN_LIT("Matroska");
    size_t limit = len < 65536 ? len : 65536;
    for (size_t i = 0; i + 4 < limit; ++i) {
        if (p[i] != 0x86 || (p[i + 1] & 0x80) == 0) continue;
        size_t n = p[i + 1] & 0x7Fu;
        if (n < 3 || i + 2 + n > limit || p[i + 2] != 'A' || p[i + 3] != '_') continue;
        for (size_t k = 0; k < sizeof NAMES / sizeof NAMES[0]; ++k) {
            size_t m = proven_cstr_len(NAMES[k].id);
            if (n >= m && proven_sys_mem_cmp(p + i + 2, NAMES[k].id, m) == 0) { out->codec = proven_u8str_view_from_cstr(NAMES[k].name); return; }
        }
        return;
    }
}

/* ---- R368 (RV-319): APEv2, at the end of a Monkey's Audio, WavPack, TTA or TAK file (and of some MP3s) ----
   A 32-byte footer ("APETAGEX", version, the tag's size counting the items and the footer, the items' count),
   before an ID3v1 tag when there is one; each item its value's size, flags, a key ended by NUL and the value:
   UTF-8 text (several values NUL-separated: the first), or binary — "Cover Art (Front)" a file name ended by NUL,
   then the picture. A field another tag of the file already gave is kept. */
static void read_apev2(proven_arena_t *arena, const uint8_t *p, size_t len, rubraview_tags_t *out) {
    if (!p || len < 32) return;
    size_t foot = len - 32;
    if (proven_sys_mem_cmp(p + foot, "APETAGEX", 8) != 0) {
        if (len < 160 || proven_sys_mem_cmp(p + len - 128, "TAG", 3) != 0) return;   /* behind an ID3v1 tag */
        foot = len - 160;
        if (proven_sys_mem_cmp(p + foot, "APETAGEX", 8) != 0) return;
    }
    uint32_t size = le32(p + foot + 12), count = le32(p + foot + 16);
    if (size < 32 || size > foot + 32) return;
    size_t at = foot + 32 - size, end = foot;
    bool front = false;
    for (uint32_t k = 0; k < count && k < 1000 && at + 8 < end; ++k) {
        uint32_t vsize = le32(p + at), flags = le32(p + at + 4);
        size_t key = at + 8, kend = key;
        while (kend < end && p[kend] != 0) kend++;
        if (kend >= end || kend - key < 2 || kend - key > 255 || vsize > end - kend - 1) return;
        proven_u8str_view_t name = { .ptr = (const proven_byte_t *)(p + key), .size = kend - key };
        const uint8_t *v = p + kend + 1;
        at = kend + 1 + vsize;
        uint32_t kind = (flags >> 1) & 3u;
        if (kind == 1) {   /* binary: a cover, the front first */
            bool is_front = same_ci(name, "COVER ART (FRONT)");
            if (!is_front && !same_ci(name, "COVER ART (OTHER)")) continue;
            if (out->art_size > 0 && (front || !is_front)) continue;
            size_t n = 0;
            while (n < vsize && v[n] != 0) n++;
            if (n >= vsize) continue;
            const uint8_t *img = v + n + 1;
            size_t img_len = vsize - n - 1;
            bool png = img_len > 8 && img[0] == 0x89 && img[1] == 'P';
            bool jpeg = img_len > 3 && img[0] == 0xFF && img[1] == 0xD8;
            if ((!png && !jpeg) || img_len > RUBRAVIEW_TAGS_MAX_ART) continue;
            out->art = img;
            out->art_size = img_len;
            out->art_mime = png ? PROVEN_LIT("image/png") : PROVEN_LIT("image/jpeg");
            front = is_front;
            continue;
        }
        if (kind != 0) continue;
        size_t first = 0;
        while (first < vsize && v[first] != 0) first++;   /* the first of several values */
        proven_u8str_view_t text = { .ptr = (const proven_byte_t *)v, .size = first };
        if (same_ci(name, "TITLE") && out->title.size == 0) out->title = trim(arena, v, first);
        else if (same_ci(name, "ARTIST") && out->artist.size == 0) out->artist = trim(arena, v, first);
        else if (same_ci(name, "ALBUM") && out->album.size == 0) out->album = trim(arena, v, first);
        else if ((same_ci(name, "YEAR") || same_ci(name, "DATE")) && out->year.size == 0) out->year = trim(arena, v, first);
        else if (same_ci(name, "TRACK") && out->track_number.size == 0) out->track_number = trim(arena, v, first);
        else if (same_ci(name, "GENRE") && out->genre.size == 0) out->genre = trim(arena, v, first);
        else if ((same_ci(name, "LYRICS") || same_ci(name, "UNSYNCEDLYRICS")) && out->lyrics_text.size == 0)
            out->lyrics_text = trim(arena, v, vsize);
        else replaygain_value(out, name, text);
    }
}

/* ---- ADTS AAC (a bare .aac): each frame's 7-byte header says the rate, the channels and its own length ----
   RV-319 (D-400): read before the MP3 scan, which took an ADTS file's stray 0xFFE bytes for MP3 frames. */

static bool read_adts(const uint8_t *p, size_t len, size_t from, rubraview_tags_t *out) {
    static const uint32_t RATES[13] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };
    if (from + 7 > len || p[from] != 0xFF || (p[from + 1] & 0xF6) != 0xF0) return false;   /* sync, layer 0 */
    uint32_t rate_index = (uint32_t)(p[from + 2] >> 2) & 0x0F;
    uint32_t channels = ((uint32_t)(p[from + 2] & 0x01) << 2) | (uint32_t)(p[from + 3] >> 6);
    if (rate_index >= 13) return false;
    /* walk the frames the head holds; a stream is two frames that chain, or one that ends the head */
    size_t at = from, frames = 0, bytes = 0;
    while (at + 7 <= len && p[at] == 0xFF && (p[at + 1] & 0xF6) == 0xF0) {
        size_t frame = ((size_t)(p[at + 3] & 0x03) << 11) | ((size_t)p[at + 4] << 3) | (size_t)(p[at + 5] >> 5);
        if (frame < 7) break;
        frames++;
        bytes += frame;
        at += frame;
    }
    if (frames == 0 || (frames == 1 && at < len)) return false;
    out->codec = PROVEN_LIT("AAC");
    out->sample_rate = RATES[rate_index];
    out->channels = channels;   /* 0: said in the stream, not the header */
    /* 1024 samples to a frame */
    double kbps = (double)bytes / (double)frames * 8.0 * (double)out->sample_rate / 1024.0 / 1000.0;
    if (kbps > 0.0 && kbps < 100000.0) out->bitrate_kbps = (uint32_t)(kbps + 0.5);
    return true;
}

/* ---- ASF (.wma): the header object's File Properties and the audio stream's WAVEFORMATEX; the tags of the
   Content Description, the Extended Content Description and the Header Extension's Metadata Library
   (RV-319, R361: what Windows Media Player writes) ---- */

static const uint8_t ASF_HEADER[16] = { 0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11, 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C };
static const uint8_t ASF_FILE_PROPS[16] = { 0xA1, 0xDC, 0xAB, 0x8C, 0x47, 0xA9, 0xCF, 0x11, 0x8E, 0xE4, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 };
static const uint8_t ASF_STREAM_PROPS[16] = { 0x91, 0x07, 0xDC, 0xB7, 0xB7, 0xA9, 0xCF, 0x11, 0x8E, 0xE6, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 };
static const uint8_t ASF_AUDIO_MEDIA[16] = { 0x40, 0x9E, 0x69, 0xF8, 0x4D, 0x5B, 0xCF, 0x11, 0xA8, 0xFD, 0x00, 0x80, 0x5F, 0x5C, 0x44, 0x2B };
static const uint8_t ASF_CONTENT_DESC[16] = { 0x33, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11, 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C };
static const uint8_t ASF_EXT_CONTENT[16] = { 0x40, 0xA4, 0xD0, 0xD2, 0x07, 0xE3, 0xD2, 0x11, 0x97, 0xF0, 0x00, 0xA0, 0xC9, 0x5E, 0xA8, 0x50 };
static const uint8_t ASF_HEADER_EXT[16] = { 0xB5, 0x03, 0xBF, 0x5F, 0x2E, 0xA9, 0xCF, 0x11, 0x8E, 0xE3, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 };
static const uint8_t ASF_METADATA_LIB[16] = { 0x94, 0x1C, 0x23, 0x44, 0x98, 0x94, 0xD1, 0x49, 0xA1, 0x41, 0x1D, 0x13, 0x4E, 0x45, 0x70, 0x54 };


/* Is this UTF-16LE name, its NUL left off, the ASCII `want`? */
static bool asf_name_is(const uint8_t *p, size_t len, const char *want) {
    size_t n = proven_cstr_len(want);
    while (len >= 2 && p[len - 2] == 0 && p[len - 1] == 0) len -= 2;
    if (len != n * 2) return false;
    for (size_t i = 0; i < n; ++i) {
        uint8_t c = p[2 * i];
        if (p[2 * i + 1] != 0) return false;
        if (c >= 'a' && c <= 'z') c = (uint8_t)(c - 'a' + 'A');
        uint8_t w = (uint8_t)want[i];
        if (w >= 'a' && w <= 'z') w = (uint8_t)(w - 'a' + 'A');
        if (c != w) return false;
    }
    return true;
}

static proven_u8str_view_t asf_number(proven_arena_t *arena, uint64_t n) {
    char text[24];
    size_t at = sizeof text;
    do { text[--at] = (char)('0' + n % 10); n /= 10; } while (n > 0 && at > 0);
    return arena_bytes(arena, rubraview_bytes_view(text + at, sizeof text - at));
}

/* A value as text: a string (type 0) as it is, a DWORD (3), QWORD (4) or WORD (5) as its number. */
static proven_u8str_view_t asf_value_text(proven_arena_t *arena, uint16_t type, const uint8_t *v, size_t len, uint64_t add) {
    if (type == 0) return utf16_to_utf8(arena, rubraview_mem_view(v, len), false);
    if (type == 3 && len >= 4) return asf_number(arena, (uint64_t)le32(v) + add);
    if (type == 4 && len >= 8) return asf_number(arena, le64(v) + add);
    if (type == 5 && len >= 2) return asf_number(arena, (uint64_t)le16(v) + add);
    return none();
}

/* WM/Picture: the picture's type, the data's length, MIME and description (UTF-16, NUL-ended), the data.
   A front cover (type 3) is taken over any other; the first picture when none is. */
static void asf_picture(const uint8_t *v, size_t len, rubraview_tags_t *out, bool *front) {
    if (len < 5 || *front) return;
    uint8_t kind = v[0];
    uint32_t size = le32(v + 1);
    size_t at = 5;
    while (at + 1 < len && (v[at] | v[at + 1])) at += 2;   /* past the MIME type; the bytes say which */
    at += 2;
    while (at + 1 < len && (v[at] | v[at + 1])) at += 2;
    at += 2;
    if (at > len || size == 0 || size > len - at || size > RUBRAVIEW_TAGS_MAX_ART) return;
    if (out->art_size > 0 && kind != 3) return;
    const uint8_t *data = v + at;
    bool png = size > 8 && data[0] == 0x89 && data[1] == 'P';
    bool jpeg = size > 3 && data[0] == 0xFF && data[1] == 0xD8;
    if (!png && !jpeg) return;   /* the image loader is told JPEG or PNG, as with every other format */
    out->art = data;
    out->art_size = size;
    out->art_mime = png ? PROVEN_LIT("image/png") : PROVEN_LIT("image/jpeg");
    if (kind == 3) *front = true;
}

/* One named value, from the Extended Content Description or the Metadata Library. */
static void asf_tag(proven_arena_t *arena, const uint8_t *name, size_t name_len, uint16_t type,
                    const uint8_t *v, size_t len, rubraview_tags_t *out, bool *front, bool *track_set) {
    if (asf_name_is(name, name_len, "WM/AlbumTitle")) { if (out->album.size == 0) out->album = asf_value_text(arena, type, v, len, 0); }
    else if (asf_name_is(name, name_len, "WM/Year")) { if (out->year.size == 0) out->year = asf_value_text(arena, type, v, len, 0); }
    else if (asf_name_is(name, name_len, "WM/Genre")) { if (out->genre.size == 0) out->genre = asf_value_text(arena, type, v, len, 0); }
    else if (asf_name_is(name, name_len, "WM/AlbumArtist")) { if (out->artist.size == 0) out->artist = asf_value_text(arena, type, v, len, 0); }
    else if (asf_name_is(name, name_len, "WM/Lyrics")) { if (out->lyrics_text.size == 0 && type == 0) out->lyrics_text = asf_value_text(arena, type, v, len, 0); }
    else if (asf_name_is(name, name_len, "WM/TrackNumber")) {   /* 1-based; it wins over WM/Track */
        proven_u8str_view_t t = asf_value_text(arena, type, v, len, 0);
        if (t.size > 0 && !*track_set) { out->track_number = t; *track_set = true; }
    } else if (asf_name_is(name, name_len, "WM/Track")) {        /* the older, 0-based one */
        if (out->track_number.size == 0) out->track_number = asf_value_text(arena, type, v, len, type == 0 ? 0 : 1);
    } else if (asf_name_is(name, name_len, "WM/Picture")) {
        if (type == 1) asf_picture(v, len, out, front);
    } else if (type == 0) {   /* REPLAYGAIN_TRACK_GAIN and the rest, as foobar2000 and others write them */
        proven_u8str_view_t n = utf16_to_utf8(arena, rubraview_mem_view(name, name_len), false);
        if (n.size > 11 && same_ci((proven_u8str_view_t){ n.ptr, 11 }, "REPLAYGAIN_"))
            replaygain_value(out, n, asf_value_text(arena, type, v, len, 0));
    }
}

static void asf_content_desc(proven_arena_t *arena, const uint8_t *o, size_t size, rubraview_tags_t *out) {
    if (size < 34) return;
    size_t lens[5], at = 34;
    for (int i = 0; i < 5; ++i) lens[i] = le16(o + 24 + 2 * i);
    for (int i = 0; i < 5; ++i) {   /* title, author, copyright, description, rating */
        if (lens[i] > size - at) return;
        proven_u8str_view_t text = utf16_to_utf8(arena, rubraview_mem_view(o + at, lens[i]), false);
        if (i == 0 && text.size > 0) out->title = text;
        else if (i == 1 && text.size > 0) out->artist = text;
        at += lens[i];
    }
}

static void asf_ext_content(proven_arena_t *arena, const uint8_t *o, size_t size, rubraview_tags_t *out, bool *front, bool *track_set) {
    if (size < 26) return;
    uint32_t count = le16(o + 24);
    size_t at = 26;
    for (uint32_t k = 0; k < count && at + 2 <= size; ++k) {
        size_t name_len = le16(o + at);
        if (name_len > size - at - 2 || size - at - 2 - name_len < 4) return;
        const uint8_t *name = o + at + 2;
        at += 2 + name_len;
        uint16_t type = (uint16_t)le16(o + at);
        size_t len = le16(o + at + 2);
        at += 4;
        if (len > size - at) return;
        asf_tag(arena, name, name_len, type, o + at, len, out, front, track_set);
        at += len;
    }
}

/* The Metadata Library: language and stream, the name's length, the type, a 32-bit data length, name, data. */
static void asf_metadata_lib(proven_arena_t *arena, const uint8_t *o, size_t size, rubraview_tags_t *out, bool *front, bool *track_set) {
    if (size < 26) return;
    uint32_t count = le16(o + 24);
    size_t at = 26;
    for (uint32_t k = 0; k < count && at + 12 <= size; ++k) {
        size_t name_len = le16(o + at + 4);
        uint16_t type = (uint16_t)le16(o + at + 6);
        uint32_t len = le32(o + at + 8);
        at += 12;
        if (name_len > size - at || len > size - at - name_len) return;
        asf_tag(arena, o + at, name_len, type, o + at + name_len, len, out, front, track_set);
        at += name_len + len;
    }
}

static void read_asf(proven_arena_t *arena, const uint8_t *p, size_t len, rubraview_tags_t *out) {
    out->codec = PROVEN_LIT("WMA");
    if (len < 30) return;
    uint64_t header = le64(p + 16);
    size_t end = header < (uint64_t)len ? (size_t)header : len;
    bool front = false, track_set = false;
    for (size_t at = 30; at + 24 <= end;) {
        uint64_t size = le64(p + at + 16);
        if (size < 24 || size > (uint64_t)(end - at)) break;
        const uint8_t *o = p + at;
        if (proven_sys_mem_cmp(o, ASF_FILE_PROPS, 16) == 0 && size >= 88) {
            uint64_t play = le64(o + 64), preroll = le64(o + 80);   /* 100 ns; ms */
            double seconds = (double)play / 1e7 - (double)preroll / 1000.0;
            if (seconds > 0.0) out->duration_seconds = seconds;
        } else if (proven_sys_mem_cmp(o, ASF_STREAM_PROPS, 16) == 0 && size >= 78 + 16 &&
                   proven_sys_mem_cmp(o + 24, ASF_AUDIO_MEDIA, 16) == 0 && out->sample_rate == 0) {
            const uint8_t *wf = o + 78;                              /* WAVEFORMATEX */
            out->channels = le16(wf + 2);
            out->sample_rate = le32(wf + 4);
            uint32_t bytes_per_second = le32(wf + 8);
            if (bytes_per_second > 0) out->bitrate_kbps = (bytes_per_second * 8 + 500) / 1000;
        } else if (proven_sys_mem_cmp(o, ASF_CONTENT_DESC, 16) == 0) {
            asf_content_desc(arena, o, (size_t)size, out);
        } else if (proven_sys_mem_cmp(o, ASF_EXT_CONTENT, 16) == 0) {
            asf_ext_content(arena, o, (size_t)size, out, &front, &track_set);
        } else if (proven_sys_mem_cmp(o, ASF_HEADER_EXT, 16) == 0 && size >= 46) {
            size_t inner = le32(o + 42);
            if (inner > (size_t)size - 46) inner = (size_t)size - 46;
            for (size_t in = 0; in + 24 <= inner;) {               /* the objects inside it */
                const uint8_t *e = o + 46 + in;
                uint64_t esize = le64(e + 16);
                if (esize < 24 || esize > (uint64_t)(inner - in)) break;
                if (proven_sys_mem_cmp(e, ASF_METADATA_LIB, 16) == 0) asf_metadata_lib(arena, e, (size_t)esize, out, &front, &track_set);
                in += (size_t)esize;
            }
        }
        at += (size_t)size;
    }
}

/* ---- MP3: the first frame header says the rest ---- */

static void read_mp3_frame(proven_mem_view_t p_view, size_t from, rubraview_tags_t *out) {
    const uint8_t *p = (const uint8_t *)p_view.ptr;
    size_t len = (size_t)p_view.size;
    static const uint32_t BITRATE_V1_L3[16] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 };
    static const uint32_t BITRATE_V2_L3[16] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 };
    static const uint32_t RATE_V1[4] = { 44100, 48000, 32000, 0 };

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
        out->codec = PROVEN_LIT("MP3");   /* R367: only when a frame is there */
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
    read_id3v2(arena, rubraview_mem_view(p, len), &out, &after_id3);       /* MP3 and often FLAC too */

    if (four(p, "fLaC")) {
        read_flac(arena, p, len, &out);
    } else if (four(p, "OggS")) {
        read_ogg(arena, p, len, &out);
    } else if (len > 12 && four(p + 4, "ftyp")) {
        /* RV-319: the whole file when there is one: an audiobook's moov is often after its sound, and its chapter
           titles are samples anywhere in it (the boxes are walked by their sizes, so little of it is read) */
        const uint8_t *mp = source.whole && source.whole_size >= len ? source.whole : p;
        size_t mlen = source.whole && source.whole_size >= len ? source.whole_size : len;
        mp4_walk(arena, rubraview_mem_view(mp, mlen), 0, &out);
        if (out.codec.size == 0) out.codec = PROVEN_LIT("AAC");
        mp4_chapters(arena, mp, mlen, &out);   /* RV-319: an audiobook's */
        for (size_t i = 0; i < out.chapter_count; ++i)
            out.chapters[i].end_seconds = i + 1 < out.chapter_count ? out.chapters[i + 1].start_seconds : out.duration_seconds;
    } else if (four(p, "RIFF") && len > 12 && four(p + 8, "WAVE")) {
        read_wav(arena, p, len, source.whole, source.whole_size, &out);
    } else if (len >= 16 && proven_sys_mem_cmp(p, ASF_HEADER, 16) == 0) {
        read_asf(arena, p, len, &out);
    } else if (four(p, "FORM") && (four(p + 8, "AIFF") || four(p + 8, "AIFC"))) {   /* R367 */
        read_aiff(arena, p, len, source.whole, source.whole_size, &out);
    } else if (rubraview_media_w64_is(p, len)) {
        read_w64(p, len, source.file_size, &out);
    } else if (four(p, "wvpk")) {
        read_wavpack(p, len, &out);
    } else if (four(p, "TTA1")) {
        read_tta(p, len, &out);
    } else if (four(p, "MAC ")) {
        read_ape(p, len, &out);
    } else if (four(p, "tBaK")) {
        out.codec = PROVEN_LIT("TAK");
    } else if (four(p, "DSD ")) {
        read_dsf(arena, p, len, source.whole, source.whole_size, &out);
    } else if (four(p, "FRM8") && four(p + 12, "DSD ")) {
        read_dff(p, len, &out);
    } else if (p[0] == 0x1A && p[1] == 0x45 && p[2] == 0xDF && p[3] == 0xA3) {
        read_mka(p, len, &out);
    } else if (four(p, ".ra\xFD") || four(p, ".RMF")) {
        out.codec = PROVEN_LIT("RealAudio");
    } else if (!read_adts(p, len, after_id3 < len ? after_id3 : 0, &out)) {
        read_mp3_frame(rubraview_mem_view(p, len), after_id3 < len ? after_id3 : 0, &out);
    }

    if (source.whole && source.whole_size >= 32) read_apev2(arena, source.whole, source.whole_size, &out);   /* R368 */
    read_id3v1(arena, rubraview_mem_view(source.tail, source.tail_size), &out);

    /* When nothing measured it, the file's own size and length do. */
    if (out.bitrate_kbps == 0 && out.duration_seconds > 0.0 && source.file_size > 0) {
        double kbps = (double)source.file_size * 8.0 / out.duration_seconds / 1000.0;
        if (kbps > 0.0 && kbps < 100000.0) out.bitrate_kbps = (uint32_t)(kbps + 0.5);
    }
    return out;
}

/* RV-319 (D-398): the rows of audio_exts.h, dotted */
#define DOTTED_ROW(ext, opens, shell, lossless) "." ext,
static const char *const AUDIO_DOTTED[] = { RUBRAVIEW_AUDIO_EXTS(DOTTED_ROW) };
#undef DOTTED_ROW

bool rubraview_tags_is_music_name(proven_u8str_view_t filename) {
    proven_u8str_view_t ext = rubraview_path_ext(filename);
    if (ext.size == 0) return false;
    for (size_t i = 0; i < RUBRAVIEW_AUDIO_EXT_COUNT; ++i) {
        if (proven_u8str_view_eq_ascii_ci(ext, proven_u8str_view_from_cstr(AUDIO_DOTTED[i]))) return true;
    }
    return false;
}

const char *rubraview_tags_cue_sibling_ext(size_t i) {
    for (int lossless = 1; lossless >= 0; --lossless) {
        for (size_t r = 0; r < RUBRAVIEW_AUDIO_EXT_COUNT; ++r) {
            if (RUBRAVIEW_AUDIO_EXT_TABLE[r].lossless != (bool)lossless) continue;
            if (i-- == 0) return AUDIO_DOTTED[r];
        }
    }
    return NULL;
}
