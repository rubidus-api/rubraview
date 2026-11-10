/* util.c - FultaArc: memory, buffers, CRC-32, ranges, streams, text helpers. MIT. */
#include "internal.h"

#include "proven/hash.h"
#include "proven/heap.h"
#include "proven/panic.h"

#include <stddef.h>

/* Allocation (D-038): every block is made, resized and released through the proven allocator its owner names
 * (fa_alloc.h); there is no hidden global allocator any more. A block is always at one fixed alignment (FA_ALIGN),
 * as the allocator trait requires, and resized with its true old size. */
void *fa_alloc(proven_allocator_t a, size_t n) {
    proven_result_mem_mut_t r = a.alloc_fn(a.ctx, n ? n : 1, FA_ALIGN);
    return r.err == PROVEN_OK ? r.value.ptr : NULL;
}
void *fa_zalloc(proven_allocator_t a, size_t count, size_t n) {
    if (n && count > SIZE_MAX / n) return NULL;
    size_t total = count * n;
    if (!total) total = 1;
    void *p = fa_alloc(a, total);
    if (p) memset(p, 0, total);
    return p;
}
void *fa_resize(proven_allocator_t a, void *p, size_t old_n, size_t new_n) {
    if (!p) return fa_alloc(a, new_n);
    proven_result_mem_mut_t r = a.realloc_fn(a.ctx, p, old_n ? old_n : 1, new_n ? new_n : 1, FA_ALIGN);
    return r.err == PROVEN_OK ? r.value.ptr : NULL;
}
void fa_release(proven_allocator_t a, void *p) {
    if (p) a.free_fn(a.ctx, p);
}
char *fa_strdup_view(proven_allocator_t a, proven_u8str_view_t s) {
    if (s.size == SIZE_MAX) return NULL;
    char *d = fa_alloc(a, s.size + 1);
    if (!d) return NULL;
    if (s.size) memcpy(d, s.ptr, s.size);
    d[s.size] = 0;
    return d;
}
char *fa_strndup_a(proven_allocator_t a, const char *s, size_t n) {
    if (n == SIZE_MAX) return NULL;
    char *d = fa_alloc(a, n + 1);
    if (!d) return NULL;
    if (n) memcpy(d, s, n);
    d[n] = 0;
    return d;
}
fulta_arc_err_t fa_allocator_pick(const fulta_arc_allocator_t *ua, proven_allocator_t *out) {
    int set = ua ? (ua->alloc != NULL) + (ua->realloc != NULL) + (ua->free != NULL) : 0;
    if (set != 0 && set != 3) return FULTA_ARC_ERR_INVALID_ARG;
    *out = set == 3 ? fa_allocator_from_user(ua) : proven_heap_allocator();
    return FULTA_ARC_OK;
}

/* The codec memory budget, expressed as proven's allocator trait (fa_budget_as_allocator). Distinct from the
 * plain fa_alloc calls: a block carries an FA_ALIGN-sized size prefix (holding the whole backing allocation's size) so the
 * trait's free/realloc can credit the live counter -- proven's free_fn is handed only a pointer -- and so the
 * returned pointer stays FA_ALIGN-aligned (PPMd and LZMA state need it). Only fundamental alignment (<= FA_ALIGN) is
 * served, which is every codec state/buffer. These blocks live and die strictly inside one codec's alloc/free hook;
 * they never reach fa_release, so the prefix is only ever read back here. */
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

fulta_arc_err_t fa_buf_append(fa_buf_t *b, proven_mem_view_t bytes) {
    const void *data = bytes.ptr;
    size_t n = bytes.size;
    if (n > SIZE_MAX - b->size) return FULTA_ARC_ERR_NOMEM;
    if (b->size + n > b->cap) {
        size_t cap = b->cap ? b->cap : 64;
        while (cap < b->size + n) {
            if (cap > SIZE_MAX / 2) { cap = b->size + n; break; }
            cap *= 2;
        }
        uint8_t *p = fa_resize(b->alloc, b->data, b->cap, cap);
        if (!p) return FULTA_ARC_ERR_NOMEM;
        b->data = p;
        b->cap = cap;
    }
    if (n) memcpy(b->data + b->size, data, n);
    b->size += n;
    return FULTA_ARC_OK;
}

fulta_arc_err_t fa_buf_byte(fa_buf_t *b, uint8_t v) { return fa_buf_append(b, fa_bytes(&v, 1)); }

void fa_buf_free(fa_buf_t *b) {
    fa_release(b->alloc, b->data);
    *b = (fa_buf_t){.alloc = b->alloc};
}

/* ---- CRC-32 ------------------------------------------------------------------------------------------------- */

/* CRC-32 (IEEE 802.3, as ZIP): proven's, which reads eight bytes a step. */
uint32_t fa_crc32(uint32_t crc, proven_mem_view_t data) {
    return proven_crc32_update(crc, data);
}

/* ---- ranges -------------------------------------------------------------------------------------------------- */

