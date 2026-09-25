#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <d3d11_4.h>   /* ID3D11Multithread (RV-062) */
#include <dxgi1_2.h>
#include <dwrite.h>
#include <string.h>
#include <stdio.h>

/*
 * MinGW declares IID_IDWriteFactory with DEFINE_GUID but its import
 * library does not export it, so the value is defined here for this
 * translation unit. The bytes are exactly those in the toolchain's own
 * dwrite.h declaration (b859ee5a-d838-4b5b-a2e8-1adc7d93db48).
 */
static const GUID RUBRAVIEW_IID_IDWriteFactory =
    { 0xb859ee5a, 0xd838, 0x4b5b, { 0xa2, 0xe8, 0x1a, 0xdc, 0x7d, 0x93, 0xdb, 0x48 } };

/*
 * The same applies to the Direct2D 1.1 and DXGI interface ids the device
 * path needs. Each value is copied from the toolchain's own header
 * (d2d1_1.h, dxgi.h, dxgi1_2.h) rather than written from memory.
 */
static const GUID RV_IID_ID2D1Factory1 =
    { 0xbb12d362, 0xdaee, 0x4b9a, { 0xaa, 0x1d, 0x14, 0xba, 0x40, 0x1c, 0xfa, 0x1f } };
static const GUID RV_IID_IDXGIDevice =
    { 0x54ec77fa, 0x1377, 0x44e6, { 0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c } };
static const GUID RV_IID_IDXGIFactory2 =
    { 0x50c83a1c, 0xe072, 0x4c48, { 0x87, 0xb0, 0x36, 0x30, 0xfa, 0x36, 0xa6, 0xd0 } };
static const GUID RV_IID_IDXGISurface =
    { 0xcafcb56c, 0x6ac3, 0x4889, { 0xbf, 0x47, 0x9e, 0x23, 0xbb, 0xd2, 0x60, 0xec } };
#include "rubraview/pal/pal_render.h"
#include "rubraview/pal/pal_render_d2d_internal.h"

#define VIDEO_SOURCES 8   /* the decoder's slot textures a film's texture has opened (4 slots, a spare) */

struct rubraview_texture {
    ID2D1Bitmap *bitmap;
    ID3D11Texture2D *d3d;                /* RV-062: a film's texture on the card; NULL otherwise */
    /* RV-062: the decoder's shared slot textures, opened on this device. */
    struct { const void *source; ID3D11Texture2D *opened; IDXGIKeyedMutex *mutex; } sources[VIDEO_SOURCES];
    int32_t width, height;
    struct rubraview_renderer *owner;    /* so destroy can recycle without a global */
    struct rubraview_texture *next_free; /* recycled struct free list */
};

/*
 * RV-064 moved this backend from Direct2D 1.0's ID2D1HwndRenderTarget to
 * 1.1's ID2D1DeviceContext on a DXGI swap chain. Three things needed it,
 * and all three were blocked until now:
 *
 *   - cubic bitmap interpolation (§3.4), which 1.0 simply does not have;
 *   - the effect graph behind §3.13's live adjustment preview;
 *   - the slide-show cross-fade (§3.2.5), which M3 had to leave undrawn.
 *
 * A device context *is* an ID2D1RenderTarget, so every drawing call in
 * this file and in the WIC backend carried over unchanged.
 */
/* Where the last failure happened, and what the OS said about it. A
   global rather than a field because the failure that matters most is
   the one where no renderer exists to hold it. */
static const char *g_last_error = NULL;
static HRESULT g_last_hresult = S_OK;
static D3D_FEATURE_LEVEL g_feature_level = (D3D_FEATURE_LEVEL)0;
static bool g_used_warp = false;

static bool fail_step(const char *what, HRESULT hr) {
    g_last_error = what;
    g_last_hresult = hr;
    return false;
}

u8str_t rubraview_pal_render_last_error(void) {
    if (!g_last_error) return (u8str_t){ .ptr = "", .len = 0 };
    return (u8str_t){ .ptr = g_last_error, .len = strlen(g_last_error) };
}

uint32_t rubraview_pal_render_last_hresult(void) {
    return (uint32_t)g_last_hresult;
}

struct rubraview_renderer {
    proven_arena_t *arena;
    ID2D1Factory1 *factory;
    ID2D1Device *device;
    ID2D1DeviceContext *target;    /* the render target, and the effect graph's owner */
    ID3D11Device *d3d;
    ID3D11Device *decode_d3d;      /* RV-062: the decoder's own device, same card; made on first use */
    IDXGISwapChain1 *swap_chain;
    ID2D1Bitmap1 *back_buffer;
    IDWriteFactory *dwrite;      /* NULL when DirectWrite is unavailable: text is then skipped, not fatal */
    HWND hwnd;
    int32_t width, height;
    bool drawing;
    struct rubraview_texture *free_textures;
};

/* ---- internal accessors shared with the WIC image backend ---- */

ID2D1RenderTarget *rubraview_d2d_render_target(rubraview_renderer_t *renderer) {
    return renderer ? (ID2D1RenderTarget*)renderer->target : NULL;
}

ID2D1DeviceContext *rubraview_d2d_device_context(rubraview_renderer_t *renderer) {
    return renderer ? renderer->target : NULL;
}

rubraview_texture_t *rubraview_d2d_texture_wrap(rubraview_renderer_t *renderer, ID2D1Bitmap *bitmap,
                                                int32_t width, int32_t height) {
    if (!renderer || !bitmap) return NULL;

    struct rubraview_texture *tex = renderer->free_textures;
    if (tex) {
        renderer->free_textures = tex->next_free;
    } else {
        proven_result_mem_mut_t res = proven_arena_alloc(renderer->arena, sizeof(struct rubraview_texture));
        if (!proven_is_ok(res.err)) return NULL;
        tex = (struct rubraview_texture*)(void*)res.value.ptr;
    }

    /* The size comes from the caller, not from GetPixelSize — see the
       note on this function in pal_render_d2d_internal.h. */
    tex->bitmap = bitmap;
    tex->d3d = NULL;
    memset(tex->sources, 0, sizeof(tex->sources));
    tex->width = width;
    tex->height = height;
    tex->owner = renderer;
    tex->next_free = NULL;
    return tex;
}

/* Recycles the struct so a long viewing session does not grow the arena
   by one struct per image opened; the COM bitmap itself is released. */
static void texture_recycle(rubraview_renderer_t *renderer, struct rubraview_texture *tex) {
    tex->bitmap = NULL;
    tex->next_free = renderer->free_textures;
    renderer->free_textures = tex;
}

/* ---- lifecycle ---- */

/* The swap chain's back buffer, wrapped as a Direct2D bitmap and made
   the context's target. Called at start-up and again after every resize,
   because the back buffer is a different surface each time. */
