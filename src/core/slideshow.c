#include "rubraview/slideshow.h"

#include <math.h>
#include <stdio.h>

static double dmax(double a, double b) { return a > b ? a : b; }

static double clamp_interval(double seconds) {
    if (seconds < 0.1) return 0.1;
    if (seconds > 300.0) return 300.0;
    return seconds;
}

rubraview_slideshow_t rubraview_slideshow_create(size_t item_count, double interval_seconds, rubraview_loop_policy_t loop) {
    return (rubraview_slideshow_t){
        .interval_seconds = clamp_interval(interval_seconds),
        .loop = loop,
        .paused = false,
        .current_index = 0,
        .item_count = item_count,
        .elapsed_in_item = 0.0,
    };
}

void rubraview_slideshow_set_interval(rubraview_slideshow_t *s, double seconds) {
    if (!s) return;
    s->interval_seconds = clamp_interval(seconds);
}

double rubraview_slideshow_interval_cycle(double seconds) {
    static const double CYCLE[] = { 3.0, 5.0, 10.0, 1.0, 2.0 };
    size_t count = sizeof(CYCLE) / sizeof(CYCLE[0]);
    for (size_t i = 0; i < count; ++i) {
        if (fabs(CYCLE[i] - seconds) < 1e-6) return CYCLE[(i + 1) % count];
    }
    return CYCLE[0];
}

double rubraview_slideshow_interval_nudge(double base_seconds, int steps) {
    double seconds = floor((base_seconds + (double)steps * 0.5) * 2.0 + 0.5) / 2.0;
    if (seconds < 0.5) seconds = 0.5;
    return clamp_interval(seconds);
}

int rubraview_slideshow_interval_text(char *buffer, size_t size, double seconds) {
    if (!buffer || size == 0) return 0;
    int n = snprintf(buffer, size, "%.1f", seconds);
    if (n <= 0 || (size_t)n >= size) return 0;
    if (n >= 2 && buffer[n - 1] == '0' && buffer[n - 2] == '.') { n -= 2; buffer[n] = '\0'; }
    if ((size_t)n + 2 >= size) return 0;
    buffer[n++] = ' ';
    buffer[n++] = 's';
    buffer[n] = '\0';
    return n;
}

void rubraview_slideshow_pause(rubraview_slideshow_t *s) {
    if (s) s->paused = true;
}

void rubraview_slideshow_resume(rubraview_slideshow_t *s) {
    if (s) s->paused = false;
}

double rubraview_slideshow_required_dwell(const rubraview_slideshow_item_t *item, double interval_seconds) {
    if (!item) return interval_seconds;
    switch (item->kind) {
        case RUBRAVIEW_MEDIA_ANIMATED:
            return dmax(interval_seconds, item->duration_seconds);
        case RUBRAVIEW_MEDIA_VIDEO:
            return item->duration_seconds;
        case RUBRAVIEW_MEDIA_STILL:
        default:
            return interval_seconds;
    }
}

rubraview_slideshow_event_t rubraview_slideshow_tick(rubraview_slideshow_t *s, const rubraview_slideshow_item_t *items, double delta_seconds) {
    if (!s || !items || s->paused || s->item_count == 0) return RUBRAVIEW_SLIDESHOW_NONE;

    s->elapsed_in_item += delta_seconds;

    double dwell = rubraview_slideshow_required_dwell(&items[s->current_index], s->interval_seconds);
    if (s->elapsed_in_item < dwell) return RUBRAVIEW_SLIDESHOW_NONE;

    s->elapsed_in_item = 0.0;

    if (s->loop == RUBRAVIEW_LOOP_REPEAT_SINGLE) {
        return RUBRAVIEW_SLIDESHOW_ADVANCED; /* restarts the same item's dwell timer */
    }

    if (s->current_index + 1 < s->item_count) {
        s->current_index += 1;
        return RUBRAVIEW_SLIDESHOW_ADVANCED;
    }

    if (s->loop == RUBRAVIEW_LOOP_ALL) {
        s->current_index = 0;
        return RUBRAVIEW_SLIDESHOW_ADVANCED;
    }

    s->paused = true; /* LOOP_PLAY_ONCE: stop on the last item */
    return RUBRAVIEW_SLIDESHOW_ENDED;
}
