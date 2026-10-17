#include "rubraview/batchrun.h"
#include "rubraview/effects.h"
#include "rubraview/filemanage.h"
#include "rubraview/number.h"
#include "rubraview/path.h"
#include <limits.h>
#include <string.h>
#include <stdlib.h>

/* ---- small parsing helpers ---- */

static u8str_t cstr(const char *s) {
    return (u8str_t){ .ptr = s, .len = s ? strlen(s) : 0 };
}

static bool starts_with(u8str_t s, const char *prefix, u8str_t *out_rest) {
    size_t n = strlen(prefix);
    if (s.len < n || memcmp(s.ptr, prefix, n) != 0) return false;
    if (out_rest) *out_rest = (u8str_t){ .ptr = s.ptr + n, .len = s.len - n };
    return true;
}

/* Parses a decimal integer. Refuses anything with a stray character in
   it — "--quality=90abc" is a typo, not the number 90. */
static bool parse_int(u8str_t s, long *out) {
    int64_t value = 0;
    if (!rubraview_parse_i64(s, &value) || value < LONG_MIN || value > LONG_MAX) return false;
    *out = (long)value;
    return true;
}

static bool parse_double(u8str_t s, double *out) {
    return rubraview_parse_double(s, out);
}

static bool parse_filter(u8str_t s, rubraview_resample_filter_t *out) {
    if (rubraview_u8_eq_lit(s, "lanczos3") || rubraview_u8_eq_lit(s, "lanczos")) { *out = RUBRAVIEW_FILTER_LANCZOS3; return true; }
    if (rubraview_u8_eq_lit(s, "bicubic")) { *out = RUBRAVIEW_FILTER_BICUBIC; return true; }
    if (rubraview_u8_eq_lit(s, "bilinear")) { *out = RUBRAVIEW_FILTER_BILINEAR; return true; }
    if (rubraview_u8_eq_lit(s, "nearest")) { *out = RUBRAVIEW_FILTER_NEAREST; return true; }
    return false;
}

/* ---- action list ---- */

/* Actions of the same kind are merged rather than appended, so
   `--exposure=1 --contrast=20` is one colour stage and not two passes
   over the pixels. */
static rubraview_batch_action_t *action_for(rubraview_cli_result_t *result,
                                            rubraview_batch_action_kind_t kind) {
    for (size_t i = 0; i < result->job.action_count; ++i) {
        if (result->actions[i].kind == kind) return &result->actions[i];
    }
    if (result->job.action_count >= RUBRAVIEW_BATCH_MAX_ACTIONS) return NULL;

    rubraview_batch_action_t *action = &result->actions[result->job.action_count++];
    *action = (rubraview_batch_action_t){ .kind = kind };
    if (kind == RUBRAVIEW_BATCH_RESIZE) {
        action->params.resize.filter = RUBRAVIEW_FILTER_LANCZOS3;
    }
    return action;
}

/* ---- --resize= ---- */

static bool parse_resize(u8str_t value, rubraview_batch_resize_params_t *out) {
    /* "50%" */
    if (value.len > 1 && value.ptr[value.len - 1] == '%') {
        double percent = 0.0;
        if (!parse_double((u8str_t){ .ptr = value.ptr, .len = value.len - 1 }, &percent)) return false;
        if (percent <= 0.0) return false;
        out->mode = RUBRAVIEW_RESIZE_PERCENT;
        out->value_a = percent;
        return true;
    }

    /* RV-129: "l1920" the long side, "s1080" the short one, "2mp" (or "2.5mp") megapixels */
    if (value.len > 2 && (value.ptr[value.len - 1] == 'p' || value.ptr[value.len - 1] == 'P') &&
        (value.ptr[value.len - 2] == 'm' || value.ptr[value.len - 2] == 'M')) {
        double mp = 0.0;
        if (!parse_double((u8str_t){ .ptr = value.ptr, .len = value.len - 2 }, &mp) || mp <= 0.0 || mp > 4000.0) return false;
        out->mode = RUBRAVIEW_RESIZE_MEGAPIXELS;
        out->value_a = mp;
        return true;
    }
    if (value.len > 1 && (value.ptr[0] == 'l' || value.ptr[0] == 's')) {
        long n = 0;
        if (!parse_int((u8str_t){ .ptr = value.ptr + 1, .len = value.len - 1 }, &n) || n <= 0 || n > 65535) return false;
        out->mode = value.ptr[0] == 'l' ? RUBRAVIEW_RESIZE_LONG_SIDE : RUBRAVIEW_RESIZE_SHORT_SIDE;
        out->value_a = (double)n;
        return true;
    }

    /* "w1920" / "h1080" */
    if (value.len > 1 && (value.ptr[0] == 'w' || value.ptr[0] == 'h')) {
        long n = 0;
        if (!parse_int((u8str_t){ .ptr = value.ptr + 1, .len = value.len - 1 }, &n) || n <= 0) return false;
        out->mode = value.ptr[0] == 'w' ? RUBRAVIEW_RESIZE_FIXED_WIDTH : RUBRAVIEW_RESIZE_FIXED_HEIGHT;
        out->value_a = (double)n;
        return true;
    }

    /* "1920x1080" */
    for (size_t i = 0; i < value.len; ++i) {
        if (value.ptr[i] != 'x' && value.ptr[i] != 'X') continue;
        long w = 0, h = 0;
        if (!parse_int((u8str_t){ .ptr = value.ptr, .len = i }, &w)) return false;
        if (!parse_int((u8str_t){ .ptr = value.ptr + i + 1, .len = value.len - i - 1 }, &h)) return false;
        if (w <= 0 || h <= 0) return false;
        out->mode = RUBRAVIEW_RESIZE_BOUNDING_BOX;
        out->value_a = (double)w;
        out->value_b = (double)h;
        return true;
    }
    return false;
}

