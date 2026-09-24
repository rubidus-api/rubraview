/*
 * The picker's thumbnail requests (D-34). See thumbq.h.
 */
#include "rubraview/thumbq.h"
#include <string.h>

static void remove_at(rubraview_thumbq_t *q, size_t i) {
    memmove(&q->jobs[i], &q->jobs[i + 1], (q->count - i - 1) * sizeof(q->jobs[0]));
    q->count--;
}

bool rubraview_thumbq_push(rubraview_thumbq_t *q, uint32_t generation, size_t index, bool folder,
                           double aspect, u8str_t path) {
    if (!q || !q->jobs || q->capacity == 0 || generation != q->generation) return false;
    if (path.len == 0 || path.len > RUBRAVIEW_THUMBQ_PATH) return false;
    for (size_t i = 0; i < q->count; ++i) {
        if (q->jobs[i].index == index) { remove_at(q, i); break; }   /* asked again: to the front */
    }
    if (q->count == q->capacity) remove_at(q, 0);                     /* the oldest goes */
    rubraview_thumb_job_t *job = &q->jobs[q->count++];
    job->generation = generation;
    job->index = index;
    job->folder = folder;
    job->aspect = aspect;
    job->path_len = path.len;
    memcpy(job->path, path.ptr, path.len);
    return true;
}

bool rubraview_thumbq_take(rubraview_thumbq_t *q, rubraview_thumb_job_t *out) {
    if (!q || q->count == 0 || !out) return false;
    *out = q->jobs[q->count - 1];
    q->count--;
    return true;
}

void rubraview_thumbq_set_generation(rubraview_thumbq_t *q, uint32_t generation) {
    if (!q) return;
    q->generation = generation;
    q->count = 0;
}

bool rubraview_read_budget_ok(uint64_t done, uint64_t total, double elapsed, double budget) {
    if (elapsed >= budget) return false;
    if (done >= total) return true;
    if (done == 0 || elapsed <= 0.0) return true;
    double speed = (double)done / elapsed;              /* bytes a second so far */
    double rest = (double)(total - done) / speed;
    return elapsed + rest <= budget;
}
