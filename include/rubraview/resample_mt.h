#ifndef RUBRAVIEW_RESAMPLE_MT_H
#define RUBRAVIEW_RESAMPLE_MT_H

#include "rubraview/core.h"
#include "rubraview/resample.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Tiled multi-threaded resampling (RFC-0001 §6.5.5), RV-067.
 *
 * Resizing a 60-megapixel photograph with Lanczos-3 is the most
 * expensive thing this program does on the CPU, and it divides
 * perfectly: each destination row reads the source and writes only
 * itself. So the destination is cut into horizontal bands and the bands
 * are handed to the worker pool.
 *
 * The result is byte-identical to the single-threaded call. That is not
 * a hope — it is why `rubraview_resample_band` takes the full
 * destination height for its scale, and it is what T041 checks.
 */

typedef struct proven_job_sys proven_job_sys_t;

/**
 * Resample into a new buffer in `arena`, spreading the work over the
 * pool. With `jobs` NULL, or for an image too small to be worth
 * splitting, it runs on the calling thread — the same pixels either way.
 *
 * `band_rows` is the smallest band worth giving to a worker; pass 0 for
 * the default. Handing a thread sixteen rows of work costs more in
 * scheduling than it saves.
 *
 * `worker_count` is how many workers the pool was created with. The job
 * system does not report it, and guessing it wrong is the difference
 * between splitting usefully and splitting pointlessly, so the caller
 * that created the pool says.
 */
rubraview_pixbuf_t rubraview_pixbuf_resample_mt(proven_arena_t *arena,
                                                proven_job_sys_t *jobs,
                                                size_t worker_count,
                                                const rubraview_pixbuf_t *src,
                                                int32_t dst_width, int32_t dst_height,
                                                rubraview_resample_filter_t filter,
                                                int32_t band_rows);

/**
 * How many bands a given destination would be cut into. Exposed because
 * it is the part worth checking: one band means the parallel path
 * degenerated to the serial one, and a band per row means it is
 * scheduling more than it computes.
 */
size_t rubraview_resample_band_count(int32_t dst_height, int32_t band_rows, size_t worker_count);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_RESAMPLE_MT_H */