/* ---- RV-129: the extra steps ---- */

/* "RRGGBB" (a # in front allowed) */
static bool parse_rgb(u8str_t v, uint32_t *out) {
    if (v.len > 0 && v.ptr[0] == '#') { v.ptr++; v.len--; }
    if (v.len != 6) return false;
    uint32_t c = 0;
    for (size_t i = 0; i < 6; ++i) {
        char ch = v.ptr[i];
        int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
        if (d < 0) return false;
        c = c << 4 | (uint32_t)d;
    }
    *out = c;
    return true;
}

/* The comma-separated pieces of `v`, at most `cap`; how many there were. */
static size_t split_commas(u8str_t v, u8str_t *parts, size_t cap) {
    size_t n = 0, start = 0;
    for (size_t i = 0; i <= v.len; ++i) {
        if (i < v.len && v.ptr[i] != ',') continue;
        if (n < cap) parts[n] = (u8str_t){ .ptr = v.ptr + start, .len = i - start };
        ++n;
        start = i + 1;
    }
    return n;
}

static bool parse_extra(u8str_t arg, u8str_t value, rubraview_batch_extra_params_t *x) {
    u8str_t p[4];
    long a = 0, b = 0, c = 0, d = 0;
    if (rubraview_u8_eq_lit(arg, "--autocrop")) { x->autocrop = true; return true; }
    if (rubraview_u8_eq_lit(arg, "--auto-colour")) { x->auto_colour = true; return true; }
    if (rubraview_u8_eq_lit(arg, "--negative")) { x->negative = true; return true; }
    if (starts_with(arg, "--crop=", &value)) {   /* X,Y,W,H */
        if (split_commas(value, p, 4) != 4 || !parse_int(p[0], &a) || !parse_int(p[1], &b) || !parse_int(p[2], &c) || !parse_int(p[3], &d) ||
            a < 0 || b < 0 || c < 1 || d < 1 || c > 65535 || d > 65535) return false;
        x->crop = true; x->crop_x = (int32_t)a; x->crop_y = (int32_t)b; x->crop_w = (int32_t)c; x->crop_h = (int32_t)d;
        return true;
    }
    if (starts_with(arg, "--effect=", &value)) {   /* NAME or NAME:AMOUNT, the name as the edit window says it, lower case */
        size_t k = 0;
        while (k < value.len && value.ptr[k] != ':') ++k;
        u8str_t name = { .ptr = value.ptr, .len = k };
        for (int e = 1; e < RUBRAVIEW_EFFECT_COUNT; ++e) {
            const char *n = rubraview_effect_key((rubraview_effect_t)e);
            if (!rubraview_u8_eq_lit(name, n)) continue;
            double lo = 0, hi = 0, start = 0;
            rubraview_effect_range((rubraview_effect_t)e, &lo, &hi, &start);
            double amount = start;
            if (k < value.len && (!parse_double((u8str_t){ .ptr = value.ptr + k + 1, .len = value.len - k - 1 }, &amount) || amount < lo || amount > hi)) return false;
            x->effect = e;
            x->effect_amount = amount;
            return true;
        }
        return false;
    }
    if (starts_with(arg, "--border=", &value)) {   /* PX or PX,RRGGBB (white) */
        size_t n = split_commas(value, p, 2);
        x->border_rgb = 0xFFFFFF;
        if (n < 1 || n > 2 || !parse_int(p[0], &a) || a < 1 || a > 10000 || (n == 2 && !parse_rgb(p[1], &x->border_rgb))) return false;
        x->border_px = (int32_t)a;
        return true;
    }
    if (starts_with(arg, "--canvas=", &value)) {   /* WxH or WxH,RRGGBB (black) */
        size_t n = split_commas(value, p, 2);
        x->canvas_rgb = 0;
        size_t k = 0;
        while (k < p[0].len && p[0].ptr[k] != 'x' && p[0].ptr[k] != 'X') ++k;
        if (n < 1 || n > 2 || k == p[0].len || !parse_int((u8str_t){ p[0].ptr, k }, &a) || !parse_int((u8str_t){ p[0].ptr + k + 1, p[0].len - k - 1 }, &b) ||
            a < 1 || b < 1 || a > 65535 || b > 65535 || (n == 2 && !parse_rgb(p[1], &x->canvas_rgb))) return false;
        x->canvas_w = (int32_t)a; x->canvas_h = (int32_t)b;
        return true;
    }
    if (starts_with(arg, "--watermark=", &value)) {   /* FILE or FILE,CORNER or FILE,CORNER,OPACITY; the file's own commas: the last two pieces only are read */
        static const char *const CORNERS[] = { "br", "bl", "tr", "tl", "c" };
        u8str_t file = value;
        x->watermark_corner = RUBRAVIEW_CORNER_BOTTOM_RIGHT;
        x->watermark_opacity = 50;
        for (int piece = 0; piece < 2; ++piece) {
            size_t k = file.len;
            while (k > 0 && file.ptr[k - 1] != ',') --k;
            if (k == 0) break;
            u8str_t tail = { .ptr = file.ptr + k, .len = file.len - k };
            int corner = -1;
            for (int i = 0; i < 5; ++i) if (rubraview_u8_eq_lit(tail, CORNERS[i])) corner = i;
            long o = 0;
            if (corner >= 0) x->watermark_corner = (rubraview_batch_corner_t)corner;
            else if (parse_int(tail, &o) && o >= 1 && o <= 100) x->watermark_opacity = (int32_t)o;
            else break;
            file.len = k - 1;
        }
        if (file.len == 0) return false;
        x->watermark = file;
        return true;
    }
    return false;
}

