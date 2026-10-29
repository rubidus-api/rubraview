/*
 * Headless batch mode and the console it speaks to, a new window, a batch run from the panel (§3.11, RV-017;
 * RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* ---- headless batch mode (§3.11, RV-017) ---- */

/* Writes a line to the console the shell already owns, if there is one.
   A GUI subsystem program has no stdout until it asks for its parent's,
   which is what makes `rubraview.exe --batch ... | more` work at all. */
/* Where a batch run's report goes. Three cases, and they are not the
   same handle:

   - started from a command prompt: the parent's console, and a redirect
     (`> log.txt`) must still reach the file, so the standard handle wins
     when there is one;
   - started by the dialog with a console of its own (CREATE_NEW_CONSOLE):
     this is a GUI-subsystem program, so its standard handles are empty
     and the console has to be opened by name — CONOUT$;
   - no console anywhere: nothing is written, and nothing waits.

   This was found on the VM: the run in its own window printed nothing and
   the window closed the moment it finished. */
static HANDLE console_handle(bool input) {
    HANDLE std = GetStdHandle(input ? STD_INPUT_HANDLE : STD_OUTPUT_HANDLE);
    if (std && std != INVALID_HANDLE_VALUE) return std;

    static bool attached = false;
    if (!attached) {
        attached = true;
        if (!AttachConsole(ATTACH_PARENT_PROCESS) && GetConsoleWindow() == NULL) AllocConsole();
    }
    if (GetConsoleWindow() == NULL) return NULL;
    HANDLE named = CreateFileW(input ? L"CONIN$" : L"CONOUT$",
                               GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);
    return named == INVALID_HANDLE_VALUE ? NULL : named;
}

void console_line(const char *text) {
    HANDLE out = console_handle(false);
    if (!out) return;

    /* A console takes UTF-16: writing UTF-8 bytes into one leaves them to
       be read as the code page of the day, which turned an em dash into
       "??" (and would do worse to a Korean filename). A pipe or a file
       gets the UTF-8 bytes, which is what a redirect wants. */
    DWORD mode = 0;
    DWORD written = 0;
    if (GetConsoleMode(out, &mode)) {
        WCHAR wide[1024];
        int count = MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, 1022);
        if (count > 1) {
            wide[count - 1] = L'\r';
            wide[count] = L'\n';
            WriteConsoleW(out, wide, (DWORD)(count + 1), &written, NULL);
            return;
        }
    }
    WriteFile(out, text, (DWORD)strlen(text), &written, NULL);
    WriteFile(out, "\r\n", 2, &written, NULL);
}

/* Defined with run_batch below (D-86); --subfolders reads them. */
const proven_u8str_view_t *g_launch_paths;
size_t         g_launch_count;

typedef struct batch_ctx {
    const rubraview_cli_result_t *cli;
    proven_u8str_view_t output_dir;
    size_t written;
    proven_job_sys_t *jobs;   /* RV-067's pool for resizes on the CPU; NULL on one core */
    size_t workers;
    /* RV-129: the watermark, read once for the run */
    rubraview_pixbuf_t mark;
    bool mark_tried;
    void *mark_memory;
    size_t seen;              /* RV-134: files begun so far, for {n} in a text */
    int64_t counter;          /* the {n} of the file in hand */
} batch_ctx_t;

static rubraview_orientation_t orientation_from_action(const rubraview_batch_orient_params_t *o) {
    rubraview_orientation_t out = rubraview_orientation_identity();
    for (int32_t turns = o->rotate_degrees / 90; turns > 0; --turns) {
        out = rubraview_orientation_rotate_cw(out);
    }
    if (o->flip_horizontal) out = rubraview_orientation_flip_h(out);
    if (o->flip_vertical) out = rubraview_orientation_flip_v(out);
    return out;
}

static rubraview_jpegtran_op_t jpegtran_op_for(const rubraview_batch_orient_params_t *o) {
    if (o->flip_horizontal && o->rotate_degrees == 0) return RUBRAVIEW_JPEGTRAN_FLIP_H;
    if (o->flip_vertical && o->rotate_degrees == 0) return RUBRAVIEW_JPEGTRAN_FLIP_V;
    if (o->flip_horizontal || o->flip_vertical) return RUBRAVIEW_JPEGTRAN_NONE; /* combined: not lossless */
    switch (o->rotate_degrees) {
        case 90: return RUBRAVIEW_JPEGTRAN_ROT_90;
        case 180: return RUBRAVIEW_JPEGTRAN_ROT_180;
        case 270: return RUBRAVIEW_JPEGTRAN_ROT_270;
        default: return RUBRAVIEW_JPEGTRAN_NONE;
    }
}

static int32_t resize_target(const rubraview_batch_resize_params_t *r,
                             int32_t src_w, int32_t src_h, int32_t *out_h) {
    int32_t w = 0;
    rubraview_batch_resize_target(r, src_w, src_h, &w, out_h);   /* RV-129: the core's, with the long side, megapixels, no enlarging */
    return w;
}

/* One line on the run's console naming the file and what went wrong: a
   count of failures says nothing about which or why. */
static void batch_say(proven_u8str_view_t path, const char *why) {
    char line[600];
    int n = snprintf(line, sizeof(line), "rubraview: %.*s: %s", (int)(path.size < 500 ? path.size : 500), path.ptr, why);
    if (n > 0) console_line(line);
}

/* RV-129: --keep-dates: the file written takes its source's modified time. */
static void batch_keep_date(proven_arena_t *arena, const batch_ctx_t *bc, proven_u8str_view_t source, proven_u8str_view_t written) {
    if (!bc->cli->keep_dates) return;
    rubraview_fs_entry_t e;
    if (!rubraview_pal_fs_stat(arena, source, &e) || !rubraview_pal_fs_set_mtime(written, e.mtime)) batch_say(written, "its date could not be set");
}

/* RV-187 (D-220): Edit's steps from --steps=, read once; each picture gets its own copy, fitted to its size. */
static rubraview_edit_steps_t g_batch_steps;
static bool g_batch_has_steps;

/* Decode, turn, resize, adjust, save. A step that cannot be done fails the
   file: writing it without the step would say it was done. */
