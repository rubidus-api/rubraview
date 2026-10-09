#ifndef RUBRAVIEW_APNG_H
#define RUBRAVIEW_APNG_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * D-84: an animated PNG, taken apart. A PNG decoder that knows nothing of
 * animation reads such a file as its one ordinary picture; the frames sit
 * in chunks it passes over. Nothing is decoded here: the file is walked
 * chunk by chunk, and each frame is written out again as a small PNG of
 * its own — the file's header with the frame's size, the chunks that say
 * how pixels are to be read (palette, transparency, gamma, profile), and
 * the frame's data — which any PNG decoder then reads.
 */

typedef enum rubraview_apng_dispose {
    RUBRAVIEW_APNG_DISPOSE_NONE = 0,
    RUBRAVIEW_APNG_DISPOSE_BACKGROUND,
    RUBRAVIEW_APNG_DISPOSE_PREVIOUS,
} rubraview_apng_dispose_t;

typedef struct rubraview_apng_frame {
    uint32_t width, height;
    uint32_t left, top;
    double   delay_seconds;   /* as the file says; 0 is "as fast as it goes" */
    uint8_t  dispose;         /* rubraview_apng_dispose_t */
    bool     replaces;        /* blend "source": its pixels replace what is under them */
    size_t   data_begin;      /* its data chunks lie in [data_begin, data_end) */
    size_t   data_end;
} rubraview_apng_frame_t;

typedef struct rubraview_apng {
    const uint8_t *data;      /* the file, which the caller keeps alive */
    size_t   size;
    uint32_t width, height;   /* the whole picture */
    size_t   head_begin;      /* the chunks between IHDR and the first IDAT */
    size_t   head_end;
    rubraview_apng_frame_t *frames;
    size_t   frame_count;
} rubraview_apng_t;

/** True for the eight bytes every PNG begins with. */
bool rubraview_apng_is_png(const uint8_t *data, size_t size);

/**
 * Walk the file. False when it is not an animated PNG — an ordinary PNG, a
 * file cut off before any frame, or something else — and then nothing is
 * allocated. A file cut off part-way keeps the frames that are whole.
 */
bool rubraview_apng_parse(const uint8_t *data, size_t size, rubraview_apng_t *out);

void rubraview_apng_free(rubraview_apng_t *apng);

/** How many bytes frame `index` takes as a PNG of its own, 0 when there is none. */
size_t rubraview_apng_frame_png_size(const rubraview_apng_t *apng, size_t index);

/** Write it. Returns the bytes written, 0 when `cap` is too small. */
size_t rubraview_apng_frame_png(const rubraview_apng_t *apng, size_t index, uint8_t *dst, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_APNG_H */
