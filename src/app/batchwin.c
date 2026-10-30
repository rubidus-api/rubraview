/*
 * The batch window: its choices as text, typed, saved and loaded (RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* ---- RV-129: the batch window (owner, 2026-10-17: "배치 처리 ... 그 이상") ----
   Its choices become the run's command line, one argument a token; the same
   tokens, a line each, are what is remembered (batch.last) and saved to a
   file, and they are read back through the command line's own parser. */


static void batch_token(batch_tokens_t *tk, const char *format, ...) {
    if (tk->n >= sizeof(tk->v) / sizeof(tk->v[0]) || tk->used >= sizeof(tk->store)) return;
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(tk->store + tk->used, sizeof(tk->store) - tk->used, format, ap);
    va_end(ap);
    if (n <= 0 || (size_t)n >= sizeof(tk->store) - tk->used) return;
    tk->v[tk->n++] = tk->store + tk->used;
    tk->used += (size_t)n + 1;
}

static const char *const BATCH_FILTER_WORDS[4] = { "nearest", "bilinear", "bicubic", "lanczos3" };
static const uint32_t BATCH_FRAME_RGB[3] = { 0xFFFFFF, 0x000000, 0x808080 };

/* Everything chosen, as the run's options (without the input, --batch, --pause). */
void batch_tokens(const struct batch_ui *b, batch_tokens_t *tk) {
    tk->n = 0; tk->used = 0;
    /* RV-187 (D-220): Edit's steps first; what they run (gmic.exe, the plug-ins) as the viewer has it */
    if (b->steps_from > 0 && b->steps_file[0]) {
        batch_token(tk, "--steps=%s", b->steps_file);
        if (g_gmic_exe[0]) batch_token(tk, "--gmic-exe=%s", g_gmic_exe);
        if (g_plugin_folder[0]) batch_token(tk, "--plugin-folder=%s", g_plugin_folder);
    }
    double v = b->size_value;
    switch (b->size_by) {
        case 1: if (fabs(v - 100.0) > 0.001) batch_token(tk, "--resize=%g%%", v); break;
        case 2: batch_token(tk, "--resize=l%d", (int)lround(v)); break;
        case 3: batch_token(tk, "--resize=s%d", (int)lround(v)); break;
        case 4: batch_token(tk, "--resize=w%d", (int)lround(v)); break;
        case 5: batch_token(tk, "--resize=h%d", (int)lround(v)); break;
        case 6: batch_token(tk, "--resize=%dx%d", (int)lround(v), (int)lround(b->box_h)); break;
        case 7: batch_token(tk, "--resize=%gmp", v); break;
        default: break;
    }
    if (tk->n > 0) {
        if (b->filter >= 0 && b->filter <= 3) batch_token(tk, "--filter=%s", BATCH_FILTER_WORDS[b->filter]);
        if (b->linear) batch_token(tk, "--linear");
        if (b->no_enlarge) batch_token(tk, "--no-enlarge");
    }
    if (b->sharpen > 0.5) batch_token(tk, "--sharpen=%d", (int)lround(b->sharpen));
    if (b->rotate >= 1 && b->rotate <= 3) batch_token(tk, "--rotate=%d", b->rotate * 90);
    if (b->rotate == 4) batch_token(tk, "--rotate=exif");
    if (b->flip == 1) batch_token(tk, "--flip=h");
    if (b->flip == 2) batch_token(tk, "--flip=v");
    if (b->crop[0]) batch_token(tk, "--crop=%s", b->crop);
    if (b->autocrop) batch_token(tk, "--autocrop");
    if (b->auto_colour) batch_token(tk, "--auto-colour");
    if (b->grayscale) batch_token(tk, "--grayscale");
    if (b->negative) batch_token(tk, "--negative");
    if (b->effect > 0 && b->effect < RUBRAVIEW_EFFECT_COUNT)
        batch_token(tk, "--effect=%s:%g", rubraview_effect_key((rubraview_effect_t)b->effect), b->effect_amount);
    if (b->canvas[0]) batch_token(tk, "--canvas=%s,%06X", b->canvas, (unsigned)BATCH_FRAME_RGB[b->frame_colour % 3]);
    if (b->border > 0.5) batch_token(tk, "--border=%d,%06X", (int)lround(b->border), (unsigned)BATCH_FRAME_RGB[b->frame_colour % 3]);
    if (b->mark[0]) batch_token(tk, "--watermark=%s,%s,%d", b->mark, (const char *[]){ "br", "bl", "tr", "tl", "c" }[b->mark_corner % 5],
                                (int)lround(b->mark_opacity));
    if (b->text[0]) {
        static const uint32_t TEXT_RGB[3] = { 0xFFFFFF, 0x000000, 0xFFD400 };
        batch_token(tk, "--text=%s,%s,%d,%06X", b->text, (const char *[]){ "br", "bl", "tr", "tl", "c" }[b->text_corner % 5],
                    (int)lround(b->text_size), (unsigned)TEXT_RGB[b->text_colour % 3]);
    }
    if (b->dpi >= 1.0) batch_token(tk, "--dpi=%d", (int)lround(b->dpi));
    if (b->format > 0 && b->format < RUBRAVIEW_EXPORT_FORMATS) {
        proven_u8str_view_t ext = rubraview_export_extension((rubraview_export_format_t)b->format);
        batch_token(tk, "--format=%.*s", (int)ext.size, ext.ptr);
    }
    if (b->quality >= 1.0 && fabs(b->quality - 90.0) > 0.5) batch_token(tk, "--quality=%d", (int)lround(b->quality));
    {   /* RV-140, 1.17.0: what differs from how they come, in the command line's words */
        const rubraview_export_options_t *o = &b->more, d = rubraview_export_defaults();
        static const char *const DEPTHS[] = { "rgba", "rgb", "palette", "gray" }, *const SUBS[] = { "auto", "420", "422", "444" };
        static const char *const FILTERS[] = { "level", "none", "sub", "up", "average", "paeth", "adaptive" };
        static const char *const TIFFS[] = { "auto", "none", "lzw", "zip" }, *const BMPS[] = { "32", "24", "8" }, *const KEEP[] = { "drop", "file", "txt" };
        if (o->jpeg_progressive) batch_token(tk, "--progressive");
        if (o->png_depth != d.png_depth && (unsigned)o->png_depth < 4u) batch_token(tk, "--png-depth=%s", DEPTHS[o->png_depth]);
        if (o->webp_lossless) batch_token(tk, "--lossless");
        if (o->webp_effort != d.webp_effort) batch_token(tk, "--effort=%d", (int)o->webp_effort);
        if (o->jpeg_subsampling != d.jpeg_subsampling && (unsigned)o->jpeg_subsampling < 4u) batch_token(tk, "--subsampling=%s", SUBS[o->jpeg_subsampling]);
        if (o->jpeg_grey) batch_token(tk, "--grey-jpeg");
        if (o->jpeg_optimise) batch_token(tk, "--optimise");
        if (o->jpeg_restart_rows > 0) batch_token(tk, "--restart=%d", (int)o->jpeg_restart_rows);
        if (o->png_filter != d.png_filter && (unsigned)o->png_filter < 7u) batch_token(tk, "--png-filter=%s", FILTERS[o->png_filter]);
        if (o->png_interlace) batch_token(tk, "--interlace");
        if (o->colours != d.colours) batch_token(tk, "--colours=%d", (int)o->colours);
        if (!o->dither) batch_token(tk, "--no-dither");
        if (o->tiff_compression != d.tiff_compression && (unsigned)o->tiff_compression < 4u) batch_token(tk, "--tiff=%s", TIFFS[o->tiff_compression]);
        if (o->bmp_depth != d.bmp_depth && (unsigned)o->bmp_depth < 3u) batch_token(tk, "--bmp=%s", BMPS[o->bmp_depth]);
        if (o->webp_near_lossless != d.webp_near_lossless) batch_token(tk, "--near-lossless=%d", (int)o->webp_near_lossless);
        if (o->webp_alpha_quality != d.webp_alpha_quality) batch_token(tk, "--alpha-quality=%d", (int)o->webp_alpha_quality);
        if (o->webp_sharp_yuv) batch_token(tk, "--sharp-yuv");
        if (o->keep_details != d.keep_details && (unsigned)o->keep_details < 3u) batch_token(tk, "--details=%s", KEEP[o->keep_details]);
        static const char *const PRESETS[] = { "none", "picture", "photo", "drawing", "icon", "text" };
        if (o->webp_preset > 0 && o->webp_preset < 6) batch_token(tk, "--webp-preset=%s", PRESETS[o->webp_preset]);
        if (o->avif_chroma == 1) batch_token(tk, "--avif-chroma=444");   /* 1.31.0 */
        if (o->avif_bits == 10) batch_token(tk, "--avif-bits=10");
        if (o->target_kb > 0) batch_token(tk, "--target-kb=%d", (int)o->target_kb);
        if (o->tiff_cmyk) batch_token(tk, "--cmyk");
    }
    if (b->privacy) batch_token(tk, "--privacy-clean");
    if (b->name[0] && strcmp(b->name, "{name}.{ext}") != 0) batch_token(tk, "--name=%s", b->name);
    if (b->start > 1.5) batch_token(tk, "--start=%d", (int)lround(b->start));
    for (size_t r = 0; r < RUBRAVIEW_BATCH_MAX_REPLACES; ++r)   /* RV-194: several, in order */
        if (b->replace[r][0] && strchr(b->replace[r], ',') && b->replace[r][0] != ',') batch_token(tk, "--replace=%s", b->replace[r]);
    if (b->n_step > 1.5) batch_token(tk, "--n-step=%d", (int)lround(b->n_step));
    if (b->name_case == 1) batch_token(tk, "--case=lower");
    if (b->name_case == 2) batch_token(tk, "--case=upper");
    if (b->no_spaces) batch_token(tk, "--no-spaces");
    if (b->exists == 1) batch_token(tk, "--exists=skip");
    if (b->exists == 2) batch_token(tk, "--exists=number");
    if (b->keep_dates) batch_token(tk, "--keep-dates");
    if (b->recursive) batch_token(tk, "--recursive");
    if (b->subfolders) batch_token(tk, "--subfolders");
    if (b->out[0]) batch_token(tk, "--out=%s", b->out);
}

