#include "rubraview/mp4.h"
#include "rubraview/lang.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FOURCC(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint64_t be64(const uint8_t *p) { return ((uint64_t)be32(p) << 32) | be32(p + 4); }

typedef struct box {
    uint32_t type;
    uint64_t body;   /* where its content begins */
    uint64_t end;    /* where the box after it begins */
} box_t;

/* A box's header at `at` inside [at, limit): 32-bit size, or 1 and a 64-bit one, or 0 for "to the end". */
static bool box_at(const rubraview_mp4_t *mp4, uint64_t at, uint64_t limit, box_t *out) {
    if (limit > mp4->size) limit = mp4->size;
    if (at >= limit || limit - at < 8) return false;
    const uint8_t *p = mp4->data + at;
    uint64_t size = be32(p), header = 8;
    out->type = be32(p + 4);
    if (size == 1) {
        if (limit - at < 16) return false;
        size = be64(p + 8);
        header = 16;
    } else if (size == 0) {
        size = limit - at;
    }
    if (size < header || size > limit - at) return false;
    out->body = at + header;
    out->end = at + size;
    return true;
}

/* A FullBox's content after its version and flags; false when there is not room for them and `need` more. */
static bool full_body(const box_t *b, uint64_t need, uint64_t *out_at, uint8_t *out_version, const rubraview_mp4_t *mp4) {
    if (b->end - b->body < 4 + need) return false;
    *out_version = mp4->data[b->body];
    *out_at = b->body + 4;
    return true;
}

/* A table of `count` entries of `entry` bytes from `at`, inside the box: false when it claims more than the box holds. */
static bool table_fits(const box_t *b, uint64_t at, uint32_t count, uint64_t entry) {
    uint64_t bytes = 0;
    if (PROVEN_CKD_MUL(&bytes, (uint64_t)count, entry)) return false;
    return at <= b->end && bytes <= b->end - at;
}

static void read_stbl(const rubraview_mp4_t *mp4, const box_t *stbl, rubraview_mp4_track_t *t) {
    box_t b;
    for (uint64_t at = stbl->body; at < stbl->end && box_at(mp4, at, stbl->end, &b); at = b.end) {
        uint64_t p = 0;
        uint8_t version = 0;
        switch (b.type) {
            case FOURCC('s', 't', 's', 'd'):
                /* entry count, then the first entry: its size and its four letters */
                if (full_body(&b, 12, &p, &version, mp4) && be32(mp4->data + p) > 0) t->format = be32(mp4->data + p + 8);
                break;
            case FOURCC('s', 't', 't', 's'):
                if (full_body(&b, 4, &p, &version, mp4) && table_fits(&b, p + 4, be32(mp4->data + p), 8)) {
                    t->stts_count = be32(mp4->data + p);
                    t->stts_at = p + 4;
                }
                break;
            case FOURCC('s', 't', 's', 'c'):
                if (full_body(&b, 4, &p, &version, mp4) && table_fits(&b, p + 4, be32(mp4->data + p), 12)) {
                    t->stsc_count = be32(mp4->data + p);
                    t->stsc_at = p + 4;
                }
                break;
            case FOURCC('s', 't', 's', 'z'):
                if (full_body(&b, 8, &p, &version, mp4)) {
                    uint32_t one = be32(mp4->data + p), count = be32(mp4->data + p + 4);
                    if (one != 0 || table_fits(&b, p + 8, count, 4)) {
                        t->stsz_size = one;
                        t->stsz_count = count;
                        t->stsz_at = p + 8;
                    }
                }
                break;
            case FOURCC('s', 't', 'c', 'o'):
            case FOURCC('c', 'o', '6', '4'): {
                bool wide = b.type == FOURCC('c', 'o', '6', '4');
                if (full_body(&b, 4, &p, &version, mp4) && table_fits(&b, p + 4, be32(mp4->data + p), wide ? 8 : 4)) {
                    t->stco_count = be32(mp4->data + p);
                    t->stco_at = p + 4;
                    t->co64 = wide;
                }
                break;
            }
            default: break;
        }
        if (b.end <= at) break;
    }
}

