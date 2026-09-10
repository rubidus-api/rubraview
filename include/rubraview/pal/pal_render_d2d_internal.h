#ifndef RUBRAVIEW_PAL_RENDER_D2D_INTERNAL_H
#define RUBRAVIEW_PAL_RENDER_D2D_INTERNAL_H

/*
 * Windows-only bridge between the Direct2D renderer and the WIC image
 * backend. Both live behind the neutral PAL headers; this one is not a
 * PAL contract and is never included by portable code — it is the seam
 * where the two Win32 backends must share Direct2D types
 * (§4.1.1's WIC-to-D2D interop needs the render target that owns the
 * bitmap it creates).
 */

#ifdef _WIN32
#include <d2d1.h>
#include <d2d1_1.h>
#include "rubraview/pal/pal_render.h"

/** The render target backing this renderer, for CreateBitmapFromWicBitmap. */
ID2D1RenderTarget *rubraview_d2d_render_target(rubraview_renderer_t *renderer);

/* The same object as a Direct2D 1.1 device context (RV-064), for the
   callers that need what 1.0 could not do: cubic interpolation and the
   effect graph. */
ID2D1DeviceContext *rubraview_d2d_device_context(rubraview_renderer_t *renderer);

/**
 * Wrap a Direct2D bitmap as an opaque rubraview texture. The texture
 * takes ownership: rubraview_pal_texture_destroy releases the bitmap.
 */
/*
 * The caller supplies the size rather than the bitmap being asked for
 * it.
 *
 * `ID2D1Bitmap::GetPixelSize` returns a struct *by value*, and how a
 * COM method returns an aggregate is exactly the place two compilers
 * can disagree — MinGW's own header carries a second declaration of
 * this method for that reason. On the owner's Windows 11 machine a
 * 4032x3024 photograph came back as 1435680840x390, which is not a
 * size; it is whatever happened to be in the register.
 *
 * WIC already knows the size and reports it through out-parameters,
 * where there is nothing to disagree about. So it is passed in, and the
 * call with the doubtful shape is not made at all.
 */
rubraview_texture_t *rubraview_d2d_texture_wrap(rubraview_renderer_t *renderer, ID2D1Bitmap *bitmap,
                                                int32_t width, int32_t height);

#endif /* _WIN32 */

#endif /* RUBRAVIEW_PAL_RENDER_D2D_INTERNAL_H */
