#include "rubraview/fileinfo.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static u8str_t keep(proven_arena_t *arena, const char *text, size_t n) {
    proven_result_mem_mut_t res = proven_arena_alloc(arena, n + 1);
    if (!proven_is_ok(res.err)) return (u8str_t){ .ptr = "", .len = 0 };
    if (n) memcpy(res.value.ptr, text, n);
    res.value.ptr[n] = '\0';
    return (u8str_t){ .ptr = (const char*)res.value.ptr, .len = n };
}

static void push(proven_arena_t *arena, rubraview_info_t *info, rubraview_info_line_t line) {
    if (!arena || !info) return;
    if (info->count == info->capacity) {
        size_t cap = info->capacity ? info->capacity * 2 : 32;
        proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, cap, sizeof(rubraview_info_line_t));
        if (!proven_is_ok(res.err)) return;
        rubraview_info_line_t *grown = (rubraview_info_line_t*)(void*)res.value.ptr;
        if (info->count) memcpy(grown, info->lines, info->count * sizeof(*grown));
        info->lines = grown;
        info->capacity = cap;
    }
    info->lines[info->count++] = line;
}

void rubraview_info_heading(proven_arena_t *arena, rubraview_info_t *info, const char *title) {
    push(arena, info, (rubraview_info_line_t){ .heading = true, .label = keep(arena, title, strlen(title)) });
}

void rubraview_info_add(proven_arena_t *arena, rubraview_info_t *info, const char *label, u8str_t value) {
    if (value.len == 0) return;
    push(arena, info, (rubraview_info_line_t){ .label = keep(arena, label, strlen(label)),
                                               .value = keep(arena, value.ptr, value.len) });
}

void rubraview_info_addf(proven_arena_t *arena, rubraview_info_t *info, const char *label, const char *format, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    if (n <= 0) return;
    size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
    rubraview_info_add(arena, info, label, (u8str_t){ .ptr = buf, .len = len });
}

static size_t done(char *buf, size_t cap, int n) {
    if (n < 0) { buf[0] = '\0'; return 0; }
    return (size_t)n < cap ? (size_t)n : cap - 1;
}

size_t rubraview_info_bytes(char *buf, size_t cap, uint64_t bytes) {
    char digits[32], grouped[48];
    int d = snprintf(digits, sizeof(digits), "%llu", (unsigned long long)bytes);
    size_t g = 0;
    for (int i = 0; i < d; ++i) {
        if (i > 0 && (d - i) % 3 == 0) grouped[g++] = ',';
        grouped[g++] = digits[i];
    }
    grouped[g] = '\0';
    if (bytes < 1024) return done(buf, cap, snprintf(buf, cap, "%s bytes", grouped));
    static const char *const UNIT[] = { "KB", "MB", "GB", "TB" };
    double v = (double)bytes / 1024.0;
    int u = 0;
    while (v >= 1024.0 && u < 3) { v /= 1024.0; ++u; }
    return done(buf, cap, snprintf(buf, cap, "%.2f %s (%s bytes)", v, UNIT[u], grouped));
}

size_t rubraview_info_exposure(char *buf, size_t cap, uint32_t num, uint32_t den) {
    if (den == 0) { buf[0] = '\0'; return 0; }
    double s = (double)num / (double)den;
    if (s >= 1.0 || num == 0) return done(buf, cap, snprintf(buf, cap, "%g s", s));
    return done(buf, cap, snprintf(buf, cap, "1/%.0f s", 1.0 / s));
}

static size_t dms(char *buf, size_t cap, double v, char positive, char negative) {
    char hemisphere = v < 0 ? negative : positive;
    v = fabs(v);
    int deg = (int)v;
    double rest = (v - deg) * 60.0;
    int min = (int)rest;
    double sec = (rest - min) * 60.0;
    if (sec >= 59.95) { sec = 0.0; if (++min == 60) { min = 0; ++deg; } }
    return done(buf, cap, snprintf(buf, cap, "%d\xC2\xB0%d'%.1f\"%c", deg, min, sec, hemisphere));
}

size_t rubraview_info_gps(char *buf, size_t cap, double latitude, double longitude) {
    char a[48], b[48];
    dms(a, sizeof(a), latitude, 'N', 'S');
    dms(b, sizeof(b), longitude, 'E', 'W');
    return done(buf, cap, snprintf(buf, cap, "%s %s", a, b));
}

size_t rubraview_info_duration(char *buf, size_t cap, double seconds) {
    if (!(seconds >= 0.0)) seconds = 0.0;
    long long ms = llround(seconds * 1000.0);
    long long h = ms / 3600000, m = (ms / 60000) % 60, s = (ms / 1000) % 60, f = ms % 1000;
    if (h > 0) return done(buf, cap, snprintf(buf, cap, "%lld:%02lld:%02lld.%03lld", h, m, s, f));
    return done(buf, cap, snprintf(buf, cap, "%lld:%02lld.%03lld", m, s, f));
}

