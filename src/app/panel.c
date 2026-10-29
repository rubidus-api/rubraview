/*
 * The panel: a row turned back into what it stands for, its buttons, a value typed into a slider, the two boxes
 * beside Edit, a press, the panel drawn (RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* Turns a panel row back into the thing it stands for. Keeping this in
   one place is what lets the panel model stay ignorant of editing. */
void panel_apply_row(app_state_t *app, int32_t index) {
    if (index < 0 || (size_t)index >= app->panel.row_count) return;
    const rubraview_panel_row_t *row = &app->panel.rows[index];
    double v = row->value;

    if (app->panel_is_export) {
        switch (row->id) {
            case PANEL_FORMAT:    app->export_options.format = (rubraview_export_format_t)(int32_t)v; break;
            case PANEL_QUALITY:   app->export_options.jpeg_quality = (int32_t)v;
                                  app->export_options.webp_quality = (int32_t)v; break;
            case PANEL_PNG_LEVEL: app->export_options.png_compression = (int32_t)v; break;
            case PANEL_PRIVACY:   app->export_options.privacy_clean = v > 0.5; break;
            default: break;
        }
        bool rows_change = export_more_apply(&app->export_options, row->id, v);
        rubraview_export_clamp(&app->export_options);
        /* Another format, WebP's lossless, a palette or not: the rows that have use change with it. */
        if (rows_change || row->id == PANEL_FORMAT || row->id == PANEL_PRIVACY) {
            panel_open_export(app);
        }
        return;
    }

    if (app->panel_is_convert) {
        switch (row->id) {
            case PANEL_CONV_FORMAT: rubraview_settings_set(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("convert_format"), v); break;
            case PANEL_CONV_METHOD: rubraview_settings_set(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("convert_method"), v); break;
            case PANEL_CONV_LEVEL:  rubraview_settings_set(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("convert_level"), v); break;
            default: break;
        }
        return;
    }

    if (app->panel_is_print) {
        switch (row->id) {
            case PANEL_PRINT_TURN:      rubraview_settings_set(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("turn"), v); break;
            case PANEL_PRINT_SIZE:      rubraview_settings_set(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("size"), v); break;
            case PANEL_PRINT_SCALE:     rubraview_settings_set(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("scale"), v); break;
            case PANEL_PRINT_PLACE:     rubraview_settings_set(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("place"), v); break;
            case PANEL_PRINT_MARGIN:    rubraview_settings_set(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("margin_mm"), v); break;
            case PANEL_PRINT_PER_SHEET: rubraview_settings_set(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("per_sheet"), v);
                                        app->print_preview_sheet = 0; print_thumbs_load(app); panel_open_print(app); break;
            /* 1.19.0 */
            case PANEL_PRINT_PRINTER:
                app->print_printer = (int32_t)v;
                rubraview_settings_set_text(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("printer"), app_keep(app, print_printer_name(app)));
                print_papers_load(app);
                print_sheet_load(app);
                panel_open_print(app);
                break;
            case PANEL_PRINT_PAPER:
                app->print_paper = (int32_t)v;
                if (app->print_paper >= 0 && (size_t)app->print_paper < app->print_paper_count)
                    rubraview_settings_set(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("paper"), (double)app->print_papers[app->print_paper].id);
                print_sheet_load(app);
                break;
            case PANEL_PRINT_ORIENT: rubraview_settings_set(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("orientation"), v); print_sheet_load(app); break;
            case PANEL_PRINT_COPIES: rubraview_settings_set(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("copies"), v); break;
            case PANEL_PRINT_SHEET:  app->print_preview_sheet = (int32_t)v; print_thumbs_load(app); break;
            default: break;
        }
        return;
    }

    if (app->panel_is_batch) {   /* RV-129 */
        struct batch_ui *b = &app->batch;
        bool again = false;
        switch (row->id) {
            case PANEL_BATCH_SIZE_BY: {
                b->size_by = (int32_t)v; again = true;
                static const double START[8] = { 100, 50, 1920, 1080, 1920, 1080, 1920, 2 };
                b->size_value = START[b->size_by & 7];
                break;
            }
            case PANEL_BATCH_RESIZE:        b->size_value = v; break;
            case PANEL_BATCH_BOX_H:         b->box_h = v; break;
            case PANEL_BATCH_FILTER:        b->filter = (int32_t)v; break;
            case PANEL_BATCH_LINEAR:        b->linear = v > 0.5; break;
            case PANEL_BATCH_NO_ENLARGE:    b->no_enlarge = v > 0.5; break;
            case PANEL_BATCH_SHARPEN:       b->sharpen = v; break;
            case PANEL_BATCH_ROTATE:        b->rotate = (int32_t)v; break;
            case PANEL_BATCH_FLIP:          b->flip = (int32_t)v; break;
            case PANEL_BATCH_AUTOCROP:      b->autocrop = v > 0.5; break;
            case PANEL_BATCH_AUTOCOLOUR:    b->auto_colour = v > 0.5; break;
            case PANEL_BATCH_GRAY:          b->grayscale = v > 0.5; break;
            case PANEL_BATCH_NEGATIVE:      b->negative = v > 0.5; break;
            case PANEL_BATCH_EFFECT: {
                b->effect = (int32_t)v; again = true;
                double start = 0;
                rubraview_effect_range((rubraview_effect_t)b->effect, NULL, NULL, &start);
                b->effect_amount = start;
                break;
            }
            case PANEL_BATCH_EFFECT_AMOUNT: b->effect_amount = v; break;
            case PANEL_BATCH_STEPS:   /* RV-187: none, the steps in Edit now, a file */
                b->steps_from = (int32_t)v; again = true;
                if (b->steps_from == 1 && !batch_steps_write(app)) {
                    osd_say(app, TU8("Edit has no steps yet: make a change in Edit (E) first, or choose a steps file"));
                    b->steps_from = 0;
                }
                if (b->steps_from == 0) b->steps_file[0] = '\0';
                if (b->steps_from == 2 && !strstr(b->steps_file, ".rvsteps")) b->steps_file[0] = '\0';
                break;
            case PANEL_BATCH_BORDER:        b->border = v; break;
            case PANEL_BATCH_FRAME_COLOUR:  b->frame_colour = (int32_t)v; break;
            case PANEL_BATCH_MARK_CORNER:   b->mark_corner = (int32_t)v; break;
            case PANEL_BATCH_MARK_OPACITY:  b->mark_opacity = v; break;
            case PANEL_BATCH_TEXT_CORNER:   b->text_corner = (int32_t)v; break;
            case PANEL_BATCH_TEXT_SIZE:     b->text_size = v; break;
            case PANEL_BATCH_TEXT_COLOUR:   b->text_colour = (int32_t)v; break;
            case PANEL_BATCH_DPI:           b->dpi = v; break;
            case PANEL_BATCH_FORMAT:        b->format = (int32_t)v; again = true; break;
            case PANEL_BATCH_QUALITY:       b->quality = v; break;
            case PANEL_PRIVACY:             b->privacy = v > 0.5; app->export_options.privacy_clean = b->privacy; again = true; break;
            default: if (export_more_apply(&b->more, row->id, v)) again = true; break;
            case PANEL_BATCH_START:         b->start = v; break;
            case PANEL_BATCH_EXISTS:        b->exists = (int32_t)v; break;
            case PANEL_BATCH_KEEP_DATES:    b->keep_dates = v > 0.5; break;
            case PANEL_BATCH_RECURSIVE:     b->recursive = v > 0.5; again = true; break;
            case PANEL_BATCH_SUBFOLDERS:    b->subfolders = v > 0.5; break;
        }
        if (again) panel_open_batch(app);
        return;
    }

    if (gmic_row_changed(app, row->id, v)) return;   /* RFC-0009 C: its parameters' rows */
    if (ffmpeg_row_changed(app, row->id, v)) return;   /* RFC-0009 D: its options' rows */
    switch (row->id) {
        case PANEL_EXPOSURE:    rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_EXPOSURE, (float)v); break;
        case PANEL_BRIGHTNESS:  rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_BRIGHTNESS, (float)v); break;
        case PANEL_CONTRAST:    rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_CONTRAST, (float)v); break;
        case PANEL_SATURATION:  rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_SATURATION, (float)v); break;
        case PANEL_TEMPERATURE: rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_TEMPERATURE, (float)v); break;
        case PANEL_TINT:        rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_TINT, (float)v); break;
        case PANEL_BAL_R:       app->edit.params.balance_r = (float)v; break;
        case PANEL_BAL_G:       app->edit.params.balance_g = (float)v; break;
        case PANEL_BAL_B:       app->edit.params.balance_b = (float)v; break;
        case PANEL_EFFECT_GROUP:   /* another kind: none of it chosen yet */
            app->effect_group = (int32_t)v;
            app->effect_choice = RUBRAVIEW_EFFECT_NONE;
            panel_build_edit(app);
            break;
        case PANEL_EFFECT: {
            rubraview_effect_t members[RUBRAVIEW_EFFECT_COUNT];
            size_t n = rubraview_effect_group_members((rubraview_effect_group_t)app->effect_group, members, RUBRAVIEW_EFFECT_COUNT);
            size_t pick = (size_t)lround(v);
            app->effect_choice = pick >= 1 && pick <= n ? (int32_t)members[pick - 1] : RUBRAVIEW_EFFECT_NONE;
            double start = 0.0;
            rubraview_effect_range((rubraview_effect_t)app->effect_choice, NULL, NULL, &start);
            app->effect_amount = start;
            panel_build_edit(app);   /* the amount's range is the effect's */
            break;
        }
        case PANEL_EFFECT_AMOUNT: app->effect_amount = v; break;
        case PANEL_PLUGIN: {   /* RFC-0007: another plug-in, its parameters at their defaults */
            const rubraview_plugin_t *list[RUBRAVIEW_PLUGIN_MAX];
            size_t n = plugins_usable(list, RUBRAVIEW_PLUGIN_MAX);
            app->plugin_choice = (int32_t)lround(v);
            if (app->plugin_choice < 0 || (size_t)app->plugin_choice > n) app->plugin_choice = 0;
            if (app->plugin_choice > 0) rubraview_plugin_params_clamp(list[app->plugin_choice - 1], app->plugin_values, 0);
            panel_build_edit(app);
            break;
        }
        case PANEL_PLUGIN_P0: case PANEL_PLUGIN_P1: case PANEL_PLUGIN_P2: case PANEL_PLUGIN_P3:
        case PANEL_PLUGIN_P4: case PANEL_PLUGIN_P5: case PANEL_PLUGIN_P6: case PANEL_PLUGIN_P7:
            app->plugin_values[row->id - PANEL_PLUGIN_P0] = v;
            break;
        case PANEL_COMPARE:     app->edit_compare = v > 0.5; break;
        case PANEL_GAMMA:       rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_MIDTONE_GAMMA, (float)v); break;
        case PANEL_BLUR:        rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_BLUR_SIGMA, (float)v); break;
        case PANEL_SHARPEN:     rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_SHARPEN_AMOUNT, (float)v); break;
        case PANEL_BLACK:       rubraview_edit_set_black_point(&app->edit, (int32_t)v);
                                rubraview_panel_set_value(&app->panel, PANEL_WHITE,
                                                          (double)app->edit.params.white_point); break;
        case PANEL_WHITE:       rubraview_edit_set_white_point(&app->edit, (int32_t)v);
                                rubraview_panel_set_value(&app->panel, PANEL_BLACK,
                                                          (double)app->edit.params.black_point); break;
        case PANEL_CROP_RATIO:  rubraview_edit_crop_set_ratio(&app->edit, (rubraview_crop_ratio_t)(int32_t)v); crop_shown_again(app); break;
        /* RV-108 */
        case PANEL_BLUR_KIND:    app->edit.blur_box = v > 0.5; break;
        case PANEL_SHARPEN_KIND: app->edit.sharpen_laplacian = v > 0.5; break;
        case PANEL_SHARPEN_RADIUS: rubraview_edit_set_slider(&app->edit, RUBRAVIEW_SLIDER_SHARPEN_RADIUS, (float)v); break;
        case PANEL_SHARPEN_THRESHOLD: app->edit.sharpen_threshold = (uint8_t)(v < 0.0 ? 0.0 : v > 255.0 ? 255.0 : v); break;
        case PANEL_RESIZE_PRESET: {
            int32_t w = 0, h = 0, sw = 0, sh = 0;
            (void)rubraview_pal_window_monitor_size(app->window, &sw, &sh);
            if (rubraview_resize_preset_size((rubraview_resize_preset_t)(int32_t)v, app->edit.image_width, app->edit.image_height,
                                             sw, sh, app->edit.resize_lock_aspect, &w, &h)) {
                app->edit.resize_width = w;
                app->edit.resize_height = h;
                app->unit_px |= RUBRAVIEW_RATIO_RESIZE;   /* RV-187: a size named in pixels stays those pixels */
                rubraview_panel_set_value(&app->panel, PANEL_RESIZE, 100.0 * (double)w / (double)app->edit.image_width);
            }
            int32_t k = rubraview_panel_find(&app->panel, PANEL_RESIZE_PX);
            if (k >= 0) app->panel.rows[k].label = px_button_label(app);
            break;
        }
        case PANEL_RESIZE_LOCK:
            app->edit.resize_lock_aspect = v > 0.5;
            if (app->edit.resize_lock_aspect && app->edit.resize_width > 0) {   /* the height follows the width again */
                int32_t w = 0, h = 0;
                rubraview_edit_session_t whole = edit_whole(app);
                if (rubraview_edit_resize_pair(&whole, app->edit.resize_width, true, &w, &h)) { app->edit.resize_width = w; app->edit.resize_height = h; }
                int32_t k = rubraview_panel_find(&app->panel, PANEL_RESIZE_PX);
                if (k >= 0) app->panel.rows[k].label = px_button_label(app);
            }
            break;
        case PANEL_RESIZE_FILTER:  app->edit.resize_filter = (rubraview_resample_filter_t)(int32_t)v; break;
        case PANEL_RESIZE_LINEAR:  app->edit.resize_linear = v > 0.5; break;
        case PANEL_RESIZE_SHARPEN: app->edit.resize_sharpen = (float)v; break;
        case PANEL_RESIZE_DPI: {
            app->edit.resize_dpi = v;
            int32_t k = rubraview_panel_find(&app->panel, PANEL_RESIZE_PX);
            if (k >= 0) app->panel.rows[k].label = px_button_label(app);
            break;
        }
        case PANEL_RESIZE: {
            /* A hundred is the size as it is; the size is of the whole picture (RV-128: whatever is selected). */
            int32_t w = 0, h = 0;
            rubraview_edit_session_t whole = edit_whole(app);
            rubraview_panel_set_value(&app->panel, PANEL_RESIZE_PRESET, 0.0);
            if (lround(v) == 100 || !rubraview_edit_resize_percent(&whole, v, &w, &h)) {
                app->edit.resize_width = app->edit.resize_height = 0;
            } else {
                rubraview_edit_set_resize(&app->edit, w, h, true);
                /* RV-187: a percent is a ratio, the same share of any picture */
                app->unit_px &= ~(uint32_t)RUBRAVIEW_RATIO_RESIZE;
                app->unit_frac[4] = app->unit_frac[5] = v / 100.0;
                app->unit_frac_px[4] = app->edit.resize_width;
                app->unit_frac_px[5] = app->edit.resize_height;
                app->unit_frac_set |= RUBRAVIEW_RATIO_RESIZE;
            }
            int32_t k = rubraview_panel_find(&app->panel, PANEL_RESIZE_PX);
            if (k >= 0) app->panel.rows[k].label = px_button_label(app);
            break;
        }
        default: break;
    }
    /* The preview is of the tones, the blur and the sharpening: any of them changing draws it again. */
    app->edit_shown_valid = false;
    app->edit_rev++;
    /* The owner (2026-10-18): the change is on the picture at once, a step of the undo list. */
    if (edit_panel_open(app)) {
        switch (row->id) {
            case PANEL_EXPOSURE: case PANEL_BRIGHTNESS: case PANEL_CONTRAST: case PANEL_SATURATION: case PANEL_TEMPERATURE:
            case PANEL_TINT: case PANEL_BAL_R: case PANEL_BAL_G: case PANEL_BAL_B: case PANEL_GAMMA: case PANEL_BLUR:
            case PANEL_SHARPEN: case PANEL_BLACK: case PANEL_WHITE: case PANEL_BLUR_KIND: case PANEL_SHARPEN_KIND:
            case PANEL_SHARPEN_RADIUS: case PANEL_SHARPEN_THRESHOLD:
                edit_live_update(app, RUBRAVIEW_STEP_ADJUST);
                break;
            case PANEL_EFFECT: case PANEL_EFFECT_AMOUNT: case PANEL_EFFECT_GROUP:
                edit_live_update(app, RUBRAVIEW_STEP_EFFECT);
                break;
            default: break;
        }
    }
}

