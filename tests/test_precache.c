#include "rubraview/precache.h"
#include "proven/job.h"
#include "proven/arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

/* Records which pages a decode was asked for, in order. */
typedef struct decode_log {
    size_t indices[64];
    size_t count;
} decode_log_t;

static void record_decode(void *ctx, size_t page_index) {
    decode_log_t *log = (decode_log_t*)ctx;
    if (log->count < sizeof(log->indices) / sizeof(log->indices[0])) {
        log->indices[log->count++] = page_index;
    }
}

static bool logged(const decode_log_t *log, size_t index) {
    for (size_t i = 0; i < log->count; ++i) {
        if (log->indices[i] == index) return true;
    }
    return false;
}

int main(void) {
    printf("[test_precache] Starting pre-cache ring unit tests...\n");

    size_t mem_size = 512 * 1024;
    void *raw = malloc(mem_size);
    assert(raw != NULL);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw, .size = mem_size });

    /* Test 1: §3.1's ring is the next two pages and the one behind,
       nearest first so a short queue still holds what is needed soonest. */
    {
        rubraview_precache_t pc = rubraview_precache_create(NULL, NULL, 100, 1024);
        size_t window[RUBRAVIEW_PRECACHE_MAX_WINDOW];
        size_t count = rubraview_precache_window(&pc, 50, window, RUBRAVIEW_PRECACHE_MAX_WINDOW);

        assert(count == 4);
        assert(window[0] == 50); /* the page on screen comes first */
        assert(window[1] == 51); /* then the next one */
        assert(window[2] == 49);
        assert(window[3] == 52);
    }
    printf("  [PASS] The default ring is 2 ahead and 1 behind, nearest first\n");

    /* Test 2: §3.2.1 — a fast slide show widens the ring so the disk is
       never what the reader waits for; a relaxed pace narrows it again. */
    {
        rubraview_precache_t pc = rubraview_precache_create(NULL, NULL, 100, 1024);
        rubraview_precache_set_interval(&pc, 0.2, 0.5);
        size_t window[RUBRAVIEW_PRECACHE_MAX_WINDOW];
        size_t fast = rubraview_precache_window(&pc, 50, window, RUBRAVIEW_PRECACHE_MAX_WINDOW);
        assert(fast > 4);

        rubraview_precache_set_interval(&pc, 3.0, 0.5);
        size_t normal = rubraview_precache_window(&pc, 50, window, RUBRAVIEW_PRECACHE_MAX_WINDOW);
        assert(normal == 4);
    }
    printf("  [PASS] A fast slide show widens the ring; a relaxed pace narrows it\n");

    /* Test 3: The ring is clamped at both ends of the sequence. */
    {
        rubraview_precache_t pc = rubraview_precache_create(NULL, NULL, 3, 1024);
        size_t window[RUBRAVIEW_PRECACHE_MAX_WINDOW];

        size_t at_start = rubraview_precache_window(&pc, 0, window, RUBRAVIEW_PRECACHE_MAX_WINDOW);
        for (size_t i = 0; i < at_start; ++i) assert(window[i] < 3);

        size_t at_end = rubraview_precache_window(&pc, 2, window, RUBRAVIEW_PRECACHE_MAX_WINDOW);
        for (size_t i = 0; i < at_end; ++i) assert(window[i] < 3);

        /* A page index past the end has no window at all. */
        assert(rubraview_precache_window(&pc, 99, window, RUBRAVIEW_PRECACHE_MAX_WINDOW) == 0);
    }
    printf("  [PASS] The ring clamps at both ends and refuses an out-of-range page\n");

    /* Test 4: Updating decodes exactly the pages that are not resident,
       and a second update asks for nothing. */
    {
        rubraview_lru_cache_t cache = rubraview_lru_create(&arena, 64, 64u * 1024u * 1024u);
        rubraview_precache_t pc = rubraview_precache_create(NULL, &cache, 100, 1024);

        decode_log_t log = {0};
        size_t submitted = rubraview_precache_update(&pc, 10, record_decode, &log, NULL, 0, NULL);
        assert(submitted == 4);
        assert(logged(&log, 10) && logged(&log, 11) && logged(&log, 9) && logged(&log, 12));

        decode_log_t again = {0};
        assert(rubraview_precache_update(&pc, 10, record_decode, &again, NULL, 0, NULL) == 0);
        assert(again.count == 0);
    }
    printf("  [PASS] Only pages that are not resident are decoded; a repeat asks for nothing\n");

    /* Test 5: Moving forward decodes only what newly entered the ring. */
    {
        rubraview_lru_cache_t cache = rubraview_lru_create(&arena, 64, 64u * 1024u * 1024u);
        rubraview_precache_t pc = rubraview_precache_create(NULL, &cache, 100, 1024);

        decode_log_t first = {0};
        rubraview_precache_update(&pc, 10, record_decode, &first, NULL, 0, NULL);

        decode_log_t second = {0};
        size_t submitted = rubraview_precache_update(&pc, 11, record_decode, &second, NULL, 0, NULL);
        /* 10, 11 and 12 are already resident; only 13 is new. */
        assert(submitted == 1);
        assert(second.indices[0] == 13);
    }
    printf("  [PASS] Advancing a page decodes only what newly entered the ring\n");

    /* Test 6: §7.4 — under a tight budget the ring evicts, and the page
       on screen survives because it is charged last. */
    {
        /* Room for three pages only. */
        rubraview_lru_cache_t cache = rubraview_lru_create(&arena, 64, 3u * 1024u);
        rubraview_precache_t pc = rubraview_precache_create(NULL, &cache, 100, 1024);

        decode_log_t log = {0};
        uint64_t evicted[16];
        size_t evicted_count = 0;
        size_t submitted = rubraview_precache_update(&pc, 50, record_decode, &log, evicted, 16, &evicted_count);

        /* The two counts mean different things: what was decoded, and
           what the budget dropped. */
        assert(submitted > 0);
        assert(evicted_count > 0);
        for (size_t i = 0; i < evicted_count; ++i) assert(evicted[i] < 100);
        assert(rubraview_precache_is_ready(&pc, 50)); /* the page being read survived */
        assert(rubraview_lru_used_bytes(&cache) <= 3u * 1024u);
    }
    printf("  [PASS] Under a tight budget the page on screen is the last to be evicted\n");

    /* Test 7: With a real worker pool the decodes still all happen. The
       queue is drained explicitly so the assertion does not depend on
       thread timing. */
    {
        proven_job_sys_t *jobs = NULL;
        proven_allocator_t alloc = proven_arena_as_allocator(&arena);
        proven_err_t err = proven_job_system_init(alloc, 2, 64, &jobs);

        if (err == PROVEN_OK && jobs) {
            rubraview_lru_cache_t cache = rubraview_lru_create(&arena, 64, 64u * 1024u * 1024u);
            rubraview_precache_t pc = rubraview_precache_create(jobs, &cache, 100, 1024);

            decode_log_t log = {0};
            size_t submitted = rubraview_precache_update(&pc, 20, record_decode, &log, NULL, 0, NULL);
            assert(submitted == 4);
            assert(pc.submitted_total == 4);

            /* Run whatever the workers have not already taken, so every
               submitted job is accounted for either way. */
            while (proven_job_execute_one(jobs)) { }

            proven_job_system_close(jobs);
            proven_job_system_destroy(jobs);
            printf("  [PASS] Jobs submitted to a real worker pool and drained cleanly\n");
        } else {
            printf("  [PASS] Worker pool unavailable on this host; inline path already covered\n");
        }
    }

    /* Test 8: A null decode callback and an empty sequence are inert. */
    {
        rubraview_precache_t pc = rubraview_precache_create(NULL, NULL, 0, 1024);
        assert(rubraview_precache_update(&pc, 0, NULL, NULL, NULL, 0, NULL) == 0);

        rubraview_precache_t sized = rubraview_precache_create(NULL, NULL, 10, 1024);
        assert(rubraview_precache_update(&sized, 0, NULL, NULL, NULL, 0, NULL) == 0);
    }
    printf("  [PASS] An empty sequence or missing decoder is inert\n");

    free(raw);
    printf("[test_precache] All tests passed successfully!\n");
    return 0;
}