static void read_track(rubraview_mp4_t *mp4, const box_t *trak) {
    if (mp4->track_count >= RUBRAVIEW_MP4_MAX_TRACKS) return;
    rubraview_mp4_track_t t = { .language = "und" };
    box_t b, m, n;
    for (uint64_t at = trak->body; at < trak->end && box_at(mp4, at, trak->end, &b); at = b.end) {
        uint64_t p = 0;
        uint8_t version = 0;
        if (b.type == FOURCC('t', 'k', 'h', 'd') && full_body(&b, 20, &p, &version, mp4)) {
            t.id = be32(mp4->data + p + (version == 1 ? 16 : 8));
            /* RV-202: times, id, a reserved word, the duration, eight reserved bytes, layer, group, volume, two
               reserved; then the matrix (nine 32-bit numbers) and the width and height */
            uint64_t matrix = p + (version == 1 ? 48 : 36);
            if (matrix + 44 <= b.end) {
                int32_t a = (int32_t)be32(mp4->data + matrix), d = (int32_t)be32(mp4->data + matrix + 16);
                int32_t bb = (int32_t)be32(mp4->data + matrix + 4), c = (int32_t)be32(mp4->data + matrix + 12);
                t.quarter_turn = a == 0 && d == 0 && bb != 0 && c != 0;
                t.shown_width = (double)be32(mp4->data + matrix + 36) / 65536.0;
                t.shown_height = (double)be32(mp4->data + matrix + 40) / 65536.0;
            }
        } else if (b.type == FOURCC('m', 'd', 'i', 'a')) {
            for (uint64_t q = b.body; q < b.end && box_at(mp4, q, b.end, &m); q = m.end) {
                if (m.type == FOURCC('m', 'd', 'h', 'd') && full_body(&m, 20, &p, &version, mp4)) {
                    /* times of 32 or 64 bits, then the timescale, the duration, the language */
                    uint64_t scale_at = p + (version == 1 ? 16 : 8);
                    uint64_t lang_at = scale_at + 4 + (version == 1 ? 8 : 4);
                    if (lang_at + 2 <= m.end) {
                        t.timescale = be32(mp4->data + scale_at);
                        uint16_t code = (uint16_t)((mp4->data[lang_at] << 8) | mp4->data[lang_at + 1]);
                        char l0 = (char)(((code >> 10) & 31) + 0x60), l1 = (char)(((code >> 5) & 31) + 0x60), l2 = (char)((code & 31) + 0x60);
                        if (l0 >= 'a' && l0 <= 'z' && l1 >= 'a' && l1 <= 'z' && l2 >= 'a' && l2 <= 'z') {
                            t.language[0] = l0; t.language[1] = l1; t.language[2] = l2; t.language[3] = '\0';
                        }
                    }
                } else if (m.type == FOURCC('h', 'd', 'l', 'r') && full_body(&m, 8, &p, &version, mp4)) {
                    t.handler = be32(mp4->data + p + 4);
                } else if (m.type == FOURCC('m', 'i', 'n', 'f')) {
                    for (uint64_t r = m.body; r < m.end && box_at(mp4, r, m.end, &n); r = n.end) {
                        if (n.type == FOURCC('s', 't', 'b', 'l')) read_stbl(mp4, &n, &t);
                        if (n.end <= r) break;
                    }
                }
                if (m.end <= q) break;
            }
        }
        if (b.end <= at) break;
    }
    if (t.id == 0) return;
    bool text = t.handler == FOURCC('s', 'b', 't', 'l') || t.handler == FOURCC('t', 'e', 'x', 't') || t.handler == FOURCC('s', 'u', 'b', 't');
    if (text) t.subtitle = t.format == FOURCC('t', 'x', '3', 'g') ? RUBRAVIEW_MP4_SUB_TX3G : RUBRAVIEW_MP4_SUB_OTHER;
    mp4->tracks[mp4->track_count++] = t;
}

/* RV-210: `facts` reads a fragmented file's `moov` too, its length from `mvex/mehd` when `mvhd` has none. */
static bool open_file(rubraview_mp4_t *mp4, const uint8_t *data, uint64_t size, const char **out_why, bool facts) {
    const char *sink = NULL;
    if (!out_why) out_why = &sink;
    *out_why = NULL;
    if (!mp4 || !data || size < 16) { *out_why = N_("not an MP4 file"); return false; }
    *mp4 = (rubraview_mp4_t){ .data = data, .size = size };
    box_t b, moov = {0};
    bool have_ftyp = false, have_moov = false;
    for (uint64_t at = 0; at < size && box_at(mp4, at, size, &b); at = b.end) {
        if (b.type == FOURCC('f', 't', 'y', 'p')) have_ftyp = true;
        if (b.type == FOURCC('m', 'o', 'o', 'f') && !facts) { *out_why = N_("a fragmented MP4 is not read yet"); return false; }
        if (b.type == FOURCC('m', 'o', 'o', 'v') && !have_moov) { moov = b; have_moov = true; }
        if (b.end <= at) break;
    }
    if (!have_ftyp && !have_moov) { *out_why = N_("not an MP4 file"); return false; }
    if (!have_moov) { *out_why = N_("the MP4 file's head (moov) could not be read"); return false; }
    mp4->moov_at = moov.body - 8;
    mp4->moov_end = moov.end;
    for (uint64_t at = moov.body; at < moov.end && box_at(mp4, at, moov.end, &b); at = b.end) {
        if (b.type == FOURCC('m', 'v', 'e', 'x') && !facts) { *out_why = N_("a fragmented MP4 is not read yet"); return false; }
        if (b.type == FOURCC('m', 'v', 'e', 'x')) {
            box_t m;
            for (uint64_t q = b.body; q < b.end && box_at(mp4, q, b.end, &m); q = m.end) {
                uint64_t p = 0;
                uint8_t version = 0;
                if (m.type == FOURCC('m', 'e', 'h', 'd') && full_body(&m, 4, &p, &version, mp4) && p + (version == 1 ? 8 : 4) <= m.end)
                    mp4->fragment_duration = version == 1 ? be64(mp4->data + p) : be32(mp4->data + p);
                if (m.end <= q) break;
            }
        }
        if (b.type == FOURCC('t', 'r', 'a', 'k')) read_track(mp4, &b);
        uint64_t p = 0;
        uint8_t version = 0;
        if (b.type == FOURCC('m', 'v', 'h', 'd') && full_body(&b, 16, &p, &version, mp4)) {
            /* RV-202: two times of 32 or 64 bits, the timescale, the duration */
            uint64_t scale_at = p + (version == 1 ? 16 : 8);
            if (scale_at + 4 + (version == 1 ? 8 : 4) <= b.end) {
                mp4->movie_timescale = be32(mp4->data + scale_at);
                mp4->movie_duration = version == 1 ? be64(mp4->data + scale_at + 4) : be32(mp4->data + scale_at + 4);
            }
        }
        if (b.end <= at) break;
    }
    return true;
}