static bool batch_pixels(proven_arena_t *arena, batch_ctx_t *bc, const rubraview_batch_input_t *input,
                         proven_u8str_view_t out_path, const rubraview_batch_orient_params_t *orient,
                         const rubraview_batch_resize_params_t *resize, const rubraview_batch_color_params_t *color,
                         const rubraview_batch_extra_params_t *extra,
                         rubraview_export_options_t options, rubraview_export_format_t source_format) {
    rubraview_pixbuf_t pixels = rubraview_pal_image_read_pixels(arena, input->path, NULL, 0,
                                                                orient && orient->use_exif_auto_orient);
    if (!rubraview_pixbuf_is_valid(&pixels)) { batch_say(input->path, "could not be read"); return false; }

    /* RV-129: the cut first, on the picture as it was read */
    if (extra && extra->crop) {
        int32_t x = extra->crop_x < pixels.width - 1 ? extra->crop_x : pixels.width - 1;
        int32_t y = extra->crop_y < pixels.height - 1 ? extra->crop_y : pixels.height - 1;
        int32_t w = extra->crop_w < pixels.width - x ? extra->crop_w : pixels.width - x;
        int32_t h = extra->crop_h < pixels.height - y ? extra->crop_h : pixels.height - y;
        rubraview_pixbuf_t cut = rubraview_pixbuf_crop(arena, &pixels, x, y, w, h);
        if (!rubraview_pixbuf_is_valid(&cut)) { batch_say(input->path, "could not be cropped"); return false; }
        pixels = cut;
    }
    if (extra && extra->autocrop) {
        int32_t x = 0, y = 0, w = 0, h = 0;
        bool found = (rubraview_filter_autotrim_rect(&pixels, 245, true, &x, &y, &w, &h) && (w < pixels.width || h < pixels.height)) ||
                     (rubraview_filter_autotrim_rect(&pixels, 15, false, &x, &y, &w, &h) && (w < pixels.width || h < pixels.height));
        if (found && w >= 2 && h >= 2) {
            rubraview_pixbuf_t cut = rubraview_pixbuf_crop(arena, &pixels, x, y, w, h);
            if (!rubraview_pixbuf_is_valid(&cut)) { batch_say(input->path, "could not be cropped"); return false; }
            pixels = cut;
        }
    }

    if (orient && !orient->use_exif_auto_orient) {
        rubraview_orientation_t o = orientation_from_action(orient);
        rubraview_pixbuf_t turned = rubraview_pixbuf_orient(arena, &pixels, o);
        if (!rubraview_pixbuf_is_valid(&turned)) { batch_say(input->path, "could not be turned"); return false; }
        pixels = turned;
    }

    if (g_batch_has_steps) {   /* RV-187: Edit's steps, on the picture as it is turned, before Batch's own size and frame */
        static rubraview_edit_steps_t steps;
        steps = g_batch_steps;
        rubraview_edit_steps_rebase(&steps, pixels.width, pixels.height);
        for (size_t i = 0; i < steps.cursor; ++i) {
            rubraview_pixbuf_t done = rubraview_edit_step_apply(arena, &steps.steps[i], &pixels, 1.0);
            if (!rubraview_pixbuf_is_valid(&done)) {
                char said[700];
                snprintf(said, sizeof(said), "Edit's step %u did not run: %s", (unsigned)(i + 1), g_plugin_why ? g_plugin_why : "out of memory");
                batch_say(input->path, said);
                return false;
            }
            pixels = done;
        }
    }

    if (resize) {
        int32_t target_h = 0;
        int32_t target_w = resize_target(resize, pixels.width, pixels.height, &target_h);
        if (target_w != pixels.width || target_h != pixels.height) {
            /* The card when D-38 installed it and the picture is worth it;
               otherwise every core (RV-067), the same bytes as one. */
            rubraview_pixbuf_t scaled = resize->linear   /* RV-128: in linear light, on one core */
                ? rubraview_pixbuf_resample_linear(arena, &pixels, target_w, target_h, resize->filter)
                : rubraview_pixbuf_resample_mt(arena, bc->jobs, bc->workers, &pixels, target_w, target_h, resize->filter, 0);
            if (!rubraview_pixbuf_is_valid(&scaled)) { batch_say(input->path, "could not be resized"); return false; }
            pixels = scaled;
        }
    }

    if (color) {
        if (color->has_color_adjust) rubraview_color_adjust(&pixels, &color->adjust);
        if (color->grayscale) {
            rubraview_color_adjust_params_t gray = { .exposure_ev = 0.0f, .contrast = 0.0f,
                                                     .saturation = 0.0f, .gamma = 1.0f };
            rubraview_color_adjust(&pixels, &gray);
        }
        if (color->has_unsharp) {
            rubraview_pixbuf_t sharp = rubraview_filter_unsharp_mask(arena, &pixels, color->sigma,
                                                                     color->amount, color->threshold);
            if (!rubraview_pixbuf_is_valid(&sharp)) { batch_say(input->path, "could not be sharpened"); return false; }
            pixels = sharp;
        }
    }

    if (extra) {   /* RV-129 */
        if (extra->auto_colour) {
            rubraview_edit_session_t s = rubraview_edit_begin(pixels.width, pixels.height);
            rubraview_edit_auto_colour(&s, &pixels);
            rubraview_pixbuf_t fixed = rubraview_edit_commit(arena, &s, &pixels);
            if (!rubraview_pixbuf_is_valid(&fixed)) { batch_say(input->path, "the colours could not be corrected"); return false; }
            pixels = fixed;
        }
        if (extra->negative || extra->effect > 0) {
            rubraview_effect_t list[2] = { extra->negative ? RUBRAVIEW_EFFECT_NEGATIVE : RUBRAVIEW_EFFECT_NONE, (rubraview_effect_t)extra->effect };
            for (int k = 0; k < 2; ++k) {
                if (list[k] <= RUBRAVIEW_EFFECT_NONE) continue;
                double amount = k == 0 ? 1.0 : extra->effect_amount;
                rubraview_pixbuf_t done = rubraview_effect_apply(arena, &pixels, list[k], amount);
                if (!rubraview_pixbuf_is_valid(&done)) { batch_say(input->path, "the effect could not be applied"); return false; }
                pixels = done;
            }
        }
        if (extra->plugin[0]) {   /* RFC-0007: a plug-in, named on the command line */
            rubraview_edit_step_t step = { .kind = RUBRAVIEW_STEP_PLUGIN, .plugin_param_count = extra->plugin_param_count };
            snprintf(step.plugin, sizeof(step.plugin), "%s", extra->plugin);
            memcpy(step.plugin_params, extra->plugin_params, sizeof(step.plugin_params));
            rubraview_pixbuf_t done = plugin_runner(NULL, arena, &step, &pixels);
            if (!rubraview_pixbuf_is_valid(&done)) {
                char said[400];
                snprintf(said, sizeof(said), "the plug-in did not run: %s", g_plugin_why ? g_plugin_why : "?");
                batch_say(input->path, said);
                return false;
            }
            pixels = done;
        }
        if (extra->gmic.size > 0) {   /* RFC-0009 C: a G'MIC command, by gmic.exe */
            rubraview_edit_step_t step = { .kind = RUBRAVIEW_STEP_GMIC };
            memcpy(step.command, extra->gmic.ptr, extra->gmic.size < sizeof(step.command) ? extra->gmic.size : sizeof(step.command) - 1);
            rubraview_pixbuf_t done = plugin_runner(NULL, arena, &step, &pixels);
            if (!rubraview_pixbuf_is_valid(&done)) {
                char said[700];
                snprintf(said, sizeof(said), "G'MIC did not run: %s", g_plugin_why ? g_plugin_why : "?");
                batch_say(input->path, said);
                return false;
            }
            pixels = done;
        }
        if (extra->ffmpeg_filter.size > 0) {   /* RFC-0009 D: an FFmpeg filter graph, by avfilter */
            rubraview_edit_step_t step = { .kind = RUBRAVIEW_STEP_FFMPEG };
            memcpy(step.command, extra->ffmpeg_filter.ptr, extra->ffmpeg_filter.size < sizeof(step.command) ? extra->ffmpeg_filter.size : sizeof(step.command) - 1);
            rubraview_pixbuf_t done = plugin_runner(NULL, arena, &step, &pixels);
            if (!rubraview_pixbuf_is_valid(&done)) {
                char said[700];
                snprintf(said, sizeof(said), "the FFmpeg filter did not run: %s", g_plugin_why ? g_plugin_why : "?");
                batch_say(input->path, said);
                return false;
            }
            pixels = done;
        }
        if (extra->canvas_w > 0 && extra->canvas_h > 0) {
            rubraview_pixbuf_t placed = rubraview_batch_canvas(arena, &pixels, extra->canvas_w, extra->canvas_h, extra->canvas_rgb);
            if (!rubraview_pixbuf_is_valid(&placed)) { batch_say(input->path, "the canvas could not be made"); return false; }
            pixels = placed;
        }
        if (extra->border_px > 0) {
            rubraview_pixbuf_t framed = rubraview_batch_border(arena, &pixels, extra->border_px, extra->border_rgb);
            if (!rubraview_pixbuf_is_valid(&framed)) { batch_say(input->path, "the border could not be made"); return false; }
            pixels = framed;
        }
        if (extra->watermark.size > 0) {
            if (!bc->mark_tried) {   /* read once, into memory of its own that outlives every file's */
                bc->mark_tried = true;
                size_t room = 64u << 20;
                bc->mark_memory = malloc(room);
                if (bc->mark_memory) {
                    proven_arena_t ma = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)bc->mark_memory, .size = room });
                    bc->mark = rubraview_pal_image_read_pixels_within(&ma, extra->watermark, NULL, 0, true, 2048, 2048);
                }
                if (!rubraview_pixbuf_is_valid(&bc->mark)) batch_say(extra->watermark, "the watermark could not be read");
            }
            rubraview_pixbuf_t mark = bc->mark;
            if (!rubraview_pixbuf_is_valid(&mark)) return false;
            if (mark.format != pixels.format) {   /* the same byte order as the picture */
                mark = rubraview_pixbuf_crop(arena, &bc->mark, 0, 0, bc->mark.width, bc->mark.height);
                if (!rubraview_pixbuf_is_valid(&mark)) return false;
                for (int32_t y = 0; y < mark.height; ++y) {
                    uint8_t *q = mark.pixels + (size_t)y * (size_t)mark.stride;
                    for (int32_t x = 0; x < mark.width; ++x, q += 4) { uint8_t r = q[0]; q[0] = q[2]; q[2] = r; }
                }
                mark.format = pixels.format;
            }
            if (!rubraview_batch_watermark(arena, &pixels, &mark, extra->watermark_corner, extra->watermark_opacity)) {
                batch_say(input->path, "the watermark could not be laid on"); return false;
            }
        }
        if (extra->text.size > 0) {   /* RV-134: words on it, last of all */
            proven_u8str_view_t name = rubraview_path_basename(input->path);
            proven_u8str_view_t ext = rubraview_path_ext(name);
            if (ext.size > 0 && ext.ptr[0] == '.') { ext.ptr++; ext.size--; }
            char date[40] = "";
            rubraview_fs_entry_t e;
            if (rubraview_pal_fs_stat(arena, input->path, &e)) { info_local_time(date, sizeof(date), e.mtime); date[10] = '\0'; }
            proven_u8str_view_t words = rubraview_batch_format_name_ex(arena, extra->text, rubraview_path_stem(name), ext, pixels.width, pixels.height,
                                                           cstr(date), bc->counter, rubraview_path_basename(rubraview_path_dirname(input->path)));
            int32_t shorter = pixels.width < pixels.height ? pixels.width : pixels.height;
            int32_t px = shorter * extra->text_size / 100;
            rubraview_pixbuf_t mask = rubraview_pal_text_mask(arena, words, extra->text_font, px < 8 ? 8 : px);
            rubraview_pixbuf_t layer = rubraview_batch_text_layer(arena, &mask, extra->text_rgb, pixels.format);
            int32_t room = pixels.width - 2 * (shorter / 50) - 2;
            if (!rubraview_pixbuf_is_valid(&layer) ||
                !rubraview_batch_overlay(arena, &pixels, &layer, extra->text_corner, 100, room > 1 ? room : 1)) {
                batch_say(input->path, "the text could not be drawn"); return false;
            }
        }
    }

    if (options.format == RUBRAVIEW_EXPORT_SAME_AS_SOURCE) options.format = source_format;
    if (!rubraview_pal_image_save(out_path, &pixels, &options)) {
        const char *why = rubraview_pal_image_save_why();   /* RV-139 */
        batch_say(input->path, why ? why : "could not be written");
        return false;
    }
    int kept = details_keep(input->path, out_path, out_path, options.format, &options);   /* 1.17.0 */
    if (kept == DETAILS_TXT) batch_say(out_path, "its details that it cannot hold are in a .txt beside it");
    if (kept == DETAILS_FAILED) batch_say(out_path, "its details could not be kept");

    bc->written++;
    return true;
}

