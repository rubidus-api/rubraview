/*
 * A page inside an archive deleted, renamed or saved, and the save jobs that write a page over its
 * original (D-88, R149; RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* ---- D-88 (R149): a page inside an archive deleted, renamed, saved ---- */

/* D-89: how what is packed again is packed — the settings the convert
   panel shows. 0 stores; 1..9 packs. */
static int arc_level(const app_state_t *app) {
    if (lround(rubraview_settings_get(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("convert_method"))) == 1) return 0;
    int level = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("convert_level")));
    return level < 1 ? 1 : level > 9 ? 9 : level;
}

rubraview_arcedit_target_t arc_convert_target(const app_state_t *app) {
    return lround(rubraview_settings_get(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("convert_format"))) == 1
        ? RUBRAVIEW_ARCEDIT_7Z : RUBRAVIEW_ARCEDIT_ZIP;
}

/* D-89: the question of what the archive becomes, as a panel: the format,
   stored or packed, how hard. Its button writes; closing it leaves the book. */
static void panel_open_convert(app_state_t *app) {
    if (app->panel.open) panel_close(app);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    app->panel = rubraview_panel_create(TU8("Write the archive again"), dpi);
    app->panel_is_export = false;
    app->panel_is_batch = false;
    app->panel_is_print = false;
    app->panel_is_convert = true;
    rubraview_panel_add_choice(&app->panel, PANEL_CONV_FORMAT, TU8("Becomes"),
                               arc_convert_target(app) == RUBRAVIEW_ARCEDIT_7Z ? 1 : 0, 2);
    rubraview_panel_add_choice(&app->panel, PANEL_CONV_METHOD, TU8("Method"), arc_level(app) == 0 ? 1 : 0, 2);
    rubraview_panel_add_slider(&app->panel, PANEL_CONV_LEVEL, TU8("Level"),
                               rubraview_settings_get(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("convert_level")), 1.0, 9.0, 1.0);
    rubraview_panel_add_separator(&app->panel);
    rubraview_panel_add_button(&app->panel, PANEL_CONV_NOW, TU8("Write (Enter)"));
    app->panel.open = true;
    panel_relayout(app);
    app->arc_ask = ARC_ASK_FORMAT;
    osd_say(app, TU8("this archive cannot be changed as it is: it is written again, the old file goes to the Recycle Bin"));
}

/* D-89: the file an archive was before its last edit in place is kept
   beside it until the next edit or the end of the program. */
void arc_undo_drop(app_state_t *app) {
    if (app->arc_undo_backup[0]) (void)rubraview_pal_fs_delete(cstr(app->arc_undo_backup));
    app->arc_undo_backup[0] = '\0';
    app->arc_undo_path[0] = '\0';
}

bool on_archive_page(const app_state_t *app) {
    return app->source.kind != RUBRAVIEW_PAGE_SOURCE_FOLDER && app->source.archive_path.size > 0 && !app->list_is_set &&
           app->source.arc != NULL && current_page_index(app) >= 0;
}

void arc_ask_clear(app_state_t *app) {
    free(app->arc_ask_data);
    app->arc_ask_data = NULL;
    app->arc_ask_size = 0;
    app->arc_ask = ARC_ASK_NONE;
}

static DWORD WINAPI arc_job_run(LPVOID param) {
    arc_job_t *job = (arc_job_t*)param;
    job->result = (int)rubraview_arcedit_run(&job->job);
    InterlockedExchange(&job->done, 1);
    rubraview_pal_window_wake(job->wake);
    return 0;
}

static bool arc_file_exists(proven_u8str_view_t path) {
    uint8_t mem[4096];
    proven_arena_t probe = proven_arena_create((proven_mem_mut_t){ .ptr = mem, .size = sizeof(mem) });
    rubraview_fs_entry_t entry;
    return rubraview_pal_fs_stat(&probe, path, &entry);
}

/* The archive is written again, the question (if there was one) answered. */
void arc_edit_start(app_state_t *app, rubraview_arcedit_target_t target) {
    rubraview_arcedit_op_t op = app->arc_ask_op;
    uint8_t *data = app->arc_ask_data;
    size_t size = app->arc_ask_size;
    app->arc_ask_data = NULL;          /* the job's from here */
    arc_ask_clear(app);

    arc_job_t *job = NULL;
    const char *why = T("not changed: could not start");
    if (!on_archive_page(app) || !rubraview_path_same(cstr(app->arc_ask_path), app->source.archive_path)) goto fail;
    rubraview_arcedit_route_t route = rubraview_arcedit_route(&app->source);
    if (route != RUBRAVIEW_ARCEDIT_IN_PLACE && route != RUBRAVIEW_ARCEDIT_CONVERT) goto fail;
    if (route == RUBRAVIEW_ARCEDIT_IN_PLACE) target = RUBRAVIEW_ARCEDIT_SAME;
    else if (target == RUBRAVIEW_ARCEDIT_SAME) goto fail;

    proven_u8str_view_t first = rubraview_page_source_first_volume(&app->source);
    proven_u8str_view_t opened = app->source.archive_path;
    if (first.size == 0) first = opened;
    job = (arc_job_t*)calloc(1, sizeof(*job));
    if (!job) goto fail;
    if (first.size >= sizeof(job->archive) || opened.size >= sizeof(job->opened) || first.size + 16 >= sizeof(job->out)) goto fail;
    memcpy(job->archive, first.ptr, first.size);
    memcpy(job->opened, opened.ptr, opened.size);
    memcpy(job->out, first.ptr, first.size);
    memcpy(job->out + first.size, ".rvnew", 7);
    memcpy(job->name, app->arc_ask_name, sizeof(job->name));

    /* A 7z is packed from a copy of every file laid beside it first. */
    if (target == RUBRAVIEW_ARCEDIT_7Z) {
        uint64_t unpacked = 0, free_bytes = 0;
        for (size_t i = 0; ; ++i) {
            const fulta_arc_entry_t *en = fulta_arc_entry(app->source.arc, i);
            if (!en) break;
            unpacked += en->size;
        }
        if (rubraview_pal_fs_free_bytes(rubraview_path_dirname(first), &free_bytes) && free_bytes < unpacked * 2u + (64u << 20)) {
            static char line[128];
            snprintf(line, sizeof(line), T("not changed: a 7z needs about %llu MB free beside the archive"),
                     (unsigned long long)((unpacked * 2u + (64u << 20)) >> 20));
            why = line;
            goto fail;
        }
    }

    int32_t page = current_page_index(app);
    job->page = (size_t)page;
    job->convert = route == RUBRAVIEW_ARCEDIT_CONVERT;
    job->data = data;
    data = NULL;
    job->wake = app->window;
    /* Where the reader is afterwards: on the page itself under its new
       name, or (a page deleted) on the one that takes its number. */
    proven_u8str_view_t shown = page_display_name(app, (size_t)page);
    if (op == RUBRAVIEW_ARCEDIT_REPLACE && job->name[0] == '\0') {
        if (shown.size < sizeof(job->land)) memcpy(job->land, shown.ptr, shown.size);
    } else if (op != RUBRAVIEW_ARCEDIT_DELETE) {
        rubraview_arcedit_sibling_name(shown, cstr(job->name), job->land, sizeof(job->land));
    }
    rubraview_codepage_t names = app->names_codepage >= 0 ? (rubraview_codepage_t)app->names_codepage : app->archive_codepage;
    job->job = (rubraview_arcedit_job_t){
        .archive_path = job->archive, .codepage = names, .op = op,
        .entry_index = app->arc_ask_entry, .name = job->name[0] ? job->name : NULL,
        .data = job->data, .size = size, .target = target, .out_path = job->out,
        .level = arc_level(app),
    };
    arc_undo_drop(app);   /* one step back, and it is this edit's from here */
    /* The choice holds for this book for the session. */
    if (job->convert && opened.size < sizeof(app->arc_choice_path)) {
        memcpy(app->arc_choice_path, opened.ptr, opened.size);
        app->arc_choice_path[opened.size] = '\0';
        app->arc_choice = target;
    }
    job->thread = CreateThread(NULL, 0, arc_job_run, job, 0, NULL);
    if (!job->thread) goto fail;
    app->arc_job = job;
    if (app->panel.open) panel_close(app);
    osd_say(app, TU8("writing the archive..."));
    return;

fail:
    if (job) free(job->data);
    free(job);
    free(data);
    osd_say(app, cstr(why));
}

