#include "rubraview/progress.h"

/* RV-268: a long job's speed and the time it has left; see the header. */

void rubraview_progress_begin(rubraview_progress_t *p, double now) {
    *p = (rubraview_progress_t){ .started = now, .last_t = now };
}

void rubraview_progress_sample(rubraview_progress_t *p, uint64_t done, double now) {
    if (done < p->last_done) {   /* a new phase from 0: its own speed */
        p->started = p->last_t = now;
        p->last_done = done;
        p->have_rate = false;
        p->rate = 0.0;
        return;
    }
    double dt = now - p->last_t;
    if (dt < 0.5) return;
    double rate = (double)(done - p->last_done) / dt;
    p->rate = p->have_rate ? p->rate * 0.7 + rate * 0.3 : rate;   /* a stretch moves it a little at a time */
    p->have_rate = true;
    p->last_t = now;
    p->last_done = done;
}

double rubraview_progress_left(const rubraview_progress_t *p, uint64_t done, uint64_t total, double now) {
    if (total == 0 || done >= total) return done >= total && total > 0 ? 0.0 : -1.0;
    if (!p->have_rate || p->rate <= 0.0) return -1.0;
    if (now - p->started < 1.0 || done * 100u < total) return -1.0;   /* too early to say */
    return (double)(total - done) / p->rate;
}

int rubraview_progress_percent(uint64_t done, uint64_t total) {
    if (total == 0) return 0;
    if (done >= total) return 100;
    return (int)((double)done * 100.0 / (double)total);
}

void rubraview_progress_split(double seconds, int *hours, int *minutes, int *secs) {
    if (seconds < 0.0) seconds = 0.0;
    if (seconds > 359999.0) seconds = 359999.0;
    long s = (long)seconds;
    if ((double)s < seconds) s++;   /* rounded up */
    *hours = (int)(s / 3600);
    *minutes = (int)(s / 60 % 60);
    *secs = (int)(s % 60);
}
