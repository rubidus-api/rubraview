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
#include "rubraview/pal/pal_render.h"

/** The render target backing this renderer, for CreateBitmapFromWicBitmap. */
ID2D1RenderTarget *rubraview_d2d_render_target(rubraview_renderer_t *renderer);

/**
 * Wrap a Direct2D bitmap as an opaque rubraview texture. The texture
 * takes ownership: rubraview_pal_texture_destroy releases the bitmap.
 */
rubraview_texture_t *rubraview_d2d_texture_wrap(rubraview_renderer_t *renderer, ID2D1Bitmap *bitmap);

#endif /* _WIN32 */

#endif /* RUBRAVIEW_PAL_RENDER_D2D_INTERNAL_H */
