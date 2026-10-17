#include "rubraview/batch.h"
#include "rubraview/glob.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

bool rubraview_batch_file_matches(const rubraview_batch_job_t *job, u8str_t filename, uint64_t file_size) {
    if (!job) return false;

    if (job->include_pattern.len > 0 && !rubraview_glob_match_list(filename, job->include_pattern)) {
        return false;
    }
    if (job->exclude_pattern.len > 0 && rubraview_glob_match_list(filename, job->exclude_pattern)) {
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

typedef struct byte_buf {
    uint8_t *data;
    size_t len, cap;
} byte_buf_t;

static bool byte_buf_reserve(proven_arena_t *arena, byte_buf_t *b, size_t extra) {
    if (b->len + extra <= b->cap) return true;
    size_t new_cap = b->cap == 0 ? 128 : b->cap * 2;
    while (new_cap < b->len + extra) new_cap *= 2;

    proven_result_mem_mut_t res = proven_arena_alloc(arena, new_cap);
    if (!proven_is_ok(res.err)) return false;
    if (b->data && b->len > 0) memcpy(res.value.ptr, b->data, b->len);
    b->data = res.value.ptr;
    b->cap = new_cap;
    return true;
}

static bool byte_buf_append(proven_arena_t *arena, byte_buf_t *b, const char *s, size_t n) {
    if (n == 0) return true;
    if (!byte_buf_reserve(arena, b, n)) return false;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    return true;
}

static bool byte_buf_append_str(proven_arena_t *arena, byte_buf_t *b, u8str_t s) {
    return byte_buf_append(arena, b, s.ptr, s.len);
}

u8str_t rubraview_batch_format_name(proven_arena_t *arena, u8str_t pattern, u8str_t name_stem, u8str_t ext, int32_t width, int32_t height, u8str_t date_str) {
    return rubraview_batch_format_name_ex(arena, pattern, name_stem, ext, width, height, date_str, 1, (u8str_t){ .ptr = "", .len = 0 });
}

u8str_t rubraview_batch_format_name_ex(proven_arena_t *arena, u8str_t pattern, u8str_t name_stem, u8str_t ext,
                                       int32_t width, int32_t height, u8str_t date_str, int64_t counter, u8str_t parent) {
    if (!arena) return (u8str_t){ .ptr = "", .len = 0 };

    /* rubraview_path_ext keeps the leading dot; the {ext} token does not. */
    u8str_t ext_no_dot = ext;
    if (ext_no_dot.len > 0 && ext_no_dot.ptr[0] == '.') {
        ext_no_dot.ptr += 1;
        ext_no_dot.len -= 1;
    }

    byte_buf_t buf = {0};
    size_t i = 0;
    while (i < pattern.len) {
        if (pattern.ptr[i] != '{') {
            byte_buf_append(arena, &buf, pattern.ptr + i, 1);
            i++;
            continue;
        }

        size_t close = i + 1;
        while (close < pattern.len && pattern.ptr[close] != '}') close++;
        if (close >= pattern.len) {
            /* No matching '}': copy the rest through unchanged. */
            byte_buf_append(arena, &buf, pattern.ptr + i, pattern.len - i);
            break;
        }

        const char *tok = pattern.ptr + i + 1;
        size_t tok_len = close - i - 1;

        if (rubraview_u8_eq_lit((u8str_t){ .ptr = tok, .len = tok_len }, "name")) {
            byte_buf_append_str(arena, &buf, name_stem);
        } else if (rubraview_u8_eq_lit((u8str_t){ .ptr = tok, .len = tok_len }, "ext")) {
            byte_buf_append_str(arena, &buf, ext_no_dot);
        } else if (tok_len == 1 && tok[0] == 'w') {
            char num[16];
            int n = snprintf(num, sizeof(num), "%d", width);
            if (n > 0) byte_buf_append(arena, &buf, num, (size_t)n);
        } else if (tok_len == 1 && tok[0] == 'h') {
            char num[16];
            int n = snprintf(num, sizeof(num), "%d", height);
            if (n > 0) byte_buf_append(arena, &buf, num, (size_t)n);
        } else if (rubraview_u8_eq_lit((u8str_t){ .ptr = tok, .len = tok_len }, "date")) {
            byte_buf_append_str(arena, &buf, date_str);
        } else if (rubraview_u8_eq_lit((u8str_t){ .ptr = tok, .len = tok_len }, "parent")) {
            byte_buf_append_str(arena, &buf, parent);
        } else if (tok_len >= 1 && tok[0] == 'n' && (tok_len == 1 || (tok_len == 3 && tok[1] == ':' && tok[2] >= '1' && tok[2] <= '9'))) {
            /* RV-129: the counter, {n:3} padded with zeros to three digits */
            char num[32];
            int pad = tok_len == 3 ? tok[2] - '0' : 0;
            int n = snprintf(num, sizeof(num), "%0*lld", pad, (long long)counter);
            if (n > 0) byte_buf_append(arena, &buf, num, (size_t)n);
        } else {
            /* Unrecognized token: copy the whole "{...}" through unchanged. */
            byte_buf_append(arena, &buf, pattern.ptr + i, close - i + 1);
        }

        i = close + 1;
    }

    proven_result_mem_mut_t res = proven_arena_alloc(arena, buf.len + 1);
    if (!proven_is_ok(res.err)) return (u8str_t){ .ptr = "", .len = 0 };
    if (buf.len > 0) memcpy(res.value.ptr, buf.data, buf.len);
    res.value.ptr[buf.len] = '\0';

    return (u8str_t){ .ptr = (const char*)res.value.ptr, .len = buf.len };
}

/* ---- RV-129 ---- */

u8str_t rubraview_batch_replace_all(proven_arena_t *arena, u8str_t text, u8str_t from, u8str_t to) {
    if (!arena || from.len == 0 || text.len < from.len) return text;
    byte_buf_t buf = {0};
    bool any = false;
    for (size_t i = 0; i < text.len;) {
        if (i + from.len <= text.len && memcmp(text.ptr + i, from.ptr, from.len) == 0) {
            byte_buf_append_str(arena, &buf, to);
            i += from.len;
            any = true;
        } else {
            byte_buf_append(arena, &buf, text.ptr + i, 1);
            ++i;
        }
    }
    if (!any) return text;
    proven_result_mem_mut_t res = proven_arena_alloc(arena, buf.len + 1);
    if (!proven_is_ok(res.err)) return text;
    if (buf.len) memcpy(res.value.ptr, buf.data, buf.len);
    res.value.ptr[buf.len] = '\0';
    return (u8str_t){ .ptr = (const char*)res.value.ptr, .len = buf.len };
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
    if (!arena || !dst || !dst->pixels || !mark || !mark->pixels || mark->width <= 0 || mark->height <= 0) return false;
    if (dst->format != mark->format || (dst->format != RUBRAVIEW_PIXFMT_RGBA8 && dst->format != RUBRAVIEW_PIXFMT_BGRA8)) return false;
    if (opacity < 1) opacity = 1;
    if (opacity > 100) opacity = 100;
    rubraview_pixbuf_t m = *mark;
    int32_t most = dst->width / 4 > 1 ? dst->width / 4 : 1;
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