/* An edit of entry `entry` is asked for. `data` (malloc) becomes this
   function's, whatever happens. */
void arc_edit_request(app_state_t *app, rubraview_arcedit_op_t op, size_t entry, const char *name, uint8_t *data, size_t size) {
    if (op == RUBRAVIEW_ARCEDIT_DELETE) { free(data); arc_delete_ask(app, false); return; }   /* RV-212: it asks which */
    const char *why = NULL;
    if (app->arc_job) why = T("the archive is still being written");
    else if (!on_archive_page(app)) why = T("not changed: the page is not in an archive");
    rubraview_arcedit_route_t route = why ? RUBRAVIEW_ARCEDIT_NOT_AN_ARCHIVE : rubraview_arcedit_route(&app->source);
    if (!why && route == RUBRAVIEW_ARCEDIT_ENCRYPTED) why = rubraview_arcedit_result_text(RUBRAVIEW_ARCEDIT_REFUSED_ENCRYPTED);
    if (!why && route == RUBRAVIEW_ARCEDIT_NOT_AN_ARCHIVE) why = T("not changed: the page is not in an archive");
    if (!why && name && strlen(name) >= sizeof(app->arc_ask_name)) why = T("not changed: that name is too long");
    if (why) {
        free(data);
        osd_say(app, cstr(why));
        return;
    }
    arc_ask_clear(app);
    app->arc_ask_op = op;
    app->arc_ask_entry = entry;
    memset(app->arc_ask_name, 0, sizeof(app->arc_ask_name));
    if (name) memcpy(app->arc_ask_name, name, strlen(name));
    app->arc_ask_data = data;
    app->arc_ask_size = size;
    memset(app->arc_ask_path, 0, sizeof(app->arc_ask_path));
    if (app->source.archive_path.size < sizeof(app->arc_ask_path)) memcpy(app->arc_ask_path, app->source.archive_path.ptr, app->source.archive_path.size);

    bool chosen = app->arc_choice != RUBRAVIEW_ARCEDIT_SAME &&
                  rubraview_path_same(cstr(app->arc_choice_path), app->source.archive_path);
    if (route == RUBRAVIEW_ARCEDIT_CONVERT && !chosen) panel_open_convert(app);
    else arc_edit_start(app, route == RUBRAVIEW_ARCEDIT_CONVERT ? app->arc_choice : RUBRAVIEW_ARCEDIT_SAME);
}

/* RV-212 (owner, 2026-10-31: "압축파일 내에서 del 키를 누르면 해당 파일만 지울지 압축파일 전체를 지울지 묻게 해
   주세요. 물론 여기엔 취소도 있고요"): Delete on a page inside an archive asks which — the page, taken out of the
   archive (D-88), or the archive's file itself, to the Recycle Bin (Shift+Delete: for good) — or neither. A choice
   that cannot be made here is shown, greyed, with why. */
void arc_delete_ask(app_state_t *app, bool purge) {
    if (!on_archive_page(app)) return;
    if (app->arc_job) { osd_say(app, TU8("the archive is still being written")); return; }
    rubraview_arcedit_route_t route = rubraview_arcedit_route(&app->source);
    const char *why = NULL;
    if (route == RUBRAVIEW_ARCEDIT_ENCRYPTED) why = rubraview_arcedit_result_text(RUBRAVIEW_ARCEDIT_REFUSED_ENCRYPTED);
    else if (route == RUBRAVIEW_ARCEDIT_NOT_AN_ARCHIVE) why = T("not changed: the page is not in an archive");
    else if (page_count(app) <= 1) why = T("it is the archive's last page");
    bool whole = rubraview_page_source_volumes(&app->source, NULL) < 2;   /* a split set's parts are not found here */
    if (why && !whole) { osd_say(app, cstr(why)); return; }
    arc_ask_clear(app);
    app->arc_ask_op = RUBRAVIEW_ARCEDIT_DELETE;
    app->arc_ask_entry = app->source.pages[current_page_index(app)].entry_index;
    memset(app->arc_ask_name, 0, sizeof(app->arc_ask_name));
    memset(app->arc_ask_path, 0, sizeof(app->arc_ask_path));
    if (app->source.archive_path.size < sizeof(app->arc_ask_path)) memcpy(app->arc_ask_path, app->source.archive_path.ptr, app->source.archive_path.size);
    app->arc_ask_page_ok = why == NULL;
    app->arc_ask_whole_ok = whole;
    app->arc_ask_purge = purge;
    snprintf(app->arc_ask_why, sizeof(app->arc_ask_why), "%s", why ? why : (whole ? "" : T("a split archive: its parts are deleted in Explorer")));
    app->arc_ask = ARC_ASK_DELETE;
}

/* The archive's file itself: let go, to the Recycle Bin (or gone), then the archive beside it, or the folder. */
static void arc_delete_whole(app_state_t *app) {
    proven_u8str_view_t path = app_keep(app, app->source.archive_path);
    proven_u8str_view_t dir = app_keep(app, app->source_dir.size ? app->source_dir : rubraview_path_dirname(path));
    rubraview_fs_listing_t listing = rubraview_pal_fs_list_dir(app->arena, dir);
    proven_u8str_view_t next = rubraview_page_source_sibling_archive(app->arena, &listing, path, true);
    if (next.size == 0) next = rubraview_page_source_sibling_archive(app->arena, &listing, path, false);
    /* an edit of this archive kept to be undone (D-89) is for nobody now: Ctrl+Z brings the archive back instead */
    if (rubraview_path_same(cstr(app->arc_undo_path), path)) arc_undo_drop(app);
    history_remember(app);
    unload_all_pages(app);
    source_close(app);   /* Windows deletes nothing that is mapped */
    /* nothing open now, as at the start: no book, no page on screen (its spreads would still name one) */
    app->source.archive_path = PROVEN_LIT("");
    app->source.kind = RUBRAVIEW_PAGE_SOURCE_FOLDER;
    app->layout.count = 0;
    app->spread_index = 0;
    bool purge = app->arc_ask_purge;
    bool ok = purge ? rubraview_pal_fs_delete(path) : rubraview_pal_fs_recycle(path);
    if (!ok) {
        osd_say(app, TU8("could not delete the archive (is it open elsewhere?)"));
        open_path(app, path);
        return;
    }
    rubraview_undo_push(&app->undo, (rubraview_file_action_t){
        .op = purge ? RUBRAVIEW_FILE_OP_PURGE : RUBRAVIEW_FILE_OP_RECYCLE,
        .source_path = path,
    });
    if (next.size > 0) open_path(app, next);
    else {   /* nothing beside it: its folder, as a file removed leaves it, and the open-file screen there */
        open_folder(app, dir);
        picker_open(app);
        picker_navigate(app, dir);
    }
    osd_say(app, purge ? TU8("the archive was deleted for good") : TU8("the archive went to the recycle bin"));
}

