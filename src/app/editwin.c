/*
 * Edit: its window and steps, the pixel box, the live preview and histogram, the selection and the curve,
 * plug-ins, G'MIC and FFmpeg filters as steps, the Export rows, one edit onto the whole list
 * (RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* ---- The Adjust panel's size in pixels (owner, 2026-10-15) ---- */

static void px_edit_fill(app_state_t *app, int32_t field, int32_t value) {
    int n = snprintf(app->px_edit_text[field], sizeof(app->px_edit_text[0]), "%d", (int)value);
    app->px_edit_length[field] = n > 0 ? (size_t)n : 0;
}

/* RV-128: the resize is of the whole picture whatever is selected: the
   size helpers are given the session with the selection left out. */
rubraview_edit_session_t edit_whole(const app_state_t *app) {
    rubraview_edit_session_t s = app->edit;
    s.crop_active = false;
    return s;
}

static int32_t px_edit_value(const app_state_t *app, int32_t field) {
    if (!app->px_edit_crop) {   /* a size: pixels, or a length in any unit (RV-128) */
        int32_t whole = field == 0 ? app->edit.image_width : app->edit.image_height, px = 0;
        proven_u8str_view_t typed = { .ptr = (const proven_byte_t *)app->px_edit_text[field], .size = app->px_edit_length[field] };
        return rubraview_resize_parse_length(typed, whole, app->edit.resize_dpi, &px) ? px : 0;
    }
    int32_t v = 0;
    for (size_t i = 0; i < app->px_edit_length[field]; ++i) v = v * 10 + (app->px_edit_text[field][i] - '0');
    return v;
}

int32_t px_edit_fields(const app_state_t *app) { return app->px_edit_crop ? 4 : 2; }

/* The crop's numbers into the box, from the session: after a drag on the picture too. */
void px_edit_fill_crop(app_state_t *app) {
    const rubraview_edit_session_t *e = &app->edit;
    rubraview_crop_rect_t c = e->crop_active ? e->crop : (rubraview_crop_rect_t){ 0, 0, e->image_width, e->image_height };
    px_edit_fill(app, 0, c.x);
    px_edit_fill(app, 1, c.y);
    px_edit_fill(app, 2, c.width);
    px_edit_fill(app, 3, c.height);
}

void px_edit_begin_crop(app_state_t *app) {
    if (app->edit.image_width <= 0 || app->edit.image_height <= 0) { osd_say(app, TU8("the picture's size is not known yet")); return; }
    app->px_edit_crop = true;
    px_edit_fill_crop(app);
    app->px_edit_field = 0;
    app->px_edit_open = true;
    rubraview_pal_window_text_input(app->window, true);
}

void px_edit_begin(app_state_t *app) {
    const rubraview_edit_session_t *e = &app->edit;
    app->px_edit_crop = false;
    int32_t w = e->resize_width, h = e->resize_height;
    if (w <= 0 || h <= 0) { w = e->image_width; h = e->image_height; }   /* the whole picture (RV-128) */
    if (w <= 0 || h <= 0) { osd_say(app, TU8("the picture's size is not known yet")); return; }
    px_edit_fill(app, 0, w);
    px_edit_fill(app, 1, h);
    app->px_edit_field = 0;
    app->px_edit_open = true;
    rubraview_pal_window_text_input(app->window, true);
}

void px_edit_close(app_state_t *app) {
    app->px_edit_open = false;
    rubraview_pal_window_text_input(app->window, false);
}

/* The field in hand changed: the other follows the shape. */
static void px_edit_follow(app_state_t *app) {
    if (app->px_edit_crop) {
        /* The owner's edit: the four numbers move the selection as they are typed. */
        if (rubraview_edit_crop_set(&app->edit, px_edit_value(app, 0), px_edit_value(app, 1), px_edit_value(app, 2), px_edit_value(app, 3))) {
            app->edit.crop_active = true;
            app->edit_rev++;
            app->edit_shown_valid = false;
            edit_sel_sync(app);
        }
        return;
    }
    int32_t f = app->px_edit_field;
    int32_t w = px_edit_value(app, 0), h = px_edit_value(app, 1);
    rubraview_edit_session_t whole = edit_whole(app);
    if (px_edit_value(app, f) > 0 && whole.resize_lock_aspect && rubraview_edit_resize_pair(&whole, px_edit_value(app, f), f == 0, &w, &h)) {
        px_edit_fill(app, 1 - f, f == 0 ? h : w);
    }
}

proven_u8str_view_t crop_button_label(const app_state_t *app);
static bool px_edit_commit(app_state_t *app) {
    if (app->px_edit_crop) {
        if (!rubraview_edit_crop_set(&app->edit, px_edit_value(app, 0), px_edit_value(app, 1), px_edit_value(app, 2), px_edit_value(app, 3))) {
            osd_say(app, TU8("the corner must be inside the picture, and the size at least 1 pixel"));
            return false;
        }
        if (app->panel.open) panel_build_edit(app);   /* the ratio is free now, the sizes from the cut */
        app->edit_shown_valid = false;
        char line[80];
        const rubraview_crop_rect_t *c = &app->edit.crop;
        int n = snprintf(line, sizeof(line), T("cut at %d, %d: %d x %d pixels"), (int)c->x, (int)c->y, (int)c->width, (int)c->height);
        if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
        return true;
    }
    int32_t f = app->px_edit_field;
    int32_t w = px_edit_value(app, 0), h = px_edit_value(app, 1);
    rubraview_edit_session_t whole = edit_whole(app);
    if (w <= 0 || h <= 0 || (whole.resize_lock_aspect && !rubraview_edit_resize_pair(&whole, px_edit_value(app, f), f == 0, &w, &h))) {
        osd_say(app, TU8("a size is 1 to 65535 pixels, or a length: 21cm, 210mm, 8.5in, 50%"));
        return false;
    }
    rubraview_edit_set_resize(&app->edit, w, h, f == 0);
    app->edit.resize_width = w;    /* both as typed, also with the lock off */
    app->edit.resize_height = h;
    if (app->panel.open) panel_build_edit(app);   /* Resize % and its pixels from the session */
    app->edit_shown_valid = false;
    char line[64];
    int n = snprintf(line, sizeof(line), T("saved at %d x %d pixels"), (int)w, (int)h);
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
    return true;
}

bool px_edit_handle_key(app_state_t *app, rubraview_key_combo_t combo) {
    if (!app->px_edit_open) return false;
    if (key_is(combo, "Escape")) { px_edit_close(app); return true; }
    if (key_is(combo, "Enter")) {
        if (px_edit_commit(app)) px_edit_close(app);
        return true;
    }
    if (key_is(combo, "Tab") || key_is(combo, "Down")) {
        app->px_edit_field = (app->px_edit_field + 1) % px_edit_fields(app);
        return true;
    }
    if (key_is(combo, "Shift+Tab") || key_is(combo, "Up")) {
        app->px_edit_field = (app->px_edit_field + px_edit_fields(app) - 1) % px_edit_fields(app);
        return true;
    }
    if (key_is(combo, "Backspace")) {
        int32_t f = app->px_edit_field;
        if (app->px_edit_length[f] > 0) app->px_edit_text[f][--app->px_edit_length[f]] = '\0';
        px_edit_follow(app);
        return true;
    }
    return true;   /* the digits arrive as text; the rest is swallowed */
}

void px_edit_text_typed(app_state_t *app, proven_u8str_view_t text) {
    int32_t f = app->px_edit_field;
    for (size_t i = 0; i < text.size; ++i) {
        char c = text.ptr[i];
        bool digit = c >= '0' && c <= '9';
        /* RV-128: a size may be typed with its unit: 21cm, 210 mm, 8.5in, 50% */
        bool unit = !app->px_edit_crop && c != '\0' && strchr(".,% cmMiInNpPxX\"", c) != NULL;
        if (!digit && !unit) continue;
        size_t most = app->px_edit_crop ? 5u : sizeof(app->px_edit_text[0]) - 1u;
        if (app->px_edit_length[f] + 1 >= sizeof(app->px_edit_text[0]) || app->px_edit_length[f] >= most) break;
        if (app->px_edit_length[f] == 1 && app->px_edit_text[f][0] == '0') app->px_edit_length[f] = 0;
        app->px_edit_text[f][app->px_edit_length[f]++] = c;
        app->px_edit_text[f][app->px_edit_length[f]] = '\0';
    }
    px_edit_follow(app);
}

/* ---- the owner's edit onto the whole list (2026-10-18) ----
   "재생목록 중 이미지 전체에 적용 버튼을 달아서, 그걸 누르면 이미지 전체에 해당 효과가
   적용된 사본이 동일 폴더에 사용자가 지정한 접두사 아니면 접미사를 달고 생기게". */

void apply_all_begin(app_state_t *app) {
    if (!edit_panel_open(app)) { osd_say(app, TU8("open Edit (E) and make the change first")); return; }
    edit_live_close(app);
    if (app->edit_steps.cursor == 0) { osd_say(app, TU8("make a change in Edit first: its steps are what is applied")); return; }
    if (app->source.kind != RUBRAVIEW_PAGE_SOURCE_FOLDER) { osd_say(app, TU8("the pictures of a folder or a list, not of an archive")); return; }
    if (app->apply_job) { osd_say(app, TU8("still applying the last one")); return; }
    if (app->rename_active) rename_end(app);
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    proven_u8str_view_t last = rubraview_settings_get_text(&app->settings, PROVEN_LIT("edit"), PROVEN_LIT("copy_name"));
    rubraview_textedit_set(&app->rename_edit, last.size > 0 ? last : PROVEN_LIT("*_edit"));
    rubraview_textedit_select_all(&app->rename_edit);
    app->rename_active = true;
    app->rename_is_apply = true;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, true);
    osd_say(app, TU8("each copy's name: * is the picture's own (*_edit, edited_*); Enter"));
}

static DWORD WINAPI apply_all_run(LPVOID param) {
    apply_all_job_t *job = (apply_all_job_t*)param;
    bool com = SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
    for (size_t i = 0; i < job->count && !InterlockedCompareExchange(&job->cancel, 0, 0); ++i) {
        proven_u8str_view_t path = cstr(job->paths[i]);
        bool ok = false;
        int32_t w = 0, h = 0;
        if (com && rubraview_pal_image_size(path, &w, &h) && w > 0 && h > 0) {
            rubraview_edit_steps_t steps = job->steps;
            rubraview_edit_steps_rebase(&steps, w, h);   /* the same parts of this picture */
            size_t biggest = picture_bytes((size_t)w, (size_t)h, 4u, 0);
            for (size_t k = 1; k <= steps.cursor; ++k) {
                int32_t sw = 0, sh = 0;
                rubraview_edit_steps_size(&steps, k, &sw, &sh);
                size_t b = picture_bytes((size_t)sw, (size_t)sh, 4u, 0);
                if (b > biggest) biggest = b;
            }
            size_t half = bytes_scaled(biggest, 6u, 16u << 20), all = bytes_scaled(half, 2u, 0);
            size_t first = picture_bytes((size_t)w, (size_t)h, 4u * 3u, 16u << 20);
            void *mem = all == SIZE_MAX || first == SIZE_MAX || PROVEN_CKD_ADD(&all, all, first) ? NULL : malloc(all);
            if (mem) {
                proven_arena_t read = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)mem, .size = first });
                proven_arena_t lanes[2] = {
                    proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)mem + first, .size = half }),
                    proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)mem + first + half, .size = half }),
                };
                rubraview_pixbuf_t px = rubraview_pal_image_read_pixels(&read, path, NULL, 0, true);
                ok = rubraview_pixbuf_is_valid(&px);
                for (size_t k = 0; ok && k < steps.cursor; ++k) {
                    proven_arena_t *lane = &lanes[k % 2u];
                    proven_arena_reset(lane);
                    rubraview_pixbuf_t next = rubraview_edit_step_apply(lane, &steps.steps[k], &px, 1.0);
                    if (rubraview_pixbuf_is_valid(&next)) px = next; else ok = false;
                }
                if (ok && job->over) {
                    /* RV-137: written beside it first, then the original to the Recycle Bin and this in its place */
                    rubraview_export_options_t options = job->options;
                    rubraview_export_format_t f = rubraview_export_format_for_name(path);
                    options.format = f;
                    proven_u8str_view_t ext = rubraview_export_extension(f);
                    proven_u8str_view_t stem = rubraview_path_stem(rubraview_path_basename(path));
                    proven_u8str_view_t dir = rubraview_path_dirname(path);
                    char tmp[2048];
                    int n = snprintf(tmp, sizeof(tmp), "%.*s\\%.*s.rvsaving.%.*s", (int)dir.size, dir.ptr, (int)stem.size, stem.ptr, (int)ext.size, ext.ptr);
                    proven_u8str_view_t out = { .ptr = (const proven_byte_t *)tmp, .size = n > 0 && (size_t)n < sizeof(tmp) ? (size_t)n : 0 };
                    ok = f != RUBRAVIEW_EXPORT_SAME_AS_SOURCE && out.size > 0 && rubraview_pal_image_save(out, &px, &options);
                    if (ok) (void)details_keep(path, out, path, f, &options);   /* 1.17.0: read from the original before it goes */
                    ok = ok && rubraview_pal_fs_recycle(path) && rubraview_pal_fs_replace(out, path);
                    if (!ok && out.size > 0 && rubraview_pal_fs_exists(out) && rubraview_pal_fs_exists(path)) (void)rubraview_pal_fs_recycle(out);
                } else if (ok) {
                    /* in its own format when it can be written, else PNG (RV-139: AVIF and JPEG XL when FFmpeg can) */
                    rubraview_export_options_t options = job->options;
                    rubraview_export_format_t f = rubraview_export_format_for_name(path);
                    uint32_t ff = f == RUBRAVIEW_EXPORT_AVIF || f == RUBRAVIEW_EXPORT_JXL ? rubraview_pal_ffmpeg_image_encoders(NULL, 0) : 0u;
                    if ((f == RUBRAVIEW_EXPORT_AVIF && !(ff & RUBRAVIEW_FFMPEG_WRITES_AVIF)) ||
                        (f == RUBRAVIEW_EXPORT_JXL && !(ff & RUBRAVIEW_FFMPEG_WRITES_JXL))) f = RUBRAVIEW_EXPORT_SAME_AS_SOURCE;
                    if (f == RUBRAVIEW_EXPORT_SAME_AS_SOURCE) f = RUBRAVIEW_EXPORT_PNG;
                    options.format = f;
                    proven_u8str_view_t ext = rubraview_export_extension(f);
                    proven_u8str_view_t stem = rubraview_path_stem(rubraview_path_basename(path));
                    char name[1024];
                    int n = snprintf(name, sizeof(name), "%s%.*s%s.%.*s", job->prefix, (int)stem.size, stem.ptr, job->suffix, (int)ext.size, ext.ptr);
                    proven_arena_t *names = &lanes[steps.cursor % 2u];
                    proven_u8str_view_t target = n > 0 && (size_t)n < sizeof(name)
                        ? rubraview_filing_target(names, rubraview_path_dirname(path), (proven_u8str_view_t){ .ptr = (const proven_byte_t *)name, .size = (size_t)n }, filing_exists, NULL)
                        : PROVEN_LIT("");
                    ok = target.size > 0 && rubraview_pal_image_save(target, &px, &options);
                    if (ok) (void)details_keep(path, target, target, f, &options);   /* 1.17.0 */
                }
                free(mem);
            }
        }
        if (ok) InterlockedIncrement(&job->done); else InterlockedIncrement(&job->failed);
        rubraview_pal_window_wake(job->wake);
    }
    rubraview_pal_image_thread_end();
    if (com) CoUninitialize();
    InterlockedExchange(&job->finished, 1);
    rubraview_pal_window_wake(job->wake);
    return 0;
}

/* A copy Edit made, by its name as the copies' pattern makes them ("*_edit"):
   left out when the steps go onto the list's pictures, as copies or over the
   originals (owner, 2026-10-18), so a copy is not edited a second time. */
static bool edit_is_copy_name(proven_u8str_view_t path, proven_u8str_view_t pattern) {
    if (pattern.size == 0) pattern = PROVEN_LIT("*_edit");
    return rubraview_glob_match_list(rubraview_path_stem(rubraview_path_basename(path)), pattern);
}

void apply_all_typed(app_state_t *app) {
    proven_u8str_view_t text = rubraview_textedit_text(&app->rename_edit);
    char pattern[160];
    size_t n = text.size < sizeof(pattern) - 1 ? text.size : sizeof(pattern) - 1;
    memcpy(pattern, text.ptr, n);
    pattern[n] = '\0';
    rename_end(app);
    char *star = strchr(pattern, '*');
    bool bad = !star || strchr(star + 1, '*') || n == 1 || strpbrk(pattern, "\\/:\"<>|?");
    if (bad) { osd_say(app, TU8("the name needs one * for the picture's own name and something beside it: *_edit")); return; }
    if (app->apply_job || app->edit_steps.cursor == 0 || app->source.kind != RUBRAVIEW_PAGE_SOURCE_FOLDER) return;
    rubraview_settings_set_text(&app->settings, PROVEN_LIT("edit"), PROVEN_LIT("copy_name"), app_keep(app, cstr(pattern)));
    apply_all_job_t *job = (apply_all_job_t*)calloc(1, sizeof(*job));
    if (!job) return;
    size_t count = page_count(app);
    job->paths = (char**)calloc(count ? count : 1, sizeof(char*));
    for (size_t i = 0; job->paths && i < count; ++i) {
        proven_u8str_view_t p = app->source.pages[i].path;
        if (p.size == 0 || !rubraview_glob_match_list(rubraview_path_basename(p), PROVEN_LIT(IMAGE_FILTER))) continue;   /* pictures only */
        if (edit_is_copy_name(p, cstr(pattern))) continue;                                                    /* not a copy made before */
        char *copy = (char*)malloc(p.size + 1);
        if (!copy) continue;
        memcpy(copy, p.ptr, p.size);
        copy[p.size] = '\0';
        job->paths[job->count++] = copy;
    }
    if (job->count == 0) { free(job->paths); free(job); osd_say(app, TU8("no pictures in the list")); return; }
    *star = '\0';
    snprintf(job->prefix, sizeof(job->prefix), "%s", pattern);
    snprintf(job->suffix, sizeof(job->suffix), "%s", star + 1);
    apply_all_start(app, job);
}

/* The job's pictures listed: the steps in force, the export's choices, a thread. */
void apply_all_start(app_state_t *app, apply_all_job_t *job) {
    job->steps = app->edit_steps;
    job->options = app->export_options;
    if (app->edit.resize_dpi > 0.0) job->options.dpi = app->edit.resize_dpi;
    job->wake = app->window;
    job->thread = CreateThread(NULL, 0, apply_all_run, job, 0, NULL);
    if (!job->thread) {
        for (size_t i = 0; i < job->count; ++i) free(job->paths[i]);
        free(job->paths); free(job);
        osd_say(app, TU8("could not start applying"));
        return;
    }
    app->apply_job = job;
    app->apply_said = -1;
    char said[96];
    int m = snprintf(said, sizeof(said), T("applying the edit to %zu pictures..."), job->count);
    if (m > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)said, .size = (size_t)m });
}

/* RV-137 (owner, 2026-10-18): the steps written over every picture of the
   folder or playlist, on a second press, the old files to the Recycle Bin. */
void apply_all_originals(app_state_t *app) {
    static double asked_at = -100.0;
    if (!edit_panel_open(app)) return;
    edit_live_close(app);
    if (app->edit_steps.cursor == 0) { osd_say(app, TU8("make a change in Edit first: its steps are what is applied")); return; }
    if (app->source.kind != RUBRAVIEW_PAGE_SOURCE_FOLDER || app->source.archive_path.size > 0) { osd_say(app, TU8("the pictures of a folder or a list, not of an archive")); return; }
    if (app->apply_job) { osd_say(app, TU8("still applying the last one")); return; }
    size_t count = page_count(app), pictures = 0;
    proven_u8str_view_t copies = rubraview_settings_get_text(&app->settings, PROVEN_LIT("edit"), PROVEN_LIT("copy_name"));   /* Edit's copies are not originals */
    for (size_t i = 0; i < count; ++i) {
        proven_u8str_view_t p = app->source.pages[i].path;
        if (p.size > 0 && rubraview_glob_match_list(rubraview_path_basename(p), PROVEN_LIT(IMAGE_FILTER)) && !edit_is_copy_name(p, copies)) pictures++;
    }
    if (pictures == 0) { osd_say(app, TU8("no pictures in the list")); return; }
    double now = rubraview_pal_time_now_seconds();
    if (now - asked_at > 5.0) {
        asked_at = now;
        char said[160];
        int m = snprintf(said, sizeof(said), T("press again to write the changes over %zu originals (the old files go to the Recycle Bin)"), pictures);
        if (m > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)said, .size = (size_t)m });
        return;
    }
    asked_at = -100.0;
    apply_all_job_t *job = (apply_all_job_t*)calloc(1, sizeof(*job));
    if (!job) return;
    job->paths = (char**)calloc(count ? count : 1, sizeof(char*));
    for (size_t i = 0; job->paths && i < count; ++i) {
        proven_u8str_view_t p = app->source.pages[i].path;
        if (p.size == 0 || !rubraview_glob_match_list(rubraview_path_basename(p), PROVEN_LIT(IMAGE_FILTER)) || edit_is_copy_name(p, copies)) continue;
        char *copy = (char*)malloc(p.size + 1);
        if (!copy) continue;
        memcpy(copy, p.ptr, p.size);
        copy[p.size] = '\0';
        job->paths[job->count++] = copy;
    }
    if (job->count == 0) { free(job->paths); free(job); return; }
    job->over = true;
    apply_all_start(app, job);
}

/* How far it is, said as it goes; at the end, what was made. A quit stops it after the picture in hand. */
void apply_all_poll(app_state_t *app, bool quitting) {
    apply_all_job_t *job = app->apply_job;
    if (!job) return;
    if (quitting) InterlockedExchange(&job->cancel, 1);
    LONG done = InterlockedCompareExchange(&job->done, 0, 0), failed = InterlockedCompareExchange(&job->failed, 0, 0);
    if (!quitting && !InterlockedCompareExchange(&job->finished, 0, 0)) {
        if (done + failed != app->apply_said) {
            app->apply_said = done + failed;
            char said[96];
            int m = snprintf(said, sizeof(said), T("applying the edit: %ld / %zu"), (long)(done + failed), job->count);
            if (m > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)said, .size = (size_t)m });
        }
        return;
    }
    WaitForSingleObject(job->thread, INFINITE);
    CloseHandle(job->thread);
    done = job->done; failed = job->failed;
    bool over = job->over;
    for (size_t i = 0; i < job->count; ++i) free(job->paths[i]);
    free(job->paths);
    free(job);
    app->apply_job = NULL;
    if (quitting) return;
    char said[128];
    int m = over ? (failed ? snprintf(said, sizeof(said), T("%ld originals written over; %ld could not be"), (long)done, (long)failed)
                           : snprintf(said, sizeof(said), T("%ld originals written over"), (long)done))
          : failed ? snprintf(said, sizeof(said), T("%ld copies made beside the pictures; %ld could not be"), (long)done, (long)failed)
                   : snprintf(said, sizeof(said), T("%ld copies made beside the pictures"), (long)done);
    if (m > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)said, .size = (size_t)m });
    if (over && done > 0) {
        /* the pictures hold the changes now: shown again they would be made twice, and read again from their files */
        if (edit_panel_open(app)) {
            rubraview_edit_steps_reset(&app->edit_steps);
            edit_steps_changed(app);
        }
        unload_all_pages(app);
        app->needs_relayout = true;
        if (edit_panel_open(app)) {   /* Edit's preview made again from the files as they are now */
            edit_preview_close(app);
            app->edit_preview_tries = 0;
        }
    }
    /* the folder again, the picture on screen kept */
    int32_t page = current_page_index(app);
    if (page >= 0 && done > 0) reopen_on_path(app, app_keep(app, app->source.pages[page].path), (size_t)page);
}

