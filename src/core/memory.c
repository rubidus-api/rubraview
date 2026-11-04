#include "rubraview/core.h"
#include "rubraview/memory.h"
#include "proven/arena.h"
#include "proven/heap.h"
#include "proven/types.h"

#include <stdlib.h>
#include <string.h>

/* RFC-0006. A chunk is its header and then its bytes, one malloc. */
struct rubraview_chunk {
    rubraview_chunk_t *prev;
    size_t             size;
    max_align_t        start[];   /* the arena's bytes, aligned for anything */
};

#define REGION_SLACK 64u   /* what an aligned allocation may lose to its alignment */

static rubraview_chunk_t *chunk_new(size_t size) {
    size_t total = 0;
    if (PROVEN_CKD_ADD(&total, sizeof(rubraview_chunk_t), size)) return NULL;
    rubraview_chunk_t *c = (rubraview_chunk_t*)rubraview_alloc(total);
    if (!c) return NULL;
    c->prev = NULL;
    c->size = size;
    return c;
}

static proven_arena_t chunk_arena(rubraview_chunk_t *c) {
    return proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)c->start, .size = c->size });
}

static void note_high_water(rubraview_region_t *r) {
    size_t used = rubraview_region_used(r);
    if (used > r->high_water) r->high_water = used;
}

bool rubraview_region_init(rubraview_region_t *region, proven_u8str_view_t name_view, size_t limit) {
    const char *name = (const char *)name_view.ptr;
    size_t chunk_bytes = (size_t)name_view.size;
    if (!region || chunk_bytes == 0) return false;
    *region = (rubraview_region_t){ .name = name, .chunk_bytes = chunk_bytes, .limit = limit < chunk_bytes ? chunk_bytes : limit };
    rubraview_chunk_t *c = chunk_new(chunk_bytes);
    if (!c) return false;
    region->newest = c;
    region->held = chunk_bytes;
    region->arena = chunk_arena(c);
    return true;
}

size_t rubraview_region_used(const rubraview_region_t *region) {
    return region ? region->used_before + region->arena.offset : 0;
}

bool rubraview_region_ensure(rubraview_region_t *region, size_t need) {
    if (!region || !region->newest) return false;
    size_t want = 0;
    if (PROVEN_CKD_ADD(&want, need, (size_t)REGION_SLACK)) return false;
    size_t room = region->arena.backing.size > region->arena.offset ? region->arena.backing.size - region->arena.offset : 0;
    if (room >= want) return true;
    size_t size = want > region->chunk_bytes ? want : region->chunk_bytes;
    size_t after = 0;
    if (PROVEN_CKD_ADD(&after, region->held, size) || after > region->limit) return false;
    rubraview_chunk_t *c = chunk_new(size);
    if (!c) return false;
    note_high_water(region);
    region->used_before += region->arena.offset;
    c->prev = region->newest;
    region->newest = c;
    region->held = after;
    region->arena = chunk_arena(c);   /* on to a new arena; the old one keeps what it holds */
    return true;
}

void rubraview_region_reset(rubraview_region_t *region) {
    if (!region || !region->newest) return;
    note_high_water(region);
    rubraview_chunk_t *c = region->newest;
    while (c->prev) {
        rubraview_chunk_t *older = c->prev;
        rubraview_free(c);
        c = older;
    }
    region->newest = c;
    region->held = c->size;
    region->used_before = 0;
    region->arena = chunk_arena(c);
}

void rubraview_region_release(rubraview_region_t *region) {
    if (!region) return;
    rubraview_chunk_t *c = region->newest;
    while (c) {
        rubraview_chunk_t *older = c->prev;
        rubraview_free(c);
        c = older;
    }
    region->newest = NULL;
    region->held = region->used_before = 0;
    region->arena = (proven_arena_t){0};
}