static void arc_delete_choose(app_state_t *app, int32_t which) {
    if (which == ARC_DEL_PAGE && app->arc_ask_page_ok) {
        rubraview_arcedit_route_t route = on_archive_page(app) ? rubraview_arcedit_route(&app->source) : RUBRAVIEW_ARCEDIT_NOT_AN_ARCHIVE;
        bool chosen = app->arc_choice != RUBRAVIEW_ARCEDIT_SAME && rubraview_path_same(cstr(app->arc_choice_path), app->source.archive_path);
        if (route == RUBRAVIEW_ARCEDIT_CONVERT && !chosen) panel_open_convert(app);
        else arc_edit_start(app, route == RUBRAVIEW_ARCEDIT_CONVERT ? app->arc_choice : RUBRAVIEW_ARCEDIT_SAME);
        return;
    }
    if (which == ARC_DEL_WHOLE && app->arc_ask_whole_ok) {
        arc_ask_clear(app);
        if (rubraview_path_same(cstr(app->arc_ask_path), app->source.archive_path)) arc_delete_whole(app);
        return;
    }
    if (which == ARC_DEL_CANCEL) arc_ask_clear(app);
}

bool arc_ask_press(app_state_t *app, double x, double y) {
    if (app->arc_ask != ARC_ASK_DELETE) return false;
    for (int32_t i = 0; i < app->arc_ask_btn_n; ++i)
        if (pal_rect_has(app->arc_ask_btn[i], x, y)) { arc_delete_choose(app, app->arc_ask_btn_kind[i]); return true; }
    return true;   /* the question stands until it is answered */
}

/* The same, of the page on screen. */
void arc_edit_on_page(app_state_t *app, rubraview_arcedit_op_t op, const char *name) {
    if (!on_archive_page(app)) return;
    arc_edit_request(app, op, app->source.pages[current_page_index(app)].entry_index, name, NULL, 0);
}

bool arc_ask_key(app_state_t *app, rubraview_key_combo_t combo) {
    if (app->arc_ask == ARC_ASK_NONE) return false;
    if (app->arc_ask == ARC_ASK_DELETE) {
        /* RV-212: 1 (or Enter) the page, 2 the whole archive; Esc, N, anything else neither */
        if (combo.modifiers != RUBRAVIEW_MOD_NONE && combo.modifiers != RUBRAVIEW_MOD_SHIFT) return true;
        if (key_is(combo, "1") || key_is(combo, "Numpad1") || key_is(combo, "Enter")) {
            if (app->arc_ask_page_ok) arc_delete_choose(app, ARC_DEL_PAGE);
        } else if (key_is(combo, "2") || key_is(combo, "Numpad2")) {
            if (app->arc_ask_whole_ok) arc_delete_choose(app, ARC_DEL_WHOLE);
        } else if (!key_is(combo, "Shift")) {
            arc_ask_clear(app);
        }
        return true;
    }
    /* D-89: the convert panel is open. Enter is its button; Z and 7 pick the
       format; any other key goes its usual way, and what it does closes the panel. */
    if (combo.modifiers != RUBRAVIEW_MOD_NONE) return false;
    if (key_is(combo, "Enter")) {
        rubraview_arcedit_target_t target = arc_convert_target(app);
        app->panel_is_convert = false;
        panel_close(app);
        arc_edit_start(app, target);
        return true;
    }
    if (key_is(combo, "Z") || key_is(combo, "7")) {
        double v = key_is(combo, "7") ? 1.0 : 0.0;
        rubraview_settings_set(&app->settings, PROVEN_LIT("archive"), PROVEN_LIT("convert_format"), v);
        rubraview_panel_set_value(&app->panel, PANEL_CONV_FORMAT, v);
        return true;
    }
    return false;
}

/* A name beside `wanted` that no file has: `book-1.cbz`, `book-2.cbz`, ... */
static proven_u8str_view_t arc_free_name(app_state_t *app, proven_u8str_view_t wanted) {
    if (!arc_file_exists(wanted)) return wanted;
    proven_u8str_view_t dir = rubraview_path_dirname(wanted), base = rubraview_path_basename(wanted);
    proven_u8str_view_t stem = rubraview_path_stem(base);
    proven_u8str_view_t ext = { .ptr = base.ptr + stem.size, .size = base.size - stem.size };
    for (int n = 1; n < 1000; ++n) {
        char name[1024];
        int len = snprintf(name, sizeof(name), "%.*s-%d%.*s", (int)stem.size, stem.ptr, n, (int)ext.size, ext.ptr);
        if (len <= 0 || (size_t)len >= sizeof(name)) break;
        proven_u8str_view_t path = rubraview_path_join(app->arena, dir, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)name, .size = (size_t)len });
        if (path.size > 0 && !arc_file_exists(path)) return path;
    }
    return PROVEN_LIT("");
}

/* The book again, on the page named or numbered. */
static void arc_reopen(app_state_t *app, proven_u8str_view_t path, const char *land, size_t page) {
    if (!open_archive(app, path)) return;
    size_t start = page < page_count(app) ? page : page_count(app) - 1;
    if (land && land[0]) {
        for (size_t i = 0; i < page_count(app); ++i) {
            proven_u8str_view_t name = page_display_name(app, i);
            if (name.size == strlen(land) && memcmp(name.ptr, land, name.size) == 0) { start = i; break; }
        }
    }
    finish_open(app, start);
}

/* The new archive is written and checked: it takes the old one's place.
   The viewer lets go of the old file first — Windows replaces nothing
   that is mapped — and opens the book again afterwards. */
