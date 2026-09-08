#ifndef RUBRAVIEW_PRECACHE_H
#define RUBRAVIEW_PRECACHE_H

#include "rubraview/core.h"
#include "rubraview/lru.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The asynchronous pre-cache ring (RFC-0001 §3.1, §3.2.1). Decoding the
 * next page while the reader is still looking at this one is what makes
 * a page flip feel instant; the ring decides *which* pages are worth
 * having ready and hands the decoding to worker threads, while the
 * memory budget (RV-028) decides what has to go.
 *
 * §3.1 keeps the next two pages and the previous one. §3.2.1 widens the
 * ring to six or ten during a fast slide show, where a page is on screen
 * for a tenth of a second and disk latency would otherwise show.
 *
 * Threading: every decision here — the window, the budget, the job
 * submission — runs on the calling thread. Only the decode callback runs
 * on a worker, so the caller's decoder is the only thing that has to be
 * thread-safe. (On Windows that means a WIC factory per worker, since
 * WIC objects are not free-threaded.)
 */

typedef struct proven_job_sys proven_job_sys_t;

/** Called on a worker thread to decode one page. */
typedef void (*rubraview_precache_decode_fn)(void *ctx, size_t page_index);

#define RUBRAVIEW_PRECACHE_MAX_WINDOW 16

typedef struct rubraview_precache {
    proven_job_sys_t     *jobs;   /* NULL runs decodes inline on the calling thread */
    rubraview_lru_cache_t *cache; /* the §7.4 budget; NULL means no eviction bookkeeping */
    size_t page_count;
    size_t lookahead;             /* pages after the current one */
    size_t lookbehind;            /* pages before it */
    size_t estimated_bytes;       /* per decoded page, charged to the budget */
    size_t submitted_total;       /* jobs handed to workers since creation, for diagnostics */
} rubraview_precache_t;

rubraview_precache_t rubraview_precache_create(proven_job_sys_t *jobs,
                                               rubraview_lru_cache_t *cache,
                                               size_t page_count,
                                               size_t estimated_bytes);

/**
 * §3.2.1: adapt the ring to the slide-show pace. At or above
 * `fast_threshold` seconds per slide the ring stays at §3.1's 2 ahead
 * and 1 behind; below it the ring widens so the disk is never the thing
 * the reader waits for.
 */
void rubraview_precache_set_interval(rubraview_precache_t *precache,
                                     double slide_interval_seconds,
                                     double fast_threshold_seconds);

/**
 * The pages that should be resident for a given current page, nearest
 * first, clamped to the sequence. Writing the order out explicitly is
 * what lets the caller — and the test — see that the next page is
 * fetched before the one after it.
 */
size_t rubraview_precache_window(const rubraview_precache_t *precache,
                                 size_t current,
                                 size_t *out_indices,
                                 size_t out_capacity);

/**
 * Bring the ring up to date for `current`: charge the window to the
 * budget newest-nearest so the current page is the last thing evicted,
 * and submit a decode job for anything in the window that is not
 * already cached.
 *
 * Returns how many decodes were submitted. Keys the budget evicted are
 * written to `out_evicted`, and how many of them are valid is reported
 * through `out_evicted_count` — the two counts are different things, and
 * a caller that releases resources needs the second one.
 *
 * With no job system the decode runs inline, which is what the test
 * suite uses to keep ordering deterministic.
 */
size_t rubraview_precache_update(rubraview_precache_t *precache,
                                 size_t current,
                                 rubraview_precache_decode_fn decode,
                                 void *ctx,
                                 uint64_t *out_evicted,
                                 size_t evicted_capacity,
                                 size_t *out_evicted_count);

/** True when a page is already resident and needs no decode. */
bool rubraview_precache_is_ready(const rubraview_precache_t *precache, size_t index);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PRECACHE_H */