/* ---- the owner's edit (2026-10-17): one window, steps on a selection ---- */

void edit_sel_sync(app_state_t *app);
static void edit_steps_build(app_state_t *app);

/* The picture as the steps in force left it. */
void edit_cur_size(const app_state_t *app, int32_t *w, int32_t *h) {
    rubraview_edit_steps_size(&app->edit_steps, app->edit_steps.cursor, w, h);
}

/* What an effect is put on now: the selection, or the whole picture. */
static rubraview_edit_area_t edit_area_now(const app_state_t *app) {
    rubraview_edit_area_t a = { .shape = RUBRAVIEW_AREA_ALL };
    if (app->edit.crop_active && app->edit.crop.width > 0 && app->edit.crop.height > 0) {
        a.shape = app->sel_shape == RUBRAVIEW_AREA_ELLIPSE ? RUBRAVIEW_AREA_ELLIPSE : RUBRAVIEW_AREA_RECT;
        a.rect = app->edit.crop;
        a.feather = app->sel_feather;
    }
    return a;
}

/* The step being made, as it would go into the list. */
rubraview_edit_step_t edit_pending(const app_state_t *app) {
    rubraview_edit_step_t s = { .kind = RUBRAVIEW_STEP_ADJUST, .area = edit_area_now(app), .values = app->edit };
    return s;
}

/* The values back to neutral for the next step; the selection and its ratio stay. */
void edit_values_fresh(app_state_t *app) {
    int32_t w = 0, h = 0;
    edit_cur_size(app, &w, &h);
    rubraview_edit_session_t fresh = rubraview_edit_begin(w, h);
    /* RV-128: how to resize is a preference, kept from step to step */
    fresh.resize_lock_aspect = app->edit.resize_lock_aspect;
    fresh.resize_filter = app->edit.resize_filter;
    fresh.resize_linear = app->edit.resize_linear;
    fresh.resize_sharpen = app->edit.resize_sharpen;
    fresh.resize_dpi = app->edit.resize_dpi;
    fresh.crop_active = app->edit.crop_active;
    fresh.crop = app->edit.crop;
    fresh.crop_ratio = app->edit.crop_ratio;
    app->edit = fresh;
}

/* After any change of the steps: the picture's size, the panels, the preview. */
void edit_steps_changed(app_state_t *app) {
    int32_t w = 0, h = 0;
    edit_cur_size(app, &w, &h);
    app->edit.image_width = w;
    app->edit.image_height = h;
    if (app->edit.crop_active && (app->edit.crop.x + app->edit.crop.width > w || app->edit.crop.y + app->edit.crop.height > h)) {
        app->edit.crop_active = false;   /* a selection outside the picture as it is now */
    }
    app->edit_rev++;
    app->edit_shown_valid = false;
    if (app->panel.open) panel_build_edit(app);
    edit_sel_sync(app);
    edit_steps_build(app);
}

static bool edit_area_same(const rubraview_edit_area_t *a, const rubraview_edit_area_t *b) {
    if (a->shape != b->shape || a->feather != b->feather) return false;
    return a->shape == RUBRAVIEW_AREA_ALL || memcmp(&a->rect, &b->rect, sizeof(a->rect)) == 0;
}

/* The change in hand ends: it stays a step, and the next change starts from nothing. */
void edit_live_close(app_state_t *app) {
    if (!app->edit_live) return;
    app->edit_live = false;
    if (app->edit_live_kind == RUBRAVIEW_STEP_EFFECT) app->effect_choice = RUBRAVIEW_EFFECT_NONE;
    else edit_values_fresh(app);
    app->edit_rev++;
    app->edit_shown_valid = false;
    if (edit_panel_open(app)) panel_build_edit(app);
}

/* A value changed (a slider, a curve, an effect or its amount): it is a step now. */
void edit_live_update(app_state_t *app, int32_t kind) {
    app->edit_live_busy = true;
    rubraview_edit_step_t s = kind == RUBRAVIEW_STEP_EFFECT
        ? (rubraview_edit_step_t){ .kind = RUBRAVIEW_STEP_EFFECT, .area = edit_area_now(app), .effect = app->effect_choice, .amount = app->effect_amount }
        : edit_pending(app);
    if (app->edit_live && app->edit_live_kind == kind && edit_area_same(&s.area, &app->edit_live_area) && app->edit_steps.cursor > 0) {
        (void)rubraview_edit_steps_set_last(&app->edit_steps, &s);   /* set again; taken off when back to nothing */
        if (!rubraview_edit_step_does_something(&s)) app->edit_live = false;
    } else {
        app->edit_live = false;   /* another kind, or another area: a new step */
        if (rubraview_edit_steps_push(&app->edit_steps, &s)) {
            app->edit_live = true;
            app->edit_live_kind = kind;
            app->edit_live_area = s.area;
        } else if (rubraview_edit_step_does_something(&s)) {
            osd_say(app, TU8("the list of steps is full: save, or undo some"));
        }
    }
    app->edit_rev++;
    app->edit_shown_valid = false;
    edit_steps_build(app);   /* the undo list; the panel itself is not built again under a moving slider */
    app->edit_live_busy = false;
}

bool edit_apply_pending(app_state_t *app, bool say) {
    if (app->edit_live) { edit_live_close(app); return true; }   /* already a step: it ends here */
    rubraview_edit_step_t s = edit_pending(app);
    if (!rubraview_edit_steps_push(&app->edit_steps, &s)) {
        if (say) osd_say(app, app->edit_steps.cursor >= RUBRAVIEW_EDIT_MAX_STEPS ? TU8("the list of steps is full: save, or undo some")
                                                                              : TU8("nothing to apply: move a slider first"));
        return false;
    }
    edit_values_fresh(app);
    edit_steps_changed(app);
    if (say) osd_say(app, s.area.shape == RUBRAVIEW_AREA_ALL ? TU8("applied to the whole picture (Ctrl+Z undoes it)")
                                                             : TU8("applied to the selection (Ctrl+Z undoes it)"));
    return true;
}

void edit_crop_to_selection(app_state_t *app) {
    if (!app->edit.crop_active) { osd_say(app, TU8("select an area on the picture first")); return; }
    (void)edit_apply_pending(app, false);
    rubraview_edit_step_t s = { .kind = RUBRAVIEW_STEP_CROP, .area = { .shape = RUBRAVIEW_AREA_RECT, .rect = app->edit.crop } };
    if (!rubraview_edit_steps_push(&app->edit_steps, &s)) return;
    app->edit.crop_active = false;
    edit_values_fresh(app);
    edit_steps_changed(app);
    osd_say(app, TU8("cropped to the selection (Ctrl+Z undoes it)"));
}

void edit_resize_apply(app_state_t *app) {
    if (app->edit.resize_width <= 0 || app->edit.resize_height <= 0) { osd_say(app, TU8("choose the size first (the Resize percent or its pixels)")); return; }
    (void)edit_apply_pending(app, false);
    rubraview_edit_step_t s = { .kind = RUBRAVIEW_STEP_RESIZE, .values = app->edit };
    if (!rubraview_edit_steps_push(&app->edit_steps, &s)) return;
    app->edit.crop_active = false;
    edit_values_fresh(app);
    edit_steps_changed(app);
    osd_say(app, TU8("resized (Ctrl+Z undoes it)"));
}

void edit_effect_apply(app_state_t *app) {
    if (app->edit_live && app->edit_live_kind == RUBRAVIEW_STEP_EFFECT) { edit_live_close(app); osd_say(app, TU8("effect applied (Ctrl+Z undoes it)")); return; }
    if (app->effect_choice <= RUBRAVIEW_EFFECT_NONE) { osd_say(app, TU8("choose an effect first")); return; }
    (void)edit_apply_pending(app, false);
    rubraview_edit_step_t s = { .kind = RUBRAVIEW_STEP_EFFECT, .area = edit_area_now(app), .effect = app->effect_choice, .amount = app->effect_amount };
    if (!rubraview_edit_steps_push(&app->edit_steps, &s)) return;
    app->effect_choice = RUBRAVIEW_EFFECT_NONE;
    edit_steps_changed(app);
    osd_say(app, TU8("effect applied (Ctrl+Z undoes it)"));
}

/* The owner's automatic colour correction, measured on the picture as the steps left it. */
void edit_auto_colour(app_state_t *app) {
    if (!rubraview_pixbuf_is_valid(&app->edit_small)) { osd_say(app, TU8("the picture is not read yet")); return; }
    edit_live_close(app);   /* measured on the steps as they are, then a step of its own */
    size_t bytes = picture_bytes((size_t)app->edit_small.width, (size_t)app->edit_small.height, 4u * (6u + 4u * app->edit_steps.cursor), 8u << 20);
    void *mem = malloc(bytes);
    if (!mem) return;
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)mem, .size = bytes });
    double ratio = app->edit_steps.width > 0 ? (double)app->edit_small.width / (double)app->edit_steps.width : 1.0;
    rubraview_pixbuf_t now = rubraview_edit_steps_apply(&arena, &app->edit_steps, &app->edit_small, ratio);
    if (rubraview_pixbuf_is_valid(&now)) {
        rubraview_edit_auto_colour(&app->edit, &now);
        edit_live_update(app, RUBRAVIEW_STEP_ADJUST);
        panel_build_edit(app);
        osd_say(app, TU8("colours corrected (Ctrl+Z undoes it)"));
    }
    free(mem);
}

void edit_undo(app_state_t *app, bool redo) {
    edit_live_close(app);
    bool moved = redo ? rubraview_edit_steps_redo(&app->edit_steps) : rubraview_edit_steps_undo(&app->edit_steps);
    if (!moved) { osd_say(app, redo ? TU8("nothing to redo") : TU8("nothing to undo")); return; }
    edit_steps_changed(app);
    osd_say(app, redo ? TU8("redone") : TU8("undone"));
}

/* The undo list's words for a step. */
/* RV-137 (VM, 2026-10-18: two rows both "Adjust: whole"): what an adjustment
   moved, its first two values by name ("Saturation -100, Exposure +1.20"). */
static const char *edit_adjust_words(const rubraview_edit_step_t *s, char *buf, size_t cap) {
    const rubraview_edit_params_t *q = &s->values.params;
    struct { const char *name; double v; bool fine; } all[] = {
        { T("Exposure"), q->exposure_ev, true }, { T("Brightness"), q->brightness, false }, { T("Contrast"), q->contrast, false },
        { T("Saturation"), q->saturation, false }, { T("Temperature"), q->temperature, false }, { T("Tint"), q->tint, false },
        { T("Red"), q->balance_r, false }, { T("Green"), q->balance_g, false }, { T("Blue"), q->balance_b, false },
        { T("Black point"), (double)q->black_point, false }, { T("White point"), q->white_point == 255 ? 0.0 : (double)q->white_point, false },
        { T("Midtone"), fabs(q->midtone_gamma - 1.0) < 1e-4 ? 0.0 : q->midtone_gamma, true },
        { T("Blur"), s->values.blur_sigma, true }, { T("Sharpen"), s->values.sharpen_amount, false },
    };
    size_t used = 0, said = 0;
    buf[0] = '\0';
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i) {
        if (fabs(all[i].v) < 1e-4) continue;
        if (said == 2) { snprintf(buf + used, cap - used, ", ..."); break; }
        int n = all[i].fine ? snprintf(buf + used, cap - used, "%s%s %+.2f", said ? ", " : "", all[i].name, all[i].v)
                            : snprintf(buf + used, cap - used, "%s%s %+.0f", said ? ", " : "", all[i].name, all[i].v);
        if (n <= 0 || (size_t)n >= cap - used) break;
        used += (size_t)n;
        said++;
    }
    return said ? buf : T("Adjust");   /* the curve alone, or nothing named */
}

static proven_u8str_view_t edit_step_words(const rubraview_edit_step_t *s, size_t number, char *buffer, size_t size) {
    char moved[80];
    const char *what = s->kind == RUBRAVIEW_STEP_CROP ? T("Crop") : s->kind == RUBRAVIEW_STEP_RESIZE ? T("Resize")
                     : s->kind == RUBRAVIEW_STEP_EFFECT ? effect_word(s->effect)
                     : s->kind == RUBRAVIEW_STEP_PLUGIN ? s->plugin   /* RFC-0007: the plug-in's file */
                     : s->kind == RUBRAVIEW_STEP_GMIC ? gmic_step_name(s)   /* RFC-0009 C: its filter's name, or the command */
                     : s->kind == RUBRAVIEW_STEP_FFMPEG ? s->command   /* RFC-0009 D: the graph */
                     : edit_adjust_words(s, moved, sizeof(moved));
    if (!what) what = T("Adjust");
    const char *where = s->area.shape == RUBRAVIEW_AREA_ELLIPSE ? T("ellipse") : s->area.shape == RUBRAVIEW_AREA_RECT ? T("rectangle") : T("whole");
    int n;
    if (s->kind == RUBRAVIEW_STEP_RESIZE) {
        n = snprintf(buffer, size, "%zu. %s %d x %d", number, what, (int)s->values.resize_width, (int)s->values.resize_height);
    } else if (s->kind == RUBRAVIEW_STEP_CROP || s->area.shape != RUBRAVIEW_AREA_ALL) {
        n = snprintf(buffer, size, "%zu. %s: %s %d,%d %dx%d", number, what, s->kind == RUBRAVIEW_STEP_CROP ? T("rectangle") : where,
                     (int)s->area.rect.x, (int)s->area.rect.y, (int)s->area.rect.width, (int)s->area.rect.height);
    } else {
        n = snprintf(buffer, size, "%zu. %s: %s", number, what, where);
    }
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)buffer, .size = n > 0 ? ((size_t)n < size ? (size_t)n : size - 1) : 0 };
}

/* The undo list: a row for the picture as it was and one for each step; the steps in force marked. */
static void edit_steps_build(app_state_t *app) {
    if (!app->steps_panel.open) return;
    bool placed = app->steps_panel.placed;
    double px = app->steps_panel.place_x, py = app->steps_panel.place_y;
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    app->steps_panel = rubraview_panel_create(TU8("Steps (undo list)"), dpi);
    app->steps_panel.bounds.width = 300.0 * dpi;
    app->steps_panel.placed = placed;
    app->steps_panel.place_x = placed ? px : 12.0 * dpi;
    app->steps_panel.place_y = placed ? py : 120.0 * dpi;
    app->steps_panel.placed = true;
    static char words[RUBRAVIEW_EDIT_MAX_STEPS + 1][96];
    int n0 = snprintf(words[0], sizeof(words[0]), "%s%s", app->edit_steps.cursor == 0 ? "> " : "  ", T("0. The picture as it was"));
    rubraview_panel_add_button(&app->steps_panel, PANEL_STEP_FIRST, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)words[0], .size = n0 > 0 ? (size_t)n0 : 0 });
    for (size_t i = 0; i < app->edit_steps.count; ++i) {
        char line[96];
        proven_u8str_view_t w = edit_step_words(&app->edit_steps.steps[i], i + 1, line, sizeof(line));
        int n = snprintf(words[i + 1], sizeof(words[0]), "%s%.*s", i + 1 == app->edit_steps.cursor ? "> " : i + 1 > app->edit_steps.cursor ? "- " : "  ",
                         (int)w.size, w.ptr);
        rubraview_panel_add_button(&app->steps_panel, PANEL_STEP_FIRST + (int32_t)i + 1, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)words[i + 1], .size = n > 0 ? (size_t)n : 0 });
    }
    app->steps_panel.open = true;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    rubraview_panel_layout(&app->steps_panel, (double)w, (double)h);
}

void edit_steps_toggle(app_state_t *app) {
    app->steps_panel.open = !app->steps_panel.open;
    if (app->steps_panel.open) edit_steps_build(app);
}

/* The selection's numbers, a box of their own: shape, corner, size, feather. */
static void edit_sel_build(app_state_t *app) {
    bool placed = app->sel_panel.placed;
    double px = app->sel_panel.place_x, py = app->sel_panel.place_y;
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    int32_t w = 0, h = 0;
    edit_cur_size(app, &w, &h);
    const rubraview_crop_rect_t *c = &app->edit.crop;
    bool on = app->edit.crop_active;
    app->sel_panel = rubraview_panel_create(TU8("Selection"), dpi);
    app->sel_panel.bounds.width = 300.0 * dpi;
    app->sel_panel.label_width = 90.0 * dpi;
    app->sel_panel.placed = true;
    app->sel_panel.place_x = placed ? px : 12.0 * dpi;
    app->sel_panel.place_y = placed ? py : 360.0 * dpi;
    rubraview_panel_add_choice(&app->sel_panel, PANEL_SEL_SHAPE, TU8("Shape"), app->sel_shape == RUBRAVIEW_AREA_ELLIPSE ? 1 : 0, 2);
    rubraview_panel_add_slider(&app->sel_panel, PANEL_SEL_X, PROVEN_LIT("X"), on ? c->x : 0, 0.0, w > 1 ? w - 1 : 1, 1.0);
    rubraview_panel_add_slider(&app->sel_panel, PANEL_SEL_Y, PROVEN_LIT("Y"), on ? c->y : 0, 0.0, h > 1 ? h - 1 : 1, 1.0);
    rubraview_panel_add_slider(&app->sel_panel, PANEL_SEL_W, TU8("Width"), on ? c->width : w, 1.0, w > 1 ? w : 2, 1.0);
    rubraview_panel_add_slider(&app->sel_panel, PANEL_SEL_H, TU8("Height"), on ? c->height : h, 1.0, h > 1 ? h : 2, 1.0);
    rubraview_panel_add_slider(&app->sel_panel, PANEL_SEL_FEATHER, TU8("Soft edge"), app->sel_feather, 0.0, 200.0, 1.0);
    rubraview_panel_add_button(&app->sel_panel, PANEL_SEL_TYPE, TU8("Type the four numbers..."));
    rubraview_panel_add_button(&app->sel_panel, PANEL_SEL_ALL, TU8("Select none (the whole picture)"));
    app->sel_panel.open = true;
    int32_t ww = 0, wh = 0;
    rubraview_pal_window_get_size(app->window, &ww, &wh);
    rubraview_panel_layout(&app->sel_panel, (double)ww, (double)wh);
}

/* The numbers follow the selection drawn or moved on the picture. */
void edit_sel_sync(app_state_t *app) {
    if (!app->edit_live_busy) edit_live_close(app);   /* another selection: the next change is a step of its own */
    if (!app->sel_panel.open) return;
    edit_sel_build(app);
}

/* A number of the box moved: the selection follows at once. */
void edit_sel_apply_row(app_state_t *app, int32_t index) {
    if (index < 0 || (size_t)index >= app->sel_panel.row_count) return;
    const rubraview_panel_row_t *row = &app->sel_panel.rows[index];
    int32_t w = 0, h = 0;
    edit_cur_size(app, &w, &h);
    if (!app->edit.crop_active) app->edit.crop = (rubraview_crop_rect_t){ 0, 0, w, h };
    rubraview_crop_rect_t c = app->edit.crop;
    int32_t v = (int32_t)lround(row->value);
    switch (row->id) {
        case PANEL_SEL_SHAPE: app->sel_shape = v == 1 ? RUBRAVIEW_AREA_ELLIPSE : RUBRAVIEW_AREA_RECT; break;
        case PANEL_SEL_FEATHER: app->sel_feather = v; break;
        case PANEL_SEL_X: c.x = v; break;
        case PANEL_SEL_Y: c.y = v; break;
        case PANEL_SEL_W: c.width = v; break;
        case PANEL_SEL_H: c.height = v; break;
        default: return;
    }
    if (c.x + c.width > w) c.width = w - c.x;
    if (c.y + c.height > h) c.height = h - c.y;
    if (c.width >= 1 && c.height >= 1 && rubraview_edit_crop_set(&app->edit, c.x, c.y, c.width, c.height)) app->edit.crop_active = true;
    app->edit_rev++;
    app->edit_shown_valid = false;
    if (row->id == PANEL_SEL_X || row->id == PANEL_SEL_Y || row->id == PANEL_SEL_W || row->id == PANEL_SEL_H) {
        /* The others' ranges follow the corner and the size. */
        rubraview_panel_set_value(&app->sel_panel, PANEL_SEL_W, app->edit.crop.width);
        rubraview_panel_set_value(&app->sel_panel, PANEL_SEL_H, app->edit.crop.height);
    }
}

void edit_sel_toggle(app_state_t *app) {
    if (app->sel_panel.open) { app->sel_panel.open = false; return; }
    edit_sel_build(app);
}

static proven_u8str_view_t edit_sel_label(const app_state_t *app) {
    static char line[96];
    if (!app->edit.crop_active) return TU8("Selection: none (the whole picture)...");
    const rubraview_crop_rect_t *c = &app->edit.crop;
    int n = snprintf(line, sizeof(line), T("Selection: %s %d,%d %dx%d..."), app->sel_shape == RUBRAVIEW_AREA_ELLIPSE ? T("ellipse") : T("rectangle"),
                     (int)c->x, (int)c->y, (int)c->width, (int)c->height);
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = n > 0 ? (size_t)n : 0 };
}

/* Edit's rows by section, in whichever order the panel wants them (RV-137). */
static void edit_rows_tone(app_state_t *app) {
    const rubraview_edit_session_t *e = &app->edit;
    rubraview_panel_add_slider(&app->panel, PANEL_EXPOSURE, TU8("Exposure"), e->params.exposure_ev, -3.0, 3.0, 0.0);
    rubraview_panel_add_slider(&app->panel, PANEL_BRIGHTNESS, TU8("Brightness"), e->params.brightness, -100.0, 100.0, 0.0);
    rubraview_panel_add_slider(&app->panel, PANEL_CONTRAST, TU8("Contrast"), e->params.contrast, -100.0, 100.0, 0.0);
    rubraview_panel_add_slider(&app->panel, PANEL_SATURATION, TU8("Saturation"), e->params.saturation, -100.0, 100.0, 0.0);
    rubraview_panel_add_slider(&app->panel, PANEL_TEMPERATURE, TU8("Temperature"), e->params.temperature, -100.0, 100.0, 0.0);
    rubraview_panel_add_slider(&app->panel, PANEL_TINT, TU8("Tint"), e->params.tint, -100.0, 100.0, 0.0);
    rubraview_panel_add_slider(&app->panel, PANEL_BAL_R, TU8("Red"), e->params.balance_r, -100.0, 100.0, 1.0);
    rubraview_panel_add_slider(&app->panel, PANEL_BAL_G, TU8("Green"), e->params.balance_g, -100.0, 100.0, 1.0);
    rubraview_panel_add_slider(&app->panel, PANEL_BAL_B, TU8("Blue"), e->params.balance_b, -100.0, 100.0, 1.0);
    rubraview_panel_add_button(&app->panel, PANEL_AUTO_COLOUR, TU8("Automatic colour correction"));
}

static void edit_rows_levels(app_state_t *app) {
    const rubraview_edit_session_t *e = &app->edit;
    rubraview_panel_add_slider(&app->panel, PANEL_BLACK, TU8("Black point"), (double)e->params.black_point, 0.0, 254.0, 1.0);
    rubraview_panel_add_slider(&app->panel, PANEL_WHITE, TU8("White point"), (double)e->params.white_point, 1.0, 255.0, 1.0);
    rubraview_panel_add_slider(&app->panel, PANEL_GAMMA, TU8("Midtone"), e->params.midtone_gamma, 0.1, 3.0, 0.0);
}

