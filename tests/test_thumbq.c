#include "rubraview/thumbq.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    printf("[test_thumbq] Starting thumbnail request queue tests...\n");
    static rubraview_thumb_job_t storage[3];
    rubraview_thumbq_t q = { .jobs = storage, .capacity = 3 };
    rubraview_thumbq_set_generation(&q, 7);
    rubraview_thumb_job_t job;

    assert(!rubraview_thumbq_take(&q, &job));
    assert(rubraview_thumbq_push(&q, 7, 0, false, 1.5, U8("C:\\a\\0.jpg")));
    assert(rubraview_thumbq_push(&q, 7, 1, true, 1.5, U8("C:\\a\\sub")));
    assert(rubraview_thumbq_take(&q, &job) && job.index == 1 && job.folder && job.path_len == 8 &&
           memcmp(job.path, "C:\\a\\sub", 8) == 0);
    assert(rubraview_thumbq_take(&q, &job) && job.index == 0 && job.aspect == 1.5);
    assert(!rubraview_thumbq_take(&q, &job));
    printf("  [PASS] The newest request is taken first, with its path and shape\n");

    /* Asked again: once, at the front. */
    assert(rubraview_thumbq_push(&q, 7, 4, false, 1.0, U8("x4")));
    assert(rubraview_thumbq_push(&q, 7, 5, false, 1.0, U8("x5")));
    assert(rubraview_thumbq_push(&q, 7, 4, false, 1.0, U8("x4")));
    assert(q.count == 2);
    assert(rubraview_thumbq_take(&q, &job) && job.index == 4);
    assert(rubraview_thumbq_take(&q, &job) && job.index == 5);
    printf("  [PASS] A tile asked for again moves to the front, not in twice\n");

    /* Full: the oldest is dropped. */
    for (size_t i = 10; i < 14; ++i) assert(rubraview_thumbq_push(&q, 7, i, false, 1.0, U8("p")));
    assert(q.count == 3);
    assert(rubraview_thumbq_take(&q, &job) && job.index == 13);
    assert(rubraview_thumbq_take(&q, &job) && job.index == 12);
    assert(rubraview_thumbq_take(&q, &job) && job.index == 11);
    assert(!rubraview_thumbq_take(&q, &job));
    printf("  [PASS] A full queue drops its oldest request\n");

    /* Another listing: the old requests are gone and refused. */
    assert(rubraview_thumbq_push(&q, 7, 20, false, 1.0, U8("old")));
    rubraview_thumbq_set_generation(&q, 8);
    assert(!rubraview_thumbq_take(&q, &job));
    assert(!rubraview_thumbq_push(&q, 7, 21, false, 1.0, U8("late")));
    assert(rubraview_thumbq_push(&q, 8, 0, false, 1.0, U8("new")));
    printf("  [PASS] A new generation empties the queue and refuses the old one\n");

    /* A path that does not fit, or none, is refused. */
    char big[RUBRAVIEW_THUMBQ_PATH + 1];
    memset(big, 'a', sizeof(big));
    assert(!rubraview_thumbq_push(&q, 8, 1, false, 1.0, (u8str_t){ .ptr = big, .len = sizeof(big) }));
    assert(!rubraview_thumbq_push(&q, 8, 1, false, 1.0, U8("")));
    printf("  [PASS] An empty or overlong path is refused\n");

    /* The reading budget: go on only while the rest fits at this speed. */
    assert(rubraview_read_budget_ok(0, 1000, 0.0, 1.5));
    assert(rubraview_read_budget_ok(500, 1000, 0.5, 1.5));          /* 1 s in all */
    assert(!rubraview_read_budget_ok(100, 1000, 0.5, 1.5));         /* 5 s in all */
    assert(rubraview_read_budget_ok(1000, 1000, 1.4, 1.5));         /* done in time */
    assert(!rubraview_read_budget_ok(1000, 1000, 1.6, 1.5));        /* over, even if done */
    assert(!rubraview_read_budget_ok(0, 1000, 2.0, 1.5));
    printf("  [PASS] Reading goes on only while the rest fits in the time left at the speed seen\n");

    printf("[test_thumbq] All tests passed successfully!\n");
    return 0;
}
