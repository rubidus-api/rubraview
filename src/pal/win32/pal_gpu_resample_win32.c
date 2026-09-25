/*
 * D-38: bicubic and Lanczos-3 resizing on the graphics card, for export and
 * batch runs. The card runs the CPU's own two passes — horizontal into a
 * float intermediate, then vertical — with weight tables the CPU computed
 * (rubraview_resample_axis_weights), so the shaders only multiply and add,
 * in the CPU's order, `precise` against fused multiply-adds. Anything that
 * does not work — no card, no shader compiler, a picture too large for a
 * buffer, a failed call — returns false and the CPU resizes instead.
 *
 * The shader compiler is Windows' own d3dcompiler_47.dll, loaded at run
 * time from System32 (present on Windows 10 and 11); the build host has no
 * offline HLSL compiler. The device is this module's own, made once, and
 * every call holds one lock: resizing can come from job threads.
 */
#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3dcommon.h>
#include <dxgi.h>
#include <stdlib.h>
#include <string.h>

#include "rubraview/pal/pal_gpu_resample.h"
#include "rubraview/resample.h"

#define GPU_INTER_MAX_BYTES (32u << 20)    /* the float intermediate, per band */
#define GPU_SOURCE_MAX_BYTES (256u << 20)  /* a source larger than this stays on the CPU */
#define GPU_MIN_PIXELS_ON 1000000ull      /* below a megapixel the round trip costs more */

typedef HRESULT (WINAPI *d3dcompile_fn)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *,
                                        LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);

static CRITICAL_SECTION g_lock;
static bool g_lock_ready;
static ID3D11Device *g_device;
static ID3D11DeviceContext *g_context;
static ID3D11ComputeShader *g_hpass, *g_vpass;
static ID3D11Buffer *g_params;
static char g_describe[96] = "not started";

static const char SHADER[] =
    "cbuffer P : register(b0) { uint sw; uint dw; uint taps; uint lo; uint y0; uint rows; uint src_rows; uint pad; };\n"
    "ByteAddressBuffer Src : register(t0);\n"
    "StructuredBuffer<int> Xi : register(t1);\n"
    "StructuredBuffer<float> Xw : register(t2);\n"
    "StructuredBuffer<int> Yi : register(t3);\n"
    "StructuredBuffer<float> Yw : register(t4);\n"
    "StructuredBuffer<float4> InterIn : register(t5);\n"
    "RWStructuredBuffer<float4> Inter : register(u0);\n"
    "RWByteAddressBuffer Out : register(u1);\n"
    "[numthreads(16, 16, 1)]\n"
    "void hpass(uint3 id : SV_DispatchThreadID) {\n"
    "  if (id.x >= dw || id.y >= src_rows) return;\n"
    "  uint y = lo + id.y;\n"
    "  precise float4 acc = float4(0, 0, 0, 0);\n"
    "  for (uint j = 0; j < taps; ++j) {\n"
    "    uint p = Src.Load((y * sw + (uint)Xi[id.x * taps + j]) * 4);\n"
    "    precise float4 c = float4(p & 255, (p >> 8) & 255, (p >> 16) & 255, p >> 24);\n"
    "    precise float w = Xw[id.x * taps + j];\n"
    "    acc = acc + c * w;\n"
    "  }\n"
    "  Inter[id.y * dw + id.x] = acc;\n"
    "}\n"
    "[numthreads(16, 16, 1)]\n"
    "void vpass(uint3 id : SV_DispatchThreadID) {\n"
    "  if (id.x >= dw || id.y >= rows) return;\n"
    "  uint y = y0 + id.y;\n"
    "  precise float4 v = float4(0, 0, 0, 0);\n"
    "  for (uint i = 0; i < taps; ++i) {\n"
    "    precise float w = Yw[y * taps + i];\n"
    "    v = v + InterIn[((uint)Yi[y * taps + i] - lo) * dw + id.x] * w;\n"
    "  }\n"
    "  int4 q = clamp((int4)(v + 0.5), 0, 255);\n"
    "  Out.Store((id.y * dw + id.x) * 4, (uint)q.x | ((uint)q.y << 8) | ((uint)q.z << 16) | ((uint)q.w << 24));\n"
    "}\n";

static void release_all(void) {
    if (g_hpass) { ID3D11ComputeShader_Release(g_hpass); g_hpass = NULL; }
    if (g_vpass) { ID3D11ComputeShader_Release(g_vpass); g_vpass = NULL; }
    if (g_params) { ID3D11Buffer_Release(g_params); g_params = NULL; }
    if (g_context) { ID3D11DeviceContext_Release(g_context); g_context = NULL; }
    if (g_device) { ID3D11Device_Release(g_device); g_device = NULL; }
}