static void edit_rows_detail(app_state_t *app) {
    const rubraview_edit_session_t *e = &app->edit;
    rubraview_panel_add_slider(&app->panel, PANEL_BLUR, TU8("Blur"), e->blur_sigma, 0.0, 50.0, 0.0);
    rubraview_panel_add_choice(&app->panel, PANEL_BLUR_KIND, TU8("Blur kind"), e->blur_box ? 1 : 0, 2);
    rubraview_panel_add_slider(&app->panel, PANEL_SHARPEN, TU8("Sharpen"), e->sharpen_amount, 0.0, 300.0, 0.0);
    rubraview_panel_add_choice(&app->panel, PANEL_SHARPEN_KIND, TU8("Sharpen kind"), e->sharpen_laplacian ? 1 : 0, 2);
    rubraview_panel_add_slider(&app->panel, PANEL_SHARPEN_RADIUS, PROVEN_LIT("  radius"), e->sharpen_radius, 0.5, 10.0, 0.0);
    rubraview_panel_add_slider(&app->panel, PANEL_SHARPEN_THRESHOLD, PROVEN_LIT("  threshold"), (double)e->sharpen_threshold, 0.0, 255.0, 1.0);
    /* Changes are on the picture at once (owner, 2026-10-18): this only ends the step, so the next change is one of its own. */
    rubraview_panel_add_button(&app->panel, PANEL_APPLY_SEL, e->crop_active ? TU8("Done on the selection: next change a new step (Enter)")
                                                                            : TU8("Done: next change a new step (Enter)"));
}

static rubraview_pixbuf_t gmic_runner(proven_arena_t *arena, const rubraview_edit_step_t *step, const rubraview_pixbuf_t *src);
static void edit_rows_gmic(app_state_t *app);
static rubraview_pixbuf_t ffmpeg_runner(proven_arena_t *arena, const rubraview_edit_step_t *step, const rubraview_pixbuf_t *src);
static void edit_rows_ffmpeg(app_state_t *app);

/* ---- RFC-0007 (RV-138, D-186): plug-in filters, found in their folder and run in a child process ---- */

rubraview_plugin_t g_plugins[RUBRAVIEW_PLUGIN_MAX];
size_t g_plugin_count;
char g_plugin_folder[520];
static SRWLOCK g_plugin_lock = SRWLOCK_INIT;
_Thread_local const char *g_plugin_why;   /* the last run that failed on this thread: why */

/* What a plug-in made, kept: drawing the preview again does not start it again. */
typedef struct plugin_result {
    char file[RUBRAVIEW_PLUGIN_FILE_BYTES];
    uint8_t sha256[32];
    double params[RUBRAVIEW_PLUGIN_PARAMS];
    int32_t width, height;
    uint64_t sum;
    uint8_t *bgra;
} plugin_result_t;
static plugin_result_t g_plugin_results[4];
static size_t g_plugin_result_next;
static SRWLOCK g_plugin_results_lock = SRWLOCK_INIT;

void plugins_look(proven_u8str_view_t folder) {
    AcquireSRWLockExclusive(&g_plugin_lock);
    size_t n = folder.size < sizeof(g_plugin_folder) ? folder.size : 0;
    memcpy(g_plugin_folder, folder.ptr, n);
    g_plugin_folder[n] = '\0';
    g_plugin_count = n ? rubraview_pal_plugins_find(cstr(g_plugin_folder), g_plugins, RUBRAVIEW_PLUGIN_MAX) : 0;
    ReleaseSRWLockExclusive(&g_plugin_lock);
}

/* Settings › Display › Plug-ins: on, and the folder (the default one when none is named). */
void plugins_from_settings(app_state_t *app) {
    static int was_on = -1;
    static char was_folder[520];
    bool on = rubraview_settings_get(&app->settings, PROVEN_LIT("edit"), PROVEN_LIT("plugins")) > 0.5;
    proven_u8str_view_t folder = rubraview_settings_get_text(&app->settings, PROVEN_LIT("edit"), PROVEN_LIT("plugin_folder"));
    char chosen[520] = "";
    if (folder.size > 0 && folder.size < sizeof(chosen)) { memcpy(chosen, folder.ptr, folder.size); chosen[folder.size] = '\0'; }
    else if (on) (void)rubraview_pal_plugins_default_folder(chosen, sizeof(chosen), true);
    if (was_on == (int)on && strcmp(was_folder, chosen) == 0) return;
    was_on = on;
    snprintf(was_folder, sizeof(was_folder), "%s", chosen);
    plugins_look(on ? cstr(chosen) : PROVEN_LIT(""));
    app->plugin_choice = 0;
}

size_t plugins_usable(const rubraview_plugin_t **out, size_t capacity) {
    size_t n = 0;
    for (size_t i = 0; i < g_plugin_count && n < capacity; ++i) if (g_plugins[i].usable) out[n++] = &g_plugins[i];
    return n;
}

