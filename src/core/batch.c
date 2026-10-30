#include "rubraview/batch.h"
#include "rubraview/glob.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

bool rubraview_batch_file_matches(const rubraview_batch_job_t *job, proven_u8str_view_t filename, uint64_t file_size) {
    if (!job) return false;

    if (job->include_pattern.size > 0 && !rubraview_glob_match_list(filename, job->include_pattern)) {
        return false;
    }
    if (job->exclude_pattern.size > 0 && rubraview_glob_match_list(filename, job->exclude_pattern)) {
        return false;
    }
    if (job->min_size_bytes > 0 && file_size < job->min_size_bytes) {
        return false;
    }
    if (job->max_size_bytes > 0 && file_size > job->max_size_bytes) {
        return false;
    }
    return true;
}

proven_u8str_view_t rubraview_batch_format_name(proven_arena_t *arena, proven_u8str_view_t pattern, proven_u8str_view_t name_stem, proven_u8str_view_t ext, int32_t width, int32_t height, proven_u8str_view_t date_str) {
    return rubraview_batch_format_name_ex(arena, pattern, name_stem, ext, width, height, date_str, 1, PROVEN_LIT(""));
}

proven_u8str_view_t rubraview_batch_format_name_ex(proven_arena_t *arena, proven_u8str_view_t pattern, proven_u8str_view_t name_stem, proven_u8str_view_t ext,
                                       int32_t width, int32_t height, proven_u8str_view_t date_str, int64_t counter, proven_u8str_view_t parent) {
    rubraview_batch_name_facts_t facts = { .stem = name_stem, .ext = ext, .parent = parent, .date = date_str,
                                           .width = width, .height = height, .counter = counter, .taken = 0 };
    return rubraview_batch_format_name_facts(arena, pattern, &facts);
}

/* RV-194: {taken:FORMAT} — yyyy, yy, mm, dd, hh, mi, ss from yyyymmddhhmmss; the rest copied */
static void append_taken(proven_arena_t *arena, proven_u8str_t *buf, int64_t taken, const char *format, size_t len) {
    if (taken <= 0) return;
    int part[6] = { (int)(taken / 10000000000LL), (int)(taken / 100000000LL % 100), (int)(taken / 1000000LL % 100),
                    (int)(taken / 10000LL % 100), (int)(taken / 100LL % 100), (int)(taken % 100) };
    for (size_t i = 0; i < len;) {
        char num[8];
        int n = -1, used = 0;
        if (i + 4 <= len && memcmp(format + i, "yyyy", 4) == 0) { n = snprintf(num, sizeof(num), "%04d", part[0]); used = 4; }
        else if (i + 2 <= len && memcmp(format + i, "yy", 2) == 0) { n = snprintf(num, sizeof(num), "%02d", part[0] % 100); used = 2; }
        else if (i + 2 <= len && memcmp(format + i, "mm", 2) == 0) { n = snprintf(num, sizeof(num), "%02d", part[1]); used = 2; }
        else if (i + 2 <= len && memcmp(format + i, "dd", 2) == 0) { n = snprintf(num, sizeof(num), "%02d", part[2]); used = 2; }
        else if (i + 2 <= len && memcmp(format + i, "hh", 2) == 0) { n = snprintf(num, sizeof(num), "%02d", part[3]); used = 2; }
        else if (i + 2 <= len && memcmp(format + i, "mi", 2) == 0) { n = snprintf(num, sizeof(num), "%02d", part[4]); used = 2; }
        else if (i + 2 <= len && memcmp(format + i, "ss", 2) == 0) { n = snprintf(num, sizeof(num), "%02d", part[5]); used = 2; }
        if (n > 0) { rubraview_text_append(arena, buf, num, (size_t)n); i += (size_t)used; }
        else { rubraview_text_append(arena, buf, format + i, 1); i++; }
    }
}

