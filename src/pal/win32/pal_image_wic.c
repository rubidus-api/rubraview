#ifdef _WIN32
#define COBJMACROS
#include <stdarg.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <windows.h>
#include <wincodec.h>
#include <d2d1.h>
#include <string.h>
#include "rubraview/pal/pal_image.h"
#include "rubraview/pal/pal_render_d2d_internal.h"
#include "rubraview/viewport.h"

/* A page's texture holds at most this many pixels (512 MB at four bytes
   each); a bigger picture is shown reduced, see finish_decode_frame. */
#define PAGE_MAX_PIXELS ((uint64_t)128 << 20)

/*
 * The imaging factory is created once and reused. The viewer is
 * single-threaded through M3; the pre-cache worker introduced in M4
 * (RV-044) must either create its own factory or serialise access —
 * WIC objects are not free-threaded by default.
 */
static IWICImagingFactory *g_wic_factory = NULL;

static IWICImagingFactory *wic_factory(void);

bool rubraview_pal_image_startup(void) {
    /* See the header: this is about timing, not capability. Doing it
       here — after CoInitializeEx, before any window — is what keeps the
       first image from deadlocking an apartment nobody is pumping. */
    return wic_factory() != NULL;
}

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

/* The frame turned upright. The flip/rotator is never put straight on the
   frame: a quarter turn makes every output row a column of the source, and
   a JPEG frame cannot hand out a column without decoding the whole file
   again — row after row. On the VM a 1600x1200 photo with EXIF 6 took
   6.5 s that way and a 4032x3024 one 92 s; decoded into memory first, 22
   and 228 ms. The frame is decoded once, into `*out_cached`, and turned
   there. Returns the frame itself when upright or when anything fails. */