static ID3D11ComputeShader *compile(d3dcompile_fn fn, const char *entry) {
    ID3DBlob *code = NULL, *errors = NULL;
    /* 1 << 13 is D3DCOMPILE_IEEE_STRICTNESS: keep the CPU's arithmetic. */
    HRESULT hr = fn(SHADER, sizeof(SHADER) - 1, "rubraview_resample", NULL, NULL, entry, "cs_5_0", 1u << 13, 0,
                    &code, &errors);
    if (errors) ID3D10Blob_Release(errors);
    if (FAILED(hr) || !code) return NULL;
    ID3D11ComputeShader *shader = NULL;
    if (FAILED(ID3D11Device_CreateComputeShader(g_device, ID3D10Blob_GetBufferPointer(code),
                                                ID3D10Blob_GetBufferSize(code), NULL, &shader))) {
        shader = NULL;
    }
    ID3D10Blob_Release(code);
    return shader;
}

static ID3D11Buffer *make_buffer(UINT bytes, UINT bind, UINT misc, UINT stride, D3D11_USAGE usage, UINT cpu,
                                 const void *data) {
    D3D11_BUFFER_DESC desc = { .ByteWidth = bytes, .Usage = usage, .BindFlags = bind, .CPUAccessFlags = cpu,
                               .MiscFlags = misc, .StructureByteStride = stride };
    D3D11_SUBRESOURCE_DATA init = { .pSysMem = data };
    ID3D11Buffer *buffer = NULL;
    if (FAILED(ID3D11Device_CreateBuffer(g_device, &desc, data ? &init : NULL, &buffer))) return NULL;
    return buffer;
}

static ID3D11ShaderResourceView *make_srv(ID3D11Buffer *buffer, UINT elements, bool raw) {
    D3D11_SHADER_RESOURCE_VIEW_DESC desc = { .Format = raw ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN,
                                             .ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX };
    desc.BufferEx.FirstElement = 0;
    desc.BufferEx.NumElements = elements;
    desc.BufferEx.Flags = raw ? D3D11_BUFFEREX_SRV_FLAG_RAW : 0;
    ID3D11ShaderResourceView *view = NULL;
    if (FAILED(ID3D11Device_CreateShaderResourceView(g_device, (ID3D11Resource*)buffer, &desc, &view))) return NULL;
    return view;
}

static ID3D11UnorderedAccessView *make_uav(ID3D11Buffer *buffer, UINT elements, bool raw) {
    D3D11_UNORDERED_ACCESS_VIEW_DESC desc = { .Format = raw ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN,
                                              .ViewDimension = D3D11_UAV_DIMENSION_BUFFER };
    desc.Buffer.FirstElement = 0;
    desc.Buffer.NumElements = elements;
    desc.Buffer.Flags = raw ? D3D11_BUFFER_UAV_FLAG_RAW : 0;
    ID3D11UnorderedAccessView *view = NULL;
    if (FAILED(ID3D11Device_CreateUnorderedAccessView(g_device, (ID3D11Resource*)buffer, &desc, &view))) return NULL;
    return view;
}

#define SAFE_RELEASE(p, fn) do { if (p) { fn(p); (p) = NULL; } } while (0)

typedef struct { UINT sw, dw, taps, lo, y0, rows, src_rows, pad; } params_t;