bool rubraview_mp4_open(rubraview_mp4_t *mp4, const uint8_t *data, uint64_t size, const char **out_why) {
    return open_file(mp4, data, size, out_why, false);
}

bool rubraview_mp4_open_moov(rubraview_mp4_t *mp4, const uint8_t *moov, uint64_t size) {
    return open_file(mp4, moov, size, NULL, true);
}

bool rubraview_mp4_frame(const rubraview_mp4_t *mp4, int32_t *out_width, int32_t *out_height, double *out_seconds) {
    if (out_width) *out_width = 0;
    if (out_height) *out_height = 0;
    if (out_seconds) *out_seconds = 0.0;
    if (!mp4) return false;
    uint64_t duration = mp4->movie_duration ? mp4->movie_duration : mp4->fragment_duration;   /* RV-210 */
    if (out_seconds && mp4->movie_timescale > 0 && duration != UINT64_MAX && duration != UINT32_MAX)
        *out_seconds = (double)duration / (double)mp4->movie_timescale;
    for (size_t i = 0; i < mp4->track_count; ++i) {
        const rubraview_mp4_track_t *t = &mp4->tracks[i];
        if (t->handler != FOURCC('v', 'i', 'd', 'e') || !(t->shown_width >= 1.0) || !(t->shown_height >= 1.0) ||
            t->shown_width > 1e6 || t->shown_height > 1e6) continue;
        int32_t w = (int32_t)(t->shown_width + 0.5), h = (int32_t)(t->shown_height + 0.5);
        if (out_width) *out_width = t->quarter_turn ? h : w;
        if (out_height) *out_height = t->quarter_turn ? w : h;
        return true;
    }
    return false;
}

/* How many samples chunk `chunk` (from 0) holds: the last stsc entry whose first chunk is not after it. */
static bool chunk_samples(const rubraview_mp4_t *mp4, const rubraview_mp4_track_t *t, rubraview_mp4_walk_t *w, uint32_t *out) {
    while (w->stsc_index + 1 < t->stsc_count &&
           be32(mp4->data + t->stsc_at + (uint64_t)(w->stsc_index + 1) * 12) <= w->chunk + 1) w->stsc_index++;
    if (w->stsc_index >= t->stsc_count) return false;
    const uint8_t *e = mp4->data + t->stsc_at + (uint64_t)w->stsc_index * 12;
    if (be32(e) > w->chunk + 1) return false;    /* the table does not begin at the first chunk */
    *out = be32(e + 4);
    return true;
}

bool rubraview_mp4_sample_next(const rubraview_mp4_t *mp4, const rubraview_mp4_track_t *t, rubraview_mp4_walk_t *w,
                               rubraview_mp4_sample_t *out) {
    if (!mp4 || !t || !w || !out) return false;
    if (w->sample >= t->stsz_count || t->stco_count == 0 || t->stsc_count == 0 || t->stts_count == 0) return false;
    /* the next chunk, when this one is spent (one with no samples is passed over) */
    if (!w->started || w->left_in_chunk == 0) {
        for (;;) {
            if (w->started) w->chunk++;
            w->started = true;
            if (w->chunk >= t->stco_count) return false;
            uint32_t n = 0;
            if (!chunk_samples(mp4, t, w, &n)) return false;
            w->offset = t->co64 ? be64(mp4->data + t->stco_at + (uint64_t)w->chunk * 8)
                                : be32(mp4->data + t->stco_at + (uint64_t)w->chunk * 4);
            w->left_in_chunk = n;
            if (n > 0) break;
        }
    }
    /* its time */
    while (w->stts_left == 0) {
        if (w->stts_index >= t->stts_count) return false;
        w->stts_left = be32(mp4->data + t->stts_at + (uint64_t)w->stts_index * 8);
        if (w->stts_left == 0) w->stts_index++;
    }
    uint32_t delta = be32(mp4->data + t->stts_at + (uint64_t)w->stts_index * 8 + 4);
    uint32_t size = t->stsz_size ? t->stsz_size : be32(mp4->data + t->stsz_at + (uint64_t)w->sample * 4);
    out->offset = w->offset;
    out->size = size;
    out->start = w->time;
    out->duration = delta;
    if (PROVEN_CKD_ADD(&w->offset, w->offset, (uint64_t)size) || PROVEN_CKD_ADD(&w->time, w->time, (uint64_t)delta)) return false;
    w->sample++;
    w->left_in_chunk--;
    if (--w->stts_left == 0) w->stts_index++;
    return true;
}

