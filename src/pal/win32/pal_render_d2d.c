#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <d3d11_4.h>   /* ID3D11Multithread (RV-062) */
#include <dxgi1_2.h>
#include <dxgi1_6.h>   /* IDXGISwapChain3, IDXGIOutput6 (RV-153) */
#include <dwrite.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

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
#include "rubraview/pal/pal_hdr.h"
#include "rubraview/lang.h"
#include "rubraview/screencopy.h"

#define VIDEO_SOURCES 8   /* the decoder's slot textures a film's texture has opened (4 slots, a spare) */

struct rubraview_texture {
    ID2D1Bitmap *bitmap;
    ID3D11Texture2D *d3d;                /* RV-062: a film's texture on the card; NULL otherwise */
    bool fp16;                           /* RV-153: R16G16B16A16_FLOAT, for an HDR screen */
    /* RV-062: the decoder's shared slot textures, opened on this device. */
    struct { const void *source; ID3D11Texture2D *opened; IDXGIKeyedMutex *mutex; } sources[VIDEO_SOURCES];
    int32_t width, height;
    uint64_t content;                    /* RV-166: a new value at every write into it (screencopy.h's key) */
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

proven_u8str_view_t rubraview_pal_render_last_error(void) {
    if (!g_last_error) return PROVEN_LIT("");
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)g_last_error, .size = strlen(g_last_error) };
}

uint32_t rubraview_pal_render_last_hresult(void) {
    return (uint32_t)g_last_hresult;
}

struct rubraview_renderer {
    proven_arena_t *arena;
    double present_seconds;        /* RV-215: the last Present, waiting for the screen included */
    WCHAR text_family[64];         /* RV-107: "" for the program's own face */
    DWRITE_FONT_WEIGHT text_weight;
    DWRITE_FONT_STYLE  text_style;
    WCHAR ui_family[64];           /* RV-136: the program's own face; "" for Segoe UI */
    WCHAR mono_family[64];         /* 1.25.0: the settings window's face; "" for Consolas */
    double mono_pad;               /* 1.25.0: room above each fixed-width line (half the extra line spacing) */
    DWRITE_FONT_WEIGHT ui_weight;
    DWRITE_FONT_STYLE  ui_style;
    ID2D1Factory1 *factory;
    ID2D1Device *device;
    ID2D1DeviceContext *target;    /* the render target, and the effect graph's owner */
    ID3D11Device *d3d;
    ID3D11Device *decode_d3d;      /* RV-062: the decoder's own device, same card; made on first use */
    bool force_lost;               /* the next present is treated as a lost device (a test's trigger) */
    IDXGISwapChain1 *swap_chain;
    int32_t present_model;         /* RUBRAVIEW_PRESENT_*: how the swap chain hands frames to the compositor */
    ID2D1Bitmap1 *back_buffer;     /* Direct2D's target: the swap chain's buffer, or (HDR) the scene */
    /* RV-153 (D-183): the window sent to an HDR screen as scRGB, FP16 */
    bool hdr;                      /* in that mode */
    double hdr_sdr_white;          /* the nits the scene's 1.0 stands for */
    ID3D11Texture2D *scene;
    ID3D11ShaderResourceView *scene_view;
    ID3D11RenderTargetView *back_view;
    rubraview_hdr_present_t *hdr_present;
    const char *hdr_why;           /* the last refusal (an N_ literal) */
    IDWriteFactory *dwrite;      /* NULL when DirectWrite is unavailable: text is then skipped, not fatal */
    HWND hwnd;
    int32_t width, height;
    bool drawing;
    struct rubraview_texture *free_textures;
    /* RV-166 (D-199): without a card, a settled page is drawn from a copy at screen size */
    bool software;                 /* WARP or Microsoft's Basic Render Driver */
    uint64_t content_serial;       /* the last value handed to a texture's `content` */
    rubraview_screencopy_t screencopy;
    ID2D1Bitmap1 *copies[RUBRAVIEW_SCREENCOPY_SLOTS];
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
        /* Its own memory, recycled through the free list: the arena the
           renderer was made with may fill and be left behind (2026-09-29). */
        tex = (struct rubraview_texture*)calloc(1, sizeof(struct rubraview_texture));
        if (!tex) return NULL;
    }

    /* The size comes from the caller, not from GetPixelSize — see the
       note on this function in pal_render_d2d_internal.h. */
    tex->bitmap = bitmap;
    tex->d3d = NULL;
    memset(tex->sources, 0, sizeof(tex->sources));
    tex->width = width;
    tex->height = height;
    tex->content = ++renderer->content_serial;
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

/* RV-166: the copies screencopy.c let go of. */
static void free_copies(rubraview_renderer_t *renderer, const bool drop[RUBRAVIEW_SCREENCOPY_SLOTS]) {
    for (int i = 0; i < RUBRAVIEW_SCREENCOPY_SLOTS; ++i) {
        if (drop[i] && renderer->copies[i]) {
            ID2D1Bitmap_Release((ID2D1Bitmap*)renderer->copies[i]);
            renderer->copies[i] = NULL;
        }
    }
}

static void drop_all_copies(rubraview_renderer_t *renderer) {
    bool drop[RUBRAVIEW_SCREENCOPY_SLOTS];
    rubraview_screencopy_reset(&renderer->screencopy, drop);
    for (int i = 0; i < RUBRAVIEW_SCREENCOPY_SLOTS; ++i) drop[i] = true;   /* whatever is held goes */
    free_copies(renderer, drop);
}

/* ---- lifecycle ---- */

/* The swap chain's back buffer, wrapped as a Direct2D bitmap and made
   the context's target. Called at start-up and again after every resize,
   because the back buffer is a different surface each time. */
static const GUID RV_IID_ID3D11Texture2D_bb =
    { 0x6f15aaf2, 0xd208, 0x4e89, { 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c } };

/* RV-153: Direct2D draws into an FP16 scene; the swap chain's FP16 buffer is written by our pass. */
static bool bind_hdr_scene(struct rubraview_renderer *r) {
    ID3D11Texture2D *back = NULL;
    HRESULT hr = IDXGISwapChain1_GetBuffer(r->swap_chain, 0, &RV_IID_ID3D11Texture2D_bb, (void**)&back);
    if (FAILED(hr) || !back) return fail_step("getting the swap chain's FP16 buffer", hr);
    hr = ID3D11Device_CreateRenderTargetView(r->d3d, (ID3D11Resource*)back, NULL, &r->back_view);
    ID3D11Texture2D_Release(back);
    if (FAILED(hr) || !r->back_view) { r->back_view = NULL; return fail_step("a view of the FP16 buffer", hr); }
    D3D11_TEXTURE2D_DESC desc = {
        .Width = (UINT)r->width, .Height = (UINT)r->height, .MipLevels = 1, .ArraySize = 1,
        .Format = DXGI_FORMAT_R16G16B16A16_FLOAT, .SampleDesc = { .Count = 1, .Quality = 0 },
        .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
    };
    hr = ID3D11Device_CreateTexture2D(r->d3d, &desc, NULL, &r->scene);
    if (FAILED(hr) || !r->scene) { r->scene = NULL; return fail_step("the FP16 scene", hr); }
    hr = ID3D11Device_CreateShaderResourceView(r->d3d, (ID3D11Resource*)r->scene, NULL, &r->scene_view);
    if (FAILED(hr) || !r->scene_view) { r->scene_view = NULL; return fail_step("a view of the FP16 scene", hr); }
    IDXGISurface *surface = NULL;
    hr = ID3D11Texture2D_QueryInterface(r->scene, &RV_IID_IDXGISurface, (void**)&surface);
    if (FAILED(hr) || !surface) return fail_step("the FP16 scene's surface", hr);
    D2D1_BITMAP_PROPERTIES1 props = {
        .pixelFormat = { .format = DXGI_FORMAT_R16G16B16A16_FLOAT, .alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED },
        .dpiX = 96.0f, .dpiY = 96.0f,
        .bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET,
    };
    hr = ID2D1DeviceContext_CreateBitmapFromDxgiSurface(r->target, surface, &props, &r->back_buffer);
    IDXGISurface_Release(surface);
    if (FAILED(hr) || !r->back_buffer) { r->back_buffer = NULL; return fail_step("wrapping the FP16 scene for Direct2D", hr); }
    ID2D1DeviceContext_SetTarget(r->target, (struct ID2D1Image*)r->back_buffer);
    ID2D1RenderTarget_SetDpi((ID2D1RenderTarget*)r->target, 96.0f, 96.0f);
    return true;
}