static void batch_defaults(struct batch_ui *b) {
    *b = (struct batch_ui){ .loaded = true, .size_by = 0, .size_value = 100.0, .box_h = 1080.0, .filter = 3, .effect_amount = 0.0,
                            .mark_corner = 0, .mark_opacity = 50.0, .quality = 90.0, .start = 1.0, .text_size = 4.0,
                            .n_step = 1.0, .digits = 1.0, .p_name = true };
    snprintf(b->name, sizeof(b->name), "%s", "{name}.{ext}");
    b->more = rubraview_export_defaults();
}

/* The tokens (a line each) read back into the choices; false when they do not parse. */
static bool batch_from_text(app_state_t *app, struct batch_ui *b, proven_u8str_view_t text) {
    const char *argv[72];
    int argc = 0;
    argv[argc++] = "rubraview";
    argv[argc++] = "--batch";
    for (size_t i = 0; i < text.size && argc < 70;) {
        size_t e = i;
        while (e < text.size && text.ptr[e] != '\n' && text.ptr[e] != '\r') ++e;
        if (e > i) {
            proven_u8str_view_t line = app_keep(app, (proven_u8str_view_t){ .ptr = text.ptr + i, .size = e - i });
            argv[argc++] = (const char *)line.ptr;
        }
        i = e + 1;
    }
    argv[argc++] = "in";
    static rubraview_cli_result_t r;
    rubraview_cli_parse(&r, app->arena, argc, argv);
    if (r.err != RUBRAVIEW_CLI_OK) return false;
    batch_defaults(b);
    for (size_t i = 0; i < r.job.action_count; ++i) {
        const rubraview_batch_action_t *a = &r.actions[i];
        if (a->kind == RUBRAVIEW_BATCH_RESIZE) {
            static const int32_t BY[] = { 1, 6, 4, 5, 2, 3, 7 };   /* the core's modes in the window's order */
            b->size_by = (int)a->params.resize.mode < 7 ? BY[a->params.resize.mode] : 0;
            b->size_value = a->params.resize.value_a;
            b->box_h = a->params.resize.mode == RUBRAVIEW_RESIZE_BOUNDING_BOX ? a->params.resize.value_b : b->box_h;
            b->filter = (int32_t)a->params.resize.filter;
            b->linear = a->params.resize.linear;
            b->no_enlarge = a->params.resize.no_enlarge;
            if (a->params.resize.value_a <= 0.0) b->size_by = 0;
        } else if (a->kind == RUBRAVIEW_BATCH_ORIENT) {
            const rubraview_batch_orient_params_t *o = &a->params.orient;
            b->rotate = o->use_exif_auto_orient ? 4 : o->rotate_degrees / 90;
            b->flip = o->flip_horizontal ? 1 : o->flip_vertical ? 2 : 0;
        } else if (a->kind == RUBRAVIEW_BATCH_COLOR_ADJUST) {
            b->grayscale = a->params.color.grayscale;
            if (a->params.color.has_unsharp) b->sharpen = a->params.color.amount * 100.0;
        } else if (a->kind == RUBRAVIEW_BATCH_PRIVACY_SCRUB) {
            b->privacy = true;
        } else if (a->kind == RUBRAVIEW_BATCH_EXTRA) {
            const rubraview_batch_extra_params_t *x = &a->params.extra;
            if (x->crop) snprintf(b->crop, sizeof(b->crop), "%d,%d,%d,%d", (int)x->crop_x, (int)x->crop_y, (int)x->crop_w, (int)x->crop_h);
            b->autocrop = x->autocrop; b->auto_colour = x->auto_colour; b->negative = x->negative;
            b->effect = x->effect; b->effect_amount = x->effect_amount;
            if (x->canvas_w > 0) snprintf(b->canvas, sizeof(b->canvas), "%dx%d", (int)x->canvas_w, (int)x->canvas_h);
            b->border = x->border_px;
            uint32_t rgb = x->border_px > 0 ? x->border_rgb : x->canvas_rgb;
            b->frame_colour = rgb == 0 ? 1 : rgb == 0x808080 ? 2 : 0;
            if (x->watermark.size > 0) snprintf(b->mark, sizeof(b->mark), "%.*s", (int)x->watermark.size, x->watermark.ptr);
            b->mark_corner = (int32_t)x->watermark_corner; b->mark_opacity = x->watermark_opacity;
            if (x->text.size > 0) {
                snprintf(b->text, sizeof(b->text), "%.*s", (int)x->text.size, x->text.ptr);
                b->text_corner = (int32_t)x->text_corner; b->text_size = x->text_size;
                b->text_colour = x->text_rgb == 0 ? 1 : x->text_rgb == 0xFFD400 ? 2 : 0;
            }
        } else if (a->kind == RUBRAVIEW_BATCH_CONVERT) {
            b->format = (int32_t)r.export_options.format;
        }
    }
    if (r.export_options.jpeg_quality > 0) b->quality = r.export_options.jpeg_quality;
    b->dpi = r.export_options.dpi;
    b->more = r.export_options;
    if (r.job.naming_pattern.size > 0) snprintf(b->name, sizeof(b->name), "%.*s", (int)r.job.naming_pattern.size, r.job.naming_pattern.ptr);
    b->start = r.job.counter_start > 0 ? r.job.counter_start : 1;
    for (size_t k = 0; k < r.job.replace_count && k < RUBRAVIEW_BATCH_MAX_REPLACES; ++k)
        snprintf(b->replace[k], sizeof(b->replace[k]), "%.*s,%.*s", (int)r.job.replaces[k].from.size, r.job.replaces[k].from.ptr,
                 (int)r.job.replaces[k].to.size, r.job.replaces[k].to.ptr);
    b->n_step = r.job.counter_step > 0 ? r.job.counter_step : 1;   /* RV-194 */
    b->name_case = r.job.name_case;
    b->no_spaces = r.job.spaces_to_underscore;
    b->own_pattern = strcmp(b->name, "{name}.{ext}") != 0;   /* a pattern from before: shown as typed */
    b->exists = r.exists; b->keep_dates = r.keep_dates; b->recursive = r.recursive; b->subfolders = r.subfolders;
    if (r.steps_file.size > 0) {   /* RV-187: the steps in Edit, or a file of them */
        snprintf(b->steps_file, sizeof(b->steps_file), "%.*s", (int)r.steps_file.size, r.steps_file.ptr);
        b->steps_from = proven_u8str_view_eq(rubraview_path_basename(r.steps_file), PROVEN_LIT("batch-steps.rvsteps")) ? 1 : 2;
    }
    if (r.output_dir.size > 0) snprintf(b->out, sizeof(b->out), "%.*s", (int)r.output_dir.size, r.output_dir.ptr);
    return true;
}

/* The tokens as text, a line each. */
proven_u8str_view_t batch_text(app_state_t *app) {
    batch_tokens_t tk;
    batch_tokens(&app->batch, &tk);
    char out[8192];
    size_t at = 0;
    for (size_t i = 0; i < tk.n; ++i) {
        int n = snprintf(out + at, sizeof(out) - at, "%s\n", tk.v[i]);
        if (n <= 0 || (size_t)n >= sizeof(out) - at) break;
        at += (size_t)n;
    }
    return app_keep(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)out, .size = at });
}