static proven_result_mem_mut_t region_alloc(void *ctx, proven_size_t size, proven_size_t align) {
    rubraview_region_t *r = (rubraview_region_t*)ctx;
    proven_result_mem_mut_t res = { .err = PROVEN_ERR_NOMEM };
    size_t need = 0;
    if (!r || PROVEN_CKD_ADD(&need, (size_t)size, (size_t)align) || !rubraview_region_ensure(r, need)) return res;
    res = proven_arena_alloc_aligned(&r->arena, size, align);
    if (proven_is_ok(res.err)) note_high_water(r);
    return res;
}

static proven_result_mem_mut_t region_realloc(void *ctx, void *old_ptr, proven_size_t old_size, proven_size_t new_size, proven_size_t align) {
    proven_result_mem_mut_t res = region_alloc(ctx, new_size, align);
    if (proven_is_ok(res.err) && old_ptr && old_size) proven_sys_mem_copy(res.value.ptr, old_ptr, old_size < new_size ? old_size : new_size);
    return res;
}

static void region_free(void *ctx, void *ptr) {
    (void)ctx; (void)ptr;   /* an arena's: given back when the region is reset */
}

proven_allocator_t rubraview_region_allocator(rubraview_region_t *region) {
    return (proven_allocator_t){ .ctx = region, .alloc_fn = region_alloc, .realloc_fn = region_realloc, .free_fn = region_free };
}

/* ---- a pool of one type ---- */

bool rubraview_type_pool_init(rubraview_type_pool_t *pool, proven_allocator_t base, size_t item_size, size_t stack_room) {
    if (!pool) return false;
    *pool = (rubraview_type_pool_t){ .item_size = item_size };
    return proven_is_ok(proven_pool_init(&pool->pool, base, item_size, 16, stack_room));
}

void *rubraview_type_pool_take(rubraview_type_pool_t *pool) {
    if (!pool) return NULL;
    bool from_stack = pool->pool.bin_len > 0;
    proven_allocator_t a = proven_pool_as_allocator(&pool->pool);
    proven_result_mem_mut_t res = a.alloc_fn(a.ctx, pool->item_size, 16);
    if (!proven_is_ok(res.err)) return NULL;
    pool->live++;
    if (pool->live > pool->peak) pool->peak = pool->live;
    if (from_stack) pool->reused++;
    return res.value.ptr;
}

void rubraview_type_pool_give(rubraview_type_pool_t *pool, void *item) {
    if (!pool || !item) return;
    proven_allocator_t a = proven_pool_as_allocator(&pool->pool);
    a.free_fn(a.ctx, item);   /* onto the stack, or back to the base past its room */
    if (pool->live) pool->live--;
}

void rubraview_type_pool_release(rubraview_type_pool_t *pool) {
    if (!pool) return;
    proven_pool_destroy(&pool->pool);   /* the stack's items back to the base */
    *pool = (rubraview_type_pool_t){0};
}

/* ---- blocks ---- */

bool rubraview_block_pool_init(rubraview_block_pool_t *pool, size_t block_bytes, size_t stack_room) {
    if (!pool || block_bytes == 0) return false;
    pool->block_bytes = block_bytes;
    return rubraview_type_pool_init(&pool->items, proven_heap_allocator(), block_bytes, stack_room);
}

bool rubraview_block_take(rubraview_block_pool_t *pool, proven_arena_t *out_arena, void **out_block) {
    if (!pool || !out_arena || !out_block) return false;
    void *block = rubraview_type_pool_take(&pool->items);
    if (!block) return false;
    *out_block = block;
    *out_arena = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)block, .size = pool->block_bytes });
    return true;
}

void rubraview_block_give(rubraview_block_pool_t *pool, void *block) {
    if (pool && block) rubraview_type_pool_give(&pool->items, block);
}

void rubraview_block_pool_release(rubraview_block_pool_t *pool) {
    if (pool) rubraview_type_pool_release(&pool->items);
}

/* ---- two regions in turn ---- */

