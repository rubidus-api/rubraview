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

/* What becomes of a frame's own rectangle before the next one is drawn. */
typedef enum rubraview_frame_disposal {
    RUBRAVIEW_FRAME_KEEP = 0,   /* it stays, and the next is drawn over it */
    RUBRAVIEW_FRAME_CLEAR,      /* its rectangle becomes transparent again */
    RUBRAVIEW_FRAME_RESTORE,    /* what was there before it comes back */
} rubraview_frame_disposal_t;

typedef struct rubraview_frame {
    double delay_seconds; /* §3.20.1: GIF stores this in hundredths of a second */
    int32_t width, height; /* ICO mipmaps differ in size from one another */
    /* D-82: a GIF's frame is often only the part that changed. Where it
       sits on the whole picture, and how it leaves. */
    int32_t left, top;
    uint8_t disposal;      /* rubraview_frame_disposal_t */
    bool    replaces;      /* its pixels replace what is under them, transparent ones too */
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
    /* D-82: an animation is played as a film is. */
    bool   once;         /* stop on the last frame instead of going round */
    bool   ended;        /* it did: playing again starts from the beginning */
    double ab_a, ab_b;   /* A-B repeat in seconds from the start, -1 when unset */
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
    RUBRAVIEW_ANIMATION_ENDED,       /* `once`: stopped on the last frame */
} rubraview_animation_event_t;

/**
 * Advance the clock. Sub-page containers never advance on their own, so
 * this is a no-op for them — a scanned document does not turn its own
 * pages.
 */
rubraview_animation_event_t rubraview_animation_tick(rubraview_animation_t *animation, double delta_seconds);

/** The total length of one pass, which §3.2.6 uses as an animation's dwell time. */
double rubraview_animation_cycle_seconds(const rubraview_animation_t *animation);

/**
 * D-82: where the animation is, in seconds from its first frame, and going
 * there. The time falls inside one frame; a seek shows that frame and keeps
 * the rest of its delay. Past the end is the last frame.
 */
double rubraview_animation_position(const rubraview_animation_t *animation);
void rubraview_animation_seek(rubraview_animation_t *animation, double seconds);

/** Back to the first frame and paused, as a film's Stop. */
void rubraview_animation_stop(rubraview_animation_t *animation);

/*
 * D-82: the whole picture a partial frame belongs to. A GIF's frame may be
 * a rectangle of it with see-through pixels, meant to be laid over the
 * frames before; shown alone it is the wrong size and the wrong colours.
 * The canvas holds the picture as it stands after one frame and reaches
 * another by drawing the frames between, from the nearest frame that needs
 * nothing before it. Pixels are premultiplied BGRA, top row first.
 */
typedef bool (*rubraview_frame_read_fn)(void *ctx, size_t index, uint8_t *dst, size_t stride);

typedef struct rubraview_frame_canvas {
    const rubraview_frame_t *frames;
    size_t   frame_count;
    int32_t  width, height;
    uint8_t *pixels;    /* width * height * 4: the picture */
    uint8_t *backup;    /* the same size: what RESTORE brings back */
    uint8_t *scratch;   /* the same size: one frame as read */
    int64_t  shown;     /* the frame `pixels` stands at, -1 for none */
} rubraview_frame_canvas_t;

/** The caller owns the three buffers, each `width * height * 4` bytes. */
void rubraview_frame_canvas_init(rubraview_frame_canvas_t *canvas, const rubraview_frame_t *frames, size_t frame_count,
                                 int32_t width, int32_t height, uint8_t *pixels, uint8_t *backup, uint8_t *scratch);

/** The frame drawing has to start from to show `index`: one that needs none before it. */
size_t rubraview_frame_canvas_start(const rubraview_frame_canvas_t *canvas, size_t index);

/** Make `pixels` the picture at `index`. False when a frame could not be read. */
bool rubraview_frame_canvas_show(rubraview_frame_canvas_t *canvas, size_t index, rubraview_frame_read_fn read, void *ctx);

/** §3.20.2: the largest frame, which is the one an ICO should open at. */
size_t rubraview_animation_largest_frame(const rubraview_animation_t *animation);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_ANIMATION_H */