static bool batch_process(proven_arena_t *arena, const rubraview_batch_job_t *job,
                          const rubraview_batch_input_t *input, proven_u8str_view_t output_name, void *ctx) {
    batch_ctx_t *bc = (batch_ctx_t*)ctx;
    bc->counter = (int64_t)(job->counter_start > 0 ? job->counter_start : 1) + (int64_t)bc->seen++;   /* as the core counts for the name */
    proven_u8str_view_t out_dir = bc->output_dir.size > 0 ? bc->output_dir : rubraview_path_dirname(input->path);
    /* The name carries the source's extension. When the job converts, the
       file must be named after what is actually written, or a PNG lands
       in a .jpg and nothing can open it. */
    if (bc->cli->export_options.format != RUBRAVIEW_EXPORT_SAME_AS_SOURCE) {
        proven_u8str_view_t want = rubraview_export_extension(bc->cli->export_options.format);
        proven_u8str_view_t stem = rubraview_path_stem(output_name);
        if (want.size > 0 && stem.size > 0) {
            size_t n = stem.size + 1 + want.size;
            proven_result_mem_mut_t res = proven_arena_alloc(arena, n + 1);
            if (proven_is_ok(res.err)) {
                char *p = (char*)res.value.ptr;
                memcpy(p, stem.ptr, stem.size);
                p[stem.size] = '.';
                memcpy(p + stem.size + 1, want.ptr, want.size);
                p[n] = '\0';
                output_name = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)p, .size = n };
            }
        }
    }
    /* RV-129: --subfolders keeps where under the input the file was */
    if (bc->cli->subfolders && bc->output_dir.size > 0) {
        proven_u8str_view_t here = rubraview_path_dirname(input->path);
        for (size_t r = 0; r < g_launch_count; ++r) {
            proven_u8str_view_t root = g_launch_paths[r];
            if (here.size > root.size + 1 && memcmp(here.ptr, root.ptr, root.size) == 0 && (here.ptr[root.size] == '\\' || here.ptr[root.size] == '/')) {
                out_dir = rubraview_path_join(arena, out_dir, (proven_u8str_view_t){ .ptr = here.ptr + root.size + 1, .size = here.size - root.size - 1 });
                if (out_dir.size == 0 || (!rubraview_pal_fs_make_dirs(out_dir) && !rubraview_pal_fs_exists(out_dir))) {
                    batch_say(input->path, "its folder under the output could not be made");
                    return false;
                }
                break;
            }
        }
    }
    proven_u8str_view_t out_path = rubraview_path_join(arena, out_dir, output_name);
    if (out_path.size == 0) return false;
    /* RV-129: --exists: replace it (as before), leave it, or take the next free name */
    if (bc->cli->exists != 0 && rubraview_pal_fs_exists(out_path)) {
        if (bc->cli->exists == 1) { batch_say(out_path, "is there already: left as it is"); return true; }
        out_path = rubraview_filing_target(arena, out_dir, output_name, filing_exists, NULL);
        if (out_path.size == 0) { batch_say(input->path, "no free name left beside it"); return false; }
    }

    const rubraview_batch_orient_params_t *orient = NULL;
    const rubraview_batch_resize_params_t *resize = NULL;
    const rubraview_batch_color_params_t *color = NULL;
    const rubraview_batch_extra_params_t *extra = NULL;
    for (size_t i = 0; i < job->action_count; ++i) {
        switch (job->actions[i].kind) {
            case RUBRAVIEW_BATCH_EXTRA: extra = &job->actions[i].params.extra; break;
            case RUBRAVIEW_BATCH_ORIENT: orient = &job->actions[i].params.orient; break;
            case RUBRAVIEW_BATCH_RESIZE: resize = &job->actions[i].params.resize; break;
            case RUBRAVIEW_BATCH_COLOR_ADJUST: color = &job->actions[i].params.color; break;
            default: break;
        }
    }

    rubraview_export_options_t options = bc->cli->export_options;
    rubraview_export_format_t source_format = rubraview_export_format_for_name(input->path);

    /* §3.9/§3.10: a JPEG that is only being turned, or only being
       cleaned, never goes near a decoder. This is the whole reason
       libjpeg-turbo is vendored. */
    if (source_format == RUBRAVIEW_EXPORT_JPEG &&
        (options.format == RUBRAVIEW_EXPORT_SAME_AS_SOURCE || options.format == RUBRAVIEW_EXPORT_JPEG) &&
        !resize && !color && !extra && !g_batch_has_steps) {
        rubraview_jpegtran_op_t op = orient ? jpegtran_op_for(orient) : RUBRAVIEW_JPEGTRAN_NONE;
        bool lossless_possible = !orient || !orient->use_exif_auto_orient;

        if (lossless_possible && (op != RUBRAVIEW_JPEGTRAN_NONE || options.privacy_clean)) {
            proven_u8str_view_t bytes = rubraview_pal_fs_read_file(arena, input->path, MAX_ARCHIVE_BYTES);
            if (bytes.size > 0) {
                rubraview_jpegtran_result_t turned = rubraview_jpegtran_apply(
                    arena, (const uint8_t*)bytes.ptr, bytes.size, op,
                    RUBRAVIEW_JPEGTRAN_KEEP_EDGE, options.privacy_clean);
                /* RV-134: a DPI asked for goes into the JFIF block; a file without one is written the other way */
                bool dpi_ok = options.dpi <= 0.0 || (turned.err == RUBRAVIEW_JPEGTRAN_OK &&
                              rubraview_jpeg_set_dpi((uint8_t*)(uintptr_t)turned.data.ptr, turned.data.size, options.dpi));
                if (turned.err == RUBRAVIEW_JPEGTRAN_OK && dpi_ok) {
                    if (rubraview_pal_fs_write_file(out_path, turned.data)) {
                        bc->written++;
                        batch_keep_date(arena, bc, input->path, out_path);
                        return true;
                    }
                    return false;
                }
            }
            /* Fall through: if the lossless path could not do it, the
               ordinary one still can. */
        }
    }

    /* A picture whose steps need more than the run's arena gets room of its
       own, sized before it is decoded. Before this, a step with no room
       failed quietly and the file was written without it (left open by D-39). */
    void *own = NULL;
    proven_arena_t own_arena;
    int32_t sw = 0, sh = 0;
    if (rubraview_pal_image_size(input->path, &sw, &sh)) {
        int32_t th = 0, tw = resize ? resize_target(resize, sw, sh, &th) : 0;
        int32_t th2 = 0, tw2 = resize ? resize_target(resize, sh, sw, &th2) : 0;   /* turned first, perhaps */
        if ((int64_t)tw2 * th2 > (int64_t)tw * th) { tw = tw2; th = th2; }
        size_t need = rubraview_batch_work_bytes(sw, sh, tw, th, orient != NULL, resize != NULL,
                                                 color && color->has_unsharp);
        if (g_batch_has_steps && need != SIZE_MAX) {   /* RV-187: each of Edit's steps makes a copy, at the largest size it reaches */
            static rubraview_edit_steps_t sized;
            sized = g_batch_steps;
            rubraview_edit_steps_rebase(&sized, sw, sh);
            int64_t most = (int64_t)sw * sh;
            for (size_t i = 1; i <= sized.cursor; ++i) {
                int32_t w = 0, h = 0;
                rubraview_edit_steps_size(&sized, i, &w, &h);
                if ((int64_t)w * h > most) most = (int64_t)w * h;
            }
            need += (size_t)most * 4u * 6u + (16u << 20);   /* the step's input, its output, an area's piece, a filter's work */
        }
        if (extra && need != SIZE_MAX) {   /* RV-129: the extra steps make whole copies, a frame or a canvas a larger one */
            int64_t big = (int64_t)(tw > sw ? tw : sw) * (int64_t)(th > sh ? th : sh);
            int64_t framed = (int64_t)(tw + 2 * extra->border_px + extra->canvas_w) * (int64_t)(th + 2 * extra->border_px + extra->canvas_h);
            if (framed > big) big = framed;
            need += (size_t)big * 4u * 4u + (8u << 20);
        }
        if (resize && resize->linear && need != SIZE_MAX) need += (size_t)tw * (size_t)sh * 16u + (size_t)tw * (size_t)th * 4u;
        size_t room = arena->backing.size > arena->offset ? (size_t)(arena->backing.size - arena->offset) : 0;
        if (need > room) {
            own = need == SIZE_MAX ? NULL : malloc(need);
            if (!own) { batch_say(input->path, "not enough memory for a picture this large"); return false; }
            own_arena = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)own, .size = need });
            arena = &own_arena;
        }
    }
    /* RV-109: an animation that is only changed to WebP keeps moving. */
    rubraview_export_format_t to = options.format == RUBRAVIEW_EXPORT_SAME_AS_SOURCE ? source_format : options.format;
    bool plain = !orient && !resize && !color && !extra && !g_batch_has_steps;
    int moved = to == RUBRAVIEW_EXPORT_WEBP && plain ? rubraview_pal_image_save_animation_webp(out_path, input->path, NULL, 0, &options)
              : (to == RUBRAVIEW_EXPORT_AVIF || to == RUBRAVIEW_EXPORT_JXL) && plain   /* 1.31.0: AVIF and JPEG XL too */
                ? rubraview_pal_image_save_animation_ffmpeg(out_path, input->path, NULL, 0, &options, to == RUBRAVIEW_EXPORT_AVIF) : 0;
    bool ok = false;
    if (moved > 0) { ok = true; bc->written++; (void)details_keep(input->path, out_path, out_path, to, &options); }
    else if (moved < 0) {
        const char *why = rubraview_pal_image_save_why();
        batch_say(input->path, why ? why : "the animation could not be written");
    }
    else ok = batch_pixels(arena, bc, input, out_path, orient, resize, color, extra, options, source_format);
    if (ok) batch_keep_date(arena, bc, input->path, out_path);
    free(own);
    return ok;
}