static void panel_button(app_state_t *app, int32_t index) {
    if (index < 0 || (size_t)index >= app->panel.row_count) return;
    if (app->panel.rows[index].id >= PANEL_GMIC_P0 && app->panel.rows[index].id < PANEL_GMIC_P0 + PANEL_GMIC_ROWS &&
        gmic_button(app, app->panel.rows[index].id)) return;   /* RFC-0009 C: a text, a colour or a point typed */
    if (app->panel.rows[index].id >= PANEL_FFMPEG_P0 && app->panel.rows[index].id < PANEL_FFMPEG_P0 + PANEL_FFMPEG_ROWS &&
        ffmpeg_button(app, app->panel.rows[index].id)) return;   /* RFC-0009 D: an option typed */
    switch (app->panel.rows[index].id) {
        case PANEL_RESET:
            /* Everything since the panel opened: the steps, the values, the selection. */
            app->edit_live = false;
            rubraview_edit_steps_reset(&app->edit_steps);
            rubraview_edit_reset(&app->edit);
            edit_values_fresh(app);
            app->edit.crop_active = false;
            edit_steps_changed(app);
            osd_say(app, TU8("back to the picture as it was"));
            break;
        case PANEL_APPLY_SEL:    (void)edit_apply_pending(app, true); break;
        case PANEL_EFFECT_APPLY: edit_effect_apply(app); break;
        case PANEL_PLUGIN_APPLY: edit_plugin_apply(app); break;   /* RFC-0007 */
        case PANEL_GMIC_APPLY: case PANEL_GMIC_TYPE: (void)gmic_button(app, app->panel.rows[index].id); break;   /* RFC-0009 C */
        case PANEL_FFMPEG_APPLY: case PANEL_FFMPEG_TYPE: (void)ffmpeg_button(app, app->panel.rows[index].id); break;   /* RFC-0009 D */
        case PANEL_AUTO_COLOUR:  edit_auto_colour(app); break;
        case PANEL_CROP_SEL:     edit_crop_to_selection(app); break;
        case PANEL_RESIZE_APPLY: edit_resize_apply(app); break;
        case PANEL_UNDO:         edit_undo(app, false); break;
        case PANEL_REDO:         edit_undo(app, true); break;
        case PANEL_STEPS:        edit_steps_toggle(app); break;
        case PANEL_STEPS_SAVE:   edit_steps_save_or_load(app, true); break;    /* RV-187 */
        case PANEL_STEPS_LOAD:   edit_steps_save_or_load(app, false); break;
        case PANEL_CHANGES_REMOVE: changes_remove_checked(app); break;
        case PANEL_SEL_MODE:
            sel_mode_set(app, !app->sel_mode);
            osd_say(app, app->sel_mode ? TU8("drag on the picture to choose an area (Esc: stop choosing)") : TU8("a drag moves the picture again"));
            panel_build_edit(app);
            break;
        case PANEL_SEL_COPY:     edit_copy_area(app); break;
        case PANEL_ALL_COPIES:   apply_all_begin(app); break;
        case PANEL_ALL_ORIGINALS: apply_all_originals(app); break;
        case PANEL_SEL_BOX:      edit_sel_toggle(app); break;
        case PANEL_SAVE_ORIGINAL:
            (void)edit_apply_pending(app, false);
            if (on_archive_page(app)) panel_write_archive_page(app, true);
            else edit_save_over(app);
            break;
        case PANEL_EDIT_PAGE:
            g_edit_page_two = !g_edit_page_two;
            panel_build_edit(app);
            break;
        case PANEL_RESIZE_PX:
            px_edit_begin(app);
            break;
        case PANEL_CROP_PX:
            px_edit_begin_crop(app);
            break;
        case PANEL_TRIM: {
            /* RV-108 (§3.13): the plain margin of a scan — white, or black when there is no white one —
               found on the preview's small copy and marked as the cut, for the reader to see before saving. */
            const rubraview_pixbuf_t *small = &app->edit_small;
            int32_t x = 0, y = 0, w = 0, h = 0;
            bool found = rubraview_pixbuf_is_valid(small) &&
                         rubraview_filter_autotrim_rect(small, 245, true, &x, &y, &w, &h) &&
                         (w < small->width || h < small->height);
            if (!found) {
                found = rubraview_pixbuf_is_valid(small) &&
                        rubraview_filter_autotrim_rect(small, 15, false, &x, &y, &w, &h) &&
                        (w < small->width || h < small->height);
            }
            if (!found || w < 2 || h < 2) { osd_say(app, TU8("no plain margin was found")); break; }
            double sx = (double)app->edit.image_width / (double)small->width;
            double sy = (double)app->edit.image_height / (double)small->height;
            rubraview_edit_crop_set_ratio(&app->edit, RUBRAVIEW_CROP_FREE);
            app->unit_px &= ~(uint32_t)RUBRAVIEW_RATIO_AREA;   /* RV-187: drawn, that part of the picture */
            app->unit_frac_set &= ~(uint32_t)RUBRAVIEW_RATIO_AREA;
            rubraview_edit_crop_drag(&app->edit, (int32_t)floor(x * sx), (int32_t)floor(y * sy),
                                     (int32_t)ceil((x + w) * sx), (int32_t)ceil((y + h) * sy));
            app->edit_rev++;
            panel_build_edit(app);
            edit_sel_sync(app);
            osd_say(app, TU8("the inside of the margins is selected: Crop to the selection cuts them"));
            break;
        }
        case PANEL_SAVE_COPY:
            (void)edit_apply_pending(app, false);
            panel_write_current(app, true);
            panel_close(app);
            break;
        case PANEL_SAVE_OVER:
            panel_write_archive_page(app, true);
            panel_close(app);
            break;
        case PANEL_EXPORT_NOW:
            panel_write_current(app, app->export_from_edit);   /* opened from Edit: the edits written in */
            panel_close(app);
            break;
        case PANEL_BATCH_CROP:   /* RV-187: a crop or an effect kept from saved choices, dropped (Edit's steps do them now) */
            app->batch.crop[0] = '\0';
            app->batch.effect = 0;
            panel_open_batch(app);
            osd_say(app, TU8("the saved crop and effect dropped: Edit's steps do these"));
            break;
        case PANEL_BATCH_STEPS_FILE: {   /* RV-187: a .rvsteps file */
            static const rubraview_file_filter_t FILTERS[] = { { "Edit's steps (*.rvsteps)", "*.rvsteps" } };
            rubraview_file_dialog_opts_t opts = { .title = T("Edit's steps to run on each picture"), .filters = FILTERS, .filter_count = 1,
                                                  .parent_window_handle = rubraview_pal_window_native_handle(app->window) };
            rubraview_file_dialog_result_t picked = rubraview_pal_file_dialog_open(app->arena, &opts);
            if (picked.accepted && picked.count > 0) {
                proven_u8str_view_t text = rubraview_pal_fs_read_file(app->arena, picked.paths[0], 4u << 20);
                static rubraview_edit_steps_t probe;
                const char *why = NULL;
                if (text.size == 0 || !rubraview_edit_steps_parse(app->arena, text, &probe, &why)) {
                    char line[300];
                    snprintf(line, sizeof(line), T("not used: %s"), why ? rubraview_tr(why) : T("the file could not be read"));
                    osd_say(app, cstr(line));
                } else {
                    snprintf(app->batch.steps_file, sizeof(app->batch.steps_file), "%.*s", (int)picked.paths[0].size, picked.paths[0].ptr);
                    edit_steps_say_programs(app, &probe);
                }
            }
            panel_open_batch(app);
            break;
        }
        case PANEL_BATCH_CANVAS: case PANEL_BATCH_NAME: case PANEL_BATCH_REPLACE: case PANEL_BATCH_OUT: case PANEL_BATCH_TEXT:
            batch_type_begin(app, app->panel.rows[index].id);
            break;
        case PANEL_BATCH_TEST: batch_try_names(app); break;
        case PANEL_BATCH_SAVE: batch_save_or_load(app, true); break;
        case PANEL_BATCH_LOAD: batch_save_or_load(app, false); break;
        case PANEL_BATCH_MARK:
            if (app->batch.mark[0]) { app->batch.mark[0] = '\0'; panel_open_batch(app); break; }   /* pressed again: none */
            {
                static const rubraview_file_filter_t FILTERS[] = { { "Pictures (*.png;*.webp;*.gif;*.jpg)", "*.png;*.webp;*.gif;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff" } };
                rubraview_file_dialog_opts_t opts = { .title = T("A picture to lay over each one"), .filters = FILTERS, .filter_count = 1,
                                                      .parent_window_handle = rubraview_pal_window_native_handle(app->window) };
                rubraview_file_dialog_result_t picked = rubraview_pal_file_dialog_open(app->arena, &opts);
                if (picked.accepted && picked.count > 0)
                    snprintf(app->batch.mark, sizeof(app->batch.mark), "%.*s", (int)picked.paths[0].size, picked.paths[0].ptr);
                panel_open_batch(app);
            }
            break;
        case PANEL_BATCH_RUN:
            /* The dialog builds the same job the command line builds,
               and hands it to the same engine (§3.11). */
            panel_run_batch(app);
            panel_close(app);
            break;
        case PANEL_PRINT_DIALOG:
            app->print_with_dialog = true;
            /* fall through */
        case PANEL_PRINT_NOW:
            /* The panel goes first: the system's dialog holds the window while it is up. */
            app->print_going = true;
            panel_close(app);
            print_now(app);
            app->print_going = false;
            print_temp_drop(app);
            break;
        case PANEL_CONV_NOW: {
            rubraview_arcedit_target_t target = arc_convert_target(app);
            app->panel_is_convert = false;   /* answered: closing the panel is not a "no" */
            panel_close(app);
            arc_edit_start(app, target);
            break;
        }
        default: break;
    }
}

