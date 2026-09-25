#ifndef RUBRAVIEW_PAL_GPU_RESAMPLE_H
#define RUBRAVIEW_PAL_GPU_RESAMPLE_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* D-38: `[display] gpu_resize`. off: the CPU always. on: the graphics card
   for bicubic and Lanczos-3 resizes of a megapixel or more, when there is a
   card with compute shaders. always: every such resize, and Windows'
   software rasteriser (WARP) when there is no card — for testing. */
typedef enum rubraview_gpu_resize_mode {
    RUBRAVIEW_GPU_RESIZE_OFF = 0,
    RUBRAVIEW_GPU_RESIZE_ON,
    RUBRAVIEW_GPU_RESIZE_ALWAYS,
} rubraview_gpu_resize_mode_t;

/* Installs (or removes) the resize accelerator; true when it is in place.
   Safe to call again when the setting changes. */
bool rubraview_pal_gpu_resample_start(rubraview_gpu_resize_mode_t mode);
void rubraview_pal_gpu_resample_stop(void);
/* What it runs on, or why it does not: "hardware", "WARP (software)", ... */
const char *rubraview_pal_gpu_resample_describe(void);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_GPU_RESAMPLE_H */