static uint64_t bytes_sum(const uint8_t *p, size_t n) {   /* FNV-1a: the same picture or not */
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

/* Esc while a plug-in works: cancel. */
static bool plugin_tick(void *context, double progress) {
    (void)context; (void)progress;
    return (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
}

rubraview_pixbuf_t plugin_runner(void *user, proven_arena_t *arena, const rubraview_edit_step_t *step, const rubraview_pixbuf_t *src) {
    (void)user;
    rubraview_pixbuf_t empty = {0};
    if (step->kind == RUBRAVIEW_STEP_GMIC) return rubraview_pixbuf_is_valid(src) ? gmic_runner(arena, step, src) : empty;   /* RFC-0009 C */
    if (step->kind == RUBRAVIEW_STEP_FFMPEG) return rubraview_pixbuf_is_valid(src) ? ffmpeg_runner(arena, step, src) : empty;   /* RFC-0009 D */
    g_plugin_why = NULL;
    if (!rubraview_pixbuf_is_valid(src) || (src->format != RUBRAVIEW_PIXFMT_RGBA8 && src->format != RUBRAVIEW_PIXFMT_BGRA8)) return empty;
    rubraview_plugin_t plugin;
    bool found = false;
    AcquireSRWLockShared(&g_plugin_lock);
    for (size_t i = 0; i < g_plugin_count && !found; ++i) {
        if (rubraview_plugin_file_matches(g_plugins[i].file, cstr(step->plugin))) { plugin = g_plugins[i]; found = true; }
    }
    ReleaseSRWLockShared(&g_plugin_lock);
    if (!found) { g_plugin_why = N_("the plug-in is not in the plug-in folder, or plug-ins are off"); return empty; }
    if (!plugin.usable) { g_plugin_why = plugin.why; return empty; }
    static const uint8_t NONE[32] = { 0 };
    if (memcmp(step->plugin_sha256, NONE, 32) != 0 && memcmp(step->plugin_sha256, plugin.sha256, 32) != 0) {
        g_plugin_why = N_("the plug-in has changed since this step was made");
        return empty;
    }
    double values[RUBRAVIEW_PLUGIN_PARAMS];
    memcpy(values, step->plugin_params, sizeof(values));
    rubraview_plugin_params_clamp(&plugin, values, step->plugin_param_count);
    size_t row = (size_t)src->width * 4u, bytes = row * (size_t)src->height;
    uint8_t *in = (uint8_t*)malloc(bytes), *out = NULL;
    if (!in) { g_plugin_why = N_("out of memory"); return empty; }
    bool rgba = src->format == RUBRAVIEW_PIXFMT_RGBA8;
    for (int32_t y = 0; y < src->height; ++y) {
        const uint8_t *s = src->pixels + (size_t)y * (size_t)src->stride;
        uint8_t *d = in + (size_t)y * row;
        if (!rgba) { memcpy(d, s, row); continue; }
        for (int32_t x = 0; x < src->width; ++x) { d[x * 4] = s[x * 4 + 2]; d[x * 4 + 1] = s[x * 4 + 1]; d[x * 4 + 2] = s[x * 4]; d[x * 4 + 3] = s[x * 4 + 3]; }
    }
    uint64_t sum = bytes_sum(in, bytes);
    AcquireSRWLockShared(&g_plugin_results_lock);
    for (size_t i = 0; i < 4 && !out; ++i) {
        const plugin_result_t *r = &g_plugin_results[i];
        if (r->bgra && r->sum == sum && r->width == src->width && r->height == src->height && strcmp(r->file, plugin.file) == 0 &&
            memcmp(r->sha256, plugin.sha256, 32) == 0 && memcmp(r->params, values, sizeof(values)) == 0 && (out = (uint8_t*)malloc(bytes)) != NULL)
            memcpy(out, r->bgra, bytes);
    }
    ReleaseSRWLockShared(&g_plugin_results_lock);
    if (!out) {
        out = (uint8_t*)malloc(bytes);
        if (!out || !rubraview_pal_plugin_run(&plugin, values, in, src->width, src->height, out, plugin_tick, NULL, &g_plugin_why)) {
            if (!g_plugin_why) g_plugin_why = N_("out of memory");
            free(in);
            free(out);
            return empty;
        }
        uint8_t *kept = (uint8_t*)malloc(bytes);
        if (kept) {
            memcpy(kept, out, bytes);
            AcquireSRWLockExclusive(&g_plugin_results_lock);
            plugin_result_t *r = &g_plugin_results[g_plugin_result_next++ % 4];
            free(r->bgra);
            *r = (plugin_result_t){ .width = src->width, .height = src->height, .sum = sum, .bgra = kept };
            snprintf(r->file, sizeof(r->file), "%s", plugin.file);
            memcpy(r->sha256, plugin.sha256, 32);
            memcpy(r->params, values, sizeof(values));
            ReleaseSRWLockExclusive(&g_plugin_results_lock);
        }
    }
    free(in);
    rubraview_pixbuf_t made = rubraview_pixbuf_create(arena, src->width, src->height, src->format);
    if (rubraview_pixbuf_is_valid(&made)) {
        for (int32_t y = 0; y < made.height; ++y) {
            const uint8_t *s = out + (size_t)y * row;
            uint8_t *d = made.pixels + (size_t)y * (size_t)made.stride;
            if (!rgba) { memcpy(d, s, row); continue; }
            for (int32_t x = 0; x < made.width; ++x) { d[x * 4] = s[x * 4 + 2]; d[x * 4 + 1] = s[x * 4 + 1]; d[x * 4 + 2] = s[x * 4]; d[x * 4 + 3] = s[x * 4 + 3]; }
        }
    }
    free(out);
    return made;
}

static void edit_rows_plugins(app_state_t *app);

static void edit_rows_effect(app_state_t *app) {
    /* By kind (owner, 2026-10-18): the kind, then its effects; 0 is none. */
    if (app->effect_choice != RUBRAVIEW_EFFECT_NONE) app->effect_group = (int32_t)rubraview_effect_group_of((rubraview_effect_t)app->effect_choice);
    rubraview_effect_t members[RUBRAVIEW_EFFECT_COUNT];
    size_t n = rubraview_effect_group_members((rubraview_effect_group_t)app->effect_group, members, RUBRAVIEW_EFFECT_COUNT);
    int32_t at = 0;
    for (size_t i = 0; i < n; ++i) if ((int32_t)members[i] == app->effect_choice) at = (int32_t)i + 1;
    rubraview_panel_add_choice(&app->panel, PANEL_EFFECT_GROUP, TU8("Effects"), app->effect_group, RUBRAVIEW_EFFECT_GROUPS);
    rubraview_panel_add_choice(&app->panel, PANEL_EFFECT, TU8("Effect"), at, (int32_t)n + 1);
    {
        double lo = 0.0, hi = 1.0;
        rubraview_effect_range((rubraview_effect_t)app->effect_choice, &lo, &hi, NULL);
        rubraview_panel_add_slider(&app->panel, PANEL_EFFECT_AMOUNT, TU8("  amount"), app->effect_amount, lo, hi, 1.0);
    }
    rubraview_panel_add_button(&app->panel, PANEL_EFFECT_APPLY, TU8("Keep this effect; the next is added on top"));
    edit_rows_plugins(app);   /* RFC-0007 */
    edit_rows_gmic(app);      /* RFC-0009 C */
    edit_rows_ffmpeg(app);    /* RFC-0009 D */
    rubraview_panel_add_toggle(&app->panel, PANEL_COMPARE, TU8("Show the original"), app->edit_compare);
}

/* RFC-0007: the plug-ins found, the one chosen's parameters, and a button that makes it a step. */
static void edit_rows_plugins(app_state_t *app) {
    const rubraview_plugin_t *list[RUBRAVIEW_PLUGIN_MAX];
    size_t n = plugins_usable(list, RUBRAVIEW_PLUGIN_MAX);
    if (n == 0) return;
    if (app->plugin_choice < 0 || (size_t)app->plugin_choice > n) app->plugin_choice = 0;
    rubraview_panel_add_choice(&app->panel, PANEL_PLUGIN, TU8("Plug-in"), app->plugin_choice, (int32_t)n + 1);
    if (app->plugin_choice == 0) return;
    const rubraview_plugin_t *p = list[app->plugin_choice - 1];
    for (int32_t i = 0; i < p->param_count; ++i) {
        const rubraview_plugin_param_t *q = &p->params[i];
        int32_t id = PANEL_PLUGIN_P0 + i;
        static char labels[RUBRAVIEW_PLUGIN_PARAMS][80];   /* the rows' words live as long as the panel */
        snprintf(labels[i], sizeof(labels[i]), "  %s", q->name);
        proven_u8str_view_t name = cstr(labels[i]);
        if (q->kind == RVF_BOOL) rubraview_panel_add_toggle(&app->panel, id, name, app->plugin_values[i] > 0.5);
        else if (q->kind == RVF_CHOICE) rubraview_panel_add_choice(&app->panel, id, name, (int32_t)app->plugin_values[i], (int32_t)q->max + 1);
        else rubraview_panel_add_slider(&app->panel, id, name, app->plugin_values[i], q->min, q->max, q->kind == RVF_INT ? 1.0 : q->step);
    }
    rubraview_panel_add_button(&app->panel, PANEL_PLUGIN_APPLY, TU8("Run the plug-in: a step (Esc cancels)"));
}

/* The chosen plug-in, run on the picture as a step of its own. */
void edit_plugin_apply(app_state_t *app) {
    const rubraview_plugin_t *list[RUBRAVIEW_PLUGIN_MAX];
    size_t n = plugins_usable(list, RUBRAVIEW_PLUGIN_MAX);
    if (app->plugin_choice <= 0 || (size_t)app->plugin_choice > n) { osd_say(app, TU8("choose a plug-in first")); return; }
    const rubraview_plugin_t *p = list[app->plugin_choice - 1];
    (void)edit_apply_pending(app, false);
    rubraview_edit_step_t s = { .kind = RUBRAVIEW_STEP_PLUGIN, .area = edit_area_now(app), .plugin_param_count = p->param_count };
    snprintf(s.plugin, sizeof(s.plugin), "%s", p->file);
    memcpy(s.plugin_sha256, p->sha256, 32);
    memcpy(s.plugin_params, app->plugin_values, sizeof(s.plugin_params));
    rubraview_plugin_params_clamp(p, s.plugin_params, p->param_count);
    if (!rubraview_edit_steps_push(&app->edit_steps, &s)) return;
    edit_steps_changed(app);
    osd_say(app, TU8("plug-in applied (Ctrl+Z undoes it)"));
}

/* ---- RFC-0009 C: G'MIC, run by the user's own gmic.exe (a program of its own: D-189's line) ---- */


char g_gmic_exe[520];                 /* the program found where Settings › External names it; "" none */
char g_gmic_where[520];               /* what was named, as it was read */
static void *g_gmic_mem;                     /* the filter list's arena */
static proven_arena_t g_gmic_arena;
rubraview_gmic_catalog_t g_gmic;      /* the filter list; empty until it is made and read */
const char *g_gmic_list_why;          /* why there is none (an N_ literal), or NULL */
static _Thread_local char g_gmic_said[512];  /* what G'MIC said when a run failed */

/* The filter list as G'MIC wrote it, kept beside the settings. */
static proven_u8str_view_t gmic_list_path(app_state_t *app) {
    return rubraview_path_join(app->arena, rubraview_path_dirname(app->settings_path), PROVEN_LIT("gmic-filters.json"));
}

/* The list read from where it is kept; false (and why) when it is not there or not G'MIC's. */
static bool gmic_list_load(app_state_t *app) {
    proven_u8str_view_t path = gmic_list_path(app);
    free(g_gmic_mem);
    g_gmic_mem = NULL;
    g_gmic = (rubraview_gmic_catalog_t){0};
    g_gmic_list_why = N_("no filter list yet: press Make the filter list");
    rubraview_fs_entry_t entry;
    if (!rubraview_pal_fs_stat(app->arena, path, &entry) || entry.size_bytes == 0) return false;
    if (entry.size_bytes > 64u << 20) { g_gmic_list_why = N_("the filter list is too large"); return false; }
    size_t room = (size_t)entry.size_bytes * 3u + (8u << 20);   /* the text, and what is read out of it */
    g_gmic_mem = malloc(room);
    if (!g_gmic_mem) { g_gmic_list_why = N_("out of memory"); return false; }
    g_gmic_arena = proven_arena_create((proven_mem_mut_t){ .ptr = g_gmic_mem, .size = room });
    proven_u8str_view_t text = rubraview_pal_fs_read_file(&g_gmic_arena, path, (size_t)entry.size_bytes);
    const char *why = NULL;
    g_gmic = rubraview_gmic_catalog_parse(&g_gmic_arena, text, &why);
    if (g_gmic.filter_count == 0) {
        free(g_gmic_mem);
        g_gmic_mem = NULL;
        g_gmic = (rubraview_gmic_catalog_t){0};
        g_gmic_list_why = why ? why : N_("the filter list names no filter");
        return false;
    }
    g_gmic_list_why = NULL;
    return true;
}

/* Settings › External › G'MIC: the program looked for again when another place is named. */
void gmic_from_settings(app_state_t *app) {
    proven_u8str_view_t where = rubraview_settings_get_text(&app->settings, PROVEN_LIT("gmic"), PROVEN_LIT("path"));
    char named[520] = "";
    if (where.size < sizeof(named)) { memcpy(named, where.ptr, where.size); named[where.size] = '\0'; }
    static bool looked;
    if (looked && strcmp(named, g_gmic_where) == 0) return;
    looked = true;
    snprintf(g_gmic_where, sizeof(g_gmic_where), "%s", named);
    proven_u8str_view_t exe = rubraview_pal_gmic_find(app->arena, cstr(named));
    snprintf(g_gmic_exe, sizeof(g_gmic_exe), "%.*s", (int)exe.size, (const char *)exe.ptr);
    if (g_gmic_exe[0] && g_gmic.filter_count == 0) (void)gmic_list_load(app);
    app->gmic_category = 0;
}

/* ---- the filter list made, off the main thread: `gmic up` (G'MIC fetches its filters), then the list ---- */

gmic_list_job_t *g_gmic_job;

static DWORD WINAPI gmic_list_run(LPVOID arg) {
    gmic_list_job_t *job = (gmic_list_job_t *)arg;
    proven_u8str_view_t exe = cstr(job->exe);
    const proven_u8str_view_t up[] = { PROVEN_LIT("up") };
    rubraview_gmic_exec_t r = rubraview_pal_gmic_exec(exe, up, 1, 5u * 60u * 1000u, NULL, NULL);
    if (r.ok) {
        const proven_u8str_view_t list[] = { PROVEN_LIT("-v"), PROVEN_LIT("-1"), PROVEN_LIT("parse_gui"), PROVEN_LIT("json"),
                                             PROVEN_LIT("ot"), cstr(job->out) };
        r = rubraview_pal_gmic_exec(exe, list, 6, 15u * 60u * 1000u, NULL, NULL);
    }
    job->ok = r.ok;
    snprintf(job->said, sizeof(job->said), "%s", r.said);
    InterlockedExchange(&job->done, 1);
    rubraview_pal_window_wake(job->wake);
    return 0;
}

void gmic_list_make(app_state_t *app) {
    if (g_gmic_job) { settings_say(app, T("the filter list is being made")); return; }
    if (!g_gmic_exe[0]) { settings_say(app, T("name gmic.exe or its folder first")); return; }
    gmic_list_job_t *job = (gmic_list_job_t *)calloc(1, sizeof(*job));
    proven_u8str_view_t out = rubraview_pal_gmic_temp(app->arena, 0, true);
    if (!job || out.size < 4 || out.size >= sizeof(job->out)) { free(job); settings_say(app, T("the filter list could not be made")); return; }
    snprintf(job->exe, sizeof(job->exe), "%s", g_gmic_exe);
    snprintf(job->out, sizeof(job->out), "%.*s.json", (int)(out.size - 4), (const char *)out.ptr);   /* ...-out.png -> ...-out.json */
    job->wake = app->window;
    job->thread = CreateThread(NULL, 0, gmic_list_run, job, 0, NULL);
    if (!job->thread) { free(job); settings_say(app, T("the filter list could not be made")); return; }
    g_gmic_job = job;
    settings_say(app, T("G'MIC is fetching its filters and listing them: a few minutes"));
}

/* Once a pass: the list made is moved beside the settings and read. */
void gmic_list_poll(app_state_t *app) {
    gmic_list_job_t *job = g_gmic_job;
    if (!job || InterlockedCompareExchange(&job->done, 1, 1) != 1) return;
    g_gmic_job = NULL;
    WaitForSingleObject(job->thread, INFINITE);
    CloseHandle(job->thread);
    proven_u8str_view_t made = cstr(job->out), kept = gmic_list_path(app);
    bool ok = job->ok && rubraview_pal_fs_exists(made);
    if (ok) {
        (void)rubraview_pal_fs_delete(kept);
        ok = rubraview_pal_fs_move(made, kept) && gmic_list_load(app);
    }
    char line[240];   /* the settings window's message line */
    char said[160];
    snprintf(said, sizeof(said), "%s", job->said[0] ? job->said + (strlen(job->said) > 150 ? strlen(job->said) - 150 : 0) : T("G'MIC said nothing"));
    if (ok) snprintf(line, sizeof(line), T("G'MIC's filter list: %d filters (Edit > G'MIC)"), g_gmic.filter_count);
    else snprintf(line, sizeof(line), T("the filter list could not be made: %s"), said);
    (void)rubraview_pal_fs_delete(made);
    free(job);
    osd_say(app, cstr(line));
    if (app->settings_window) settings_say(app, line);
}

/* ---- the Edit window's rows ---- */

/* The `k`th filter of category `category` (from 1); NULL past its end. */
static const rubraview_gmic_filter_t *gmic_filter_in(int32_t category, int32_t k, int32_t *count) {
    const rubraview_gmic_filter_t *found = NULL;
    int32_t n = 0;
    for (int32_t i = 0; i < g_gmic.filter_count; ++i) {
        if (g_gmic.filters[i].category != category - 1) continue;
        if (n == k) found = &g_gmic.filters[i];
        n++;
    }
    if (count) *count = n;
    return found;
}

static const rubraview_gmic_filter_t *gmic_chosen(const app_state_t *app) {
    return app->gmic_category > 0 ? gmic_filter_in(app->gmic_category, app->gmic_filter, NULL) : NULL;
}

/* The parameters shown, in their order: the index of the `row`th one that is not hidden; -1 past them. */
static int32_t gmic_param_of_row(const rubraview_gmic_filter_t *f, int32_t row) {
    for (int32_t i = 0, n = 0; f && i < f->param_count && i < RUBRAVIEW_GMIC_MAX_SHOWN; ++i) {
        if (f->params[i].hidden) continue;
        if (n++ == row) return i;
    }
    return -1;
}

static void gmic_defaults(app_state_t *app) {
    const rubraview_gmic_filter_t *f = gmic_chosen(app);
    for (int32_t i = 0; f && i < f->param_count && i < RUBRAVIEW_GMIC_MAX_SHOWN; ++i) {
        app->gmic_values[i] = f->params[i].value;
        snprintf(app->gmic_texts[i], sizeof(app->gmic_texts[i]), "%.*s", (int)f->params[i].text.size, (const char *)f->params[i].text.ptr);
    }
}

static bool gmic_is_typed(rubraview_gmic_kind_t kind) {
    return kind == RUBRAVIEW_GMIC_TEXT || kind == RUBRAVIEW_GMIC_FILE || kind == RUBRAVIEW_GMIC_FOLDER ||
           kind == RUBRAVIEW_GMIC_COLOR || kind == RUBRAVIEW_GMIC_POINT;
}

static void edit_rows_gmic(app_state_t *app) {
    if (!g_gmic_exe[0]) return;
    if (g_gmic.filter_count > 0) {
        if (app->gmic_category < 0 || app->gmic_category > g_gmic.category_count) app->gmic_category = 0;
        rubraview_panel_add_choice(&app->panel, PANEL_GMIC_CATEGORY, TU8("G'MIC"), app->gmic_category, g_gmic.category_count + 1);
        if (app->gmic_category > 0) {
            int32_t count = 0;
            (void)gmic_filter_in(app->gmic_category, 0, &count);
            if (app->gmic_filter < 0 || app->gmic_filter >= count) app->gmic_filter = 0;
            rubraview_panel_add_choice(&app->panel, PANEL_GMIC_FILTER, TU8("  filter"), app->gmic_filter, count);
            const rubraview_gmic_filter_t *f = gmic_chosen(app);
            static char labels[PANEL_GMIC_ROWS][120];   /* the rows' words live as long as the panel */
            for (int32_t row = 0; row < PANEL_GMIC_ROWS; ++row) {
                int32_t i = gmic_param_of_row(f, row);
                if (i < 0) break;
                const rubraview_gmic_param_t *q = &f->params[i];
                int32_t id = PANEL_GMIC_P0 + row;
                if (gmic_is_typed(q->kind)) {
                    snprintf(labels[row], sizeof(labels[row]), "  %.*s: %s", (int)q->name.size, (const char *)q->name.ptr, app->gmic_texts[i]);
                    rubraview_panel_add_button(&app->panel, id, cstr(labels[row]));
                    continue;
                }
                snprintf(labels[row], sizeof(labels[row]), "  %.*s", (int)q->name.size, (const char *)q->name.ptr);
                proven_u8str_view_t name = cstr(labels[row]);
                if (q->kind == RUBRAVIEW_GMIC_BOOL) rubraview_panel_add_toggle(&app->panel, id, name, app->gmic_values[i] > 0.5);
                else if (q->kind == RUBRAVIEW_GMIC_CHOICE) rubraview_panel_add_choice(&app->panel, id, name, (int32_t)app->gmic_values[i], q->choice_count);
                else if (q->kind == RUBRAVIEW_GMIC_INT || q->kind == RUBRAVIEW_GMIC_FLOAT)
                    rubraview_panel_add_slider(&app->panel, id, name, app->gmic_values[i], q->min, q->max,
                                               q->kind == RUBRAVIEW_GMIC_INT ? 1.0 : (q->max - q->min) / 100.0);
            }
            rubraview_panel_add_button(&app->panel, PANEL_GMIC_APPLY, TU8("Run G'MIC: a step (Esc cancels)"));
        }
    }
    rubraview_panel_add_button(&app->panel, PANEL_GMIC_TYPE, TU8("Type a G'MIC command..."));
}

/* A choice row's words: the categories, the filters of the one chosen, a parameter's choices. */
static proven_u8str_view_t gmic_choice_word(const app_state_t *app, int32_t id, int32_t i, char *buffer, size_t size) {
    proven_u8str_view_t w = PROVEN_LIT("");
    if (id == PANEL_GMIC_CATEGORY) {
        if (i <= 0) return cstr(T("none"));
        if (i <= g_gmic.category_count) w = g_gmic.categories[i - 1];
    } else if (id == PANEL_GMIC_FILTER) {
        const rubraview_gmic_filter_t *f = gmic_filter_in(app->gmic_category, i, NULL);
        if (f) w = f->name;
    } else {
        const rubraview_gmic_filter_t *f = gmic_chosen(app);
        int32_t p = gmic_param_of_row(f, id - PANEL_GMIC_P0);
        if (p >= 0 && f->params[p].kind == RUBRAVIEW_GMIC_CHOICE && i >= 0 && i < f->params[p].choice_count) w = f->params[p].choices[i];
    }
    snprintf(buffer, size, "%.*s", (int)w.size, (const char *)w.ptr);
    return cstr(buffer);
}

/* A row's value changed; true when it was one of G'MIC's. */
bool gmic_row_changed(app_state_t *app, int32_t id, double v) {
    if (id == PANEL_GMIC_CATEGORY || id == PANEL_GMIC_FILTER) {
        if (id == PANEL_GMIC_CATEGORY) { app->gmic_category = (int32_t)lround(v); app->gmic_filter = 0; }
        else app->gmic_filter = (int32_t)lround(v);
        gmic_defaults(app);
        panel_build_edit(app);
        return true;
    }
    if (id < PANEL_GMIC_P0 || id >= PANEL_GMIC_P0 + PANEL_GMIC_ROWS) return false;
    int32_t p = gmic_param_of_row(gmic_chosen(app), id - PANEL_GMIC_P0);
    if (p >= 0) app->gmic_values[p] = v;
    return true;
}

/* The box for a parameter's text (its index), or for a whole command (-1). */
static void gmic_type_begin(app_state_t *app, int32_t param) {
    if (app->rename_active) rename_end(app);
    const char *now = param >= 0 ? app->gmic_texts[param] : app->gmic_last_typed;
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    rubraview_textedit_set(&app->rename_edit, cstr(now));
    rubraview_textedit_select_all(&app->rename_edit);
    app->rename_active = true;
    app->rename_is_gmic = true;
    app->gmic_typing = param;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, true);
    osd_say(app, param >= 0 ? TU8("type it, then Enter (Esc leaves it)")
                            : TU8("a G'MIC command, run on the picture with your rights (Enter runs it, Esc leaves it)"));
}

/* A button of G'MIC's pressed; true when it was one. */
bool gmic_button(app_state_t *app, int32_t id) {
    if (id == PANEL_GMIC_TYPE) { gmic_type_begin(app, -1); return true; }
    if (id >= PANEL_GMIC_P0 && id < PANEL_GMIC_P0 + PANEL_GMIC_ROWS) {
        int32_t p = gmic_param_of_row(gmic_chosen(app), id - PANEL_GMIC_P0);
        if (p >= 0) gmic_type_begin(app, p);
        return true;
    }
    if (id != PANEL_GMIC_APPLY) return false;
    const rubraview_gmic_filter_t *f = gmic_chosen(app);
    if (!f) { osd_say(app, TU8("choose a G'MIC filter first")); return true; }
    proven_u8str_view_t texts[RUBRAVIEW_GMIC_MAX_SHOWN];
    for (int32_t i = 0; i < RUBRAVIEW_GMIC_MAX_SHOWN; ++i) texts[i] = cstr(app->gmic_texts[i]);
    char args[RUBRAVIEW_GMIC_ARGS_MAX];
    size_t n = rubraview_gmic_args(f, app->gmic_values, texts, args, sizeof(args));
    rubraview_edit_step_t s = { .kind = RUBRAVIEW_STEP_GMIC, .area = edit_area_now(app) };
    int w = snprintf(s.command, sizeof(s.command), "%.*s%s%s", (int)f->command.size, (const char *)f->command.ptr, n ? " " : "", args);
    if (w <= 0 || (size_t)w >= sizeof(s.command)) { osd_say(app, TU8("this filter's settings are too long for a step")); return true; }
    (void)edit_apply_pending(app, false);
    if (!rubraview_edit_steps_push(&app->edit_steps, &s)) return true;
    edit_steps_changed(app);
    osd_say(app, TU8("G'MIC applied (Ctrl+Z undoes it)"));
    return true;
}

/* Enter in the box: a parameter's text kept, or the command typed made a step. */
void gmic_typed(app_state_t *app) {
    proven_u8str_view_t text = rubraview_textedit_text(&app->rename_edit);
    int32_t param = app->gmic_typing;
    rename_end(app);
    if (param >= 0 && param < RUBRAVIEW_GMIC_MAX_SHOWN) {
        snprintf(app->gmic_texts[param], sizeof(app->gmic_texts[param]), "%.*s", (int)text.size, (const char *)text.ptr);
        panel_build_edit(app);
        return;
    }
    text = proven_u8str_view_trim(text);
    if (text.size == 0) return;
    rubraview_edit_step_t s = { .kind = RUBRAVIEW_STEP_GMIC, .area = edit_area_now(app) };
    if (text.size >= sizeof(s.command)) { osd_say(app, TU8("the command is too long for a step")); return; }
    memcpy(s.command, text.ptr, text.size);
    snprintf(app->gmic_last_typed, sizeof(app->gmic_last_typed), "%s", s.command);
    (void)edit_apply_pending(app, false);
    if (!rubraview_edit_steps_push(&app->edit_steps, &s)) return;
    edit_steps_changed(app);
    osd_say(app, TU8("G'MIC applied (Ctrl+Z undoes it)"));
}

/* A G'MIC step as the change list names it: the filter its command is, by its name; else the command typed. */
const char *gmic_step_name(const rubraview_edit_step_t *step) {
    static char name[160];
    proven_u8str_view_t items[RUBRAVIEW_GMIC_MAX_ITEMS];
    if (rubraview_gmic_split(proven_u8str_view_from_cstr(step->command), items, RUBRAVIEW_GMIC_MAX_ITEMS) >= 1) {
        for (int32_t i = 0; i < g_gmic.filter_count; ++i) {
            if (!proven_u8str_view_eq(g_gmic.filters[i].command, items[0])) continue;
            snprintf(name, sizeof(name), "G'MIC %.*s", (int)g_gmic.filters[i].name.size, (const char *)g_gmic.filters[i].name.ptr);
            return name;
        }
    }
    return step->command;
}

/* ---- running a step: the picture to a file, gmic.exe on it, its file read back ---- */

static bool gmic_tick(void *context) {
    (void)context;
    return (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
}

typedef struct gmic_result {
    char command[RUBRAVIEW_EDIT_COMMAND_MAX];
    int32_t width, height, out_width, out_height;
    uint64_t sum;
    uint8_t *rgba;
} gmic_result_t;
static gmic_result_t g_gmic_results[4];
static size_t g_gmic_result_next;
static SRWLOCK g_gmic_results_lock = SRWLOCK_INIT;

/* `src` as RGBA, tightly packed. */
static uint8_t *gmic_rgba_of(const rubraview_pixbuf_t *src) {
    size_t row = (size_t)src->width * 4u;
    uint8_t *out = (uint8_t *)rubraview_malloc_product(row, (size_t)src->height, 1);
    if (!out) return NULL;
    bool bgra = src->format == RUBRAVIEW_PIXFMT_BGRA8;
    for (int32_t y = 0; y < src->height; ++y) {
        const uint8_t *s = src->pixels + (size_t)y * (size_t)src->stride;
        uint8_t *d = out + (size_t)y * row;
        if (!bgra) { memcpy(d, s, row); continue; }
        for (int32_t x = 0; x < src->width; ++x) { d[x * 4] = s[x * 4 + 2]; d[x * 4 + 1] = s[x * 4 + 1]; d[x * 4 + 2] = s[x * 4]; d[x * 4 + 3] = s[x * 4 + 3]; }
    }
    return out;
}

/* RGBA as a pixbuf of `format`, in the arena. */
static rubraview_pixbuf_t gmic_pixbuf_of(proven_arena_t *arena, const uint8_t *rgba, int32_t w, int32_t h, rubraview_pixel_format_t format) {
    rubraview_pixbuf_t out = rubraview_pixbuf_create(arena, w, h, format);
    if (!rubraview_pixbuf_is_valid(&out)) return out;
    for (int32_t y = 0; y < h; ++y) {
        const uint8_t *s = rgba + (size_t)y * (size_t)w * 4u;
        uint8_t *d = out.pixels + (size_t)y * (size_t)out.stride;
        if (format != RUBRAVIEW_PIXFMT_BGRA8) { memcpy(d, s, (size_t)w * 4u); continue; }
        for (int32_t x = 0; x < w; ++x) { d[x * 4] = s[x * 4 + 2]; d[x * 4 + 1] = s[x * 4 + 1]; d[x * 4 + 2] = s[x * 4]; d[x * 4 + 3] = s[x * 4 + 3]; }
    }
    return out;
}

static rubraview_pixbuf_t gmic_runner(proven_arena_t *arena, const rubraview_edit_step_t *step, const rubraview_pixbuf_t *src) {
    rubraview_pixbuf_t empty = {0};
    g_plugin_why = NULL;
    if (!g_gmic_exe[0]) { g_plugin_why = N_("G'MIC is not found (Settings › External)"); return empty; }
    uint8_t *in = gmic_rgba_of(src);
    if (!in) { g_plugin_why = N_("out of memory"); return empty; }
    size_t bytes = (size_t)src->width * 4u * (size_t)src->height;
    uint64_t sum = bytes_sum(in, bytes);
    rubraview_pixbuf_t done = empty;
    AcquireSRWLockShared(&g_gmic_results_lock);
    for (size_t i = 0; i < 4 && !done.pixels; ++i) {
        const gmic_result_t *r = &g_gmic_results[i];
        if (r->rgba && r->sum == sum && r->width == src->width && r->height == src->height && strcmp(r->command, step->command) == 0)
            done = gmic_pixbuf_of(arena, r->rgba, r->out_width, r->out_height, src->format);
    }
    ReleaseSRWLockShared(&g_gmic_results_lock);
    if (done.pixels) { free(in); return done; }

    static volatile LONG run_number;
    uint32_t tag = (uint32_t)InterlockedIncrement(&run_number);
    proven_u8str_view_t in_path = rubraview_pal_gmic_temp(arena, tag, false), out_path = rubraview_pal_gmic_temp(arena, tag, true);
    rubraview_pixbuf_t whole = { .pixels = in, .width = src->width, .height = src->height, .stride = src->width * 4,
                                 .format = RUBRAVIEW_PIXFMT_RGBA8 };
    rubraview_export_options_t png = rubraview_export_defaults();
    png.format = RUBRAVIEW_EXPORT_PNG;
    png.png_compression = 1;   /* speed: the file lives a moment */
    if (in_path.size == 0 || !rubraview_pal_image_save(in_path, &whole, &png)) {
        free(in);
        g_plugin_why = N_("the picture could not be handed to G'MIC");
        return empty;
    }
    free(in);
    proven_u8str_view_t items[RUBRAVIEW_GMIC_MAX_ITEMS];
    size_t n = rubraview_gmic_split(cstr(step->command), items + 1, RUBRAVIEW_GMIC_MAX_ITEMS - 3);
    rubraview_gmic_exec_t r = { .ok = false };
    if (n > 0) {
        items[0] = in_path;
        items[n + 1] = PROVEN_LIT("-o");
        items[n + 2] = out_path;
        r = rubraview_pal_gmic_exec(cstr(g_gmic_exe), items, n + 3, 10u * 60u * 1000u, gmic_tick, NULL);
    }
    (void)rubraview_pal_fs_delete(in_path);
    rubraview_pixbuf_t back = r.ok ? rubraview_pal_image_read_pixels(arena, out_path, NULL, 0, false) : empty;
    (void)rubraview_pal_fs_delete(out_path);
    if (!rubraview_pixbuf_is_valid(&back)) {
        if (r.cancelled) g_plugin_why = N_("cancelled");
        else if (r.cut) g_plugin_why = N_("G'MIC did not finish in time; it was ended");
        else if (n == 0) g_plugin_why = N_("the G'MIC command is empty or too long");
        else {
            /* its last line says what went wrong */
            const char *say = r.said;
            size_t len = strlen(say);
            while (len > 0 && (say[len - 1] == '\n' || say[len - 1] == ' ')) len--;
            size_t start = len;
            while (start > 0 && say[start - 1] != '\n') start--;
            snprintf(g_gmic_said, sizeof(g_gmic_said), "G'MIC: %.*s", (int)(len - start), say + start);
            g_plugin_why = len > 0 ? g_gmic_said : N_("G'MIC gave no picture back");
        }
        return empty;
    }
    if (back.format != src->format && (back.format == RUBRAVIEW_PIXFMT_RGBA8 || back.format == RUBRAVIEW_PIXFMT_BGRA8)) {
        uint8_t *rgba = gmic_rgba_of(&back);
        if (rgba) { back = gmic_pixbuf_of(arena, rgba, back.width, back.height, src->format); free(rgba); }
    }
    uint8_t *kept = rubraview_pixbuf_is_valid(&back) ? gmic_rgba_of(&back) : NULL;
    if (kept) {
        AcquireSRWLockExclusive(&g_gmic_results_lock);
        gmic_result_t *slot = &g_gmic_results[g_gmic_result_next++ % 4];
        free(slot->rgba);
        *slot = (gmic_result_t){ .width = src->width, .height = src->height, .out_width = back.width, .out_height = back.height,
                                 .sum = sum, .rgba = kept };
        snprintf(slot->command, sizeof(slot->command), "%s", step->command);
        ReleaseSRWLockExclusive(&g_gmic_results_lock);
    }
    return back;
}

/* ---- RFC-0009 D: FFmpeg's filters, by avfilter of the FFmpeg Settings › External names (inside the program, as
   FFmpeg's decoders are: D-8) ---- */

#define FFMPEG_GROUP_SIZE 24

static void *g_ffm_mem;                      /* the list's room */
static proven_arena_t g_ffm_arena;
rubraview_ffilter_catalog_t g_ffm;    /* the filters; empty until avfilter is loaded and asked */
static int32_t g_ffm_first[RUBRAVIEW_FFILTER_MAX_GROUPS];
static int32_t g_ffm_groups;
static bool g_ffm_asked;
static _Thread_local char g_ffm_said[512];   /* what FFmpeg said when a run failed */

/* The list, made once avfilter is there; asked again only after a failed try when FFmpeg is found anew. */
bool ffmpeg_list_ready(void) {
    if (g_ffm.filter_count > 0) return true;
    if (rubraview_pal_ffilter_state() != RUBRAVIEW_FFILTER_READY || g_ffm_asked) return false;
    g_ffm_asked = true;
    size_t room = 8u << 20;
    g_ffm_mem = malloc(room);
    if (!g_ffm_mem) return false;
    g_ffm_arena = proven_arena_create((proven_mem_mut_t){ .ptr = g_ffm_mem, .size = room });
    if (!rubraview_pal_ffilter_list(&g_ffm_arena, &g_ffm)) {
        free(g_ffm_mem);
        g_ffm_mem = NULL;
        g_ffm = (rubraview_ffilter_catalog_t){0};
        return false;
    }
    g_ffm_groups = rubraview_ffilter_groups(&g_ffm, FFMPEG_GROUP_SIZE, g_ffm_first, RUBRAVIEW_FFILTER_MAX_GROUPS);
    return true;
}

/* The `k`th filter of group `group` (from 1); NULL past its end. */
static const rubraview_ffilter_t *ffmpeg_filter_in(int32_t group, int32_t k, int32_t *count) {
    if (count) *count = 0;
    if (group < 1 || group > g_ffm_groups) return NULL;
    int32_t a = g_ffm_first[group - 1], b = group < g_ffm_groups ? g_ffm_first[group] : g_ffm.filter_count;
    if (count) *count = b - a;
    return k >= 0 && a + k < b ? &g_ffm.filters[a + k] : NULL;
}

static const rubraview_ffilter_t *ffmpeg_chosen(const app_state_t *app) {
    return app->ffmpeg_group > 0 ? ffmpeg_filter_in(app->ffmpeg_group, app->ffmpeg_filter, NULL) : NULL;
}

static void ffmpeg_defaults(app_state_t *app) {
    const rubraview_ffilter_t *f = ffmpeg_chosen(app);
    for (int32_t i = 0; f && i < f->option_count && i < RUBRAVIEW_FFILTER_MAX_SHOWN; ++i) {
        app->ffmpeg_values[i] = f->options[i].value;
        snprintf(app->ffmpeg_texts[i], sizeof(app->ffmpeg_texts[i]), "%.*s", (int)f->options[i].text.size, (const char *)f->options[i].text.ptr);
    }
}

static void edit_rows_ffmpeg(app_state_t *app) {
    if (!ffmpeg_list_ready()) return;
    if (app->ffmpeg_group < 0 || app->ffmpeg_group > g_ffm_groups) app->ffmpeg_group = 0;
    rubraview_panel_add_choice(&app->panel, PANEL_FFMPEG_GROUP, TU8("FFmpeg filters"), app->ffmpeg_group, g_ffm_groups + 1);
    if (app->ffmpeg_group > 0) {
        int32_t count = 0;
        (void)ffmpeg_filter_in(app->ffmpeg_group, 0, &count);
        if (app->ffmpeg_filter < 0 || app->ffmpeg_filter >= count) app->ffmpeg_filter = 0;
        rubraview_panel_add_choice(&app->panel, PANEL_FFMPEG_FILTER, TU8("  filter"), app->ffmpeg_filter, count);
        const rubraview_ffilter_t *f = ffmpeg_chosen(app);
        static char labels[PANEL_FFMPEG_ROWS][120];   /* the rows' words live as long as the panel */
        for (int32_t i = 0; f && i < f->option_count && i < PANEL_FFMPEG_ROWS; ++i) {
            const rubraview_ffilter_option_t *q = &f->options[i];
            int32_t id = PANEL_FFMPEG_P0 + i;
            if (q->kind == RUBRAVIEW_FFILTER_TEXT) {
                snprintf(labels[i], sizeof(labels[i]), "  %.*s: %s", (int)q->name.size, (const char *)q->name.ptr,
                         app->ffmpeg_texts[i][0] ? app->ffmpeg_texts[i] : T("(FFmpeg's default)"));
                rubraview_panel_add_button(&app->panel, id, cstr(labels[i]));
                continue;
            }
            snprintf(labels[i], sizeof(labels[i]), "  %.*s", (int)q->name.size, (const char *)q->name.ptr);
            proven_u8str_view_t name = cstr(labels[i]);
            if (q->kind == RUBRAVIEW_FFILTER_BOOL) rubraview_panel_add_toggle(&app->panel, id, name, app->ffmpeg_values[i] > 0.5);
            else if (q->kind == RUBRAVIEW_FFILTER_CHOICE) rubraview_panel_add_choice(&app->panel, id, name, (int32_t)app->ffmpeg_values[i], q->choice_count);
            else rubraview_panel_add_slider(&app->panel, id, name, app->ffmpeg_values[i], q->min, q->max,
                                            q->kind == RUBRAVIEW_FFILTER_INT ? 1.0 : (q->max - q->min) / 100.0);
        }
        rubraview_panel_add_button(&app->panel, PANEL_FFMPEG_APPLY, TU8("Run the FFmpeg filter: a step"));
    }
    rubraview_panel_add_button(&app->panel, PANEL_FFMPEG_TYPE, TU8("Type an FFmpeg filter graph..."));
}

/* A choice row's words: the groups, the filters of the one chosen (with what they do), an option's choices. */
static proven_u8str_view_t ffmpeg_choice_word(const app_state_t *app, int32_t id, int32_t i, char *buffer, size_t size) {
    buffer[0] = '\0';
    if (id == PANEL_FFMPEG_GROUP) {
        if (i <= 0) return cstr(T("none"));
        rubraview_ffilter_group_name(&g_ffm, g_ffm_first, g_ffm_groups, i - 1, buffer, size);
    } else if (id == PANEL_FFMPEG_FILTER) {
        const rubraview_ffilter_t *f = ffmpeg_filter_in(app->ffmpeg_group, i, NULL);
        if (f) snprintf(buffer, size, "%.*s \xE2\x80\x94 %.*s", (int)f->name.size, (const char *)f->name.ptr,
                        (int)(f->description.size < 60 ? f->description.size : 60), (const char *)f->description.ptr);
    } else {
        const rubraview_ffilter_t *f = ffmpeg_chosen(app);
        int32_t p = id - PANEL_FFMPEG_P0;
        if (f && p >= 0 && p < f->option_count && f->options[p].kind == RUBRAVIEW_FFILTER_CHOICE && i >= 0 && i < f->options[p].choice_count)
            snprintf(buffer, size, "%.*s", (int)f->options[p].choices[i].size, (const char *)f->options[p].choices[i].ptr);
    }
    return cstr(buffer);
}

/* A row's value changed; true when it was one of FFmpeg's. */
bool ffmpeg_row_changed(app_state_t *app, int32_t id, double v) {
    if (id == PANEL_FFMPEG_GROUP || id == PANEL_FFMPEG_FILTER) {
        if (id == PANEL_FFMPEG_GROUP) { app->ffmpeg_group = (int32_t)lround(v); app->ffmpeg_filter = 0; }
        else app->ffmpeg_filter = (int32_t)lround(v);
        ffmpeg_defaults(app);
        panel_build_edit(app);
        return true;
    }
    if (id < PANEL_FFMPEG_P0 || id >= PANEL_FFMPEG_P0 + PANEL_FFMPEG_ROWS) return false;
    app->ffmpeg_values[id - PANEL_FFMPEG_P0] = v;
    return true;
}

/* The box for an option's text (its index), or for a whole graph (-1). */
static void ffmpeg_type_begin(app_state_t *app, int32_t option) {
    if (app->rename_active) rename_end(app);
    const char *now = option >= 0 ? app->ffmpeg_texts[option] : app->ffmpeg_last_typed;
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    rubraview_textedit_set(&app->rename_edit, cstr(now));
    rubraview_textedit_select_all(&app->rename_edit);
    app->rename_active = true;
    app->rename_is_ffmpeg = true;
    app->ffmpeg_typing = option;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, true);
    osd_say(app, option >= 0 ? TU8("type it, then Enter (Esc leaves it)")
                             : TU8("an FFmpeg filter graph such as unsharp=5:5:1.0 (Enter runs it, Esc leaves it)"));
}

/* The graph made a step. */
static void ffmpeg_push(app_state_t *app, proven_u8str_view_t graph) {
    const char *refused = rubraview_ffilter_refused(graph);
    if (refused) {
        char line[160];
        snprintf(line, sizeof(line), T("the filter %s is not allowed here"), refused);
        osd_say(app, cstr(line));
        return;
    }
    rubraview_edit_step_t s = { .kind = RUBRAVIEW_STEP_FFMPEG, .area = edit_area_now(app) };
    if (graph.size == 0 || graph.size >= sizeof(s.command)) { osd_say(app, TU8("the filter graph is too long for a step")); return; }
    memcpy(s.command, graph.ptr, graph.size);
    (void)edit_apply_pending(app, false);
    if (!rubraview_edit_steps_push(&app->edit_steps, &s)) return;
    edit_steps_changed(app);
    osd_say(app, TU8("FFmpeg filter applied (Ctrl+Z undoes it)"));
}

/* A button of FFmpeg's pressed; true when it was one. */
bool ffmpeg_button(app_state_t *app, int32_t id) {
    if (id == PANEL_FFMPEG_TYPE) { ffmpeg_type_begin(app, -1); return true; }
    if (id >= PANEL_FFMPEG_P0 && id < PANEL_FFMPEG_P0 + PANEL_FFMPEG_ROWS) { ffmpeg_type_begin(app, id - PANEL_FFMPEG_P0); return true; }
    if (id != PANEL_FFMPEG_APPLY) return false;
    const rubraview_ffilter_t *f = ffmpeg_chosen(app);
    if (!f) { osd_say(app, TU8("choose an FFmpeg filter first")); return true; }
    proven_u8str_view_t texts[RUBRAVIEW_FFILTER_MAX_SHOWN];
    for (int32_t i = 0; i < RUBRAVIEW_FFILTER_MAX_SHOWN; ++i) texts[i] = cstr(app->ffmpeg_texts[i]);
    char graph[RUBRAVIEW_EDIT_COMMAND_MAX];
    size_t n = rubraview_ffilter_graph(f, app->ffmpeg_values, texts, graph, sizeof(graph));
    if (n == 0) { osd_say(app, TU8("this filter's settings are too long for a step")); return true; }
    ffmpeg_push(app, proven_u8str_view_from_bytes(graph, n));
    return true;
}

/* Enter in the box: an option's text kept, or the graph typed made a step. */
void ffmpeg_typed(app_state_t *app) {
    proven_u8str_view_t text = rubraview_textedit_text(&app->rename_edit);
    int32_t option = app->ffmpeg_typing;
    rename_end(app);
    if (option >= 0 && option < RUBRAVIEW_FFILTER_MAX_SHOWN) {
        snprintf(app->ffmpeg_texts[option], sizeof(app->ffmpeg_texts[option]), "%.*s", (int)text.size, (const char *)text.ptr);
        panel_build_edit(app);
        return;
    }
    text = proven_u8str_view_trim(text);
    if (text.size == 0) return;
    snprintf(app->ffmpeg_last_typed, sizeof(app->ffmpeg_last_typed), "%.*s", (int)text.size, (const char *)text.ptr);
    ffmpeg_push(app, text);
}

/* ---- running a step: in the program, one frame through the graph ---- */

typedef struct ffmpeg_result {
    char graph[RUBRAVIEW_EDIT_COMMAND_MAX];
    int32_t width, height, out_width, out_height;
    uint64_t sum;
    uint8_t *bgra;
} ffmpeg_result_t;
static ffmpeg_result_t g_ffm_results[4];
static size_t g_ffm_result_next;
static SRWLOCK g_ffm_results_lock = SRWLOCK_INIT;

/* `src` as BGRA, tightly packed; or BGRA back as a pixbuf of `format` (the two swaps are one). */
static uint8_t *ffmpeg_bgra_of(const uint8_t *pixels, int32_t w, int32_t h, size_t stride, bool swap) {
    size_t row = (size_t)w * 4u;
    uint8_t *out = (uint8_t *)rubraview_malloc_product(row, (size_t)h, 1);
    if (!out) return NULL;
    for (int32_t y = 0; y < h; ++y) {
        const uint8_t *s = pixels + (size_t)y * stride;
        uint8_t *d = out + (size_t)y * row;
        if (!swap) { memcpy(d, s, row); continue; }
        for (int32_t x = 0; x < w; ++x) { d[x * 4] = s[x * 4 + 2]; d[x * 4 + 1] = s[x * 4 + 1]; d[x * 4 + 2] = s[x * 4]; d[x * 4 + 3] = s[x * 4 + 3]; }
    }
    return out;
}

static rubraview_pixbuf_t ffmpeg_pixbuf_of(proven_arena_t *arena, const uint8_t *bgra, int32_t w, int32_t h, rubraview_pixel_format_t format) {
    rubraview_pixbuf_t out = rubraview_pixbuf_create(arena, w, h, format);
    if (!rubraview_pixbuf_is_valid(&out)) return out;
    bool swap = format != RUBRAVIEW_PIXFMT_BGRA8;
    for (int32_t y = 0; y < h; ++y) {
        const uint8_t *s = bgra + (size_t)y * (size_t)w * 4u;
        uint8_t *d = out.pixels + (size_t)y * (size_t)out.stride;
        if (!swap) { memcpy(d, s, (size_t)w * 4u); continue; }
        for (int32_t x = 0; x < w; ++x) { d[x * 4] = s[x * 4 + 2]; d[x * 4 + 1] = s[x * 4 + 1]; d[x * 4 + 2] = s[x * 4]; d[x * 4 + 3] = s[x * 4 + 3]; }
    }
    return out;
}

static rubraview_pixbuf_t ffmpeg_runner(proven_arena_t *arena, const rubraview_edit_step_t *step, const rubraview_pixbuf_t *src) {
    rubraview_pixbuf_t empty = {0};
    g_plugin_why = NULL;
    if (src->format != RUBRAVIEW_PIXFMT_RGBA8 && src->format != RUBRAVIEW_PIXFMT_BGRA8) { g_plugin_why = N_("not an 8-bit picture"); return empty; }
    if (rubraview_pal_ffilter_state() != RUBRAVIEW_FFILTER_READY) {
        g_plugin_why = N_("FFmpeg's filters are not there: avfilter-12.dll beside FFmpeg (Settings › External)");
        return empty;
    }
    uint8_t *in = ffmpeg_bgra_of(src->pixels, src->width, src->height, (size_t)src->stride, src->format != RUBRAVIEW_PIXFMT_BGRA8);
    if (!in) { g_plugin_why = N_("out of memory"); return empty; }
    uint64_t sum = bytes_sum(in, (size_t)src->width * 4u * (size_t)src->height);
    rubraview_pixbuf_t done = empty;
    AcquireSRWLockShared(&g_ffm_results_lock);
    for (size_t i = 0; i < 4 && !done.pixels; ++i) {
        const ffmpeg_result_t *r = &g_ffm_results[i];
        if (r->bgra && r->sum == sum && r->width == src->width && r->height == src->height && strcmp(r->graph, step->command) == 0)
            done = ffmpeg_pixbuf_of(arena, r->bgra, r->out_width, r->out_height, src->format);
    }
    ReleaseSRWLockShared(&g_ffm_results_lock);
    if (done.pixels) { free(in); return done; }

    uint8_t *out = NULL;
    int32_t ow = 0, oh = 0;
    char said[400];
    bool ok = rubraview_pal_ffilter_run(step->command, in, src->width, src->height, &out, &ow, &oh, said, sizeof(said));
    free(in);
    if (!ok) {
        snprintf(g_ffm_said, sizeof(g_ffm_said), "FFmpeg: %s", said[0] ? said : "no picture came back");
        g_plugin_why = g_ffm_said;
        return empty;
    }
    done = ffmpeg_pixbuf_of(arena, out, ow, oh, src->format);
    if (rubraview_pixbuf_is_valid(&done)) {
        AcquireSRWLockExclusive(&g_ffm_results_lock);
        ffmpeg_result_t *slot = &g_ffm_results[g_ffm_result_next++ % 4];
        free(slot->bgra);
        *slot = (ffmpeg_result_t){ .width = src->width, .height = src->height, .out_width = ow, .out_height = oh, .sum = sum, .bgra = out };
        snprintf(slot->graph, sizeof(slot->graph), "%s", step->command);
        ReleaseSRWLockExclusive(&g_ffm_results_lock);
    } else {
        free(out);
        g_plugin_why = N_("out of memory");
    }
    return done;
}

static void edit_rows_resize(app_state_t *app) {
    const rubraview_edit_session_t *e = &app->edit;
    rubraview_panel_add_button(&app->panel, PANEL_CROP_SEL, TU8("Crop to the selection"));
    rubraview_panel_add_button(&app->panel, PANEL_TRIM, TU8("Select the plain margins' inside"));
    int32_t cw = 0, ch = 0;
    edit_cur_size(app, &cw, &ch);
    double percent = e->resize_width > 0 && cw > 0 ? 100.0 * (double)e->resize_width / (double)cw : 100.0;
    rubraview_panel_add_slider(&app->panel, PANEL_RESIZE, TU8("Resize %"), percent, 10.0, 500.0, 1.0);
    rubraview_panel_add_choice(&app->panel, PANEL_RESIZE_PRESET, TU8("  or a size"), 0, RUBRAVIEW_RESIZE_PRESET_COUNT);
    rubraview_panel_add_button(&app->panel, PANEL_RESIZE_PX, px_button_label(app));
    rubraview_panel_add_toggle(&app->panel, PANEL_RESIZE_LOCK, TU8("  Keep the shape"), e->resize_lock_aspect);
    rubraview_panel_add_choice(&app->panel, PANEL_RESIZE_FILTER, TU8("  Filter"), (int32_t)e->resize_filter, 4);
    rubraview_panel_add_toggle(&app->panel, PANEL_RESIZE_LINEAR, TU8("  In linear light"), e->resize_linear);
    rubraview_panel_add_slider(&app->panel, PANEL_RESIZE_SHARPEN, TU8("  Sharpen after"), e->resize_sharpen, 0.0, 100.0, 1.0);
    rubraview_panel_add_slider(&app->panel, PANEL_RESIZE_DPI, TU8("  DPI (cm, in)"), e->resize_dpi > 0.0 ? e->resize_dpi : 300.0, 72.0, 1200.0, 1.0);
    rubraview_panel_add_button(&app->panel, PANEL_RESIZE_APPLY, TU8("Resize"));
}

static void edit_rows_save(app_state_t *app) {
    /* D-88: inside an archive, Save writes the page over in the archive. */
    rubraview_panel_add_button(&app->panel, PANEL_SAVE_ORIGINAL, on_archive_page(app) ? TU8("Save over the page") : TU8("Save (over the original)"));
    rubraview_panel_add_button(&app->panel, PANEL_SAVE_COPY, TU8("Save a copy"));
}

/* RV-137 (owner, 2026-10-18): a drag on the picture moves it and the wheel
   zooms, as without Edit; an area is chosen after this button, with a cross
   for the pointer; and the area copied to the clipboard at once. */
static void edit_rows_choose(app_state_t *app) {
    rubraview_panel_add_button(&app->panel, PANEL_SEL_MODE, app->sel_mode ? TU8("Choosing an area: drag on the picture (press to stop)")
                                                                          : TU8("Choose an area on the picture"));
    rubraview_panel_add_button(&app->panel, PANEL_SEL_COPY, app->edit.crop_active ? TU8("Copy the area to the clipboard")
                                                                                  : TU8("Copy the picture to the clipboard"));
}

/* RV-137: Edit's rows in its own window, in the order its width wants
   (the owner, 2026-10-18, chose these three): one column scrolled; two;
   three. A column begins at a section, never inside one. */
static void edit_rows_windowed(app_state_t *app) {
    enum { CHOOSE, CURVE, SEL, TONE, LEVELS, DETAIL, EFFECT, RESIZE, HISTORY, SAVE, NEXT, END };
    /* The change list near the top of its column: it is what the window is for (VM, 2026-10-18:
       under the selection's numbers it was below a 680-pixel window's bottom). */
    /* The owner (2026-10-18): first of all, top left, the button that chooses an area; then the
       edits; then the curve. */
    static const int ORDER[3][14] = {
        { CHOOSE, HISTORY, CURVE, TONE, LEVELS, DETAIL, EFFECT, SEL, RESIZE, SAVE, END },
        { CHOOSE, HISTORY, CURVE, TONE, LEVELS, NEXT, SEL, DETAIL, EFFECT, RESIZE, SAVE, END },
        { CHOOSE, HISTORY, CURVE, SEL, NEXT, TONE, LEVELS, DETAIL, NEXT, EFFECT, RESIZE, SAVE, END },
    };
    const rubraview_edit_session_t *e = &app->edit;
    rubraview_panel_t *p = &app->panel;
    double dpi = rubraview_pal_window_dpi_scale(app->edit_window);
    int32_t mode = app->edit_mode >= 0 && app->edit_mode <= 2 ? app->edit_mode : 2, column = 0;
    app->edit_firsts[0] = app->edit_firsts[1] = app->edit_firsts[2] = 0;
    for (int k = 0; ORDER[mode][k] != END; ++k) {
        switch (ORDER[mode][k]) {
            case NEXT: app->edit_firsts[++column] = p->row_count; continue;
            case CHOOSE: edit_rows_choose(app); break;
            case CURVE: rubraview_panel_add_spacer(p, PANEL_SP_CURVE, 184.0 * dpi); break;
            case SEL:
                rubraview_panel_add_spacer(p, PANEL_SP_SEL, 200.0 * dpi);   /* as tall as its box, set when laid out */
                rubraview_panel_add_choice(p, PANEL_CROP_RATIO, TU8("Crop ratio"), (int32_t)e->crop_ratio, 5);
                break;
            case TONE: edit_rows_tone(app); break;
            case LEVELS: edit_rows_levels(app); break;
            case DETAIL: edit_rows_detail(app); break;
            case EFFECT: edit_rows_effect(app); break;
            case RESIZE: edit_rows_resize(app); break;
            case HISTORY:   /* undo and redo are Ctrl+Z / Ctrl+Y and a click on a change */
                rubraview_panel_add_spacer(p, PANEL_SP_CHANGES, 160.0 * dpi);
                rubraview_panel_add_button(p, PANEL_CHANGES_REMOVE, TU8("Remove the ticked edits"));
                rubraview_panel_add_button(p, PANEL_ALL_COPIES, TU8("Copies of every picture in the list..."));
                rubraview_panel_add_button(p, PANEL_ALL_ORIGINALS, TU8("Apply to every original in the list..."));
                rubraview_panel_add_button(p, PANEL_RESET, TU8("Reset: back to the picture as it was"));
                break;
            case SAVE: edit_rows_save(app); break;
            default: break;
        }
        int next = ORDER[mode][k + 1];
        if (next != NEXT && next != END) rubraview_panel_add_separator(p);
    }
}

void panel_build_edit(app_state_t *app) {
    double dpi = rubraview_pal_window_dpi_scale(app->edit_window ? app->edit_window : app->window);
    double column_scroll[RUBRAVIEW_PANEL_MAX_COLUMNS];
    memcpy(column_scroll, app->panel.column_scroll, sizeof(column_scroll));   /* each column stays where it was scrolled */
    app->edit_preview_tries = 0;
    const rubraview_edit_session_t *e = &app->edit;
    bool placed = app->panel.open && edit_panel_open(app) && app->panel.placed;
    double px = app->panel.place_x, py = app->panel.place_y, scroll = app->panel.open ? app->panel.scroll : 0.0;
    app->panel = rubraview_panel_create(TU8("Edit"), dpi);
    app->panel.placed = placed;
    app->panel.place_x = px;
    app->panel.place_y = py;
    app->panel.scroll = scroll;
    app->panel_is_export = false;
    app->panel_is_batch = false;
    app->panel_is_print = false;
    app->panel_is_convert = false;

    if (app->edit_window) {
        memcpy(app->panel.column_scroll, column_scroll, sizeof(column_scroll));
        edit_rows_windowed(app);
        app->panel.open = true;
        panel_relayout(app);
        return;
    }
    /* First where: the selection (drawn on the picture, or its numbers). */
    rubraview_panel_add_button(&app->panel, PANEL_SEL_BOX, edit_sel_label(app));
    edit_rows_choose(app);
    rubraview_panel_add_choice(&app->panel, PANEL_CROP_RATIO, TU8("Crop ratio"), (int32_t)e->crop_ratio, 5);
    rubraview_panel_add_separator(&app->panel);
    /* Then what: every effect, in one window (owner, 2026-10-17). */
    edit_rows_tone(app);
    rubraview_panel_add_separator(&app->panel);
    edit_rows_levels(app);
    rubraview_panel_add_separator(&app->panel);
    edit_rows_detail(app);
    rubraview_panel_add_separator(&app->panel);
    /* Effects (owner, 2026-10-17): one chosen, seen on the picture, applied as a step. */
    edit_rows_effect(app);
    rubraview_panel_add_separator(&app->panel);
    edit_rows_resize(app);
    rubraview_panel_add_separator(&app->panel);
    rubraview_panel_add_button(&app->panel, PANEL_UNDO, TU8("Undo (Ctrl+Z)"));
    rubraview_panel_add_button(&app->panel, PANEL_REDO, TU8("Redo (Ctrl+Y)"));
    rubraview_panel_add_button(&app->panel, PANEL_STEPS, TU8("Undo list..."));
    rubraview_panel_add_button(&app->panel, PANEL_RESET, TU8("Reset: back to the picture as it was"));
    rubraview_panel_add_separator(&app->panel);
    edit_rows_save(app);

    app->panel.open = true;
    panel_relayout(app);
}

/* §3.13's workbench, and the owner's edit: the steps start empty over the
   picture as it is; the values are the next step's. */
void panel_open_edit(app_state_t *app) {
    int32_t page = current_page_index(app);
    if (page < 0) return;

    if (app->edit_kept && app->edit_steps.width > 0) {
        app->edit_kept = false;   /* back from Export: the edits as they were (the preview rebases them) */
    } else {
        app->edit_kept = false;
        app->edit = rubraview_edit_begin(real_w(&app->pages[page]), real_h(&app->pages[page]));
        rubraview_edit_steps_begin(&app->edit_steps, real_w(&app->pages[page]), real_h(&app->pages[page]));
        app->sel_shape = RUBRAVIEW_AREA_RECT;
        app->sel_feather = 0;
    }
    app->edit_rev++;
    app->panel.open = false;
    panel_build_edit(app);
    edit_preview_open(app);
    edit_window_show(app);   /* RV-137: in a window of its own beside the viewer */
    toolbox_refresh(app);    /* what does not work in Edit leaves the toolbox */
}

/* The effects' names in the language in use (effects.c keeps them in English). */
const char *effect_word(int32_t e) {
    switch (e) {
        case RUBRAVIEW_EFFECT_NONE: return T("none");
        case RUBRAVIEW_EFFECT_GRAYSCALE: return T("Grey");
        case RUBRAVIEW_EFFECT_SEPIA: return T("Sepia");
        case RUBRAVIEW_EFFECT_NEGATIVE: return T("Negative");
        case RUBRAVIEW_EFFECT_SOLARIZE: return T("Solarize");
        case RUBRAVIEW_EFFECT_POSTERIZE: return T("Posterize");
        case RUBRAVIEW_EFFECT_EMBOSS: return T("Emboss");
        case RUBRAVIEW_EFFECT_EDGES: return T("Edges");
        case RUBRAVIEW_EFFECT_MEDIAN: return T("Median (noise out)");
        case RUBRAVIEW_EFFECT_OIL: return T("Oil paint");
        case RUBRAVIEW_EFFECT_PIXELIZE: return T("Mosaic (pixelize)");
        case RUBRAVIEW_EFFECT_NOISE: return T("Noise");
        case RUBRAVIEW_EFFECT_VIGNETTE: return T("Vignette");
        case RUBRAVIEW_EFFECT_SWIRL: return T("Swirl");
        case RUBRAVIEW_EFFECT_RADIAL_BLUR: return T("Radial blur");
        case RUBRAVIEW_EFFECT_ZOOM_BLUR: return T("Zoom blur");
        case RUBRAVIEW_EFFECT_WAVE: return T("Wave");
        case RUBRAVIEW_EFFECT_RED_EYE: return T("Red eye reduction");
        case RUBRAVIEW_EFFECT_CHROMATIC: return T("Colour fringe fix");
        case RUBRAVIEW_EFFECT_BUTTON_3D: return T("3D button");
        case RUBRAVIEW_EFFECT_FRAGMENT: return T("Fragment");
        case RUBRAVIEW_EFFECT_EXPLOSION: return T("Explosion");
        case RUBRAVIEW_EFFECT_THRESHOLD: return T("Black and white");
        case RUBRAVIEW_EFFECT_DITHER: return T("Black and white, dithered");
        case RUBRAVIEW_EFFECT_COLOURS: return T("Fewer colours");
        case RUBRAVIEW_EFFECT_COLOURS_DITHER: return T("Fewer colours, dithered");
        case RUBRAVIEW_EFFECT_SKETCH: return T("Pencil sketch");
        default: return NULL;
    }
}

static const char *effect_group_word(int32_t g) {
    switch ((rubraview_effect_group_t)g) {
        case RUBRAVIEW_EFFECT_GROUP_COLOUR: return T("Colour");
        case RUBRAVIEW_EFFECT_GROUP_ARTISTIC: return T("Artistic");
        case RUBRAVIEW_EFFECT_GROUP_BLUR: return T("Blur and noise");
        case RUBRAVIEW_EFFECT_GROUP_DISTORT: return T("Distort");
        case RUBRAVIEW_EFFECT_GROUP_FIX: return T("Fix and light");
        default: return NULL;
    }
}

/* RV-128 */
static const char *resize_preset_word(int32_t i) {
    switch ((rubraview_resize_preset_t)i) {
        case RUBRAVIEW_RESIZE_PRESET_NONE:      return T("(choose)");
        case RUBRAVIEW_RESIZE_PRESET_HALF:      return T("half");
        case RUBRAVIEW_RESIZE_PRESET_DOUBLE:    return T("double");
        case RUBRAVIEW_RESIZE_PRESET_SCREEN:    return T("fit the screen");
        case RUBRAVIEW_RESIZE_PRESET_640:       return "640 x 480";
        case RUBRAVIEW_RESIZE_PRESET_800:       return "800 x 600";
        case RUBRAVIEW_RESIZE_PRESET_1024:      return "1024 x 768";
        case RUBRAVIEW_RESIZE_PRESET_720P:      return "1280 x 720 (HD)";
        case RUBRAVIEW_RESIZE_PRESET_1080P:     return "1920 x 1080 (Full HD)";
        case RUBRAVIEW_RESIZE_PRESET_1440P:     return "2560 x 1440 (QHD)";
        case RUBRAVIEW_RESIZE_PRESET_4K:        return "3840 x 2160 (4K)";
        case RUBRAVIEW_RESIZE_PRESET_8K:        return "7680 x 4320 (8K)";
        case RUBRAVIEW_RESIZE_PRESET_LONG_1024: return T("long side 1024");
        case RUBRAVIEW_RESIZE_PRESET_LONG_2048: return T("long side 2048");
        case RUBRAVIEW_RESIZE_PRESET_MP_2:      return T("2 megapixels");
        case RUBRAVIEW_RESIZE_PRESET_MP_8:      return T("8 megapixels");
        case RUBRAVIEW_RESIZE_PRESET_MP_12:     return T("12 megapixels");
        default: return NULL;
    }
}

/* What a row's value is called: a choice's word, or a number worth reading. */
proven_u8str_view_t adjust_row_text(const app_state_t *app, const rubraview_panel_row_t *row, char *buffer, size_t size) {
    (void)app;
    const char *const CROP[] = { T("free"), "1:1", "4:3", "16:9", T("the picture's") };
    const char *const BLUR[] = { T("soft (Gaussian)"), T("flat (box)") };
    const char *const SHARP[] = { T("unsharp mask"), T("Laplacian") };
    const char *const FORMAT[RUBRAVIEW_EXPORT_FORMATS] = { T("as the file"), T("JPEG"), T("PNG"), T("WebP"), T("GIF"), T("BMP"), T("TIFF"), T("ICO"),
                                                           T("AVIF (FFmpeg)"), T("JPEG XL (FFmpeg)") };   /* RV-139 */
    const char *const DEPTH[] = { T("colour + alpha (32)"), T("colour (24)"), T("a palette"), T("grey (8)") };
    /* RV-140 */
    const char *const SUBSAMPLING[] = { T("as the writer does (4:2:0)"), T("4:2:0 (smallest)"), T("4:2:2"), T("4:4:4 (sharpest colour)") };
    const char *const PNG_FILTER[] = { T("by the level"), T("none"), T("sub"), T("up"), T("average"), T("Paeth"), T("adaptive") };
    const char *const TIFF_COMP[] = { T("as the writer does"), T("none"), T("LZW"), T("ZIP (Deflate)") };
    const char *const BMP_BITS[] = { T("32 bits (with alpha)"), T("24 bits"), T("8 bits (a palette)") };
    const char *const KEEP[] = { T("dropped"), T("kept in the file"), T("in the file, or a .txt beside it") };
    const char *const PRESET[] = { T("none"), T("picture"), T("photo"), T("drawing"), T("icon"), T("text") };
    int32_t i = (int32_t)row->value;
    const char *word = NULL;
    if (row->id >= PANEL_GMIC_P0 && row->id < PANEL_GMIC_P0 + PANEL_GMIC_ROWS) return gmic_choice_word(app, row->id, i, buffer, size);
    if (row->id >= PANEL_FFMPEG_P0 && row->id < PANEL_FFMPEG_P0 + PANEL_FFMPEG_ROWS) return ffmpeg_choice_word(app, row->id, i, buffer, size);
    switch (row->id) {
        case PANEL_CROP_RATIO:   word = i >= 0 && i < 5 ? CROP[i] : NULL; break;
        case PANEL_EFFECT: {   /* within the kind chosen */
            rubraview_effect_t members[RUBRAVIEW_EFFECT_COUNT];
            size_t n = rubraview_effect_group_members((rubraview_effect_group_t)app->effect_group, members, RUBRAVIEW_EFFECT_COUNT);
            word = i <= 0 ? effect_word(RUBRAVIEW_EFFECT_NONE) : (size_t)i <= n ? effect_word(members[i - 1]) : NULL;
            break;
        }
        case PANEL_EFFECT_GROUP: word = effect_group_word(i); break;
        case PANEL_PLUGIN: {   /* RFC-0007 */
            const rubraview_plugin_t *list[RUBRAVIEW_PLUGIN_MAX];
            size_t n = plugins_usable(list, RUBRAVIEW_PLUGIN_MAX);
            word = i <= 0 ? T("none") : (size_t)i <= n ? list[i - 1]->name : NULL;
            break;
        }
        case PANEL_PLUGIN_P0: case PANEL_PLUGIN_P1: case PANEL_PLUGIN_P2: case PANEL_PLUGIN_P3:
        case PANEL_PLUGIN_P4: case PANEL_PLUGIN_P5: case PANEL_PLUGIN_P6: case PANEL_PLUGIN_P7: {
            const rubraview_plugin_t *list[RUBRAVIEW_PLUGIN_MAX];
            size_t n = plugins_usable(list, RUBRAVIEW_PLUGIN_MAX);
            const rubraview_plugin_t *p = app->plugin_choice > 0 && (size_t)app->plugin_choice <= n ? list[app->plugin_choice - 1] : NULL;
            int32_t k = row->id - PANEL_PLUGIN_P0;
            if (p && k < p->param_count && p->params[k].kind == RVF_CHOICE &&
                rubraview_plugin_choice_word(p->params[k].choices, i, buffer, size)) return cstr(buffer);
            break;
        }
        case PANEL_SEL_SHAPE:    word = i == 1 ? T("ellipse") : T("rectangle"); break;
        case PANEL_GMIC_CATEGORY: case PANEL_GMIC_FILTER: return gmic_choice_word(app, row->id, i, buffer, size);   /* RFC-0009 C */
        case PANEL_FFMPEG_GROUP: case PANEL_FFMPEG_FILTER: return ffmpeg_choice_word(app, row->id, i, buffer, size);   /* RFC-0009 D */
        case PANEL_RESIZE_PRESET: word = resize_preset_word(i); break;
        /* RV-129: the batch window */
        case PANEL_BATCH_SIZE_BY: {
            const char *const BY[] = { T("as it is"), T("percent"), T("long side"), T("short side"), T("width"), T("height"), T("fit in a box"), T("megapixels") };
            word = i >= 0 && i < 8 ? BY[i] : NULL;
            break;
        }
        case PANEL_BATCH_FILTER:  word = i == 0 ? T("nearest (pixel art)") : i == 1 ? T("bilinear") : i == 2 ? T("bicubic") : i == 3 ? T("Lanczos-3 (sharpest)") : NULL; break;
        case PANEL_BATCH_FORMAT:  word = i >= 0 && i < RUBRAVIEW_EXPORT_FORMATS ? FORMAT[i] : NULL; break;
        case PANEL_BATCH_ROTATE: {
            const char *const TURN[] = { T("no"), T("a quarter right"), T("half round"), T("a quarter left"), T("as its EXIF says") };
            word = i >= 0 && i < 5 ? TURN[i] : NULL;
            break;
        }
        case PANEL_BATCH_FLIP:    word = i == 0 ? T("no") : i == 1 ? T("left to right") : i == 2 ? T("upside down") : NULL; break;
        case PANEL_BATCH_EFFECT:  word = i == 0 ? T("none") : effect_word(i); break;
        case PANEL_BATCH_FRAME_COLOUR: word = i == 0 ? T("white") : i == 1 ? T("black") : i == 2 ? T("grey") : NULL; break;
        case PANEL_BATCH_MARK_CORNER: {
            const char *const AT[] = { T("bottom right"), T("bottom left"), T("top right"), T("top left"), T("centre") };
            word = i >= 0 && i < 5 ? AT[i] : NULL;
            break;
        }
        case PANEL_BATCH_TEXT_CORNER: {
            const char *const AT[] = { T("bottom right"), T("bottom left"), T("top right"), T("top left"), T("centre") };
            word = i >= 0 && i < 5 ? AT[i] : NULL;
            break;
        }
        case PANEL_BATCH_TEXT_COLOUR: word = i == 0 ? T("white") : i == 1 ? T("black") : i == 2 ? T("yellow") : NULL; break;
        case PANEL_BATCH_TEXT_SIZE: case PANEL_BATCH_DPI: snprintf(buffer, size, "%d", (int)lround(row->value)); return cstr(buffer);
        case PANEL_BATCH_EXISTS:  word = i == 0 ? T("replace it") : i == 1 ? T("leave it, skip") : i == 2 ? T("a new name (-1, -2...)") : NULL; break;
        case PANEL_BATCH_RESIZE:  snprintf(buffer, size, "%g", row->value); return cstr(buffer);
        case PANEL_BATCH_BOX_H: case PANEL_BATCH_SHARPEN: case PANEL_BATCH_BORDER: case PANEL_BATCH_MARK_OPACITY:
        case PANEL_BATCH_QUALITY: case PANEL_BATCH_START: case PANEL_BATCH_EFFECT_AMOUNT:
            snprintf(buffer, size, "%d", (int)lround(row->value)); return cstr(buffer);
        case PANEL_RESIZE_FILTER: word = i == 0 ? T("nearest (pixel art)") : i == 1 ? T("bilinear") : i == 2 ? T("bicubic") : i == 3 ? T("Lanczos-3 (sharpest)") : NULL; break;
        case PANEL_RESIZE_SHARPEN: case PANEL_RESIZE_DPI: snprintf(buffer, size, "%d", (int)lround(row->value)); return cstr(buffer);
        case PANEL_BLUR_KIND:    word = i >= 0 && i < 2 ? BLUR[i] : NULL; break;
        case PANEL_SHARPEN_KIND: word = i >= 0 && i < 2 ? SHARP[i] : NULL; break;
        case PANEL_FORMAT:       word = i >= 0 && i < RUBRAVIEW_EXPORT_FORMATS ? FORMAT[i] : NULL; break;
        case PANEL_PNG_DEPTH:    word = i >= 0 && i < 4 ? DEPTH[i] : NULL; break;
        case PANEL_JPEG_SUBSAMPLING: word = i >= 0 && i < 4 ? SUBSAMPLING[i] : NULL; break;
        case PANEL_PNG_FILTER:   word = i >= 0 && i < 7 ? PNG_FILTER[i] : NULL; break;
        case PANEL_TIFF_COMPRESSION: word = i >= 0 && i < 4 ? TIFF_COMP[i] : NULL; break;
        case PANEL_BMP_DEPTH:    word = i >= 0 && i < 3 ? BMP_BITS[i] : NULL; break;
        case PANEL_KEEP_DETAILS: word = i >= 0 && i < 3 ? KEEP[i] : NULL; break;
        case PANEL_WEBP_PRESET:  word = i >= 0 && i < 6 ? PRESET[i] : NULL; break;
        case PANEL_AVIF_CHROMA:  word = i == 1 ? T("4:4:4 (sharpest colour)") : T("4:2:0 (smallest)"); break;   /* 1.31.0 */
        case PANEL_AVIF_BITS:    word = i == 1 ? T("10 bits") : T("8 bits"); break;
        case PANEL_TARGET_KB:
            if (i <= 0) { word = T("off"); break; }
            snprintf(buffer, size, T("at most %d KB"), (int)i); return cstr(buffer);
        case PANEL_JPEG_RESTART:
            if (i == 0) { word = T("none"); break; }
            snprintf(buffer, size, T("every %d rows of blocks"), (int)i); return cstr(buffer);
        case PANEL_COLOURS: case PANEL_WEBP_ALPHA: snprintf(buffer, size, "%d", (int)lround(row->value)); return cstr(buffer);
        case PANEL_WEBP_NEAR:
            if (i >= 100) { word = T("off"); break; }
            snprintf(buffer, size, "%d", (int)i); return cstr(buffer);
        case PANEL_RESIZE:           snprintf(buffer, size, "%d", (int)lround(row->value)); return cstr(buffer);
        case PANEL_SHARPEN_THRESHOLD: snprintf(buffer, size, "%d", (int)lround(row->value)); return cstr(buffer);
        case PANEL_SHARPEN_RADIUS:   snprintf(buffer, size, "%.1f", row->value); return cstr(buffer);
        default: break;
    }
    if (word) { snprintf(buffer, size, "%s", word); return cstr(buffer); }
    snprintf(buffer, size, "[%d]", (int)i);
    return cstr(buffer);
}

/* A format's own choices (RV-108, RV-109, RV-140), for Export and the batch window alike. */
void export_more_rows(rubraview_panel_t *pn, const rubraview_export_options_t *o, uint32_t rows) {
    if (rows & RUBRAVIEW_EXPORT_ROW_TARGET) {   /* 1.17.0: the quality found for a size */
        rubraview_panel_add_slider(pn, PANEL_TARGET_KB, TU8("File size at most (KB, 0: off)"), (double)o->target_kb, 0.0, 20000.0, 10.0);
    }
    if (rows & RUBRAVIEW_EXPORT_ROW_PROGRESSIVE) {
        rubraview_panel_add_toggle(pn, PANEL_PROGRESSIVE, TU8("Progressive JPEG"), o->jpeg_progressive);
    }
    if (rows & RUBRAVIEW_EXPORT_ROW_JPEG_MORE) {   /* RV-140 */
        rubraview_panel_add_choice(pn, PANEL_JPEG_SUBSAMPLING, TU8("Colour subsampling"),
                                   (int32_t)o->jpeg_subsampling, 4);
        rubraview_panel_add_toggle(pn, PANEL_JPEG_GREY, TU8("Grey JPEG"), o->jpeg_grey);
        rubraview_panel_add_toggle(pn, PANEL_JPEG_OPTIMISE, TU8("Optimised tables (smaller)"), o->jpeg_optimise);
        rubraview_panel_add_slider(pn, PANEL_JPEG_RESTART, TU8("Restart markers"),
                                   (double)o->jpeg_restart_rows, 0.0, 64.0, 1.0);
    }
    /* RV-108 (§3.10): the two choices the writer had and no row gave. */
    if (rows & RUBRAVIEW_EXPORT_ROW_PNG_DEPTH) {
        rubraview_panel_add_choice(pn, PANEL_PNG_DEPTH, TU8("PNG colours"), (int32_t)o->png_depth, 4);
    }
    if (rows & RUBRAVIEW_EXPORT_ROW_PNG_MORE) {   /* RV-140 */
        rubraview_panel_add_choice(pn, PANEL_PNG_FILTER, TU8("PNG filter"), (int32_t)o->png_filter, 7);
        rubraview_panel_add_toggle(pn, PANEL_PNG_INTERLACE, TU8("Interlaced (Adam7)"), o->png_interlace);
    }
    if (rows & RUBRAVIEW_EXPORT_ROW_TIFF) {
        rubraview_panel_add_choice(pn, PANEL_TIFF_COMPRESSION, TU8("TIFF compression"),
                                   (int32_t)o->tiff_compression, 4);
        rubraview_panel_add_toggle(pn, PANEL_TIFF_CMYK, TU8("CMYK (for print)"), o->tiff_cmyk);   /* 1.17.0 */
    }
    if (rows & RUBRAVIEW_EXPORT_ROW_BMP) {
        rubraview_panel_add_choice(pn, PANEL_BMP_DEPTH, TU8("BMP bits"), (int32_t)o->bmp_depth, 3);
    }
    if (rows & RUBRAVIEW_EXPORT_ROW_COLOURS) {
        rubraview_panel_add_slider(pn, PANEL_COLOURS, TU8("Palette colours"),
                                   (double)rubraview_export_palette_size(o), 2.0, 256.0, 1.0);
        rubraview_panel_add_toggle(pn, PANEL_DITHER, TU8("Dither"), o->dither);
    }
    /* RV-109: WebP is written by the vendored libwebp. */
    if (rows & RUBRAVIEW_EXPORT_ROW_WEBP) {
        rubraview_panel_add_toggle(pn, PANEL_WEBP_LOSSLESS, TU8("WebP lossless"), o->webp_lossless);
        rubraview_panel_add_slider(pn, PANEL_WEBP_EFFORT, TU8("WebP effort"),
                                   (double)o->webp_effort, 0.0, 6.0, 1.0);
    }
    if (rows & RUBRAVIEW_EXPORT_ROW_WEBP) {   /* 1.17.0 */
        rubraview_panel_add_choice(pn, PANEL_WEBP_PRESET, TU8("WebP preset"), o->webp_preset, 6);
    }
    if (rows & RUBRAVIEW_EXPORT_ROW_WEBP_NEAR) {   /* RV-140 */
        rubraview_panel_add_slider(pn, PANEL_WEBP_NEAR, TU8("Near-lossless"),
                                   (double)o->webp_near_lossless, 0.0, 100.0, 1.0);
    }
    if (rows & RUBRAVIEW_EXPORT_ROW_AVIF) {   /* 1.31.0 */
        rubraview_panel_add_choice(pn, PANEL_AVIF_CHROMA, TU8("AVIF colour"), o->avif_chroma, 2);
        rubraview_panel_add_choice(pn, PANEL_AVIF_BITS, TU8("AVIF depth"), o->avif_bits == 10 ? 1 : 0, 2);
    }
    if (rows & RUBRAVIEW_EXPORT_ROW_WEBP_LOSSY) {
        rubraview_panel_add_slider(pn, PANEL_WEBP_ALPHA, TU8("Alpha quality"),
                                   (double)o->webp_alpha_quality, 0.0, 100.0, 1.0);
        rubraview_panel_add_toggle(pn, PANEL_WEBP_SHARP, TU8("Sharp colour edges (slower)"), o->webp_sharp_yuv);
    }
}

/* The same choices read back from a row; true when the rows that have use change with it. */
bool export_more_apply(rubraview_export_options_t *o, int32_t id, double v) {
    switch (id) {
        case PANEL_PNG_DEPTH:     o->png_depth = (rubraview_png_depth_t)(int32_t)v; return true;
        case PANEL_PROGRESSIVE:   o->jpeg_progressive = v > 0.5; break;
        case PANEL_WEBP_LOSSLESS: o->webp_lossless = v > 0.5; return true;
        case PANEL_WEBP_EFFORT:   o->webp_effort = (int32_t)v; break;
        case PANEL_JPEG_SUBSAMPLING: o->jpeg_subsampling = (rubraview_jpeg_subsampling_t)(int32_t)v; break;
        case PANEL_JPEG_GREY:     o->jpeg_grey = v > 0.5; break;
        case PANEL_JPEG_OPTIMISE: o->jpeg_optimise = v > 0.5; break;
        case PANEL_JPEG_RESTART:  o->jpeg_restart_rows = (int32_t)lround(v); break;
        case PANEL_PNG_FILTER:    o->png_filter = (rubraview_png_filter_t)(int32_t)v; break;
        case PANEL_PNG_INTERLACE: o->png_interlace = v > 0.5; break;
        case PANEL_COLOURS:       o->colours = (int32_t)lround(v); break;
        case PANEL_DITHER:        o->dither = v > 0.5; break;
        case PANEL_TIFF_COMPRESSION: o->tiff_compression = (rubraview_tiff_compression_t)(int32_t)v; break;
        case PANEL_BMP_DEPTH:     o->bmp_depth = (rubraview_bmp_depth_t)(int32_t)v; return true;
        case PANEL_WEBP_NEAR:     o->webp_near_lossless = (int32_t)lround(v); break;
        case PANEL_WEBP_ALPHA:    o->webp_alpha_quality = (int32_t)lround(v); break;
        case PANEL_WEBP_SHARP:    o->webp_sharp_yuv = v > 0.5; break;
        case PANEL_KEEP_DETAILS:  o->keep_details = (int32_t)v; break;   /* 1.17.0 */
        case PANEL_WEBP_PRESET:   o->webp_preset = (int32_t)v; break;
        case PANEL_TARGET_KB:     o->target_kb = (int32_t)lround(v); break;
        case PANEL_TIFF_CMYK:     o->tiff_cmyk = v > 0.5; break;
        case PANEL_AVIF_CHROMA:   o->avif_chroma = (int32_t)v; break;   /* 1.31.0 */
        case PANEL_AVIF_BITS:     o->avif_bits = (int32_t)v == 1 ? 10 : 8; break;
        default: break;
    }
    rubraview_export_clamp(o);
    return false;
}

void panel_open_export(app_state_t *app) {
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    app->panel = rubraview_panel_create(TU8("Export"), dpi);
    app->panel_is_export = true;
    app->panel_is_batch = false;
    app->panel_is_print = false;
    app->panel_is_convert = false;

    rubraview_panel_add_choice(&app->panel, PANEL_FORMAT, TU8("Format"), (int32_t)app->export_options.format, RUBRAVIEW_EXPORT_FORMATS);
    /* Only what the format chosen has use for (owner, 2026-10-15); "as the file" is the file's own. */
    int32_t page = current_page_index(app);
    proven_u8str_view_t name = page >= 0 && (size_t)page < page_count(app) ? page_display_name(app, (size_t)page) : PROVEN_LIT("");
    uint32_t rows = rubraview_export_rows_of(&app->export_options, name);   /* RV-140: the palette's rows only with one */
    if (rows & RUBRAVIEW_EXPORT_ROW_QUALITY) {
        rubraview_panel_add_slider(&app->panel, PANEL_QUALITY, TU8("Quality"),
                                   (double)app->export_options.jpeg_quality, 1.0, 100.0, 1.0);
    }
    if (rows & RUBRAVIEW_EXPORT_ROW_PNG_LEVEL) {
        rubraview_panel_add_slider(&app->panel, PANEL_PNG_LEVEL, TU8("PNG level"),
                                   (double)app->export_options.png_compression, 0.0, 9.0, 1.0);
    }
    export_more_rows(&app->panel, &app->export_options, rows);
    rubraview_panel_add_separator(&app->panel);
    /* §3.10 makes this an explicit tick, and it starts unticked: quietly
       discarding a photographer's metadata would be its own surprise. */
    /* 1.17.0: the source's details through the pixels written again; not shown while they are stripped */
    if (!app->export_options.privacy_clean) {
        rubraview_panel_add_choice(&app->panel, PANEL_KEEP_DETAILS, TU8("Camera details, comments"),
                                   app->export_options.keep_details, 3);
    }
    if (rows & RUBRAVIEW_EXPORT_ROW_PRIVACY) {
        rubraview_panel_add_toggle(&app->panel, PANEL_PRIVACY, TU8("Privacy clean"),
                                   app->export_options.privacy_clean);
        rubraview_panel_add_separator(&app->panel);
    }
    rubraview_panel_add_button(&app->panel, PANEL_EXPORT_NOW, TU8("Export"));

    app->panel.open = true;
    panel_relayout(app);
}

/* ---- RV-065: the adjust panel's live preview and histogram ---- */

void edit_preview_close(app_state_t *app) {
    if (app->edit_preview) rubraview_pal_texture_destroy(app->edit_preview);
    app->edit_preview = NULL;
    free(app->edit_small_mem);
    free(app->edit_work_mem);
    app->edit_small_mem = app->edit_work_mem = NULL;
    app->edit_small = (rubraview_pixbuf_t){0};
    app->edit_page = -1;
    app->edit_shown_valid = false;
    app->edit_hist_ok = false;
}

/* The page read once at full size, in memory of its own that is given back
   straight away (the app arena is a fixed 64 MB and a 12 MP photo is 48),
   shrunk to what the window shows, and kept while the panel is open. */
void edit_preview_open(app_state_t *app) {
    edit_preview_close(app);
    int32_t page = current_page_index(app);
    if (page < 0 || (size_t)page >= page_count(app) || !app->pages[page].loaded) return;
    int32_t w = real_w(&app->pages[page]), h = real_h(&app->pages[page]);
    if (w <= 0 || h <= 0) return;

    /* The owner (2026-10-18): Edit open, another picture: the undo list's steps go with
       the reader, onto it, shown there (its file is not touched until it is saved). */
    if (app->edit_steps.width > 0 && (app->edit_steps.width != w || app->edit_steps.height != h)) {
        if (app->edit_live) {   /* the change in hand ends where it was made */
            app->edit_live = false;
            if (app->edit_live_kind == RUBRAVIEW_STEP_EFFECT) app->effect_choice = RUBRAVIEW_EFFECT_NONE; else edit_values_fresh(app);
        }
        int32_t ow = 0, oh = 0, nw = 0, nh = 0;
        edit_cur_size(app, &ow, &oh);
        rubraview_edit_steps_rebase(&app->edit_steps, w, h);
        edit_cur_size(app, &nw, &nh);
        if (app->edit.crop_active) app->edit.crop = rubraview_edit_rect_rebase(app->edit.crop, ow, oh, nw, nh);
        app->edit.image_width = nw;
        app->edit.image_height = nh;
        app->edit_rev++;
        if (edit_panel_open(app)) panel_build_edit(app);
        edit_steps_build(app);
        if (app->sel_panel.open) edit_sel_build(app);
    }

    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    int32_t pw = 0, ph = 0;
    rubraview_edit_preview_size(&app->edit, win_w, win_h, &pw, &ph);
    if (pw <= 0 || ph <= 0) return;

    /* The whole picture when it fits in memory; otherwise — a page shown
       reduced, or a malloc refused — only the window's worth, read
       through the decoder's scaler, so a very large page still gets its
       preview. */
    bool within = app->pages[page].reduced;
    size_t big = 0;
    void *scratch_mem = NULL;
    if (!within) {
        big = picture_bytes((size_t)w, (size_t)h, 4u * 2u, 16u << 20);
        scratch_mem = malloc(big);
        within = scratch_mem == NULL;
    }
    if (within) {
        big = picture_bytes((size_t)pw, (size_t)ph, 4u * 3u, 16u << 20);
        scratch_mem = malloc(big);
        if (!scratch_mem) return;
    }
    proven_arena_t scratch = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)scratch_mem, .size = big });
    rubraview_pixbuf_t full = {0};
    proven_u8str_view_t path = app->source.pages[page].path;
    int32_t max_w = within ? pw : 0, max_h = within ? ph : 0;
    if (path.size > 0) {
        full = rubraview_pal_image_read_pixels_within(&scratch, path, NULL, 0, true, max_w, max_h);
    } else {
        rubraview_page_bytes_t bytes = { .ok = false };
        if (page_read_cost(app, (size_t)page) <= PAGE_JOB_MIN_COST) {
            AcquireSRWLockExclusive(&app->source_lock);
            bytes = rubraview_page_source_read(&scratch, &app->source, (size_t)page, MAX_PAGE_BYTES);
            ReleaseSRWLockExclusive(&app->source_lock);
        }
        if (bytes.ok && bytes.data.size > 0) {
            full = rubraview_pal_image_read_pixels_within(&scratch, PROVEN_LIT(""),
                                                          (const uint8_t*)bytes.data.ptr, bytes.data.size, true,
                                                          max_w, max_h);
        }
    }
    if (rubraview_pixbuf_is_valid(&full) && pw > 0 && ph > 0) {
        size_t small = picture_bytes((size_t)pw, (size_t)ph, 4u, 0);
        app->edit_small_bytes = small + (1u << 20);
        app->edit_work_bytes = small * 10u + (8u << 20);   /* commit's stages and the BGRA copy */
        app->edit_small_mem = malloc(app->edit_small_bytes);
        app->edit_work_mem = malloc(app->edit_work_bytes);
        if (app->edit_small_mem && app->edit_work_mem) {
            proven_arena_t keep = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)app->edit_small_mem,
                                                                          .size = app->edit_small_bytes });
            rubraview_pixbuf_t rgba = full.format == RUBRAVIEW_PIXFMT_RGBA8
                ? full : rubraview_pixbuf_convert(&scratch, &full, RUBRAVIEW_PIXFMT_RGBA8);
            app->edit_small = rubraview_pixbuf_resample(&keep, &rgba, pw, ph, RUBRAVIEW_FILTER_BILINEAR);
            if (rubraview_pixbuf_is_valid(&app->edit_small)) app->edit_page = page;
        }
    }
    free(scratch_mem);
    if (app->edit_page < 0) edit_preview_close(app);
}