proven_u8str_view_t rubraview_batch_format_name_facts(proven_arena_t *arena, proven_u8str_view_t pattern, const rubraview_batch_name_facts_t *facts) {
    if (!arena || !facts) return PROVEN_LIT("");
    proven_u8str_view_t name_stem = facts->stem, ext = facts->ext, date_str = facts->date, parent = facts->parent;
    int32_t width = facts->width, height = facts->height;
    int64_t counter = facts->counter;

    /* rubraview_path_ext keeps the leading dot; the {ext} token does not. */
    proven_u8str_view_t ext_no_dot = ext;
    if (ext_no_dot.size > 0 && ext_no_dot.ptr[0] == '.') {
        ext_no_dot.ptr += 1;
        ext_no_dot.size -= 1;
    }

    proven_u8str_t buf = {0};
    size_t i = 0;
    while (i < pattern.size) {
        if (pattern.ptr[i] != '{') {
            rubraview_text_append(arena, &buf, (const char *)pattern.ptr + i, 1);
            i++;
            continue;
        }

        size_t close = i + 1;
        while (close < pattern.size && pattern.ptr[close] != '}') close++;
        if (close >= pattern.size) {
            /* No matching '}': copy the rest through unchanged. */
            rubraview_text_append(arena, &buf, (const char *)pattern.ptr + i, pattern.size - i);
            break;
        }

        const char *tok = (const char *)pattern.ptr + i + 1;
        size_t tok_len = close - i - 1;

        if (proven_u8str_view_eq((proven_u8str_view_t){ .ptr = (const proven_byte_t *)tok, .size = tok_len }, PROVEN_LIT("name"))) {
            rubraview_text_append(arena, &buf, (name_stem).ptr, (name_stem).size);
        } else if (proven_u8str_view_eq((proven_u8str_view_t){ .ptr = (const proven_byte_t *)tok, .size = tok_len }, PROVEN_LIT("ext"))) {
            rubraview_text_append(arena, &buf, (ext_no_dot).ptr, (ext_no_dot).size);
        } else if (tok_len == 1 && (tok[0] == 'w' || tok[0] == 'h') && (tok[0] == 'w' ? width : height) < 0) {
            rubraview_text_append(arena, &buf, (const char *)pattern.ptr + i, close - i + 1);   /* RV-194: filled in once known */
        } else if (tok_len >= 5 && memcmp(tok, "taken", 5) == 0 && (tok_len == 5 || tok[5] == ':')) {
            if (tok_len == 5) append_taken(arena, &buf, facts->taken, "yyyy-mm-dd", 10);
            else append_taken(arena, &buf, facts->taken, tok + 6, tok_len - 6);
        } else if (tok_len == 1 && tok[0] == 'w') {
            char num[16];
            int n = snprintf(num, sizeof(num), "%d", width);
            if (n > 0) rubraview_text_append(arena, &buf, num, (size_t)n);
        } else if (tok_len == 1 && tok[0] == 'h') {
            char num[16];
            int n = snprintf(num, sizeof(num), "%d", height);
            if (n > 0) rubraview_text_append(arena, &buf, num, (size_t)n);
        } else if (proven_u8str_view_eq((proven_u8str_view_t){ .ptr = (const proven_byte_t *)tok, .size = tok_len }, PROVEN_LIT("date"))) {
            rubraview_text_append(arena, &buf, (date_str).ptr, (date_str).size);
        } else if (proven_u8str_view_eq((proven_u8str_view_t){ .ptr = (const proven_byte_t *)tok, .size = tok_len }, PROVEN_LIT("parent"))) {
            rubraview_text_append(arena, &buf, (parent).ptr, (parent).size);
        } else if (tok_len >= 1 && tok[0] == 'n' && (tok_len == 1 || (tok_len == 3 && tok[1] == ':' && tok[2] >= '1' && tok[2] <= '9'))) {
            /* RV-129: the counter, {n:3} padded with zeros to three digits */
            char num[32];
            int pad = tok_len == 3 ? tok[2] - '0' : 0;
            int n = snprintf(num, sizeof(num), "%0*lld", pad, (long long)counter);
            if (n > 0) rubraview_text_append(arena, &buf, num, (size_t)n);
        } else {
            /* Unrecognized token: copy the whole "{...}" through unchanged. */
            rubraview_text_append(arena, &buf, (const char *)pattern.ptr + i, close - i + 1);
        }

        i = close + 1;
    }

    return rubraview_text_view(&buf);
}

/* ---- RV-194 ---- */

int64_t rubraview_batch_taken_from_text(proven_u8str_view_t text) {
    /* yyyy?mm?dd?hh?mi?ss: 19 characters, digits where digits go */
    if (text.size < 19) return 0;
    static const int AT[14] = { 0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18 };
    int64_t v = 0;
    for (int i = 0; i < 14; ++i) {
        char c = (char)text.ptr[AT[i]];
        if (c < '0' || c > '9') return 0;
        v = v * 10 + (c - '0');
    }
    return v / 10000000000LL >= 1 ? v : 0;   /* a year of 0000 is "not set" */
}

