#ifndef RUBRAVIEW_PROGRESS_H
#define RUBRAVIEW_PROGRESS_H

#include "rubraview/core.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-268 (owner, 2026-11-10: "진행할 때 메인 창 안에 진행정도를 표시해주는 카드 창 ... 용량기준 진행률이나
 * 진행숫자(파일기준) 그리고 예상 남은시간 같은 것도 보여주고요"): how fast a long job goes and how long it has left,
 * from its counter sampled as the window draws. The speed is smoothed (a sudden slow or fast stretch moves it a
 * little at a time); nothing is guessed in the first second or before the first percent.
 */
typedef struct rubraview_progress {
    double started;        /* seconds, when it began */
    double last_t;         /* the last sample's time */
    uint64_t last_done;
    double rate;           /* units (bytes, or files) a second, smoothed */
    bool have_rate;
} rubraview_progress_t;

/** Begins counting at `now` (seconds). */
void rubraview_progress_begin(rubraview_progress_t *p, double now);

/**
 * A sample: `done` units at `now`. Taken into the speed once at least half a second has passed since the last one
 * taken; a counter that went back (a new phase began from 0) starts the speed again.
 */
void rubraview_progress_sample(rubraview_progress_t *p, uint64_t done, double now);

/** Seconds left for `total` at the speed known, or -1 while it cannot be said yet. */
double rubraview_progress_left(const rubraview_progress_t *p, uint64_t done, uint64_t total, double now);

/** A whole percent, 0..100 (0 when `total` is 0). */
int rubraview_progress_percent(uint64_t done, uint64_t total);

/** Seconds as hours, minutes and seconds, rounded up to the second (a few seconds left is never "0"). */
void rubraview_progress_split(double seconds, int *hours, int *minutes, int *secs);

#ifdef __cplusplus
}
#endif

#endif