static bool bind_back_buffer(struct rubraview_renderer *r) {
    if (r->hdr) return bind_hdr_scene(r);
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
    drop_all_copies(r);   /* RV-166: a resize, an HDR switch, a lost device or the end */
    if (r->target) ID2D1DeviceContext_SetTarget(r->target, NULL);
    if (r->back_buffer) {
        /* MinGW's C headers do not chain the inherited methods on the
           1.1 interfaces, so the base interface is the one to call. */
        ID2D1Bitmap_Release((ID2D1Bitmap*)r->back_buffer);
        r->back_buffer = NULL;
    }
    if (r->scene_view) { ID3D11ShaderResourceView_Release(r->scene_view); r->scene_view = NULL; }
    if (r->scene) { ID3D11Texture2D_Release(r->scene); r->scene = NULL; }
    if (r->back_view) { ID3D11RenderTargetView_Release(r->back_view); r->back_view = NULL; }
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
    r->software = g_used_warp;
    if (SUCCEEDED(IDXGIDevice_GetAdapter(dxgi_device, &adapter)) && adapter) {
        /* RV-166: the hardware path can land on Microsoft's Basic Render Driver (vendor 0x1414), software too */
        DXGI_ADAPTER_DESC desc;
        if (SUCCEEDED(IDXGIAdapter_GetDesc(adapter, &desc)) && desc.VendorId == 0x1414) r->software = true;
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

    /* The flip model (D-179): the compositor takes the back buffer as it lies instead of copying it into the
       window's redirection surface, one copy less a frame and the way to independent flip. FLIP_DISCARD
       (Windows 10) first, FLIP_SEQUENTIAL (8) next, the copy model last. Every frame is drawn whole from a
       clear, so a discarded back buffer loses nothing. A layered window (the floating widget, its opacity)
       keeps the copy model: DWM's alpha is applied to the redirection surface a flip chain does not use. */
    static const struct { DXGI_SWAP_EFFECT effect; int32_t model; } effects[] = {
        { DXGI_SWAP_EFFECT_FLIP_DISCARD, RUBRAVIEW_PRESENT_FLIP_DISCARD },
        { DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, RUBRAVIEW_PRESENT_FLIP_SEQUENTIAL },
        { DXGI_SWAP_EFFECT_DISCARD, RUBRAVIEW_PRESENT_COPY },
    };
    bool layered = (GetWindowLongPtrW(r->hwnd, GWL_EXSTYLE) & WS_EX_LAYERED) != 0;
    hr = E_FAIL;
    for (size_t i = layered ? 2 : 0; i < sizeof(effects) / sizeof(effects[0]); i++) {
        desc.SwapEffect = effects[i].effect;
        hr = IDXGIFactory2_CreateSwapChainForHwnd(dxgi_factory, (IUnknown*)r->d3d, r->hwnd,
                                                  &desc, NULL, NULL, &r->swap_chain);
        if (SUCCEEDED(hr) && r->swap_chain) { r->present_model = effects[i].model; break; }
        r->swap_chain = NULL;
    }
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

proven_u8str_view_t rubraview_pal_render_describe(rubraview_renderer_t *renderer, char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size < 32) return PROVEN_LIT("");

    int written = snprintf(buffer, buffer_size,
        "driver: %s   feature level: 0x%04X   size: %dx%d   swap chain: %s   text: %s",
        g_used_warp ? "WARP (software)" : "hardware",
        (unsigned)g_feature_level,
        renderer ? renderer->width : 0,
        renderer ? renderer->height : 0,
        (renderer && renderer->swap_chain) ? "yes" : "no",
        (renderer && renderer->dwrite) ? "DirectWrite" : "unavailable");

    if (written <= 0) return PROVEN_LIT("");
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)buffer, .size = (size_t)written };
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

void rubraview_pal_render_force_lost(rubraview_renderer_t *renderer) {
    if (renderer) renderer->force_lost = true;
}

void rubraview_pal_render_destroy(rubraview_renderer_t *renderer) {
    if (!renderer) return;
    release_back_buffer(renderer);
    rubraview_pal_hdr_present_free(renderer->hdr_present);
    renderer->hdr_present = NULL;
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
    bool drop[RUBRAVIEW_SCREENCOPY_SLOTS];
    rubraview_screencopy_frame(&renderer->screencopy, drop);   /* RV-166: copies not drawn last frame go */
    free_copies(renderer, drop);
    ID2D1RenderTarget_BeginDraw(rt);
    renderer->drawing = true;
    /* RV-153: ClearType is not promised on a float target */
    ID2D1RenderTarget_SetTextAntialiasMode(rt, renderer->hdr ? D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE : D2D1_TEXT_ANTIALIAS_MODE_DEFAULT);

    D2D1_COLOR_F color = {
        .r = (float)((clear_argb >> 16) & 0xFF) / 255.0f,
        .g = (float)((clear_argb >> 8) & 0xFF) / 255.0f,
        .b = (float)(clear_argb & 0xFF) / 255.0f,
        .a = (float)((clear_argb >> 24) & 0xFF) / 255.0f,
    };
    ID2D1RenderTarget_Clear(rt, &color);
}

double rubraview_pal_render_present_seconds(const rubraview_renderer_t *renderer) {
    return renderer ? renderer->present_seconds : 0.0;
}

