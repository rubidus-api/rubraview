#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include "rubraview/pal/pal_time.h"

double rubraview_pal_time_now_seconds(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void rubraview_pal_time_sleep_ms(uint32_t milliseconds) {
    struct timespec req = {
        .tv_sec = (time_t)(milliseconds / 1000u),
        .tv_nsec = (long)(milliseconds % 1000u) * 1000000L,
    };
    nanosleep(&req, NULL);
}

#endif /* !_WIN32 */