size_t rubraview_info_fourcc(char *buf, size_t cap, uint32_t fourcc) {
    char c[4] = { (char)(fourcc & 0xFF), (char)((fourcc >> 8) & 0xFF), (char)((fourcc >> 16) & 0xFF), (char)(fourcc >> 24) };
    bool printable = true;
    for (int i = 0; i < 4; ++i) printable = printable && c[i] >= 0x20 && c[i] < 0x7F;
    if (!printable) return done(buf, cap, snprintf(buf, cap, "0x%08X", (unsigned)fourcc));
    return done(buf, cap, snprintf(buf, cap, "%c%c%c%c", c[0], c[1], c[2], c[3]));
}

static const char *orientation_words(int32_t o) {
    switch (o) {
        case 1: return "1 (upright)";
        case 2: return "2 (mirrored)";
        case 3: return "3 (upside down)";
        case 4: return "4 (flipped)";
        case 5: return "5 (mirrored, turned a quarter to the left)";
        case 6: return "6 (turned a quarter to the right)";
        case 7: return "7 (mirrored, turned a quarter to the right)";
        case 8: return "8 (turned a quarter to the left)";
        default: return NULL;
    }
}

static u8str_t lit(const char *s) { return (u8str_t){ .ptr = s, .len = strlen(s) }; }

void rubraview_info_add_exif(proven_arena_t *arena, rubraview_info_t *info, const rubraview_exif_info_t *x) {
    if (!x || !x->found) return;
    rubraview_info_heading(arena, info, "EXIF");
    char b[160];
    /* "Canon EOS R5", once: many cameras put the maker in the model too */
    size_t ml = strlen(x->make);
    if (ml > 0 && strncmp(x->model, x->make, ml) != 0) rubraview_info_addf(arena, info, "Camera", "%s %s", x->make, x->model);
    else rubraview_info_add(arena, info, "Camera", lit(x->model[0] ? x->model : x->make));
    rubraview_info_add(arena, info, "Lens", lit(x->lens));
    rubraview_info_add(arena, info, "Taken", lit(x->date_taken));
    rubraview_info_add(arena, info, "Changed", lit(x->date_modified));
    if (x->has_exposure) rubraview_info_add(arena, info, "Exposure", (u8str_t){ b, rubraview_info_exposure(b, sizeof(b), x->exposure_num, x->exposure_den) });
    if (x->has_fnumber) rubraview_info_addf(arena, info, "Aperture", "f/%.1f", x->fnumber);
    if (x->iso) rubraview_info_addf(arena, info, "ISO", "%u", (unsigned)x->iso);
    if (x->has_focal) {
        if (x->focal_35mm) rubraview_info_addf(arena, info, "Focal length", "%.0f mm (%u mm in 35 mm terms)", x->focal_mm, (unsigned)x->focal_35mm);
        else rubraview_info_addf(arena, info, "Focal length", "%.0f mm", x->focal_mm);
    }
    if (x->has_bias) rubraview_info_addf(arena, info, "Exposure bias", "%+.2f EV", x->exposure_bias);
    if (x->has_flash) rubraview_info_add(arena, info, "Flash", lit((x->flash & 1) ? "fired" : "did not fire"));
    const char *o = orientation_words(x->orientation);
    if (o) rubraview_info_add(arena, info, "Orientation", lit(o));
    if (x->pixel_x && x->pixel_y) rubraview_info_addf(arena, info, "Pixels (EXIF)", "%u x %u", (unsigned)x->pixel_x, (unsigned)x->pixel_y);
    if (x->has_gps) rubraview_info_add(arena, info, "Location", (u8str_t){ b, rubraview_info_gps(b, sizeof(b), x->latitude, x->longitude) });
    if (x->has_altitude) rubraview_info_addf(arena, info, "Altitude", "%.0f m", x->altitude_m);
    rubraview_info_add(arena, info, "Software", lit(x->software));
    rubraview_info_add(arena, info, "Artist", lit(x->artist));
    rubraview_info_add(arena, info, "Copyright", lit(x->copyright));
}

size_t rubraview_info_rows(proven_arena_t *arena, const rubraview_info_t *info, int32_t label_cols,
                           u8str_t *rows, size_t capacity) {
    if (!arena || !info || !rows) return 0;
    size_t n = 0;
    for (size_t i = 0; i < info->count && n < capacity; ++i) {
        const rubraview_info_line_t *l = &info->lines[i];
        if (l->heading) { rows[n++] = l->label; continue; }
        size_t pad = label_cols > (int32_t)l->label.len ? (size_t)label_cols - l->label.len : 1;
        size_t len = 2 + l->label.len + pad + l->value.len;
        proven_result_mem_mut_t res = proven_arena_alloc(arena, len + 1);
        if (!proven_is_ok(res.err)) break;
        char *p = (char*)res.value.ptr;
        memcpy(p, "  ", 2);
        memcpy(p + 2, l->label.ptr, l->label.len);
        memset(p + 2 + l->label.len, ' ', pad);
        memcpy(p + 2 + l->label.len + pad, l->value.ptr, l->value.len);
        p[len] = '\0';
        rows[n++] = (u8str_t){ .ptr = p, .len = len };
    }
    return n;
}