static proven_u8str_view_t batch_row_label(char *buf, size_t cap, const char *what, const char *value, const char *empty) {
    int n = snprintf(buf, cap, "%s: %s...", what, value && value[0] ? value : empty);
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)buf, .size = n > 0 ? ((size_t)n < cap ? (size_t)n : cap - 1) : 0 };
}

/* RV-194: in a window of its own, the label column as wide as its longest name (as Edit's, RV-188), at most 45 %. */
static void owned_measure_labels(app_state_t *app, rubraview_renderer_t *r, rubraview_window_t *win) {
    if (!r || !win) return;
    double size = app->panel.row_height * 0.45, most = 0.0;
    for (size_t i = 0; i < app->panel.row_count; ++i) {
        const rubraview_panel_row_t *row = &app->panel.rows[i];
        if (row->kind != RUBRAVIEW_ROW_SLIDER && row->kind != RUBRAVIEW_ROW_TOGGLE && row->kind != RUBRAVIEW_ROW_CHOICE) continue;
        double tw = 0.0;
        if (rubraview_pal_render_measure_text(r, row->label, size, &tw) && tw > most) most = tw;
    }
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(win, &w, &h);
    double want = most + size * 4.2 + app->panel.padding * 1.6, cap = (double)w * 0.45;
    if (want > cap) want = cap;
    if (want > app->panel.label_width) app->panel.label_width = want;
}

void panel_open_batch(app_state_t *app) {
    struct batch_ui *b = &app->batch;
    if (!b->loaded) {
        batch_defaults(b);
        proven_u8str_view_t last = rubraview_settings_get_text(&app->settings, PROVEN_LIT("batch"), PROVEN_LIT("last"));
        if (last.size > 0) {   /* stored with \n turned to | (settings.ini is a line a key) */
            char text[4096];
            size_t n = last.size < sizeof(text) ? last.size : sizeof(text);
            for (size_t i = 0; i < n; ++i) text[i] = last.ptr[i] == '|' ? '\n' : last.ptr[i];
            if (!batch_from_text(app, b, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)text, .size = n })) batch_defaults(b);
        }
    }
    double dpi = rubraview_pal_window_dpi_scale(app->batch_window ? app->batch_window : app->window);
    bool placed = app->panel.placed && app->panel_is_batch;
    double px = app->panel.place_x, py = app->panel.place_y;
    double scroll = app->panel_is_batch ? app->panel.column_scroll[0] : 0.0;
    app->panel = rubraview_panel_create(TU8("Batch"), dpi);
    if (placed) { app->panel.placed = true; app->panel.place_x = px; app->panel.place_y = py; }
    app->panel.column_scroll[0] = scroll;   /* RV-194: a row changed keeps the window where it was */
    app->panel_is_export = false;
    app->panel_is_batch = true;
    app->panel_is_print = false;
    app->panel_is_convert = false;
    app->panel_is_names = false;
    rubraview_panel_t *pn = &app->panel;
    static char labels[12][1100];

    /* RV-194 (owner, 2026-10-30: "어떤 파일들에 적용되는지도 알기 어려워요"): first, which files, with the list a press away */
    rubraview_panel_add_heading(pn, TU8("1. The files"));
    size_t total = 0, pictures = batch_list_pictures(app, NULL, 0, &total);
    if (app->batch_list_count > 0)   /* D-86: "Convert..." from Explorer: these files */
        snprintf(labels[8], sizeof(labels[8]), T("The %zu files chosen in Explorer"), app->batch_list_count);
    else if (total == 0)
        snprintf(labels[8], sizeof(labels[8]), "%s", T("Nothing is open: open pictures or a playlist first (press: the list)"));
    else
        snprintf(labels[8], sizeof(labels[8]), T("The %zu pictures of the list now open, in its order (of %zu: films, music and text left out). Press: show the list (F3)"),
                 pictures, total);
    rubraview_panel_add_button(pn, PANEL_BATCH_SHOW_LIST, cstr(labels[8]));

    /* RV-194: second, Edit's steps — what they are and where they are made */
    rubraview_panel_add_heading(pn, TU8("2. Edit's steps, first"));
    snprintf(labels[9], sizeof(labels[9]), T("Every step in Edit's list is applied, in that order, to every picture: %zu step%s in Edit now. Press: open Edit (E)"),
             app->edit_steps.cursor, app->edit_steps.cursor == 1 ? "" : TC("plural", "s"));
    rubraview_panel_add_button(pn, PANEL_BATCH_OPEN_EDIT, cstr(labels[9]));
    rubraview_panel_add_choice(pn, PANEL_BATCH_STEPS, TU8("Edit's steps"), b->steps_from, 3);
    if (b->steps_from == 2) {
        proven_u8str_view_t base = rubraview_path_basename(cstr(b->steps_file));
        char shown[300];
        snprintf(shown, sizeof(shown), "%.*s", (int)(base.size < 200 ? base.size : 200), base.ptr);
        rubraview_panel_add_button(pn, PANEL_BATCH_STEPS_FILE, batch_row_label(labels[0], sizeof(labels[0]), T("  Steps file"), shown, T("choose a .rvsteps file")));
    }

    rubraview_panel_add_heading(pn, TU8("3. Then, on each picture"));
    rubraview_panel_add_choice(pn, PANEL_BATCH_SIZE_BY, TU8("Size"), b->size_by, 8);
    if (b->size_by > 0) {
        double lo = 1, hi = 16000, step = 1;
        if (b->size_by == 1) { lo = 0.01; hi = 1000; step = 0.01; }   /* RV-187: finer than a percent */
        if (b->size_by == 7) { lo = 0.1; hi = 200; step = 0.1; }
        rubraview_panel_add_slider(pn, PANEL_BATCH_RESIZE, b->size_by == 6 ? TU8("  width") : TU8("  value"), b->size_value, lo, hi, step);
        if (b->size_by == 6) rubraview_panel_add_slider(pn, PANEL_BATCH_BOX_H, TU8("  height"), b->box_h, 1, 16000, 1);
        rubraview_panel_add_choice(pn, PANEL_BATCH_FILTER, TU8("  Filter"), b->filter, 4);
        rubraview_panel_add_toggle(pn, PANEL_BATCH_LINEAR, TU8("  In linear light"), b->linear);
        rubraview_panel_add_toggle(pn, PANEL_BATCH_NO_ENLARGE, TU8("  Never enlarge"), b->no_enlarge);
    }
    rubraview_panel_add_slider(pn, PANEL_BATCH_SHARPEN, TU8("Sharpen"), b->sharpen, 0, 300, 5);
    rubraview_panel_add_choice(pn, PANEL_BATCH_ROTATE, TU8("Turn"), b->rotate, 5);
    rubraview_panel_add_choice(pn, PANEL_BATCH_FLIP, TU8("Mirror"), b->flip, 3);
    rubraview_panel_add_toggle(pn, PANEL_BATCH_AUTOCROP, TU8("Cut plain borders"), b->autocrop);
    rubraview_panel_add_toggle(pn, PANEL_BATCH_AUTOCOLOUR, TU8("Automatic colour"), b->auto_colour);
    rubraview_panel_add_toggle(pn, PANEL_BATCH_GRAY, TU8("Grayscale"), b->grayscale);
    rubraview_panel_add_toggle(pn, PANEL_BATCH_NEGATIVE, TU8("Negative"), b->negative);
    /* RV-187: a crop and an effect are Edit's steps now; ones from choices saved before still run, until dropped */
    if (b->crop[0] || b->effect > 0) {
        char kept[160];
        snprintf(kept, sizeof(kept), T("Kept from saved choices: %s%s%s (press: drop them)"), b->crop[0] ? T("a crop") : "",
                 b->crop[0] && b->effect > 0 ? ", " : "", b->effect > 0 ? effect_word(b->effect) : "");
        rubraview_panel_add_button(pn, PANEL_BATCH_CROP, app_keep(app, cstr(kept)));
    }
    rubraview_panel_add_button(pn, PANEL_BATCH_CANVAS, batch_row_label(labels[1], sizeof(labels[1]), T("Canvas WxH"), b->canvas, T("none")));
    rubraview_panel_add_slider(pn, PANEL_BATCH_BORDER, TU8("Border px"), b->border, 0, 500, 1);
    rubraview_panel_add_choice(pn, PANEL_BATCH_FRAME_COLOUR, TU8("  their colour"), b->frame_colour, 3);
    {
        proven_u8str_view_t base = rubraview_path_basename(cstr(b->mark));
        char shown[300];
        snprintf(shown, sizeof(shown), "%.*s", (int)(base.size < 200 ? base.size : 200), base.ptr);
        rubraview_panel_add_button(pn, PANEL_BATCH_MARK, batch_row_label(labels[2], sizeof(labels[2]), T("Watermark"), shown, T("none (choose a picture)")));
    }
    if (b->mark[0]) {
        rubraview_panel_add_choice(pn, PANEL_BATCH_MARK_CORNER, TU8("  where"), b->mark_corner, 5);
        rubraview_panel_add_slider(pn, PANEL_BATCH_MARK_OPACITY, TU8("  opacity %"), b->mark_opacity, 1, 100, 1);
    }
    /* RV-134: words on each picture */
    rubraview_panel_add_button(pn, PANEL_BATCH_TEXT, batch_row_label(labels[7], sizeof(labels[7]), T("Text"), b->text, T("none")));
    if (b->text[0]) {
        rubraview_panel_add_choice(pn, PANEL_BATCH_TEXT_CORNER, TU8("  where"), b->text_corner, 5);
        rubraview_panel_add_slider(pn, PANEL_BATCH_TEXT_SIZE, TU8("  size %"), b->text_size, 1, 50, 1);
        rubraview_panel_add_choice(pn, PANEL_BATCH_TEXT_COLOUR, TU8("  colour"), b->text_colour, 3);
    }

    rubraview_panel_add_heading(pn, TU8("4. Written as"));
    rubraview_panel_add_choice(pn, PANEL_BATCH_FORMAT, TU8("Format"), b->format, RUBRAVIEW_EXPORT_FORMATS);
    rubraview_panel_add_slider(pn, PANEL_BATCH_QUALITY, TU8("  quality"), b->quality, 1, 100, 1);
    {   /* RV-140: the format's own choices, as Export has them */
        rubraview_export_options_t o = b->more;
        o.format = (rubraview_export_format_t)b->format;
        export_more_rows(pn, &o, rubraview_export_rows_of(&o, PROVEN_LIT("")));
    }
    if (!b->privacy) rubraview_panel_add_choice(pn, PANEL_KEEP_DETAILS, TU8("Camera details, comments"), b->more.keep_details, 3);   /* 1.17.0 */
    rubraview_panel_add_toggle(pn, PANEL_PRIVACY, TU8("Privacy clean"), b->privacy);
    rubraview_panel_add_slider(pn, PANEL_BATCH_DPI, TU8("DPI written (0: as it is)"), b->dpi, 0, 1200, 1);
    /* RV-194: the names in a window of their own, the pattern shown here */
    rubraview_panel_add_button(pn, PANEL_BATCH_NAMES, batch_row_label(labels[3], sizeof(labels[3]), T("Names (press: set them, with a preview)"), b->name, "{name}.{ext}"));
    rubraview_panel_add_toggle(pn, PANEL_BATCH_KEEP_DATES, TU8("Keep the files' dates"), b->keep_dates);
    rubraview_panel_add_button(pn, PANEL_BATCH_OUT, batch_row_label(labels[5], sizeof(labels[5]), T("Into"), b->out, T("rubraview-out beside them")));

    rubraview_panel_add_heading(pn, TU8("5. Go"));
    rubraview_panel_add_button(pn, PANEL_BATCH_SAVE, TU8("Save these choices..."));
    rubraview_panel_add_button(pn, PANEL_BATCH_LOAD, TU8("Load choices..."));
    if (app->batch_list_count > 0) {
        snprintf(labels[6], sizeof(labels[6]), T("Run on the %zu chosen file%s"), app->batch_list_count, app->batch_list_count == 1 ? "" : TC("plural", "s"));
    } else {
        snprintf(labels[6], sizeof(labels[6]), T("Run on the %zu picture%s"), pictures, pictures == 1 ? "" : TC("plural", "s"));
    }
    rubraview_panel_add_button(pn, PANEL_BATCH_RUN, cstr(labels[6]));
    if (app->batch_window) rubraview_panel_add_button(pn, PANEL_BATCH_CLOSE, TU8("Close (Esc)"));
    owned_measure_labels(app, app->batch_renderer, app->batch_window);

    app->panel.open = true;
    panel_relayout(app);
}