static IWICBitmapSource *upright_source(IWICImagingFactory *factory, IWICBitmapFrameDecode *frame,
                                        int32_t orientation, IWICBitmap **out_cached,
                                        IWICBitmapFlipRotator **out_rotator) {
    *out_cached = NULL;
    *out_rotator = NULL;
    IWICBitmapSource *source = (IWICBitmapSource*)frame;
    if (orientation == 1) return source;

    IWICBitmap *cached = NULL;
    if (FAILED(IWICImagingFactory_CreateBitmapFromSource(factory, source, WICBitmapCacheOnLoad, &cached)) || !cached) {
        return source;
    }
    IWICBitmapFlipRotator *rotator = NULL;
    if (FAILED(IWICImagingFactory_CreateBitmapFlipRotator(factory, &rotator)) || !rotator ||
        FAILED(IWICBitmapFlipRotator_Initialize(rotator, (IWICBitmapSource*)cached, exif_to_transform(orientation)))) {
        if (rotator) IWICBitmapFlipRotator_Release(rotator);
        IWICBitmap_Release(cached);
        return source;
    }
    *out_cached = cached;
    *out_rotator = rotator;
    return (IWICBitmapSource*)rotator;
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
/*
 * §4.3's colour transform.
 *
 * Anything that does not work exactly is skipped rather than forced: a
 * picture with slightly wrong colours beats a picture of noise. The
 * checks after Initialize exist because succeeding and being right are
 * not the same thing — a transform that disagrees with its own source
 * about the size or the pixel format is not used.
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

    /* Succeeding is not the same as being right. If the transform does
       not describe the same picture it was given, it is not used —
       that mismatch is what produced noise instead of a photograph. */
    UINT in_w = 0, in_h = 0, out_w = 0, out_h = 0;
    if (FAILED(IWICBitmapSource_GetSize(source, &in_w, &in_h)) ||
        FAILED(IWICColorTransform_GetSize(transform, &out_w, &out_h)) ||
        in_w != out_w || in_h != out_h || out_w == 0 || out_h == 0) {
        IWICColorTransform_Release(transform);
        return source;
    }

    WICPixelFormatGUID out_format;
    if (FAILED(IWICColorTransform_GetPixelFormat(transform, &out_format)) ||
        !IsEqualGUID(&out_format, &GUID_WICPixelFormat32bppPBGRA)) {
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

    /* The source handed to the converter is either the frame itself or
       the frame decoded and turned upright. */
    IWICBitmap *cached = NULL;
    IWICBitmapFlipRotator *rotator = NULL;
    IWICBitmapSource *source = upright_source(factory, frame, apply_exif_orientation ? result.exif_orientation : 1,
                                              &cached, &rotator);

    IWICColorTransform *color_transform = NULL;
    source = apply_color_management(factory, frame, source, &color_transform);

    IWICFormatConverter *converter = NULL;
    if (FAILED(IWICImagingFactory_CreateFormatConverter(factory, &converter)) || !converter) {
        if (color_transform) IWICColorTransform_Release(color_transform);
        if (rotator) IWICBitmapFlipRotator_Release(rotator);
        if (cached) IWICBitmap_Release(cached);
        IWICBitmapFrameDecode_Release(frame);
        return result;
    }

    HRESULT hr = IWICFormatConverter_Initialize(converter, source,
                                                &GUID_WICPixelFormat32bppPBGRA,
                                                WICBitmapDitherTypeNone, NULL, 0.0,
                                                WICBitmapPaletteTypeMedianCut);
    if (SUCCEEDED(hr)) {
        /* Ask WIC for the size, through out-parameters. This is the
           number the texture carries from here on; the Direct2D call
           that returns a struct by value is not used, because on the
           owner's machine it produced 1435680840x390 for a 4032x3024
           photograph (see pal_render_d2d_internal.h). */
        UINT decoded_w = 0, decoded_h = 0;
        HRESULT size_hr = IWICFormatConverter_GetSize(converter, &decoded_w, &decoded_h);
        if (SUCCEEDED(size_hr) && decoded_w > 0 && decoded_h > 0 &&
            decoded_w <= INT32_MAX && decoded_h <= INT32_MAX) {
            result.full_width = (int32_t)decoded_w;
            result.full_height = (int32_t)decoded_h;

            /* The very large picture's fall-back. A bitmap over the
               device's largest side, or past the pixel budget, cannot be
               made at all — the page used to fail. It is decoded through
               WIC's scaler to what fits instead, and when even that is
               refused (the card's memory), at half again, a few times. */
            int32_t tw = 0, th = 0;
            UINT32 max_side = ID2D1RenderTarget_GetMaximumBitmapSize(rt);
            rubraview_fit_within_limits(result.full_width, result.full_height,
                                        max_side > 0 && max_side <= INT32_MAX ? (int32_t)max_side : 16384,
                                        PAGE_MAX_PIXELS, &tw, &th);
            for (int attempt = 0; attempt < 5 && tw > 0 && th > 0; ++attempt) {
                bool reduce = tw != result.full_width || th != result.full_height;
                IWICBitmapScaler *scaler = NULL;
                IWICFormatConverter *scaled = NULL;
                IWICBitmapSource *feed = (IWICBitmapSource*)converter;
                if (reduce) {
                    /* The scaler goes on the source and the conversion
                       after it: a scaler's output format is its own
                       choice, and PBGRA is what the bitmap must be. */
                    if (FAILED(IWICImagingFactory_CreateBitmapScaler(factory, &scaler)) || !scaler) break;
                    if (FAILED(IWICBitmapScaler_Initialize(scaler, source, (UINT)tw, (UINT)th,
                                                           WICBitmapInterpolationModeFant)) ||
                        FAILED(IWICImagingFactory_CreateFormatConverter(factory, &scaled)) || !scaled ||
                        FAILED(IWICFormatConverter_Initialize(scaled, (IWICBitmapSource*)scaler,
                                                              &GUID_WICPixelFormat32bppPBGRA,
                                                              WICBitmapDitherTypeNone, NULL, 0.0,
                                                              WICBitmapPaletteTypeMedianCut))) {
                        if (scaled) IWICFormatConverter_Release(scaled);
                        IWICBitmapScaler_Release(scaler);
                        break;
                    }
                    feed = (IWICBitmapSource*)scaled;
                }
                ID2D1Bitmap *bitmap = NULL;
                hr = ID2D1RenderTarget_CreateBitmapFromWicBitmap(rt, feed, NULL, &bitmap);
                if (scaled) IWICFormatConverter_Release(scaled);
                if (scaler) IWICBitmapScaler_Release(scaler);
                if (SUCCEEDED(hr) && bitmap) {
                    rubraview_texture_t *texture = rubraview_d2d_texture_wrap(renderer, bitmap, tw, th);
                    if (texture) {
                        rubraview_pal_texture_size(texture, &result.width, &result.height);
                        result.texture = texture;
                        result.reduced = reduce;
                        result.ok = true;
                    } else {
                        ID2D1Bitmap_Release(bitmap);
                    }
                    break;
                }
                /* A device that is gone will not take a smaller one either. */
                if (hr == D2DERR_RECREATE_TARGET || tw <= 1024 || th <= 1) break;
                tw /= 2;
                th = th / 2 > 0 ? th / 2 : 1;
            }
        }
        /* A bitmap whose size is unknown is not made at all: it would be
           laid out against a number nobody measured. */
    }

    IWICFormatConverter_Release(converter);
    if (color_transform) IWICColorTransform_Release(color_transform);
    if (rotator) IWICBitmapFlipRotator_Release(rotator);
    if (cached) IWICBitmap_Release(cached);
    IWICBitmapFrameDecode_Release(frame);
    return result;
}

static rubraview_image_load_result_t finish_decode(rubraview_renderer_t *renderer,
                                                   IWICBitmapDecoder *decoder,
                                                   bool apply_exif_orientation) {
    return finish_decode_frame(renderer, decoder, 0, apply_exif_orientation);
}

/* The largest file a page may be. Reading a file whole is what keeps it
   unlocked, so the size has to be bounded somewhere. */
#define MAX_IMAGE_FILE_BYTES ((DWORD)1 << 30)

/* Opens a decoder for a path, converting at the OS boundary only.
 *
 * WIC is never given the file. A decoder made by CreateDecoderFromFilename
 * kept its handle open — without write or delete sharing — for as long
 * as anything held it, and on the Win11 VM every page the viewer had
 * decoded, neighbours included, could then not be renamed, deleted or
 * saved over. Instead the file is read whole with full sharing, the
 * handle closed at once, and WIC decodes from a stream that owns the
 * bytes. Nothing the viewer keeps can lock a user's file. */
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

    HANDLE file = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE) return NULL;

    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(file, &file_size) || file_size.QuadPart <= 0 ||
        file_size.QuadPart > (LONGLONG)MAX_IMAGE_FILE_BYTES) {
        CloseHandle(file);
        return NULL;
    }
    DWORD size = (DWORD)file_size.QuadPart;

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory) { CloseHandle(file); return NULL; }

    BYTE *bytes = (BYTE*)GlobalLock(memory);
    DWORD total = 0;
    while (bytes && total < size) {
        DWORD got = 0;
        if (!ReadFile(file, bytes + total, size - total, &got, NULL) || got == 0) break;
        total += got;
    }
    if (bytes) GlobalUnlock(memory);
    CloseHandle(file);
    if (!bytes || total != size) { GlobalFree(memory); return NULL; }

    /* fDeleteOnRelease: the stream frees the memory when the last
       reference — ours or the decoder's — goes away. */
    IStream *stream = NULL;
    if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream)) || !stream) {
        GlobalFree(memory);
        return NULL;
    }
    ULARGE_INTEGER stream_size = { .QuadPart = size };
    IStream_SetSize(stream, stream_size);

    IWICBitmapDecoder *decoder = NULL;
    HRESULT hr = IWICImagingFactory_CreateDecoderFromStream(factory, stream, NULL,
                                                            WICDecodeMetadataCacheOnDemand, &decoder);
    IStream_Release(stream); /* the decoder holds its own reference */
    if (FAILED(hr)) return NULL;
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

    if (!renderer) return result;

    /* decoder_for_path reads the file whole so WIC never holds it open. */
    IWICBitmapDecoder *decoder = decoder_for_path(path);
    if (!decoder) return result;

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

