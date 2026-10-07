/* util.c - FultaArc: memory, buffers, CRC-32, ranges, streams, text helpers. MIT. */
#include "internal.h"

#include "proven/hash.h"

#include <stdlib.h>

void *fa_malloc(size_t n) { return malloc(n ? n : 1); }
void *fa_calloc(size_t count, size_t n) {
    if (n && count > SIZE_MAX / n) return NULL;
    return calloc(count ? count : 1, n ? n : 1);
}
void *fa_realloc(void *p, size_t n) { return realloc(p, n ? n : 1); }
void fa_free(void *p) { free(p); }

char *fa_strndup(const char *s, size_t n) {
    char *d = fa_malloc(n + 1);
    if (!d) return NULL;
    if (n) memcpy(d, s, n);
    d[n] = 0;
    return d;
}

fulta_arc_err_t fa_buf_append(fa_buf_t *b, const void *data, size_t n) {
    if (n > SIZE_MAX - b->size) return FULTA_ARC_ERR_NOMEM;
    if (b->size + n > b->cap) {
        size_t cap = b->cap ? b->cap : 64;
        while (cap < b->size + n) {
            if (cap > SIZE_MAX / 2) { cap = b->size + n; break; }
            cap *= 2;
        }
        uint8_t *p = fa_realloc(b->data, cap);
        if (!p) return FULTA_ARC_ERR_NOMEM;
        b->data = p;
        b->cap = cap;
    }
    if (n) memcpy(b->data + b->size, data, n);
    b->size += n;
    return FULTA_ARC_OK;
}

fulta_arc_err_t fa_buf_byte(fa_buf_t *b, uint8_t v) { return fa_buf_append(b, &v, 1); }

void fa_buf_free(fa_buf_t *b) {
    fa_free(b->data);
    *b = (fa_buf_t){0};
}

/* ---- CRC-32 ------------------------------------------------------------------------------------------------- */

/* CRC-32 (IEEE 802.3, as ZIP): proven's, which reads eight bytes a step. */
uint32_t fa_crc32(uint32_t crc, const void *data, size_t n) {
    return proven_crc32_update(crc, (proven_mem_view_t){.ptr = data, .size = n});
}

/* ---- ranges -------------------------------------------------------------------------------------------------- */

fulta_arc_err_t fa_source_read_exact(const fulta_arc_source_t *s, uint64_t offset, void *buf, size_t n) {
    uint8_t *p = buf;
    while (n) {
        size_t got = 0;
        fulta_arc_err_t e = s->read_at(s->ctx, offset, p, n, &got);
        if (e) return e;
        if (!got) return FULTA_ARC_ERR_TRUNCATED;
        p += got;
        offset += got;
        n -= got;
    }
    return FULTA_ARC_OK;
}

fulta_arc_err_t fa_range_read(const fa_range_t *r, uint64_t offset, void *buf, size_t n, size_t *got) {
    *got = 0;
    uint8_t *p = buf;
    for (size_t i = 0; i < r->count && n; i++) {
        const fa_piece_t *pc = &r->pieces[i];
        if (offset >= pc->size) { offset -= pc->size; continue; }
        uint64_t avail = pc->size - offset;
        size_t take = n < avail ? n : (size_t)avail;
        size_t g = 0;
        fulta_arc_err_t e = pc->src->read_at(pc->src->ctx, pc->offset + offset, p, take, &g);
        if (e) return e;
        if (!g) return FULTA_ARC_ERR_TRUNCATED;
        *got += g;
        p += g;
        n -= g;
        if (g < take) return FULTA_ARC_OK;
        offset = 0;
    }
    return FULTA_ARC_OK;
}

fulta_arc_err_t fa_range_read_exact(const fa_range_t *r, uint64_t offset, void *buf, size_t n) {
    uint8_t *p = buf;
    while (n) {
        size_t got = 0;
        fulta_arc_err_t e = fa_range_read(r, offset, p, n, &got);
        if (e) return e;
        if (!got) return FULTA_ARC_ERR_TRUNCATED;
        p += got;
        offset += got;
        n -= got;
    }
    return FULTA_ARC_OK;
}

/* ---- streams ------------------------------------------------------------------------------------------------- */

fulta_arc_err_t fa_stream_read_exact(fa_stream_t *s, void *buf, size_t n) {
    uint8_t *p = buf;
    while (n) {
        size_t got = 0;
        fulta_arc_err_t e = s->read(s, p, n, &got);
        if (e) return e;
        if (!got) return FULTA_ARC_ERR_TRUNCATED;
        p += got;
        n -= got;
    }
    return FULTA_ARC_OK;
}

