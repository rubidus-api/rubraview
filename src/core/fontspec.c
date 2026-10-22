#include "rubraview/fontspec.h"
#include <stdio.h>
#include <string.h>

static const struct { int32_t weight; const char *name; } WEIGHTS[RUBRAVIEW_FONT_WEIGHTS] = {
    { 100, "Thin" }, { 200, "ExtraLight" }, { 300, "Light" }, { 400, "Regular" }, { 500, "Medium" },
    { 600, "SemiBold" }, { 700, "Bold" }, { 800, "ExtraBold" }, { 900, "Black" },
};

int32_t rubraview_font_weight_at(int32_t index) {
    if (index < 0) index = 0;
    if (index >= RUBRAVIEW_FONT_WEIGHTS) index = RUBRAVIEW_FONT_WEIGHTS - 1;
    return WEIGHTS[index].weight;
}

static int32_t nearest(int32_t weight) {
    int32_t best = 3;
    for (int32_t i = 0; i < RUBRAVIEW_FONT_WEIGHTS; ++i) {
        int32_t d = WEIGHTS[i].weight - weight, b = WEIGHTS[best].weight - weight;
        if ((d < 0 ? -d : d) < (b < 0 ? -b : b)) best = i;
    }
    return best;
}

const char *rubraview_font_weight_name(int32_t weight) {
    return WEIGHTS[nearest(weight)].name;
}


static bool same_word(proven_u8str_view_t a, const char *b) {
    size_t n = strlen(b);
    if (a.size != n) return false;
    for (size_t i = 0; i < n; ++i) {
        char x = a.ptr[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return false;
    }
    return true;
}

/* The part after the last comma, and what comes before it. */
static bool last_part(proven_u8str_view_t t, proven_u8str_view_t *head, proven_u8str_view_t *tail) {
    for (size_t i = t.size; i-- > 0;) {
        if (t.ptr[i] != ',') continue;
        *head = proven_u8str_view_trim((proven_u8str_view_t){ t.ptr, i });
        *tail = proven_u8str_view_trim((proven_u8str_view_t){ t.ptr + i + 1, t.size - i - 1 });
        return true;
    }
    return false;
}

rubraview_font_spec_t rubraview_font_spec_parse(proven_u8str_view_t text) {
    rubraview_font_spec_t spec = { .family = proven_u8str_view_trim(text), .weight = 400, .italic = false };
    proven_u8str_view_t head, tail;
    if (last_part(spec.family, &head, &tail) && (same_word(tail, "Italic") || same_word(tail, "Oblique"))) {
        spec.italic = true;
        spec.family = head;
    }
    if (last_part(spec.family, &head, &tail)) {
        for (int32_t i = 0; i < RUBRAVIEW_FONT_WEIGHTS; ++i) {
            if (!same_word(tail, WEIGHTS[i].name)) continue;
            spec.weight = WEIGHTS[i].weight;
            spec.family = head;
            break;
        }
    }
    return spec;
}

proven_u8str_view_t rubraview_font_spec_format(char *buf, size_t cap, proven_u8str_view_t family, int32_t weight, bool italic) {
    if (!buf || cap == 0) return PROVEN_LIT("");
    family = proven_u8str_view_trim(family);
    int32_t w = WEIGHTS[nearest(weight)].weight;
    int n = family.size == 0 ? 0
          : snprintf(buf, cap, "%.*s%s%s%s", (int)family.size, family.ptr,
                     w != 400 ? ", " : "", w != 400 ? rubraview_font_weight_name(w) : "", italic ? ", Italic" : "");
    if (n < 0 || (size_t)n >= cap) n = 0;
    buf[n] = '\0';
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)buf, .size = (size_t)n };
}