bool rubraview_halves_init(rubraview_halves_t *h, proven_u8str_view_t name_view, size_t limit) {
    const char *name = (const char *)name_view.ptr;
    size_t chunk_bytes = (size_t)name_view.size;
    if (!h) return false;
    *h = (rubraview_halves_t){0};
    if (!rubraview_region_init(&h->half[0], rubraview_bytes_view(name, chunk_bytes), limit)) return false;
    if (rubraview_region_init(&h->half[1], rubraview_bytes_view(name, chunk_bytes), limit)) return true;
    rubraview_region_release(&h->half[0]);
    return false;
}

void rubraview_halves_release(rubraview_halves_t *h) {
    if (!h) return;
    rubraview_region_release(&h->half[0]);
    rubraview_region_release(&h->half[1]);
}

proven_arena_t *rubraview_halves_live(rubraview_halves_t *h, size_t need) {
    if (!h) return NULL;
    rubraview_region_t *r = &h->half[h->live & 1];
    return rubraview_region_ensure(r, need) ? &r->arena : NULL;
}

proven_arena_t *rubraview_halves_next(rubraview_halves_t *h, size_t need) {
    if (!h) return NULL;
    rubraview_region_t *r = &h->half[(h->live & 1) ^ 1];
    rubraview_region_reset(r);
    return rubraview_region_ensure(r, need) ? &r->arena : NULL;
}

void rubraview_halves_swap(rubraview_halves_t *h) {
    if (h) h->live ^= 1;
}

void rubraview_halves_reset(rubraview_halves_t *h) {
    if (!h) return;
    rubraview_region_reset(&h->half[0]);
    rubraview_region_reset(&h->half[1]);
}

size_t rubraview_halves_used(const rubraview_halves_t *h) {
    return h ? rubraview_region_used(&h->half[0]) + rubraview_region_used(&h->half[1]) : 0;
}

size_t rubraview_halves_held(const rubraview_halves_t *h) {
    return h ? h->half[0].held + h->half[1].held : 0;
}

/* ---- the program's memory ---- */

#define MB (1024u * 1024u)
static const size_t SCRATCH_BYTES[RUBRAVIEW_SCRATCH_CLASSES] = { 1u * MB, 4u * MB, 16u * MB, 64u * MB };

bool rubraview_mem_init(rubraview_mem_t *mem) {
    if (!mem) return false;
    *mem = (rubraview_mem_t){0};
    bool ok = rubraview_region_init(&mem->persist, rubraview_bytes_view("persist", 64u * MB), (size_t)1024u * MB) &&
              rubraview_region_init(&mem->media, rubraview_bytes_view("media", 4u * MB), 256u * MB) &&
              rubraview_region_init(&mem->bgm, rubraview_bytes_view("bgm", 4u * MB), 64u * MB) &&
              rubraview_region_init(&mem->recent[0], rubraview_bytes_view("recent", 256u * 1024u), 16u * MB) &&
              rubraview_region_init(&mem->recent[1], rubraview_bytes_view("recent", 256u * 1024u), 16u * MB) &&
              rubraview_halves_init(&mem->settings, rubraview_bytes_view("settings", 256u * 1024u), 16u * MB) &&
              rubraview_halves_init(&mem->subs, rubraview_bytes_view("subs", 4u * MB), 128u * MB) &&
              rubraview_block_pool_init(&mem->windows, 1u * MB, 8);
    for (int c = 0; ok && c < RUBRAVIEW_SCRATCH_CLASSES; ++c)
        ok = rubraview_block_pool_init(&mem->scratch[c], SCRATCH_BYTES[c], c < 2 ? 2 : 1);   /* the big ones are not kept many */
    mem->restart_at = 512u * MB;
    mem->heap_restart_at = (size_t)1536u * MB;
    if (!ok) rubraview_mem_release(mem);
    return ok;
}