/* Runs the session over the reduced copy when a slider or a curve changed
   since the last run: a new picture for the page and a new histogram. The
   crop is left out — it is shown by its own overlay, on the whole picture. */
static bool edit_extras_same(const rubraview_edit_session_t *a, const rubraview_edit_session_t *b) {
    return a->blur_sigma == b->blur_sigma && a->blur_box == b->blur_box &&
           a->sharpen_amount == b->sharpen_amount && a->sharpen_radius == b->sharpen_radius &&
           a->sharpen_threshold == b->sharpen_threshold && a->sharpen_laplacian == b->sharpen_laplacian;
}

void edit_preview_update(app_state_t *app) {
    if (!edit_panel_open(app)) return;
    if (!app->edit_small_mem || app->edit_page < 0) {
        /* Not had when the panel opened — the page still on its way, a page job under way:
           asked again, twice a second, until it is (owner, 2026-10-15: the picture shows the change). */
        double now = rubraview_pal_time_now_seconds();
        if (now - app->edit_preview_tried < 0.5 || app->edit_preview_tries >= 20) return;
        app->edit_preview_tried = now;
        app->edit_preview_tries++;
        edit_preview_open(app);
        if (!app->edit_small_mem || app->edit_page < 0) return;
    }
    if (!app->edit_work_mem) return;
    if (current_page_index(app) != app->edit_page) { edit_preview_close(app); return; }
    if (app->edit_shown_valid && app->edit_shown_rev == app->edit_rev &&
        memcmp(&app->edit_shown_params, &app->edit.params, sizeof(app->edit.params)) == 0 &&
        memcmp(app->edit_shown_curves, app->edit.curves, sizeof(app->edit.curves)) == 0 &&
        edit_extras_same(&app->edit_shown_session, &app->edit)) {
        return;
    }
    app->edit_shown_session = app->edit;
    app->edit_shown_rev = app->edit_rev;
    app->edit_shown_params = app->edit.params;
    memcpy(app->edit_shown_curves, app->edit.curves, sizeof(app->edit.curves));
    app->edit_shown_valid = true;

    /* The owner's edit: the steps in force, then the step being made on its area,
       on the small copy; room for each step's buffers. */
    size_t small_bytes = picture_bytes((size_t)app->edit_small.width, (size_t)app->edit_small.height, 4u, 0);
    size_t need = small_bytes * (10u + 4u * app->edit_steps.cursor) + (8u << 20);
    if (need > app->edit_work_bytes) {
        void *more = realloc(app->edit_work_mem, need);
        if (!more) return;
        app->edit_work_mem = more;
        app->edit_work_bytes = need;
    }
    proven_arena_t work = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)app->edit_work_mem,
                                                                  .size = app->edit_work_bytes });
    double ratio = app->edit_steps.width > 0 ? (double)app->edit_small.width / (double)app->edit_steps.width : 1.0;
    g_plugin_why = NULL;
    rubraview_pixbuf_t edited = rubraview_edit_steps_apply(&work, &app->edit_steps, &app->edit_small, ratio);
    if (g_plugin_why) {   /* RFC-0007, RFC-0009 C: a step that did not run is left out of the preview; said why */
        char said[320];
        snprintf(said, sizeof(said), T("a step did not run: %s"), rubraview_tr(g_plugin_why));   /* a plug-in's, or G'MIC's */
        osd_say(app, cstr(said));
        g_plugin_why = NULL;
    }
    if (!rubraview_pixbuf_is_valid(&edited)) return;
    /* Every change is a step already (owner, 2026-10-18): nothing is laid over the steps. */
    if (app->edit_compare) edited = rubraview_pixbuf_clone(&work, &app->edit_small);   /* the original, to compare */
    rubraview_histogram_compute(&edited, app->edit_hist[0], app->edit_hist[1], app->edit_hist[2], app->edit_hist[3]);
    app->edit_hist_ok = true;
    rubraview_pixbuf_t bgra = edited.format == RUBRAVIEW_PIXFMT_BGRA8
        ? edited : rubraview_pixbuf_convert(&work, &edited, RUBRAVIEW_PIXFMT_BGRA8);
    if (!rubraview_pixbuf_is_valid(&bgra)) return;
    if (!app->edit_preview || app->edit_preview_w != bgra.width || app->edit_preview_h != bgra.height) {
        if (app->edit_preview) rubraview_pal_texture_destroy(app->edit_preview);
        app->edit_preview = rubraview_pal_texture_create_bgra(app->renderer, bgra.width, bgra.height);
        app->edit_preview_w = bgra.width;
        app->edit_preview_h = bgra.height;
    }
    if (app->edit_preview) (void)rubraview_pal_texture_upload_bgra(app->edit_preview, bgra.pixels, bgra.stride);
}