static void arc_job_finish(app_state_t *app, arc_job_t *job) {
    proven_u8str_view_t out = cstr(job->out), first = cstr(job->archive);
    proven_u8str_view_t opened = app_keep(app, cstr(job->opened));
    proven_u8str_view_t final = opened;
    const char *said = NULL;
    bool done = false;

    history_remember(app);
    unload_all_pages(app);
    source_close(app);

    bool can_undo = false;
    if (!job->convert) {
        /* D-89: the old file steps aside as `name.rvold` so that Ctrl+Z can bring it back. */
        char backup[sizeof(app->arc_undo_backup)];
        bool kept = first.size + 7 < sizeof(backup);
        if (kept) {
            memcpy(backup, first.ptr, first.size);
            memcpy(backup + first.size, ".rvold", 7);
            kept = rubraview_pal_fs_replace(first, cstr(backup));
        }
        done = rubraview_pal_fs_replace(out, first);
        if (!done && kept) (void)rubraview_pal_fs_replace(cstr(backup), first);   /* back where it was */
        if (done && kept) {
            memcpy(app->arc_undo_backup, backup, sizeof(backup));
            memset(app->arc_undo_path, 0, sizeof(app->arc_undo_path));
            memcpy(app->arc_undo_path, first.ptr, first.size);
            can_undo = true;
        }
        said = done ? NULL : T("not changed: the archive could not be replaced (is it open elsewhere?)");
    } else {
        char name[1024];
        proven_u8str_view_t base = rubraview_path_basename(first);
        size_t n = rubraview_arcedit_converted_name(base, job->job.target, name, sizeof(name));
        proven_u8str_view_t wanted = n > 0 ? rubraview_path_join(app->arena, rubraview_path_dirname(first), (proven_u8str_view_t){ .ptr = (const proven_byte_t *)name, .size = n })
                               : PROVEN_LIT("");
        bool takes_old_name = false;
        for (size_t i = 0; i < job->job.volume_count; ++i) {
            if (wanted.size > 0 && rubraview_path_same(wanted, cstr(job->job.volumes[i]))) takes_old_name = true;
        }
        if (wanted.size == 0) {
            said = T("not changed: the new archive's name is too long");
        } else if (takes_old_name) {
            /* A solid 7z becoming a 7z: the old one leaves for the bin, then the new one has its name. */
            bool binned = true;
            for (size_t i = 0; i < job->job.volume_count; ++i) {
                if (!rubraview_pal_fs_recycle(cstr(job->job.volumes[i]))) binned = false;
            }
            if (binned && rubraview_pal_fs_replace(out, wanted)) { done = true; final = wanted; }
            else {
                /* The old file would not go: the new one is kept beside it under a free name. */
                proven_u8str_view_t other = arc_free_name(app, wanted);
                if (other.size > 0 && rubraview_pal_fs_move(out, other)) {
                    done = true;
                    final = other;
                    said = T("the old archive could not be put in the Recycle Bin: the new one is beside it");
                } else {
                    said = T("not changed: the archive could not be replaced (is it open elsewhere?)");
                }
            }
        } else {
            /* Another name: never over a file that is already there. */
            proven_u8str_view_t target = arc_free_name(app, wanted);
            if (target.size > 0 && rubraview_pal_fs_move(out, target)) {
                done = true;
                final = target;
                for (size_t i = 0; i < job->job.volume_count; ++i) {
                    if (!rubraview_pal_fs_recycle(cstr(job->job.volumes[i]))) said = T("written; the old archive could not be put in the Recycle Bin");
                }
            } else {
                said = T("not changed: the new archive could not be given its name");
            }
        }
    }
    if (!done) (void)rubraview_pal_fs_delete(out);
    final = app_keep(app, final);
    /* The format chosen holds for the book under its new name too. */
    if (done && job->convert && final.size < sizeof(app->arc_choice_path)) {
        memcpy(app->arc_choice_path, final.ptr, final.size);
        app->arc_choice_path[final.size] = '\0';
    }

    rubraview_arcedit_op_t op = job->job.op;
    arc_reopen(app, done ? final : opened, done ? job->land : NULL, job->page);
    if (said) { osd_say(app, cstr(said)); return; }
    char line[256];
    proven_u8str_view_t base = rubraview_path_basename(final);
    const char *what = op == RUBRAVIEW_ARCEDIT_DELETE ? T("page deleted") : op == RUBRAVIEW_ARCEDIT_RENAME ? T("page renamed")
                     : op == RUBRAVIEW_ARCEDIT_REPLACE ? T("page saved over") : T("copy saved in the archive");
    if (job->convert) snprintf(line, sizeof(line), T("%s; the book is now %.*s"), what, (int)(base.size < 160 ? base.size : 160), base.ptr);
    else snprintf(line, sizeof(line), "%s%s", what, can_undo ? T("  (Ctrl+Z takes it back)") : "");
    osd_say(app, cstr(line));
}

/* D-89: the last edit in place taken back — the book as it was before it,
   while the reader is still in that book. */
bool arc_undo(app_state_t *app) {
    if (!app->arc_undo_backup[0] || app->arc_job) return false;
    if (!on_archive_page(app)) return false;
    proven_u8str_view_t first = rubraview_page_source_first_volume(&app->source);
    if (!rubraview_path_same(first.size ? first : app->source.archive_path, cstr(app->arc_undo_path))) return false;
    proven_u8str_view_t opened = app_keep(app, app->source.archive_path);
    size_t page = (size_t)current_page_index(app);
    history_remember(app);
    unload_all_pages(app);
    source_close(app);
    bool ok = rubraview_pal_fs_replace(cstr(app->arc_undo_backup), cstr(app->arc_undo_path));
    if (ok) { app->arc_undo_backup[0] = '\0'; app->arc_undo_path[0] = '\0'; }
    arc_reopen(app, opened, NULL, page);
    osd_say(app, ok ? TU8("the archive is as it was before the last change") : TU8("could not take the change back"));
    return true;
}

/* Each pass: how far the writing has got; a finished one is taken up.
   A quit stops it and waits — the old archive stands. */
void arc_job_poll(app_state_t *app, bool quitting) {
    arc_job_t *job = app->arc_job;
    if (!job) {
        if (quitting) arc_ask_clear(app);
        return;
    }
    if (quitting) atomic_store(&job->job.cancel, true);
    if (!quitting && !InterlockedCompareExchange(&job->done, 0, 0)) {
        uint64_t done = atomic_load(&job->job.done), total = atomic_load(&job->job.total);
        char line[96];
        if (atomic_load(&job->job.cancel)) snprintf(line, sizeof(line), T("stopping..."));
        else if (total > 0) snprintf(line, sizeof(line), T("writing the archive... %d %%  (Esc stops it)"), (int)(done * 100u / total));
        else snprintf(line, sizeof(line), T("writing the archive...  (Esc stops it)"));
        osd_say(app, cstr(line));
        return;
    }
    WaitForSingleObject(job->thread, INFINITE);
    CloseHandle(job->thread);
    app->arc_job = NULL;               /* opening is allowed again */
    if (!quitting) {
        if (job->result == RUBRAVIEW_ARCEDIT_OK) arc_job_finish(app, job);
        else osd_say(app, cstr(rubraview_arcedit_result_text((rubraview_arcedit_result_t)job->result)));
    } else if (job->result == RUBRAVIEW_ARCEDIT_OK) {
        (void)rubraview_pal_fs_delete(cstr(job->out));   /* written, but the reader left: the old one stands */
    }
    rubraview_arcedit_job_free(&job->job);
    free(job->data);
    free(job);
}

/* A file for a moment's use in the system's temporary folder. */
bool arc_temp_path(char *dst, size_t cap, const char *tag, proven_u8str_view_t leaf) {
    WCHAR wide[MAX_PATH + 1];
    char dir[MAX_PATH * 3 + 1];
    DWORD n = GetTempPathW(MAX_PATH + 1, wide);
    if (n == 0 || n > MAX_PATH) return false;
    if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, dir, (int)sizeof(dir), NULL, NULL) <= 1) return false;
    int len = snprintf(dst, cap, "%srubraview-%s-%lu-%lu-%.*s", dir, tag, (unsigned long)GetCurrentProcessId(),
                       (unsigned long)GetTickCount(), (int)leaf.size, leaf.ptr);
    return len > 0 && (size_t)len < cap;
}

/* Save a copy's work, off the main thread: everything it needs copied in,
   its own memory, its own WIC factory. `result` indexes save_job_poll's lines. */
