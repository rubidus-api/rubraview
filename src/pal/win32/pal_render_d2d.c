#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <d2d1.h>
#include <string.h>
#include "rubraview/pal/pal_render.h"
#include "rubraview/pal/pal_render_d2d_internal.h"

struct rubraview_texture {
    ID2D1Bitmap *bitmap;
    int32_t width, height;
    struct rubraview_renderer *owner;    /* so destroy can recycle without a global */
    struct rubraview_texture *next_free; /* recycled struct free list */
};

struct rubraview_renderer {
    proven_arena_t *arena;
    ID2D1Factory *factory;
    ID2D1HwndRenderTarget *target;
    HWND hwnd;
    int32_t width, height;
    bool drawing;
    struct rubraview_texture *free_textures;
};

/* ---- internal accessors shared with the WIC image backend ---- */

ID2D1RenderTarget *rubraview_d2d_render_target(rubraview_renderer_t *renderer) {
    return renderer ? (ID2D1RenderTarget*)renderer->target : NULL;
}

rubraview_texture_t *rubraview_d2d_texture_wrap(rubraview_renderer_t *renderer, ID2D1Bitmap *bitmap) {
    if (!renderer || !bitmap) return NULL;

    struct rubraview_texture *tex = renderer->free_textures;
    if (tex) {
        renderer->free_textures = tex->next_free;
    } else {
        proven_result_mem_mut_t res = proven_arena_alloc(renderer->arena, sizeof(struct rubraview_texture));
        if (!proven_is_ok(res.err)) return NULL;
        tex = (struct rubraview_texture*)(void*)res.value.ptr;
    }

    D2D1_SIZE_U size = ID2D1Bitmap_GetPixelSize(bitmap);
    tex->bitmap = bitmap;
    tex->width = (int32_t)size.width;
    tex->height = (int32_t)size.height;
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

static bool create_target(struct rubraview_renderer *r) {
    D2D1_RENDER_TARGET_PROPERTIES props = {
        .type = D2D1_RENDER_TARGET_TYPE_DEFAULT,
        .pixelFormat = { .format = DXGI_FORMAT_B8G8R8A8_UNORM, .alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED },
        .dpiX = 0.0f, .dpiY = 0.0f, /* 0 means "use the desktop DPI"; we work in physical pixels below */
        .usage = D2D1_RENDER_TARGET_USAGE_NONE,
        .minLevel = D2D1_FEATURE_LEVEL_DEFAULT,
    };
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_props = {
        .hwnd = r->hwnd,
        .pixelSize = { (UINT32)r->width, (UINT32)r->height },
        .presentOptions = D2D1_PRESENT_OPTIONS_NONE,
    };

    HRESULT hr = ID2D1Factory_CreateHwndRenderTarget(r->factory, &props, &hwnd_props, &r->target);
    if (FAILED(hr) || !r->target) return false;

    /* Draw in physical pixels: the viewport transform (RV-022) already
       accounts for DPI, so Direct2D must not scale a second time. */
    ID2D1RenderTarget_SetDpi((ID2D1RenderTarget*)r->target, 96.0f, 96.0f);
    return true;
}

rubraview_renderer_t *rubraview_pal_render_create(proven_arena_t *arena, void *native_window_handle, int32_t width, int32_t height) {
    if (!arena || !native_window_handle || width <= 0 || height <= 0) return NULL;

    proven_result_mem_mut_t res = proven_arena_alloc(arena, sizeof(struct rubraview_renderer));
    if (!proven_is_ok(res.err)) return NULL;
    struct rubraview_renderer *r = (struct rubraview_renderer*)(void*)res.value.ptr;
    memset(r, 0, sizeof(*r));

    r->arena = arena;
    r->hwnd = (HWND)native_window_handle;
    r->width = width;
    r->height = height;

    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory, NULL, (void**)&r->factory);
    if (FAILED(hr) || !r->factory) return NULL;

    if (!create_target(r)) {
        ID2D1Factory_Release(r->factory);
        return NULL;
    }

    return r;
}

void rubraview_pal_render_destroy(rubraview_renderer_t *renderer) {
    if (!renderer) return;
    if (renderer->target) {
        ID2D1HwndRenderTarget_Release(renderer->target);
        renderer->target = NULL;
    }
    if (renderer->factory) {
        ID2D1Factory_Release(renderer->factory);
        renderer->factory = NULL;
    }
}

bool rubraview_pal_render_resize(rubraview_renderer_t *renderer, int32_t width, int32_t height) {
    if (!renderer || !renderer->target || width <= 0 || height <= 0) return false;
    renderer->width = width;
    renderer->height = height;
    D2D1_SIZE_U size = { (UINT32)width, (UINT32)height };
    HRESULT hr = ID2D1HwndRenderTarget_Resize(renderer->target, &size);
    return SUCCEEDED(hr);
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

    if (hr == (HRESULT)D2DERR_RECREATE_TARGET) {
        /* Device lost: drop the target so the caller can rebuild it and
           reload its textures (every ID2D1Bitmap died with it). */
        ID2D1HwndRenderTarget_Release(renderer->target);
        renderer->target = NULL;
        renderer->free_textures = NULL;
        if (create_target(renderer)) {
            return false; /* recreated, but the caller must reload textures */
        }
        return false;
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
 * ID2D1HwndRenderTarget (Direct2D 1.0) offers only nearest-neighbour and
 * linear bitmap interpolation. Cubic modes need an ID2D1DeviceContext
 * (Direct2D 1.1), which arrives with the effect graph in M6 (RV-064);
 * until then CUBIC and HIGH_QUALITY_CUBIC render as linear. Nearest —
 * the mode pixel art actually depends on (§3.5) — is exact.
 */
static D2D1_BITMAP_INTERPOLATION_MODE to_d2d_interpolation(rubraview_interpolation_t interp) {
    return (interp == RUBRAVIEW_INTERP_NEAREST)
        ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
        : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
}

void rubraview_pal_render_draw_texture(rubraview_renderer_t *renderer,
                                       const rubraview_texture_t *texture,
                                       rubraview_mat3x2_t transform,
                                       rubraview_interpolation_t interpolation) {
    if (!renderer || !renderer->target || !texture || !texture->bitmap) return;

    rubraview_src_rect_t src = { 0.0, 0.0, (double)texture->width, (double)texture->height };
    rubraview_pal_render_draw_texture_region(renderer, texture, src, transform, interpolation);
}

void rubraview_pal_render_draw_texture_region(rubraview_renderer_t *renderer,
                                              const rubraview_texture_t *texture,
                                              rubraview_src_rect_t src,
                                              rubraview_mat3x2_t transform,
                                              rubraview_interpolation_t interpolation) {
    if (!renderer || !renderer->target || !texture || !texture->bitmap) return;

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

    ID2D1RenderTarget_DrawBitmap(rt, texture->bitmap, &dest, 1.0f,
                                 to_d2d_interpolation(interpolation), &source);

    D2D1_MATRIX_3X2_F identity = to_d2d_matrix(rubraview_mat3x2_identity());
    ID2D1RenderTarget_SetTransform(rt, &identity);
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
    if (texture->owner) {
        texture_recycle(texture->owner, texture);
    }
}

#endif /* _WIN32 */