/* A row whose value is text: typed in the rename box. */
/* RV-194: the names window's parts made into the pattern (unless it was typed whole and no part has changed since). */
void batch_compose_name(app_state_t *app) {
    struct batch_ui *b = &app->batch;
    static const char *const SEPS[4] = { "_", "-", " ", "" };
    rubraview_batch_name_parts_t parts = {
        .before = cstr(b->before), .after = cstr(b->after), .sep = cstr(SEPS[(unsigned)b->sep % 4u]),
        .name = b->p_name, .parent = b->p_parent, .counter = b->p_counter, .size = b->p_size, .taken = b->p_taken,
        .digits = (int32_t)lround(b->digits), .taken_form = b->taken_form };
    proven_u8str_view_t made = rubraview_batch_compose_pattern(app->arena, &parts);
    snprintf(b->name, sizeof(b->name), "%.*s", (int)made.size, (const char *)made.ptr);
    b->own_pattern = false;
}

/* The replaces kept in order with no gaps (one emptied goes). */
void batch_replaces_pack(struct batch_ui *b) {
    size_t to = 0;
    for (size_t r = 0; r < RUBRAVIEW_BATCH_MAX_REPLACES; ++r) {
        if (!b->replace[r][0]) continue;
        if (to != r) { memcpy(b->replace[to], b->replace[r], sizeof(b->replace[0])); b->replace[r][0] = '\0'; }
        to++;
    }
}

/* The text a row types into (RV-194: the names window's too), and its room. */
static char *batch_field(app_state_t *app, int32_t id, size_t *cap) {
    struct batch_ui *b = &app->batch;
    if (id >= PANEL_NAMES_REPLACE0 && id <= PANEL_NAMES_REPLACE7) { *cap = sizeof(b->replace[0]); return b->replace[id - PANEL_NAMES_REPLACE0]; }
    switch (id) {
        case PANEL_BATCH_TEXT:   *cap = sizeof(b->text);   return b->text;
        case PANEL_BATCH_CROP:   *cap = sizeof(b->crop);   return b->crop;
        case PANEL_BATCH_CANVAS: *cap = sizeof(b->canvas); return b->canvas;
        case PANEL_BATCH_NAME: case PANEL_NAMES_PATTERN: *cap = sizeof(b->name); return b->name;
        case PANEL_NAMES_BEFORE: *cap = sizeof(b->before); return b->before;
        case PANEL_NAMES_AFTER:  *cap = sizeof(b->after);  return b->after;
        default:                 *cap = sizeof(b->out);    return b->out;
    }
}

void batch_type_begin(app_state_t *app, int32_t id) {
    size_t cap = 0;
    const char *now = batch_field(app, id, &cap);
    if (app->rename_active) rename_end(app);
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    rubraview_textedit_set(&app->rename_edit, cstr(now));
    rubraview_textedit_select_all(&app->rename_edit);
    app->rename_active = true;
    app->rename_composing_length = 0;
    app->batch_typing = id;
    rubraview_pal_window_text_input(app->names_window && app->panel_is_names ? app->names_window : app->batch_window ? app->batch_window : app->window, true);
    osd_say(app, id == PANEL_BATCH_TEXT ? TU8("the words: {name} {n} {n:3} {parent} {date} {w} {h} work here too; Enter (empty: none)")
               : id == PANEL_BATCH_NAME || id == PANEL_NAMES_PATTERN ? TU8("{name} {ext} {n} {n:3} {parent} {taken:yyyy-mm-dd} {w} {h}, then Enter (empty: as it was)")
               : id == PANEL_NAMES_BEFORE || id == PANEL_NAMES_AFTER ? TU8("the words, then Enter (empty: none)")
               : id >= PANEL_NAMES_REPLACE0 && id <= PANEL_NAMES_REPLACE7 ? TU8("what,with: IMG_,photo-  then Enter (empty: this one goes)")
               : id == PANEL_BATCH_CROP ? TU8("X,Y,W,H in pixels, then Enter (empty: no crop)")
               : id == PANEL_BATCH_CANVAS ? TU8("width x height, e.g. 1920x1080, then Enter (empty: none)")
               : id == PANEL_BATCH_REPLACE ? TU8("what,with: IMG_,photo-  then Enter (empty: nothing)")
               : TU8("a folder, then Enter (empty: rubraview-out beside the files)"));
}

static void batch_typed_in(app_state_t *app);

/* RV-194: the box was opened in Batch's or the names' window; Enter may come from the viewer's: the panel it was
   typed for stands in for the change, never the viewer's own (Edit's would be lost). */