/* The adjusted picture over the page, where the page is on screen. */
void draw_edit_preview(app_state_t *app) {
    if (!edit_panel_open(app) || !app->edit_preview || !app->edit_small_mem ||
        app->edit_page != current_page_index(app)) return;
    rubraview_pal_rect_t rect, page_rect;
    double scale = 1.0, page_scale = 1.0;
    bool changed = false;
    if (!edit_view_rect(app, &rect, &scale, &changed) || app->edit_preview_w <= 0 || app->edit_preview_h <= 0) return;
    if (app->edit_compare && page_screen_rect(app, &page_rect, &page_scale)) { rect = page_rect; changed = false; }   /* the original in its own place */
    /* Cropped or resized: the page as it was is covered, the result fitted in its place. */
    if (changed && page_screen_rect(app, &page_rect, &page_scale)) rubraview_pal_render_fill_rect(app->renderer, page_rect, COLOR_CANVAS, 0.0);
    rubraview_mat3x2_t place = rubraview_mat3x2_multiply(
        rubraview_mat3x2_scale(rect.width / (double)app->edit_preview_w, rect.height / (double)app->edit_preview_h),
        rubraview_mat3x2_translate(rect.x, rect.y));
    rubraview_pal_render_draw_texture(app->renderer, app->edit_preview, place, RUBRAVIEW_INTERP_LINEAR);
}

