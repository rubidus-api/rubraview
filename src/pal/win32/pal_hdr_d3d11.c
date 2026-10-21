#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3d11_4.h>   /* ID3D11Multithread */
#include <d3dcommon.h>
#include <stdlib.h>
#include <string.h>
#include "rubraview/pal/pal_hdr.h"
#include "rubraview/lang.h"

/*
 * RV-150 (D-180): HDR to SDR on the graphics card. The same steps as
 * tonemap.c, in the same order, so the host suite's numbers are the ones
 * the VM checks the screen against: the codes to Y'CbCr, BT.2020's matrix,
 * PQ or HLG (with its 1000-nit OOTF) to nits, BT.2390's EETF on the
 * brightest channel, BT.2020 to BT.709, sRGB's curve.
 *
 * The shader is compiled at run time by Windows' d3dcompiler_47.dll, as
 * the resize shaders are (D-55); it runs on WARP as well, which is how the
 * VM sees it.
 */

typedef HRESULT (WINAPI *d3dcompile_fn)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *,
                                        LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);

static const GUID HDR_IID_ID3D11Multithread =
    { 0x9b7e4e00, 0x342c, 0x4106, { 0xa1, 0x9f, 0x4f, 0x27, 0x04, 0xf6, 0x89, 0xf0 } };

