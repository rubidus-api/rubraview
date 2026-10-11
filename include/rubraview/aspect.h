#ifndef RUBRAVIEW_ASPECT_H
#define RUBRAVIEW_ASPECT_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Looking at a picture or a film in another shape (owner, 2026-10-11:
 * "비율조정 기능 ... 4:3이나 16:9 16:10 등 혹은 영화관 비율 ... A4 B4 US Letter
 * 등"). The file is not changed: what is on screen is stretched to the
 * chosen width : height, its height kept. A ratio can be turned on its
 * side (16:9 becomes 9:16; a sheet of paper, landscape).
 */
typedef enum rubraview_aspect {
    RUBRAVIEW_ASPECT_ORIGINAL = 0,   /* the file's own shape */
    RUBRAVIEW_ASPECT_4_3,
    RUBRAVIEW_ASPECT_5_4,
    RUBRAVIEW_ASPECT_3_2,
    RUBRAVIEW_ASPECT_16_10,
    RUBRAVIEW_ASPECT_16_9,
    RUBRAVIEW_ASPECT_CINEMA_FLAT,    /* 1.85 : 1 */
    RUBRAVIEW_ASPECT_21_9,           /* 64 : 27, what "21:9" screens are */
    RUBRAVIEW_ASPECT_CINEMA_SCOPE,   /* 2.39 : 1 */
    RUBRAVIEW_ASPECT_1_1,
    RUBRAVIEW_ASPECT_PAPER,          /* 1 : sqrt 2 — every A and B size (A4, B4, ...) */
    RUBRAVIEW_ASPECT_LETTER,         /* 8.5 : 11, US Letter */
    RUBRAVIEW_ASPECT_CUSTOM,         /* the two numbers in the settings */
    RUBRAVIEW_ASPECT_COUNT,
} rubraview_aspect_t;

/* "aspect_16_9"; and the choice an action names (COUNT when it names none). */
const char *rubraview_aspect_action(rubraview_aspect_t aspect);
rubraview_aspect_t rubraview_aspect_from_action(u8str_t action);
/* "16:9", "1.85:1", "A4 / B4", "original". */
const char *rubraview_aspect_label(rubraview_aspect_t aspect);
/* The next choice round, for one key or tile. */
rubraview_aspect_t rubraview_aspect_next(rubraview_aspect_t aspect);

/* Width over height as seen; 0 for the file's own (and for a custom pair
   that is not two positive numbers). `on_its_side` turns it over. */
double rubraview_aspect_ratio(rubraview_aspect_t aspect, double custom_width, double custom_height, bool on_its_side);

/*
 * The size a picture of `width` x `height` (as stored) is laid out at to
 * be seen at `ratio`. `turned` says the picture is shown turned a quarter
 * (its stored width is the seen height). The seen height is kept. A ratio
 * of 0 leaves the size alone.
 */
void rubraview_aspect_apply(double ratio, bool turned, int32_t width, int32_t height, int32_t *out_width, int32_t *out_height);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_ASPECT_H */
