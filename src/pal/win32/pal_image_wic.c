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

/*
 * §4.3: a photo from a modern camera or phone often carries a Display P3
 * or Adobe RGB profile. Ignoring it is what makes such pictures look
 * dull on an sRGB monitor, so the frame is converted through WIC's own
 * colour transform when it declares a profile that is not already sRGB.
 *
 * A file with no profile, or one WIC cannot build a transform for, is
 * passed through untouched — showing the picture slightly off is better
 * than not showing it.
 */
static IWICBitmapSource *apply_color_management(IWICImagingFactory *factory,
                                                IWICBitmapFrameDecode *frame,
                                                IWICBitmapSource *source,
                                                IWICColorTransform **out_transform) {
    *out_transform = NULL;

    UINT context_count = 0;
    if (FAILED(IWICBitmapFrameDecode_GetColorContexts(frame, 0, NULL, &context_count)) || context_count == 0) {
        return source; /* no embedded profile: already sRGB by convention */
    }

    IWICColorContext *embedded = NULL;
    if (FAILED(IWICImagingFactory_CreateColorContext(factory, &embedded)) || !embedded) return source;

    IWICColorContext *contexts[1] = { embedded };
    UINT actual = 0;
    if (FAILED(IWICBitmapFrameDecode_GetColorContexts(frame, 1, contexts, &actual)) || actual == 0) {
        IWICColorContext_Release(embedded);
        return source;
    }

    IWICColorContext *destination = NULL;
    if (FAILED(IWICImagingFactory_CreateColorContext(factory, &destination)) || !destination) {
        IWICColorContext_Release(embedded);
        return source;
    }
    /* sRGB is the destination: the canvas and the swap chain are sRGB
       until the wide-gamut display path arrives. */
    if (FAILED(IWICColorContext_InitializeFromExifColorSpace(destination, 1))) {
        IWICColorContext_Release(destination);
        IWICColorContext_Release(embedded);
        return source;
    }

    IWICColorTransform *transform = NULL;
    if (FAILED(IWICImagingFactory_CreateColorTransformer(factory, &transform)) || !transform) {
        IWICColorContext_Release(destination);
        IWICColorContext_Release(embedded);
        return source;
    }

    HRESULT hr = IWICColorTransform_Initialize(transform, source, embedded, destination,
                                               &GUID_WICPixelFormat32bppPBGRA);
    IWICColorContext_Release(destination);
    IWICColorContext_Release(embedded);

    if (FAILED(hr)) {
        IWICColorTransform_Release(transform);
        return source;
    }

    *out_transform = transform;
    return (IWICBitmapSource*)transform;
}

/* Shared tail: frame -> (optional orientation transform) -> colour
   management -> 32bppPBGRA -> ID2D1Bitmap (§4.1.1's WIC-to-Direct2D
   interop, §4.3's colour transform). */
static rubraview_image_load_result_t finish_decode_frame(rubraview_renderer_t *renderer,
                                                          IWICBitmapDecoder *decoder,
                                                          UINT frame_index,
                                                          bool apply_exif_orientation) {
    rubraview_image_load_result_t result = { .texture = NULL, .width = 0, .height = 0, .exif_orientation = 1, .ok = false };

    IWICImagingFactory *factory = wic_factory();
    ID2D1RenderTarget *rt = rubraview_d2d_render_target(renderer);
    if (!factory || !rt) return result;

    IWICBitmapFrameDecode *frame = NULL;
    if (FAILED(IWICBitmapDecoder_GetFrame(decoder, frame_index, &frame)) || !frame) return result;

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

    IWICColorTransform *color_transform = NULL;
    source = apply_color_management(factory, frame, source, &color_transform);

    IWICFormatConverter *converter = NULL;
    if (FAILED(IWICImagingFactory_CreateFormatConverter(factory, &converter)) || !converter) {
        if (color_transform) IWICColorTransform_Release(color_transform);
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
    if (color_transform) IWICColorTransform_Release(color_transform);
    if (rotator) IWICBitmapFlipRotator_Release(rotator);
    IWICBitmapFrameDecode_Release(frame);
    return result;
}

static rubraview_image_load_result_t finish_decode(rubraview_renderer_t *renderer,
                                                   IWICBitmapDecoder *decoder,
                                                   bool apply_exif_orientation) {
    return finish_decode_frame(renderer, decoder, 0, apply_exif_orientation);
}

/* Opens a decoder for a path, converting at the OS boundary only. */
static IWICBitmapDecoder *decoder_for_path(u8str_t path) {
    IWICImagingFactory *factory = wic_factory();
    if (!factory || path.len == 0 || path.len >= MAX_PATH * 4) return NULL;

    char narrow[MAX_PATH * 4];
    memcpy(narrow, path.ptr, path.len);
    narrow[path.len] = '\0';

    WCHAR wide[MAX_PATH * 2];
    if (MultiByteToWideChar(CP_UTF8, 0, narrow, -1, wide, (int)(sizeof(wide) / sizeof(wide[0]))) <= 0) {
        return NULL;
    }

    IWICBitmapDecoder *decoder = NULL;
    if (FAILED(IWICImagingFactory_CreateDecoderFromFilename(factory, wide, NULL, GENERIC_READ,
                                                            WICDecodeMetadataCacheOnDemand, &decoder))) {
        return NULL;
    }
    return decoder;
}

/* Opens a decoder over bytes already in memory — the path an archive
   page takes, since it has no filename to give WIC. The stream must
   outlive the decoder, so the caller is handed both. */
static IWICBitmapDecoder *decoder_for_memory(const uint8_t *data, size_t size, IWICStream **out_stream) {
    *out_stream = NULL;
    IWICImagingFactory *factory = wic_factory();
    if (!factory || !data || size == 0 || size > UINT32_MAX) return NULL;

    IWICStream *stream = NULL;
    if (FAILED(IWICImagingFactory_CreateStream(factory, &stream)) || !stream) return NULL;
    if (FAILED(IWICStream_InitializeFromMemory(stream, (BYTE*)(uintptr_t)data, (DWORD)size))) {
        IWICStream_Release(stream);
        return NULL;
    }

    IWICBitmapDecoder *decoder = NULL;
    if (FAILED(IWICImagingFactory_CreateDecoderFromStream(factory, (IStream*)stream, NULL,
                                                          WICDecodeMetadataCacheOnDemand, &decoder)) || !decoder) {
        IWICStream_Release(stream);
        return NULL;
    }
    *out_stream = stream;
    return decoder;
}

/* One entry point for both: a path when there is one, bytes otherwise. */
static IWICBitmapDecoder *decoder_for_source(u8str_t path, const uint8_t *data, size_t size,
                                             IWICStream **out_stream) {
    *out_stream = NULL;
    if (path.len > 0) return decoder_for_path(path);
    return decoder_for_memory(data, size, out_stream);
}

/* §3.20.1: GIF stores a frame's delay in hundredths of a second under
   the graphic control extension. Other containers state none, which the
   animation clock replaces with its own minimum. */
static double frame_delay_seconds(IWICBitmapDecoder *decoder, UINT frame_index) {
    IWICBitmapFrameDecode *frame = NULL;
    if (FAILED(IWICBitmapDecoder_GetFrame(decoder, frame_index, &frame)) || !frame) return 0.0;

    double seconds = 0.0;
    IWICMetadataQueryReader *reader = NULL;
    if (SUCCEEDED(IWICBitmapFrameDecode_GetMetadataQueryReader(frame, &reader)) && reader) {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(IWICMetadataQueryReader_GetMetadataByName(reader, L"/grctlext/Delay", &value)) &&
            value.vt == VT_UI2) {
            seconds = (double)value.uiVal / 100.0;
        }
        PropVariantClear(&value);
        IWICMetadataQueryReader_Release(reader);
    }

    IWICBitmapFrameDecode_Release(frame);
    return seconds;
}

