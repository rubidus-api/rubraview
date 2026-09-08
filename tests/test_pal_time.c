#include "rubraview/pal/pal_time.h"
#include <stdio.h>
#include <assert.h>

int main(void) {
    printf("[test_pal_time] Starting monotonic clock PAL unit tests...\n");

    /* Test 1: The clock returns a usable value. */
    double t0 = rubraview_pal_time_now_seconds();
    assert(t0 > 0.0);
    printf("  [PASS] Clock returns a positive timestamp\n");

    /* Test 2: The clock never goes backwards across repeated reads. */
    double previous = t0;
    for (int i = 0; i < 1000; ++i) {
        double now = rubraview_pal_time_now_seconds();
        assert(now >= previous);
        previous = now;
    }
    printf("  [PASS] Clock is monotonic across 1000 reads\n");

    /* Test 3: Sleeping advances the clock by roughly the requested time.
       The lower bound is loose (schedulers overshoot, never undershoot by
       much) and there is no upper bound, since a loaded machine may sleep
       far longer -- this checks the clock tracks real elapsed time, not
       that the sleep is precise. */
    double before = rubraview_pal_time_now_seconds();
    rubraview_pal_time_sleep_ms(20);
    double after = rubraview_pal_time_now_seconds();
    double elapsed = after - before;
    assert(elapsed >= 0.010);
    printf("  [PASS] Sleep advances the clock (measured %.1f ms for a 20 ms sleep)\n", elapsed * 1000.0);

    printf("[test_pal_time] All tests passed successfully!\n");
    return 0;
}