/* ---- the parser ---- */

void rubraview_cli_parse(rubraview_cli_result_t *out, proven_arena_t *arena,
                         int argc, const char *const *argv) {
    if (!out) return;
    rubraview_cli_result_t result = {0};
    /* Every argument could be a path: room for them all. */
    u8str_t *inputs = NULL;
    if (arena && argc > 1) {
        proven_result_mem_mut_t res = proven_arena_alloc(arena, (size_t)argc * sizeof(u8str_t));
        if (proven_is_ok(res.err)) inputs = (u8str_t*)(void*)res.value.ptr;
    }
    result.inputs = inputs;
    result.export_options = rubraview_export_defaults();
    result.job.actions = result.actions;
    result.job.naming_pattern = U8("{name}.{ext}");

    for (int i = 1; i < argc; ++i) {
        u8str_t arg = cstr(argv[i]);
        u8str_t value = {0};

        if (arg.len == 0) continue;

        /* Anything that is not a flag is the input path. The last one
           wins rather than being silently ignored. */
        if (arg.ptr[0] != '-') {
            result.input = arg;
            if (inputs) inputs[result.input_count++] = arg;
            continue;
        }

        if (rubraview_shell_verb_for_flag(arg) != RUBRAVIEW_SHELL_VERB_NONE) {
            result.shell_verb = rubraview_shell_verb_for_flag(arg);
            continue;
        }
        if (rubraview_u8_eq_lit(arg, "--whole")) { result.shell_whole = true; continue; }
        if (starts_with(arg, "--paths-from=", &value)) { result.paths_from = value; continue; }
        if (rubraview_u8_eq_lit(arg, "--paths-temp")) { result.paths_temp = true; continue; }
        if (rubraview_u8_eq_lit(arg, "--no-types")) { result.shell_no_types = true; continue; }
        if (rubraview_u8_eq_lit(arg, "--no-menu")) { result.shell_no_menu = true; continue; }
        if (rubraview_u8_eq_lit(arg, "--remove-types")) { result.shell_remove_types = true; continue; }
        if (rubraview_u8_eq_lit(arg, "--remove-menu")) { result.shell_remove_menu = true; continue; }

        if (rubraview_u8_eq_lit(arg, "--batch")) { result.batch_mode = true; continue; }
        if (rubraview_u8_eq_lit(arg, "--register-shell")) { result.register_shell = true; continue; }
        if (rubraview_u8_eq_lit(arg, "--unregister-shell")) { result.unregister_shell = true; continue; }
        if (rubraview_u8_eq_lit(arg, "--all-users")) { result.shell_all_users = true; continue; }
        if (rubraview_u8_eq_lit(arg, "--wipe-shell")) { result.wipe_shell = true; continue; }
        if (rubraview_u8_eq_lit(arg, "--setup")) { result.shell_setup = true; continue; }
        if (starts_with(arg, "--menu-types=", &value)) {
            result.shell_menu_types = rubraview_shell_selection_parse(arena, value);
            if (result.shell_menu_types.len == 0) {
                result.err = RUBRAVIEW_CLI_ERR_BAD_VALUE; result.offending = arg; *out = result; out->job.actions = out->actions; return;
            }
            continue;
        }
        if (starts_with(arg, "--types=", &value)) {
            result.shell_types = rubraview_shell_selection_parse(arena, value);
            if (result.shell_types.len == 0) {
                result.err = RUBRAVIEW_CLI_ERR_BAD_VALUE; result.offending = arg; *out = result; out->job.actions = out->actions; return;
            }
            continue;
        }
        if (rubraview_u8_eq_lit(arg, "--new-instance")) { result.new_instance = true; continue; }
        if (rubraview_u8_eq_lit(arg, "--diag")) { result.diagnostics = true; continue; }
        if (rubraview_u8_eq_lit(arg, "--version") || rubraview_u8_eq_lit(arg, "-v")) { result.show_version = true; continue; }
        if (rubraview_u8_eq_lit(arg, "--probe-media")) { result.probe_media = true; continue; }
        if (rubraview_u8_eq_lit(arg, "--probe-gpu")) { result.probe_gpu = true; continue; }
        if (rubraview_u8_eq_lit(arg, "--recursive")) { result.recursive = true; continue; }
        if (starts_with(arg, "--sort=", &value)) {
            static const struct { const char *word; rubraview_sort_mode_t mode; } SORTS[] = {
                { "natural", RUBRAVIEW_SORT_NAME_NATURAL }, { "lexical", RUBRAVIEW_SORT_NAME_LEXICAL },
                { "modified", RUBRAVIEW_SORT_DATE_MODIFIED }, { "created", RUBRAVIEW_SORT_DATE_CREATED },
                { "size", RUBRAVIEW_SORT_FILE_SIZE }, { "random", RUBRAVIEW_SORT_RANDOM },
            };
            bool known = false;
            for (size_t k = 0; k < sizeof(SORTS) / sizeof(SORTS[0]) && !known; ++k) {
                if (rubraview_u8_eq_lit(value, SORTS[k].word)) { result.sort_mode = SORTS[k].mode; known = true; }
            }
            if (!known) { result.err = RUBRAVIEW_CLI_ERR_BAD_VALUE; result.offending = arg; *out = result; out->job.actions = out->actions; return; }
            result.sort_given = true;
            continue;
        }
        if (rubraview_u8_eq_lit(arg, "--descending")) { result.descending_given = true; result.descending = true; continue; }
        if (rubraview_u8_eq_lit(arg, "--ascending")) { result.descending_given = true; result.descending = false; continue; }
        if (rubraview_u8_eq_lit(arg, "--pause")) { result.pause_at_end = true; continue; }
        if (rubraview_u8_eq_lit(arg, "--grayscale")) {
            rubraview_batch_action_t *a = action_for(&result, RUBRAVIEW_BATCH_COLOR_ADJUST);
            if (!a) { result.err = RUBRAVIEW_CLI_ERR_TOO_MANY_ACTIONS; result.offending = arg; *out = result; out->job.actions = out->actions; return; }
            a->params.color.grayscale = true;
            continue;
        }
        if (rubraview_u8_eq_lit(arg, "--privacy-clean")) {
            rubraview_batch_action_t *a = action_for(&result, RUBRAVIEW_BATCH_PRIVACY_SCRUB);
            if (!a) { result.err = RUBRAVIEW_CLI_ERR_TOO_MANY_ACTIONS; result.offending = arg; *out = result; out->job.actions = out->actions; return; }
            a->params.privacy.strip_all_exif = true;
            a->params.privacy.strip_xmp = true;
            a->params.privacy.strip_iptc = true;
            result.export_options.privacy_clean = true;
            continue;
        }

        if (starts_with(arg, "--resize=", &value)) {
            rubraview_batch_action_t *a = action_for(&result, RUBRAVIEW_BATCH_RESIZE);
            if (!a) { result.err = RUBRAVIEW_CLI_ERR_TOO_MANY_ACTIONS; result.offending = arg; *out = result; out->job.actions = out->actions; return; }
            rubraview_resample_filter_t keep = a->params.resize.filter;
            if (!parse_resize(value, &a->params.resize)) {
                result.err = RUBRAVIEW_CLI_ERR_BAD_VALUE; result.offending = arg; *out = result; out->job.actions = out->actions; return;
            }
            a->params.resize.filter = keep;
            continue;
        }

        if (starts_with(arg, "--filter=", &value)) {
            rubraview_batch_action_t *a = action_for(&result, RUBRAVIEW_BATCH_RESIZE);
            if (!a) { result.err = RUBRAVIEW_CLI_ERR_TOO_MANY_ACTIONS; result.offending = arg; *out = result; out->job.actions = out->actions; return; }
            if (!parse_filter(value, &a->params.resize.filter)) {
                result.err = RUBRAVIEW_CLI_ERR_BAD_VALUE; result.offending = arg; *out = result; out->job.actions = out->actions; return;
            }
            continue;
        }

        if (starts_with(arg, "--format=", &value)) {
            /* The format is named the way a filename names it, so
               "--format=jpg" and a ".jpg" output agree by construction. */
            char dotted[16];
            if (value.len == 0 || value.len > 8) {
                result.err = RUBRAVIEW_CLI_ERR_BAD_VALUE; result.offending = arg; *out = result; out->job.actions = out->actions; return;
            }
            dotted[0] = 'x'; dotted[1] = '.';
            memcpy(dotted + 2, value.ptr, value.len);
            rubraview_export_format_t format =
                rubraview_export_format_for_name((u8str_t){ .ptr = dotted, .len = value.len + 2 });
            if (format == RUBRAVIEW_EXPORT_SAME_AS_SOURCE) {
                result.err = RUBRAVIEW_CLI_ERR_BAD_VALUE; result.offending = arg; *out = result; out->job.actions = out->actions; return;
            }
            result.export_options.format = format;

            rubraview_batch_action_t *a = action_for(&result, RUBRAVIEW_BATCH_CONVERT);
            if (!a) { result.err = RUBRAVIEW_CLI_ERR_TOO_MANY_ACTIONS; result.offending = arg; *out = result; out->job.actions = out->actions; return; }
            a->params.convert.target_ext = rubraview_export_extension(format);
            continue;
        }

        if (starts_with(arg, "--quality=", &value)) {
            long q = 0;
            if (!parse_int(value, &q) || q < 1 || q > 100) {
                result.err = RUBRAVIEW_CLI_ERR_BAD_VALUE; result.offending = arg; *out = result; out->job.actions = out->actions; return;
            }
            result.export_options.jpeg_quality = (int32_t)q;
            result.export_options.webp_quality = (int32_t)q;
            rubraview_batch_action_t *a = action_for(&result, RUBRAVIEW_BATCH_CONVERT);
            if (a) a->params.convert.quality = (int32_t)q;
            continue;
        }

        /* RV-108 (§3.10): the two choices of the writers. */
        if (rubraview_u8_eq_lit(arg, "--progressive")) { result.export_options.jpeg_progressive = true; continue; }
        if (starts_with(arg, "--png-depth=", &value)) {
            static const char *const DEPTHS[] = { "rgba", "rgb", "palette", "gray" };
            int found = -1;
            for (int k = 0; k < 4 && found < 0; ++k) if (rubraview_u8_eq_lit(value, DEPTHS[k])) found = k;
            if (found < 0) { result.err = RUBRAVIEW_CLI_ERR_BAD_VALUE; result.offending = arg; *out = result; out->job.actions = out->actions; return; }
            result.export_options.png_depth = (rubraview_png_depth_t)found;
            continue;
        }

        /* RV-109: how a WebP is written. */
        if (rubraview_u8_eq_lit(arg, "--lossless")) { result.export_options.webp_lossless = true; continue; }
        if (starts_with(arg, "--effort=", &value)) {
            int64_t effort = -1;
            if (!rubraview_parse_i64(value, &effort) || effort < 0 || effort > 6) {
                result.err = RUBRAVIEW_CLI_ERR_BAD_VALUE; result.offending = arg; *out = result; out->job.actions = out->actions; return;
            }
            result.export_options.webp_effort = (int32_t)effort;
            continue;
        }

        if (starts_with(arg, "--rotate=", &value)) {
            rubraview_batch_action_t *a = action_for(&result, RUBRAVIEW_BATCH_ORIENT);
            if (!a) { result.err = RUBRAVIEW_CLI_ERR_TOO_MANY_ACTIONS; result.offending = arg; *out = result; out->job.actions = out->actions; return; }
            if (rubraview_u8_eq_lit(value, "exif")) {
                a->params.orient.use_exif_auto_orient = true;
            } else {
                long deg = 0;
                if (!parse_int(value, &deg) || (deg != 0 && deg != 90 && deg != 180 && deg != 270)) {
                    result.err = RUBRAVIEW_CLI_ERR_BAD_VALUE; result.offending = arg; *out = result; out->job.actions = out->actions; return;
                }
                a->params.orient.rotate_degrees = (int32_t)deg;
            }
            continue;
        }

        if (starts_with(arg, "--flip=", &value)) {
            rubraview_batch_action_t *a = action_for(&result, RUBRAVIEW_BATCH_ORIENT);
            if (!a) { result.err = RUBRAVIEW_CLI_ERR_TOO_MANY_ACTIONS; result.offending = arg; *out = result; out->job.actions = out->actions; return; }
            if (rubraview_u8_eq_lit(value, "h")) a->params.orient.flip_horizontal = true;
            else if (rubraview_u8_eq_lit(value, "v")) a->params.orient.flip_vertical = true;
            else { result.err = RUBRAVIEW_CLI_ERR_BAD_VALUE; result.offending = arg; *out = result; out->job.actions = out->actions; return; }
            continue;
        }

        if (starts_with(arg, "--sharpen=", &value)) {
            rubraview_batch_action_t *a = action_for(&result, RUBRAVIEW_BATCH_COLOR_ADJUST);
            if (!a) { result.err = RUBRAVIEW_CLI_ERR_TOO_MANY_ACTIONS; result.offending = arg; *out = result; out->job.actions = out->actions; return; }

            u8str_t amount_str = value, radius_str = {0};
            for (size_t k = 0; k < value.len; ++k) {
                if (value.ptr[k] != ',') continue;
                amount_str = (u8str_t){ .ptr = value.ptr, .len = k };
                radius_str = (u8str_t){ .ptr = value.ptr + k + 1, .len = value.len - k - 1 };
                break;
            }

            double amount = 0.0, radius = 1.0;
            if (!parse_double(amount_str, &amount) || amount < 0.0) {
                result.err = RUBRAVIEW_CLI_ERR_BAD_VALUE; result.offending = arg; *out = result; out->job.actions = out->actions; return;
            }
            if (radius_str.len > 0 && (!parse_double(radius_str, &radius) || radius <= 0.0)) {
                result.err = RUBRAVIEW_CLI_ERR_BAD_VALUE; result.offending = arg; *out = result; out->job.actions = out->actions; return;
            }
            a->params.color.has_unsharp = true;
            a->params.color.amount = (float)(amount / 100.0);
            a->params.color.sigma = (float)radius;
            continue;
        }

        if (starts_with(arg, "--exposure=", &value) || starts_with(arg, "--contrast=", &value)) {
            bool is_exposure = arg.ptr[2] == 'e';
            double v = 0.0;
            if (!parse_double(value, &v)) {
                result.err = RUBRAVIEW_CLI_ERR_BAD_VALUE; result.offending = arg; *out = result; out->job.actions = out->actions; return;
            }
            if (is_exposure && (v < -3.0 || v > 3.0)) {
                result.err = RUBRAVIEW_CLI_ERR_BAD_VALUE; result.offending = arg; *out = result; out->job.actions = out->actions; return;
            }
            if (!is_exposure && (v < -100.0 || v > 100.0)) {
                result.err = RUBRAVIEW_CLI_ERR_BAD_VALUE; result.offending = arg; *out = result; out->job.actions = out->actions; return;
            }

            rubraview_batch_action_t *a = action_for(&result, RUBRAVIEW_BATCH_COLOR_ADJUST);
            if (!a) { result.err = RUBRAVIEW_CLI_ERR_TOO_MANY_ACTIONS; result.offending = arg; *out = result; out->job.actions = out->actions; return; }
            if (!a->params.color.has_color_adjust) {
                a->params.color.has_color_adjust = true;
                a->params.color.adjust.saturation = 1.0f;
                a->params.color.adjust.gamma = 1.0f;
            }
            if (is_exposure) a->params.color.adjust.exposure_ev = (float)v;
            else a->params.color.adjust.contrast = (float)v;
            continue;
        }

        /* ---- RV-129 ---- */
        if (rubraview_u8_eq_lit(arg, "--no-enlarge") || rubraview_u8_eq_lit(arg, "--linear")) {
            rubraview_batch_action_t *a = action_for(&result, RUBRAVIEW_BATCH_RESIZE);
            if (!a) { result.err = RUBRAVIEW_CLI_ERR_TOO_MANY_ACTIONS; result.offending = arg; *out = result; out->job.actions = out->actions; return; }
            if (arg.ptr[2] == 'n') a->params.resize.no_enlarge = true; else a->params.resize.linear = true;
            continue;
        }
        if (starts_with(arg, "--crop=", &value) || rubraview_u8_eq_lit(arg, "--autocrop") || rubraview_u8_eq_lit(arg, "--auto-colour") ||
            rubraview_u8_eq_lit(arg, "--negative") || starts_with(arg, "--effect=", &value) || starts_with(arg, "--border=", &value) ||
            starts_with(arg, "--canvas=", &value) || starts_with(arg, "--watermark=", &value)) {
            rubraview_batch_action_t *a = action_for(&result, RUBRAVIEW_BATCH_EXTRA);
            if (!a) { result.err = RUBRAVIEW_CLI_ERR_TOO_MANY_ACTIONS; result.offending = arg; *out = result; out->job.actions = out->actions; return; }
            if (!parse_extra(arg, value, &a->params.extra)) {
                result.err = RUBRAVIEW_CLI_ERR_BAD_VALUE; result.offending = arg; *out = result; out->job.actions = out->actions; return;
            }
            continue;
        }
        if (starts_with(arg, "--start=", &value)) {
            long n = 0;
            if (!parse_int(value, &n) || n < 0 || n > 999999999) { result.err = RUBRAVIEW_CLI_ERR_BAD_VALUE; result.offending = arg; *out = result; out->job.actions = out->actions; return; }
            result.job.counter_start = (int32_t)n;
            continue;
        }
        if (starts_with(arg, "--replace=", &value)) {
            /* FROM,TO: the first comma divides them; TO may be empty */
            size_t k = 0;
            while (k < value.len && value.ptr[k] != ',') ++k;
            if (k == 0 || k == value.len) { result.err = RUBRAVIEW_CLI_ERR_BAD_VALUE; result.offending = arg; *out = result; out->job.actions = out->actions; return; }
            result.job.replace_from = (u8str_t){ .ptr = value.ptr, .len = k };
            result.job.replace_to = (u8str_t){ .ptr = value.ptr + k + 1, .len = value.len - k - 1 };
            continue;
        }
        if (starts_with(arg, "--exists=", &value)) {
            if (rubraview_u8_eq_lit(value, "replace")) result.exists = 0;
            else if (rubraview_u8_eq_lit(value, "skip")) result.exists = 1;
            else if (rubraview_u8_eq_lit(value, "number")) result.exists = 2;
            else { result.err = RUBRAVIEW_CLI_ERR_BAD_VALUE; result.offending = arg; *out = result; out->job.actions = out->actions; return; }
            continue;
        }
        if (rubraview_u8_eq_lit(arg, "--keep-dates")) { result.keep_dates = true; continue; }
        if (rubraview_u8_eq_lit(arg, "--subfolders")) { result.subfolders = true; continue; }

        if (starts_with(arg, "--include=", &value)) { result.job.include_pattern = value; continue; }
        if (starts_with(arg, "--exclude=", &value)) { result.job.exclude_pattern = value; continue; }
        if (starts_with(arg, "--name=", &value)) { result.job.naming_pattern = value; continue; }
        if (starts_with(arg, "--out=", &value)) { result.output_dir = value; continue; }

        if (starts_with(arg, "--min-size=", &value) || starts_with(arg, "--max-size=", &value)) {
            long n = 0;
            if (!parse_int(value, &n) || n < 0) {
                result.err = RUBRAVIEW_CLI_ERR_BAD_VALUE; result.offending = arg; *out = result; out->job.actions = out->actions; return;
            }
            if (arg.ptr[2] == 'm' && arg.ptr[3] == 'i') result.job.min_size_bytes = (uint64_t)n;
            else result.job.max_size_bytes = (uint64_t)n;
            continue;
        }

        /* Not recognised. Refusing here is the whole point: a typo must
           not turn into a directory processed the wrong way. */
        result.err = RUBRAVIEW_CLI_ERR_UNKNOWN_FLAG;
        result.offending = arg;
        *out = result; out->job.actions = out->actions; return;
    }

    rubraview_export_clamp(&result.export_options);

    if (result.batch_mode && result.input.len == 0 && result.paths_from.len == 0) {
        result.err = RUBRAVIEW_CLI_ERR_NO_INPUT;
        *out = result; out->job.actions = out->actions; return;
    }
    *out = result; out->job.actions = out->actions; return;
}