size_t rubraview_pal_image_frame_info(u8str_t path,
                                      const uint8_t *data, size_t size,
                                      rubraview_frame_t *out_frames, size_t cap) {
    IWICStream *stream = NULL;
    IWICBitmapDecoder *decoder = decoder_for_source(path, data, size, &stream);
    if (!decoder) {
        if (stream) IWICStream_Release(stream);
        return 0;
    }

    UINT count = 0;
    HRESULT hr = IWICBitmapDecoder_GetFrameCount(decoder, &count);
    if (FAILED(hr)) count = 0;

    for (UINT i = 0; out_frames && i < count && (size_t)i < cap; ++i) {
        out_frames[i].delay_seconds = frame_delay_seconds(decoder, i);
        out_frames[i].width = 0;
        out_frames[i].height = 0;

        /* An ICO's frames differ in size and nothing else, so the size
           is not a detail here — it is how the largest one is found. */
        IWICBitmapFrameDecode *frame = NULL;
        if (SUCCEEDED(IWICBitmapDecoder_GetFrame(decoder, i, &frame)) && frame) {
            UINT w = 0, h = 0;
            if (SUCCEEDED(IWICBitmapFrameDecode_GetSize(frame, &w, &h))) {
                out_frames[i].width = (int32_t)w;
                out_frames[i].height = (int32_t)h;
            }
            IWICBitmapFrameDecode_Release(frame);
        }
    }

    IWICBitmapDecoder_Release(decoder);
    if (stream) IWICStream_Release(stream);
    return (size_t)count;
}

rubraview_image_load_result_t rubraview_pal_image_load_frame(rubraview_renderer_t *renderer,
                                                             u8str_t path,
                                                             const uint8_t *data, size_t size,
                                                             size_t frame_index,
                                                             bool apply_exif_orientation,
                                                             double *out_delay_seconds) {
    rubraview_image_load_result_t result = { .texture = NULL, .width = 0, .height = 0, .exif_orientation = 1, .ok = false };
    if (out_delay_seconds) *out_delay_seconds = 0.0;

    IWICStream *stream = NULL;
    IWICBitmapDecoder *decoder = decoder_for_source(path, data, size, &stream);
    if (!decoder || !renderer) {
        if (decoder) IWICBitmapDecoder_Release(decoder);
        if (stream) IWICStream_Release(stream);
        return result;
    }

    UINT count = 0;
    if (FAILED(IWICBitmapDecoder_GetFrameCount(decoder, &count)) || frame_index >= (size_t)count) {
        IWICBitmapDecoder_Release(decoder);
        if (stream) IWICStream_Release(stream);
        return result;
    }

    if (out_delay_seconds) *out_delay_seconds = frame_delay_seconds(decoder, (UINT)frame_index);
    result = finish_decode_frame(renderer, decoder, (UINT)frame_index, apply_exif_orientation);

    IWICBitmapDecoder_Release(decoder);
    if (stream) IWICStream_Release(stream);
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