enum { SAVE_OK = 0, SAVE_UNREADABLE, SAVE_EDIT_FAILED, SAVE_UNWRITABLE, SAVE_NO_MEMORY };

static proven_u8str_view_t save_read_whole(proven_u8str_view_t path, void **out_mem) {
    *out_mem = NULL;
    rubraview_fs_entry_t entry;
    uint8_t probe_mem[4096];
    proven_arena_t probe = proven_arena_create((proven_mem_mut_t){ .ptr = probe_mem, .size = sizeof(probe_mem) });
    if (!rubraview_pal_fs_stat(&probe, path, &entry) || entry.size_bytes == 0 || entry.size_bytes > MAX_ARCHIVE_BYTES) {
        return PROVEN_LIT("");
    }
    size_t size = (size_t)entry.size_bytes + 4096;
    *out_mem = malloc(size);
    if (!*out_mem) return PROVEN_LIT("");
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)*out_mem, .size = size });
    return rubraview_pal_fs_read_file(&arena, path, MAX_ARCHIVE_BYTES);
}

static bool details_path_taken(proven_u8str_view_t path, void *ctx) { (void)ctx; return rubraview_pal_fs_exists(path); }

/* 1.18.0 (owner, 2026-10-18: "그림파일 읽을 때, 옆에 동일한 이름이 있는 텍스트 파일이 있으면 읽어보고
   루브라뷰가 만든 파일이면 ... 파일 내부 정보인 것처럼"): the details a Rubraview .txt beside `picture`
   holds for it — photo.txt, photo-1.txt ... photo-20.txt, the first that names this picture. Any
   thread; `arena` keeps what is read. `out_path`, when given, is the .txt's path. */
#define DETAILS_TXT_MAX_BYTES (1u << 20)
bool details_beside(proven_arena_t *arena, proven_u8str_view_t picture, rubraview_details_src_t *out, proven_u8str_view_t *out_path) {
    *out = (rubraview_details_src_t){0};
    for (unsigned k = 0; k <= 20; ++k) {
        proven_u8str_view_t path = rubraview_details_txt_candidate(arena, picture, k);
        if (path.size == 0) return false;
        if (!rubraview_pal_fs_exists(path)) continue;
        proven_u8str_view_t text = rubraview_pal_fs_read_file(arena, path, DETAILS_TXT_MAX_BYTES);
        if (text.size > 0 && rubraview_details_from_txt(arena, text, picture, out)) {
            if (out_path) *out_path = path;
            return true;
        }
    }
    return false;
}

/* 1.17.0 (owner, 2026-10-18): what `source` said (EXIF, XMP, IPTC, comments) put into `written`, a
   picture just written as `format` with its pixels only, where that format has a place; and, asked
   for, what it cannot hold in a .txt beside `named` (the name the file will have: an original
   written over is written beside it first). Any thread: its own memory. `named` empty: no .txt
   (a page going into an archive). Returns DETAILS_* for the caller to say. */

int details_keep(proven_u8str_view_t source, proven_u8str_view_t written, proven_u8str_view_t named, rubraview_export_format_t format,
                        const rubraview_export_options_t *options) {
    if (!options || options->privacy_clean || options->keep_details <= RUBRAVIEW_KEEP_DETAILS_DROP) return DETAILS_NONE;
    void *src_mem = NULL, *out_mem = NULL;
    proven_u8str_view_t src = save_read_whole(source, &src_mem);
    int said = DETAILS_NONE;
    if (src.size > 0) {
        proven_u8str_view_t out = save_read_whole(written, &out_mem);
        /* what is read, the file made again with the blocks, the words and the .txt */
        size_t room = 0;
        if (out.size > 0 && !PROVEN_CKD_ADD(&room, out.size, src.size) && !PROVEN_CKD_MUL(&room, room, (size_t)3) &&
            !PROVEN_CKD_ADD(&room, room, (size_t)(8u << 20))) {
            void *work_mem = malloc(room);
            if (work_mem) {
                proven_arena_t work = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)work_mem, .size = room });
                rubraview_details_src_t details, beside;
                bool own = rubraview_details_read(&work, (const uint8_t*)src.ptr, src.size, &details);
                /* 1.18.0: what a .txt beside the source kept for it goes on with it */
                if (details_beside(&work, source, &beside, NULL)) { rubraview_details_merge(&work, &details, &beside); own = true; }
                if (own) {
                    rubraview_details_put_t put = rubraview_details_put(&work, format, out, &details);
                    if (put.file.ptr != out.ptr) said = rubraview_pal_fs_write_file(written, put.file) ? DETAILS_IN_FILE : DETAILS_FAILED;
                    if (!put.all_kept && said != DETAILS_FAILED && options->keep_details >= RUBRAVIEW_KEEP_DETAILS_OR_TXT && named.size > 0) {
                        /* the one this picture has already, written again; else a free name */
                        rubraview_details_src_t had;
                        proven_u8str_view_t txt_path = PROVEN_LIT("");
                        if (!details_beside(&work, named, &had, &txt_path)) txt_path = rubraview_details_txt_path(&work, named, details_path_taken, NULL);
                        proven_u8str_view_t txt = rubraview_details_txt(&work, &details, rubraview_path_basename(source), rubraview_path_basename(named));
                        if (txt_path.size > 0 && txt.size > 0) {
                            /* a mark that says UTF-8, for Notepad and the like */
                            proven_result_mem_mut_t res = proven_arena_alloc(&work, txt.size + 3);
                            if (proven_is_ok(res.err)) {
                                memcpy(res.value.ptr, "\xEF\xBB\xBF", 3);
                                memcpy(res.value.ptr + 3, txt.ptr, txt.size);
                                said = rubraview_pal_fs_write_file(txt_path, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)res.value.ptr, .size = txt.size + 3 })
                                           ? DETAILS_TXT : DETAILS_FAILED;
                            }
                        }
                    }
                }
                free(work_mem);
            }
        }
    }
    free(out_mem);
    free(src_mem);
    return said;
}

/* The name the written file ends with: the original's, when it is written over; none for an archive's page. */
static proven_u8str_view_t save_job_named(const save_job_t *job) {
    if (job->into_archive) return PROVEN_LIT("");
    return cstr(job->over_original ? job->source : job->out);
}

