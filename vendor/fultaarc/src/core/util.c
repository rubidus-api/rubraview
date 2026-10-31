/* util.c - FultaArc: memory, buffers, CRC-32, ranges, streams, text helpers. MIT. */
#include "internal.h"

#include "proven/hash.h"
#include "proven/heap.h"
#include "proven/panic.h"

#include <stddef.h>

/* Every internal allocation funnels through these four, and through them onto proven's allocator trait.
 * The LZMA SDK rides along: its ISzAlloc (src/codec/codecs.c) calls fa_malloc/fa_free. Stage 0 (D-021)
 * binds the seam to proven's heap allocator (malloc underneath); a later stage lets a caller supply one
 * per archive and places a memory budget behind it. A block is always allocated, reallocated and freed at
 * one fixed alignment, as the allocator trait requires, and never at an over-aligned class. This seam is
 * deliberately heap-pinned: it passes `old_size = 0` to the trait's realloc, which is correct only because the
 * backing is proven's heap (on POSIX a fundamental-align request stays on plain malloc/realloc/free, which ignore
 * old_size; on Windows the heap uses the _aligned_* family, which also does not need the caller's old_size). A
 * non-heap backing that needs a true old_size is not this seam but the budget allocator below, which carries the
 * size in a per-block header. The memory that crosses the public free() boundary (fulta_arc_extract_alloc) is
 * deliberately NOT from this seam; see arc.c. FA_ALIGN is in internal.h (shared with the budget allocator). */
static proven_allocator_t fa_allocator(void) { return proven_heap_allocator(); }

void *fa_malloc(size_t n) {
    proven_allocator_t a = fa_allocator();
    proven_result_mem_mut_t r = a.alloc_fn(a.ctx, n ? n : 1, FA_ALIGN);
    return r.err == PROVEN_OK ? r.value.ptr : NULL;
}
void *fa_calloc(size_t count, size_t n) {
    if (n && count > SIZE_MAX / n) return NULL;
    size_t total = count * n;
    if (!total) total = 1;
    void *p = fa_malloc(total);
    if (p) memset(p, 0, total);
    return p;
}
void *fa_realloc(void *p, size_t n) {
    proven_allocator_t a = fa_allocator();
    proven_result_mem_mut_t r = a.realloc_fn(a.ctx, p, 0 /* old_size: unused by the heap at FA_ALIGN */,
                                             n ? n : 1, FA_ALIGN);
    return r.err == PROVEN_OK ? r.value.ptr : NULL;
}
void fa_free(void *p) {
    proven_allocator_t a = fa_allocator();
    a.free_fn(a.ctx, p);
}

/* The codec memory budget, expressed as proven's allocator trait (fa_budget_as_allocator). Distinct from the
 * fa_malloc seam: a block carries an FA_ALIGN-sized size prefix (holding the whole backing allocation's size) so the
 * trait's free/realloc can credit the live counter -- proven's free_fn is handed only a pointer -- and so the
 * returned pointer stays FA_ALIGN-aligned (PPMd and LZMA state need it). Only fundamental alignment (<= FA_ALIGN) is
 * served, which is every codec state/buffer. These blocks live and die strictly inside one codec's alloc/free hook;
 * they never reach fa_free or libc free, so the prefix is only ever read back here. */
static proven_result_mem_mut_t budget_alloc(void *ctx, proven_size_t size, proven_size_t align) {
    fa_budget_t *b = ctx;
    proven_result_mem_mut_t r = { .err = PROVEN_ERR_INVALID_ARG };
    if (size == 0 || align > FA_ALIGN) return r;   /* size 0 is a caller bug (as proven's heap); only fundamental align */
    if (size > SIZE_MAX - FA_ALIGN) { r.err = PROVEN_ERR_NOMEM; return r; }
    size_t total = FA_ALIGN + size;
    if (b->limit && (b->live > b->limit || total > b->limit - b->live)) {
        b->exceeded = true;
        r.err = PROVEN_ERR_NOMEM;
        return r;
    }
    proven_result_mem_mut_t br = b->backing.alloc_fn(b->backing.ctx, total, FA_ALIGN);
    if (br.err != PROVEN_OK) return br;
    uint8_t *base = br.value.ptr;
    memcpy(base, &total, sizeof total);   /* sizeof(size_t) <= FA_ALIGN */
    b->live += total;
    r.err = PROVEN_OK;
    r.value.ptr = base + FA_ALIGN;
    r.value.size = size;
    return r;
}

