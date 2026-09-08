#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <wincodec.h>
#include <d2d1.h>
#include <string.h>
#include "rubraview/pal/pal_image.h"
#include "rubraview/pal/pal_render_d2d_internal.h"

/*
 * The imaging factory is created once and reused. The viewer is
 * single-threaded through M3; the pre-cache worker introduced in M4
 * (RV-044) must either create its own factory or serialise access —
 * WIC objects are not free-threaded by default.
 */
static IWICImagingFactory *g_wic_factory = NULL;

static IWICImagingFactory *wic_factory(void) {
    if (g_wic_factory) return g_wic_factory;

    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory, (void**)&g_wic_factory);
    if (FAILED(hr)) {
        g_wic_factory = NULL;
    }
    return g_wic_factory;
}

/* Maps the EXIF 0x0112 orientation value (RV-029 reads the same tag from
   raw bytes) to the WIC transform that makes the image upright. WIC
   rotations are clockwise. */
static WICBitmapTransformOptions exif_to_transform(int32_t orientation) {
    switch (orientation) {
        case 2:  return WICBitmapTransformFlipHorizontal;
        case 3:  return WICBitmapTransformRotate180;
        case 4:  return WICBitmapTransformFlipVertical;
        case 5:  return WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal;
        case 6:  return WICBitmapTransformRotate90;
        case 7:  return WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal;
        case 8:  return WICBitmapTransformRotate270;
        case 1:
        default: return WICBitmapTransformRotate0;
    }
}

/* Reads the orientation tag through WIC's metadata query reader. The
   query path differs by container: JPEG nests the TIFF IFD under APP1,
   TIFF exposes it directly. Returns 1 (upright) when absent. */
static int32_t read_orientation(IWICBitmapFrameDecode *frame) {
    IWICMetadataQueryReader *reader = NULL;
    if (FAILED(IWICBitmapFrameDecode_GetMetadataQueryReader(frame, &reader)) || !reader) {
        return 1;
    }

    static const WCHAR *const QUERIES[] = {
        L"/app1/ifd/{ushort=274}", /* JPEG */
        L"/ifd/{ushort=274}",      /* TIFF */
    };

    int32_t orientation = 1;
    for (size_t i = 0; i < sizeof(QUERIES) / sizeof(QUERIES[0]); ++i) {
        PROPVARIANT value;
        PropVariantInit(&value);
        HRESULT hr = IWICMetadataQueryReader_GetMetadataByName(reader, QUERIES[i], &value);
        if (SUCCEEDED(hr) && value.vt == VT_UI2) {
            int32_t v = (int32_t)value.uiVal;
            if (v >= 1 && v <= 8) orientation = v;
            PropVariantClear(&value);
            break;
        }
        PropVariantClear(&value);
    }

    IWICMetadataQueryReader_Release(reader);
    return orientation;
}

/* Shared tail: frame -> (optional orientation transform) -> 32bppPBGRA
   -> ID2D1Bitmap (§4.1.1's WIC-to-Direct2D interop). */