bool rubraview_pal_render_end(rubraview_renderer_t *renderer) {
    if (!renderer || !renderer->target || !renderer->drawing) return true;

    ID2D1RenderTarget *rt = (ID2D1RenderTarget*)renderer->target;
    HRESULT hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    renderer->drawing = false;

    if (SUCCEEDED(hr) && renderer->hdr) {   /* RV-153: the scene to the scRGB buffer, the SDR white in place */
        rubraview_pal_hdr_present_run(renderer->hdr_present, renderer->scene_view, renderer->back_view,
                                      renderer->width, renderer->height, renderer->hdr_sdr_white / 80.0, true);
        /* decoded: Direct2D does not linearise on a float target (seen on the VM, 2026-10-21), so the scene
           holds sRGB-encoded values, the film's too */
    }
    renderer->present_seconds = 0.0;
    if (SUCCEEDED(hr) && renderer->swap_chain) {
        LARGE_INTEGER f, a, b;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&a);
        hr = IDXGISwapChain1_Present(renderer->swap_chain, 1, 0);
        QueryPerformanceCounter(&b);
        renderer->present_seconds = (double)(b.QuadPart - a.QuadPart) / (double)f.QuadPart;
    }

    if (renderer->force_lost) {
        renderer->force_lost = false;
        hr = DXGI_ERROR_DEVICE_REMOVED;
    }
    if (hr == (HRESULT)D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED ||
        hr == DXGI_ERROR_DEVICE_RESET) {
        /* The device is gone — a driver update, a GPU reset, a laptop
           switching graphics adapters. Everything built on it died with
           it, textures included, so the whole device is rebuilt and the
           caller is told to reload. */
        release_back_buffer(renderer);
        renderer->hdr = false;   /* RV-153: the new chain starts in SDR; the caller asks again */
        rubraview_pal_hdr_present_free(renderer->hdr_present);
        renderer->hdr_present = NULL;
        if (renderer->swap_chain) { IDXGISwapChain1_Release(renderer->swap_chain); renderer->swap_chain = NULL; }
        if (renderer->target) { ID2D1RenderTarget_Release((ID2D1RenderTarget*)renderer->target); renderer->target = NULL; }
        if (renderer->device) { ID2D1Resource_Release((ID2D1Resource*)renderer->device); renderer->device = NULL; }
        if (renderer->d3d) { ID3D11Device_Release(renderer->d3d); renderer->d3d = NULL; }
        /* A reset or a TDR takes every device on the card, the decoder's
           too (RV-062): a removed one is let go so the film, reopened by
           the caller, gets a fresh one. A living one is kept. */
        if (renderer->decode_d3d && ID3D11Device_GetDeviceRemovedReason(renderer->decode_d3d) != S_OK) {
            ID3D11Device_Release(renderer->decode_d3d);
            renderer->decode_d3d = NULL;
        }
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
static bool draw_from_copy(rubraview_renderer_t *renderer, const rubraview_texture_t *texture,
                           rubraview_mat3x2_t transform, rubraview_interpolation_t interpolation, double opacity);

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
    if (draw_from_copy(renderer, texture, transform, interpolation, opacity)) return;
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

/* RV-166 (D-199): on a software adapter, a page drawn the same way two frames running is drawn once into a
   bitmap the size of the target, at its own place with the same filter, and from then on the page's rectangle
   of that bitmap is copied 1:1 (Direct2D's high-quality cubic reduction of a 12-megapixel picture took some
   180 ms of CPU a frame on the VM). True when the copy drew it. */
static bool draw_from_copy(rubraview_renderer_t *renderer, const rubraview_texture_t *texture,
                           rubraview_mat3x2_t transform, rubraview_interpolation_t interpolation, double opacity) {
    rubraview_screencopy_key_t key = {
        .texture = texture, .content = texture->content,
        .a = transform.a, .b = transform.b, .c = transform.c, .d = transform.d, .e = transform.e, .f = transform.f,
        .src_w = (double)texture->width, .src_h = (double)texture->height,
        .filter = (int32_t)interpolation, .target_w = renderer->width, .target_h = renderer->height,
    };
    if (!renderer->drawing ||
        !rubraview_screencopy_eligible(&key, renderer->software, texture->d3d != NULL, renderer->hdr, opacity,
                                       interpolation != RUBRAVIEW_INTERP_NEAREST)) return false;
    int slot = -1;
    bool evict = false;
    rubraview_screencopy_action_t action = rubraview_screencopy_step(&renderer->screencopy, &key, &slot, &evict);
    if (evict && slot >= 0 && renderer->copies[slot]) {
        ID2D1Bitmap_Release((ID2D1Bitmap*)renderer->copies[slot]);
        renderer->copies[slot] = NULL;
    }
    if (action == RUBRAVIEW_SCREENCOPY_DIRECT || slot < 0) return false;

    if (action == RUBRAVIEW_SCREENCOPY_MAKE) {
        int32_t x = 0, y = 0, w = 0, h = 0;
        bool ok = rubraview_screencopy_bounds(&key, &x, &y, &w, &h);
        ID2D1Bitmap1 *copy = NULL;
        if (ok) {
            D2D1_BITMAP_PROPERTIES1 props = {
                .pixelFormat = { .format = DXGI_FORMAT_B8G8R8A8_UNORM, .alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED },
                .dpiX = 96.0f, .dpiY = 96.0f,
                .bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET,
                .colorContext = NULL,
            };
            /* The whole target, so the page is drawn at the very place it has on screen: a copy moved by whole
               pixels came out up to 2 levels off with high-quality cubic (T245). */
            D2D1_SIZE_U size = { .width = (UINT32)renderer->width, .height = (UINT32)renderer->height };
            /* mingw's ID2D1DeviceContext_CreateBitmap names a vtable member that does not exist. */
            ok = SUCCEEDED(renderer->target->lpVtbl->CreateBitmap(renderer->target, size, NULL, 0, &props, &copy)) && copy;
        }
        if (ok) {
            /* Drawn into the copy as it would be on screen. */
            struct ID2D1Image *screen = NULL;
            ID2D1DeviceContext_GetTarget(renderer->target, &screen);
            ID2D1DeviceContext_SetTarget(renderer->target, (struct ID2D1Image*)copy);
            D2D1_COLOR_F clear = { 0.0f, 0.0f, 0.0f, 0.0f };
            ID2D1RenderTarget_Clear((ID2D1RenderTarget*)renderer->target, &clear);
            rubraview_src_rect_t src = { 0.0, 0.0, (double)texture->width, (double)texture->height };
            draw_region_opacity(renderer, texture, src, transform, interpolation, 1.0f);
            ID2D1DeviceContext_SetTarget(renderer->target, screen);
            if (screen) IUnknown_Release((IUnknown*)screen);
            renderer->copies[slot] = copy;
        } else if (copy) {
            ID2D1Bitmap_Release((ID2D1Bitmap*)copy);
        }
        rubraview_screencopy_made(&renderer->screencopy, slot, ok, x, y, w, h);
        if (!ok) return false;
    }

    const rubraview_screencopy_slot_t *held = &renderer->screencopy.slots[slot];
    ID2D1Bitmap1 *copy = renderer->copies[slot];
    if (!copy) return false;
    ID2D1RenderTarget *rt = (ID2D1RenderTarget*)renderer->target;
    D2D1_MATRIX_3X2_F identity = to_d2d_matrix(rubraview_mat3x2_identity());
    ID2D1RenderTarget_SetTransform(rt, &identity);
    /* Only the page's own rectangle is copied, 1:1. */
    D2D1_RECT_F rect = { (FLOAT)held->x, (FLOAT)held->y, (FLOAT)(held->x + held->w), (FLOAT)(held->y + held->h) };
    renderer->target->lpVtbl->DrawBitmap(renderer->target, (ID2D1Bitmap*)copy, &rect, 1.0f,
                                         D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, &rect, NULL);
    return true;
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
    if (texture->owner) {   /* RV-166: a copy made from it goes with it */
        bool drop[RUBRAVIEW_SCREENCOPY_SLOTS];
        rubraview_screencopy_forget(&texture->owner->screencopy, texture, drop);
        free_copies(texture->owner, drop);
    }
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

static DWRITE_FONT_WEIGHT weight_of(int32_t weight) {
    return weight >= 100 && weight <= 950 ? (DWRITE_FONT_WEIGHT)weight : DWRITE_FONT_WEIGHT_NORMAL;
}

static void wide_family(WCHAR out[64], proven_u8str_view_t family) {
    out[0] = 0;
    if (family.size == 0 || family.size > 120) return;
    int n = MultiByteToWideChar(CP_UTF8, 0, (const char *)family.ptr, (int)family.size, out, 63);
    out[n > 0 && n < 64 ? n : 0] = 0;
}

void rubraview_pal_render_set_text_font(rubraview_renderer_t *renderer, proven_u8str_view_t family, int32_t weight, bool italic) {
    if (!renderer) return;
    wide_family(renderer->text_family, family);
    renderer->text_weight = weight_of(weight);
    renderer->text_style = italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
}

void rubraview_pal_render_set_text_family(rubraview_renderer_t *renderer, proven_u8str_view_t family) {
    rubraview_pal_render_set_text_font(renderer, family, 0, false);
}

void rubraview_pal_render_set_ui_font(rubraview_renderer_t *renderer, proven_u8str_view_t family, int32_t weight, bool italic) {
    if (!renderer) return;
    wide_family(renderer->ui_family, family);
    renderer->ui_weight = weight_of(weight);
    renderer->ui_style = italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
}

void rubraview_pal_render_set_mono_family(rubraview_renderer_t *renderer, proven_u8str_view_t family) {
    if (renderer) wide_family(renderer->mono_family, family);
}

void rubraview_pal_render_set_mono_pad(rubraview_renderer_t *renderer, double pad) {
    if (renderer) renderer->mono_pad = pad > 0.0 ? pad : 0.0;
}

/* The face draw_text and the measures use: the one set for now, else the program's own. */
static const WCHAR *face_family(const rubraview_renderer_t *r) {
    return r->text_family[0] ? r->text_family : r->ui_family[0] ? r->ui_family : L"Segoe UI";
}
static DWRITE_FONT_WEIGHT face_weight(const rubraview_renderer_t *r) {
    return r->text_family[0] ? (r->text_weight ? r->text_weight : DWRITE_FONT_WEIGHT_NORMAL) : (r->ui_weight ? r->ui_weight : DWRITE_FONT_WEIGHT_NORMAL);
}
static DWRITE_FONT_STYLE face_style(const rubraview_renderer_t *r) {
    return r->text_family[0] ? r->text_style : r->ui_style;
}

static int family_name_compare(const void *a, const void *b) {
    const rubraview_font_family_t *x = (const rubraview_font_family_t*)a, *y = (const rubraview_font_family_t*)b;
    size_t n = x->shown.size < y->shown.size ? x->shown.size : y->shown.size;
    for (size_t i = 0; i < n; ++i) {
        int cx = (unsigned char)x->shown.ptr[i], cy = (unsigned char)y->shown.ptr[i];
        if (cx >= 'A' && cx <= 'Z') cx += 32;
        if (cy >= 'A' && cy <= 'Z') cy += 32;
        if (cx != cy) return cx - cy;
    }
    return x->shown.size < y->shown.size ? -1 : x->shown.size > y->shown.size ? 1 : 0;
}

/* A localized name as UTF-8 in `arena`: the locale's, else English, else the first. */
static proven_u8str_view_t family_name(proven_arena_t *arena, IDWriteLocalizedStrings *names, const WCHAR *locale) {
    UINT32 index = 0, len = 0;
    BOOL exists = FALSE;
    if (locale && (FAILED(IDWriteLocalizedStrings_FindLocaleName(names, locale, &index, &exists)) || !exists)) index = UINT32_MAX;
    if (index == UINT32_MAX && (FAILED(IDWriteLocalizedStrings_FindLocaleName(names, L"en-us", &index, &exists)) || !exists)) index = 0;
    WCHAR wide[128];
    if (FAILED(IDWriteLocalizedStrings_GetStringLength(names, index, &len)) || len == 0 || len >= 128 ||
        FAILED(IDWriteLocalizedStrings_GetString(names, index, wide, 128))) return PROVEN_LIT("");
    char utf8[400];
    int n = WideCharToMultiByte(CP_UTF8, 0, wide, (int)len, utf8, (int)sizeof(utf8), NULL, NULL);
    if (n <= 0) return PROVEN_LIT("");
    proven_result_mem_mut_t m = proven_arena_alloc(arena, (size_t)n);
    if (!proven_is_ok(m.err)) return PROVEN_LIT("");
    memcpy(m.value.ptr, utf8, (size_t)n);
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)m.value.ptr, .size = (size_t)n };
}