fulta_arc_err_t fa_stream_skip(fa_stream_t *s, uint64_t n) {
    uint8_t tmp[16384];
    while (n) {
        size_t want = n < sizeof tmp ? (size_t)n : sizeof tmp;
        size_t got = 0;
        fulta_arc_err_t e = s->read(s, tmp, want, &got);
        if (e) return e;
        if (!got) return FULTA_ARC_ERR_TRUNCATED;
        n -= got;
    }
    return FULTA_ARC_OK;
}

void fa_stream_destroy(fa_stream_t *s) {
    if (s && s->destroy) s->destroy(s);
}

typedef struct range_stream {
    fa_stream_t base;
    fa_range_t range;
    uint64_t pos, end;
} range_stream_t;

static fulta_arc_err_t range_stream_read(fa_stream_t *s, void *buf, size_t n, size_t *got) {
    range_stream_t *r = (range_stream_t *)s;
    *got = 0;
    if (r->pos >= r->end) return FULTA_ARC_OK;
    if (n > r->end - r->pos) n = (size_t)(r->end - r->pos);
    fulta_arc_err_t e = fa_range_read(&r->range, r->pos, buf, n, got);
    if (e) return e;
    if (!*got) return FULTA_ARC_ERR_TRUNCATED;
    r->pos += *got;
    return FULTA_ARC_OK;
}

static void range_stream_destroy(fa_stream_t *s) {
    range_stream_t *r = (range_stream_t *)s;
    fa_free(r->range.pieces);
    fa_free(r);
}

fulta_arc_err_t fa_stream_range(const fa_range_t *r, uint64_t offset, uint64_t size, fa_stream_t **out) {
    if (offset > r->size || size > r->size - offset) return FULTA_ARC_ERR_TRUNCATED;
    range_stream_t *s = fa_calloc(1, sizeof *s);
    if (!s) return FULTA_ARC_ERR_NOMEM;
    s->range.pieces = fa_calloc(r->count, sizeof *r->pieces);
    if (!s->range.pieces) { fa_free(s); return FULTA_ARC_ERR_NOMEM; }
    memcpy(s->range.pieces, r->pieces, r->count * sizeof *r->pieces);
    s->range.count = r->count;
    s->range.size = r->size;
    s->pos = offset;
    s->end = offset + size;
    s->base.read = range_stream_read;
    s->base.destroy = range_stream_destroy;
    *out = &s->base;
    return FULTA_ARC_OK;
}

typedef struct mem_stream { fa_stream_t base; const uint8_t *p; size_t n, pos; } mem_stream_t;

static fulta_arc_err_t mem_stream_read(fa_stream_t *s, void *buf, size_t n, size_t *got) {
    mem_stream_t *m = (mem_stream_t *)s;
    size_t left = m->n - m->pos;
    if (n > left) n = left;
    if (n) memcpy(buf, m->p + m->pos, n);
    m->pos += n;
    *got = n;
    return FULTA_ARC_OK;
}

static void mem_stream_destroy(fa_stream_t *s) { fa_free(s); }

fulta_arc_err_t fa_stream_memory(const void *data, size_t size, fa_stream_t **out) {
    mem_stream_t *m = fa_calloc(1, sizeof *m);
    if (!m) return FULTA_ARC_ERR_NOMEM;
    m->p = data;
    m->n = size;
    m->base.read = mem_stream_read;
    m->base.destroy = mem_stream_destroy;
    *out = &m->base;
    return FULTA_ARC_OK;
}

typedef struct limit_stream { fa_stream_t base; fa_stream_t *in; uint64_t left; } limit_stream_t;

static fulta_arc_err_t limit_read(fa_stream_t *s, void *buf, size_t n, size_t *got) {
    limit_stream_t *l = (limit_stream_t *)s;
    *got = 0;
    if (!l->left) return FULTA_ARC_OK;
    if (n > l->left) n = (size_t)l->left;
    fulta_arc_err_t e = l->in->read(l->in, buf, n, got);
    if (e) return e;
    if (!*got) return FULTA_ARC_ERR_TRUNCATED;
    l->left -= *got;
    return FULTA_ARC_OK;
}

static void limit_destroy(fa_stream_t *s) {
    limit_stream_t *l = (limit_stream_t *)s;
    fa_stream_destroy(l->in);
    fa_free(l);
}

fulta_arc_err_t fa_stream_limit(fa_stream_t *in, uint64_t size, fa_stream_t **out) {
    limit_stream_t *l = fa_calloc(1, sizeof *l);
    if (!l) { fa_stream_destroy(in); return FULTA_ARC_ERR_NOMEM; }
    l->in = in;
    l->left = size;
    l->base.read = limit_read;
    l->base.destroy = limit_destroy;
    *out = &l->base;
    return FULTA_ARC_OK;
}

void fa_bytes_init(fa_bytes_t *b, fa_stream_t *in) {
    b->in = in;
    b->pos = b->len = 0;
    b->eof = false;
    b->err = FULTA_ARC_OK;
    b->consumed = 0;
}