/* The owner's edit: a slider's name pressed asks for its value, typed. */
static bool edit_type_value_at(app_state_t *app, rubraview_panel_t *panel, int32_t which, double px, double py) {
    int32_t index = rubraview_panel_row_at(panel, px, py);
    if (index < 0 || panel->rows[index].kind != RUBRAVIEW_ROW_SLIDER) return false;
    rubraview_rect_t c = rubraview_panel_control_rect(panel, (size_t)index);
    if (px >= c.x) return false;
    if (app->rename_active) rename_end(app);
    char now[32];
    const rubraview_panel_row_t *row = &panel->rows[index];
    int n = snprintf(now, sizeof(now), row->step >= 1.0 ? "%.0f" : "%.2f", row->value);
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    rubraview_textedit_set(&app->rename_edit, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)now, .size = n > 0 ? (size_t)n : 0 });
    rubraview_textedit_select_all(&app->rename_edit);
    app->rename_active = true;
    app->rename_is_value = true;
    app->rename_composing_length = 0;
    app->typed_panel = which;
    app->typed_id = row->id;
    rubraview_pal_window_text_input(app->window, true);
    osd_say(app, TU8("type the value, then Enter (Esc leaves it)"));
    return true;
}

void edit_value_typed(app_state_t *app) {
    proven_u8str_view_t text = rubraview_textedit_text(&app->rename_edit);
    char buf[48];
    size_t n = text.size < sizeof(buf) - 1 ? text.size : sizeof(buf) - 1;
    memcpy(buf, text.ptr, n);
    buf[n] = '\0';
    for (size_t i = 0; i < n; ++i) if (buf[i] == ',') buf[i] = '.';
    char *end = NULL;
    double v = strtod(buf, &end);
    int32_t which = app->typed_panel, id = app->typed_id;
    rename_end(app);
    if (!end || end == buf) { osd_say(app, TU8("that is not a number")); return; }
    rubraview_panel_t *panel = which == 2 ? &app->sel_panel : &app->panel;
    rubraview_panel_set_value(panel, id, v);
    int32_t index = rubraview_panel_find(panel, id);
    if (which == 2) edit_sel_apply_row(app, index); else panel_apply_row(app, index);
}