proven_u8str_view_t rubraview_batch_compose_pattern(proven_arena_t *arena, const rubraview_batch_name_parts_t *parts) {
    if (!arena || !parts) return PROVEN_LIT("");
    static const char *const TAKEN[4] = { "{taken:yyyy-mm-dd}", "{taken:yyyymmdd}", "{taken:yyyy-mm-dd_hhmiss}", "{taken:yymmdd}" };
    proven_u8str_t buf = {0};
    rubraview_text_append(arena, &buf, (const char *)parts->before.ptr, parts->before.size);
    bool any = false;
    char counter[8];
    snprintf(counter, sizeof(counter), parts->digits > 1 && parts->digits <= 9 ? "{n:%d}" : "{n}", (int)parts->digits);
    const char *middle[5] = { parts->parent ? "{parent}" : NULL, parts->name ? "{name}" : NULL, parts->counter ? counter : NULL,
                              parts->size ? "{w}x{h}" : NULL, parts->taken ? TAKEN[(unsigned)parts->taken_form % 4u] : NULL };
    for (int i = 0; i < 5; ++i) {
        if (!middle[i]) continue;
        if (any) rubraview_text_append(arena, &buf, (const char *)parts->sep.ptr, parts->sep.size);
        rubraview_text_append(arena, &buf, middle[i], strlen(middle[i]));
        any = true;
    }
    rubraview_text_append(arena, &buf, (const char *)parts->after.ptr, parts->after.size);
    if (!any && parts->before.size == 0 && parts->after.size == 0) return PROVEN_LIT("{name}.{ext}");
    rubraview_text_append(arena, &buf, ".{ext}", 6);
    return rubraview_text_view(&buf);
}

/* ---- RV-129 ---- */

proven_u8str_view_t rubraview_batch_replace_all(proven_arena_t *arena, proven_u8str_view_t text, proven_u8str_view_t from, proven_u8str_view_t to) {
    if (!arena || from.size == 0 || text.size < from.size) return text;
    proven_u8str_t buf = {0};
    bool any = false;
    for (size_t i = 0; i < text.size;) {
        if (i + from.size <= text.size && memcmp(text.ptr + i, from.ptr, from.size) == 0) {
            rubraview_text_append(arena, &buf, (to).ptr, (to).size);
            i += from.size;
            any = true;
        } else {
            rubraview_text_append(arena, &buf, (const char *)text.ptr + i, 1);
            ++i;
        }
    }
    if (!any) return text;
    return rubraview_text_view(&buf);
}

void rubraview_batch_resize_target(const rubraview_batch_resize_params_t *r, int32_t src_w, int32_t src_h,
                                   int32_t *out_w, int32_t *out_h) {
    double w = src_w, h = src_h;
    if (r && src_w > 0 && src_h > 0) {
        double s = 1.0;
        bool by_scale = true;
        switch (r->mode) {
            case RUBRAVIEW_RESIZE_PERCENT: s = r->value_a / 100.0; break;
            case RUBRAVIEW_RESIZE_BOUNDING_BOX: {
                double sx = r->value_a / src_w, sy = r->value_b / src_h;
                s = sx < sy ? sx : sy;
                if (s > 1.0) s = 1.0;   /* a bounding box shrinks; it does not enlarge */
                break;
            }
            case RUBRAVIEW_RESIZE_FIXED_WIDTH:  s = r->value_a / src_w; break;
            case RUBRAVIEW_RESIZE_FIXED_HEIGHT: s = r->value_a / src_h; break;
            case RUBRAVIEW_RESIZE_LONG_SIDE:    s = r->value_a / (src_w > src_h ? src_w : src_h); break;
            case RUBRAVIEW_RESIZE_SHORT_SIDE:   s = r->value_a / (src_w < src_h ? src_w : src_h); break;
            case RUBRAVIEW_RESIZE_MEGAPIXELS:   s = sqrt(r->value_a * 1e6 / ((double)src_w * (double)src_h)); break;
            default: by_scale = false; break;
        }
        if (by_scale && s > 0.0) {
            if (r->no_enlarge && s > 1.0) s = 1.0;
            if (r->mode == RUBRAVIEW_RESIZE_FIXED_WIDTH && s != 1.0) { w = r->value_a; h = src_h * s; }
            else if (r->mode == RUBRAVIEW_RESIZE_FIXED_HEIGHT && s != 1.0) { h = r->value_a; w = src_w * s; }
            else { w = src_w * s; h = src_h * s; }
        }
    }
    long rw = lround(w), rh = lround(h);
    *out_w = rw < 1 ? 1 : rw > 65535 ? 65535 : (int32_t)rw;
    *out_h = rh < 1 ? 1 : rh > 65535 ? 65535 : (int32_t)rh;
}