void rubraview_mem_release(rubraview_mem_t *mem) {
    if (!mem) return;
    rubraview_region_release(&mem->persist);
    rubraview_region_release(&mem->media);
    rubraview_region_release(&mem->bgm);
    rubraview_region_release(&mem->recent[0]);
    rubraview_region_release(&mem->recent[1]);
    rubraview_halves_release(&mem->settings);
    rubraview_halves_release(&mem->subs);
    rubraview_block_pool_release(&mem->windows);
    for (int c = 0; c < RUBRAVIEW_SCRATCH_CLASSES; ++c) rubraview_block_pool_release(&mem->scratch[c]);
}

bool rubraview_scratch_take(rubraview_mem_t *mem, size_t bytes, rubraview_scratch_t *out) {
    if (!mem || !out || bytes == 0) return false;
    *out = (rubraview_scratch_t){ .cls = -1 };
    for (int c = 0; c < RUBRAVIEW_SCRATCH_CLASSES; ++c) {
        if (bytes > SCRATCH_BYTES[c]) continue;
        if (!rubraview_block_take(&mem->scratch[c], &out->arena, &out->block)) return false;
        out->cls = c;
        out->bytes = SCRATCH_BYTES[c];
        return true;
    }
    void *block = rubraview_alloc(bytes);   /* larger than every class: its own, freed when given */
    if (!block) return false;
    out->block = block;
    out->bytes = bytes;
    out->arena = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)block, .size = bytes });
    mem->scratch_big += bytes;
    return true;
}

void rubraview_scratch_give(rubraview_mem_t *mem, rubraview_scratch_t *scratch) {
    if (!mem || !scratch || !scratch->block) return;
    if (scratch->cls >= 0) {
        rubraview_block_give(&mem->scratch[scratch->cls], scratch->block);
    } else {
        rubraview_free(scratch->block);
        mem->scratch_big -= scratch->bytes <= mem->scratch_big ? scratch->bytes : mem->scratch_big;
    }
    *scratch = (rubraview_scratch_t){ .cls = -1 };
}

proven_arena_t *rubraview_mem_recent_next(rubraview_mem_t *mem) {
    if (!mem) return NULL;
    rubraview_region_t *idle = &mem->recent[mem->recent_live ^ 1];
    rubraview_region_reset(idle);   /* the list before the one in use: nobody holds it any more */
    return &idle->arena;
}

void rubraview_mem_recent_swap(rubraview_mem_t *mem) {
    if (mem) mem->recent_live ^= 1;
}

static size_t pool_held(const rubraview_block_pool_t *p) {
    return (p->items.live + p->items.pool.bin_len) * p->block_bytes;
}

rubraview_mem_stats_t rubraview_mem_stats(const rubraview_mem_t *mem) {
    rubraview_mem_stats_t s = {0};
    if (!mem) return s;
    s.persist_used = rubraview_region_used(&mem->persist);
    s.persist_held = mem->persist.held;
    s.persist_peak = mem->persist.high_water > s.persist_used ? mem->persist.high_water : s.persist_used;
    s.media_used = rubraview_region_used(&mem->media);
    s.media_peak = mem->media.high_water > s.media_used ? mem->media.high_water : s.media_used;
    s.recent_used = rubraview_region_used(&mem->recent[mem->recent_live]);
    s.settings_used = rubraview_halves_used(&mem->settings);
    s.subs_used = rubraview_halves_used(&mem->subs);
    s.windows_live = mem->windows.items.live;
    s.windows_reused = mem->windows.items.reused;
    for (int c = 0; c < RUBRAVIEW_SCRATCH_CLASSES; ++c) s.scratch_live += mem->scratch[c].items.live * mem->scratch[c].block_bytes;
    s.scratch_live += mem->scratch_big;
    s.total_held = mem->persist.held + mem->media.held + mem->bgm.held + mem->recent[0].held + mem->recent[1].held + rubraview_halves_held(&mem->settings) + rubraview_halves_held(&mem->subs) + pool_held(&mem->windows) + mem->scratch_big;
    for (int c = 0; c < RUBRAVIEW_SCRATCH_CLASSES; ++c) s.total_held += pool_held(&mem->scratch[c]);
    return s;
}