static bool bind_back_buffer(struct rubraview_renderer *r) {
    IDXGISurface *surface = NULL;
    HRESULT hr_buffer = IDXGISwapChain1_GetBuffer(r->swap_chain, 0, &RV_IID_IDXGISurface, (void**)&surface);
    if (FAILED(hr_buffer) || !surface) {
        return fail_step("getting the swap chain's back buffer", hr_buffer);
    }

    D2D1_BITMAP_PROPERTIES1 props = {
        /* The alpha mode has to agree with the swap chain's. The chain
           below is created with DXGI_ALPHA_MODE_IGNORE — an ordinary
           opaque window — so the bitmap must ignore alpha too. Asking
           for premultiplied against an opaque chain is rejected, and the
           rejection is silent unless someone is looking. */
        .pixelFormat = { .format = DXGI_FORMAT_B8G8R8A8_UNORM, .alphaMode = D2D1_ALPHA_MODE_IGNORE },
        /* Physical pixels: the viewport transform (RV-022) already
           accounts for DPI, so Direct2D must not scale a second time. */
        .dpiX = 96.0f, .dpiY = 96.0f,
        .bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        .colorContext = NULL,
    };

    HRESULT hr = ID2D1DeviceContext_CreateBitmapFromDxgiSurface(r->target, surface, &props, &r->back_buffer);
    IDXGISurface_Release(surface);
    if (FAILED(hr) || !r->back_buffer) {
        return fail_step("wrapping the back buffer as a Direct2D bitmap", hr);
    }

    ID2D1DeviceContext_SetTarget(r->target, (struct ID2D1Image*)r->back_buffer);
    ID2D1RenderTarget_SetDpi((ID2D1RenderTarget*)r->target, 96.0f, 96.0f);
    return true;
}

static void release_back_buffer(struct rubraview_renderer *r) {
    if (r->target) ID2D1DeviceContext_SetTarget(r->target, NULL);
    if (r->back_buffer) {
        /* MinGW's C headers do not chain the inherited methods on the
           1.1 interfaces, so the base interface is the one to call. */
        ID2D1Bitmap_Release((ID2D1Bitmap*)r->back_buffer);
        r->back_buffer = NULL;
    }
}

/* Brings up D3D11, Direct2D and the swap chain. The hardware driver is
   tried first and WARP — the software rasteriser — second, so a machine
   with no usable GPU still shows images rather than failing to start. */
static bool create_device(struct rubraview_renderer *r) {
    /* BGRA is required by Direct2D; video support lets the device be lent
       to a decoder (RV-062). A driver that refuses the second gets the
       first alone. */
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,  D3D_FEATURE_LEVEL_9_1,
    };

    g_used_warp = false;
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags,
                                   levels, (UINT)(sizeof(levels) / sizeof(levels[0])),
                                   D3D11_SDK_VERSION, &r->d3d, &g_feature_level, NULL);
    if (FAILED(hr)) {
        flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags,
                               levels, (UINT)(sizeof(levels) / sizeof(levels[0])),
                               D3D11_SDK_VERSION, &r->d3d, &g_feature_level, NULL);
    }
    if (FAILED(hr)) {
        g_used_warp = true;
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, flags,
                               levels, (UINT)(sizeof(levels) / sizeof(levels[0])),
                               D3D11_SDK_VERSION, &r->d3d, &g_feature_level, NULL);
    }
    if (FAILED(hr) || !r->d3d) return fail_step("creating the Direct3D 11 device", hr);

    IDXGIDevice *dxgi_device = NULL;
    HRESULT hr_qi = ID3D11Device_QueryInterface(r->d3d, &RV_IID_IDXGIDevice, (void**)&dxgi_device);
    if (FAILED(hr_qi) || !dxgi_device) {
        return fail_step("asking the D3D device for its DXGI interface", hr_qi);
    }

    hr = ID2D1Factory1_CreateDevice(r->factory, dxgi_device, &r->device);
    if (FAILED(hr) || !r->device) {
        IDXGIDevice_Release(dxgi_device);
        return fail_step("creating the Direct2D device", hr);
    }

    hr = ID2D1Device_CreateDeviceContext(r->device, D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &r->target);
    if (FAILED(hr) || !r->target) {
        IDXGIDevice_Release(dxgi_device);
        return fail_step("creating the Direct2D device context", hr);
    }

    IDXGIAdapter *adapter = NULL;
    IDXGIFactory2 *dxgi_factory = NULL;
    if (SUCCEEDED(IDXGIDevice_GetAdapter(dxgi_device, &adapter)) && adapter) {
        IDXGIAdapter_GetParent(adapter, &RV_IID_IDXGIFactory2, (void**)&dxgi_factory);
        IDXGIAdapter_Release(adapter);
    }
    IDXGIDevice_Release(dxgi_device);
    if (!dxgi_factory) return fail_step("finding the DXGI factory", E_FAIL);

    DXGI_SWAP_CHAIN_DESC1 desc = {
        .Width = (UINT)r->width,
        .Height = (UINT)r->height,
        .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
        .Stereo = FALSE,
        .SampleDesc = { .Count = 1, .Quality = 0 },
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = 2,
        .Scaling = DXGI_SCALING_STRETCH,
        .SwapEffect = DXGI_SWAP_EFFECT_DISCARD,
        .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
        .Flags = 0,
    };

    hr = IDXGIFactory2_CreateSwapChainForHwnd(dxgi_factory, (IUnknown*)r->d3d, r->hwnd,
                                              &desc, NULL, NULL, &r->swap_chain);
    IDXGIFactory2_Release(dxgi_factory);
    if (FAILED(hr) || !r->swap_chain) return fail_step("creating the swap chain for the window", hr);

    return bind_back_buffer(r);
}

/*
 * §3.13 describes the live preview as a Direct2D effect graph. That is
 * not reachable from here: MinGW's `d2d1_1.h` declares ID2D1Effect for
 * C++ only — there is no C vtable for it — so a C program on this
 * toolchain cannot build one.
 *
 * The preview is therefore computed the other way round: the *same*
 * commit code runs on a reduced-size copy of the image (see
 * `rubraview_edit_preview_size`). That is fast enough to move with a
 * slider on any image a screen can show, and it has a property the
 * effect graph does not — the preview and the final result are produced
 * by one implementation, so they cannot drift apart. If a toolchain ever
 * exposes the effect interfaces in C, this is where the graph would go.
 */

u8str_t rubraview_pal_render_describe(rubraview_renderer_t *renderer, char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size < 32) return (u8str_t){ .ptr = "", .len = 0 };

    int written = snprintf(buffer, buffer_size,
        "driver: %s   feature level: 0x%04X   size: %dx%d   swap chain: %s   text: %s",
        g_used_warp ? "WARP (software)" : "hardware",
        (unsigned)g_feature_level,
        renderer ? renderer->width : 0,
        renderer ? renderer->height : 0,
        (renderer && renderer->swap_chain) ? "yes" : "no",
        (renderer && renderer->dwrite) ? "DirectWrite" : "unavailable");

    if (written <= 0) return (u8str_t){ .ptr = "", .len = 0 };
    return (u8str_t){ .ptr = buffer, .len = (size_t)written };
}