static void fill_rgb(rubraview_pixbuf_t *p, uint32_t rgb) {
    uint8_t r = (uint8_t)(rgb >> 16), g = (uint8_t)(rgb >> 8), b = (uint8_t)rgb;
    bool bgra = p->format == RUBRAVIEW_PIXFMT_BGRA8;
    for (int32_t y = 0; y < p->height; ++y) {
        uint8_t *q = p->pixels + (size_t)y * (size_t)p->stride;
        for (int32_t x = 0; x < p->width; ++x, q += 4) { q[0] = bgra ? b : r; q[1] = g; q[2] = bgra ? r : b; q[3] = 255; }
    }
}

static rubraview_pixbuf_t place_on(proven_arena_t *arena, const rubraview_pixbuf_t *src, int32_t w, int32_t h, uint32_t rgb) {
    rubraview_pixbuf_t none = {0};
    if (!arena || !src || !src->pixels || (src->format != RUBRAVIEW_PIXFMT_RGBA8 && src->format != RUBRAVIEW_PIXFMT_BGRA8)) return none;
    if (w < src->width) w = src->width;
    if (h < src->height) h = src->height;
    if (w > 65535 || h > 65535) return none;
    rubraview_pixbuf_t out = rubraview_pixbuf_create(arena, w, h, src->format);
    if (!out.pixels) return none;
    fill_rgb(&out, rgb);
    int32_t ox = (w - src->width) / 2, oy = (h - src->height) / 2;
    for (int32_t y = 0; y < src->height; ++y)
        memcpy(out.pixels + (size_t)(y + oy) * (size_t)out.stride + (size_t)ox * 4u,
               src->pixels + (size_t)y * (size_t)src->stride, (size_t)src->width * 4u);
    return out;
}

rubraview_pixbuf_t rubraview_batch_border(proven_arena_t *arena, const rubraview_pixbuf_t *src, int32_t px, uint32_t rgb) {
    if (!src || px <= 0 || px > 10000) return src ? *src : (rubraview_pixbuf_t){0};
    return place_on(arena, src, src->width + 2 * px, src->height + 2 * px, rgb);
}

rubraview_pixbuf_t rubraview_batch_canvas(proven_arena_t *arena, const rubraview_pixbuf_t *src, int32_t w, int32_t h, uint32_t rgb) {
    return place_on(arena, src, w, h, rgb);
}

bool rubraview_batch_watermark(proven_arena_t *arena, rubraview_pixbuf_t *dst, const rubraview_pixbuf_t *mark,
                               rubraview_batch_corner_t corner, int32_t opacity) {
    return rubraview_batch_overlay(arena, dst, mark, corner, opacity, dst ? dst->width / 4 : 0);
}