static void budget_free(void *ctx, void *ptr) {
    if (!ptr) return;
    fa_budget_t *b = ctx;
    uint8_t *base = (uint8_t *)ptr - FA_ALIGN;
    size_t total = 0;
    memcpy(&total, base, sizeof total);
    /* live < total is never an input condition: it means a block was freed through the wrong budget or its prefix
     * was corrupted -- a wiring bug in a codec hook. Be loud (proven's panic, the idiom this allocator lives in);
     * do not silently clamp, and never set `exceeded` here (that would mislabel the next error as LIMIT). */
    if (b->live < total) proven_panic("fa budget_free: live underflow (freed through the wrong budget?)");
    else b->live -= total;
    b->backing.free_fn(b->backing.ctx, base);
}

/* The codecs never realloc through the budget, but a proven_allocator_t must be complete and correct: alloc a new
 * block, copy, free the old; on failure the old block is left intact (the trait's failure-atomicity). */
static proven_result_mem_mut_t budget_realloc(void *ctx, void *old_ptr, proven_size_t old_size,
                                              proven_size_t new_size, proven_size_t align) {
    if (!old_ptr) return budget_alloc(ctx, new_size, align);
    if (new_size == 0) { budget_free(ctx, old_ptr); return (proven_result_mem_mut_t){ .err = PROVEN_OK }; }
    size_t total = 0;
    memcpy(&total, (uint8_t *)old_ptr - FA_ALIGN, sizeof total);
    size_t real_old = total - FA_ALIGN;   /* the size we actually handed out; the block's own record */
    if (old_size != real_old)             /* proven's contract: a wrong old_size is a caller bug, not a resize */
        return (proven_result_mem_mut_t){ .err = PROVEN_ERR_INVALID_ARG };
    proven_result_mem_mut_t r = budget_alloc(ctx, new_size, align);
    if (r.err != PROVEN_OK) return r;     /* failure atomicity: the old block is left intact */
    memcpy(r.value.ptr, old_ptr, real_old < new_size ? real_old : new_size);
    budget_free(ctx, old_ptr);
    return r;
}

proven_allocator_t fa_budget_as_allocator(fa_budget_t *b) {
    return (proven_allocator_t){ .ctx = b, .alloc_fn = budget_alloc,
                                 .realloc_fn = budget_realloc, .free_fn = budget_free };
}

/* Re-arm a budget for a fresh decode: clear the sticky `exceeded` so a refusal in the previous decode cannot
 * mislabel this one. `live` is already 0 between decodes (each codec frees its own buffers on destroy). */
void fa_budget_arm(fa_budget_t *b) { b->exceeded = false; }

/* Bridge a caller's plain-pointer allocator to proven's result-returning trait. The trait's ctx carries the public
 * allocator struct, which lives on the archive handle (arc->opt.allocator) for the handle's lifetime. */