/* One resize, the lock held. */
static bool resize_locked(const rubraview_pixbuf_t *src, rubraview_pixbuf_t *dst, rubraview_resample_filter_t filter) {
    int32_t taps = rubraview_resample_taps(filter);
    if (taps == 0 || !g_device) return false;
    uint64_t src_bytes = (uint64_t)src->width * (uint64_t)src->height * 4u;
    if (src_bytes == 0 || src_bytes > GPU_SOURCE_MAX_BYTES) return false;
    size_t dw = (size_t)dst->width, dh = (size_t)dst->height, sw = (size_t)src->width, sh = (size_t)src->height;

    bool ok = false;
    int32_t *xi = malloc(sizeof(int32_t) * dw * (size_t)taps), *yi = malloc(sizeof(int32_t) * dh * (size_t)taps);
    float *xw = malloc(sizeof(float) * dw * (size_t)taps), *yw = malloc(sizeof(float) * dh * (size_t)taps);
    uint32_t *packed = malloc((size_t)src_bytes);
    ID3D11Buffer *b_src = NULL, *b_xi = NULL, *b_xw = NULL, *b_yi = NULL, *b_yw = NULL, *b_inter = NULL, *b_out = NULL, *b_read = NULL;
    ID3D11ShaderResourceView *v_src = NULL, *v_xi = NULL, *v_xw = NULL, *v_yi = NULL, *v_yw = NULL, *v_inter_in = NULL;
    ID3D11UnorderedAccessView *u_inter = NULL, *u_out = NULL;
    if (!xi || !yi || !xw || !yw || !packed) goto done;
    if (!rubraview_resample_axis_weights(filter, src->width, dst->width, xi, xw) ||
        !rubraview_resample_axis_weights(filter, src->height, dst->height, yi, yw)) goto done;
    for (size_t y = 0; y < sh; ++y) memcpy(packed + y * sw, src->pixels + (ptrdiff_t)y * src->stride, sw * 4u);

    /* Bands of destination rows small enough that the source rows they
       need fit the intermediate. */
    size_t max_src_rows = GPU_INTER_MAX_BYTES / (dw * 16u);
    if (max_src_rows < (size_t)taps + 2) goto done;
    size_t band = 256;
    for (;;) {
        size_t worst = 0;
        for (size_t y0 = 0; y0 < dh; y0 += band) {
            size_t y1 = y0 + band < dh ? y0 + band : dh;
            int32_t lo = yi[y0 * (size_t)taps], hi = yi[(y1 - 1) * (size_t)taps + (size_t)taps - 1];
            for (size_t k = y0 * (size_t)taps; k < y1 * (size_t)taps; ++k) { if (yi[k] < lo) lo = yi[k]; if (yi[k] > hi) hi = yi[k]; }
            if ((size_t)(hi - lo + 1) > worst) worst = (size_t)(hi - lo + 1);
        }
        if (worst <= max_src_rows) break;
        if (band == 1) goto done;
        band /= 2;
    }

    b_src = make_buffer((UINT)src_bytes, D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0,
                        D3D11_USAGE_IMMUTABLE, 0, packed);
    b_xi = make_buffer((UINT)(dw * taps * 4), D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4, D3D11_USAGE_IMMUTABLE, 0, xi);
    b_xw = make_buffer((UINT)(dw * taps * 4), D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4, D3D11_USAGE_IMMUTABLE, 0, xw);
    b_yi = make_buffer((UINT)(dh * taps * 4), D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4, D3D11_USAGE_IMMUTABLE, 0, yi);
    b_yw = make_buffer((UINT)(dh * taps * 4), D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4, D3D11_USAGE_IMMUTABLE, 0, yw);
    b_inter = make_buffer((UINT)(dw * max_src_rows * 16), D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
                          D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 16, D3D11_USAGE_DEFAULT, 0, NULL);
    b_out = make_buffer((UINT)(dw * band * 4), D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0,
                        D3D11_USAGE_DEFAULT, 0, NULL);
    b_read = make_buffer((UINT)(dw * band * 4), 0, 0, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ, NULL);
    if (!b_src || !b_xi || !b_xw || !b_yi || !b_yw || !b_inter || !b_out || !b_read) goto done;
    v_src = make_srv(b_src, (UINT)(src_bytes / 4), true);
    v_xi = make_srv(b_xi, (UINT)(dw * taps), false);
    v_xw = make_srv(b_xw, (UINT)(dw * taps), false);
    v_yi = make_srv(b_yi, (UINT)(dh * taps), false);
    v_yw = make_srv(b_yw, (UINT)(dh * taps), false);
    v_inter_in = make_srv(b_inter, (UINT)(dw * max_src_rows), false);
    u_inter = make_uav(b_inter, (UINT)(dw * max_src_rows), false);
    u_out = make_uav(b_out, (UINT)(dw * band), true);
    if (!v_src || !v_xi || !v_xw || !v_yi || !v_yw || !v_inter_in || !u_inter || !u_out) goto done;

    ok = true;
    for (size_t y0 = 0; y0 < dh && ok; y0 += band) {
        size_t y1 = y0 + band < dh ? y0 + band : dh;
        int32_t lo = yi[y0 * (size_t)taps], hi = lo;
        for (size_t k = y0 * (size_t)taps; k < y1 * (size_t)taps; ++k) { if (yi[k] < lo) lo = yi[k]; if (yi[k] > hi) hi = yi[k]; }
        params_t p = { (UINT)sw, (UINT)dw, (UINT)taps, (UINT)lo, (UINT)y0, (UINT)(y1 - y0), (UINT)(hi - lo + 1), 0 };
        ID3D11DeviceContext_UpdateSubresource(g_context, (ID3D11Resource*)g_params, 0, NULL, &p, 0, 0);
        ID3D11DeviceContext_CSSetConstantBuffers(g_context, 0, 1, &g_params);

        /* horizontal: source rows lo..hi into the intermediate */
        ID3D11ShaderResourceView *h_srv[3] = { v_src, v_xi, v_xw };
        ID3D11DeviceContext_CSSetShaderResources(g_context, 0, 3, h_srv);
        ID3D11DeviceContext_CSSetUnorderedAccessViews(g_context, 0, 1, &u_inter, NULL);
        ID3D11DeviceContext_CSSetShader(g_context, g_hpass, NULL, 0);
        ID3D11DeviceContext_Dispatch(g_context, (UINT)((dw + 15) / 16), (UINT)((p.src_rows + 15) / 16), 1);
        ID3D11UnorderedAccessView *none_uav = NULL;
        ID3D11DeviceContext_CSSetUnorderedAccessViews(g_context, 0, 1, &none_uav, NULL);

        /* vertical: the band's rows out of it */
        ID3D11ShaderResourceView *v_srv[3] = { v_yi, v_yw, v_inter_in };
        ID3D11DeviceContext_CSSetShaderResources(g_context, 3, 3, v_srv);
        ID3D11DeviceContext_CSSetUnorderedAccessViews(g_context, 1, 1, &u_out, NULL);
        ID3D11DeviceContext_CSSetShader(g_context, g_vpass, NULL, 0);
        ID3D11DeviceContext_Dispatch(g_context, (UINT)((dw + 15) / 16), (UINT)((p.rows + 15) / 16), 1);
        ID3D11DeviceContext_CSSetUnorderedAccessViews(g_context, 1, 1, &none_uav, NULL);
        ID3D11ShaderResourceView *none_srv[6] = { NULL, NULL, NULL, NULL, NULL, NULL };
        ID3D11DeviceContext_CSSetShaderResources(g_context, 0, 6, none_srv);

        ID3D11DeviceContext_CopyResource(g_context, (ID3D11Resource*)b_read, (ID3D11Resource*)b_out);
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(ID3D11DeviceContext_Map(g_context, (ID3D11Resource*)b_read, 0, D3D11_MAP_READ, 0, &mapped))) {
            ok = false;
            break;
        }
        for (size_t y = y0; y < y1; ++y) {
            memcpy(dst->pixels + (ptrdiff_t)y * dst->stride, (const uint8_t*)mapped.pData + (y - y0) * dw * 4u, dw * 4u);
        }
        ID3D11DeviceContext_Unmap(g_context, (ID3D11Resource*)b_read, 0);
    }
    /* A card that was reset in the middle gives no picture: the CPU does it. */
    if (ok && ID3D11Device_GetDeviceRemovedReason(g_device) != S_OK) ok = false;

