#ifndef RUBRAVIEW_PAL_IMAGE_WIC_INTERNAL_H
#define RUBRAVIEW_PAL_IMAGE_WIC_INTERNAL_H
#ifdef _WIN32

#include <wincodec.h>
#include "rubraview/core.h"

/*
 * One rectangle of a picture in memory, decoded as the page itself is —
 * its EXIF turn (when asked), its colour profile — then clipped to
 * (x, y, w, h) in upright picture pixels and scaled to out_w x out_h, as
 * premultiplied BGRA into `dst`, top row first. For the tile thread (D-40),
 * which passes its own factory: the viewer's is not shared across threads.
 */
bool rubraview_wic_region_pbgra(IWICImagingFactory *factory, const uint8_t *data, size_t size,
                                bool apply_exif_orientation,
                                int32_t x, int32_t y, int32_t w, int32_t h,
                                int32_t out_w, int32_t out_h, uint8_t *dst, uint32_t stride);

#endif
#endif