bool rubraview_batch_overlay(proven_arena_t *arena, rubraview_pixbuf_t *dst, const rubraview_pixbuf_t *mark,
                             rubraview_batch_corner_t corner, int32_t opacity, int32_t max_width) {
    if (!arena || !dst || !dst->pixels || !mark || !mark->pixels || mark->width <= 0 || mark->height <= 0) return false;
    if (dst->format != mark->format || (dst->format != RUBRAVIEW_PIXFMT_RGBA8 && dst->format != RUBRAVIEW_PIXFMT_BGRA8)) return false;
    if (opacity < 1) opacity = 1;
    if (opacity > 100) opacity = 100;
    rubraview_pixbuf_t m = *mark;
    int32_t most = max_width > 1 ? max_width : 1;
    if (m.width > most) {
        int32_t mh = (int32_t)lround((double)m.height * most / m.width);
        m = rubraview_pixbuf_resample(arena, mark, most, mh > 0 ? mh : 1, RUBRAVIEW_FILTER_BICUBIC);
        if (!m.pixels) return false;
    }
    int32_t shorter = dst->width < dst->height ? dst->width : dst->height;
    int32_t margin = shorter / 50;
    int32_t x0, y0;
    switch (corner) {
        case RUBRAVIEW_CORNER_TOP_LEFT:     x0 = margin; y0 = margin; break;
        case RUBRAVIEW_CORNER_TOP_RIGHT:    x0 = dst->width - m.width - margin; y0 = margin; break;
        case RUBRAVIEW_CORNER_BOTTOM_LEFT:  x0 = margin; y0 = dst->height - m.height - margin; break;
        case RUBRAVIEW_CORNER_CENTRE:       x0 = (dst->width - m.width) / 2; y0 = (dst->height - m.height) / 2; break;
        default:                            x0 = dst->width - m.width - margin; y0 = dst->height - m.height - margin; break;
    }
    for (int32_t y = 0; y < m.height; ++y) {
        int32_t dy = y0 + y;
        if (dy < 0 || dy >= dst->height) continue;
        const uint8_t *s = m.pixels + (size_t)y * (size_t)m.stride;
        uint8_t *d = dst->pixels + (size_t)dy * (size_t)dst->stride;
        for (int32_t x = 0; x < m.width; ++x) {
            int32_t dx = x0 + x;
            if (dx < 0 || dx >= dst->width) continue;
            uint32_t a = (uint32_t)s[(size_t)x * 4u + 3] * (uint32_t)opacity;   /* 0 .. 25500 */
            uint8_t *q = d + (size_t)dx * 4u;
            for (int c = 0; c < 3; ++c) q[c] = (uint8_t)((q[c] * (25500u - a) + s[(size_t)x * 4u + c] * a + 12750u) / 25500u);
        }
    }
    return true;
}

rubraview_pixbuf_t rubraview_batch_text_layer(proven_arena_t *arena, const rubraview_pixbuf_t *mask, uint32_t rgb,
                                              rubraview_pixel_format_t format) {
    rubraview_pixbuf_t none = {0};
    if (!arena || !mask || !mask->pixels || mask->format != RUBRAVIEW_PIXFMT_GRAY8 ||
        (format != RUBRAVIEW_PIXFMT_RGBA8 && format != RUBRAVIEW_PIXFMT_BGRA8)) return none;
    int32_t pad = mask->height / 12 + 1;   /* room for the shadow */
    int32_t w = mask->width + 2 * pad, h = mask->height + 2 * pad;
    rubraview_pixbuf_t out = rubraview_pixbuf_create(arena, w, h, format);
    if (!out.pixels) return none;
    memset(out.pixels, 0, (size_t)out.stride * (size_t)h);
    uint8_t r = (uint8_t)(rgb >> 16), g = (uint8_t)(rgb >> 8), b = (uint8_t)rgb;
    bool bgra = format == RUBRAVIEW_PIXFMT_BGRA8;
    int32_t off = pad / 2 > 0 ? pad / 2 : 1;
    /* The shadow: the mask spread a little and shifted down-right, black at half strength. */
    for (int32_t y = 0; y < h; ++y) {
        uint8_t *q = out.pixels + (size_t)y * (size_t)out.stride;
        for (int32_t x = 0; x < w; ++x, q += 4) {
            int32_t best = 0;
            for (int32_t dy = -1; dy <= 1; ++dy)
                for (int32_t dx = -1; dx <= 1; ++dx) {
                    int32_t mx = x - pad - off + dx, my = y - pad - off + dy;
                    if (mx < 0 || my < 0 || mx >= mask->width || my >= mask->height) continue;
                    int32_t v = mask->pixels[(size_t)my * (size_t)mask->stride + (size_t)mx];
                    if (v > best) best = v;
                }
            q[3] = (uint8_t)(best / 2);
        }
    }
    /* The letters over it: their colour, at their coverage; the shadow under them shows through the rest. */
    for (int32_t y = 0; y < mask->height; ++y) {
        const uint8_t *m = mask->pixels + (size_t)y * (size_t)mask->stride;
        uint8_t *q = out.pixels + (size_t)(y + pad) * (size_t)out.stride + (size_t)pad * 4u;
        for (int32_t x = 0; x < mask->width; ++x, q += 4) {
            uint32_t a = m[x], sa = q[3];
            uint32_t oa = a + sa * (255u - a) / 255u;   /* letters over shadow */
            if (oa == 0) continue;
            uint8_t cr = (uint8_t)(r * a / oa), cg = (uint8_t)(g * a / oa), cb = (uint8_t)(b * a / oa);   /* the shadow is black */
            q[0] = bgra ? cb : cr; q[1] = cg; q[2] = bgra ? cr : cb; q[3] = (uint8_t)oa;
        }
    }
    return out;
}
