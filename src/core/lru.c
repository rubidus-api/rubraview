#include "rubraview/lru.h"

rubraview_lru_cache_t rubraview_lru_create(proven_arena_t *arena, size_t max_entries, size_t budget_bytes) {
    rubraview_lru_cache_t cache = {
        .entries = NULL, .capacity = 0, .head = -1, .tail = -1,
        .budget_bytes = budget_bytes, .used_bytes = 0, .free_head = -1,
    };
    if (!arena || max_entries == 0) return cache;

    proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, max_entries, sizeof(rubraview_cache_entry_t));
    if (!proven_is_ok(res.err)) return cache;

    cache.entries = (rubraview_cache_entry_t*)(void*)res.value.ptr;
    cache.capacity = max_entries;

    for (size_t i = 0; i < max_entries; ++i) {
        cache.entries[i] = (rubraview_cache_entry_t){
            .key = 0, .bytes = 0, .prev = -1,
            .next = (i + 1 < max_entries) ? (int32_t)(i + 1) : -1,
            .in_use = false,
        };
    }
    cache.free_head = 0;

    return cache;
}

static int32_t find_slot(const rubraview_lru_cache_t *c, uint64_t key) {
    for (int32_t i = 0; i < (int32_t)c->capacity; ++i) {
        if (c->entries[i].in_use && c->entries[i].key == key) return i;
    }
    return -1;
}

static void unlink_slot(rubraview_lru_cache_t *c, int32_t slot) {
    int32_t p = c->entries[slot].prev;
    int32_t n = c->entries[slot].next;
    if (p != -1) c->entries[p].next = n; else c->head = n;
    if (n != -1) c->entries[n].prev = p; else c->tail = p;
    c->entries[slot].prev = -1;
    c->entries[slot].next = -1;
}

static void push_front_slot(rubraview_lru_cache_t *c, int32_t slot) {
    c->entries[slot].prev = -1;
    c->entries[slot].next = c->head;
    if (c->head != -1) c->entries[c->head].prev = slot;
    c->head = slot;
    if (c->tail == -1) c->tail = slot;
}

static void free_slot(rubraview_lru_cache_t *c, int32_t slot) {
    c->entries[slot].in_use = false;
    c->entries[slot].next = c->free_head;
    c->free_head = slot;
}

static int32_t take_free_slot(rubraview_lru_cache_t *c) {
    int32_t slot = c->free_head;
    c->free_head = c->entries[slot].next;
    return slot;
}

static void record_eviction(rubraview_lru_cache_t *c, int32_t victim, uint64_t *out_evicted, size_t out_cap, size_t *evicted_count) {
    c->used_bytes -= c->entries[victim].bytes;
    uint64_t victim_key = c->entries[victim].key;
    unlink_slot(c, victim);
    free_slot(c, victim);
    if (out_evicted && *evicted_count < out_cap) out_evicted[*evicted_count] = victim_key;
    (*evicted_count)++;
}

size_t rubraview_lru_touch(rubraview_lru_cache_t *cache, uint64_t key, size_t bytes, uint64_t *out_evicted, size_t out_cap) {
    if (!cache || !cache->entries || cache->capacity == 0) return 0;
    size_t evicted_count = 0;

    int32_t slot = find_slot(cache, key);
    if (slot != -1) {
        cache->used_bytes -= cache->entries[slot].bytes;
        cache->entries[slot].bytes = bytes;
        cache->used_bytes += bytes;
        unlink_slot(cache, slot);
        push_front_slot(cache, slot);
    } else {
        if (cache->free_head == -1 && cache->tail != -1) {
            /* Every physical slot is in use: evict exactly one (the hard
               max_entries cap is independent of the byte budget). */
            record_eviction(cache, cache->tail, out_evicted, out_cap, &evicted_count);
        }
        slot = take_free_slot(cache);
        cache->entries[slot].key = key;
        cache->entries[slot].bytes = bytes;
        cache->entries[slot].in_use = true;
        cache->used_bytes += bytes;
        push_front_slot(cache, slot);
    }

    /* Evict least-recently-used entries until back under budget, but
       never the entry just touched (it is always at the head now). */
    while (cache->used_bytes > cache->budget_bytes && cache->tail != -1 && cache->tail != slot) {
        record_eviction(cache, cache->tail, out_evicted, out_cap, &evicted_count);
    }

    return evicted_count;
}

bool rubraview_lru_contains(const rubraview_lru_cache_t *cache, uint64_t key) {
    if (!cache || !cache->entries) return false;
    return find_slot(cache, key) != -1;
}

void rubraview_lru_remove(rubraview_lru_cache_t *cache, uint64_t key) {
    if (!cache || !cache->entries) return;
    int32_t slot = find_slot(cache, key);
    if (slot == -1) return;
    cache->used_bytes -= cache->entries[slot].bytes;
    unlink_slot(cache, slot);
    free_slot(cache, slot);
}

size_t rubraview_lru_used_bytes(const rubraview_lru_cache_t *cache) {
    if (!cache) return 0;
    return cache->used_bytes;
}