/* ---- reading pixels back, and writing them out (§3.10, §3.13) ---- */

/* The viewing path never brings pixels to the CPU (§4.1.1). The editing
   workbench has to: its commit layer runs the core engine on a real
   buffer, not on a GPU texture. */
static rubraview_pixbuf_t read_pixels_within(proven_arena_t *arena, u8str_t path,
                                             const uint8_t *data, size_t size,
                                             bool apply_exif_orientation, int32_t max_w, int32_t max_h) {
    rubraview_pixbuf_t empty = {0};
    IWICImagingFactory *factory = wic_factory();
    if (!factory || !arena) return empty;

    IWICStream *stream = NULL;
    IWICBitmapDecoder *decoder = decoder_for_source(path, data, size, &stream);
    if (!decoder) { if (stream) IWICStream_Release(stream); return empty; }

    IWICBitmapFrameDecode *frame = NULL;
    if (FAILED(IWICBitmapDecoder_GetFrame(decoder, 0, &frame)) || !frame) {
        IWICBitmapDecoder_Release(decoder);
        if (stream) IWICStream_Release(stream);
        return empty;
    }

    IWICBitmap *cached = NULL;
    IWICBitmapFlipRotator *rotator = NULL;
    int32_t orientation = read_orientation(frame);
    IWICBitmapSource *source = upright_source(factory, frame, apply_exif_orientation ? orientation : 1,
                                              &cached, &rotator);

    /* The core engine works in RGBA8, so the conversion happens here
       rather than being a special case inside every filter. */
    IWICFormatConverter *converter = NULL;
    rubraview_pixbuf_t out = empty;
    if (SUCCEEDED(IWICImagingFactory_CreateFormatConverter(factory, &converter)) && converter &&
        SUCCEEDED(IWICFormatConverter_Initialize(converter, source, &GUID_WICPixelFormat32bppRGBA,
                                                 WICBitmapDitherTypeNone, NULL, 0.0,
                                                 WICBitmapPaletteTypeMedianCut))) {
        UINT w = 0, h = 0;
        IWICBitmapSource *feed = (IWICBitmapSource*)converter;
        IWICBitmapScaler *scaler = NULL;
        IWICFormatConverter *scaled = NULL;
        if (SUCCEEDED(IWICFormatConverter_GetSize(converter, &w, &h)) && w > 0 && h > 0 &&
            w <= INT32_MAX && h <= INT32_MAX && max_w > 0 && max_h > 0 &&
            (w > (UINT)max_w || h > (UINT)max_h)) {
            /* Asked for no more than a box: WIC's scaler reads the source
               a band at a time, so the full size is never held. */
            double k = (double)max_w / (double)w;
            if ((double)max_h / (double)h < k) k = (double)max_h / (double)h;
            UINT sw = (UINT)((double)w * k), sh = (UINT)((double)h * k);
            if (sw < 1) sw = 1;
            if (sh < 1) sh = 1;
            /* Scaler on the source, RGBA after it: a scaler picks its
               own output format (BGRA here), which would swap red and
               blue if it came last. */
            if (SUCCEEDED(IWICImagingFactory_CreateBitmapScaler(factory, &scaler)) && scaler &&
                SUCCEEDED(IWICBitmapScaler_Initialize(scaler, source, sw, sh, WICBitmapInterpolationModeFant)) &&
                SUCCEEDED(IWICImagingFactory_CreateFormatConverter(factory, &scaled)) && scaled &&
                SUCCEEDED(IWICFormatConverter_Initialize(scaled, (IWICBitmapSource*)scaler,
                                                         &GUID_WICPixelFormat32bppRGBA,
                                                         WICBitmapDitherTypeNone, NULL, 0.0,
                                                         WICBitmapPaletteTypeMedianCut))) {
                feed = (IWICBitmapSource*)scaled;
                w = sw;
                h = sh;
            } else {
                w = h = 0;
            }
        }
        /* CopyPixels takes the buffer's size as a UINT. */
        if (w > 0 && h > 0 && w <= INT32_MAX && h <= INT32_MAX &&
            (uint64_t)w * 4u * (uint64_t)h <= (uint64_t)UINT_MAX) {
            rubraview_pixbuf_t pb = rubraview_pixbuf_create(arena, (int32_t)w, (int32_t)h,
                                                            RUBRAVIEW_PIXFMT_RGBA8);
            if (rubraview_pixbuf_is_valid(&pb)) {
                UINT stride = (UINT)pb.stride;
                UINT buffer_size = stride * h;
                if (SUCCEEDED(IWICBitmapSource_CopyPixels(feed, NULL, stride, buffer_size, pb.pixels))) {
                    out = pb;
                }
            }
        }
        if (scaled) IWICFormatConverter_Release(scaled);
        if (scaler) IWICBitmapScaler_Release(scaler);
    }

    if (converter) IWICFormatConverter_Release(converter);
    if (rotator) IWICBitmapFlipRotator_Release(rotator);
    if (cached) IWICBitmap_Release(cached);
    IWICBitmapFrameDecode_Release(frame);
    IWICBitmapDecoder_Release(decoder);
    if (stream) IWICStream_Release(stream);
    return out;
}

