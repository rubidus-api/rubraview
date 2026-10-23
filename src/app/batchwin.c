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
    double v = b->size_value;
    switch (b->size_by) {
        case 1: if (fabs(v - 100.0) > 0.01) batch_token(tk, "--resize=%g%%", v); break;
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
    if (b->replace[0] && strchr(b->replace, ',') && b->replace[0] != ',') batch_token(tk, "--replace=%s", b->replace);
    if (b->exists == 1) batch_token(tk, "--exists=skip");
    if (b->exists == 2) batch_token(tk, "--exists=number");
    if (b->keep_dates) batch_token(tk, "--keep-dates");
    if (b->recursive) batch_token(tk, "--recursive");
    if (b->subfolders) batch_token(tk, "--subfolders");
    if (b->out[0]) batch_token(tk, "--out=%s", b->out);
}

static void batch_defaults(struct batch_ui *b) {
    *b = (struct batch_ui){ .loaded = true, .size_by = 0, .size_value = 100.0, .box_h = 1080.0, .filter = 3, .effect_amount = 0.0,
                            .mark_corner = 0, .mark_opacity = 50.0, .quality = 90.0, .start = 1.0, .text_size = 4.0 };
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
    if (r.job.replace_from.size > 0)
        snprintf(b->replace, sizeof(b->replace), "%.*s,%.*s", (int)r.job.replace_from.size, r.job.replace_from.ptr, (int)r.job.replace_to.size, r.job.replace_to.ptr);
    b->exists = r.exists; b->keep_dates = r.keep_dates; b->recursive = r.recursive; b->subfolders = r.subfolders;
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
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    bool placed = app->panel.placed && app->panel_is_batch;
    double px = app->panel.place_x, py = app->panel.place_y;
    app->panel = rubraview_panel_create(TU8("Batch"), dpi);
    if (placed) { app->panel.placed = true; app->panel.place_x = px; app->panel.place_y = py; }
    app->panel_is_export = false;
    app->panel_is_batch = true;
    app->panel_is_print = false;
    app->panel_is_convert = false;
    rubraview_panel_t *pn = &app->panel;
    static char labels[8][1100];

    /* The size */
    rubraview_panel_add_choice(pn, PANEL_BATCH_SIZE_BY, TU8("Size"), b->size_by, 8);
    if (b->size_by > 0) {
        double lo = 1, hi = 16000, step = 1;
        if (b->size_by == 1) { lo = 1; hi = 1000; }
        if (b->size_by == 7) { lo = 0.1; hi = 200; step = 0.1; }
        rubraview_panel_add_slider(pn, PANEL_BATCH_RESIZE, b->size_by == 6 ? TU8("  width") : TU8("  value"), b->size_value, lo, hi, step);
        if (b->size_by == 6) rubraview_panel_add_slider(pn, PANEL_BATCH_BOX_H, TU8("  height"), b->box_h, 1, 16000, 1);
        rubraview_panel_add_choice(pn, PANEL_BATCH_FILTER, TU8("  Filter"), b->filter, 4);
        rubraview_panel_add_toggle(pn, PANEL_BATCH_LINEAR, TU8("  In linear light"), b->linear);
        rubraview_panel_add_toggle(pn, PANEL_BATCH_NO_ENLARGE, TU8("  Never enlarge"), b->no_enlarge);
    }
    rubraview_panel_add_slider(pn, PANEL_BATCH_SHARPEN, TU8("Sharpen"), b->sharpen, 0, 300, 5);
    rubraview_panel_add_separator(pn);
    /* Turning and cutting */
    rubraview_panel_add_choice(pn, PANEL_BATCH_ROTATE, TU8("Turn"), b->rotate, 5);
    rubraview_panel_add_choice(pn, PANEL_BATCH_FLIP, TU8("Mirror"), b->flip, 3);
    rubraview_panel_add_button(pn, PANEL_BATCH_CROP, batch_row_label(labels[0], sizeof(labels[0]), T("Crop X,Y,W,H"), b->crop, T("none")));
    rubraview_panel_add_toggle(pn, PANEL_BATCH_AUTOCROP, TU8("Cut plain borders"), b->autocrop);
    rubraview_panel_add_separator(pn);
    /* Colour and effects */
    rubraview_panel_add_toggle(pn, PANEL_BATCH_AUTOCOLOUR, TU8("Automatic colour"), b->auto_colour);
    rubraview_panel_add_toggle(pn, PANEL_BATCH_GRAY, TU8("Grayscale"), b->grayscale);
    rubraview_panel_add_toggle(pn, PANEL_BATCH_NEGATIVE, TU8("Negative"), b->negative);
    rubraview_panel_add_choice(pn, PANEL_BATCH_EFFECT, TU8("Effect"), b->effect, RUBRAVIEW_EFFECT_COUNT);
    if (b->effect > 0) {
        double lo = 0, hi = 1;
        rubraview_effect_range((rubraview_effect_t)b->effect, &lo, &hi, NULL);
        rubraview_panel_add_slider(pn, PANEL_BATCH_EFFECT_AMOUNT, TU8("  amount"), b->effect_amount, lo, hi, 1);
    }
    rubraview_panel_add_separator(pn);
    /* Frame, canvas, watermark */
    rubraview_panel_add_button(pn, PANEL_BATCH_CANVAS, batch_row_label(labels[1], sizeof(labels[1]), T("Canvas WxH"), b->canvas, T("none")));
    rubraview_panel_add_slider(pn, PANEL_BATCH_BORDER, TU8("Border px"), b->border, 0, 500, 1);
    rubraview_panel_add_choice(pn, PANEL_BATCH_FRAME_COLOUR, TU8("  their colour"), b->frame_colour, 3);
    {
        proven_u8str_view_t mark = cstr(b->mark);
        proven_u8str_view_t base = rubraview_path_basename(mark);
        char shown[300];
        snprintf(shown, sizeof(shown), "%.*s", (int)(base.size < 200 ? base.size : 200), base.ptr);
        rubraview_panel_add_button(pn, PANEL_BATCH_MARK, batch_row_label(labels[2], sizeof(labels[2]), T("Watermark"), shown, T("none (choose a picture)")));
    }
    if (b->mark[0]) {
        rubraview_panel_add_choice(pn, PANEL_BATCH_MARK_CORNER, TU8("  where"), b->mark_corner, 5);
        rubraview_panel_add_slider(pn, PANEL_BATCH_MARK_OPACITY, TU8("  opacity %"), b->mark_opacity, 1, 100, 1);
    }
    rubraview_panel_add_separator(pn);
    /* What is written */
    /* RV-134: words on each picture */
    rubraview_panel_add_button(pn, PANEL_BATCH_TEXT, batch_row_label(labels[7], sizeof(labels[7]), T("Text"), b->text, T("none")));
    if (b->text[0]) {
        rubraview_panel_add_choice(pn, PANEL_BATCH_TEXT_CORNER, TU8("  where"), b->text_corner, 5);
        rubraview_panel_add_slider(pn, PANEL_BATCH_TEXT_SIZE, TU8("  size %"), b->text_size, 1, 50, 1);
        rubraview_panel_add_choice(pn, PANEL_BATCH_TEXT_COLOUR, TU8("  colour"), b->text_colour, 3);
    }
    rubraview_panel_add_separator(pn);
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
    rubraview_panel_add_separator(pn);
    /* Names and places */
    rubraview_panel_add_button(pn, PANEL_BATCH_NAME, batch_row_label(labels[3], sizeof(labels[3]), T("Names"), b->name, "{name}.{ext}"));
    rubraview_panel_add_slider(pn, PANEL_BATCH_START, TU8("  {n} from"), b->start, 0, 99999, 1);
    rubraview_panel_add_button(pn, PANEL_BATCH_REPLACE, batch_row_label(labels[4], sizeof(labels[4]), T("Replace in names (from,to)"), b->replace, T("nothing")));
    rubraview_panel_add_choice(pn, PANEL_BATCH_EXISTS, TU8("A name already there"), b->exists, 3);
    rubraview_panel_add_toggle(pn, PANEL_BATCH_KEEP_DATES, TU8("Keep the files' dates"), b->keep_dates);
    rubraview_panel_add_toggle(pn, PANEL_BATCH_RECURSIVE, TU8("Folders inside too"), b->recursive);
    if (b->recursive) rubraview_panel_add_toggle(pn, PANEL_BATCH_SUBFOLDERS, TU8("  the same folders in the output"), b->subfolders);
    rubraview_panel_add_button(pn, PANEL_BATCH_OUT, batch_row_label(labels[5], sizeof(labels[5]), T("Into"), b->out, T("rubraview-out beside them")));
    rubraview_panel_add_separator(pn);
    rubraview_panel_add_button(pn, PANEL_BATCH_TEST, TU8("Try the names on the first files"));
    rubraview_panel_add_button(pn, PANEL_BATCH_SAVE, TU8("Save these choices..."));
    rubraview_panel_add_button(pn, PANEL_BATCH_LOAD, TU8("Load choices..."));
    if (app->batch_list_count > 0) {
        /* D-86: "Convert..." from Explorer: these files, not the folder being read. */
        snprintf(labels[6], sizeof(labels[6]), T("Run on the %zu chosen file%s"), app->batch_list_count, app->batch_list_count == 1 ? "" : TC("plural", "s"));
        rubraview_panel_add_button(pn, PANEL_BATCH_RUN, cstr(labels[6]));
    } else {
        rubraview_panel_add_button(pn, PANEL_BATCH_RUN, TU8("Run on this folder"));
    }

    app->panel.open = true;
    panel_relayout(app);
}

