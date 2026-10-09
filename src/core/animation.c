#include "rubraview/animation.h"
#include <string.h>

/* §3.20.1's speed ladder. */
static const double SPEEDS[] = { 0.25, 0.5, 1.0, 1.5, 2.0 };
#define SPEED_COUNT ((int)(sizeof(SPEEDS) / sizeof(SPEEDS[0])))

rubraview_frame_kind_t rubraview_animation_classify(const rubraview_frame_t *frames, size_t frame_count) {
    if (!frames) return RUBRAVIEW_FRAMES_SUBPAGES;
    for (size_t i = 0; i < frame_count; ++i) {
        if (frames[i].delay_seconds > 0.0) return RUBRAVIEW_FRAMES_ANIMATION;
    }
    return RUBRAVIEW_FRAMES_SUBPAGES;
}

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
        .once = false,
        .ended = false,
        .ab_a = -1.0,
        .ab_b = -1.0,
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
    if (!animation || animation->kind != RUBRAVIEW_FRAMES_ANIMATION) return;
    /* At the end, playing again starts from the beginning — as a film does. */
    if (animation->ended) {
        animation->current = 0;
        animation->elapsed = 0.0;
        animation->ended = false;
    }
    animation->paused = false;
}

void rubraview_animation_step(rubraview_animation_t *animation, bool forward) {
    if (!animation || animation->frame_count == 0) return;

    /* Stepping is a deliberate act, so it stops playback (§3.20.1). */
    animation->paused = true;
    animation->elapsed = 0.0;
    animation->ended = false;

    if (forward) {
        animation->current = (animation->current + 1) % animation->frame_count;
    } else {
        animation->current = (animation->current == 0)
            ? animation->frame_count - 1
            : animation->current - 1;
    }
}

static double frame_delay(const rubraview_animation_t *animation, size_t index) {
    double delay = animation->frames[index].delay_seconds;
    /* A frame with no stated delay would otherwise spin as fast as the
       frame loop; GIF writers routinely leave it at zero. */
    return delay > 0.0 ? delay : 0.1;
}

static bool ab_set(const rubraview_animation_t *animation) {
    return animation->ab_a >= 0.0 && animation->ab_b > animation->ab_a;
}

rubraview_animation_event_t rubraview_animation_tick(rubraview_animation_t *animation, double delta_seconds) {
    if (!animation || animation->frame_count == 0) return RUBRAVIEW_ANIMATION_NONE;
    if (animation->kind != RUBRAVIEW_FRAMES_ANIMATION) return RUBRAVIEW_ANIMATION_NONE;
    if (animation->paused || delta_seconds <= 0.0) return RUBRAVIEW_ANIMATION_NONE;

    double speed = animation->speed > 0.0 ? animation->speed : 1.0;
    animation->elapsed += delta_seconds * speed;

    /* What is left over after a frame's delay belongs to the next frame:
       dropped, every frame would last a tick too long and the whole
       animation run slow. A long tick passes several frames, once round
       at the most. */
    rubraview_animation_event_t event = RUBRAVIEW_ANIMATION_NONE;
    for (size_t passed = 0; passed < animation->frame_count; ++passed) {
        double delay = frame_delay(animation, animation->current);
        if (animation->elapsed < delay) break;

        if (animation->current + 1 >= animation->frame_count) {
            animation->loops_done++;
            if (ab_set(animation)) {
                /* B at or past the end: round to A, not to the start. */
                rubraview_animation_seek(animation, animation->ab_a);
                return RUBRAVIEW_ANIMATION_FRAME_CHANGED;
            }
            if (animation->once) {
                animation->elapsed = delay;
                animation->paused = true;
                animation->ended = true;
                return RUBRAVIEW_ANIMATION_ENDED;
            }
            animation->elapsed -= delay;
            animation->current = 0;
            event = RUBRAVIEW_ANIMATION_LOOPED;
        } else {
            animation->elapsed -= delay;
            animation->current++;
            if (event == RUBRAVIEW_ANIMATION_NONE) event = RUBRAVIEW_ANIMATION_FRAME_CHANGED;
        }
    }
    /* More than once round in one tick: the rest is not owed. */
    if (animation->elapsed >= frame_delay(animation, animation->current)) animation->elapsed = 0.0;

    /* D-82 A-B repeat: past B, back to A. */
    if (ab_set(animation) && rubraview_animation_position(animation) >= animation->ab_b) {
        rubraview_animation_seek(animation, animation->ab_a);
        return RUBRAVIEW_ANIMATION_FRAME_CHANGED;
    }
    return event;
}