fulta_arc_err_t fa_source_read_exact(const fulta_arc_source_t *s, uint64_t offset, proven_mem_mut_t buf) {
    uint8_t *p = buf.ptr;
    size_t n = buf.size;
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

fulta_arc_err_t fa_range_read_exact(const fa_range_t *r, uint64_t offset, proven_mem_mut_t buf) {
    uint8_t *p = buf.ptr;
    size_t n = buf.size;
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

fulta_arc_err_t fa_stream_read_exact(fa_stream_t *s, proven_mem_mut_t buf) {
    uint8_t *p = buf.ptr;
    size_t n = buf.size;
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

void *fa_stream_new(proven_allocator_t a, size_t size, fulta_arc_err_t (*read)(fa_stream_t *, void *, size_t, size_t *),
                    void (*destroy)(fa_stream_t *)) {
    fa_stream_t *s = fa_zalloc(a, 1, size);
    if (!s) return NULL;
    s->read = read;
    s->destroy = destroy;
    s->alloc = a;
    return s;
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
    fa_release(s->alloc, r->range.pieces);
    fa_stream_release(s);
}

fulta_arc_err_t fa_stream_range(proven_allocator_t a, const fa_range_t *r, uint64_t offset, uint64_t size,
                                fa_stream_t **out) {
    if (offset > r->size || size > r->size - offset) return FULTA_ARC_ERR_TRUNCATED;
    range_stream_t *s = fa_stream_new(a, sizeof *s, range_stream_read, range_stream_destroy);
    if (!s) return FULTA_ARC_ERR_NOMEM;
    s->range.pieces = fa_zalloc(a, r->count, sizeof *r->pieces);
    if (!s->range.pieces) { fa_stream_release(&s->base); return FULTA_ARC_ERR_NOMEM; }
    memcpy(s->range.pieces, r->pieces, r->count * sizeof *r->pieces);
    s->range.count = r->count;
    s->range.size = r->size;
    s->pos = offset;
    s->end = offset + size;
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

static void mem_stream_destroy(fa_stream_t *s) { fa_stream_release(s); }

fulta_arc_err_t fa_stream_memory(proven_allocator_t a, const void *data, size_t size, fa_stream_t **out) {
    mem_stream_t *m = fa_stream_new(a, sizeof *m, mem_stream_read, mem_stream_destroy);
    if (!m) return FULTA_ARC_ERR_NOMEM;
    m->p = data;
    m->n = size;
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
    fa_stream_release(s);
}

static fulta_arc_err_t limit_make(fa_stream_t *in, uint64_t size, bool strict, fa_stream_t **out) {
    limit_stream_t *l = fa_stream_new(in->alloc, sizeof *l, limit_read, limit_destroy);
    if (!l) { fa_stream_destroy(in); return FULTA_ARC_ERR_NOMEM; }
    l->in = in;
    l->left = size;
    l->strict = strict;
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

int fa_bytes_fill(fa_bytes_t *b) {
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

bool fa_utf8_valid(proven_mem_view_t bytes) {
    const uint8_t *s = bytes.ptr;
    size_t n = bytes.size;
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
    return fa_buf_append(b, fa_bytes(t, n));
}

char *fa_utf16le_to_utf8(proven_allocator_t a, proven_mem_view_t bytes) {
    const uint8_t *s = bytes.ptr;
    size_t units = bytes.size / 2;
    fa_buf_t b = {.alloc = a};
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

/* A path part that Windows would read as "." or "..": nothing left once trailing dots and spaces are trimmed (Win32
 * path normalisation trims them), e.g. "..", ".. ", ". .", "  ". Only a bare "." is harmless (it is skipped). */
static bool part_is_dots(const char *p, size_t len) {
    size_t t = len;
    while (t && (p[t - 1] == '.' || p[t - 1] == ' ')) t--;
    return len && t == 0;
}

bool fa_sanitize_name(char *name) {
    /* split on '/' and '\\', drop empty and "." parts and a leading drive ("X:" or "X:part", which Windows reads
     * as drive-relative), reject a part Windows reads as ".." (part_is_dots), rejoin with '/' */
    size_t w = 0, i = 0, n = strlen(name);
    while (i < n) {
        size_t j = i;
        while (j < n && name[j] != '/' && name[j] != '\\') j++;
        size_t len = j - i;
        const char *part = name + i;
        /* the first part that is kept may not start with a drive, however many empty or "." parts come before it
           and however many drive prefixes it carries ("C:D:x") - CBMC found "//.\\a:x" keeping "a:" */
        while (w == 0 && len >= 2 && part[1] == ':' &&
               ((part[0] >= 'A' && part[0] <= 'Z') || (part[0] >= 'a' && part[0] <= 'z'))) {
            part += 2;
            len -= 2;
        }
        bool skip = len == 0 || (len == 1 && part[0] == '.');
        if (!skip && part_is_dots(part, len)) return false;
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

bool fa_name_part_is_dots(const char *part, size_t len) { return part_is_dots(part, len); }

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