rubraview_pixbuf_t rubraview_pal_image_read_pixels(proven_arena_t *arena,
                                                   u8str_t path,
                                                   const uint8_t *data, size_t size,
                                                   bool apply_exif_orientation) {
    return read_pixels_within(arena, path, data, size, apply_exif_orientation, 0, 0);
}

rubraview_pixbuf_t rubraview_pal_image_read_pixels_within(proven_arena_t *arena,
                                                          u8str_t path,
                                                          const uint8_t *data, size_t size,
                                                          bool apply_exif_orientation,
                                                          int32_t max_width, int32_t max_height) {
    return read_pixels_within(arena, path, data, size, apply_exif_orientation, max_width, max_height);
}

static const GUID *container_for_format(rubraview_export_format_t format) {
    switch (format) {
        case RUBRAVIEW_EXPORT_JPEG: return &GUID_ContainerFormatJpeg;
        case RUBRAVIEW_EXPORT_PNG:  return &GUID_ContainerFormatPng;
        case RUBRAVIEW_EXPORT_WEBP: return &GUID_ContainerFormatWmp; /* see the note in save() */
        case RUBRAVIEW_EXPORT_GIF:  return &GUID_ContainerFormatGif;
        case RUBRAVIEW_EXPORT_BMP:  return &GUID_ContainerFormatBmp;
        case RUBRAVIEW_EXPORT_TIFF: return &GUID_ContainerFormatTiff;
        case RUBRAVIEW_EXPORT_ICO:  return &GUID_ContainerFormatIco;
        default: return NULL;
    }
}