static const char SHADER[] =
    "cbuffer tone : register(b0) { float4 a; float4 b; };\n"
    /* a: sample to code, transfer, full range, largest code; b: peak, white, 2^(bits-8), 2^(bits-1) */
    "Texture2D<float> plane_y : register(t0);\n"
    "Texture2D<float> plane_u : register(t1);\n"
    "Texture2D<float> plane_v : register(t2);\n"
    "SamplerState linear_clamp : register(s0);\n"
    "struct vs_out { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "vs_out vs(uint id : SV_VertexID) {\n"
    "    vs_out o;\n"
    "    float2 t = float2((id << 1) & 2, id & 2);\n"
    "    o.uv = t;\n"
    "    o.pos = float4(t.x * 2.0 - 1.0, 1.0 - t.y * 2.0, 0.0, 1.0);\n"
    "    return o;\n"
    "}\n"
    "static const float M1 = 2610.0 / 16384.0, M2 = 2523.0 / 4096.0 * 128.0;\n"
    "static const float C1 = 3424.0 / 4096.0, C2 = 2413.0 / 4096.0 * 32.0, C3 = 2392.0 / 4096.0 * 32.0;\n"
    "static const float HA = 0.17883277, HB = 0.28466892, HC = 0.55991073;\n"
    "float pq_to_nits(float s) {\n"
    "    float e = pow(saturate(s), 1.0 / M2);\n"
    "    return 10000.0 * pow(max(e - C1, 0.0) / (C2 - C3 * e), 1.0 / M1);\n"
    "}\n"
    "float nits_to_pq(float n) {\n"
    "    float y = pow(max(n, 0.0) / 10000.0, M1);\n"
    "    return pow((C1 + C2 * y) / (1.0 + C3 * y), M2);\n"
    "}\n"
    "float hlg_to_scene(float s) {\n"
    "    s = saturate(s);\n"
    "    return s <= 0.5 ? s * s / 3.0 : (exp((s - HC) / HA) + HB) / 12.0;\n"
    "}\n"
    "float bt2390(float n, float source_peak, float target_peak) {\n"
    "    if (n <= 0.0) return 0.0;\n"
    "    if (!(source_peak > target_peak)) return min(n, target_peak);\n"
    "    float spq = nits_to_pq(source_peak);\n"
    "    float e1 = min(nits_to_pq(n) / spq, 1.0);\n"
    "    float ml = nits_to_pq(target_peak) / spq;\n"
    "    float ks = max(1.5 * ml - 0.5, 0.0);\n"
    "    float e2 = e1;\n"
    "    if (e1 > ks) {\n"
    "        float t = (e1 - ks) / (1.0 - ks), t2 = t * t, t3 = t2 * t;\n"
    "        e2 = (2.0 * t3 - 3.0 * t2 + 1.0) * ks + (t3 - 2.0 * t2 + t) * (1.0 - ks) + (-2.0 * t3 + 3.0 * t2) * ml;\n"
    "    }\n"
    "    return pq_to_nits(e2 * spq);\n"
    "}\n"
    "float srgb(float v) {\n"
    "    v = saturate(v);\n"
    "    return v <= 0.0031308 ? 12.92 * v : 1.055 * pow(v, 1.0 / 2.4) - 0.055;\n"
    "}\n"
    "float4 ps(vs_out i) : SV_Target {\n"
    "    float yc = plane_y.Sample(linear_clamp, i.uv) * a.x;\n"
    "    float cbc = plane_u.Sample(linear_clamp, i.uv) * a.x;\n"
    "    float crc = plane_v.Sample(linear_clamp, i.uv) * a.x;\n"
    "    float yv, cbv, crv;\n"
    "    if (a.z > 0.5) { yv = yc / a.w; cbv = (cbc - b.w) / a.w; crv = (crc - b.w) / a.w; }\n"
    "    else { yv = (yc - 16.0 * b.z) / (219.0 * b.z); cbv = (cbc - 128.0 * b.z) / (224.0 * b.z); crv = (crc - 128.0 * b.z) / (224.0 * b.z); }\n"
    "    float r = saturate(yv + 1.4746 * crv);\n"
    "    float bl = saturate(yv + 1.8814 * cbv);\n"
    "    float g = saturate((yv - 0.2627 * (yv + 1.4746 * crv) - 0.0593 * (yv + 1.8814 * cbv)) / 0.6780);\n"
    "    float3 l;\n"
    "    float peak = b.x;\n"
    "    if (a.y > 1.5) {\n"
    "        float3 s = float3(hlg_to_scene(r), hlg_to_scene(g), hlg_to_scene(bl));\n"
    "        float ys = 0.2627 * s.x + 0.6780 * s.y + 0.0593 * s.z;\n"
    "        l = ys > 0.0 ? 1000.0 * pow(ys, 0.2) * s : float3(0.0, 0.0, 0.0);\n"
    "        peak = 1000.0;\n"
    "    } else {\n"
    "        l = float3(pq_to_nits(r), pq_to_nits(g), pq_to_nits(bl));\n"
    "    }\n"
    "    float m = max(l.x, max(l.y, l.z));\n"
    "    l *= m > 0.0 ? bt2390(m, peak, b.y) / m / b.y : 0.0;\n"
    "    float3 o = float3( 1.6605 * l.x - 0.5876 * l.y - 0.0728 * l.z,\n"
    "                      -0.1246 * l.x + 1.1329 * l.y - 0.0083 * l.z,\n"
    "                      -0.0182 * l.x - 0.1006 * l.y + 1.1187 * l.z);\n"
    "    return float4(srgb(o.x), srgb(o.y), srgb(o.z), 1.0);\n"
    "}\n";

#define TARGET_VIEWS 8   /* a film's slots, with room */

struct rubraview_hdr_pass {
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    ID3D11Multithread *lock;
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    ID3D11Buffer *params;
    ID3D11SamplerState *sampler;
    ID3D11Texture2D *planes[3];
    ID3D11ShaderResourceView *views[3];
    int32_t sizes[3][2];
    struct { ID3D11Texture2D *texture; ID3D11RenderTargetView *view; } targets[TARGET_VIEWS];
    int target_count, target_next;
};

static ID3DBlob *compile(d3dcompile_fn fn, const char *entry, const char *profile) {
    ID3DBlob *code = NULL, *errors = NULL;
    HRESULT hr = fn(SHADER, sizeof(SHADER) - 1, "rubraview_hdr", NULL, NULL, entry, profile, 0, 0, &code, &errors);
    if (errors) ID3D10Blob_Release(errors);
    return SUCCEEDED(hr) ? code : NULL;
}

void rubraview_pal_hdr_pass_free(rubraview_hdr_pass_t *p) {
    if (!p) return;
    for (int i = 0; i < p->target_count; ++i) if (p->targets[i].view) ID3D11RenderTargetView_Release(p->targets[i].view);
    for (int i = 0; i < 3; ++i) {
        if (p->views[i]) ID3D11ShaderResourceView_Release(p->views[i]);
        if (p->planes[i]) ID3D11Texture2D_Release(p->planes[i]);
    }
    if (p->sampler) ID3D11SamplerState_Release(p->sampler);
    if (p->params) ID3D11Buffer_Release(p->params);
    if (p->ps) ID3D11PixelShader_Release(p->ps);
    if (p->vs) ID3D11VertexShader_Release(p->vs);
    if (p->lock) ID3D11Multithread_Release(p->lock);
    if (p->context) ID3D11DeviceContext_Release(p->context);
    if (p->device) ID3D11Device_Release(p->device);
    free(p);
}

rubraview_hdr_pass_t *rubraview_pal_hdr_pass_create(void *d3d11_device, const char **why) {
    static d3dcompile_fn compile_fn;
    static bool tried;
    const char *dummy;
    if (!why) why = &dummy;
    *why = NULL;
    if (!d3d11_device) { *why = N_("no Direct3D 11 device for tone mapping was made"); return NULL; }
    if (!tried) {
        tried = true;
        HMODULE compiler = LoadLibraryExW(L"d3dcompiler_47.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        compile_fn = compiler ? (d3dcompile_fn)(void*)GetProcAddress(compiler, "D3DCompile") : NULL;
    }
    if (!compile_fn) { *why = N_("no shader compiler (d3dcompiler_47.dll)"); return NULL; }

    rubraview_hdr_pass_t *p = (rubraview_hdr_pass_t*)calloc(1, sizeof(*p));
    if (!p) { *why = N_("out of memory"); return NULL; }
    p->device = (ID3D11Device*)d3d11_device;
    ID3D11Device_AddRef(p->device);
    ID3D11Device_GetImmediateContext(p->device, &p->context);
    if (p->context && FAILED(ID3D11DeviceContext_QueryInterface(p->context, &HDR_IID_ID3D11Multithread, (void**)&p->lock)))
        p->lock = NULL;

    ID3DBlob *vs = compile(compile_fn, "vs", "vs_4_0"), *ps = compile(compile_fn, "ps", "ps_4_0");
    if (vs && FAILED(ID3D11Device_CreateVertexShader(p->device, ID3D10Blob_GetBufferPointer(vs), ID3D10Blob_GetBufferSize(vs), NULL, &p->vs)))
        p->vs = NULL;
    if (ps && FAILED(ID3D11Device_CreatePixelShader(p->device, ID3D10Blob_GetBufferPointer(ps), ID3D10Blob_GetBufferSize(ps), NULL, &p->ps)))
        p->ps = NULL;
    if (vs) ID3D10Blob_Release(vs);
    if (ps) ID3D10Blob_Release(ps);

    D3D11_BUFFER_DESC cb = { .ByteWidth = 32, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER };
    if (FAILED(ID3D11Device_CreateBuffer(p->device, &cb, NULL, &p->params))) p->params = NULL;
    D3D11_SAMPLER_DESC sd = {
        .Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
        .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
        .ComparisonFunc = D3D11_COMPARISON_NEVER, .MaxLOD = D3D11_FLOAT32_MAX,
    };
    if (FAILED(ID3D11Device_CreateSamplerState(p->device, &sd, &p->sampler))) p->sampler = NULL;

    if (!p->context || !p->vs || !p->ps || !p->params || !p->sampler) {
        *why = N_("the tone mapping shader would not build");
        rubraview_pal_hdr_pass_free(p);
        return NULL;
    }
    return p;
}

static bool plane_ready(rubraview_hdr_pass_t *p, int i, int32_t w, int32_t h) {
    if (p->planes[i] && p->sizes[i][0] == w && p->sizes[i][1] == h) return true;
    if (p->views[i]) { ID3D11ShaderResourceView_Release(p->views[i]); p->views[i] = NULL; }
    if (p->planes[i]) { ID3D11Texture2D_Release(p->planes[i]); p->planes[i] = NULL; }
    D3D11_TEXTURE2D_DESC desc = {
        .Width = (UINT)w, .Height = (UINT)h, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R16_UNORM,
        .SampleDesc = { .Count = 1, .Quality = 0 }, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE,
    };
    if (FAILED(ID3D11Device_CreateTexture2D(p->device, &desc, NULL, &p->planes[i]))) { p->planes[i] = NULL; return false; }
    if (FAILED(ID3D11Device_CreateShaderResourceView(p->device, (ID3D11Resource*)p->planes[i], NULL, &p->views[i]))) {
        p->views[i] = NULL;
        return false;
    }
    p->sizes[i][0] = w;
    p->sizes[i][1] = h;
    return true;
}

static ID3D11RenderTargetView *target_view(rubraview_hdr_pass_t *p, ID3D11Texture2D *texture) {
    for (int i = 0; i < p->target_count; ++i) if (p->targets[i].texture == texture) return p->targets[i].view;
    ID3D11RenderTargetView *view = NULL;
    if (FAILED(ID3D11Device_CreateRenderTargetView(p->device, (ID3D11Resource*)texture, NULL, &view))) return NULL;
    int slot = p->target_count < TARGET_VIEWS ? p->target_count++ : p->target_next++ % TARGET_VIEWS;
    if (p->targets[slot].view) ID3D11RenderTargetView_Release(p->targets[slot].view);
    p->targets[slot].texture = texture;
    p->targets[slot].view = view;
    return view;
}

bool rubraview_pal_hdr_pass_run(rubraview_hdr_pass_t *p, const rubraview_hdr_planes_t *planes,
                                const rubraview_tonemap_t *tone, void *target) {
    if (!p || !planes || !tone || !target || planes->width <= 0 || planes->height <= 0 ||
        planes->chroma_width <= 0 || planes->chroma_height <= 0) return false;
    int bits = tone->bits >= 8 && tone->bits <= 16 ? tone->bits : 10;
    float params[8] = {
        65535.0f, (float)tone->transfer, tone->full_range ? 1.0f : 0.0f, (float)((1 << bits) - 1),
        (float)tone->peak_nits, (float)(tone->white_nits > 0.0 ? tone->white_nits : RUBRAVIEW_SDR_WHITE_NITS),
        (float)(1 << (bits - 8)), (float)(1 << (bits - 1)),
    };

    if (p->lock) ID3D11Multithread_Enter(p->lock);   /* the context is shared with the decoders' threads */
    bool ok = plane_ready(p, 0, planes->width, planes->height) &&
              plane_ready(p, 1, planes->chroma_width, planes->chroma_height) &&
              plane_ready(p, 2, planes->chroma_width, planes->chroma_height);
    ID3D11RenderTargetView *rtv = ok ? target_view(p, (ID3D11Texture2D*)target) : NULL;
    if (rtv) {
        for (int i = 0; i < 3; ++i)
            ID3D11DeviceContext_UpdateSubresource(p->context, (ID3D11Resource*)p->planes[i], 0, NULL,
                                                  planes->data[i], (UINT)planes->stride[i], 0);
        ID3D11DeviceContext_UpdateSubresource(p->context, (ID3D11Resource*)p->params, 0, NULL, params, 0, 0);
        D3D11_VIEWPORT viewport = { 0.0f, 0.0f, (float)planes->width, (float)planes->height, 0.0f, 1.0f };
        ID3D11DeviceContext_IASetInputLayout(p->context, NULL);
        ID3D11DeviceContext_IASetPrimitiveTopology(p->context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11DeviceContext_VSSetShader(p->context, p->vs, NULL, 0);
        ID3D11DeviceContext_PSSetShader(p->context, p->ps, NULL, 0);
        ID3D11DeviceContext_PSSetShaderResources(p->context, 0, 3, p->views);
        ID3D11DeviceContext_PSSetSamplers(p->context, 0, 1, &p->sampler);
        ID3D11DeviceContext_PSSetConstantBuffers(p->context, 0, 1, &p->params);
        ID3D11DeviceContext_RSSetState(p->context, NULL);
        ID3D11DeviceContext_RSSetViewports(p->context, 1, &viewport);
        ID3D11DeviceContext_OMSetBlendState(p->context, NULL, NULL, 0xFFFFFFFFu);
        ID3D11DeviceContext_OMSetDepthStencilState(p->context, NULL, 0);
        ID3D11DeviceContext_OMSetRenderTargets(p->context, 1, &rtv, NULL);
        ID3D11DeviceContext_Draw(p->context, 3, 0);
        ID3D11ShaderResourceView *none[3] = { NULL, NULL, NULL };
        ID3D11DeviceContext_PSSetShaderResources(p->context, 0, 3, none);
        ID3D11DeviceContext_OMSetRenderTargets(p->context, 0, NULL, NULL);
    }
    if (p->lock) ID3D11Multithread_Leave(p->lock);
    return rtv != NULL;
}

#endif /* _WIN32 */
