#include "rubraview/ffilter.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- sorting and groups ---- */

static int by_name(const void *a, const void *b) {
    const rubraview_ffilter_t *x = (const rubraview_ffilter_t *)a, *y = (const rubraview_ffilter_t *)b;
    size_t n = x->name.size < y->name.size ? x->name.size : y->name.size;
    int c = n ? memcmp(x->name.ptr, y->name.ptr, n) : 0;
    return c ? c : (x->name.size > y->name.size) - (x->name.size < y->name.size);
}

void rubraview_ffilter_catalog_sort(rubraview_ffilter_catalog_t *catalog) {
    if (catalog && catalog->filters && catalog->filter_count > 1)
        qsort(catalog->filters, (size_t)catalog->filter_count, sizeof(catalog->filters[0]), by_name);
}

static int first_letter(const rubraview_ffilter_t *f) {
    return f->name.size ? rubraview_ascii_lower(f->name.ptr[0]) : 0;
}

int32_t rubraview_ffilter_groups(const rubraview_ffilter_catalog_t *catalog, int32_t size, int32_t *first, int32_t cap) {
    if (!catalog || !first || cap <= 0 || catalog->filter_count <= 0) return 0;
    if (size < 1) size = 1;
    /* fewer, larger groups when the list would need more than there is room for */
    while ((catalog->filter_count + size - 1) / size > cap) size *= 2;
    int32_t groups = 0, at = 0;
    first[groups++] = 0;
    for (int32_t i = 1; i < catalog->filter_count; ++i) {
        bool full = i - at >= size;
        bool letter = first_letter(&catalog->filters[i]) != first_letter(&catalog->filters[i - 1]);
        /* past the size, the next letter starts a group; a letter far too long is cut where it is */
        if ((full && letter) || i - at >= size * 2) {
            if (groups == cap) break;
            first[groups++] = at = i;
        }
    }
    return groups;
}

void rubraview_ffilter_group_name(const rubraview_ffilter_catalog_t *catalog, const int32_t *first, int32_t groups,
                                  int32_t g, char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!catalog || !first || g < 0 || g >= groups) return;
    int32_t a = first[g], b = (g + 1 < groups ? first[g + 1] : catalog->filter_count) - 1;
    if (a < 0 || b < a || b >= catalog->filter_count) return;
    int x = first_letter(&catalog->filters[a]), y = first_letter(&catalog->filters[b]);
    if (x == y) {
        /* one letter in two groups or more: by the names' beginnings */
        const rubraview_ffilter_t *fa = &catalog->filters[a], *fb = &catalog->filters[b];
        snprintf(out, cap, "%.*s \xE2\x80\x93 %.*s", (int)(fa->name.size < 4 ? fa->name.size : 4), (const char *)fa->name.ptr,
                 (int)(fb->name.size < 4 ? fb->name.size : 4), (const char *)fb->name.ptr);
    } else {
        snprintf(out, cap, "%c \xE2\x80\x93 %c", x, y);
    }
}

/* ---- a filter's rows as a graph ---- */

typedef struct out_buf {
    char *p;
    size_t n, cap;
    bool full;
} out_buf_t;

static void put_byte(out_buf_t *o, char c) {
    if (o->full || o->n + 1 >= o->cap) { o->full = true; return; }
    o->p[o->n++] = c;
    o->p[o->n] = '\0';
}

/* One byte of an option's value: escaped for the filter's options, then that for the graph. */
static void put_value_byte(out_buf_t *o, unsigned char c) {
    if (c < 0x20 || c == 0x7F) c = ' ';
    bool option = c == '\\' || c == '\'' || c == ':';
    if (option) { put_byte(o, '\\'); put_byte(o, '\\'); }   /* the option's `\`, escaped for the graph */
    if (c == '\\' || c == '\'' || c == '[' || c == ']' || c == ',' || c == ';') put_byte(o, '\\');
    put_byte(o, (char)c);
}

static void put_value(out_buf_t *o, proven_u8str_view_t v) {
    for (size_t i = 0; i < v.size; ++i) put_value_byte(o, v.ptr[i]);
}