size_t rubraview_pal_font_families(proven_arena_t *arena, rubraview_font_family_t **out) {
    *out = NULL;
    IDWriteFactory *dwrite = NULL;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &RUBRAVIEW_IID_IDWriteFactory, (IUnknown**)&dwrite)) || !dwrite) return 0;
    IDWriteFontCollection *fonts = NULL;
    size_t count = 0;
    if (SUCCEEDED(IDWriteFactory_GetSystemFontCollection(dwrite, &fonts, FALSE)) && fonts) {
        UINT32 total = IDWriteFontCollection_GetFontFamilyCount(fonts);
        proven_result_mem_mut_t m = proven_arena_alloc(arena, (size_t)(total ? total : 1) * sizeof(rubraview_font_family_t));
        WCHAR locale[LOCALE_NAME_MAX_LENGTH];
        bool has_locale = GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH) > 0;
        if (proven_is_ok(m.err)) {
            rubraview_font_family_t *list = (rubraview_font_family_t*)(void*)m.value.ptr;
            for (UINT32 i = 0; i < total; ++i) {
                IDWriteFontFamily *family = NULL;
                IDWriteLocalizedStrings *names = NULL;
                if (FAILED(IDWriteFontCollection_GetFontFamily(fonts, i, &family)) || !family) continue;
                if (SUCCEEDED(IDWriteFontFamily_GetFamilyNames(family, &names)) && names) {
                    proven_u8str_view_t name = family_name(arena, names, NULL);
                    proven_u8str_view_t shown = has_locale ? family_name(arena, names, locale) : name;
                    if (name.size > 0 && name.ptr[0] != '@') list[count++] = (rubraview_font_family_t){ .shown = shown.size ? shown : name, .name = name };
                    IDWriteLocalizedStrings_Release(names);
                }
                IDWriteFontFamily_Release(family);
            }
            qsort(list, count, sizeof(*list), family_name_compare);
            *out = list;
        }
        IDWriteFontCollection_Release(fonts);
    }
    IDWriteFactory_Release(dwrite);
    return count;
}

