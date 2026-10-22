#ifndef RUBRAVIEW_PAL_FFILTER_H
#define RUBRAVIEW_PAL_FFILTER_H

/*
 * RFC-0009 D: FFmpeg's video filters, by `avfilter-12.dll` of the same FFmpeg the user named (Settings ›
 * External). It is optional: without it FFmpeg plays and reads as before and only the filters are missing. The
 * filters run inside the program, as FFmpeg's decoders do (D-8), so a run cannot be cancelled or timed out.
 */

#include "rubraview/core.h"
#include "rubraview/ffilter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum rubraview_ffilter_state {
    RUBRAVIEW_FFILTER_NO_FFMPEG = 0,   /* FFmpeg itself is not loaded */
    RUBRAVIEW_FFILTER_NO_DLL,          /* avfilter-12.dll is not beside the other five, or would not load */
    RUBRAVIEW_FFILTER_OTHER_VERSION,   /* it is another major version than the headers here know */
    RUBRAVIEW_FFILTER_READY,
} rubraview_ffilter_state_t;

rubraview_ffilter_state_t rubraview_pal_ffilter_state(void);

/**
 * The filters with one video input and one video output that change the picture (not those of a graphics card,
 * not the refused ones), each with its options as rows, into `arena`, sorted. False when there are none.
 */
bool rubraview_pal_ffilter_list(proven_arena_t *arena, rubraview_ffilter_catalog_t *out);

/**
 * `graph` on the BGRA picture `bgra` (`width` x `height`, tightly packed), one frame through; the result, of
 * any size, BGRA, malloc'd into `*out`. On failure `said` holds FFmpeg's message (or why), NUL-terminated.
 */
bool rubraview_pal_ffilter_run(const char *graph, const uint8_t *bgra, int32_t width, int32_t height,
                               uint8_t **out, int32_t *out_width, int32_t *out_height, char *said, size_t said_size);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_FFILTER_H */