/* A time in a track's ticks as SubRip writes it; the multiplication is checked. */
static int put_clock(char *buf, size_t cap, uint64_t ticks, uint32_t scale) {
    uint64_t ms = 0;
    if (scale == 0 || PROVEN_CKD_MUL(&ms, ticks, (uint64_t)1000)) ms = 0;
    else ms = (ms + scale / 2) / scale;
    return snprintf(buf, cap, "%02llu:%02llu:%02llu,%03llu", (unsigned long long)(ms / 3600000), (unsigned long long)(ms / 60000 % 60),
                    (unsigned long long)(ms / 1000 % 60), (unsigned long long)(ms % 1000));
}

size_t rubraview_mp4_subtitle_cue(const rubraview_mp4_t *mp4, const rubraview_mp4_track_t *t, unsigned counter,
                                  const rubraview_mp4_sample_t *s, char *out, size_t cap) {
    if (!mp4 || !t || !s || t->subtitle != RUBRAVIEW_MP4_SUB_TX3G || s->size < 2) return 0;
    if (s->offset > mp4->size || s->size > mp4->size - s->offset) return 0;
    const uint8_t *p = mp4->data + s->offset;
    uint32_t len = ((uint32_t)p[0] << 8) | p[1];
    if (len == 0 || len > s->size - 2) return 0;               /* a gap, or a length past the sample */
    const char *text = (const char*)p + 2;
    if (len >= 2 && (uint8_t)text[0] == 0xFE && (uint8_t)text[1] == 0xFF) return 0;   /* UTF-16: not written */
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r' || text[len - 1] == ' ' || text[len - 1] == '\0')) len--;
    if (len == 0) return 0;

    char head[96], from[32], to[32];
    if (put_clock(from, sizeof(from), s->start, t->timescale) <= 0) return 0;
    uint64_t end = 0;
    if (PROVEN_CKD_ADD(&end, s->start, s->duration)) end = s->start;
    if (put_clock(to, sizeof(to), end, t->timescale) <= 0) return 0;
    int hn = snprintf(head, sizeof(head), "%u\n%s --> %s\n", counter, from, to);
    if (hn <= 0) return 0;
    size_t need = (size_t)hn, n = 0;
    bool line_start = true;
    /* The words, a line end as one '\n' and no empty line among them: one would end the cue. */
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1) {
            if (!out || need > cap) return need;
            memcpy(out, head, (size_t)hn);
            n = (size_t)hn;
            line_start = true;
        }
        for (uint32_t i = 0; i < len; ++i) {
            char c = text[i];
            if (c == '\r') continue;
            if (c == '\n') {
                if (!line_start) { if (pass) out[n++] = '\n'; else need++; line_start = true; }
                continue;
            }
            if (pass) out[n++] = c; else need++;
            line_start = false;
        }
        if (pass) { out[n++] = '\n'; out[n++] = '\n'; } else need += 2;
    }
    return need;
}

/* ---- a subtitle put into an MP4 film (RV-115, D-130) ---- */

typedef struct wbuf { uint8_t *data; size_t len, cap; bool failed; } wbuf_t;

static void wput(wbuf_t *b, const void *bytes, size_t n) {
    if (b->failed || n == 0) return;
    size_t want = 0;
    if (PROVEN_CKD_ADD(&want, b->len, n)) { b->failed = true; return; }
    if (want > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < want) if (PROVEN_CKD_MUL(&cap, cap, (size_t)2)) { b->failed = true; return; }
        uint8_t *data = realloc(b->data, cap);
        if (!data) { b->failed = true; return; }
        b->data = data;
        b->cap = cap;
    }
    memcpy(b->data + b->len, bytes, n);
    b->len += n;
}
static void w8(wbuf_t *b, uint8_t v) { wput(b, &v, 1); }
static void w16(wbuf_t *b, uint16_t v) { uint8_t x[2] = { (uint8_t)(v >> 8), (uint8_t)v }; wput(b, x, 2); }
static void w32(wbuf_t *b, uint32_t v) { uint8_t x[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v }; wput(b, x, 4); }
static void w64(wbuf_t *b, uint64_t v) { w32(b, (uint32_t)(v >> 32)); w32(b, (uint32_t)v); }
static void wtype(wbuf_t *b, const char *t) { wput(b, t, 4); }
static void wfull(wbuf_t *b, uint8_t version, uint32_t flags) { w8(b, version); w8(b, (uint8_t)(flags >> 16)); w8(b, (uint8_t)(flags >> 8)); w8(b, (uint8_t)flags); }
static void wbuf_free(wbuf_t *b) { free(b->data); *b = (wbuf_t){0}; }

