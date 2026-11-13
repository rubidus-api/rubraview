#ifndef RUBRAVIEW_PAL_TIME_H
#define RUBRAVIEW_PAL_TIME_H

#include <stdbool.h>
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

/** A sleep, used to yield between frames; under 16 ms on the high-resolution timer where Windows has one (RV-273). */
void rubraview_pal_time_sleep_ms(uint32_t milliseconds);

/**
 * RV-215: fine waits while `fine` (a film or an animation playing, the screen moving), Windows' own otherwise.
 * Windows' default tick is 15.6 ms: a sleep of 4 ms, or a wait of 2, lasted a whole tick, and a film's frames and a
 * picture's movement came a tick late now and then. Cheap to call every pass. Returns what waits are now
 * (D-248: the information window's Timer row). RV-273 (D-337): the short waits of rubraview_pal_window_wait_seconds
 * and rubraview_pal_time_sleep_ms use a high-resolution waitable timer where Windows has one (10 1803 on); the
 * system's timer is still set to 1 ms while `fine` (T405), unless RUBRAVIEW_TIMER_NO_PERIOD asks to compare without.
 */
typedef enum rubraview_timer_kind {
    RUBRAVIEW_TIMER_TICK = 0,    /* Windows' default tick */
    RUBRAVIEW_TIMER_PERIOD = 1,  /* the system's timer at 1 ms */
    RUBRAVIEW_TIMER_HIGH = 2,    /* the high-resolution waitable timer, the system's timer untouched */
} rubraview_timer_kind_t;
rubraview_timer_kind_t rubraview_pal_time_fine(bool fine);
/** RV-273: whether short waits can use the high-resolution waitable timer (and are let to). */
bool rubraview_pal_time_high_resolution(void);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_TIME_H */