static rubraview_image_load_result_t finish_decode(rubraview_renderer_t *renderer,
                                                   IWICBitmapDecoder *decoder,
                                                   bool apply_exif_orientation) {
    rubraview_image_load_result_t result = { .texture = NULL, .width = 0, .height = 0, .exif_orientation = 1, .ok = false };

    IWICImagingFactory *factory = wic_factory();
    ID2D1RenderTarget *rt = rubraview_d2d_render_target(renderer);
    if (!factory || !rt) return result;

    IWICBitmapFrameDecode *frame = NULL;
    if (FAILED(IWICBitmapDecoder_GetFrame(decoder, 0, &frame)) || !frame) return result;

    result.exif_orientation = read_orientation(frame);

    /* The source handed to the converter is either the frame itself or a
       flip/rotator wrapping it. */
    IWICBitmapSource *source = (IWICBitmapSource*)frame;
    IWICBitmapFlipRotator *rotator = NULL;

    if (apply_exif_orientation && result.exif_orientation != 1) {
        if (SUCCEEDED(IWICImagingFactory_CreateBitmapFlipRotator(factory, &rotator)) && rotator) {
            if (SUCCEEDED(IWICBitmapFlipRotator_Initialize(rotator, (IWICBitmapSource*)frame,
                                                           exif_to_transform(result.exif_orientation)))) {
                source = (IWICBitmapSource*)rotator;
            } else {
                IWICBitmapFlipRotator_Release(rotator);
                rotator = NULL;
            }
        }
    }

    IWICFormatConverter *converter = NULL;
    if (FAILED(IWICImagingFactory_CreateFormatConverter(factory, &converter)) || !converter) {
        if (rotator) IWICBitmapFlipRotator_Release(rotator);
        IWICBitmapFrameDecode_Release(frame);
        return result;
    }

    HRESULT hr = IWICFormatConverter_Initialize(converter, source,
                                                &GUID_WICPixelFormat32bppPBGRA,
                                                WICBitmapDitherTypeNone, NULL, 0.0,
                                                WICBitmapPaletteTypeMedianCut);
    if (SUCCEEDED(hr)) {
        ID2D1Bitmap *bitmap = NULL;
        hr = ID2D1RenderTarget_CreateBitmapFromWicBitmap(rt, (IWICBitmapSource*)converter, NULL, &bitmap);
        if (SUCCEEDED(hr) && bitmap) {
            rubraview_texture_t *texture = rubraview_d2d_texture_wrap(renderer, bitmap);
            if (texture) {
                rubraview_pal_texture_size(texture, &result.width, &result.height);
                result.texture = texture;
                result.ok = true;
            } else {
                ID2D1Bitmap_Release(bitmap);
            }
        }
    }

    IWICFormatConverter_Release(converter);
    if (rotator) IWICBitmapFlipRotator_Release(rotator);
    IWICBitmapFrameDecode_Release(frame);
    return result;
}

rubraview_image_load_result_t rubraview_pal_image_load_texture(rubraview_renderer_t *renderer,
                                                               u8str_t path,
                                                               bool apply_exif_orientation) {
    rubraview_image_load_result_t result = { .texture = NULL, .width = 0, .height = 0, .exif_orientation = 1, .ok = false };

    IWICImagingFactory *factory = wic_factory();
    if (!factory || !renderer || path.len == 0 || path.len >= MAX_PATH * 4) return result;

    /* The caller's slice need not be NUL-terminated: convert from a
       bounded stack copy at the OS boundary (§7.2.3). */
    char narrow[MAX_PATH * 4];
    memcpy(narrow, path.ptr, path.len);
    narrow[path.len] = '\0';

    WCHAR wide[MAX_PATH * 2];
    if (MultiByteToWideChar(CP_UTF8, 0, narrow, -1, wide, (int)(sizeof(wide) / sizeof(wide[0]))) <= 0) {
        return result;
    }

    IWICBitmapDecoder *decoder = NULL;
    HRESULT hr = IWICImagingFactory_CreateDecoderFromFilename(factory, wide, NULL, GENERIC_READ,
                                                              WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) return result;

    result = finish_decode(renderer, decoder, apply_exif_orientation);
    IWICBitmapDecoder_Release(decoder);
    return result;
}

rubraview_image_load_result_t rubraview_pal_image_load_texture_from_memory(rubraview_renderer_t *renderer,
                                                                           const uint8_t *data,
                                                                           size_t size,
                                                                           bool apply_exif_orientation) {
    rubraview_image_load_result_t result = { .texture = NULL, .width = 0, .height = 0, .exif_orientation = 1, .ok = false };

    IWICImagingFactory *factory = wic_factory();
    if (!factory || !renderer || !data || size == 0 || size > UINT32_MAX) return result;

    /* InitializeFromMemory does not copy: `data` must stay valid for the
       duration of this call, which it does since decoding completes
       before returning. This is the path CBZ pages take in M4. */
    IWICStream *stream = NULL;
    if (FAILED(IWICImagingFactory_CreateStream(factory, &stream)) || !stream) return result;

    HRESULT hr = IWICStream_InitializeFromMemory(stream, (BYTE*)(uintptr_t)data, (DWORD)size);
    if (FAILED(hr)) {
        IWICStream_Release(stream);
        return result;
    }

    IWICBitmapDecoder *decoder = NULL;
    hr = IWICImagingFactory_CreateDecoderFromStream(factory, (IStream*)stream, NULL,
                                                   WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) {
        IWICStream_Release(stream);
        return result;
    }

    result = finish_decode(renderer, decoder, apply_exif_orientation);
    IWICBitmapDecoder_Release(decoder);
    IWICStream_Release(stream);
    return result;
}

#endif /* _WIN32 */
