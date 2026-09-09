#ifndef RUBRAVIEW_ANIMATION_H
#define RUBRAVIEW_ANIMATION_H

#include "rubraview/core.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Animated images and multi-frame containers (RFC-0001 §3.20). Two
 * different things share one shape here:
 *
 *  - §3.20.1 animated GIF, WebP and APNG, where frames advance on their
 *    own timing and the reader can pause, step and change speed;
 *  - §3.20.2 multi-page TIFF and multi-resolution ICO, where the
 *    "frames" are sub-pages the reader steps through deliberately and
 *    nothing advances by itself.
 *
 * The frame data and its decoding belong to the image PAL; the timing,
 * the stepping and the loop accounting are here, driven by an injected
 * delta so they can be tested without a clock.
 */

typedef enum rubraview_frame_kind {
    RUBRAVIEW_FRAMES_ANIMATION = 0, /* advances on its own timing */
    RUBRAVIEW_FRAMES_SUBPAGES,      /* stepped by the reader (TIFF pages, ICO sizes) */
} rubraview_frame_kind_t;

typedef struct rubraview_frame {
    double delay_seconds; /* §3.20.1: GIF stores this in hundredths of a second */
    int32_t width, height; /* ICO mipmaps differ in size from one another */
} rubraview_frame_t;

typedef struct rubraview_animation {
    rubraview_frame_kind_t kind;
    const rubraview_frame_t *frames;
    size_t frame_count;

    size_t current;
    double elapsed;      /* within the current frame */
    double speed;        /* §3.20.1: 0.25x to 2.0x */
    bool   paused;
    size_t loops_done;   /* completed passes, which the slide show waits for (§3.2.6) */
} rubraview_animation_t;

/**
 * Decide which of the two kinds a multi-frame file is, from the frames
 * themselves. The container does not say: what separates an animated
 * GIF from a scanned TIFF or an icon is that one states timing and the
 * others state none. A single frame is neither — it is an ordinary
 * image, and the caller should not build an animation for it.
 */
rubraview_frame_kind_t rubraview_animation_classify(const rubraview_frame_t *frames, size_t frame_count);

rubraview_animation_t rubraview_animation_create(rubraview_frame_kind_t kind,
                                                  const rubraview_frame_t *frames,
                                                  size_t frame_count);

/**
 * §3.20.1's speed cycle: 0.25x, 0.5x, 1.0x, 1.5x, 2.0x. Stepping past
 * either end stays there rather than wrapping, so holding the key does
 * not jump from fastest to slowest.
 */
double rubraview_animation_step_speed(double current_speed, bool faster);

void rubraview_animation_pause(rubraview_animation_t *animation);
void rubraview_animation_resume(rubraview_animation_t *animation);

/** Precision stepping (`.` and `,`), which also pauses playback. */
void rubraview_animation_step(rubraview_animation_t *animation, bool forward);

typedef enum rubraview_animation_event {
    RUBRAVIEW_ANIMATION_NONE = 0,
    RUBRAVIEW_ANIMATION_FRAME_CHANGED,
    RUBRAVIEW_ANIMATION_LOOPED,      /* wrapped past the last frame */
} rubraview_animation_event_t;

/**
 * Advance the clock. Sub-page containers never advance on their own, so
 * this is a no-op for them — a scanned document does not turn its own
 * pages.
 */
rubraview_animation_event_t rubraview_animation_tick(rubraview_animation_t *animation, double delta_seconds);

/** The total length of one pass, which §3.2.6 uses as an animation's dwell time. */
double rubraview_animation_cycle_seconds(const rubraview_animation_t *animation);

/** §3.20.2: the largest frame, which is the one an ICO should open at. */
size_t rubraview_animation_largest_frame(const rubraview_animation_t *animation);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_ANIMATION_H */