/* Opens a file stream for writing, converting the path at the boundary. */
static IWICStream *writable_stream(IWICImagingFactory *factory, u8str_t path) {
    if (!factory || path.len == 0 || path.len >= MAX_PATH * 4) return NULL;

    char narrow[MAX_PATH * 4];
    memcpy(narrow, path.ptr, path.len);
    narrow[path.len] = '\0';

    WCHAR wide[MAX_PATH * 2];
    if (MultiByteToWideChar(CP_UTF8, 0, narrow, -1, wide, (int)(sizeof(wide) / sizeof(wide[0]))) <= 0) {
        return NULL;
    }

    IWICStream *stream = NULL;
    if (FAILED(IWICImagingFactory_CreateStream(factory, &stream)) || !stream) return NULL;
    if (FAILED(IWICStream_InitializeFromFilename(stream, wide, GENERIC_WRITE))) {
        IWICStream_Release(stream);
        return NULL;
    }
    return stream;
}

/* Sets the per-format knobs §3.10 exposes. A property WIC does not know
   is simply not set: an encoder that ignores a quality hint still writes
   a correct file, and refusing to save over it would help nobody. */
static void apply_encoder_options(IPropertyBag2 *bag,
                                  const rubraview_export_options_t *options,
                                  rubraview_export_format_t format) {
    if (!bag || !options) return;

    PROPBAG2 option = {0};
    VARIANT value;

    if (format == RUBRAVIEW_EXPORT_JPEG) {
        option.pstrName = (LPOLESTR)L"ImageQuality";
        VariantInit(&value);
        value.vt = VT_R4;
        value.fltVal = (float)options->jpeg_quality / 100.0f;
        IPropertyBag2_Write(bag, 1, &option, &value);

        if (options->jpeg_progressive) {
            option.pstrName = (LPOLESTR)L"JpegYCrCbSubsampling";
            VariantInit(&value);
            value.vt = VT_UI1;
            value.bVal = WICJpegYCrCbSubsampling420;
            IPropertyBag2_Write(bag, 1, &option, &value);
        }
    } else if (format == RUBRAVIEW_EXPORT_PNG) {
        /* WIC's PNG encoder has no compression *level*; what it exposes
           is the filter, and interlacing. Level 0 is taken as "do not
           spend time", which is what the filter choice controls. */
        option.pstrName = (LPOLESTR)L"FilterOption";
        VariantInit(&value);
        value.vt = VT_UI1;
        value.bVal = options->png_compression == 0
                       ? WICPngFilterNone : WICPngFilterAdaptive;
        IPropertyBag2_Write(bag, 1, &option, &value);
    } else if (format == RUBRAVIEW_EXPORT_TIFF) {
        option.pstrName = (LPOLESTR)L"CompressionQuality";
        VariantInit(&value);
        value.vt = VT_R4;
        value.fltVal = 1.0f;
        IPropertyBag2_Write(bag, 1, &option, &value);
    }
}

/* Writes one frame of `pixels`, scaled to `width` x `height` when those
   differ from the buffer's own size (which is how the ICO writer makes
   its mipmaps). */