static bool is_word_byte(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static bool is_word(proven_u8str_view_t w) {
    if (w.size == 0) return false;
    for (size_t i = 0; i < w.size; ++i) if (!is_word_byte(w.ptr[i])) return false;
    return true;
}

size_t rubraview_ffilter_graph(const rubraview_ffilter_t *filter, const double *values, const proven_u8str_view_t *texts,
                               char *out, size_t cap) {
    if (!filter || !out || cap == 0) return 0;
    out[0] = '\0';
    if (!is_word(filter->name)) return 0;
    out_buf_t o = { .p = out, .cap = cap };
    for (size_t i = 0; i < filter->name.size; ++i) put_byte(&o, (char)filter->name.ptr[i]);
    int32_t written = 0;
    for (int32_t i = 0; i < filter->option_count && i < RUBRAVIEW_FFILTER_MAX_SHOWN; ++i) {
        const rubraview_ffilter_option_t *p = &filter->options[i];
        if (!is_word(p->name)) continue;
        double v = values ? values[i] : p->value;
        if (!isfinite(v)) v = p->value;
        char num[64];
        proven_u8str_view_t value = { 0 };
        switch (p->kind) {
            case RUBRAVIEW_FFILTER_INT: {
                long long r = llround(fmin(fmax(v, p->min), p->max)), d = llround(p->value);
                if (r == d) continue;
                snprintf(num, sizeof(num), "%lld", r);
                value = proven_u8str_view_from_cstr(num);
                break;
            }
            case RUBRAVIEW_FFILTER_FLOAT: {
                double r = fmin(fmax(v, p->min), p->max);
                if (fabs(r - p->value) <= 1e-9 * fmax(1.0, fabs(p->value))) continue;
                snprintf(num, sizeof(num), "%.9g", r);
                value = proven_u8str_view_from_cstr(num);
                break;
            }
            case RUBRAVIEW_FFILTER_BOOL:
                if ((v > 0.5) == (p->value > 0.5)) continue;
                value = v > 0.5 ? PROVEN_LIT("1") : PROVEN_LIT("0");
                break;
            case RUBRAVIEW_FFILTER_CHOICE: {
                int32_t k = (int32_t)fmin(fmax(floor(v), 0.0), (double)(p->choice_count > 0 ? p->choice_count - 1 : 0));
                if (p->choice_count <= 0 || k == (int32_t)floor(p->value) || !is_word(p->choices[k])) continue;
                value = p->choices[k];
                break;
            }
            default: {   /* text: as typed, when it is not the default */
                proven_u8str_view_t t = texts && texts[i].ptr ? proven_u8str_view_trim(texts[i]) : (proven_u8str_view_t){ 0 };
                if (t.size == 0 || proven_u8str_view_eq(t, p->text)) continue;
                value = t;
                break;
            }
        }
        put_byte(&o, written++ ? ':' : '=');
        for (size_t k = 0; k < p->name.size; ++k) put_byte(&o, (char)p->name.ptr[k]);
        put_byte(&o, '=');
        put_value(&o, value);
    }
    if (o.full) { out[0] = '\0'; return 0; }
    return o.n;
}

/* ---- names a graph may not use ---- */

static const char *const REFUSED[] = { "movie", "amovie", "zmq", "azmq", "frei0r", "frei0r_src", "ladspa", "lv2" };

const char *rubraview_ffilter_refused(proven_u8str_view_t graph) {
    char word[32];
    size_t n = 0;
    bool long_word = false;
    for (size_t i = 0; i <= graph.size; ++i) {
        unsigned char c = i < graph.size ? graph.ptr[i] : ' ';
        if (c == '\'' || c == '\\') continue;   /* FFmpeg takes these out as it reads: `mo'v'ie` is `movie` */
        if (is_word_byte(c)) {
            if (n + 1 < sizeof(word)) word[n++] = (char)c;
            else long_word = true;
            continue;
        }
        if (n && !long_word) {
            word[n] = '\0';
            for (size_t k = 0; k < sizeof(REFUSED) / sizeof(REFUSED[0]); ++k)
                if (strcmp(word, REFUSED[k]) == 0) return REFUSED[k];
        }
        n = 0;
        long_word = false;
    }
    return NULL;
}