rubraview_mem_pressure_t rubraview_mem_pressure(const rubraview_mem_t *mem) {
    if (!mem) return RUBRAVIEW_MEM_OK;
    if (mem->full) return RUBRAVIEW_MEM_FULL;
    rubraview_mem_stats_t s = rubraview_mem_stats(mem);
    if (s.persist_used >= mem->restart_at || s.total_held >= mem->heap_restart_at) return RUBRAVIEW_MEM_HIGH;
    return RUBRAVIEW_MEM_OK;
}

/* ---- RV-220 (D-275): the program's heap, as proven's allocator trait over the C runtime's malloc family ---- */

static proven_result_mem_mut_t heap_alloc(void *ctx, proven_size_t size, proven_size_t align) {
    (void)ctx;
    if (size == 0 || align > (proven_size_t)alignof(max_align_t)) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_INVALID_ARG };
    void *p = malloc((size_t)size);
    return p ? (proven_result_mem_mut_t){ .err = PROVEN_OK, .value = { .ptr = p, .size = size } }
             : (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
}

static proven_result_mem_mut_t heap_realloc(void *ctx, void *old_ptr, proven_size_t old_size, proven_size_t new_size, proven_size_t align) {
    (void)ctx; (void)old_size;
    if (align > (proven_size_t)alignof(max_align_t)) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_INVALID_ARG };
    if (new_size == 0) { free(old_ptr); return (proven_result_mem_mut_t){ .err = PROVEN_OK }; }
    void *p = realloc(old_ptr, (size_t)new_size);
    return p ? (proven_result_mem_mut_t){ .err = PROVEN_OK, .value = { .ptr = p, .size = new_size } }
             : (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
}

static void heap_free(void *ctx, void *ptr) {
    (void)ctx;
    free(ptr);
}

proven_allocator_t rubraview_heap(void) {
    return (proven_allocator_t){ .ctx = NULL, .alloc_fn = heap_alloc, .realloc_fn = heap_realloc, .free_fn = heap_free };
}

void *rubraview_alloc(proven_size_t bytes) {
    proven_allocator_t h = rubraview_heap();
    proven_result_mem_mut_t r = h.alloc_fn(h.ctx, bytes ? bytes : 1, (proven_size_t)alignof(max_align_t));
    return r.err == PROVEN_OK ? r.value.ptr : NULL;
}

void *rubraview_alloc_zero(proven_size_t count, proven_size_t size) {
    proven_size_t bytes = 0;
    if (PROVEN_CKD_MUL(&bytes, count, size)) return NULL;
    void *p = rubraview_alloc(bytes);
    if (p) proven_sys_mem_set(p, 0, bytes ? bytes : 1);
    return p;
}

void *rubraview_realloc(proven_mem_mut_t block_view) {
    void *block = (void *)block_view.ptr;
    proven_size_t bytes = (proven_size_t)block_view.size;
    proven_allocator_t h = rubraview_heap();
    proven_result_mem_mut_t r = h.realloc_fn(h.ctx, block, 0, bytes ? bytes : 1, (proven_size_t)alignof(max_align_t));
    return r.err == PROVEN_OK ? r.value.ptr : NULL;
}

void rubraview_free(void *block) {
    if (!block) return;
    proven_allocator_t h = rubraview_heap();
    h.free_fn(h.ctx, block);
}

char *rubraview_strndup_view(proven_u8str_view_t text) {
    char *copy = (char *)rubraview_alloc(text.size + 1);
    if (!copy) return NULL;
    if (text.size) proven_sys_mem_copy(copy, text.ptr, text.size);
    copy[text.size] = '\0';
    return copy;
}

char *rubraview_strdup(const char *text) {
    return text ? rubraview_strndup_view(proven_u8str_view_from_cstr(text)) : NULL;
}