/* Collects the files a run will work on: one file, or a directory, or a
   directory tree when --recursive was given. */
static size_t collect_inputs(proven_arena_t *arena, proven_u8str_view_t root, bool recursive,
                             rubraview_batch_input_t *out, size_t capacity, size_t count) {
    rubraview_fs_listing_t listing = rubraview_pal_fs_list_dir(arena, root);
    if (listing.count == 0) {
        /* Not a directory: treat it as the single file it is. */
        if (count < capacity && rubraview_pal_fs_exists(root)) {
            out[count].path = root;
            out[count].size_bytes = 0;
            count++;
        }
        return count;
    }

    for (size_t i = 0; i < listing.count && count < capacity; ++i) {
        const rubraview_fs_entry_t *entry = &listing.entries[i];
        if (entry->is_directory) {
            if (recursive) count = collect_inputs(arena, entry->path, true, out, capacity, count);
            continue;
        }
        out[count].path = entry->path;
        out[count].size_bytes = entry->size_bytes;
        count++;
    }
    return count;
}

#define BATCH_MAX_INPUTS 8192

/* Formats "N of M" without pulling in printf's locale machinery. */
void append_number(char *buf, size_t cap, size_t *pos, size_t value) {
    char digits[24];
    size_t n = 0;
    if (value == 0) digits[n++] = '0';
    while (value > 0 && n < sizeof(digits)) { digits[n++] = (char)('0' + (value % 10)); value /= 10; }
    while (n > 0 && *pos + 1 < cap) buf[(*pos)++] = digits[--n];
    buf[*pos] = '\0';
}

