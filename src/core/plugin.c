#include "rubraview/core.h"
#include "rubraview/plugin.h"
#include "rubraview/plugin_filter.h"
#include "rubraview/lang.h"
#include "proven/sha2.h"
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
    proven_sys_mem_set(s, 0, sizeof(*s));
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
    if (src) proven_sys_mem_copy(dst, src, n);
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
        start = rubraview_cstr_chr(start, '|');
        if (!start) return false;
        start++;
    }
    const char *end = rubraview_cstr_chr(start, '|');
    size_t n = end ? (size_t)(end - start) : proven_cstr_len(start);
    if (n + 1 > capacity) n = capacity - 1;
    proven_sys_mem_copy(out, start, n);
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

bool rubraview_plugin_file_matches(const char *file, proven_u8str_view_t name) {
    if (!file || name.size == 0) return false;
    size_t n = proven_cstr_len(file);
    static const char SUFFIX[] = ".rvf.dll";
    size_t stem = n >= sizeof(SUFFIX) - 1 && rubraview_cstr_cmp(file + n - (sizeof(SUFFIX) - 1), SUFFIX) == 0 ? n - (sizeof(SUFFIX) - 1) : n;
    for (size_t k = 0; k < 2; ++k) {
        size_t want = k == 0 ? n : stem;
        if (proven_u8str_view_eq_ascii_ci(name, proven_u8str_view_from_bytes(file, want))) return true;
    }
    return false;
}

bool rubraview_plugin_arg_parse(proven_u8str_view_t value, char *name, size_t capacity, double *params, int32_t *count) {
    if (!name || capacity == 0 || !params || !count) return false;
    size_t colon = 0;
    while (colon < value.size && value.ptr[colon] != ':') colon++;
    if (colon == 0 || colon + 1 > capacity) return false;
    proven_sys_mem_copy(name, value.ptr, colon);
    name[colon] = '\0';
    *count = 0;
    if (colon >= value.size) return true;
    size_t at = colon + 1;
    while (at <= value.size) {
        size_t end = at;
        while (end < value.size && value.ptr[end] != ',') end++;
        if (end == at || *count >= RUBRAVIEW_PLUGIN_PARAMS || end - at >= 32) return false;
        char number[32];
        proven_sys_mem_copy(number, value.ptr + at, end - at);
        number[end - at] = '\0';
        char *stop = NULL;
        double v = proven_strtod(number, &stop);
        if (!stop || *stop != '\0' || !isfinite(v)) return false;
        params[(*count)++] = v;
        at = end + 1;
        if (end == value.size) break;
    }
    return true;
}

/* SHA-256 (FIPS 180-4) is proven_crypto's (D-329). */
void rubraview_sha256(proven_mem_view_t data_view, uint8_t out[32]) { proven_sha256(data_view, out); }