void batch_typed(app_state_t *app) {
    if (!app->batch_window || owned_panel_active()) { batch_typed_in(app); return; }
    int32_t id = app->batch_typing;
    bool names = app->names_window && ((id >= PANEL_NAMES_NAME && id <= PANEL_NAMES_REPLACE7) || id == PANEL_BATCH_START || id == PANEL_BATCH_EXISTS);
    owned_panel_in(app, names ? 2 : 1);
    batch_typed_in(app);
    owned_panel_out(app);
    batch_refresh(app);
    app->names_dirty = true;
}

static void batch_typed_in(app_state_t *app) {
    proven_u8str_view_t text = rubraview_textedit_text(&app->rename_edit);
    int32_t id = app->batch_typing;
    size_t cap = 0;
    char *dst = batch_field(app, id, &cap);
    char was[1024];
    snprintf(was, sizeof(was), "%s", dst);
    size_t n = text.size < cap - 1 ? text.size : cap - 1;
    memcpy(dst, text.ptr, n);
    dst[n] = '\0';
    if ((id == PANEL_BATCH_NAME || id == PANEL_NAMES_PATTERN) && n == 0) snprintf(dst, cap, "%s", "{name}.{ext}");
    if (id == PANEL_NAMES_PATTERN) app->batch.own_pattern = true;                   /* RV-194: typed whole */
    if (id == PANEL_NAMES_BEFORE || id == PANEL_NAMES_AFTER) batch_compose_name(app); /* a part: the pattern made again */
    if (id >= PANEL_NAMES_REPLACE0 && id <= PANEL_NAMES_REPLACE7) batch_replaces_pack(&app->batch);
    app->batch_typing = 0;
    rename_end(app);
    /* What the run's parser would refuse is refused here, with the word for why. */
    batch_tokens_t tk;
    batch_tokens(&app->batch, &tk);
    const char *argv[70];
    int argc = 0;
    argv[argc++] = "rubraview"; argv[argc++] = "--batch";
    for (size_t i = 0; i < tk.n && argc < 68; ++i) argv[argc++] = tk.v[i];
    argv[argc++] = "in";
    static rubraview_cli_result_t r;
    rubraview_cli_parse(&r, app->arena, argc, argv);
    if (r.err != RUBRAVIEW_CLI_OK) {
        snprintf(dst, cap, "%s", was);
        osd_say(app, TU8("that is not a value this takes; left as it was"));
    }
    if (app->panel_is_names) names_panel_build(app); else panel_open_batch(app);
}

/* ---- RV-194: what Batch works on, and the names it will write ---- */

static bool batch_is_picture(proven_u8str_view_t path) {
    return path.size > 0 && !rubraview_stream_is_address(path) &&
           rubraview_glob_match_list(rubraview_path_basename(path), PROVEN_LIT(IMAGE_FILTER));
}

/* The list now open, as it shows (its filter, its order): its pictures into `out` (up to `cap`), how many there are,
   and in `*total` how many rows it has. An archive's pages are no files of their own: none. */
size_t batch_list_pictures(app_state_t *app, proven_u8str_view_t *out, size_t cap, size_t *total) {
    size_t rows = page_count(app) > 0 && app->archive_bytes.size == 0 ? list_rows(app) : 0;
    if (total) *total = rows;
    size_t n = 0;
    for (size_t r = 0; r < rows; ++r) {
        size_t i = list_page(app, r);
        if (i >= page_count(app)) continue;
        proven_u8str_view_t path = app->source.pages[i].path;
        if (!batch_is_picture(path)) continue;
        if (out && n < cap) out[n] = path;
        n++;
    }
    return n;
}

/* The date a picture was taken (its EXIF), else its file's time, as yyyymmddhhmmss; 0 when neither is read. */
int64_t batch_taken_of(proven_arena_t *arena, proven_u8str_view_t path) {
    proven_u8str_view_t bytes = rubraview_pal_fs_read_file(arena, path, 64u << 20);
    if (bytes.size > 0) {
        rubraview_exif_info_t exif;
        if (rubraview_exif_read((const uint8_t *)bytes.ptr, bytes.size, &exif) && exif.date_taken[0]) {
            int64_t v = rubraview_batch_taken_from_text(cstr(exif.date_taken));
            if (v > 0) return v;
        }
    }
    rubraview_fs_entry_t e;
    char when[40] = "";
    if (rubraview_pal_fs_stat(arena, path, &e)) info_local_time(when, sizeof(when), e.mtime);
    return rubraview_batch_taken_from_text(cstr(when));
}

/* The preview's files: the chosen ones, else the list's pictures; their dates read once (the names window opening). */
void batch_preview_files(app_state_t *app) {
    proven_u8str_view_t pics[12];
    size_t n = 0;
    if (app->batch_list_count > 0) {
        for (size_t i = 0; i < app->batch_list_count && n < 12; ++i) pics[n++] = app->batch_list[i];
    } else {
        n = batch_list_pictures(app, pics, 12, NULL);
        if (n > 12) n = 12;
    }
    app->names_file_count = n;
    void *mem = malloc(80u << 20);
    proven_arena_t scratch = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t *)mem, .size = mem ? 80u << 20 : 0 });
    bool dates = strstr(app->batch.name, "{taken") != NULL || app->batch.p_taken;
    for (size_t i = 0; i < n; ++i) {
        app->names_files[i] = app_keep(app, pics[i]);
        if (mem) proven_arena_reset(&scratch);
        app->names_taken[i] = dates && mem ? batch_taken_of(&scratch, pics[i]) : 0;
    }
    free(mem);
}

/* The new name of the preview's `i`-th file, as the run makes it (its size still {w}x{h}). */
proven_u8str_view_t batch_preview_name(app_state_t *app, size_t i, proven_arena_t *arena) {
    if (i >= app->names_file_count) return PROVEN_LIT("");
    batch_tokens_t tk;
    batch_tokens(&app->batch, &tk);
    const char *argv[70];
    int argc = 0;
    argv[argc++] = "rubraview"; argv[argc++] = "--batch";
    for (size_t k = 0; k < tk.n && argc < 68; ++k) argv[argc++] = tk.v[k];
    argv[argc++] = "in";
    static rubraview_cli_result_t r;
    rubraview_cli_parse(&r, arena, argc, argv);
    if (r.err != RUBRAVIEW_CLI_OK) return PROVEN_LIT("?");
    rubraview_batch_input_t in = { app->names_files[i], 1, app->names_taken[i] };
    proven_u8str_view_t ext = r.export_options.format != RUBRAVIEW_EXPORT_SAME_AS_SOURCE ? rubraview_export_extension(r.export_options.format) : PROVEN_LIT("");
    return rubraview_batch_output_name(arena, &r.job, &in, i, ext);
}

void batch_save_or_load(app_state_t *app, bool saving) {
    static const rubraview_file_filter_t FILTERS[] = { { "Batch choices (*.rvbatch)", "*.rvbatch" } };
    rubraview_file_dialog_opts_t opts = {
        .title = saving ? T("Save the batch choices") : T("Load batch choices"), .filters = FILTERS, .filter_count = 1,
        .parent_window_handle = rubraview_pal_window_native_handle(app->window),
    };
    rubraview_file_dialog_result_t picked = saving ? rubraview_pal_file_dialog_save(app->arena, &opts) : rubraview_pal_file_dialog_open(app->arena, &opts);
    if (!picked.accepted || picked.count == 0) return;
    proven_u8str_view_t path = picked.paths[0];
    if (saving) {
        if (!rubraview_path_has_ext(rubraview_path_basename(path), ".rvbatch")) {
            char with[1100];
            int n = snprintf(with, sizeof(with), "%.*s.rvbatch", (int)path.size, path.ptr);
            if (n > 0 && (size_t)n < sizeof(with)) path = app_keep(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)with, .size = (size_t)n });
        }
        osd_say(app, rubraview_pal_fs_write_file(path, batch_text(app)) ? TU8("the choices were saved") : TU8("could not write the file"));
        return;
    }
    proven_u8str_view_t text = rubraview_pal_fs_read_file(app->arena, path, 64u * 1024u);
    struct batch_ui b = app->batch;
    if (text.size == 0 || !batch_from_text(app, &b, text)) { osd_say(app, TU8("that file holds no batch choices this version takes")); return; }
    app->batch = b;
    osd_say(app, TU8("the choices were loaded"));
    panel_open_batch(app);
}

/* ---- RV-187 (D-220): the steps in Edit, where Batch reads them ---- */

proven_u8str_view_t batch_steps_path(app_state_t *app) {
    return rubraview_path_join(app->arena, rubraview_path_dirname(app->settings_path), PROVEN_LIT("batch-steps.rvsteps"));
}