rubraview_renderer_t *rubraview_pal_render_create(proven_arena_t *arena, void *native_window_handle, int32_t width, int32_t height) {
    g_last_error = NULL;
    g_last_hresult = S_OK;
    if (!arena || !native_window_handle || width <= 0 || height <= 0) {
        fail_step("bad arguments to the renderer", E_INVALIDARG);
        return NULL;
    }

    proven_result_mem_mut_t res = proven_arena_alloc(arena, sizeof(struct rubraview_renderer));
    if (!proven_is_ok(res.err)) return NULL;
    struct rubraview_renderer *r = (struct rubraview_renderer*)(void*)res.value.ptr;
    memset(r, 0, sizeof(*r));

    r->arena = arena;
    r->hwnd = (HWND)native_window_handle;
    r->width = width;
    r->height = height;

    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &RV_IID_ID2D1Factory1, NULL, (void**)&r->factory);
    if (FAILED(hr) || !r->factory) {
        fail_step("creating the Direct2D factory", hr);
        return NULL;
    }

    if (!create_device(r)) {
        rubraview_pal_render_destroy(r);
        return NULL;
    }

    /* Text is chrome, not the canvas: if DirectWrite cannot start the
       viewer still shows images, just without the OSD and tile captions. */
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &RUBRAVIEW_IID_IDWriteFactory,
                                   (IUnknown**)&r->dwrite))) {
        r->dwrite = NULL;
    }

    return r;
}

void rubraview_pal_render_destroy(rubraview_renderer_t *renderer) {
    if (!renderer) return;
    release_back_buffer(renderer);
    if (renderer->swap_chain) { IDXGISwapChain1_Release(renderer->swap_chain); renderer->swap_chain = NULL; }
    if (renderer->target) { ID2D1RenderTarget_Release((ID2D1RenderTarget*)renderer->target); renderer->target = NULL; }
    if (renderer->device) { ID2D1Resource_Release((ID2D1Resource*)renderer->device); renderer->device = NULL; }
    if (renderer->d3d) { ID3D11Device_Release(renderer->d3d); renderer->d3d = NULL; }
    /* A film still open holds its own reference to the decoder's device. */
    if (renderer->decode_d3d) { ID3D11Device_Release(renderer->decode_d3d); renderer->decode_d3d = NULL; }
    if (renderer->dwrite) {
        IDWriteFactory_Release(renderer->dwrite);
        renderer->dwrite = NULL;
    }
    if (renderer->factory) {
        ID2D1Factory_Release((ID2D1Factory*)renderer->factory);
        renderer->factory = NULL;
    }
}

bool rubraview_pal_render_resize(rubraview_renderer_t *renderer, int32_t width, int32_t height) {
    if (!renderer || !renderer->target || !renderer->swap_chain || width <= 0 || height <= 0) return false;
    renderer->width = width;
    renderer->height = height;

    /* The back buffer must be let go before the swap chain can resize —
       a surface still referenced cannot be replaced. */
    release_back_buffer(renderer);
    HRESULT hr = IDXGISwapChain1_ResizeBuffers(renderer->swap_chain, 0, (UINT)width, (UINT)height,
                                               DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) return false;
    return bind_back_buffer(renderer);
}

/* ---- frame ---- */

void rubraview_pal_render_begin(rubraview_renderer_t *renderer, uint32_t clear_argb) {
    if (!renderer || !renderer->target) return;

    ID2D1RenderTarget *rt = (ID2D1RenderTarget*)renderer->target;
    ID2D1RenderTarget_BeginDraw(rt);
    renderer->drawing = true;

    D2D1_COLOR_F color = {
        .r = (float)((clear_argb >> 16) & 0xFF) / 255.0f,
        .g = (float)((clear_argb >> 8) & 0xFF) / 255.0f,
        .b = (float)(clear_argb & 0xFF) / 255.0f,
        .a = (float)((clear_argb >> 24) & 0xFF) / 255.0f,
    };
    ID2D1RenderTarget_Clear(rt, &color);
}

bool rubraview_pal_render_end(rubraview_renderer_t *renderer) {
    if (!renderer || !renderer->target || !renderer->drawing) return true;

    ID2D1RenderTarget *rt = (ID2D1RenderTarget*)renderer->target;
    HRESULT hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    renderer->drawing = false;

    if (SUCCEEDED(hr) && renderer->swap_chain) {
        hr = IDXGISwapChain1_Present(renderer->swap_chain, 1, 0);
    }

    if (hr == (HRESULT)D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED ||
        hr == DXGI_ERROR_DEVICE_RESET) {
        /* The device is gone — a driver update, a GPU reset, a laptop
           switching graphics adapters. Everything built on it died with
           it, textures included, so the whole device is rebuilt and the
           caller is told to reload. */
        release_back_buffer(renderer);
        if (renderer->swap_chain) { IDXGISwapChain1_Release(renderer->swap_chain); renderer->swap_chain = NULL; }
        if (renderer->target) { ID2D1RenderTarget_Release((ID2D1RenderTarget*)renderer->target); renderer->target = NULL; }
        if (renderer->device) { ID2D1Resource_Release((ID2D1Resource*)renderer->device); renderer->device = NULL; }
        if (renderer->d3d) { ID3D11Device_Release(renderer->d3d); renderer->d3d = NULL; }
        renderer->free_textures = NULL;
        (void)create_device(renderer);
        return false; /* recreated or not, the caller must reload textures */
    }
    return SUCCEEDED(hr);
}

/* ---- drawing ---- */

static D2D1_MATRIX_3X2_F to_d2d_matrix(rubraview_mat3x2_t m) {
    /* rubraview: x' = a*x + c*y + e, y' = b*x + d*y + f
       Direct2D: x' = x*_11 + y*_21 + _31, y' = x*_12 + y*_22 + _32 */
    D2D1_MATRIX_3X2_F out;
    out._11 = (FLOAT)m.a; out._12 = (FLOAT)m.b;
    out._21 = (FLOAT)m.c; out._22 = (FLOAT)m.d;
    out._31 = (FLOAT)m.e; out._32 = (FLOAT)m.f;
    return out;
}

/*
 * With the device context (RV-064) the cubic modes are finally real. Under
 * Direct2D 1.0 they had to be drawn as linear, which is the limitation M2
 * recorded; nothing else about §3.4 changed, and NEAREST — the mode pixel
 * art depends on (§3.5) — was always exact.
 */
static D2D1_INTERPOLATION_MODE to_d2d_interpolation(rubraview_interpolation_t interp) {
    switch (interp) {
        case RUBRAVIEW_INTERP_NEAREST:            return D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR;
        case RUBRAVIEW_INTERP_CUBIC:              return D2D1_INTERPOLATION_MODE_CUBIC;
        case RUBRAVIEW_INTERP_HIGH_QUALITY_CUBIC: return D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC;
        case RUBRAVIEW_INTERP_LINEAR:
        default:                                  return D2D1_INTERPOLATION_MODE_LINEAR;
    }
}