/* A box around what `content` holds. */
static void wbox(wbuf_t *out, const char *type, const wbuf_t *content) {
    if (content->failed || content->len > UINT32_MAX - 8) { out->failed = true; return; }
    w32(out, (uint32_t)(content->len + 8));
    wtype(out, type);
    wput(out, content->data, content->len);
}

static const uint32_t IDENTITY[9] = { 0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000 };

/* The new track, as FFmpeg writes a 3GPP text one. */
static void write_trak(wbuf_t *out, const rubraview_mp4_put_t *put, uint32_t movie_scale, uint64_t duration_ms, proven_u8str_view_t language,
                       proven_u8str_view_t name, uint64_t chunk_at) {
    wbuf_t trak = {0}, b = {0}, mdia = {0}, minf = {0}, stbl = {0}, x = {0}, edts = {0};
    uint64_t movie_duration = 0;
    if (PROVEN_CKD_MUL(&movie_duration, duration_ms, (uint64_t)movie_scale)) movie_duration = 0;
    movie_duration /= 1000;
    bool wide = movie_duration > UINT32_MAX || duration_ms > UINT32_MAX;

    wfull(&b, wide ? 1 : 0, 0x3);                       /* enabled, in the movie */
    if (wide) { w64(&b, 0); w64(&b, 0); w32(&b, put->track_id); w32(&b, 0); w64(&b, movie_duration); }
    else { w32(&b, 0); w32(&b, 0); w32(&b, put->track_id); w32(&b, 0); w32(&b, (uint32_t)movie_duration); }
    w32(&b, 0); w32(&b, 0); w16(&b, 0); w16(&b, 0); w16(&b, 0); w16(&b, 0);   /* reserved, layer, group, volume, reserved */
    for (int i = 0; i < 9; ++i) w32(&b, IDENTITY[i]);
    w32(&b, 0); w32(&b, 0);                             /* width, height */
    wbox(&trak, "tkhd", &b); wbuf_free(&b);

    wfull(&x, wide ? 1 : 0, 0); w32(&x, 1);             /* one edit: the whole track from its start */
    if (wide) { w64(&x, movie_duration); w64(&x, 0); } else { w32(&x, (uint32_t)movie_duration); w32(&x, 0); }
    w16(&x, 1); w16(&x, 0);
    wbox(&edts, "elst", &x); wbuf_free(&x);
    wbox(&trak, "edts", &edts); wbuf_free(&edts);

    uint16_t code = 0x55C4;                             /* "und" */
    if (language.size == 3) {
        bool letters = true;
        for (int i = 0; i < 3; ++i) letters = letters && language.ptr[i] >= 'a' && language.ptr[i] <= 'z';
        if (letters) code = (uint16_t)(((language.ptr[0] - 0x60) << 10) | ((language.ptr[1] - 0x60) << 5) | (language.ptr[2] - 0x60));
    }
    wfull(&b, wide ? 1 : 0, 0);
    if (wide) { w64(&b, 0); w64(&b, 0); w32(&b, 1000); w64(&b, duration_ms); }
    else { w32(&b, 0); w32(&b, 0); w32(&b, 1000); w32(&b, (uint32_t)duration_ms); }
    w16(&b, code); w16(&b, 0);
    wbox(&mdia, "mdhd", &b); wbuf_free(&b);
    wfull(&b, 0, 0); w32(&b, 0); wtype(&b, "sbtl"); w32(&b, 0); w32(&b, 0); w32(&b, 0);
    wput(&b, name.size ? (const char *)name.ptr : "SubtitleHandler", name.size ? (name.size > 200 ? 200 : name.size) : 15); w8(&b, 0);
    wbox(&mdia, "hdlr", &b); wbuf_free(&b);

    wfull(&b, 0, 0); wbox(&minf, "nmhd", &b); wbuf_free(&b);
    wbuf_t dinf = {0}, dref = {0};
    wfull(&dref, 0, 0); w32(&dref, 1); wfull(&b, 0, 1); wbox(&dref, "url ", &b); wbuf_free(&b);
    wbox(&dinf, "dref", &dref); wbuf_free(&dref);
    wbox(&minf, "dinf", &dinf); wbuf_free(&dinf);

    /* stsd: one TextSampleEntry — centred at the bottom, white on clear, Arial 16 */
    wbuf_t entry = {0}, ftab = {0};
    for (int i = 0; i < 6; ++i) w8(&entry, 0);
    w16(&entry, 1);                                     /* data reference */
    w32(&entry, 0); w8(&entry, 1); w8(&entry, 0xFF);    /* display flags; centred; at the bottom */
    w32(&entry, 0x000000FF);                            /* background */
    w16(&entry, 0); w16(&entry, 0); w16(&entry, 0); w16(&entry, 0);   /* the box */
    w16(&entry, 0); w16(&entry, 0); w16(&entry, 1); w8(&entry, 0); w8(&entry, 0x10); w32(&entry, 0xFFFFFFFF);   /* the style */
    w16(&ftab, 1); w16(&ftab, 1); w8(&ftab, 5); wput(&ftab, "Arial", 5);
    wbox(&entry, "ftab", &ftab); wbuf_free(&ftab);
    wfull(&x, 0, 0); w32(&x, 1); wbox(&x, "tx3g", &entry); wbuf_free(&entry);
    wbox(&stbl, "stsd", &x); wbuf_free(&x);
    wfull(&x, 0, 0); w32(&x, (uint32_t)put->sample_count);
    for (size_t i = 0; i < put->sample_count; ++i) { w32(&x, 1); w32(&x, (uint32_t)put->samples[i].duration); }
    wbox(&stbl, "stts", &x); wbuf_free(&x);
    wfull(&x, 0, 0); w32(&x, 1); w32(&x, 1); w32(&x, (uint32_t)put->sample_count); w32(&x, 1);   /* one chunk holds them all */
    wbox(&stbl, "stsc", &x); wbuf_free(&x);
    wfull(&x, 0, 0); w32(&x, 0); w32(&x, (uint32_t)put->sample_count);
    for (size_t i = 0; i < put->sample_count; ++i) w32(&x, put->samples[i].size);
    wbox(&stbl, "stsz", &x); wbuf_free(&x);
    wfull(&x, 0, 0); w32(&x, 1);
    if (chunk_at > UINT32_MAX) { w64(&x, chunk_at); wbox(&stbl, "co64", &x); } else { w32(&x, (uint32_t)chunk_at); wbox(&stbl, "stco", &x); }
    wbuf_free(&x);
    wbox(&minf, "stbl", &stbl); wbuf_free(&stbl);
    wbox(&mdia, "minf", &minf); wbuf_free(&minf);
    wbox(&trak, "mdia", &mdia); wbuf_free(&mdia);
    wbox(out, "trak", &trak); wbuf_free(&trak);
}