bool batch_steps_write(app_state_t *app) {
    if (app->edit_steps.cursor == 0) return false;
    proven_u8str_view_t text = rubraview_edit_steps_serialize(app->arena, &app->edit_steps);
    proven_u8str_view_t path = batch_steps_path(app);
    if (text.size == 0 || !rubraview_pal_fs_write_file_atomic(path, text)) return false;
    snprintf(app->batch.steps_file, sizeof(app->batch.steps_file), "%.*s", (int)path.size, (const char *)path.ptr);
    return true;
}

/* ---- RV-194 (owner, 2026-10-30: "일괄처리는 내부 창이 아니라 별개의 창으로 모달리스 창이 뜨게"): Batch in a window of its
   own, and its names in another. Each keeps its own panel; while one is handled or drawn it stands in `app->panel`
   with its flag set, so every panel function serves it as it serves the viewer's, and the viewer's (Edit's, Export's)
   is put back after. ---- */

#define BATCH_BACKGROUND 0xFF1A1A1Au
enum { BATCH_AFTER_NONE = 0, BATCH_AFTER_LIST, BATCH_AFTER_EDIT, BATCH_AFTER_NAMES, BATCH_AFTER_CLOSE, BATCH_AFTER_NAMES_CLOSE };

static struct { rubraview_panel_t panel; bool is_export, is_batch, is_print, is_convert, is_names; } g_kept;
static int g_owned;   /* 1: Batch's panel stands in app->panel, 2: the names', 0: neither */

void owned_panel_in(app_state_t *app, int which) {
    if (g_owned) return;
    g_kept.panel = app->panel;
    g_kept.is_export = app->panel_is_export; g_kept.is_batch = app->panel_is_batch; g_kept.is_print = app->panel_is_print;
    g_kept.is_convert = app->panel_is_convert; g_kept.is_names = app->panel_is_names;
    app->panel = which == 1 ? app->batch_panel : app->names_panel;
    app->panel_is_export = app->panel_is_print = app->panel_is_convert = false;
    app->panel_is_batch = which == 1;
    app->panel_is_names = which == 2;
    g_owned = which;
}

void owned_panel_out(app_state_t *app) {
    if (!g_owned) return;
    if (g_owned == 1) app->batch_panel = app->panel; else app->names_panel = app->panel;
    app->panel = g_kept.panel;
    app->panel_is_export = g_kept.is_export; app->panel_is_batch = g_kept.is_batch; app->panel_is_print = g_kept.is_print;
    app->panel_is_convert = g_kept.is_convert; app->panel_is_names = g_kept.is_names;
    g_owned = 0;
}

bool owned_panel_active(void) { return g_owned != 0; }

static void owned_window_layout(app_state_t *app, int which) {
    rubraview_window_t *win = which == 1 ? app->batch_window : app->names_window;
    if (!win) return;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(win, &w, &h);
    double dpi = rubraview_pal_window_dpi_scale(win);
    /* the names window: its choices at the left, the preview at the right */
    double width = which == 2 ? fmax(300.0 * dpi, (double)w * 0.5) : (double)w;
    rubraview_panel_layout_columns(&app->panel, (rubraview_rect_t){ 0.0, 0.0, width, (double)h }, 1, NULL);
}

/* Called through panel_relayout while an owned panel stands in. */
bool owned_panel_relayout(app_state_t *app) {
    if (!g_owned) return false;
    owned_window_layout(app, g_owned);
    return true;
}

void batch_refresh(app_state_t *app) {
    if (!app->batch_window) return;
    bool mine = g_owned == 1;
    if (!mine && g_owned) return;   /* the names' panel stands in: Batch is made again after it */
    if (!mine) owned_panel_in(app, 1);
    panel_open_batch(app);
    if (!mine) owned_panel_out(app);
    app->batch_dirty = true;
}

static void owned_window_frame(app_state_t *app, rubraview_window_t *win, const int32_t frame[4], int32_t right_of) {
    if (frame[2] > 0 && frame[3] > 0) { rubraview_pal_window_set_frame(win, frame[0], frame[1], frame[2], frame[3]); return; }
    int32_t vx = 0, vy = 0, vw = 0, vh = 0, ex = 0, ey = 0, ew = 0, eh = 0, sw = 0, sh = 0;
    if (rubraview_pal_window_get_frame(app->window, &vx, &vy, &vw, &vh) && rubraview_pal_window_get_frame(win, &ex, &ey, &ew, &eh)) {
        int32_t x = (right_of ? right_of : vx + vw) + 8;
        if (rubraview_pal_window_monitor_size(app->window, &sw, &sh) && sw > 0 && x + ew > sw) x = sw - ew > 0 ? sw - ew : 0;
        rubraview_pal_window_set_frame(win, x, vy, ew, eh);
    }
}

void batch_window_close(app_state_t *app) {
    if (app->names_window) names_window_close(app);
    if (!app->batch_window) return;
    if (app->batch_typing && app->rename_active) { app->batch_typing = 0; rename_end(app); }
    if (app->chooser.open && app->chooser.where == CHOOSE_IN_BATCH) app->chooser.open = false;
    rubraview_pal_window_get_frame(app->batch_window, &app->batch_frame[0], &app->batch_frame[1], &app->batch_frame[2], &app->batch_frame[3]);
    if (app->batch_renderer) rubraview_pal_render_destroy(app->batch_renderer);
    rubraview_pal_window_destroy(app->batch_window); window_block_give(app, &app->batch_block);
    app->batch_renderer = NULL;
    app->batch_window = NULL;
    app->batch_panel.open = false;
    app->batch_list_count = 0;   /* Explorer's files were for this time */
}

static bool batch_window_show(app_state_t *app) {
    if (app->batch_window) return true;
    rubraview_window_config_t config = { .title = T("Rubraview Batch"), .width = 620, .height = 760, .owner = app->window };
    app->batch_window = rubraview_pal_window_create(window_block_take(app, &app->batch_block), &config);
    if (!app->batch_window) return false;
    owned_window_frame(app, app->batch_window, app->batch_frame, 0);
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->batch_window, &w, &h);
    app->batch_renderer = rubraview_pal_render_create(window_block_arena(app, &app->batch_block), rubraview_pal_window_native_handle(app->batch_window), w, h);
    if (!app->batch_renderer) {
        rubraview_pal_window_destroy(app->batch_window); window_block_give(app, &app->batch_block);
        app->batch_window = NULL;
        return false;
    }
    ui_font_apply_all(app);
    app->batch_px = app->batch_py = -1.0;
    return true;
}

/* Ctrl+B, the menu's Batch, Explorer's Convert...: the window opened (or, open, closed). */
void batch_window_toggle(app_state_t *app, bool from_explorer) {
    if (app->batch_window && !from_explorer) { batch_window_close(app); return; }
    if (!from_explorer) app->batch_list_count = 0;   /* opened by hand: the list now open */
    bool fresh = !app->batch_window;
    if (!batch_window_show(app)) { panel_open_batch(app); return; }   /* no window: the panel in the viewer, as before */
    if (fresh) memset(app->batch_panel.column_scroll, 0, sizeof(app->batch_panel.column_scroll));   /* opened again: from its top */
    batch_refresh(app);
}

static void owned_after(app_state_t *app) {
    int32_t after = app->batch_after;
    app->batch_after = BATCH_AFTER_NONE;
    switch (after) {
        case BATCH_AFTER_LIST:  if (!app->files_open) files_show(app); break;   /* the list in a window of its own, not under this one */
        case BATCH_AFTER_EDIT:  if (!edit_panel_open(app)) handle_action(app, PROVEN_LIT("open_edit")); break;
        case BATCH_AFTER_NAMES: names_window_open(app); break;
        case BATCH_AFTER_CLOSE: batch_window_close(app); break;
        case BATCH_AFTER_NAMES_CLOSE: names_window_close(app); batch_refresh(app); break;
        default: break;
    }
}

void batch_after(app_state_t *app, int32_t what) { app->batch_after = what; }