/* The two boxes beside the edit: the selection's numbers and the undo list. */
bool edit_side_press(app_state_t *app, double px, double py) {
    if (!edit_panel_open(app)) return false;
    rubraview_panel_t *boxes[2] = { &app->steps_panel, &app->sel_panel };
    for (int b = 0; b < 2; ++b) {
        rubraview_panel_t *box = boxes[b];
        if (!box->open || !rubraview_rect_contains(box->bounds, px, py)) continue;
        if (scrollbar_press_panel(app, box, px, py)) return true;   /* RV-172 */
        if (rubraview_panel_title_hit(box, px, py)) { rubraview_panel_move_begin(box, px, py); return true; }
        if (box == &app->sel_panel && edit_type_value_at(app, box, 2, px, py)) return true;
        int32_t row = -1;
        rubraview_panel_event_t ev = rubraview_panel_press(box, px, py, &row);
        if (ev == RUBRAVIEW_PANEL_CLOSE) { box->open = false; return true; }
        if (row < 0) return true;
        if (ev == RUBRAVIEW_PANEL_CHOOSE) {   /* 1.24.0 */
            rubraview_rect_t c = rubraview_panel_control_rect(box, (size_t)row);
            chooser_open_panel(app, box == &app->sel_panel ? 2 : 1, row, c.x, c.y + c.height, c.y);
            return true;
        }
        int32_t id = box->rows[row].id;
        if (box == &app->steps_panel && id >= PANEL_STEP_FIRST) {
            edit_live_close(app);
            if (rubraview_edit_steps_go(&app->edit_steps, (size_t)(id - PANEL_STEP_FIRST))) edit_steps_changed(app);
        } else if (ev == RUBRAVIEW_PANEL_VALUE_CHANGED) {
            edit_sel_apply_row(app, row);
        } else if (ev == RUBRAVIEW_PANEL_BUTTON_PRESSED && id == PANEL_SEL_ALL) {
            app->edit.crop_active = false;
            app->edit_rev++;
            app->edit_shown_valid = false;
            panel_build_edit(app);
            edit_sel_sync(app);
        } else if (ev == RUBRAVIEW_PANEL_BUTTON_PRESSED && id == PANEL_SEL_TYPE) {
            px_edit_begin_crop(app);
        }
        return true;
    }
    return false;
}

