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

/*
 * A decoder over bytes in memory, by the caller's own factory: Windows'
 * codec, or the decoders built in (AVIF first of all, JPEG XL when Windows
 * has none) — the one road every reader of a picture's bytes takes. The
 * stream, when one comes back, must outlive the decoder.
 */
IWICBitmapDecoder *rubraview_wic_decoder_for_bytes(IWICImagingFactory *factory, const uint8_t *data, size_t size,
                                                   IWICStream **out_stream);

/*
 * What a built-in decoder reads, as an uncompressed TIFF in memory, for a
 * reader that will come back for many rectangles. NULL when Windows reads
 * the bytes itself or nothing does. Let go with rubraview_wic_own_tiff_free.
 */
uint8_t *rubraview_wic_own_tiff(IWICImagingFactory *factory, const uint8_t *data, size_t size,
                                size_t *out_size, void **out_owner);
void rubraview_wic_own_tiff_free(void *owner);

#endif
#endif
