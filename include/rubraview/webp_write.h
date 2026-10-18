#ifndef RUBRAVIEW_WEBP_WRITE_H
#define RUBRAVIEW_WEBP_WRITE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Writing WebP (RV-109, D-106). Windows reads WebP and cannot write it,
 * so the vendored libwebp does: a still picture, or an animation built a
 * frame at a time. Nothing here touches a file or the operating system.
 *
 * Pixels are BGRA (or RGBA: `rgba`), top row first, `stride` bytes a row; `premultiplied`
 * says the colours were already multiplied by their alpha (what the
 * viewer's own buffers hold), and they are divided back before they are
 * written. The bytes handed back are freed with `rubraview_webp_free`.
 */
typedef struct rubraview_webp_options {
    bool    lossless;
    int32_t quality;        /* 1..100; how small a lossless file is tried for, or how good a lossy one is */
    int32_t effort;         /* 0..6: more is slower and smaller */
    bool    premultiplied;
    bool    rgba;           /* the bytes are R, G, B, A in that order, not B, G, R, A */
    /* RV-140: zero-initialised means each as libwebp has it (near-lossless off, alpha 100, sharp YUV off) */
    int32_t near_lossless;  /* lossless: 1..99 lets nearby values change for a smaller file; 0 or 100: off */
    int32_t alpha_quality;  /* lossy: 1..100, how well the transparency is kept; 0: as libwebp has it (100) */
    bool    sharp_yuv;      /* lossy: the slower, sharper RGB to YUV */
} rubraview_webp_options_t;

/** One picture. False for a size WebP cannot hold (above 16383 a side) or no memory. */
bool rubraview_webp_write(const uint8_t *bgra, int32_t width, int32_t height, int32_t stride,
                          const rubraview_webp_options_t *options, uint8_t **out_bytes, size_t *out_len);

typedef struct rubraview_webp_anim rubraview_webp_anim_t;

/** An animation of `width` x `height`; `loop_count` 0 goes round for ever. NULL when it cannot be begun. */
rubraview_webp_anim_t *rubraview_webp_anim_begin(int32_t width, int32_t height, int32_t loop_count,
                                                 const rubraview_webp_options_t *options);

/** The whole picture as it stands for the next `duration_ms` (a frame of 0 is given 10, as browsers do). */
bool rubraview_webp_anim_add(rubraview_webp_anim_t *anim, const uint8_t *bgra, int32_t stride, int32_t duration_ms);

/** The file's bytes; the animation is let go of whether this succeeds or not. */
bool rubraview_webp_anim_end(rubraview_webp_anim_t *anim, uint8_t **out_bytes, size_t *out_len);

/** Give up on an animation that was begun. */
void rubraview_webp_anim_abort(rubraview_webp_anim_t *anim);

void rubraview_webp_free(uint8_t *bytes);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_WEBP_WRITE_H */