u8str_t rubraview_cli_error_text(rubraview_cli_err_t err) {
    switch (err) {
        case RUBRAVIEW_CLI_OK: return U8("");
        case RUBRAVIEW_CLI_ERR_UNKNOWN_FLAG: return U8("unknown option");
        case RUBRAVIEW_CLI_ERR_BAD_VALUE: return U8("the value is not one this option accepts");
        case RUBRAVIEW_CLI_ERR_NO_INPUT: return U8("--batch needs a file or directory to work on");
        case RUBRAVIEW_CLI_ERR_TOO_MANY_ACTIONS: return U8("too many actions in one run");
        default: return U8("bad command line");
    }
}

/* ---- the engine ---- */

rubraview_batch_report_t rubraview_batch_run(proven_arena_t *arena,
                                             const rubraview_batch_job_t *job,
                                             const rubraview_batch_input_t *inputs, size_t input_count,
                                             u8str_t date_str,
                                             rubraview_batch_process_fn process, void *ctx,
                                             rubraview_batch_item_result_t *out_results,
                                             size_t results_capacity) {
    rubraview_batch_report_t report = {0};
    if (!arena || !job || !inputs) return report;

    report.total = input_count;

    for (size_t i = 0; i < input_count; ++i) {
        const rubraview_batch_input_t *input = &inputs[i];
        u8str_t name = rubraview_path_basename(input->path);

        rubraview_batch_item_result_t item = {
            .status = RUBRAVIEW_BATCH_STATUS_SKIPPED,
            .input_path = input->path,
            .output_name = { .ptr = "", .len = 0 },
        };

        if (!rubraview_batch_file_matches(job, name, input->size_bytes)) {
            report.skipped++;
            if (out_results && i < results_capacity) out_results[i] = item;
            continue;
        }

        /* §3.11: the arena is reset before every file, so the memory a
           run holds is the memory one file needs — not the sum of them.
           This is also why the output name is built after the reset. */
        proven_arena_reset(arena);

        u8str_t stem = rubraview_path_stem(name);
        u8str_t ext = rubraview_path_ext(name);
        if (ext.len > 0 && ext.ptr[0] == '.') { ext.ptr++; ext.len--; }

        /* A convert action decides the extension; without one the file
           keeps the one it had. */
        for (size_t a = 0; a < job->action_count; ++a) {
            if (job->actions[a].kind == RUBRAVIEW_BATCH_CONVERT &&
                job->actions[a].params.convert.target_ext.len > 0) {
                ext = job->actions[a].params.convert.target_ext;
            }
        }

        /* RV-129: the stem's words replaced, the counter, the folder's name */
        stem = rubraview_batch_replace_all(arena, stem, job->replace_from, job->replace_to);
        int64_t counter = (int64_t)(job->counter_start > 0 ? job->counter_start : 1) + (int64_t)(report.processed + report.failed);
        u8str_t parent = rubraview_path_basename(rubraview_path_dirname(input->path));
        item.output_name = rubraview_batch_format_name_ex(arena, job->naming_pattern, stem, ext, 0, 0, date_str, counter, parent);

        bool ok = process ? process(arena, job, input, item.output_name, ctx) : true;
        item.status = ok ? RUBRAVIEW_BATCH_STATUS_OK : RUBRAVIEW_BATCH_STATUS_FAILED;
        if (ok) report.processed++; else report.failed++;

        if (out_results && i < results_capacity) out_results[i] = item;
    }

    return report;
}

size_t rubraview_batch_work_bytes(int32_t width, int32_t height, int32_t out_w, int32_t out_h,
                                  bool turn, bool resize, bool sharpen) {
    if (width <= 0 || height <= 0) return 0;
    uint64_t source = (uint64_t)width * (uint64_t)height * 4u;
    uint64_t target = resize && out_w > 0 && out_h > 0 ? (uint64_t)out_w * (uint64_t)out_h * 4u : source;
    uint64_t total = source;                    /* read_pixels: one RGBA picture */
    if (turn) total += source;                  /* rubraview_pixbuf_orient's copy */
    if (resize) total += target;                /* the resampler's destination (its intermediate is on the stack) */
    if (sharpen) total += 3u * target;          /* blur's two and unsharp's result */
    total += 16u * 1024u * 1024u;               /* names, arena alignment */
    return total > (uint64_t)SIZE_MAX ? SIZE_MAX : (size_t)total;
}

int rubraview_shell_setup_exit(int code, bool removing) {
    if (removing) return 0;
    return code == 1 ? 1 : 0;
}