double rubraview_animation_position(const rubraview_animation_t *animation) {
    if (!animation || animation->frame_count == 0) return 0.0;
    if (animation->kind != RUBRAVIEW_FRAMES_ANIMATION) return 0.0;
    double at = 0.0;
    for (size_t i = 0; i < animation->current && i < animation->frame_count; ++i) at += frame_delay(animation, i);
    double delay = frame_delay(animation, animation->current);
    double within = animation->elapsed < 0.0 ? 0.0 : animation->elapsed;
    return at + (within < delay ? within : delay);
}

void rubraview_animation_seek(rubraview_animation_t *animation, double seconds) {
    if (!animation || animation->frame_count == 0) return;
    if (animation->kind != RUBRAVIEW_FRAMES_ANIMATION) return;
    if (!(seconds > 0.0)) seconds = 0.0;
    animation->ended = false;
    double at = 0.0;
    for (size_t i = 0; i < animation->frame_count; ++i) {
        double delay = frame_delay(animation, i);
        if (seconds < at + delay) {
            animation->current = i;
            animation->elapsed = seconds - at;
            return;
        }
        at += delay;
    }
    animation->current = animation->frame_count - 1;
    animation->elapsed = 0.0;
}

void rubraview_animation_stop(rubraview_animation_t *animation) {
    if (!animation || animation->frame_count == 0) return;
    if (animation->kind != RUBRAVIEW_FRAMES_ANIMATION) return;
    animation->current = 0;
    animation->elapsed = 0.0;
    animation->ended = false;
    animation->paused = true;
}

double rubraview_animation_cycle_seconds(const rubraview_animation_t *animation) {
    if (!animation || animation->frame_count == 0) return 0.0;
    if (animation->kind != RUBRAVIEW_FRAMES_ANIMATION) return 0.0;

    double total = 0.0;
    for (size_t i = 0; i < animation->frame_count; ++i) total += frame_delay(animation, i);
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

/* ---- the whole picture of a partial frame (D-82) ---- */

void rubraview_frame_canvas_init(rubraview_frame_canvas_t *canvas, const rubraview_frame_t *frames, size_t frame_count,
                                 int32_t width, int32_t height, uint8_t *pixels, uint8_t *backup, uint8_t *scratch) {
    if (!canvas) return;
    *canvas = (rubraview_frame_canvas_t){
        .frames = frames, .frame_count = frames ? frame_count : 0,
        .width = width, .height = height,
        .pixels = pixels, .backup = backup, .scratch = scratch,
        .shown = -1,
    };
}

/* The part of a frame's rectangle that lies on the canvas. */
typedef struct frame_rect { int32_t x, y, w, h; } frame_rect_t;

static frame_rect_t rect_on_canvas(const rubraview_frame_canvas_t *canvas, const rubraview_frame_t *frame) {
    int64_t x0 = frame->left, y0 = frame->top;
    int64_t x1 = x0 + frame->width, y1 = y0 + frame->height;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > canvas->width) x1 = canvas->width;
    if (y1 > canvas->height) y1 = canvas->height;
    if (x1 <= x0 || y1 <= y0) return (frame_rect_t){ 0, 0, 0, 0 };
    return (frame_rect_t){ (int32_t)x0, (int32_t)y0, (int32_t)(x1 - x0), (int32_t)(y1 - y0) };
}

static bool covers_canvas(const rubraview_frame_canvas_t *canvas, const rubraview_frame_t *frame) {
    return frame->left <= 0 && frame->top <= 0 &&
           (int64_t)frame->left + frame->width >= canvas->width &&
           (int64_t)frame->top + frame->height >= canvas->height;
}

/* A frame that needs nothing before it: the first, one that replaces the
   whole picture, or one that follows a frame which cleared all of it. */