static int save_job_work(save_job_t *job) {
    proven_u8str_view_t source = { .ptr = (const proven_byte_t *)job->source, .size = strlen(job->source) };
    proven_u8str_view_t out_path = { .ptr = (const proven_byte_t *)job->out, .size = strlen(job->out) };

    if (job->route == RUBRAVIEW_EXPORT_STRIP_ONLY || job->route == RUBRAVIEW_EXPORT_COPY || job->route == RUBRAVIEW_EXPORT_JPEG_LOSSLESS) {
        void *mem = NULL;
        proven_u8str_view_t bytes = save_read_whole(source, &mem);
        int result = SAVE_UNREADABLE;
        if (bytes.size > 0 && job->route == RUBRAVIEW_EXPORT_COPY) {
            result = rubraview_pal_fs_write_file(out_path, bytes) ? SAVE_OK : SAVE_UNWRITABLE;
        } else if (bytes.size > 0) {
            /* the strip's output is at most the input: room for it beside */
            size_t room = bytes.size * 2u + (1u << 20);
            void *work_mem = malloc(room);
            result = SAVE_NO_MEMORY;
            if (work_mem) {
                proven_arena_t work = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)work_mem, .size = room });
                /* 1.17.0: or its scans, tables and restarts written again, its markers kept unless privacy clean */
                rubraview_jpegtran_result_t stripped = job->route == RUBRAVIEW_EXPORT_JPEG_LOSSLESS
                    ? rubraview_jpegtran_rewrite(&work, (const uint8_t*)bytes.ptr, bytes.size, job->options.jpeg_progressive,
                                                 job->options.jpeg_optimise, job->options.jpeg_restart_rows, !job->options.privacy_clean)
                    : rubraview_jpegtran_strip_metadata(&work, (const uint8_t*)bytes.ptr, bytes.size);
                result = stripped.err != RUBRAVIEW_JPEGTRAN_OK ? SAVE_UNREADABLE
                       : rubraview_pal_fs_write_file(out_path, stripped.data) ? SAVE_OK : SAVE_UNWRITABLE;
                free(work_mem);
            }
        }
        free(mem);
        return result;
    }

    /* RV-109: an animation written as WebP keeps moving — every frame at
       its time. Not when it was edited: an edit is made on one picture. */
    {
        rubraview_export_format_t to = job->options.format == RUBRAVIEW_EXPORT_SAME_AS_SOURCE ? job->source_format
                                                                                              : job->options.format;
        if ((to == RUBRAVIEW_EXPORT_WEBP || to == RUBRAVIEW_EXPORT_AVIF || to == RUBRAVIEW_EXPORT_JXL) && !job->apply_edit) {
            int moved = to == RUBRAVIEW_EXPORT_WEBP ? rubraview_pal_image_save_animation_webp(out_path, source, NULL, 0, &job->options)
                      : rubraview_pal_image_save_animation_ffmpeg(out_path, source, NULL, 0, &job->options, to == RUBRAVIEW_EXPORT_AVIF);   /* 1.31.0 */
            if (moved > 0) (void)details_keep(source, out_path, save_job_named(job), to, &job->options);
            if (moved < 0) job->why = rubraview_pal_image_save_why();
            if (moved != 0) return moved > 0 ? SAVE_OK : SAVE_UNWRITABLE;
        }
    }

    /* The pixels get memory of their own, sized to the picture: the app
       arena is a fixed 64 MB, a 3840x2400 page is 37 MB decoded, and the
       commit's first copy used to fail there — which wrote the page
       unedited, as if the edit had been saved (T093). */
    size_t src_bytes = picture_bytes((size_t)job->width, (size_t)job->height, 4u, 0);
    size_t out_bytes = src_bytes;
    if (job->apply_edit && job->edit.resize_width > 0 && job->edit.resize_height > 0) {
        size_t r = picture_bytes((size_t)job->edit.resize_width, (size_t)job->edit.resize_height, 4u, 0);
        if (r > out_bytes) out_bytes = r;
    }
    /* decode and its conversion, then the commit's clone, blur and sharpen
       (each a copy plus a row buffer's worth), then the resize */
    size_t need = bytes_scaled(src_bytes, 8u, 0), twice_out = bytes_scaled(out_bytes, 2u, 16u << 20);
    if (need == SIZE_MAX || twice_out == SIZE_MAX || PROVEN_CKD_ADD(&need, need, twice_out)) need = SIZE_MAX;   /* malloc refuses it */
    void *work_mem = malloc(need);
    if (!work_mem) return SAVE_NO_MEMORY;
    proven_arena_t work = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)work_mem, .size = need });

    int result = SAVE_OK;
    rubraview_pixbuf_t pixels = rubraview_pal_image_read_pixels(&work, source, NULL, 0, true);
    if (!rubraview_pixbuf_is_valid(&pixels)) result = SAVE_UNREADABLE;
    void *steps_mem = NULL;
    if (result == SAVE_OK && job->apply_edit && job->steps.cursor > 0) {
        /* The owner's edit: each step from one buffer into the other, the
           one before it given back as the next is made. */
        size_t biggest = src_bytes;
        for (size_t i = 1; i <= job->steps.cursor; ++i) {
            int32_t sw = 0, sh = 0;
            rubraview_edit_steps_size(&job->steps, i, &sw, &sh);
            size_t b = picture_bytes((size_t)sw, (size_t)sh, 4u, 0);
            if (b > biggest) biggest = b;
        }
        size_t half = bytes_scaled(biggest, 6u, 16u << 20), both = bytes_scaled(half, 2u, 0);
        steps_mem = both == SIZE_MAX ? NULL : malloc(both);
        if (!steps_mem) {
            result = SAVE_NO_MEMORY;
        } else {
            proven_arena_t lanes[2] = {
                proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)steps_mem, .size = half }),
                proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)steps_mem + half, .size = half }),
            };
            for (size_t i = 0; i < job->steps.cursor && result == SAVE_OK; ++i) {
                proven_arena_t *lane = &lanes[i % 2u];
                proven_arena_reset(lane);
                g_plugin_why = NULL;
                rubraview_pixbuf_t next = rubraview_edit_step_apply(lane, &job->steps.steps[i], &pixels, 1.0);
                if (rubraview_pixbuf_is_valid(&next)) pixels = next; else { result = SAVE_EDIT_FAILED; job->why = g_plugin_why; }
            }
        }
    }
    if (result == SAVE_OK && job->apply_edit && !rubraview_edit_is_neutral(&job->edit)) {
        rubraview_pixbuf_t edited = rubraview_edit_commit(&work, &job->edit, &pixels);
        if (rubraview_pixbuf_is_valid(&edited)) pixels = edited; else result = SAVE_EDIT_FAILED;
    }
    if (result == SAVE_OK) {
        rubraview_export_options_t options = job->options;
        if (options.format == RUBRAVIEW_EXPORT_SAME_AS_SOURCE) options.format = job->source_format;
        if (!rubraview_pal_image_save(out_path, &pixels, &options)) { result = SAVE_UNWRITABLE; job->why = rubraview_pal_image_save_why(); }
        else (void)details_keep(source, out_path, save_job_named(job), options.format, &options);
    }
    free(steps_mem);
    free(work_mem);
    return result;
}

static DWORD WINAPI save_job_run(LPVOID param) {
    save_job_t *job = (save_job_t*)param;
    bool com = SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
    job->result = com ? save_job_work(job) : SAVE_UNREADABLE;
    rubraview_pal_image_thread_end();
    if (com) CoUninitialize();
    InterlockedExchange(&job->done, 1);
    rubraview_pal_window_wake(job->wake);   /* the main loop hears it at once */
    return 0;
}

/* The workbench's Save a copy, and the export dialog's Export, are the
   same act: run the session, then write. §3.10 decides whether the write
   even needs an encoder. */
/* D-88: the page of an archive with the workbench's edit, written into the
   archive — over the page, or beside it as `name_edit.ext`. The page is
   laid in a temporary file, the copy made from it into another exactly as
   a loose file's is, and that file's bytes then go into the archive. */