/* A row whose value is text: typed in the rename box. */
void batch_type_begin(app_state_t *app, int32_t id) {
    const char *now = id == PANEL_BATCH_TEXT ? app->batch.text : id == PANEL_BATCH_CROP ? app->batch.crop : id == PANEL_BATCH_CANVAS ? app->batch.canvas
                    : id == PANEL_BATCH_NAME ? app->batch.name : id == PANEL_BATCH_REPLACE ? app->batch.replace : app->batch.out;
    if (app->rename_active) rename_end(app);
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    rubraview_textedit_set(&app->rename_edit, cstr(now));
    rubraview_textedit_select_all(&app->rename_edit);
    app->rename_active = true;
    app->rename_composing_length = 0;
    app->batch_typing = id;
    rubraview_pal_window_text_input(app->window, true);
    osd_say(app, id == PANEL_BATCH_TEXT ? TU8("the words: {name} {n} {n:3} {parent} {date} {w} {h} work here too; Enter (empty: none)")
               : id == PANEL_BATCH_NAME ? TU8("{name} {ext} {n} {n:3} {parent} {date} {w} {h}, then Enter (empty: as it was)")
               : id == PANEL_BATCH_CROP ? TU8("X,Y,W,H in pixels, then Enter (empty: no crop)")
               : id == PANEL_BATCH_CANVAS ? TU8("width x height, e.g. 1920x1080, then Enter (empty: none)")
               : id == PANEL_BATCH_REPLACE ? TU8("what,with: IMG_,photo-  then Enter (empty: nothing)")
               : TU8("a folder, then Enter (empty: rubraview-out beside the files)"));
}