done:
    SAFE_RELEASE(u_out, ID3D11UnorderedAccessView_Release);
    SAFE_RELEASE(u_inter, ID3D11UnorderedAccessView_Release);
    SAFE_RELEASE(v_inter_in, ID3D11ShaderResourceView_Release);
    SAFE_RELEASE(v_yw, ID3D11ShaderResourceView_Release);
    SAFE_RELEASE(v_yi, ID3D11ShaderResourceView_Release);
    SAFE_RELEASE(v_xw, ID3D11ShaderResourceView_Release);
    SAFE_RELEASE(v_xi, ID3D11ShaderResourceView_Release);
    SAFE_RELEASE(v_src, ID3D11ShaderResourceView_Release);
    SAFE_RELEASE(b_read, ID3D11Buffer_Release);
    SAFE_RELEASE(b_out, ID3D11Buffer_Release);
    SAFE_RELEASE(b_inter, ID3D11Buffer_Release);
    SAFE_RELEASE(b_yw, ID3D11Buffer_Release);
    SAFE_RELEASE(b_yi, ID3D11Buffer_Release);
    SAFE_RELEASE(b_xw, ID3D11Buffer_Release);
    SAFE_RELEASE(b_xi, ID3D11Buffer_Release);
    SAFE_RELEASE(b_src, ID3D11Buffer_Release);
    free(xi); free(yi); free(xw); free(yw); free(packed);
    if (!ok && g_device && ID3D11Device_GetDeviceRemovedReason(g_device) != S_OK) {
        release_all();   /* a lost card: no more tries this run */
        strcpy(g_describe, "lost (the CPU resizes)");
        rubraview_resample_set_accel(NULL, NULL, 0);
    }
    return ok;
}

