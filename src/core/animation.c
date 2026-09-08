#include "rubraview/animation.h"

/* §3.20.1's speed ladder. */
static const double SPEEDS[] = { 0.25, 0.5, 1.0, 1.5, 2.0 };
#define SPEED_COUNT ((int)(sizeof(SPEEDS) / sizeof(SPEEDS[0])))

rubraview_animation_t rubraview_animation_create(rubraview_frame_kind_t kind,
                                                  const rubraview_frame_t *frames,
                                                  size_t frame_count) {
    return (rubraview_animation_t){
        .kind = kind,
        .frames = frames,
        .frame_count = frames ? frame_count : 0,
        .current = 0,
        .elapsed = 0.0,
        .speed = 1.0,
        /* A sub-page container is not playing anything, so it starts
           parked rather than paused-and-waiting. */
        .paused = (kind == RUBRAVIEW_FRAMES_SUBPAGES),
        .loops_done = 0,
    };
}

double rubraview_animation_step_speed(double current_speed, bool faster) {
    int nearest = 0;
    double best_distance = -1.0;
    for (int i = 0; i < SPEED_COUNT; ++i) {
        double d = SPEEDS[i] - current_speed;
        if (d < 0.0) d = -d;
        if (best_distance < 0.0 || d < best_distance) {
            best_distance = d;
            nearest = i;
        }
    }

    int next = nearest + (faster ? 1 : -1);
    if (next < 0) next = 0;
    if (next >= SPEED_COUNT) next = SPEED_COUNT - 1;
    return SPEEDS[next];
}

void rubraview_animation_pause(rubraview_animation_t *animation) {
    if (animation) animation->paused = true;
}

void rubraview_animation_resume(rubraview_animation_t *animation) {
    /* Sub-pages have nothing to resume; letting them "play" would turn a
       scanned document's pages by itself. */
    if (animation && animation->kind == RUBRAVIEW_FRAMES_ANIMATION) animation->paused = false;
}

void rubraview_animation_step(rubraview_animation_t *animation, bool forward) {
    if (!animation || animation->frame_count == 0) return;

    /* Stepping is a deliberate act, so it stops playback (§3.20.1). */
    animation->paused = true;
    animation->elapsed = 0.0;

    if (forward) {
        animation->current = (animation->current + 1) % animation->frame_count;
    } else {
        animation->current = (animation->current == 0)
            ? animation->frame_count - 1
            : animation->current - 1;
    }
}

rubraview_animation_event_t rubraview_animation_tick(rubraview_animation_t *animation, double delta_seconds) {
    if (!animation || animation->frame_count == 0) return RUBRAVIEW_ANIMATION_NONE;
    if (animation->kind != RUBRAVIEW_FRAMES_ANIMATION) return RUBRAVIEW_ANIMATION_NONE;
    if (animation->paused || delta_seconds <= 0.0) return RUBRAVIEW_ANIMATION_NONE;

    double speed = animation->speed > 0.0 ? animation->speed : 1.0;
    animation->elapsed += delta_seconds * speed;

    double delay = animation->frames[animation->current].delay_seconds;
    /* A frame with no stated delay would otherwise spin as fast as the
       frame loop; GIF writers routinely leave it at zero. */
    if (delay <= 0.0) delay = 0.1;

    if (animation->elapsed < delay) return RUBRAVIEW_ANIMATION_NONE;

    animation->elapsed = 0.0;
    animation->current++;

    if (animation->current >= animation->frame_count) {
        animation->current = 0;
        animation->loops_done++;
        return RUBRAVIEW_ANIMATION_LOOPED;
    }
    return RUBRAVIEW_ANIMATION_FRAME_CHANGED;
}

double rubraview_animation_cycle_seconds(const rubraview_animation_t *animation) {
    if (!animation || animation->frame_count == 0) return 0.0;
    if (animation->kind != RUBRAVIEW_FRAMES_ANIMATION) return 0.0;

    double total = 0.0;
    for (size_t i = 0; i < animation->frame_count; ++i) {
        double delay = animation->frames[i].delay_seconds;
        if (delay <= 0.0) delay = 0.1;
        total += delay;
    }
    return total;
}

size_t rubraview_animation_largest_frame(const rubraview_animation_t *animation) {
    if (!animation || animation->frame_count == 0) return 0;

    size_t best = 0;
    int64_t best_area = -1;
    for (size_t i = 0; i < animation->frame_count; ++i) {
        int64_t area = (int64_t)animation->frames[i].width * (int64_t)animation->frames[i].height;
        if (area > best_area) {
            best_area = area;
            best = i;
        }
    }
    return best;
}