/* The histogram behind the curve: the active channel's, luminance for RGB
   and Luma. Scaled to the tallest bin that is not the first or the last,
   so a clipped white or black does not flatten everything else. */
static void draw_edit_histogram(app_state_t *app, rubraview_pal_rect_t box) {
    if (!app->edit_hist_ok) return;
    int ch = 3;
    uint32_t color = 0x90A0A0A0u;
    switch (app->edit.active_channel) {
        case RUBRAVIEW_EDIT_CHANNEL_RED:   ch = 0; color = 0x70FF5050u; break;
        case RUBRAVIEW_EDIT_CHANNEL_GREEN: ch = 1; color = 0x7050FF50u; break;
        case RUBRAVIEW_EDIT_CHANNEL_BLUE:  ch = 2; color = 0x805080FFu; break;
        default: break;
    }
    const uint32_t *hist = app->edit_hist[ch];
    uint32_t top = 1;
    for (int i = 1; i < 255; ++i) if (hist[i] > top) top = hist[i];
    double bin = box.width / 256.0;
    for (int i = 0; i < 256; ++i) {
        double f = (double)hist[i] / (double)top;
        if (f > 1.0) f = 1.0;
        double hgt = box.height * f;
        if (hgt < 0.5) continue;
        rubraview_pal_render_fill_rect(app->renderer,
            (rubraview_pal_rect_t){ box.x + bin * i, box.y + box.height - hgt, bin + 0.5, hgt }, color, 0.0);
    }
}

/* The owner's edit: the selection, on the picture — a rectangle or an
   ellipse, the rest a little dimmed, eight handles to size it by, its
   corner and size written by it. */
static void sel_handles(double x, double y, double w, double h, double out[8][2]) {
    const double pts[8][2] = { { x, y }, { x + w * 0.5, y }, { x + w, y }, { x + w, y + h * 0.5 },
                               { x + w, y + h }, { x + w * 0.5, y + h }, { x, y + h }, { x, y + h * 0.5 } };
    memcpy(out, pts, sizeof(pts));
}

static bool sel_screen_rect(app_state_t *app, double *x, double *y, double *w, double *h) {
    rubraview_pal_rect_t rect;
    double scale = 1.0;
    if (!app->edit.crop_active || !edit_view_rect(app, &rect, &scale, NULL)) return false;
    const rubraview_crop_rect_t *c = &app->edit.crop;
    *x = rect.x + c->x * scale; *y = rect.y + c->y * scale;
    *w = c->width * scale; *h = c->height * scale;
    return *w > 0.0 && *h > 0.0;
}

void draw_crop_overlay(app_state_t *app) {
    if (!edit_panel_open(app) || !app->edit.crop_active) return;
    rubraview_pal_rect_t rect;
    double scale = 1.0;
    if (!edit_view_rect(app, &rect, &scale, NULL)) return;
    double x, y, w, h;
    if (!sel_screen_rect(app, &x, &y, &w, &h)) return;
    const rubraview_crop_rect_t c = app->edit.crop;
    const uint32_t shade = 0x50000000u;
    rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ rect.x, rect.y, rect.width, y - rect.y }, shade, 0.0);
    rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ rect.x, y + h, rect.width, rect.y + rect.height - (y + h) }, shade, 0.0);
    rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ rect.x, y, x - rect.x, h }, shade, 0.0);
    rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ x + w, y, rect.x + rect.width - (x + w), h }, shade, 0.0);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    if (app->sel_shape == RUBRAVIEW_AREA_ELLIPSE) {
        /* An ellipse as dots round it, its box faint. */
        rubraview_pal_render_stroke_rect(app->renderer, (rubraview_pal_rect_t){ x, y, w, h }, 0x60FFFFFFu, 1.0, 0.0);
        int steps = 180;
        for (int k = 0; k < steps; ++k) {
            double a = 6.283185307179586 * k / steps;
            double ex = x + w * 0.5 + cos(a) * w * 0.5, ey = y + h * 0.5 + sin(a) * h * 0.5;
            rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ ex - 1.0 * dpi, ey - 1.0 * dpi, 2.0 * dpi, 2.0 * dpi },
                                           (k / 3) % 2 ? 0xFF000000u : 0xFFFFFFFFu, 0.0);
        }
    } else {
        rubraview_pal_render_stroke_rect(app->renderer, (rubraview_pal_rect_t){ x, y, w, h }, 0xFFFFFFFFu, 1.0, 0.0);
        rubraview_pal_render_stroke_rect(app->renderer, (rubraview_pal_rect_t){ x - 1.0, y - 1.0, w + 2.0, h + 2.0 }, 0x80000000u, 1.0, 0.0);
    }
    double hs[8][2];
    sel_handles(x, y, w, h, hs);
    double grip = 9.0 * dpi;
    for (int i = 0; i < 8; ++i) {
        rubraview_pal_rect_t g = { hs[i][0] - grip * 0.5, hs[i][1] - grip * 0.5, grip, grip };
        rubraview_pal_render_fill_rect(app->renderer, g, 0xFFFFFFFFu, 0.0);
        rubraview_pal_render_stroke_rect(app->renderer, g, 0xFF000000u, 1.0, 0.0);
    }
    char line[96];
    int n = snprintf(line, sizeof(line), "%d, %d   %d x %d", (int)c.x, (int)c.y, (int)c.width, (int)c.height);
    rubraview_pal_rect_t label = { x, y - 22.0 * dpi, 200.0 * dpi, 20.0 * dpi };
    if (label.y < rect.y) label.y = y + 2.0 * dpi;
    rubraview_pal_render_fill_rect(app->renderer, label, 0xC0000000u, 0.0);
    if (n > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n }, label, 13.0 * dpi, 0xFFFFFFFFu, RUBRAVIEW_TEXT_LEFT);
}

/* What a press on the selection takes: a handle (3..10), the inside to move it (2), or nothing (0). */
int32_t sel_hit(app_state_t *app, double px, double py) {
    double x, y, w, h;
    if (!edit_panel_open(app) || !sel_screen_rect(app, &x, &y, &w, &h)) return 0;
    double dpi = rubraview_pal_window_dpi_scale(app->window), reach = 8.0 * dpi;
    double hs[8][2];
    sel_handles(x, y, w, h, hs);
    for (int i = 0; i < 8; ++i) if (fabs(px - hs[i][0]) <= reach && fabs(py - hs[i][1]) <= reach) return 3 + i;
    if (px > x && px < x + w && py > y && py < y + h) return 2;
    return 0;
}

/* A drag of the selection: moved whole, or sized by the handle taken (the opposite side stays). */
void sel_drag_to(app_state_t *app, int32_t ix, int32_t iy) {
    rubraview_crop_rect_t r = app->sel_grab_rect;
    int32_t dx = ix - app->sel_grab_x, dy = iy - app->sel_grab_y;
    int32_t w = app->edit.image_width, h = app->edit.image_height;
    if (app->sel_grab == 2) {
        r.x += dx; r.y += dy;
        if (r.x < 0) r.x = 0;
        if (r.y < 0) r.y = 0;
        if (r.x + r.width > w) r.x = w - r.width;
        if (r.y + r.height > h) r.y = h - r.height;
        (void)rubraview_edit_crop_set(&app->edit, r.x, r.y, r.width, r.height);
    } else {
        int k = app->sel_grab - 3;   /* 0 top-left, then clockwise */
        int32_t x0 = r.x, y0 = r.y, x1 = r.x + r.width, y1 = r.y + r.height;
        bool left = k == 0 || k == 6 || k == 7, right = k == 2 || k == 3 || k == 4;
        bool top = k == 0 || k == 1 || k == 2, bottom = k == 4 || k == 5 || k == 6;
        if (left) x0 += dx;
        if (right) x1 += dx;
        if (top) y0 += dy;
        if (bottom) y1 += dy;
        if (app->edit.crop_ratio != RUBRAVIEW_CROP_FREE && (k == 0 || k == 2 || k == 4 || k == 6)) {
            /* A corner with a ratio: drawn again from the opposite corner, the ratio kept. */
            int32_t ax = left ? r.x + r.width : r.x, ay = top ? r.y + r.height : r.y;
            rubraview_edit_crop_drag(&app->edit, ax, ay, left ? x0 : x1, top ? y0 : y1);
        } else {
            if (x1 < x0 + 1) { int32_t s = x0; x0 = x1 - 1 < 0 ? 0 : x1 - 1; if (x0 == s) x1 = x0 + 1; }
            if (y1 < y0 + 1) { int32_t s = y0; y0 = y1 - 1 < 0 ? 0 : y1 - 1; if (y0 == s) y1 = y0 + 1; }
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 > w) x1 = w;
            if (y1 > h) y1 = h;
            if (x1 > x0 && y1 > y0) (void)rubraview_edit_crop_set(&app->edit, x0, y0, x1 - x0, y1 - y0);
        }
    }
    app->edit.crop_active = true;
    app->edit_rev++;
    app->edit_shown_valid = false;
}

/* §3.13's curve widget. The model is already there and tested — points
   sorted, endpoints kept, neighbours not crossed (rubraview_edit_curve_*);
   what was missing was a box to see it in. It sits under the workbench
   panel, 0,0 at the bottom left like every curve editor, and draws the
   line by sampling the same LUT the commit uses, so what is on screen is
   what will be applied. */
rubraview_pal_rect_t curve_widget_rect(const app_state_t *app) {
    if (!app->panel.open || app->panel_is_export || app->panel_is_batch || app->panel_is_print || app->panel_is_convert) {
        return (rubraview_pal_rect_t){0};
    }
    if (app->edit_window) {   /* RV-137: its room at the top of the Edit window's first column */
        int32_t i = rubraview_panel_find(&app->panel, PANEL_SP_CURVE);
        rubraview_rect_t r = i >= 0 ? rubraview_panel_row_rect(&app->panel, (size_t)i) : (rubraview_rect_t){0};
        double side = r.width < r.height - 8.0 ? r.width : r.height - 8.0;
        if (r.width <= 0.0 || side <= 0.0) return (rubraview_pal_rect_t){0};
        return (rubraview_pal_rect_t){ r.x, r.y + 4.0, side, side };
    }
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double side = 168.0 * dpi;
    double gap = 8.0 * dpi;
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);

    /* Under the panel when there is room for the whole box; beside it
       otherwise, which is what a short window gives (measured on the VM:
       under a full-height workbench the box ran off the bottom edge and
       lay over the panel's last rows). */
    double x = app->panel.bounds.x;
    double y = app->panel.bounds.y + app->panel.bounds.height + gap;
    if (y + side + gap > (double)win_h) {
        x = app->panel.bounds.x - side - gap;
        y = app->panel.bounds.y;
    }
    if (x < gap) x = gap;
    if (y + side > (double)win_h) y = (double)win_h - side - gap;
    if (y < 0.0) y = 0.0;
    if (x + side > (double)win_w) x = (double)win_w - side - gap;
    if (x < 0.0) x = 0.0;
    return (rubraview_pal_rect_t){ x, y, side, side };
}

/* The channel button in the curve box's top-left corner. */
static rubraview_pal_rect_t curve_channel_rect(rubraview_pal_rect_t box, double dpi) {
    return (rubraview_pal_rect_t){ box.x + 3.0 * dpi, box.y + 3.0 * dpi, 58.0 * dpi, 20.0 * dpi };
}