void batch_typed(app_state_t *app) {
    proven_u8str_view_t text = rubraview_textedit_text(&app->rename_edit);
    int32_t id = app->batch_typing;
    char *dst = id == PANEL_BATCH_TEXT ? app->batch.text : id == PANEL_BATCH_CROP ? app->batch.crop : id == PANEL_BATCH_CANVAS ? app->batch.canvas
              : id == PANEL_BATCH_NAME ? app->batch.name : id == PANEL_BATCH_REPLACE ? app->batch.replace : app->batch.out;
    size_t cap = id == PANEL_BATCH_TEXT ? sizeof(app->batch.text) : id == PANEL_BATCH_CROP ? sizeof(app->batch.crop) : id == PANEL_BATCH_CANVAS ? sizeof(app->batch.canvas)
               : id == PANEL_BATCH_NAME ? sizeof(app->batch.name) : id == PANEL_BATCH_REPLACE ? sizeof(app->batch.replace) : sizeof(app->batch.out);
    char was[1024];
    snprintf(was, sizeof(was), "%s", dst);
    size_t n = text.size < cap - 1 ? text.size : cap - 1;
    memcpy(dst, text.ptr, n);
    dst[n] = '\0';
    if (id == PANEL_BATCH_NAME && n == 0) snprintf(dst, cap, "%s", "{name}.{ext}");
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
    panel_open_batch(app);
}