void append_text(char *buf, size_t cap, size_t *pos, const char *text) {
    while (*text && *pos + 1 < cap) buf[(*pos)++] = *text++;
    buf[*pos] = '\0';
}

/* RV-068: the batch dialog's Run. The dialog does not convert anything
   itself: it writes the command line `--batch` already understands and
   starts a second copy of the program with it, in a console window of
   its own (owner, 2026-09-20). A run then keeps going when the viewer is
   closed, a viewer that hangs does not take it down, and the window it
   reports into can be read afterwards — `--pause` holds it open.

   Two things are decided here rather than asked: the run is over the
   folder being read, and it writes into a `rubraview-out` folder beside
   those pictures, so pressing Run can never overwrite an original.
   RV-129: a folder may be typed instead; the pictures' own folder is then
   refused while a name already there would be replaced. */
/* Appends `text`, in quotes when it holds a space, so a folder with one
   in its name survives the command line. */
void append_arg(char *buf, size_t cap, size_t *pos, proven_u8str_view_t text) {
    bool quote = false;
    for (size_t i = 0; i < text.size; ++i) if (text.ptr[i] == ' ') { quote = true; break; }
    /* No separator before the first token: a command line that starts
       with a space has no program name in it, and CreateProcess refuses
       it — which is exactly how this was found. */
    if (*pos > 0 && *pos + 1 < cap) buf[(*pos)++] = ' ';
    if (quote && *pos + 1 < cap) buf[(*pos)++] = '"';
    for (size_t i = 0; i < text.size && *pos + 1 < cap; ++i) buf[(*pos)++] = text.ptr[i];
    if (quote && *pos + 1 < cap) buf[(*pos)++] = '"';
    buf[*pos] = '\0';
}