static void draw_region_opacity(rubraview_renderer_t *renderer,
                                const rubraview_texture_t *texture,
                                rubraview_src_rect_t src,
                                rubraview_mat3x2_t transform,
                                rubraview_interpolation_t interpolation,
                                FLOAT opacity);

void rubraview_pal_render_draw_texture(rubraview_renderer_t *renderer,
                                       const rubraview_texture_t *texture,
                                       rubraview_mat3x2_t transform,
                                       rubraview_interpolation_t interpolation) {
    if (!renderer || !renderer->target || !texture || !texture->bitmap) return;

    rubraview_src_rect_t src = { 0.0, 0.0, (double)texture->width, (double)texture->height };
    rubraview_pal_render_draw_texture_region(renderer, texture, src, transform, interpolation);
}

void rubraview_pal_render_draw_texture_opacity(rubraview_renderer_t *renderer,
                                               const rubraview_texture_t *texture,
                                               rubraview_mat3x2_t transform,
                                               rubraview_interpolation_t interpolation,
                                               double opacity) {
    if (!renderer || !renderer->target || !texture || !texture->bitmap) return;
    if (opacity <= 0.0) return;
    if (opacity > 1.0) opacity = 1.0;

    rubraview_src_rect_t src = { 0.0, 0.0, (double)texture->width, (double)texture->height };
    draw_region_opacity(renderer, texture, src, transform, interpolation, (FLOAT)opacity);
}

static void draw_region_opacity(rubraview_renderer_t *renderer,
                                const rubraview_texture_t *texture,
                                rubraview_src_rect_t src,
                                rubraview_mat3x2_t transform,
                                rubraview_interpolation_t interpolation,
                                FLOAT opacity) {
    ID2D1RenderTarget *rt = (ID2D1RenderTarget*)renderer->target;
    D2D1_MATRIX_3X2_F matrix = to_d2d_matrix(transform);
    ID2D1RenderTarget_SetTransform(rt, &matrix);

    /* The destination is the region's own pixel space at the origin; the
       transform places and scales it on screen. */
    D2D1_RECT_F dest = {
        .left = 0.0f, .top = 0.0f,
        .right = (FLOAT)(src.right - src.left),
        .bottom = (FLOAT)(src.bottom - src.top),
    };
    D2D1_RECT_F source = {
        .left = (FLOAT)src.left, .top = (FLOAT)src.top,
        .right = (FLOAT)src.right, .bottom = (FLOAT)src.bottom,
    };

    /* The device context's DrawBitmap is the one that takes a
       D2D1_INTERPOLATION_MODE — the render target's older overload only
       knows the two 1.0 modes.

       It is called through the vtable rather than through the
       ID2D1DeviceContext_DrawBitmap macro because that macro is wrong in
       this toolchain's d2d1_1.h: it expands to a member named
       `ID2D1DeviceContext_DrawBitmap`, while the struct's member is
       `DrawBitmap`. */
    renderer->target->lpVtbl->DrawBitmap(renderer->target, texture->bitmap, &dest, opacity,
                                         to_d2d_interpolation(interpolation), &source, NULL);

    D2D1_MATRIX_3X2_F identity = to_d2d_matrix(rubraview_mat3x2_identity());
    ID2D1RenderTarget_SetTransform(rt, &identity);
}

void rubraview_pal_render_draw_texture_region(rubraview_renderer_t *renderer,
                                              const rubraview_texture_t *texture,
                                              rubraview_src_rect_t src,
                                              rubraview_mat3x2_t transform,
                                              rubraview_interpolation_t interpolation) {
    if (!renderer || !renderer->target || !texture || !texture->bitmap) return;
    draw_region_opacity(renderer, texture, src, transform, interpolation, 1.0f);
}

void rubraview_pal_render_draw_pixel_grid(rubraview_renderer_t *renderer,
                                          rubraview_mat3x2_t transform,
                                          int32_t texture_width,
                                          int32_t texture_height,
                                          uint32_t line_argb) {
    if (!renderer || !renderer->target || texture_width <= 0 || texture_height <= 0) return;

    /* The viewport transform is always axis-aligned (translate + scale;
       rotation is applied as a separate whole-image transform), so a
       texture-space coordinate maps to screen with x' = a*x + e. That
       lets the visible index range be computed directly instead of
       walking every pixel boundary of a large image. */
    double scale_x = transform.a;
    double scale_y = transform.d;
    if (scale_x <= 0.0 || scale_y <= 0.0) return;

    ID2D1RenderTarget *rt = (ID2D1RenderTarget*)renderer->target;

    ID2D1SolidColorBrush *brush = NULL;
    D2D1_COLOR_F color = {
        .r = (float)((line_argb >> 16) & 0xFF) / 255.0f,
        .g = (float)((line_argb >> 8) & 0xFF) / 255.0f,
        .b = (float)(line_argb & 0xFF) / 255.0f,
        .a = (float)((line_argb >> 24) & 0xFF) / 255.0f,
    };
    if (FAILED(ID2D1RenderTarget_CreateSolidColorBrush(rt, &color, NULL, &brush)) || !brush) return;

    D2D1_MATRIX_3X2_F matrix = to_d2d_matrix(transform);
    ID2D1RenderTarget_SetTransform(rt, &matrix);

    /* One physical pixel wide after the transform scales the stroke. */
    FLOAT stroke = (FLOAT)(1.0 / scale_x);

    double first_x = (0.0 - transform.e) / scale_x;
    double last_x = ((double)renderer->width - transform.e) / scale_x;
    if (first_x < 0.0) first_x = 0.0;
    if (last_x > (double)texture_width) last_x = (double)texture_width;

    double first_y = (0.0 - transform.f) / scale_y;
    double last_y = ((double)renderer->height - transform.f) / scale_y;
    if (first_y < 0.0) first_y = 0.0;
    if (last_y > (double)texture_height) last_y = (double)texture_height;

    for (int32_t x = (int32_t)first_x; x <= (int32_t)last_x; ++x) {
        D2D1_POINT_2F a = { (FLOAT)x, (FLOAT)first_y };
        D2D1_POINT_2F b = { (FLOAT)x, (FLOAT)last_y };
        ID2D1RenderTarget_DrawLine(rt, a, b, (ID2D1Brush*)brush, stroke, NULL);
    }
    for (int32_t y = (int32_t)first_y; y <= (int32_t)last_y; ++y) {
        D2D1_POINT_2F a = { (FLOAT)first_x, (FLOAT)y };
        D2D1_POINT_2F b = { (FLOAT)last_x, (FLOAT)y };
        ID2D1RenderTarget_DrawLine(rt, a, b, (ID2D1Brush*)brush, stroke, NULL);
    }

    D2D1_MATRIX_3X2_F identity = to_d2d_matrix(rubraview_mat3x2_identity());
    ID2D1RenderTarget_SetTransform(rt, &identity);
    ID2D1SolidColorBrush_Release(brush);
}

/* ---- textures ---- */

void rubraview_pal_texture_size(const rubraview_texture_t *texture, int32_t *out_width, int32_t *out_height) {
    if (!texture) return;
    if (out_width) *out_width = texture->width;
    if (out_height) *out_height = texture->height;
}