bool panel_handle_press(app_state_t *app, double px, double py) {
    if (!app->panel.open) return false;

    /* The curve box is drawn outside the panel's own rectangle, so the
       rule below ("a click outside closes it") would shut the workbench
       the moment a point was grabbed. It is asked first. */
    if (curve_widget_press(app, px, py, false)) return true;
    if (scrollbar_press_panel(app, &app->panel, px, py)) return true;   /* RV-172: a column's bar */

    /* The owner's edit: taken by its title, it moves. */
    if (rubraview_panel_title_hit(&app->panel, px, py)) { rubraview_panel_move_begin(&app->panel, px, py); return true; }
    /* A value's name (left of its slider) is pressed: the value is typed. */
    /* 1.24.0 (owner, 2026-10-19): every panel's sliders, print and export too */
    if (edit_type_value_at(app, &app->panel, 1, px, py)) return true;
    int32_t row = -1;
    rubraview_panel_event_t event = rubraview_panel_press(&app->panel, px, py, &row);
    if (event == RUBRAVIEW_PANEL_CLOSE) {   /* its X (owner, 2026-10-15) */
        panel_close(app);
        return true;
    }
    if (event == RUBRAVIEW_PANEL_VALUE_CHANGED) {
        panel_apply_row(app, row);
        return true;
    }
    if (event == RUBRAVIEW_PANEL_CHOOSE) {   /* 1.24.0: its words to pick from, under it */
        rubraview_rect_t c = rubraview_panel_control_rect(&app->panel, (size_t)row);
        chooser_open_panel(app, 1, row, c.x, c.y + c.height, c.y);
        return true;
    }
    if (event == RUBRAVIEW_PANEL_BUTTON_PRESSED) {
        panel_button(app, row);
        return true;
    }
    /* A click outside the panel closes it, which is the same rule the
       menu box follows (§3.6.2) — not the edit's, whose selection is
       drawn outside it. */
    if (edit_panel_open(app) && !rubraview_rect_contains(app->panel.bounds, px, py)) return false;
    if (!rubraview_rect_contains(app->panel.bounds, px, py)) {
        panel_close(app);
        return true;
    }
    return true;   /* the click landed on the panel: it is not the canvas's */
}