void append_arg_cstr(char *buf, size_t cap, size_t *pos, const char *text) {
    append_arg(buf, cap, pos, cstr(text));
}

/* File › New window (owner, 2026-10-16): this program once more, with
   nothing open, a window of its own whatever "Reuse the open window" says. */
void new_window(app_state_t *app) {
    new_window_open(app, PROVEN_LIT(""));
}

/* RV-186: the viewer once more, on `path` (none: as it starts). */
void new_window_open(app_state_t *app, proven_u8str_view_t path) {
    proven_u8str_view_t exe = rubraview_pal_process_executable(app->arena);
    char line[4096];
    size_t pos = 0;
    line[0] = '\0';
    if (exe.size > 0) {
        append_arg(line, sizeof(line), &pos, exe);
        append_arg_cstr(line, sizeof(line), &pos, "--new-instance");
        if (path.size > 0) append_arg(line, sizeof(line), &pos, path);
    }
    if (exe.size == 0 || !rubraview_pal_process_start((proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = pos })) {
        osd_say(app, TU8("a new window could not be started"));
    }
}

/* The chosen files as a list file in the user's temporary folder, which
   the batch run reads and removes (--paths-temp). Empty when it could not
   be written. */
static proven_u8str_view_t batch_list_file(app_state_t *app) {
    proven_u8str_view_t none = PROVEN_LIT("");
    WCHAR wide[MAX_PATH + 1];
    char dir[MAX_PATH * 3 + 1];
    DWORD n = GetTempPathW(MAX_PATH + 1, wide);
    if (n == 0 || n > MAX_PATH) return none;
    int written = WideCharToMultiByte(CP_UTF8, 0, wide, -1, dir, (int)sizeof(dir), NULL, NULL);
    if (written <= 1) return none;

    size_t total = 0;
    for (size_t i = 0; i < app->batch_list_count; ++i) total += app->batch_list[i].size + 1;
    proven_result_mem_mut_t res = proven_arena_alloc(app->arena, total + (size_t)written + 64);
    if (!proven_is_ok(res.err)) return none;
    char *text = (char*)(void*)res.value.ptr;
    size_t at = 0;
    for (size_t i = 0; i < app->batch_list_count; ++i) {
        memcpy(text + at, app->batch_list[i].ptr, app->batch_list[i].size);
        at += app->batch_list[i].size;
        text[at++] = '\n';
    }
    char *name = text + total;
    int name_len = snprintf(name, (size_t)written + 64, "%srubraview-batch-%lu-%lu.txt", dir,
                            (unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount());
    if (name_len <= 0) return none;
    proven_u8str_view_t path = { .ptr = (const proven_byte_t *)name, .size = (size_t)name_len };
    return rubraview_pal_fs_write_file(path, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)text, .size = total }) ? path : none;
}

void panel_run_batch(app_state_t *app) {
    bool on_list = app->batch_list_count > 0;
    if (!on_list && (app->source_dir.size == 0 || app->archive_bytes.size > 0)) {
        osd_say(app, TU8("batch runs on a folder of pictures, not on an archive"));
        return;
    }

    proven_u8str_view_t exe = rubraview_pal_process_executable(app->arena);
    if (exe.size == 0) {
        osd_say(app, TU8("could not find this program to start a batch run"));
        return;
    }

    /* RV-187: the steps in Edit now, written where the run reads them */
    if (app->batch.steps_from == 1 && !batch_steps_write(app)) {
        osd_say(app, TU8("Edit has no steps yet: make a change in Edit (E) first, or choose a steps file"));
        return;
    }
    if (app->batch.steps_from == 2 && !app->batch.steps_file[0]) {
        osd_say(app, TU8("choose the steps file first, or no steps"));
        return;
    }
    /* RV-129: the window's choices, as the tokens the run is given */
    batch_tokens_t tk;
    batch_tokens(&app->batch, &tk);
    bool any_step = false;
    for (size_t i = 0; i < tk.n; ++i) {
        const char *k = tk.v[i];
        if (strncmp(k, "--name=", 7) && strncmp(k, "--start=", 8) && strncmp(k, "--replace=", 10) && strncmp(k, "--exists=", 9) &&
            strncmp(k, "--out=", 6) && strcmp(k, "--keep-dates") && strcmp(k, "--recursive") && strcmp(k, "--subfolders")) any_step = true;
    }
    bool renaming = (app->batch.name[0] && strcmp(app->batch.name, "{name}.{ext}") != 0) || app->batch.replace[0];
    if (!any_step && !renaming) {
        osd_say(app, TU8("nothing to do: choose a size, a step, a format, or new names"));
        return;
    }
    /* Remembered for the next time (settings.ini holds a key a line: | between them) */
    {
        proven_u8str_view_t text = batch_text(app);
        char kept[4096];
        size_t n = text.size < sizeof(kept) ? text.size : sizeof(kept);
        for (size_t i = 0; i < n; ++i) kept[i] = text.ptr[i] == '\n' ? '|' : text.ptr[i];
        while (n > 0 && kept[n - 1] == '|') --n;
        rubraview_settings_set_text(&app->settings, PROVEN_LIT("batch"), PROVEN_LIT("last"), app_keep(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)kept, .size = n }));
    }

    /* Chosen files: beside the first of them; a folder of its own when one was typed. */
    proven_u8str_view_t beside = on_list ? rubraview_path_dirname(app->batch_list[0]) : app->source_dir;
    proven_u8str_view_t out_dir = app->batch.out[0] ? cstr(app->batch.out) : rubraview_path_join(app->arena, beside, PROVEN_LIT("rubraview-out"));
    if (out_dir.size == 0 || (!rubraview_pal_fs_make_dirs(out_dir) && !rubraview_pal_fs_exists(out_dir))) {
        osd_say(app, TU8("could not make the output folder"));
        return;
    }
    if (app->batch.exists == 0 && rubraview_path_same(out_dir, beside)) {
        osd_say(app, TU8("into the pictures' own folder, a name already there must be skipped or numbered, never replaced"));
        return;
    }

    char line[8192];
    size_t pos = 0;
    line[0] = '\0';
    append_arg(line, sizeof(line), &pos, exe);
    append_arg_cstr(line, sizeof(line), &pos, "--batch");
    append_arg_cstr(line, sizeof(line), &pos, "--pause");
    for (size_t i = 0; i < tk.n; ++i) if (strncmp(tk.v[i], "--out=", 6) != 0) append_arg_cstr(line, sizeof(line), &pos, tk.v[i]);
    {
        char out_arg[1100];
        int n = snprintf(out_arg, sizeof(out_arg), "--out=%.*s", (int)out_dir.size, out_dir.ptr);
        if (n > 0 && (size_t)n < sizeof(out_arg)) append_arg_cstr(line, sizeof(line), &pos, out_arg);
    }
    if (on_list) {
        proven_u8str_view_t list = batch_list_file(app);
        if (list.size == 0) {
            osd_say(app, TU8("could not write the list of files for the batch run"));
            return;
        }
        char list_arg[1100];
        int n = snprintf(list_arg, sizeof(list_arg), "--paths-from=%.*s", (int)list.size, list.ptr);
        append_arg_cstr(line, sizeof(line), &pos, "--paths-temp");
        if (n > 0) append_arg(line, sizeof(line), &pos, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)list_arg, .size = (size_t)n });
    } else {
        append_arg(line, sizeof(line), &pos, app->source_dir);
    }

    if (rubraview_pal_process_start_console((proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = pos })) {
        osd_say(app, TU8("batch started in a window of its own"));
    } else {
        osd_say(app, TU8("could not start the batch run"));
    }
}