void rubraview_pal_texture_destroy(rubraview_texture_t *texture) {
    if (!texture) return;
    if (texture->bitmap) {
        ID2D1Bitmap_Release(texture->bitmap);
        texture->bitmap = NULL;
    }
    if (texture->d3d) {
        ID3D11Texture2D_Release(texture->d3d);
        texture->d3d = NULL;
    }
    for (int i = 0; i < VIDEO_SOURCES; ++i) {
        if (texture->sources[i].mutex) IDXGIKeyedMutex_Release(texture->sources[i].mutex);
        if (texture->sources[i].opened) ID3D11Texture2D_Release(texture->sources[i].opened);
        texture->sources[i].source = NULL;
        texture->sources[i].opened = NULL;
        texture->sources[i].mutex = NULL;
    }
    if (texture->owner) {
        texture_recycle(texture->owner, texture);
    }
}

/* ---- UI chrome primitives (M3) ---- */

static D2D1_COLOR_F argb_to_color(uint32_t argb) {
    return (D2D1_COLOR_F){
        .r = (float)((argb >> 16) & 0xFF) / 255.0f,
        .g = (float)((argb >> 8) & 0xFF) / 255.0f,
        .b = (float)(argb & 0xFF) / 255.0f,
        .a = (float)((argb >> 24) & 0xFF) / 255.0f,
    };
}

static D2D1_RECT_F to_d2d_rect(rubraview_pal_rect_t r) {
    return (D2D1_RECT_F){
        .left = (FLOAT)r.x, .top = (FLOAT)r.y,
        .right = (FLOAT)(r.x + r.width), .bottom = (FLOAT)(r.y + r.height),
    };
}

/* Chrome is drawn in client pixels, so the viewport transform must not
   apply to it. */
static void set_identity(ID2D1RenderTarget *rt) {
    D2D1_MATRIX_3X2_F identity = to_d2d_matrix(rubraview_mat3x2_identity());
    ID2D1RenderTarget_SetTransform(rt, &identity);
}

void rubraview_pal_render_fill_rect(rubraview_renderer_t *renderer,
                                    rubraview_pal_rect_t rect,
                                    uint32_t argb,
                                    double corner_radius) {
    if (!renderer || !renderer->target) return;
    ID2D1RenderTarget *rt = (ID2D1RenderTarget*)renderer->target;

    ID2D1SolidColorBrush *brush = NULL;
    D2D1_COLOR_F color = argb_to_color(argb);
    if (FAILED(ID2D1RenderTarget_CreateSolidColorBrush(rt, &color, NULL, &brush)) || !brush) return;

    set_identity(rt);
    if (corner_radius > 0.0) {
        D2D1_ROUNDED_RECT rr = {
            .rect = to_d2d_rect(rect),
            .radiusX = (FLOAT)corner_radius,
            .radiusY = (FLOAT)corner_radius,
        };
        ID2D1RenderTarget_FillRoundedRectangle(rt, &rr, (ID2D1Brush*)brush);
    } else {
        D2D1_RECT_F r = to_d2d_rect(rect);
        ID2D1RenderTarget_FillRectangle(rt, &r, (ID2D1Brush*)brush);
    }

    ID2D1SolidColorBrush_Release(brush);
}

void rubraview_pal_render_stroke_rect(rubraview_renderer_t *renderer,
                                      rubraview_pal_rect_t rect,
                                      uint32_t argb,
                                      double stroke_width,
                                      double corner_radius) {
    if (!renderer || !renderer->target) return;
    if (stroke_width <= 0.0) stroke_width = 1.0;
    ID2D1RenderTarget *rt = (ID2D1RenderTarget*)renderer->target;

    ID2D1SolidColorBrush *brush = NULL;
    D2D1_COLOR_F color = argb_to_color(argb);
    if (FAILED(ID2D1RenderTarget_CreateSolidColorBrush(rt, &color, NULL, &brush)) || !brush) return;

    set_identity(rt);
    if (corner_radius > 0.0) {
        D2D1_ROUNDED_RECT rr = {
            .rect = to_d2d_rect(rect),
            .radiusX = (FLOAT)corner_radius,
            .radiusY = (FLOAT)corner_radius,
        };
        ID2D1RenderTarget_DrawRoundedRectangle(rt, &rr, (ID2D1Brush*)brush, (FLOAT)stroke_width, NULL);
    } else {
        D2D1_RECT_F r = to_d2d_rect(rect);
        ID2D1RenderTarget_DrawRectangle(rt, &r, (ID2D1Brush*)brush, (FLOAT)stroke_width, NULL);
    }

    ID2D1SolidColorBrush_Release(brush);
}

/* Whether Segoe MDL2 Assets is installed: asked once. */
static int g_icon_font = -1;

static bool icon_font_present(IDWriteFactory *dwrite) {
    if (g_icon_font >= 0) return g_icon_font == 1;
    g_icon_font = 0;
    IDWriteFontCollection *fonts = NULL;
    if (SUCCEEDED(IDWriteFactory_GetSystemFontCollection(dwrite, &fonts, FALSE)) && fonts) {
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (SUCCEEDED(IDWriteFontCollection_FindFamilyName(fonts, L"Segoe MDL2 Assets", &index, &exists)) && exists) {
            g_icon_font = 1;
        }
        IDWriteFontCollection_Release(fonts);
    }
    return g_icon_font == 1;
}

