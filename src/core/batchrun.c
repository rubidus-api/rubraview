#include "rubraview/batchrun.h"
#include "rubraview/path.h"
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

static bool equals(u8str_t s, const char *lit) {
    size_t n = strlen(lit);
    return s.len == n && memcmp(s.ptr, lit, n) == 0;
}

/* Parses a decimal integer. Refuses anything with a stray character in
   it — "--quality=90abc" is a typo, not the number 90. */
static bool parse_int(u8str_t s, long *out) {
    if (s.len == 0 || s.len > 20) return false;
    char buf[24];
    memcpy(buf, s.ptr, s.len);
    buf[s.len] = '\0';

    char *end = NULL;
    long value = strtol(buf, &end, 10);
    if (!end || *end != '\0') return false;
    *out = value;
    return true;
}

static bool parse_double(u8str_t s, double *out) {
    if (s.len == 0 || s.len > 32) return false;
    char buf[36];
    memcpy(buf, s.ptr, s.len);
    buf[s.len] = '\0';

    char *end = NULL;
    double value = strtod(buf, &end);
    if (!end || *end != '\0') return false;
    *out = value;
    return true;
}

static bool parse_filter(u8str_t s, rubraview_resample_filter_t *out) {
    if (equals(s, "lanczos3") || equals(s, "lanczos")) { *out = RUBRAVIEW_FILTER_LANCZOS3; return true; }
    if (equals(s, "bicubic")) { *out = RUBRAVIEW_FILTER_BICUBIC; return true; }
    if (equals(s, "bilinear")) { *out = RUBRAVIEW_FILTER_BILINEAR; return true; }
    if (equals(s, "nearest")) { *out = RUBRAVIEW_FILTER_NEAREST; return true; }
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

/* ---- the parser ---- */

void rubraview_cli_parse(rubraview_cli_result_t *out, proven_arena_t *arena,
                         int argc, const char *const *argv) {
    (void)arena;
    if (!out) return;
    rubraview_cli_result_t result = {0};
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
            continue;
        }

        if (equals(arg, "--batch")) { result.batch_mode = true; continue; }
        if (equals(arg, "--register-shell")) { result.register_shell = true; continue; }
        if (equals(arg, "--unregister-shell")) { result.unregister_shell = true; continue; }
        if (equals(arg, "--new-instance")) { result.new_instance = true; continue; }
        if (equals(arg, "--diag")) { result.diagnostics = true; continue; }
        if (equals(arg, "--version") || equals(arg, "-v")) { result.show_version = true; continue; }
        if (equals(arg, "--probe-media")) { result.probe_media = true; continue; }
        if (equals(arg, "--probe-gpu")) { result.probe_gpu = true; continue; }
        if (equals(arg, "--recursive")) { result.recursive = true; continue; }
        if (equals(arg, "--pause")) { result.pause_at_end = true; continue; }
        if (equals(arg, "--grayscale")) {
            rubraview_batch_action_t *a = action_for(&result, RUBRAVIEW_BATCH_COLOR_ADJUST);
            if (!a) { result.err = RUBRAVIEW_CLI_ERR_TOO_MANY_ACTIONS; result.offending = arg; *out = result; out->job.actions = out->actions; return; }
            a->params.color.grayscale = true;
            continue;
        }
        if (equals(arg, "--privacy-clean")) {
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

        if (starts_with(arg, "--rotate=", &value)) {
            rubraview_batch_action_t *a = action_for(&result, RUBRAVIEW_BATCH_ORIENT);
            if (!a) { result.err = RUBRAVIEW_CLI_ERR_TOO_MANY_ACTIONS; result.offending = arg; *out = result; out->job.actions = out->actions; return; }
            if (equals(value, "exif")) {
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
            if (equals(value, "h")) a->params.orient.flip_horizontal = true;
            else if (equals(value, "v")) a->params.orient.flip_vertical = true;
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

    if (result.batch_mode && result.input.len == 0) {
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

        item.output_name = rubraview_batch_format_name(arena, job->naming_pattern, stem, ext, 0, 0, date_str);

        bool ok = process ? process(arena, job, input, item.output_name, ctx) : true;
        item.status = ok ? RUBRAVIEW_BATCH_STATUS_OK : RUBRAVIEW_BATCH_STATUS_FAILED;
        if (ok) report.processed++; else report.failed++;

        if (out_results && i < results_capacity) out_results[i] = item;
    }

    return report;
}