/* D-38 in a batch run, which starts before the viewer reads its settings:
   settings.ini found by the same rule (beside the program, else AppData). */
static rubraview_gpu_resize_mode_t batch_gpu_resize_mode(proven_arena_t *arena) {
    proven_u8str_view_t exe = rubraview_pal_process_executable(arena);
    proven_u8str_view_t beside = exe.size > 0 ? rubraview_path_dirname(exe) : PROVEN_LIT(".");
    proven_u8str_view_t portable_ini = rubraview_path_join(arena, beside, PROVEN_LIT("settings.ini"));
    rubraview_config_mode_t mode = rubraview_config_mode_for(portable_ini.size > 0 && rubraview_pal_fs_exists(portable_ini));
    char appdata_utf8[1024];
    proven_u8str_view_t appdata = PROVEN_LIT(".");
    DWORD written = GetEnvironmentVariableA("APPDATA", appdata_utf8, (DWORD)sizeof(appdata_utf8));
    if (written > 0 && written < sizeof(appdata_utf8)) appdata = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)appdata_utf8, .size = written };
    proven_u8str_view_t path = rubraview_config_path(arena, mode, beside, appdata, PROVEN_LIT("settings.ini"));
    rubraview_settings_t settings = rubraview_settings_load(arena, rubraview_pal_fs_read_file(arena, path, 256u * 1024u));
    return (rubraview_gpu_resize_mode_t)lround(rubraview_settings_get(&settings, PROVEN_LIT("display"), PROVEN_LIT("gpu_resize")));
}

/* D-86: the paths this launch was given — those on the command line, then
   the lines of --paths-from — for a batch run, a hand-over, or the first
   thing the window does. */
const proven_u8str_view_t *g_launch_paths;
size_t         g_launch_count;
rubraview_shell_verb_t g_launch_verb;
bool           g_launch_whole;
int32_t        g_launch_page;   /* --page=N, for the first thing opened */

void launch_paths_gather(proven_arena_t *arena, const rubraview_cli_result_t *cli) {
    proven_u8str_view_t text = PROVEN_LIT("");
    size_t listed = 0;
    if (cli->paths_from.size > 0) {
        text = rubraview_pal_fs_read_file(arena, cli->paths_from, 16u * 1024u * 1024u);
        listed = rubraview_shellreq_split_lines(text, NULL, 0);
        /* A list written for this launch alone is not left behind. */
        if (cli->paths_temp) (void)rubraview_pal_fs_delete(cli->paths_from);
    }
    size_t total = cli->input_count + listed;
    if (total == 0) return;
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, total, sizeof(proven_u8str_view_t));
    if (!proven_is_ok(res.err)) return;
    proven_u8str_view_t *paths = (proven_u8str_view_t*)(void*)res.value.ptr;
    for (size_t i = 0; i < cli->input_count; ++i) paths[i] = cli->inputs[i];
    if (listed > 0) (void)rubraview_shellreq_split_lines(text, paths + cli->input_count, listed);
    g_launch_paths = paths;
    g_launch_count = total;
}