static bool write_frame(IWICImagingFactory *factory, IWICBitmapEncoder *encoder,
                        const rubraview_pixbuf_t *pixels,
                        const rubraview_export_options_t *options,
                        rubraview_export_format_t format,
                        UINT width, UINT height) {
    IWICBitmapFrameEncode *frame = NULL;
    IPropertyBag2 *bag = NULL;
    if (FAILED(IWICBitmapEncoder_CreateNewFrame(encoder, &frame, &bag)) || !frame) return false;

    apply_encoder_options(bag, options, format);
    if (FAILED(IWICBitmapFrameEncode_Initialize(frame, bag))) {
        if (bag) IPropertyBag2_Release(bag);
        IWICBitmapFrameEncode_Release(frame);
        return false;
    }
    if (bag) IPropertyBag2_Release(bag);

    /* The source is a WIC bitmap over the caller's buffer; nothing is
       copied unless a scale is asked for. */
    IWICBitmap *bitmap = NULL;
    HRESULT hr = IWICImagingFactory_CreateBitmapFromMemory(
        factory, (UINT)pixels->width, (UINT)pixels->height,
        &GUID_WICPixelFormat32bppRGBA, (UINT)pixels->stride,
        (UINT)pixels->stride * (UINT)pixels->height, pixels->pixels, &bitmap);
    if (FAILED(hr) || !bitmap) {
        IWICBitmapFrameEncode_Release(frame);
        return false;
    }

    IWICBitmapSource *source = (IWICBitmapSource*)bitmap;
    IWICBitmapScaler *scaler = NULL;
    if (width != (UINT)pixels->width || height != (UINT)pixels->height) {
        if (SUCCEEDED(IWICImagingFactory_CreateBitmapScaler(factory, &scaler)) && scaler &&
            SUCCEEDED(IWICBitmapScaler_Initialize(scaler, (IWICBitmapSource*)bitmap, width, height,
                                                  WICBitmapInterpolationModeFant))) {
            source = (IWICBitmapSource*)scaler;
        }
    }

    /* WIC picks the nearest pixel format the container supports; asking
       for one it cannot write and then insisting is how encoders fail. */
    WICPixelFormatGUID pixel_format = GUID_WICPixelFormat32bppBGRA;
    if (format == RUBRAVIEW_EXPORT_JPEG || format == RUBRAVIEW_EXPORT_PNG) {
        if (options && format == RUBRAVIEW_EXPORT_PNG &&
            (options->png_depth == RUBRAVIEW_PNG_RGB24 || options->png_depth == RUBRAVIEW_PNG_PALETTE8 ||
             options->png_depth == RUBRAVIEW_PNG_GRAY8)) {
            pixel_format = options->png_depth == RUBRAVIEW_PNG_GRAY8
                             ? GUID_WICPixelFormat8bppGray
                             : (options->png_depth == RUBRAVIEW_PNG_PALETTE8
                                  ? GUID_WICPixelFormat8bppIndexed
                                  : GUID_WICPixelFormat24bppBGR);
        } else if (format == RUBRAVIEW_EXPORT_JPEG) {
            pixel_format = GUID_WICPixelFormat24bppBGR;  /* JPEG has no alpha */
        }
    }
    IWICBitmapFrameEncode_SetPixelFormat(frame, &pixel_format);
    IWICBitmapFrameEncode_SetSize(frame, width, height);

    bool ok = false;
    IWICFormatConverter *converter = NULL;
    if (SUCCEEDED(IWICImagingFactory_CreateFormatConverter(factory, &converter)) && converter &&
        SUCCEEDED(IWICFormatConverter_Initialize(converter, source, &pixel_format,
                                                 WICBitmapDitherTypeErrorDiffusion, NULL, 0.0,
                                                 WICBitmapPaletteTypeMedianCut))) {
        if (SUCCEEDED(IWICBitmapFrameEncode_WriteSource(frame, (IWICBitmapSource*)converter, NULL)) &&
            SUCCEEDED(IWICBitmapFrameEncode_Commit(frame))) {
            ok = true;
        }
    }

    if (converter) IWICFormatConverter_Release(converter);
    if (scaler) IWICBitmapScaler_Release(scaler);
    IWICBitmap_Release(bitmap);
    IWICBitmapFrameEncode_Release(frame);
    return ok;
}

