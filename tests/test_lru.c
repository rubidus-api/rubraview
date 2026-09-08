#include "rubraview/lru.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

int main(void) {
    printf("[test_lru] Starting memory budget / two-tier LRU cache unit tests...\n");

    size_t mem_size = 64 * 1024;
    void *raw_mem = malloc(mem_size);
    assert(raw_mem != NULL);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw_mem, .size = mem_size });

    /* Test 1: Basic insert, contains, and byte accounting. */
    {
        rubraview_lru_cache_t cache = rubraview_lru_create(&arena, 10, 1000);
        size_t evicted = rubraview_lru_touch(&cache, 1, 100, NULL, 0);
        assert(evicted == 0);
        assert(rubraview_lru_contains(&cache, 1));
        assert(!rubraview_lru_contains(&cache, 2));
        assert(rubraview_lru_used_bytes(&cache) == 100);
    }
    printf("  [PASS] Basic insert, contains, and byte accounting\n");

    /* Test 2: Re-touching an existing key updates its size without
       double-counting bytes. */
    {
        rubraview_lru_cache_t cache = rubraview_lru_create(&arena, 10, 1000);
        rubraview_lru_touch(&cache, 1, 100, NULL, 0);
        rubraview_lru_touch(&cache, 1, 150, NULL, 0);
        assert(rubraview_lru_used_bytes(&cache) == 150); /* not 250 */
    }
    printf("  [PASS] Re-touching an existing key updates size, not double-counted\n");

    /* Test 3: Byte-budget eviction removes the least-recently-used entry. */
    {
        rubraview_lru_cache_t cache = rubraview_lru_create(&arena, 10, 250);
        rubraview_lru_touch(&cache, 1 /* A */, 100, NULL, 0);
        rubraview_lru_touch(&cache, 2 /* B */, 100, NULL, 0);

        uint64_t evicted_keys[4];
        size_t evicted = rubraview_lru_touch(&cache, 3 /* C */, 100, evicted_keys, 4);
        assert(evicted == 1);
        assert(evicted_keys[0] == 1); /* A was least-recently-used */
        assert(!rubraview_lru_contains(&cache, 1));
        assert(rubraview_lru_contains(&cache, 2));
        assert(rubraview_lru_contains(&cache, 3));
        assert(rubraview_lru_used_bytes(&cache) == 200);
    }
    printf("  [PASS] Byte-budget eviction removes least-recently-used entry\n");

    /* Test 4: The max_entries slot cap evicts even when the byte budget
       has ample room left. */
    {
        rubraview_lru_cache_t cache = rubraview_lru_create(&arena, 2, 1000000);
        rubraview_lru_touch(&cache, 1, 1, NULL, 0);
        rubraview_lru_touch(&cache, 2, 1, NULL, 0);
        size_t evicted = rubraview_lru_touch(&cache, 3, 1, NULL, 0);
        assert(evicted == 1);
        assert(!rubraview_lru_contains(&cache, 1)); /* evicted to free a physical slot */
        assert(rubraview_lru_contains(&cache, 2));
        assert(rubraview_lru_contains(&cache, 3));
    }
    printf("  [PASS] max_entries hard cap evicts even under budget\n");

    /* Test 5: Re-touching promotes an entry to MRU, protecting it from
       the next eviction in favor of the entry that became LRU instead. */
    {
        rubraview_lru_cache_t cache = rubraview_lru_create(&arena, 10, 250);
        rubraview_lru_touch(&cache, 1 /* A */, 100, NULL, 0);
        rubraview_lru_touch(&cache, 2 /* B */, 100, NULL, 0);
        rubraview_lru_touch(&cache, 1 /* re-touch A: A is now MRU, B is now LRU */, 100, NULL, 0);

        uint64_t evicted_keys[4];
        size_t evicted = rubraview_lru_touch(&cache, 3 /* C */, 100, evicted_keys, 4);
        assert(evicted == 1);
        assert(evicted_keys[0] == 2); /* B evicted, not A */
        assert(rubraview_lru_contains(&cache, 1));
        assert(!rubraview_lru_contains(&cache, 2));
    }
    printf("  [PASS] Re-touching promotes to MRU, changing eviction order\n");

    /* Test 6: Explicit remove drops a key regardless of LRU position and
       updates byte accounting. */
    {
        rubraview_lru_cache_t cache = rubraview_lru_create(&arena, 10, 1000);
        rubraview_lru_touch(&cache, 1, 100, NULL, 0);
        rubraview_lru_touch(&cache, 2, 50, NULL, 0);
        rubraview_lru_remove(&cache, 1);
        assert(!rubraview_lru_contains(&cache, 1));
        assert(rubraview_lru_contains(&cache, 2));
        assert(rubraview_lru_used_bytes(&cache) == 50);
    }
    printf("  [PASS] Explicit remove drops a key and updates byte accounting\n");

    /* Test 7: A single entry larger than the whole budget is still
       admitted best-effort (never evicts the entry just touched). */
    {
        rubraview_lru_cache_t cache = rubraview_lru_create(&arena, 5, 50);
        size_t evicted = rubraview_lru_touch(&cache, 1, 1000, NULL, 0);
        assert(evicted == 0);
        assert(rubraview_lru_contains(&cache, 1));
        assert(rubraview_lru_used_bytes(&cache) == 1000);
    }
    printf("  [PASS] Oversized single entry admitted best-effort, not self-evicted\n");

    /* Test 8: A zero-capacity cache is inert, not a crash. */
    {
        rubraview_lru_cache_t cache = rubraview_lru_create(&arena, 0, 1000);
        size_t evicted = rubraview_lru_touch(&cache, 1, 100, NULL, 0);
        assert(evicted == 0);
        assert(!rubraview_lru_contains(&cache, 1));
        assert(rubraview_lru_used_bytes(&cache) == 0);
    }
    printf("  [PASS] Zero-capacity cache is inert, no crash\n");

    /* Test 9: Repeated insert/evict cycles (beyond capacity many times
       over) leave the cache internally consistent -- exercises the
       free-list reuse path under ASan. */
    {
        rubraview_lru_cache_t cache = rubraview_lru_create(&arena, 4, 400);
        for (uint64_t k = 0; k < 50; ++k) {
            rubraview_lru_touch(&cache, k, 100, NULL, 0);
        }
        /* Only the last 4 touched keys should remain. */
        for (uint64_t k = 0; k < 46; ++k) assert(!rubraview_lru_contains(&cache, k));
        for (uint64_t k = 46; k < 50; ++k) assert(rubraview_lru_contains(&cache, k));
        assert(rubraview_lru_used_bytes(&cache) == 400);
    }
    printf("  [PASS] Many insert/evict cycles remain internally consistent\n");

    free(raw_mem);
    printf("[test_lru] All tests passed successfully!\n");
    return 0;
}