void rubraview_mp4_put_free(rubraview_mp4_put_t *put) {
    if (!put) return;
    free(put->tail);
    free(put->samples);
    put->tail = NULL;
    put->samples = NULL;
}

static bool shown(const rubraview_subtitle_track_t *t, const rubraview_subtitle_cue_t *cue) {
    if (cue->text.size == 0) return false;
    if (t->shown_language.size == 0) return true;
    return cue->language.size == t->shown_language.size && memcmp(cue->language.ptr, t->shown_language.ptr, cue->language.size) == 0;
}

static uint64_t to_ms(double seconds) { return seconds > 0.0 ? (uint64_t)(seconds * 1000.0 + 0.5) : 0; }

bool rubraview_mp4_put_plan(rubraview_mp4_put_t *put, const rubraview_mp4_t *mp4, const rubraview_subtitle_track_t *subtitle,
                            proven_u8str_view_t language, proven_u8str_view_t name, const char **out_why) {
    const char *sink = NULL;
    if (!out_why) out_why = &sink;
    *out_why = NULL;
    if (!put || !mp4 || !subtitle || subtitle->count == 0) { *out_why = N_("nothing to put in"); return false; }
    *put = (rubraview_mp4_put_t){ .mp4 = mp4 };

    /* The film's own boxes: none may run "to the end", and the moov is one of them. */
    box_t b, moov = {0};
    bool have_moov = false;
    uint64_t at = 0;
    while (at < mp4->size && box_at(mp4, at, mp4->size, &b)) {
        if (be32(mp4->data + at) == 0) { *out_why = N_("the film's last box runs to the end of the file"); return false; }
        if (b.type == FOURCC('m', 'o', 'o', 'v') && !have_moov) { moov = b; have_moov = true; put->type_at = at + 4; }
        if (b.end <= at) break;
        at = b.end;
    }
    if (!have_moov) { *out_why = N_("the film's head (moov) could not be found"); return false; }
    if (at != mp4->size) { *out_why = N_("the film does not end where its last box does"); return false; }

    /* The samples: the words, a gap as an empty sample, no two at once. */
    size_t room = subtitle->count * 2 + 1;
    put->samples = calloc(room, sizeof(*put->samples));
    if (!put->samples) { *out_why = N_("no memory"); return false; }
    wbuf_t data = {0};
    uint64_t now = 0;
    for (size_t i = 0; i < subtitle->count; ++i) {
        const rubraview_subtitle_cue_t *cue = &subtitle->cues[i];
        if (!shown(subtitle, cue)) continue;
        uint64_t start = to_ms(cue->start_seconds), end = to_ms(cue->end_seconds);
        for (size_t k = i + 1; k < subtitle->count; ++k) {      /* the next one shown ends this one */
            if (!shown(subtitle, &subtitle->cues[k])) continue;
            uint64_t next = to_ms(subtitle->cues[k].start_seconds);
            if (next < end) end = next;
            break;
        }
        if (start < now) start = now;
        if (end <= start) continue;
        if (start > now) {
            put->samples[put->sample_count++] = (rubraview_mp4_put_sample_t){ .at = (uint32_t)data.len, .size = 2, .start = now, .duration = start - now };
            w16(&data, 0);
        }
        size_t len = cue->text.size > 65535 ? 65535 : cue->text.size;
        while (len > 0 && len < cue->text.size && ((unsigned char)cue->text.ptr[len] & 0xC0) == 0x80) len--;   /* whole characters */
        put->samples[put->sample_count++] = (rubraview_mp4_put_sample_t){ .at = (uint32_t)data.len, .size = (uint32_t)(len + 2),
                                                                          .start = start, .duration = end - start };
        w16(&data, (uint16_t)len);
        wput(&data, cue->text.ptr, len);
        now = end;
        if (data.len > UINT32_MAX / 2) { wbuf_free(&data); rubraview_mp4_put_free(put); *out_why = N_("the subtitle is too long"); return false; }
    }
    if (put->sample_count == 0 || data.failed) { wbuf_free(&data); rubraview_mp4_put_free(put); *out_why = N_("the subtitle holds no lines to put in"); return false; }

    /* The track's number, and the movie's clock. */
    uint32_t movie_scale = 1000;
    uint64_t mvhd_at = 0, mvhd_end = 0;
    for (uint64_t p = moov.body; p < moov.end && box_at(mp4, p, moov.end, &b); p = b.end) {
        if (b.type == FOURCC('m', 'v', 'h', 'd')) {
            uint8_t version = mp4->data[b.body];
            /* version, flags, the two times, the timescale, ... then the next track id, last */
            uint64_t scale_at = b.body + 4 + (version == 1 ? 16 : 8), next_at = b.body + (version == 1 ? 108 : 96);
            if (next_at + 4 <= b.end) {
                movie_scale = be32(mp4->data + scale_at);
                uint32_t next = be32(mp4->data + next_at);
                if (next > 0 && next != UINT32_MAX && next > put->track_id) put->track_id = next;
                mvhd_at = p;
                mvhd_end = b.end;
            }
        }
        if (b.end <= p) break;
    }
    for (size_t i = 0; i < mp4->track_count; ++i) if (mp4->tracks[i].id >= put->track_id) put->track_id = mp4->tracks[i].id + 1;
    if (mvhd_at == 0 || movie_scale == 0 || put->track_id == 0 || put->track_id == UINT32_MAX) {
        wbuf_free(&data); rubraview_mp4_put_free(put); *out_why = N_("the film's movie header could not be read"); return false;
    }

    /* The tail: an mdat of the samples, then the new moov. */
    wbuf_t tail = {0}, newmoov = {0};
    bool big = data.len + 8 > UINT32_MAX;
    if (big) { w32(&tail, 1); wtype(&tail, "mdat"); w64(&tail, data.len + 16); }
    else { w32(&tail, (uint32_t)(data.len + 8)); wtype(&tail, "mdat"); }
    uint64_t chunk_at = mp4->size + tail.len;
    wput(&tail, data.data, data.len);
    for (size_t i = 0; i < put->sample_count; ++i) put->samples[i].at += (uint32_t)(tail.len - data.len);
    wbuf_free(&data);
    for (uint64_t p = moov.body; p < moov.end && box_at(mp4, p, moov.end, &b); p = b.end) {
        if (p == mvhd_at) {
            /* the movie header as it was, its next track id past the new one */
            uint8_t version = mp4->data[b.body];
            uint64_t next_at = b.body + (version == 1 ? 108 : 96);
            wput(&newmoov, mp4->data + p, next_at - p);
            w32(&newmoov, put->track_id + 1);
            wput(&newmoov, mp4->data + next_at + 4, mvhd_end - next_at - 4);
        } else {
            wput(&newmoov, mp4->data + p, b.end - p);
        }
        if (b.end <= p) break;
    }
    uint64_t last = put->samples[put->sample_count - 1].start + put->samples[put->sample_count - 1].duration;
    write_trak(&newmoov, put, movie_scale, last, language, name, chunk_at);
    wbox(&tail, "moov", &newmoov);
    wbuf_free(&newmoov);
    if (tail.failed) { wbuf_free(&tail); rubraview_mp4_put_free(put); *out_why = N_("no memory"); return false; }
    put->tail = tail.data;
    put->tail_size = tail.len;
    put->total = mp4->size + tail.len;
    return true;
}