static bool save_frames(u8str_t path, const rubraview_pixbuf_t *pixels,
                        const rubraview_export_options_t *options,
                        rubraview_export_format_t format,
                        const int32_t *sizes, size_t size_count) {
    IWICImagingFactory *factory = wic_factory();
    if (!factory || !rubraview_pixbuf_is_valid(pixels)) return false;

    const GUID *container = container_for_format(format);
    if (!container) return false;

    IWICStream *stream = writable_stream(factory, path);
    if (!stream) return false;

    IWICBitmapEncoder *encoder = NULL;
    if (FAILED(IWICImagingFactory_CreateEncoder(factory, container, NULL, &encoder)) || !encoder) {
        IWICStream_Release(stream);
        return false;
    }
    if (FAILED(IWICBitmapEncoder_Initialize(encoder, (IStream*)stream, WICBitmapEncoderNoCache))) {
        IWICBitmapEncoder_Release(encoder);
        IWICStream_Release(stream);
        return false;
    }

    bool ok = true;
    if (sizes && size_count > 0) {
        for (size_t i = 0; i < size_count && ok; ++i) {
            if (sizes[i] <= 0) continue;
            ok = write_frame(factory, encoder, pixels, options, format,
                             (UINT)sizes[i], (UINT)sizes[i]);
        }
    } else {
        ok = write_frame(factory, encoder, pixels, options, format,
                         (UINT)pixels->width, (UINT)pixels->height);
    }

    if (ok) ok = SUCCEEDED(IWICBitmapEncoder_Commit(encoder));

    IWICBitmapEncoder_Release(encoder);
    IWICStream_Release(stream);
    return ok;
}

bool rubraview_pal_image_save(u8str_t path,
                              const rubraview_pixbuf_t *pixels,
                              const rubraview_export_options_t *options) {
    if (!options) return false;

    rubraview_export_format_t format = options->format;
    if (format == RUBRAVIEW_EXPORT_SAME_AS_SOURCE) {
        /* No source to be the same as: the filename decides. */
        format = rubraview_export_format_for_name(path);
    }
    if (format == RUBRAVIEW_EXPORT_SAME_AS_SOURCE) return false;

    if (format == RUBRAVIEW_EXPORT_ICO && options->ico_multi_size) {
        int32_t sizes[8] = {0};
        size_t n = rubraview_export_ico_sizes(sizes, 8);
        return save_frames(path, pixels, options, format, sizes, n);
    }

    /* WebP: Windows has no built-in WebP *encoder*. WIC decodes WebP
       through a system codec on current Windows, but writing one needs a
       codec that may not be installed, so this simply fails rather than
       writing a file with the wrong contents. */
    return save_frames(path, pixels, options, format, NULL, 0);
}

bool rubraview_pal_image_save_ico(u8str_t path,
                                  const rubraview_pixbuf_t *pixels,
                                  const int32_t *sizes, size_t size_count) {
    rubraview_export_options_t options = rubraview_export_defaults();
    options.format = RUBRAVIEW_EXPORT_ICO;
    return save_frames(path, pixels, &options, RUBRAVIEW_EXPORT_ICO, sizes, size_count);
}

/* ---- telling the reader why an image did not appear ---- */

/* Appends to a fixed buffer, never past its end. */
static void diag_add(char *buffer, size_t size, size_t *used, const char *fmt, ...) {
    if (*used + 1 >= size) return;
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buffer + *used, size - *used, fmt, args);
    va_end(args);
    if (n > 0) *used += (size_t)n;
    if (*used >= size) *used = size - 1;
}