int fa_bytes_get(fa_bytes_t *b) {
    if (b->pos == b->len) {
        if (b->eof || b->err) return -1;
        size_t got = 0;
        b->err = b->in->read(b->in, b->buf, sizeof b->buf, &got);
        if (b->err) return -1;
        if (!got) { b->eof = true; return -1; }
        b->pos = 0;
        b->len = got;
    }
    b->consumed++;
    return b->buf[b->pos++];
}

/* ---- text ---------------------------------------------------------------------------------------------------- */

bool fa_utf8_valid(const uint8_t *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        uint8_t c = s[i];
        if (c < 0x80) { i++; continue; }
        size_t len;
        uint32_t cp, min;
        if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; min = 0x80; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; min = 0x800; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; min = 0x10000; }
        else return false;
        if (i + len > n) return false;
        for (size_t k = 1; k < len; k++) {
            if ((s[i + k] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (s[i + k] & 0x3F);
        }
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        i += len;
    }
    return true;
}

fulta_arc_err_t fa_utf8_put(fa_buf_t *b, uint32_t cp) {
    uint8_t t[4];
    size_t n;
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    if (cp < 0x80) { t[0] = (uint8_t)cp; n = 1; }
    else if (cp < 0x800) { t[0] = (uint8_t)(0xC0 | (cp >> 6)); t[1] = (uint8_t)(0x80 | (cp & 0x3F)); n = 2; }
    else if (cp < 0x10000) {
        t[0] = (uint8_t)(0xE0 | (cp >> 12)); t[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        t[2] = (uint8_t)(0x80 | (cp & 0x3F)); n = 3;
    } else {
        t[0] = (uint8_t)(0xF0 | (cp >> 18)); t[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
        t[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F)); t[3] = (uint8_t)(0x80 | (cp & 0x3F)); n = 4;
    }
    return fa_buf_append(b, t, n);
}

char *fa_utf16le_to_utf8(const uint8_t *s, size_t units) {
    fa_buf_t b = {0};
    for (size_t i = 0; i < units; i++) {
        uint32_t u = fa_le16(s + 2 * i);
        if (u >= 0xD800 && u <= 0xDBFF && i + 1 < units) {
            uint32_t v = fa_le16(s + 2 * i + 2);
            if (v >= 0xDC00 && v <= 0xDFFF) {
                u = 0x10000 + ((u - 0xD800) << 10) + (v - 0xDC00);
                i++;
            }
        }
        if (fa_utf8_put(&b, u)) { fa_buf_free(&b); return NULL; }
    }
    if (fa_buf_byte(&b, 0)) { fa_buf_free(&b); return NULL; }
    return (char *)b.data;
}

bool fa_sanitize_name(char *name) {
    /* split on '/' and '\\', drop empty, "." and a leading drive "X:", reject "..", rejoin with '/' */
    size_t w = 0, i = 0, n = strlen(name);
    bool first = true;
    while (i < n) {
        size_t j = i;
        while (j < n && name[j] != '/' && name[j] != '\\') j++;
        size_t len = j - i;
        const char *part = name + i;
        bool skip = len == 0 || (len == 1 && part[0] == '.');
        if (first && len == 2 && part[1] == ':' &&
            ((part[0] >= 'A' && part[0] <= 'Z') || (part[0] >= 'a' && part[0] <= 'z')))
            skip = true;
        if (len == 2 && part[0] == '.' && part[1] == '.') return false;
        first = false;
        if (!skip) {
            if (w) name[w++] = '/';
            memmove(name + w, part, len);
            w += len;
        }
        i = j + 1;
    }
    name[w] = 0;
    return w > 0;
}

/* ---- time ---------------------------------------------------------------------------------------------------- */

static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

int64_t fa_dos_time_ns(uint16_t date, uint16_t time) {
    unsigned d = date & 31, m = (date >> 5) & 15, y = 1980 + (date >> 9);
    if (d == 0) d = 1;
    if (m == 0) m = 1;
    if (m > 12) m = 12;
    int64_t days = days_from_civil(y, m, d);
    int64_t secs = days * 86400 + (time >> 11) * 3600 + ((time >> 5) & 63) * 60 + (time & 31) * 2;
    return secs * 1000000000;
}

/* FILETIME (100 ns since 1601) to ns since 1970; times past int64 nanoseconds (years before 1678 or after 2262)
 * are clamped. */
int64_t fa_filetime_ns(uint64_t ft) {
    int64_t us = (int64_t)(ft / 10) - INT64_C(11644473600000000);
    if (us >= INT64_MAX / 1000) return INT64_MAX;
    if (us <= INT64_MIN / 1000) return INT64_MIN;
    return us * 1000 + (int64_t)(ft % 10) * 100;
}