void panel_write_archive_page(app_state_t *app, bool over) {
    if (!on_archive_page(app)) return;
    int32_t page = current_page_index(app);
    if (app->arc_job) { osd_say(app, TU8("the archive is still being written")); return; }
    if (app->save_job) { osd_say(app, TU8("still saving the last copy")); return; }
    if (rubraview_arcedit_route(&app->source) == RUBRAVIEW_ARCEDIT_ENCRYPTED) {
        osd_say(app, cstr(rubraview_arcedit_result_text(RUBRAVIEW_ARCEDIT_REFUSED_ENCRYPTED)));
        return;
    }
    int32_t w = real_w(&app->pages[page]), h = real_h(&app->pages[page]);
    if (w <= 0 || h <= 0) { w = app->edit.image_width; h = app->edit.image_height; }
    if (w <= 0 || h <= 0) { osd_say(app, TU8("not saved: the page is not read yet")); return; }

    proven_u8str_view_t shown = rubraview_path_basename(page_display_name(app, (size_t)page));
    rubraview_export_options_t options = app->export_options;
    if (edit_panel_open(app) && app->edit.resize_dpi > 0.0) options.dpi = app->edit.resize_dpi;   /* RV-134: the DPI chosen in Edit */
    rubraview_export_format_t source_format = rubraview_export_format_for_name(shown);
    rubraview_export_format_t target = options.format == RUBRAVIEW_EXPORT_SAME_AS_SOURCE ? source_format : options.format;
    proven_u8str_view_t stem = rubraview_path_stem(shown), ext = rubraview_export_extension(target);
    proven_u8str_view_t source_ext = { .ptr = shown.ptr + stem.size, .size = shown.size - stem.size };   /* with its dot */
    /* Saved over in its own format the page keeps its name; in another, it takes that one's extension. */
    proven_u8str_view_t name = over ? (target == source_format ? PROVEN_LIT("")
                                                   : rubraview_batch_format_name(app->arena, PROVEN_LIT("{name}.{ext}"), stem, ext, 0, 0, PROVEN_LIT("")))
                        : rubraview_batch_format_name(app->arena, PROVEN_LIT("{name}_edit.{ext}"), stem, ext, 0, 0, PROVEN_LIT(""));

    save_job_t *job = (save_job_t*)calloc(1, sizeof(*job));
    if (!job) { osd_say(app, TU8("not saved: not enough memory")); return; }
    proven_u8str_view_t out_leaf = rubraview_batch_format_name(app->arena, PROVEN_LIT("out.{ext}"), stem, ext, 0, 0, PROVEN_LIT(""));
    bool ok = name.size < sizeof(job->arc_name) && app->source.archive_path.size < sizeof(job->arc_path) &&
              arc_temp_path(job->source, sizeof(job->source), "page", source_ext) &&
              arc_temp_path(job->out, sizeof(job->out), "edit", out_leaf);
    /* The page's bytes, read here: the archive is the main thread's to read. */
    if (ok) {
        page_job_stop(app);
        size_t budget = rubraview_page_source_read_budget(&app->source, (size_t)page);
        void *mem = budget > 0 ? malloc(budget) : NULL;
        ok = false;
        if (mem) {
            proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)mem, .size = budget });
            rubraview_page_bytes_t bytes = rubraview_page_source_read(&arena, &app->source, (size_t)page, MAX_PAGE_BYTES);
            ok = bytes.ok && bytes.data.size > 0 && rubraview_pal_fs_write_file(cstr(job->source), bytes.data);
            free(mem);
        }
    }
    if (!ok) { free(job); osd_say(app, TU8("not saved: the page could not be read")); return; }

    job->into_archive = true;
    job->arc_op = over ? RUBRAVIEW_ARCEDIT_REPLACE : RUBRAVIEW_ARCEDIT_ADD;
    job->arc_entry = app->source.pages[page].entry_index;
    memcpy(job->arc_path, app->source.archive_path.ptr, app->source.archive_path.size);
    if (name.size) memcpy(job->arc_name, name.ptr, name.size);
    job->edit = app->edit;
    job->edit.crop_active = false;   /* the selection is not a cut: the steps say what is cut (the owner's edit) */
    job->edit.resize_width = job->edit.resize_height = 0;
    job->steps = app->edit_steps;
    job->apply_edit = true;
    job->options = options;
    job->source_format = source_format;
    job->route = rubraview_export_plan(&options, source_format, true);
    job->width = w;
    job->height = h;
    job->wake = app->window;
    job->thread = CreateThread(NULL, 0, save_job_run, job, 0, NULL);
    if (!job->thread) {
        (void)rubraview_pal_fs_delete(cstr(job->source));
        free(job);
        osd_say(app, TU8("not saved: could not start"));
        return;
    }
    app->save_job = job;
    osd_say(app, TU8("making the edited page..."));
}

void panel_write_current(app_state_t *app, bool apply_edit) {
    int32_t page = current_page_index(app);
    if (page < 0) return;

    proven_u8str_view_t source = app->source.pages[page].path;
    if (source.size == 0) {
        /* D-88: an archive page's copy goes into the archive, beside the page. */
        if (apply_edit && on_archive_page(app)) panel_write_archive_page(app, false);
        return;
    }

    rubraview_export_options_t options = app->export_options;
    if (apply_edit && app->edit.resize_dpi > 0.0) options.dpi = app->edit.resize_dpi;   /* RV-134: the DPI chosen in Edit */
    rubraview_export_format_t source_format = rubraview_export_format_for_name(source);
    rubraview_edit_step_t pending_now = edit_pending(app);
    bool pixels_change = apply_edit && (rubraview_edit_step_does_something(&pending_now) || app->edit_steps.cursor > 0);

    proven_u8str_view_t stem = rubraview_path_stem(rubraview_path_basename(source));
    rubraview_export_format_t target = options.format == RUBRAVIEW_EXPORT_SAME_AS_SOURCE
                                         ? source_format : options.format;
    proven_u8str_view_t ext = rubraview_export_extension(target);
    proven_u8str_view_t name = rubraview_batch_format_name(app->arena, PROVEN_LIT("{name}_edit.{ext}"), stem, ext, 0, 0, PROVEN_LIT(""));
    proven_u8str_view_t out_path = rubraview_path_join(app->arena, rubraview_path_dirname(source), name);
    if (out_path.size == 0) return;

    rubraview_export_route_t route = rubraview_export_plan(&options, source_format, pixels_change);

    int32_t w = real_w(&app->pages[page]), h = real_h(&app->pages[page]);
    if (w <= 0 || h <= 0) { w = app->edit.image_width; h = app->edit.image_height; }
    if (route != RUBRAVIEW_EXPORT_STRIP_ONLY && route != RUBRAVIEW_EXPORT_COPY && route != RUBRAVIEW_EXPORT_JPEG_LOSSLESS && (w <= 0 || h <= 0)) {
        osd_say(app, TU8("not saved: the page is not read yet"));
        return;
    }
    if (app->save_job) { osd_say(app, TU8("still saving the last copy")); return; }
    if (source.size >= sizeof(app->save_job->source) || out_path.size >= sizeof(app->save_job->out)) return;

    /* The copy is made on a thread of its own (owner, 2026-09-28): a
       large picture took the window with it for as long as it took. */
    save_job_t *job = (save_job_t*)calloc(1, sizeof(*job));
    if (!job) { osd_say(app, TU8("not saved: not enough memory")); return; }
    memcpy(job->source, source.ptr, source.size);
    memcpy(job->out, out_path.ptr, out_path.size);
    job->edit = app->edit;
    job->edit.crop_active = false;   /* the selection is not a cut: the steps say what is cut (the owner's edit) */
    job->edit.resize_width = job->edit.resize_height = 0;
    if (apply_edit) job->steps = app->edit_steps;
    job->apply_edit = apply_edit;
    job->options = options;
    job->source_format = source_format;
    job->route = route;
    job->width = w;
    job->height = h;
    job->wake = app->window;
    job->thread = CreateThread(NULL, 0, save_job_run, job, 0, NULL);
    if (!job->thread) { free(job); osd_say(app, TU8("not saved: could not start")); return; }
    app->save_job = job;
    osd_say(app, TU8("saving a copy..."));
}