static void draw_owned(app_state_t *app, int which) {
    rubraview_window_t *win = which == 1 ? app->batch_window : app->names_window;
    rubraview_renderer_t *r = which == 1 ? app->batch_renderer : app->names_renderer;
    if (!win || !r) return;
    rubraview_renderer_t *viewer = app->renderer;
    double vx = app->pointer_x, vy = app->pointer_y;
    app->renderer = r;
    app->pointer_x = which == 1 ? app->batch_px : app->names_px;
    app->pointer_y = which == 1 ? app->batch_py : app->names_py;
    owned_panel_in(app, which);
    owned_window_layout(app, which);
    rubraview_pal_render_begin(app->renderer, BATCH_BACKGROUND);
    draw_panel_of(app, &app->panel, false);
    int32_t ww = 0, wh = 0;
    rubraview_pal_window_get_size(win, &ww, &wh);
    double dpi = rubraview_pal_window_dpi_scale(win);
    if (which == 2) draw_names_preview(app, (double)ww, (double)wh, dpi);
    if (app->rename_active && app->batch_typing && ((which == 2) == (app->names_window != NULL))) draw_rename_box(app, (double)ww, (double)wh, dpi);
    draw_chooser(app, app->renderer, which == 1 ? CHOOSE_IN_BATCH : CHOOSE_IN_NAMES, app->pointer_x, app->pointer_y);
    owned_panel_out(app);
    bool ok = rubraview_pal_render_end(app->renderer);
    app->renderer = viewer;
    app->pointer_x = vx;
    app->pointer_y = vy;
    if (!ok) {   /* its device went: made again */
        if (which == 1) { batch_window_close(app); batch_window_toggle(app, false); }
        else { names_window_close(app); names_window_open(app); }
    }
}

void draw_batch_window(app_state_t *app) {
    if (app->batch_window && app->batch_dirty) { app->batch_dirty = false; draw_owned(app, 1); }
    if (app->names_window && app->names_dirty) { app->names_dirty = false; draw_owned(app, 2); }
}

/* A press on an owned panel: the viewer's panel_handle_press without what is the viewer's (its title, a click
   outside closing it). */
static void owned_press(app_state_t *app, int which, double x, double y) {
    if (scrollbar_press_panel(app, &app->panel, x, y)) return;
    int32_t row = -1;
    rubraview_panel_event_t event = rubraview_panel_press(&app->panel, x, y, &row);
    if (event == RUBRAVIEW_PANEL_VALUE_CHANGED) panel_apply_row(app, row);
    else if (event == RUBRAVIEW_PANEL_CHOOSE) {
        rubraview_rect_t c = rubraview_panel_control_rect(&app->panel, (size_t)row);
        chooser_open_panel(app, which == 1 ? 3 : 4, row, c.x, c.y + c.height, c.y);
    } else if (event == RUBRAVIEW_PANEL_BUTTON_PRESSED) panel_button(app, row);
}

static size_t owned_pump(app_state_t *app, int which) {
    size_t handled = 0;
    rubraview_window_event_t event;
    for (;;) {
        rubraview_window_t *win = which == 1 ? app->batch_window : app->names_window;
        if (!win || !rubraview_pal_window_poll_event(win, &event)) break;
        handled++;
        if (which == 1) app->batch_dirty = true; else app->names_dirty = true;
        int where = which == 1 ? CHOOSE_IN_BATCH : CHOOSE_IN_NAMES;
        double *px = which == 1 ? &app->batch_px : &app->names_px, *py = which == 1 ? &app->batch_py : &app->names_py;
        if (event.kind == RUBRAVIEW_WINDOW_EVENT_CLOSE) { batch_after(app, which == 1 ? BATCH_AFTER_CLOSE : BATCH_AFTER_NAMES_CLOSE); owned_after(app); break; }
        if (event.kind == RUBRAVIEW_WINDOW_EVENT_KEY_DOWN) {
            if (chooser_key(app, where, event.key.combo)) continue;
            if (app->rename_active && app->batch_typing) {   /* a name or a folder typed here: the box takes the key */
                owned_panel_in(app, which);
                dispatch_key(app, event.key.combo);
                owned_panel_out(app);
                owned_after(app);
                if (which == 2) batch_refresh(app);
                continue;
            }
            if (key_is(event.key.combo, "Escape")) { batch_after(app, which == 1 ? BATCH_AFTER_CLOSE : BATCH_AFTER_NAMES_CLOSE); owned_after(app); break; }
            dispatch_key(app, event.key.combo);   /* the rest are the viewer's keys */
            continue;
        }
        if (event.kind == RUBRAVIEW_WINDOW_EVENT_TEXT || event.kind == RUBRAVIEW_WINDOW_EVENT_COMPOSITION) {
            if (app->batch_typing) rename_text(app, &event);
            continue;
        }
        owned_panel_in(app, which);
        owned_window_layout(app, which);
        switch (event.kind) {
            case RUBRAVIEW_WINDOW_EVENT_RESIZE:
                rubraview_pal_render_resize(which == 1 ? app->batch_renderer : app->names_renderer, event.resize.width, event.resize.height);
                owned_window_layout(app, which);
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE:
                *px = event.mouse.x; *py = event.mouse.y;
                if (scrollbar_motion(app, win, event.mouse.x, event.mouse.y)) break;
                if (app->panel.active_row >= 0) {
                    int32_t dragged = -1;
                    if (rubraview_panel_drag(&app->panel, event.mouse.x, event.mouse.y, &dragged) == RUBRAVIEW_PANEL_VALUE_CHANGED) panel_apply_row(app, dragged);
                }
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_LEAVE:
                *px = *py = -1.0;
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN:
                if (chooser_press(app, where, event.mouse.x, event.mouse.y)) break;
                if (app->rename_active && app->batch_typing && rename_box_press(app, event.mouse.x, event.mouse.y)) break;
                if (event.mouse.button != RUBRAVIEW_MOUSE_LEFT) break;
                owned_press(app, which, event.mouse.x, event.mouse.y);
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_UP:
                (void)scrollbar_release(app, win);
                rubraview_panel_release(&app->panel);
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_WHEEL:
                if (chooser_wheel(app, where, event.mouse.wheel_delta)) break;
                rubraview_panel_scroll_at(&app->panel, event.mouse.x, -event.mouse.wheel_delta * app->panel.row_height * 3.0);
                break;
            default:
                break;
        }
        owned_panel_out(app);
        owned_after(app);
        if (which == 2) batch_refresh(app);   /* the names row in Batch follows */
    }
    rubraview_window_t *win = which == 1 ? app->batch_window : app->names_window;
    bool *on = which == 1 ? &app->batch_text_on : &app->names_text_on;
    bool typing = app->rename_active && app->batch_typing != 0;
    if (win && typing != *on) { rubraview_pal_window_text_input(win, typing); *on = typing; }
    return handled;
}

size_t batch_pump(app_state_t *app) {
    return owned_pump(app, 1) + owned_pump(app, 2);
}

/* ---- the names window ---- */

void names_window_close(app_state_t *app) {
    if (!app->names_window) return;
    if (app->batch_typing && app->rename_active) { app->batch_typing = 0; rename_end(app); }
    if (app->chooser.open && app->chooser.where == CHOOSE_IN_NAMES) app->chooser.open = false;
    rubraview_pal_window_get_frame(app->names_window, &app->names_frame[0], &app->names_frame[1], &app->names_frame[2], &app->names_frame[3]);
    if (app->names_renderer) rubraview_pal_render_destroy(app->names_renderer);
    rubraview_pal_window_destroy(app->names_window); window_block_give(app, &app->names_block);
    app->names_renderer = NULL;
    app->names_window = NULL;
    app->names_panel.open = false;
}

void names_window_open(app_state_t *app) {
    if (app->names_window) return;
    rubraview_window_config_t config = { .title = T("Rubraview Batch: names"), .width = 900, .height = 640, .owner = app->batch_window ? app->batch_window : app->window };
    app->names_window = rubraview_pal_window_create(window_block_take(app, &app->names_block), &config);
    if (!app->names_window) { osd_say(app, TU8("the names window could not be opened")); return; }
    int32_t bx = 0, by = 0, bw = 0, bh = 0;
    int32_t right = app->batch_window && rubraview_pal_window_get_frame(app->batch_window, &bx, &by, &bw, &bh) ? bx + bw : 0;
    owned_window_frame(app, app->names_window, app->names_frame, right);
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->names_window, &w, &h);
    app->names_renderer = rubraview_pal_render_create(window_block_arena(app, &app->names_block), rubraview_pal_window_native_handle(app->names_window), w, h);
    if (!app->names_renderer) {
        rubraview_pal_window_destroy(app->names_window); window_block_give(app, &app->names_block);
        app->names_window = NULL;
        return;
    }
    ui_font_apply_all(app);
    app->names_px = app->names_py = -1.0;
    batch_preview_files(app);
    memset(app->names_panel.column_scroll, 0, sizeof(app->names_panel.column_scroll));   /* from its top */
    owned_panel_in(app, 2);
    names_panel_build(app);
    owned_panel_out(app);
    app->names_dirty = true;
}

static const char *const NAMES_TAKEN_FORMS[4] = { "2024-05-01", "20240501", "2024-05-01_132207", "240501" };