int rubraview_cstr_cmp_n(const char *a, proven_u8str_view_t b_view) {
    const char *b = (const char *)b_view.ptr;
    proven_size_t n = (proven_size_t)b_view.size;
    proven_u8str_view_t x = proven_u8str_view_from_cstr(a ? a : ""), y = proven_u8str_view_from_cstr(b ? b : "");
    if (x.size > n) x.size = n;
    if (y.size > n) y.size = n;
    return proven_u8str_view_cmp(x, y);
}

int rubraview_cstr_cmp_ci(const char *a, const char *b) {
    const unsigned char *x = (const unsigned char *)(a ? a : ""), *y = (const unsigned char *)(b ? b : "");
    for (;; ++x, ++y) {
        int cx = *x >= 'A' && *x <= 'Z' ? *x + 32 : *x, cy = *y >= 'A' && *y <= 'Z' ? *y + 32 : *y;
        if (cx != cy || !cx) return cx - cy;
    }
}

/* RV-220: printf's %.Ng through proven's formatter. */
static void strip_fraction_zeros(char *s) {
    char *e = s;
    while (*e && *e != 'e') e++;   /* the mantissa ends at an exponent, if any */
    char *dot = NULL;
    for (char *p = s; p < e; ++p) if (*p == '.') dot = p;
    if (!dot) return;
    char *last = e - 1;
    while (last > dot && *last == '0') last--;
    if (last == dot) last--;
    size_t tail = proven_cstr_len(e);
    proven_sys_mem_move(last + 1, e, tail + 1);
}

const char *rubraview_g_text(char *buf, double value, int sig) {
    static const char *const FIXED[18] = { "{:.0}", "{:.1}", "{:.2}", "{:.3}", "{:.4}", "{:.5}", "{:.6}", "{:.7}", "{:.8}",
                                           "{:.9}", "{:.10}", "{:.11}", "{:.12}", "{:.13}", "{:.14}", "{:.15}", "{:.16}", "{:.17}" };
    static const char *const SCI[18] = { "{:.0e}", "{:.1e}", "{:.2e}", "{:.3e}", "{:.4e}", "{:.5e}", "{:.6e}", "{:.7e}", "{:.8e}",
                                         "{:.9e}", "{:.10e}", "{:.11e}", "{:.12e}", "{:.13e}", "{:.14e}", "{:.15e}", "{:.16e}", "{:.17e}" };
    const size_t cap = 32;
    if (sig < 1) sig = 1;
    if (sig > 17) sig = 17;
    if (value != value) { proven_sys_mem_copy(buf, "nan", 4); return buf; }
    if (value == 0.0) {
        bool negative = 1.0 / value < 0.0;
        proven_sys_mem_copy(buf, negative ? "-0" : "0", negative ? 3 : 2);
        return buf;
    }
    if (value > 1.7976931348623157e308 || value < -1.7976931348623157e308) {
        proven_sys_mem_copy(buf, value > 0 ? "inf" : "-inf", value > 0 ? 4 : 5);
        return buf;
    }
    /* the exponent as printf's %e would show it after rounding to `sig` digits */
    char sci[40];
    (void)proven_fmt_to(sci, sizeof sci, SCI[sig - 1], PROVEN_ARG(value));
    int exponent = 0;
    for (char *p = sci; *p; ++p) {
        if (*p == 'e') {
            int sign = 1, at = 1;
            if (p[1] == '-') { sign = -1; at = 2; } else if (p[1] == '+') at = 2;
            for (char *q = p + at; *q >= '0' && *q <= '9'; ++q) exponent = exponent * 10 + (*q - '0');
            exponent *= sign;
            break;
        }
    }
    if (exponent < -4 || exponent >= sig) {
        (void)proven_fmt_to(buf, cap, SCI[sig - 1], PROVEN_ARG(value));
    } else {
        int decimals = sig - 1 - exponent;
        if (decimals > 17) decimals = 17;
        (void)proven_fmt_to(buf, cap, FIXED[decimals], PROVEN_ARG(value));
    }
    strip_fraction_zeros(buf);
    return buf;
}