int rubraview_mp4_put_step(rubraview_mp4_put_t *put, rubraview_mp4_emit_fn emit, void *ctx, size_t budget) {
    if (!put || !emit || !put->tail) return -1;
    if (budget == 0) budget = 1;
    static const uint8_t FREE[4] = { 'f', 'r', 'e', 'e' };
    for (;;) {
        const uint8_t *base;
        uint64_t size;
        switch (put->index) {
            case 0: base = put->mp4->data; size = put->type_at; break;
            case 1: base = FREE; size = 4; break;
            case 2: base = put->mp4->data + put->type_at + 4; size = put->mp4->size - put->type_at - 4; break;
            case 3: base = put->tail; size = put->tail_size; break;
            default: return 0;
        }
        uint64_t left = size - put->offset;
        size_t n = left < budget ? (size_t)left : budget;
        if (n > 0 && !emit(ctx, base + put->offset, n)) return -1;
        put->offset += n;
        budget -= n;
        if (put->offset == size) { put->index++; put->offset = 0; }
        if (put->index > 3) return 0;
        if (budget == 0) return 1;
    }
}

/* ---- the new film checked against the old ---- */

static bool same_tables(const rubraview_mp4_t *a, const rubraview_mp4_track_t *x, const rubraview_mp4_t *b, const rubraview_mp4_track_t *y) {
    if (x->id != y->id || x->handler != y->handler || x->format != y->format || x->timescale != y->timescale ||
        x->stts_count != y->stts_count || x->stsc_count != y->stsc_count || x->stsz_count != y->stsz_count ||
        x->stco_count != y->stco_count || x->stsz_size != y->stsz_size || x->co64 != y->co64) return false;
    return memcmp(a->data + x->stts_at, b->data + y->stts_at, (size_t)x->stts_count * 8) == 0 &&
           memcmp(a->data + x->stsc_at, b->data + y->stsc_at, (size_t)x->stsc_count * 12) == 0 &&
           (x->stsz_size || memcmp(a->data + x->stsz_at, b->data + y->stsz_at, (size_t)x->stsz_count * 4) == 0) &&
           memcmp(a->data + x->stco_at, b->data + y->stco_at, (size_t)x->stco_count * (x->co64 ? 8 : 4)) == 0;
}