bool rubraview_pal_render_draw_icon(rubraview_renderer_t *renderer, uint32_t code_point,
                                    rubraview_pal_rect_t rect, double size, uint32_t argb) {
    if (!renderer || !renderer->target || !renderer->dwrite || code_point == 0 || code_point > 0xFFFF) return false;
    if (!icon_font_present(renderer->dwrite)) return false;
    IDWriteTextFormat *format = NULL;
    if (FAILED(IDWriteFactory_CreateTextFormat(renderer->dwrite, L"Segoe MDL2 Assets", NULL, DWRITE_FONT_WEIGHT_NORMAL,
                                               DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                               (FLOAT)(size > 0.0 ? size : 16.0), L"", &format)) || !format) {
        return false;
    }
    IDWriteTextFormat_SetTextAlignment(format, DWRITE_TEXT_ALIGNMENT_CENTER);
    IDWriteTextFormat_SetParagraphAlignment(format, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    ID2D1RenderTarget *rt = (ID2D1RenderTarget*)renderer->target;
    ID2D1SolidColorBrush *brush = NULL;
    D2D1_COLOR_F color = argb_to_color(argb);
    bool ok = SUCCEEDED(ID2D1RenderTarget_CreateSolidColorBrush(rt, &color, NULL, &brush)) && brush;
    if (ok) {
        set_identity(rt);
        WCHAR glyph = (WCHAR)code_point;
        D2D1_RECT_F layout = to_d2d_rect(rect);
        ID2D1RenderTarget_DrawText(rt, &glyph, 1, format, &layout, (ID2D1Brush*)brush,
                                   D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
        ID2D1SolidColorBrush_Release(brush);
    }
    IDWriteTextFormat_Release(format);
    return ok;
}

bool rubraview_pal_render_draw_text(rubraview_renderer_t *renderer,
                                    u8str_t text,
                                    rubraview_pal_rect_t rect,
                                    double font_size,
                                    uint32_t argb,
                                    rubraview_text_align_t align) {
    if (!renderer || !renderer->target || !renderer->dwrite) return false;
    if (text.len == 0 || text.len > 4096) return false;
    if (font_size <= 0.0) font_size = 14.0;

    /* UTF-8 to UTF-16 at the API boundary only (§7.2.3). */
    WCHAR wide[4096];
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, text.ptr, (int)text.len,
                                       wide, (int)(sizeof(wide) / sizeof(wide[0])));
    if (wide_len <= 0) return false;

    IDWriteTextFormat *format = NULL;
    HRESULT hr = IDWriteFactory_CreateTextFormat(renderer->dwrite, L"Segoe UI", NULL,
                                                 DWRITE_FONT_WEIGHT_NORMAL,
                                                 DWRITE_FONT_STYLE_NORMAL,
                                                 DWRITE_FONT_STRETCH_NORMAL,
                                                 (FLOAT)font_size, L"", &format);
    if (FAILED(hr) || !format) return false;

    DWRITE_TEXT_ALIGNMENT text_align = DWRITE_TEXT_ALIGNMENT_LEADING;
    if (align == RUBRAVIEW_TEXT_CENTER) text_align = DWRITE_TEXT_ALIGNMENT_CENTER;
    else if (align == RUBRAVIEW_TEXT_RIGHT) text_align = DWRITE_TEXT_ALIGNMENT_TRAILING;
    IDWriteTextFormat_SetTextAlignment(format, text_align);
    IDWriteTextFormat_SetParagraphAlignment(format, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    ID2D1RenderTarget *rt = (ID2D1RenderTarget*)renderer->target;
    ID2D1SolidColorBrush *brush = NULL;
    D2D1_COLOR_F color = argb_to_color(argb);
    if (FAILED(ID2D1RenderTarget_CreateSolidColorBrush(rt, &color, NULL, &brush)) || !brush) {
        IDWriteTextFormat_Release(format);
        return false;
    }

    set_identity(rt);
    D2D1_RECT_F layout = to_d2d_rect(rect);
    ID2D1RenderTarget_DrawText(rt, wide, (UINT32)wide_len, (IDWriteTextFormat*)format, &layout,
                               (ID2D1Brush*)brush, D2D1_DRAW_TEXT_OPTIONS_NONE,
                               DWRITE_MEASURING_MODE_NATURAL);

    ID2D1SolidColorBrush_Release(brush);
    IDWriteTextFormat_Release(format);
    return true;
}

bool rubraview_pal_render_measure_text(rubraview_renderer_t *renderer, u8str_t text, double font_size,
                                       double *out_width) {
    if (!renderer || !renderer->dwrite || !out_width) return false;
    *out_width = 0.0;
    if (text.len == 0) return true;
    if (text.len > 4096) return false;
    if (font_size <= 0.0) font_size = 14.0;
    WCHAR wide[4096];
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, text.ptr, (int)text.len, wide, (int)(sizeof(wide) / sizeof(wide[0])));
    if (wide_len <= 0) return false;
    IDWriteTextFormat *format = NULL;
    if (FAILED(IDWriteFactory_CreateTextFormat(renderer->dwrite, L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_NORMAL,
                                               DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                               (FLOAT)font_size, L"", &format)) || !format) {
        return false;
    }
    IDWriteTextFormat_SetWordWrapping(format, DWRITE_WORD_WRAPPING_NO_WRAP);
    IDWriteTextLayout *layout = NULL;
    bool ok = SUCCEEDED(IDWriteFactory_CreateTextLayout(renderer->dwrite, wide, (UINT32)wide_len, format,
                                                        100000.0f, 1000.0f, &layout)) && layout;
    if (ok) {
        DWRITE_TEXT_METRICS metrics;
        ok = SUCCEEDED(IDWriteTextLayout_GetMetrics(layout, &metrics));
        if (ok) *out_width = (double)metrics.widthIncludingTrailingWhitespace;
        IDWriteTextLayout_Release(layout);
    }
    IDWriteTextFormat_Release(format);
    return ok;
}

/* ---- fixed-width text for the settings window (§3.22, D-13) ---- */

/* Consolas ships with every Windows since Vista. A character it has no
   glyph for — Hangul, most of CJK — is drawn by DirectWrite's own font
   fallback; the grid counts those as two cells, which is close to what
   the fallback face draws. */
#define MONO_FAMILY L"Consolas"

static IDWriteTextFormat *mono_format(rubraview_renderer_t *renderer, double font_size) {
    IDWriteTextFormat *format = NULL;
    if (FAILED(IDWriteFactory_CreateTextFormat(renderer->dwrite, MONO_FAMILY, NULL,
                                               DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                               DWRITE_FONT_STRETCH_NORMAL, (FLOAT)font_size, L"", &format)) ||
        !format) {
        return NULL;
    }
    IDWriteTextFormat_SetTextAlignment(format, DWRITE_TEXT_ALIGNMENT_LEADING);
    IDWriteTextFormat_SetParagraphAlignment(format, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    IDWriteTextFormat_SetWordWrapping(format, DWRITE_WORD_WRAPPING_NO_WRAP);
    return format;
}

bool rubraview_pal_render_mono_cell(rubraview_renderer_t *renderer, double font_size,
                                    double *out_width, double *out_height) {
    if (!renderer || !renderer->dwrite || font_size <= 0.0) return false;
    IDWriteTextFormat *format = mono_format(renderer, font_size);
    if (!format) return false;
    /* Ten characters measured, one tenth taken: a single glyph's advance
       is rounded more coarsely than a run of them. */
    IDWriteTextLayout *text = NULL;
    bool ok = SUCCEEDED(IDWriteFactory_CreateTextLayout(renderer->dwrite, L"MMMMMMMMMM", 10, format,
                                                        10000.0f, 1000.0f, &text)) && text;
    if (ok) {
        DWRITE_TEXT_METRICS metrics;
        ok = SUCCEEDED(IDWriteTextLayout_GetMetrics(text, &metrics));
        if (ok) {
            if (out_width) *out_width = (double)metrics.widthIncludingTrailingWhitespace / 10.0;
            if (out_height) *out_height = (double)metrics.height;
        }
        IDWriteTextLayout_Release(text);
    }
    IDWriteTextFormat_Release(format);
    return ok;
}

bool rubraview_pal_render_draw_text_mono(rubraview_renderer_t *renderer, u8str_t text,
                                         double x, double y, double font_size, uint32_t argb) {
    if (!renderer || !renderer->target || !renderer->dwrite) return false;
    if (text.len == 0 || text.len > 4096 || font_size <= 0.0) return false;

    WCHAR wide[4096];
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, text.ptr, (int)text.len,
                                       wide, (int)(sizeof(wide) / sizeof(wide[0])));
    if (wide_len <= 0) return false;

    IDWriteTextFormat *format = mono_format(renderer, font_size);
    if (!format) return false;
    ID2D1RenderTarget *rt = (ID2D1RenderTarget*)renderer->target;
    ID2D1SolidColorBrush *brush = NULL;
    D2D1_COLOR_F color = argb_to_color(argb);
    if (FAILED(ID2D1RenderTarget_CreateSolidColorBrush(rt, &color, NULL, &brush)) || !brush) {
        IDWriteTextFormat_Release(format);
        return false;
    }
    set_identity(rt);
    D2D1_RECT_F layout = { (FLOAT)x, (FLOAT)y, (FLOAT)(x + 100000.0), (FLOAT)(y + font_size * 2.0) };
    ID2D1RenderTarget_DrawText(rt, wide, (UINT32)wide_len, format, &layout, (ID2D1Brush*)brush,
                               D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
    ID2D1SolidColorBrush_Release(brush);
    IDWriteTextFormat_Release(format);
    return true;
}

/* ---- textures filled from memory (video frames, M5) ---- */

rubraview_texture_t *rubraview_pal_texture_create_bgra(rubraview_renderer_t *renderer,
                                                       int32_t width, int32_t height) {
    if (!renderer || !renderer->target || width <= 0 || height <= 0) return NULL;
    /* Decoders leave the fourth byte undefined, so it is ignored rather
       than read as transparency. */
    D2D1_BITMAP_PROPERTIES props = {
        .pixelFormat = { .format = DXGI_FORMAT_B8G8R8A8_UNORM, .alphaMode = D2D1_ALPHA_MODE_IGNORE },
        .dpiX = 96.0f,
        .dpiY = 96.0f,
    };
    D2D1_SIZE_U size = { .width = (UINT32)width, .height = (UINT32)height };
    ID2D1Bitmap *bitmap = NULL;
    HRESULT hr = ID2D1RenderTarget_CreateBitmap((ID2D1RenderTarget*)renderer->target, size, NULL, 0,
                                                &props, &bitmap);
    if (FAILED(hr) || !bitmap) return NULL;
    rubraview_texture_t *texture = rubraview_d2d_texture_wrap(renderer, bitmap, width, height);
    if (!texture) ID2D1Bitmap_Release(bitmap);
    return texture;
}

bool rubraview_pal_texture_upload_bgra(rubraview_texture_t *texture, const uint8_t *pixels, int32_t stride) {
    if (!texture || !texture->bitmap || !pixels || stride <= 0) return false;
    return SUCCEEDED(ID2D1Bitmap_CopyFromMemory(texture->bitmap, NULL, pixels, (UINT32)stride));
}

/* ---- RV-062: a film decoded on the card ---- */

static const GUID RV_IID_ID3D11VideoDevice =
    { 0x10ec4d5b, 0x975a, 0x4689, { 0xb9, 0xe4, 0xd0, 0xaa, 0xc3, 0x0f, 0xe3, 0x33 } };
static const GUID RV_IID_ID3D11Multithread =
    { 0x9b7e4e00, 0x342c, 0x4106, { 0xa1, 0x9f, 0x4f, 0x27, 0x04, 0xf6, 0x89, 0xf0 } };

/* RV-062: the decoder gets a device of its own on the same card, not the
   renderer's. Lent the renderer's, Media Foundation's threads and the
   decode thread used the device Direct2D draws with from this thread, and
   on a real card (the owner's Intel UHD 730, 0.0.7) the whole window went
   black and the film timed out opening; the probe, which always used a
   device of its own, played the same file. Frames cross between the two
   devices as shared textures, on the card. */
void *rubraview_pal_render_video_device(rubraview_renderer_t *renderer, uint32_t *out_decoder_profiles) {
    if (out_decoder_profiles) *out_decoder_profiles = 0;
    if (!renderer || !renderer->d3d) return NULL;
    if (!renderer->decode_d3d) {
        IDXGIDevice *dxgi = NULL;
        IDXGIAdapter *adapter = NULL;
        if (SUCCEEDED(ID3D11Device_QueryInterface(renderer->d3d, &RV_IID_IDXGIDevice, (void**)&dxgi)) && dxgi) {
            IDXGIDevice_GetAdapter(dxgi, &adapter);
            IDXGIDevice_Release(dxgi);
        }
        /* The renderer's card first, with video support; then without it
           (a software adapter has none — Media Foundation's processor still
           turns frames into textures there); then the default card. */
        const struct { bool on_adapter; UINT flags; } TRIES[] = {
            { true,  D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT },
            { true,  D3D11_CREATE_DEVICE_BGRA_SUPPORT },
            { false, D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT },
        };
        HRESULT hr = E_FAIL;
        for (size_t i = 0; i < sizeof(TRIES) / sizeof(TRIES[0]) && !renderer->decode_d3d; ++i) {
            if (TRIES[i].on_adapter && !adapter) continue;
            IDXGIAdapter *a = TRIES[i].on_adapter ? adapter : NULL;
            hr = D3D11CreateDevice(a, a ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, NULL,
                                   TRIES[i].flags, NULL, 0, D3D11_SDK_VERSION, &renderer->decode_d3d, NULL, NULL);
            if (FAILED(hr)) renderer->decode_d3d = NULL;
        }
        if (adapter) IDXGIAdapter_Release(adapter);
        if (!renderer->decode_d3d) {
            g_last_hresult = hr;   /* --probe-gpu prints it */
            return NULL;
        }
        /* Media Foundation uses it from its own threads. */
        ID3D11DeviceContext *context = NULL;
        ID3D11Device_GetImmediateContext(renderer->decode_d3d, &context);
        if (context) {
            ID3D11Multithread *mt = NULL;
            if (SUCCEEDED(ID3D11DeviceContext_QueryInterface(context, &RV_IID_ID3D11Multithread, (void**)&mt)) && mt) {
                ID3D11Multithread_SetMultithreadProtected(mt, TRUE);
                ID3D11Multithread_Release(mt);
            }
            ID3D11DeviceContext_Release(context);
        }
    }
    ID3D11VideoDevice *video = NULL;
    if (SUCCEEDED(ID3D11Device_QueryInterface(renderer->decode_d3d, &RV_IID_ID3D11VideoDevice, (void**)&video)) && video) {
        if (out_decoder_profiles) *out_decoder_profiles = ID3D11VideoDevice_GetVideoDecoderProfileCount(video);
        ID3D11VideoDevice_Release(video);
    }
    return renderer->decode_d3d;
}

rubraview_texture_t *rubraview_pal_texture_create_video(rubraview_renderer_t *renderer, int32_t width, int32_t height) {
    if (!renderer || !renderer->d3d || !renderer->target || width <= 0 || height <= 0) return NULL;
    D3D11_TEXTURE2D_DESC desc = {
        .Width = (UINT)width, .Height = (UINT)height, .MipLevels = 1, .ArraySize = 1,
        .Format = DXGI_FORMAT_B8G8R8A8_UNORM, .SampleDesc = { .Count = 1, .Quality = 0 },
        .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
    };
    ID3D11Texture2D *d3d = NULL;
    if (FAILED(ID3D11Device_CreateTexture2D(renderer->d3d, &desc, NULL, &d3d)) || !d3d) return NULL;
    IDXGISurface *surface = NULL;
    ID2D1Bitmap1 *bitmap = NULL;
    if (SUCCEEDED(ID3D11Texture2D_QueryInterface(d3d, &RV_IID_IDXGISurface, (void**)&surface)) && surface) {
        D2D1_BITMAP_PROPERTIES1 props = {
            .pixelFormat = { .format = DXGI_FORMAT_B8G8R8A8_UNORM, .alphaMode = D2D1_ALPHA_MODE_IGNORE },
            .dpiX = 96.0f, .dpiY = 96.0f, .bitmapOptions = D2D1_BITMAP_OPTIONS_NONE,
        };
        if (FAILED(ID2D1DeviceContext_CreateBitmapFromDxgiSurface(renderer->target, surface, &props, &bitmap))) bitmap = NULL;
        IDXGISurface_Release(surface);
    }
    if (!bitmap) { ID3D11Texture2D_Release(d3d); return NULL; }
    rubraview_texture_t *texture = rubraview_d2d_texture_wrap(renderer, (ID2D1Bitmap*)bitmap, width, height);
    if (!texture) { ID2D1Bitmap_Release((ID2D1Bitmap*)bitmap); ID3D11Texture2D_Release(d3d); return NULL; }
    texture->d3d = d3d;
    return texture;
}

static const GUID RV_IID_IDXGIResource =
    { 0x035f3ab4, 0x482e, 0x4e50, { 0xb4, 0x1f, 0x8a, 0x7f, 0x8b, 0xd8, 0x96, 0x0b } };
static const GUID RV_IID_IDXGIKeyedMutex =
    { 0x9d8e1289, 0xd7b3, 0x465f, { 0x81, 0x26, 0x25, 0x0e, 0x34, 0x9a, 0xf8, 0x5d } };
static const GUID RV_IID_ID3D11Texture2D =
    { 0x6f15aaf2, 0xd208, 0x4e89, { 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c } };

bool rubraview_pal_texture_copy_video_frame(rubraview_texture_t *texture, const void *frame_texture,
                                            uint32_t subresource) {
    if (!texture || !texture->d3d || !texture->owner || !frame_texture) return false;
    ID3D11Device *device = texture->owner->d3d;
    /* The decoder's slot texture, opened here once through its shared handle. */
    int slot = -1, free_slot = -1;
    for (int i = 0; i < VIDEO_SOURCES; ++i) {
        if (texture->sources[i].source == frame_texture) { slot = i; break; }
        if (!texture->sources[i].source && free_slot < 0) free_slot = i;
    }
    if (slot < 0) {
        if (free_slot < 0) return false;
        ID3D11Texture2D *source = (ID3D11Texture2D*)(void*)(uintptr_t)frame_texture;
        IDXGIResource *resource = NULL;
        HANDLE shared = NULL;
        if (SUCCEEDED(ID3D11Texture2D_QueryInterface(source, &RV_IID_IDXGIResource, (void**)&resource)) && resource) {
            IDXGIResource_GetSharedHandle(resource, &shared);
            IDXGIResource_Release(resource);
        }
        ID3D11Texture2D *opened = NULL;
        if (!shared || FAILED(ID3D11Device_OpenSharedResource(device, shared, &RV_IID_ID3D11Texture2D, (void**)&opened)) || !opened) {
            return false;
        }
        IDXGIKeyedMutex *mutex = NULL;
        if (FAILED(ID3D11Texture2D_QueryInterface(opened, &RV_IID_IDXGIKeyedMutex, (void**)&mutex))) mutex = NULL;
        texture->sources[free_slot].source = frame_texture;
        texture->sources[free_slot].opened = opened;
        texture->sources[free_slot].mutex = mutex;
        slot = free_slot;
    }
    ID3D11DeviceContext *context = NULL;
    ID3D11Device_GetImmediateContext(device, &context);
    if (!context) return false;
    IDXGIKeyedMutex *mutex = texture->sources[slot].mutex;
    /* The decode thread writes the slot under the same key: a frame is read
       only once the other device has finished writing it. */
    bool held = !mutex || IDXGIKeyedMutex_AcquireSync(mutex, 0, 100) == S_OK;
    if (held) {
        D3D11_BOX box = { .left = 0, .top = 0, .front = 0,
                          .right = (UINT)texture->width, .bottom = (UINT)texture->height, .back = 1 };
        ID3D11DeviceContext_CopySubresourceRegion(context, (ID3D11Resource*)texture->d3d, 0, 0, 0, 0,
                                                  (ID3D11Resource*)texture->sources[slot].opened, subresource, &box);
        if (mutex) IDXGIKeyedMutex_ReleaseSync(mutex, 0);
    }
    ID3D11DeviceContext_Release(context);
    return held;
}

bool rubraview_pal_texture_video_brightness(rubraview_texture_t *texture, double *out_brightness) {
    if (!texture || !texture->d3d || !texture->owner || !out_brightness) return false;
    ID3D11Device *device = texture->owner->d3d;
    D3D11_TEXTURE2D_DESC desc = {
        .Width = (UINT)texture->width, .Height = (UINT)texture->height, .MipLevels = 1, .ArraySize = 1,
        .Format = DXGI_FORMAT_B8G8R8A8_UNORM, .SampleDesc = { .Count = 1, .Quality = 0 },
        .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ,
    };
    ID3D11Texture2D *staging = NULL;
    if (FAILED(ID3D11Device_CreateTexture2D(device, &desc, NULL, &staging)) || !staging) return false;
    ID3D11DeviceContext *context = NULL;
    ID3D11Device_GetImmediateContext(device, &context);
    bool ok = false;
    if (context) {
        ID3D11DeviceContext_CopyResource(context, (ID3D11Resource*)staging, (ID3D11Resource*)texture->d3d);
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ID3D11DeviceContext_Map(context, (ID3D11Resource*)staging, 0, D3D11_MAP_READ, 0, &mapped))) {
            double sum = 0.0;
            long n = 0;
            for (int32_t y = 0; y < texture->height; y += 8) {
                const uint8_t *row = (const uint8_t*)mapped.pData + (size_t)y * mapped.RowPitch;
                for (int32_t x = 0; x < texture->width; x += 8, ++n) sum += (row[x * 4] + row[x * 4 + 1] + row[x * 4 + 2]) / 3.0;
            }
            ID3D11DeviceContext_Unmap(context, (ID3D11Resource*)staging, 0);
            *out_brightness = n > 0 ? sum / (double)n : 0.0;
            ok = true;
        }
        ID3D11DeviceContext_Release(context);
    }
    ID3D11Texture2D_Release(staging);
    return ok;
}

#endif /* _WIN32 */