u8str_t rubraview_pal_image_diagnose(rubraview_renderer_t *renderer, u8str_t path,
                                     char *buffer, size_t buffer_size) {
    u8str_t empty = { .ptr = "", .len = 0 };
    if (!buffer || buffer_size < 128) return empty;

    size_t used = 0;
    buffer[0] = '\0';

    IWICImagingFactory *factory = wic_factory();
    diag_add(buffer, buffer_size, &used, "WIC factory: %s\r\n", factory ? "ok" : "FAILED");
    if (!factory) return (u8str_t){ .ptr = buffer, .len = used };

    ID2D1RenderTarget *rt = rubraview_d2d_render_target(renderer);
    diag_add(buffer, buffer_size, &used, "render target: %s\r\n", rt ? "ok" : "FAILED (no renderer)");
    if (!rt) return (u8str_t){ .ptr = buffer, .len = used };

    IWICBitmapDecoder *decoder = decoder_for_path(path);
    diag_add(buffer, buffer_size, &used, "opening the file: %s\r\n",
             decoder ? "ok" : "FAILED (no codec for this format, or the path is wrong)");
    if (!decoder) return (u8str_t){ .ptr = buffer, .len = used };

    UINT frame_count = 0;
    IWICBitmapDecoder_GetFrameCount(decoder, &frame_count);
    diag_add(buffer, buffer_size, &used, "frames in the file: %u\r\n", frame_count);

    IWICBitmapFrameDecode *frame = NULL;
    HRESULT hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    diag_add(buffer, buffer_size, &used, "getting frame 0: %s (0x%08lX)\r\n",
             SUCCEEDED(hr) ? "ok" : "FAILED", (unsigned long)hr);
    if (FAILED(hr) || !frame) { IWICBitmapDecoder_Release(decoder); return (u8str_t){ .ptr = buffer, .len = used }; }

    UINT w = 0, h = 0;
    IWICBitmapFrameDecode_GetSize(frame, &w, &h);
    diag_add(buffer, buffer_size, &used, "size: %ux%u\r\n", w, h);

    int32_t orientation = read_orientation(frame);
    diag_add(buffer, buffer_size, &used, "EXIF orientation: %d\r\n", orientation);

    UINT contexts = 0;
    IWICBitmapFrameDecode_GetColorContexts(frame, 0, NULL, &contexts);
    diag_add(buffer, buffer_size, &used, "embedded colour profiles: %u\r\n", contexts);

    /* Each stage separately, so a failure names itself rather than
       disappearing into a single "could not load". */
    IWICBitmapSource *source = (IWICBitmapSource*)frame;
    IWICColorTransform *transform = NULL;
    IWICBitmapSource *managed = apply_color_management(factory, frame, source, &transform);
    diag_add(buffer, buffer_size, &used, "colour management: %s\r\n",
             transform ? "applied" : (contexts > 0 ? "skipped (could not build a transform)" : "not needed"));
    source = managed;

    IWICFormatConverter *converter = NULL;
    hr = IWICImagingFactory_CreateFormatConverter(factory, &converter);
    diag_add(buffer, buffer_size, &used, "creating the converter: %s (0x%08lX)\r\n",
             SUCCEEDED(hr) ? "ok" : "FAILED", (unsigned long)hr);

    if (SUCCEEDED(hr) && converter) {
        hr = IWICFormatConverter_Initialize(converter, source, &GUID_WICPixelFormat32bppPBGRA,
                                            WICBitmapDitherTypeNone, NULL, 0.0,
                                            WICBitmapPaletteTypeMedianCut);
        diag_add(buffer, buffer_size, &used, "converting to 32bppPBGRA: %s (0x%08lX)\r\n",
                 SUCCEEDED(hr) ? "ok" : "FAILED", (unsigned long)hr);

        if (SUCCEEDED(hr)) {
            ID2D1Bitmap *bitmap = NULL;
            hr = ID2D1RenderTarget_CreateBitmapFromWicBitmap(rt, (IWICBitmapSource*)converter, NULL, &bitmap);
            diag_add(buffer, buffer_size, &used, "uploading to the GPU: %s (0x%08lX)\r\n",
                     SUCCEEDED(hr) ? "ok" : "FAILED", (unsigned long)hr);
            if (SUCCEEDED(hr) && bitmap) {
                /* Three ways of asking the same question. They should
                   agree; when they do not, the one that disagrees is
                   the bug, and printing all three says which. */
                UINT cw = 0, ch = 0;
                IWICFormatConverter_GetSize(converter, &cw, &ch);
                diag_add(buffer, buffer_size, &used, "size from WIC converter: %ux%u\r\n", cw, ch);

                D2D1_SIZE_U by_value = ID2D1Bitmap_GetPixelSize(bitmap);
                diag_add(buffer, buffer_size, &used,
                         "size from D2D GetPixelSize (returns a struct by value): %ux%u%s\r\n",
                         by_value.width, by_value.height,
                         (by_value.width == cw && by_value.height == ch)
                           ? "" : "   <-- DISAGREES, this call's ABI is wrong here");

                ID2D1Bitmap_Release(bitmap);
            } else if (hr == (HRESULT)D2DERR_UNSUPPORTED_PIXEL_FORMAT) {
                diag_add(buffer, buffer_size, &used,
                         "  the target's pixel format does not accept this bitmap\r\n");
            }
        }
        IWICFormatConverter_Release(converter);
    }

    if (transform) IWICColorTransform_Release(transform);
    IWICBitmapFrameDecode_Release(frame);
    IWICBitmapDecoder_Release(decoder);
    return (u8str_t){ .ptr = buffer, .len = used };
}

#endif /* _WIN32 */