void draw_curve_widget(app_state_t *app) {
    rubraview_pal_rect_t box = curve_widget_rect(app);
    if (box.width <= 0.0) return;

    double dpi = rubraview_pal_window_dpi_scale(app->window);
    rubraview_pal_render_fill_rect(app->renderer, box, COLOR_BOX_FILL, 3.0);
    rubraview_pal_render_stroke_rect(app->renderer, box, COLOR_BOX_BORDER, 1.0, 3.0);
    draw_edit_histogram(app, box);   /* RV-065: what the adjusted picture holds, behind its curve */

    /* quarters, so a point's height can be judged without a ruler */
    for (int i = 1; i < 4; ++i) {
        double t = (double)i / 4.0;
        rubraview_pal_render_fill_rect(app->renderer,
            (rubraview_pal_rect_t){ box.x + box.width * t, box.y, 1.0, box.height }, 0x30FFFFFFu, 0.0);
        rubraview_pal_render_fill_rect(app->renderer,
            (rubraview_pal_rect_t){ box.x, box.y + box.height * t, box.width, 1.0 }, 0x30FFFFFFu, 0.0);
    }

    const rubraview_edit_curve_t *curve = &app->edit.curves[app->edit.active_channel];
    uint8_t lut[256];
    rubraview_curve_build_lut(lut, curve->points, curve->point_count);

    double dot = 2.0 * dpi;
    for (int i = 0; i < 256; ++i) {
        double px = box.x + box.width * ((double)i / 255.0);
        double py = box.y + box.height * (1.0 - (double)lut[i] / 255.0);
        rubraview_pal_render_fill_rect(app->renderer,
            (rubraview_pal_rect_t){ px - dot * 0.5, py - dot * 0.5, dot, dot }, 0xFFFFFFFFu, 0.0);
    }

    double grip = 7.0 * dpi;
    for (size_t i = 0; i < curve->point_count; ++i) {
        double px = box.x + box.width * ((double)curve->points[i].x / 255.0);
        double py = box.y + box.height * (1.0 - (double)curve->points[i].y / 255.0);
        rubraview_pal_render_fill_rect(app->renderer,
            (rubraview_pal_rect_t){ px - grip * 0.5, py - grip * 0.5, grip, grip }, 0xFFFFD000u, 0.0);
    }

    /* The label is the channel button: a click steps forward, a right
       click back (T093). */
    const char *const CHANNEL_NAMES[5] = { T("RGB >"), T("Red >"), T("Green >"), T("Blue >"), T("Luma >") };
    rubraview_pal_rect_t tab = curve_channel_rect(box, dpi);
    rubraview_pal_render_fill_rect(app->renderer, tab, 0xC0202020u, 3.0 * dpi);
    rubraview_pal_render_stroke_rect(app->renderer, tab, 0x80FFFFFFu, 1.0, 3.0 * dpi);
    rubraview_pal_rect_t label = { tab.x + 5.0 * dpi, tab.y + 2.0 * dpi, tab.width - 8.0 * dpi, tab.height - 4.0 * dpi };
    rubraview_pal_render_draw_text(app->renderer, cstr(CHANNEL_NAMES[app->edit.active_channel]),
                                   label, 12.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
}

/* A press inside the widget: the nearest point, or a new one. The right
   button takes an interior point off, which is how every curve editor
   does it and what rubraview_edit_curve_remove is for. */
/* Inside the curve box? The crop drag asks before it claims a press. */
bool point_in_curve_widget(const app_state_t *app, double px, double py) {
    rubraview_pal_rect_t box = curve_widget_rect(app);
    return box.width > 0.0 && px >= box.x && px <= box.x + box.width &&
           py >= box.y && py <= box.y + box.height;
}

bool curve_widget_press(app_state_t *app, double px, double py, bool remove) {
    rubraview_pal_rect_t box = curve_widget_rect(app);
    if (box.width <= 0.0) return false;
    if (px < box.x || px > box.x + box.width || py < box.y || py > box.y + box.height) return false;

    rubraview_pal_rect_t tab = curve_channel_rect(box, rubraview_pal_window_dpi_scale(app->window));
    if (px >= tab.x && px <= tab.x + tab.width && py >= tab.y && py <= tab.y + tab.height) {
        rubraview_edit_cycle_channel(&app->edit, remove ? -1 : 1);
        app->curve_dragging = -1;
        return true;
    }

    float x = (float)((px - box.x) / box.width * 255.0);
    float y = (float)((1.0 - (py - box.y) / box.height) * 255.0);
    int32_t index = rubraview_edit_curve_grab(&app->edit, x, y, 10.0f);
    if (index < 0) return true;   /* inside the box, but the curve is full */
    if (remove) {
        rubraview_edit_curve_remove(&app->edit, index);
        app->curve_dragging = -1;
        edit_live_update(app, RUBRAVIEW_STEP_ADJUST);
        return true;
    }
    rubraview_edit_curve_move(&app->edit, index, x, y);
    app->curve_dragging = index;
    edit_live_update(app, RUBRAVIEW_STEP_ADJUST);
    return true;
}

void curve_widget_drag(app_state_t *app, double px, double py) {
    rubraview_pal_rect_t box = curve_widget_rect(app);
    if (box.width <= 0.0 || app->curve_dragging < 0) return;
    float x = (float)((px - box.x) / box.width * 255.0);
    float y = (float)((1.0 - (py - box.y) / box.height) * 255.0);
    rubraview_edit_curve_move(&app->edit, app->curve_dragging, x, y);
    edit_live_update(app, RUBRAVIEW_STEP_ADJUST);
}

/* ---- RV-137: Edit in a window of its own (owner, 2026-10-18) ----
   "edit 창은 메인 창 내부가 아니라 설정창처럼 외부에 독립된 모달리스 창으로":
   the panel, the curve, the selection's numbers and the change list live in
   a window beside the viewer; the picture, its selection and the preview
   stay in the viewer. Laid out by its width in one, two or three columns.
   While it is open the viewer's own paths do not ask these boxes: their
   rectangles are in this window's pixels. */

#define EDIT_BACKGROUND 0xFF1A1A1Au

static bool edit_windowed(const app_state_t *app) {
    return app->edit_window != NULL && edit_panel_open(app);
}

static rubraview_rect_t edit_room(const app_state_t *app, int32_t id) {
    int32_t i = rubraview_panel_find(&app->panel, id);
    return i >= 0 ? rubraview_panel_row_rect(&app->panel, (size_t)i) : (rubraview_rect_t){0};
}

void edit_window_layout(app_state_t *app) {
    if (!edit_windowed(app)) return;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->edit_window, &w, &h);
    double dpi = rubraview_pal_window_dpi_scale(app->edit_window);
    int32_t mode = (double)w / dpi < 560.0 ? 0 : (double)w / dpi < 940.0 ? 1 : 2;
    if (mode != app->edit_mode) {   /* another width band: the rows in its order */
        app->edit_mode = mode;
        panel_build_edit(app);      /* lays itself out again through here */
        return;
    }
    double head = app->panel.row_height;
    if (app->sel_panel.open)
        rubraview_panel_set_spacer(&app->panel, PANEL_SP_SEL, head + (double)app->sel_panel.row_count * app->sel_panel.row_height + app->sel_panel.padding * 2.0);
    size_t shown = app->edit_steps.count < 3 ? 3 : app->edit_steps.count > 8 ? 8 : app->edit_steps.count;
    rubraview_panel_set_spacer(&app->panel, PANEL_SP_CHANGES, head + (double)shown * app->changes.row_height + 4.0 * dpi);
    rubraview_panel_layout_columns(&app->panel, (rubraview_rect_t){ 0.0, 0.0, (double)w, (double)h }, mode + 1, app->edit_firsts);
    rubraview_rect_t r = edit_room(app, PANEL_SP_SEL);
    if (app->sel_panel.open) {
        if (r.width > 0.0) rubraview_panel_layout_columns(&app->sel_panel, (rubraview_rect_t){ r.x, r.y + head, r.width, r.height - head }, 1, NULL);
        else app->sel_panel.bounds = (rubraview_rect_t){ 0.0, -1.0e6, 0.0, 0.0 };
    }
    r = edit_room(app, PANEL_SP_CHANGES);
    app->changes.x = r.x;
    app->changes.y = r.y + head;
    app->changes.width = r.width;
    app->changes.height = r.width > 0.0 ? r.height - head : 0.0;
}

void edit_window_close(app_state_t *app) {
    if (!app->edit_window) return;
    rubraview_pal_window_get_frame(app->edit_window, &app->edit_frame[0], &app->edit_frame[1], &app->edit_frame[2], &app->edit_frame[3]);
    if (app->edit_renderer) rubraview_pal_render_destroy(app->edit_renderer);
    rubraview_pal_window_destroy(app->edit_window); window_block_give(app, &app->edit_block);
    app->edit_renderer = NULL;
    app->edit_window = NULL;
    app->edit_text_on = false;
    app->sel_panel.open = false;
}

void edit_window_show(app_state_t *app) {
    if (app->edit_window || !edit_panel_open(app)) return;
    rubraview_window_config_t config = { .title = T("Rubraview Edit"), .width = 980, .height = 640, .owner = app->window };
    app->edit_window = rubraview_pal_window_create(window_block_take(app, &app->edit_block), &config);
    if (!app->edit_window) return;   /* the panel stays in the viewer, as before */
    if (app->edit_frame[2] > 0 && app->edit_frame[3] > 0) {
        rubraview_pal_window_set_frame(app->edit_window, app->edit_frame[0], app->edit_frame[1], app->edit_frame[2], app->edit_frame[3]);
    } else {   /* beside the viewer's right edge, or over it when the screen has no room */
        int32_t vx = 0, vy = 0, vw = 0, vh = 0, ex = 0, ey = 0, ew = 0, eh = 0, sw = 0, sh = 0;
        if (rubraview_pal_window_get_frame(app->window, &vx, &vy, &vw, &vh) && rubraview_pal_window_get_frame(app->edit_window, &ex, &ey, &ew, &eh)) {
            int32_t x = vx + vw + 8;
            if (rubraview_pal_window_monitor_size(app->window, &sw, &sh) && sw > 0 && x + ew > sw) x = sw - ew > 0 ? sw - ew : 0;
            rubraview_pal_window_set_frame(app->edit_window, x, vy, ew, eh);
        }
    }
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->edit_window, &w, &h);
    app->edit_renderer = rubraview_pal_render_create(window_block_arena(app, &app->edit_block), rubraview_pal_window_native_handle(app->edit_window), w, h);
    if (!app->edit_renderer) {
        rubraview_pal_window_destroy(app->edit_window); window_block_give(app, &app->edit_block);
        app->edit_window = NULL;
        return;
    }
    ui_font_apply_all(app);
    double dpi = rubraview_pal_window_dpi_scale(app->edit_window);
    app->changes = rubraview_listwin_create(dpi);
    app->changes.editable = true;     /* each row's box, x and arrows */
    app->changes.own_window = true;   /* no X or R of its own */
    app->changes.title_height = 0.0;
    app->changes.bar_height = 0.0;
    app->changes.placed = app->changes.open = true;
    memset(app->change_checked, 0, sizeof(app->change_checked));
    app->change_press = -1;
    app->change_dragging = false;
    app->steps_panel.open = false;    /* the change list is in the window */
    app->edit_mode = -1;
    edit_sel_build(app);              /* the selection's numbers, always there */
    panel_build_edit(app);
    app->edit_dirty = true;
}

/* The change list: a press on a row's box, x, arrows or name. */
static void changes_changed(app_state_t *app) {
    memset(app->change_checked, 0, sizeof(app->change_checked));
    edit_steps_changed(app);
}

void changes_remove_checked(app_state_t *app) {
    edit_live_close(app);
    bool any = false;
    for (size_t i = 0; i < app->edit_steps.cursor; ++i) any = any || app->change_checked[i];
    if (!any) { osd_say(app, TU8("tick the edits to remove first")); return; }
    size_t n = rubraview_edit_steps_remove(&app->edit_steps, app->change_checked);
    changes_changed(app);
    char said[96];
    int m = snprintf(said, sizeof(said), T("%zu edits removed"), n);
    if (m > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)said, .size = (size_t)m });
}

static bool changes_press(app_state_t *app, double x, double y) {
    rubraview_listwin_t *lw = &app->changes;
    if (lw->height <= 0.0 || !rubraview_rect_contains((rubraview_rect_t){ lw->x, lw->y, lw->width, lw->height }, x, y)) return false;
    size_t count = app->edit_steps.count, index = 0;
    switch (rubraview_listwin_hit(lw, x, y, count, &index)) {
        case RUBRAVIEW_LISTWIN_ROW_CHECK:
            if (index < RUBRAVIEW_EDIT_MAX_STEPS) app->change_checked[index] = !app->change_checked[index];
            break;
        case RUBRAVIEW_LISTWIN_ROW_REMOVE: {
            if (index >= count) break;
            edit_live_close(app);
            bool gone[RUBRAVIEW_EDIT_MAX_STEPS] = { false };
            gone[index] = true;
            if (index < app->edit_steps.cursor && rubraview_edit_steps_remove(&app->edit_steps, gone)) changes_changed(app);
            break;
        }
        case RUBRAVIEW_LISTWIN_ROW_UP:
        case RUBRAVIEW_LISTWIN_ROW_DOWN: {
            bool up = rubraview_listwin_hit(lw, x, y, count, &index) == RUBRAVIEW_LISTWIN_ROW_UP;
            edit_live_close(app);
            if (rubraview_edit_steps_move(&app->edit_steps, index, up)) changes_changed(app);
            break;
        }
        case RUBRAVIEW_LISTWIN_ROW:   /* held: dragged, it moves (owner, 2026-10-18); let go where it was, the picture after it */
            if (index < count) {
                app->change_press = (int32_t)index;
                app->change_press_y = y;
                app->change_dragging = false;
            }
            break;
        default: break;
    }
    return true;
}

/* Where a change carried to `y` would go: the row it goes before (count: at the end). */
static size_t changes_drop_at(const app_state_t *app, double y) {
    size_t in_force = app->edit_steps.cursor;
    size_t at = rubraview_listwin_insert_index(&app->changes, y, app->edit_steps.count);
    return at > in_force ? in_force : at;   /* among the changes in force */
}

static void changes_motion(app_state_t *app, double y) {
    if (app->change_press < 0) return;
    if (!app->change_dragging && fabs(y - app->change_press_y) >= 6.0 * rubraview_pal_window_dpi_scale(app->edit_window)) app->change_dragging = true;
}

static void changes_release(app_state_t *app, double y) {
    if (app->change_press < 0) return;
    size_t from = (size_t)app->change_press;
    bool dragged = app->change_dragging;
    app->change_press = -1;
    app->change_dragging = false;
    edit_live_close(app);
    if (!dragged) {   /* a click: the picture as it stood after this change */
        if (rubraview_edit_steps_go(&app->edit_steps, from + 1)) edit_steps_changed(app);
        return;
    }
    size_t at = changes_drop_at(app, y), to = at > from ? at - 1 : at;
    if (from < app->edit_steps.cursor && rubraview_edit_steps_move_to(&app->edit_steps, from, to)) changes_changed(app);
}

static void draw_changes(app_state_t *app) {
    rubraview_listwin_t *lw = &app->changes;
    rubraview_renderer_t *rr = app->renderer;
    size_t count = app->edit_steps.count;
    if (lw->height <= 0.0) return;
    rubraview_listwin_scroll(lw, 0, count);
    size_t rows = rubraview_listwin_rows_visible(lw);
    double small = lw->row_height * 0.5, text_x = rubraview_listwin_row_text_x(lw);
    for (size_t i = lw->first; i < count && i < lw->first + rows; ++i) {
        rubraview_rect_t r0 = rubraview_listwin_row_rect(lw, i, count);
        bool in_force = i < app->edit_steps.cursor;
        if (i + 1 == app->edit_steps.cursor) rubraview_pal_render_fill_rect(rr, (rubraview_pal_rect_t){ r0.x, r0.y, r0.width, r0.height }, 0xFF2B4A6Fu, 2.0);
        else if (rubraview_rect_contains(r0, app->pointer_x, app->pointer_y)) rubraview_pal_render_fill_rect(rr, (rubraview_pal_rect_t){ r0.x, r0.y, r0.width, r0.height }, COLOR_TILE_FILL, 2.0);
        list_check_mark(rr, rubraview_listwin_row_part_rect(lw, i, count, RUBRAVIEW_LISTWIN_ROW_CHECK), app->change_checked[i], false);
        list_button(rr, app->pointer_x, app->pointer_y, rubraview_listwin_row_part_rect(lw, i, count, RUBRAVIEW_LISTWIN_ROW_REMOVE), "x", small, false, COLOR_TEXT);
        list_button(rr, app->pointer_x, app->pointer_y, rubraview_listwin_row_part_rect(lw, i, count, RUBRAVIEW_LISTWIN_ROW_UP), "\xE2\x96\xB2", small * 0.8, false,
                    i > 0 && in_force ? COLOR_TEXT : COLOR_BOX_BORDER);
        list_button(rr, app->pointer_x, app->pointer_y, rubraview_listwin_row_part_rect(lw, i, count, RUBRAVIEW_LISTWIN_ROW_DOWN), "\xE2\x96\xBC", small * 0.8, false,
                    i + 1 < app->edit_steps.cursor ? COLOR_TEXT : COLOR_BOX_BORDER);
        char line[96], fit[200];
        proven_u8str_view_t words = edit_step_words(&app->edit_steps.steps[i], i + 1, line, sizeof(line));
        rubraview_pal_rect_t t = { text_x, r0.y, r0.x + r0.width - text_x - 4.0, r0.height };
        rubraview_pal_render_draw_text(rr, files_fit_text(fit, sizeof(fit), words, t.width, small), t, small,
                                       in_force ? COLOR_TEXT : COLOR_BOX_BORDER, RUBRAVIEW_TEXT_LEFT);   /* undone: dimmed */
    }
    if (app->change_dragging && app->change_press >= 0) {   /* where the carried change would go */
        size_t at = changes_drop_at(app, app->pointer_y);
        double y = at < count ? rubraview_listwin_row_rect(lw, at, count).y
                              : rubraview_listwin_row_rect(lw, count - 1, count).y + lw->row_height;
        if (y >= lw->y - 1.0 && y <= lw->y + lw->height + 1.0)
            rubraview_pal_render_fill_rect(rr, (rubraview_pal_rect_t){ lw->x, y - 1.0, lw->width, 2.0 }, 0xFF5B9BD5u, 0.0);
    }
    if (count == 0) {
        rubraview_pal_render_draw_text(rr, TU8("no edit yet: move a slider or choose an effect"),
                                       (rubraview_pal_rect_t){ lw->x, lw->y, lw->width, lw->row_height }, small, COLOR_BOX_BORDER, RUBRAVIEW_TEXT_LEFT);
    }
}

static void edit_heading(app_state_t *app, int32_t id, const char *words, size_t number) {
    rubraview_rect_t r = edit_room(app, id);
    if (r.width <= 0.0) return;
    char text[96];
    int n = number ? snprintf(text, sizeof(text), "%s   %zu", words, number) : snprintf(text, sizeof(text), "%s", words);
    if (n > 0) rubraview_pal_render_draw_text(app->renderer, (proven_u8str_view_t){ (const proven_byte_t *)text, (size_t)n }, (rubraview_pal_rect_t){ r.x, r.y, r.width, app->panel.row_height },
                                              app->panel.row_height * 0.5, SETTINGS_ACCENT, RUBRAVIEW_TEXT_LEFT);
}

/* What the window shows, cheaply: drawn again when it changes (or twice a second, for the histogram). */
static uint64_t edit_window_signature(const app_state_t *app) {
    uint64_t h = 1469598103934665603ull;
    double v[] = { (double)app->edit_rev, (double)app->edit_steps.count, (double)app->edit_steps.cursor, (double)app->edit.crop_active,
                   app->edit.crop.x, app->edit.crop.y, app->edit.crop.width, app->edit.crop.height, (double)app->panel.row_count,
                   (double)app->sel_panel.row_count, (double)app->edit.active_channel };
    for (size_t i = 0; i < sizeof(v) / sizeof(v[0]); ++i) { uint64_t b; memcpy(&b, &v[i], 8); h = (h ^ b) * 1099511628211ull; }
    for (size_t i = 0; i < app->panel.row_count; ++i) { uint64_t b; memcpy(&b, &app->panel.rows[i].value, 8); h = (h ^ b) * 1099511628211ull; }
    return h;
}

void draw_curve_widget(app_state_t *app);
void draw_edit_window(app_state_t *app) {
    if (!app->edit_window) return;
    if (!edit_panel_open(app)) { edit_window_close(app); return; }
    double now = rubraview_pal_time_now_seconds();
    uint64_t sig = edit_window_signature(app);
    if (!app->edit_dirty && sig == app->edit_sig && now - app->edit_drawn_at < 0.5) return;
    app->edit_dirty = false;
    app->edit_sig = sig;
    app->edit_drawn_at = now;
    edit_window_layout(app);
    /* drawn by the viewer's own drawing code, on this window's renderer with this window's pointer */
    rubraview_renderer_t *viewer = app->renderer;
    double vx = app->pointer_x, vy = app->pointer_y;
    app->renderer = app->edit_renderer;
    app->pointer_x = app->edit_px;
    app->pointer_y = app->edit_py;
    rubraview_pal_render_begin(app->renderer, EDIT_BACKGROUND);
    draw_panel_of(app, &app->panel, true);
    if (app->sel_panel.open && edit_room(app, PANEL_SP_SEL).width > 0.0) {
        edit_heading(app, PANEL_SP_SEL, T("Selection"), 0);
        draw_panel_of(app, &app->sel_panel, true);
    }
    edit_heading(app, PANEL_SP_CHANGES, T("Edits (Ctrl+Z undoes, Ctrl+Y redoes)"), app->edit_steps.count);
    draw_changes(app);
    draw_curve_widget(app);
    if (app->rename_active && (app->rename_is_value || app->rename_is_apply)) {
        int32_t ww = 0, wh = 0;
        rubraview_pal_window_get_size(app->edit_window, &ww, &wh);
        draw_rename_box(app, (double)ww, (double)wh, rubraview_pal_window_dpi_scale(app->edit_window));
    }
    if (app->px_edit_open) {   /* a value typed: its box where it was asked for */
        int32_t ww = 0, wh = 0;
        rubraview_pal_window_get_size(app->edit_window, &ww, &wh);
        draw_px_edit_box(app, (double)ww, (double)wh, rubraview_pal_window_dpi_scale(app->edit_window));
    }
    draw_chooser(app, app->renderer, CHOOSE_IN_EDIT, app->edit_px, app->edit_py);
    bool ok = rubraview_pal_render_end(app->renderer);
    app->renderer = viewer;
    app->pointer_x = vx;
    app->pointer_y = vy;
    if (!ok) { edit_window_close(app); edit_window_show(app); }   /* its device went: made again */
}

size_t edit_pump(app_state_t *app) {
    size_t handled = 0;
    rubraview_window_event_t event;
    while (app->edit_window && rubraview_pal_window_poll_event(app->edit_window, &event)) {
        handled++;
        app->edit_dirty = true;
        if (!edit_panel_open(app)) { edit_window_close(app); break; }
        edit_window_layout(app);
        switch (event.kind) {
            case RUBRAVIEW_WINDOW_EVENT_CLOSE:
                panel_close(app);   /* its X is Edit's */
                return handled;
            case RUBRAVIEW_WINDOW_EVENT_RESIZE:
                rubraview_pal_render_resize(app->edit_renderer, event.resize.width, event.resize.height);
                edit_window_layout(app);
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE:
                app->edit_px = event.mouse.x;
                app->edit_py = event.mouse.y;
                if (app->change_press >= 0) { changes_motion(app, event.mouse.y); break; }
                if (app->curve_dragging >= 0) { curve_widget_drag(app, event.mouse.x, event.mouse.y); break; }
                if (app->panel.active_row >= 0) {
                    int32_t dragged = -1;
                    if (rubraview_panel_drag(&app->panel, event.mouse.x, event.mouse.y, &dragged) == RUBRAVIEW_PANEL_VALUE_CHANGED) panel_apply_row(app, dragged);
                    break;
                }
                if (app->sel_panel.active_row >= 0) {
                    int32_t dragged = -1;
                    if (rubraview_panel_drag(&app->sel_panel, event.mouse.x, event.mouse.y, &dragged) == RUBRAVIEW_PANEL_VALUE_CHANGED) edit_sel_apply_row(app, dragged);
                }
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_LEAVE:
                app->edit_px = app->edit_py = -1.0;
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN:
                if (event.mouse.button == RUBRAVIEW_MOUSE_RIGHT) { (void)curve_widget_press(app, event.mouse.x, event.mouse.y, true); break; }
                if (chooser_press(app, CHOOSE_IN_EDIT, event.mouse.x, event.mouse.y)) break;   /* 1.24.0 */
                if ((app->rename_is_value || app->rename_is_apply) && rename_box_press(app, event.mouse.x, event.mouse.y)) break;
                if (event.mouse.button != RUBRAVIEW_MOUSE_LEFT) break;
                if (changes_press(app, event.mouse.x, event.mouse.y)) break;
                if (edit_side_press(app, event.mouse.x, event.mouse.y)) break;
                (void)panel_handle_press(app, event.mouse.x, event.mouse.y);
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_UP:
                changes_release(app, event.mouse.y);
                app->curve_dragging = -1;
                rubraview_panel_release(&app->panel);
                rubraview_panel_release(&app->sel_panel);
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_WHEEL:
                if (chooser_wheel(app, CHOOSE_IN_EDIT, event.mouse.wheel_delta)) break;
                if (app->changes.height > 0.0 && rubraview_rect_contains((rubraview_rect_t){ app->changes.x, app->changes.y, app->changes.width, app->changes.height }, event.mouse.x, event.mouse.y)) {
                    rubraview_listwin_scroll(&app->changes, event.mouse.wheel_delta > 0 ? -1 : 1, app->edit_steps.count);
                    break;
                }
                rubraview_panel_scroll_at(&app->panel, event.mouse.x, -event.mouse.wheel_delta * app->panel.row_height * 3.0);
                edit_window_layout(app);
                break;
            case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN:
                if (chooser_key(app, CHOOSE_IN_EDIT, event.key.combo)) break;
                /* Esc leaves Edit here (the viewer's Esc ends the program); the rest are the viewer's keys. */
                if (key_is(event.key.combo, "Escape") && app->sel_mode) { sel_mode_set(app, false); panel_build_edit(app); break; }
                if (key_is(event.key.combo, "Escape") && !app->rename_active && !app->px_edit_open) { panel_close(app); return handled; }
                dispatch_key(app, event.key.combo);
                break;
            case RUBRAVIEW_WINDOW_EVENT_TEXT:
            case RUBRAVIEW_WINDOW_EVENT_COMPOSITION:
                rename_text(app, &event);   /* a name or a number typed while this window has the keys */
                break;
            default:
                break;
        }
    }
    bool typing = app->rename_active || app->px_edit_open;
    if (app->edit_window && typing != app->edit_text_on) {
        rubraview_pal_window_text_input(app->edit_window, typing);
        app->edit_text_on = typing;
    }
    return handled;
}