bool rubraview_pal_render_draw_text(rubraview_renderer_t *renderer,
                                    proven_u8str_view_t text,
                                    rubraview_pal_rect_t rect,
                                    double font_size,
                                    uint32_t argb,
                                    rubraview_text_align_t align) {
    if (!renderer || !renderer->target || !renderer->dwrite) return false;
    if (text.size == 0 || text.size > 4096) return false;
    if (font_size <= 0.0) font_size = 14.0;

    /* UTF-8 to UTF-16 at the API boundary only (§7.2.3). */
    WCHAR wide[4096];
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, (LPCCH)text.ptr, (int)text.size,
                                       wide, (int)(sizeof(wide) / sizeof(wide[0])));
    if (wide_len <= 0) return false;

    IDWriteTextFormat *format = NULL;
    HRESULT hr = IDWriteFactory_CreateTextFormat(renderer->dwrite, face_family(renderer), NULL,
                                                 face_weight(renderer),
                                                 face_style(renderer),
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

bool rubraview_pal_render_measure_text(rubraview_renderer_t *renderer, proven_u8str_view_t text, double font_size,
                                       double *out_width) {
    if (!renderer || !renderer->dwrite || !out_width) return false;
    *out_width = 0.0;
    if (text.size == 0) return true;
    if (text.size > 4096) return false;
    if (font_size <= 0.0) font_size = 14.0;
    WCHAR wide[4096];
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, (LPCCH)text.ptr, (int)text.size, wide, (int)(sizeof(wide) / sizeof(wide[0])));
    if (wide_len <= 0) return false;
    IDWriteTextFormat *format = NULL;
    if (FAILED(IDWriteFactory_CreateTextFormat(renderer->dwrite, face_family(renderer), NULL, face_weight(renderer),
                                               face_style(renderer), DWRITE_FONT_STRETCH_NORMAL,
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

bool rubraview_pal_render_measure_text_wrapped(rubraview_renderer_t *renderer, proven_u8str_view_t text, double font_size,
                                               double wrap_width, double *out_width, double *out_height) {
    if (!renderer || !renderer->dwrite || !out_width || !out_height) return false;
    *out_width = *out_height = 0.0;
    if (text.size == 0) return true;
    if (text.size > 4096) return false;
    if (font_size <= 0.0) font_size = 14.0;
    if (wrap_width < 1.0) wrap_width = 1.0;
    WCHAR wide[4096];
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, (LPCCH)text.ptr, (int)text.size, wide, (int)(sizeof(wide) / sizeof(wide[0])));
    if (wide_len <= 0) return false;
    /* The same face and wrapping draw_text draws with. */
    IDWriteTextFormat *format = NULL;
    if (FAILED(IDWriteFactory_CreateTextFormat(renderer->dwrite, face_family(renderer), NULL, face_weight(renderer),
                                               face_style(renderer), DWRITE_FONT_STRETCH_NORMAL,
                                               (FLOAT)font_size, L"", &format)) || !format) {
        return false;
    }
    IDWriteTextLayout *layout = NULL;
    bool ok = SUCCEEDED(IDWriteFactory_CreateTextLayout(renderer->dwrite, wide, (UINT32)wide_len, format,
                                                        (FLOAT)wrap_width, 100000.0f, &layout)) && layout;
    if (ok) {
        DWRITE_TEXT_METRICS metrics;
        ok = SUCCEEDED(IDWriteTextLayout_GetMetrics(layout, &metrics));
        if (ok) {
            *out_width = (double)metrics.width;
            *out_height = (double)metrics.height;
        }
        IDWriteTextLayout_Release(layout);
    }
    IDWriteTextFormat_Release(format);
    return ok;
}

/* ---- RFC-0005: a text file's paragraphs ---- */

struct rubraview_text_block {
    IDWriteTextLayout *layout;
    uint32_t *u8_of;     /* UTF-16 index -> UTF-8 offset, wide_len + 1 of them */
    UINT32 wide_len;
    size_t utf8_len;
    double width, height;
};

static UINT32 block_u16(const rubraview_text_block_t *b, size_t u8) {
    UINT32 lo = 0, hi = b->wide_len;
    while (lo < hi) {
        UINT32 mid = (lo + hi) / 2;
        if (b->u8_of[mid] < u8) lo = mid + 1; else hi = mid;
    }
    return lo;
}

rubraview_text_block_t *rubraview_pal_text_block_make(rubraview_renderer_t *renderer, proven_u8str_view_t text,
                                                      const rubraview_text_style_t *style) {
    if (!renderer || !renderer->dwrite || !style || text.size > 0x7FFFFFF0u) return NULL;
    rubraview_text_block_t *b = (rubraview_text_block_t *)calloc(1, sizeof(*b));
    if (!b) return NULL;
    WCHAR *wide = (WCHAR *)malloc((text.size + 1) * sizeof(WCHAR));
    b->u8_of = (uint32_t *)malloc((text.size + 2) * sizeof(uint32_t));
    if (!wide || !b->u8_of) { free(wide); free(b->u8_of); free(b); return NULL; }
    /* UTF-8 to UTF-16 by hand, keeping where each unit came from. */
    const uint8_t *s = (const uint8_t *)text.ptr;
    UINT32 w = 0;
    for (size_t i = 0; i < text.size;) {
        uint8_t c = s[i];
        int n = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        if (i + (size_t)n > text.size) n = 1;
        uint32_t cp = n == 1 ? c : c & (n == 2 ? 0x1F : n == 3 ? 0x0F : 0x07);
        for (int k = 1; k < n; ++k) cp = (cp << 6) | (s[i + (size_t)k] & 0x3F);
        if (cp >= 0x10000) {
            cp -= 0x10000;
            b->u8_of[w] = (uint32_t)i; wide[w++] = (WCHAR)(0xD800 + (cp >> 10));
            b->u8_of[w] = (uint32_t)i; wide[w++] = (WCHAR)(0xDC00 + (cp & 0x3FF));
        } else {
            b->u8_of[w] = (uint32_t)i; wide[w++] = (WCHAR)cp;
        }
        i += (size_t)n;
    }
    b->u8_of[w] = (uint32_t)text.size;
    b->wide_len = w;
    b->utf8_len = text.size;

    WCHAR family[64];
    int fn = style->family.size > 0 ? MultiByteToWideChar(CP_UTF8, 0, (LPCCH)style->family.ptr, (int)style->family.size, family, 63) : 0;
    family[fn > 0 ? fn : 0] = 0;
    double size = style->size > 1.0 ? style->size : 16.0;
    IDWriteTextFormat *format = NULL;
    if (FAILED(IDWriteFactory_CreateTextFormat(renderer->dwrite, family[0] ? family : renderer->ui_family[0] ? renderer->ui_family : L"Segoe UI", NULL,
                                               style->weight >= 100 ? weight_of(style->weight) : family[0] ? DWRITE_FONT_WEIGHT_NORMAL : face_weight(renderer),
                                               style->italic ? DWRITE_FONT_STYLE_ITALIC : family[0] ? DWRITE_FONT_STYLE_NORMAL : renderer->ui_style,
                                               DWRITE_FONT_STRETCH_NORMAL, (FLOAT)size, L"", &format)) || !format) {
        free(wide); free(b->u8_of); free(b);
        return NULL;
    }
    double spacing = size * (style->line_spacing > 0.5 ? style->line_spacing : 1.5);
    IDWriteTextFormat_SetLineSpacing(format, DWRITE_LINE_SPACING_METHOD_UNIFORM, (FLOAT)spacing, (FLOAT)(spacing * 0.5 + size * 0.4));
    IDWriteTextFormat_SetWordWrapping(format, style->wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    double width = style->wrap && style->width > 8.0 ? style->width : 1.0e6;
    /* An empty paragraph still takes a line. */
    static const WCHAR space[] = L" ";
    HRESULT hr = IDWriteFactory_CreateTextLayout(renderer->dwrite, w > 0 ? wide : space, w > 0 ? w : 1, format,
                                                 (FLOAT)width, 1.0e7f, &b->layout);
    IDWriteTextFormat_Release(format);
    free(wide);
    if (FAILED(hr) || !b->layout) { free(b->u8_of); free(b); return NULL; }
    DWRITE_TEXT_METRICS m;
    if (SUCCEEDED(IDWriteTextLayout_GetMetrics(b->layout, &m))) {
        b->width = (double)m.widthIncludingTrailingWhitespace;
        b->height = (double)m.height;
    }
    return b;
}

void rubraview_pal_text_block_free(rubraview_text_block_t *block) {
    if (!block) return;
    if (block->layout) IDWriteTextLayout_Release(block->layout);
    free(block->u8_of);
    free(block);
}

void rubraview_pal_text_block_size(const rubraview_text_block_t *block, double *width, double *height) {
    if (width) *width = block ? block->width : 0.0;
    if (height) *height = block ? block->height : 0.0;
}

void rubraview_pal_text_block_draw(rubraview_renderer_t *renderer, const rubraview_text_block_t *block,
                                   double x, double y, uint32_t argb,
                                   const rubraview_text_mark_t *marks, size_t mark_count) {
    if (!renderer || !renderer->target || !block || !block->layout) return;
    ID2D1RenderTarget *rt = (ID2D1RenderTarget*)renderer->target;
    set_identity(rt);
    for (size_t k = 0; k < mark_count; ++k) {
        if (marks[k].len == 0 || marks[k].start >= block->utf8_len) continue;
        UINT32 a = block_u16(block, marks[k].start), e = block_u16(block, marks[k].start + marks[k].len);
        if (e <= a) continue;
        DWRITE_HIT_TEST_METRICS hits[64];
        UINT32 count = 0;
        if (FAILED(IDWriteTextLayout_HitTestTextRange(block->layout, a, e - a, (FLOAT)x, (FLOAT)y, hits, 64, &count))) continue;
        ID2D1SolidColorBrush *mb = NULL;
        D2D1_COLOR_F mc = argb_to_color(marks[k].argb);
        if (FAILED(ID2D1RenderTarget_CreateSolidColorBrush(rt, &mc, NULL, &mb)) || !mb) continue;
        for (UINT32 h = 0; h < count && h < 64; ++h) {
            D2D1_RECT_F r = { hits[h].left, hits[h].top, hits[h].left + hits[h].width, hits[h].top + hits[h].height };
            ID2D1RenderTarget_FillRectangle(rt, &r, (ID2D1Brush*)mb);
        }
        ID2D1SolidColorBrush_Release(mb);
    }
    ID2D1SolidColorBrush *brush = NULL;
    D2D1_COLOR_F color = argb_to_color(argb);
    if (FAILED(ID2D1RenderTarget_CreateSolidColorBrush(rt, &color, NULL, &brush)) || !brush) return;
    D2D1_POINT_2F origin = { (FLOAT)x, (FLOAT)y };
    ID2D1RenderTarget_DrawTextLayout(rt, origin, (IDWriteTextLayout*)block->layout, (ID2D1Brush*)brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
    ID2D1SolidColorBrush_Release(brush);
}

void rubraview_pal_text_block_draw_upto(rubraview_renderer_t *renderer, const rubraview_text_block_t *block,
                                        double x, double y, double height, uint32_t argb,
                                        const rubraview_text_mark_t *marks, size_t mark_count) {
    if (!renderer || !renderer->target || !block || !block->layout) return;
    ID2D1RenderTarget *rt = (ID2D1RenderTarget*)renderer->target;
    set_identity(rt);
    D2D1_RECT_F clip = { (FLOAT)-1.0e6, (FLOAT)y, (FLOAT)1.0e6, (FLOAT)(y + height) };
    ID2D1RenderTarget_PushAxisAlignedClip(rt, &clip, D2D1_ANTIALIAS_MODE_ALIASED);
    rubraview_pal_text_block_draw(renderer, block, x, y, argb, marks, mark_count);
    ID2D1RenderTarget_PopAxisAlignedClip(rt);
}

bool rubraview_pal_text_block_last_line(const rubraview_text_block_t *block, size_t *start, double *top) {
    if (!block || !block->layout || block->wide_len == 0) return false;
    UINT32 lines = 0;
    IDWriteTextLayout_GetLineMetrics(block->layout, NULL, 0, &lines);
    if (lines == 0) return false;
    DWRITE_LINE_METRICS *m = (DWRITE_LINE_METRICS*)malloc(lines * sizeof(*m));
    if (!m) return false;
    bool ok = SUCCEEDED(IDWriteTextLayout_GetLineMetrics(block->layout, m, lines, &lines)) && lines > 0;
    if (ok) {
        UINT32 at = 0;
        FLOAT y = 0.0f;
        for (UINT32 i = 0; i + 1 < lines; ++i) { at += m[i].length; y += m[i].height; }
        if (at > block->wide_len) at = block->wide_len;
        if (start) *start = block->u8_of[at];
        if (top) *top = (double)y;
    }
    free(m);
    return ok;
}

size_t rubraview_pal_text_block_hit(const rubraview_text_block_t *block, double x, double y) {
    if (!block || !block->layout || block->wide_len == 0) return 0;
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS m;
    if (FAILED(IDWriteTextLayout_HitTestPoint(block->layout, (FLOAT)x, (FLOAT)y, &trailing, &inside, &m))) return 0;
    UINT32 at = m.textPosition + (trailing ? m.length : 0);
    if (at > block->wide_len) at = block->wide_len;
    return block->u8_of[at];
}

bool rubraview_pal_text_block_line_of(const rubraview_text_block_t *block, size_t at, double *top, double *height) {
    if (!block || !block->layout) return false;
    FLOAT px = 0, py = 0;
    DWRITE_HIT_TEST_METRICS m;
    UINT32 u = block_u16(block, at);
    if (u >= block->wide_len && block->wide_len > 0) u = block->wide_len - 1;
    if (FAILED(IDWriteTextLayout_HitTestTextPosition(block->layout, u, FALSE, &px, &py, &m))) return false;
    if (top) *top = (double)m.top;
    if (height) *height = (double)m.height;
    return true;
}

/* ---- fixed-width text for the settings window (§3.22, D-13) ---- */

/* Consolas ships with every Windows since Vista. A character it has no
   glyph for — Hangul, most of CJK — is drawn by DirectWrite's own font
   fallback; the grid counts those as two cells, which is close to what
   the fallback face draws. */
#define MONO_FAMILY L"Consolas"

static IDWriteTextFormat *mono_format(rubraview_renderer_t *renderer, double font_size) {
    IDWriteTextFormat *format = NULL;
    if (FAILED(IDWriteFactory_CreateTextFormat(renderer->dwrite, renderer->mono_family[0] ? renderer->mono_family : MONO_FAMILY, NULL,
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
    /* A row as tall as the tallest script it may hold: Korean, Japanese,
       Chinese and Persian come from fallback faces taller than Consolas,
       and a row measured on Latin alone let them run into the next one and
       off the window's bottom (owner, 2026-10-18). */
    static const WCHAR mixed[] = L"Mg\uAC00\u3042\u6F22\u067E\u06AF";
    IDWriteTextLayout *tall = NULL;
    if (ok && out_height && SUCCEEDED(IDWriteFactory_CreateTextLayout(renderer->dwrite, mixed, (UINT32)(sizeof(mixed) / sizeof(mixed[0]) - 1),
                                                                      format, 10000.0f, 1000.0f, &tall)) && tall) {
        DWRITE_TEXT_METRICS metrics;
        if (SUCCEEDED(IDWriteTextLayout_GetMetrics(tall, &metrics)) && (double)metrics.height > *out_height) *out_height = (double)metrics.height;
        IDWriteTextLayout_Release(tall);
    }
    IDWriteTextFormat_Release(format);
    return ok;
}

bool rubraview_pal_render_draw_text_mono(rubraview_renderer_t *renderer, proven_u8str_view_t text,
                                         double x, double y, double font_size, uint32_t argb) {
    if (!renderer || !renderer->target || !renderer->dwrite) return false;
    if (text.size == 0 || text.size > 4096 || font_size <= 0.0) return false;

    WCHAR wide[4096];
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, (LPCCH)text.ptr, (int)text.size,
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
    y += renderer->mono_pad;   /* 1.25.0: the line spacing, the text in the middle of its taller row */
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

static uint16_t u8_to_half(uint8_t v);

bool rubraview_pal_texture_upload_bgra(rubraview_texture_t *texture, const uint8_t *pixels, int32_t stride) {
    if (!texture || !texture->bitmap || !pixels || stride <= 0) return false;
    if (texture->owner) texture->content = ++texture->owner->content_serial;   /* RV-166: no copy of what it held */
    if (texture->fp16) {   /* RV-153: a picture the film's shader did not draw, into its FP16 texture */
        size_t row = (size_t)texture->width * 8u;
        uint16_t *half = (uint16_t*)rubraview_malloc_product(row, (size_t)texture->height, 1);
        if (!half) return false;
        for (int32_t y = 0; y < texture->height; ++y) {
            const uint8_t *in = pixels + (size_t)y * (size_t)stride;
            uint16_t *out = half + (size_t)y * (size_t)texture->width * 4u;
            for (int32_t x = 0; x < texture->width; ++x) {
                out[x * 4] = u8_to_half(in[x * 4 + 2]);
                out[x * 4 + 1] = u8_to_half(in[x * 4 + 1]);
                out[x * 4 + 2] = u8_to_half(in[x * 4]);
                out[x * 4 + 3] = 0x3C00u;   /* 1.0 */
            }
        }
        bool ok = SUCCEEDED(ID2D1Bitmap_CopyFromMemory(texture->bitmap, NULL, half, (UINT32)row));
        free(half);
        return ok;
    }
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

bool rubraview_pal_render_facts(rubraview_renderer_t *renderer, rubraview_render_facts_t *out) {
    if (!out) return false;
    *out = (rubraview_render_facts_t){ .warp = g_used_warp, .feature_level = (uint32_t)g_feature_level, .buffers = 2, .vsync = true };
    if (!renderer || !renderer->d3d) return false;
    out->screen_copies = renderer->software;
    out->present_model = renderer->present_model;
    IDXGIDevice *dxgi = NULL;
    IDXGIAdapter *adapter = NULL;
    if (SUCCEEDED(ID3D11Device_QueryInterface(renderer->d3d, &RV_IID_IDXGIDevice, (void**)&dxgi)) && dxgi) {
        IDXGIDevice_GetAdapter(dxgi, &adapter);
        IDXGIDevice_Release(dxgi);
    }
    if (adapter) {
        DXGI_ADAPTER_DESC desc;
        if (SUCCEEDED(IDXGIAdapter_GetDesc(adapter, &desc))) {
            int n = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, out->adapter, (int)sizeof(out->adapter), NULL, NULL);
            if (n <= 0) out->adapter[0] = '\0';
            out->video_memory = (uint64_t)desc.DedicatedVideoMemory;
        }
        IDXGIAdapter_Release(adapter);
    }
    out->decode_device = renderer->decode_d3d != NULL;
    out->hdr_output = renderer->hdr;
    out->hdr_sdr_white = renderer->hdr_sdr_white;
    if (renderer->decode_d3d) {
        ID3D11VideoDevice *video = NULL;
        if (SUCCEEDED(ID3D11Device_QueryInterface(renderer->decode_d3d, &RV_IID_ID3D11VideoDevice, (void**)&video)) && video) {
            out->decoder_profiles = ID3D11VideoDevice_GetVideoDecoderProfileCount(video);
            ID3D11VideoDevice_Release(video);
        }
    }
    return true;
}

/* ---- RV-153 (D-183): HDR output ---- */

static const GUID RV_IID_IDXGISwapChain3 =
    { 0x94d99bdb, 0xf1f8, 0x4ab0, { 0xb2, 0x36, 0x7d, 0xa0, 0x17, 0x0e, 0xda, 0xb1 } };
static const GUID RV_IID_IDXGIOutput6 =
    { 0x068346e8, 0xaaec, 0x4b84, { 0xad, 0xd7, 0x13, 0x7f, 0x51, 0x3f, 0x77, 0xa1 } };
static const GUID RV_IID_IDXGIFactory1 =
    { 0x770aae78, 0xf26f, 0x4dba, { 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87 } };

/* Where Windows shows SDR's white on the screen named `gdi_name` (\\.\DISPLAYn); 0 when it will not say. */
static double sdr_white_of(const WCHAR *gdi_name) {
    UINT32 paths = 0, modes = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &paths, &modes) != ERROR_SUCCESS || paths == 0) return 0.0;
    DISPLAYCONFIG_PATH_INFO *path = (DISPLAYCONFIG_PATH_INFO*)calloc(paths, sizeof(*path));
    DISPLAYCONFIG_MODE_INFO *mode = (DISPLAYCONFIG_MODE_INFO*)calloc(modes ? modes : 1, sizeof(*mode));
    double nits = 0.0;
    if (path && mode && QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &paths, path, &modes, mode, NULL) == ERROR_SUCCESS) {
        for (UINT32 i = 0; i < paths && nits == 0.0; ++i) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source = { .header = {
                .type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, .size = sizeof(source),
                .adapterId = path[i].sourceInfo.adapterId, .id = path[i].sourceInfo.id } };
            if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS || wcscmp(source.viewGdiDeviceName, gdi_name) != 0) continue;
            DISPLAYCONFIG_SDR_WHITE_LEVEL white = { .header = {
                .type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL, .size = sizeof(white),
                .adapterId = path[i].targetInfo.adapterId, .id = path[i].targetInfo.id } };
            if (DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS && white.SDRWhiteLevel > 0)
                nits = (double)white.SDRWhiteLevel / 1000.0 * 80.0;
        }
    }
    free(path);
    free(mode);
    return nits;
}

bool rubraview_pal_render_hdr_display(rubraview_renderer_t *renderer, rubraview_hdr_display_t *out) {
    if (!out) return false;
    *out = (rubraview_hdr_display_t){ .sdr_white_nits = 80.0 };
    if (!renderer || !renderer->hwnd) return false;
    HMONITOR monitor = MonitorFromWindow(renderer->hwnd, MONITOR_DEFAULTTONEAREST);
    IDXGIFactory1 *factory = NULL;
    if (FAILED(CreateDXGIFactory1(&RV_IID_IDXGIFactory1, (void**)&factory)) || !factory) return false;
    bool found = false;
    /* every card's outputs: on a laptop with two cards the screen may hang off the other one */
    IDXGIAdapter1 *adapter = NULL;
    for (UINT a = 0; !found && IDXGIFactory1_EnumAdapters1(factory, a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
        IDXGIOutput *output = NULL;
        for (UINT o = 0; !found && IDXGIAdapter1_EnumOutputs(adapter, o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
            IDXGIOutput6 *six = NULL;
            DXGI_OUTPUT_DESC1 desc;
            if (SUCCEEDED(IDXGIOutput_QueryInterface(output, &RV_IID_IDXGIOutput6, (void**)&six)) && six) {
                if (SUCCEEDED(IDXGIOutput6_GetDesc1(six, &desc)) && desc.Monitor == monitor) {
                    found = true;
                    out->hdr = desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
                    double peak = (double)desc.MaxLuminance;
                    out->peak_nits = peak > 0.0 ? (peak < 100.0 ? 100.0 : peak > 10000.0 ? 10000.0 : peak) : 0.0;
                    double white = sdr_white_of(desc.DeviceName);
                    if (white > 0.0) out->sdr_white_nits = white;
                }
                IDXGIOutput6_Release(six);
            }
            IDXGIOutput_Release(output);
        }
        IDXGIAdapter1_Release(adapter);
    }
    IDXGIFactory1_Release(factory);
    return found;
}

bool rubraview_pal_render_set_hdr(rubraview_renderer_t *r, bool on, double sdr_white_nits, const char **why) {
    const char *dummy;
    if (!why) why = &dummy;
    *why = NULL;
    if (!r || !r->swap_chain || !r->target || r->drawing) { *why = N_("no window to draw"); return false; }
    if (!(sdr_white_nits > 0.0)) sdr_white_nits = 80.0;
    if (on == r->hdr && (!on || r->hdr_sdr_white == sdr_white_nits)) return true;
    if (on && r->hdr) { r->hdr_sdr_white = sdr_white_nits; return true; }
    IDXGISwapChain3 *three = NULL;
    if (FAILED(IDXGISwapChain1_QueryInterface(r->swap_chain, &RV_IID_IDXGISwapChain3, (void**)&three)) || !three) {
        *why = N_("this Windows cannot set the window's colour space");
        return false;
    }
    bool ok = false;
    if (on) {
        UINT support = 0;
        if (r->present_model == RUBRAVIEW_PRESENT_COPY) *why = N_("the window is shown through the copy model, which has no HDR");
        else if (!r->hdr_present && !(r->hdr_present = rubraview_pal_hdr_present_create(r->d3d, why))) {}
        else {
            release_back_buffer(r);
            r->hdr = true;
            r->hdr_sdr_white = sdr_white_nits;
            /* the colour space is asked of the chain once it is FP16: a BGRA chain refuses scRGB */
            if (FAILED(IDXGISwapChain1_ResizeBuffers(r->swap_chain, 0, (UINT)r->width, (UINT)r->height, DXGI_FORMAT_R16G16B16A16_FLOAT, 0)))
                *why = N_("the window could not be made FP16");
            else if (FAILED(IDXGISwapChain3_CheckColorSpaceSupport(three, DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &support)) ||
                     !(support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) ||
                     FAILED(IDXGISwapChain3_SetColorSpace1(three, DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709)))
                *why = N_("the swap chain refuses scRGB");
            else if (!bind_back_buffer(r))
                *why = N_("the window could not be made FP16");
            else
                ok = true;
        }
    }
    if (!ok && (r->hdr || !on)) {   /* off, or back from a half-made HDR chain */
        release_back_buffer(r);
        r->hdr = false;
        bool back = SUCCEEDED(IDXGISwapChain1_ResizeBuffers(r->swap_chain, 0, (UINT)r->width, (UINT)r->height, DXGI_FORMAT_B8G8R8A8_UNORM, 0));
        IDXGISwapChain3_SetColorSpace1(three, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
        back = back && bind_back_buffer(r);
        if (!on) ok = back;
    }
    IDXGISwapChain3_Release(three);
    if (!ok && !*why) *why = N_("the window could not be made FP16");
    r->hdr_why = ok ? NULL : *why;
    return ok;
}

static rubraview_texture_t *create_video(rubraview_renderer_t *renderer, int32_t width, int32_t height, bool fp16) {
    if (!renderer || !renderer->d3d || !renderer->target || width <= 0 || height <= 0) return NULL;
    DXGI_FORMAT format = fp16 ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
    D3D11_TEXTURE2D_DESC desc = {
        .Width = (UINT)width, .Height = (UINT)height, .MipLevels = 1, .ArraySize = 1,
        .Format = format, .SampleDesc = { .Count = 1, .Quality = 0 },
        .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
    };
    ID3D11Texture2D *d3d = NULL;
    if (FAILED(ID3D11Device_CreateTexture2D(renderer->d3d, &desc, NULL, &d3d)) || !d3d) return NULL;
    IDXGISurface *surface = NULL;
    ID2D1Bitmap1 *bitmap = NULL;
    if (SUCCEEDED(ID3D11Texture2D_QueryInterface(d3d, &RV_IID_IDXGISurface, (void**)&surface)) && surface) {
        D2D1_BITMAP_PROPERTIES1 props = {
            .pixelFormat = { .format = format, .alphaMode = fp16 ? D2D1_ALPHA_MODE_PREMULTIPLIED : D2D1_ALPHA_MODE_IGNORE },
            .dpiX = 96.0f, .dpiY = 96.0f, .bitmapOptions = D2D1_BITMAP_OPTIONS_NONE,
        };
        if (FAILED(ID2D1DeviceContext_CreateBitmapFromDxgiSurface(renderer->target, surface, &props, &bitmap))) bitmap = NULL;
        IDXGISurface_Release(surface);
    }
    if (!bitmap) { ID3D11Texture2D_Release(d3d); return NULL; }
    rubraview_texture_t *texture = rubraview_d2d_texture_wrap(renderer, (ID2D1Bitmap*)bitmap, width, height);
    if (!texture) { ID2D1Bitmap_Release((ID2D1Bitmap*)bitmap); ID3D11Texture2D_Release(d3d); return NULL; }
    texture->d3d = d3d;
    texture->fp16 = fp16;
    return texture;
}

rubraview_texture_t *rubraview_pal_texture_create_video(rubraview_renderer_t *renderer, int32_t width, int32_t height) {
    return create_video(renderer, width, height, false);
}

rubraview_texture_t *rubraview_pal_texture_create_video_hdr(rubraview_renderer_t *renderer, int32_t width, int32_t height) {
    return create_video(renderer, width, height, true);
}

/* RV-153: FP16's halves to and from the 8-bit values they stand for (clipped at 1). */
static uint8_t half_to_u8(uint16_t h) {
    uint32_t e = (h >> 10) & 0x1Fu, m = h & 0x3FFu;
    if (h & 0x8000u) return 0;
    double v = e == 0 ? (double)m / 16777216.0 : e == 31 ? 1.0 : ldexp(1.0 + (double)m / 1024.0, (int)e - 15);
    return v >= 1.0 ? 255 : (uint8_t)(v * 255.0 + 0.5);
}

static uint16_t u8_to_half(uint8_t v) {
    if (v == 0) return 0;
    double f = (double)v / 255.0;
    int e = 0;
    double m = frexp(f, &e);   /* f = m * 2^e, m in [0.5, 1) */
    int exponent = e - 1 + 15;
    uint32_t mant = (uint32_t)((m * 2.0 - 1.0) * 1024.0 + 0.5);
    if (mant == 1024) { mant = 0; exponent++; }
    return (uint16_t)(((uint32_t)exponent << 10) | mant);
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
    texture->content = ++texture->owner->content_serial;   /* RV-166 */
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

bool rubraview_pal_texture_read_video(rubraview_texture_t *texture, uint8_t *dst, size_t stride) {
    if (!texture || !texture->d3d || !texture->owner || !dst || stride < (size_t)texture->width * 4u) return false;
    ID3D11Device *device = texture->owner->d3d;
    D3D11_TEXTURE2D_DESC desc = {
        .Width = (UINT)texture->width, .Height = (UINT)texture->height, .MipLevels = 1, .ArraySize = 1,
        .Format = texture->fp16 ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM, .SampleDesc = { .Count = 1, .Quality = 0 },
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
            for (int32_t y = 0; y < texture->height; ++y) {
                const uint8_t *row = (const uint8_t*)mapped.pData + (size_t)y * mapped.RowPitch;
                if (texture->fp16) {   /* RV-153: clipped at the SDR white */
                    const uint16_t *h = (const uint16_t*)(const void*)row;
                    uint8_t *out = dst + (size_t)y * stride;
                    for (int32_t x = 0; x < texture->width; ++x) {
                        out[x * 4] = half_to_u8(h[x * 4 + 2]);
                        out[x * 4 + 1] = half_to_u8(h[x * 4 + 1]);
                        out[x * 4 + 2] = half_to_u8(h[x * 4]);
                        out[x * 4 + 3] = 255;
                    }
                } else {
                    memcpy(dst + (size_t)y * stride, row, (size_t)texture->width * 4u);
                }
            }
            ID3D11DeviceContext_Unmap(context, (ID3D11Resource*)staging, 0);
            ok = true;
        }
        ID3D11DeviceContext_Release(context);
    }
    ID3D11Texture2D_Release(staging);
    return ok;
}

bool rubraview_pal_texture_video_brightness(rubraview_texture_t *texture, double *out_brightness) {
    if (!texture || !texture->d3d || !texture->owner || !out_brightness || texture->fp16) return false;
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