/* The first files' names as the run would write them. */
void batch_try_names(app_state_t *app) {
    batch_tokens_t tk;
    batch_tokens(&app->batch, &tk);
    const char *argv[70];
    int argc = 0;
    argv[argc++] = "rubraview"; argv[argc++] = "--batch";
    for (size_t i = 0; i < tk.n && argc < 68; ++i) argv[argc++] = tk.v[i];
    argv[argc++] = "in";
    static rubraview_cli_result_t r;
    rubraview_cli_parse(&r, app->arena, argc, argv);
    if (r.err != RUBRAVIEW_CLI_OK) { osd_say(app, TU8("the choices do not make a run")); return; }
    rubraview_batch_input_t in[3];
    size_t count = 0;
    for (size_t i = 0; i < app->batch_list_count && count < 3; ++i) in[count++] = (rubraview_batch_input_t){ app->batch_list[i], 1 };
    for (size_t i = 0; app->batch_list_count == 0 && i < page_count(app) && count < 3; ++i)
        in[count++] = (rubraview_batch_input_t){ app->source.pages[i].path, 1 };
    if (count == 0) { osd_say(app, TU8("no files to try the names on")); return; }
    char said[sizeof(app->notice)];
    size_t at = 0;
    struct names_ctx { char *said; size_t *at; size_t cap; } nc = { said, &at, sizeof(said) };
    said[0] = '\0';
    void *mem = malloc(1u << 20);
    if (!mem) return;
    proven_arena_t scratch = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)mem, .size = 1u << 20 });
    for (size_t i = 0; i < count; ++i) {
        proven_arena_reset(&scratch);
        proven_u8str_view_t name = rubraview_path_basename(in[i].path);
        proven_u8str_view_t stem = rubraview_batch_replace_all(&scratch, rubraview_path_stem(name), r.job.replace_from, r.job.replace_to);
        proven_u8str_view_t ext = rubraview_path_ext(name);
        if (ext.size > 0 && ext.ptr[0] == '.') { ext.ptr++; ext.size--; }
        if (r.export_options.format != RUBRAVIEW_EXPORT_SAME_AS_SOURCE) ext = rubraview_export_extension(r.export_options.format);
        proven_u8str_view_t out = rubraview_batch_format_name_ex(&scratch, r.job.naming_pattern, stem, ext, 0, 0, PROVEN_LIT(""),
                                                     (int64_t)(r.job.counter_start > 0 ? r.job.counter_start : 1) + (int64_t)i,
                                                     rubraview_path_basename(rubraview_path_dirname(in[i].path)));
        int n = snprintf(nc.said + *nc.at, nc.cap - *nc.at, "%s%.*s > %.*s", i ? ";  " : "", (int)(name.size < 60 ? name.size : 60), name.ptr,
                         (int)(out.size < 60 ? out.size : 60), out.ptr);
        if (n > 0 && (size_t)n < nc.cap - *nc.at) *nc.at += (size_t)n;
    }
    free(mem);
    osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)said, .size = at });
    app->notice_seconds = 8.0;
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