static proven_result_mem_mut_t user_alloc(void *ctx, proven_size_t size, proven_size_t align) {
    const fulta_arc_allocator_t *ua = ctx;
    proven_result_mem_mut_t r = { .err = PROVEN_ERR_NOMEM };
    void *p = ua->alloc(ua->ctx, size, align);
    if (p) { r.err = PROVEN_OK; r.value.ptr = p; r.value.size = size; }
    return r;
}
static proven_result_mem_mut_t user_realloc(void *ctx, void *old_ptr, proven_size_t old_size,
                                            proven_size_t new_size, proven_size_t align) {
    const fulta_arc_allocator_t *ua = ctx;
    proven_result_mem_mut_t r = { .err = PROVEN_ERR_NOMEM };
    void *p = ua->realloc(ua->ctx, old_ptr, old_size, new_size, align);
    if (p) { r.err = PROVEN_OK; r.value.ptr = p; r.value.size = new_size; }
    else if (new_size == 0) r.err = PROVEN_OK;   /* a free, not a failure */
    return r;
}
static void user_free(void *ctx, void *ptr) {
    const fulta_arc_allocator_t *ua = ctx;
    ua->free(ua->ctx, ptr);
}
proven_allocator_t fa_allocator_from_user(const fulta_arc_allocator_t *ua) {
    return (proven_allocator_t){ .ctx = (void *)ua, .alloc_fn = user_alloc,
                                 .realloc_fn = user_realloc, .free_fn = user_free };
}

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

typedef struct limit_stream { fa_stream_t base; fa_stream_t *in; uint64_t left; bool strict, drained; } limit_stream_t;

/* strict: the declared size is reached; draw one more inner byte so the inner decoder validates whatever follows
 * the data (e.g. AZO's end record), and reject anything that does not cleanly end there. Done eagerly, the moment
 * `left` hits 0, because the consumer (fa_pump) stops reading once it has the declared size and would never give us
 * a later read to trigger on. */
static fulta_arc_err_t limit_drain(limit_stream_t *l) {
    if (!l->strict || l->drained) return FULTA_ARC_OK;
    l->drained = true;
    uint8_t x;
    size_t g = 0;
    fulta_arc_err_t e = l->in->read(l->in, &x, 1, &g);
    if (e) return e;
    if (g) return FULTA_ARC_ERR_CORRUPT;                 /* more data than the declared size */
    return FULTA_ARC_OK;
}

static fulta_arc_err_t limit_read(fa_stream_t *s, void *buf, size_t n, size_t *got) {
    limit_stream_t *l = (limit_stream_t *)s;
    *got = 0;
    if (!l->left) return limit_drain(l);                 /* size was 0, or a later read after the last bytes */
    if (n > l->left) n = (size_t)l->left;
    fulta_arc_err_t e = l->in->read(l->in, buf, n, got);
    if (e) return e;
    if (!*got) return FULTA_ARC_ERR_TRUNCATED;
    l->left -= *got;
    if (!l->left) { fulta_arc_err_t de = limit_drain(l); if (de) return de; }   /* reached the size: validate now */
    return FULTA_ARC_OK;
}

static void limit_destroy(fa_stream_t *s) {
    limit_stream_t *l = (limit_stream_t *)s;
    fa_stream_destroy(l->in);
    fa_free(l);
}

static fulta_arc_err_t limit_make(fa_stream_t *in, uint64_t size, bool strict, fa_stream_t **out) {
    limit_stream_t *l = fa_calloc(1, sizeof *l);
    if (!l) { fa_stream_destroy(in); return FULTA_ARC_ERR_NOMEM; }
    l->in = in;
    l->left = size;
    l->strict = strict;
    l->base.read = limit_read;
    l->base.destroy = limit_destroy;
    *out = &l->base;
    return FULTA_ARC_OK;
}

fulta_arc_err_t fa_stream_limit(fa_stream_t *in, uint64_t size, fa_stream_t **out) {
    return limit_make(in, size, false, out);
}

/* Like fa_stream_limit, but after `size` bytes it draws one more byte from `in` and fails (CORRUPT) unless `in`
 * is cleanly at its end there — so a decoder with a trailer (AZO's end record) still validates it even though the
 * consumer stops at the declared size. */
fulta_arc_err_t fa_stream_limit_strict(fa_stream_t *in, uint64_t size, fa_stream_t **out) {
    return limit_make(in, size, true, out);
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