static bool starts_alone(const rubraview_frame_canvas_t *canvas, size_t index) {
    if (index == 0) return true;
    const rubraview_frame_t *frame = &canvas->frames[index];
    if (frame->replaces && covers_canvas(canvas, frame)) return true;
    const rubraview_frame_t *before = &canvas->frames[index - 1];
    return before->disposal == RUBRAVIEW_FRAME_CLEAR && covers_canvas(canvas, before);
}

size_t rubraview_frame_canvas_start(const rubraview_frame_canvas_t *canvas, size_t index) {
    if (!canvas || canvas->frame_count == 0) return 0;
    if (index >= canvas->frame_count) index = canvas->frame_count - 1;
    while (index > 0 && !starts_alone(canvas, index)) --index;
    return index;
}

bool rubraview_frame_canvas_show(rubraview_frame_canvas_t *canvas, size_t index, rubraview_frame_read_fn read, void *ctx) {
    if (!canvas || !read || !canvas->pixels || !canvas->backup || !canvas->scratch) return false;
    if (index >= canvas->frame_count || canvas->width <= 0 || canvas->height <= 0) return false;
    if (canvas->shown >= 0 && (size_t)canvas->shown == index) return true;

    size_t row = (size_t)canvas->width * 4;
    size_t whole = row * (size_t)canvas->height;
    size_t start = rubraview_frame_canvas_start(canvas, index);
    /* Forward from where the picture stands, when that is nearer. */
    size_t from = (canvas->shown >= 0 && (size_t)canvas->shown < index && (size_t)canvas->shown + 1 >= start)
                ? (size_t)canvas->shown + 1 : start;
    canvas->shown = -1;

    for (size_t f = from; f <= index; ++f) {
        const rubraview_frame_t *frame = &canvas->frames[f];
        if (f == start) {
            memset(canvas->pixels, 0, whole);
        } else {
            /* The frame before leaves as it said it would. */
            const rubraview_frame_t *before = &canvas->frames[f - 1];
            frame_rect_t gone = rect_on_canvas(canvas, before);
            if (before->disposal == RUBRAVIEW_FRAME_CLEAR) {
                for (int32_t y = 0; y < gone.h; ++y) {
                    memset(canvas->pixels + (size_t)(gone.y + y) * row + (size_t)gone.x * 4, 0, (size_t)gone.w * 4);
                }
            } else if (before->disposal == RUBRAVIEW_FRAME_RESTORE) {
                for (int32_t y = 0; y < gone.h; ++y) {
                    size_t at = (size_t)(gone.y + y) * row + (size_t)gone.x * 4;
                    memcpy(canvas->pixels + at, canvas->backup + at, (size_t)gone.w * 4);
                }
            }
        }
        if (frame->disposal == RUBRAVIEW_FRAME_RESTORE) memcpy(canvas->backup, canvas->pixels, whole);

        if (frame->width <= 0 || frame->height <= 0 ||
            frame->width > canvas->width || frame->height > canvas->height) {
            /* A frame larger than the picture it belongs to: the buffer
               is the picture's size, so it is not read at all. */
            return false;
        }
        size_t stride = (size_t)frame->width * 4;
        if (!read(ctx, f, canvas->scratch, stride)) return false;

        frame_rect_t on = rect_on_canvas(canvas, frame);
        for (int32_t y = 0; y < on.h; ++y) {
            const uint8_t *src = canvas->scratch + (size_t)(on.y - frame->top + y) * stride + (size_t)(on.x - frame->left) * 4;
            uint8_t *dst = canvas->pixels + (size_t)(on.y + y) * row + (size_t)on.x * 4;
            if (frame->replaces) {
                memcpy(dst, src, (size_t)on.w * 4);
                continue;
            }
            for (int32_t x = 0; x < on.w; ++x, src += 4, dst += 4) {
                uint32_t a = src[3];
                if (a == 255) {
                    dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = 255;
                } else if (a != 0) {
                    /* Premultiplied over: the frame, and what shows through it. */
                    uint32_t keep = 255 - a;
                    for (int c = 0; c < 4; ++c) {
                        uint32_t v = src[c] + (dst[c] * keep + 127) / 255;
                        dst[c] = (uint8_t)(v > 255 ? 255 : v);
                    }
                }
            }
        }
    }
    canvas->shown = (int64_t)index;
    return true;
}
