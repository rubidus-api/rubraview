#include "rubraview/plugin.h"
#include "rubraview/plugin_filter.h"
#include "rubraview/lang.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* RFC-0007 (RV-138, D-186): what the viewer and a plug-in's child process agree on. */

#define HEADER_ALIGN 64u

static size_t header_bytes(void) {
    return (sizeof(rubraview_plugin_shared_t) + HEADER_ALIGN - 1u) / HEADER_ALIGN * HEADER_ALIGN;
}

size_t rubraview_plugin_shared_bytes(int32_t width, int32_t height) {
    if (width <= 0 || height <= 0 || width > 65535 || height > 65535) return 0;
    uint64_t picture = (uint64_t)width * (uint64_t)height * 4u;
    uint64_t total = (uint64_t)header_bytes() + picture * 2u;
    if (total > (uint64_t)SIZE_MAX || total > (uint64_t)1 << 34) return 0;
    return (size_t)total;
}

void rubraview_plugin_shared_init(rubraview_plugin_shared_t *s, int32_t op, int32_t width, int32_t height) {
    memset(s, 0, sizeof(*s));
    s->magic = RUBRAVIEW_PLUGIN_MAGIC;
    s->header_bytes = (uint32_t)header_bytes();
    s->op = op;
    s->state = RUBRAVIEW_PLUGIN_ASKED;
    s->width = width;
    s->height = height;
}

uint8_t *rubraview_plugin_shared_in(rubraview_plugin_shared_t *s) {
    return (uint8_t*)s + s->header_bytes;
}

uint8_t *rubraview_plugin_shared_out(rubraview_plugin_shared_t *s) {
    return (uint8_t*)s + s->header_bytes + (size_t)s->width * (size_t)s->height * 4u;
}

static void copy_text(char *dst, size_t capacity, const char *src) {
    size_t n = 0;
    if (src) while (src[n] && n + 1 < capacity) n++;
    if (src) memcpy(dst, src, n);
    dst[n] = '\0';
}

int32_t rubraview_plugin_choice_count(const char *choices) {
    if (!choices || !choices[0]) return 0;
    int32_t n = 1;
    for (const char *c = choices; *c; ++c) if (*c == '|') n++;
    return n;
}

bool rubraview_plugin_choice_word(const char *choices, int32_t index, char *out, size_t capacity) {
    if (!choices || index < 0 || !out || capacity == 0) return false;
    const char *start = choices;
    for (int32_t i = 0; i < index; ++i) {
        start = strchr(start, '|');
        if (!start) return false;
        start++;
    }
    const char *end = strchr(start, '|');
    size_t n = end ? (size_t)(end - start) : strlen(start);
    if (n + 1 > capacity) n = capacity - 1;
    memcpy(out, start, n);
    out[n] = '\0';
    return true;
}

bool rubraview_plugin_take_description(const rubraview_plugin_shared_t *s, rubraview_plugin_t *out, const char **why) {
    const char *dummy;
    if (!why) why = &dummy;
    *why = NULL;
    if (!s || !out || s->magic != RUBRAVIEW_PLUGIN_MAGIC || s->state != RUBRAVIEW_PLUGIN_DONE) { *why = N_("it did not describe itself"); return false; }
    if (s->abi != RVF_ABI) { *why = N_("it was written for another version of the plug-in interface"); return false; }
    if (s->described < 0 || s->described > RUBRAVIEW_PLUGIN_PARAMS) { *why = N_("it has more parameters than Rubraview takes"); return false; }
    copy_text(out->name, sizeof(out->name), s->name[0] ? s->name : out->file);
    copy_text(out->author, sizeof(out->author), s->author);
    copy_text(out->version, sizeof(out->version), s->version);
    copy_text(out->licence, sizeof(out->licence), s->licence);
    out->param_count = s->described;
    for (int32_t i = 0; i < s->described; ++i) {
        rubraview_plugin_param_t p = s->described_params[i];
        p.name[sizeof(p.name) - 1] = '\0';
        p.choices[sizeof(p.choices) - 1] = '\0';
        if (p.kind == RVF_BOOL) { p.min = 0.0; p.max = 1.0; p.step = 1.0; }
        if (p.kind == RVF_CHOICE) { p.min = 0.0; p.max = (double)(rubraview_plugin_choice_count(p.choices) - 1); p.step = 1.0; }
        if (p.kind < RVF_INT || p.kind > RVF_CHOICE || !isfinite(p.min) || !isfinite(p.max) || !(p.max >= p.min) ||
            (p.kind == RVF_CHOICE && p.max < 0.0)) {
            *why = N_("a parameter's range makes no sense");
            return false;
        }
        if (!(p.step > 0.0) || !isfinite(p.step)) p.step = p.kind == RVF_FLOAT ? (p.max - p.min) / 100.0 : 1.0;
        if (!(p.step > 0.0)) p.step = 1.0;
        out->params[i] = p;
    }
    out->usable = true;
    out->why = NULL;
    return true;
}