/* Draws whichever of the three panels is open. The model decided every
   rectangle; this only fills them. */
void draw_panel_of(app_state_t *app, const rubraview_panel_t *panel, bool numbers);
void draw_panel(app_state_t *app) {
    bool windowed = app->edit_window && edit_panel_open(app);   /* RV-137: Edit draws in its own window */
    if (app->panel.open && !windowed) draw_panel_of(app, &app->panel, edit_panel_open(app));
    /* The owner's edit: the selection's numbers and the undo list, each a box of its own. */
    if (edit_panel_open(app) && app->sel_panel.open && !windowed) draw_panel_of(app, &app->sel_panel, true);
    if (edit_panel_open(app) && app->steps_panel.open && !windowed) draw_panel_of(app, &app->steps_panel, false);
    /* RV-133: the classify window says what the page on screen is set aside for */
    if (app->classify_panel.open) {
        if (app->classify_shown_page != current_page_index(app)) classify_build(app);
        draw_panel_of(app, &app->classify_panel, false);
    }
}

void draw_panel_of(app_state_t *app, const rubraview_panel_t *panel, bool numbers) {
    rubraview_pal_rect_t body = { panel->bounds.x, panel->bounds.y,
                                  panel->bounds.width, panel->bounds.height };
    double text_size = panel->row_height * 0.45;
    if (panel->bare) goto rows;   /* RV-137: the window is its box; no title or X of its own */
    rubraview_pal_render_fill_rect(app->renderer, body, COLOR_BOX_FILL, 3.0);
    rubraview_pal_render_stroke_rect(app->renderer, body, COLOR_BOX_BORDER, 1.0, 3.0);

    rubraview_pal_rect_t title = { panel->bounds.x + panel->padding,
                                   panel->bounds.y + panel->padding,
                                   panel->bounds.width - panel->padding * 2.0,
                                   panel->row_height };
    rubraview_pal_render_draw_text(app->renderer, panel->title, title,
                                   text_size * 1.15, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    {
        /* The X that closes it with the mouse (owner, 2026-10-15). */
        rubraview_rect_t xr = rubraview_panel_close_rect(panel);
        rubraview_pal_rect_t xb = { xr.x, xr.y, xr.width, xr.height };
        if (rubraview_rect_contains(xr, app->pointer_x, app->pointer_y)) rubraview_pal_render_fill_rect(app->renderer, xb, COLOR_CLOSE_HOVER, 2.0);
        draw_sign(app->renderer, RUBRAVIEW_ICON_CLOSE, "X", xb, text_size * 0.9, COLOR_TEXT);   /* RV-181 */
    }

rows:
    g_conv_is_7z = lround(rubraview_settings_get(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("convert_format"))) == 1;
    for (size_t i = 0; i < panel->row_count; ++i) {
        const rubraview_panel_row_t *row = &panel->rows[i];
        rubraview_rect_t r = rubraview_panel_row_rect(panel, i);
        if (r.width <= 0.0) continue;  /* scrolled out of the box */
        if (row->kind == RUBRAVIEW_ROW_SPACER) continue;   /* room the caller draws in */

        if (row->kind == RUBRAVIEW_ROW_SEPARATOR) {
            rubraview_pal_rect_t rule = { r.x, r.y + r.height * 0.5, r.width, 1.0 };
            rubraview_pal_render_fill_rect(app->renderer, rule, COLOR_BOX_BORDER, 0.0);
            continue;
        }
        if (row->kind == RUBRAVIEW_ROW_HEADING) {   /* RV-188: a section's name, in the headings' colour, a rule under it */
            rubraview_pal_rect_t words = { r.x, r.y, r.width, r.height - 3.0 };
            rubraview_pal_render_draw_text(app->renderer, rubraview_tr_u8(row->label), words, text_size * 1.02, SETTINGS_ACCENT, RUBRAVIEW_TEXT_LEFT);
            rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ r.x, r.y + r.height - 2.0, r.width, 1.0 }, 0x80E0B46Au, 0.0);
            continue;
        }

        rubraview_rect_t c = rubraview_panel_control_rect(panel, i);

        if (row->kind == RUBRAVIEW_ROW_BUTTON) {
            rubraview_pal_rect_t button = { c.x, c.y + c.height * 0.15, c.width, c.height * 0.7 };
            rubraview_pal_render_fill_rect(app->renderer, button, COLOR_TILE_FILL, 2.0);
            rubraview_pal_render_stroke_rect(app->renderer, button, COLOR_BOX_BORDER, 1.0, 2.0);
            rubraview_pal_render_draw_text(app->renderer, row->label, button,
                                           text_size, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
            continue;
        }

        rubraview_pal_rect_t label = { r.x, r.y, panel->label_width, r.height };
        rubraview_pal_render_draw_text(app->renderer, row->label, label,
                                       text_size, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);

        if (row->kind == RUBRAVIEW_ROW_SLIDER) {
            rubraview_pal_rect_t track = { c.x, c.y + c.height * 0.4, c.width, c.height * 0.2 };
            rubraview_pal_render_fill_rect(app->renderer, track, COLOR_BOX_BORDER, 1.0);

            double fill = rubraview_panel_fill_fraction(panel, i);
            rubraview_pal_rect_t handle = { c.x + c.width * fill - c.height * 0.2, c.y,
                                            c.height * 0.4, c.height };
            rubraview_pal_render_fill_rect(app->renderer, handle, COLOR_TEXT, 2.0);
            if (numbers) {
                /* The owner's edit: every value as a number, typed by pressing its name. */
                char number[24];
                int k = snprintf(number, sizeof(number), row->step >= 1.0 ? "%.0f" : "%.2f", row->value);
                rubraview_pal_rect_t at = { r.x, r.y, panel->label_width - panel->padding * 0.5, r.height };
                if (k > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)number, .size = (size_t)k }, at,
                                                          text_size * 0.9, 0xFFB8D0F0u, RUBRAVIEW_TEXT_RIGHT);
            } else if (!app->panel_is_print && !app->panel_is_convert &&
                (row->id == PANEL_RESIZE || row->id == PANEL_SHARPEN_RADIUS || row->id == PANEL_SHARPEN_THRESHOLD)) {
                char number[16];
                rubraview_pal_rect_t at = { r.x, r.y, panel->label_width - panel->padding, r.height };
                rubraview_pal_render_draw_text(app->renderer, adjust_row_text(app, row, number, sizeof(number)),
                                               at, text_size, COLOR_TEXT, RUBRAVIEW_TEXT_RIGHT);
            }
            bool shown = numbers || (!app->panel_is_print && !app->panel_is_convert &&
                         (row->id == PANEL_RESIZE || row->id == PANEL_SHARPEN_RADIUS || row->id == PANEL_SHARPEN_THRESHOLD));
            if (app->panel_is_print || app->panel_is_convert) {
                /* D-87: a scale or a margin is chosen by its number. */
                char number[16];
                rubraview_pal_rect_t at = { r.x, r.y, panel->label_width - panel->padding, r.height };
                rubraview_pal_render_draw_text(app->renderer, print_row_text(row, number, sizeof(number)),
                                               at, text_size, COLOR_TEXT, RUBRAVIEW_TEXT_RIGHT);
                shown = true;
            }
            if (!shown) {   /* 1.24.0: every slider says its number */
                char number[24];
                int k = snprintf(number, sizeof(number), row->step >= 1.0 ? "%.0f" : "%.2f", row->value);
                rubraview_pal_rect_t at = { r.x, r.y, panel->label_width - panel->padding, r.height };
                if (k > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)number, .size = (size_t)k }, at,
                                                          text_size, COLOR_TEXT, RUBRAVIEW_TEXT_RIGHT);
            }
            {   /* 1.24.0: and in a box, which a press opens for typing (as its name does) */
                double fw = text_size * 4.2;
                rubraview_pal_rect_t field = { r.x + panel->label_width - panel->padding * 0.4 - fw, r.y + r.height * 0.14, fw, r.height * 0.72 };
                rubraview_pal_render_stroke_rect(app->renderer, field, pal_rect_has(field, app->pointer_x, app->pointer_y) ? COLOR_TILE_CURRENT : COLOR_BOX_BORDER,
                                                 1.0, 2.0);
            }
        } else if (row->kind == RUBRAVIEW_ROW_TOGGLE) {
            rubraview_pal_rect_t box = { c.x, c.y, c.height, c.height };
            rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_BOX_BORDER, 1.0, 2.0);
            if (row->value > 0.5) {
                rubraview_pal_rect_t mark = { box.x + box.width * 0.25, box.y + box.height * 0.25,
                                              box.width * 0.5, box.height * 0.5 };
                rubraview_pal_render_fill_rect(app->renderer, mark, COLOR_TEXT, 1.0);
            }
        } else if (row->kind == RUBRAVIEW_ROW_CHOICE && (app->panel_is_print || app->panel_is_convert)) {
            /* D-87: the print panel's choices have names. */
            char words[48];
            rubraview_pal_rect_t value = { c.x, c.y, c.width, c.height };
            rubraview_pal_render_draw_text(app->renderer, print_row_text(row, words, sizeof(words)),
                                           value, text_size, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
        } else if (row->kind == RUBRAVIEW_ROW_CHOICE) {
            /* RV-108: a choice says its word. */
            char words[48];
            rubraview_pal_rect_t value = { c.x, c.y, c.width, c.height };
            rubraview_pal_render_draw_text(app->renderer, adjust_row_text(app, row, words, sizeof(words)),
                                           value, text_size, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
        }
    }
    draw_panel_scrollbars(app, panel);   /* RV-172: where its rows are, and moved there */
}