/* Each pass: a finished copy says how it went and lets go of its thread. */
void save_job_poll(app_state_t *app, bool wait) {
    save_job_t *job = app->save_job;
    if (!job) return;
    if (!wait && !InterlockedCompareExchange(&job->done, 0, 0)) return;
    WaitForSingleObject(job->thread, INFINITE);   /* a quit waits: a copy is never left half written */
    CloseHandle(job->thread);
    const char *const SAID[] = {
        T("saved a copy beside the page"), T("not saved: the picture could not be read"),
        T("not saved: the edit could not be applied"), T("not saved: the file could not be written"),
        T("not saved: not enough memory for this picture"),
    };
    if (job->into_archive) {
        /* D-88: the copy was made between two temporary files; its bytes go into the archive now. */
        void *mem = NULL;
        proven_u8str_view_t bytes = job->result == SAVE_OK ? save_read_whole(cstr(job->out), &mem) : PROVEN_LIT("");
        (void)rubraview_pal_fs_delete(cstr(job->source));
        (void)rubraview_pal_fs_delete(cstr(job->out));
        app->save_job = NULL;
        if (wait) {
            /* the reader is leaving: nothing is changed */
        } else if (job->result != SAVE_OK || bytes.size == 0) {
            osd_say(app, cstr(SAID[job->result != SAVE_OK ? job->result : SAVE_UNWRITABLE]));
        } else if (!on_archive_page(app) || !rubraview_path_same(cstr(job->arc_path), app->source.archive_path)) {
            osd_say(app, TU8("not saved: another book is open now"));
        } else {
            uint8_t *data = (uint8_t*)malloc(bytes.size);
            if (data) {
                memcpy(data, bytes.ptr, bytes.size);
                arc_edit_request(app, job->arc_op, job->arc_entry, job->arc_name[0] ? job->arc_name : NULL, data, bytes.size);
            } else {
                osd_say(app, cstr(SAID[SAVE_NO_MEMORY]));
            }
        }
        free(mem);
        free(job);
        return;
    }
    if (job->over_original && job->result == SAVE_OK) {
        /* The owner's edit: the written file takes the original's place; the original goes to the Recycle Bin. */
        bool moved = rubraview_pal_fs_recycle(cstr(job->source)) && rubraview_pal_fs_move(cstr(job->out), cstr(job->source));
        if (!moved) (void)rubraview_pal_fs_delete(cstr(job->out));
        if (!wait) {
            osd_say(app, moved ? TU8("saved over the original (the old file is in the Recycle Bin)")
                               : TU8("not saved: the original could not be replaced"));
            if (moved) {
                /* The page is read again as it is now, and the edit starts over on it. */
                int32_t page = current_page_index(app);
                if (page >= 0 && (size_t)page < page_count(app) && rubraview_path_same(app->source.pages[page].path, cstr(job->source))) {
                    if (edit_panel_open(app)) panel_close(app);
                    unload_all_pages(app);   /* read again from the file as it is now */
                    app->needs_relayout = true;
                }
            }
        }
        free(job);
        app->save_job = NULL;
        return;
    }
    if (job->over_original) (void)rubraview_pal_fs_delete(cstr(job->out));
    if (!wait && (job->result == SAVE_UNWRITABLE || job->result == SAVE_EDIT_FAILED) && job->why) {   /* RV-139; RFC-0007 */
        char line[256];
        snprintf(line, sizeof(line), T("not saved: %s"), rubraview_tr(job->why));
        osd_say(app, cstr(line));
    } else if (!wait && job->result >= 0 && job->result < (int)(sizeof(SAID) / sizeof(SAID[0]))) osd_say(app, cstr(SAID[job->result]));
    free(job);
    app->save_job = NULL;
}

/* The owner's edit: Save writes over the original, asked first by a second
   press within a few seconds; the copy is made beside it under another name
   in the original's own format, then takes its place. */
void edit_save_over(app_state_t *app) {
    static double asked_at = -100.0;
    double now = rubraview_pal_time_now_seconds();
    int32_t page = current_page_index(app);
    if (page < 0) return;
    proven_u8str_view_t source = app->source.pages[page].path;
    if (source.size == 0) return;
    rubraview_edit_step_t pending = edit_pending(app);
    if (app->edit_steps.cursor == 0 && !rubraview_edit_step_does_something(&pending)) { osd_say(app, TU8("nothing to save: no step was made")); return; }
    if (now - asked_at > 4.0) {
        asked_at = now;
        osd_say(app, TU8("press Save again to write over the original (the old file goes to the Recycle Bin)"));
        return;
    }
    asked_at = -100.0;
    if (app->save_job) { osd_say(app, TU8("still saving the last copy")); return; }
    rubraview_export_options_t options = app->export_options;
    options.format = RUBRAVIEW_EXPORT_SAME_AS_SOURCE;
    rubraview_export_format_t source_format = rubraview_export_format_for_name(source);
    proven_u8str_view_t stem = rubraview_path_stem(rubraview_path_basename(source));
    proven_u8str_view_t ext = rubraview_export_extension(source_format);
    proven_u8str_view_t name = rubraview_batch_format_name(app->arena, PROVEN_LIT("{name}.rvsaving.{ext}"), stem, ext, 0, 0, PROVEN_LIT(""));
    proven_u8str_view_t out_path = rubraview_path_join(app->arena, rubraview_path_dirname(source), name);
    int32_t w = real_w(&app->pages[page]), h = real_h(&app->pages[page]);
    if (out_path.size == 0 || w <= 0 || h <= 0) { osd_say(app, TU8("not saved: the page is not read yet")); return; }
    save_job_t *job = (save_job_t*)calloc(1, sizeof(*job));
    if (!job || source.size >= sizeof(job->source) || out_path.size >= sizeof(job->out)) { free(job); osd_say(app, TU8("not saved: not enough memory")); return; }
    memcpy(job->source, source.ptr, source.size);
    memcpy(job->out, out_path.ptr, out_path.size);
    job->edit = app->edit;
    job->edit.crop_active = false;   /* the selection is not a cut: the steps say what is cut (the owner's edit) */
    job->edit.resize_width = job->edit.resize_height = 0;
    job->steps = app->edit_steps;
    job->apply_edit = true;
    job->over_original = true;
    job->options = options;
    job->source_format = source_format;
    job->route = rubraview_export_plan(&options, source_format, true);
    job->width = w;
    job->height = h;
    job->wake = app->window;
    job->thread = CreateThread(NULL, 0, save_job_run, job, 0, NULL);
    if (!job->thread) { free(job); osd_say(app, TU8("not saved: could not start")); return; }
    app->save_job = job;
    osd_say(app, TU8("saving over the original..."));
}
