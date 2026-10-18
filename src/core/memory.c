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
    rubraview_chunk_t *c = (rubraview_chunk_t*)malloc(total);
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

bool rubraview_region_init(rubraview_region_t *region, const char *name, size_t chunk_bytes, size_t limit) {
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
        free(c);
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
        free(c);
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
    if (proven_is_ok(res.err) && old_ptr && old_size) memcpy(res.value.ptr, old_ptr, old_size < new_size ? old_size : new_size);
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

/* ---- the program's memory ---- */

#define MB (1024u * 1024u)
static const size_t SCRATCH_BYTES[RUBRAVIEW_SCRATCH_CLASSES] = { 1u * MB, 4u * MB, 16u * MB, 64u * MB };

bool rubraview_mem_init(rubraview_mem_t *mem) {
    if (!mem) return false;
    *mem = (rubraview_mem_t){0};
    bool ok = rubraview_region_init(&mem->persist, "persist", 64u * MB, (size_t)1024u * MB) &&
              rubraview_region_init(&mem->media, "media", 4u * MB, 256u * MB) &&
              rubraview_region_init(&mem->bgm, "bgm", 4u * MB, 64u * MB) &&
              rubraview_region_init(&mem->recent[0], "recent", 256u * 1024u, 16u * MB) &&
              rubraview_region_init(&mem->recent[1], "recent", 256u * 1024u, 16u * MB) &&
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
    void *block = malloc(bytes);   /* larger than every class: its own, freed when given */
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
        free(scratch->block);
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
    s.windows_live = mem->windows.items.live;
    s.windows_reused = mem->windows.items.reused;
    for (int c = 0; c < RUBRAVIEW_SCRATCH_CLASSES; ++c) s.scratch_live += mem->scratch[c].items.live * mem->scratch[c].block_bytes;
    s.scratch_live += mem->scratch_big;
    s.total_held = mem->persist.held + mem->media.held + mem->bgm.held + mem->recent[0].held + mem->recent[1].held + pool_held(&mem->windows) + mem->scratch_big;
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