bool rubraview_mp4_check_begin(rubraview_mp4_check_t *check, const rubraview_mp4_put_t *put, const uint8_t *made, uint64_t made_size) {
    if (!check || !put || !made) return false;
    *check = (rubraview_mp4_check_t){0};
    if (made_size != put->total) { check->why = N_("the new film is not the size it was laid out to be"); return false; }
    return true;
}

int rubraview_mp4_check_step(rubraview_mp4_check_t *check, const rubraview_mp4_put_t *put, const uint8_t *made, size_t budget) {
    if (!check || !put || check->why) return -1;
    const rubraview_mp4_t *old = put->mp4;
    if (check->phase == 0) {
        /* every byte of the film, but the four letters that say `free` */
        uint64_t end = check->at + budget < old->size ? check->at + budget : old->size;
        uint64_t mark = put->type_at;
        if (check->at < mark && end > mark) end = mark;
        if (check->at == mark) {
            if (memcmp(made + mark, "free", 4) != 0) { check->why = N_("the film's old head was not set aside"); return -1; }
            check->at += 4;
            return 1;
        }
        if (memcmp(made + check->at, old->data + check->at, (size_t)(end - check->at)) != 0) {
            check->why = N_("a byte of the film differs in the new one");
            return -1;
        }
        check->at = end;
        if (check->at < old->size) return 1;
        check->phase = 1;
        return 1;
    }
    /* the new head: the film's tracks as they were, and the subtitle */
    if (!rubraview_mp4_open(&check->made, made, put->total, &check->why)) return -1;
    if (check->made.track_count != old->track_count + 1) { check->why = N_("the new film's tracks are not the film's and one more"); return -1; }
    for (size_t i = 0; i < old->track_count; ++i) {
        if (!same_tables(old, &old->tracks[i], &check->made, &check->made.tracks[i])) { check->why = N_("a track of the new film is not as it was"); return -1; }
    }
    const rubraview_mp4_track_t *added = &check->made.tracks[old->track_count];
    if (added->id != put->track_id || added->subtitle != RUBRAVIEW_MP4_SUB_TX3G || added->timescale != 1000) {
        check->why = N_("the subtitle track was not found in the new film");
        return -1;
    }
    rubraview_mp4_walk_t walk = {0};
    rubraview_mp4_sample_t s;
    size_t n = 0;
    while (rubraview_mp4_sample_next(&check->made, added, &walk, &s)) {
        if (n >= put->sample_count) { check->why = N_("the new film holds more subtitle samples than were put in"); return -1; }
        const rubraview_mp4_put_sample_t *want = &put->samples[n];
        if (s.offset != old->size + want->at || s.size != want->size || s.start != want->start || s.duration != want->duration ||
            memcmp(made + s.offset, put->tail + want->at, want->size) != 0) {
            check->why = N_("a subtitle in the new film is not the one put in");
            return -1;
        }
        n++;
    }
    if (n != put->sample_count) { check->why = N_("not every subtitle is in the new film"); return -1; }
    return 0;
}
