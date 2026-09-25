#include "rubraview/resample_mt.h"
#include "proven/job.h"
#include <stdatomic.h>

/* Below this many destination rows the split is not worth its own
   scheduling; the whole image is done on the calling thread. */
#define RESAMPLE_MIN_ROWS_TO_SPLIT 64
#define RESAMPLE_DEFAULT_BAND_ROWS 64
#define RESAMPLE_MAX_BANDS 64

size_t rubraview_resample_band_count(int32_t dst_height, int32_t band_rows, size_t worker_count) {
    if (dst_height <= 0) return 0;
    if (band_rows <= 0) band_rows = RESAMPLE_DEFAULT_BAND_ROWS;
    if (dst_height < RESAMPLE_MIN_ROWS_TO_SPLIT || worker_count <= 1) return 1;

    size_t bands = (size_t)((dst_height + band_rows - 1) / band_rows);

    /* More bands than workers is fine and helps a ragged image finish
       evenly, but there is no gain past a few per worker. */
    size_t cap = worker_count * 4;
    if (cap > RESAMPLE_MAX_BANDS) cap = RESAMPLE_MAX_BANDS;
    if (bands > cap) bands = cap;
    if (bands == 0) bands = 1;
    return bands;
}

typedef struct band_job {
    const rubraview_pixbuf_t *src;
    rubraview_pixbuf_t *dst;
    rubraview_resample_filter_t filter;
    int32_t y0, y1;
    atomic_int *remaining;
} band_job_t;

static void run_band(void *arg) {
    band_job_t *job = (band_job_t*)arg;
    if (!job) return;
    rubraview_resample_band(job->src, job->dst, job->filter, job->y0, job->y1);
    atomic_fetch_sub(job->remaining, 1);
}

rubraview_pixbuf_t rubraview_pixbuf_resample_mt(proven_arena_t *arena,
                                                proven_job_sys_t *jobs,
                                                size_t worker_count,
                                                const rubraview_pixbuf_t *src,
                                                int32_t dst_width, int32_t dst_height,
                                                rubraview_resample_filter_t filter,
                                                int32_t band_rows) {
    if (!arena || !rubraview_pixbuf_is_valid(src) || dst_width <= 0 || dst_height <= 0) {
        return (rubraview_pixbuf_t){0};
    }

    size_t bands = rubraview_resample_band_count(dst_height, band_rows, jobs ? worker_count : 0);

    if (!jobs || bands <= 1) {
        return rubraview_pixbuf_resample(arena, src, dst_width, dst_height, filter);
    }

    rubraview_pixbuf_t dst = rubraview_pixbuf_create(arena, dst_width, dst_height, src->format);
    if (!rubraview_pixbuf_is_valid(&dst)) return (rubraview_pixbuf_t){0};
    /* D-38: the card takes the whole picture at once, before any banding. */
    if (rubraview_resample_try_accel(src, &dst, filter)) return dst;

    /* The band descriptors live on this stack frame, which outlives the
       jobs because the function does not return until every one of them
       has run. */
    band_job_t descriptors[RESAMPLE_MAX_BANDS];
    atomic_int remaining;
    atomic_init(&remaining, (int)bands);

    int32_t rows_each = (dst_height + (int32_t)bands - 1) / (int32_t)bands;
    size_t submitted = 0;

    for (size_t i = 0; i < bands; ++i) {
        int32_t y0 = (int32_t)i * rows_each;
        int32_t y1 = y0 + rows_each;
        if (y1 > dst_height) y1 = dst_height;

        descriptors[i] = (band_job_t){
            .src = src, .dst = &dst, .filter = filter,
            .y0 = y0, .y1 = y1, .remaining = &remaining,
        };

        if (y0 >= y1) { atomic_fetch_sub(&remaining, 1); continue; }

        if (!proven_job_submit(jobs, run_band, &descriptors[i])) {
            /* The queue is full. Rather than spin, this band is done
               here and now — the work still gets done, just not in
               parallel. */
            run_band(&descriptors[i]);
        }
        submitted++;
    }

    /* Help drain the queue instead of idling, then wait for whatever the
       workers still hold. Both are needed: a job this thread cannot see
       may already be running on a worker. */
    while (atomic_load(&remaining) > 0) {
        if (!proven_job_execute_one(jobs)) {
            /* Nothing left to take; the rest is in flight. */
            if (atomic_load(&remaining) > 0) continue;
        }
    }

    (void)submitted;
    return dst;
}
