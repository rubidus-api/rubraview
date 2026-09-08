#ifndef RUBRAVIEW_PAL_TIME_H
#define RUBRAVIEW_PAL_TIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * High-precision monotonic clock (RFC-0001 §8.2, §3.2.1). Windows uses
 * QueryPerformanceCounter; the host build uses clock_gettime(CLOCK_MONOTONIC).
 * The zero point is arbitrary and fixed for the life of the process — only
 * differences are meaningful. Never goes backwards and is unaffected by
 * wall-clock adjustments, which is what the slide-show timer (RV-032)
 * requires to pace correctly.
 */
double rubraview_pal_time_now_seconds(void);

/** Coarse sleep, used to yield between frames; not a precision timer. */
void rubraview_pal_time_sleep_ms(uint32_t milliseconds);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_TIME_H */
