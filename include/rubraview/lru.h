#ifndef RUBRAVIEW_LRU_H
#define RUBRAVIEW_LRU_H

#include "rubraview/core.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Memory budget and two-tier LRU cache tracker (RFC-0001 §7.4). This
 * module is a generic, byte-budgeted, intrusive-doubly-linked-list LRU
 * over opaque uint64_t keys — it tracks *which* keys are cached and their
 * byte cost, and tells the caller which keys to evict; it does not own
 * or free the underlying resource (VRAM texture, decoded pixbuf, ...)
 * itself. One instance is Tier 2 (decoded pixbuf cache); Tier 1 (VRAM
 * texture eviction, driven by a GPU callback) is the same tracker used a
 * second time in the Windows milestone (M2+) with its own budget.
 *
 * Key lookup is a linear scan bounded by `max_entries` (the cache holds
 * at most a few hundred entries at realistic viewing/pre-cache scale per
 * the RFC, so this is simpler and just as fast in practice as a hash
 * table, without adding one). Insertion, MRU promotion, and eviction are
 * O(1) via the intrusive linked list.
 */

typedef struct rubraview_cache_entry {
    uint64_t key;
    size_t   bytes;
    int32_t  prev, next; /* intrusive doubly-linked list indices, -1 = none; `next` is reused as the free-list link when the slot is not in use */
    bool     in_use;
} rubraview_cache_entry_t;

typedef struct rubraview_lru_cache {
    rubraview_cache_entry_t *entries; /* arena-allocated, length == capacity */
    size_t   capacity;    /* hard cap on distinct tracked entries, independent of budget_bytes */
    int32_t  head;        /* most-recently-used slot, -1 if empty */
    int32_t  tail;        /* least-recently-used slot, -1 if empty */
    size_t   budget_bytes;
    size_t   used_bytes;
    int32_t  free_head;   /* singly-linked list (via `next`) of unused slots, -1 if full */
} rubraview_lru_cache_t;

rubraview_lru_cache_t rubraview_lru_create(proven_arena_t *arena, size_t max_entries, size_t budget_bytes);

/**
 * Record that `key` (costing `bytes`) was just used: inserts it if new,
 * or updates its size and promotes it to most-recently-used if already
 * present. May evict least-recently-used entries — to free a physical
 * slot when `max_entries` is full, and/or to bring `used_bytes` back
 * under `budget_bytes` — but never evicts the entry just touched, even
 * if its own size exceeds the budget (a single oversized entry is
 * admitted best-effort rather than refused). Evicted keys are written to
 * `out_evicted` (bounded by `out_cap`; pass NULL/0 to ignore them) and
 * the number evicted is returned — the caller must release whatever
 * resource each evicted key names.
 */
size_t rubraview_lru_touch(rubraview_lru_cache_t *cache, uint64_t key, size_t bytes, uint64_t *out_evicted, size_t out_cap);

bool rubraview_lru_contains(const rubraview_lru_cache_t *cache, uint64_t key);

/** Explicitly drop a key (e.g. the caller is freeing it for its own reasons), independent of eviction. */
void rubraview_lru_remove(rubraview_lru_cache_t *cache, uint64_t key);

size_t rubraview_lru_used_bytes(const rubraview_lru_cache_t *cache);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_LRU_H */