static bool accel(void *context, const rubraview_pixbuf_t *src, rubraview_pixbuf_t *dst, rubraview_resample_filter_t filter) {
    (void)context;
    if (!g_lock_ready || src->format != dst->format) return false;
    EnterCriticalSection(&g_lock);
    bool ok = resize_locked(src, dst, filter);
    LeaveCriticalSection(&g_lock);
    return ok;
}

bool rubraview_pal_gpu_resample_start(rubraview_gpu_resize_mode_t mode) {
    if (!g_lock_ready) { InitializeCriticalSection(&g_lock); g_lock_ready = true; }
    EnterCriticalSection(&g_lock);
    rubraview_resample_set_accel(NULL, NULL, 0);
    release_all();
    bool started = false;
    if (mode == RUBRAVIEW_GPU_RESIZE_OFF) {
        strcpy(g_describe, "off");
        goto out;
    }
    HMODULE compiler = LoadLibraryExW(L"d3dcompiler_47.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    d3dcompile_fn compile_fn = compiler ? (d3dcompile_fn)(void*)GetProcAddress(compiler, "D3DCompile") : NULL;
    if (!compile_fn) { strcpy(g_describe, "no shader compiler (d3dcompiler_47.dll)"); goto out; }

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    const char *kind = "hardware";
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, levels, 2, D3D11_SDK_VERSION,
                                   &g_device, NULL, &g_context);
    if (FAILED(hr) && mode == RUBRAVIEW_GPU_RESIZE_ALWAYS) {
        /* for testing: Windows' software rasteriser runs the same shaders */
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, levels, 2, D3D11_SDK_VERSION,
                               &g_device, NULL, &g_context);
        kind = "WARP (software)";
    }
    if (FAILED(hr) || !g_device) { g_device = NULL; g_context = NULL; strcpy(g_describe, "no card with compute shaders"); goto out; }
    /* Without a card, Windows still offers a "hardware" device: the Basic
       Render Driver, which runs on the CPU and is slower than resizing
       there directly (VM, 2026-09-25). `on` leaves it alone. */
    {
        IDXGIDevice *dxgi = NULL;
        IDXGIAdapter *adapter = NULL;
        DXGI_ADAPTER_DESC desc;
        bool basic = false;
        if (SUCCEEDED(ID3D11Device_QueryInterface(g_device, &IID_IDXGIDevice, (void**)&dxgi)) && dxgi &&
            SUCCEEDED(IDXGIDevice_GetAdapter(dxgi, &adapter)) && adapter &&
            SUCCEEDED(IDXGIAdapter_GetDesc(adapter, &desc))) {
            basic = desc.VendorId == 0x1414 && desc.DeviceId == 0x8C;
        }
        if (adapter) IDXGIAdapter_Release(adapter);
        if (dxgi) IDXGIDevice_Release(dxgi);
        if (basic) {
            if (mode != RUBRAVIEW_GPU_RESIZE_ALWAYS) {
                release_all();
                strcpy(g_describe, "no graphics card (Basic Render Driver): the CPU resizes");
                goto out;
            }
            kind = "Basic Render Driver (software)";
        }
    }
    g_hpass = compile(compile_fn, "hpass");
    g_vpass = compile(compile_fn, "vpass");
    g_params = make_buffer(sizeof(params_t), D3D11_BIND_CONSTANT_BUFFER, 0, 0, D3D11_USAGE_DEFAULT, 0, NULL);
    if (!g_hpass || !g_vpass || !g_params) { release_all(); strcpy(g_describe, "the shaders would not build"); goto out; }
    rubraview_resample_set_accel(accel, NULL, mode == RUBRAVIEW_GPU_RESIZE_ALWAYS ? 0 : GPU_MIN_PIXELS_ON);
    strcpy(g_describe, kind);
    started = true;
out:
    LeaveCriticalSection(&g_lock);
    return started;
}

void rubraview_pal_gpu_resample_stop(void) {
    if (!g_lock_ready) return;
    EnterCriticalSection(&g_lock);
    rubraview_resample_set_accel(NULL, NULL, 0);
    release_all();
    strcpy(g_describe, "stopped");
    LeaveCriticalSection(&g_lock);
}

const char *rubraview_pal_gpu_resample_describe(void) {
    return g_describe;
}

#endif /* _WIN32 */
