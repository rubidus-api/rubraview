#include "rubraview/mp4.h"

#include <stdio.h>
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

bool rubraview_mp4_open(rubraview_mp4_t *mp4, const uint8_t *data, uint64_t size, const char **out_why) {
    const char *sink = NULL;
    if (!out_why) out_why = &sink;
    *out_why = NULL;
    if (!mp4 || !data || size < 16) { *out_why = "not an MP4 file"; return false; }
    *mp4 = (rubraview_mp4_t){ .data = data, .size = size };
    box_t b, moov = {0};
    bool have_ftyp = false, have_moov = false;
    for (uint64_t at = 0; at < size && box_at(mp4, at, size, &b); at = b.end) {
        if (b.type == FOURCC('f', 't', 'y', 'p')) have_ftyp = true;
        if (b.type == FOURCC('m', 'o', 'o', 'f')) { *out_why = "a fragmented MP4 is not read yet"; return false; }
        if (b.type == FOURCC('m', 'o', 'o', 'v') && !have_moov) { moov = b; have_moov = true; }
        if (b.end <= at) break;
    }
    if (!have_ftyp && !have_moov) { *out_why = "not an MP4 file"; return false; }
    if (!have_moov) { *out_why = "the MP4 file's head (moov) could not be read"; return false; }
    mp4->moov_at = moov.body - 8;
    mp4->moov_end = moov.end;
    for (uint64_t at = moov.body; at < moov.end && box_at(mp4, at, moov.end, &b); at = b.end) {
        if (b.type == FOURCC('m', 'v', 'e', 'x')) { *out_why = "a fragmented MP4 is not read yet"; return false; }
        if (b.type == FOURCC('t', 'r', 'a', 'k')) read_track(mp4, &b);
        if (b.end <= at) break;
    }
    return true;
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