int run_batch(proven_arena_t *arena, const rubraview_cli_result_t *cli) {
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, BATCH_MAX_INPUTS, sizeof(rubraview_batch_input_t));
    if (!proven_is_ok(res.err)) {
        console_line("rubraview: out of memory building the file list");
        return 1;
    }
    rubraview_batch_input_t *inputs = (rubraview_batch_input_t*)(void*)res.value.ptr;

    size_t count = 0;
    for (size_t i = 0; i < g_launch_count; ++i) {
        count = collect_inputs(arena, g_launch_paths[i], cli->recursive, inputs, BATCH_MAX_INPUTS, count);
    }
    if (count == 0) {
        console_line("rubraview: nothing to do — no files matched");
        return 1;
    }

    /* RV-187 (D-220): --steps=, Edit's steps, read before anything is opened; what they run is named */
    bool steps_plugin = false, steps_gmic = false;
    if (cli->steps_file.size > 0) {
        proven_u8str_view_t text = rubraview_pal_fs_read_file(arena, cli->steps_file, 4u << 20);
        const char *why = NULL;
        if (text.size == 0 || !rubraview_edit_steps_parse(arena, text, &g_batch_steps, &why)) {
            char line[700];
            snprintf(line, sizeof(line), "rubraview: --steps: %s", text.size == 0 ? "the file could not be read" : why ? why : "not a file of Edit's steps");
            console_line(line);
            return 1;
        }
        g_batch_has_steps = g_batch_steps.cursor > 0;
        for (size_t i = 0; i < g_batch_steps.cursor; ++i) {
            const rubraview_edit_step_t *st = &g_batch_steps.steps[i];
            char line[1200];
            if (st->kind == RUBRAVIEW_STEP_PLUGIN) { steps_plugin = true; snprintf(line, sizeof(line), "rubraview: step %u runs the plug-in %s", (unsigned)(i + 1), st->plugin); console_line(line); }
            if (st->kind == RUBRAVIEW_STEP_GMIC) { steps_gmic = true; snprintf(line, sizeof(line), "rubraview: step %u runs G'MIC: %s", (unsigned)(i + 1), st->command); console_line(line); }
            if (st->kind == RUBRAVIEW_STEP_FFMPEG) { snprintf(line, sizeof(line), "rubraview: step %u runs the FFmpeg filter %s", (unsigned)(i + 1), st->command); console_line(line); }
        }
        rubraview_edit_set_plugin_runner(plugin_runner, NULL);
    }

    /* RFC-0007: --plugin's folder (--plugin-folder, else the default one), looked in once */
    for (size_t i = 0; i < cli->job.action_count && i < RUBRAVIEW_BATCH_MAX_ACTIONS; ++i) {
        const rubraview_batch_action_t *a = &cli->job.actions[i];
        if (a->kind != RUBRAVIEW_BATCH_EXTRA || !a->params.extra.plugin[0]) continue;
        char folder[520] = "";
        if (a->params.extra.plugin_folder.size > 0 && a->params.extra.plugin_folder.size < sizeof(folder))
            memcpy(folder, a->params.extra.plugin_folder.ptr, a->params.extra.plugin_folder.size);
        else (void)rubraview_pal_plugins_default_folder(folder, sizeof(folder), false);
        plugins_look(cstr(folder));
        steps_plugin = false;   /* looked in already */
        break;
    }
    if (steps_plugin) {   /* RV-187: a steps file's plug-in, in --plugin-folder or the default one */
        char folder[520] = "";
        for (size_t i = 0; i < cli->job.action_count && i < RUBRAVIEW_BATCH_MAX_ACTIONS; ++i) {
            const rubraview_batch_action_t *a = &cli->job.actions[i];
            if (a->kind == RUBRAVIEW_BATCH_EXTRA && a->params.extra.plugin_folder.size > 0 && a->params.extra.plugin_folder.size < sizeof(folder))
                memcpy(folder, a->params.extra.plugin_folder.ptr, a->params.extra.plugin_folder.size);
        }
        if (!folder[0]) (void)rubraview_pal_plugins_default_folder(folder, sizeof(folder), false);
        plugins_look(cstr(folder));
    }
    /* RFC-0009 C: --gmic's program, named by --gmic-exe (RV-187: a steps file's G'MIC step too) */
    for (size_t i = 0; i < cli->job.action_count && i < RUBRAVIEW_BATCH_MAX_ACTIONS; ++i) {
        const rubraview_batch_action_t *a = &cli->job.actions[i];
        if (a->kind != RUBRAVIEW_BATCH_EXTRA || (a->params.extra.gmic.size == 0 && !(steps_gmic && a->params.extra.gmic_exe.size > 0))) continue;
        if (a->params.extra.gmic_exe.size > 0) {
            proven_u8str_view_t exe = rubraview_pal_gmic_find(arena, a->params.extra.gmic_exe);
            snprintf(g_gmic_exe, sizeof(g_gmic_exe), "%.*s", (int)exe.size, (const char *)exe.ptr);
        }
        if (!g_gmic_exe[0]) {
            console_line("rubraview: --gmic needs --gmic-exe=<gmic.exe or its folder>, and it was not found there");
            return 1;
        }
        break;
    }
    if (steps_gmic && !g_gmic_exe[0]) {   /* RV-187 */
        console_line("rubraview: the steps run G'MIC: name gmic.exe or its folder with --gmic-exe=");
        return 1;
    }

    /* --out= names a folder that may not be there yet: made first, or every file would fail
       to be written (the window made its rubraview-out itself; the command line did not). */
    if (cli->output_dir.size > 0 && !rubraview_pal_fs_make_dirs(cli->output_dir) && !rubraview_pal_fs_exists(cli->output_dir)) {
        console_line("rubraview: the --out folder could not be made");
        return 1;
    }

    /* §3.11: one arena for the work, reset between files, so a run over
       ten thousand files uses what one file needs. */
    void *work_memory = malloc(BATCH_WORK_ARENA_BYTES);
    if (!work_memory) {
        console_line("rubraview: out of memory");
        return 1;
    }
    proven_arena_t work = proven_arena_create(
        (proven_mem_mut_t){ .ptr = (proven_byte_t*)work_memory, .size = BATCH_WORK_ARENA_BYTES });

    (void)rubraview_pal_gpu_resample_start(batch_gpu_resize_mode(arena));   /* D-38 */
    batch_ctx_t ctx = { .cli = cli, .output_dir = cli->output_dir, .written = 0 };
    /* A batch run has no window and no Direct2D: the resize bands may go to
       worker threads (the viewer itself keeps none, §3.1). */
    DWORD cores = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    ctx.workers = cores > 16 ? 16 : cores;
    if (ctx.workers > 1 &&
        !proven_is_ok(proven_job_system_init(proven_arena_as_allocator(arena), ctx.workers, 256, &ctx.jobs))) {
        ctx.jobs = NULL;
    }
    rubraview_batch_report_t report = rubraview_batch_run(&work, &cli->job, inputs, count,
                                                          PROVEN_LIT(""), batch_process, &ctx, NULL, 0);
    rubraview_pal_gpu_resample_stop();
    if (ctx.jobs) { proven_job_system_close(ctx.jobs); proven_job_system_destroy(ctx.jobs); }
    free(ctx.mark_memory);
    free(work_memory);

    char line[160];
    size_t pos = 0;
    line[0] = '\0';
    append_text(line, sizeof(line), &pos, "rubraview: ");
    append_number(line, sizeof(line), &pos, report.processed);
    append_text(line, sizeof(line), &pos, " converted, ");
    append_number(line, sizeof(line), &pos, report.skipped);
    append_text(line, sizeof(line), &pos, " skipped, ");
    append_number(line, sizeof(line), &pos, report.failed);
    append_text(line, sizeof(line), &pos, " failed");
    console_line(line);

    /* --pause: a run started from the dialog has a console of its own,
       and that window closes with the process. Wait for a key so the
       counts above can be read. */
    if (cli->pause_at_end) {
        console_line("press a key to close this window");
        HANDLE in = console_handle(true);
        if (in) {
            FlushConsoleInputBuffer(in);
            for (;;) {
                INPUT_RECORD record;
                DWORD read = 0;
                if (!ReadConsoleInputW(in, &record, 1, &read) || read == 0) break;
                if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown) break;
            }
        }
    }

    return report.failed > 0 ? 1 : 0;
}
