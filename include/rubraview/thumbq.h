#ifndef RUBRAVIEW_THUMBQ_H
#define RUBRAVIEW_THUMBQ_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The picker's thumbnail requests, waiting for the background thread
 * (D-34, owner 2026-09-25: the list first, the pictures added as they are
 * made). No locking here: the thread that owns the queue holds its lock.
 *
 * - The newest request is taken first, so after a scroll the tiles now on
 *   screen are made before the ones scrolled past.
 * - A tile asked for again moves to the front instead of being queued twice.
 * - A full queue drops its oldest request (a tile long scrolled away).
 * - Requests belong to a generation — one folder listing. A new generation
 *   empties the queue, and a request of any other generation is refused.
 */

#define RUBRAVIEW_THUMBQ_PATH 1024

typedef struct rubraview_thumb_job {
    uint32_t generation;
    size_t   index;            /* the tile it is for */
    bool     folder;           /* show the folder's first picture or film */
    double   aspect;           /* the tile's width / height */
    size_t   path_len;
    char     path[RUBRAVIEW_THUMBQ_PATH];   /* UTF-8, not NUL-terminated beyond path_len */
} rubraview_thumb_job_t;

typedef struct rubraview_thumbq {
    rubraview_thumb_job_t *jobs;   /* caller's storage, `capacity` long */
    size_t capacity, count;
    uint32_t generation;
} rubraview_thumbq_t;

/* False when refused: another generation, a path too long, or no storage. */
bool rubraview_thumbq_push(rubraview_thumbq_t *q, uint32_t generation, size_t index, bool folder,
                           double aspect, u8str_t path);

/* The newest request, removed from the queue; false when there is none. */
bool rubraview_thumbq_take(rubraview_thumbq_t *q, rubraview_thumb_job_t *out);

/* A new listing: forget every request of the old one. */
void rubraview_thumbq_set_generation(rubraview_thumbq_t *q, uint32_t generation);

/*
 * Owner, 2026-09-25: an archive's thumbnail is tried within a time limit,
 * and given up as soon as the reading looks too slow to finish in it.
 * After `done` of `total` bytes in `elapsed` seconds, can the rest be read
 * by `budget` at the speed seen so far? Nothing read yet is a yes while
 * there is time left.
 */
bool rubraview_read_budget_ok(uint64_t done, uint64_t total, double elapsed, double budget);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_THUMBQ_H */