/* The names window's choices; built while its panel stands in. */
void names_panel_build(app_state_t *app) {
    struct batch_ui *b = &app->batch;
    double dpi = rubraview_pal_window_dpi_scale(app->names_window ? app->names_window : app->window);
    double scroll = app->panel_is_names ? app->panel.column_scroll[0] : 0.0;
    app->panel = rubraview_panel_create(TU8("Names"), dpi);
    app->panel.column_scroll[0] = scroll;
    app->panel_is_names = true;
    app->panel_is_batch = false;
    rubraview_panel_t *pn = &app->panel;
    static char labels[12 + RUBRAVIEW_BATCH_MAX_REPLACES][400];
    rubraview_panel_add_heading(pn, TU8("The name, from these parts"));
    rubraview_panel_add_button(pn, PANEL_NAMES_BEFORE, batch_row_label(labels[0], sizeof(labels[0]), T("Text before"), b->before, T("none")));
    rubraview_panel_add_toggle(pn, PANEL_NAMES_PARENT, TU8("The folder's name"), b->p_parent);
    rubraview_panel_add_toggle(pn, PANEL_NAMES_NAME, TU8("The file's own name"), b->p_name);
    rubraview_panel_add_toggle(pn, PANEL_NAMES_COUNTER, TU8("A number"), b->p_counter);
    if (b->p_counter) {
        rubraview_panel_add_slider(pn, PANEL_BATCH_START, TU8("  from"), b->start, 0, 99999, 1);
        rubraview_panel_add_slider(pn, PANEL_NAMES_STEP, TU8("  going up by"), b->n_step, 1, 1000, 1);
        rubraview_panel_add_slider(pn, PANEL_NAMES_DIGITS, TU8("  digits (zeros in front)"), b->digits, 1, 9, 1);
    }
    rubraview_panel_add_toggle(pn, PANEL_NAMES_SIZE, TU8("The picture's size (width x height)"), b->p_size);
    rubraview_panel_add_toggle(pn, PANEL_NAMES_TAKEN, TU8("The date it was taken (else the file's date)"), b->p_taken);
    if (b->p_taken) rubraview_panel_add_choice(pn, PANEL_NAMES_TAKEN_FORM, TU8("  written as"), b->taken_form, 4);
    rubraview_panel_add_button(pn, PANEL_NAMES_AFTER, batch_row_label(labels[1], sizeof(labels[1]), T("Text after"), b->after, T("none")));
    rubraview_panel_add_choice(pn, PANEL_NAMES_SEP, TU8("Between the parts"), b->sep, 4);
    rubraview_panel_add_heading(pn, TU8("Then"));
    rubraview_panel_add_choice(pn, PANEL_NAMES_CASE, TU8("Letters"), b->name_case, 3);
    rubraview_panel_add_toggle(pn, PANEL_NAMES_SPACES, TU8("Spaces become _"), b->no_spaces);
    size_t used = 0;
    for (size_t r = 0; r < RUBRAVIEW_BATCH_MAX_REPLACES && b->replace[r][0]; ++r, ++used) {
        const char *comma = strchr(b->replace[r], ',');
        snprintf(labels[2 + r], sizeof(labels[2 + r]), T("Replace \"%.*s\" with \"%s\" (press: change; empty: remove)"),
                 comma ? (int)(comma - b->replace[r]) : (int)strlen(b->replace[r]), b->replace[r], comma ? comma + 1 : "");
        rubraview_panel_add_button(pn, PANEL_NAMES_REPLACE0 + (int32_t)r, cstr(labels[2 + r]));
    }
    if (used < RUBRAVIEW_BATCH_MAX_REPLACES) rubraview_panel_add_button(pn, PANEL_NAMES_ADD_REPLACE, TU8("Replace words in the name..."));
    rubraview_panel_add_choice(pn, PANEL_BATCH_EXISTS, TU8("A name already there"), b->exists, 3);
    rubraview_panel_add_heading(pn, TU8("The pattern"));
    snprintf(labels[10], sizeof(labels[10]), "%s%s", b->name, b->own_pattern ? T("  (typed: a part changed makes it again)") : "");
    rubraview_panel_add_button(pn, PANEL_NAMES_PATTERN, batch_row_label(labels[11], sizeof(labels[11]), T("Pattern (press: type your own)"), labels[10], "{name}.{ext}"));
    rubraview_panel_add_button(pn, PANEL_NAMES_DONE, TU8("Done (Esc)"));
    owned_measure_labels(app, app->names_renderer, app->names_window);
    app->panel.open = true;
    owned_window_layout(app, 2);
}

const char *names_choice_word(int32_t id, int32_t index) {
    static const char *const SEPS[4] = { N_("_  (underscore)"), N_("-  (hyphen)"), N_("a space"), N_("nothing") };
    static const char *const CASES[3] = { N_("as they are"), N_("lower case"), N_("UPPER CASE") };
    if (id == PANEL_NAMES_SEP) return rubraview_tr(SEPS[(unsigned)index % 4u]);
    if (id == PANEL_NAMES_CASE) return rubraview_tr(CASES[(unsigned)index % 3u]);
    if (id == PANEL_NAMES_TAKEN_FORM) return NAMES_TAKEN_FORMS[(unsigned)index % 4u];
    return NULL;
}

/* The preview: each file's name now, and what it will be; a name met twice, or there already, marked. */
void draw_names_preview(app_state_t *app, double win_w, double win_h, double dpi) {
    double x0 = app->panel.bounds.x + app->panel.bounds.width + 12.0 * dpi, w = win_w - x0 - 12.0 * dpi;
    if (w < 80.0 * dpi) return;
    double row = 26.0 * dpi, font = 13.0 * dpi, y = 12.0 * dpi;
    rubraview_pal_render_draw_text(app->renderer, TU8("Preview: the first files, now -> new"), (rubraview_pal_rect_t){ x0, y, w, row }, 14.0 * dpi, 0xFFFFC14Du, RUBRAVIEW_TEXT_LEFT);
    y += row * 1.3;
    if (app->names_file_count == 0) {
        rubraview_pal_render_draw_text(app->renderer, TU8("no pictures in the list now open"), (rubraview_pal_rect_t){ x0, y, w, row }, font, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
        return;
    }
    void *mem = malloc(4u << 20);
    if (!mem) return;
    proven_arena_t scratch = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t *)mem, .size = 4u << 20 });
    proven_u8str_view_t made[12];
    for (size_t i = 0; i < app->names_file_count; ++i) made[i] = batch_preview_name(app, i, &scratch);
    proven_u8str_view_t beside = rubraview_path_dirname(app->names_files[0]);
    proven_u8str_view_t out_dir = app->batch.out[0] ? cstr(app->batch.out) : rubraview_path_join(&scratch, beside, PROVEN_LIT("rubraview-out"));
    size_t clashes = 0;
    for (size_t i = 0; i < app->names_file_count && y + row * 2.0 < win_h; ++i) {
        bool twice = false;
        for (size_t k = 0; k < app->names_file_count && !twice; ++k) twice = k != i && rubraview_path_same(made[k], made[i]);
        bool there = !rubraview_batch_name_wants_size(made[i]) && rubraview_pal_fs_exists(rubraview_path_join(&scratch, out_dir, made[i]));
        if (twice || there) clashes++;
        proven_u8str_view_t old = rubraview_path_basename(app->names_files[i]);
        char line[700];
        int n = snprintf(line, sizeof(line), "%.*s", (int)old.size, (const char *)old.ptr);
        rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = n > 0 ? (size_t)n : 0 },
                                       (rubraview_pal_rect_t){ x0, y, w, row }, font, 0xFFB0B0B0u, RUBRAVIEW_TEXT_LEFT);
        y += row * 0.85;
        n = snprintf(line, sizeof(line), "  -> %.*s%s", (int)made[i].size, (const char *)made[i].ptr,
                     twice ? T("   ! the same as another") : there ? T("   ! there already") : "");
        rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = n > 0 ? (size_t)n : 0 },
                                       (rubraview_pal_rect_t){ x0, y, w, row }, font, twice || there ? 0xFFFF7070u : COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
        y += row * 1.1;
    }
    char foot[300];
    int n = snprintf(foot, sizeof(foot), "%s%s", rubraview_batch_name_wants_size(made[0]) ? T("{w}x{h}: the size each file comes out at. ") : "",
                     clashes ? T("A marked name follows \"A name already there\".") : "");
    if (n > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)foot, .size = (size_t)n },
                                              (rubraview_pal_rect_t){ x0, y + row * 0.4, w, row * 2.0 }, font, 0xFFB0B0B0u, RUBRAVIEW_TEXT_LEFT);
    free(mem);
}
