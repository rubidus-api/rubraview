#include "rubraview/precache.h"
#include "proven/job.h"
#include <stdatomic.h>
#include <string.h>

/* §3.1's ring: the next two pages and the one behind. */
#define PRECACHE_AHEAD_NORMAL 2
#define PRECACHE_BEHIND_NORMAL 1
/* §3.2.1's widened ring for a fast slide show. */
#define PRECACHE_AHEAD_FAST 8
#define PRECACHE_BEHIND_FAST 2

/*
 * One decode job. The slots are recycled: a viewer that flips through
 * ten thousand pages must not leave ten thousand payloads behind, and
 * the ring is small enough that a fixed pool always suffices.
 */
typedef struct precache_job {
    rubraview_precache_decode_fn decode;
    void  *ctx;
    size_t index;
    /* Claimed by the submitting thread and released by the worker that
       ran it, so the flag is touched from two threads and has to be
       atomic. */
    atomic_bool in_use;
} precache_job_t;

/* One pool for the process: the viewer has a single ring, and a fixed
   pool cannot grow without bound however many pages are flipped. */
static precache_job_t g_job_slots[RUBRAVIEW_PRECACHE_MAX_WINDOW * 4];

static precache_job_t *take_job_slot(void) {
    for (size_t i = 0; i < sizeof(g_job_slots) / sizeof(g_job_slots[0]); ++i) {
        bool expected = false;
        if (atomic_compare_exchange_strong(&g_job_slots[i].in_use, &expected, true)) {
            return &g_job_slots[i];
        }
    }
    return NULL; /* every slot in flight: this page waits for the next update */
}

static void run_precache_job(void *arg) {
    precache_job_t *job = (precache_job_t*)arg;
    if (!job) return;
    if (job->decode) job->decode(job->ctx, job->index);
    atomic_store(&job->in_use, false);
}

rubraview_precache_t rubraview_precache_create(proven_job_sys_t *jobs,
                                               rubraview_lru_cache_t *cache,
                                               size_t page_count,
                                               size_t estimated_bytes) {
    return (rubraview_precache_t){
        .jobs = jobs,
        .cache = cache,
        .page_count = page_count,
        .lookahead = PRECACHE_AHEAD_NORMAL,
        .lookbehind = PRECACHE_BEHIND_NORMAL,
        .estimated_bytes = estimated_bytes,
        .submitted_total = 0,
    };
}

void rubraview_precache_set_interval(rubraview_precache_t *precache,
                                     double slide_interval_seconds,
                                     double fast_threshold_seconds) {
    if (!precache) return;

    if (slide_interval_seconds > 0.0 && slide_interval_seconds < fast_threshold_seconds) {
        precache->lookahead = PRECACHE_AHEAD_FAST;
        precache->lookbehind = PRECACHE_BEHIND_FAST;
    } else {
        precache->lookahead = PRECACHE_AHEAD_NORMAL;
        precache->lookbehind = PRECACHE_BEHIND_NORMAL;
    }
}

size_t rubraview_precache_window(const rubraview_precache_t *precache,
                                 size_t current,
                                 size_t *out_indices,
                                 size_t out_capacity) {
    if (!precache || !out_indices || out_capacity == 0) return 0;
    if (precache->page_count == 0 || current >= precache->page_count) return 0;

    size_t count = 0;

    /* Nearest first: the current page, then alternating forward and
       back, so a short queue still holds what the reader needs soonest. */
    out_indices[count++] = current;

    size_t ahead = 1, behind = 1;
    while (count < out_capacity &&
           (ahead <= precache->lookahead || behind <= precache->lookbehind)) {
        if (ahead <= precache->lookahead) {
            size_t index = current + ahead;
            if (index < precache->page_count) out_indices[count++] = index;
            ahead++;
            if (count >= out_capacity) break;
        }

        if (behind <= precache->lookbehind) {
            if (current >= behind) out_indices[count++] = current - behind;
            behind++;
        }
    }

    return count;
}

bool rubraview_precache_is_ready(const rubraview_precache_t *precache, size_t index) {
    if (!precache || !precache->cache) return false;
    return rubraview_lru_contains(precache->cache, (uint64_t)index);
}

size_t rubraview_precache_update(rubraview_precache_t *precache,
                                 size_t current,
                                 rubraview_precache_decode_fn decode,
                                 void *ctx,
                                 uint64_t *out_evicted,
                                 size_t evicted_capacity,
                                 size_t *out_evicted_count) {
    if (out_evicted_count) *out_evicted_count = 0;
    if (!precache) return 0;

    size_t window[RUBRAVIEW_PRECACHE_MAX_WINDOW];
    size_t window_count = rubraview_precache_window(precache, current, window, RUBRAVIEW_PRECACHE_MAX_WINDOW);
    if (window_count == 0) return 0;

    /* Which pages still need decoding is decided before the budget is
       touched, since touching marks them resident. */
    bool needs_decode[RUBRAVIEW_PRECACHE_MAX_WINDOW] = {0};
    for (size_t i = 0; i < window_count; ++i) {
        needs_decode[i] = !rubraview_precache_is_ready(precache, window[i]);
    }

    /* Charge the budget furthest-first, so the current page ends up the
       most recently used and is the last thing eviction would take. */
    size_t evicted_total = 0;
    if (precache->cache) {
        for (size_t i = window_count; i-- > 0;) {
            uint64_t scratch[8];
            size_t evicted = rubraview_lru_touch(precache->cache, (uint64_t)window[i],
                                                 precache->estimated_bytes, scratch,
                                                 sizeof(scratch) / sizeof(scratch[0]));
            for (size_t e = 0; e < evicted && e < sizeof(scratch) / sizeof(scratch[0]); ++e) {
                if (out_evicted && evicted_total < evicted_capacity) out_evicted[evicted_total] = scratch[e];
                evicted_total++;
            }
        }
    }

    size_t submitted = 0;
    for (size_t i = 0; i < window_count; ++i) {
        if (!needs_decode[i] || !decode) continue;

        if (!precache->jobs) {
            /* No worker pool: decode inline. This is the deterministic
               path the tests use. */
            decode(ctx, window[i]);
            submitted++;
            continue;
        }

        precache_job_t *slot = take_job_slot();
        if (!slot) break; /* the pool is saturated; the next update retries */
        slot->decode = decode;
        slot->ctx = ctx;
        slot->index = window[i];

        if (!proven_job_submit(precache->jobs, run_precache_job, slot)) {
            atomic_store(&slot->in_use, false);
            break; /* the queue is full: stop rather than spin */
        }
        submitted++;
    }

    /* Only the evictions that fitted in the caller's array can be acted
       on; the rest fall out of the window again on a later pass. */
    if (out_evicted_count) {
        *out_evicted_count = evicted_total < evicted_capacity ? evicted_total : evicted_capacity;
    }

    precache->submitted_total += submitted;
    return submitted;
}
