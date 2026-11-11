#ifdef _WIN32
#define COBJMACROS
#include "rubraview/core.h"
#include <windows.h>
#include <d3d11.h>
#include <d3dcommon.h>
#include <stdlib.h>
#include <string.h>
#include "rubraview/pal/pal_vgrade.h"
#include "rubraview/lang.h"

/*
 * RV-270 (D-330): a film's adjustment and its upscaler on the graphics card, the steps of vgrade.c in the same
 * order: the per-channel tables (Edit's commit, rubraview_edit_tone_tables) and the saturation; the Gaussian
 * blur in two passes; the unsharp mask (a blur across, then down with the mix); for a film drawn larger than
 * its pixels, the edge-directed Lanczos-2 into a float picture of the size on screen, then the sharpening held
 * inside each pixel's neighbours.
 *
 * Compiled at run time by Windows' d3dcompiler_47.dll, as the HDR pass is (D-180); it runs on WARP, which is
 * how the VM sees it. One engine a renderer, on its device and the UI thread's immediate context, outside
 * Direct2D's drawing.
 */

typedef HRESULT (WINAPI *d3dcompile_fn)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *,
                                        LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);

static const char SHADER[] =
    "cbuffer p : register(b0) { float4 a; float4 b; float4 w[3]; };\n"
    /* tone:    a.x saturation, a.y 1 an HDR texture (values past 1 kept)
       blur:    a.y HDR, a.z radius, a.w 0 across, 1 down; w the weights from the centre
       sharpen: a.x amount, a.y HDR, a.z radius; w the weights (t0 the picture, t1 its blur across)
       up:      a.x strength, a.y HDR; b.xy the source's size, b.zw the target's
       edge:    a.x strength, a.y HDR */
    "Texture2D<float4> t0 : register(t0);\n"
    "Texture2D<float4> t1 : register(t1);\n"
    "struct vs_out { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "vs_out vs(uint id : SV_VertexID) {\n"
    "    vs_out o;\n"
    "    float2 t = float2((id << 1) & 2, id & 2);\n"
    "    o.uv = t;\n"
    "    o.pos = float4(t.x * 2.0 - 1.0, 1.0 - t.y * 2.0, 0.0, 1.0);\n"
    "    return o;\n"
    "}\n"
    "float3 limit(float3 v) { return a.y > 0.5 ? max(v, 0.0) : saturate(v); }\n"
    "float weight(int k) { return w[k >> 2][k & 3]; }\n"
    "int2 last_of(Texture2D<float4> t) { uint x, y; t.GetDimensions(x, y); return int2(x, y) - 1; }\n"
    "float4 at(Texture2D<float4> t, int2 p, int2 last) { return t.Load(int3(clamp(p, int2(0, 0), last), 0)); }\n"
    /* vgrade.c's rubraview_vgrade_tone_value: t1 is 256 x 1, red green blue the three tables */
    "float table(float coded, int ch) {\n"
    "    if (!(coded > 0.0)) return t1.Load(int3(0, 0, 0))[ch];\n"
    "    if (coded >= 1.0) return coded * t1.Load(int3(255, 0, 0))[ch];\n"
    "    float x = coded * 255.0;\n"
    "    int i = min((int)x, 254);\n"
    "    float f = x - (float)i;\n"
    "    float lo = t1.Load(int3(i, 0, 0))[ch], hi = t1.Load(int3(i + 1, 0, 0))[ch];\n"
    "    return lo + (hi - lo) * f;\n"
    "}\n"
    "float4 ps_tone(vs_out i) : SV_Target {\n"
    "    float4 c = t0.Load(int3(i.pos.xy, 0));\n"
    "    float3 v = float3(table(c.r, 0), table(c.g, 1), table(c.b, 2));\n"
    "    if (a.x != 1.0) {\n"   /* rubraview_color_saturate */
    "        float lum = 0.2126 * v.r + 0.7152 * v.g + 0.0722 * v.b;\n"
    "        v = limit(lum + (v - lum) * a.x);\n"
    "    }\n"
    "    return float4(v, 1.0);\n"
    "}\n"
    "float3 blur_of(Texture2D<float4> t, int2 p, int2 d, int r) {\n"
    "    int2 last = last_of(t);\n"
    "    float3 s = t.Load(int3(p, 0)).rgb * weight(0);\n"
    "    [loop] for (int k = 1; k <= r; ++k) s += (at(t, p + d * k, last).rgb + at(t, p - d * k, last).rgb) * weight(k);\n"
    "    return s;\n"
    "}\n"
    "float4 ps_blur(vs_out i) : SV_Target {\n"
    "    int2 d = a.w > 0.5 ? int2(0, 1) : int2(1, 0);\n"
    "    return float4(limit(blur_of(t0, int2(i.pos.xy), d, (int)a.z)), 1.0);\n"
    "}\n"
    "float4 ps_sharpen(vs_out i) : SV_Target {\n"   /* the unsharp mask: c + amount (c - blur), threshold 0 */
    "    int2 p = int2(i.pos.xy);\n"
    "    float3 c = t0.Load(int3(p, 0)).rgb;\n"
    "    float3 s = blur_of(t1, p, int2(0, 1), (int)a.z);\n"
    "    return float4(limit(c + a.x * (c - s)), 1.0);\n"
    "}\n"
    "float luma(float3 v) { return 0.2126 * v.r + 0.7152 * v.g + 0.0722 * v.b; }\n"
    "float lanczos2(float d) {\n"
    "    d = abs(d);\n"
    "    if (d < 1e-5) return 1.0;\n"
    "    if (d >= 2.0) return 0.0;\n"
    "    float pd = 3.14159265 * d;\n"
    "    return 2.0 * sin(pd) * sin(pd * 0.5) / (pd * pd);\n"
    "}\n"
    "float4 ps_up(vs_out i) : SV_Target {\n"   /* vgrade.c's upscale_at */
    "    float2 pos = (floor(i.pos.xy) + 0.5) * b.xy / b.zw - 0.5;\n"
    "    float2 f = floor(pos);\n"
    "    float tx = pos.x - f.x, ty = pos.y - f.y;\n"
    "    int2 base = int2(f);\n"
    "    int2 last = last_of(t0);\n"
    "    float3 t[4][4];\n"
    "    float l[4][4];\n"
    "    [unroll] for (int j = 0; j < 4; ++j) [unroll] for (int k = 0; k < 4; ++k) {\n"
    "        t[j][k] = at(t0, base + int2(k - 1, j - 1), last).rgb;\n"
    "        l[j][k] = luma(t[j][k]);\n"
    "    }\n"
    "    float gx = 0.0, gy = 0.0;\n"
    "    [unroll] for (int j2 = 1; j2 <= 2; ++j2) [unroll] for (int k2 = 1; k2 <= 2; ++k2) {\n"
    "        float wt = (k2 == 1 ? 1.0 - tx : tx) * (j2 == 1 ? 1.0 - ty : ty);\n"
    "        gx += wt * (l[j2][k2 + 1] - l[j2][k2 - 1]) * 0.5;\n"
    "        gy += wt * (l[j2 + 1][k2] - l[j2 - 1][k2]) * 0.5;\n"
    "    }\n"
    "    float len = sqrt(gx * gx + gy * gy);\n"
    "    float dx = 1.0, dy = 0.0, e = 0.0;\n"
    "    if (len > 1.0 / 1024.0) { dx = gx / len; dy = gy / len; e = min(len * 4.0, 1.0) * a.x; }\n"
    "    float across = 1.0 + 0.5 * e, along = 1.0 / (1.0 + e);\n"
    "    float3 sum = float3(0.0, 0.0, 0.0);\n"
    "    float wsum = 0.0;\n"
    "    [unroll] for (int j3 = 0; j3 < 4; ++j3) [unroll] for (int k3 = 0; k3 < 4; ++k3) {\n"
    "        float ox = (float)(k3 - 1) - tx, oy = (float)(j3 - 1) - ty;\n"
    "        float u = ox * dx + oy * dy, v = -ox * dy + oy * dx;\n"
    "        float wt = lanczos2(sqrt(u * across * u * across + v * along * v * along));\n"
    "        sum += wt * t[j3][k3];\n"
    "        wsum += wt;\n"
    "    }\n"
    "    float3 lo = min(min(t[1][1], t[1][2]), min(t[2][1], t[2][2]));\n"
    "    float3 hi = max(max(t[1][1], t[1][2]), max(t[2][1], t[2][2]));\n"
    "    float3 r = wsum > 1e-4 ? sum / wsum : t[1][1];\n"
    "    return float4(clamp(r, lo, hi), 1.0);\n"
    "}\n"
    "float4 ps_edge(vs_out i) : SV_Target {\n"   /* vgrade.c's step 2 */
    "    int2 p = int2(i.pos.xy);\n"
    "    int2 last = last_of(t0);\n"
    "    float3 c = t0.Load(int3(p, 0)).rgb;\n"
    "    float3 nl = at(t0, p + int2(-1, 0), last).rgb, nr = at(t0, p + int2(1, 0), last).rgb;\n"
    "    float3 nu = at(t0, p + int2(0, -1), last).rgb, nd = at(t0, p + int2(0, 1), last).rgb;\n"
    "    float3 mn = min(min(min(c, nl), min(nr, nu)), nd);\n"
    "    float3 mx = max(max(max(c, nl), max(nr, nu)), nd);\n"
    "    float3 room = min(mn, 1.0 - mx) / max(mx, 1e-5);\n"
    "    float3 amp = sqrt(saturate(room));\n"
    "    float3 v = c + amp * a.x * 0.5 * (4.0 * c - nl - nr - nu - nd) * 0.25;\n"
    "    return float4(clamp(v, mn, mx), 1.0);\n"
    "}\n";

#define VG_PS_TONE 0
#define VG_PS_BLUR 1
#define VG_PS_SHARPEN 2
#define VG_PS_UP 3
#define VG_PS_EDGE 4
#define VG_PS_COUNT 5

typedef struct vg_scratch {
    ID3D11Texture2D *texture;
    ID3D11ShaderResourceView *srv;
    ID3D11RenderTargetView *rtv;
    int32_t width, height;
} vg_scratch_t;

struct rubraview_vgrade_gpu {
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps[VG_PS_COUNT];
    ID3D11Buffer *params;
    ID3D11Texture2D *table;
    ID3D11ShaderResourceView *table_view;
    vg_scratch_t scratch[3];   /* two at the film's size, one at the size on screen */
};

static d3dcompile_fn compiler(void) {
    static d3dcompile_fn compile_fn;
    static bool tried;
    if (!tried) {
        tried = true;
        HMODULE dll = LoadLibraryExW(L"d3dcompiler_47.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        compile_fn = dll ? (d3dcompile_fn)(void*)GetProcAddress(dll, "D3DCompile") : NULL;
    }
    return compile_fn;
}

static ID3DBlob *compile(d3dcompile_fn fn, const char *entry, const char *profile) {
    ID3DBlob *code = NULL, *errors = NULL;
    HRESULT hr = fn(SHADER, sizeof(SHADER) - 1, "rubraview_vgrade", NULL, NULL, entry, profile, 0, 0, &code, &errors);
    if (errors) ID3D10Blob_Release(errors);
    return SUCCEEDED(hr) ? code : NULL;
}

static void scratch_free(vg_scratch_t *s) {
    if (s->rtv) ID3D11RenderTargetView_Release(s->rtv);
    if (s->srv) ID3D11ShaderResourceView_Release(s->srv);
    if (s->texture) ID3D11Texture2D_Release(s->texture);
    proven_sys_mem_set(s, 0, sizeof(*s));
}

void rubraview_pal_vgrade_gpu_free(rubraview_vgrade_gpu_t *g) {
    if (!g) return;
    for (int i = 0; i < 3; ++i) scratch_free(&g->scratch[i]);
    if (g->table_view) ID3D11ShaderResourceView_Release(g->table_view);
    if (g->table) ID3D11Texture2D_Release(g->table);
    if (g->params) ID3D11Buffer_Release(g->params);
    for (int i = 0; i < VG_PS_COUNT; ++i) if (g->ps[i]) ID3D11PixelShader_Release(g->ps[i]);
    if (g->vs) ID3D11VertexShader_Release(g->vs);
    if (g->context) ID3D11DeviceContext_Release(g->context);
    if (g->device) ID3D11Device_Release(g->device);
    rubraview_free(g);
}

rubraview_vgrade_gpu_t *rubraview_pal_vgrade_gpu_create(void *d3d11_device, const char **why) {
    const char *dummy;
    if (!why) why = &dummy;
    *why = NULL;
    d3dcompile_fn fn = compiler();
    if (!d3d11_device) { *why = N_("no Direct3D 11 device"); return NULL; }
    if (!fn) { *why = N_("no shader compiler (d3dcompiler_47.dll)"); return NULL; }
    rubraview_vgrade_gpu_t *g = (rubraview_vgrade_gpu_t*)rubraview_alloc_zero(1, sizeof(*g));
    if (!g) { *why = N_("out of memory"); return NULL; }
    g->device = (ID3D11Device*)d3d11_device;
    ID3D11Device_AddRef(g->device);
    ID3D11Device_GetImmediateContext(g->device, &g->context);
    bool ok = g->context != NULL;
    ID3DBlob *vs = compile(fn, "vs", "vs_4_0");
    if (vs && FAILED(ID3D11Device_CreateVertexShader(g->device, ID3D10Blob_GetBufferPointer(vs), ID3D10Blob_GetBufferSize(vs), NULL, &g->vs)))
        g->vs = NULL;
    if (vs) ID3D10Blob_Release(vs);
    static const char *const ENTRIES[VG_PS_COUNT] = { "ps_tone", "ps_blur", "ps_sharpen", "ps_up", "ps_edge" };
    for (int i = 0; i < VG_PS_COUNT && ok; ++i) {
        ID3DBlob *ps = compile(fn, ENTRIES[i], "ps_4_0");
        if (!ps || FAILED(ID3D11Device_CreatePixelShader(g->device, ID3D10Blob_GetBufferPointer(ps), ID3D10Blob_GetBufferSize(ps), NULL, &g->ps[i]))) {
            g->ps[i] = NULL;
            ok = false;
        }
        if (ps) ID3D10Blob_Release(ps);
    }
    D3D11_BUFFER_DESC cb = { .ByteWidth = 80, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER };
    if (FAILED(ID3D11Device_CreateBuffer(g->device, &cb, NULL, &g->params))) g->params = NULL;
    D3D11_TEXTURE2D_DESC td = {
        .Width = 256, .Height = 1, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
        .SampleDesc = { .Count = 1, .Quality = 0 }, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE,
    };
    if (FAILED(ID3D11Device_CreateTexture2D(g->device, &td, NULL, &g->table))) g->table = NULL;
    if (g->table && FAILED(ID3D11Device_CreateShaderResourceView(g->device, (ID3D11Resource*)g->table, NULL, &g->table_view)))
        g->table_view = NULL;
    if (!ok || !g->vs || !g->params || !g->table_view) {
        *why = N_("the picture adjustment shader would not build");
        rubraview_pal_vgrade_gpu_free(g);
        return NULL;
    }
    return g;
}

/* A float picture of the pass's own, `w` x `h`, kept until the size changes. */
static bool scratch_ready(rubraview_vgrade_gpu_t *g, vg_scratch_t *s, int32_t w, int32_t h) {
    if (s->texture && s->width == w && s->height == h) return true;
    scratch_free(s);
    D3D11_TEXTURE2D_DESC desc = {
        .Width = (UINT)w, .Height = (UINT)h, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
        .SampleDesc = { .Count = 1, .Quality = 0 }, .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
    };
    if (FAILED(ID3D11Device_CreateTexture2D(g->device, &desc, NULL, &s->texture))) { s->texture = NULL; return false; }
    if (FAILED(ID3D11Device_CreateShaderResourceView(g->device, (ID3D11Resource*)s->texture, NULL, &s->srv)) ||
        FAILED(ID3D11Device_CreateRenderTargetView(g->device, (ID3D11Resource*)s->texture, NULL, &s->rtv))) {
        scratch_free(s);
        return false;
    }
    s->width = w;
    s->height = h;
    return true;
}

/* One shader over a whole target of `w` x `h`, with `params` (20 floats) and t0, t1. */
static void draw(rubraview_vgrade_gpu_t *g, int ps, const float params[20], ID3D11ShaderResourceView *v0,
                 ID3D11ShaderResourceView *v1, ID3D11RenderTargetView *rtv, int32_t w, int32_t h) {
    ID3D11DeviceContext *c = g->context;
    ID3D11DeviceContext_UpdateSubresource(c, (ID3D11Resource*)g->params, 0, NULL, params, 0, 0);
    D3D11_VIEWPORT viewport = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
    ID3D11ShaderResourceView *views[2] = { v0, v1 };
    ID3D11DeviceContext_IASetInputLayout(c, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(c, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(c, g->vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(c, g->ps[ps], NULL, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(c, 0, 1, &g->params);
    ID3D11DeviceContext_RSSetState(c, NULL);
    ID3D11DeviceContext_RSSetViewports(c, 1, &viewport);
    ID3D11DeviceContext_OMSetBlendState(c, NULL, NULL, 0xFFFFFFFFu);
    ID3D11DeviceContext_OMSetDepthStencilState(c, NULL, 0);
    ID3D11DeviceContext_OMSetRenderTargets(c, 1, &rtv, NULL);
    ID3D11DeviceContext_PSSetShaderResources(c, 0, 2, views);
    ID3D11DeviceContext_Draw(c, 3, 0);
    ID3D11ShaderResourceView *none[2] = { NULL, NULL };
    ID3D11DeviceContext_PSSetShaderResources(c, 0, 2, none);
    ID3D11DeviceContext_OMSetRenderTargets(c, 0, NULL, NULL);
}

static void put_weights(float params[20], const float *weights) {
    for (int k = 0; k < RUBRAVIEW_VGRADE_TAPS && k < 12; ++k) params[8 + k] = weights[k];
}

typedef struct vg_views {
    ID3D11ShaderResourceView *src;
    ID3D11RenderTargetView *dst;
} vg_views_t;

static bool views_of(rubraview_vgrade_gpu_t *g, void *src, void *dst, vg_views_t *out) {
    out->src = NULL;
    out->dst = NULL;
    if (FAILED(ID3D11Device_CreateShaderResourceView(g->device, (ID3D11Resource*)src, NULL, &out->src))) out->src = NULL;
    if (FAILED(ID3D11Device_CreateRenderTargetView(g->device, (ID3D11Resource*)dst, NULL, &out->dst))) out->dst = NULL;
    return out->src && out->dst;
}

static void views_free(vg_views_t *v) {
    if (v->src) ID3D11ShaderResourceView_Release(v->src);
    if (v->dst) ID3D11RenderTargetView_Release(v->dst);
}

bool rubraview_pal_vgrade_gpu_grade(rubraview_vgrade_gpu_t *g, const rubraview_vgrade_prepared_t *p, void *src, void *dst,
                                    int32_t w, int32_t h, bool hdr) {
    if (!g || !p || !src || !dst || w <= 0 || h <= 0) return false;
    vg_views_t v;
    if (!views_of(g, src, dst, &v)) { views_free(&v); return false; }
    int steps = (p->tone ? 1 : 0) + (p->blur_radius > 0 ? 2 : 0) + (p->sharpen_radius > 0 ? 2 : 0);
    bool ok = steps > 0 && scratch_ready(g, &g->scratch[0], w, h) && (steps < 2 || scratch_ready(g, &g->scratch[1], w, h));
    if (ok) {
        /* each step reads `from` and writes the next free picture, the last of all into `dst` */
        ID3D11ShaderResourceView *from = v.src;
        int done = 0, next = 0;
        float params[20];
        if (p->tone) {
            float rows[256][4];
            for (int i = 0; i < 256; ++i) {
                rows[i][0] = p->table[0][i]; rows[i][1] = p->table[1][i]; rows[i][2] = p->table[2][i]; rows[i][3] = 1.0f;
            }
            ID3D11DeviceContext_UpdateSubresource(g->context, (ID3D11Resource*)g->table, 0, NULL, rows, sizeof(rows), 0);
            proven_sys_mem_set(params, 0, sizeof(params));
            params[0] = p->saturation;
            params[1] = hdr ? 1.0f : 0.0f;
            bool last = ++done == steps;
            draw(g, VG_PS_TONE, params, from, g->table_view, last ? v.dst : g->scratch[next].rtv, w, h);
            if (!last) { from = g->scratch[next].srv; next ^= 1; }
        }
        if (p->blur_radius > 0) {
            for (int pass = 0; pass < 2; ++pass) {
                proven_sys_mem_set(params, 0, sizeof(params));
                params[1] = hdr ? 1.0f : 0.0f;
                params[2] = (float)p->blur_radius;
                params[3] = (float)pass;
                put_weights(params, p->blur_weights);
                bool last = ++done == steps;
                draw(g, VG_PS_BLUR, params, from, NULL, last ? v.dst : g->scratch[next].rtv, w, h);
                if (!last) { from = g->scratch[next].srv; next ^= 1; }
            }
        }
        if (p->sharpen_radius > 0) {
            /* the blur across into the free picture; then down, mixed with the picture it came from */
            proven_sys_mem_set(params, 0, sizeof(params));
            params[1] = hdr ? 1.0f : 0.0f;
            params[2] = (float)p->sharpen_radius;
            params[3] = 0.0f;
            put_weights(params, p->sharpen_weights);
            ++done;
            draw(g, VG_PS_BLUR, params, from, NULL, g->scratch[next].rtv, w, h);
            ID3D11ShaderResourceView *across = g->scratch[next].srv;
            params[0] = p->sharpen_amount;
            ++done;
            draw(g, VG_PS_SHARPEN, params, from, across, v.dst, w, h);
        }
    }
    views_free(&v);
    return ok;
}

bool rubraview_pal_vgrade_gpu_upscale(rubraview_vgrade_gpu_t *g, float strength, void *src, int32_t sw, int32_t sh,
                                      void *dst, int32_t dw, int32_t dh, bool hdr) {
    if (!g || !src || !dst || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return false;
    vg_views_t v;
    if (!views_of(g, src, dst, &v)) { views_free(&v); return false; }
    bool ok = scratch_ready(g, &g->scratch[2], dw, dh);
    if (ok) {
        float params[20] = { strength, hdr ? 1.0f : 0.0f, 0.0f, 0.0f, (float)sw, (float)sh, (float)dw, (float)dh };
        draw(g, VG_PS_UP, params, v.src, NULL, g->scratch[2].rtv, dw, dh);
        draw(g, VG_PS_EDGE, params, g->scratch[2].srv, NULL, v.dst, dw, dh);
    }
    views_free(&v);
    return ok;
}

#endif /* _WIN32 */