void rubraview_plugin_params_clamp(const rubraview_plugin_t *plugin, double *values, int32_t given) {
    if (!plugin || !values) return;
    for (int32_t i = 0; i < plugin->param_count && i < RUBRAVIEW_PLUGIN_PARAMS; ++i) {
        const rubraview_plugin_param_t *p = &plugin->params[i];
        double v = i < given && isfinite(values[i]) ? values[i] : p->value;
        if (p->kind != RVF_FLOAT) v = round(v);
        if (v < p->min) v = p->min;
        if (v > p->max) v = p->max;
        values[i] = v;
    }
    for (int32_t i = plugin->param_count; i < RUBRAVIEW_PLUGIN_PARAMS; ++i) values[i] = 0.0;
}

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }

bool rubraview_plugin_file_matches(const char *file, u8str_t name) {
    if (!file || name.len == 0) return false;
    size_t n = strlen(file);
    static const char SUFFIX[] = ".rvf.dll";
    size_t stem = n >= sizeof(SUFFIX) - 1 && strcmp(file + n - (sizeof(SUFFIX) - 1), SUFFIX) == 0 ? n - (sizeof(SUFFIX) - 1) : n;
    for (size_t k = 0; k < 2; ++k) {
        size_t want = k == 0 ? n : stem;
        if (name.len != want) continue;
        bool same = true;
        for (size_t i = 0; i < want && same; ++i) same = lower(file[i]) == lower(name.ptr[i]);
        if (same) return true;
    }
    return false;
}

bool rubraview_plugin_arg_parse(u8str_t value, char *name, size_t capacity, double *params, int32_t *count) {
    if (!name || capacity == 0 || !params || !count) return false;
    size_t colon = 0;
    while (colon < value.len && value.ptr[colon] != ':') colon++;
    if (colon == 0 || colon + 1 > capacity) return false;
    memcpy(name, value.ptr, colon);
    name[colon] = '\0';
    *count = 0;
    if (colon >= value.len) return true;
    size_t at = colon + 1;
    while (at <= value.len) {
        size_t end = at;
        while (end < value.len && value.ptr[end] != ',') end++;
        if (end == at || *count >= RUBRAVIEW_PLUGIN_PARAMS || end - at >= 32) return false;
        char number[32];
        memcpy(number, value.ptr + at, end - at);
        number[end - at] = '\0';
        char *stop = NULL;
        double v = strtod(number, &stop);
        if (!stop || *stop != '\0' || !isfinite(v)) return false;
        params[(*count)++] = v;
        at = end + 1;
        if (end == value.len) break;
    }
    return true;
}

/* ---- SHA-256, FIPS 180-4 ---- */

static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void sha_block(uint32_t h[8], const uint8_t *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t t1 = hh + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void rubraview_sha256(const uint8_t *data, size_t size, uint8_t out[32]) {
    uint32_t h[8] = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
    size_t full = size / 64u;
    for (size_t i = 0; i < full; ++i) sha_block(h, data + i * 64u);
    uint8_t last[128] = { 0 };
    size_t rest = size - full * 64u;
    if (rest) memcpy(last, data + full * 64u, rest);
    last[rest] = 0x80;
    size_t blocks = rest + 9u > 64u ? 2u : 1u;
    uint64_t bits = (uint64_t)size * 8u;
    for (int i = 0; i < 8; ++i) last[blocks * 64u - 1u - (size_t)i] = (uint8_t)(bits >> (8 * i));
    for (size_t i = 0; i < blocks; ++i) sha_block(h, last + i * 64u);
    for (int i = 0; i < 8; ++i) { out[i * 4] = (uint8_t)(h[i] >> 24); out[i * 4 + 1] = (uint8_t)(h[i] >> 16); out[i * 4 + 2] = (uint8_t)(h[i] >> 8); out[i * 4 + 3] = (uint8_t)h[i]; }
}
